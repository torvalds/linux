/*	$OpenBSD: ip6_mroute.c,v 1.155 2025/09/16 09:18:55 florian Exp $	*/
/*	$NetBSD: ip6_mroute.c,v 1.59 2003/12/10 09:28:38 itojun Exp $	*/
/*	$KAME: ip6_mroute.c,v 1.45 2001/03/25 08:38:51 itojun Exp $	*/

/*
 * Copyright (C) 1998 WIDE Project.
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

/*	BSDI ip_mroute.c,v 2.10 1996/11/14 00:29:52 jch Exp	*/

/*
 * Copyright (c) 1989 Stephen Deering
 * Copyright (c) 1992, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
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
 *      @(#)ip_mroute.c 8.2 (Berkeley) 11/15/93
 */

/*
 * IP multicast forwarding procedures
 *
 * Written by David Waitzman, BBN Labs, August 1988.
 * Modified by Steve Deering, Stanford, February 1989.
 * Modified by Mark J. Steiglitz, Stanford, May, 1991
 * Modified by Van Jacobson, LBL, January 1993
 * Modified by Ajit Thyagarajan, PARC, August 1993
 * Modified by Bill Fenner, PARC, April 1994
 *
 * MROUTING Revision: 3.5.1.2
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip6_mroute.h>
#include <netinet/in_pcb.h>

/*
 * Locks used to protect data:
 *	I	immutable after creation
 */

/* #define MCAST_DEBUG */

