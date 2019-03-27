/*	$KAME: radix_mpath.c,v 1.17 2004/11/08 10:29:39 itojun Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2001 WIDE Project.
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
 * THE AUTHORS DO NOT GUARANTEE THAT THIS SOFTWARE DOES NOT INFRINGE
 * ANY OTHERS' INTELLECTUAL PROPERTIES. IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY INFRINGEMENT OF ANY OTHERS' INTELLECTUAL
 * PROPERTIES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/syslog.h>
#include <net/radix.h>
#include <net/radix_mpath.h>
#include <sys/rmlock.h>
#include <net/route.h>
#include <net/route_var.h>
#include <net/if.h>
#include <net/if_var.h>

/*
 * give some jitter to hash, to avoid synchronization between routers
 */
static uint32_t hashjitter;

int
rt_mpath_capable(struct rib_head *rnh)
{

	return rnh->rnh_multipath;
}

int
rn_mpath_capable(struct radix_head *rh)
{

	return (rt_mpath_capable((struct rib_head *)rh));
}

struct radix_node *
rn_mpath_next(struct radix_node *rn)
{
	struct radix_node *next;

	if (!rn->rn_dupedkey)
		return NULL;
	next = rn->rn_dupedkey;
	if (rn->rn_mask == next->rn_mask)
		return next;
	else
		return NULL;
}

uint32_t
rn_mpath_count(struct radix_node *rn)
{
	uint32_t i = 0;
	struct rtentry *rt;
	
	while (rn != NULL) {
		rt = (struct rtentry *)rn;
		i += rt->rt_weight;
		rn = rn_mpath_next(rn);
	}
	return (i);
}

struct rtentry *
rt_mpath_matchgate(struct rtentry *rt, struct sockaddr *gate)
{
	struct radix_node *rn;

	if (!gate || !rt->rt_gateway)
		return NULL;

	/* beyond here, we use rn as the master copy */
	rn = (struct radix_node *)rt;
	do {
		rt = (struct rtentry *)rn;
		/*
		 * we are removing an address alias that has 
		 * the same prefix as another address
		 * we need to compare the interface address because
		 * rt_gateway is a special sockadd_dl structure
		 */
		if (rt->rt_gateway->sa_family == AF_LINK) {
			if (!memcmp(rt->rt_ifa->ifa_addr, gate, gate->sa_len))
				break;
		}

		/*
		 * Check for other options:
		 * 1) Routes with 'real' IPv4/IPv6 gateway
		 * 2) Loopback host routes (another AF_LINK/sockadd_dl check)
		 * */
		if (rt->rt_gateway->sa_len == gate->sa_len &&
		    !memcmp(rt->rt_gateway, gate, gate->sa_len))
			break;
	} while ((rn = rn_mpath_next(rn)) != NULL);

	return (struct rtentry *)rn;
}

/* 
 * go through the chain and unlink "rt" from the list
 * the caller will free "rt"
 */
int
rt_mpath_deldup(struct rtentry *headrt, struct rtentry *rt)
{
        struct radix_node *t, *tt;

        if (!headrt || !rt)
            return (0);
        t = (struct radix_node *)headrt;
        tt = rn_mpath_next(t);
        while (tt) {
            if (tt == (struct radix_node *)rt) {
                t->rn_dupedkey = tt->rn_dupedkey;
                tt->rn_dupedkey = NULL;
    	        tt->rn_flags &= ~RNF_ACTIVE;
	        tt[1].rn_flags &= ~RNF_ACTIVE;
                return (1);
            }
            t = tt;
            tt = rn_mpath_next((struct radix_node *)t);
        }
        return (0);
}

/*
 * check if we have the same key/mask/gateway on the table already.
 * Assume @rt rt_key host bits are cleared according to @netmask
 */
int
rt_mpath_conflict(struct rib_head *rnh, struct rtentry *rt,
    struct sockaddr *netmask)
{
	struct radix_node *rn, *rn1;
	struct rtentry *rt1;

	rn = (struct radix_node *)rt;
	rn1 = rnh->rnh_lookup(rt_key(rt), netmask, &rnh->head);
	if (!rn1 || rn1->rn_flags & RNF_ROOT)
		return (0);

