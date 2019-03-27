/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Bruce Simpson.
 * Copyright (c) 1988 Stephen Deering.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)igmp.c	8.1 (Berkeley) 7/19/93
 */

/*
 * Internet Group Management Protocol (IGMP) routines.
 * [RFC1112, RFC2236, RFC3376]
 *
 * Written by Steve Deering, Stanford, May 1988.
 * Modified by Rosen Sharma, Stanford, Aug 1994.
 * Modified by Bill Fenner, Xerox PARC, Feb 1995.
 * Modified to fully comply to IGMPv2 by Bill Fenner, Oct 1995.
 * Significantly rewritten for IGMPv3, VIMAGE, and SMP by Bruce Simpson.
 *
 * MULTICAST Revision: 3.5.1.4
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/condvar.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#include <netinet/igmp.h>
#include <netinet/igmp_var.h>

#include <machine/in_cksum.h>

#include <security/mac/mac_framework.h>

#ifndef KTR_IGMPV3
#define KTR_IGMPV3 KTR_INET
#endif

static struct igmp_ifsoftc *
		igi_alloc_locked(struct ifnet *);
static void	igi_delete_locked(const struct ifnet *);
static void	igmp_dispatch_queue(struct mbufq *, int, const int);
static void	igmp_fasttimo_vnet(void);
static void	igmp_final_leave(struct in_multi *, struct igmp_ifsoftc *);
static int	igmp_handle_state_change(struct in_multi *,
		    struct igmp_ifsoftc *);
static int	igmp_initial_join(struct in_multi *, struct igmp_ifsoftc *);
static int	igmp_input_v1_query(struct ifnet *, const struct ip *,
		    const struct igmp *);
static int	igmp_input_v2_query(struct ifnet *, const struct ip *,
		    const struct igmp *);
static int	igmp_input_v3_query(struct ifnet *, const struct ip *,
		    /*const*/ struct igmpv3 *);
static int	igmp_input_v3_group_query(struct in_multi *,
		    struct igmp_ifsoftc *, int, /*const*/ struct igmpv3 *);
static int	igmp_input_v1_report(struct ifnet *, /*const*/ struct ip *,
		    /*const*/ struct igmp *);
static int	igmp_input_v2_report(struct ifnet *, /*const*/ struct ip *,
		    /*const*/ struct igmp *);
static void	igmp_intr(struct mbuf *);
static int	igmp_isgroupreported(const struct in_addr);
static struct mbuf *
		igmp_ra_alloc(void);
#ifdef KTR
static char *	igmp_rec_type_to_str(const int);
#endif
static void	igmp_set_version(struct igmp_ifsoftc *, const int);
static void	igmp_slowtimo_vnet(void);
static int	igmp_v1v2_queue_report(struct in_multi *, const int);
static void	igmp_v1v2_process_group_timer(struct in_multi *, const int);
static void	igmp_v1v2_process_querier_timers(struct igmp_ifsoftc *);
static void	igmp_v2_update_group(struct in_multi *, const int);
static void	igmp_v3_cancel_link_timers(struct igmp_ifsoftc *);
static void	igmp_v3_dispatch_general_query(struct igmp_ifsoftc *);
static struct mbuf *
		igmp_v3_encap_report(struct ifnet *, struct mbuf *);
static int	igmp_v3_enqueue_group_record(struct mbufq *,
		    struct in_multi *, const int, const int, const int);
static int	igmp_v3_enqueue_filter_change(struct mbufq *,
		    struct in_multi *);
static void	igmp_v3_process_group_timers(struct in_multi_head *,
		    struct mbufq *, struct mbufq *, struct in_multi *,
		    const int);
static int	igmp_v3_merge_state_changes(struct in_multi *,
		    struct mbufq *);
static void	igmp_v3_suppress_group_record(struct in_multi *);
static int	sysctl_igmp_default_version(SYSCTL_HANDLER_ARGS);
static int	sysctl_igmp_gsr(SYSCTL_HANDLER_ARGS);
static int	sysctl_igmp_ifinfo(SYSCTL_HANDLER_ARGS);

static const struct netisr_handler igmp_nh = {
	.nh_name = "igmp",
	.nh_handler = igmp_intr,
	.nh_proto = NETISR_IGMP,
	.nh_policy = NETISR_POLICY_SOURCE,
};

/*
 * System-wide globals.
 *
 * Unlocked access to these is OK, except for the global IGMP output
 * queue. The IGMP subsystem lock ends up being system-wide for the moment,
 * because all VIMAGEs have to share a global output queue, as netisrs
 * themselves are not virtualized.
 *
 * Locking:
 *  * The permitted lock order is: IN_MULTI_LIST_LOCK, IGMP_LOCK, IF_ADDR_LOCK.
 *    Any may be taken independently; if any are held at the same
 *    time, the above lock order must be followed.
 *  * All output is delegated to the netisr.
 *    Now that Giant has been eliminated, the netisr may be inlined.
 *  * IN_MULTI_LIST_LOCK covers in_multi.
 *  * IGMP_LOCK covers igmp_ifsoftc and any global variables in this file,
 *    including the output queue.
 *  * IF_ADDR_LOCK covers if_multiaddrs, which is used for a variety of
 *    per-link state iterators.
 *  * igmp_ifsoftc is valid as long as PF_INET is attached to the interface,
 *    therefore it is not refcounted.
 *    We allow unlocked reads of igmp_ifsoftc when accessed via in_multi.
 *
 * Reference counting
 *  * IGMP acquires its own reference every time an in_multi is passed to
 *    it and the group is being joined for the first time.
 *  * IGMP releases its reference(s) on in_multi in a deferred way,
 *    because the operations which process the release run as part of
 *    a loop whose control variables are directly affected by the release
 *    (that, and not recursing on the IF_ADDR_LOCK).
 *
 * VIMAGE: Each in_multi corresponds to an ifp, and each ifp corresponds
 * to a vnet in ifp->if_vnet.
 *
 * SMPng: XXX We may potentially race operations on ifma_protospec.
 * The problem is that we currently lack a clean way of taking the
 * IF_ADDR_LOCK() between the ifnet and in layers w/o recursing,
 * as anything which modifies ifma needs to be covered by that lock.
 * So check for ifma_protospec being NULL before proceeding.
 */
struct mtx		 igmp_mtx;

struct mbuf		*m_raopt;		 /* Router Alert option */
static MALLOC_DEFINE(M_IGMP, "igmp", "igmp state");

/*
 * VIMAGE-wide globals.
 *
 * The IGMPv3 timers themselves need to run per-image, however,
 * protosw timers run globally (see tcp).
 * An ifnet can only be in one vimage at a time, and the loopback
 * ifnet, loif, is itself virtualized.
 * It would otherwise be possible to seriously hose IGMP state,
 * and create inconsistencies in upstream multicast routing, if you have
 * multiple VIMAGEs running on the same link joining different multicast
 * groups, UNLESS the "primary IP address" is different. This is because
 * IGMP for IPv4 does not force link-local addresses to be used for each
 * node, unlike MLD for IPv6.
 * Obviously the IGMPv3 per-interface state has per-vimage granularity
 * also as a result.
 *
 * FUTURE: Stop using IFP_TO_IA/INADDR_ANY, and use source address selection
 * policy to control the address used by IGMP on the link.
 */
VNET_DEFINE_STATIC(int, interface_timers_running);	/* IGMPv3 general
							 * query response */
VNET_DEFINE_STATIC(int, state_change_timers_running);	/* IGMPv3 state-change
							 * retransmit */
VNET_DEFINE_STATIC(int, current_state_timers_running);	/* IGMPv1/v2 host
							 * report; IGMPv3 g/sg
							 * query response */

#define	V_interface_timers_running	VNET(interface_timers_running)
#define	V_state_change_timers_running	VNET(state_change_timers_running)
#define	V_current_state_timers_running	VNET(current_state_timers_running)

VNET_DEFINE_STATIC(LIST_HEAD(, igmp_ifsoftc), igi_head) =
    LIST_HEAD_INITIALIZER(igi_head);
VNET_DEFINE_STATIC(struct igmpstat, igmpstat) = {
	.igps_version = IGPS_VERSION_3,
	.igps_len = sizeof(struct igmpstat),
};
VNET_DEFINE_STATIC(struct timeval, igmp_gsrdelay) = {10, 0};

#define	V_igi_head			VNET(igi_head)
#define	V_igmpstat			VNET(igmpstat)
#define	V_igmp_gsrdelay			VNET(igmp_gsrdelay)

VNET_DEFINE_STATIC(int, igmp_recvifkludge) = 1;
VNET_DEFINE_STATIC(int, igmp_sendra) = 1;
VNET_DEFINE_STATIC(int, igmp_sendlocal) = 1;
VNET_DEFINE_STATIC(int, igmp_v1enable) = 1;
VNET_DEFINE_STATIC(int, igmp_v2enable) = 1;
VNET_DEFINE_STATIC(int, igmp_legacysupp);
VNET_DEFINE_STATIC(int, igmp_default_version) = IGMP_VERSION_3;

#define	V_igmp_recvifkludge		VNET(igmp_recvifkludge)
#define	V_igmp_sendra			VNET(igmp_sendra)
#define	V_igmp_sendlocal		VNET(igmp_sendlocal)
#define	V_igmp_v1enable			VNET(igmp_v1enable)
#define	V_igmp_v2enable			VNET(igmp_v2enable)
#define	V_igmp_legacysupp		VNET(igmp_legacysupp)
#define	V_igmp_default_version		VNET(igmp_default_version)

/*
 * Virtualized sysctls.
 */
SYSCTL_STRUCT(_net_inet_igmp, IGMPCTL_STATS, stats, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmpstat), igmpstat, "");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, recvifkludge, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_recvifkludge), 0,
    "Rewrite IGMPv1/v2 reports from 0.0.0.0 to contain subnet address");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, sendra, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_sendra), 0,
    "Send IP Router Alert option in IGMPv2/v3 messages");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, sendlocal, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_sendlocal), 0,
    "Send IGMP membership reports for 224.0.0.0/24 groups");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, v1enable, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_v1enable), 0,
    "Enable backwards compatibility with IGMPv1");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, v2enable, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_v2enable), 0,
    "Enable backwards compatibility with IGMPv2");
SYSCTL_INT(_net_inet_igmp, OID_AUTO, legacysupp, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(igmp_legacysupp), 0,
    "Allow v1/v2 reports to suppress v3 group responses");
SYSCTL_PROC(_net_inet_igmp, OID_AUTO, default_version,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    &VNET_NAME(igmp_default_version), 0, sysctl_igmp_default_version, "I",
    "Default version of IGMP to run on each interface");
SYSCTL_PROC(_net_inet_igmp, OID_AUTO, gsrdelay,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    &VNET_NAME(igmp_gsrdelay.tv_sec), 0, sysctl_igmp_gsr, "I",
    "Rate limit for IGMPv3 Group-and-Source queries in seconds");

/*
 * Non-virtualized sysctls.
 */
static SYSCTL_NODE(_net_inet_igmp, OID_AUTO, ifinfo,
    CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_igmp_ifinfo,
    "Per-interface IGMPv3 state");

static __inline void
igmp_save_context(struct mbuf *m, struct ifnet *ifp)
{

#ifdef VIMAGE
	m->m_pkthdr.PH_loc.ptr = ifp->if_vnet;
#endif /* VIMAGE */
	m->m_pkthdr.flowid = ifp->if_index;
}

static __inline void
igmp_scrub_context(struct mbuf *m)
{

	m->m_pkthdr.PH_loc.ptr = NULL;
	m->m_pkthdr.flowid = 0;
}

/*
 * Restore context from a queued IGMP output chain.
 * Return saved ifindex.
 *
 * VIMAGE: The assertion is there to make sure that we
 * actually called CURVNET_SET() with what's in the mbuf chain.
 */
static __inline uint32_t
igmp_restore_context(struct mbuf *m)
{

#ifdef notyet
#if defined(VIMAGE) && defined(INVARIANTS)
	KASSERT(curvnet == (m->m_pkthdr.PH_loc.ptr),
	    ("%s: called when curvnet was not restored", __func__));
#endif
#endif
	return (m->m_pkthdr.flowid);
}

/*
 * Retrieve or set default IGMP version.
 *
 * VIMAGE: Assume curvnet set by caller.
 * SMPng: NOTE: Serialized by IGMP lock.
 */
static int
sysctl_igmp_default_version(SYSCTL_HANDLER_ARGS)
{
	int	 error;
	int	 new;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error)
		return (error);

	IGMP_LOCK();

	new = V_igmp_default_version;

	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || !req->newptr)
		goto out_locked;

	if (new < IGMP_VERSION_1 || new > IGMP_VERSION_3) {
		error = EINVAL;
		goto out_locked;
	}

	CTR2(KTR_IGMPV3, "change igmp_default_version from %d to %d",
	     V_igmp_default_version, new);

	V_igmp_default_version = new;

out_locked:
	IGMP_UNLOCK();
	return (error);
}

/*
 * Retrieve or set threshold between group-source queries in seconds.
 *
 * VIMAGE: Assume curvnet set by caller.
 * SMPng: NOTE: Serialized by IGMP lock.
 */
static int
sysctl_igmp_gsr(SYSCTL_HANDLER_ARGS)
{
	int error;
	int i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error)
		return (error);

	IGMP_LOCK();

	i = V_igmp_gsrdelay.tv_sec;

	error = sysctl_handle_int(oidp, &i, 0, req);
	if (error || !req->newptr)
		goto out_locked;

	if (i < -1 || i >= 60) {
		error = EINVAL;
		goto out_locked;
	}

	CTR2(KTR_IGMPV3, "change igmp_gsrdelay from %d to %d",
	     V_igmp_gsrdelay.tv_sec, i);
	V_igmp_gsrdelay.tv_sec = i;

out_locked:
	IGMP_UNLOCK();
	return (error);
}

/*
 * Expose struct igmp_ifsoftc to userland, keyed by ifindex.
 * For use by ifmcstat(8).
 *
 * SMPng: NOTE: Does an unlocked ifindex space read.
 * VIMAGE: Assume curvnet set by caller. The node handler itself
 * is not directly virtualized.
 */
static int
sysctl_igmp_ifinfo(SYSCTL_HANDLER_ARGS)
{
	int			*name;
	int			 error;
	u_int			 namelen;
	struct ifnet		*ifp;
	struct igmp_ifsoftc	*igi;

	name = (int *)arg1;
	namelen = arg2;

	if (req->newptr != NULL)
		return (EPERM);

	if (namelen != 1)
		return (EINVAL);

	error = sysctl_wire_old_buffer(req, sizeof(struct igmp_ifinfo));
	if (error)
		return (error);

	IN_MULTI_LIST_LOCK();
	IGMP_LOCK();

	if (name[0] <= 0 || name[0] > V_if_index) {
		error = ENOENT;
		goto out_locked;
	}

	error = ENOENT;

	ifp = ifnet_byindex(name[0]);
	if (ifp == NULL)
		goto out_locked;

	LIST_FOREACH(igi, &V_igi_head, igi_link) {
		if (ifp == igi->igi_ifp) {
			struct igmp_ifinfo info;

			info.igi_version = igi->igi_version;
			info.igi_v1_timer = igi->igi_v1_timer;
			info.igi_v2_timer = igi->igi_v2_timer;
			info.igi_v3_timer = igi->igi_v3_timer;
			info.igi_flags = igi->igi_flags;
			info.igi_rv = igi->igi_rv;
			info.igi_qi = igi->igi_qi;
			info.igi_qri = igi->igi_qri;
			info.igi_uri = igi->igi_uri;
			error = SYSCTL_OUT(req, &info, sizeof(info));
			break;
		}
	}

out_locked:
	IGMP_UNLOCK();
	IN_MULTI_LIST_UNLOCK();
	return (error);
}