#ifdef MCAST_DEBUG
int mcast6_debug = 1;
#define DPRINTF(fmt, args...)						\
	do {								\
		if (mcast6_debug)					\
			printf("%s:%d " fmt "\n",			\
			    __func__, __LINE__, ## args);		\
	} while (0)
#else
#define DPRINTF(fmt, args...)			\
	do { } while (0)
#endif

int ip6_mdq(struct mbuf *, struct ifnet *, struct rtentry *, int);
void phyint_send6(struct ifnet *, struct ip6_hdr *, struct mbuf *, int, int);

/*
 * Globals.  All but ip6_mrouter, ip6_mrtproto and mrt6stat could be static,
 * except for netstat or debugging purposes.
 */
struct socket  *ip6_mrouter[RT_TABLEID_MAX + 1];
struct rttimer_queue ip6_mrouterq;
int		ip6_mrouter_ver = 0;
int		ip6_mrtproto;    /* [I] for netstat only */
struct cpumem *mrt6counters;

int get_sg6_cnt(struct sioc_sg_req6 *, unsigned int);
int get_mif6_cnt(struct sioc_mif_req6 *, unsigned int);
int ip6_mrouter_init(struct socket *, int, int);
int add_m6if(struct socket *, struct mif6ctl *);
int del_m6if(struct socket *, mifi_t *);
int add_m6fc(struct socket *, struct mf6cctl *);
int del_m6fc(struct socket *, struct mf6cctl *);
void mf6c_expire_route(struct rtentry *, u_int);
struct ifnet *mrt6_iflookupbymif(mifi_t, unsigned int);
struct rtentry *mf6c_find(struct ifnet *, struct in6_addr *, unsigned int);
struct rtentry *mrt6_mcast_add(struct ifnet *, struct sockaddr *);
void mrt6_mcast_del(struct rtentry *, unsigned int);

/*
 * Handle MRT setsockopt commands to modify the multicast routing tables.
 */
int
ip6_mrouter_set(int cmd, struct socket *so, struct mbuf *m)
{
	struct inpcb	*inp = sotoinpcb(so);

	if (cmd != MRT6_INIT && so != ip6_mrouter[inp->inp_rtableid])
		return (EPERM);

	switch (cmd) {
	case MRT6_INIT:
		if (m == NULL || m->m_len < sizeof(int))
			return (EINVAL);
		return (ip6_mrouter_init(so, *mtod(m, int *), cmd));
	case MRT6_DONE:
		return (ip6_mrouter_done(so));
	case MRT6_ADD_MIF:
		if (m == NULL || m->m_len < sizeof(struct mif6ctl))
			return (EINVAL);
		return (add_m6if(so, mtod(m, struct mif6ctl *)));
	case MRT6_DEL_MIF:
		if (m == NULL || m->m_len < sizeof(mifi_t))
			return (EINVAL);
		return (del_m6if(so, mtod(m, mifi_t *)));
	case MRT6_ADD_MFC:
		if (m == NULL || m->m_len < sizeof(struct mf6cctl))
			return (EINVAL);
		return (add_m6fc(so, mtod(m, struct mf6cctl *)));
	case MRT6_DEL_MFC:
		if (m == NULL || m->m_len < sizeof(struct mf6cctl))
			return (EINVAL);
		return (del_m6fc(so, mtod(m,  struct mf6cctl *)));
	default:
		return (EOPNOTSUPP);
	}
}

/*
 * Handle MRT getsockopt commands
 */
int
ip6_mrouter_get(int cmd, struct socket *so, struct mbuf *m)
{
	struct inpcb	*inp = sotoinpcb(so);

	if (so != ip6_mrouter[inp->inp_rtableid])
		return (EPERM);

	switch (cmd) {
	default:
		return EOPNOTSUPP;
	}
}

void
mrt6_init(void)
{
	mrt6counters = counters_alloc(mrt6s_ncounters);

	rt_timer_queue_init(&ip6_mrouterq, MCAST_EXPIRE_TIMEOUT,
	    &mf6c_expire_route);
}

/*
 * Handle ioctl commands to obtain information from the cache
 */
int
mrt6_ioctl(struct socket *so, u_long cmd, caddr_t data)
{
	struct inpcb *inp = sotoinpcb(so);
	int error;

	if (inp == NULL)
		return (ENOTCONN);

	KERNEL_LOCK();

	switch (cmd) {
	case SIOCGETSGCNT_IN6:
		NET_LOCK_SHARED();
		error = get_sg6_cnt((struct sioc_sg_req6 *)data,
		    inp->inp_rtableid);
		NET_UNLOCK_SHARED();
		break;
	case SIOCGETMIFCNT_IN6:
		NET_LOCK_SHARED();
		error = get_mif6_cnt((struct sioc_mif_req6 *)data,
		    inp->inp_rtableid);
		NET_UNLOCK_SHARED();
		break;
	default:
		error = ENOTTY;
		break;
	}

	KERNEL_UNLOCK();
	return error;
}

/*
 * returns the packet, byte, rpf-failure count for the source group provided
 */
int
get_sg6_cnt(struct sioc_sg_req6 *req, unsigned int rtableid)
{
	struct rtentry *rt;
	struct mf6c *mf6c;

	rt = mf6c_find(NULL, &req->grp.sin6_addr, rtableid);
	if (rt == NULL) {
		req->pktcnt = req->bytecnt = req->wrong_if = 0xffffffff;
		return EADDRNOTAVAIL;
	}

	req->pktcnt = req->bytecnt = req->wrong_if = 0;
	do {
		mf6c = (struct mf6c *)rt->rt_llinfo;
		if (mf6c == NULL)
			continue;

		req->pktcnt += mf6c->mf6c_pkt_cnt;
		req->bytecnt += mf6c->mf6c_byte_cnt;
		req->wrong_if += mf6c->mf6c_wrong_if;
	} while ((rt = rtable_iterate(rt)) != NULL);

	return 0;
}

/*
 * returns the input and output packet and byte counts on the mif provided
 */
int
get_mif6_cnt(struct sioc_mif_req6 *req, unsigned int rtableid)
{
	struct ifnet *ifp;
	struct mif6 *m6;

	if ((ifp = mrt6_iflookupbymif(req->mifi, rtableid)) == NULL)
		return EINVAL;

	m6 = (struct mif6 *)ifp->if_mcast6;
	req->icount = m6->m6_pkt_in;
	req->ocount = m6->m6_pkt_out;
	req->ibytes = m6->m6_bytes_in;
	req->obytes = m6->m6_bytes_out;

	return 0;
}

int
mrt6_sysctl_mif(void *oldp, size_t *oldlenp)
{
	TAILQ_HEAD(, ifnet) if_tmplist =
	    TAILQ_HEAD_INITIALIZER(if_tmplist);
	struct ifnet *ifp;
	caddr_t where = oldp;
	size_t needed, given;
	struct mif6 *mifp;
	struct mif6info minfo;
	int error = 0;

	given = *oldlenp;
	needed = 0;
	memset(&minfo, 0, sizeof minfo);

	rw_enter_write(&if_tmplist_lock);
	NET_LOCK_SHARED();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_mcast6 != NULL) {
			if_ref(ifp);
			TAILQ_INSERT_TAIL(&if_tmplist, ifp, if_tmplist);
		}
	}
	NET_UNLOCK_SHARED();

	TAILQ_FOREACH (ifp, &if_tmplist, if_tmplist) {
		NET_LOCK_SHARED();
		if ((mifp = (struct mif6 *)ifp->if_mcast6) == NULL) {
			NET_UNLOCK_SHARED();
			continue;
		}

		minfo.m6_mifi = mifp->m6_mifi;
		minfo.m6_flags = mifp->m6_flags;
		minfo.m6_lcl_addr = mifp->m6_lcl_addr;
		minfo.m6_ifindex = ifp->if_index;
		minfo.m6_pkt_in = mifp->m6_pkt_in;
		minfo.m6_pkt_out = mifp->m6_pkt_out;
		minfo.m6_bytes_in = mifp->m6_bytes_in;
		minfo.m6_bytes_out = mifp->m6_bytes_out;
		minfo.m6_rate_limit = mifp->m6_rate_limit;
		NET_UNLOCK_SHARED();

		needed += sizeof(minfo);
		if (where && needed <= given) {
			error = copyout(&minfo, where, sizeof(minfo));
			if (error)
				break;
			where += sizeof(minfo);
		}
	}

	while ((ifp = TAILQ_FIRST(&if_tmplist))) {
		TAILQ_REMOVE(&if_tmplist, ifp, if_tmplist);
		if_put(ifp);
	}

	rw_exit_write(&if_tmplist_lock);

	if (error)
		return (error);

	if (where) {
		*oldlenp = needed;
		if (given < needed)
			return (ENOMEM);
	} else
		*oldlenp = (11 * needed) / 10;

	return (0);
}

struct mf6csysctlarg {
	struct mf6cinfo	*ms6a_minfos;
	size_t		 ms6a_len;
	size_t		 ms6a_needed;
};

