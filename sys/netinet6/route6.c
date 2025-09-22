/*	$OpenBSD: route6.c,v 1.26 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: route6.c,v 1.22 2000/12/03 00:54:00 itojun Exp $	*/

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

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#include <netinet/icmp6.h>

/*
 * proto is unused
 */

int
route6_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct ip6_hdr *ip6;
	struct ip6_rthdr *rh;
	int off = *offp, rhlen;

	ip6 = mtod(*mp, struct ip6_hdr *);
	rh = ip6_exthdr_get(mp, off, sizeof(*rh));
	if (rh == NULL) {
		ip6stat_inc(ip6s_tooshort);
		return IPPROTO_DONE;
	}

	switch (rh->ip6r_type) {
	case IPV6_RTHDR_TYPE_0:
		/*
		 * RFC 5095 specifies to handle routing header type 0
		 * the same way as an unrecognised routing type.
		 */
		/* FALLTHROUGH */
	default:
		/* unknown routing type */
		if (rh->ip6r_segleft == 0) {
			rhlen = (rh->ip6r_len + 1) << 3;
			break;	/* Final dst. Just ignore the header. */
		}
		ip6stat_inc(ip6s_badoptions);
		icmp6_error(*mp, ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER,
			    (caddr_t)&rh->ip6r_type - (caddr_t)ip6);
		return (IPPROTO_DONE);
	}

	*offp += rhlen;
	return (rh->ip6r_nxt);
}
