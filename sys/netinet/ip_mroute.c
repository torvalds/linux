/*	$OpenBSD: ip_mroute.c,v 1.150 2025/07/19 16:40:40 mvs Exp $	*/
/*	$NetBSD: ip_mroute.c,v 1.85 2004/04/26 01:31:57 matt Exp $	*/

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
 * Modified by Charles M. Hannum, NetBSD, May 1995.
 * Modified by Ahmed Helmy, SGI, June 1996
 * Modified by George Edmond Eddy (Rusty), ISI, February 1998
 * Modified by Pavlin Radoslavov, USC/ISI, May 1998, August 1999, October 2000
 * Modified by Hitoshi Asaeda, WIDE, August 2000
 * Modified by Pavlin Radoslavov, ICSI, October 2002
 *
 * MROUTING Revision: 1.2
 * advanced API support, bandwidth metering and signaling
 */

#include <sys/param.h>
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
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/igmp.h>
#include <netinet/ip_mroute.h>

/*
 * Locks used to protect data:
 *	I	immutable after creation
 */

/* #define MCAST_DEBUG */

#ifdef MCAST_DEBUG
int mcast_debug = 1;
#define DPRINTF(fmt, args...)						\
	do {								\
		if (mcast_debug)					\
			printf("%s:%d " fmt "\n",			\
			    __func__, __LINE__, ## args);		\
	} while (0)
#else
#define DPRINTF(fmt, args...)			\
	do { } while (0)
#endif

/*
 * Globals.  All but ip_mrouter and ip_mrtproto could be static,
 * except for netstat or debugging purposes.
 */
struct socket	*ip_mrouter[RT_TABLEID_MAX + 1];
struct rttimer_queue ip_mrouterq;
uint64_t	 mrt_count[RT_TABLEID_MAX + 1];
int		ip_mrtproto = IGMP_DVMRP;    /* [I] for netstat only */

struct cpumem *mrtcounters;

struct rtentry	*mfc_find(struct ifnet *, struct in_addr *, unsigned int);
int get_sg_cnt(unsigned int, struct sioc_sg_req *);
int get_vif_cnt(unsigned int, struct sioc_vif_req *);
int mrt_rtwalk_mfcsysctl(struct rtentry *, void *, unsigned int);
int ip_mrouter_init(struct socket *, struct mbuf *);
int mrouter_rtwalk_delete(struct rtentry *, void *, unsigned int);
int get_version(struct mbuf *);
int add_vif(struct socket *, struct mbuf *);
int del_vif(struct socket *, struct mbuf *);
void update_mfc_params(struct mfcctl2 *, int, unsigned int);
void mfc_expire_route(struct rtentry *, u_int);
int mfc_add(struct mfcctl2 *, struct in_addr *, struct in_addr *,
    int, unsigned int, int);
int add_mfc(struct socket *, struct mbuf *);
int del_mfc(struct socket *, struct mbuf *);
int set_api_config(struct socket *, struct mbuf *); /* chose API capabilities */
int get_api_support(struct mbuf *);
int get_api_config(struct mbuf *);
int socket_send(struct socket *, struct mbuf *,
			    struct sockaddr_in *);
int ip_mdq(struct mbuf *, struct ifnet *, struct rtentry *, int);
struct ifnet *if_lookupbyvif(vifi_t, unsigned int);
struct rtentry *rt_mcast_add(struct ifnet *, struct sockaddr *,
    struct sockaddr *);
void mrt_mcast_del(struct rtentry *, unsigned int);

/*
 * Kernel multicast routing API capabilities and setup.
 * If more API capabilities are added to the kernel, they should be
 * recorded in `mrt_api_support'.
 */
static const u_int32_t mrt_api_support = (MRT_MFC_FLAGS_DISABLE_WRONGVIF |
					  MRT_MFC_RP);
static u_int32_t mrt_api_config = 0;

/*
 * Find a route for a given Multicast group address.
 * Type of service parameter to be added in the future!!!
 * Statistics are updated by the caller if needed (mrts_mfc_lookups and
 * mrts_mfc_misses)
 */
struct rtentry *
mfc_find(struct ifnet *ifp, struct in_addr *group, unsigned int rtableid)
{
	struct rtentry		*rt;
	struct sockaddr_in	 msin;

	memset(&msin, 0, sizeof(msin));
	msin.sin_len = sizeof(msin);
	msin.sin_family = AF_INET;
	msin.sin_addr = *group;

	rt = rtalloc(sintosa(&msin), 0, rtableid);
	do {
		if (!rtisvalid(rt)) {
			rtfree(rt);
			return NULL;
		}
		/* Don't consider non multicast routes. */
		if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
		    (RTF_HOST | RTF_MULTICAST))
			continue;
		/* Return first occurrence if interface is not specified. */
		if (ifp == NULL)
			return (rt);
		if (rt->rt_ifidx == ifp->if_index)
			return (rt);
	} while ((rt = rtable_iterate(rt)) != NULL);

	return (NULL);
}

/*
 * Handle MRT setsockopt commands to modify the multicast routing tables.
 */