/*
 * Dispatch an entire queue of pending packet chains
 * using the netisr.
 * VIMAGE: Assumes the vnet pointer has been set.
 */
static void
igmp_dispatch_queue(struct mbufq *mq, int limit, const int loop)
{
	struct mbuf *m;

	while ((m = mbufq_dequeue(mq)) != NULL) {
		CTR3(KTR_IGMPV3, "%s: dispatch %p from %p", __func__, mq, m);
		if (loop)
			m->m_flags |= M_IGMP_LOOP;
		netisr_dispatch(NETISR_IGMP, m);
		if (--limit == 0)
			break;
	}
}

/*
 * Filter outgoing IGMP report state by group.
 *
 * Reports are ALWAYS suppressed for ALL-HOSTS (224.0.0.1).
 * If the net.inet.igmp.sendlocal sysctl is 0, then IGMP reports are
 * disabled for all groups in the 224.0.0.0/24 link-local scope. However,
 * this may break certain IGMP snooping switches which rely on the old
 * report behaviour.
 *
 * Return zero if the given group is one for which IGMP reports
 * should be suppressed, or non-zero if reports should be issued.
 */
static __inline int
igmp_isgroupreported(const struct in_addr addr)
{

	if (in_allhosts(addr) ||
	    ((!V_igmp_sendlocal && IN_LOCAL_GROUP(ntohl(addr.s_addr)))))
		return (0);

	return (1);
}

/*
 * Construct a Router Alert option to use in outgoing packets.
 */
static struct mbuf *
igmp_ra_alloc(void)
{
	struct mbuf	*m;
	struct ipoption	*p;

	m = m_get(M_WAITOK, MT_DATA);
	p = mtod(m, struct ipoption *);
	p->ipopt_dst.s_addr = INADDR_ANY;
	p->ipopt_list[0] = (char)IPOPT_RA;	/* Router Alert Option */
	p->ipopt_list[1] = 0x04;		/* 4 bytes long */
	p->ipopt_list[2] = IPOPT_EOL;		/* End of IP option list */
	p->ipopt_list[3] = 0x00;		/* pad byte */
	m->m_len = sizeof(p->ipopt_dst) + p->ipopt_list[1];

	return (m);
}

/*
 * Attach IGMP when PF_INET is attached to an interface.
 */
struct igmp_ifsoftc *
igmp_domifattach(struct ifnet *ifp)
{
	struct igmp_ifsoftc *igi;

	CTR3(KTR_IGMPV3, "%s: called for ifp %p(%s)",
	    __func__, ifp, ifp->if_xname);

	IGMP_LOCK();

	igi = igi_alloc_locked(ifp);
	if (!(ifp->if_flags & IFF_MULTICAST))
		igi->igi_flags |= IGIF_SILENT;

	IGMP_UNLOCK();

	return (igi);
}

/*
 * VIMAGE: assume curvnet set by caller.
 */
static struct igmp_ifsoftc *
igi_alloc_locked(/*const*/ struct ifnet *ifp)
{
	struct igmp_ifsoftc *igi;

	IGMP_LOCK_ASSERT();

	igi = malloc(sizeof(struct igmp_ifsoftc), M_IGMP, M_NOWAIT|M_ZERO);
	if (igi == NULL)
		goto out;

	igi->igi_ifp = ifp;
	igi->igi_version = V_igmp_default_version;
	igi->igi_flags = 0;
	igi->igi_rv = IGMP_RV_INIT;
	igi->igi_qi = IGMP_QI_INIT;
	igi->igi_qri = IGMP_QRI_INIT;
	igi->igi_uri = IGMP_URI_INIT;
	mbufq_init(&igi->igi_gq, IGMP_MAX_RESPONSE_PACKETS);

	LIST_INSERT_HEAD(&V_igi_head, igi, igi_link);

	CTR2(KTR_IGMPV3, "allocate igmp_ifsoftc for ifp %p(%s)",
	     ifp, ifp->if_xname);

out:
	return (igi);
}

/*
 * Hook for ifdetach.
 *
 * NOTE: Some finalization tasks need to run before the protocol domain
 * is detached, but also before the link layer does its cleanup.
 *
 * SMPNG: igmp_ifdetach() needs to take IF_ADDR_LOCK().
 * XXX This is also bitten by unlocked ifma_protospec access.
 */
void
igmp_ifdetach(struct ifnet *ifp)
{
	struct igmp_ifsoftc	*igi;
	struct ifmultiaddr	*ifma, *next;
	struct in_multi		*inm;
	struct in_multi_head inm_free_tmp;
	CTR3(KTR_IGMPV3, "%s: called for ifp %p(%s)", __func__, ifp,
	    ifp->if_xname);

	SLIST_INIT(&inm_free_tmp);
	IGMP_LOCK();

	igi = ((struct in_ifinfo *)ifp->if_afdata[AF_INET])->ii_igmp;
	if (igi->igi_version == IGMP_VERSION_3) {
		IF_ADDR_WLOCK(ifp);
	restart:
		CK_STAILQ_FOREACH_SAFE(ifma, &ifp->if_multiaddrs, ifma_link, next) {
			if (ifma->ifma_addr->sa_family != AF_INET ||
			    ifma->ifma_protospec == NULL)
				continue;
			inm = (struct in_multi *)ifma->ifma_protospec;
			if (inm->inm_state == IGMP_LEAVING_MEMBER)
				inm_rele_locked(&inm_free_tmp, inm);
			inm_clear_recorded(inm);
			if (__predict_false(ifma_restart)) {
				ifma_restart = false;
				goto restart;
			}
		}
		IF_ADDR_WUNLOCK(ifp);
		inm_release_list_deferred(&inm_free_tmp);
	}
	IGMP_UNLOCK();

}

/*
 * Hook for domifdetach.
 */
void
igmp_domifdetach(struct ifnet *ifp)
{

	CTR3(KTR_IGMPV3, "%s: called for ifp %p(%s)",
	    __func__, ifp, ifp->if_xname);

	IGMP_LOCK();
	igi_delete_locked(ifp);
	IGMP_UNLOCK();
}

static void
igi_delete_locked(const struct ifnet *ifp)
{
	struct igmp_ifsoftc *igi, *tigi;

	CTR3(KTR_IGMPV3, "%s: freeing igmp_ifsoftc for ifp %p(%s)",
	    __func__, ifp, ifp->if_xname);

	IGMP_LOCK_ASSERT();

	LIST_FOREACH_SAFE(igi, &V_igi_head, igi_link, tigi) {
		if (igi->igi_ifp == ifp) {
			/*
			 * Free deferred General Query responses.
			 */
			mbufq_drain(&igi->igi_gq);

			LIST_REMOVE(igi, igi_link);
			free(igi, M_IGMP);
			return;
		}
	}
}

/*
 * Process a received IGMPv1 query.
 * Return non-zero if the message should be dropped.
 *
 * VIMAGE: The curvnet pointer is derived from the input ifp.
 */
static int
igmp_input_v1_query(struct ifnet *ifp, const struct ip *ip,
    const struct igmp *igmp)
{
	struct epoch_tracker	 et;
	struct ifmultiaddr	*ifma;
	struct igmp_ifsoftc	*igi;
	struct in_multi		*inm;

	/*
	 * IGMPv1 Host Mmembership Queries SHOULD always be addressed to
	 * 224.0.0.1. They are always treated as General Queries.
	 * igmp_group is always ignored. Do not drop it as a userland
	 * daemon may wish to see it.
	 * XXX SMPng: unlocked increments in igmpstat assumed atomic.
	 */
	if (!in_allhosts(ip->ip_dst) || !in_nullhost(igmp->igmp_group)) {
		IGMPSTAT_INC(igps_rcv_badqueries);
		return (0);
	}
	IGMPSTAT_INC(igps_rcv_gen_queries);

	IN_MULTI_LIST_LOCK();
	IGMP_LOCK();

	igi = ((struct in_ifinfo *)ifp->if_afdata[AF_INET])->ii_igmp;
	KASSERT(igi != NULL, ("%s: no igmp_ifsoftc for ifp %p", __func__, ifp));

	if (igi->igi_flags & IGIF_LOOPBACK) {
		CTR2(KTR_IGMPV3, "ignore v1 query on IGIF_LOOPBACK ifp %p(%s)",
		    ifp, ifp->if_xname);
		goto out_locked;
	}

	/*
	 * Switch to IGMPv1 host compatibility mode.
	 */
	igmp_set_version(igi, IGMP_VERSION_1);

	CTR2(KTR_IGMPV3, "process v1 query on ifp %p(%s)", ifp, ifp->if_xname);

	/*
	 * Start the timers in all of our group records
	 * for the interface on which the query arrived,
	 * except those which are already running.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;
		inm = (struct in_multi *)ifma->ifma_protospec;
		if (inm->inm_timer != 0)
			continue;
		switch (inm->inm_state) {
		case IGMP_NOT_MEMBER:
		case IGMP_SILENT_MEMBER:
			break;
		case IGMP_G_QUERY_PENDING_MEMBER:
		case IGMP_SG_QUERY_PENDING_MEMBER:
		case IGMP_REPORTING_MEMBER:
		case IGMP_IDLE_MEMBER:
		case IGMP_LAZY_MEMBER:
		case IGMP_SLEEPING_MEMBER:
		case IGMP_AWAKENING_MEMBER:
			inm->inm_state = IGMP_REPORTING_MEMBER;
			inm->inm_timer = IGMP_RANDOM_DELAY(
			    IGMP_V1V2_MAX_RI * PR_FASTHZ);
			V_current_state_timers_running = 1;
			break;
		case IGMP_LEAVING_MEMBER:
			break;
		}
	}
	NET_EPOCH_EXIT(et);

out_locked:
	IGMP_UNLOCK();
	IN_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Process a received IGMPv2 general or group-specific query.
 */
static int
igmp_input_v2_query(struct ifnet *ifp, const struct ip *ip,
    const struct igmp *igmp)
{
	struct epoch_tracker	 et;
	struct ifmultiaddr	*ifma;
	struct igmp_ifsoftc	*igi;
	struct in_multi		*inm;
	int			 is_general_query;
	uint16_t		 timer;

	is_general_query = 0;

	/*
	 * Validate address fields upfront.
	 * XXX SMPng: unlocked increments in igmpstat assumed atomic.
	 */
	if (in_nullhost(igmp->igmp_group)) {
		/*
		 * IGMPv2 General Query.
		 * If this was not sent to the all-hosts group, ignore it.
		 */
		if (!in_allhosts(ip->ip_dst))
			return (0);
		IGMPSTAT_INC(igps_rcv_gen_queries);
		is_general_query = 1;
	} else {
		/* IGMPv2 Group-Specific Query. */
		IGMPSTAT_INC(igps_rcv_group_queries);
	}

	IN_MULTI_LIST_LOCK();
	IGMP_LOCK();

	igi = ((struct in_ifinfo *)ifp->if_afdata[AF_INET])->ii_igmp;
	KASSERT(igi != NULL, ("%s: no igmp_ifsoftc for ifp %p", __func__, ifp));

	if (igi->igi_flags & IGIF_LOOPBACK) {
		CTR2(KTR_IGMPV3, "ignore v2 query on IGIF_LOOPBACK ifp %p(%s)",
		    ifp, ifp->if_xname);
		goto out_locked;
	}

	/*
	 * Ignore v2 query if in v1 Compatibility Mode.
	 */
	if (igi->igi_version == IGMP_VERSION_1)
		goto out_locked;

	igmp_set_version(igi, IGMP_VERSION_2);

	timer = igmp->igmp_code * PR_FASTHZ / IGMP_TIMER_SCALE;
	if (timer == 0)
		timer = 1;

	if (is_general_query) {
		/*
		 * For each reporting group joined on this
		 * interface, kick the report timer.
		 */
		CTR2(KTR_IGMPV3, "process v2 general query on ifp %p(%s)",
		    ifp, ifp->if_xname);
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_INET ||
			    ifma->ifma_protospec == NULL)
				continue;
			inm = (struct in_multi *)ifma->ifma_protospec;
			igmp_v2_update_group(inm, timer);
		}
		NET_EPOCH_EXIT(et);
	} else {
		/*
		 * Group-specific IGMPv2 query, we need only
		 * look up the single group to process it.
		 */
		inm = inm_lookup(ifp, igmp->igmp_group);
		if (inm != NULL) {
			CTR3(KTR_IGMPV3,
			    "process v2 query 0x%08x on ifp %p(%s)",
			    ntohl(igmp->igmp_group.s_addr), ifp, ifp->if_xname);
			igmp_v2_update_group(inm, timer);
		}
	}

out_locked:
	IGMP_UNLOCK();
	IN_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Update the report timer on a group in response to an IGMPv2 query.
 *
 * If we are becoming the reporting member for this group, start the timer.
 * If we already are the reporting member for this group, and timer is
 * below the threshold, reset it.
 *
 * We may be updating the group for the first time since we switched
 * to IGMPv3. If we are, then we must clear any recorded source lists,
 * and transition to REPORTING state; the group timer is overloaded
 * for group and group-source query responses. 
 *
 * Unlike IGMPv3, the delay per group should be jittered
 * to avoid bursts of IGMPv2 reports.
 */
static void
igmp_v2_update_group(struct in_multi *inm, const int timer)
{

	CTR4(KTR_IGMPV3, "0x%08x: %s/%s timer=%d", __func__,
	    ntohl(inm->inm_addr.s_addr), inm->inm_ifp->if_xname, timer);

	IN_MULTI_LIST_LOCK_ASSERT();

	switch (inm->inm_state) {
	case IGMP_NOT_MEMBER:
	case IGMP_SILENT_MEMBER:
		break;
	case IGMP_REPORTING_MEMBER:
		if (inm->inm_timer != 0 &&
		    inm->inm_timer <= timer) {
			CTR1(KTR_IGMPV3, "%s: REPORTING and timer running, "
			    "skipping.", __func__);
			break;
		}
		/* FALLTHROUGH */
	case IGMP_SG_QUERY_PENDING_MEMBER:
	case IGMP_G_QUERY_PENDING_MEMBER:
	case IGMP_IDLE_MEMBER:
	case IGMP_LAZY_MEMBER:
	case IGMP_AWAKENING_MEMBER:
		CTR1(KTR_IGMPV3, "%s: ->REPORTING", __func__);
		inm->inm_state = IGMP_REPORTING_MEMBER;
		inm->inm_timer = IGMP_RANDOM_DELAY(timer);
		V_current_state_timers_running = 1;
		break;
	case IGMP_SLEEPING_MEMBER:
		CTR1(KTR_IGMPV3, "%s: ->AWAKENING", __func__);
		inm->inm_state = IGMP_AWAKENING_MEMBER;
		break;
	case IGMP_LEAVING_MEMBER:
		break;
	}
}

/*
 * Process a received IGMPv3 general, group-specific or
 * group-and-source-specific query.
 * Assumes m has already been pulled up to the full IGMP message length.
 * Return 0 if successful, otherwise an appropriate error code is returned.
 */
