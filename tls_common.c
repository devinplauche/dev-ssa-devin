#include <event2/bufferevent.h>
#include <errno.h>
#include <string.h>

#include <openssl/err.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>

#include "netlink.h"
#include "tls_common.h"
#include "log.h"

#define MAX_BUFFER	1024*1024*10

void associate_fd(connection* conn, evutil_socket_t ifd) {
	bufferevent_setfd(conn->plain.bev, ifd);
	bufferevent_enable(conn->plain.bev, EV_READ | EV_WRITE);

	log_printf(LOG_INFO, "plain bev enabled\n");
	return;
}


void tls_bev_write_cb(struct bufferevent *bev, void *arg) {
	//log_printf(LOG_DEBUG, "write event on bev %p\n", bev);
	connection* ctx = arg;
	channel* endpoint = (bev == ctx->secure.bev) ? &ctx->plain : &ctx->secure;
	struct evbuffer* out_buf;

	if (endpoint->closed == 1) {
		out_buf = bufferevent_get_output(bev);
		if (evbuffer_get_length(out_buf) == 0) {
			//bufferevent_free(bev);
			//shutdown_tls_conn_ctx(ctx);
		}
		return;
	}

	if (endpoint->bev && !(bufferevent_get_enabled(endpoint->bev) & EV_READ)) {
		bufferevent_setwatermark(bev, EV_WRITE, 0, 0);
		bufferevent_enable(endpoint->bev, EV_READ);
	}
	return;
}

void tls_bev_read_cb(struct bufferevent *bev, void *arg) {
	//log_printf(LOG_DEBUG, "read event on bev %p\n", bev);
	connection* ctx = arg;
	channel* endpoint = (bev == ctx->secure.bev) ? &ctx->plain : &ctx->secure;
	struct evbuffer* in_buf;
	struct evbuffer* out_buf;
	size_t in_len;

	in_buf = bufferevent_get_input(bev);
	in_len = evbuffer_get_length(in_buf);
	
	if (endpoint->closed == 1) {
		evbuffer_drain(in_buf, in_len);
		return;
	}

	if (in_len == 0) {
		return;
	}

	out_buf = bufferevent_get_output(endpoint->bev);
	evbuffer_add_buffer(out_buf, in_buf);

	if (evbuffer_get_length(out_buf) >= MAX_BUFFER) {
		log_printf(LOG_DEBUG, "Overflowing buffer, slowing down\n");
		bufferevent_setwatermark(endpoint->bev, EV_WRITE, MAX_BUFFER / 2, MAX_BUFFER);
		bufferevent_disable(bev, EV_READ);
	}
	return;
}

void tls_bev_event_cb(struct bufferevent *bev, short events, void *arg) {
	connection* ctx = arg;
	unsigned long ssl_err;
	channel* endpoint = (bev == ctx->secure.bev) ? &ctx->plain : &ctx->secure;
	channel* startpoint = (bev == ctx->secure.bev) ? &ctx->secure : &ctx->plain;
	if (events & BEV_EVENT_CONNECTED) {
		log_printf(LOG_DEBUG, "%s endpoint connected\n", bev == ctx->secure.bev ? "encrypted" : "plaintext");
		//startpoint->connected = 1;
		if (bev == ctx->secure.bev) {
			//log_printf(LOG_INFO, "Is handshake finished?: %d\n", SSL_is_init_finished(ctx->tls));
			log_printf(LOG_INFO, "Negotiated connection with %s\n", SSL_get_version(ctx->tls));
			if (bufferevent_getfd(ctx->plain.bev) == -1) {
				netlink_handshake_notify_kernel(ctx->daemon, ctx->id, 0);
			}
			else {
				bufferevent_enable(ctx->plain.bev, EV_READ | EV_WRITE);
				bufferevent_socket_connect(ctx->plain.bev, ctx->addr, ctx->addrlen);
			}
		}
	}
	if (events & BEV_EVENT_ERROR) {
		//log_printf(LOG_DEBUG, "%s endpoint encountered an error\n", bev == ctx->secure.bev ? "encrypted" : "plaintext");
		if (errno) {
			if (errno == ECONNRESET || errno == EPIPE) {
				log_printf(LOG_INFO, "Connection closed\n");
			}
			else {
				log_printf(LOG_INFO, "An unhandled error has occurred\n");
			}
			startpoint->closed = 1;
		}
		if (bev == ctx->secure.bev) {
			while ((ssl_err = bufferevent_get_openssl_error(bev))) {
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
	}
	if (events & BEV_EVENT_EOF) {
		log_printf(LOG_DEBUG, "%s endpoint got EOF\n", bev == ctx->secure.bev ? "encrypted" : "plaintext");
		if (bufferevent_getfd(endpoint->bev) == -1) {
			endpoint->closed = 1;
		}
		else if (endpoint->closed == 0) {
			if (evbuffer_get_length(bufferevent_get_input(startpoint->bev)) > 0) {
				tls_bev_read_cb(endpoint->bev, ctx);
			}
			if (evbuffer_get_length(bufferevent_get_output(endpoint->bev)) == 0) {
				endpoint->closed = 1;
			}
		}
		startpoint->closed = 1;
	}
	/* If both channels are closed now, free everything */
	if (endpoint->closed == 1 && startpoint->closed == 1) {
		if (bufferevent_getfd(ctx->plain.bev) == -1) {
			netlink_handshake_notify_kernel(ctx->daemon, ctx->id, -EHOSTUNREACH);
		}
		/* TODO: this function never actually did anything. Change this??? */
		/* shutdown_tls_conn_ctx(ctx); */
	}
	return;
}




int get_peer_certificate(connection* conn_ctx, char** data, unsigned int* len) {
	X509* cert;
	BIO* bio;
	char* bio_data;
	char* pem_data;
	unsigned int cert_len;

	if (conn_ctx->tls == NULL) {
		return 0;
	}
	cert = SSL_get_peer_certificate(conn_ctx->tls);
	if (cert == NULL) {
		return 0;
	}
	bio = BIO_new(BIO_s_mem());
	if (bio == NULL) {
		X509_free(cert);
		return 0;
	}
	if (PEM_write_bio_X509(bio, cert) == 0) {
		X509_free(cert);
		BIO_free(bio);
		return 0;
	}

	cert_len = BIO_get_mem_data(bio, &bio_data);
	pem_data = malloc(cert_len + 1); /* +1 for null terminator */
	if (pem_data == NULL) {
		X509_free(cert);
		BIO_free(bio);
		return 0;
	}

	memcpy(pem_data, bio_data, cert_len);
	pem_data[cert_len] = '\0';
	X509_free(cert);
	BIO_free(bio);

	*data = pem_data;
	*len = cert_len;
	return 1;
}

int get_peer_identity(connection* conn_ctx, char** data, unsigned int* len) {
	X509* cert;
	X509_NAME* subject_name;
	char* identity;
	if (conn_ctx->tls == NULL) {
		return 0;
	}
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