	/* key/mask are the same. compare gateway for all multipaths */
	do {
		rt1 = (struct rtentry *)rn1;

		/* sanity: no use in comparing the same thing */
		if (rn1 == rn)
			continue;
        
		if (rt1->rt_gateway->sa_family == AF_LINK) {
			if (rt1->rt_ifa->ifa_addr->sa_len != rt->rt_ifa->ifa_addr->sa_len ||
			    bcmp(rt1->rt_ifa->ifa_addr, rt->rt_ifa->ifa_addr, 
			    rt1->rt_ifa->ifa_addr->sa_len))
				continue;
		} else {
			if (rt1->rt_gateway->sa_len != rt->rt_gateway->sa_len ||
			    bcmp(rt1->rt_gateway, rt->rt_gateway,
			    rt1->rt_gateway->sa_len))
				continue;
		}

		/* all key/mask/gateway are the same.  conflicting entry. */
		return (EEXIST);
	} while ((rn1 = rn_mpath_next(rn1)) != NULL);

	return (0);
}

static struct rtentry *
rt_mpath_selectrte(struct rtentry *rte, uint32_t hash)
{
	struct radix_node *rn0, *rn;
	uint32_t total_weight;
	struct rtentry *rt;
	int64_t weight;

	/* beyond here, we use rn as the master copy */
	rn0 = rn = (struct radix_node *)rte;
	rt = rte;

	/* gw selection by Modulo-N Hash (RFC2991) XXX need improvement? */
	total_weight = rn_mpath_count(rn0);
	hash += hashjitter;
	hash %= total_weight;
	for (weight = abs((int32_t)hash);
	     rt != NULL && weight >= rt->rt_weight; 
	     weight -= (rt == NULL) ? 0 : rt->rt_weight) {
		
		/* stay within the multipath routes */
		if (rn->rn_dupedkey && rn->rn_mask != rn->rn_dupedkey->rn_mask)
			break;
		rn = rn->rn_dupedkey;
		rt = (struct rtentry *)rn;
	}

	return (rt);
}

struct rtentry *
rt_mpath_select(struct rtentry *rte, uint32_t hash)
{
	if (rn_mpath_next((struct radix_node *)rte) == NULL)
		return (rte);

	return (rt_mpath_selectrte(rte, hash));
}

void
rtalloc_mpath_fib(struct route *ro, uint32_t hash, u_int fibnum)
{
	struct rtentry *rt;

	/*
	 * XXX we don't attempt to lookup cached route again; what should
	 * be done for sendto(3) case?
	 */
	if (ro->ro_rt && ro->ro_rt->rt_ifp && (ro->ro_rt->rt_flags & RTF_UP)
	    && RT_LINK_IS_UP(ro->ro_rt->rt_ifp))
		return;				 
	ro->ro_rt = rtalloc1_fib(&ro->ro_dst, 1, 0, fibnum);

	/* if the route does not exist or it is not multipath, don't care */
	if (ro->ro_rt == NULL)
		return;
	if (rn_mpath_next((struct radix_node *)ro->ro_rt) == NULL) {
		RT_UNLOCK(ro->ro_rt);
		return;
	}

	rt = rt_mpath_selectrte(ro->ro_rt, hash);
	/* XXX try filling rt_gwroute and avoid unreachable gw  */

	/* gw selection has failed - there must be only zero weight routes */
	if (!rt) {
		RT_UNLOCK(ro->ro_rt);
		ro->ro_rt = NULL;
		return;
	}
	if (ro->ro_rt != rt) {
		RTFREE_LOCKED(ro->ro_rt);
		ro->ro_rt = rt;
		RT_LOCK(ro->ro_rt);
		RT_ADDREF(ro->ro_rt);

	} 
	RT_UNLOCK(ro->ro_rt);
}

extern int	in6_inithead(void **head, int off);
extern int	in_inithead(void **head, int off);

#ifdef INET
int
rn4_mpath_inithead(void **head, int off)
{
	struct rib_head *rnh;

	hashjitter = arc4random();
	if (in_inithead(head, off) == 1) {
		rnh = (struct rib_head *)*head;
		rnh->rnh_multipath = 1;
		return 1;
	} else
		return 0;
}
#endif

#ifdef INET6
int
rn6_mpath_inithead(void **head, int off)
{
	struct rib_head *rnh;

	hashjitter = arc4random();
	if (in6_inithead(head, off) == 1) {
		rnh = (struct rib_head *)*head;
		rnh->rnh_multipath = 1;
		return 1;
	} else
		return 0;
}

#endif
