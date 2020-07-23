#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include "../../in_tls.h"

#define CERT_FILE_A	"../certificate_a.pem"
#define KEY_FILE_A	"../key_a.pem"
#define CERT_FILE_B	"../certificate_b.pem"
#define KEY_FILE_B	"../key_b.pem"
#define BUFFER_SIZE	2048

void handle_req(char* req, char* resp, int num_received);

int main() {
	char servername[255];
	int servername_len = sizeof(servername);
	char request[BUFFER_SIZE];
	char response[BUFFER_SIZE];
	memset(request, 0, BUFFER_SIZE);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("192.168.254.64");
	addr.sin_port = htons(443);

	int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TLS);

	//bind(fd, (struct sockaddr*)&addr, sizeof(addr)); //needed?

	listen(fd, SOMAXCONN);
	char cipher[] = "TLS_AES_256_GCM_SHA384";
	char cipher1[] = "ECDHE-RSA-AES256-GCM-SHA384";
	char bad_cipher[] = "NULL-MD5"; //TODO: test more bad ciphers
	char two_12ciphers[] = "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384";
	char two_ciphers[] = "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256";
	char good_and_bad[] = "NULL-MD5:ECDHE-RSA-AES256-GCM-SHA384";

	//printf("%s\n", cipher);

	if (setsockopt(fd, IPPROTO_TLS, TLS_DISABLE_CIPHER, cipher, strlen(cipher)+1) == -1) {
		perror("setsockopt: TLS_DISABLE_CIPHER");
		//close(fd);

	}

	if (setsockopt(fd, IPPROTO_TLS, TLS_ENABLE_CIPHER, good_and_bad, strlen(good_and_bad) + 1) == -1) {//my Tests
		perror("setsockopt: TLS_ENABLE_CIPHER");
		//close(fd);
	}
	while (1) {
		struct sockaddr_storage addr;
		socklen_t addr_len = sizeof(addr);
		int c_fd = accept(fd, (struct sockaddr*)&addr, &addr_len);
        printf("Connection received!\n");
				char data[4096];
				socklen_t data_len = sizeof(data);
				data[0] = '\0'; //empty
			  if (getsockopt(fd, IPPROTO_TLS, TLS_TRUSTED_CIPHERS, data, &data_len) == -1) {
					perror("TLS_TRUSTED_CIPHERS");
				}
				data_len = sizeof(data);

			  if (getsockopt(fd, IPPROTO_TLS, TLS_NEGOTIATED_CIPHER, data, &data_len) == -1) {
					perror("TLS_NEGOTIATED_CIPHER");
				}
		int num_received = recv(c_fd, request, BUFFER_SIZE, 0);
		printf("Received %i bytes from client.\n", num_received);
		handle_req(request, response, num_received);
		int num_sent = send(c_fd, response, num_received+1, 0); /* +1 for EOF */
		printf("Sent %i bytes to client.\n", num_sent);
		close(c_fd);
	}
	return 0;
}

void handle_req(char* req, char* resp, int num_received) {
	memcpy(resp, req, num_received);
	resp[num_received] = '\0';
	return;
}
