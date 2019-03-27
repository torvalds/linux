/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)route.c	8.3.1.1 (Berkeley) 2/23/95
 * $FreeBSD$
 */
/************************************************************************
 * Note: In this file a 'fib' is a "forwarding information base"	*
 * Which is the new name for an in kernel routing (next hop) table.	*
 ***********************************************************************/

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_route.h"
#include "opt_sctp.h"
#include "opt_mrouting.h"
#include "opt_mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/sysproto.h>
#include <sys/proc.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rmlock.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/route_var.h>
#include <net/vnet.h>

#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif

#include <netinet/in.h>
#include <netinet/ip_mroute.h>

#include <vm/uma.h>

#define	RT_MAXFIBS	UINT16_MAX

/* Kernel config default option. */
#ifdef ROUTETABLES
#if ROUTETABLES <= 0
#error "ROUTETABLES defined too low"
#endif
#if ROUTETABLES > RT_MAXFIBS
#error "ROUTETABLES defined too big"
#endif
#define	RT_NUMFIBS	ROUTETABLES
#endif /* ROUTETABLES */
/* Initialize to default if not otherwise set. */
#ifndef	RT_NUMFIBS
#define	RT_NUMFIBS	1
#endif

#if defined(INET) || defined(INET6)
#ifdef SCTP
extern void sctp_addr_change(struct ifaddr *ifa, int cmd);
#endif /* SCTP */
#endif


/* This is read-only.. */
u_int rt_numfibs = RT_NUMFIBS;
SYSCTL_UINT(_net, OID_AUTO, fibs, CTLFLAG_RDTUN, &rt_numfibs, 0, "");

/*
 * By default add routes to all fibs for new interfaces.
 * Once this is set to 0 then only allocate routes on interface
 * changes for the FIB of the caller when adding a new set of addresses
 * to an interface.  XXX this is a shotgun aproach to a problem that needs
 * a more fine grained solution.. that will come.
 * XXX also has the problems getting the FIB from curthread which will not
 * always work given the fib can be overridden and prefixes can be added
 * from the network stack context.
 */
VNET_DEFINE(u_int, rt_add_addr_allfibs) = 1;
SYSCTL_UINT(_net, OID_AUTO, add_addr_allfibs, CTLFLAG_RWTUN | CTLFLAG_VNET,
    &VNET_NAME(rt_add_addr_allfibs), 0, "");

VNET_DEFINE(struct rtstat, rtstat);
#define	V_rtstat	VNET(rtstat)

VNET_DEFINE(struct rib_head *, rt_tables);
#define	V_rt_tables	VNET(rt_tables)

VNET_DEFINE(int, rttrash);		/* routes not in table but not freed */
#define	V_rttrash	VNET(rttrash)


/*
 * Convert a 'struct radix_node *' to a 'struct rtentry *'.
 * The operation can be done safely (in this code) because a
 * 'struct rtentry' starts with two 'struct radix_node''s, the first
 * one representing leaf nodes in the routing tree, which is
 * what the code in radix.c passes us as a 'struct radix_node'.
 *
 * But because there are a lot of assumptions in this conversion,
 * do not cast explicitly, but always use the macro below.
 */
#define RNTORT(p)	((struct rtentry *)(p))

VNET_DEFINE_STATIC(uma_zone_t, rtzone);		/* Routing table UMA zone. */
#define	V_rtzone	VNET(rtzone)

static int rtrequest1_fib_change(struct rib_head *, struct rt_addrinfo *,
    struct rtentry **, u_int);
static void rt_setmetrics(const struct rt_addrinfo *, struct rtentry *);
static int rt_ifdelroute(const struct rtentry *rt, void *arg);
static struct rtentry *rt_unlinkrte(struct rib_head *rnh,
    struct rt_addrinfo *info, int *perror);
static void rt_notifydelete(struct rtentry *rt, struct rt_addrinfo *info);
#ifdef RADIX_MPATH
static struct radix_node *rt_mpath_unlink(struct rib_head *rnh,
    struct rt_addrinfo *info, struct rtentry *rto, int *perror);
#endif
static int rt_exportinfo(struct rtentry *rt, struct rt_addrinfo *info,
    int flags);

struct if_mtuinfo
{
	struct ifnet	*ifp;
	int		mtu;
};

static int	if_updatemtu_cb(struct radix_node *, void *);

/*
 * handler for net.my_fibnum
 */
static int
sysctl_my_fibnum(SYSCTL_HANDLER_ARGS)
{
        int fibnum;
        int error;
 
        fibnum = curthread->td_proc->p_fibnum;
        error = sysctl_handle_int(oidp, &fibnum, 0, req);
        return (error);
}

SYSCTL_PROC(_net, OID_AUTO, my_fibnum, CTLTYPE_INT|CTLFLAG_RD,
            NULL, 0, &sysctl_my_fibnum, "I", "default FIB of caller");

static __inline struct rib_head **
rt_tables_get_rnh_ptr(int table, int fam)
{
	struct rib_head **rnh;

	KASSERT(table >= 0 && table < rt_numfibs, ("%s: table out of bounds.",
	    __func__));
	KASSERT(fam >= 0 && fam < (AF_MAX+1), ("%s: fam out of bounds.",
	    __func__));

	/* rnh is [fib=0][af=0]. */
	rnh = (struct rib_head **)V_rt_tables;
	/* Get the offset to the requested table and fam. */
	rnh += table * (AF_MAX+1) + fam;

	return (rnh);
}

struct rib_head *
rt_tables_get_rnh(int table, int fam)
{

	return (*rt_tables_get_rnh_ptr(table, fam));
}

u_int
rt_tables_get_gen(int table, int fam)
{
	struct rib_head *rnh;

	rnh = *rt_tables_get_rnh_ptr(table, fam);
	KASSERT(rnh != NULL, ("%s: NULL rib_head pointer table %d fam %d",
	    __func__, table, fam));
	return (rnh->rnh_gen);
}


/*
 * route initialization must occur before ip6_init2(), which happenas at
 * SI_ORDER_MIDDLE.
 */
static void
route_init(void)
{

	/* whack the tunable ints into  line. */
	if (rt_numfibs > RT_MAXFIBS)
		rt_numfibs = RT_MAXFIBS;
	if (rt_numfibs == 0)
		rt_numfibs = 1;
}
SYSINIT(route_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, route_init, NULL);

static int
rtentry_zinit(void *mem, int size, int how)
{
	struct rtentry *rt = mem;

	rt->rt_pksent = counter_u64_alloc(how);
	if (rt->rt_pksent == NULL)
		return (ENOMEM);

	RT_LOCK_INIT(rt);

	return (0);
}

static void
rtentry_zfini(void *mem, int size)
{
	struct rtentry *rt = mem;

	RT_LOCK_DESTROY(rt);
	counter_u64_free(rt->rt_pksent);
}

static int
rtentry_ctor(void *mem, int size, void *arg, int how)
{
	struct rtentry *rt = mem;

	bzero(rt, offsetof(struct rtentry, rt_endzero));
	counter_u64_zero(rt->rt_pksent);
	rt->rt_chain = NULL;

	return (0);
}

static void
rtentry_dtor(void *mem, int size, void *arg)
{
	struct rtentry *rt = mem;

	RT_UNLOCK_COND(rt);
}

static void
vnet_route_init(const void *unused __unused)
{
	struct domain *dom;
	struct rib_head **rnh;
	int table;
	int fam;

	V_rt_tables = malloc(rt_numfibs * (AF_MAX+1) *
	    sizeof(struct rib_head *), M_RTABLE, M_WAITOK|M_ZERO);

	V_rtzone = uma_zcreate("rtentry", sizeof(struct rtentry),
	    rtentry_ctor, rtentry_dtor,
	    rtentry_zinit, rtentry_zfini, UMA_ALIGN_PTR, 0);
	for (dom = domains; dom; dom = dom->dom_next) {
		if (dom->dom_rtattach == NULL)
			continue;

		for  (table = 0; table < rt_numfibs; table++) {
			fam = dom->dom_family;
			if (table != 0 && fam != AF_INET6 && fam != AF_INET)
				break;

			rnh = rt_tables_get_rnh_ptr(table, fam);
			if (rnh == NULL)
				panic("%s: rnh NULL", __func__);
			dom->dom_rtattach((void **)rnh, 0);
		}
	}
}
VNET_SYSINIT(vnet_route_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_FOURTH,
    vnet_route_init, 0);