static int
igmp_input_v3_query(struct ifnet *ifp, const struct ip *ip,
    /*const*/ struct igmpv3 *igmpv3)
{
	struct igmp_ifsoftc	*igi;
	struct in_multi		*inm;
	int			 is_general_query;
	uint32_t		 maxresp, nsrc, qqi;
	uint16_t		 timer;
	uint8_t			 qrv;

	is_general_query = 0;

	CTR2(KTR_IGMPV3, "process v3 query on ifp %p(%s)", ifp, ifp->if_xname);

	maxresp = igmpv3->igmp_code;	/* in 1/10ths of a second */
	if (maxresp >= 128) {
		maxresp = IGMP_MANT(igmpv3->igmp_code) <<
			  (IGMP_EXP(igmpv3->igmp_code) + 3);
	}

	/*
	 * Robustness must never be less than 2 for on-wire IGMPv3.
	 * FUTURE: Check if ifp has IGIF_LOOPBACK set, as we will make
	 * an exception for interfaces whose IGMPv3 state changes
	 * are redirected to loopback (e.g. MANET).
	 */
	qrv = IGMP_QRV(igmpv3->igmp_misc);
	if (qrv < 2) {
		CTR3(KTR_IGMPV3, "%s: clamping qrv %d to %d", __func__,
		    qrv, IGMP_RV_INIT);
		qrv = IGMP_RV_INIT;
	}

	qqi = igmpv3->igmp_qqi;
	if (qqi >= 128) {
		qqi = IGMP_MANT(igmpv3->igmp_qqi) <<
		     (IGMP_EXP(igmpv3->igmp_qqi) + 3);
	}

	timer = maxresp * PR_FASTHZ / IGMP_TIMER_SCALE;
	if (timer == 0)
		timer = 1;

	nsrc = ntohs(igmpv3->igmp_numsrc);

	/*
	 * Validate address fields and versions upfront before
	 * accepting v3 query.
	 * XXX SMPng: Unlocked access to igmpstat counters here.
	 */
	if (in_nullhost(igmpv3->igmp_group)) {
		/*
		 * IGMPv3 General Query.
		 *
		 * General Queries SHOULD be directed to 224.0.0.1.
		 * A general query with a source list has undefined
		 * behaviour; discard it.
		 */
		IGMPSTAT_INC(igps_rcv_gen_queries);
		if (!in_allhosts(ip->ip_dst) || nsrc > 0) {
			IGMPSTAT_INC(igps_rcv_badqueries);
			return (0);
		}
		is_general_query = 1;
	} else {
		/* Group or group-source specific query. */
		if (nsrc == 0)
			IGMPSTAT_INC(igps_rcv_group_queries);
		else
			IGMPSTAT_INC(igps_rcv_gsr_queries);
	}

	IN_MULTI_LIST_LOCK();
	IGMP_LOCK();

	igi = ((struct in_ifinfo *)ifp->if_afdata[AF_INET])->ii_igmp;
	KASSERT(igi != NULL, ("%s: no igmp_ifsoftc for ifp %p", __func__, ifp));

	if (igi->igi_flags & IGIF_LOOPBACK) {
		CTR2(KTR_IGMPV3, "ignore v3 query on IGIF_LOOPBACK ifp %p(%s)",
		    ifp, ifp->if_xname);
		goto out_locked;
	}

	/*
	 * Discard the v3 query if we're in Compatibility Mode.
	 * The RFC is not obviously worded that hosts need to stay in
	 * compatibility mode until the Old Version Querier Present
	 * timer expires.
	 */
	if (igi->igi_version != IGMP_VERSION_3) {
		CTR3(KTR_IGMPV3, "ignore v3 query in v%d mode on ifp %p(%s)",
		    igi->igi_version, ifp, ifp->if_xname);
		goto out_locked;
	}

	igmp_set_version(igi, IGMP_VERSION_3);
	igi->igi_rv = qrv;
	igi->igi_qi = qqi;
	igi->igi_qri = maxresp;

	CTR4(KTR_IGMPV3, "%s: qrv %d qi %d qri %d", __func__, qrv, qqi,
	    maxresp);

	if (is_general_query) {
		/*
		 * Schedule a current-state report on this ifp for
		 * all groups, possibly containing source lists.
		 * If there is a pending General Query response
		 * scheduled earlier than the selected delay, do
		 * not schedule any other reports.
		 * Otherwise, reset the interface timer.
		 */
		CTR2(KTR_IGMPV3, "process v3 general query on ifp %p(%s)",
		    ifp, ifp->if_xname);
		if (igi->igi_v3_timer == 0 || igi->igi_v3_timer >= timer) {
			igi->igi_v3_timer = IGMP_RANDOM_DELAY(timer);
			V_interface_timers_running = 1;
		}
	} else {
		/*
		 * Group-source-specific queries are throttled on
		 * a per-group basis to defeat denial-of-service attempts.
		 * Queries for groups we are not a member of on this
		 * link are simply ignored.
		 */
		inm = inm_lookup(ifp, igmpv3->igmp_group);
		if (inm == NULL)
			goto out_locked;
		if (nsrc > 0) {
			if (!ratecheck(&inm->inm_lastgsrtv,
			    &V_igmp_gsrdelay)) {
				CTR1(KTR_IGMPV3, "%s: GS query throttled.",
				    __func__);
				IGMPSTAT_INC(igps_drop_gsr_queries);
				goto out_locked;
			}
		}
		CTR3(KTR_IGMPV3, "process v3 0x%08x query on ifp %p(%s)",
		     ntohl(igmpv3->igmp_group.s_addr), ifp, ifp->if_xname);
		/*
		 * If there is a pending General Query response
		 * scheduled sooner than the selected delay, no
		 * further report need be scheduled.
		 * Otherwise, prepare to respond to the
		 * group-specific or group-and-source query.
		 */
		if (igi->igi_v3_timer == 0 || igi->igi_v3_timer >= timer)
			igmp_input_v3_group_query(inm, igi, timer, igmpv3);
	}

out_locked:
	IGMP_UNLOCK();
	IN_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Process a received IGMPv3 group-specific or group-and-source-specific
 * query.
 * Return <0 if any error occurred. Currently this is ignored.
 */
static int
igmp_input_v3_group_query(struct in_multi *inm, struct igmp_ifsoftc *igi,
    int timer, /*const*/ struct igmpv3 *igmpv3)
{
	int			 retval;
	uint16_t		 nsrc;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	retval = 0;

	switch (inm->inm_state) {
	case IGMP_NOT_MEMBER:
	case IGMP_SILENT_MEMBER:
	case IGMP_SLEEPING_MEMBER:
	case IGMP_LAZY_MEMBER:
	case IGMP_AWAKENING_MEMBER:
	case IGMP_IDLE_MEMBER:
	case IGMP_LEAVING_MEMBER:
		return (retval);
		break;
	case IGMP_REPORTING_MEMBER:
	case IGMP_G_QUERY_PENDING_MEMBER:
	case IGMP_SG_QUERY_PENDING_MEMBER:
		break;
	}

	nsrc = ntohs(igmpv3->igmp_numsrc);

	/*
	 * Deal with group-specific queries upfront.
	 * If any group query is already pending, purge any recorded
	 * source-list state if it exists, and schedule a query response
	 * for this group-specific query.
	 */
	if (nsrc == 0) {
		if (inm->inm_state == IGMP_G_QUERY_PENDING_MEMBER ||
		    inm->inm_state == IGMP_SG_QUERY_PENDING_MEMBER) {
			inm_clear_recorded(inm);
			timer = min(inm->inm_timer, timer);
		}
		inm->inm_state = IGMP_G_QUERY_PENDING_MEMBER;
		inm->inm_timer = IGMP_RANDOM_DELAY(timer);
		V_current_state_timers_running = 1;
		return (retval);
	}

	/*
	 * Deal with the case where a group-and-source-specific query has
	 * been received but a group-specific query is already pending.
	 */
	if (inm->inm_state == IGMP_G_QUERY_PENDING_MEMBER) {
		timer = min(inm->inm_timer, timer);
		inm->inm_timer = IGMP_RANDOM_DELAY(timer);
		V_current_state_timers_running = 1;
		return (retval);
	}

	/*
	 * Finally, deal with the case where a group-and-source-specific
	 * query has been received, where a response to a previous g-s-r
	 * query exists, or none exists.
	 * In this case, we need to parse the source-list which the Querier
	 * has provided us with and check if we have any source list filter
	 * entries at T1 for these sources. If we do not, there is no need
	 * schedule a report and the query may be dropped.
	 * If we do, we must record them and schedule a current-state
	 * report for those sources.
	 * FIXME: Handling source lists larger than 1 mbuf requires that
	 * we pass the mbuf chain pointer down to this function, and use
	 * m_getptr() to walk the chain.
	 */
	if (inm->inm_nsrc > 0) {
		const struct in_addr	*ap;
		int			 i, nrecorded;

		ap = (const struct in_addr *)(igmpv3 + 1);
		nrecorded = 0;
		for (i = 0; i < nsrc; i++, ap++) {
			retval = inm_record_source(inm, ap->s_addr);
			if (retval < 0)
				break;
			nrecorded += retval;
		}
		if (nrecorded > 0) {
			CTR1(KTR_IGMPV3,
			    "%s: schedule response to SG query", __func__);
			inm->inm_state = IGMP_SG_QUERY_PENDING_MEMBER;
			inm->inm_timer = IGMP_RANDOM_DELAY(timer);
			V_current_state_timers_running = 1;
		}
	}

	return (retval);
}

/*
 * Process a received IGMPv1 host membership report.
 *
 * NOTE: 0.0.0.0 workaround breaks const correctness.
 */
static int
igmp_input_v1_report(struct ifnet *ifp, /*const*/ struct ip *ip,
    /*const*/ struct igmp *igmp)
{
	struct rm_priotracker in_ifa_tracker;
	struct in_ifaddr *ia;
	struct in_multi *inm;

	IGMPSTAT_INC(igps_rcv_reports);

	if (ifp->if_flags & IFF_LOOPBACK)
		return (0);

	if (!IN_MULTICAST(ntohl(igmp->igmp_group.s_addr)) ||
	    !in_hosteq(igmp->igmp_group, ip->ip_dst)) {
		IGMPSTAT_INC(igps_rcv_badreports);
		return (EINVAL);
	}

	/*
	 * RFC 3376, Section 4.2.13, 9.2, 9.3:
	 * Booting clients may use the source address 0.0.0.0. Some
	 * IGMP daemons may not know how to use IP_RECVIF to determine
	 * the interface upon which this message was received.
	 * Replace 0.0.0.0 with the subnet address if told to do so.
	 */
	if (V_igmp_recvifkludge && in_nullhost(ip->ip_src)) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		IFP_TO_IA(ifp, ia, &in_ifa_tracker);
		if (ia != NULL)
			ip->ip_src.s_addr = htonl(ia->ia_subnet);
		NET_EPOCH_EXIT(et);
	}

	CTR3(KTR_IGMPV3, "process v1 report 0x%08x on ifp %p(%s)",
	     ntohl(igmp->igmp_group.s_addr), ifp, ifp->if_xname);

	/*
	 * IGMPv1 report suppression.
	 * If we are a member of this group, and our membership should be
	 * reported, stop our group timer and transition to the 'lazy' state.
	 */
	IN_MULTI_LIST_LOCK();
	inm = inm_lookup(ifp, igmp->igmp_group);
	if (inm != NULL) {
		struct igmp_ifsoftc *igi;

		igi = inm->inm_igi;
		if (igi == NULL) {
			KASSERT(igi != NULL,
			    ("%s: no igi for ifp %p", __func__, ifp));
			goto out_locked;
		}

		IGMPSTAT_INC(igps_rcv_ourreports);

		/*
		 * If we are in IGMPv3 host mode, do not allow the
		 * other host's IGMPv1 report to suppress our reports
		 * unless explicitly configured to do so.
		 */
		if (igi->igi_version == IGMP_VERSION_3) {
			if (V_igmp_legacysupp)
				igmp_v3_suppress_group_record(inm);
			goto out_locked;
		}

		inm->inm_timer = 0;

		switch (inm->inm_state) {
		case IGMP_NOT_MEMBER:
		case IGMP_SILENT_MEMBER:
			break;
		case IGMP_IDLE_MEMBER:
		case IGMP_LAZY_MEMBER:
		case IGMP_AWAKENING_MEMBER:
			CTR3(KTR_IGMPV3,
			    "report suppressed for 0x%08x on ifp %p(%s)",
			    ntohl(igmp->igmp_group.s_addr), ifp,
			    ifp->if_xname);
		case IGMP_SLEEPING_MEMBER:
			inm->inm_state = IGMP_SLEEPING_MEMBER;
			break;
		case IGMP_REPORTING_MEMBER:
			CTR3(KTR_IGMPV3,
			    "report suppressed for 0x%08x on ifp %p(%s)",
			    ntohl(igmp->igmp_group.s_addr), ifp,
			    ifp->if_xname);
			if (igi->igi_version == IGMP_VERSION_1)
				inm->inm_state = IGMP_LAZY_MEMBER;
			else if (igi->igi_version == IGMP_VERSION_2)
				inm->inm_state = IGMP_SLEEPING_MEMBER;
			break;
		case IGMP_G_QUERY_PENDING_MEMBER:
		case IGMP_SG_QUERY_PENDING_MEMBER:
		case IGMP_LEAVING_MEMBER:
			break;
		}
	}

out_locked:
	IN_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Process a received IGMPv2 host membership report.
 *
 * NOTE: 0.0.0.0 workaround breaks const correctness.
 */
static int
igmp_input_v2_report(struct ifnet *ifp, /*const*/ struct ip *ip,
    /*const*/ struct igmp *igmp)
{
	struct rm_priotracker in_ifa_tracker;
	struct epoch_tracker et;
	struct in_ifaddr *ia;
	struct in_multi *inm;

	/*
	 * Make sure we don't hear our own membership report.  Fast
	 * leave requires knowing that we are the only member of a
	 * group.
	 */
	NET_EPOCH_ENTER(et);
	IFP_TO_IA(ifp, ia, &in_ifa_tracker);
	if (ia != NULL && in_hosteq(ip->ip_src, IA_SIN(ia)->sin_addr)) {
		NET_EPOCH_EXIT(et);
		return (0);
	}

	IGMPSTAT_INC(igps_rcv_reports);

	if (ifp->if_flags & IFF_LOOPBACK) {
		NET_EPOCH_EXIT(et);
		return (0);
	}

	if (!IN_MULTICAST(ntohl(igmp->igmp_group.s_addr)) ||
	    !in_hosteq(igmp->igmp_group, ip->ip_dst)) {
		NET_EPOCH_EXIT(et);
		IGMPSTAT_INC(igps_rcv_badreports);
		return (EINVAL);
	}

	/*
	 * RFC 3376, Section 4.2.13, 9.2, 9.3:
	 * Booting clients may use the source address 0.0.0.0. Some
	 * IGMP daemons may not know how to use IP_RECVIF to determine
	 * the interface upon which this message was received.
	 * Replace 0.0.0.0 with the subnet address if told to do so.
	 */
	if (V_igmp_recvifkludge && in_nullhost(ip->ip_src)) {
		if (ia != NULL)
			ip->ip_src.s_addr = htonl(ia->ia_subnet);
	}
	NET_EPOCH_EXIT(et);

	CTR3(KTR_IGMPV3, "process v2 report 0x%08x on ifp %p(%s)",
	     ntohl(igmp->igmp_group.s_addr), ifp, ifp->if_xname);

	/*
	 * IGMPv2 report suppression.
	 * If we are a member of this group, and our membership should be
	 * reported, and our group timer is pending or about to be reset,
	 * stop our group timer by transitioning to the 'lazy' state.
	 */
	IN_MULTI_LIST_LOCK();
	inm = inm_lookup(ifp, igmp->igmp_group);
	if (inm != NULL) {
		struct igmp_ifsoftc *igi;

		igi = inm->inm_igi;
		KASSERT(igi != NULL, ("%s: no igi for ifp %p", __func__, ifp));

		IGMPSTAT_INC(igps_rcv_ourreports);

		/*
		 * If we are in IGMPv3 host mode, do not allow the
		 * other host's IGMPv1 report to suppress our reports
		 * unless explicitly configured to do so.
		 */
		if (igi->igi_version == IGMP_VERSION_3) {
			if (V_igmp_legacysupp)
				igmp_v3_suppress_group_record(inm);
			goto out_locked;
		}

		inm->inm_timer = 0;

		switch (inm->inm_state) {
		case IGMP_NOT_MEMBER:
		case IGMP_SILENT_MEMBER:
		case IGMP_SLEEPING_MEMBER:
			break;
		case IGMP_REPORTING_MEMBER:
		case IGMP_IDLE_MEMBER:
		case IGMP_AWAKENING_MEMBER:
			CTR3(KTR_IGMPV3,
			    "report suppressed for 0x%08x on ifp %p(%s)",
			    ntohl(igmp->igmp_group.s_addr), ifp, ifp->if_xname);
		case IGMP_LAZY_MEMBER:
			inm->inm_state = IGMP_LAZY_MEMBER;
			break;
		case IGMP_G_QUERY_PENDING_MEMBER:
		case IGMP_SG_QUERY_PENDING_MEMBER:
		case IGMP_LEAVING_MEMBER:
			break;
		}
	}

out_locked:
	IN_MULTI_LIST_UNLOCK();

	return (0);
}

int
igmp_input(struct mbuf **mp, int *offp, int proto)
{
	int iphlen;
	struct ifnet *ifp;
	struct igmp *igmp;
	struct ip *ip;
	struct mbuf *m;
	int igmplen;
	int minlen;
	int queryver;

	CTR3(KTR_IGMPV3, "%s: called w/mbuf (%p,%d)", __func__, *mp, *offp);

	m = *mp;
	ifp = m->m_pkthdr.rcvif;
	*mp = NULL;

	IGMPSTAT_INC(igps_rcv_total);

	ip = mtod(m, struct ip *);
	iphlen = *offp;
	igmplen = ntohs(ip->ip_len) - iphlen;

	/*
	 * Validate lengths.
	 */
	if (igmplen < IGMP_MINLEN) {
		IGMPSTAT_INC(igps_rcv_tooshort);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	/*
	 * Always pullup to the minimum size for v1/v2 or v3
	 * to amortize calls to m_pullup().
	 */
	minlen = iphlen;
	if (igmplen >= IGMP_V3_QUERY_MINLEN)
		minlen += IGMP_V3_QUERY_MINLEN;
	else
		minlen += IGMP_MINLEN;
	if ((!M_WRITABLE(m) || m->m_len < minlen) &&
	    (m = m_pullup(m, minlen)) == NULL) {
		IGMPSTAT_INC(igps_rcv_tooshort);
		return (IPPROTO_DONE);
	}
	ip = mtod(m, struct ip *);

	/*
	 * Validate checksum.
	 */
	m->m_data += iphlen;
	m->m_len -= iphlen;
	igmp = mtod(m, struct igmp *);
	if (in_cksum(m, igmplen)) {
		IGMPSTAT_INC(igps_rcv_badsum);
		m_freem(m);
		return (IPPROTO_DONE);
	}
	m->m_data -= iphlen;
	m->m_len += iphlen;

	/*
	 * IGMP control traffic is link-scope, and must have a TTL of 1.
	 * DVMRP traffic (e.g. mrinfo, mtrace) is an exception;
	 * probe packets may come from beyond the LAN.
	 */
	if (igmp->igmp_type != IGMP_DVMRP && ip->ip_ttl != 1) {
		IGMPSTAT_INC(igps_rcv_badttl);
		m_freem(m);
		return (IPPROTO_DONE);
	}

	switch (igmp->igmp_type) {
	case IGMP_HOST_MEMBERSHIP_QUERY:
		if (igmplen == IGMP_MINLEN) {
			if (igmp->igmp_code == 0)
				queryver = IGMP_VERSION_1;
			else
				queryver = IGMP_VERSION_2;
		} else if (igmplen >= IGMP_V3_QUERY_MINLEN) {
			queryver = IGMP_VERSION_3;
		} else {
			IGMPSTAT_INC(igps_rcv_tooshort);
			m_freem(m);
			return (IPPROTO_DONE);
		}

		switch (queryver) {
		case IGMP_VERSION_1:
			IGMPSTAT_INC(igps_rcv_v1v2_queries);
			if (!V_igmp_v1enable)
				break;
			if (igmp_input_v1_query(ifp, ip, igmp) != 0) {
				m_freem(m);
				return (IPPROTO_DONE);
			}
			break;

		case IGMP_VERSION_2:
			IGMPSTAT_INC(igps_rcv_v1v2_queries);
			if (!V_igmp_v2enable)
				break;
			if (igmp_input_v2_query(ifp, ip, igmp) != 0) {
				m_freem(m);
				return (IPPROTO_DONE);
			}
			break;

		case IGMP_VERSION_3: {
				struct igmpv3 *igmpv3;
				uint16_t igmpv3len;
				uint16_t nsrc;

				IGMPSTAT_INC(igps_rcv_v3_queries);
				igmpv3 = (struct igmpv3 *)igmp;
				/*
				 * Validate length based on source count.
				 */
				nsrc = ntohs(igmpv3->igmp_numsrc);
				if (nsrc * sizeof(in_addr_t) >
				    UINT16_MAX - iphlen - IGMP_V3_QUERY_MINLEN) {
					IGMPSTAT_INC(igps_rcv_tooshort);
					return (IPPROTO_DONE);
				}
				/*
				 * m_pullup() may modify m, so pullup in
				 * this scope.
				 */
				igmpv3len = iphlen + IGMP_V3_QUERY_MINLEN +
				   sizeof(struct in_addr) * nsrc;
				if ((!M_WRITABLE(m) ||
				     m->m_len < igmpv3len) &&
				    (m = m_pullup(m, igmpv3len)) == NULL) {
					IGMPSTAT_INC(igps_rcv_tooshort);
					return (IPPROTO_DONE);
				}
				igmpv3 = (struct igmpv3 *)(mtod(m, uint8_t *)
				    + iphlen);
				if (igmp_input_v3_query(ifp, ip, igmpv3) != 0) {
					m_freem(m);
					return (IPPROTO_DONE);
				}
			}
			break;
		}
		break;

	case IGMP_v1_HOST_MEMBERSHIP_REPORT:
		if (!V_igmp_v1enable)
			break;
		if (igmp_input_v1_report(ifp, ip, igmp) != 0) {
			m_freem(m);
			return (IPPROTO_DONE);
		}
		break;

	case IGMP_v2_HOST_MEMBERSHIP_REPORT:
		if (!V_igmp_v2enable)
			break;
		if (!ip_checkrouteralert(m))
			IGMPSTAT_INC(igps_rcv_nora);
		if (igmp_input_v2_report(ifp, ip, igmp) != 0) {
			m_freem(m);
			return (IPPROTO_DONE);
		}
		break;

	case IGMP_v3_HOST_MEMBERSHIP_REPORT:
		/*
		 * Hosts do not need to process IGMPv3 membership reports,
		 * as report suppression is no longer required.
		 */
		if (!ip_checkrouteralert(m))
			IGMPSTAT_INC(igps_rcv_nora);
		break;

	default:
		break;
	}

	/*
	 * Pass all valid IGMP packets up to any process(es) listening on a
	 * raw IGMP socket.
	 */
	*mp = m;
	return (rip_input(mp, offp, proto));
}


/*
 * Fast timeout handler (global).
 * VIMAGE: Timeout handlers are expected to service all vimages.
 */
void
igmp_fasttimo(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		igmp_fasttimo_vnet();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * Fast timeout handler (per-vnet).
 * Sends are shuffled off to a netisr to deal with Giant.
 *
 * VIMAGE: Assume caller has set up our curvnet.
 */
static void
igmp_fasttimo_vnet(void)
{
	struct mbufq		 scq;	/* State-change packets */
	struct mbufq		 qrq;	/* Query response packets */
	struct ifnet		*ifp;
	struct igmp_ifsoftc	*igi;
	struct ifmultiaddr	*ifma, *next;
	struct in_multi		*inm;
	struct in_multi_head inm_free_tmp;
	int			 loop, uri_fasthz;

	loop = 0;
	uri_fasthz = 0;

	/*
	 * Quick check to see if any work needs to be done, in order to
	 * minimize the overhead of fasttimo processing.
	 * SMPng: XXX Unlocked reads.
	 */
	if (!V_current_state_timers_running &&
	    !V_interface_timers_running &&
	    !V_state_change_timers_running)
		return;

	SLIST_INIT(&inm_free_tmp);
	IN_MULTI_LIST_LOCK();
	IGMP_LOCK();

	/*
	 * IGMPv3 General Query response timer processing.
	 */
	if (V_interface_timers_running) {
		CTR1(KTR_IGMPV3, "%s: interface timers running", __func__);

		V_interface_timers_running = 0;
		LIST_FOREACH(igi, &V_igi_head, igi_link) {
			if (igi->igi_v3_timer == 0) {
				/* Do nothing. */
			} else if (--igi->igi_v3_timer == 0) {
				igmp_v3_dispatch_general_query(igi);
			} else {
				V_interface_timers_running = 1;
			}
		}
	}

	if (!V_current_state_timers_running &&
	    !V_state_change_timers_running)
		goto out_locked;

	V_current_state_timers_running = 0;
	V_state_change_timers_running = 0;

	CTR1(KTR_IGMPV3, "%s: state change timers running", __func__);

	/*
	 * IGMPv1/v2/v3 host report and state-change timer processing.
	 * Note: Processing a v3 group timer may remove a node.
	 */
	LIST_FOREACH(igi, &V_igi_head, igi_link) {
		ifp = igi->igi_ifp;

		if (igi->igi_version == IGMP_VERSION_3) {
			loop = (igi->igi_flags & IGIF_LOOPBACK) ? 1 : 0;
			uri_fasthz = IGMP_RANDOM_DELAY(igi->igi_uri *
			    PR_FASTHZ);
			mbufq_init(&qrq, IGMP_MAX_G_GS_PACKETS);
			mbufq_init(&scq, IGMP_MAX_STATE_CHANGE_PACKETS);
		}

		IF_ADDR_WLOCK(ifp);
	restart:
		CK_STAILQ_FOREACH_SAFE(ifma, &ifp->if_multiaddrs, ifma_link, next) {
			if (ifma->ifma_addr->sa_family != AF_INET ||
			    ifma->ifma_protospec == NULL)
				continue;
			inm = (struct in_multi *)ifma->ifma_protospec;
			switch (igi->igi_version) {
			case IGMP_VERSION_1:
			case IGMP_VERSION_2:
				igmp_v1v2_process_group_timer(inm,
				    igi->igi_version);
				break;
			case IGMP_VERSION_3:
				igmp_v3_process_group_timers(&inm_free_tmp, &qrq,
				    &scq, inm, uri_fasthz);
				break;
			}
			if (__predict_false(ifma_restart)) {
				ifma_restart = false;
				goto restart;
			}
		}
		IF_ADDR_WUNLOCK(ifp);

		if (igi->igi_version == IGMP_VERSION_3) {
			igmp_dispatch_queue(&qrq, 0, loop);
			igmp_dispatch_queue(&scq, 0, loop);

			/*
			 * Free the in_multi reference(s) for this
			 * IGMP lifecycle.
			 */
			inm_release_list_deferred(&inm_free_tmp);
		}
	}

out_locked:
	IGMP_UNLOCK();
	IN_MULTI_LIST_UNLOCK();
}

/*
 * Update host report group timer for IGMPv1/v2.
 * Will update the global pending timer flags.
 */
static void
igmp_v1v2_process_group_timer(struct in_multi *inm, const int version)
{
	int report_timer_expired;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	if (inm->inm_timer == 0) {
		report_timer_expired = 0;
	} else if (--inm->inm_timer == 0) {
		report_timer_expired = 1;
	} else {
		V_current_state_timers_running = 1;
		return;
	}

	switch (inm->inm_state) {
	case IGMP_NOT_MEMBER:
	case IGMP_SILENT_MEMBER:
	case IGMP_IDLE_MEMBER:
	case IGMP_LAZY_MEMBER:
	case IGMP_SLEEPING_MEMBER:
	case IGMP_AWAKENING_MEMBER:
		break;
	case IGMP_REPORTING_MEMBER:
		if (report_timer_expired) {
			inm->inm_state = IGMP_IDLE_MEMBER;
			(void)igmp_v1v2_queue_report(inm,
			    (version == IGMP_VERSION_2) ?
			     IGMP_v2_HOST_MEMBERSHIP_REPORT :
			     IGMP_v1_HOST_MEMBERSHIP_REPORT);
		}
		break;
	case IGMP_G_QUERY_PENDING_MEMBER:
	case IGMP_SG_QUERY_PENDING_MEMBER:
	case IGMP_LEAVING_MEMBER:
		break;
	}
}

/*
 * Update a group's timers for IGMPv3.
 * Will update the global pending timer flags.
 * Note: Unlocked read from igi.
 */
static void
igmp_v3_process_group_timers(struct in_multi_head *inmh,
    struct mbufq *qrq, struct mbufq *scq,
    struct in_multi *inm, const int uri_fasthz)
{
	int query_response_timer_expired;
	int state_change_retransmit_timer_expired;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	query_response_timer_expired = 0;
	state_change_retransmit_timer_expired = 0;

	/*
	 * During a transition from v1/v2 compatibility mode back to v3,
	 * a group record in REPORTING state may still have its group
	 * timer active. This is a no-op in this function; it is easier
	 * to deal with it here than to complicate the slow-timeout path.
	 */
	if (inm->inm_timer == 0) {
		query_response_timer_expired = 0;
	} else if (--inm->inm_timer == 0) {
		query_response_timer_expired = 1;
	} else {
		V_current_state_timers_running = 1;
	}

	if (inm->inm_sctimer == 0) {
		state_change_retransmit_timer_expired = 0;
	} else if (--inm->inm_sctimer == 0) {
		state_change_retransmit_timer_expired = 1;
	} else {
		V_state_change_timers_running = 1;
	}

	/* We are in fasttimo, so be quick about it. */
	if (!state_change_retransmit_timer_expired &&
	    !query_response_timer_expired)
		return;

	switch (inm->inm_state) {
	case IGMP_NOT_MEMBER:
	case IGMP_SILENT_MEMBER:
	case IGMP_SLEEPING_MEMBER:
	case IGMP_LAZY_MEMBER:
	case IGMP_AWAKENING_MEMBER:
	case IGMP_IDLE_MEMBER:
		break;
	case IGMP_G_QUERY_PENDING_MEMBER:
	case IGMP_SG_QUERY_PENDING_MEMBER:
		/*
		 * Respond to a previously pending Group-Specific
		 * or Group-and-Source-Specific query by enqueueing
		 * the appropriate Current-State report for
		 * immediate transmission.
		 */
		if (query_response_timer_expired) {
			int retval __unused;

			retval = igmp_v3_enqueue_group_record(qrq, inm, 0, 1,
			    (inm->inm_state == IGMP_SG_QUERY_PENDING_MEMBER));
			CTR2(KTR_IGMPV3, "%s: enqueue record = %d",
			    __func__, retval);
			inm->inm_state = IGMP_REPORTING_MEMBER;
			/* XXX Clear recorded sources for next time. */
			inm_clear_recorded(inm);
		}
		/* FALLTHROUGH */
	case IGMP_REPORTING_MEMBER:
	case IGMP_LEAVING_MEMBER:
		if (state_change_retransmit_timer_expired) {
			/*
			 * State-change retransmission timer fired.
			 * If there are any further pending retransmissions,
			 * set the global pending state-change flag, and
			 * reset the timer.
			 */
			if (--inm->inm_scrv > 0) {
				inm->inm_sctimer = uri_fasthz;
				V_state_change_timers_running = 1;
			}
			/*
			 * Retransmit the previously computed state-change
			 * report. If there are no further pending
			 * retransmissions, the mbuf queue will be consumed.
			 * Update T0 state to T1 as we have now sent
			 * a state-change.
			 */
			(void)igmp_v3_merge_state_changes(inm, scq);

			inm_commit(inm);
			CTR3(KTR_IGMPV3, "%s: T1 -> T0 for 0x%08x/%s", __func__,
			    ntohl(inm->inm_addr.s_addr),
			    inm->inm_ifp->if_xname);

			/*
			 * If we are leaving the group for good, make sure
			 * we release IGMP's reference to it.
			 * This release must be deferred using a SLIST,
			 * as we are called from a loop which traverses
			 * the in_ifmultiaddr TAILQ.
			 */
			if (inm->inm_state == IGMP_LEAVING_MEMBER &&
			    inm->inm_scrv == 0) {
				inm->inm_state = IGMP_NOT_MEMBER;
				inm_rele_locked(inmh, inm);
			}
		}
		break;
	}
}


/*
 * Suppress a group's pending response to a group or source/group query.
 *
 * Do NOT suppress state changes. This leads to IGMPv3 inconsistency.
 * Do NOT update ST1/ST0 as this operation merely suppresses
 * the currently pending group record.
 * Do NOT suppress the response to a general query. It is possible but
 * it would require adding another state or flag.
 */
static void
igmp_v3_suppress_group_record(struct in_multi *inm)
{

	IN_MULTI_LIST_LOCK_ASSERT();

	KASSERT(inm->inm_igi->igi_version == IGMP_VERSION_3,
		("%s: not IGMPv3 mode on link", __func__));

	if (inm->inm_state != IGMP_G_QUERY_PENDING_MEMBER ||
	    inm->inm_state != IGMP_SG_QUERY_PENDING_MEMBER)
		return;

	if (inm->inm_state == IGMP_SG_QUERY_PENDING_MEMBER)
		inm_clear_recorded(inm);

	inm->inm_timer = 0;
	inm->inm_state = IGMP_REPORTING_MEMBER;
}

/*
 * Switch to a different IGMP version on the given interface,
 * as per Section 7.2.1.
 */
static void
igmp_set_version(struct igmp_ifsoftc *igi, const int version)
{
	int old_version_timer;

	IGMP_LOCK_ASSERT();

	CTR4(KTR_IGMPV3, "%s: switching to v%d on ifp %p(%s)", __func__,
	    version, igi->igi_ifp, igi->igi_ifp->if_xname);

	if (version == IGMP_VERSION_1 || version == IGMP_VERSION_2) {
		/*
		 * Compute the "Older Version Querier Present" timer as per
		 * Section 8.12.
		 */
		old_version_timer = igi->igi_rv * igi->igi_qi + igi->igi_qri;
		old_version_timer *= PR_SLOWHZ;

		if (version == IGMP_VERSION_1) {
			igi->igi_v1_timer = old_version_timer;
			igi->igi_v2_timer = 0;
		} else if (version == IGMP_VERSION_2) {
			igi->igi_v1_timer = 0;
			igi->igi_v2_timer = old_version_timer;
		}
	}

	if (igi->igi_v1_timer == 0 && igi->igi_v2_timer > 0) {
		if (igi->igi_version != IGMP_VERSION_2) {
			igi->igi_version = IGMP_VERSION_2;
			igmp_v3_cancel_link_timers(igi);
		}
	} else if (igi->igi_v1_timer > 0) {
		if (igi->igi_version != IGMP_VERSION_1) {
			igi->igi_version = IGMP_VERSION_1;
			igmp_v3_cancel_link_timers(igi);
		}
	}
}

/*
 * Cancel pending IGMPv3 timers for the given link and all groups
 * joined on it; state-change, general-query, and group-query timers.
 *
 * Only ever called on a transition from v3 to Compatibility mode. Kill
 * the timers stone dead (this may be expensive for large N groups), they
 * will be restarted if Compatibility Mode deems that they must be due to
 * query processing.
 */
static void
igmp_v3_cancel_link_timers(struct igmp_ifsoftc *igi)
{
	struct ifmultiaddr	*ifma;
	struct ifnet		*ifp;
	struct in_multi		*inm;
	struct in_multi_head inm_free_tmp;
	struct epoch_tracker et;

	CTR3(KTR_IGMPV3, "%s: cancel v3 timers on ifp %p(%s)", __func__,
	    igi->igi_ifp, igi->igi_ifp->if_xname);

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();
	SLIST_INIT(&inm_free_tmp);

	/*
	 * Stop the v3 General Query Response on this link stone dead.
	 * If fasttimo is woken up due to V_interface_timers_running,
	 * the flag will be cleared if there are no pending link timers.
	 */
	igi->igi_v3_timer = 0;

	/*
	 * Now clear the current-state and state-change report timers
	 * for all memberships scoped to this link.
	 */
	ifp = igi->igi_ifp;
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;
		inm = (struct in_multi *)ifma->ifma_protospec;
		switch (inm->inm_state) {
		case IGMP_NOT_MEMBER:
		case IGMP_SILENT_MEMBER:
		case IGMP_IDLE_MEMBER:
		case IGMP_LAZY_MEMBER:
		case IGMP_SLEEPING_MEMBER:
		case IGMP_AWAKENING_MEMBER:
			/*
			 * These states are either not relevant in v3 mode,
			 * or are unreported. Do nothing.
			 */
			break;
		case IGMP_LEAVING_MEMBER:
			/*
			 * If we are leaving the group and switching to
			 * compatibility mode, we need to release the final
			 * reference held for issuing the INCLUDE {}, and
			 * transition to REPORTING to ensure the host leave
			 * message is sent upstream to the old querier --
			 * transition to NOT would lose the leave and race.
			 */
			inm_rele_locked(&inm_free_tmp, inm);
			/* FALLTHROUGH */
		case IGMP_G_QUERY_PENDING_MEMBER:
		case IGMP_SG_QUERY_PENDING_MEMBER:
			inm_clear_recorded(inm);
			/* FALLTHROUGH */
		case IGMP_REPORTING_MEMBER:
			inm->inm_state = IGMP_REPORTING_MEMBER;
			break;
		}
		/*
		 * Always clear state-change and group report timers.
		 * Free any pending IGMPv3 state-change records.
		 */
		inm->inm_sctimer = 0;
		inm->inm_timer = 0;
		mbufq_drain(&inm->inm_scq);
	}
	NET_EPOCH_EXIT(et);

	inm_release_list_deferred(&inm_free_tmp);
}

/*
 * Update the Older Version Querier Present timers for a link.
 * See Section 7.2.1 of RFC 3376.
 */
static void
igmp_v1v2_process_querier_timers(struct igmp_ifsoftc *igi)
{

	IGMP_LOCK_ASSERT();

	if (igi->igi_v1_timer == 0 && igi->igi_v2_timer == 0) {
		/*
		 * IGMPv1 and IGMPv2 Querier Present timers expired.
		 *
		 * Revert to IGMPv3.
		 */
		if (igi->igi_version != IGMP_VERSION_3) {
			CTR5(KTR_IGMPV3,
			    "%s: transition from v%d -> v%d on %p(%s)",
			    __func__, igi->igi_version, IGMP_VERSION_3,
			    igi->igi_ifp, igi->igi_ifp->if_xname);
			igi->igi_version = IGMP_VERSION_3;
		}
	} else if (igi->igi_v1_timer == 0 && igi->igi_v2_timer > 0) {
		/*
		 * IGMPv1 Querier Present timer expired,
		 * IGMPv2 Querier Present timer running.
		 * If IGMPv2 was disabled since last timeout,
		 * revert to IGMPv3.
		 * If IGMPv2 is enabled, revert to IGMPv2.
		 */
		if (!V_igmp_v2enable) {
			CTR5(KTR_IGMPV3,
			    "%s: transition from v%d -> v%d on %p(%s)",
			    __func__, igi->igi_version, IGMP_VERSION_3,
			    igi->igi_ifp, igi->igi_ifp->if_xname);
			igi->igi_v2_timer = 0;
			igi->igi_version = IGMP_VERSION_3;
		} else {
			--igi->igi_v2_timer;
			if (igi->igi_version != IGMP_VERSION_2) {
				CTR5(KTR_IGMPV3,
				    "%s: transition from v%d -> v%d on %p(%s)",
				    __func__, igi->igi_version, IGMP_VERSION_2,
				    igi->igi_ifp, igi->igi_ifp->if_xname);
				igi->igi_version = IGMP_VERSION_2;
				igmp_v3_cancel_link_timers(igi);
			}
		}
	} else if (igi->igi_v1_timer > 0) {
		/*
		 * IGMPv1 Querier Present timer running.
		 * Stop IGMPv2 timer if running.
		 *
		 * If IGMPv1 was disabled since last timeout,
		 * revert to IGMPv3.
		 * If IGMPv1 is enabled, reset IGMPv2 timer if running.
		 */
		if (!V_igmp_v1enable) {
			CTR5(KTR_IGMPV3,
			    "%s: transition from v%d -> v%d on %p(%s)",
			    __func__, igi->igi_version, IGMP_VERSION_3,
			    igi->igi_ifp, igi->igi_ifp->if_xname);
			igi->igi_v1_timer = 0;
			igi->igi_version = IGMP_VERSION_3;
		} else {
			--igi->igi_v1_timer;
		}
		if (igi->igi_v2_timer > 0) {
			CTR3(KTR_IGMPV3,
			    "%s: cancel v2 timer on %p(%s)",
			    __func__, igi->igi_ifp, igi->igi_ifp->if_xname);
			igi->igi_v2_timer = 0;
		}
	}
}

/*
 * Global slowtimo handler.
 * VIMAGE: Timeout handlers are expected to service all vimages.
 */
void
igmp_slowtimo(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		igmp_slowtimo_vnet();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * Per-vnet slowtimo handler.
 */
static void
igmp_slowtimo_vnet(void)
{
	struct igmp_ifsoftc *igi;

	IGMP_LOCK();

	LIST_FOREACH(igi, &V_igi_head, igi_link) {
		igmp_v1v2_process_querier_timers(igi);
	}

	IGMP_UNLOCK();
}

/*
 * Dispatch an IGMPv1/v2 host report or leave message.
 * These are always small enough to fit inside a single mbuf.
 */
static int
igmp_v1v2_queue_report(struct in_multi *inm, const int type)
{
	struct ifnet		*ifp;
	struct igmp		*igmp;
	struct ip		*ip;
	struct mbuf		*m;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	ifp = inm->inm_ifp;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOMEM);
	M_ALIGN(m, sizeof(struct ip) + sizeof(struct igmp));

	m->m_pkthdr.len = sizeof(struct ip) + sizeof(struct igmp);

	m->m_data += sizeof(struct ip);
	m->m_len = sizeof(struct igmp);

	igmp = mtod(m, struct igmp *);
	igmp->igmp_type = type;
	igmp->igmp_code = 0;
	igmp->igmp_group = inm->inm_addr;
	igmp->igmp_cksum = 0;
	igmp->igmp_cksum = in_cksum(m, sizeof(struct igmp));

	m->m_data -= sizeof(struct ip);
	m->m_len += sizeof(struct ip);

	ip = mtod(m, struct ip *);
	ip->ip_tos = 0;
	ip->ip_len = htons(sizeof(struct ip) + sizeof(struct igmp));
	ip->ip_off = 0;
	ip->ip_p = IPPROTO_IGMP;
	ip->ip_src.s_addr = INADDR_ANY;

	if (type == IGMP_HOST_LEAVE_MESSAGE)
		ip->ip_dst.s_addr = htonl(INADDR_ALLRTRS_GROUP);
	else
		ip->ip_dst = inm->inm_addr;

	igmp_save_context(m, ifp);

	m->m_flags |= M_IGMPV2;
	if (inm->inm_igi->igi_flags & IGIF_LOOPBACK)
		m->m_flags |= M_IGMP_LOOP;

	CTR2(KTR_IGMPV3, "%s: netisr_dispatch(NETISR_IGMP, %p)", __func__, m);
	netisr_dispatch(NETISR_IGMP, m);

	return (0);
}

/*
 * Process a state change from the upper layer for the given IPv4 group.
 *
 * Each socket holds a reference on the in_multi in its own ip_moptions.
 * The socket layer will have made the necessary updates to.the group
 * state, it is now up to IGMP to issue a state change report if there
 * has been any change between T0 (when the last state-change was issued)
 * and T1 (now).
 *
 * We use the IGMPv3 state machine at group level. The IGMP module
 * however makes the decision as to which IGMP protocol version to speak.
 * A state change *from* INCLUDE {} always means an initial join.
 * A state change *to* INCLUDE {} always means a final leave.
 *
 * FUTURE: If IGIF_V3LITE is enabled for this interface, then we can
 * save ourselves a bunch of work; any exclusive mode groups need not
 * compute source filter lists.
 *
 * VIMAGE: curvnet should have been set by caller, as this routine
 * is called from the socket option handlers.
 */
int
igmp_change_state(struct in_multi *inm)
{
	struct igmp_ifsoftc *igi;
	struct ifnet *ifp;
	int error;

	error = 0;
	IN_MULTI_LOCK_ASSERT();
	/*
	 * Try to detect if the upper layer just asked us to change state
	 * for an interface which has now gone away.
	 */
	KASSERT(inm->inm_ifma != NULL, ("%s: no ifma", __func__));
	ifp = inm->inm_ifma->ifma_ifp;
	/*
	 * Sanity check that netinet's notion of ifp is the
	 * same as net's.
	 */
	KASSERT(inm->inm_ifp == ifp, ("%s: bad ifp", __func__));

	IGMP_LOCK();

	igi = ((struct in_ifinfo *)ifp->if_afdata[AF_INET])->ii_igmp;
	KASSERT(igi != NULL, ("%s: no igmp_ifsoftc for ifp %p", __func__, ifp));

	/*
	 * If we detect a state transition to or from MCAST_UNDEFINED
	 * for this group, then we are starting or finishing an IGMP
	 * life cycle for this group.
	 */
	if (inm->inm_st[1].iss_fmode != inm->inm_st[0].iss_fmode) {
		CTR3(KTR_IGMPV3, "%s: inm transition %d -> %d", __func__,
		    inm->inm_st[0].iss_fmode, inm->inm_st[1].iss_fmode);
		if (inm->inm_st[0].iss_fmode == MCAST_UNDEFINED) {
			CTR1(KTR_IGMPV3, "%s: initial join", __func__);
			error = igmp_initial_join(inm, igi);
			goto out_locked;
		} else if (inm->inm_st[1].iss_fmode == MCAST_UNDEFINED) {
			CTR1(KTR_IGMPV3, "%s: final leave", __func__);
			igmp_final_leave(inm, igi);
			goto out_locked;
		}
	} else {
		CTR1(KTR_IGMPV3, "%s: filter set change", __func__);
	}

	error = igmp_handle_state_change(inm, igi);

out_locked:
	IGMP_UNLOCK();
	return (error);
}

/*
 * Perform the initial join for an IGMP group.
 *
 * When joining a group:
 *  If the group should have its IGMP traffic suppressed, do nothing.
 *  IGMPv1 starts sending IGMPv1 host membership reports.
 *  IGMPv2 starts sending IGMPv2 host membership reports.
 *  IGMPv3 will schedule an IGMPv3 state-change report containing the
 *  initial state of the membership.
 */
static int
igmp_initial_join(struct in_multi *inm, struct igmp_ifsoftc *igi)
{
	struct ifnet		*ifp;
	struct mbufq		*mq;
	int			 error, retval, syncstates;
 
	CTR4(KTR_IGMPV3, "%s: initial join 0x%08x on ifp %p(%s)", __func__,
	    ntohl(inm->inm_addr.s_addr), inm->inm_ifp, inm->inm_ifp->if_xname);

	error = 0;
	syncstates = 1;

	ifp = inm->inm_ifp;

	IN_MULTI_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	KASSERT(igi && igi->igi_ifp == ifp, ("%s: inconsistent ifp", __func__));

	/*
	 * Groups joined on loopback or marked as 'not reported',
	 * e.g. 224.0.0.1, enter the IGMP_SILENT_MEMBER state and
	 * are never reported in any IGMP protocol exchanges.
	 * All other groups enter the appropriate IGMP state machine
	 * for the version in use on this link.
	 * A link marked as IGIF_SILENT causes IGMP to be completely
	 * disabled for the link.
	 */
	if ((ifp->if_flags & IFF_LOOPBACK) ||
	    (igi->igi_flags & IGIF_SILENT) ||
	    !igmp_isgroupreported(inm->inm_addr)) {
		CTR1(KTR_IGMPV3,
"%s: not kicking state machine for silent group", __func__);
		inm->inm_state = IGMP_SILENT_MEMBER;
		inm->inm_timer = 0;
	} else {
		/*
		 * Deal with overlapping in_multi lifecycle.
		 * If this group was LEAVING, then make sure
		 * we drop the reference we picked up to keep the
		 * group around for the final INCLUDE {} enqueue.
		 */
		if (igi->igi_version == IGMP_VERSION_3 &&
		    inm->inm_state == IGMP_LEAVING_MEMBER) {
			MPASS(inm->inm_refcount > 1);
			inm_rele_locked(NULL, inm);
		}
		inm->inm_state = IGMP_REPORTING_MEMBER;

		switch (igi->igi_version) {
		case IGMP_VERSION_1:
		case IGMP_VERSION_2:
			inm->inm_state = IGMP_IDLE_MEMBER;
			error = igmp_v1v2_queue_report(inm,
			    (igi->igi_version == IGMP_VERSION_2) ?
			     IGMP_v2_HOST_MEMBERSHIP_REPORT :
			     IGMP_v1_HOST_MEMBERSHIP_REPORT);
			if (error == 0) {
				inm->inm_timer = IGMP_RANDOM_DELAY(
				    IGMP_V1V2_MAX_RI * PR_FASTHZ);
				V_current_state_timers_running = 1;
			}
			break;

		case IGMP_VERSION_3:
			/*
			 * Defer update of T0 to T1, until the first copy
			 * of the state change has been transmitted.
			 */
			syncstates = 0;

			/*
			 * Immediately enqueue a State-Change Report for
			 * this interface, freeing any previous reports.
			 * Don't kick the timers if there is nothing to do,
			 * or if an error occurred.
			 */
			mq = &inm->inm_scq;
			mbufq_drain(mq);
			retval = igmp_v3_enqueue_group_record(mq, inm, 1,
			    0, 0);
			CTR2(KTR_IGMPV3, "%s: enqueue record = %d",
			    __func__, retval);
			if (retval <= 0) {
				error = retval * -1;
				break;
			}

			/*
			 * Schedule transmission of pending state-change
			 * report up to RV times for this link. The timer
			 * will fire at the next igmp_fasttimo (~200ms),
			 * giving us an opportunity to merge the reports.
			 */
			if (igi->igi_flags & IGIF_LOOPBACK) {
				inm->inm_scrv = 1;
			} else {
				KASSERT(igi->igi_rv > 1,
				   ("%s: invalid robustness %d", __func__,
				    igi->igi_rv));
				inm->inm_scrv = igi->igi_rv;
			}
			inm->inm_sctimer = 1;
			V_state_change_timers_running = 1;

			error = 0;
			break;
		}
	}

	/*
	 * Only update the T0 state if state change is atomic,
	 * i.e. we don't need to wait for a timer to fire before we
	 * can consider the state change to have been communicated.
	 */
	if (syncstates) {
		inm_commit(inm);
		CTR3(KTR_IGMPV3, "%s: T1 -> T0 for 0x%08x/%s", __func__,
		    ntohl(inm->inm_addr.s_addr), inm->inm_ifp->if_xname);
	}

	return (error);
}

/*
 * Issue an intermediate state change during the IGMP life-cycle.
 */
static int
igmp_handle_state_change(struct in_multi *inm, struct igmp_ifsoftc *igi)
{
	struct ifnet		*ifp;
	int			 retval;

	CTR4(KTR_IGMPV3, "%s: state change for 0x%08x on ifp %p(%s)", __func__,
	    ntohl(inm->inm_addr.s_addr), inm->inm_ifp, inm->inm_ifp->if_xname);

	ifp = inm->inm_ifp;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	KASSERT(igi && igi->igi_ifp == ifp, ("%s: inconsistent ifp", __func__));

	if ((ifp->if_flags & IFF_LOOPBACK) ||
	    (igi->igi_flags & IGIF_SILENT) ||
	    !igmp_isgroupreported(inm->inm_addr) ||
	    (igi->igi_version != IGMP_VERSION_3)) {
		if (!igmp_isgroupreported(inm->inm_addr)) {
			CTR1(KTR_IGMPV3,
"%s: not kicking state machine for silent group", __func__);
		}
		CTR1(KTR_IGMPV3, "%s: nothing to do", __func__);
		inm_commit(inm);
		CTR3(KTR_IGMPV3, "%s: T1 -> T0 for 0x%08x/%s", __func__,
		    ntohl(inm->inm_addr.s_addr), inm->inm_ifp->if_xname);
		return (0);
	}

	mbufq_drain(&inm->inm_scq);

	retval = igmp_v3_enqueue_group_record(&inm->inm_scq, inm, 1, 0, 0);
	CTR2(KTR_IGMPV3, "%s: enqueue record = %d", __func__, retval);
	if (retval <= 0)
		return (-retval);

	/*
	 * If record(s) were enqueued, start the state-change
	 * report timer for this group.
	 */
	inm->inm_scrv = ((igi->igi_flags & IGIF_LOOPBACK) ? 1 : igi->igi_rv);
	inm->inm_sctimer = 1;
	V_state_change_timers_running = 1;

	return (0);
}

/*
 * Perform the final leave for an IGMP group.
 *
 * When leaving a group:
 *  IGMPv1 does nothing.
 *  IGMPv2 sends a host leave message, if and only if we are the reporter.
 *  IGMPv3 enqueues a state-change report containing a transition
 *  to INCLUDE {} for immediate transmission.
 */
static void
igmp_final_leave(struct in_multi *inm, struct igmp_ifsoftc *igi)
{
	int syncstates;

	syncstates = 1;

	CTR4(KTR_IGMPV3, "%s: final leave 0x%08x on ifp %p(%s)",
	    __func__, ntohl(inm->inm_addr.s_addr), inm->inm_ifp,
	    inm->inm_ifp->if_xname);

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	switch (inm->inm_state) {
	case IGMP_NOT_MEMBER:
	case IGMP_SILENT_MEMBER:
	case IGMP_LEAVING_MEMBER:
		/* Already leaving or left; do nothing. */
		CTR1(KTR_IGMPV3,
"%s: not kicking state machine for silent group", __func__);
		break;
	case IGMP_REPORTING_MEMBER:
	case IGMP_IDLE_MEMBER:
	case IGMP_G_QUERY_PENDING_MEMBER:
	case IGMP_SG_QUERY_PENDING_MEMBER:
		if (igi->igi_version == IGMP_VERSION_2) {
#ifdef INVARIANTS
			if (inm->inm_state == IGMP_G_QUERY_PENDING_MEMBER ||
			    inm->inm_state == IGMP_SG_QUERY_PENDING_MEMBER)
			panic("%s: IGMPv3 state reached, not IGMPv3 mode",
			     __func__);
#endif
			igmp_v1v2_queue_report(inm, IGMP_HOST_LEAVE_MESSAGE);
			inm->inm_state = IGMP_NOT_MEMBER;
		} else if (igi->igi_version == IGMP_VERSION_3) {
			/*
			 * Stop group timer and all pending reports.
			 * Immediately enqueue a state-change report
			 * TO_IN {} to be sent on the next fast timeout,
			 * giving us an opportunity to merge reports.
			 */
			mbufq_drain(&inm->inm_scq);
			inm->inm_timer = 0;
			if (igi->igi_flags & IGIF_LOOPBACK) {
				inm->inm_scrv = 1;
			} else {
				inm->inm_scrv = igi->igi_rv;
			}
			CTR4(KTR_IGMPV3, "%s: Leaving 0x%08x/%s with %d "
			    "pending retransmissions.", __func__,
			    ntohl(inm->inm_addr.s_addr),
			    inm->inm_ifp->if_xname, inm->inm_scrv);
			if (inm->inm_scrv == 0) {
				inm->inm_state = IGMP_NOT_MEMBER;
				inm->inm_sctimer = 0;
			} else {
				int retval __unused;

				inm_acquire_locked(inm);

				retval = igmp_v3_enqueue_group_record(
				    &inm->inm_scq, inm, 1, 0, 0);
				KASSERT(retval != 0,
				    ("%s: enqueue record = %d", __func__,
				     retval));

				inm->inm_state = IGMP_LEAVING_MEMBER;
				inm->inm_sctimer = 1;
				V_state_change_timers_running = 1;
				syncstates = 0;
			}
			break;
		}
		break;
	case IGMP_LAZY_MEMBER:
	case IGMP_SLEEPING_MEMBER:
	case IGMP_AWAKENING_MEMBER:
		/* Our reports are suppressed; do nothing. */
		break;
	}

	if (syncstates) {
		inm_commit(inm);
		CTR3(KTR_IGMPV3, "%s: T1 -> T0 for 0x%08x/%s", __func__,
		    ntohl(inm->inm_addr.s_addr), inm->inm_ifp->if_xname);
		inm->inm_st[1].iss_fmode = MCAST_UNDEFINED;
		CTR3(KTR_IGMPV3, "%s: T1 now MCAST_UNDEFINED for 0x%08x/%s",
		    __func__, ntohl(inm->inm_addr.s_addr),
		    inm->inm_ifp->if_xname);
	}
}

/*
 * Enqueue an IGMPv3 group record to the given output queue.
 *
 * XXX This function could do with having the allocation code
 * split out, and the multiple-tree-walks coalesced into a single
 * routine as has been done in igmp_v3_enqueue_filter_change().
 *
 * If is_state_change is zero, a current-state record is appended.
 * If is_state_change is non-zero, a state-change report is appended.
 *
 * If is_group_query is non-zero, an mbuf packet chain is allocated.
 * If is_group_query is zero, and if there is a packet with free space
 * at the tail of the queue, it will be appended to providing there
 * is enough free space.
 * Otherwise a new mbuf packet chain is allocated.
 *
 * If is_source_query is non-zero, each source is checked to see if
 * it was recorded for a Group-Source query, and will be omitted if
 * it is not both in-mode and recorded.
 *
 * The function will attempt to allocate leading space in the packet
 * for the IP/IGMP header to be prepended without fragmenting the chain.
 *
 * If successful the size of all data appended to the queue is returned,
 * otherwise an error code less than zero is returned, or zero if
 * no record(s) were appended.
 */
static int
igmp_v3_enqueue_group_record(struct mbufq *mq, struct in_multi *inm,
    const int is_state_change, const int is_group_query,
    const int is_source_query)
{
	struct igmp_grouprec	 ig;
	struct igmp_grouprec	*pig;
	struct ifnet		*ifp;
	struct ip_msource	*ims, *nims;
	struct mbuf		*m0, *m, *md;
	int			 is_filter_list_change;
	int			 minrec0len, m0srcs, msrcs, nbytes, off;
	int			 record_has_sources;
	int			 now;
	int			 type;
	in_addr_t		 naddr;
	uint8_t			 mode;

	IN_MULTI_LIST_LOCK_ASSERT();

	ifp = inm->inm_ifp;
	is_filter_list_change = 0;
	m = NULL;
	m0 = NULL;
	m0srcs = 0;
	msrcs = 0;
	nbytes = 0;
	nims = NULL;
	record_has_sources = 1;
	pig = NULL;
	type = IGMP_DO_NOTHING;
	mode = inm->inm_st[1].iss_fmode;

	/*
	 * If we did not transition out of ASM mode during t0->t1,
	 * and there are no source nodes to process, we can skip
	 * the generation of source records.
	 */
	if (inm->inm_st[0].iss_asm > 0 && inm->inm_st[1].iss_asm > 0 &&
	    inm->inm_nsrc == 0)
		record_has_sources = 0;

	if (is_state_change) {
		/*
		 * Queue a state change record.
		 * If the mode did not change, and there are non-ASM
		 * listeners or source filters present,
		 * we potentially need to issue two records for the group.
		 * If we are transitioning to MCAST_UNDEFINED, we need
		 * not send any sources.
		 * If there are ASM listeners, and there was no filter
		 * mode transition of any kind, do nothing.
		 */
		if (mode != inm->inm_st[0].iss_fmode) {
			if (mode == MCAST_EXCLUDE) {
				CTR1(KTR_IGMPV3, "%s: change to EXCLUDE",
				    __func__);
				type = IGMP_CHANGE_TO_EXCLUDE_MODE;
			} else {
				CTR1(KTR_IGMPV3, "%s: change to INCLUDE",
				    __func__);
				type = IGMP_CHANGE_TO_INCLUDE_MODE;
				if (mode == MCAST_UNDEFINED)
					record_has_sources = 0;
			}
		} else {
			if (record_has_sources) {
				is_filter_list_change = 1;
			} else {
				type = IGMP_DO_NOTHING;
			}
		}
	} else {
		/*
		 * Queue a current state record.
		 */
		if (mode == MCAST_EXCLUDE) {
			type = IGMP_MODE_IS_EXCLUDE;
		} else if (mode == MCAST_INCLUDE) {
			type = IGMP_MODE_IS_INCLUDE;
			KASSERT(inm->inm_st[1].iss_asm == 0,
			    ("%s: inm %p is INCLUDE but ASM count is %d",
			     __func__, inm, inm->inm_st[1].iss_asm));
		}
	}

	/*
	 * Generate the filter list changes using a separate function.
	 */
	if (is_filter_list_change)
		return (igmp_v3_enqueue_filter_change(mq, inm));

	if (type == IGMP_DO_NOTHING) {
		CTR3(KTR_IGMPV3, "%s: nothing to do for 0x%08x/%s", __func__,
		    ntohl(inm->inm_addr.s_addr), inm->inm_ifp->if_xname);
		return (0);
	}

	/*
	 * If any sources are present, we must be able to fit at least
	 * one in the trailing space of the tail packet's mbuf,
	 * ideally more.
	 */
	minrec0len = sizeof(struct igmp_grouprec);
	if (record_has_sources)
		minrec0len += sizeof(in_addr_t);

	CTR4(KTR_IGMPV3, "%s: queueing %s for 0x%08x/%s", __func__,
	    igmp_rec_type_to_str(type), ntohl(inm->inm_addr.s_addr),
	    inm->inm_ifp->if_xname);

	/*
	 * Check if we have a packet in the tail of the queue for this
	 * group into which the first group record for this group will fit.
	 * Otherwise allocate a new packet.
	 * Always allocate leading space for IP+RA_OPT+IGMP+REPORT.
	 * Note: Group records for G/GSR query responses MUST be sent
	 * in their own packet.
	 */
	m0 = mbufq_last(mq);
	if (!is_group_query &&
	    m0 != NULL &&
	    (m0->m_pkthdr.PH_vt.vt_nrecs + 1 <= IGMP_V3_REPORT_MAXRECS) &&
	    (m0->m_pkthdr.len + minrec0len) <
	     (ifp->if_mtu - IGMP_LEADINGSPACE)) {
		m0srcs = (ifp->if_mtu - m0->m_pkthdr.len -
			    sizeof(struct igmp_grouprec)) / sizeof(in_addr_t);
		m = m0;
		CTR1(KTR_IGMPV3, "%s: use existing packet", __func__);
	} else {
		if (mbufq_full(mq)) {
			CTR1(KTR_IGMPV3, "%s: outbound queue full", __func__);
			return (-ENOMEM);
		}
		m = NULL;
		m0srcs = (ifp->if_mtu - IGMP_LEADINGSPACE -
		    sizeof(struct igmp_grouprec)) / sizeof(in_addr_t);
		if (!is_state_change && !is_group_query) {
			m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			if (m)
				m->m_data += IGMP_LEADINGSPACE;
		}
		if (m == NULL) {
			m = m_gethdr(M_NOWAIT, MT_DATA);
			if (m)
				M_ALIGN(m, IGMP_LEADINGSPACE);
		}
		if (m == NULL)
			return (-ENOMEM);

		igmp_save_context(m, ifp);

		CTR1(KTR_IGMPV3, "%s: allocated first packet", __func__);
	}

	/*
	 * Append group record.
	 * If we have sources, we don't know how many yet.
	 */
	ig.ig_type = type;
	ig.ig_datalen = 0;
	ig.ig_numsrc = 0;
	ig.ig_group = inm->inm_addr;
	if (!m_append(m, sizeof(struct igmp_grouprec), (void *)&ig)) {
		if (m != m0)
			m_freem(m);
		CTR1(KTR_IGMPV3, "%s: m_append() failed.", __func__);
		return (-ENOMEM);
	}
	nbytes += sizeof(struct igmp_grouprec);

	/*
	 * Append as many sources as will fit in the first packet.
	 * If we are appending to a new packet, the chain allocation
	 * may potentially use clusters; use m_getptr() in this case.
	 * If we are appending to an existing packet, we need to obtain
	 * a pointer to the group record after m_append(), in case a new
	 * mbuf was allocated.
	 * Only append sources which are in-mode at t1. If we are
	 * transitioning to MCAST_UNDEFINED state on the group, do not
	 * include source entries.
	 * Only report recorded sources in our filter set when responding
	 * to a group-source query.
	 */
	if (record_has_sources) {
		if (m == m0) {
			md = m_last(m);
			pig = (struct igmp_grouprec *)(mtod(md, uint8_t *) +
			    md->m_len - nbytes);
		} else {
			md = m_getptr(m, 0, &off);
			pig = (struct igmp_grouprec *)(mtod(md, uint8_t *) +
			    off);
		}
		msrcs = 0;
		RB_FOREACH_SAFE(ims, ip_msource_tree, &inm->inm_srcs, nims) {
			CTR2(KTR_IGMPV3, "%s: visit node 0x%08x", __func__,
			    ims->ims_haddr);
			now = ims_get_mode(inm, ims, 1);
			CTR2(KTR_IGMPV3, "%s: node is %d", __func__, now);
			if ((now != mode) ||
			    (now == mode && mode == MCAST_UNDEFINED)) {
				CTR1(KTR_IGMPV3, "%s: skip node", __func__);
				continue;
			}
			if (is_source_query && ims->ims_stp == 0) {
				CTR1(KTR_IGMPV3, "%s: skip unrecorded node",
				    __func__);
				continue;
			}
			CTR1(KTR_IGMPV3, "%s: append node", __func__);
			naddr = htonl(ims->ims_haddr);
			if (!m_append(m, sizeof(in_addr_t), (void *)&naddr)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_IGMPV3, "%s: m_append() failed.",
				    __func__);
				return (-ENOMEM);
			}
			nbytes += sizeof(in_addr_t);
			++msrcs;
			if (msrcs == m0srcs)
				break;
		}
		CTR2(KTR_IGMPV3, "%s: msrcs is %d this packet", __func__,
		    msrcs);
		pig->ig_numsrc = htons(msrcs);
		nbytes += (msrcs * sizeof(in_addr_t));
	}

	if (is_source_query && msrcs == 0) {
		CTR1(KTR_IGMPV3, "%s: no recorded sources to report", __func__);
		if (m != m0)
			m_freem(m);
		return (0);
	}

	/*
	 * We are good to go with first packet.
	 */
	if (m != m0) {
		CTR1(KTR_IGMPV3, "%s: enqueueing first packet", __func__);
		m->m_pkthdr.PH_vt.vt_nrecs = 1;
		mbufq_enqueue(mq, m);
	} else
		m->m_pkthdr.PH_vt.vt_nrecs++;

	/*
	 * No further work needed if no source list in packet(s).
	 */
	if (!record_has_sources)
		return (nbytes);

	/*
	 * Whilst sources remain to be announced, we need to allocate
	 * a new packet and fill out as many sources as will fit.
	 * Always try for a cluster first.
	 */
	while (nims != NULL) {
		if (mbufq_full(mq)) {
			CTR1(KTR_IGMPV3, "%s: outbound queue full", __func__);
			return (-ENOMEM);
		}
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m)
			m->m_data += IGMP_LEADINGSPACE;
		if (m == NULL) {
			m = m_gethdr(M_NOWAIT, MT_DATA);
			if (m)
				M_ALIGN(m, IGMP_LEADINGSPACE);
		}
		if (m == NULL)
			return (-ENOMEM);
		igmp_save_context(m, ifp);
		md = m_getptr(m, 0, &off);
		pig = (struct igmp_grouprec *)(mtod(md, uint8_t *) + off);
		CTR1(KTR_IGMPV3, "%s: allocated next packet", __func__);

		if (!m_append(m, sizeof(struct igmp_grouprec), (void *)&ig)) {
			if (m != m0)
				m_freem(m);
			CTR1(KTR_IGMPV3, "%s: m_append() failed.", __func__);
			return (-ENOMEM);
		}
		m->m_pkthdr.PH_vt.vt_nrecs = 1;
		nbytes += sizeof(struct igmp_grouprec);

		m0srcs = (ifp->if_mtu - IGMP_LEADINGSPACE -
		    sizeof(struct igmp_grouprec)) / sizeof(in_addr_t);

		msrcs = 0;
		RB_FOREACH_FROM(ims, ip_msource_tree, nims) {
			CTR2(KTR_IGMPV3, "%s: visit node 0x%08x", __func__,
			    ims->ims_haddr);
			now = ims_get_mode(inm, ims, 1);
			if ((now != mode) ||
			    (now == mode && mode == MCAST_UNDEFINED)) {
				CTR1(KTR_IGMPV3, "%s: skip node", __func__);
				continue;
			}
			if (is_source_query && ims->ims_stp == 0) {
				CTR1(KTR_IGMPV3, "%s: skip unrecorded node",
				    __func__);
				continue;
			}
			CTR1(KTR_IGMPV3, "%s: append node", __func__);
			naddr = htonl(ims->ims_haddr);
			if (!m_append(m, sizeof(in_addr_t), (void *)&naddr)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_IGMPV3, "%s: m_append() failed.",
				    __func__);
				return (-ENOMEM);
			}
			++msrcs;
			if (msrcs == m0srcs)
				break;
		}
		pig->ig_numsrc = htons(msrcs);
		nbytes += (msrcs * sizeof(in_addr_t));

		CTR1(KTR_IGMPV3, "%s: enqueueing next packet", __func__);
		mbufq_enqueue(mq, m);
	}

	return (nbytes);
}

