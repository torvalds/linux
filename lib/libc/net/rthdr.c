/*	$KAME: rthdr.c,v 1.19 2003/06/06 10:48:51 itojun Exp $	*/

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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <string.h>
#include <stdio.h>

/*
 * RFC2292 API
 */

size_t
inet6_rthdr_space(int type, int seg)
{
	switch (type) {
	case IPV6_RTHDR_TYPE_0:
		if (seg < 1 || seg > 23)
			return (0);
#ifdef COMPAT_RFC2292
		return (CMSG_SPACE(sizeof(struct in6_addr) * (seg - 1) +
		    sizeof(struct ip6_rthdr0)));
#else
		return (CMSG_SPACE(sizeof(struct in6_addr) * seg +
		    sizeof(struct ip6_rthdr0)));
#endif 
	default:
		return (0);
	}
}

struct cmsghdr *
inet6_rthdr_init(void *bp, int type)
{
	struct cmsghdr *ch = (struct cmsghdr *)bp;
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(ch);

	ch->cmsg_level = IPPROTO_IPV6;
	ch->cmsg_type = IPV6_RTHDR;

	switch (type) {
	case IPV6_RTHDR_TYPE_0:
#ifdef COMPAT_RFC2292
		ch->cmsg_len = CMSG_LEN(sizeof(struct ip6_rthdr0) -
		    sizeof(struct in6_addr));
#else
		ch->cmsg_len = CMSG_LEN(sizeof(struct ip6_rthdr0));
#endif 

		bzero(rthdr, sizeof(struct ip6_rthdr0));
		rthdr->ip6r_type = IPV6_RTHDR_TYPE_0;
		return (ch);
	default:
		return (NULL);
	}
}

/* ARGSUSED */
int
inet6_rthdr_add(struct cmsghdr *cmsg, const struct in6_addr *addr, u_int flags)
{
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(cmsg);

	switch (rthdr->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
	{
		struct ip6_rthdr0 *rt0 = (struct ip6_rthdr0 *)rthdr;
		if (flags != IPV6_RTHDR_LOOSE && flags != IPV6_RTHDR_STRICT)
			return (-1);
		if (rt0->ip6r0_segleft == 23)
			return (-1);

#ifdef COMPAT_RFC1883		/* XXX */
		if (flags == IPV6_RTHDR_STRICT) {
			int c, b;
			c = rt0->ip6r0_segleft / 8;
			b = rt0->ip6r0_segleft % 8;
			rt0->ip6r0_slmap[c] |= (1 << (7 - b));
		}
#else
		if (flags != IPV6_RTHDR_LOOSE)
			return (-1);
#endif 
		rt0->ip6r0_segleft++;
		bcopy(addr, (caddr_t)rt0 + ((rt0->ip6r0_len + 1) << 3),
		    sizeof(struct in6_addr));
		rt0->ip6r0_len += sizeof(struct in6_addr) >> 3;
		cmsg->cmsg_len = CMSG_LEN((rt0->ip6r0_len + 1) << 3);
		break;
	}
	default:
		return (-1);
	}

	return (0);
}

/* ARGSUSED */
int
inet6_rthdr_lasthop(struct cmsghdr *cmsg, unsigned int flags)
{
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(cmsg);

	switch (rthdr->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
	{
		struct ip6_rthdr0 *rt0 = (struct ip6_rthdr0 *)rthdr;
#ifdef COMPAT_RFC1883		/* XXX */
		if (flags != IPV6_RTHDR_LOOSE && flags != IPV6_RTHDR_STRICT)
			return (-1);
#endif /* COMPAT_RFC1883 */
		if (rt0->ip6r0_segleft > 23)
			return (-1);
#ifdef COMPAT_RFC1883		/* XXX */
		if (flags == IPV6_RTHDR_STRICT) {
			int c, b;
			c = rt0->ip6r0_segleft / 8;
			b = rt0->ip6r0_segleft % 8;
			rt0->ip6r0_slmap[c] |= (1 << (7 - b));
		}
#else
		if (flags != IPV6_RTHDR_LOOSE)
			return (-1);
#endif /* COMPAT_RFC1883 */
		break;
	}
	default:
		return (-1);
	}

	return (0);
}

#if 0
int
inet6_rthdr_reverse(const struct cmsghdr *in, struct cmsghdr *out)
{

	return (-1);
}
#endif

int
inet6_rthdr_segments(const struct cmsghdr *cmsg)
{
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(cmsg);

	switch (rthdr->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
	{
		struct ip6_rthdr0 *rt0 = (struct ip6_rthdr0 *)rthdr;

		if (rt0->ip6r0_len % 2 || 46 < rt0->ip6r0_len)
			return (-1);

		return (rt0->ip6r0_len * 8) / sizeof(struct in6_addr);
	}

	default:
		return (-1);
	}
}

struct in6_addr *
inet6_rthdr_getaddr(struct cmsghdr *cmsg, int idx)
{
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(cmsg);

	switch (rthdr->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
	{
		struct ip6_rthdr0 *rt0 = (struct ip6_rthdr0 *)rthdr;
		int naddr;

		if (rt0->ip6r0_len % 2 || 46 < rt0->ip6r0_len)
			return NULL;
		naddr = (rt0->ip6r0_len * 8) / sizeof(struct in6_addr);
		if (idx <= 0 || naddr < idx)
			return NULL;
#ifdef COMPAT_RFC2292
		return (((struct in6_addr *)(rt0 + 1)) + idx - 1);
#else
		return (((struct in6_addr *)(rt0 + 1)) + idx);
#endif
	}

	default:
		return NULL;
	}
}