#ifdef VIMAGE
static void
vnet_route_uninit(const void *unused __unused)
{
	int table;
	int fam;
	struct domain *dom;
	struct rib_head **rnh;

	for (dom = domains; dom; dom = dom->dom_next) {
		if (dom->dom_rtdetach == NULL)
			continue;

		for (table = 0; table < rt_numfibs; table++) {
			fam = dom->dom_family;

			if (table != 0 && fam != AF_INET6 && fam != AF_INET)
				break;

			rnh = rt_tables_get_rnh_ptr(table, fam);
			if (rnh == NULL)
				panic("%s: rnh NULL", __func__);
			dom->dom_rtdetach((void **)rnh, 0);
		}
	}

	free(V_rt_tables, M_RTABLE);
	uma_zdestroy(V_rtzone);
}
VNET_SYSUNINIT(vnet_route_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_FIRST,
    vnet_route_uninit, 0);
#endif

struct rib_head *
rt_table_init(int offset)
{
	struct rib_head *rh;

	rh = malloc(sizeof(struct rib_head), M_RTABLE, M_WAITOK | M_ZERO);

	/* TODO: These details should be hidded inside radix.c */
	/* Init masks tree */
	rn_inithead_internal(&rh->head, rh->rnh_nodes, offset);
	rn_inithead_internal(&rh->rmhead.head, rh->rmhead.mask_nodes, 0);
	rh->head.rnh_masks = &rh->rmhead;

	/* Init locks */
	RIB_LOCK_INIT(rh);

	/* Finally, set base callbacks */
	rh->rnh_addaddr = rn_addroute;
	rh->rnh_deladdr = rn_delete;
	rh->rnh_matchaddr = rn_match;
	rh->rnh_lookup = rn_lookup;
	rh->rnh_walktree = rn_walktree;
	rh->rnh_walktree_from = rn_walktree_from;

	return (rh);
}

static int
rt_freeentry(struct radix_node *rn, void *arg)
{
	struct radix_head * const rnh = arg;
	struct radix_node *x;

	x = (struct radix_node *)rn_delete(rn + 2, NULL, rnh);
	if (x != NULL)
		R_Free(x);
	return (0);
}

void
rt_table_destroy(struct rib_head *rh)
{

	rn_walktree(&rh->rmhead.head, rt_freeentry, &rh->rmhead.head);

	/* Assume table is already empty */
	RIB_LOCK_DESTROY(rh);
	free(rh, M_RTABLE);
}


#ifndef _SYS_SYSPROTO_H_
struct setfib_args {
	int     fibnum;
};
#endif
int
sys_setfib(struct thread *td, struct setfib_args *uap)
{
	if (uap->fibnum < 0 || uap->fibnum >= rt_numfibs)
		return EINVAL;
	td->td_proc->p_fibnum = uap->fibnum;
	return (0);
}

/*
 * Packet routing routines.
 */
void
rtalloc_ign_fib(struct route *ro, u_long ignore, u_int fibnum)
{
	struct rtentry *rt;

	if ((rt = ro->ro_rt) != NULL) {
		if (rt->rt_ifp != NULL && rt->rt_flags & RTF_UP)
			return;
		RTFREE(rt);
		ro->ro_rt = NULL;
	}
	ro->ro_rt = rtalloc1_fib(&ro->ro_dst, 1, ignore, fibnum);
	if (ro->ro_rt)
		RT_UNLOCK(ro->ro_rt);
}

/*
 * Look up the route that matches the address given
 * Or, at least try.. Create a cloned route if needed.
 *
 * The returned route, if any, is locked.
 */
struct rtentry *
rtalloc1(struct sockaddr *dst, int report, u_long ignflags)
{

	return (rtalloc1_fib(dst, report, ignflags, RT_DEFAULT_FIB));
}

struct rtentry *
rtalloc1_fib(struct sockaddr *dst, int report, u_long ignflags,
		    u_int fibnum)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct rtentry *newrt;
	struct rt_addrinfo info;
	int err = 0, msgtype = RTM_MISS;

	KASSERT((fibnum < rt_numfibs), ("rtalloc1_fib: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, dst->sa_family);
	newrt = NULL;
	if (rh == NULL)
		goto miss;

	/*
	 * Look up the address in the table for that Address Family
	 */
	if ((ignflags & RTF_RNH_LOCKED) == 0)
		RIB_RLOCK(rh);
#ifdef INVARIANTS
	else
		RIB_LOCK_ASSERT(rh);
#endif
	rn = rh->rnh_matchaddr(dst, &rh->head);
	if (rn && ((rn->rn_flags & RNF_ROOT) == 0)) {
		newrt = RNTORT(rn);
		RT_LOCK(newrt);
		RT_ADDREF(newrt);
		if ((ignflags & RTF_RNH_LOCKED) == 0)
			RIB_RUNLOCK(rh);
		return (newrt);

	} else if ((ignflags & RTF_RNH_LOCKED) == 0)
		RIB_RUNLOCK(rh);
	/*
	 * Either we hit the root or could not find any match,
	 * which basically means: "cannot get there from here".
	 */
miss:
	V_rtstat.rts_unreach++;

	if (report) {
		/*
		 * If required, report the failure to the supervising
		 * Authorities.
		 * For a delete, this is not an error. (report == 0)
		 */
		bzero(&info, sizeof(info));
		info.rti_info[RTAX_DST] = dst;
		rt_missmsg_fib(msgtype, &info, 0, err, fibnum);
	}
	return (newrt);
}

/*
 * Remove a reference count from an rtentry.
 * If the count gets low enough, take it out of the routing table
 */
void
rtfree(struct rtentry *rt)
{
	struct rib_head *rnh;

	KASSERT(rt != NULL,("%s: NULL rt", __func__));
	rnh = rt_tables_get_rnh(rt->rt_fibnum, rt_key(rt)->sa_family);
	KASSERT(rnh != NULL,("%s: NULL rnh", __func__));

	RT_LOCK_ASSERT(rt);

	/*
	 * The callers should use RTFREE_LOCKED() or RTFREE(), so
	 * we should come here exactly with the last reference.
	 */
	RT_REMREF(rt);
	if (rt->rt_refcnt > 0) {
		log(LOG_DEBUG, "%s: %p has %d refs\n", __func__, rt, rt->rt_refcnt);
		goto done;
	}

	/*
	 * On last reference give the "close method" a chance
	 * to cleanup private state.  This also permits (for
	 * IPv4 and IPv6) a chance to decide if the routing table
	 * entry should be purged immediately or at a later time.
	 * When an immediate purge is to happen the close routine
	 * typically calls rtexpunge which clears the RTF_UP flag
	 * on the entry so that the code below reclaims the storage.
	 */
	if (rt->rt_refcnt == 0 && rnh->rnh_close)
		rnh->rnh_close((struct radix_node *)rt, &rnh->head);

	/*
	 * If we are no longer "up" (and ref == 0)
	 * then we can free the resources associated
	 * with the route.
	 */
	if ((rt->rt_flags & RTF_UP) == 0) {
		if (rt->rt_nodes->rn_flags & (RNF_ACTIVE | RNF_ROOT))
			panic("rtfree 2");
		/*
		 * the rtentry must have been removed from the routing table
		 * so it is represented in rttrash.. remove that now.
		 */
		V_rttrash--;
#ifdef	DIAGNOSTIC
		if (rt->rt_refcnt < 0) {
			printf("rtfree: %p not freed (neg refs)\n", rt);
			goto done;
		}
#endif
		/*
		 * release references on items we hold them on..
		 * e.g other routes and ifaddrs.
		 */
		if (rt->rt_ifa)
			ifa_free(rt->rt_ifa);
		/*
		 * The key is separatly alloc'd so free it (see rt_setgate()).
		 * This also frees the gateway, as they are always malloc'd
		 * together.
		 */
		R_Free(rt_key(rt));

		/*
		 * and the rtentry itself of course
		 */
		uma_zfree(V_rtzone, rt);
		return;
	}
done:
	RT_UNLOCK(rt);
}


/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 */
void
rtredirect_fib(struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct sockaddr *src,
	u_int fibnum)
{
	struct rtentry *rt;
	int error = 0;
	short *stat = NULL;
	struct rt_addrinfo info;
	struct epoch_tracker et;
	struct ifaddr *ifa;
	struct rib_head *rnh;

	ifa = NULL;
	NET_EPOCH_ENTER(et);
	rnh = rt_tables_get_rnh(fibnum, dst->sa_family);
	if (rnh == NULL) {
		error = EAFNOSUPPORT;
		goto out;
	}
	/* verify the gateway is directly reachable */
	if ((ifa = ifa_ifwithnet(gateway, 0, fibnum)) == NULL) {
		error = ENETUNREACH;
		goto out;
	}
	rt = rtalloc1_fib(dst, 0, 0UL, fibnum);	/* NB: rt is locked */
	/*
	 * If the redirect isn't from our current router for this dst,
	 * it's either old or wrong.  If it redirects us to ourselves,
	 * we have a routing loop, perhaps as a result of an interface
	 * going down recently.
	 */
	if (!(flags & RTF_DONE) && rt) {
		if (!sa_equal(src, rt->rt_gateway)) {
			error = EINVAL;
			goto done;
		}
		if (rt->rt_ifa != ifa && ifa->ifa_addr->sa_family != AF_LINK) {
			error = EINVAL;
			goto done;
		}
	}
	if ((flags & RTF_GATEWAY) && ifa_ifwithaddr_check(gateway)) {
		error = EHOSTUNREACH;
		goto done;
	}
	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if (rt == NULL || (rt_mask(rt) && rt_mask(rt)->sa_len < 2))
		goto create;
	/*
	 * Don't listen to the redirect if it's
	 * for a route to an interface.
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		if (((rt->rt_flags & RTF_HOST) == 0) && (flags & RTF_HOST)) {
			/*
			 * Changing from route to net => route to host.
			 * Create new route, rather than smashing route to net.
			 */
		create:
			if (rt != NULL)
				RTFREE_LOCKED(rt);
		
			flags |= RTF_DYNAMIC;
			bzero((caddr_t)&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			info.rti_info[RTAX_GATEWAY] = gateway;
			info.rti_info[RTAX_NETMASK] = netmask;
			ifa_ref(ifa);
			info.rti_ifa = ifa;
			info.rti_flags = flags;
			error = rtrequest1_fib(RTM_ADD, &info, &rt, fibnum);
			if (rt != NULL) {
				RT_LOCK(rt);
				flags = rt->rt_flags;
			}
			
			stat = &V_rtstat.rts_dynamic;
		} else {

			/*
			 * Smash the current notion of the gateway to
			 * this destination.  Should check about netmask!!!
			 */
			if ((flags & RTF_GATEWAY) == 0)
				rt->rt_flags &= ~RTF_GATEWAY;
			rt->rt_flags |= RTF_MODIFIED;
			flags |= RTF_MODIFIED;
			stat = &V_rtstat.rts_newgateway;
			/*
			 * add the key and gateway (in one malloc'd chunk).
			 */
			RT_UNLOCK(rt);
			RIB_WLOCK(rnh);
			RT_LOCK(rt);
			rt_setgate(rt, rt_key(rt), gateway);
			RIB_WUNLOCK(rnh);
		}
	} else
		error = EHOSTUNREACH;
done:
	if (rt)
		RTFREE_LOCKED(rt);
 out:
	NET_EPOCH_EXIT(et);
	if (error)
		V_rtstat.rts_badredirect++;
	else if (stat != NULL)
		(*stat)++;
	bzero((caddr_t)&info, sizeof(info));
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_NETMASK] = netmask;
	info.rti_info[RTAX_AUTHOR] = src;
	rt_missmsg_fib(RTM_REDIRECT, &info, flags, error, fibnum);
}