/*
 * Type used to mark record pass completion.
 * We exploit the fact we can cast to this easily from the
 * current filter modes on each ip_msource node.
 */
typedef enum {
	REC_NONE = 0x00,	/* MCAST_UNDEFINED */
	REC_ALLOW = 0x01,	/* MCAST_INCLUDE */
	REC_BLOCK = 0x02,	/* MCAST_EXCLUDE */
	REC_FULL = REC_ALLOW | REC_BLOCK
} rectype_t;

/*
 * Enqueue an IGMPv3 filter list change to the given output queue.
 *
 * Source list filter state is held in an RB-tree. When the filter list
 * for a group is changed without changing its mode, we need to compute
 * the deltas between T0 and T1 for each source in the filter set,
 * and enqueue the appropriate ALLOW_NEW/BLOCK_OLD records.
 *
 * As we may potentially queue two record types, and the entire R-B tree
 * needs to be walked at once, we break this out into its own function
 * so we can generate a tightly packed queue of packets.
 *
 * XXX This could be written to only use one tree walk, although that makes
 * serializing into the mbuf chains a bit harder. For now we do two walks
 * which makes things easier on us, and it may or may not be harder on
 * the L2 cache.
 *
 * If successful the size of all data appended to the queue is returned,
 * otherwise an error code less than zero is returned, or zero if
 * no record(s) were appended.
 */
