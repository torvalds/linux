// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include "../../net/lib/ksft.h"

int main(int argc, char *argv[])
{
	struct sockaddr_storage address;
	struct addrinfo *result;
	struct addrinfo hints;
	unsigned int napi_id;
	socklen_t addr_len;
	socklen_t optlen;
	char buf[1024];
	int opt = 1;
	int family;
	int server;
	int client;
	int ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	ret = getaddrinfo(argv[1], argv[2], &hints, &result);
	if (ret != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		return 1;
	}

	family = result->ai_family;
	addr_len = result->ai_addrlen;

	server = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0) {
		perror("socket creation failed");
		freeaddrinfo(result);
		if (errno == EAFNOSUPPORT)
			return -1;
		return 1;
	}

	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt");
		freeaddrinfo(result);
		return 1;
	}

	memcpy(&address, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);

	if (bind(server, (struct sockaddr *)&address, addr_len) < 0) {
		perror("bind failed");
		return 1;
	}

	if (listen(server, 1) < 0) {
		perror("listen");
		return 1;
	}

	ksft_ready();

	client = accept(server, NULL, 0);
	if (client < 0) {
		perror("accept");
		return 1;
	}

	optlen = sizeof(napi_id);
	ret = getsockopt(client, SOL_SOCKET, SO_INCOMING_NAPI_ID, &napi_id,
			 &optlen);
	if (ret != 0) {
		perror("getsockopt");
		return 1;
	}

	read(client, buf, 1024);

	ksft_wait();

	if (napi_id == 0) {
		fprintf(stderr, "napi ID is 0\n");
		return 1;
	}

	close(client);
	close(server);

	return 0;
}
