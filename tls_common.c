#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <openssl/err.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>

#include "netlink.h"
#include "tls_common.h"
#include "tls_client.h"
#include "tls_server.h"
#include "log.h"

#define MAX_BUFFER	1024*1024*10 /* 10 Megabits */

int clear_from_cipherlist(char* cipher, STACK_OF(SSL_CIPHER)* cipherlist);
int get_ciphers_strlen(STACK_OF(SSL_CIPHER)* ciphers);
int get_ciphers_string(STACK_OF(SSL_CIPHER)* ciphers, char* buf, int buf_len);

int handle_event_connected(connection* conn, unsigned long id, 
		daemon_context* daemon, channel* startpoint, channel* endpoint);
int handle_event_error(connection* conn, 
		int bev_error, channel* startpoint, channel* endpoint);
int handle_event_eof(connection* conn, channel* startpoint, channel* endpoint);

/*
 *******************************************************************************
 *                       BUFFEREVENT CALLBACK FUNCTIONS
 *******************************************************************************
 */

/**
 * Bufferevents automatically read data in from their fd to their read buffer,
 * as well as reading data out from their write buffer to their fd. All that
 * these callbacks to is notify you when these read/write operations have been
 * triggered. Since we don't modify the watermarks of the read_cb, it is 
 * triggered every time new information is read in from a file descriptor, and
 * never stops reading. 
 * This write_cb has functionality that works in tandem with the read callback;
 * when too much data has been put into the write buffer (out_buf >= MAX_BUFFER)
 * the read_cb temporarily disables itself from reading in new data and sets
 * the other bufferevent's writing watermarks so that it will not trigger
 * a write callback until half of that data has been written out. Once that
 * happens, the write_cb re-enables the other bufferevent's reading capabilities
 * and resets its own writing watermarks to 0, so that its write_cb will not be
 * triggered until no data is left to be written.
 * Note that this function is not called every time a write operation occurs, 
 * and it certainly does not cause data to be written from the write buffer to
 * the fd. It merely reports once all data to be written from a buffer has been
 * written.
 *
 */
void tls_bev_write_cb(struct bufferevent *bev, void *arg) {

	connection* conn = ((sock_context*)arg)->conn;
	channel* endpoint = (bev == conn->secure.bev) ? &conn->plain : &conn->secure;
	
	log_printf(LOG_DEBUG, "write event on bev %p (%s)\n", bev, 
			(bev == conn->secure.bev) ? "secure" : "plain");

	if (endpoint->bev && !(bufferevent_get_enabled(endpoint->bev) & EV_READ)) {
		bufferevent_setwatermark(bev, EV_WRITE, 0, 0);
		bufferevent_enable(endpoint->bev, EV_READ);
	}
	return;
}

/**
 * Every time data is read into a bufferevent from its associated fd, this
 * function will be called. It takes that data and writes it to the write buffer
 * of the other bufferevent. If too much data is being fed through, the read 
 * operation of this bufferevent will be turned off until the other buffer has
 * written enough data out.
 */
void tls_bev_read_cb(struct bufferevent* bev, void* arg) {
	/* TODO: set read high-water mark?? */
	
	connection* conn = ((sock_context*)arg)->conn;
	channel* endpoint = (bev == conn->secure.bev) 
			? &conn->plain : &conn->secure;
	struct evbuffer* in_buf;
	struct evbuffer* out_buf;
	size_t in_len;

	log_printf(LOG_DEBUG, "read event on bev %p (%s)\n", bev, 
			(bev == conn->secure.bev) ? "secure" : "plain");

	in_buf = bufferevent_get_input(bev);
	in_len = evbuffer_get_length(in_buf);
	if (in_len == 0)
		return;

	/* clear read buffer if already closed */
	if (endpoint->closed == 1) {
		log_printf(LOG_DEBUG, "drained buffer.\n");
		evbuffer_drain(in_buf, in_len);
		return;
	}

	/* copy content to the output buffer of the other bufferevent */
	out_buf = bufferevent_get_output(endpoint->bev);
	evbuffer_add_buffer(out_buf, in_buf);

	if (evbuffer_get_length(out_buf) >= MAX_BUFFER) {
		log_printf(LOG_DEBUG, "Overflowing buffer, slowing down\n");
		bufferevent_setwatermark(endpoint->bev, 
				EV_WRITE, MAX_BUFFER / 2, MAX_BUFFER);
		bufferevent_disable(bev, EV_READ);
	}
	return;
}

