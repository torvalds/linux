/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 *	$KAME: ip6_mroute.c,v 1.58 2001/12/18 02:36:31 itojun Exp $
 */

/*-
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
 *	@(#)ip_mroute.c	8.2 (Berkeley) 11/15/93
 *	BSDI ip_mroute.c,v 2.10 1996/11/14 00:29:52 jch Exp
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
 * MROUTING Revision: 3.5.1.2 + PIM-SMv2 (pimd) Support
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/callout.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/sdt.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/icmp6.h>
#include <netinet/ip_encap.h>

#include <netinet/ip6.h>
#include <netinet/in_kdtrace.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_mroute.h>
#include <netinet6/pim6.h>
#include <netinet6/pim6_var.h>

static MALLOC_DEFINE(M_MRTABLE6, "mf6c", "multicast forwarding cache entry");

static int	ip6_mdq(struct mbuf *, struct ifnet *, struct mf6c *);
static void	phyint_send(struct ip6_hdr *, struct mif6 *, struct mbuf *);
static int	register_send(struct ip6_hdr *, struct mif6 *, struct mbuf *);
static int	set_pim6(int *);
static int	socket_send(struct socket *, struct mbuf *,
		    struct sockaddr_in6 *);

extern int in6_mcast_loop;
extern struct domain inet6domain;

static const struct encaptab *pim6_encap_cookie;
static int pim6_encapcheck(const struct mbuf *, int, int, void *);
static int pim6_input(struct mbuf *, int, int, void *);

static const struct encap_config ipv6_encap_cfg = {
	.proto = IPPROTO_PIM,
	.min_length = sizeof(struct ip6_hdr) + PIM_MINLEN,
	.exact_match = 8,
	.check = pim6_encapcheck,
	.input = pim6_input
};


VNET_DEFINE_STATIC(int, ip6_mrouter_ver) = 0;
#define	V_ip6_mrouter_ver	VNET(ip6_mrouter_ver)

SYSCTL_DECL(_net_inet6);
SYSCTL_DECL(_net_inet6_ip6);
static SYSCTL_NODE(_net_inet6, IPPROTO_PIM, pim, CTLFLAG_RW, 0, "PIM");

static struct mrt6stat mrt6stat;
SYSCTL_STRUCT(_net_inet6_ip6, OID_AUTO, mrt6stat, CTLFLAG_RW,
    &mrt6stat, mrt6stat,
    "Multicast Routing Statistics (struct mrt6stat, netinet6/ip6_mroute.h)");

#define	MRT6STAT_INC(name)	mrt6stat.name += 1
#define NO_RTE_FOUND	0x1
#define RTE_FOUND	0x2

static struct mtx mrouter6_mtx;
#define	MROUTER6_LOCK()		mtx_lock(&mrouter6_mtx)
#define	MROUTER6_UNLOCK()	mtx_unlock(&mrouter6_mtx)
#define	MROUTER6_LOCK_ASSERT()	do {					\
	mtx_assert(&mrouter6_mtx, MA_OWNED);				\
	NET_ASSERT_GIANT();						\
} while (0)
#define	MROUTER6_LOCK_INIT()	\
	mtx_init(&mrouter6_mtx, "IPv6 multicast forwarding", NULL, MTX_DEF)
#define	MROUTER6_LOCK_DESTROY()	mtx_destroy(&mrouter6_mtx)

static struct mf6c *mf6ctable[MF6CTBLSIZ];
SYSCTL_OPAQUE(_net_inet6_ip6, OID_AUTO, mf6ctable, CTLFLAG_RD,
    &mf6ctable, sizeof(mf6ctable), "S,*mf6ctable[MF6CTBLSIZ]",
    "IPv6 Multicast Forwarding Table (struct *mf6ctable[MF6CTBLSIZ], "
    "netinet6/ip6_mroute.h)");

static struct mtx mfc6_mtx;
#define	MFC6_LOCK()		mtx_lock(&mfc6_mtx)
#define	MFC6_UNLOCK()		mtx_unlock(&mfc6_mtx)
#define	MFC6_LOCK_ASSERT()	do {					\
	mtx_assert(&mfc6_mtx, MA_OWNED);				\
	NET_ASSERT_GIANT();						\
} while (0)
#define	MFC6_LOCK_INIT()		\
	mtx_init(&mfc6_mtx, "IPv6 multicast forwarding cache", NULL, MTX_DEF)
#define	MFC6_LOCK_DESTROY()	mtx_destroy(&mfc6_mtx)

static u_char n6expire[MF6CTBLSIZ];

static struct mif6 mif6table[MAXMIFS];
static int
sysctl_mif6table(SYSCTL_HANDLER_ARGS)
{
	struct mif6_sctl *out;
	int error;

	out = malloc(sizeof(struct mif6_sctl) * MAXMIFS, M_TEMP,
	    M_WAITOK | M_ZERO);
	for (int i = 0; i < MAXMIFS; i++) {
		out[i].m6_flags		= mif6table[i].m6_flags;
		out[i].m6_rate_limit	= mif6table[i].m6_rate_limit;
		out[i].m6_lcl_addr	= mif6table[i].m6_lcl_addr;
		if (mif6table[i].m6_ifp != NULL)
			out[i].m6_ifp	= mif6table[i].m6_ifp->if_index;
		else
			out[i].m6_ifp	= 0;
		out[i].m6_pkt_in	= mif6table[i].m6_pkt_in;
		out[i].m6_pkt_out	= mif6table[i].m6_pkt_out;
		out[i].m6_bytes_in	= mif6table[i].m6_bytes_in;
		out[i].m6_bytes_out	= mif6table[i].m6_bytes_out;
	}
	error = SYSCTL_OUT(req, out, sizeof(struct mif6_sctl) * MAXMIFS);
	free(out, M_TEMP);
	return (error);
}
SYSCTL_PROC(_net_inet6_ip6, OID_AUTO, mif6table, CTLTYPE_OPAQUE | CTLFLAG_RD,
    NULL, 0, sysctl_mif6table, "S,mif6_sctl[MAXMIFS]",
    "IPv6 Multicast Interfaces (struct mif6_sctl[MAXMIFS], "
    "netinet6/ip6_mroute.h)");

static struct mtx mif6_mtx;
#define	MIF6_LOCK()		mtx_lock(&mif6_mtx)
#define	MIF6_UNLOCK()		mtx_unlock(&mif6_mtx)
#define	MIF6_LOCK_ASSERT()	mtx_assert(&mif6_mtx, MA_OWNED)
#define	MIF6_LOCK_INIT()	\
	mtx_init(&mif6_mtx, "IPv6 multicast interfaces", NULL, MTX_DEF)
#define	MIF6_LOCK_DESTROY()	mtx_destroy(&mif6_mtx)

#ifdef MRT6DEBUG
VNET_DEFINE_STATIC(u_int, mrt6debug) = 0;	/* debug level */
#define	V_mrt6debug		VNET(mrt6debug)
#define DEBUG_MFC	0x02
#define DEBUG_FORWARD	0x04
#define DEBUG_EXPIRE	0x08
#define DEBUG_XMIT	0x10
#define DEBUG_REG	0x20
#define DEBUG_PIM	0x40
#define	DEBUG_ERR	0x80
#define	DEBUG_ANY	0x7f
#define	MRT6_DLOG(m, fmt, ...)	\
	if (V_mrt6debug & (m))	\
		log(((m) & DEBUG_ERR) ? LOG_ERR: LOG_DEBUG, \
		    "%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define	MRT6_DLOG(m, fmt, ...)
#endif

static void	expire_upcalls(void *);
#define	EXPIRE_TIMEOUT	(hz / 4)	/* 4x / second */
#define	UPCALL_EXPIRE	6		/* number of timeouts */

/*
 * XXX TODO: maintain a count to if_allmulti() calls in struct ifnet.
 */

/*
 * 'Interfaces' associated with decapsulator (so we can tell
 * packets that went through it from ones that get reflected
 * by a broken gateway).  Different from IPv4 register_if,
 * these interfaces are linked into the system ifnet list,
 * because per-interface IPv6 statistics are maintained in
 * ifp->if_afdata.  But it does not have any routes point
 * to them.  I.e., packets can't be sent this way.  They
 * only exist as a placeholder for multicast source
 * verification.
 */
static struct ifnet *multicast_register_if6;

#define ENCAP_HOPS 64

/*
 * Private variables.
 */
static mifi_t nummifs = 0;
static mifi_t reg_mif_num = (mifi_t)-1;

static struct pim6stat pim6stat;
SYSCTL_STRUCT(_net_inet6_pim, PIM6CTL_STATS, stats, CTLFLAG_RW,
    &pim6stat, pim6stat,
    "PIM Statistics (struct pim6stat, netinet6/pim6_var.h)");

#define	PIM6STAT_INC(name)	pim6stat.name += 1
VNET_DEFINE_STATIC(int, pim6);
#define	V_pim6		VNET(pim6)

/*
 * Hash function for a source, group entry
 */
#define MF6CHASH(a, g) MF6CHASHMOD((a).s6_addr32[0] ^ (a).s6_addr32[1] ^ \
				   (a).s6_addr32[2] ^ (a).s6_addr32[3] ^ \
				   (g).s6_addr32[0] ^ (g).s6_addr32[1] ^ \
				   (g).s6_addr32[2] ^ (g).s6_addr32[3])

