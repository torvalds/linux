/*
 *  Copyright (c) 2016 Limelight Networks
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
 *  Authors: George Neville-Neil
 *
 * $FreeBSD$
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <err.h>
#include <sysexits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define buflen 80

/*
 * Setup a TCP server listening on a port for connections, all of
 * which subseuqently have their user cookie set.
 */
int
main(int argc, char **argv)
{
	struct sockaddr_in srv;
	int sock, accepted, port, cookie;
	int ret;
	char recvbuf[buflen];

	if (argc != 3) {
		fprintf(stderr, "Usage: %s port cookie\n", argv[0]);
		exit(2);
	}

	port = atoi(argv[1]);
	cookie = atoi(argv[2]);

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		err(EXIT_FAILURE, "socket");

	memset(&srv, 0, sizeof(srv));
	srv.sin_len = sizeof(srv);
	srv.sin_family = AF_INET;
	srv.sin_port = htons(port);
	srv.sin_addr.s_addr = INADDR_ANY;

	if (bind(sock, (struct sockaddr *)&srv, srv.sin_len) < 0)
		err(EX_OSERR, "failed to bind to port %d", port);

	if (listen(sock, 5) < 0)
		err(EX_OSERR, "failed to listen on socket");

	ret = setsockopt(sock, SOL_SOCKET, SO_USER_COOKIE, &cookie, sizeof(cookie));
	if (ret < 0)
		err(EX_OSERR, "setsockopt(SO_USER_COOKIE)");

	while (1) {

		accepted = accept(sock, NULL, 0);

		if (accepted < 0)
			err(EX_OSERR, "accept failed");

		ret = setsockopt(accepted, SOL_SOCKET, SO_USER_COOKIE,
				 &cookie, sizeof(cookie));
		if (ret < 0)
			err(EX_OSERR, "setsockopt(SO_USER_COOKIE)");

		ret = read(accepted, &recvbuf, buflen);

		if (ret < 0)
			warn("failed read");

		close(accepted);
	}

	return (0);
}