int
ip_mrouter_set(struct socket *so, int optname, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	int error;

	if (optname != MRT_INIT &&
	    so != ip_mrouter[inp->inp_rtableid])
		error = ENOPROTOOPT;
	else
		switch (optname) {
		case MRT_INIT:
			error = ip_mrouter_init(so, m);
			break;
		case MRT_DONE:
			error = ip_mrouter_done(so);
			break;
		case MRT_ADD_VIF:
			error = add_vif(so, m);
			break;
		case MRT_DEL_VIF:
			error = del_vif(so, m);
			break;
		case MRT_ADD_MFC:
			error = add_mfc(so, m);
			break;
		case MRT_DEL_MFC:
			error = del_mfc(so, m);
			break;
		case MRT_API_CONFIG:
			error = set_api_config(so, m);
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}

	return (error);
}

/*
 * Handle MRT getsockopt commands
 */
int
ip_mrouter_get(struct socket *so, int optname, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	int error;

	if (so != ip_mrouter[inp->inp_rtableid])
		error = ENOPROTOOPT;
	else {
		switch (optname) {
		case MRT_VERSION:
			error = get_version(m);
			break;
		case MRT_API_SUPPORT:
			error = get_api_support(m);
			break;
		case MRT_API_CONFIG:
			error = get_api_config(m);
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}
	}

	return (error);
}

void
mrt_init(void)
{
	mrtcounters = counters_alloc(mrts_ncounters);

	rt_timer_queue_init(&ip_mrouterq, MCAST_EXPIRE_FREQUENCY,
	    &mfc_expire_route);
}

/*
 * Handle ioctl commands to obtain information from the cache
 */
int
mrt_ioctl(struct socket *so, u_long cmd, caddr_t data)
{
	struct inpcb *inp = sotoinpcb(so);
	int error;

	if (inp == NULL)
		return (ENOTCONN);

	KERNEL_LOCK();

	if (so != ip_mrouter[inp->inp_rtableid])
		error = EINVAL;
	else
		switch (cmd) {
		case SIOCGETVIFCNT:
			NET_LOCK_SHARED();
			error = get_vif_cnt(inp->inp_rtableid,
			    (struct sioc_vif_req *)data);
			NET_UNLOCK_SHARED();
			break;
		case SIOCGETSGCNT:
			NET_LOCK_SHARED();
			error = get_sg_cnt(inp->inp_rtableid,
			    (struct sioc_sg_req *)data);
			NET_UNLOCK_SHARED();
			break;
		default:
			error = ENOTTY;
			break;
		}

	KERNEL_UNLOCK();
	return (error);
}

/*
 * returns the packet, byte, rpf-failure count for the source group provided
 */
int
get_sg_cnt(unsigned int rtableid, struct sioc_sg_req *req)
{
	struct rtentry *rt;
	struct mfc *mfc;

	rt = mfc_find(NULL, &req->grp, rtableid);
	if (rt == NULL) {
		req->pktcnt = req->bytecnt = req->wrong_if = 0xffffffff;
		return (EADDRNOTAVAIL);
	}

	req->pktcnt = req->bytecnt = req->wrong_if = 0;
	do {
		/* Don't consider non multicast routes. */
		if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
		    (RTF_HOST | RTF_MULTICAST))
			continue;

		mfc = (struct mfc *)rt->rt_llinfo;
		if (mfc == NULL)
			continue;

		req->pktcnt += mfc->mfc_pkt_cnt;
		req->bytecnt += mfc->mfc_byte_cnt;
		req->wrong_if += mfc->mfc_wrong_if;
	} while ((rt = rtable_iterate(rt)) != NULL);

	return (0);
}

/*
 * returns the input and output packet and byte counts on the vif provided
 */
int
get_vif_cnt(unsigned int rtableid, struct sioc_vif_req *req)
{
	struct ifnet	*ifp;
	struct vif	*v;
	vifi_t		 vifi = req->vifi;

	if ((ifp = if_lookupbyvif(vifi, rtableid)) == NULL)
		return (EINVAL);

	v = (struct vif *)ifp->if_mcast;
	req->icount = v->v_pkt_in;
	req->ocount = v->v_pkt_out;
	req->ibytes = v->v_bytes_in;
	req->obytes = v->v_bytes_out;

	return (0);
}