/*
 * Find a route for a given origin IPv6 address and Multicast group address.
 */
#define MF6CFIND(o, g, rt) do { \
	struct mf6c *_rt = mf6ctable[MF6CHASH(o,g)]; \
	rt = NULL; \
	while (_rt) { \
		if (IN6_ARE_ADDR_EQUAL(&_rt->mf6c_origin.sin6_addr, &(o)) && \
		    IN6_ARE_ADDR_EQUAL(&_rt->mf6c_mcastgrp.sin6_addr, &(g)) && \
		    (_rt->mf6c_stall == NULL)) { \
			rt = _rt; \
			break; \
		} \
		_rt = _rt->mf6c_next; \
	} \
	if (rt == NULL) { \
		MRT6STAT_INC(mrt6s_mfc_misses); \
	} \
} while (/*CONSTCOND*/ 0)

/*
 * Macros to compute elapsed time efficiently
 * Borrowed from Van Jacobson's scheduling code
 * XXX: replace with timersub() ?
 */
#define TV_DELTA(a, b, delta) do { \
	    int xxs; \
		\
	    delta = (a).tv_usec - (b).tv_usec; \
	    if ((xxs = (a).tv_sec - (b).tv_sec)) { \
	       switch (xxs) { \
		      case 2: \
			  delta += 1000000; \
			      /* FALLTHROUGH */ \
		      case 1: \
			  delta += 1000000; \
			  break; \
		      default: \
			  delta += (1000000 * xxs); \
	       } \
	    } \
} while (/*CONSTCOND*/ 0)

/* XXX: replace with timercmp(a, b, <) ? */
#define TV_LT(a, b) (((a).tv_usec < (b).tv_usec && \
	      (a).tv_sec <= (b).tv_sec) || (a).tv_sec < (b).tv_sec)

#ifdef UPCALL_TIMING
#define UPCALL_MAX	50
static u_long upcall_data[UPCALL_MAX + 1];
static void collate();
#endif /* UPCALL_TIMING */

static int ip6_mrouter_init(struct socket *, int, int);
static int add_m6fc(struct mf6cctl *);
static int add_m6if(struct mif6ctl *);
static int del_m6fc(struct mf6cctl *);
static int del_m6if(mifi_t *);
static int del_m6if_locked(mifi_t *);
static int get_mif6_cnt(struct sioc_mif_req6 *);
static int get_sg_cnt(struct sioc_sg_req6 *);

static struct callout expire_upcalls_ch;

int X_ip6_mforward(struct ip6_hdr *, struct ifnet *, struct mbuf *);
int X_ip6_mrouter_done(void);
int X_ip6_mrouter_set(struct socket *, struct sockopt *);
int X_ip6_mrouter_get(struct socket *, struct sockopt *);
int X_mrt6_ioctl(u_long, caddr_t);

/*
 * Handle MRT setsockopt commands to modify the multicast routing tables.
 */