int
mrt6_rtwalk_mf6csysctl(struct rtentry *rt, void *arg, unsigned int rtableid)
{
	struct mf6c		*mf6c = (struct mf6c *)rt->rt_llinfo;
	struct mf6csysctlarg	*msa = arg;
	struct ifnet		*ifp;
	struct mif6		*m6;
	struct mf6cinfo		*minfo;
	int			 new = 0;

	/* Skip entries being removed. */
	if (mf6c == NULL)
		return 0;

	/* Skip non-multicast routes. */
	if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
	    (RTF_HOST | RTF_MULTICAST))
		return 0;

	/* User just asked for the output size. */
	if (msa->ms6a_minfos == NULL) {
		msa->ms6a_needed += sizeof(*minfo);
		return 0;
	}

	/* Skip route with invalid interfaces. */
	if ((ifp = if_get(rt->rt_ifidx)) == NULL)
		return 0;
	if ((m6 = (struct mif6 *)ifp->if_mcast6) == NULL) {
		if_put(ifp);
		return 0;
	}

	for (minfo = msa->ms6a_minfos;
	    (uint8_t *)(minfo + 1) <=
	    (uint8_t *)msa->ms6a_minfos + msa->ms6a_len;
	    minfo++) {
		/* Find a new entry or update old entry. */
		if (!IN6_ARE_ADDR_EQUAL(&minfo->mf6c_origin.sin6_addr,
		    &satosin6(rt->rt_gateway)->sin6_addr) ||
		    !IN6_ARE_ADDR_EQUAL(&minfo->mf6c_mcastgrp.sin6_addr,
		    &satosin6(rt_key(rt))->sin6_addr)) {
			if (!IN6_IS_ADDR_UNSPECIFIED(
			    &minfo->mf6c_origin.sin6_addr) ||
			    !IN6_IS_ADDR_UNSPECIFIED(
			    &minfo->mf6c_mcastgrp.sin6_addr))
				continue;

			new = 1;
		}

		minfo->mf6c_origin = *satosin6(rt->rt_gateway);
		minfo->mf6c_mcastgrp = *satosin6(rt_key(rt));
		minfo->mf6c_parent = mf6c->mf6c_parent;
		minfo->mf6c_pkt_cnt += mf6c->mf6c_pkt_cnt;
		minfo->mf6c_byte_cnt += mf6c->mf6c_byte_cnt;
		IF_SET(m6->m6_mifi, &minfo->mf6c_ifset);
		break;
	}

	if (new != 0)
		msa->ms6a_needed += sizeof(*minfo);

	if_put(ifp);

	return 0;
}

int
mrt6_sysctl_mrt6stat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[mrt6s_ncounters];
	struct mrt6stat mrt6stat;
	int i = 0;

#define ASSIGN(field)  do { mrt6stat.field = counters[i++]; } while (0)

	memset(&mrt6stat, 0, sizeof mrt6stat);
	counters_read(mrt6counters, counters, nitems(counters), NULL);

	ASSIGN(mrt6s_mfc_lookups);
	ASSIGN(mrt6s_mfc_misses);
	ASSIGN(mrt6s_upcalls);
	ASSIGN(mrt6s_no_route);
	ASSIGN(mrt6s_bad_tunnel);
	ASSIGN(mrt6s_cant_tunnel);
	ASSIGN(mrt6s_wrong_if);
	ASSIGN(mrt6s_upq_ovflw);
	ASSIGN(mrt6s_cache_cleanups);
	ASSIGN(mrt6s_drop_sel);
	ASSIGN(mrt6s_q_overflow);
	ASSIGN(mrt6s_pkt2large);
	ASSIGN(mrt6s_upq_sockfull);

#undef ASSIGN

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &mrt6stat, sizeof(mrt6stat)));
}

int
mrt6_sysctl_mfc(void *oldp, size_t *oldlenp)
{
	unsigned int		 rtableid;
	int			 error;
	struct mf6csysctlarg	 msa;

	if (oldp != NULL && *oldlenp > MAXPHYS)
		return EINVAL;

	memset(&msa, 0, sizeof(msa));
	if (oldp != NULL && *oldlenp > 0) {
		msa.ms6a_minfos = malloc(*oldlenp, M_TEMP, M_WAITOK | M_ZERO);
		msa.ms6a_len = *oldlenp;
	}

	NET_LOCK();
	for (rtableid = 0; rtableid <= RT_TABLEID_MAX; rtableid++) {
		rtable_walk(rtableid, AF_INET6, NULL, mrt6_rtwalk_mf6csysctl,
		    &msa);
	}
	NET_UNLOCK();

	if (msa.ms6a_minfos != NULL && msa.ms6a_needed > 0 &&
	    (error = copyout(msa.ms6a_minfos, oldp, msa.ms6a_needed)) != 0) {
		free(msa.ms6a_minfos, M_TEMP, msa.ms6a_len);
		return error;
	}

	free(msa.ms6a_minfos, M_TEMP, msa.ms6a_len);
	*oldlenp = msa.ms6a_needed;

	return 0;
}

/*
 * Enable multicast routing
 */