/*
 * Routing table ioctl interface.
 */
int
rtioctl_fib(u_long req, caddr_t data, u_int fibnum)
{

	/*
	 * If more ioctl commands are added here, make sure the proper
	 * super-user checks are being performed because it is possible for
	 * prison-root to make it this far if raw sockets have been enabled
	 * in jails.
	 */
#ifdef INET
	/* Multicast goop, grrr... */
	return mrt_ioctl ? mrt_ioctl(req, data, fibnum) : EOPNOTSUPP;
#else /* INET */
	return ENXIO;
#endif /* INET */
}

struct ifaddr *
ifa_ifwithroute(int flags, const struct sockaddr *dst, struct sockaddr *gateway,
				u_int fibnum)
{
	struct ifaddr *ifa;
	int not_found = 0;

	MPASS(in_epoch(net_epoch_preempt));
	if ((flags & RTF_GATEWAY) == 0) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a pt to pt link
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = NULL;
		if (flags & RTF_HOST)
			ifa = ifa_ifwithdstaddr(dst, fibnum);
		if (ifa == NULL)
			ifa = ifa_ifwithaddr(gateway);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway, fibnum);
	}
	if (ifa == NULL)
		ifa = ifa_ifwithnet(gateway, 0, fibnum);
	if (ifa == NULL) {
		struct rtentry *rt;

		rt = rtalloc1_fib(gateway, 0, flags, fibnum);
		if (rt == NULL)
			goto out;
		/*
		 * dismiss a gateway that is reachable only
		 * through the default router
		 */
		switch (gateway->sa_family) {
		case AF_INET:
			if (satosin(rt_key(rt))->sin_addr.s_addr == INADDR_ANY)
				not_found = 1;
			break;
		case AF_INET6:
			if (IN6_IS_ADDR_UNSPECIFIED(&satosin6(rt_key(rt))->sin6_addr))
				not_found = 1;
			break;
		default:
			break;
		}
		if (!not_found && rt->rt_ifa != NULL) {
			ifa = rt->rt_ifa;
		}
		RT_REMREF(rt);
		RT_UNLOCK(rt);
		if (not_found || ifa == NULL)
			goto out;
	}
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr *oifa = ifa;
		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == NULL)
			ifa = oifa;
	}
 out:
	return (ifa);
}

/*
 * Do appropriate manipulations of a routing tree given
 * all the bits of info needed
 */
int
rtrequest_fib(int req,
	struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct rtentry **ret_nrt,
	u_int fibnum)
{
	struct rt_addrinfo info;

	if (dst->sa_len == 0)
		return(EINVAL);

	bzero((caddr_t)&info, sizeof(info));
	info.rti_flags = flags;
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_NETMASK] = netmask;
	return rtrequest1_fib(req, &info, ret_nrt, fibnum);
}


/*
 * Copy most of @rt data into @info.
 *
 * If @flags contains NHR_COPY, copies dst,netmask and gw to the
 * pointers specified by @info structure. Assume such pointers
 * are zeroed sockaddr-like structures with sa_len field initialized
 * to reflect size of the provided buffer. if no NHR_COPY is specified,
 * point dst,netmask and gw @info fields to appropriate @rt values.
 *
 * if @flags contains NHR_REF, do refcouting on rt_ifp.
 *
 * Returns 0 on success.
 */
int
rt_exportinfo(struct rtentry *rt, struct rt_addrinfo *info, int flags)
{
	struct rt_metrics *rmx;
	struct sockaddr *src, *dst;
	int sa_len;

	if (flags & NHR_COPY) {
		/* Copy destination if dst is non-zero */
		src = rt_key(rt);
		dst = info->rti_info[RTAX_DST];
		sa_len = src->sa_len;
		if (dst != NULL) {
			if (src->sa_len > dst->sa_len)
				return (ENOMEM);
			memcpy(dst, src, src->sa_len);
			info->rti_addrs |= RTA_DST;
		}

		/* Copy mask if set && dst is non-zero */
		src = rt_mask(rt);
		dst = info->rti_info[RTAX_NETMASK];
		if (src != NULL && dst != NULL) {

			/*
			 * Radix stores different value in sa_len,
			 * assume rt_mask() to have the same length
			 * as rt_key()
			 */
			if (sa_len > dst->sa_len)
				return (ENOMEM);
			memcpy(dst, src, src->sa_len);
			info->rti_addrs |= RTA_NETMASK;
		}

		/* Copy gateway is set && dst is non-zero */
		src = rt->rt_gateway;
		dst = info->rti_info[RTAX_GATEWAY];
		if ((rt->rt_flags & RTF_GATEWAY) && src != NULL && dst != NULL){
			if (src->sa_len > dst->sa_len)
				return (ENOMEM);
			memcpy(dst, src, src->sa_len);
			info->rti_addrs |= RTA_GATEWAY;
		}
	} else {
		info->rti_info[RTAX_DST] = rt_key(rt);
		info->rti_addrs |= RTA_DST;
		if (rt_mask(rt) != NULL) {
			info->rti_info[RTAX_NETMASK] = rt_mask(rt);
			info->rti_addrs |= RTA_NETMASK;
		}
		if (rt->rt_flags & RTF_GATEWAY) {
			info->rti_info[RTAX_GATEWAY] = rt->rt_gateway;
			info->rti_addrs |= RTA_GATEWAY;
		}
	}

	rmx = info->rti_rmx;
	if (rmx != NULL) {
		info->rti_mflags |= RTV_MTU;
		rmx->rmx_mtu = rt->rt_mtu;
	}

	info->rti_flags = rt->rt_flags;
	info->rti_ifp = rt->rt_ifp;
	info->rti_ifa = rt->rt_ifa;
	ifa_ref(info->rti_ifa);
	if (flags & NHR_REF) {
		/* Do 'traditional' refcouting */
		if_ref(info->rti_ifp);
	}

	return (0);
}

