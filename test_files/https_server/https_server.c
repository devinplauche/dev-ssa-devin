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
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons(443);

	int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TLS);
	bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (setsockopt(fd, IPPROTO_TLS, TLS_CERTIFICATE_CHAIN, CERT_FILE_A, sizeof(CERT_FILE_A)) == -1) {
		perror("cert a");
	}
	if (setsockopt(fd, IPPROTO_TLS, TLS_PRIVATE_KEY, KEY_FILE_A, sizeof(KEY_FILE_A)) == -1) {
		perror("key a");
	}
	if (setsockopt(fd, IPPROTO_TLS, TLS_CERTIFICATE_CHAIN, CERT_FILE_B, sizeof(CERT_FILE_B)) == -1) {
		perror("cert b");
	}
	if (setsockopt(fd, IPPROTO_TLS, TLS_PRIVATE_KEY, KEY_FILE_B, sizeof(KEY_FILE_B)) == -1) {
		perror("key b");
	}
	listen(fd, SOMAXCONN);

	while (1) {	
		struct sockaddr_storage addr;
		socklen_t addr_len = sizeof(addr);
		int c_fd = accept(fd, (struct sockaddr*)&addr, &addr_len);
        printf("Connection received!\n");
		if (getsockopt(c_fd, IPPROTO_TLS, TLS_HOSTNAME, servername, &servername_len) == -1) {
			perror("getsockopt: TLS_HOSTNAME");
			exit(EXIT_FAILURE);
		}
		printf("Client requested host %d %s\n", servername_len,  servername);
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
