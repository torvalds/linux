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
 *
 *	$KAME: in6_rmx.c,v 1.11 2001/07/26 06:53:16 jinmei Exp $
 */

/*-
 * Copyright 1994, 1995 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/rwlock.h>
#include <sys/syslog.h>
#include <sys/callout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/route_var.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/in_var.h>

#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

#include <netinet/icmp6.h>
#include <netinet6/nd6.h>

#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

extern int	in6_inithead(void **head, int off);
#ifdef VIMAGE
extern int	in6_detachhead(void **head, int off);
#endif

/*
 * Do what we need to do when inserting a route.
 */
static struct radix_node *
in6_addroute(void *v_arg, void *n_arg, struct radix_head *head,
    struct radix_node *treenodes)
{
	struct rtentry *rt = (struct rtentry *)treenodes;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)rt_key(rt);

	if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
		rt->rt_flags |= RTF_MULTICAST;

	/*
	 * A little bit of help for both IPv6 output and input:
	 *   For local addresses, we make sure that RTF_LOCAL is set,
	 *   with the thought that this might one day be used to speed up
	 *   ip_input().
	 *
	 * We also mark routes to multicast addresses as such, because
	 * it's easy to do and might be useful (but this is much more
	 * dubious since it's so easy to inspect the address).  (This
	 * is done above.)
	 *
	 * XXX
	 * should elaborate the code.
	 */
	if (rt->rt_flags & RTF_HOST) {
		if (IN6_ARE_ADDR_EQUAL(&satosin6(rt->rt_ifa->ifa_addr)
					->sin6_addr,
				       &sin6->sin6_addr)) {
			rt->rt_flags |= RTF_LOCAL;
		}
	}

	if (rt->rt_ifp != NULL) {

		/*
		 * Check route MTU:
		 * inherit interface MTU if not set or
		 * check if MTU is too large.
		 */
		if (rt->rt_mtu == 0) {
			rt->rt_mtu = IN6_LINKMTU(rt->rt_ifp);
		} else if (rt->rt_mtu > IN6_LINKMTU(rt->rt_ifp))
			rt->rt_mtu = IN6_LINKMTU(rt->rt_ifp);
	}

	return (rn_addroute(v_arg, n_arg, head, treenodes));
}

/*
 * Age old PMTUs.
 */
struct mtuex_arg {
	struct rib_head *rnh;
	time_t nextstop;
};
VNET_DEFINE_STATIC(struct callout, rtq_mtutimer);
#define	V_rtq_mtutimer			VNET(rtq_mtutimer)

static int
in6_mtuexpire(struct rtentry *rt, void *rock)
{
	struct mtuex_arg *ap = rock;

	if (rt->rt_expire && !(rt->rt_flags & RTF_PROBEMTU)) {
		if (rt->rt_expire <= time_uptime) {
			rt->rt_flags |= RTF_PROBEMTU;
		} else {
			ap->nextstop = lmin(ap->nextstop, rt->rt_expire);
		}
	}

	return (0);
}

#define	MTUTIMO_DEFAULT	(60*1)

static void
in6_mtutimo_setwa(struct rib_head *rnh, uint32_t fibum, int af,
    void *_arg)
{
	struct mtuex_arg *arg;

	arg = (struct mtuex_arg *)_arg;

	arg->rnh = rnh;
}

static void
in6_mtutimo(void *rock)
{
	CURVNET_SET_QUIET((struct vnet *) rock);
	struct timeval atv;
	struct mtuex_arg arg;

	rt_foreach_fib_walk(AF_INET6, in6_mtutimo_setwa, in6_mtuexpire, &arg);

	atv.tv_sec = MTUTIMO_DEFAULT;
	atv.tv_usec = 0;
	callout_reset(&V_rtq_mtutimer, tvtohz(&atv), in6_mtutimo, rock);
	CURVNET_RESTORE();
}

/*
 * Initialize our routing tree.
 */
VNET_DEFINE_STATIC(int, _in6_rt_was_here);
#define	V__in6_rt_was_here	VNET(_in6_rt_was_here)

int
in6_inithead(void **head, int off)
{
	struct rib_head *rh;

	rh = rt_table_init(offsetof(struct sockaddr_in6, sin6_addr) << 3);
	if (rh == NULL)
		return (0);

	rh->rnh_addaddr = in6_addroute;
	*head = (void *)rh;

	if (V__in6_rt_was_here == 0) {
		callout_init(&V_rtq_mtutimer, 1);
		in6_mtutimo(curvnet);	/* kick off timeout first time */
		V__in6_rt_was_here = 1;
	}

	return (1);
}

#ifdef VIMAGE
int
in6_detachhead(void **head, int off)
{

	callout_drain(&V_rtq_mtutimer);
	rt_table_destroy((struct rib_head *)(*head));

	return (1);
}
#endif

/*
 * Extended API for IPv6 FIB support.
 */
void
in6_rtredirect(struct sockaddr *dst, struct sockaddr *gw, struct sockaddr *nm,
    int flags, struct sockaddr *src, u_int fibnum)
{

	rtredirect_fib(dst, gw, nm, flags, src, fibnum);
}

int
in6_rtrequest(int req, struct sockaddr *dst, struct sockaddr *gw,
    struct sockaddr *mask, int flags, struct rtentry **ret_nrt, u_int fibnum)
{

	return (rtrequest_fib(req, dst, gw, mask, flags, ret_nrt, fibnum));
}

void
in6_rtalloc(struct route_in6 *ro, u_int fibnum)
{

	rtalloc_ign_fib((struct route *)ro, 0ul, fibnum);
}

void
in6_rtalloc_ign(struct route_in6 *ro, u_long ignflags, u_int fibnum)
{

	rtalloc_ign_fib((struct route *)ro, ignflags, fibnum);
}

struct rtentry *
in6_rtalloc1(struct sockaddr *dst, int report, u_long ignflags, u_int fibnum)
{

	return (rtalloc1_fib(dst, report, ignflags, fibnum));
}