static int
igmp_v3_enqueue_filter_change(struct mbufq *mq, struct in_multi *inm)
{
	static const int MINRECLEN =
	    sizeof(struct igmp_grouprec) + sizeof(in_addr_t);
	struct ifnet		*ifp;
	struct igmp_grouprec	 ig;
	struct igmp_grouprec	*pig;
	struct ip_msource	*ims, *nims;
	struct mbuf		*m, *m0, *md;
	in_addr_t		 naddr;
	int			 m0srcs, nbytes, npbytes, off, rsrcs, schanged;
	int			 nallow, nblock;
	uint8_t			 mode, now, then;
	rectype_t		 crt, drt, nrt;

	IN_MULTI_LIST_LOCK_ASSERT();

	if (inm->inm_nsrc == 0 ||
	    (inm->inm_st[0].iss_asm > 0 && inm->inm_st[1].iss_asm > 0))
		return (0);

	ifp = inm->inm_ifp;			/* interface */
	mode = inm->inm_st[1].iss_fmode;	/* filter mode at t1 */
	crt = REC_NONE;	/* current group record type */
	drt = REC_NONE;	/* mask of completed group record types */
	nrt = REC_NONE;	/* record type for current node */
	m0srcs = 0;	/* # source which will fit in current mbuf chain */
	nbytes = 0;	/* # of bytes appended to group's state-change queue */
	npbytes = 0;	/* # of bytes appended this packet */
	rsrcs = 0;	/* # sources encoded in current record */
	schanged = 0;	/* # nodes encoded in overall filter change */
	nallow = 0;	/* # of source entries in ALLOW_NEW */
	nblock = 0;	/* # of source entries in BLOCK_OLD */
	nims = NULL;	/* next tree node pointer */

	/*
	 * For each possible filter record mode.
	 * The first kind of source we encounter tells us which
	 * is the first kind of record we start appending.
	 * If a node transitioned to UNDEFINED at t1, its mode is treated
	 * as the inverse of the group's filter mode.
	 */
	while (drt != REC_FULL) {
		do {
			m0 = mbufq_last(mq);
			if (m0 != NULL &&
			    (m0->m_pkthdr.PH_vt.vt_nrecs + 1 <=
			     IGMP_V3_REPORT_MAXRECS) &&
			    (m0->m_pkthdr.len + MINRECLEN) <
			     (ifp->if_mtu - IGMP_LEADINGSPACE)) {
				m = m0;
				m0srcs = (ifp->if_mtu - m0->m_pkthdr.len -
					    sizeof(struct igmp_grouprec)) /
				    sizeof(in_addr_t);
				CTR1(KTR_IGMPV3,
				    "%s: use previous packet", __func__);
			} else {
				m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
				if (m)
					m->m_data += IGMP_LEADINGSPACE;
				if (m == NULL) {
					m = m_gethdr(M_NOWAIT, MT_DATA);
					if (m)
						M_ALIGN(m, IGMP_LEADINGSPACE);
				}
				if (m == NULL) {
					CTR1(KTR_IGMPV3,
					    "%s: m_get*() failed", __func__);
					return (-ENOMEM);
				}
				m->m_pkthdr.PH_vt.vt_nrecs = 0;
				igmp_save_context(m, ifp);
				m0srcs = (ifp->if_mtu - IGMP_LEADINGSPACE -
				    sizeof(struct igmp_grouprec)) /
				    sizeof(in_addr_t);
				npbytes = 0;
				CTR1(KTR_IGMPV3,
				    "%s: allocated new packet", __func__);
			}
			/*
			 * Append the IGMP group record header to the
			 * current packet's data area.
			 * Recalculate pointer to free space for next
			 * group record, in case m_append() allocated
			 * a new mbuf or cluster.
			 */
			memset(&ig, 0, sizeof(ig));
			ig.ig_group = inm->inm_addr;
			if (!m_append(m, sizeof(ig), (void *)&ig)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_IGMPV3,
				    "%s: m_append() failed", __func__);
				return (-ENOMEM);
			}
			npbytes += sizeof(struct igmp_grouprec);
			if (m != m0) {
				/* new packet; offset in c hain */
				md = m_getptr(m, npbytes -
				    sizeof(struct igmp_grouprec), &off);
				pig = (struct igmp_grouprec *)(mtod(md,
				    uint8_t *) + off);
			} else {
				/* current packet; offset from last append */
				md = m_last(m);
				pig = (struct igmp_grouprec *)(mtod(md,
				    uint8_t *) + md->m_len -
				    sizeof(struct igmp_grouprec));
			}
			/*
			 * Begin walking the tree for this record type
			 * pass, or continue from where we left off
			 * previously if we had to allocate a new packet.
			 * Only report deltas in-mode at t1.
			 * We need not report included sources as allowed
			 * if we are in inclusive mode on the group,
			 * however the converse is not true.
			 */
			rsrcs = 0;
			if (nims == NULL)
				nims = RB_MIN(ip_msource_tree, &inm->inm_srcs);
			RB_FOREACH_FROM(ims, ip_msource_tree, nims) {
				CTR2(KTR_IGMPV3, "%s: visit node 0x%08x",
				    __func__, ims->ims_haddr);
				now = ims_get_mode(inm, ims, 1);
				then = ims_get_mode(inm, ims, 0);
				CTR3(KTR_IGMPV3, "%s: mode: t0 %d, t1 %d",
				    __func__, then, now);
				if (now == then) {
					CTR1(KTR_IGMPV3,
					    "%s: skip unchanged", __func__);
					continue;
				}
				if (mode == MCAST_EXCLUDE &&
				    now == MCAST_INCLUDE) {
					CTR1(KTR_IGMPV3,
					    "%s: skip IN src on EX group",
					    __func__);
					continue;
				}
				nrt = (rectype_t)now;
				if (nrt == REC_NONE)
					nrt = (rectype_t)(~mode & REC_FULL);
				if (schanged++ == 0) {
					crt = nrt;
				} else if (crt != nrt)
					continue;
				naddr = htonl(ims->ims_haddr);
				if (!m_append(m, sizeof(in_addr_t),
				    (void *)&naddr)) {
					if (m != m0)
						m_freem(m);
					CTR1(KTR_IGMPV3,
					    "%s: m_append() failed", __func__);
					return (-ENOMEM);
				}
				nallow += !!(crt == REC_ALLOW);
				nblock += !!(crt == REC_BLOCK);
				if (++rsrcs == m0srcs)
					break;
			}
			/*
			 * If we did not append any tree nodes on this
			 * pass, back out of allocations.
			 */
			if (rsrcs == 0) {
				npbytes -= sizeof(struct igmp_grouprec);
				if (m != m0) {
					CTR1(KTR_IGMPV3,
					    "%s: m_free(m)", __func__);
					m_freem(m);
				} else {
					CTR1(KTR_IGMPV3,
					    "%s: m_adj(m, -ig)", __func__);
					m_adj(m, -((int)sizeof(
					    struct igmp_grouprec)));
				}
				continue;
			}
			npbytes += (rsrcs * sizeof(in_addr_t));
			if (crt == REC_ALLOW)
				pig->ig_type = IGMP_ALLOW_NEW_SOURCES;
			else if (crt == REC_BLOCK)
				pig->ig_type = IGMP_BLOCK_OLD_SOURCES;
			pig->ig_numsrc = htons(rsrcs);
			/*
			 * Count the new group record, and enqueue this
			 * packet if it wasn't already queued.
			 */
			m->m_pkthdr.PH_vt.vt_nrecs++;
			if (m != m0)
				mbufq_enqueue(mq, m);
			nbytes += npbytes;
		} while (nims != NULL);
		drt |= crt;
		crt = (~crt & REC_FULL);
	}

	CTR3(KTR_IGMPV3, "%s: queued %d ALLOW_NEW, %d BLOCK_OLD", __func__,
	    nallow, nblock);

	return (nbytes);
}