/* FEATURE: Maybe split client and server events to be more readable?? */
void tls_bev_event_cb(struct bufferevent *bev, short events, void *arg) {
	
	log_printf(LOG_DEBUG, "Made it into bev_event_cb\n");

	sock_context* sock_ctx = (sock_context*) arg;
	daemon_context* daemon = sock_ctx->daemon;
	connection* conn = sock_ctx->conn;
	unsigned long id = sock_ctx->id;
	int ret = 0, bev_error = errno;

	channel* endpoint = (bev == conn->secure.bev) 
			? &conn->plain : &conn->secure;
	channel* startpoint = (bev == conn->secure.bev) 
			? &conn->secure : &conn->plain;

	if (events & BEV_EVENT_CONNECTED) {
		/* debugging...
		log_printf(LOG_DEBUG, "Int addr:\n");
		log_printf_addr(&sock_ctx->int_addr);
		log_printf(LOG_DEBUG, "Rem addr:\n");
		log_printf_addr(&sock_ctx->rem_addr);
		*/
		ret = handle_event_connected(conn, id, daemon, startpoint, endpoint);
		if (ret != 0) 
			goto err;
	}
	if (events & BEV_EVENT_ERROR) {
		ret = handle_event_error(conn, bev_error, startpoint, endpoint);
		if (ret != 0)
			goto err;
	}
	if (events & BEV_EVENT_EOF) {
		ret = handle_event_eof(conn, startpoint, endpoint);
		if (ret != 0)
			goto err;
	}

	/* If both channels are closed, free everything & reset sock_ctx */
	if (endpoint->closed == 1 && startpoint->closed == 1) {
		if (bufferevent_getfd(conn->plain.bev) == NOT_CONN_BEV)
			netlink_handshake_notify_kernel(daemon, id, -ECONNABORTED);
		
		connection_shutdown(conn);
	}
	return;
  err:
	/* Errors here are unrecoverable and require full connection termination */
	
	log_printf(LOG_ERROR, "An unrecoverable error occurred.\n");
	return;
}

/*
 *******************************************************************************
 *                   BUFFEREVENT CALLBACK HELPER FUNCTIONS
 *******************************************************************************
 */

/**
 * Handles the case where a given channel's bufferevent connected successfully.
 * This function is called when a client's secure channel connects, or when a
 * server's secure OR plain channel connects. However, it is not called when a 
 * client's plain channel is connected; when accept_cb is called the file 
 * descriptor passed in is already connected, so when it is associated with the
 * plain channel bufferevent (in associate_fd()) it will not trigger 
 * connected event.
 * @param startpoint The channel that triggered the bufferevent.
 * @param endpoint The other channel associated with conn (for instance, if the
 * secure channel triggered this event then the endpoint would be the plain 
 * channel, and vice versa).
 * @returns 0 on success, or -errno if an error occurred.
 */
int handle_event_connected(connection* conn, unsigned long id, 
		daemon_context* daemon, channel* startpoint, channel* endpoint) {

	log_printf(LOG_DEBUG, "%s endpoint connected\n",
			startpoint->bev == conn->secure.bev ? "encrypted" : "plaintext");

	if (startpoint->bev == conn->secure.bev) {
		log_printf(LOG_INFO, "Negotiated connection with %s\n", 
				SSL_get_version(conn->tls));

		if (bufferevent_getfd(conn->plain.bev) == NOT_CONN_BEV) {
			netlink_handshake_notify_kernel(daemon, id, 0);
		} else {
			log_printf(LOG_DEBUG, "Now negotiating internal connection.\n");
			log_printf_addr(conn->addr);

			bufferevent_enable(conn->plain.bev, EV_READ | EV_WRITE);
			bufferevent_socket_connect(conn->plain.bev, conn->addr, conn->addrlen);
		}
	}

	return 0;
}