/*
 * Lookups up route entry for @dst in RIB database for fib @fibnum.
 * Exports entry data to @info using rt_exportinfo().
 *
 * if @flags contains NHR_REF, refcouting is performed on rt_ifp.
 *   All references can be released later by calling rib_free_info()
 *
 * Returns 0 on success.
 * Returns ENOENT for lookup failure, ENOMEM for export failure.
 */
int
rib_lookup_info(uint32_t fibnum, const struct sockaddr *dst, uint32_t flags,
    uint32_t flowid, struct rt_addrinfo *info)
{
	RIB_RLOCK_TRACKER;
	struct rib_head *rh;
	struct radix_node *rn;
	struct rtentry *rt;
	int error;

	KASSERT((fibnum < rt_numfibs), ("rib_lookup_rte: bad fibnum"));
	rh = rt_tables_get_rnh(fibnum, dst->sa_family);
	if (rh == NULL)
		return (ENOENT);

	RIB_RLOCK(rh);
	rn = rh->rnh_matchaddr(__DECONST(void *, dst), &rh->head);
	if (rn != NULL && ((rn->rn_flags & RNF_ROOT) == 0)) {
		rt = RNTORT(rn);
		/* Ensure route & ifp is UP */
		if (RT_LINK_IS_UP(rt->rt_ifp)) {
			flags = (flags & NHR_REF) | NHR_COPY;
			error = rt_exportinfo(rt, info, flags);
			RIB_RUNLOCK(rh);

			return (error);
		}
	}
	RIB_RUNLOCK(rh);

	return (ENOENT);
}

/*
 * Releases all references acquired by rib_lookup_info() when
 * called with NHR_REF flags.
 */
void
rib_free_info(struct rt_addrinfo *info)
{

	if_rele(info->rti_ifp);
}

/*
 * Iterates over all existing fibs in system calling
 *  @setwa_f function prior to traversing each fib.
 *  Calls @wa_f function for each element in current fib.
 * If af is not AF_UNSPEC, iterates over fibs in particular
 * address family.
 */
void
rt_foreach_fib_walk(int af, rt_setwarg_t *setwa_f, rt_walktree_f_t *wa_f,
    void *arg)
{
	struct rib_head *rnh;
	uint32_t fibnum;
	int i;

	for (fibnum = 0; fibnum < rt_numfibs; fibnum++) {
		/* Do we want some specific family? */
		if (af != AF_UNSPEC) {
			rnh = rt_tables_get_rnh(fibnum, af);
			if (rnh == NULL)
				continue;
			if (setwa_f != NULL)
				setwa_f(rnh, fibnum, af, arg);

			RIB_WLOCK(rnh);
			rnh->rnh_walktree(&rnh->head, (walktree_f_t *)wa_f,arg);
			RIB_WUNLOCK(rnh);
			continue;
		}

		for (i = 1; i <= AF_MAX; i++) {
			rnh = rt_tables_get_rnh(fibnum, i);
			if (rnh == NULL)
				continue;
			if (setwa_f != NULL)
				setwa_f(rnh, fibnum, i, arg);

			RIB_WLOCK(rnh);
			rnh->rnh_walktree(&rnh->head, (walktree_f_t *)wa_f,arg);
			RIB_WUNLOCK(rnh);
		}
	}
}

struct rt_delinfo
{
	struct rt_addrinfo info;
	struct rib_head *rnh;
	struct rtentry *head;
};

/*
 * Conditionally unlinks @rn from radix tree based
 * on info data passed in @arg.
 */
static int
rt_checkdelroute(struct radix_node *rn, void *arg)
{
	struct rt_delinfo *di;
	struct rt_addrinfo *info;
	struct rtentry *rt;
	int error;

	di = (struct rt_delinfo *)arg;
	rt = (struct rtentry *)rn;
	info = &di->info;
	error = 0;

	info->rti_info[RTAX_DST] = rt_key(rt);
	info->rti_info[RTAX_NETMASK] = rt_mask(rt);
	info->rti_info[RTAX_GATEWAY] = rt->rt_gateway;

	rt = rt_unlinkrte(di->rnh, info, &error);
	if (rt == NULL) {
		/* Either not allowed or not matched. Skip entry */
		return (0);
	}

	/* Entry was unlinked. Add to the list and return */
	rt->rt_chain = di->head;
	di->head = rt;

	return (0);
}

/*
 * Iterates over all existing fibs in system.
 * Deletes each element for which @filter_f function returned
 * non-zero value.
 * If @af is not AF_UNSPEC, iterates over fibs in particular
 * address family.
 */
void
rt_foreach_fib_walk_del(int af, rt_filter_f_t *filter_f, void *arg)
{
	struct rib_head *rnh;
	struct rt_delinfo di;
	struct rtentry *rt;
	uint32_t fibnum;
	int i, start, end;

	bzero(&di, sizeof(di));
	di.info.rti_filter = filter_f;
	di.info.rti_filterdata = arg;

	for (fibnum = 0; fibnum < rt_numfibs; fibnum++) {
		/* Do we want some specific family? */
		if (af != AF_UNSPEC) {
			start = af;
			end = af;
		} else {
			start = 1;
			end = AF_MAX;
		}

		for (i = start; i <= end; i++) {
			rnh = rt_tables_get_rnh(fibnum, i);
			if (rnh == NULL)
				continue;
			di.rnh = rnh;

			RIB_WLOCK(rnh);
			rnh->rnh_walktree(&rnh->head, rt_checkdelroute, &di);
			RIB_WUNLOCK(rnh);

			if (di.head == NULL)
				continue;

			/* We might have something to reclaim */
			while (di.head != NULL) {
				rt = di.head;
				di.head = rt->rt_chain;
				rt->rt_chain = NULL;

				/* TODO std rt -> rt_addrinfo export */
				di.info.rti_info[RTAX_DST] = rt_key(rt);
				di.info.rti_info[RTAX_NETMASK] = rt_mask(rt);

				rt_notifydelete(rt, &di.info);
				RTFREE_LOCKED(rt);
			}

		}
	}
}

/*
 * Delete Routes for a Network Interface
 *
 * Called for each routing entry via the rnh->rnh_walktree() call above
 * to delete all route entries referencing a detaching network interface.
 *
 * Arguments:
 *	rt	pointer to rtentry
 *	arg	argument passed to rnh->rnh_walktree() - detaching interface
 *
 * Returns:
 *	0	successful
 *	errno	failed - reason indicated
 */
static int
rt_ifdelroute(const struct rtentry *rt, void *arg)
{
	struct ifnet	*ifp = arg;

	if (rt->rt_ifp != ifp)
		return (0);

	/*
	 * Protect (sorta) against walktree recursion problems
	 * with cloned routes
	 */
	if ((rt->rt_flags & RTF_UP) == 0)
		return (0);

	return (1);
}

/*
 * Delete all remaining routes using this interface
 * Unfortuneatly the only way to do this is to slog through
 * the entire routing table looking for routes which point
 * to this interface...oh well...
 */
void
rt_flushifroutes_af(struct ifnet *ifp, int af)
{
	KASSERT((af >= 1 && af <= AF_MAX), ("%s: af %d not >= 1 and <= %d",
	    __func__, af, AF_MAX));

	rt_foreach_fib_walk_del(af, rt_ifdelroute, ifp);
}

void
rt_flushifroutes(struct ifnet *ifp)
{

	rt_foreach_fib_walk_del(AF_UNSPEC, rt_ifdelroute, ifp);
}

/*
 * Conditionally unlinks rtentry matching data inside @info from @rnh.
 * Returns unlinked, locked and referenced @rtentry on success,
 * Returns NULL and sets @perror to:
 * ESRCH - if prefix was not found,
 * EADDRINUSE - if trying to delete PINNED route without appropriate flag.
 * ENOENT - if supplied filter function returned 0 (not matched).
 */
