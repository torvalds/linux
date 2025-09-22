/*	$OpenBSD: rthdr.c,v 1.13 2022/12/27 17:10:06 jmc Exp $	*/
/*	$KAME: rthdr.c,v 1.22 2006/02/09 08:18:58 keiichi Exp $	*/

/*
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <string.h>
#include <stdio.h>

/*
 * RFC3542 (2292bis) API
 */

socklen_t
inet6_rth_space(int type, int segments)
{
	switch (type) {
	case IPV6_RTHDR_TYPE_0:
		return (((segments * 2) + 1) << 3);
	default:
		return (0);	/* type not supported */
	}
}
DEF_WEAK(inet6_rth_space);

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
		memmove(rth0_out, rth0_in, ((rth0_in->ip6r0_len) + 1) << 3);
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