int
ip6_mrouter_init(struct socket *so, int v, int cmd)
{
	struct inpcb *inp = sotoinpcb(so);
	unsigned int rtableid = inp->inp_rtableid;

	if (so->so_type != SOCK_RAW ||
	    so->so_proto->pr_protocol != IPPROTO_ICMPV6)
		return (EOPNOTSUPP);

	if (v != 1)
		return (ENOPROTOOPT);

	if (ip6_mrouter[rtableid] != NULL)
		return (EADDRINUSE);

	ip6_mrouter[rtableid] = so;
	ip6_mrouter_ver = cmd;

	return (0);
}

int
mrouter6_rtwalk_delete(struct rtentry *rt, void *arg, unsigned int rtableid)
{
	/* Skip non-multicast routes. */
	if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
	    (RTF_HOST | RTF_MULTICAST))
		return 0;

	return EEXIST;
}

/*
 * Disable multicast routing
 */
int
ip6_mrouter_done(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);
	struct ifnet *ifp;
	unsigned int rtableid = inp->inp_rtableid;
	int error;

	NET_ASSERT_LOCKED();

	/* Delete all remaining installed multicast routes. */
	do {
		struct rtentry *rt = NULL;

		error = rtable_walk(rtableid, AF_INET6, &rt,
		    mrouter6_rtwalk_delete, NULL);
		if (rt != NULL && error == EEXIST) {
			mrt6_mcast_del(rt, rtableid);
			error = EAGAIN;
		}
		rtfree(rt);
	} while (error == EAGAIN);

	/* Unregister all interfaces in the domain. */
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rtableid)
			continue;

		ip6_mrouter_detach(ifp);
	}

	ip6_mrouter[inp->inp_rtableid] = NULL;
	ip6_mrouter_ver = 0;

	return 0;
}

void
ip6_mrouter_detach(struct ifnet *ifp)
{
	struct mif6 *m6 = (struct mif6 *)ifp->if_mcast6;
	struct in6_ifreq ifr;

	if (m6 == NULL)
		return;

	ifp->if_mcast6 = NULL;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_addr.sin6_family = AF_INET6;
	ifr.ifr_addr.sin6_addr = in6addr_any;
	KERNEL_LOCK();
	(*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)&ifr);
	KERNEL_UNLOCK();

	free(m6, M_MRTABLE, sizeof(*m6));
}

/*
 * Add a mif to the mif table
 */
int
add_m6if(struct socket *so, struct mif6ctl *mifcp)
{
	struct inpcb *inp = sotoinpcb(so);
	struct mif6 *mifp;
	struct ifnet *ifp;
	struct in6_ifreq ifr;
	int error;
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	if (mifcp->mif6c_mifi >= MAXMIFS)
		return EINVAL;

	if (mrt6_iflookupbymif(mifcp->mif6c_mifi, rtableid) != NULL)
		return EADDRINUSE; /* XXX: is it appropriate? */

	{
		ifp = if_get(mifcp->mif6c_pifi);
		if (ifp == NULL)
			return ENXIO;

		/* Make sure the interface supports multicast */
		if ((ifp->if_flags & IFF_MULTICAST) == 0) {
			if_put(ifp);
			return EOPNOTSUPP;
		}

		/*
		 * Enable promiscuous reception of all IPv6 multicasts
		 * from the interface.
		 */
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_addr.sin6_family = AF_INET6;
		ifr.ifr_addr.sin6_addr = in6addr_any;
		KERNEL_LOCK();
		error = (*ifp->if_ioctl)(ifp, SIOCADDMULTI, (caddr_t)&ifr);
		KERNEL_UNLOCK();

		if (error) {
			if_put(ifp);
			return error;
		}
	}

	mifp = malloc(sizeof(*mifp), M_MRTABLE, M_WAITOK | M_ZERO);
	ifp->if_mcast6	   = (caddr_t)mifp;
	mifp->m6_mifi	   = mifcp->mif6c_mifi;
	mifp->m6_flags     = mifcp->mif6c_flags;
#ifdef notyet
	/* scaling up here allows division by 1024 in critical code */
	mifp->m6_rate_limit = mifcp->mif6c_rate_limit * 1024 / 1000;
#endif

	if_put(ifp);

	return 0;
}

/*
 * Delete a mif from the mif table
 */
int
del_m6if(struct socket *so, mifi_t *mifip)
{
	struct inpcb *inp = sotoinpcb(so);
	struct ifnet *ifp;

	NET_ASSERT_LOCKED();

	if (*mifip >= MAXMIFS)
		return EINVAL;
	if ((ifp = mrt6_iflookupbymif(*mifip, inp->inp_rtableid)) == NULL)
		return EINVAL;

	ip6_mrouter_detach(ifp);

	return 0;
}

int
mf6c_add_route(struct ifnet *ifp, struct sockaddr *origin,
    struct sockaddr *group, struct mf6cctl *mf6cc, int wait)
{
	struct rtentry *rt;
	struct mf6c *mf6c;
	unsigned int rtableid = ifp->if_rdomain;
#ifdef MCAST_DEBUG
	char bsrc[INET6_ADDRSTRLEN], bdst[INET6_ADDRSTRLEN];
#endif /* MCAST_DEBUG */

	rt = mrt6_mcast_add(ifp, group);
	if (rt == NULL)
		return ENOENT;

