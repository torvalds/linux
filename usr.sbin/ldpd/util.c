/*	$OpenBSD: util.c,v 1.7 2022/12/28 21:30:17 jmc Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <sys/types.h>
#include <string.h>

#include "ldpd.h"
#include "log.h"

uint8_t
mask2prefixlen(in_addr_t ina)
{
	if (ina == 0)
		return (0);
	else
		return (33 - ffs(ntohl(ina)));
}

uint8_t
mask2prefixlen6(struct sockaddr_in6 *sa_in6)
{
	unsigned int l = 0;
	uint8_t *ap, *ep;

	/*
	 * sin6_len is the size of the sockaddr so subtract the offset of
	 * the possibly truncated sin6_addr struct.
	 */
	ap = (uint8_t *)&sa_in6->sin6_addr;
	ep = (uint8_t *)sa_in6 + sa_in6->sin6_len;
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
		fatalx("inet6 prefixlen out of bound");
	return (l);
}

in_addr_t
prefixlen2mask(uint8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	return (htonl(0xffffffff << (32 - prefixlen)));
}

struct in6_addr *
prefixlen2mask6(uint8_t prefixlen)
{
	static struct in6_addr	mask;
	int			i;

	memset(&mask, 0, sizeof(mask));
	for (i = 0; i < prefixlen / 8; i++)
		mask.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		mask.s6_addr[prefixlen / 8] = 0xff00 >> i;

	return (&mask);
}

void
ldp_applymask(int af, union ldpd_addr *dest, const union ldpd_addr *src,
    int prefixlen)
{
	struct in6_addr	mask;
	int		i;

	switch (af) {
	case AF_INET:
		dest->v4.s_addr = src->v4.s_addr & prefixlen2mask(prefixlen);
		break;
	case AF_INET6:
		memset(&mask, 0, sizeof(mask));
		for (i = 0; i < prefixlen / 8; i++)
			mask.s6_addr[i] = 0xff;
		i = prefixlen % 8;
		if (i)
			mask.s6_addr[prefixlen / 8] = 0xff00 >> i;

		for (i = 0; i < 16; i++)
			dest->v6.s6_addr[i] = src->v6.s6_addr[i] &
			    mask.s6_addr[i];
		break;
	default:
		fatalx("ldp_applymask: unknown af");
	}
}

int
ldp_addrcmp(int af, const union ldpd_addr *a, const union ldpd_addr *b)
{
	switch (af) {
	case AF_INET:
		if (a->v4.s_addr == b->v4.s_addr)
			return (0);
		return ((ntohl(a->v4.s_addr) > ntohl(b->v4.s_addr)) ? 1 : -1);
	case AF_INET6:
		return (memcmp(&a->v6, &b->v6, sizeof(struct in6_addr)));
	default:
		fatalx("ldp_addrcmp: unknown af");
	}
}

int
ldp_addrisset(int af, const union ldpd_addr *addr)
{
	switch (af) {
	case AF_UNSPEC:
		return (0);
	case AF_INET:
		if (addr->v4.s_addr != INADDR_ANY)
			return (1);
		break;
	case AF_INET6:
		if (!IN6_IS_ADDR_UNSPECIFIED(&addr->v6))
			return (1);
		break;
	default:
		fatalx("ldp_addrisset: unknown af");
	}

	return (0);
}

int
ldp_prefixcmp(int af, const union ldpd_addr *a, const union ldpd_addr *b,
    uint8_t prefixlen)
{
	in_addr_t	mask, aa, ba;
	int		i;
	uint8_t		m;

	switch (af) {
	case AF_INET:
		if (prefixlen == 0)
			return (0);
		if (prefixlen > 32)
			fatalx("ldp_prefixcmp: bad IPv4 prefixlen");
		mask = htonl(prefixlen2mask(prefixlen));
		aa = htonl(a->v4.s_addr) & mask;
		ba = htonl(b->v4.s_addr) & mask;
		return (aa - ba);
	case AF_INET6:
		if (prefixlen == 0)
			return (0);
		if (prefixlen > 128)
			fatalx("ldp_prefixcmp: bad IPv6 prefixlen");
		for (i = 0; i < prefixlen / 8; i++)
			if (a->v6.s6_addr[i] != b->v6.s6_addr[i])
				return (a->v6.s6_addr[i] - b->v6.s6_addr[i]);
		i = prefixlen % 8;
		if (i) {
			m = 0xff00 >> i;
			if ((a->v6.s6_addr[prefixlen / 8] & m) !=
			    (b->v6.s6_addr[prefixlen / 8] & m))
				return ((a->v6.s6_addr[prefixlen / 8] & m) -
				    (b->v6.s6_addr[prefixlen / 8] & m));
		}
		return (0);
	default:
		fatalx("ldp_prefixcmp: unknown af");
	}
	return (-1);
}