int
X_ip6_mrouter_set(struct socket *so, struct sockopt *sopt)
{
	int error = 0;
	int optval;
	struct mif6ctl mifc;
	struct mf6cctl mfcc;
	mifi_t mifi;

	if (so != V_ip6_mrouter && sopt->sopt_name != MRT6_INIT)
		return (EPERM);

	switch (sopt->sopt_name) {
	case MRT6_INIT:
#ifdef MRT6_OINIT
	case MRT6_OINIT:
#endif
		error = sooptcopyin(sopt, &optval, sizeof(optval),
		    sizeof(optval));
		if (error)
			break;
		error = ip6_mrouter_init(so, optval, sopt->sopt_name);
		break;
	case MRT6_DONE:
		error = X_ip6_mrouter_done();
		break;
	case MRT6_ADD_MIF:
		error = sooptcopyin(sopt, &mifc, sizeof(mifc), sizeof(mifc));
		if (error)
			break;
		error = add_m6if(&mifc);
		break;
	case MRT6_ADD_MFC:
		error = sooptcopyin(sopt, &mfcc, sizeof(mfcc), sizeof(mfcc));
		if (error)
			break;
		error = add_m6fc(&mfcc);
		break;
	case MRT6_DEL_MFC:
		error = sooptcopyin(sopt, &mfcc, sizeof(mfcc), sizeof(mfcc));
		if (error)
			break;
		error = del_m6fc(&mfcc);
		break;
	case MRT6_DEL_MIF:
		error = sooptcopyin(sopt, &mifi, sizeof(mifi), sizeof(mifi));
		if (error)
			break;
		error = del_m6if(&mifi);
		break;
	case MRT6_PIM:
		error = sooptcopyin(sopt, &optval, sizeof(optval),
		    sizeof(optval));
		if (error)
			break;
		error = set_pim6(&optval);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/*
 * Handle MRT getsockopt commands
 */
int
X_ip6_mrouter_get(struct socket *so, struct sockopt *sopt)
{
	int error = 0;

	if (so != V_ip6_mrouter)
		return (EACCES);

	switch (sopt->sopt_name) {
		case MRT6_PIM:
			error = sooptcopyout(sopt, &V_pim6, sizeof(V_pim6));
			break;
	}
	return (error);
}

/*
 * Handle ioctl commands to obtain information from the cache
 */
int
X_mrt6_ioctl(u_long cmd, caddr_t data)
{
	int ret;

	ret = EINVAL;

	switch (cmd) {
	case SIOCGETSGCNT_IN6:
		ret = get_sg_cnt((struct sioc_sg_req6 *)data);
		break;

	case SIOCGETMIFCNT_IN6:
		ret = get_mif6_cnt((struct sioc_mif_req6 *)data);
		break;

	default:
		break;
	}

	return (ret);
}

/*
 * returns the packet, byte, rpf-failure count for the source group provided
 */
static int
get_sg_cnt(struct sioc_sg_req6 *req)
{
	struct mf6c *rt;
	int ret;

	ret = 0;

	MFC6_LOCK();

	MF6CFIND(req->src.sin6_addr, req->grp.sin6_addr, rt);
	if (rt == NULL) {
		ret = ESRCH;
	} else {
		req->pktcnt = rt->mf6c_pkt_cnt;
		req->bytecnt = rt->mf6c_byte_cnt;
		req->wrong_if = rt->mf6c_wrong_if;
	}

	MFC6_UNLOCK();

	return (ret);
}

/*
 * returns the input and output packet and byte counts on the mif provided
 */
static int
get_mif6_cnt(struct sioc_mif_req6 *req)
{
	mifi_t mifi;
	int ret;

	ret = 0;
	mifi = req->mifi;

	MIF6_LOCK();

	if (mifi >= nummifs) {
		ret = EINVAL;
	} else {
		req->icount = mif6table[mifi].m6_pkt_in;
		req->ocount = mif6table[mifi].m6_pkt_out;
		req->ibytes = mif6table[mifi].m6_bytes_in;
		req->obytes = mif6table[mifi].m6_bytes_out;
	}

	MIF6_UNLOCK();

	return (ret);
}

static int
set_pim6(int *i)
{
	if ((*i != 1) && (*i != 0))
		return (EINVAL);

	V_pim6 = *i;

	return (0);
}

/*
 * Enable multicast routing
 */
static int
ip6_mrouter_init(struct socket *so, int v, int cmd)
{

	MRT6_DLOG(DEBUG_ANY, "so_type = %d, pr_protocol = %d",
	    so->so_type, so->so_proto->pr_protocol);

	if (so->so_type != SOCK_RAW ||
	    so->so_proto->pr_protocol != IPPROTO_ICMPV6)
		return (EOPNOTSUPP);

	if (v != 1)
		return (ENOPROTOOPT);

	MROUTER6_LOCK();

	if (V_ip6_mrouter != NULL) {
		MROUTER6_UNLOCK();
		return (EADDRINUSE);
	}

	V_ip6_mrouter = so;
	V_ip6_mrouter_ver = cmd;

	bzero((caddr_t)mf6ctable, sizeof(mf6ctable));
	bzero((caddr_t)n6expire, sizeof(n6expire));

	V_pim6 = 0;/* used for stubbing out/in pim stuff */

	callout_init(&expire_upcalls_ch, 0);
	callout_reset(&expire_upcalls_ch, EXPIRE_TIMEOUT,
	    expire_upcalls, NULL);

	MROUTER6_UNLOCK();
	MRT6_DLOG(DEBUG_ANY, "finished");

	return (0);
}

/*
 * Disable IPv6 multicast forwarding.
 */
int
X_ip6_mrouter_done(void)
{
	mifi_t mifi;
	u_long i;
	struct mf6c *rt;
	struct rtdetq *rte;

	MROUTER6_LOCK();

	if (V_ip6_mrouter == NULL) {
		MROUTER6_UNLOCK();
		return (EINVAL);
	}

	/*
	 * For each phyint in use, disable promiscuous reception of all IPv6
	 * multicasts.
	 */
	for (mifi = 0; mifi < nummifs; mifi++) {
		if (mif6table[mifi].m6_ifp &&
		    !(mif6table[mifi].m6_flags & MIFF_REGISTER)) {
			if_allmulti(mif6table[mifi].m6_ifp, 0);
		}
	}
	bzero((caddr_t)mif6table, sizeof(mif6table));
	nummifs = 0;

	V_pim6 = 0; /* used to stub out/in pim specific code */

	callout_stop(&expire_upcalls_ch);

	/*
	 * Free all multicast forwarding cache entries.
	 */
	MFC6_LOCK();
	for (i = 0; i < MF6CTBLSIZ; i++) {
		rt = mf6ctable[i];
		while (rt) {
			struct mf6c *frt;

			for (rte = rt->mf6c_stall; rte != NULL; ) {
				struct rtdetq *n = rte->next;

				m_freem(rte->m);
				free(rte, M_MRTABLE6);
				rte = n;
			}
			frt = rt;
			rt = rt->mf6c_next;
			free(frt, M_MRTABLE6);
		}
	}
	bzero((caddr_t)mf6ctable, sizeof(mf6ctable));
	MFC6_UNLOCK();

	/*
	 * Reset register interface
	 */
	if (reg_mif_num != (mifi_t)-1 && multicast_register_if6 != NULL) {
		if_detach(multicast_register_if6);
		if_free(multicast_register_if6);
		reg_mif_num = (mifi_t)-1;
		multicast_register_if6 = NULL;
	}

	V_ip6_mrouter = NULL;
	V_ip6_mrouter_ver = 0;

	MROUTER6_UNLOCK();
	MRT6_DLOG(DEBUG_ANY, "finished");

	return (0);
}

static struct sockaddr_in6 sin6 = { sizeof(sin6), AF_INET6 };

/*
 * Add a mif to the mif table
 */
static int
add_m6if(struct mif6ctl *mifcp)
{
	struct mif6 *mifp;
	struct ifnet *ifp;
	int error;

	MIF6_LOCK();

	if (mifcp->mif6c_mifi >= MAXMIFS) {
		MIF6_UNLOCK();
		return (EINVAL);
	}
	mifp = mif6table + mifcp->mif6c_mifi;
	if (mifp->m6_ifp != NULL) {
		MIF6_UNLOCK();
		return (EADDRINUSE); /* XXX: is it appropriate? */
	}
	if (mifcp->mif6c_pifi == 0 || mifcp->mif6c_pifi > V_if_index) {
		MIF6_UNLOCK();
		return (ENXIO);
	}

	ifp = ifnet_byindex(mifcp->mif6c_pifi);

	if (mifcp->mif6c_flags & MIFF_REGISTER) {
		if (reg_mif_num == (mifi_t)-1) {
			ifp = if_alloc(IFT_OTHER);

			if_initname(ifp, "register_mif", 0);
			ifp->if_flags |= IFF_LOOPBACK;
			if_attach(ifp);
			multicast_register_if6 = ifp;
			reg_mif_num = mifcp->mif6c_mifi;
			/*
			 * it is impossible to guess the ifindex of the
			 * register interface.  So mif6c_pifi is automatically
			 * calculated.
			 */
			mifcp->mif6c_pifi = ifp->if_index;
		} else {
			ifp = multicast_register_if6;
		}
	} else {
		/* Make sure the interface supports multicast */
		if ((ifp->if_flags & IFF_MULTICAST) == 0) {
			MIF6_UNLOCK();
			return (EOPNOTSUPP);
		}

		error = if_allmulti(ifp, 1);
		if (error) {
			MIF6_UNLOCK();
			return (error);
		}
	}

	mifp->m6_flags     = mifcp->mif6c_flags;
	mifp->m6_ifp       = ifp;

	/* initialize per mif pkt counters */
	mifp->m6_pkt_in    = 0;
	mifp->m6_pkt_out   = 0;
	mifp->m6_bytes_in  = 0;
	mifp->m6_bytes_out = 0;

	/* Adjust nummifs up if the mifi is higher than nummifs */
	if (nummifs <= mifcp->mif6c_mifi)
		nummifs = mifcp->mif6c_mifi + 1;

	MIF6_UNLOCK();
	MRT6_DLOG(DEBUG_ANY, "mif #%d, phyint %s", mifcp->mif6c_mifi,
	    if_name(ifp));

	return (0);
}

/*
 * Delete a mif from the mif table
 */
static int
del_m6if_locked(mifi_t *mifip)
{
	struct mif6 *mifp = mif6table + *mifip;
	mifi_t mifi;
	struct ifnet *ifp;

	MIF6_LOCK_ASSERT();

	if (*mifip >= nummifs)
		return (EINVAL);
	if (mifp->m6_ifp == NULL)
		return (EINVAL);

	if (!(mifp->m6_flags & MIFF_REGISTER)) {
		/* XXX: TODO: Maintain an ALLMULTI refcount in struct ifnet. */
		ifp = mifp->m6_ifp;
		if_allmulti(ifp, 0);
	} else {
		if (reg_mif_num != (mifi_t)-1 &&
		    multicast_register_if6 != NULL) {
			if_detach(multicast_register_if6);
			if_free(multicast_register_if6);
			reg_mif_num = (mifi_t)-1;
			multicast_register_if6 = NULL;
		}
	}

	bzero((caddr_t)mifp, sizeof(*mifp));

	/* Adjust nummifs down */
	for (mifi = nummifs; mifi > 0; mifi--)
		if (mif6table[mifi - 1].m6_ifp)
			break;
	nummifs = mifi;
	MRT6_DLOG(DEBUG_ANY, "mif %d, nummifs %d", *mifip, nummifs);

	return (0);
}

static int
del_m6if(mifi_t *mifip)
{
	int cc;

	MIF6_LOCK();
	cc = del_m6if_locked(mifip);
	MIF6_UNLOCK();

	return (cc);
}

/*
 * Add an mfc entry
 */
static int
add_m6fc(struct mf6cctl *mfccp)
{
	struct mf6c *rt;
	u_long hash;
	struct rtdetq *rte;
	u_short nstl;
	char ip6bufo[INET6_ADDRSTRLEN], ip6bufg[INET6_ADDRSTRLEN];

	MFC6_LOCK();

	MF6CFIND(mfccp->mf6cc_origin.sin6_addr,
		 mfccp->mf6cc_mcastgrp.sin6_addr, rt);

	/* If an entry already exists, just update the fields */
	if (rt) {
		MRT6_DLOG(DEBUG_MFC, "no upcall o %s g %s p %x",
		    ip6_sprintf(ip6bufo, &mfccp->mf6cc_origin.sin6_addr),
		    ip6_sprintf(ip6bufg, &mfccp->mf6cc_mcastgrp.sin6_addr),
		    mfccp->mf6cc_parent);

		rt->mf6c_parent = mfccp->mf6cc_parent;
		rt->mf6c_ifset = mfccp->mf6cc_ifset;

		MFC6_UNLOCK();
		return (0);
	}

	/*
	 * Find the entry for which the upcall was made and update
	 */
	hash = MF6CHASH(mfccp->mf6cc_origin.sin6_addr,
			mfccp->mf6cc_mcastgrp.sin6_addr);
	for (rt = mf6ctable[hash], nstl = 0; rt; rt = rt->mf6c_next) {
		if (IN6_ARE_ADDR_EQUAL(&rt->mf6c_origin.sin6_addr,
				       &mfccp->mf6cc_origin.sin6_addr) &&
		    IN6_ARE_ADDR_EQUAL(&rt->mf6c_mcastgrp.sin6_addr,
				       &mfccp->mf6cc_mcastgrp.sin6_addr) &&
		    (rt->mf6c_stall != NULL)) {

			if (nstl++)
				log(LOG_ERR,
				    "add_m6fc: %s o %s g %s p %x dbx %p\n",
				    "multiple kernel entries",
				    ip6_sprintf(ip6bufo,
					    &mfccp->mf6cc_origin.sin6_addr),
				    ip6_sprintf(ip6bufg,
					    &mfccp->mf6cc_mcastgrp.sin6_addr),
				    mfccp->mf6cc_parent, rt->mf6c_stall);

			MRT6_DLOG(DEBUG_MFC, "o %s g %s p %x dbg %p",
			    ip6_sprintf(ip6bufo,
			    &mfccp->mf6cc_origin.sin6_addr),
			    ip6_sprintf(ip6bufg,
				&mfccp->mf6cc_mcastgrp.sin6_addr),
			    mfccp->mf6cc_parent, rt->mf6c_stall);

			rt->mf6c_origin     = mfccp->mf6cc_origin;
			rt->mf6c_mcastgrp   = mfccp->mf6cc_mcastgrp;
			rt->mf6c_parent     = mfccp->mf6cc_parent;
			rt->mf6c_ifset	    = mfccp->mf6cc_ifset;
			/* initialize pkt counters per src-grp */
			rt->mf6c_pkt_cnt    = 0;
			rt->mf6c_byte_cnt   = 0;
			rt->mf6c_wrong_if   = 0;

			rt->mf6c_expire = 0;	/* Don't clean this guy up */
			n6expire[hash]--;

			/* free packets Qed at the end of this entry */
			for (rte = rt->mf6c_stall; rte != NULL; ) {
				struct rtdetq *n = rte->next;
				ip6_mdq(rte->m, rte->ifp, rt);
				m_freem(rte->m);
#ifdef UPCALL_TIMING
				collate(&(rte->t));
#endif /* UPCALL_TIMING */
				free(rte, M_MRTABLE6);
				rte = n;
			}
			rt->mf6c_stall = NULL;
		}
	}

	/*
	 * It is possible that an entry is being inserted without an upcall
	 */
	if (nstl == 0) {
		MRT6_DLOG(DEBUG_MFC, "no upcall h %lu o %s g %s p %x", hash,
		    ip6_sprintf(ip6bufo, &mfccp->mf6cc_origin.sin6_addr),
		    ip6_sprintf(ip6bufg, &mfccp->mf6cc_mcastgrp.sin6_addr),
		    mfccp->mf6cc_parent);

		for (rt = mf6ctable[hash]; rt; rt = rt->mf6c_next) {

			if (IN6_ARE_ADDR_EQUAL(&rt->mf6c_origin.sin6_addr,
					       &mfccp->mf6cc_origin.sin6_addr)&&
			    IN6_ARE_ADDR_EQUAL(&rt->mf6c_mcastgrp.sin6_addr,
					       &mfccp->mf6cc_mcastgrp.sin6_addr)) {

				rt->mf6c_origin     = mfccp->mf6cc_origin;
				rt->mf6c_mcastgrp   = mfccp->mf6cc_mcastgrp;
				rt->mf6c_parent     = mfccp->mf6cc_parent;
				rt->mf6c_ifset	    = mfccp->mf6cc_ifset;
				/* initialize pkt counters per src-grp */
				rt->mf6c_pkt_cnt    = 0;
				rt->mf6c_byte_cnt   = 0;
				rt->mf6c_wrong_if   = 0;

				if (rt->mf6c_expire)
					n6expire[hash]--;
				rt->mf6c_expire	   = 0;
			}
		}
		if (rt == NULL) {
			/* no upcall, so make a new entry */
			rt = (struct mf6c *)malloc(sizeof(*rt), M_MRTABLE6,
						  M_NOWAIT);
			if (rt == NULL) {
				MFC6_UNLOCK();
				return (ENOBUFS);
			}

			/* insert new entry at head of hash chain */
			rt->mf6c_origin     = mfccp->mf6cc_origin;
			rt->mf6c_mcastgrp   = mfccp->mf6cc_mcastgrp;
			rt->mf6c_parent     = mfccp->mf6cc_parent;
			rt->mf6c_ifset	    = mfccp->mf6cc_ifset;
			/* initialize pkt counters per src-grp */
			rt->mf6c_pkt_cnt    = 0;
			rt->mf6c_byte_cnt   = 0;
			rt->mf6c_wrong_if   = 0;
			rt->mf6c_expire     = 0;
			rt->mf6c_stall = NULL;

			/* link into table */
			rt->mf6c_next  = mf6ctable[hash];
			mf6ctable[hash] = rt;
		}
	}

	MFC6_UNLOCK();
	return (0);
}

#ifdef UPCALL_TIMING
/*
 * collect delay statistics on the upcalls
 */
static void
collate(struct timeval *t)
{
	u_long d;
	struct timeval tp;
	u_long delta;

	GET_TIME(tp);

	if (TV_LT(*t, tp))
	{
		TV_DELTA(tp, *t, delta);

		d = delta >> 10;
		if (d > UPCALL_MAX)
			d = UPCALL_MAX;

		++upcall_data[d];
	}
}
#endif /* UPCALL_TIMING */

/*
 * Delete an mfc entry
 */
static int
del_m6fc(struct mf6cctl *mfccp)
{
#ifdef MRT6DEBUG
	char ip6bufo[INET6_ADDRSTRLEN], ip6bufg[INET6_ADDRSTRLEN];
#endif
	struct sockaddr_in6	origin;
	struct sockaddr_in6	mcastgrp;
	struct mf6c		*rt;
	struct mf6c		**nptr;
	u_long		hash;

	origin = mfccp->mf6cc_origin;
	mcastgrp = mfccp->mf6cc_mcastgrp;
	hash = MF6CHASH(origin.sin6_addr, mcastgrp.sin6_addr);

	MRT6_DLOG(DEBUG_MFC, "orig %s mcastgrp %s",
	    ip6_sprintf(ip6bufo, &origin.sin6_addr),
	    ip6_sprintf(ip6bufg, &mcastgrp.sin6_addr));

	MFC6_LOCK();

	nptr = &mf6ctable[hash];
	while ((rt = *nptr) != NULL) {
		if (IN6_ARE_ADDR_EQUAL(&origin.sin6_addr,
				       &rt->mf6c_origin.sin6_addr) &&
		    IN6_ARE_ADDR_EQUAL(&mcastgrp.sin6_addr,
				       &rt->mf6c_mcastgrp.sin6_addr) &&
		    rt->mf6c_stall == NULL)
			break;

		nptr = &rt->mf6c_next;
	}
	if (rt == NULL) {
		MFC6_UNLOCK();
		return (EADDRNOTAVAIL);
	}

	*nptr = rt->mf6c_next;
	free(rt, M_MRTABLE6);

	MFC6_UNLOCK();

	return (0);
}

static int
socket_send(struct socket *s, struct mbuf *mm, struct sockaddr_in6 *src)
{

	if (s) {
		if (sbappendaddr(&s->so_rcv,
				 (struct sockaddr *)src,
				 mm, (struct mbuf *)0) != 0) {
			sorwakeup(s);
			return (0);
		}
	}
	m_freem(mm);
	return (-1);
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
 *
 * NOTE: this implementation assumes that m->m_pkthdr.rcvif is NULL iff
 * this function is called in the originating context (i.e., not when
 * forwarding a packet from other node).  ip6_output(), which is currently the
 * only function that calls this function is called in the originating context,
 * explicitly ensures this condition.  It is caller's responsibility to ensure
 * that if this function is called from somewhere else in the originating
 * context in the future.
 */
int
X_ip6_mforward(struct ip6_hdr *ip6, struct ifnet *ifp, struct mbuf *m)
{
	struct rtdetq *rte;
	struct mbuf *mb0;
	struct mf6c *rt;
	struct mif6 *mifp;
	struct mbuf *mm;
	u_long hash;
	mifi_t mifi;
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];
#ifdef UPCALL_TIMING
	struct timeval tp;

	GET_TIME(tp);
#endif /* UPCALL_TIMING */

	MRT6_DLOG(DEBUG_FORWARD, "src %s, dst %s, ifindex %d",
	    ip6_sprintf(ip6bufs, &ip6->ip6_src),
	    ip6_sprintf(ip6bufd, &ip6->ip6_dst), ifp->if_index);

	/*
	 * Don't forward a packet with Hop limit of zero or one,
	 * or a packet destined to a local-only group.
	 */
	if (ip6->ip6_hlim <= 1 || IN6_IS_ADDR_MC_INTFACELOCAL(&ip6->ip6_dst) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&ip6->ip6_dst))
		return (0);
	ip6->ip6_hlim--;

	/*
	 * Source address check: do not forward packets with unspecified
	 * source. It was discussed in July 2000, on ipngwg mailing list.
	 * This is rather more serious than unicast cases, because some
	 * MLD packets can be sent with the unspecified source address
	 * (although such packets must normally set 1 to the hop limit field).
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
		IP6STAT_INC(ip6s_cantforward);
		if (V_ip6_log_time + V_ip6_log_interval < time_uptime) {
			V_ip6_log_time = time_uptime;
			log(LOG_DEBUG,
			    "cannot forward "
			    "from %s to %s nxt %d received on %s\n",
			    ip6_sprintf(ip6bufs, &ip6->ip6_src),
			    ip6_sprintf(ip6bufd, &ip6->ip6_dst),
			    ip6->ip6_nxt,
			    if_name(m->m_pkthdr.rcvif));
		}
		return (0);
	}

	MFC6_LOCK();

	/*
	 * Determine forwarding mifs from the forwarding cache table
	 */
	MF6CFIND(ip6->ip6_src, ip6->ip6_dst, rt);
	MRT6STAT_INC(mrt6s_mfc_lookups);

	/* Entry exists, so forward if necessary */
	if (rt) {
		MFC6_UNLOCK();
		return (ip6_mdq(m, ifp, rt));
	}

	/*
	 * If we don't have a route for packet's origin,
	 * Make a copy of the packet & send message to routing daemon.
	 */
	MRT6STAT_INC(mrt6s_no_route);
	MRT6_DLOG(DEBUG_FORWARD | DEBUG_MFC, "no rte s %s g %s",
	    ip6_sprintf(ip6bufs, &ip6->ip6_src),
	    ip6_sprintf(ip6bufd, &ip6->ip6_dst));

	/*
	 * Allocate mbufs early so that we don't do extra work if we
	 * are just going to fail anyway.
	 */
	rte = (struct rtdetq *)malloc(sizeof(*rte), M_MRTABLE6, M_NOWAIT);
	if (rte == NULL) {
		MFC6_UNLOCK();
		return (ENOBUFS);
	}
	mb0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	/*
	 * Pullup packet header if needed before storing it,
	 * as other references may modify it in the meantime.
	 */
	if (mb0 && (!M_WRITABLE(mb0) || mb0->m_len < sizeof(struct ip6_hdr)))
		mb0 = m_pullup(mb0, sizeof(struct ip6_hdr));
	if (mb0 == NULL) {
		free(rte, M_MRTABLE6);
		MFC6_UNLOCK();
		return (ENOBUFS);
	}

	/* is there an upcall waiting for this packet? */
	hash = MF6CHASH(ip6->ip6_src, ip6->ip6_dst);
	for (rt = mf6ctable[hash]; rt; rt = rt->mf6c_next) {
		if (IN6_ARE_ADDR_EQUAL(&ip6->ip6_src,
		    &rt->mf6c_origin.sin6_addr) &&
		    IN6_ARE_ADDR_EQUAL(&ip6->ip6_dst,
		    &rt->mf6c_mcastgrp.sin6_addr) && (rt->mf6c_stall != NULL))
			break;
	}

	if (rt == NULL) {
		struct mrt6msg *im;
#ifdef MRT6_OINIT
		struct omrt6msg *oim;
#endif
		/* no upcall, so make a new entry */
		rt = (struct mf6c *)malloc(sizeof(*rt), M_MRTABLE6, M_NOWAIT);
		if (rt == NULL) {
			free(rte, M_MRTABLE6);
			m_freem(mb0);
			MFC6_UNLOCK();
			return (ENOBUFS);
		}
		/*
		 * Make a copy of the header to send to the user
		 * level process
		 */
		mm = m_copym(mb0, 0, sizeof(struct ip6_hdr), M_NOWAIT);
		if (mm == NULL) {
			free(rte, M_MRTABLE6);
			m_freem(mb0);
			free(rt, M_MRTABLE6);
			MFC6_UNLOCK();
			return (ENOBUFS);
		}

		/*
		 * Send message to routing daemon
		 */
		sin6.sin6_addr = ip6->ip6_src;
		im = NULL;
#ifdef MRT6_OINIT
		oim = NULL;
#endif
		switch (V_ip6_mrouter_ver) {
#ifdef MRT6_OINIT
		case MRT6_OINIT:
			oim = mtod(mm, struct omrt6msg *);
			oim->im6_msgtype = MRT6MSG_NOCACHE;
			oim->im6_mbz = 0;
			break;
#endif
		case MRT6_INIT:
			im = mtod(mm, struct mrt6msg *);
			im->im6_msgtype = MRT6MSG_NOCACHE;
			im->im6_mbz = 0;
			break;
		default:
			free(rte, M_MRTABLE6);
			m_freem(mb0);
			free(rt, M_MRTABLE6);
			MFC6_UNLOCK();
			return (EINVAL);
		}

		MRT6_DLOG(DEBUG_FORWARD, "getting the iif info in the kernel");
		for (mifp = mif6table, mifi = 0;
		    mifi < nummifs && mifp->m6_ifp != ifp; mifp++, mifi++)
				;

		switch (V_ip6_mrouter_ver) {
#ifdef MRT6_OINIT
		case MRT6_OINIT:
			oim->im6_mif = mifi;
			break;
#endif
		case MRT6_INIT:
			im->im6_mif = mifi;
			break;
		}

		if (socket_send(V_ip6_mrouter, mm, &sin6) < 0) {
			log(LOG_WARNING, "ip6_mforward: ip6_mrouter "
			    "socket queue full\n");
			MRT6STAT_INC(mrt6s_upq_sockfull);
			free(rte, M_MRTABLE6);
			m_freem(mb0);
			free(rt, M_MRTABLE6);
			MFC6_UNLOCK();
			return (ENOBUFS);
		}

		MRT6STAT_INC(mrt6s_upcalls);

		/* insert new entry at head of hash chain */
		bzero(rt, sizeof(*rt));
		rt->mf6c_origin.sin6_family = AF_INET6;
		rt->mf6c_origin.sin6_len = sizeof(struct sockaddr_in6);
		rt->mf6c_origin.sin6_addr = ip6->ip6_src;
		rt->mf6c_mcastgrp.sin6_family = AF_INET6;
		rt->mf6c_mcastgrp.sin6_len = sizeof(struct sockaddr_in6);
		rt->mf6c_mcastgrp.sin6_addr = ip6->ip6_dst;
		rt->mf6c_expire = UPCALL_EXPIRE;
		n6expire[hash]++;
		rt->mf6c_parent = MF6C_INCOMPLETE_PARENT;

		/* link into table */
		rt->mf6c_next  = mf6ctable[hash];
		mf6ctable[hash] = rt;
		/* Add this entry to the end of the queue */
		rt->mf6c_stall = rte;
	} else {
		/* determine if q has overflowed */
		struct rtdetq **p;
		int npkts = 0;

		for (p = &rt->mf6c_stall; *p != NULL; p = &(*p)->next)
			if (++npkts > MAX_UPQ6) {
				MRT6STAT_INC(mrt6s_upq_ovflw);
				free(rte, M_MRTABLE6);
				m_freem(mb0);
				MFC6_UNLOCK();
				return (0);
			}

		/* Add this entry to the end of the queue */
		*p = rte;
	}

	rte->next = NULL;
	rte->m = mb0;
	rte->ifp = ifp;
#ifdef UPCALL_TIMING
	rte->t = tp;
#endif /* UPCALL_TIMING */

	MFC6_UNLOCK();

	return (0);
}

/*
 * Clean up cache entries if upcalls are not serviced
 * Call from the Slow Timeout mechanism, every half second.
 */
static void
expire_upcalls(void *unused)
{
#ifdef MRT6DEBUG
	char ip6bufo[INET6_ADDRSTRLEN], ip6bufg[INET6_ADDRSTRLEN];
#endif
	struct rtdetq *rte;
	struct mf6c *mfc, **nptr;
	u_long i;

	MFC6_LOCK();
	for (i = 0; i < MF6CTBLSIZ; i++) {
		if (n6expire[i] == 0)
			continue;
		nptr = &mf6ctable[i];
		while ((mfc = *nptr) != NULL) {
			rte = mfc->mf6c_stall;
			/*
			 * Skip real cache entries
			 * Make sure it wasn't marked to not expire (shouldn't happen)
			 * If it expires now
			 */
			if (rte != NULL &&
			    mfc->mf6c_expire != 0 &&
			    --mfc->mf6c_expire == 0) {
				MRT6_DLOG(DEBUG_EXPIRE, "expiring (%s %s)",
				    ip6_sprintf(ip6bufo, &mfc->mf6c_origin.sin6_addr),
				    ip6_sprintf(ip6bufg, &mfc->mf6c_mcastgrp.sin6_addr));
				/*
				 * drop all the packets
				 * free the mbuf with the pkt, if, timing info
				 */
				do {
					struct rtdetq *n = rte->next;
					m_freem(rte->m);
					free(rte, M_MRTABLE6);
					rte = n;
				} while (rte != NULL);
				MRT6STAT_INC(mrt6s_cache_cleanups);
				n6expire[i]--;

				*nptr = mfc->mf6c_next;
				free(mfc, M_MRTABLE6);
			} else {
				nptr = &mfc->mf6c_next;
			}
		}
	}
	MFC6_UNLOCK();
	callout_reset(&expire_upcalls_ch, EXPIRE_TIMEOUT,
	    expire_upcalls, NULL);
}

/*
 * Packet forwarding routine once entry in the cache is made
 */
static int
ip6_mdq(struct mbuf *m, struct ifnet *ifp, struct mf6c *rt)
{
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	mifi_t mifi, iif;
	struct mif6 *mifp;
	int plen = m->m_pkthdr.len;
	struct in6_addr src0, dst0; /* copies for local work */
	u_int32_t iszone, idzone, oszone, odzone;
	int error = 0;

/*
 * Macro to send packet on mif.  Since RSVP packets don't get counted on
 * input, they shouldn't get counted on output, so statistics keeping is
 * separate.
 */

#define MC6_SEND(ip6, mifp, m) do {				\
	if ((mifp)->m6_flags & MIFF_REGISTER)			\
		register_send((ip6), (mifp), (m));		\
	else							\
		phyint_send((ip6), (mifp), (m));		\
} while (/*CONSTCOND*/ 0)

	/*
	 * Don't forward if it didn't arrive from the parent mif
	 * for its origin.
	 */
	mifi = rt->mf6c_parent;
	if ((mifi >= nummifs) || (mif6table[mifi].m6_ifp != ifp)) {
		/* came in the wrong interface */
		MRT6_DLOG(DEBUG_FORWARD,
		    "wrong if: ifid %d mifi %d mififid %x", ifp->if_index,
		    mifi, mif6table[mifi].m6_ifp->if_index);
		MRT6STAT_INC(mrt6s_wrong_if);
		rt->mf6c_wrong_if++;
		/*
		 * If we are doing PIM processing, and we are forwarding
		 * packets on this interface, send a message to the
		 * routing daemon.
		 */
		/* have to make sure this is a valid mif */
		if (mifi < nummifs && mif6table[mifi].m6_ifp)
			if (V_pim6 && (m->m_flags & M_LOOP) == 0) {
				/*
				 * Check the M_LOOP flag to avoid an
				 * unnecessary PIM assert.
				 * XXX: M_LOOP is an ad-hoc hack...
				 */
				static struct sockaddr_in6 sin6 =
				{ sizeof(sin6), AF_INET6 };

				struct mbuf *mm;
				struct mrt6msg *im;
#ifdef MRT6_OINIT
				struct omrt6msg *oim;
#endif

				mm = m_copym(m, 0, sizeof(struct ip6_hdr),
				    M_NOWAIT);
				if (mm &&
				    (!M_WRITABLE(mm) ||
				     mm->m_len < sizeof(struct ip6_hdr)))
					mm = m_pullup(mm, sizeof(struct ip6_hdr));
				if (mm == NULL)
					return (ENOBUFS);

#ifdef MRT6_OINIT
				oim = NULL;
#endif
				im = NULL;
				switch (V_ip6_mrouter_ver) {
#ifdef MRT6_OINIT
				case MRT6_OINIT:
					oim = mtod(mm, struct omrt6msg *);
					oim->im6_msgtype = MRT6MSG_WRONGMIF;
					oim->im6_mbz = 0;
					break;
#endif
				case MRT6_INIT:
					im = mtod(mm, struct mrt6msg *);
					im->im6_msgtype = MRT6MSG_WRONGMIF;
					im->im6_mbz = 0;
					break;
				default:
					m_freem(mm);
					return (EINVAL);
				}

				for (mifp = mif6table, iif = 0;
				     iif < nummifs && mifp &&
					     mifp->m6_ifp != ifp;
				     mifp++, iif++)
					;

				switch (V_ip6_mrouter_ver) {
#ifdef MRT6_OINIT
				case MRT6_OINIT:
					oim->im6_mif = iif;
					sin6.sin6_addr = oim->im6_src;
					break;
#endif
				case MRT6_INIT:
					im->im6_mif = iif;
					sin6.sin6_addr = im->im6_src;
					break;
				}

				MRT6STAT_INC(mrt6s_upcalls);

				if (socket_send(V_ip6_mrouter, mm, &sin6) < 0) {
					MRT6_DLOG(DEBUG_ANY,
					    "ip6_mrouter socket queue full");
					MRT6STAT_INC(mrt6s_upq_sockfull);
					return (ENOBUFS);
				}	/* if socket Q full */
			}		/* if PIM */
		return (0);
	}			/* if wrong iif */

	/* If I sourced this packet, it counts as output, else it was input. */
	if (m->m_pkthdr.rcvif == NULL) {
		/* XXX: is rcvif really NULL when output?? */
		mif6table[mifi].m6_pkt_out++;
		mif6table[mifi].m6_bytes_out += plen;
	} else {
		mif6table[mifi].m6_pkt_in++;
		mif6table[mifi].m6_bytes_in += plen;
	}
	rt->mf6c_pkt_cnt++;
	rt->mf6c_byte_cnt += plen;

	/*
	 * For each mif, forward a copy of the packet if there are group
	 * members downstream on the interface.
	 */
	src0 = ip6->ip6_src;
	dst0 = ip6->ip6_dst;
	if ((error = in6_setscope(&src0, ifp, &iszone)) != 0 ||
	    (error = in6_setscope(&dst0, ifp, &idzone)) != 0) {
		IP6STAT_INC(ip6s_badscope);
		return (error);
	}
	for (mifp = mif6table, mifi = 0; mifi < nummifs; mifp++, mifi++) {
		if (IF_ISSET(mifi, &rt->mf6c_ifset)) {
			/*
			 * check if the outgoing packet is going to break
			 * a scope boundary.
			 * XXX For packets through PIM register tunnel
			 * interface, we believe a routing daemon.
			 */
			if (!(mif6table[rt->mf6c_parent].m6_flags &
			      MIFF_REGISTER) &&
			    !(mif6table[mifi].m6_flags & MIFF_REGISTER)) {
				if (in6_setscope(&src0, mif6table[mifi].m6_ifp,
				    &oszone) ||
				    in6_setscope(&dst0, mif6table[mifi].m6_ifp,
				    &odzone) ||
				    iszone != oszone ||
				    idzone != odzone) {
					IP6STAT_INC(ip6s_badscope);
					continue;
				}
			}

			mifp->m6_pkt_out++;
			mifp->m6_bytes_out += plen;
			MC6_SEND(ip6, mifp, m);
		}
	}
	return (0);
}

