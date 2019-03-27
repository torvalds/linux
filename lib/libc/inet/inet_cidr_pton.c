/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1998,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char rcsid[] = "$Id: inet_cidr_pton.c,v 1.6 2005/04/27 04:56:19 sra Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "port_after.h"

#ifdef SPRINTF_CHAR
# define SPRINTF(x) strlen(sprintf/**/x)
#else
# define SPRINTF(x) ((size_t)sprintf x)
#endif

static int	inet_cidr_pton_ipv4 (const char *src, u_char *dst, int *bits,
				     int ipv6);
static int	inet_cidr_pton_ipv6 (const char *src, u_char *dst, int *bits);

static int	getbits(const char *, int ipv6);

/*%
 * int
 * inet_cidr_pton(af, src, dst, *bits)
 *	convert network address from presentation to network format.
 *	accepts inet_pton()'s input for this "af" plus trailing "/CIDR".
 *	"dst" is assumed large enough for its "af".  "bits" is set to the
 *	/CIDR prefix length, which can have defaults (like /32 for IPv4).
 * return:
 *	-1 if an error occurred (inspect errno; ENOENT means bad format).
 *	0 if successful conversion occurred.
 * note:
 *	192.5.5.1/28 has a nonzero host part, which means it isn't a network
 *	as called for by inet_net_pton() but it can be a host address with
 *	an included netmask.
 * author:
 *	Paul Vixie (ISC), October 1998
 */
int
inet_cidr_pton(int af, const char *src, void *dst, int *bits) {
	switch (af) {
	case AF_INET:
		return (inet_cidr_pton_ipv4(src, dst, bits, 0));
	case AF_INET6:
		return (inet_cidr_pton_ipv6(src, dst, bits));
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}
}

static const char digits[] = "0123456789";

static int
inet_cidr_pton_ipv4(const char *src, u_char *dst, int *pbits, int ipv6) {
	const u_char *odst = dst;
	int n, ch, tmp, bits;
	size_t size = 4;

	/* Get the mantissa. */
	while (ch = *src++, (isascii(ch) && isdigit(ch))) {
		tmp = 0;
		do {
			n = strchr(digits, ch) - digits;
			assert(n >= 0 && n <= 9);
			tmp *= 10;
			tmp += n;
			if (tmp > 255)
				goto enoent;
		} while ((ch = *src++) != '\0' && isascii(ch) && isdigit(ch));
		if (size-- == 0U)
			goto emsgsize;
		*dst++ = (u_char) tmp;
		if (ch == '\0' || ch == '/')
			break;
		if (ch != '.')
			goto enoent;
	}

	/* Get the prefix length if any. */
	bits = -1;
	if (ch == '/' && dst > odst) {
		bits = getbits(src, ipv6);
		if (bits == -2)
			goto enoent;
	} else if (ch != '\0')
		goto enoent;

	/* Prefix length can default to /32 only if all four octets spec'd. */
	if (bits == -1) {
		if (dst - odst == 4)
			bits = ipv6 ? 128 : 32;
		else
			goto enoent;
	}

	/* If nothing was written to the destination, we found no address. */
	if (dst == odst)
		goto enoent;

	/* If prefix length overspecifies mantissa, life is bad. */
	if (((bits - (ipv6 ? 96 : 0)) / 8) > (dst - odst))
		goto enoent;

	/* Extend address to four octets. */
	while (size-- > 0U)
		*dst++ = 0;

	*pbits = bits;
	return (0);

 enoent:
	errno = ENOENT;
	return (-1);

 emsgsize:
	errno = EMSGSIZE;
	return (-1);
}

static int
inet_cidr_pton_ipv6(const char *src, u_char *dst, int *pbits) {
	static const char xdigits_l[] = "0123456789abcdef",
			  xdigits_u[] = "0123456789ABCDEF";
	u_char tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	int ch, saw_xdigit;
	u_int val;
	int bits;

	memset((tp = tmp), '\0', NS_IN6ADDRSZ);
	endp = tp + NS_IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':')
		if (*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	bits = -1;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			} else if (*src == '\0') {
				return (0);
			}
			if (tp + NS_INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
		    inet_cidr_pton_ipv4(curtok, tp, &bits, 1) == 0) {
			tp += NS_INADDRSZ;
			saw_xdigit = 0;
			break;	/*%< '\\0' was seen by inet_pton4(). */
		}
		if (ch == '/') {
			bits = getbits(src, 1);
			if (bits == -2)
				goto enoent;
			break;
		}
		goto enoent;
	}
	if (saw_xdigit) {
		if (tp + NS_INT16SZ > endp)
			goto emsgsize;
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		if (tp == endp)
			goto enoent;
		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}

	memcpy(dst, tmp, NS_IN6ADDRSZ);

	*pbits = bits;
	return (0);

 enoent:
	errno = ENOENT;
	return (-1);

 emsgsize:
	errno = EMSGSIZE;
	return (-1);
}

static int
getbits(const char *src, int ipv6) {
	int bits = 0;
	char *cp, ch;
	
	if (*src == '\0')			/*%< syntax */
		return (-2);
	do {
		ch = *src++;
		cp = strchr(digits, ch);
		if (cp == NULL)			/*%< syntax */
			return (-2);
		bits *= 10;
		bits += cp - digits;
		if (bits == 0 && *src != '\0')	/*%< no leading zeros */
			return (-2);
		if (bits > (ipv6 ? 128 : 32))	/*%< range error */
			return (-2);
	} while (*src != '\0');

	return (bits);
}

/*! \file */