	mf6c = malloc(sizeof(*mf6c), M_MRTABLE, wait | M_ZERO);
	if (mf6c == NULL) {
		DPRINTF("origin %s group %s parent %d (%s) malloc failed",
		    inet_ntop(AF_INET6, origin, bsrc, sizeof(bsrc)),
		    inet_ntop(AF_INET6, group, bdst, sizeof(bdst)),
		    mf6cc->mf6cc_parent, ifp->if_xname);
		mrt6_mcast_del(rt, rtableid);
		rtfree(rt);
		return ENOMEM;
	}

	rt->rt_llinfo = (caddr_t)mf6c;
	rt_timer_add(rt, &ip6_mrouterq, rtableid);
	mf6c->mf6c_parent = mf6cc->mf6cc_parent;
	rtfree(rt);

	return 0;
}

void
mf6c_update(struct mf6cctl *mf6cc, int wait, unsigned int rtableid)
{
	struct rtentry *rt;
	struct mf6c *mf6c;
	struct ifnet *ifp;
	struct sockaddr_in6 osin6, gsin6;
	mifi_t mifi;
#ifdef MCAST_DEBUG
	char bdst[INET6_ADDRSTRLEN];
#endif /* MCAST_DEBUG */

	memset(&osin6, 0, sizeof(osin6));
	osin6.sin6_family = AF_INET6;
	osin6.sin6_len = sizeof(osin6);
	osin6.sin6_addr = mf6cc->mf6cc_origin.sin6_addr;

	memset(&gsin6, 0, sizeof(gsin6));
	gsin6.sin6_family = AF_INET6;
	gsin6.sin6_len = sizeof(gsin6);
	gsin6.sin6_addr = mf6cc->mf6cc_mcastgrp.sin6_addr;

	for (mifi = 0; mifi < MAXMIFS; mifi++) {
		if (mifi == mf6cc->mf6cc_parent)
			continue;

		/* Test for mif existence and then update the entry. */
		if ((ifp = mrt6_iflookupbymif(mifi, rtableid)) == NULL)
			continue;

		rt = mf6c_find(ifp, &mf6cc->mf6cc_mcastgrp.sin6_addr, rtableid);

		/* mif not configured or removed. */
		if (!IF_ISSET(mifi, &mf6cc->mf6cc_ifset)) {
			/* Route doesn't exist, nothing to do. */
			if (rt == NULL)
				continue;

			DPRINTF("del route (group %s) for mif %d (%s)",
			    inet_ntop(AF_INET6,
			    &mf6cc->mf6cc_mcastgrp.sin6_addr, bdst,
			    sizeof(bdst)), mifi, ifp->if_xname);
			mrt6_mcast_del(rt, rtableid);
			rtfree(rt);
			continue;
		}

		/* Route exists, look for changes. */
		if (rt != NULL) {
			mf6c = (struct mf6c *)rt->rt_llinfo;
			/* Skip route being deleted. */
			if (mf6c == NULL) {
				rtfree(rt);
				continue;
			}

			/* No new changes to apply. */
			if (mf6cc->mf6cc_parent == mf6c->mf6c_parent) {
				rtfree(rt);
				continue;
			}

			DPRINTF("update route (group %s) for mif %d (%s)",
			    inet_ntop(AF_INET6,
			    &mf6cc->mf6cc_mcastgrp.sin6_addr, bdst,
			    sizeof(bdst)), mifi, ifp->if_xname);

			mf6c->mf6c_parent = mf6cc->mf6cc_parent;
			rtfree(rt);
			continue;
		}

		DPRINTF("add route (group %s) for mif %d (%s)",
		    inet_ntop(AF_INET6, &mf6cc->mf6cc_mcastgrp.sin6_addr,
		    bdst, sizeof(bdst)), mifi, ifp->if_xname);

		mf6c_add_route(ifp, sin6tosa(&osin6), sin6tosa(&gsin6),
		    mf6cc, wait);
	}

	/* Create route for the parent interface. */
	if ((ifp = mrt6_iflookupbymif(mf6cc->mf6cc_parent,
	    rtableid)) == NULL) {
		DPRINTF("failed to find upstream interface %d",
		    mf6cc->mf6cc_parent);
		return;
	}

	/* We already have a route, nothing to do here. */
	if ((rt = mf6c_find(ifp, &mf6cc->mf6cc_mcastgrp.sin6_addr,
	    rtableid)) != NULL) {
		rtfree(rt);
		return;
	}

	DPRINTF("add upstream route (group %s) for if %s",
	    inet_ntop(AF_INET6, &mf6cc->mf6cc_mcastgrp.sin6_addr,
	    bdst, sizeof(bdst)), ifp->if_xname);
	mf6c_add_route(ifp, sin6tosa(&osin6), sin6tosa(&gsin6), mf6cc, wait);
}

int
mf6c_add(struct mf6cctl *mfccp, struct in6_addr *origin,
    struct in6_addr *group, int vidx, unsigned int rtableid, int wait)
{
	struct ifnet *ifp;
	struct mif6 *m6;
	struct mf6cctl mf6cc;

	ifp = mrt6_iflookupbymif(vidx, rtableid);
	if (ifp == NULL ||
	    (m6 = (struct mif6 *)ifp->if_mcast6) == NULL)
		return ENOENT;