static void
phyint_send(struct ip6_hdr *ip6, struct mif6 *mifp, struct mbuf *m)
{
#ifdef MRT6DEBUG
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];
#endif
	struct mbuf *mb_copy;
	struct ifnet *ifp = mifp->m6_ifp;
	int error = 0;
	u_long linkmtu;

	/*
	 * Make a new reference to the packet; make sure that
	 * the IPv6 header is actually copied, not just referenced,
	 * so that ip6_output() only scribbles on the copy.
	 */
	mb_copy = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (mb_copy &&
	    (!M_WRITABLE(mb_copy) || mb_copy->m_len < sizeof(struct ip6_hdr)))
		mb_copy = m_pullup(mb_copy, sizeof(struct ip6_hdr));
	if (mb_copy == NULL) {
		return;
	}
	/* set MCAST flag to the outgoing packet */
	mb_copy->m_flags |= M_MCAST;

	/*
	 * If we sourced the packet, call ip6_output since we may devide
	 * the packet into fragments when the packet is too big for the
	 * outgoing interface.
	 * Otherwise, we can simply send the packet to the interface
	 * sending queue.
	 */
	if (m->m_pkthdr.rcvif == NULL) {
		struct ip6_moptions im6o;

		im6o.im6o_multicast_ifp = ifp;
		/* XXX: ip6_output will override ip6->ip6_hlim */
		im6o.im6o_multicast_hlim = ip6->ip6_hlim;
		im6o.im6o_multicast_loop = 1;
		error = ip6_output(mb_copy, NULL, NULL, IPV6_FORWARDING, &im6o,
		    NULL, NULL);

		MRT6_DLOG(DEBUG_XMIT, "mif %u err %d",
		    (uint16_t)(mifp - mif6table), error);
		return;
	}

	/*
	 * If configured to loop back multicasts by default,
	 * loop back a copy now.
	 */
	if (in6_mcast_loop)
		ip6_mloopback(ifp, m);

	/*
	 * Put the packet into the sending queue of the outgoing interface
	 * if it would fit in the MTU of the interface.
	 */
	linkmtu = IN6_LINKMTU(ifp);
	if (mb_copy->m_pkthdr.len <= linkmtu || linkmtu < IPV6_MMTU) {
		struct sockaddr_in6 dst6;

		bzero(&dst6, sizeof(dst6));
		dst6.sin6_len = sizeof(struct sockaddr_in6);
		dst6.sin6_family = AF_INET6;
		dst6.sin6_addr = ip6->ip6_dst;

		IP_PROBE(send, NULL, NULL, ip6, ifp, NULL, ip6);
		/*
		 * We just call if_output instead of nd6_output here, since
		 * we need no ND for a multicast forwarded packet...right?
		 */
		m_clrprotoflags(m);	/* Avoid confusing lower layers. */
		error = (*ifp->if_output)(ifp, mb_copy,
		    (struct sockaddr *)&dst6, NULL);
		MRT6_DLOG(DEBUG_XMIT, "mif %u err %d",
		    (uint16_t)(mifp - mif6table), error);
	} else {
		/*
		 * pMTU discovery is intentionally disabled by default, since
		 * various router may notify pMTU in multicast, which can be
		 * a DDoS to a router
		 */
		if (V_ip6_mcast_pmtu)
			icmp6_error(mb_copy, ICMP6_PACKET_TOO_BIG, 0, linkmtu);
		else {
			MRT6_DLOG(DEBUG_XMIT, " packet too big on %s o %s "
			    "g %s size %d (discarded)", if_name(ifp),
			    ip6_sprintf(ip6bufs, &ip6->ip6_src),
			    ip6_sprintf(ip6bufd, &ip6->ip6_dst),
			    mb_copy->m_pkthdr.len);
			m_freem(mb_copy); /* simply discard the packet */
		}
	}
}

