// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void set_addr(struct sockaddr_storage *ss, char *ip, char *port, int *len)
{
	if (ss->ss_family == AF_INET) {
		struct sockaddr_in *a = (struct sockaddr_in *)ss;

		a->sin_addr.s_addr = inet_addr(ip);
		a->sin_port = htons(atoi(port));
		*len = sizeof(*a);
	} else {
		struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;

		a->sin6_family = AF_INET6;
		inet_pton(AF_INET6, ip, &a->sin6_addr);
		a->sin6_port = htons(atoi(port));
		*len = sizeof(*a);
	}
}

static int do_client(int argc, char *argv[])
{
	struct sockaddr_storage ss;
	int csk, ret, len;

	if (argc < 5) {
		printf("%s client -4|6 IP PORT [IP PORT]\n", argv[0]);
		return -1;
	}

	bzero((void *)&ss, sizeof(ss));
	ss.ss_family = !strcmp(argv[2], "-4") ? AF_INET : AF_INET6;
	csk = socket(ss.ss_family, SOCK_STREAM, IPPROTO_SCTP);
	if (csk < 0) {
		printf("failed to create socket\n");
		return -1;
	}

	if (argc >= 7) {
		set_addr(&ss, argv[5], argv[6], &len);
		ret = bind(csk, (struct sockaddr *)&ss, len);
		if (ret < 0) {
			printf("failed to bind to address\n");
			return -1;
		}
	}

	set_addr(&ss, argv[3], argv[4], &len);
	ret = connect(csk, (struct sockaddr *)&ss, len);
	if (ret < 0)
		return -1;

	recv(csk, NULL, 0, 0);
	close(csk);

	return 0;
}

int main(int argc, char *argv[])
{
	struct sockaddr_storage ss;
	int lsk, csk, ret, len;

	if (argc < 2 || (strcmp(argv[1], "server") && strcmp(argv[1], "client"))) {
		printf("%s server|client ...\n", argv[0]);
		return -1;
	}

	if (!strcmp(argv[1], "client"))
		return do_client(argc, argv);

	if (argc < 5) {
		printf("%s server -4|6 IP PORT [IFACE]\n", argv[0]);
		return -1;
	}

	ss.ss_family = !strcmp(argv[2], "-4") ? AF_INET : AF_INET6;
	lsk = socket(ss.ss_family, SOCK_STREAM, IPPROTO_SCTP);
	if (lsk < 0) {
		printf("failed to create lsk\n");
		return -1;
	}

	if (argc >= 6) {
		ret = setsockopt(lsk, SOL_SOCKET, SO_BINDTODEVICE,
				 argv[5], strlen(argv[5]) + 1);
		if (ret < 0) {
			printf("failed to bind to device\n");
			return -1;
		}
	}

	set_addr(&ss, argv[3], argv[4], &len);
	ret = bind(lsk, (struct sockaddr *)&ss, len);
	if (ret < 0) {
		printf("failed to bind to address\n");
		return -1;
	}

	ret = listen(lsk, 5);
	if (ret < 0) {
		printf("failed to listen on port\n");
		return -1;
	}

	csk = accept(lsk, (struct sockaddr *)NULL, (socklen_t *)NULL);
	if (csk < 0) {
		printf("failed to accept new client\n");
		return -1;
	}

	close(csk);
	close(lsk);

	return 0;
}
