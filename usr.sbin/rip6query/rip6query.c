/*	$KAME: rip6query.c,v 1.11 2001/05/08 04:36:37 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
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

#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <err.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "route6d.h"

static int	s;
static struct sockaddr_in6 sin6;
static struct rip6	*ripbuf;

#define	RIPSIZE(n)	(sizeof(struct rip6) + (n-1) * sizeof(struct netinfo6))

int main(int, char **);
static void usage(void);
static const char *sa_n2a(struct sockaddr *);
static const char *inet6_n2a(struct in6_addr *);

int
main(int argc, char *argv[])
{
	struct netinfo6 *np;
	struct sockaddr_in6 fsock;
	int i, n, len;
	int c;
	int ifidx = -1;
	int error;
	socklen_t flen;
	char pbuf[10];
	struct addrinfo hints, *res;

	while ((c = getopt(argc, argv, "I:")) != -1) {
		switch (c) {
		case 'I':
			ifidx = if_nametoindex(optarg);
			if (ifidx == 0) {
				errx(1, "invalid interface %s", optarg);
				/*NOTREACHED*/
			}
			break;
		default:
			usage();
			exit(1);
			/*NOTREACHED*/
		}
	}
	argv += optind;
	argc -= optind;

	if (argc != 1) {
		usage();
		exit(1);
	}

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		err(1, "socket");
		/*NOTREACHED*/
	}

	/* getaddrinfo is preferred for addr@ifname syntax */
	snprintf(pbuf, sizeof(pbuf), "%d", RIP6_PORT);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	error = getaddrinfo(argv[0], pbuf, &hints, &res);
	if (error) {
		errx(1, "%s: %s", argv[0], gai_strerror(error));
		/*NOTREACHED*/
	}
	if (res->ai_next) {
		errx(1, "%s: %s", argv[0], "resolved to multiple addrs");
		/*NOTREACHED*/
	}
	if (sizeof(sin6) != res->ai_addrlen) {
		errx(1, "%s: %s", argv[0], "invalid addrlen");
		/*NOTREACHED*/
	}
	memcpy(&sin6, res->ai_addr, res->ai_addrlen);
	if (ifidx >= 0)
		sin6.sin6_scope_id = ifidx;

	if ((ripbuf = (struct rip6 *)malloc(BUFSIZ)) == NULL) {
		err(1, "malloc");
		/*NOTREACHED*/
	}
	ripbuf->rip6_cmd = RIP6_REQUEST;
	ripbuf->rip6_vers = RIP6_VERSION;
	ripbuf->rip6_res1[0] = 0;
	ripbuf->rip6_res1[1] = 0;
	np = ripbuf->rip6_nets;
	bzero(&np->rip6_dest, sizeof(struct in6_addr));
	np->rip6_tag = 0;
	np->rip6_plen = 0;
	np->rip6_metric = HOPCNT_INFINITY6;
	if (sendto(s, ripbuf, RIPSIZE(1), 0, (struct sockaddr *)&sin6,
			sizeof(struct sockaddr_in6)) < 0) {
		err(1, "send");
		/*NOTREACHED*/
	}
	do {
		flen = sizeof(fsock);
		if ((len = recvfrom(s, ripbuf, BUFSIZ, 0,
				(struct sockaddr *)&fsock, &flen)) < 0) {
			err(1, "recvfrom");
			/*NOTREACHED*/
		}
		printf("Response from %s len %d\n",
			sa_n2a((struct sockaddr *)&fsock), len);
		n = (len - sizeof(struct rip6) + sizeof(struct netinfo6)) /
			sizeof(struct netinfo6);
		np = ripbuf->rip6_nets;
		for (i = 0; i < n; i++, np++) {
			printf("\t%s/%d [%d]", inet6_n2a(&np->rip6_dest),
				np->rip6_plen, np->rip6_metric);
			if (np->rip6_tag)
				printf(" tag=0x%x", ntohs(np->rip6_tag));
			printf("\n");
		}
	} while (len == RIPSIZE(24));

	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: rip6query [-I iface] address\n");
}

/* getnameinfo() is preferred as we may be able to show ifindex as ifname */
static const char *
sa_n2a(struct sockaddr *sa)
{
	static char buf[NI_MAXHOST];

	if (getnameinfo(sa, sa->sa_len, buf, sizeof(buf),
			NULL, 0, NI_NUMERICHOST) != 0) {
		snprintf(buf, sizeof(buf), "%s", "(invalid)");
	}
	return buf;
}

static const char *
inet6_n2a(struct in6_addr *addr)
{
	static char buf[NI_MAXHOST];

	return inet_ntop(AF_INET6, addr, buf, sizeof(buf));
}