static int
register_send(struct ip6_hdr *ip6, struct mif6 *mif, struct mbuf *m)
{
#ifdef MRT6DEBUG
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];
#endif
	struct mbuf *mm;
	int i, len = m->m_pkthdr.len;
	static struct sockaddr_in6 sin6 = { sizeof(sin6), AF_INET6 };
	struct mrt6msg *im6;

	MRT6_DLOG(DEBUG_ANY, "src %s dst %s",
	    ip6_sprintf(ip6bufs, &ip6->ip6_src),
	    ip6_sprintf(ip6bufd, &ip6->ip6_dst));
	PIM6STAT_INC(pim6s_snd_registers);

	/* Make a copy of the packet to send to the user level process. */
	mm = m_gethdr(M_NOWAIT, MT_DATA);
	if (mm == NULL)
		return (ENOBUFS);
	mm->m_data += max_linkhdr;
	mm->m_len = sizeof(struct ip6_hdr);

	if ((mm->m_next = m_copym(m, 0, M_COPYALL, M_NOWAIT)) == NULL) {
		m_freem(mm);
		return (ENOBUFS);
	}
	i = MHLEN - M_LEADINGSPACE(mm);
	if (i > len)
		i = len;
	mm = m_pullup(mm, i);
	if (mm == NULL)
		return (ENOBUFS);
