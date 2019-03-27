/*
 *  Copyright (c) 2014 Spectra Logic Corporation
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions, and the following disclaimer,
 *     without modification.
 *  2. Redistributions in binary form must reproduce at minimum a disclaimer
 *     substantially similar to the "NO WARRANTY" disclaimer below
 *     ("Disclaimer") and any redistribution must be conditioned upon
 *     including a substantially similar Disclaimer requirement for further
 *     binary redistribution.
 *
 *  NO WARRANTY
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGES.
 *
 *  Authors: Alan Somers         (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Sends a single UDP packet to the provided address, with SO_DONTROUTE set
 * I couldn't find a way to do this with builtin utilities like nc(1)
 */
int
main(int argc, char **argv)
{
	struct sockaddr_storage dst;
	int s, t;
	int opt;
	int ret;
	ssize_t len;
	const char* sendbuf = "Hello, World!";
	const size_t buflen = 80;
	char recvbuf[buflen];
	bool v6 = false;
	const char *addr, *tapdev;
	const uint16_t port = 46120;

	bzero(&dst, sizeof(dst));
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "Usage: %s [-6] ip_address tapdev\n", argv[0]);
		exit(2);
	}

	if (strcmp("-6", argv[1]) == 0) {
		v6 = true;
		addr = argv[2];
		tapdev = argv[3];
	} else {
		addr = argv[1];
		tapdev = argv[2];
	}

	t = open(tapdev, O_RDWR | O_NONBLOCK);
	if (t < 0)
		err(EXIT_FAILURE, "open");

	if (v6)
		s = socket(PF_INET6, SOCK_DGRAM, 0);
	else
		s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		err(EXIT_FAILURE, "socket");
	opt = 1;

	ret = setsockopt(s, SOL_SOCKET, SO_DONTROUTE, &opt, sizeof(opt));
	if (ret == -1)
		err(EXIT_FAILURE, "setsockopt(SO_DONTROUTE)");

	if (v6) {
		struct sockaddr_in6 *dst6 = ((struct sockaddr_in6*)&dst);

		dst.ss_len = sizeof(struct sockaddr_in6);
		dst.ss_family = AF_INET6;
		dst6->sin6_port = htons(port);
		ret = inet_pton(AF_INET6, addr, &dst6->sin6_addr);
	} else {
		struct sockaddr_in *dst4 = ((struct sockaddr_in*)&dst);

		dst.ss_len = sizeof(struct sockaddr_in);
		dst.ss_family = AF_INET;
		dst4->sin_port = htons(port);
		ret = inet_pton(AF_INET, addr, &dst4->sin_addr);
	}
	if (ret != 1)
		err(EXIT_FAILURE, "inet_pton returned %d", ret);

	ret = sendto(s, sendbuf, strlen(sendbuf), 0, (struct sockaddr*)&dst,
	    dst.ss_len);
	if (ret == -1)
		err(EXIT_FAILURE, "sendto");

	/* Verify that the packet went to the desired tap device */

	len = read(t, recvbuf, buflen);
	if (len == 0)
		errx(EXIT_FAILURE, "read returned EOF");
	else if (len < 0 && errno == EAGAIN)
		errx(EXIT_FAILURE, "Did not receive any packets");
	else if (len < 0)
		err(EXIT_FAILURE, "read");

	/*
	 * If read returned anything at all, consider it a success.  The packet
	 * should be an Ethernet frame containing an ARP request for
	 * ip_address.  We won't bother to decode it
	 */
	return (0);
}
