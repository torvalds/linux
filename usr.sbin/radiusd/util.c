/*	$OpenBSD: util.c,v 1.3 2019/07/03 03:24:03 deraadt Exp $	*/

/*
 * Copyright (c) 2013 Internet Initiative Japan Inc.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netdb.h>
#include <string.h>
#include <stdio.h>

#include "util.h"

/*
 * Convert argument like "192.168.160.1:1723/tcp" or "[::1]:1723/tcp" to
 * match getaddrinfo(3)'s specification and pass them to getaddrinfo(3).
 */
int
addrport_parse(const char *addrport, int proto, struct addrinfo **p_ai)
{
	char		*servp, *nodep, *slash, buf[256];
	struct addrinfo	 hints;

	strlcpy(buf, addrport, sizeof(buf));
	if (buf[0] == '[' && (servp = strchr(buf, ']')) != NULL) {
		nodep = buf + 1;
		*servp++ = '\0';
		if (*servp != ':')
			servp = NULL;
	} else {
		nodep = buf;
		servp = strrchr(nodep, ':');
	}
	if (servp != NULL) {
		*servp = '\0';
		servp++;
		slash = strrchr(servp, '/');
		if (slash != NULL) {
			/*
			 * Ignore like "/tcp"
			 */
			*slash = '\0';
			slash++;
		}
	} else
		servp = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
	switch (proto) {
	case IPPROTO_TCP:
		hints.ai_socktype = SOCK_STREAM;
		break;
	case IPPROTO_UDP:
		hints.ai_socktype = SOCK_DGRAM;
		break;
	}
	hints.ai_protocol = proto;

	return (getaddrinfo(nodep, servp, &hints, p_ai));
}

/*
 * Make a string like "192.168.160.1:1723" or "[::1]:1723" from a struct
 * sockaddr
 */
const char *
addrport_tostring(struct sockaddr *sa, socklen_t salen, char *buf, size_t lbuf)
{
	char	 hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	int	 ret;

	if (getnameinfo(sa, salen, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		return (NULL);

	switch (sa->sa_family) {
	case AF_INET6:
		ret = snprintf(buf, lbuf, "[%s]:%s", hbuf, sbuf);
		break;

	case AF_INET:
		ret = snprintf(buf, lbuf, "%s:%s", hbuf, sbuf);
		break;

	default:
		return "error";
	}

	if (ret < 0 || ret >= (int)lbuf)
		return "(error)";
	return (buf);
}