	memset(&mf6cc, 0, sizeof(mf6cc));
	if (mfccp == NULL) {
		mf6cc.mf6cc_origin.sin6_family = AF_INET6;
		mf6cc.mf6cc_origin.sin6_len = sizeof(mf6cc.mf6cc_origin);
		mf6cc.mf6cc_origin.sin6_addr = *origin;
		mf6cc.mf6cc_mcastgrp.sin6_family = AF_INET6;
		mf6cc.mf6cc_mcastgrp.sin6_len = sizeof(mf6cc.mf6cc_mcastgrp);
		mf6cc.mf6cc_mcastgrp.sin6_addr = *group;
		mf6cc.mf6cc_parent = vidx;
	} else
		memcpy(&mf6cc, mfccp, sizeof(mf6cc));

	mf6c_update(&mf6cc, wait, rtableid);

	return 0;
}

int
add_m6fc(struct socket *so, struct mf6cctl *mfccp)
{
	struct inpcb *inp = sotoinpcb(so);
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	return mf6c_add(mfccp, &mfccp->mf6cc_origin.sin6_addr,
	    &mfccp->mf6cc_mcastgrp.sin6_addr, mfccp->mf6cc_parent,
	    rtableid, M_WAITOK);
}

int
del_m6fc(struct socket *so, struct mf6cctl *mfccp)
{
	struct inpcb *inp = sotoinpcb(so);
	struct rtentry *rt;
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	while ((rt = mf6c_find(NULL, &mfccp->mf6cc_mcastgrp.sin6_addr,
	    rtableid)) != NULL) {
		mrt6_mcast_del(rt, rtableid);
		rtfree(rt);
	}

	return 0;
}

int
socket6_send(struct socket *so, struct mbuf *mm, struct sockaddr_in6 *src)
{
	if (so != NULL) {
		int ret;

		mtx_enter(&so->so_rcv.sb_mtx);
		ret = sbappendaddr(&so->so_rcv, sin6tosa(src), mm, NULL);
		mtx_leave(&so->so_rcv.sb_mtx);

		if (ret != 0) {
			sorwakeup(so);
			return 0;
		}
	}
	m_freem(mm);
	return -1;
}

/*
 * IPv6 multicast forwarding function. This function assumes that the packet
 * pointed to by "ip6" has arrived on (or is about to be sent to) the interface
 * pointed to by "ifp", and the packet is to be relayed to other networks
 * that have members of the packet's destination IPv6 multicast group.
 *
 * The packet is returned unscathed to the caller, unless it is
 * erroneous, in which case a non-zero return value tells the caller to
 * discard it.
 */
int
ip6_mforward(struct ip6_hdr *ip6, struct ifnet *ifp, struct mbuf *m, int flags)
{
	struct rtentry *rt;
	struct mif6 *mifp;
	struct mbuf *mm;
	struct sockaddr_in6 sin6;
	unsigned int rtableid = ifp->if_rdomain;

	NET_ASSERT_LOCKED();

	/*
	 * Don't forward a packet with Hop limit of zero or one,
	 * or a packet destined to a local-only group.
	 */
	if (ip6->ip6_hlim <= 1 || IN6_IS_ADDR_MC_INTFACELOCAL(&ip6->ip6_dst) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&ip6->ip6_dst))
		return 0;
	ip6->ip6_hlim--;

	/*
	 * Source address check: do not forward packets with unspecified
	 * source. It was discussed in July 2000, on ipngwg mailing list.
	 * This is rather more serious than unicast cases, because some
	 * MLD packets can be sent with the unspecified source address
	 * (although such packets must normally set 1 to the hop limit field).
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
		ip6stat_inc(ip6s_cantforward);
		return 0;
	}

	/*
	 * Determine forwarding mifs from the forwarding cache table
	 */
	mrt6stat_inc(mrt6s_mfc_lookups);
	rt = mf6c_find(NULL, &ip6->ip6_dst, rtableid);

	/* Entry exists, so forward if necessary */
	if (rt) {
		return (ip6_mdq(m, ifp, rt, flags));
	} else {
		/*
		 * If we don't have a route for packet's origin,
		 * Make a copy of the packet &
		 * send message to routing daemon
		 */

		mrt6stat_inc(mrt6s_mfc_misses);
		mrt6stat_inc(mrt6s_no_route);

		{
			struct mrt6msg *im;

			if ((mifp = (struct mif6 *)ifp->if_mcast6) == NULL)
				return EHOSTUNREACH;

			/*
			 * Make a copy of the header to send to the user
			 * level process
			 */
			mm = m_copym(m, 0, sizeof(struct ip6_hdr), M_NOWAIT);
			if (mm == NULL)
				return ENOBUFS;

			/*
			 * Send message to routing daemon
			 */
			(void)memset(&sin6, 0, sizeof(sin6));
			sin6.sin6_len = sizeof(sin6);
			sin6.sin6_family = AF_INET6;
			sin6.sin6_addr = ip6->ip6_src;

			im = NULL;
			switch (ip6_mrouter_ver) {
			case MRT6_INIT:
				im = mtod(mm, struct mrt6msg *);
				im->im6_msgtype = MRT6MSG_NOCACHE;
				im->im6_mbz = 0;
				im->im6_mif = mifp->m6_mifi;
				break;
			default:
				m_freem(mm);
				return EINVAL;
			}

			if (socket6_send(ip6_mrouter[rtableid], mm,
			    &sin6) < 0) {
				log(LOG_WARNING, "ip6_mforward: ip6_mrouter "
				    "socket queue full\n");
				mrt6stat_inc(mrt6s_upq_sockfull);
				return ENOBUFS;
			}

			mrt6stat_inc(mrt6s_upcalls);

			mf6c_add(NULL, &ip6->ip6_src, &ip6->ip6_dst,
			    mifp->m6_mifi, rtableid, M_NOWAIT);
		}

		return 0;
	}
}