int handle_event_error(connection* conn, 
		int bev_error, channel* startpoint, channel* endpoint) {

	unsigned long ssl_err;

	log_printf(LOG_DEBUG, "%s endpoint encountered an error\n", 
				startpoint->bev == conn->secure.bev 
				? "encrypted" : "plaintext");

	if (bev_error) {
		if (bev_error == ECONNRESET || bev_error == EPIPE) {
			log_printf(LOG_INFO, "Connection closed\n");
		} else {
			log_printf(LOG_INFO, "An unhandled error has occurred\n");
		}
		startpoint->closed = 1;
	}
	if (startpoint->bev == conn->secure.bev) {
		while ((ssl_err = bufferevent_get_openssl_error(startpoint->bev))) {
			log_printf(LOG_ERROR, "SSL error from bufferevent: %s [%s]\n",
					ERR_func_error_string(ssl_err),
					ERR_reason_error_string(ssl_err));
		}
	}
	if (endpoint->closed == 0) {
		struct evbuffer* out_buf;
		out_buf = bufferevent_get_output(endpoint->bev);
		/* close other buffer if we're closing and it has no data left */
		if (evbuffer_get_length(out_buf) == 0) {
			endpoint->closed = 1;
		}
		startpoint->closed = 1;
	}

	return 0;
}

int handle_event_eof(connection* conn, channel* startpoint, channel* endpoint) {
	log_printf(LOG_DEBUG, "%s endpoint got EOF\n", 
				startpoint->bev == conn->secure.bev ? "encrypted" : "plaintext");
	if (bufferevent_getfd(endpoint->bev) == -1) {
		endpoint->closed = 1;
	}
	else if (endpoint->closed == 0) {
		log_printf(LOG_DEBUG, "Other endpoint not yet closed.\n");
		if (evbuffer_get_length(bufferevent_get_input(startpoint->bev)) > 0) {
			log_printf(LOG_DEBUG, "Startpoint buffer size greater than 0.\n");
			tls_bev_read_cb(endpoint->bev, conn);
		}
		if (evbuffer_get_length(bufferevent_get_output(endpoint->bev)) == 0) {
			log_printf(LOG_DEBUG, "Startpoint buffer now is 0 size.\n");
			endpoint->closed = 1;
			/*
			bufferevent_free(endpoint->bev);
			endpoint->bev = NULL;
			*/
		}
	}
	startpoint->closed = 1;
	conn->state = CLIENT_DISCONNECTED; /* TODO: does this go here? */
	/*
	bufferevent_free(startpoint->bev);
	startpoint->bev = NULL;
	*/
	return 0;
}


/*
 *******************************************************************************
 *                            GETSOCKOPT FUNCTIONS 
 *******************************************************************************
 */

/**
 * Retrieves the peer's certificate (if such exists) and allocates data to a
 * PEM-formatted string representing that certificate.
 * @param conn The connection context to retrieve a peer certificate from.
 * @param data A memory address for the certificate string to be allocated to.
 * @param len The string length of the certificate.
 * @returns 0 on success; -errno otherwise.
 */
int get_peer_certificate(connection* conn, char** data, unsigned int* len) {
	X509* cert = NULL;
	BIO* bio = NULL;
	char* bio_data = NULL;
	char* pem_data = NULL;
	unsigned int cert_len;
	int ret = 0;

	assert(conn);
	assert(conn->tls);

	cert = SSL_get_peer_certificate(conn->tls);
	if (cert == NULL) {
		/* TODO: get specific error from OpenSSL */
		ret = -ENOTCONN;
		goto end;
	}

	bio = BIO_new(BIO_s_mem());
	if (bio == NULL) {
		/* TODO: get specific error from OpenSSL */
		ret = -ENOMEM;
		goto end;
	}
		
	if (PEM_write_bio_X509(bio, cert) == 0) {
		/* TODO: get specific error from OpenSSL */
		ret = -ENOTSUP;
		goto end;
	}
		
	cert_len = BIO_get_mem_data(bio, &bio_data);
	pem_data = malloc(cert_len + 1); /* +1 for null terminator */
	if (pem_data == NULL) {
		ret = -errno;
		goto end;
	}

	memcpy(pem_data, bio_data, cert_len);
	pem_data[cert_len] = '\0';

	ret = 0;
	*data = pem_data;
	*len = cert_len + 1;
 end:
	X509_free(cert);
	BIO_free(bio);
	return ret;
}