static struct rtentry *
rt_unlinkrte(struct rib_head *rnh, struct rt_addrinfo *info, int *perror)
{
	struct sockaddr *dst, *netmask;
	struct rtentry *rt;
	struct radix_node *rn;

	dst = info->rti_info[RTAX_DST];
	netmask = info->rti_info[RTAX_NETMASK];

	rt = (struct rtentry *)rnh->rnh_lookup(dst, netmask, &rnh->head);
	if (rt == NULL) {
		*perror = ESRCH;
		return (NULL);
	}

	if ((info->rti_flags & RTF_PINNED) == 0) {
		/* Check if target route can be deleted */
		if (rt->rt_flags & RTF_PINNED) {
			*perror = EADDRINUSE;
			return (NULL);
		}
	}

	if (info->rti_filter != NULL) {
		if (info->rti_filter(rt, info->rti_filterdata) == 0) {
			/* Not matched */
			*perror = ENOENT;
			return (NULL);
		}

		/*
		 * Filter function requested rte deletion.
		 * Ease the caller work by filling in remaining info
		 * from that particular entry.
		 */
		info->rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	}

	/*
	 * Remove the item from the tree and return it.
	 * Complain if it is not there and do no more processing.
	 */
	*perror = ESRCH;
#ifdef RADIX_MPATH
	if (rt_mpath_capable(rnh))
		rn = rt_mpath_unlink(rnh, info, rt, perror);
	else
#endif
	rn = rnh->rnh_deladdr(dst, netmask, &rnh->head);
	if (rn == NULL)
		return (NULL);

	if (rn->rn_flags & (RNF_ACTIVE | RNF_ROOT))
		panic ("rtrequest delete");

	rt = RNTORT(rn);
	RT_LOCK(rt);
	RT_ADDREF(rt);
	rt->rt_flags &= ~RTF_UP;

	*perror = 0;

	return (rt);
}

static void
rt_notifydelete(struct rtentry *rt, struct rt_addrinfo *info)
{
	struct ifaddr *ifa;

	/*
	 * give the protocol a chance to keep things in sync.
	 */
	ifa = rt->rt_ifa;
	if (ifa != NULL && ifa->ifa_rtrequest != NULL)
		ifa->ifa_rtrequest(RTM_DELETE, rt, info);

	/*
	 * One more rtentry floating around that is not
	 * linked to the routing table. rttrash will be decremented
	 * when RTFREE(rt) is eventually called.
	 */
	V_rttrash++;
}


/*
 * These (questionable) definitions of apparent local variables apply
 * to the next two functions.  XXXXXX!!!
 */
#define	dst	info->rti_info[RTAX_DST]
#define	gateway	info->rti_info[RTAX_GATEWAY]
#define	netmask	info->rti_info[RTAX_NETMASK]
#define	ifaaddr	info->rti_info[RTAX_IFA]
#define	ifpaddr	info->rti_info[RTAX_IFP]
#define	flags	info->rti_flags

/*
 * Look up rt_addrinfo for a specific fib.  Note that if rti_ifa is defined,
 * it will be referenced so the caller must free it.
 */
int
rt_getifa_fib(struct rt_addrinfo *info, u_int fibnum)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	int needref, error;

	/*
	 * ifp may be specified by sockaddr_dl
	 * when protocol address is ambiguous.
	 */
	error = 0;
	needref = (info->rti_ifa == NULL);
	NET_EPOCH_ENTER(et);
	if (info->rti_ifp == NULL && ifpaddr != NULL &&
	    ifpaddr->sa_family == AF_LINK &&
	    (ifa = ifa_ifwithnet(ifpaddr, 0, fibnum)) != NULL) {
		info->rti_ifp = ifa->ifa_ifp;
	}
	if (info->rti_ifa == NULL && ifaaddr != NULL)
		info->rti_ifa = ifa_ifwithaddr(ifaaddr);
	if (info->rti_ifa == NULL) {
		struct sockaddr *sa;

		sa = ifaaddr != NULL ? ifaaddr :
		    (gateway != NULL ? gateway : dst);
		if (sa != NULL && info->rti_ifp != NULL)
			info->rti_ifa = ifaof_ifpforaddr(sa, info->rti_ifp);
		else if (dst != NULL && gateway != NULL)
			info->rti_ifa = ifa_ifwithroute(flags, dst, gateway,
							fibnum);
		else if (sa != NULL)
			info->rti_ifa = ifa_ifwithroute(flags, sa, sa,
							fibnum);
	}
	if (needref && info->rti_ifa != NULL) {
		if (info->rti_ifp == NULL)
			info->rti_ifp = info->rti_ifa->ifa_ifp;
		ifa_ref(info->rti_ifa);
	} else
		error = ENETUNREACH;
	NET_EPOCH_EXIT(et);
	return (error);
}

static int
if_updatemtu_cb(struct radix_node *rn, void *arg)
{
	struct rtentry *rt;
	struct if_mtuinfo *ifmtu;

	rt = (struct rtentry *)rn;
	ifmtu = (struct if_mtuinfo *)arg;

	if (rt->rt_ifp != ifmtu->ifp)
		return (0);

	if (rt->rt_mtu >= ifmtu->mtu) {
		/* We have to decrease mtu regardless of flags */
		rt->rt_mtu = ifmtu->mtu;
		return (0);
	}

	/*
	 * New MTU is bigger. Check if are allowed to alter it
	 */
	if ((rt->rt_flags & (RTF_FIXEDMTU | RTF_GATEWAY | RTF_HOST)) != 0) {

		/*
		 * Skip routes with user-supplied MTU and
		 * non-interface routes
		 */
		return (0);
	}

	/* We are safe to update route MTU */
	rt->rt_mtu = ifmtu->mtu;

	return (0);
}

void
rt_updatemtu(struct ifnet *ifp)
{
	struct if_mtuinfo ifmtu;
	struct rib_head *rnh;
	int i, j;

	ifmtu.ifp = ifp;

	/*
	 * Try to update rt_mtu for all routes using this interface
	 * Unfortunately the only way to do this is to traverse all
	 * routing tables in all fibs/domains.
	 */
	for (i = 1; i <= AF_MAX; i++) {
		ifmtu.mtu = if_getmtu_family(ifp, i);
		for (j = 0; j < rt_numfibs; j++) {
			rnh = rt_tables_get_rnh(j, i);
			if (rnh == NULL)
				continue;
			RIB_WLOCK(rnh);
			rnh->rnh_walktree(&rnh->head, if_updatemtu_cb, &ifmtu);
			RIB_WUNLOCK(rnh);
		}
	}
}


#if 0
int p_sockaddr(char *buf, int buflen, struct sockaddr *s);
int rt_print(char *buf, int buflen, struct rtentry *rt);

int
p_sockaddr(char *buf, int buflen, struct sockaddr *s)
{
	void *paddr = NULL;

	switch (s->sa_family) {
	case AF_INET:
		paddr = &((struct sockaddr_in *)s)->sin_addr;
		break;
	case AF_INET6:
		paddr = &((struct sockaddr_in6 *)s)->sin6_addr;
		break;
	}

	if (paddr == NULL)
		return (0);

	if (inet_ntop(s->sa_family, paddr, buf, buflen) == NULL)
		return (0);
	
	return (strlen(buf));
}

int
rt_print(char *buf, int buflen, struct rtentry *rt)
{
	struct sockaddr *addr, *mask;
	int i = 0;

	addr = rt_key(rt);
	mask = rt_mask(rt);

	i = p_sockaddr(buf, buflen, addr);
	if (!(rt->rt_flags & RTF_HOST)) {
		buf[i++] = '/';
		i += p_sockaddr(buf + i, buflen - i, mask);
	}

	if (rt->rt_flags & RTF_GATEWAY) {
		buf[i++] = '>';
		i += p_sockaddr(buf + i, buflen - i, rt->rt_gateway);
	}

	return (i);
}
#endif

#ifdef RADIX_MPATH
/*
 * Deletes key for single-path routes, unlinks rtentry with
 * gateway specified in @info from multi-path routes.
 *
 * Returnes unlinked entry. In case of failure, returns NULL
 * and sets @perror to ESRCH.
 */
static struct radix_node *
rt_mpath_unlink(struct rib_head *rnh, struct rt_addrinfo *info,
    struct rtentry *rto, int *perror)
{
	/*
	 * if we got multipath routes, we require users to specify
	 * a matching RTAX_GATEWAY.
	 */
	struct rtentry *rt; // *rto = NULL;
	struct radix_node *rn;
	struct sockaddr *gw;

	gw = info->rti_info[RTAX_GATEWAY];
	rt = rt_mpath_matchgate(rto, gw);
	if (rt == NULL) {
		*perror = ESRCH;
		return (NULL);
	}

