#include "config.h"
#include "log.h"
#include "tls_client.h"
#include "tls_common.h"
#include "tls_server.h"

#include <fcntl.h> /* for S_IFDIR/S_IFREG constants */
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/err.h>

#define UBUNTU_DEFAULT_CA "/etc/ssl/certs/ca-certificates.crt"
#define FEDORA_DEFAULT_CA "/etc/pki/tls/certs/ca-bundle.crt"
#define MAX_NUM_CIPHERS 37
#define MAX_CIPHERSUITE_STRING 150

int append_to_cipherstring(char* cipher, char** cipherstring);
int get_ciphersuite_string(connection* conn, char** buf, unsigned int* buf_len);
int get_cipher_list_string(connection* conn, char** buf, unsigned int* buf_len);
char* get_string_version(char* cipher_to_check);

char* get_string_version(char* cipher_to_check) {
	//todo hardcode list of tlsv1.3 strings here and check if provided cipher matches
	//will have to update if new ciphers are added to TLSv1.3
	char* gcm_384 = "TLS_AES_256_GCM_SHA384";
  char* gcm_256 = "TLS_AES_128_GCM_SHA256";
  char* chacha_256 = "TLS_CHACHA20_POLY1305_SHA256";
  char* ccm_256 = "TLS_AES_128_CCM_SHA256";
  char* ccm_8_256 = "TLS_AES_128_CCM_8_SHA256";
	char* tlsv13 = "TLSv1.3";
	char* tlsv12 = "TLSv12";

	if(cipher_to_check == gcm_384 || cipher_to_check == gcm_256 || cipher_to_check == chacha_256 ||
	cipher_to_check == ccm_256 || cipher_to_check == ccm_8_256) {
		return tlsv13;
	}
	else {
		return tlsv12;
	}
}
