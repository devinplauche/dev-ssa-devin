/*
 * TLS Wrapping Daemon - transparent TLS wrapping of plaintext connections
 * Copyright (C) 2017, Mark O'Neill <mark@markoneill.name>
 * All rights reserved.
 * https://owntrust.org
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

#include "tls_wrapper.h"
#include "tb_connector.h"
#include "openssl_compat.h"
#include "issue_cert.h"
#include "auth_daemon.h"
#include "log.h"
#include "config.h"
#include "netlink.h"
#include "daemon.h"

#include "tls_client.h"
#include "tls_common.h"

#define IPPROTO_TLS 	(715 % 255)




#ifdef CLIENT_AUTH
typedef struct auth_info {
	int fd;
	char* hostname;
	char* ca_name;
} auth_info_t;

typedef struct s_auth_info {
	unsigned long id;
	daemon_context* daemon;
} s_auth_info_t;

extern int auth_info_index;
char auth_daemon_name[] = "\0auth_req";
#define CLIENT_AUTH_KEY "test_files/openssl_mod_tests/client_key.key"
#define CLIENT_AUTH_CERT "test_files/openssl_mod_tests/client_pub.pem"
int client_auth_callback(SSL *s, void* hdata, size_t hdata_len, int hash_nid, int sigalg_nid, unsigned char** o_sig, size_t* o_siglen);
int client_cert_callback(SSL *s, X509** cert, EVP_PKEY** key);
void send_cert_request(int fd, char* hostname, char* ca_name);
int recv_cert_response(int fd, X509** o_cert);
void send_sign_request(int fd, void* hdata, size_t hdata_len, int hash_nid, int sigalg_nid);
int recv_sign_response(int fd, unsigned char** o_sig, size_t* o_siglen);
void send_all(int fd, char* msg, int bytes_to_send);
int auth_daemon_connect(void);
#endif




#ifdef CLIENT_AUTH
void pha_cb(const SSL* tls, int where, int ret) {
	s_auth_info_t* ai;
	/*printf("pha_cb invoked!1111111111 and where is %08X\n", where);*/
	if (where == 0x00002002) {
		ai = SSL_get_ex_data(tls, auth_info_index);
		SSL_set_info_callback((SSL*)tls, NULL);
		netlink_notify_kernel(ai->daemon, ai->id, 0);
		free(ai);
	}
	/*if (where & SSL_ST_CONNECT) {
		printf("ssl want is %08X\n", SSL_want(tls));
		//SSL_read(tls, NULL, 0);
	}*/
	return;
}
#endif




#ifdef CLIENT_AUTH
int client_auth_callback(SSL *tls, void* hdata, size_t hdata_len, int hash_nid, int sigalg_nid, unsigned char** o_sig, size_t* o_siglen) {
	auth_info_t* ai;

	log_printf(LOG_INFO, "Sigalg ID is %d\n", sigalg_nid);
	log_printf(LOG_INFO, "hash ID is %d\n", hash_nid);

        /*EVP_PKEY* pkey = NULL;
        const EVP_MD *md = NULL;
        EVP_MD_CTX *mctx = NULL;
        EVP_PKEY_CTX *pctx = NULL;
        size_t siglen;
        unsigned char* sig;*/

	ai = SSL_get_ex_data(tls, auth_info_index);
	send_sign_request(ai->fd, hdata, hdata_len, hash_nid, sigalg_nid);
	if (recv_sign_response(ai->fd, o_sig, o_siglen) == 0) {
		log_printf(LOG_ERROR, "Could not receive signature response\n");
		close(ai->fd);
		//free(ai);
		return 1;
	}
	log_printf(LOG_INFO, "Got a signature, closing fd %d\n", ai->fd);
	close(ai->fd);
	//free(ai);

        /*printf("Signing hash\n");
        //pkey = get_private_key_from_file(CLIENT_AUTH_KEY);
	pkey = get_private_key_from_buf(char* buffer);
        if (pkey == NULL) {
                return 0;
        }
        mctx = EVP_MD_CTX_new();
        if (mctx == NULL) {
                EVP_PKEY_free(pkey);
                return 0;
        }

        siglen = EVP_PKEY_size(pkey);
        sig = (unsigned char*)malloc(siglen);
        if (sig == NULL) {
                EVP_PKEY_free(pkey);
                EVP_MD_CTX_free(mctx);
                return 0;
        }
        
        md = EVP_get_digestbynid(sigalg_nid);
        if (md == NULL) {
                EVP_PKEY_free(pkey);
                EVP_MD_CTX_free(mctx);
                free(sig);
                return 0;
        }

        if (EVP_DigestSignInit(mctx, &pctx, md, NULL, pkey) <= 0) {
                EVP_PKEY_free(pkey);
                EVP_MD_CTX_free(mctx);
                free(sig);
                return 0;
        }

        if (EVP_DigestSign(mctx, sig, &siglen, hdata, hdata_len) <= 0) {
                EVP_PKEY_free(pkey);
                EVP_MD_CTX_free(mctx);
                free(sig);
                return 0;
        }

        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(mctx);*/
        
	/*
        *o_sig = sig;
        *o_siglen = siglen; */
        /* sig is freed by caller */
        return 1;
}

