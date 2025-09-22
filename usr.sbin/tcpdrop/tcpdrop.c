/* $OpenBSD: tcpdrop.c,v 1.21 2023/02/06 18:14:10 millert Exp $ */

/*
 * Copyright (c) 2004 Markus Friedl <markus@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/timeout.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <resolv.h>

__dead void	 usage(void);

__dead void
usage(void)
{
	extern char	*__progname;

	fprintf(stderr,
	    "usage: %s local-addr local-port remote-addr remote-port\n",
	    __progname);
	exit(1);
}

/*
 * Drop a tcp connection.
 */
int
main(int argc, char **argv)
{
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_DROP };
	struct addrinfo hints, *ail, *aif, *laddr, *faddr;
	char fhbuf[NI_MAXHOST], fsbuf[NI_MAXSERV];
	char lhbuf[NI_MAXHOST], lsbuf[NI_MAXSERV];
	char *laddr1, *addr1, *port1, *faddr2, *addr2, *port2;
	struct tcp_ident_mapping tir;
	int gaierr, rval = 0;

	if (unveil(_PATH_HOSTS, "r") == -1)
		err(1, "unveil %s", _PATH_HOSTS);
	if (unveil(_PATH_RESCONF, "r") == -1)
		err(1, "unveil %s", _PATH_RESCONF);
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (argc == 3) {
		char *dot;

		laddr1 = addr1 = strdup(argv[1]);
		if (!addr1)
			err(1, "strdup");
		port1 = strrchr(addr1, ':');
		dot = strrchr(addr1, '.');
		if (dot > port1)
			port1 = dot;
		if (port1)
			*port1++ = '\0';
		else
			usage();

		faddr2 = addr2 = strdup(argv[2]);
		if (!addr2)
			err(1, "strdup");
		port2 = strrchr(addr2, ':');
		dot = strrchr(addr2, '.');
		if (dot > port2)
			port2 = dot;
		if (port2)
			*port2++ = '\0';
		else
			usage();
	} else if (argc == 5) {
		laddr1 = addr1 = argv[1];
		port1 = argv[2];
		faddr2 = addr2 = argv[3];
		port2 = argv[4];
	} else
		usage();

	if (addr1[0] == '[' && addr1[strlen(addr1) - 1] == ']') {
		laddr1 = strdup(addr1);
		if (!laddr1)
			err(1, "strdup");
		laddr1[strlen(laddr1) - 1] = '\0';
		laddr1++;
	}
	if (addr2[0] == '[' && addr2[strlen(addr2) - 1] == ']') {
		faddr2 = strdup(addr2);
		if (!faddr2)
			err(1, "strdup");
		faddr2[strlen(faddr2) - 1] = '\0';
		faddr2++;
	}

	if ((gaierr = getaddrinfo(laddr1, port1, &hints, &laddr)) != 0)
		errx(1, "%s port %s: %s", addr1, port1,
		    gai_strerror(gaierr));

	if ((gaierr = getaddrinfo(faddr2, port2, &hints, &faddr)) != 0)
		errx(1, "%s port %s: %s", addr2, port2,
		    gai_strerror(gaierr));

	rval = 1;
	for (ail = laddr; ail; ail = ail->ai_next) {
		for (aif = faddr; aif; aif = aif->ai_next) {
			if (ail->ai_family != aif->ai_family)
				continue;
			rval = 0;
			memset(&tir, 0, sizeof(tir));
			memcpy(&tir.faddr, aif->ai_addr, aif->ai_addrlen);
			memcpy(&tir.laddr, ail->ai_addr, ail->ai_addrlen);

			if ((gaierr = getnameinfo(aif->ai_addr, aif->ai_addrlen,
			    fhbuf, sizeof(fhbuf), fsbuf, sizeof(fsbuf),
			    NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
				errx(1, "getnameinfo: %s", gai_strerror(gaierr));
			if ((gaierr = getnameinfo(ail->ai_addr, ail->ai_addrlen,
			    lhbuf, sizeof(lhbuf), lsbuf, sizeof(lsbuf),
			    NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
				errx(1, "getnameinfo: %s", gai_strerror(gaierr));

			if (sysctl(mib, sizeof (mib) / sizeof (int), NULL,
			    NULL, &tir, sizeof(tir)) == -1) {
				rval = 1;
				warn("%s %s %s %s", lhbuf, lsbuf, fhbuf, fsbuf);
			} else {
				if (aif->ai_family == PF_INET6)
					printf("[%s]:%s [%s]:%s dropped\n",
					    lhbuf, lsbuf, fhbuf, fsbuf);
				else
					printf("%s:%s %s:%s dropped\n",
					    lhbuf, lsbuf, fhbuf, fsbuf);
			}
		}
	}
	freeaddrinfo(laddr);
	freeaddrinfo(faddr);
	exit(rval);
}