int
mrt_sysctl_vif(void *oldp, size_t *oldlenp)
{
	TAILQ_HEAD(, ifnet) if_tmplist =
	    TAILQ_HEAD_INITIALIZER(if_tmplist);
	caddr_t where = oldp;
	size_t needed, given;
	struct ifnet *ifp;
	struct vif *vifp;
	struct vifinfo vinfo;
	int error = 0;

	given = *oldlenp;
	needed = 0;
	memset(&vinfo, 0, sizeof vinfo);

	rw_enter_write(&if_tmplist_lock);
	NET_LOCK_SHARED();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_mcast != NULL) {
			if_ref(ifp);
			TAILQ_INSERT_TAIL(&if_tmplist, ifp, if_tmplist);
		}
	}
	NET_UNLOCK_SHARED();

	TAILQ_FOREACH (ifp, &if_tmplist, if_tmplist) {
		NET_LOCK_SHARED();
		if ((vifp = (struct vif *)ifp->if_mcast) == NULL) {
			NET_UNLOCK_SHARED();
			continue;
		}

		vinfo.v_vifi = vifp->v_id;
		vinfo.v_flags = vifp->v_flags;
		vinfo.v_threshold = vifp->v_threshold;
		vinfo.v_lcl_addr = vifp->v_lcl_addr;
		vinfo.v_rmt_addr = vifp->v_rmt_addr;
		vinfo.v_pkt_in = vifp->v_pkt_in;
		vinfo.v_pkt_out = vifp->v_pkt_out;
		vinfo.v_bytes_in = vifp->v_bytes_in;
		vinfo.v_bytes_out = vifp->v_bytes_out;
		NET_UNLOCK_SHARED();

		needed += sizeof(vinfo);
		if (where && needed <= given) {
			error = copyout(&vinfo, where, sizeof(vinfo));
			if (error)
				break;
			where += sizeof(vinfo);
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

struct mfcsysctlarg {
	struct mfcinfo	*msa_minfos;
	size_t		 msa_len;
	size_t		 msa_needed;
};

int
mrt_rtwalk_mfcsysctl(struct rtentry *rt, void *arg, unsigned int rtableid)
{
	struct mfc		*mfc = (struct mfc *)rt->rt_llinfo;
	struct mfcsysctlarg	*msa = (struct mfcsysctlarg *)arg;
	struct ifnet		*ifp;
	struct vif		*v;
	struct mfcinfo		*minfo;
	int			 new = 0;

	/* Skip entries being removed. */
	if (mfc == NULL)
		return (0);

	/* Skip non-multicast routes. */
	if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
	    (RTF_HOST | RTF_MULTICAST))
		return (0);

	/* User just asked for the output size. */
	if (msa->msa_minfos == NULL) {
		msa->msa_needed += sizeof(*minfo);
		return (0);
	}

	/* Skip route with invalid interfaces. */
	if ((ifp = if_get(rt->rt_ifidx)) == NULL)
		return (0);
	if ((v = (struct vif *)ifp->if_mcast) == NULL) {
		if_put(ifp);
		return (0);
	}

	for (minfo = msa->msa_minfos;
	    (uint8_t *)(minfo + 1) <=
	    (uint8_t *)msa->msa_minfos + msa->msa_len;
	    minfo++) {
		/* Find a new entry or update old entry. */
		if (minfo->mfc_origin.s_addr !=
		    satosin(rt->rt_gateway)->sin_addr.s_addr ||
		    minfo->mfc_mcastgrp.s_addr !=
		    satosin(rt_key(rt))->sin_addr.s_addr) {
			if (minfo->mfc_origin.s_addr != 0 ||
			    minfo->mfc_mcastgrp.s_addr != 0)
				continue;

			new = 1;
		}

		minfo->mfc_origin = satosin(rt->rt_gateway)->sin_addr;
		minfo->mfc_mcastgrp = satosin(rt_key(rt))->sin_addr;
		minfo->mfc_parent = mfc->mfc_parent;
		minfo->mfc_pkt_cnt += mfc->mfc_pkt_cnt;
		minfo->mfc_byte_cnt += mfc->mfc_byte_cnt;
		minfo->mfc_ttls[v->v_id] = mfc->mfc_ttl;
		break;
	}

	if (new != 0)
		msa->msa_needed += sizeof(*minfo);

	if_put(ifp);

	return (0);
}

int
mrt_sysctl_mrtstat(void *oldp, size_t *oldlenp, void *newp)
{
	uint64_t counters[mrts_ncounters];
	struct mrtstat mrtstat;
	int i = 0;

#define ASSIGN(field)	do { mrtstat.field = counters[i++]; } while (0)

	memset(&mrtstat, 0, sizeof mrtstat);
	counters_read(mrtcounters, counters, nitems(counters), NULL);

	ASSIGN(mrts_mfc_lookups);
	ASSIGN(mrts_mfc_misses);
	ASSIGN(mrts_upcalls);
	ASSIGN(mrts_no_route);
	ASSIGN(mrts_bad_tunnel);
	ASSIGN(mrts_cant_tunnel);
	ASSIGN(mrts_wrong_if);
	ASSIGN(mrts_upq_ovflw);
	ASSIGN(mrts_cache_cleanups);
	ASSIGN(mrts_drop_sel);
	ASSIGN(mrts_q_overflow);
	ASSIGN(mrts_pkt2large);
	ASSIGN(mrts_upq_sockfull);

#undef ASSIGN

	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &mrtstat, sizeof(mrtstat)));
}

int
mrt_sysctl_mfc(void *oldp, size_t *oldlenp)
{
	unsigned int		 rtableid;
	int			 error;
	struct mfcsysctlarg	 msa;

	if (oldp != NULL && *oldlenp > MAXPHYS)
		return (EINVAL);

	memset(&msa, 0, sizeof(msa));
	if (oldp != NULL && *oldlenp > 0) {
		msa.msa_minfos = malloc(*oldlenp, M_TEMP, M_WAITOK | M_ZERO);
		msa.msa_len = *oldlenp;
	}

	NET_LOCK();
	for (rtableid = 0; rtableid <= RT_TABLEID_MAX; rtableid++) {
		rtable_walk(rtableid, AF_INET, NULL, mrt_rtwalk_mfcsysctl,
		    &msa);
	}
	NET_UNLOCK();

	if (msa.msa_minfos != NULL && msa.msa_needed > 0 &&
	    (error = copyout(msa.msa_minfos, oldp, msa.msa_needed)) != 0) {
		free(msa.msa_minfos, M_TEMP, msa.msa_len);
		return (error);
	}

	free(msa.msa_minfos, M_TEMP, msa.msa_len);
	*oldlenp = msa.msa_needed;

	return (0);
}

