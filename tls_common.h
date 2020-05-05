/*
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

#include <event2/util.h>
#include "daemon_structs.h"


/* for set_connection_type() */
#define SERVER_CONN 0
#define CLIENT_CONN 1



void set_tls_error(sock_context* sock_ctx);
void set_tls_connection_error(sock_context* sock_ctx);



/* setsockopt */
int get_peer_certificate(connection* conn, char** data, unsigned int* len);
int get_peer_identity(connection* conn_ctx, char** data, unsigned int* len);
int get_hostname(connection* conn_ctx, char** data, unsigned int* len);
int get_enabled_ciphers(connection* conn, char** data, unsigned int* len);

/* setsockopt */
int set_connection_type(connection* conn, daemon_context* daemon, int type);
int set_trusted_peer_certificates(connection* conn, char* value);
int disable_cipher(connection* conn, char* cipher);
int set_certificate_chain(connection* conn, daemon_context* ctx, char* value);
int set_private_key(connection* conn, daemon_context* ctx, char* path);


