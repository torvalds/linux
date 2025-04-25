// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "../../net/lib/ksft.h"

int main(int argc, char *argv[])
{
	struct sockaddr_in address;
	unsigned int napi_id;
	unsigned int port;
	socklen_t optlen;
	char buf[1024];
	int opt = 1;
	int server;
	int client;
	int ret;

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0) {
		perror("socket creation failed");
		if (errno == EAFNOSUPPORT)
			return -1;
		return 1;
	}

	port = atoi(argv[2]);

	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt");
		return 1;
	}

	address.sin_family = AF_INET;
	inet_pton(AF_INET, argv[1], &address.sin_addr);
	address.sin_port = htons(port);

	if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0) {
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