void
mf6c_expire_route(struct rtentry *rt, u_int rtableid)
{
	struct mf6c *mf6c = (struct mf6c *)rt->rt_llinfo;
#ifdef MCAST_DEBUG
	char bsrc[INET6_ADDRSTRLEN], bdst[INET6_ADDRSTRLEN];
#endif /* MCAST_DEBUG */

	/* Skip entry being deleted. */
	if (mf6c == NULL)
		return;

	DPRINTF("origin %s group %s interface %d expire %s",
	    inet_ntop(AF_INET6, &satosin6(rt->rt_gateway)->sin6_addr,
	    bsrc, sizeof(bsrc)),
	    inet_ntop(AF_INET6, &satosin6(rt_key(rt))->sin6_addr,
	    bdst, sizeof(bdst)), rt->rt_ifidx,
	    mf6c->mf6c_expire ? "yes" : "no");

	if (mf6c->mf6c_expire == 0) {
		mf6c->mf6c_expire = 1;
		rt_timer_add(rt, &ip6_mrouterq, rtableid);
		return;
	}

	mrt6_mcast_del(rt, rtableid);
}

/*
 * Packet forwarding routine once entry in the cache is made
 */
int
ip6_mdq(struct mbuf *m, struct ifnet *ifp, struct rtentry *rt, int flags)
{
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct mif6 *m6, *mifp = (struct mif6 *)ifp->if_mcast6;
	struct mf6c *mf6c = (struct mf6c *)rt->rt_llinfo;
	struct ifnet *ifn;
	int plen = m->m_pkthdr.len, ip6_mcast_pmtu_local;

	if (mifp == NULL || mf6c == NULL) {
		rtfree(rt);
		return EHOSTUNREACH;
	}

	/*
	 * Don't forward if it didn't arrive from the parent mif
	 * for its origin.
	 */
	if (mifp->m6_mifi != mf6c->mf6c_parent) {
		/* came in the wrong interface */
		mrt6stat_inc(mrt6s_wrong_if);
		mf6c->mf6c_wrong_if++;
		rtfree(rt);
		return 0;
	}			/* if wrong iif */

	/* If I sourced this packet, it counts as output, else it was input. */
	if (m->m_pkthdr.ph_ifidx == 0) {
		/* XXX: is ph_ifidx really 0 when output?? */
		mifp->m6_pkt_out++;
		mifp->m6_bytes_out += plen;
	} else {
		mifp->m6_pkt_in++;
		mifp->m6_bytes_in += plen;
	}

	/*
	 * For each mif, forward a copy of the packet if there are group
	 * members downstream on the interface.
	 */
	ip6_mcast_pmtu_local = atomic_load_int(&ip6_mcast_pmtu);

	do {
		/* Don't consider non multicast routes. */
		if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
		    (RTF_HOST | RTF_MULTICAST))
			continue;

		mf6c = (struct mf6c *)rt->rt_llinfo;
		if (mf6c == NULL)
			continue;

		mf6c->mf6c_pkt_cnt++;
		mf6c->mf6c_byte_cnt += m->m_pkthdr.len;

		/* Don't let this route expire. */
		mf6c->mf6c_expire = 0;

		if ((ifn = if_get(rt->rt_ifidx)) == NULL)
			continue;

		/* Sanity check: did we configure this? */
		if ((m6 = (struct mif6 *)ifn->if_mcast6) == NULL) {
			if_put(ifn);
			continue;
		}

		/* Don't send in the upstream interface. */
		if (mf6c->mf6c_parent == m6->m6_mifi) {
			if_put(ifn);
			continue;
		}

		/*
		 * check if the outgoing packet is going to break
		 * a scope boundary.
		 */
		if ((mifp->m6_flags & MIFF_REGISTER) == 0 &&
		    (m6->m6_flags & MIFF_REGISTER) == 0 &&
		    (in6_addr2scopeid(ifp->if_index, &ip6->ip6_dst) !=
		    in6_addr2scopeid(ifn->if_index, &ip6->ip6_dst) ||
		    in6_addr2scopeid(ifp->if_index, &ip6->ip6_src) !=
		    in6_addr2scopeid(ifn->if_index, &ip6->ip6_src))) {
			if_put(ifn);
			ip6stat_inc(ip6s_badscope);
			continue;
		}

		m6->m6_pkt_out++;
		m6->m6_bytes_out += plen;

		phyint_send6(ifn, ip6, m, flags, ip6_mcast_pmtu_local);
		if_put(ifn);
	} while ((rt = rtable_iterate(rt)) != NULL);

	return 0;
}

