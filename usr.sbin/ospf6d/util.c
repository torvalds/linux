/*	$OpenBSD: util.c,v 1.4 2022/12/28 21:30:18 jmc Exp $ */

/*
 * Copyright (c) 2012 Alexander Bluhm <bluhm@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
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

#include <netinet/in.h>
#include <string.h>

#include "ospf6d.h"
#include "log.h"

#define IN6_IS_SCOPE_EMBED(a)   \
	((IN6_IS_ADDR_LINKLOCAL(a)) ||  \
	 (IN6_IS_ADDR_MC_LINKLOCAL(a)) || \
	 (IN6_IS_ADDR_MC_INTFACELOCAL(a)))

void
embedscope(struct sockaddr_in6 *sin6)
{
	u_int16_t	 tmp16;

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
		bcopy(&sin6->sin6_addr.s6_addr[2], &tmp16, sizeof(tmp16));
		if (tmp16 != 0) {
			log_warnx("embedscope: address %s already has embedded "
			    "scope %u", log_sockaddr(sin6), ntohs(tmp16));
		}
		tmp16 = htons(sin6->sin6_scope_id);
		bcopy(&tmp16, &sin6->sin6_addr.s6_addr[2], sizeof(tmp16));
		sin6->sin6_scope_id = 0;
	}
}

void
recoverscope(struct sockaddr_in6 *sin6)
{
	u_int16_t	 tmp16;

	if (sin6->sin6_scope_id != 0) {
		log_warnx("recoverscope: address %s already has scope id %u",
		    log_sockaddr(sin6), sin6->sin6_scope_id);
	}

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
		bcopy(&sin6->sin6_addr.s6_addr[2], &tmp16, sizeof(tmp16));
		sin6->sin6_scope_id = ntohs(tmp16);
		sin6->sin6_addr.s6_addr[2] = 0;
		sin6->sin6_addr.s6_addr[3] = 0;
	}
}

void
addscope(struct sockaddr_in6 *sin6, u_int32_t id)
{
	if (sin6->sin6_scope_id != 0) {
		log_warnx("addscope: address %s already has scope id %u",
		    log_sockaddr(sin6), sin6->sin6_scope_id);
	}

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
		sin6->sin6_scope_id = id;
	}
}

void
clearscope(struct in6_addr *in6)
{
	if (IN6_IS_SCOPE_EMBED(in6)) {
		in6->s6_addr[2] = 0;
		in6->s6_addr[3] = 0;
	}
}

#undef IN6_IS_SCOPE_EMBED

u_int8_t
mask2prefixlen(struct sockaddr_in6 *sa_in6)
{
	u_int8_t	*ap, *ep;
	u_int		 l = 0;

	/*
	 * sin6_len is the size of the sockaddr so subtract the offset of
	 * the possibly truncated sin6_addr struct.
	 */
	ap = (u_int8_t *)&sa_in6->sin6_addr;
	ep = (u_int8_t *)sa_in6 + sa_in6->sin6_len;
	for (; ap < ep; ap++) {
		/* this "beauty" is adopted from sbin/route/show.c ... */
		switch (*ap) {
		case 0xff:
			l += 8;
			break;
		case 0xfe:
			l += 7;
			goto done;
		case 0xfc:
			l += 6;
			goto done;
		case 0xf8:
			l += 5;
			goto done;
		case 0xf0:
			l += 4;
			goto done;
		case 0xe0:
			l += 3;
			goto done;
		case 0xc0:
			l += 2;
			goto done;
		case 0x80:
			l += 1;
			goto done;
		case 0x00:
			goto done;
		default:
			fatalx("non contiguous inet6 netmask");
		}
	}

done:
	if (l > sizeof(struct in6_addr) * 8)
		fatalx("%s: prefixlen %d out of bound", __func__, l);
	return (l);
}

struct in6_addr *
prefixlen2mask(u_int8_t prefixlen)
{
	static struct in6_addr	mask;
	int			i;

	bzero(&mask, sizeof(mask));
	for (i = 0; i < prefixlen / 8; i++)
		mask.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		mask.s6_addr[prefixlen / 8] = 0xff00 >> i;

	return (&mask);
}

void
inet6applymask(struct in6_addr *dest, const struct in6_addr *src, int prefixlen)
{
	struct in6_addr	mask;
	int		i;

	bzero(&mask, sizeof(mask));
	for (i = 0; i < prefixlen / 8; i++)
		mask.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		mask.s6_addr[prefixlen / 8] = 0xff00 >> i;

	for (i = 0; i < 16; i++)
		dest->s6_addr[i] = src->s6_addr[i] & mask.s6_addr[i];
}