int
bad_addr_v4(struct in_addr addr)
{
	uint32_t	 a = ntohl(addr.s_addr);

	if (((a >> IN_CLASSA_NSHIFT) == 0) ||
	    ((a >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) ||
	    IN_MULTICAST(a))
		return (1);

	return (0);
}

int
bad_addr_v6(struct in6_addr *addr)
{
	if (IN6_IS_ADDR_UNSPECIFIED(addr) ||
	    IN6_IS_ADDR_LOOPBACK(addr) ||
	    IN6_IS_ADDR_MULTICAST(addr) ||
	    IN6_IS_ADDR_SITELOCAL(addr) ||
	    IN6_IS_ADDR_V4MAPPED(addr) ||
	    IN6_IS_ADDR_V4COMPAT(addr))
		return (1);

	return (0);
}

int
bad_addr(int af, union ldpd_addr *addr)
{
	switch (af) {
	case AF_INET:
		return (bad_addr_v4(addr->v4));
	case AF_INET6:
		return (bad_addr_v6(&addr->v6));
	default:
		fatalx("bad_addr: unknown af");
	}
}

void
embedscope(struct sockaddr_in6 *sin6)
{
	uint16_t	 tmp16;

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
		memcpy(&tmp16, &sin6->sin6_addr.s6_addr[2], sizeof(tmp16));
		if (tmp16 != 0) {
			log_warnx("%s: address %s already has embedded scope %u",
			    __func__, log_sockaddr(sin6), ntohs(tmp16));
		}
		tmp16 = htons(sin6->sin6_scope_id);
		memcpy(&sin6->sin6_addr.s6_addr[2], &tmp16, sizeof(tmp16));
		sin6->sin6_scope_id = 0;
	}
}

void
recoverscope(struct sockaddr_in6 *sin6)
{
	uint16_t	 tmp16;

	if (sin6->sin6_scope_id != 0)
		log_warnx("%s: address %s already has scope id %u",
		    __func__, log_sockaddr(sin6), sin6->sin6_scope_id);

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
		memcpy(&tmp16, &sin6->sin6_addr.s6_addr[2], sizeof(tmp16));
		sin6->sin6_scope_id = ntohs(tmp16);
		sin6->sin6_addr.s6_addr[2] = 0;
		sin6->sin6_addr.s6_addr[3] = 0;
	}
}

void
addscope(struct sockaddr_in6 *sin6, uint32_t id)
{
	if (sin6->sin6_scope_id != 0)
		log_warnx("%s: address %s already has scope id %u", __func__,
		    log_sockaddr(sin6), sin6->sin6_scope_id);

	if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr))
		sin6->sin6_scope_id = id;
}

void
clearscope(struct in6_addr *in6)
{
	if (IN6_IS_SCOPE_EMBED(in6)) {
		in6->s6_addr[2] = 0;
		in6->s6_addr[3] = 0;
	}
}

struct sockaddr *
addr2sa(int af, union ldpd_addr *addr, uint16_t port)
{
	static struct sockaddr_storage	 ss;
	struct sockaddr_in		*sa_in = (struct sockaddr_in *)&ss;
	struct sockaddr_in6		*sa_in6 = (struct sockaddr_in6 *)&ss;

	memset(&ss, 0, sizeof(ss));
	switch (af) {
	case AF_INET:
		sa_in->sin_family = AF_INET;
		sa_in->sin_len = sizeof(struct sockaddr_in);
		sa_in->sin_addr = addr->v4;
		sa_in->sin_port = htons(port);
		break;
	case AF_INET6:
		sa_in6->sin6_family = AF_INET6;
		sa_in6->sin6_len = sizeof(struct sockaddr_in6);
		sa_in6->sin6_addr = addr->v6;
		sa_in6->sin6_port = htons(port);
		break;
	default:
		fatalx("addr2sa: unknown af");
	}

	return ((struct sockaddr *)&ss);
}

void
sa2addr(struct sockaddr *sa, int *af, union ldpd_addr *addr)
{
	struct sockaddr_in		*sa_in = (struct sockaddr_in *)sa;
	struct sockaddr_in6		*sa_in6 = (struct sockaddr_in6 *)sa;

	memset(addr, 0, sizeof(*addr));
	switch (sa->sa_family) {
	case AF_INET:
		*af = AF_INET;
		addr->v4 = sa_in->sin_addr;
		break;
	case AF_INET6:
		*af = AF_INET6;
		addr->v6 = sa_in6->sin6_addr;
		break;
	default:
		fatalx("sa2addr: unknown af");
	}
}