static int
igmp_v3_merge_state_changes(struct in_multi *inm, struct mbufq *scq)
{
	struct mbufq	*gq;
	struct mbuf	*m;		/* pending state-change */
	struct mbuf	*m0;		/* copy of pending state-change */
	struct mbuf	*mt;		/* last state-change in packet */
	int		 docopy, domerge;
	u_int		 recslen;

	docopy = 0;
	domerge = 0;
	recslen = 0;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	/*
	 * If there are further pending retransmissions, make a writable
	 * copy of each queued state-change message before merging.
	 */
	if (inm->inm_scrv > 0)
		docopy = 1;

	gq = &inm->inm_scq;
#ifdef KTR
	if (mbufq_first(gq) == NULL) {
		CTR2(KTR_IGMPV3, "%s: WARNING: queue for inm %p is empty",
		    __func__, inm);
	}
#endif

	m = mbufq_first(gq);
	while (m != NULL) {
		/*
		 * Only merge the report into the current packet if
		 * there is sufficient space to do so; an IGMPv3 report
		 * packet may only contain 65,535 group records.
		 * Always use a simple mbuf chain concatentation to do this,
		 * as large state changes for single groups may have
		 * allocated clusters.
		 */
		domerge = 0;
		mt = mbufq_last(scq);
		if (mt != NULL) {
			recslen = m_length(m, NULL);

			if ((mt->m_pkthdr.PH_vt.vt_nrecs +
			    m->m_pkthdr.PH_vt.vt_nrecs <=
			    IGMP_V3_REPORT_MAXRECS) &&
			    (mt->m_pkthdr.len + recslen <=
			    (inm->inm_ifp->if_mtu - IGMP_LEADINGSPACE)))
				domerge = 1;
		}

		if (!domerge && mbufq_full(gq)) {
			CTR2(KTR_IGMPV3,
			    "%s: outbound queue full, skipping whole packet %p",
			    __func__, m);
			mt = m->m_nextpkt;
			if (!docopy)
				m_freem(m);
			m = mt;
			continue;
		}

		if (!docopy) {
			CTR2(KTR_IGMPV3, "%s: dequeueing %p", __func__, m);
			m0 = mbufq_dequeue(gq);
			m = m0->m_nextpkt;
		} else {
			CTR2(KTR_IGMPV3, "%s: copying %p", __func__, m);
			m0 = m_dup(m, M_NOWAIT);
			if (m0 == NULL)
				return (ENOMEM);
			m0->m_nextpkt = NULL;
			m = m->m_nextpkt;
		}

		if (!domerge) {
			CTR3(KTR_IGMPV3, "%s: queueing %p to scq %p)",
			    __func__, m0, scq);
			mbufq_enqueue(scq, m0);
		} else {
			struct mbuf *mtl;	/* last mbuf of packet mt */

			CTR3(KTR_IGMPV3, "%s: merging %p with scq tail %p)",
			    __func__, m0, mt);

			mtl = m_last(mt);
			m0->m_flags &= ~M_PKTHDR;
			mt->m_pkthdr.len += recslen;
			mt->m_pkthdr.PH_vt.vt_nrecs +=
			    m0->m_pkthdr.PH_vt.vt_nrecs;

			mtl->m_next = m0;
		}
	}

	return (0);
}