/*
 * Enable multicast routing
 */
int
ip_mrouter_init(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	unsigned int rtableid = inp->inp_rtableid;
	int *v;

	if (so->so_type != SOCK_RAW ||
	    so->so_proto->pr_protocol != IPPROTO_IGMP)
		return (EOPNOTSUPP);

	if (m == NULL || m->m_len < sizeof(int))
		return (EINVAL);

	v = mtod(m, int *);
	if (*v != 1)
		return (EINVAL);

	if (ip_mrouter[rtableid] != NULL)
		return (EADDRINUSE);

	ip_mrouter[rtableid] = so;

	return (0);
}

int
mrouter_rtwalk_delete(struct rtentry *rt, void *arg, unsigned int rtableid)
{
	/* Skip non-multicast routes. */
	if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
	    (RTF_HOST | RTF_MULTICAST))
		return (0);

	return EEXIST;
}

/*
 * Disable multicast routing
 */
int
ip_mrouter_done(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);
	struct ifnet *ifp;
	unsigned int rtableid = inp->inp_rtableid;
	int error;

	NET_ASSERT_LOCKED();

	/* Delete all remaining installed multicast routes. */
	do {
		struct rtentry *rt = NULL;

		error = rtable_walk(rtableid, AF_INET, &rt,
		    mrouter_rtwalk_delete, NULL);
		if (rt != NULL && error == EEXIST) {
			mrt_mcast_del(rt, rtableid);
			error = EAGAIN;
		}
		rtfree(rt);
	} while (error == EAGAIN);

	/* Unregister all interfaces in the domain. */
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rtableid)
			continue;

		vif_delete(ifp);
	}

	mrt_api_config = 0;

	ip_mrouter[rtableid] = NULL;
	mrt_count[rtableid] = 0;

	return (0);
}

int
get_version(struct mbuf *m)
{
	int *v = mtod(m, int *);

	*v = 0x0305;	/* XXX !!!! */
	m->m_len = sizeof(int);
	return (0);
}

/*
 * Configure API capabilities
 */
int
set_api_config(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	struct ifnet *ifp;
	u_int32_t *apival;
	unsigned int rtableid = inp->inp_rtableid;

	if (m == NULL || m->m_len < sizeof(u_int32_t))
		return (EINVAL);

	apival = mtod(m, u_int32_t *);

	/*
	 * We can set the API capabilities only if it is the first operation
	 * after MRT_INIT. I.e.:
	 *  - there are no vifs installed
	 *  - the MFC table is empty
	 */
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rtableid)
			continue;
		if (ifp->if_mcast == NULL)
			continue;

		*apival = 0;
		return (EPERM);
	}
	if (mrt_count[rtableid] > 0) {
		*apival = 0;
		return (EPERM);
	}

	mrt_api_config = *apival & mrt_api_support;
	*apival = mrt_api_config;

	return (0);
}

/*
 * Get API capabilities
 */
int
get_api_support(struct mbuf *m)
{
	u_int32_t *apival;

	if (m == NULL || m->m_len < sizeof(u_int32_t))
		return (EINVAL);

	apival = mtod(m, u_int32_t *);

	*apival = mrt_api_support;

	return (0);
}

/*
 * Get API configured capabilities
 */
int
get_api_config(struct mbuf *m)
{
	u_int32_t *apival;

	if (m == NULL || m->m_len < sizeof(u_int32_t))
		return (EINVAL);

	apival = mtod(m, u_int32_t *);

	*apival = mrt_api_config;

	return (0);
}

static struct sockaddr_in sin = { sizeof(sin), AF_INET };

int
add_vif(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	struct vifctl *vifcp;
	struct vif *vifp;
	struct ifaddr *ifa;
	struct ifnet *ifp;
	struct ifreq ifr;
	int error;
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	if (m == NULL || m->m_len < sizeof(struct vifctl))
		return (EINVAL);

	vifcp = mtod(m, struct vifctl *);
	if (vifcp->vifc_vifi >= MAXVIFS)
		return (EINVAL);
	if (in_nullhost(vifcp->vifc_lcl_addr))
		return (EADDRNOTAVAIL);
	if (if_lookupbyvif(vifcp->vifc_vifi, rtableid) != NULL)
		return (EADDRINUSE);

	/* Tunnels are no longer supported use gif(4) instead. */
	if (vifcp->vifc_flags & VIFF_TUNNEL)
		return (EOPNOTSUPP);
	{
		sin.sin_addr = vifcp->vifc_lcl_addr;
		ifa = ifa_ifwithaddr(sintosa(&sin), rtableid);
		if (ifa == NULL)
			return (EADDRNOTAVAIL);
	}

	/* Use the physical interface associated with the address. */
	ifp = ifa->ifa_ifp;
	if (ifp->if_mcast != NULL)
		return (EADDRINUSE);

	{
		/* Make sure the interface supports multicast. */
		if ((ifp->if_flags & IFF_MULTICAST) == 0)
			return (EOPNOTSUPP);

		/* Enable promiscuous reception of all IP multicasts. */
		memset(&ifr, 0, sizeof(ifr));
		satosin(&ifr.ifr_addr)->sin_len = sizeof(struct sockaddr_in);
		satosin(&ifr.ifr_addr)->sin_family = AF_INET;
		satosin(&ifr.ifr_addr)->sin_addr = zeroin_addr;
		KERNEL_LOCK();
		error = (*ifp->if_ioctl)(ifp, SIOCADDMULTI, (caddr_t)&ifr);
		KERNEL_UNLOCK();
		if (error)
			return (error);
	}

	vifp = malloc(sizeof(*vifp), M_MRTABLE, M_WAITOK | M_ZERO);
	ifp->if_mcast = (caddr_t)vifp;

	vifp->v_id = vifcp->vifc_vifi;
	vifp->v_flags = vifcp->vifc_flags;
	vifp->v_threshold = vifcp->vifc_threshold;
	vifp->v_lcl_addr = vifcp->vifc_lcl_addr;
	vifp->v_rmt_addr = vifcp->vifc_rmt_addr;

	return (0);
}