	/*
	 * this is the first entry in the chain
	 */
	if (rto == rt) {
		rn = rn_mpath_next((struct radix_node *)rt);
		/*
		 * there is another entry, now it's active
		 */
		if (rn) {
			rto = RNTORT(rn);
			RT_LOCK(rto);
			rto->rt_flags |= RTF_UP;
			RT_UNLOCK(rto);
		} else if (rt->rt_flags & RTF_GATEWAY) {
			/*
			 * For gateway routes, we need to 
			 * make sure that we we are deleting
			 * the correct gateway. 
			 * rt_mpath_matchgate() does not 
			 * check the case when there is only
			 * one route in the chain.  
			 */
			if (gw &&
			    (rt->rt_gateway->sa_len != gw->sa_len ||
				memcmp(rt->rt_gateway, gw, gw->sa_len))) {
				*perror = ESRCH;
				return (NULL);
			}
		}

		/*
		 * use the normal delete code to remove
		 * the first entry
		 */
		rn = rnh->rnh_deladdr(dst, netmask, &rnh->head);
		*perror = 0;
		return (rn);
	}
		
	/*
	 * if the entry is 2nd and on up
	 */
	if (rt_mpath_deldup(rto, rt) == 0)
		panic ("rtrequest1: rt_mpath_deldup");
	*perror = 0;
	rn = (struct radix_node *)rt;
	return (rn);
}
#endif