int get_peer_identity(connection* conn_ctx, char** data, unsigned int* len) {
	X509* cert;
	X509_NAME* subject_name;
	char* identity;
	if (conn_ctx->tls == NULL)
		return 0;
	cert = SSL_get_peer_certificate(conn_ctx->tls);
	if (cert == NULL) {
		log_printf(LOG_INFO, "peer cert is NULL\n");
		return 0;
	}
	subject_name = X509_get_subject_name(cert);
	identity = X509_NAME_oneline(subject_name, NULL, 0);
	*data = identity;
	*len = strlen(identity)+1;
	return 1;
}

int get_hostname(connection* conn_ctx, char** data, unsigned int* len) {
	const char* hostname;
	if (conn_ctx == NULL) {
		return 0;
	}
	hostname = SSL_get_servername(conn_ctx->tls, TLSEXT_NAMETYPE_host_name);
	*data = (char*)hostname;
	if (hostname == NULL) {
		*len = 0;
		return 1;
	}
	*len = strlen(hostname)+1;
	return 1;
}

/**
 * Allocates a string list of enabled ciphers to data.
 * @param conn The specified connection context to retrieve the ciphers from
 * @param data A pointer to a char pointer where the cipherlist string will be
 * allocated to, or NULL if no ciphers were available from the given connection.
 * This should be freed after use.
 * @returns 0 on success; -errno otherwise.
 */
int get_enabled_ciphers(connection* conn, char** data, unsigned int* len) {
	char* ciphers_str = "";
	
	assert(conn);
	assert(conn->tls);

	STACK_OF(SSL_CIPHER)* ciphers = SSL_get_ciphers(conn->tls);
	/* TODO: replace this with SSL_get1_supported_ciphers? Maybe... */
	if (ciphers == NULL)
		goto end; /* no ciphers available; just return NULL. */

	int ciphers_len = get_ciphers_strlen(ciphers);
	if (ciphers_len == 0)
		goto end;

	ciphers_str = (char*) malloc(ciphers_len + 1);
	if (ciphers_str == NULL)
		return -errno;

	if (get_ciphers_string(ciphers, ciphers_str, ciphers_len + 1) != 0) {
		log_printf(LOG_ERROR, "Buffer wasn't big enough; had to be truncated.\n");
	}

	*len = ciphers_len + 1;
 end:
	log_printf(LOG_DEBUG, "Trusted ciphers:\n%s\n", ciphers_str);
	log_printf(LOG_DEBUG, "Cipher length: %i\n", *len);
	*data = ciphers_str;
	return 0;
}

/*
 *******************************************************************************
 *                           SETSOCKOPT FUNCTIONS
 *******************************************************************************
 */

int set_connection_type(connection* conn, daemon_context* daemon, int type) {
	
	int ret = 0;
	
	switch(conn->state) {
	case CLIENT_NEW:
	case SERVER_NEW:
		if (type == CLIENT_CONN)
			ret = client_SSL_new(conn, daemon);
		else /* type == SERVER_CONN */
			ret = server_SSL_new(conn, daemon);
		
		if (ret == 0)
			conn->state = (type == CLIENT_CONN) ? CLIENT_NEW : SERVER_NEW;
		break;
	default:
		ret = -ENOPROTOOPT;
		break;
	}

	return ret;
}