void
phyint_send6(struct ifnet *ifp, struct ip6_hdr *ip6, struct mbuf *m,
    int flags, int mcast_pmtu)
{
	struct mbuf *mb_copy;
	struct sockaddr_in6 *dst6, sin6;
	int error = 0;

	NET_ASSERT_LOCKED();

	/*
	 * Make a new reference to the packet; make sure that
	 * the IPv6 header is actually copied, not just referenced,
	 * so that ip6_output() only scribbles on the copy.
	 */
	mb_copy = m_dup_pkt(m, max_linkhdr, M_NOWAIT);
	if (mb_copy == NULL)
		return;
	/* set MCAST flag to the outgoing packet */
	mb_copy->m_flags |= M_MCAST;

	/*
	 * If we sourced the packet, call ip6_output since we may divide
	 * the packet into fragments when the packet is too big for the
	 * outgoing interface.
	 * Otherwise, we can simply send the packet to the interface
	 * sending queue.
	 */
	if (m->m_pkthdr.ph_ifidx == 0) {
		struct ip6_moptions im6o;

		im6o.im6o_ifidx = ifp->if_index;
		/* XXX: ip6_output will override ip6->ip6_hlim */
		im6o.im6o_hlim = ip6->ip6_hlim;
		im6o.im6o_loop = 1;
		error = ip6_output(mb_copy, NULL, NULL, flags | IPV6_FORWARDING,
		    &im6o, NULL);
		return;
	}

	/*
	 * If we belong to the destination multicast group
	 * on the outgoing interface, loop back a copy.
	 */
	dst6 = &sin6;
	memset(&sin6, 0, sizeof(sin6));
	if (in6_hasmulti(&ip6->ip6_dst, ifp)) {
		dst6->sin6_len = sizeof(struct sockaddr_in6);
		dst6->sin6_family = AF_INET6;
		dst6->sin6_addr = ip6->ip6_dst;
		ip6_mloopback(ifp, m, dst6);
	}
	/*
	 * Put the packet into the sending queue of the outgoing interface
	 * if it would fit in the MTU of the interface.
	 */
	if (mb_copy->m_pkthdr.len <= ifp->if_mtu || ifp->if_mtu < IPV6_MMTU) {
		dst6->sin6_len = sizeof(struct sockaddr_in6);
		dst6->sin6_family = AF_INET6;
		dst6->sin6_addr = ip6->ip6_dst;
		error = ifp->if_output(ifp, mb_copy, sin6tosa(dst6), NULL);
	} else {
		if (mcast_pmtu)
			icmp6_error(mb_copy, ICMP6_PACKET_TOO_BIG, 0,
			    ifp->if_mtu);
		else {
			m_freem(mb_copy); /* simply discard the packet */
		}
	}
}

struct ifnet *
mrt6_iflookupbymif(mifi_t mifi, unsigned int rtableid)
{
	struct mif6	*m6;
	struct ifnet	*ifp;

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rtableid)
			continue;
		if ((m6 = (struct mif6 *)ifp->if_mcast6) == NULL)
			continue;
		if (m6->m6_mifi != mifi)
			continue;

		return ifp;
	}

	return NULL;
}

struct rtentry *
mf6c_find(struct ifnet *ifp, struct in6_addr *group, unsigned int rtableid)
{
	struct rtentry *rt;
	struct sockaddr_in6 msin6;

	memset(&msin6, 0, sizeof(msin6));
	msin6.sin6_family = AF_INET6;
	msin6.sin6_len = sizeof(msin6);
	msin6.sin6_addr = *group;

	rt = rtalloc(sin6tosa(&msin6), 0, rtableid);
	do {
		if (!rtisvalid(rt)) {
			rtfree(rt);
			return NULL;
		}
		if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
		    (RTF_HOST | RTF_MULTICAST))
			continue;
		/* Return first occurrence if interface is not specified. */
		if (ifp == NULL)
			return rt;
		if (rt->rt_ifidx == ifp->if_index)
			return rt;
	} while ((rt = rtable_iterate(rt)) != NULL);

	return NULL;
}

struct rtentry *
mrt6_mcast_add(struct ifnet *ifp, struct sockaddr *group)
{
	struct ifaddr *ifa;
	int rv;
	unsigned int rtableid = ifp->if_rdomain;

	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr->sa_family == AF_INET6)
			break;
	}
	if (ifa == NULL) {
		DPRINTF("ifa == NULL");
		return NULL;
	}

	rv = rt_ifa_add(ifa, RTF_HOST | RTF_MULTICAST | RTF_MPATH, group,
	    ifp->if_rdomain);
	if (rv != 0) {
		DPRINTF("rt_ifa_add failed %d", rv);
		return NULL;
	}

	return mf6c_find(ifp, &satosin6(group)->sin6_addr, rtableid);
}

void
mrt6_mcast_del(struct rtentry *rt, unsigned int rtableid)
{
	struct ifnet *ifp;
	int error;

	/* Remove all timers related to this route. */
	rt_timer_remove_all(rt);

	free(rt->rt_llinfo, M_MRTABLE, sizeof(struct mf6c));
	rt->rt_llinfo = NULL;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return;
	error = rtdeletemsg(rt, ifp, rtableid);
	if_put(ifp);

	if (error)
		DPRINTF("delete route error %d\n", error);
}