int client_cert_callback(SSL *tls, X509** cert, EVP_PKEY** key) {
	int i;
	char *ca_name;
	char name_buf[1024];
	X509_NAME* name;
	STACK_OF(X509_NAME)* names;
	auth_info_t* ai;
	int fd;
	//*cert = get_cert_from_file(CLIENT_AUTH_CERT);
	ai = SSL_get_ex_data(tls, auth_info_index);
	/* XXX improve this later to not block. This
	 * blocking POC is...well, just for POC */
	log_printf(LOG_INFO, "Client cert callback is invoked\n");

	fd = auth_daemon_connect();
	log_printf(LOG_INFO, "fd to auth daemon is %d\n", fd);
	if (fd == -1) {
		log_printf(LOG_ERROR, "Failed to connect to auth daemon\n");
		return 0;
	}
	ai->fd = fd;
	names = SSL_get_client_CA_list(tls);
	if (names == NULL) {
		send_cert_request(ai->fd, ai->hostname, NULL);
	}
	else {
		ca_name = calloc(256,1);
		for (i = 0; i < sk_X509_NAME_num(names); i++) {
			name = sk_X509_NAME_value(names, i);
			X509_NAME_oneline(name, name_buf, 1024);
			X509_NAME_get_text_by_NID(name,NID_commonName,ca_name,256);
			
			printf("Name is %s\n", name_buf);
		}
		ai->ca_name = ca_name;
		printf("%s\n",ai->ca_name);
		send_cert_request(ai->fd, ai->hostname,ai->ca_name);
	}
	if (recv_cert_response(ai->fd, cert) == 0) {
		log_printf(LOG_ERROR, "It appears the client does not want to authenticate\n");
		*cert = NULL;
		*key  = NULL;
		close(ai->fd);
		//free(ai);
		return 0;
	}
	*key = NULL;
	//*key = get_private_key_from_file(CLIENT_KEY);
	SSL_set_client_auth_cb(tls, client_auth_callback);
	return 1;
}