/* TODO: check it! */
	mm->m_pkthdr.len = len + sizeof(struct ip6_hdr);

	/*
	 * Send message to routing daemon
	 */
	sin6.sin6_addr = ip6->ip6_src;

	im6 = mtod(mm, struct mrt6msg *);
	im6->im6_msgtype      = MRT6MSG_WHOLEPKT;
	im6->im6_mbz          = 0;

	im6->im6_mif = mif - mif6table;

	/* iif info is not given for reg. encap.n */
	MRT6STAT_INC(mrt6s_upcalls);

	if (socket_send(V_ip6_mrouter, mm, &sin6) < 0) {
		MRT6_DLOG(DEBUG_ANY, "ip6_mrouter socket queue full");
		MRT6STAT_INC(mrt6s_upq_sockfull);
		return (ENOBUFS);
	}
	return (0);
}

/*
 * pim6_encapcheck() is called by the encap6_input() path at runtime to
 * determine if a packet is for PIM; allowing PIM to be dynamically loaded
 * into the kernel.
 */
static int
pim6_encapcheck(const struct mbuf *m __unused, int off __unused,
    int proto __unused, void *arg __unused)
{

    KASSERT(proto == IPPROTO_PIM, ("not for IPPROTO_PIM"));
    return (8);		/* claim the datagram. */
}