int
rtrequest1_fib(int req, struct rt_addrinfo *info, struct rtentry **ret_nrt,
				u_int fibnum)
{
	int error = 0;
	struct rtentry *rt, *rt_old;
	struct radix_node *rn;
	struct rib_head *rnh;
	struct ifaddr *ifa;
	struct sockaddr *ndst;
	struct sockaddr_storage mdst;

	KASSERT((fibnum < rt_numfibs), ("rtrequest1_fib: bad fibnum"));
	KASSERT((flags & RTF_RNH_LOCKED) == 0, ("rtrequest1_fib: locked"));
	switch (dst->sa_family) {
	case AF_INET6:
	case AF_INET:
		/* We support multiple FIBs. */
		break;
	default:
		fibnum = RT_DEFAULT_FIB;
		break;
	}

	/*
	 * Find the correct routing tree to use for this Address Family
	 */
	rnh = rt_tables_get_rnh(fibnum, dst->sa_family);
	if (rnh == NULL)
		return (EAFNOSUPPORT);

	/*
	 * If we are adding a host route then we don't want to put
	 * a netmask in the tree, nor do we want to clone it.
	 */
	if (flags & RTF_HOST)
		netmask = NULL;

	switch (req) {
	case RTM_DELETE:
		if (netmask) {
			rt_maskedcopy(dst, (struct sockaddr *)&mdst, netmask);
			dst = (struct sockaddr *)&mdst;
		}

		RIB_WLOCK(rnh);
		rt = rt_unlinkrte(rnh, info, &error);
		RIB_WUNLOCK(rnh);
		if (error != 0)
			return (error);

		rt_notifydelete(rt, info);

		/*
		 * If the caller wants it, then it can have it,
		 * but it's up to it to free the rtentry as we won't be
		 * doing it.
		 */
		if (ret_nrt) {
			*ret_nrt = rt;
			RT_UNLOCK(rt);
		} else
			RTFREE_LOCKED(rt);
		break;
	case RTM_RESOLVE:
		/*
		 * resolve was only used for route cloning
		 * here for compat
		 */
		break;
	case RTM_ADD:
		if ((flags & RTF_GATEWAY) && !gateway)
			return (EINVAL);
		if (dst && gateway && (dst->sa_family != gateway->sa_family) && 
		    (gateway->sa_family != AF_UNSPEC) && (gateway->sa_family != AF_LINK))
			return (EINVAL);

		if (info->rti_ifa == NULL) {
			error = rt_getifa_fib(info, fibnum);
			if (error)
				return (error);
		}
		rt = uma_zalloc(V_rtzone, M_NOWAIT);
		if (rt == NULL) {
			return (ENOBUFS);
		}
		rt->rt_flags = RTF_UP | flags;
		rt->rt_fibnum = fibnum;
		/*
		 * Add the gateway. Possibly re-malloc-ing the storage for it.
		 */
		if ((error = rt_setgate(rt, dst, gateway)) != 0) {
			uma_zfree(V_rtzone, rt);
			return (error);
		}

		/*
		 * point to the (possibly newly malloc'd) dest address.
		 */
		ndst = (struct sockaddr *)rt_key(rt);

		/*
		 * make sure it contains the value we want (masked if needed).
		 */
		if (netmask) {
			rt_maskedcopy(dst, ndst, netmask);
		} else
			bcopy(dst, ndst, dst->sa_len);

		/*
		 * We use the ifa reference returned by rt_getifa_fib().
		 * This moved from below so that rnh->rnh_addaddr() can
		 * examine the ifa and  ifa->ifa_ifp if it so desires.
		 */
		ifa = info->rti_ifa;
		ifa_ref(ifa);
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
		rt->rt_weight = 1;

		rt_setmetrics(info, rt);

		RIB_WLOCK(rnh);
		RT_LOCK(rt);
#ifdef RADIX_MPATH
		/* do not permit exactly the same dst/mask/gw pair */
		if (rt_mpath_capable(rnh) &&
			rt_mpath_conflict(rnh, rt, netmask)) {
			RIB_WUNLOCK(rnh);

			ifa_free(rt->rt_ifa);
			R_Free(rt_key(rt));
			uma_zfree(V_rtzone, rt);
			return (EEXIST);
		}
#endif

		/* XXX mtu manipulation will be done in rnh_addaddr -- itojun */
		rn = rnh->rnh_addaddr(ndst, netmask, &rnh->head, rt->rt_nodes);

		rt_old = NULL;
		if (rn == NULL && (info->rti_flags & RTF_PINNED) != 0) {

			/*
			 * Force removal and re-try addition
			 * TODO: better multipath&pinned support
			 */
			struct sockaddr *info_dst = info->rti_info[RTAX_DST];
			info->rti_info[RTAX_DST] = ndst;
			/* Do not delete existing PINNED(interface) routes */
			info->rti_flags &= ~RTF_PINNED;
			rt_old = rt_unlinkrte(rnh, info, &error);
			info->rti_flags |= RTF_PINNED;
			info->rti_info[RTAX_DST] = info_dst;
			if (rt_old != NULL)
				rn = rnh->rnh_addaddr(ndst, netmask, &rnh->head,
				    rt->rt_nodes);
		}
		RIB_WUNLOCK(rnh);

		if (rt_old != NULL)
			RT_UNLOCK(rt_old);

		/*
		 * If it still failed to go into the tree,
		 * then un-make it (this should be a function)
		 */
		if (rn == NULL) {
			ifa_free(rt->rt_ifa);
			R_Free(rt_key(rt));
			uma_zfree(V_rtzone, rt);
			return (EEXIST);
		} 

		if (rt_old != NULL) {
			rt_notifydelete(rt_old, info);
			RTFREE(rt_old);
		}

		/*
		 * If this protocol has something to add to this then
		 * allow it to do that as well.
		 */
		if (ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(req, rt, info);

		/*
		 * actually return a resultant rtentry and
		 * give the caller a single reference.
		 */
		if (ret_nrt) {
			*ret_nrt = rt;
			RT_ADDREF(rt);
		}
		rnh->rnh_gen++;		/* Routing table updated */
		RT_UNLOCK(rt);
		break;
	case RTM_CHANGE:
		RIB_WLOCK(rnh);
		error = rtrequest1_fib_change(rnh, info, ret_nrt, fibnum);
		RIB_WUNLOCK(rnh);
		break;
	default:
		error = EOPNOTSUPP;
	}

	return (error);
}

#undef dst
#undef gateway
#undef netmask
#undef ifaaddr
#undef ifpaddr
#undef flags

static int
rtrequest1_fib_change(struct rib_head *rnh, struct rt_addrinfo *info,
    struct rtentry **ret_nrt, u_int fibnum)
{
	struct rtentry *rt = NULL;
	int error = 0;
	int free_ifa = 0;
	int family, mtu;
	struct if_mtuinfo ifmtu;

	RIB_WLOCK_ASSERT(rnh);

	rt = (struct rtentry *)rnh->rnh_lookup(info->rti_info[RTAX_DST],
	    info->rti_info[RTAX_NETMASK], &rnh->head);

	if (rt == NULL)
		return (ESRCH);

#ifdef RADIX_MPATH
	/*
	 * If we got multipath routes,
	 * we require users to specify a matching RTAX_GATEWAY.
	 */
	if (rt_mpath_capable(rnh)) {
		rt = rt_mpath_matchgate(rt, info->rti_info[RTAX_GATEWAY]);
		if (rt == NULL)
			return (ESRCH);
	}
#endif

	RT_LOCK(rt);

	rt_setmetrics(info, rt);

	/*
	 * New gateway could require new ifaddr, ifp;
	 * flags may also be different; ifp may be specified
	 * by ll sockaddr when protocol address is ambiguous
	 */
	if (((rt->rt_flags & RTF_GATEWAY) &&
	    info->rti_info[RTAX_GATEWAY] != NULL) ||
	    info->rti_info[RTAX_IFP] != NULL ||
	    (info->rti_info[RTAX_IFA] != NULL &&
	     !sa_equal(info->rti_info[RTAX_IFA], rt->rt_ifa->ifa_addr))) {
		/*
		 * XXX: Temporarily set RTF_RNH_LOCKED flag in the rti_flags
		 *	to avoid rlock in the ifa_ifwithroute().
		 */
		info->rti_flags |= RTF_RNH_LOCKED;
		error = rt_getifa_fib(info, fibnum);
		info->rti_flags &= ~RTF_RNH_LOCKED;
		if (info->rti_ifa != NULL)
			free_ifa = 1;

		if (error != 0)
			goto bad;
	}

	/* Check if outgoing interface has changed */
	if (info->rti_ifa != NULL && info->rti_ifa != rt->rt_ifa &&
	    rt->rt_ifa != NULL) {
		if (rt->rt_ifa->ifa_rtrequest != NULL)
			rt->rt_ifa->ifa_rtrequest(RTM_DELETE, rt, info);
		ifa_free(rt->rt_ifa);
		rt->rt_ifa = NULL;
	}
	/* Update gateway address */
	if (info->rti_info[RTAX_GATEWAY] != NULL) {
		error = rt_setgate(rt, rt_key(rt), info->rti_info[RTAX_GATEWAY]);
		if (error != 0)
			goto bad;

		rt->rt_flags &= ~RTF_GATEWAY;
		rt->rt_flags |= (RTF_GATEWAY & info->rti_flags);
	}

	if (info->rti_ifa != NULL && info->rti_ifa != rt->rt_ifa) {
		ifa_ref(info->rti_ifa);
		rt->rt_ifa = info->rti_ifa;
		rt->rt_ifp = info->rti_ifp;
	}
	/* Allow some flags to be toggled on change. */
	rt->rt_flags &= ~RTF_FMASK;
	rt->rt_flags |= info->rti_flags & RTF_FMASK;

	if (rt->rt_ifa && rt->rt_ifa->ifa_rtrequest != NULL)
	       rt->rt_ifa->ifa_rtrequest(RTM_ADD, rt, info);

	/* Alter route MTU if necessary */
	if (rt->rt_ifp != NULL) {
		family = info->rti_info[RTAX_DST]->sa_family;
		mtu = if_getmtu_family(rt->rt_ifp, family);
		/* Set default MTU */
		if (rt->rt_mtu == 0)
			rt->rt_mtu = mtu;
		if (rt->rt_mtu != mtu) {
			/* Check if we really need to update */
			ifmtu.ifp = rt->rt_ifp;
			ifmtu.mtu = mtu;
			if_updatemtu_cb(rt->rt_nodes, &ifmtu);
		}
	}

	/*
	 * This route change may have modified the route's gateway.  In that
	 * case, any inpcbs that have cached this route need to invalidate their
	 * llentry cache.
	 */
	rnh->rnh_gen++;

	if (ret_nrt) {
		*ret_nrt = rt;
		RT_ADDREF(rt);
	}
bad:
	RT_UNLOCK(rt);
	if (free_ifa != 0) {
		ifa_free(info->rti_ifa);
		info->rti_ifa = NULL;
	}
	return (error);
}

static void
rt_setmetrics(const struct rt_addrinfo *info, struct rtentry *rt)
{

	if (info->rti_mflags & RTV_MTU) {
		if (info->rti_rmx->rmx_mtu != 0) {

			/*
			 * MTU was explicitly provided by user.
			 * Keep it.
			 */
			rt->rt_flags |= RTF_FIXEDMTU;
		} else {

			/*
			 * User explicitly sets MTU to 0.
			 * Assume rollback to default.
			 */
			rt->rt_flags &= ~RTF_FIXEDMTU;
		}
		rt->rt_mtu = info->rti_rmx->rmx_mtu;
	}
	if (info->rti_mflags & RTV_WEIGHT)
		rt->rt_weight = info->rti_rmx->rmx_weight;
	/* Kernel -> userland timebase conversion. */
	if (info->rti_mflags & RTV_EXPIRE)
		rt->rt_expire = info->rti_rmx->rmx_expire ?
		    info->rti_rmx->rmx_expire - time_second + time_uptime : 0;
}

int
rt_setgate(struct rtentry *rt, struct sockaddr *dst, struct sockaddr *gate)
{
	/* XXX dst may be overwritten, can we move this to below */
	int dlen = SA_SIZE(dst), glen = SA_SIZE(gate);

	/*
	 * Prepare to store the gateway in rt->rt_gateway.
	 * Both dst and gateway are stored one after the other in the same
	 * malloc'd chunk. If we have room, we can reuse the old buffer,
	 * rt_gateway already points to the right place.
	 * Otherwise, malloc a new block and update the 'dst' address.
	 */
	if (rt->rt_gateway == NULL || glen > SA_SIZE(rt->rt_gateway)) {
		caddr_t new;

		R_Malloc(new, caddr_t, dlen + glen);
		if (new == NULL)
			return ENOBUFS;
		/*
		 * XXX note, we copy from *dst and not *rt_key(rt) because
		 * rt_setgate() can be called to initialize a newly
		 * allocated route entry, in which case rt_key(rt) == NULL
		 * (and also rt->rt_gateway == NULL).
		 * Free()/free() handle a NULL argument just fine.
		 */
		bcopy(dst, new, dlen);
		R_Free(rt_key(rt));	/* free old block, if any */
		rt_key(rt) = (struct sockaddr *)new;
		rt->rt_gateway = (struct sockaddr *)(new + dlen);
	}

	/*
	 * Copy the new gateway value into the memory chunk.
	 */
	bcopy(gate, rt->rt_gateway, glen);

	return (0);
}

void
rt_maskedcopy(struct sockaddr *src, struct sockaddr *dst, struct sockaddr *netmask)
{
	u_char *cp1 = (u_char *)src;
	u_char *cp2 = (u_char *)dst;
	u_char *cp3 = (u_char *)netmask;
	u_char *cplim = cp2 + *cp3;
	u_char *cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero((caddr_t)cp2, (unsigned)(cplim2 - cp2));
}

/*
 * Set up a routing table entry, normally
 * for an interface.
 */
#define _SOCKADDR_TMPSIZE 128 /* Not too big.. kernel stack size is limited */
static inline  int
rtinit1(struct ifaddr *ifa, int cmd, int flags, int fibnum)
{
	RIB_RLOCK_TRACKER;
	struct sockaddr *dst;
	struct sockaddr *netmask;
	struct rtentry *rt = NULL;
	struct rt_addrinfo info;
	int error = 0;
	int startfib, endfib;
	char tempbuf[_SOCKADDR_TMPSIZE];
	int didwork = 0;
	int a_failure = 0;
	static struct sockaddr_dl null_sdl = {sizeof(null_sdl), AF_LINK};
	struct rib_head *rnh;

	if (flags & RTF_HOST) {
		dst = ifa->ifa_dstaddr;
		netmask = NULL;
	} else {
		dst = ifa->ifa_addr;
		netmask = ifa->ifa_netmask;
	}
	if (dst->sa_len == 0)
		return(EINVAL);
	switch (dst->sa_family) {
	case AF_INET6:
	case AF_INET:
		/* We support multiple FIBs. */
		break;
	default:
		fibnum = RT_DEFAULT_FIB;
		break;
	}
	if (fibnum == RT_ALL_FIBS) {
		if (V_rt_add_addr_allfibs == 0 && cmd == (int)RTM_ADD)
			startfib = endfib = ifa->ifa_ifp->if_fib;
		else {
			startfib = 0;
			endfib = rt_numfibs - 1;
		}
	} else {
		KASSERT((fibnum < rt_numfibs), ("rtinit1: bad fibnum"));
		startfib = fibnum;
		endfib = fibnum;
	}

	/*
	 * If it's a delete, check that if it exists,
	 * it's on the correct interface or we might scrub
	 * a route to another ifa which would
	 * be confusing at best and possibly worse.
	 */
	if (cmd == RTM_DELETE) {
		/*
		 * It's a delete, so it should already exist..
		 * If it's a net, mask off the host bits
		 * (Assuming we have a mask)
		 * XXX this is kinda inet specific..
		 */
		if (netmask != NULL) {
			rt_maskedcopy(dst, (struct sockaddr *)tempbuf, netmask);
			dst = (struct sockaddr *)tempbuf;
		}
	}
	/*
	 * Now go through all the requested tables (fibs) and do the
	 * requested action. Realistically, this will either be fib 0
	 * for protocols that don't do multiple tables or all the
	 * tables for those that do.
	 */
	for ( fibnum = startfib; fibnum <= endfib; fibnum++) {
		if (cmd == RTM_DELETE) {
			struct radix_node *rn;
			/*
			 * Look up an rtentry that is in the routing tree and
			 * contains the correct info.
			 */
			rnh = rt_tables_get_rnh(fibnum, dst->sa_family);
			if (rnh == NULL)
				/* this table doesn't exist but others might */
				continue;
			RIB_RLOCK(rnh);
			rn = rnh->rnh_lookup(dst, netmask, &rnh->head);
#ifdef RADIX_MPATH
			if (rt_mpath_capable(rnh)) {

				if (rn == NULL) 
					error = ESRCH;
				else {
					rt = RNTORT(rn);
					/*
					 * for interface route the
					 * rt->rt_gateway is sockaddr_intf
					 * for cloning ARP entries, so
					 * rt_mpath_matchgate must use the
					 * interface address
					 */
					rt = rt_mpath_matchgate(rt,
					    ifa->ifa_addr);
					if (rt == NULL) 
						error = ESRCH;
				}
			}
#endif
			error = (rn == NULL ||
			    (rn->rn_flags & RNF_ROOT) ||
			    RNTORT(rn)->rt_ifa != ifa);
			RIB_RUNLOCK(rnh);
			if (error) {
				/* this is only an error if bad on ALL tables */
				continue;
			}
		}
		/*
		 * Do the actual request
		 */
		bzero((caddr_t)&info, sizeof(info));
		ifa_ref(ifa);
		info.rti_ifa = ifa;
		info.rti_flags = flags |
		    (ifa->ifa_flags & ~IFA_RTSELF) | RTF_PINNED;
		info.rti_info[RTAX_DST] = dst;
		/* 
		 * doing this for compatibility reasons
		 */
		if (cmd == RTM_ADD)
			info.rti_info[RTAX_GATEWAY] =
			    (struct sockaddr *)&null_sdl;
		else
			info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
		info.rti_info[RTAX_NETMASK] = netmask;
		error = rtrequest1_fib(cmd, &info, &rt, fibnum);

		if (error == 0 && rt != NULL) {
			/*
			 * notify any listening routing agents of the change
			 */
			RT_LOCK(rt);
#ifdef RADIX_MPATH
			/*
			 * in case address alias finds the first address
			 * e.g. ifconfig bge0 192.0.2.246/24
			 * e.g. ifconfig bge0 192.0.2.247/24
			 * the address set in the route is 192.0.2.246
			 * so we need to replace it with 192.0.2.247
			 */
			if (memcmp(rt->rt_ifa->ifa_addr,
			    ifa->ifa_addr, ifa->ifa_addr->sa_len)) {
				ifa_free(rt->rt_ifa);
				ifa_ref(ifa);
				rt->rt_ifp = ifa->ifa_ifp;
				rt->rt_ifa = ifa;
			}
#endif
			/* 
			 * doing this for compatibility reasons
			 */
			if (cmd == RTM_ADD) {
			    ((struct sockaddr_dl *)rt->rt_gateway)->sdl_type  =
				rt->rt_ifp->if_type;
			    ((struct sockaddr_dl *)rt->rt_gateway)->sdl_index =
				rt->rt_ifp->if_index;
			}
			RT_ADDREF(rt);
			RT_UNLOCK(rt);
			rt_newaddrmsg_fib(cmd, ifa, error, rt, fibnum);
			RT_LOCK(rt);
			RT_REMREF(rt);
			if (cmd == RTM_DELETE) {
				/*
				 * If we are deleting, and we found an entry,
				 * then it's been removed from the tree..
				 * now throw it away.
				 */
				RTFREE_LOCKED(rt);
			} else {
				if (cmd == RTM_ADD) {
					/*
					 * We just wanted to add it..
					 * we don't actually need a reference.
					 */
					RT_REMREF(rt);
				}
				RT_UNLOCK(rt);
			}
			didwork = 1;
		}
		if (error)
			a_failure = error;
	}
	if (cmd == RTM_DELETE) {
		if (didwork) {
			error = 0;
		} else {
			/* we only give an error if it wasn't in any table */
			error = ((flags & RTF_HOST) ?
			    EHOSTUNREACH : ENETUNREACH);
		}
	} else {
		if (a_failure) {
			/* return an error if any of them failed */
			error = a_failure;
		}
	}
	return (error);
}

/*
 * Set up a routing table entry, normally
 * for an interface.
 */
int
rtinit(struct ifaddr *ifa, int cmd, int flags)
{
	struct sockaddr *dst;
	int fib = RT_DEFAULT_FIB;

	if (flags & RTF_HOST) {
		dst = ifa->ifa_dstaddr;
	} else {
		dst = ifa->ifa_addr;
	}

	switch (dst->sa_family) {
	case AF_INET6:
	case AF_INET:
		/* We do support multiple FIBs. */
		fib = RT_ALL_FIBS;
		break;
	}
	return (rtinit1(ifa, cmd, flags, fib));
}

/*
 * Announce interface address arrival/withdraw
 * Returns 0 on success.
 */
int
rt_addrmsg(int cmd, struct ifaddr *ifa, int fibnum)
{

	KASSERT(cmd == RTM_ADD || cmd == RTM_DELETE,
	    ("unexpected cmd %d", cmd));
	
	KASSERT(fibnum == RT_ALL_FIBS || (fibnum >= 0 && fibnum < rt_numfibs),
	    ("%s: fib out of range 0 <=%d<%d", __func__, fibnum, rt_numfibs));

#if defined(INET) || defined(INET6)
#ifdef SCTP
	/*
	 * notify the SCTP stack
	 * this will only get called when an address is added/deleted
	 * XXX pass the ifaddr struct instead if ifa->ifa_addr...
	 */
	sctp_addr_change(ifa, cmd);
#endif /* SCTP */
#endif
	return (rtsock_addrmsg(cmd, ifa, fibnum));
}

/*
 * Announce route addition/removal.
 * Users of this function MUST validate input data BEFORE calling.
 * However we have to be able to handle invalid data:
 * if some userland app sends us "invalid" route message (invalid mask,
 * no dst, wrong address families, etc...) we need to pass it back
 * to app (and any other rtsock consumers) with rtm_errno field set to
 * non-zero value.
 * Returns 0 on success.
 */
int
rt_routemsg(int cmd, struct ifnet *ifp, int error, struct rtentry *rt,
    int fibnum)
{

	KASSERT(cmd == RTM_ADD || cmd == RTM_DELETE,
	    ("unexpected cmd %d", cmd));
	
	KASSERT(fibnum == RT_ALL_FIBS || (fibnum >= 0 && fibnum < rt_numfibs),
	    ("%s: fib out of range 0 <=%d<%d", __func__, fibnum, rt_numfibs));

	KASSERT(rt_key(rt) != NULL, (":%s: rt_key must be supplied", __func__));

	return (rtsock_routemsg(cmd, ifp, error, rt, fibnum));
}

void
rt_newaddrmsg(int cmd, struct ifaddr *ifa, int error, struct rtentry *rt)
{

	rt_newaddrmsg_fib(cmd, ifa, error, rt, RT_ALL_FIBS);
}

/*
 * This is called to generate messages from the routing socket
 * indicating a network interface has had addresses associated with it.
 */
void
rt_newaddrmsg_fib(int cmd, struct ifaddr *ifa, int error, struct rtentry *rt,
    int fibnum)
{

	KASSERT(cmd == RTM_ADD || cmd == RTM_DELETE,
		("unexpected cmd %u", cmd));
	KASSERT(fibnum == RT_ALL_FIBS || (fibnum >= 0 && fibnum < rt_numfibs),
	    ("%s: fib out of range 0 <=%d<%d", __func__, fibnum, rt_numfibs));

	if (cmd == RTM_ADD) {
		rt_addrmsg(cmd, ifa, fibnum);
		if (rt != NULL)
			rt_routemsg(cmd, ifa->ifa_ifp, error, rt, fibnum);
	} else {
		if (rt != NULL)
			rt_routemsg(cmd, ifa->ifa_ifp, error, rt, fibnum);
		rt_addrmsg(cmd, ifa, fibnum);
	}
}