/*
 * Respond to a pending IGMPv3 General Query.
 */
static void
igmp_v3_dispatch_general_query(struct igmp_ifsoftc *igi)
{
	struct epoch_tracker	 et;
	struct ifmultiaddr	*ifma;
	struct ifnet		*ifp;
	struct in_multi		*inm;
	int			 retval __unused, loop;

	IN_MULTI_LIST_LOCK_ASSERT();
	IGMP_LOCK_ASSERT();

	KASSERT(igi->igi_version == IGMP_VERSION_3,
	    ("%s: called when version %d", __func__, igi->igi_version));

	/*
	 * Check that there are some packets queued. If so, send them first.
	 * For large number of groups the reply to general query can take
	 * many packets, we should finish sending them before starting of
	 * queuing the new reply.
	 */
	if (mbufq_len(&igi->igi_gq) != 0)
		goto send;

	ifp = igi->igi_ifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;

		inm = (struct in_multi *)ifma->ifma_protospec;
		KASSERT(ifp == inm->inm_ifp,
		    ("%s: inconsistent ifp", __func__));

		switch (inm->inm_state) {
		case IGMP_NOT_MEMBER:
		case IGMP_SILENT_MEMBER:
			break;
		case IGMP_REPORTING_MEMBER:
		case IGMP_IDLE_MEMBER:
		case IGMP_LAZY_MEMBER:
		case IGMP_SLEEPING_MEMBER:
		case IGMP_AWAKENING_MEMBER:
			inm->inm_state = IGMP_REPORTING_MEMBER;
			retval = igmp_v3_enqueue_group_record(&igi->igi_gq,
			    inm, 0, 0, 0);
			CTR2(KTR_IGMPV3, "%s: enqueue record = %d",
			    __func__, retval);
			break;
		case IGMP_G_QUERY_PENDING_MEMBER:
		case IGMP_SG_QUERY_PENDING_MEMBER:
		case IGMP_LEAVING_MEMBER:
			break;
		}
	}
	NET_EPOCH_EXIT(et);

