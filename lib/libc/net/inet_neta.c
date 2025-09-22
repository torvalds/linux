/*	$OpenBSD: inet_neta.c,v 1.7 2005/08/06 20:30:03 espie Exp $	*/

/*
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

/*
 * char *
 * inet_neta(src, dst, size)
 *	format an in_addr_t network number into presentation format.
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	format of ``src'' is as for inet_network().
 * author:
 *	Paul Vixie (ISC), July 1996
 */
char *
inet_neta(in_addr_t src, char *dst, size_t size)
{
	char *odst = dst;
	char *ep;
	int advance;

	if (src == 0x00000000) {
		if (size < sizeof "0.0.0.0")
			goto emsgsize;
		strlcpy(dst, "0.0.0.0", size);
		return dst;
	}
	ep = dst + size;
	if (ep <= dst)
		goto emsgsize;
	while (src & 0xffffffff) {
		u_char b = (src & 0xff000000) >> 24;

		src <<= 8;
		if (b || src) {
			if (ep - dst < sizeof "255.")
				goto emsgsize;
			advance = snprintf(dst, ep - dst, "%u", b);
			if (advance <= 0 || advance >= ep - dst)
				goto emsgsize;
			dst += advance;
			if (src != 0L) {
				if (dst + 1 >= ep)
					goto emsgsize;
				*dst++ = '.';
				*dst = '\0';
			}
		}
	}
	return (odst);

 emsgsize:
	errno = EMSGSIZE;
	return (NULL);
}