int
del_vif(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	struct ifnet *ifp;
	vifi_t *vifip;
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	if (m == NULL || m->m_len < sizeof(vifi_t))
		return (EINVAL);

	vifip = mtod(m, vifi_t *);
	if ((ifp = if_lookupbyvif(*vifip, rtableid)) == NULL)
		return (EADDRNOTAVAIL);

	vif_delete(ifp);
	return (0);
}

void
vif_delete(struct ifnet *ifp)
{
	struct vif	*v;
	struct ifreq	 ifr;

	if ((v = (struct vif *)ifp->if_mcast) == NULL)
		return;

	ifp->if_mcast = NULL;

	memset(&ifr, 0, sizeof(ifr));
	satosin(&ifr.ifr_addr)->sin_len = sizeof(struct sockaddr_in);
	satosin(&ifr.ifr_addr)->sin_family = AF_INET;
	satosin(&ifr.ifr_addr)->sin_addr = zeroin_addr;
	KERNEL_LOCK();
	(*ifp->if_ioctl)(ifp, SIOCDELMULTI, (caddr_t)&ifr);
	KERNEL_UNLOCK();

	free(v, M_MRTABLE, sizeof(*v));
}

void
mfc_expire_route(struct rtentry *rt, u_int rtableid)
{
	struct mfc	*mfc = (struct mfc *)rt->rt_llinfo;

	/* Skip entry being deleted. */
	if (mfc == NULL)
		return;

	DPRINTF("Route domain %d origin %#08X group %#08x interface %d "
	    "expire %s", rtableid, satosin(rt->rt_gateway)->sin_addr.s_addr,
	    satosin(rt_key(rt))->sin_addr.s_addr,
	    rt->rt_ifidx, mfc->mfc_expire ? "yes" : "no");

	/* Not expired, add it back to the queue. */
	if (mfc->mfc_expire == 0) {
		mfc->mfc_expire = 1;
		rt_timer_add(rt, &ip_mrouterq, rtableid);
		return;
	}

	mrt_mcast_del(rt, rtableid);
}

int
mfc_add_route(struct ifnet *ifp, struct sockaddr *origin,
    struct sockaddr *group, struct mfcctl2 *mfccp, int wait)
{
	struct vif		*v = (struct vif *)ifp->if_mcast;
	struct rtentry		*rt;
	struct mfc		*mfc;
	unsigned int		 rtableid = ifp->if_rdomain;

	rt = rt_mcast_add(ifp, origin, group);
	if (rt == NULL)
		return (EHOSTUNREACH);

	mfc = malloc(sizeof(*mfc), M_MRTABLE, wait | M_ZERO);
	if (mfc == NULL) {
		DPRINTF("origin %#08X group %#08X parent %d (%s) "
		    "malloc failed",
		    satosin(origin)->sin_addr.s_addr,
		    satosin(group)->sin_addr.s_addr,
		    mfccp->mfcc_parent, ifp->if_xname);
		mrt_mcast_del(rt, rtableid);
		rtfree(rt);
		return (ENOMEM);
	}

	rt->rt_llinfo = (caddr_t)mfc;

	rt_timer_add(rt, &ip_mrouterq, rtableid);

	mfc->mfc_parent = mfccp->mfcc_parent;
	mfc->mfc_pkt_cnt = 0;
	mfc->mfc_byte_cnt = 0;
	mfc->mfc_wrong_if = 0;
	mfc->mfc_ttl = mfccp->mfcc_ttls[v->v_id];
	mfc->mfc_flags = mfccp->mfcc_flags[v->v_id] & mrt_api_config &
	    MRT_MFC_FLAGS_ALL;
	mfc->mfc_expire = 0;

	/* set the RP address */
	if (mrt_api_config & MRT_MFC_RP)
		mfc->mfc_rp = mfccp->mfcc_rp;
	else
		mfc->mfc_rp = zeroin_addr;

	rtfree(rt);

	return (0);
}