int auth_daemon_connect(void) {
	int fd;
	struct sockaddr_un addr;
	int addr_len;
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		log_printf(LOG_ERROR, "socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, auth_daemon_name, sizeof(auth_daemon_name));
	addr_len = sizeof(auth_daemon_name) + sizeof(sa_family_t);

	if (connect(fd, (struct sockaddr*)&addr, addr_len) == -1) {
		log_printf(LOG_ERROR, "connect: %s\n", strerror(errno));
		return -1;
	}
	return fd;
}

void send_cert_request(int fd, char* hostname, char* ca_name) {
	int msg_size;
	char msg_type;
	int hostname_len;
	int ca_name_len;
	if (ca_name == NULL) {
		ca_name = hostname;
	}
	hostname_len = strlen(hostname)+1;
	ca_name_len = strlen(ca_name)+1;
	msg_size = htonl(hostname_len+ca_name_len);
	msg_type = CERTIFICATE_REQUEST;
	send_all(fd, &msg_type, 1);
	send_all(fd, (char*)&msg_size, sizeof(uint32_t));
	send_all(fd, hostname, hostname_len);
	send_all(fd, ca_name, ca_name_len);
	log_printf(LOG_DEBUG, "Sent a cert request of length %u\n", hostname_len);
	return;
}

void send_sign_request(int fd, void* hdata, size_t hdata_len, int hash_nid, int sigalg_nid) {
	int msg_size;
	char msg_type;
	msg_size = htonl(hdata_len + sizeof(hash_nid) + sizeof(sigalg_nid));
	msg_type = SIGNATURE_REQUEST;
	send_all(fd, &msg_type, 1);
	send_all(fd, (char*)&msg_size, sizeof(uint32_t));
	send_all(fd, (char*)&hash_nid, sizeof(hash_nid));
	send_all(fd, (char*)&sigalg_nid, sizeof(sigalg_nid));
	send_all(fd, hdata, hdata_len);
	log_printf(LOG_DEBUG, "Sent a sign request of length %u\n", hdata_len);
	return;
}

int recv_cert_response(int fd, X509** o_cert) {
	int bytes_read;
	char msg_type;
	int cert_len;
	char* cert_mem;
	X509* cert;
	BIO* bio;
	bytes_read = recv(fd, &msg_type, 1, MSG_WAITALL);
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read message type in cert response\n");
		return 0;
	}
	if (msg_type == FAILURE_RESPONSE) {
		log_printf(LOG_ERROR, "Device reported failure message for cert response\n");
		return 0;
	}
	bytes_read = recv(fd, &cert_len, sizeof(uint32_t), MSG_WAITALL);
	printf("bytes read = %d\n", bytes_read);
	
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read message length in cert response\n");
		return 0;
	}

	cert_len = ntohl(cert_len);
	printf("cert length = %d (%08X)\n", cert_len, cert_len);
	cert_mem = malloc(cert_len);
	if (cert_mem == NULL) {
		log_printf(LOG_ERROR, "Failed to allocate certificate length in cert response\n");
		return 0;
	}
	bytes_read = recv(fd, cert_mem, cert_len, MSG_WAITALL);
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read certificate data in cert response\n");
		return 0;
	}
	log_printf(LOG_DEBUG, "Received a response of type %d%s%s%s%s%s and length %d\n",
			msg_type,
			msg_type == 0 ? "(CERTIFICATE_REQUEST)":"",
			msg_type == 1 ? "(CERTIFICATE_RESPONSE)":"",
			msg_type == 2 ? "(SIGNATURE_REQUEST)":"",
			msg_type == 3 ? "(SIGNATURE_RESPONSE)":"",
			msg_type == 4 ? "(FAILURE_RESPONSE)":"",
			cert_len);
	bio = BIO_new(BIO_s_mem());
	if (bio == NULL) {
		log_printf(LOG_ERROR, "Failed to create BIO for certificate memory\n");
		return 0;
	}
	if (BIO_write(bio, cert_mem, cert_len) != cert_len) {
		log_printf(LOG_ERROR, "Failed to write certificate data to BIO\n");
		return 0;
	}
	cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	if (cert == NULL) {
		log_printf(LOG_ERROR, "Failed to parse auth certificate\n");
		return 0;
	}
	*o_cert = cert;
	BIO_free(bio);
	free(cert_mem);
	return 1;
}

int recv_sign_response(int fd, unsigned char** o_sig, size_t* o_siglen) {
	unsigned char* sig;
	int siglen;
	int bytes_read;
	char msg_type;
	bytes_read = recv(fd, &msg_type, 1, MSG_WAITALL);
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read message type in signature response\n");
		return 0;
	}
	if (msg_type == FAILURE_RESPONSE) {
		log_printf(LOG_ERROR, "Device reported failure message for signature response\n");
		return 0;
	}
	bytes_read = recv(fd, &siglen, sizeof(uint32_t), MSG_WAITALL);
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read message length in signature response\n");
		return 0;
	}
	siglen = ntohl(siglen);
	sig = malloc(siglen);
	if (sig == NULL) {
		log_printf(LOG_ERROR, "Failed to allocate signature response message\n");
		return 0;
	}
	bytes_read = recv(fd, sig, siglen, MSG_WAITALL);
	if (bytes_read == -1) {
		log_printf(LOG_ERROR, "Failed to read signature response\n");
		free(sig);
		return 0;
	}
	*o_sig = sig;
	*o_siglen = siglen;
	log_printf(LOG_DEBUG, "Received a response of type %d%s%s%s%s%s and length %d\n",
			msg_type,
			msg_type == 0 ? "(CERTIFICATE_REQUEST)":"",
			msg_type == 1 ? "(CERTIFICATE_RESPONSE)":"",
			msg_type == 2 ? "(SIGNATURE_REQUEST)":"",
			msg_type == 3 ? "(SIGNATURE_RESPONSE)":"",
			msg_type == 4 ? "(FAILURE_RESPONSE)":"",
			siglen);

	return 1;
}

void send_all(int fd, char* msg, int bytes_to_send) {
	int total_bytes_sent;
	int bytes_sent;
	total_bytes_sent = 0;
	while (total_bytes_sent < bytes_to_send) {
		bytes_sent = send(fd, msg + total_bytes_sent, bytes_to_send - total_bytes_sent, 0);
		if (bytes_sent == -1) {
			log_printf(LOG_ERROR, "Could not send data to auth daemon %s\n", strerror(errno));
			return;
		}
		total_bytes_sent += bytes_sent;
	}
	return;
}

#endif