int
inet6_rthdr_getflags(const struct cmsghdr *cmsg, int idx)
{
	struct ip6_rthdr *rthdr;

	rthdr = (struct ip6_rthdr *)CMSG_DATA(cmsg);

	switch (rthdr->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
	{
		struct ip6_rthdr0 *rt0 = (struct ip6_rthdr0 *)rthdr;
		int naddr;

		if (rt0->ip6r0_len % 2 || 46 < rt0->ip6r0_len)
			return (-1);
		naddr = (rt0->ip6r0_len * 8) / sizeof(struct in6_addr);
		if (idx < 0 || naddr < idx)
			return (-1);
#ifdef COMPAT_RFC1883		/* XXX */
		if (rt0->ip6r0_slmap[idx / 8] & (0x80 >> (idx % 8)))
			return IPV6_RTHDR_STRICT;
		else
			return IPV6_RTHDR_LOOSE;
#else
		return IPV6_RTHDR_LOOSE;
#endif /* COMPAT_RFC1883 */
	}

	default:
		return (-1);
	}
}

/*
 * RFC3542 API
 */

socklen_t
inet6_rth_space(int type, int segments)
{
	switch (type) {
	case IPV6_RTHDR_TYPE_0:
		if ((segments >= 0) && (segments <= 127))
			return (((segments * 2) + 1) << 3);
		/* FALLTHROUGH */
	default:
		return (0);	/* type not suppported */
	}
}

void *
inet6_rth_init(void *bp, socklen_t bp_len, int type, int segments)
{
	struct ip6_rthdr *rth = (struct ip6_rthdr *)bp;
	struct ip6_rthdr0 *rth0;

	switch (type) {
	case IPV6_RTHDR_TYPE_0:
		/* length validation */
		if (bp_len < inet6_rth_space(IPV6_RTHDR_TYPE_0, segments))
			return (NULL);
		/* segment validation */
		if ((segments < 0) || (segments > 127))
			return (NULL);

		memset(bp, 0, bp_len);
		rth0 = (struct ip6_rthdr0 *)rth;
		rth0->ip6r0_len = segments * 2;
		rth0->ip6r0_type = IPV6_RTHDR_TYPE_0;
		rth0->ip6r0_segleft = 0;
		rth0->ip6r0_reserved = 0;
		break;
	default:
		return (NULL);	/* type not supported */
	}

	return (bp);
}

int
inet6_rth_add(void *bp, const struct in6_addr *addr)
{
	struct ip6_rthdr *rth = (struct ip6_rthdr *)bp;
	struct ip6_rthdr0 *rth0;
	struct in6_addr *nextaddr;

	switch (rth->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
		rth0 = (struct ip6_rthdr0 *)rth;
		/* Don't exceed the number of stated segments */
		if (rth0->ip6r0_segleft == (rth0->ip6r0_len / 2))
			return (-1);
		nextaddr = (struct in6_addr *)(rth0 + 1) + rth0->ip6r0_segleft;
		*nextaddr = *addr;
		rth0->ip6r0_segleft++;
		break;
	default:
		return (-1);	/* type not supported */
	}

	return (0);
}

int
inet6_rth_reverse(const void *in, void *out)
{
	struct ip6_rthdr *rth_in = (struct ip6_rthdr *)in;
	struct ip6_rthdr0 *rth0_in, *rth0_out;
	int i, segments;

	switch (rth_in->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
		rth0_in = (struct ip6_rthdr0 *)in;
		rth0_out = (struct ip6_rthdr0 *)out;

		/* parameter validation XXX too paranoid? */
		if (rth0_in->ip6r0_len % 2)
			return (-1);
		segments = rth0_in->ip6r0_len / 2;

		/* we can't use memcpy here, since in and out may overlap */
		memmove((void *)rth0_out, (void *)rth0_in,
			((rth0_in->ip6r0_len) + 1) << 3);
		rth0_out->ip6r0_segleft = segments;

		/* reverse the addresses */
		for (i = 0; i < segments / 2; i++) {
			struct in6_addr addr_tmp, *addr1, *addr2;

			addr1 = (struct in6_addr *)(rth0_out + 1) + i;
			addr2 = (struct in6_addr *)(rth0_out + 1) +
				(segments - i - 1);
			addr_tmp = *addr1;
			*addr1 = *addr2;
			*addr2 = addr_tmp;
		}

		break;
	default:
		return (-1);	/* type not supported */
	}

	return (0);
}

int
inet6_rth_segments(const void *bp)
{
	struct ip6_rthdr *rh = (struct ip6_rthdr *)bp;
	struct ip6_rthdr0 *rh0;
	int addrs;

	switch (rh->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
		rh0 = (struct ip6_rthdr0 *)bp;

		/*
		 * Validation for a type-0 routing header.
		 * Is this too strict?
		 */
		if ((rh0->ip6r0_len % 2) != 0 ||
		    (addrs = (rh0->ip6r0_len >> 1)) < rh0->ip6r0_segleft)
			return (-1);

		return (addrs);
	default:
		return (-1);	/* unknown type */
	}
}

struct in6_addr *
inet6_rth_getaddr(const void *bp, int idx)
{
	struct ip6_rthdr *rh = (struct ip6_rthdr *)bp;
	struct ip6_rthdr0 *rh0;
	int addrs;

	switch (rh->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
		 rh0 = (struct ip6_rthdr0 *)bp;

		/*
		 * Validation for a type-0 routing header.
		 * Is this too strict?
		 */
		if ((rh0->ip6r0_len % 2) != 0 ||
		    (addrs = (rh0->ip6r0_len >> 1)) < rh0->ip6r0_segleft)
			return (NULL);

		if (idx < 0 || addrs <= idx)
			return (NULL);

		return (((struct in6_addr *)(rh0 + 1)) + idx);
	default:
		return (NULL);	/* unknown type */
		break;
	}
}