void
update_mfc_params(struct mfcctl2 *mfccp, int wait, unsigned int rtableid)
{
	struct rtentry		*rt;
	struct mfc		*mfc;
	struct ifnet		*ifp;
	int			 i;
	struct sockaddr_in	 osin, msin;

	memset(&osin, 0, sizeof(osin));
	osin.sin_len = sizeof(osin);
	osin.sin_family = AF_INET;
	osin.sin_addr = mfccp->mfcc_origin;

	memset(&msin, 0, sizeof(msin));
	msin.sin_len = sizeof(msin);
	msin.sin_family = AF_INET;
	msin.sin_addr = mfccp->mfcc_mcastgrp;

	for (i = 0; i < MAXVIFS; i++) {
		/* Don't add/del upstream routes here. */
		if (i == mfccp->mfcc_parent)
			continue;

		/* Test for vif existence and then update the entry. */
		if ((ifp = if_lookupbyvif(i, rtableid)) == NULL)
			continue;

		rt = mfc_find(ifp, &mfccp->mfcc_mcastgrp, rtableid);

		/* vif not configured or removed. */
		if (mfccp->mfcc_ttls[i] == 0) {
			/* Route doesn't exist, nothing to do. */
			if (rt == NULL)
				continue;

			DPRINTF("del route (group %#08X) for vif %d (%s)",
			    mfccp->mfcc_mcastgrp.s_addr, i, ifp->if_xname);
			mrt_mcast_del(rt, rtableid);
			rtfree(rt);
			continue;
		}

		/* Route exists, look for changes. */
		if (rt != NULL) {
			mfc = (struct mfc *)rt->rt_llinfo;
			/* Skip route being deleted. */
			if (mfc == NULL) {
				rtfree(rt);
				continue;
			}

			/* No new changes to apply. */
			if (mfccp->mfcc_ttls[i] == mfc->mfc_ttl &&
			    mfccp->mfcc_parent == mfc->mfc_parent) {
				rtfree(rt);
				continue;
			}

			DPRINTF("update route (group %#08X) for vif %d (%s)",
			    mfccp->mfcc_mcastgrp.s_addr, i, ifp->if_xname);
			mfc->mfc_ttl = mfccp->mfcc_ttls[i];
			mfc->mfc_parent = mfccp->mfcc_parent;
			rtfree(rt);
			continue;
		}

		DPRINTF("add route (group %#08X) for vif %d (%s)",
		    mfccp->mfcc_mcastgrp.s_addr, i, ifp->if_xname);

		mfc_add_route(ifp, sintosa(&osin), sintosa(&msin),
		    mfccp, wait);
	}

	/* Create route for the parent interface. */
	if ((ifp = if_lookupbyvif(mfccp->mfcc_parent, rtableid)) == NULL) {
		DPRINTF("failed to find upstream interface %d",
		    mfccp->mfcc_parent);
		return;
	}

	/* We already have a route, nothing to do here. */
	if ((rt = mfc_find(ifp, &mfccp->mfcc_mcastgrp, rtableid)) != NULL) {
		rtfree(rt);
		return;
	}

	DPRINTF("add upstream route (group %#08X) for if %s",
	    mfccp->mfcc_mcastgrp.s_addr, ifp->if_xname);
	mfc_add_route(ifp, sintosa(&osin), sintosa(&msin), mfccp, wait);
}

int
mfc_add(struct mfcctl2 *mfcctl2, struct in_addr *origin,
    struct in_addr *group, int vidx, unsigned int rtableid, int wait)
{
	struct ifnet		*ifp;
	struct vif		*v;
	struct mfcctl2		 mfcctl;

	ifp = if_lookupbyvif(vidx, rtableid);
	if (ifp == NULL ||
	    (v = (struct vif *)ifp->if_mcast) == NULL)
		return (EHOSTUNREACH);

	memset(&mfcctl, 0, sizeof(mfcctl));
	if (mfcctl2 == NULL) {
		mfcctl.mfcc_origin = *origin;
		mfcctl.mfcc_mcastgrp = *group;
		mfcctl.mfcc_parent = vidx;
	} else
		memcpy(&mfcctl, mfcctl2, sizeof(mfcctl));

	update_mfc_params(&mfcctl, wait, rtableid);

	return (0);
}

int
add_mfc(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	struct mfcctl2 mfcctl2;
	int mfcctl_size = sizeof(struct mfcctl);
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	if (mrt_api_config & MRT_API_FLAGS_ALL)
		mfcctl_size = sizeof(struct mfcctl2);

	if (m == NULL || m->m_len < mfcctl_size)
		return (EINVAL);

	/*
	 * select data size depending on API version.
	 */
	if (mrt_api_config & MRT_API_FLAGS_ALL) {
		struct mfcctl2 *mp2 = mtod(m, struct mfcctl2 *);
		memcpy((caddr_t)&mfcctl2, mp2, sizeof(*mp2));
	} else {
		struct mfcctl *mp = mtod(m, struct mfcctl *);
		memcpy((caddr_t)&mfcctl2, mp, sizeof(*mp));
		memset((caddr_t)&mfcctl2 + sizeof(struct mfcctl), 0,
		    sizeof(mfcctl2) - sizeof(struct mfcctl));
	}

	if (mfc_add(&mfcctl2, &mfcctl2.mfcc_origin, &mfcctl2.mfcc_mcastgrp,
	    mfcctl2.mfcc_parent, rtableid, M_WAITOK) == -1)
		return (EINVAL);

	return (0);
}