send:
	loop = (igi->igi_flags & IGIF_LOOPBACK) ? 1 : 0;
	igmp_dispatch_queue(&igi->igi_gq, IGMP_MAX_RESPONSE_BURST, loop);

	/*
	 * Slew transmission of bursts over 500ms intervals.
	 */
	if (mbufq_first(&igi->igi_gq) != NULL) {
		igi->igi_v3_timer = 1 + IGMP_RANDOM_DELAY(
		    IGMP_RESPONSE_BURST_INTERVAL);
		V_interface_timers_running = 1;
	}
}

/*
 * Transmit the next pending IGMP message in the output queue.
 *
 * We get called from netisr_processqueue(). A mutex private to igmpoq
 * will be acquired and released around this routine.
 *
 * VIMAGE: Needs to store/restore vnet pointer on a per-mbuf-chain basis.
 * MRT: Nothing needs to be done, as IGMP traffic is always local to
 * a link and uses a link-scope multicast address.
 */
static void
igmp_intr(struct mbuf *m)
{
	struct ip_moptions	 imo;
	struct ifnet		*ifp;
	struct mbuf		*ipopts, *m0;
	int			 error;
	uint32_t		 ifindex;

	CTR2(KTR_IGMPV3, "%s: transmit %p", __func__, m);

	/*
	 * Set VNET image pointer from enqueued mbuf chain
	 * before doing anything else. Whilst we use interface
	 * indexes to guard against interface detach, they are
	 * unique to each VIMAGE and must be retrieved.
	 */
	CURVNET_SET((struct vnet *)(m->m_pkthdr.PH_loc.ptr));
	ifindex = igmp_restore_context(m);

	/*
	 * Check if the ifnet still exists. This limits the scope of
	 * any race in the absence of a global ifp lock for low cost
	 * (an array lookup).
	 */
	ifp = ifnet_byindex(ifindex);
	if (ifp == NULL) {
		CTR3(KTR_IGMPV3, "%s: dropped %p as ifindex %u went away.",
		    __func__, m, ifindex);
		m_freem(m);
		IPSTAT_INC(ips_noroute);
		goto out;
	}

	ipopts = V_igmp_sendra ? m_raopt : NULL;

	imo.imo_multicast_ttl  = 1;
	imo.imo_multicast_vif  = -1;
	imo.imo_multicast_loop = (V_ip_mrouter != NULL);

	/*
	 * If the user requested that IGMP traffic be explicitly
	 * redirected to the loopback interface (e.g. they are running a
	 * MANET interface and the routing protocol needs to see the
	 * updates), handle this now.
	 */
	if (m->m_flags & M_IGMP_LOOP)
		imo.imo_multicast_ifp = V_loif;
	else
		imo.imo_multicast_ifp = ifp;

	if (m->m_flags & M_IGMPV2) {
		m0 = m;
	} else {
		m0 = igmp_v3_encap_report(ifp, m);
		if (m0 == NULL) {
			CTR2(KTR_IGMPV3, "%s: dropped %p", __func__, m);
			m_freem(m);
			IPSTAT_INC(ips_odropped);
			goto out;
		}
	}

	igmp_scrub_context(m0);
	m_clrprotoflags(m);
	m0->m_pkthdr.rcvif = V_loif;
#ifdef MAC
	mac_netinet_igmp_send(ifp, m0);
#endif
	error = ip_output(m0, ipopts, NULL, 0, &imo, NULL);
	if (error) {
		CTR3(KTR_IGMPV3, "%s: ip_output(%p) = %d", __func__, m0, error);
		goto out;
	}

	IGMPSTAT_INC(igps_snd_reports);

out:
	/*
	 * We must restore the existing vnet pointer before
	 * continuing as we are run from netisr context.
	 */
	CURVNET_RESTORE();
}

/*
 * Encapsulate an IGMPv3 report.
 *
 * The internal mbuf flag M_IGMPV3_HDR is used to indicate that the mbuf
 * chain has already had its IP/IGMPv3 header prepended. In this case
 * the function will not attempt to prepend; the lengths and checksums
 * will however be re-computed.
 *
 * Returns a pointer to the new mbuf chain head, or NULL if the
 * allocation failed.
 */
static struct mbuf *
igmp_v3_encap_report(struct ifnet *ifp, struct mbuf *m)
{
	struct rm_priotracker	in_ifa_tracker;
	struct igmp_report	*igmp;
	struct ip		*ip;
	int			 hdrlen, igmpreclen;

	KASSERT((m->m_flags & M_PKTHDR),
	    ("%s: mbuf chain %p is !M_PKTHDR", __func__, m));

	igmpreclen = m_length(m, NULL);
	hdrlen = sizeof(struct ip) + sizeof(struct igmp_report);

	if (m->m_flags & M_IGMPV3_HDR) {
		igmpreclen -= hdrlen;
	} else {
		M_PREPEND(m, hdrlen, M_NOWAIT);
		if (m == NULL)
			return (NULL);
		m->m_flags |= M_IGMPV3_HDR;
	}

	CTR2(KTR_IGMPV3, "%s: igmpreclen is %d", __func__, igmpreclen);

	m->m_data += sizeof(struct ip);
	m->m_len -= sizeof(struct ip);

	igmp = mtod(m, struct igmp_report *);
	igmp->ir_type = IGMP_v3_HOST_MEMBERSHIP_REPORT;
	igmp->ir_rsv1 = 0;
	igmp->ir_rsv2 = 0;
	igmp->ir_numgrps = htons(m->m_pkthdr.PH_vt.vt_nrecs);
	igmp->ir_cksum = 0;
	igmp->ir_cksum = in_cksum(m, sizeof(struct igmp_report) + igmpreclen);
	m->m_pkthdr.PH_vt.vt_nrecs = 0;

	m->m_data -= sizeof(struct ip);
	m->m_len += sizeof(struct ip);

	ip = mtod(m, struct ip *);
	ip->ip_tos = IPTOS_PREC_INTERNETCONTROL;
	ip->ip_len = htons(hdrlen + igmpreclen);
	ip->ip_off = htons(IP_DF);
	ip->ip_p = IPPROTO_IGMP;
	ip->ip_sum = 0;

	ip->ip_src.s_addr = INADDR_ANY;

	if (m->m_flags & M_IGMP_LOOP) {
		struct epoch_tracker et;
		struct in_ifaddr *ia;

		NET_EPOCH_ENTER(et);
		IFP_TO_IA(ifp, ia, &in_ifa_tracker);
		if (ia != NULL)
			ip->ip_src = ia->ia_addr.sin_addr;
		NET_EPOCH_EXIT(et);
	}

	ip->ip_dst.s_addr = htonl(INADDR_ALLRPTS_GROUP);

	return (m);
}

#ifdef KTR
static char *
igmp_rec_type_to_str(const int type)
{

	switch (type) {
		case IGMP_CHANGE_TO_EXCLUDE_MODE:
			return "TO_EX";
			break;
		case IGMP_CHANGE_TO_INCLUDE_MODE:
			return "TO_IN";
			break;
		case IGMP_MODE_IS_EXCLUDE:
			return "MODE_EX";
			break;
		case IGMP_MODE_IS_INCLUDE:
			return "MODE_IN";
			break;
		case IGMP_ALLOW_NEW_SOURCES:
			return "ALLOW_NEW";
			break;
		case IGMP_BLOCK_OLD_SOURCES:
			return "BLOCK_OLD";
			break;
		default:
			break;
	}
	return "unknown";
}
#endif

#ifdef VIMAGE
static void
vnet_igmp_init(const void *unused __unused)
{

	netisr_register_vnet(&igmp_nh);
}
VNET_SYSINIT(vnet_igmp_init, SI_SUB_PROTO_MC, SI_ORDER_ANY,
    vnet_igmp_init, NULL);

static void
vnet_igmp_uninit(const void *unused __unused)
{

	/* This can happen when we shutdown the entire network stack. */
	CTR1(KTR_IGMPV3, "%s: tearing down", __func__);

	netisr_unregister_vnet(&igmp_nh);
}
VNET_SYSUNINIT(vnet_igmp_uninit, SI_SUB_PROTO_MC, SI_ORDER_ANY,
    vnet_igmp_uninit, NULL);
#endif

#ifdef DDB
DB_SHOW_COMMAND(igi_list, db_show_igi_list)
{
	struct igmp_ifsoftc *igi, *tigi;
	LIST_HEAD(_igi_list, igmp_ifsoftc) *igi_head;

	if (!have_addr) {
		db_printf("usage: show igi_list <addr>\n");
		return;
	}
	igi_head = (struct _igi_list *)addr;

	LIST_FOREACH_SAFE(igi, igi_head, igi_link, tigi) {
		db_printf("igmp_ifsoftc %p:\n", igi);
		db_printf("    ifp %p\n", igi->igi_ifp);
		db_printf("    version %u\n", igi->igi_version);
		db_printf("    v1_timer %u\n", igi->igi_v1_timer);
		db_printf("    v2_timer %u\n", igi->igi_v2_timer);
		db_printf("    v3_timer %u\n", igi->igi_v3_timer);
		db_printf("    flags %#x\n", igi->igi_flags);
		db_printf("    rv %u\n", igi->igi_rv);
		db_printf("    qi %u\n", igi->igi_qi);
		db_printf("    qri %u\n", igi->igi_qri);
		db_printf("    uri %u\n", igi->igi_uri);
		/* struct mbufq    igi_gq; */
		db_printf("\n");
	}
}
#endif

static int
igmp_modevent(module_t mod, int type, void *unused __unused)
{

	switch (type) {
	case MOD_LOAD:
		CTR1(KTR_IGMPV3, "%s: initializing", __func__);
		IGMP_LOCK_INIT();
		m_raopt = igmp_ra_alloc();
		netisr_register(&igmp_nh);
		break;
	case MOD_UNLOAD:
		CTR1(KTR_IGMPV3, "%s: tearing down", __func__);
		netisr_unregister(&igmp_nh);
		m_free(m_raopt);
		m_raopt = NULL;
		IGMP_LOCK_DESTROY();
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t igmp_mod = {
    "igmp",
    igmp_modevent,
    0
};
DECLARE_MODULE(igmp, igmp_mod, SI_SUB_PROTO_MC, SI_ORDER_MIDDLE);
