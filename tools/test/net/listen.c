/*-
 * Copyright (c) 2015 George V. Neville-Neil
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define PORT 6969 /* Default port */
#define LISTEN 5  /* Default backlog */
#define RECV_LIMIT 64 /* When do we move listen to 0? */

void usage(void);

void usage()
{
	err(EX_USAGE, "listen [-p port] [-l limit]\n");
}

int main(int argc, char **argv)
{

	int ch, srv_sock, cli_sock, recvd = 0;
	int port = PORT;
	int limit = LISTEN;
	struct sockaddr_in localaddr;

	while ((ch = getopt(argc, argv, "p:l:")) != -1) {
		switch (ch) {
		case 'p':
			port = atoi(optarg);
			break;
		case 'l':
			limit = atoi(optarg);
                     break;
		case 'h':
		default:
			usage();
		}
	}

	bzero(&localaddr, sizeof(localaddr));
	localaddr.sin_len = sizeof(localaddr);
	localaddr.sin_family = AF_INET;
	localaddr.sin_port = htons(port);
	localaddr.sin_addr.s_addr = INADDR_ANY;

	srv_sock = socket(AF_INET, SOCK_STREAM, 0);

	if (bind(srv_sock, (struct sockaddr *)&localaddr,
		 sizeof(struct sockaddr_in)) < 0)
		err(EX_OSERR, "Could not bind to INADDR_ANY:%d", port);
	
	if (listen(srv_sock, limit) < 0)
		err(EX_OSERR, "Failed to listen with depth of %d\n", limit);
	
	while (((cli_sock = accept(srv_sock, NULL, NULL)) >= 0)
	       && (recvd < RECV_LIMIT)) {
		close(cli_sock);
		recvd++;
	}

	listen(srv_sock, 0);

	while (((cli_sock = accept(srv_sock, NULL, NULL)) >= 0)
	    && (recvd <= (RECV_LIMIT + limit + 1))) {
		close(cli_sock);
		recvd++;
	}

	if (accept(srv_sock, NULL, NULL) >= 0)
		printf ("Late connection at %d\n", recvd);
	else
		printf("recvd %d\n", recvd);
}