int
del_mfc(struct socket *so, struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	struct rtentry *rt;
	struct mfcctl2 mfcctl2;
	int mfcctl_size = sizeof(struct mfcctl);
	struct mfcctl *mp;
	unsigned int rtableid = inp->inp_rtableid;

	NET_ASSERT_LOCKED();

	/*
	 * XXX: for deleting MFC entries the information in entries
	 * of size "struct mfcctl" is sufficient.
	 */

	if (m == NULL || m->m_len < mfcctl_size)
		return (EINVAL);

	mp = mtod(m, struct mfcctl *);

	memcpy((caddr_t)&mfcctl2, mp, sizeof(*mp));
	memset((caddr_t)&mfcctl2 + sizeof(struct mfcctl), 0,
	    sizeof(mfcctl2) - sizeof(struct mfcctl));

	DPRINTF("origin %#08X group %#08X rtableid %d",
	    mfcctl2.mfcc_origin.s_addr, mfcctl2.mfcc_mcastgrp.s_addr, rtableid);

	while ((rt = mfc_find(NULL, &mfcctl2.mfcc_mcastgrp, rtableid)) != NULL) {
		mrt_mcast_del(rt, rtableid);
		rtfree(rt);
	}

	return (0);
}

int
socket_send(struct socket *so, struct mbuf *mm, struct sockaddr_in *src)
{
	if (so != NULL) {
		int ret;

		mtx_enter(&so->so_rcv.sb_mtx);
		ret = sbappendaddr(&so->so_rcv, sintosa(src), mm, NULL);
		mtx_leave(&so->so_rcv.sb_mtx);

		if (ret != 0) {
			sorwakeup(so);
			return (0);
		}
	}
	m_freem(mm);
	return (-1);
}

/*
 * IP multicast forwarding function. This function assumes that the packet
 * pointed to by "ip" has arrived on (or is about to be sent to) the interface
 * pointed to by "ifp", and the packet is to be relayed to other networks
 * that have members of the packet's destination IP multicast group.
 *
 * The packet is returned unscathed to the caller, unless it is
 * erroneous, in which case a non-zero return value tells the caller to
 * discard it.
 */

#define IP_HDR_LEN  20	/* # bytes of fixed IP header (excluding options) */
#define TUNNEL_LEN  12  /* # bytes of IP option for tunnel encapsulation  */

int
ip_mforward(struct mbuf *m, struct ifnet *ifp, int flags)
{
	struct ip *ip = mtod(m, struct ip *);
	struct vif *v;
	struct rtentry *rt;
	static int srctun = 0;
	struct mbuf *mm;
	unsigned int rtableid = ifp->if_rdomain;

	if (ip->ip_hl < (IP_HDR_LEN + TUNNEL_LEN) >> 2 ||
	    ((u_char *)(ip + 1))[1] != IPOPT_LSRR) {
		/*
		 * Packet arrived via a physical interface or
		 * an encapsulated tunnel or a register_vif.
		 */
	} else {
		/*
		 * Packet arrived through a source-route tunnel.
		 * Source-route tunnels are no longer supported.
		 */
		if ((srctun++ % 1000) == 0)
			log(LOG_ERR, "ip_mforward: received source-routed "
			    "packet from %x\n", ntohl(ip->ip_src.s_addr));
		return (EOPNOTSUPP);
	}

	/*
	 * Don't forward a packet with time-to-live of zero or one,
	 * or a packet destined to a local-only group.
	 */
	if (ip->ip_ttl <= 1 || IN_LOCAL_GROUP(ip->ip_dst.s_addr))
		return (0);

	/*
	 * Determine forwarding vifs from the forwarding cache table
	 */
	mrtstat_inc(mrts_mfc_lookups);
	rt = mfc_find(NULL, &ip->ip_dst, rtableid);

	/* Entry exists, so forward if necessary */
	if (rt != NULL) {
		return (ip_mdq(m, ifp, rt, flags));
	} else {
		/*
		 * If we don't have a route for packet's origin,
		 * Make a copy of the packet & send message to routing daemon
		 */
		int hlen = ip->ip_hl << 2;

		mrtstat_inc(mrts_mfc_misses);
		mrtstat_inc(mrts_no_route);

		{
			struct igmpmsg *im;

			/*
			 * Locate the vifi for the incoming interface for
			 * this packet.
			 * If none found, drop packet.
			 */
			if ((v = (struct vif *)ifp->if_mcast) == NULL)
				return (EHOSTUNREACH);
			/*
			 * Make a copy of the header to send to the user level
			 * process
			 */
			mm = m_copym(m, 0, hlen, M_NOWAIT);
			if (mm == NULL ||
			    (mm = m_pullup(mm, hlen)) == NULL)
				return (ENOBUFS);

			/*
			 * Send message to routing daemon to install
			 * a route into the kernel table
			 */

			im = mtod(mm, struct igmpmsg *);
			im->im_msgtype = IGMPMSG_NOCACHE;
			im->im_mbz = 0;
			im->im_vif = v->v_id;

			mrtstat_inc(mrts_upcalls);

			sin.sin_addr = ip->ip_src;
			if (socket_send(ip_mrouter[rtableid], mm, &sin) < 0) {
				log(LOG_WARNING, "ip_mforward: ip_mrouter "
				    "socket queue full\n");
				mrtstat_inc(mrts_upq_sockfull);
				return (ENOBUFS);
			}

			mfc_add(NULL, &ip->ip_src, &ip->ip_dst, v->v_id,
			    rtableid, M_NOWAIT);
		}

		return (0);
	}
}

