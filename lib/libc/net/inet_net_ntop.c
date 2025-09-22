/*	$OpenBSD: inet_net_ntop.c,v 1.9 2019/07/03 03:24:04 deraadt Exp $	*/

/*
 * Copyright (c) 2012 by Gilles Chehade <gilles@openbsd.org>
 * Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *inet_net_ntop_ipv4(const u_char *, int, char *, size_t);
static char *inet_net_ntop_ipv6(const u_char *, int, char *, size_t);

/*
 * char *
 * inet_net_ntop(af, src, bits, dst, size)
 *	convert network number from network to presentation format.
 *	generates CIDR style result always.
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * author:
 *	Paul Vixie (ISC), July 1996
 */
char *
inet_net_ntop(int af, const void *src, int bits, char *dst, size_t size)
{
	switch (af) {
	case AF_INET:
		return (inet_net_ntop_ipv4(src, bits, dst, size));
	case AF_INET6:
		return (inet_net_ntop_ipv6(src, bits, dst, size));
	default:
		errno = EAFNOSUPPORT;
		return (NULL);
	}
}

/*
 * static char *
 * inet_net_ntop_ipv4(src, bits, dst, size)
 *	convert IPv4 network number from network to presentation format.
 *	generates CIDR style result always.
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	network byte order assumed.  this means 192.5.5.240/28 has
 *	0x11110000 in its fourth octet.
 * author:
 *	Paul Vixie (ISC), July 1996
 */
static char *
inet_net_ntop_ipv4(const u_char *src, int bits, char *dst, size_t size)
{
	char *odst = dst;
	u_int m;
	int b;
	char *ep;
	int advance;

	ep = dst + size;
	if (ep <= dst)
		goto emsgsize;

	if (bits < 0 || bits > 32) {
		errno = EINVAL;
		return (NULL);
	}
	if (bits == 0) {
		if (ep - dst < sizeof "0")
			goto emsgsize;
		*dst++ = '0';
		*dst = '\0';
	}

	/* Format whole octets. */
	for (b = bits / 8; b > 0; b--) {
		if (ep - dst < sizeof "255.")
			goto emsgsize;
		advance = snprintf(dst, ep - dst, "%u", *src++);
		if (advance <= 0 || advance >= ep - dst)
			goto emsgsize;
		dst += advance;
		if (b > 1) {
			if (dst + 1 >= ep)
				goto emsgsize;
			*dst++ = '.';
			*dst = '\0';
		}
	}

	/* Format partial octet. */
	b = bits % 8;
	if (b > 0) {
		if (ep - dst < sizeof ".255")
			goto emsgsize;
		if (dst != odst)
			*dst++ = '.';
		m = ((1 << b) - 1) << (8 - b);
		advance = snprintf(dst, ep - dst, "%u", *src & m);
		if (advance <= 0 || advance >= ep - dst)
			goto emsgsize;
		dst += advance;
	}

	/* Format CIDR /width. */
	if (ep - dst < sizeof "/32")
		goto emsgsize;
	advance = snprintf(dst, ep - dst, "/%u", bits);
	if (advance <= 0 || advance >= ep - dst)
		goto emsgsize;
	dst += advance;
	return (odst);

 emsgsize:
	errno = EMSGSIZE;
	return (NULL);
}

static char *
inet_net_ntop_ipv6(const u_char *src, int bits, char *dst, size_t size)
{
	int	ret;
	char	buf[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255:255:255:255/128")];

	if (bits < 0 || bits > 128) {
		errno = EINVAL;
		return (NULL);
	}

	if (inet_ntop(AF_INET6, src, buf, size) == NULL)
		return (NULL);

	ret = snprintf(dst, size, "%s/%d", buf, bits);
	if (ret < 0 || ret >= size) {
		errno = EMSGSIZE;
		return (NULL); 
	}

	return (dst);
}