/*
 * PIM sparse mode hook
 * Receives the pim control messages, and passes them up to the listening
 * socket, using rip6_input.
 * The only message processed is the REGISTER pim message; the pim header
 * is stripped off, and the inner packet is passed to register_mforward.
 */
static int
pim6_input(struct mbuf *m, int off, int proto, void *arg __unused)
{
	struct pim *pim; /* pointer to a pim struct */
	struct ip6_hdr *ip6;
	int pimlen;
	int minlen;

	PIM6STAT_INC(pim6s_rcv_total);

	ip6 = mtod(m, struct ip6_hdr *);
	pimlen = m->m_pkthdr.len - off;

	/*
	 * Validate lengths
	 */
	if (pimlen < PIM_MINLEN) {
		PIM6STAT_INC(pim6s_rcv_tooshort);
		MRT6_DLOG(DEBUG_PIM, "PIM packet too short");
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/*
	 * if the packet is at least as big as a REGISTER, go ahead
	 * and grab the PIM REGISTER header size, to avoid another
	 * possible m_pullup() later.
	 *
	 * PIM_MINLEN       == pimhdr + u_int32 == 8
	 * PIM6_REG_MINLEN   == pimhdr + reghdr + eip6hdr == 4 + 4 + 40
	 */
	minlen = (pimlen >= PIM6_REG_MINLEN) ? PIM6_REG_MINLEN : PIM_MINLEN;

	/*
	 * Make sure that the IP6 and PIM headers in contiguous memory, and
	 * possibly the PIM REGISTER header
	 */
#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, minlen, IPPROTO_DONE);
	/* adjust pointer */
	ip6 = mtod(m, struct ip6_hdr *);

	/* adjust mbuf to point to the PIM header */
	pim = (struct pim *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(pim, struct pim *, m, off, minlen);
	if (pim == NULL) {
		PIM6STAT_INC(pim6s_rcv_tooshort);
		return (IPPROTO_DONE);
	}
#endif

#define PIM6_CHECKSUM
#ifdef PIM6_CHECKSUM
	{
		int cksumlen;

		/*
		 * Validate checksum.
		 * If PIM REGISTER, exclude the data packet
		 */
		if (pim->pim_type == PIM_REGISTER)
			cksumlen = PIM_MINLEN;
		else
			cksumlen = pimlen;

		if (in6_cksum(m, IPPROTO_PIM, off, cksumlen)) {
			PIM6STAT_INC(pim6s_rcv_badsum);
			MRT6_DLOG(DEBUG_PIM, "invalid checksum");
			m_freem(m);
			return (IPPROTO_DONE);
		}
	}
#endif /* PIM_CHECKSUM */

	/* PIM version check */
	if (pim->pim_ver != PIM_VERSION) {
		PIM6STAT_INC(pim6s_rcv_badversion);
		MRT6_DLOG(DEBUG_ANY | DEBUG_ERR,
		    "incorrect version %d, expecting %d",
		    pim->pim_ver, PIM_VERSION);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	if (pim->pim_type == PIM_REGISTER) {
		/*
		 * since this is a REGISTER, we'll make a copy of the register
		 * headers ip6+pim+u_int32_t+encap_ip6, to be passed up to the
		 * routing daemon.
		 */
		static struct sockaddr_in6 dst = { sizeof(dst), AF_INET6 };

		struct mbuf *mcp;
		struct ip6_hdr *eip6;
		u_int32_t *reghdr;
		int rc;
#ifdef MRT6DEBUG
		char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];
#endif

		PIM6STAT_INC(pim6s_rcv_registers);

		if ((reg_mif_num >= nummifs) || (reg_mif_num == (mifi_t) -1)) {
			MRT6_DLOG(DEBUG_PIM, "register mif not set: %d",
			    reg_mif_num);
			m_freem(m);
			return (IPPROTO_DONE);
		}

		reghdr = (u_int32_t *)(pim + 1);

		if ((ntohl(*reghdr) & PIM_NULL_REGISTER))
			goto pim6_input_to_daemon;

		/*
		 * Validate length
		 */
		if (pimlen < PIM6_REG_MINLEN) {
			PIM6STAT_INC(pim6s_rcv_tooshort);
			PIM6STAT_INC(pim6s_rcv_badregisters);
			MRT6_DLOG(DEBUG_ANY | DEBUG_ERR, "register packet "
			    "size too small %d from %s",
			    pimlen, ip6_sprintf(ip6bufs, &ip6->ip6_src));
			m_freem(m);
			return (IPPROTO_DONE);
		}

		eip6 = (struct ip6_hdr *) (reghdr + 1);
		MRT6_DLOG(DEBUG_PIM, "eip6: %s -> %s, eip6 plen %d",
		    ip6_sprintf(ip6bufs, &eip6->ip6_src),
		    ip6_sprintf(ip6bufd, &eip6->ip6_dst),
		    ntohs(eip6->ip6_plen));

		/* verify the version number of the inner packet */
		if ((eip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION) {
			PIM6STAT_INC(pim6s_rcv_badregisters);
			MRT6_DLOG(DEBUG_ANY, "invalid IP version (%d) "
			    "of the inner packet",
			    (eip6->ip6_vfc & IPV6_VERSION));
			m_freem(m);
			return (IPPROTO_DONE);
		}

		/* verify the inner packet is destined to a mcast group */
		if (!IN6_IS_ADDR_MULTICAST(&eip6->ip6_dst)) {
			PIM6STAT_INC(pim6s_rcv_badregisters);
			MRT6_DLOG(DEBUG_PIM, "inner packet of register "
			    "is not multicast %s",
			    ip6_sprintf(ip6bufd, &eip6->ip6_dst));
			m_freem(m);
			return (IPPROTO_DONE);
		}

		/*
		 * make a copy of the whole header to pass to the daemon later.
		 */
		mcp = m_copym(m, 0, off + PIM6_REG_MINLEN, M_NOWAIT);
		if (mcp == NULL) {
			MRT6_DLOG(DEBUG_ANY | DEBUG_ERR, "pim register: "
			    "could not copy register head");
			m_freem(m);
			return (IPPROTO_DONE);
		}

		/*
		 * forward the inner ip6 packet; point m_data at the inner ip6.
		 */
		m_adj(m, off + PIM_MINLEN);
		MRT6_DLOG(DEBUG_PIM, "forwarding decapsulated register: "
		    "src %s, dst %s, mif %d",
		    ip6_sprintf(ip6bufs, &eip6->ip6_src),
		    ip6_sprintf(ip6bufd, &eip6->ip6_dst), reg_mif_num);

		rc = if_simloop(mif6table[reg_mif_num].m6_ifp, m,
				dst.sin6_family, 0);

		/* prepare the register head to send to the mrouting daemon */
		m = mcp;
	}

	/*
	 * Pass the PIM message up to the daemon; if it is a register message
	 * pass the 'head' only up to the daemon. This includes the
	 * encapsulator ip6 header, pim header, register header and the
	 * encapsulated ip6 header.
	 */
  pim6_input_to_daemon:
	return (rip6_input(&m, &off, proto));
}

static int
ip6_mroute_modevent(module_t mod, int type, void *unused)
{

	switch (type) {
	case MOD_LOAD:
		MROUTER6_LOCK_INIT();
		MFC6_LOCK_INIT();
		MIF6_LOCK_INIT();

		pim6_encap_cookie = ip6_encap_attach(&ipv6_encap_cfg,
		    NULL, M_WAITOK);
		if (pim6_encap_cookie == NULL) {
			printf("ip6_mroute: unable to attach pim6 encap\n");
			MIF6_LOCK_DESTROY();
			MFC6_LOCK_DESTROY();
			MROUTER6_LOCK_DESTROY();
			return (EINVAL);
		}

		ip6_mforward = X_ip6_mforward;
		ip6_mrouter_done = X_ip6_mrouter_done;
		ip6_mrouter_get = X_ip6_mrouter_get;
		ip6_mrouter_set = X_ip6_mrouter_set;
		mrt6_ioctl = X_mrt6_ioctl;
		break;

	case MOD_UNLOAD:
		if (V_ip6_mrouter != NULL)
			return EINVAL;

		if (pim6_encap_cookie) {
			ip6_encap_detach(pim6_encap_cookie);
			pim6_encap_cookie = NULL;
		}
		X_ip6_mrouter_done();
		ip6_mforward = NULL;
		ip6_mrouter_done = NULL;
		ip6_mrouter_get = NULL;
		ip6_mrouter_set = NULL;
		mrt6_ioctl = NULL;

		MIF6_LOCK_DESTROY();
		MFC6_LOCK_DESTROY();
		MROUTER6_LOCK_DESTROY();
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t ip6_mroutemod = {
	"ip6_mroute",
	ip6_mroute_modevent,
	0
};

DECLARE_MODULE(ip6_mroute, ip6_mroutemod, SI_SUB_PROTO_MC, SI_ORDER_ANY);