/*
 * Packet forwarding routine once entry in the cache is made
 */
int
ip_mdq(struct mbuf *m, struct ifnet *ifp0, struct rtentry *rt, int flags)
{
	struct ip  *ip = mtod(m, struct ip *);
	struct mfc *mfc = (struct mfc *)rt->rt_llinfo;
	struct vif *v = (struct vif *)ifp0->if_mcast;
	struct ifnet *ifp;
	struct mbuf *mc;
	struct ip_moptions imo;

	/* Sanity check: we have all promised pointers. */
	if (v == NULL || mfc == NULL) {
		rtfree(rt);
		return (EHOSTUNREACH);
	}

	/*
	 * Don't forward if it didn't arrive from the parent vif for its origin.
	 */
	if (mfc->mfc_parent != v->v_id) {
		/* came in the wrong interface */
		mrtstat_inc(mrts_wrong_if);
		mfc->mfc_wrong_if++;
		rtfree(rt);
		return (0);
	}

	/* If I sourced this packet, it counts as output, else it was input. */
	if (in_hosteq(ip->ip_src, v->v_lcl_addr)) {
		v->v_pkt_out++;
		v->v_bytes_out += m->m_pkthdr.len;
	} else {
		v->v_pkt_in++;
		v->v_bytes_in += m->m_pkthdr.len;
	}

	/*
	 * For each vif, decide if a copy of the packet should be forwarded.
	 * Forward if:
	 *		- the ttl exceeds the vif's threshold
	 *		- there are group members downstream on interface
	 */
	do {
		/* Don't consider non multicast routes. */
		if (ISSET(rt->rt_flags, RTF_HOST | RTF_MULTICAST) !=
		    (RTF_HOST | RTF_MULTICAST))
			continue;

		mfc = (struct mfc *)rt->rt_llinfo;
		if (mfc == NULL)
			continue;

		mfc->mfc_pkt_cnt++;
		mfc->mfc_byte_cnt += m->m_pkthdr.len;

		/* Don't let this route expire. */
		mfc->mfc_expire = 0;

		if (ip->ip_ttl <= mfc->mfc_ttl)
			continue;
		if ((ifp = if_get(rt->rt_ifidx)) == NULL)
			continue;

		/* Sanity check: did we configure this? */
		if ((v = (struct vif *)ifp->if_mcast) == NULL) {
			if_put(ifp);
			continue;
		}

		/* Don't send in the upstream interface. */
		if (mfc->mfc_parent == v->v_id) {
			if_put(ifp);
			continue;
		}

		v->v_pkt_out++;
		v->v_bytes_out += m->m_pkthdr.len;

		/*
		 * Make a new reference to the packet; make sure
		 * that the IP header is actually copied, not
		 * just referenced, so that ip_output() only
		 * scribbles on the copy.
		 */
		mc = m_dup_pkt(m, max_linkhdr, M_NOWAIT);
		if (mc == NULL) {
			if_put(ifp);
			rtfree(rt);
			return (ENOBUFS);
		}

		/*
		 * if physical interface option, extract the options
		 * and then send
		 */
		imo.imo_ifidx = rt->rt_ifidx;
		imo.imo_ttl = ip->ip_ttl - IPTTLDEC;
		imo.imo_loop = 1;

		ip_output(mc, NULL, NULL, flags | IP_FORWARDING, &imo, NULL, 0);
		if_put(ifp);
	} while ((rt = rtable_iterate(rt)) != NULL);

	return (0);
}

struct ifnet *
if_lookupbyvif(vifi_t vifi, unsigned int rtableid)
{
	struct vif	*v;
	struct ifnet	*ifp;

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rtableid)
			continue;
		if ((v = (struct vif *)ifp->if_mcast) == NULL)
			continue;
		if (v->v_id != vifi)
			continue;

		return (ifp);
	}

	return (NULL);
}

struct rtentry *
rt_mcast_add(struct ifnet *ifp, struct sockaddr *origin, struct sockaddr *group)
{
	struct ifaddr		*ifa;
	int			 rv;
	unsigned int		 rtableid = ifp->if_rdomain;

	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr->sa_family == AF_INET)
			break;
	}
	if (ifa == NULL) {
		DPRINTF("ifa == NULL");
		return (NULL);
	}

	rv = rt_ifa_add(ifa, RTF_HOST | RTF_MULTICAST | RTF_MPATH,
	    group, ifp->if_rdomain);
	if (rv != 0) {
		DPRINTF("rt_ifa_add failed (%d)", rv);
		return (NULL);
	}

	mrt_count[rtableid]++;

	return (mfc_find(ifp, &satosin(group)->sin_addr, rtableid));
}

void
mrt_mcast_del(struct rtentry *rt, unsigned int rtableid)
{
	struct ifnet		*ifp;
	int			 error;

	/* Remove all timers related to this route. */
	rt_timer_remove_all(rt);

	free(rt->rt_llinfo, M_MRTABLE, sizeof(struct mfc));
	rt->rt_llinfo = NULL;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		return;
	error = rtdeletemsg(rt, ifp, rtableid);
	if_put(ifp);

	if (error)
		DPRINTF("delete route error %d\n", error);

	mrt_count[rtableid]--;
}
