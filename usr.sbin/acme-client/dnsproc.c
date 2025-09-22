/*	$Id: dnsproc.c,v 1.12 2021/12/13 13:30:39 jca Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <arpa/inet.h>

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

struct	addr {
	int	 family; /* 4 for PF_INET, 6 for PF_INET6 */
	char	 ip[INET6_ADDRSTRLEN];
};

/*
 * This is a modified version of host_dns in config.c of OpenBSD's ntpd.
 */
/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
static ssize_t
host_dns(const char *s, struct addr *vec)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error;
	ssize_t			 vecsz;
	struct sockaddr		*sa;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* DUMMY */
	hints.ai_flags = AI_ADDRCONFIG;

	error = getaddrinfo(s, NULL, &hints, &res0);

	if (error == EAI_AGAIN ||
		/* FIXME */
#ifndef __FreeBSD__
	    error == EAI_NODATA ||
#endif
	    error == EAI_NONAME)
		return 0;

	if (error) {
		warnx("%s: parse error: %s",
		    s, gai_strerror(error));
		return -1;
	}

	for (vecsz = 0, res = res0;
	    res != NULL && vecsz < MAX_SERVERS_DNS;
	    res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;

		sa = res->ai_addr;

		if (res->ai_family == AF_INET) {
			vec[vecsz].family = 4;
			inet_ntop(AF_INET,
			    &(((struct sockaddr_in *)sa)->sin_addr),
				vec[vecsz].ip, INET6_ADDRSTRLEN);
		} else {
			vec[vecsz].family = 6;
			inet_ntop(AF_INET6,
			    &(((struct sockaddr_in6 *)sa)->sin6_addr),
			    vec[vecsz].ip, INET6_ADDRSTRLEN);
		}

		dodbg("%s: DNS: %s", s, vec[vecsz].ip);
		vecsz++;
	}

	freeaddrinfo(res0);
	return vecsz;
}

int
dnsproc(int nfd)
{
	char		*look = NULL, *last = NULL;
	struct addr	 v[MAX_SERVERS_DNS];
	int		 rc = 0, cc;
	long		 lval;
	ssize_t		 vsz = 0;
	size_t		 i;
	enum dnsop	 op;

	if (pledge("stdio dns", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/*
	 * This is simple: just loop on a request operation, and each
	 * time we write back zero or more entries.
	 * Also do a simple trick and cache the last lookup.
	 */

	for (;;) {
		op = DNS__MAX;
		if ((lval = readop(nfd, COMM_DNS)) == 0)
			op = DNS_STOP;
		else if (lval == DNS_LOOKUP)
			op = lval;

		if (op == DNS__MAX) {
			warnx("unknown operation from netproc");
			goto out;
		} else if (op == DNS_STOP)
			break;

		if ((look = readstr(nfd, COMM_DNSQ)) == NULL)
			goto out;

		/*
		 * Check if we're asked to repeat the lookup.
		 * If not, request it from host_dns().
		 */

		if (last == NULL || strcmp(look, last)) {
			if ((vsz = host_dns(look, v)) < 0)
				goto out;

			free(last);
			last = look;
			look = NULL;
		} else {
			free(look);
			look = NULL;
		}

		if ((cc = writeop(nfd, COMM_DNSLEN, vsz)) == 0)
			break;
		else if (cc < 0)
			goto out;
		for (i = 0; i < (size_t)vsz; i++) {
			if (writeop(nfd, COMM_DNSF, v[i].family) <= 0)
				goto out;
			if (writestr(nfd, COMM_DNSA, v[i].ip) <= 0)
				goto out;
		}
	}

	rc = 1;
out:
	close(nfd);
	free(look);
	free(last);
	return rc;
}