/* TODO: Test this */
int set_trusted_peer_certificates(connection* conn, char* value) {
	/* XXX update this to take in-memory PEM chains as well as file names */
	/* ^ old comment, maybe still do? */

	if (conn == NULL)
		return 0;

	STACK_OF(X509_NAME)* cert_names = SSL_load_client_CA_file(value);
	if (cert_names == NULL)
		return 0;

	SSL_set_client_CA_list(conn->tls, cert_names);
	return 1;
}

/**
 * Removes a given cipher from the set of enabled ciphers for a connection.
 * TODO: Allow multiple ciphers to be disabled at the same time?
 * @param conn The connection context to remove a cipher from.
 * @param cipher A string representation of the cipher to be removed.
 * @returns 0 on success; -errno otherwise. EINVAL means the cipher to be
 * removed was not found.
 */
int disable_cipher(connection* conn, char* cipher) {

	assert(conn);
	assert(conn->tls);
	assert(cipher);

	STACK_OF(SSL_CIPHER)* cipherlist = SSL_get_ciphers(conn->tls);
	if (cipherlist == NULL)
		return -EINVAL;

	int ret = clear_from_cipherlist(cipher, cipherlist);
	if (ret != 0)
		return -EINVAL;
	
	return 0;
}

/*
 *-----------------------------------------------------------------------------
 *                             HELPER FUNCTIONS
 *----------------------------------------------------------------------------- 
 */

/**
 * Converts a stack of SSL_CIPHER objects into a single string representation
 * of all the ciphers, with each individual cipher separated by a ':'.
 * @param ciphers The stack of ciphers to convert
 * @param buf the provided buffer to put the string into.
 * @param buf_len The length of the provided buffer.
 * @returns 0 on success; -1 if the buffer was not big enough to store all of
 * the ciphers and had to be truncated.
 */
int get_ciphers_string(STACK_OF(SSL_CIPHER)* ciphers, char* buf, int buf_len) {
	int index = 0;
	for (int i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
		const SSL_CIPHER* curr = sk_SSL_CIPHER_value(ciphers, i);
		const char* cipher = SSL_CIPHER_get_name(curr);
		
		if ((index + strlen(cipher) + 1) > buf_len) {
			buf[index-1] = '\0';
			return -1; /* buf not big enough */
		}
		
		strcpy(&buf[index], cipher);
		index += strlen(cipher);
		buf[index] = ':';
		index += 1;
	}
	buf[index - 1] = '\0'; /* change last ':' to '\0' */
	return 0;
}

/**
 * Determines the combined string length of all the cipher strings.
 * @param ciphers The cipher list to measure string lengths from.
 * @returns The combined string length of the ciphers in the list (as if 
 * there were ':' characters between each cipher and a terminating
 * '\0' at the end). Never returns an error code.
 */
int get_ciphers_strlen(STACK_OF(SSL_CIPHER)* ciphers) {
	int len = 0;
	for (int i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
		const char* curr = SSL_CIPHER_get_name(sk_SSL_CIPHER_value(ciphers, i));
		len += strlen(curr) + 1; /* add ':' */
	}
	if (len != 0)
		len -= 1; /* removes the last ':' */
	return len;
}

/**
 * Iterates through the stack of ciphers and clears out ones matching
 * the given cipher name. Returns the updated cumulative length of the ciphers.
 * @param cipher The string name of the cipher to be cleared from the list.
 * @param cipherlist The stack of ciphers to be modified.
 * @returns 0 on success, or -1 if the cipher was not found.
 */
int clear_from_cipherlist(char* cipher, STACK_OF(SSL_CIPHER)* cipherlist) {
	int i = 0, has_cipher = 0;

	while (i < sk_SSL_CIPHER_num(cipherlist)) {
		const SSL_CIPHER* curr_cipher = sk_SSL_CIPHER_value(cipherlist, i);
		const char* name = SSL_CIPHER_get_name(curr_cipher);
		if (strcmp(name, cipher) == 0) {
			has_cipher = 1;
			sk_SSL_CIPHER_delete(cipherlist, i);
		} else {
			i++;
		}
	}
	/* assert: all ciphers to remove now removed */

	if (has_cipher)
		return 0;
	else
		return -1;
}
