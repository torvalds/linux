/*	$OpenBSD: gaitest.c,v 1.7 2020/02/14 19:17:33 schwarze Exp $	*/
/*	$NetBSD: gaitest.c,v 1.3 2002/07/05 15:47:43 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, and 2002 WIDE Project.
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
 */

/*
 * Please note: the order of the responses (and the regress test)
 * is dependent on the "family" keywords in resolv.conf.
 *
 * This expects the default behaviour of "family inet4 inet6"
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct addrinfo ai;

char host[NI_MAXHOST];
char serv[NI_MAXSERV];
int vflag = 0;

static void usage(void);
static void print1(const char *, const struct addrinfo *, char *, char *);
int main(int, char *[]);

static void
usage()
{
	fprintf(stderr, "usage: test [-f family] [-s socktype] [-p proto] [-DPRSv46] host serv\n");
}

static void
print1(title, res, h, s)
	const char *title;
	const struct addrinfo *res;
	char *h;
	char *s;
{
	char *start, *end;
	int error;
	const int niflag = NI_NUMERICHOST | NI_NUMERICSERV;

	if (res->ai_addr) {
		error = getnameinfo(res->ai_addr, res->ai_addr->sa_len,
				    host, sizeof(host), serv, sizeof(serv),
				    niflag);
		h = host;
		s = serv;
	} else
		error = 0;

	if (vflag) {
		start = "\t";
		end = "\n";
	} else {
		start = " ";
		end = "";
	}
	printf("%s%s", title, end);
	printf("%sflags 0x%x%s", start, res->ai_flags, end);
	printf("%sfamily %d%s", start, res->ai_family, end);
	printf("%ssocktype %d%s", start, res->ai_socktype, end);
	printf("%sprotocol %d%s", start, res->ai_protocol, end);
	printf("%saddrlen %d%s", start, res->ai_addrlen, end);
	if (error)
		printf("%serror %d%s", start, error, end);
	else {
		printf("%shost %s%s", start, h, end);
		printf("%sserv %s%s", start, s, end);
	}
#if 0
	if (res->ai_canonname)
		printf("%scname \"%s\"%s", start, res->ai_canonname, end);
#endif
	if (!vflag)
		printf("\n");

}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct addrinfo *res;
	int error, i;
	char *p, *q;
	int c;
	char nbuf[10];

	memset(&ai, 0, sizeof(ai));
	ai.ai_family = PF_UNSPEC;
	ai.ai_flags |= AI_CANONNAME;
	while ((c = getopt(argc, argv, "Df:p:PRs:Sv46")) != -1) {
		switch (c) {
		case 'D':
			ai.ai_socktype = SOCK_DGRAM;
			break;
		case 'f':
			ai.ai_family = atoi(optarg);
			break;
		case 'p':
			ai.ai_protocol = atoi(optarg);
			break;
		case 'P':
			ai.ai_flags |= AI_PASSIVE;
			break;
		case 'R':
			ai.ai_socktype = SOCK_RAW;
			break;
		case 's':
			ai.ai_socktype = atoi(optarg);
			break;
		case 'S':
			ai.ai_socktype = SOCK_STREAM;
			break;
		case 'v':
			vflag++;
			break;
		case '4':
			ai.ai_family = PF_INET;
			break;
		case '6':
			ai.ai_family = PF_INET6;
			break;
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2){
		usage();
		exit(1);
	}

	p = *argv[0] ? argv[0] : NULL;
	q = *argv[1] ? argv[1] : NULL;

	print1("arg:", &ai, p ? p : "(empty)", q ? q : "(empty)");

	error = getaddrinfo(p, q, &ai, &res);
	if (error) {
		printf("%s\n", gai_strerror(error));
		exit(1);
	}

	i = 1;
	do {
		snprintf(nbuf, sizeof(nbuf), "ai%d:", i);
		print1(nbuf, res, NULL, NULL);

		i++;
	} while ((res = res->ai_next) != NULL);

	exit(0);
}
