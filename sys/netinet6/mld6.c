/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Bruce Simpson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: mld6.c,v 1.27 2001/04/04 05:17:30 itojun Exp $
 */

/*-
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/ktr.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet/icmp6.h>
#include <netinet6/mld6.h>
#include <netinet6/mld6_var.h>

#include <security/mac/mac_framework.h>

#ifndef KTR_MLD
#define KTR_MLD KTR_INET6
#endif

static struct mld_ifsoftc *
		mli_alloc_locked(struct ifnet *);
static void	mli_delete_locked(const struct ifnet *);
static void	mld_dispatch_packet(struct mbuf *);
static void	mld_dispatch_queue(struct mbufq *, int);
static void	mld_final_leave(struct in6_multi *, struct mld_ifsoftc *);
static void	mld_fasttimo_vnet(struct in6_multi_head *inmh);
static int	mld_handle_state_change(struct in6_multi *,
		    struct mld_ifsoftc *);
static int	mld_initial_join(struct in6_multi *, struct mld_ifsoftc *,
		    const int);
#ifdef KTR
static char *	mld_rec_type_to_str(const int);
#endif
static void	mld_set_version(struct mld_ifsoftc *, const int);
static void	mld_slowtimo_vnet(void);
static int	mld_v1_input_query(struct ifnet *, const struct ip6_hdr *,
		    /*const*/ struct mld_hdr *);
static int	mld_v1_input_report(struct ifnet *, const struct ip6_hdr *,
		    /*const*/ struct mld_hdr *);
static void	mld_v1_process_group_timer(struct in6_multi_head *,
		    struct in6_multi *);
static void	mld_v1_process_querier_timers(struct mld_ifsoftc *);
static int	mld_v1_transmit_report(struct in6_multi *, const int);
static void	mld_v1_update_group(struct in6_multi *, const int);
static void	mld_v2_cancel_link_timers(struct mld_ifsoftc *);
static void	mld_v2_dispatch_general_query(struct mld_ifsoftc *);
static struct mbuf *
		mld_v2_encap_report(struct ifnet *, struct mbuf *);
static int	mld_v2_enqueue_filter_change(struct mbufq *,
		    struct in6_multi *);
static int	mld_v2_enqueue_group_record(struct mbufq *,
		    struct in6_multi *, const int, const int, const int,
		    const int);
static int	mld_v2_input_query(struct ifnet *, const struct ip6_hdr *,
		    struct mbuf *, const int, const int);
static int	mld_v2_merge_state_changes(struct in6_multi *,
		    struct mbufq *);
static void	mld_v2_process_group_timers(struct in6_multi_head *,
		    struct mbufq *, struct mbufq *,
		    struct in6_multi *, const int);
static int	mld_v2_process_group_query(struct in6_multi *,
		    struct mld_ifsoftc *mli, int, struct mbuf *, const int);
static int	sysctl_mld_gsr(SYSCTL_HANDLER_ARGS);
static int	sysctl_mld_ifinfo(SYSCTL_HANDLER_ARGS);

/*
 * Normative references: RFC 2710, RFC 3590, RFC 3810.
 *
 * Locking:
 *  * The MLD subsystem lock ends up being system-wide for the moment,
 *    but could be per-VIMAGE later on.
 *  * The permitted lock order is: IN6_MULTI_LOCK, MLD_LOCK, IF_ADDR_LOCK.
 *    Any may be taken independently; if any are held at the same
 *    time, the above lock order must be followed.
 *  * IN6_MULTI_LOCK covers in_multi.
 *  * MLD_LOCK covers per-link state and any global variables in this file.
 *  * IF_ADDR_LOCK covers if_multiaddrs, which is used for a variety of
 *    per-link state iterators.
 *
 *  XXX LOR PREVENTION
 *  A special case for IPv6 is the in6_setscope() routine. ip6_output()
 *  will not accept an ifp; it wants an embedded scope ID, unlike
 *  ip_output(), which happily takes the ifp given to it. The embedded
 *  scope ID is only used by MLD to select the outgoing interface.
 *
 *  During interface attach and detach, MLD will take MLD_LOCK *after*
 *  the IF_AFDATA_LOCK.
 *  As in6_setscope() takes IF_AFDATA_LOCK then SCOPE_LOCK, we can't call
 *  it with MLD_LOCK held without triggering an LOR. A netisr with indirect
 *  dispatch could work around this, but we'd rather not do that, as it
 *  can introduce other races.
 *
 *  As such, we exploit the fact that the scope ID is just the interface
 *  index, and embed it in the IPv6 destination address accordingly.
 *  This is potentially NOT VALID for MLDv1 reports, as they
 *  are always sent to the multicast group itself; as MLDv2
 *  reports are always sent to ff02::16, this is not an issue
 *  when MLDv2 is in use.
 *
 *  This does not however eliminate the LOR when ip6_output() itself
 *  calls in6_setscope() internally whilst MLD_LOCK is held. This will
 *  trigger a LOR warning in WITNESS when the ifnet is detached.
 *
 *  The right answer is probably to make IF_AFDATA_LOCK an rwlock, given
 *  how it's used across the network stack. Here we're simply exploiting
 *  the fact that MLD runs at a similar layer in the stack to scope6.c.
 *
 * VIMAGE:
 *  * Each in6_multi corresponds to an ifp, and each ifp corresponds
 *    to a vnet in ifp->if_vnet.
 */
static struct mtx		 mld_mtx;
static MALLOC_DEFINE(M_MLD, "mld", "mld state");

#define	MLD_EMBEDSCOPE(pin6, zoneid)					\
	if (IN6_IS_SCOPE_LINKLOCAL(pin6) ||				\
	    IN6_IS_ADDR_MC_INTFACELOCAL(pin6))				\
		(pin6)->s6_addr16[1] = htons((zoneid) & 0xFFFF)		\

/*
 * VIMAGE-wide globals.
 */
VNET_DEFINE_STATIC(struct timeval, mld_gsrdelay) = {10, 0};
VNET_DEFINE_STATIC(LIST_HEAD(, mld_ifsoftc), mli_head);
VNET_DEFINE_STATIC(int, interface_timers_running6);
VNET_DEFINE_STATIC(int, state_change_timers_running6);
VNET_DEFINE_STATIC(int, current_state_timers_running6);

#define	V_mld_gsrdelay			VNET(mld_gsrdelay)
#define	V_mli_head			VNET(mli_head)
#define	V_interface_timers_running6	VNET(interface_timers_running6)
#define	V_state_change_timers_running6	VNET(state_change_timers_running6)
#define	V_current_state_timers_running6	VNET(current_state_timers_running6)

SYSCTL_DECL(_net_inet6);	/* Note: Not in any common header. */

SYSCTL_NODE(_net_inet6, OID_AUTO, mld, CTLFLAG_RW, 0,
    "IPv6 Multicast Listener Discovery");

/*
 * Virtualized sysctls.
 */
SYSCTL_PROC(_net_inet6_mld, OID_AUTO, gsrdelay,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    &VNET_NAME(mld_gsrdelay.tv_sec), 0, sysctl_mld_gsr, "I",
    "Rate limit for MLDv2 Group-and-Source queries in seconds");

/*
 * Non-virtualized sysctls.
 */
static SYSCTL_NODE(_net_inet6_mld, OID_AUTO, ifinfo,
    CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_mld_ifinfo,
    "Per-interface MLDv2 state");

static int	mld_v1enable = 1;
SYSCTL_INT(_net_inet6_mld, OID_AUTO, v1enable, CTLFLAG_RWTUN,
    &mld_v1enable, 0, "Enable fallback to MLDv1");

static int	mld_v2enable = 1;
SYSCTL_INT(_net_inet6_mld, OID_AUTO, v2enable, CTLFLAG_RWTUN,
    &mld_v2enable, 0, "Enable MLDv2");

static int	mld_use_allow = 1;
SYSCTL_INT(_net_inet6_mld, OID_AUTO, use_allow, CTLFLAG_RWTUN,
    &mld_use_allow, 0, "Use ALLOW/BLOCK for RFC 4604 SSM joins/leaves");

/*
 * Packed Router Alert option structure declaration.
 */
struct mld_raopt {
	struct ip6_hbh		hbh;
	struct ip6_opt		pad;
	struct ip6_opt_router	ra;
} __packed;

/*
 * Router Alert hop-by-hop option header.
 */
static struct mld_raopt mld_ra = {
	.hbh = { 0, 0 },
	.pad = { .ip6o_type = IP6OPT_PADN, 0 },
	.ra = {
	    .ip6or_type = IP6OPT_ROUTER_ALERT,
	    .ip6or_len = IP6OPT_RTALERT_LEN - 2,
	    .ip6or_value[0] = ((IP6OPT_RTALERT_MLD >> 8) & 0xFF),
	    .ip6or_value[1] = (IP6OPT_RTALERT_MLD & 0xFF)
	}
};
static struct ip6_pktopts mld_po;

static __inline void
mld_save_context(struct mbuf *m, struct ifnet *ifp)
{

#ifdef VIMAGE
	m->m_pkthdr.PH_loc.ptr = ifp->if_vnet;
#endif /* VIMAGE */
	m->m_pkthdr.flowid = ifp->if_index;
}

static __inline void
mld_scrub_context(struct mbuf *m)
{

	m->m_pkthdr.PH_loc.ptr = NULL;
	m->m_pkthdr.flowid = 0;
}

/*
 * Restore context from a queued output chain.
 * Return saved ifindex.
 *
 * VIMAGE: The assertion is there to make sure that we
 * actually called CURVNET_SET() with what's in the mbuf chain.
 */
static __inline uint32_t
mld_restore_context(struct mbuf *m)
{

#if defined(VIMAGE) && defined(INVARIANTS)
	KASSERT(curvnet == m->m_pkthdr.PH_loc.ptr,
	    ("%s: called when curvnet was not restored: cuvnet %p m ptr %p",
	    __func__, curvnet, m->m_pkthdr.PH_loc.ptr));
#endif
	return (m->m_pkthdr.flowid);
}

/*
 * Retrieve or set threshold between group-source queries in seconds.
 *
 * VIMAGE: Assume curvnet set by caller.
 * SMPng: NOTE: Serialized by MLD lock.
 */
static int
sysctl_mld_gsr(SYSCTL_HANDLER_ARGS)
{
	int error;
	int i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error)
		return (error);

	MLD_LOCK();

	i = V_mld_gsrdelay.tv_sec;

	error = sysctl_handle_int(oidp, &i, 0, req);
	if (error || !req->newptr)
		goto out_locked;

	if (i < -1 || i >= 60) {
		error = EINVAL;
		goto out_locked;
	}

	CTR2(KTR_MLD, "change mld_gsrdelay from %d to %d",
	     V_mld_gsrdelay.tv_sec, i);
	V_mld_gsrdelay.tv_sec = i;

out_locked:
	MLD_UNLOCK();
	return (error);
}

/*
 * Expose struct mld_ifsoftc to userland, keyed by ifindex.
 * For use by ifmcstat(8).
 *
 * SMPng: NOTE: Does an unlocked ifindex space read.
 * VIMAGE: Assume curvnet set by caller. The node handler itself
 * is not directly virtualized.
 */
static int
sysctl_mld_ifinfo(SYSCTL_HANDLER_ARGS)
{
	int			*name;
	int			 error;
	u_int			 namelen;
	struct ifnet		*ifp;
	struct mld_ifsoftc	*mli;

	name = (int *)arg1;
	namelen = arg2;

	if (req->newptr != NULL)
		return (EPERM);

	if (namelen != 1)
		return (EINVAL);

	error = sysctl_wire_old_buffer(req, sizeof(struct mld_ifinfo));
	if (error)
		return (error);

	IN6_MULTI_LOCK();
	IN6_MULTI_LIST_LOCK();
	MLD_LOCK();

	if (name[0] <= 0 || name[0] > V_if_index) {
		error = ENOENT;
		goto out_locked;
	}

	error = ENOENT;

	ifp = ifnet_byindex(name[0]);
	if (ifp == NULL)
		goto out_locked;

	LIST_FOREACH(mli, &V_mli_head, mli_link) {
		if (ifp == mli->mli_ifp) {
			struct mld_ifinfo info;

			info.mli_version = mli->mli_version;
			info.mli_v1_timer = mli->mli_v1_timer;
			info.mli_v2_timer = mli->mli_v2_timer;
			info.mli_flags = mli->mli_flags;
			info.mli_rv = mli->mli_rv;
			info.mli_qi = mli->mli_qi;
			info.mli_qri = mli->mli_qri;
			info.mli_uri = mli->mli_uri;
			error = SYSCTL_OUT(req, &info, sizeof(info));
			break;
		}
	}

out_locked:
	MLD_UNLOCK();
	IN6_MULTI_LIST_UNLOCK();
	IN6_MULTI_UNLOCK();
	return (error);
}

/*
 * Dispatch an entire queue of pending packet chains.
 * VIMAGE: Assumes the vnet pointer has been set.
 */
static void
mld_dispatch_queue(struct mbufq *mq, int limit)
{
	struct mbuf *m;

	while ((m = mbufq_dequeue(mq)) != NULL) {
		CTR3(KTR_MLD, "%s: dispatch %p from %p", __func__, mq, m);
		mld_dispatch_packet(m);
		if (--limit == 0)
			break;
	}
}

/*
 * Filter outgoing MLD report state by group.
 *
 * Reports are ALWAYS suppressed for ALL-HOSTS (ff02::1)
 * and node-local addresses. However, kernel and socket consumers
 * always embed the KAME scope ID in the address provided, so strip it
 * when performing comparison.
 * Note: This is not the same as the *multicast* scope.
 *
 * Return zero if the given group is one for which MLD reports
 * should be suppressed, or non-zero if reports should be issued.
 */
static __inline int
mld_is_addr_reported(const struct in6_addr *addr)
{

	KASSERT(IN6_IS_ADDR_MULTICAST(addr), ("%s: not multicast", __func__));

	if (IPV6_ADDR_MC_SCOPE(addr) == IPV6_ADDR_SCOPE_NODELOCAL)
		return (0);

	if (IPV6_ADDR_MC_SCOPE(addr) == IPV6_ADDR_SCOPE_LINKLOCAL) {
		struct in6_addr tmp = *addr;
		in6_clearscope(&tmp);
		if (IN6_ARE_ADDR_EQUAL(&tmp, &in6addr_linklocal_allnodes))
			return (0);
	}

	return (1);
}

/*
 * Attach MLD when PF_INET6 is attached to an interface.
 *
 * SMPng: Normally called with IF_AFDATA_LOCK held.
 */
struct mld_ifsoftc *
mld_domifattach(struct ifnet *ifp)
{
	struct mld_ifsoftc *mli;

	CTR3(KTR_MLD, "%s: called for ifp %p(%s)",
	    __func__, ifp, if_name(ifp));

	MLD_LOCK();

	mli = mli_alloc_locked(ifp);
	if (!(ifp->if_flags & IFF_MULTICAST))
		mli->mli_flags |= MLIF_SILENT;
	if (mld_use_allow)
		mli->mli_flags |= MLIF_USEALLOW;

	MLD_UNLOCK();

	return (mli);
}

/*
 * VIMAGE: assume curvnet set by caller.
 */
static struct mld_ifsoftc *
mli_alloc_locked(/*const*/ struct ifnet *ifp)
{
	struct mld_ifsoftc *mli;

	MLD_LOCK_ASSERT();

	mli = malloc(sizeof(struct mld_ifsoftc), M_MLD, M_NOWAIT|M_ZERO);
	if (mli == NULL)
		goto out;

	mli->mli_ifp = ifp;
	mli->mli_version = MLD_VERSION_2;
	mli->mli_flags = 0;
	mli->mli_rv = MLD_RV_INIT;
	mli->mli_qi = MLD_QI_INIT;
	mli->mli_qri = MLD_QRI_INIT;
	mli->mli_uri = MLD_URI_INIT;
	mbufq_init(&mli->mli_gq, MLD_MAX_RESPONSE_PACKETS);

	LIST_INSERT_HEAD(&V_mli_head, mli, mli_link);

	CTR2(KTR_MLD, "allocate mld_ifsoftc for ifp %p(%s)",
	     ifp, if_name(ifp));

out:
	return (mli);
}

/*
 * Hook for ifdetach.
 *
 * NOTE: Some finalization tasks need to run before the protocol domain
 * is detached, but also before the link layer does its cleanup.
 * Run before link-layer cleanup; cleanup groups, but do not free MLD state.
 *
 * SMPng: Caller must hold IN6_MULTI_LOCK().
 * Must take IF_ADDR_LOCK() to cover if_multiaddrs iterator.
 * XXX This routine is also bitten by unlocked ifma_protospec access.
 */
void
mld_ifdetach(struct ifnet *ifp, struct in6_multi_head *inmh)
{
	struct epoch_tracker     et;
	struct mld_ifsoftc	*mli;
	struct ifmultiaddr	*ifma;
	struct in6_multi	*inm;

	CTR3(KTR_MLD, "%s: called for ifp %p(%s)", __func__, ifp,
	    if_name(ifp));

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK();

	mli = MLD_IFINFO(ifp);
	IF_ADDR_WLOCK(ifp);
	/*
	 * Extract list of in6_multi associated with the detaching ifp
	 * which the PF_INET6 layer is about to release.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		inm = in6m_ifmultiaddr_get_inm(ifma);
		if (inm == NULL)
			continue;
		in6m_disconnect_locked(inmh, inm);

		if (mli->mli_version == MLD_VERSION_2) {
			in6m_clear_recorded(inm);

			/*
			 * We need to release the final reference held
			 * for issuing the INCLUDE {}.
			 */
			if (inm->in6m_state == MLD_LEAVING_MEMBER) {
				inm->in6m_state = MLD_NOT_MEMBER;
				in6m_rele_locked(inmh, inm);
			}
		}
	}
	NET_EPOCH_EXIT(et);
	IF_ADDR_WUNLOCK(ifp);
	MLD_UNLOCK();
}

/*
 * Hook for domifdetach.
 * Runs after link-layer cleanup; free MLD state.
 *
 * SMPng: Normally called with IF_AFDATA_LOCK held.
 */
void
mld_domifdetach(struct ifnet *ifp)
{

	CTR3(KTR_MLD, "%s: called for ifp %p(%s)",
	    __func__, ifp, if_name(ifp));

	MLD_LOCK();
	mli_delete_locked(ifp);
	MLD_UNLOCK();
}

static void
mli_delete_locked(const struct ifnet *ifp)
{
	struct mld_ifsoftc *mli, *tmli;

	CTR3(KTR_MLD, "%s: freeing mld_ifsoftc for ifp %p(%s)",
	    __func__, ifp, if_name(ifp));

	MLD_LOCK_ASSERT();

	LIST_FOREACH_SAFE(mli, &V_mli_head, mli_link, tmli) {
		if (mli->mli_ifp == ifp) {
			/*
			 * Free deferred General Query responses.
			 */
			mbufq_drain(&mli->mli_gq);

			LIST_REMOVE(mli, mli_link);

			free(mli, M_MLD);
			return;
		}
	}
}

/*
 * Process a received MLDv1 general or address-specific query.
 * Assumes that the query header has been pulled up to sizeof(mld_hdr).
 *
 * NOTE: Can't be fully const correct as we temporarily embed scope ID in
 * mld_addr. This is OK as we own the mbuf chain.
 */
static int
mld_v1_input_query(struct ifnet *ifp, const struct ip6_hdr *ip6,
    /*const*/ struct mld_hdr *mld)
{
	struct epoch_tracker	 et;
	struct ifmultiaddr	*ifma;
	struct mld_ifsoftc	*mli;
	struct in6_multi	*inm;
	int			 is_general_query;
	uint16_t		 timer;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	is_general_query = 0;

	if (!mld_v1enable) {
		CTR3(KTR_MLD, "ignore v1 query %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &mld->mld_addr),
		    ifp, if_name(ifp));
		return (0);
	}

	/*
	 * RFC3810 Section 6.2: MLD queries must originate from
	 * a router's link-local address.
	 */
	if (!IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_src)) {
		CTR3(KTR_MLD, "ignore v1 query src %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &ip6->ip6_src),
		    ifp, if_name(ifp));
		return (0);
	}

	/*
	 * Do address field validation upfront before we accept
	 * the query.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&mld->mld_addr)) {
		/*
		 * MLDv1 General Query.
		 * If this was not sent to the all-nodes group, ignore it.
		 */
		struct in6_addr		 dst;

		dst = ip6->ip6_dst;
		in6_clearscope(&dst);
		if (!IN6_ARE_ADDR_EQUAL(&dst, &in6addr_linklocal_allnodes))
			return (EINVAL);
		is_general_query = 1;
	} else {
		/*
		 * Embed scope ID of receiving interface in MLD query for
		 * lookup whilst we don't hold other locks.
		 */
		in6_setscope(&mld->mld_addr, ifp, NULL);
	}

	IN6_MULTI_LIST_LOCK();
	MLD_LOCK();

	/*
	 * Switch to MLDv1 host compatibility mode.
	 */
	mli = MLD_IFINFO(ifp);
	KASSERT(mli != NULL, ("%s: no mld_ifsoftc for ifp %p", __func__, ifp));
	mld_set_version(mli, MLD_VERSION_1);

	timer = (ntohs(mld->mld_maxdelay) * PR_FASTHZ) / MLD_TIMER_SCALE;
	if (timer == 0)
		timer = 1;

	NET_EPOCH_ENTER(et);
	if (is_general_query) {
		/*
		 * For each reporting group joined on this
		 * interface, kick the report timer.
		 */
		CTR2(KTR_MLD, "process v1 general query on ifp %p(%s)",
			 ifp, if_name(ifp));
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			inm = in6m_ifmultiaddr_get_inm(ifma);
			if (inm == NULL)
				continue;
			mld_v1_update_group(inm, timer);
		}
	} else {
		/*
		 * MLDv1 Group-Specific Query.
		 * If this is a group-specific MLDv1 query, we need only
		 * look up the single group to process it.
		 */
		inm = in6m_lookup_locked(ifp, &mld->mld_addr);
		if (inm != NULL) {
			CTR3(KTR_MLD, "process v1 query %s on ifp %p(%s)",
			    ip6_sprintf(ip6tbuf, &mld->mld_addr),
			    ifp, if_name(ifp));
			mld_v1_update_group(inm, timer);
		}
		/* XXX Clear embedded scope ID as userland won't expect it. */
		in6_clearscope(&mld->mld_addr);
	}

	NET_EPOCH_EXIT(et);
	MLD_UNLOCK();
	IN6_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Update the report timer on a group in response to an MLDv1 query.
 *
 * If we are becoming the reporting member for this group, start the timer.
 * If we already are the reporting member for this group, and timer is
 * below the threshold, reset it.
 *
 * We may be updating the group for the first time since we switched
 * to MLDv2. If we are, then we must clear any recorded source lists,
 * and transition to REPORTING state; the group timer is overloaded
 * for group and group-source query responses. 
 *
 * Unlike MLDv2, the delay per group should be jittered
 * to avoid bursts of MLDv1 reports.
 */
static void
mld_v1_update_group(struct in6_multi *inm, const int timer)
{
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	CTR4(KTR_MLD, "%s: %s/%s timer=%d", __func__,
	    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    if_name(inm->in6m_ifp), timer);

	IN6_MULTI_LIST_LOCK_ASSERT();

	switch (inm->in6m_state) {
	case MLD_NOT_MEMBER:
	case MLD_SILENT_MEMBER:
		break;
	case MLD_REPORTING_MEMBER:
		if (inm->in6m_timer != 0 &&
		    inm->in6m_timer <= timer) {
			CTR1(KTR_MLD, "%s: REPORTING and timer running, "
			    "skipping.", __func__);
			break;
		}
		/* FALLTHROUGH */
	case MLD_SG_QUERY_PENDING_MEMBER:
	case MLD_G_QUERY_PENDING_MEMBER:
	case MLD_IDLE_MEMBER:
	case MLD_LAZY_MEMBER:
	case MLD_AWAKENING_MEMBER:
		CTR1(KTR_MLD, "%s: ->REPORTING", __func__);
		inm->in6m_state = MLD_REPORTING_MEMBER;
		inm->in6m_timer = MLD_RANDOM_DELAY(timer);
		V_current_state_timers_running6 = 1;
		break;
	case MLD_SLEEPING_MEMBER:
		CTR1(KTR_MLD, "%s: ->AWAKENING", __func__);
		inm->in6m_state = MLD_AWAKENING_MEMBER;
		break;
	case MLD_LEAVING_MEMBER:
		break;
	}
}

/*
 * Process a received MLDv2 general, group-specific or
 * group-and-source-specific query.
 *
 * Assumes that the query header has been pulled up to sizeof(mldv2_query).
 *
 * Return 0 if successful, otherwise an appropriate error code is returned.
 */
static int
mld_v2_input_query(struct ifnet *ifp, const struct ip6_hdr *ip6,
    struct mbuf *m, const int off, const int icmp6len)
{
	struct mld_ifsoftc	*mli;
	struct mldv2_query	*mld;
	struct in6_multi	*inm;
	uint32_t		 maxdelay, nsrc, qqi;
	int			 is_general_query;
	uint16_t		 timer;
	uint8_t			 qrv;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	if (!mld_v2enable) {
		CTR3(KTR_MLD, "ignore v2 query src %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &ip6->ip6_src),
		    ifp, if_name(ifp));
		return (0);
	}

	/*
	 * RFC3810 Section 6.2: MLD queries must originate from
	 * a router's link-local address.
	 */
	if (!IN6_IS_SCOPE_LINKLOCAL(&ip6->ip6_src)) {
		CTR3(KTR_MLD, "ignore v1 query src %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &ip6->ip6_src),
		    ifp, if_name(ifp));
		return (0);
	}

	is_general_query = 0;

	CTR2(KTR_MLD, "input v2 query on ifp %p(%s)", ifp, if_name(ifp));

	mld = (struct mldv2_query *)(mtod(m, uint8_t *) + off);

	maxdelay = ntohs(mld->mld_maxdelay);	/* in 1/10ths of a second */
	if (maxdelay >= 32768) {
		maxdelay = (MLD_MRC_MANT(maxdelay) | 0x1000) <<
			   (MLD_MRC_EXP(maxdelay) + 3);
	}
	timer = (maxdelay * PR_FASTHZ) / MLD_TIMER_SCALE;
	if (timer == 0)
		timer = 1;

	qrv = MLD_QRV(mld->mld_misc);
	if (qrv < 2) {
		CTR3(KTR_MLD, "%s: clamping qrv %d to %d", __func__,
		    qrv, MLD_RV_INIT);
		qrv = MLD_RV_INIT;
	}

	qqi = mld->mld_qqi;
	if (qqi >= 128) {
		qqi = MLD_QQIC_MANT(mld->mld_qqi) <<
		     (MLD_QQIC_EXP(mld->mld_qqi) + 3);
	}

	nsrc = ntohs(mld->mld_numsrc);
	if (nsrc > MLD_MAX_GS_SOURCES)
		return (EMSGSIZE);
	if (icmp6len < sizeof(struct mldv2_query) +
	    (nsrc * sizeof(struct in6_addr)))
		return (EMSGSIZE);

	/*
	 * Do further input validation upfront to avoid resetting timers
	 * should we need to discard this query.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&mld->mld_addr)) {
		/*
		 * A general query with a source list has undefined
		 * behaviour; discard it.
		 */
		if (nsrc > 0)
			return (EINVAL);
		is_general_query = 1;
	} else {
		/*
		 * Embed scope ID of receiving interface in MLD query for
		 * lookup whilst we don't hold other locks (due to KAME
		 * locking lameness). We own this mbuf chain just now.
		 */
		in6_setscope(&mld->mld_addr, ifp, NULL);
	}

	IN6_MULTI_LIST_LOCK();
	MLD_LOCK();

	mli = MLD_IFINFO(ifp);
	KASSERT(mli != NULL, ("%s: no mld_ifsoftc for ifp %p", __func__, ifp));

	/*
	 * Discard the v2 query if we're in Compatibility Mode.
	 * The RFC is pretty clear that hosts need to stay in MLDv1 mode
	 * until the Old Version Querier Present timer expires.
	 */
	if (mli->mli_version != MLD_VERSION_2)
		goto out_locked;

	mld_set_version(mli, MLD_VERSION_2);
	mli->mli_rv = qrv;
	mli->mli_qi = qqi;
	mli->mli_qri = maxdelay;

	CTR4(KTR_MLD, "%s: qrv %d qi %d maxdelay %d", __func__, qrv, qqi,
	    maxdelay);

	if (is_general_query) {
		/*
		 * MLDv2 General Query.
		 *
		 * Schedule a current-state report on this ifp for
		 * all groups, possibly containing source lists.
		 *
		 * If there is a pending General Query response
		 * scheduled earlier than the selected delay, do
		 * not schedule any other reports.
		 * Otherwise, reset the interface timer.
		 */
		CTR2(KTR_MLD, "process v2 general query on ifp %p(%s)",
		    ifp, if_name(ifp));
		if (mli->mli_v2_timer == 0 || mli->mli_v2_timer >= timer) {
			mli->mli_v2_timer = MLD_RANDOM_DELAY(timer);
			V_interface_timers_running6 = 1;
		}
	} else {
		struct epoch_tracker et;

		/*
		 * MLDv2 Group-specific or Group-and-source-specific Query.
		 *
		 * Group-source-specific queries are throttled on
		 * a per-group basis to defeat denial-of-service attempts.
		 * Queries for groups we are not a member of on this
		 * link are simply ignored.
		 */
		NET_EPOCH_ENTER(et);
		inm = in6m_lookup_locked(ifp, &mld->mld_addr);
		if (inm == NULL) {
			NET_EPOCH_EXIT(et);
			goto out_locked;
		}
		if (nsrc > 0) {
			if (!ratecheck(&inm->in6m_lastgsrtv,
			    &V_mld_gsrdelay)) {
				CTR1(KTR_MLD, "%s: GS query throttled.",
				    __func__);
				NET_EPOCH_EXIT(et);
				goto out_locked;
			}
		}
		CTR2(KTR_MLD, "process v2 group query on ifp %p(%s)",
		     ifp, if_name(ifp));
		/*
		 * If there is a pending General Query response
		 * scheduled sooner than the selected delay, no
		 * further report need be scheduled.
		 * Otherwise, prepare to respond to the
		 * group-specific or group-and-source query.
		 */
		if (mli->mli_v2_timer == 0 || mli->mli_v2_timer >= timer)
			mld_v2_process_group_query(inm, mli, timer, m, off);

		/* XXX Clear embedded scope ID as userland won't expect it. */
		in6_clearscope(&mld->mld_addr);
		NET_EPOCH_EXIT(et);
	}

out_locked:
	MLD_UNLOCK();
	IN6_MULTI_LIST_UNLOCK();

	return (0);
}

/*
 * Process a received MLDv2 group-specific or group-and-source-specific
 * query.
 * Return <0 if any error occurred. Currently this is ignored.
 */
static int
mld_v2_process_group_query(struct in6_multi *inm, struct mld_ifsoftc *mli,
    int timer, struct mbuf *m0, const int off)
{
	struct mldv2_query	*mld;
	int			 retval;
	uint16_t		 nsrc;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	retval = 0;
	mld = (struct mldv2_query *)(mtod(m0, uint8_t *) + off);

	switch (inm->in6m_state) {
	case MLD_NOT_MEMBER:
	case MLD_SILENT_MEMBER:
	case MLD_SLEEPING_MEMBER:
	case MLD_LAZY_MEMBER:
	case MLD_AWAKENING_MEMBER:
	case MLD_IDLE_MEMBER:
	case MLD_LEAVING_MEMBER:
		return (retval);
		break;
	case MLD_REPORTING_MEMBER:
	case MLD_G_QUERY_PENDING_MEMBER:
	case MLD_SG_QUERY_PENDING_MEMBER:
		break;
	}

	nsrc = ntohs(mld->mld_numsrc);

	/*
	 * Deal with group-specific queries upfront.
	 * If any group query is already pending, purge any recorded
	 * source-list state if it exists, and schedule a query response
	 * for this group-specific query.
	 */
	if (nsrc == 0) {
		if (inm->in6m_state == MLD_G_QUERY_PENDING_MEMBER ||
		    inm->in6m_state == MLD_SG_QUERY_PENDING_MEMBER) {
			in6m_clear_recorded(inm);
			timer = min(inm->in6m_timer, timer);
		}
		inm->in6m_state = MLD_G_QUERY_PENDING_MEMBER;
		inm->in6m_timer = MLD_RANDOM_DELAY(timer);
		V_current_state_timers_running6 = 1;
		return (retval);
	}

	/*
	 * Deal with the case where a group-and-source-specific query has
	 * been received but a group-specific query is already pending.
	 */
	if (inm->in6m_state == MLD_G_QUERY_PENDING_MEMBER) {
		timer = min(inm->in6m_timer, timer);
		inm->in6m_timer = MLD_RANDOM_DELAY(timer);
		V_current_state_timers_running6 = 1;
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
	 */
	if (inm->in6m_nsrc > 0) {
		struct mbuf		*m;
		uint8_t			*sp;
		int			 i, nrecorded;
		int			 soff;

		m = m0;
		soff = off + sizeof(struct mldv2_query);
		nrecorded = 0;
		for (i = 0; i < nsrc; i++) {
			sp = mtod(m, uint8_t *) + soff;
			retval = in6m_record_source(inm,
			    (const struct in6_addr *)sp);
			if (retval < 0)
				break;
			nrecorded += retval;
			soff += sizeof(struct in6_addr);
			if (soff >= m->m_len) {
				soff = soff - m->m_len;
				m = m->m_next;
				if (m == NULL)
					break;
			}
		}
		if (nrecorded > 0) {
			CTR1(KTR_MLD,
			    "%s: schedule response to SG query", __func__);
			inm->in6m_state = MLD_SG_QUERY_PENDING_MEMBER;
			inm->in6m_timer = MLD_RANDOM_DELAY(timer);
			V_current_state_timers_running6 = 1;
		}
	}

	return (retval);
}

/*
 * Process a received MLDv1 host membership report.
 * Assumes mld points to mld_hdr in pulled up mbuf chain.
 *
 * NOTE: Can't be fully const correct as we temporarily embed scope ID in
 * mld_addr. This is OK as we own the mbuf chain.
 */
static int
mld_v1_input_report(struct ifnet *ifp, const struct ip6_hdr *ip6,
    /*const*/ struct mld_hdr *mld)
{
	struct in6_addr		 src, dst;
	struct epoch_tracker	 et;
	struct in6_ifaddr	*ia;
	struct in6_multi	*inm;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	if (!mld_v1enable) {
		CTR3(KTR_MLD, "ignore v1 report %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &mld->mld_addr),
		    ifp, if_name(ifp));
		return (0);
	}

	if (ifp->if_flags & IFF_LOOPBACK)
		return (0);

	/*
	 * MLDv1 reports must originate from a host's link-local address,
	 * or the unspecified address (when booting).
	 */
	src = ip6->ip6_src;
	in6_clearscope(&src);
	if (!IN6_IS_SCOPE_LINKLOCAL(&src) && !IN6_IS_ADDR_UNSPECIFIED(&src)) {
		CTR3(KTR_MLD, "ignore v1 query src %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &ip6->ip6_src),
		    ifp, if_name(ifp));
		return (EINVAL);
	}

	/*
	 * RFC2710 Section 4: MLDv1 reports must pertain to a multicast
	 * group, and must be directed to the group itself.
	 */
	dst = ip6->ip6_dst;
	in6_clearscope(&dst);
	if (!IN6_IS_ADDR_MULTICAST(&mld->mld_addr) ||
	    !IN6_ARE_ADDR_EQUAL(&mld->mld_addr, &dst)) {
		CTR3(KTR_MLD, "ignore v1 query dst %s on ifp %p(%s)",
		    ip6_sprintf(ip6tbuf, &ip6->ip6_dst),
		    ifp, if_name(ifp));
		return (EINVAL);
	}

	/*
	 * Make sure we don't hear our own membership report, as fast
	 * leave requires knowing that we are the only member of a
	 * group. Assume we used the link-local address if available,
	 * otherwise look for ::.
	 *
	 * XXX Note that scope ID comparison is needed for the address
	 * returned by in6ifa_ifpforlinklocal(), but SHOULD NOT be
	 * performed for the on-wire address.
	 */
	ia = in6ifa_ifpforlinklocal(ifp, IN6_IFF_NOTREADY|IN6_IFF_ANYCAST);
	if ((ia && IN6_ARE_ADDR_EQUAL(&ip6->ip6_src, IA6_IN6(ia))) ||
	    (ia == NULL && IN6_IS_ADDR_UNSPECIFIED(&src))) {
		if (ia != NULL)
			ifa_free(&ia->ia_ifa);
		return (0);
	}
	if (ia != NULL)
		ifa_free(&ia->ia_ifa);

	CTR3(KTR_MLD, "process v1 report %s on ifp %p(%s)",
	    ip6_sprintf(ip6tbuf, &mld->mld_addr), ifp, if_name(ifp));

	/*
	 * Embed scope ID of receiving interface in MLD query for lookup
	 * whilst we don't hold other locks (due to KAME locking lameness).
	 */
	if (!IN6_IS_ADDR_UNSPECIFIED(&mld->mld_addr))
		in6_setscope(&mld->mld_addr, ifp, NULL);

	IN6_MULTI_LIST_LOCK();
	MLD_LOCK();
	NET_EPOCH_ENTER(et);

	/*
	 * MLDv1 report suppression.
	 * If we are a member of this group, and our membership should be
	 * reported, and our group timer is pending or about to be reset,
	 * stop our group timer by transitioning to the 'lazy' state.
	 */
	inm = in6m_lookup_locked(ifp, &mld->mld_addr);
	if (inm != NULL) {
		struct mld_ifsoftc *mli;

		mli = inm->in6m_mli;
		KASSERT(mli != NULL,
		    ("%s: no mli for ifp %p", __func__, ifp));

		/*
		 * If we are in MLDv2 host mode, do not allow the
		 * other host's MLDv1 report to suppress our reports.
		 */
		if (mli->mli_version == MLD_VERSION_2)
			goto out_locked;

		inm->in6m_timer = 0;

		switch (inm->in6m_state) {
		case MLD_NOT_MEMBER:
		case MLD_SILENT_MEMBER:
		case MLD_SLEEPING_MEMBER:
			break;
		case MLD_REPORTING_MEMBER:
		case MLD_IDLE_MEMBER:
		case MLD_AWAKENING_MEMBER:
			CTR3(KTR_MLD,
			    "report suppressed for %s on ifp %p(%s)",
			    ip6_sprintf(ip6tbuf, &mld->mld_addr),
			    ifp, if_name(ifp));
		case MLD_LAZY_MEMBER:
			inm->in6m_state = MLD_LAZY_MEMBER;
			break;
		case MLD_G_QUERY_PENDING_MEMBER:
		case MLD_SG_QUERY_PENDING_MEMBER:
		case MLD_LEAVING_MEMBER:
			break;
		}
	}

out_locked:
	NET_EPOCH_EXIT(et);
	MLD_UNLOCK();
	IN6_MULTI_LIST_UNLOCK();

	/* XXX Clear embedded scope ID as userland won't expect it. */
	in6_clearscope(&mld->mld_addr);

	return (0);
}

/*
 * MLD input path.
 *
 * Assume query messages which fit in a single ICMPv6 message header
 * have been pulled up.
 * Assume that userland will want to see the message, even if it
 * otherwise fails kernel input validation; do not free it.
 * Pullup may however free the mbuf chain m if it fails.
 *
 * Return IPPROTO_DONE if we freed m. Otherwise, return 0.
 */
int
mld_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet	*ifp;
	struct ip6_hdr	*ip6;
	struct mld_hdr	*mld;
	int		 mldlen;

	CTR3(KTR_MLD, "%s: called w/mbuf (%p,%d)", __func__, m, off);

	ifp = m->m_pkthdr.rcvif;

	ip6 = mtod(m, struct ip6_hdr *);

	/* Pullup to appropriate size. */
	mld = (struct mld_hdr *)(mtod(m, uint8_t *) + off);
	if (mld->mld_type == MLD_LISTENER_QUERY &&
	    icmp6len >= sizeof(struct mldv2_query)) {
		mldlen = sizeof(struct mldv2_query);
	} else {
		mldlen = sizeof(struct mld_hdr);
	}
	IP6_EXTHDR_GET(mld, struct mld_hdr *, m, off, mldlen);
	if (mld == NULL) {
		ICMP6STAT_INC(icp6s_badlen);
		return (IPPROTO_DONE);
	}

	/*
	 * Userland needs to see all of this traffic for implementing
	 * the endpoint discovery portion of multicast routing.
	 */
	switch (mld->mld_type) {
	case MLD_LISTENER_QUERY:
		icmp6_ifstat_inc(ifp, ifs6_in_mldquery);
		if (icmp6len == sizeof(struct mld_hdr)) {
			if (mld_v1_input_query(ifp, ip6, mld) != 0)
				return (0);
		} else if (icmp6len >= sizeof(struct mldv2_query)) {
			if (mld_v2_input_query(ifp, ip6, m, off,
			    icmp6len) != 0)
				return (0);
		}
		break;
	case MLD_LISTENER_REPORT:
		icmp6_ifstat_inc(ifp, ifs6_in_mldreport);
		if (mld_v1_input_report(ifp, ip6, mld) != 0)
			return (0);
		break;
	case MLDV2_LISTENER_REPORT:
		icmp6_ifstat_inc(ifp, ifs6_in_mldreport);
		break;
	case MLD_LISTENER_DONE:
		icmp6_ifstat_inc(ifp, ifs6_in_mlddone);
		break;
	default:
		break;
	}

	return (0);
}

/*
 * Fast timeout handler (global).
 * VIMAGE: Timeout handlers are expected to service all vimages.
 */
void
mld_fasttimo(void)
{
	struct in6_multi_head inmh;
	VNET_ITERATOR_DECL(vnet_iter);

	SLIST_INIT(&inmh);
	
	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		mld_fasttimo_vnet(&inmh);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
	in6m_release_list_deferred(&inmh);
}

/*
 * Fast timeout handler (per-vnet).
 *
 * VIMAGE: Assume caller has set up our curvnet.
 */
static void
mld_fasttimo_vnet(struct in6_multi_head *inmh)
{
	struct epoch_tracker     et;
	struct mbufq		 scq;	/* State-change packets */
	struct mbufq		 qrq;	/* Query response packets */
	struct ifnet		*ifp;
	struct mld_ifsoftc	*mli;
	struct ifmultiaddr	*ifma;
	struct in6_multi	*inm;
	int			 uri_fasthz;

	uri_fasthz = 0;

	/*
	 * Quick check to see if any work needs to be done, in order to
	 * minimize the overhead of fasttimo processing.
	 * SMPng: XXX Unlocked reads.
	 */
	if (!V_current_state_timers_running6 &&
	    !V_interface_timers_running6 &&
	    !V_state_change_timers_running6)
		return;

	IN6_MULTI_LIST_LOCK();
	MLD_LOCK();

	/*
	 * MLDv2 General Query response timer processing.
	 */
	if (V_interface_timers_running6) {
		CTR1(KTR_MLD, "%s: interface timers running", __func__);

		V_interface_timers_running6 = 0;
		LIST_FOREACH(mli, &V_mli_head, mli_link) {
			if (mli->mli_v2_timer == 0) {
				/* Do nothing. */
			} else if (--mli->mli_v2_timer == 0) {
				mld_v2_dispatch_general_query(mli);
			} else {
				V_interface_timers_running6 = 1;
			}
		}
	}

	if (!V_current_state_timers_running6 &&
	    !V_state_change_timers_running6)
		goto out_locked;

	V_current_state_timers_running6 = 0;
	V_state_change_timers_running6 = 0;

	CTR1(KTR_MLD, "%s: state change timers running", __func__);

	/*
	 * MLD host report and state-change timer processing.
	 * Note: Processing a v2 group timer may remove a node.
	 */
	LIST_FOREACH(mli, &V_mli_head, mli_link) {
		ifp = mli->mli_ifp;

		if (mli->mli_version == MLD_VERSION_2) {
			uri_fasthz = MLD_RANDOM_DELAY(mli->mli_uri *
			    PR_FASTHZ);
			mbufq_init(&qrq, MLD_MAX_G_GS_PACKETS);
			mbufq_init(&scq, MLD_MAX_STATE_CHANGE_PACKETS);
		}

		IF_ADDR_WLOCK(ifp);
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			inm = in6m_ifmultiaddr_get_inm(ifma);
			if (inm == NULL)
				continue;
			switch (mli->mli_version) {
			case MLD_VERSION_1:
				mld_v1_process_group_timer(inmh, inm);
				break;
			case MLD_VERSION_2:
				mld_v2_process_group_timers(inmh, &qrq,
				    &scq, inm, uri_fasthz);
				break;
			}
		}
		IF_ADDR_WUNLOCK(ifp);

		switch (mli->mli_version) {
		case MLD_VERSION_1:
			/*
			 * Transmit reports for this lifecycle.  This
			 * is done while not holding IF_ADDR_LOCK
			 * since this can call
			 * in6ifa_ifpforlinklocal() which locks
			 * IF_ADDR_LOCK internally as well as
			 * ip6_output() to transmit a packet.
			 */
			while ((inm = SLIST_FIRST(inmh)) != NULL) {
				SLIST_REMOVE_HEAD(inmh, in6m_defer);
				(void)mld_v1_transmit_report(inm,
				    MLD_LISTENER_REPORT);
			}
			break;
		case MLD_VERSION_2:
			mld_dispatch_queue(&qrq, 0);
			mld_dispatch_queue(&scq, 0);
			break;
		}
		NET_EPOCH_EXIT(et);
	}

out_locked:
	MLD_UNLOCK();
	IN6_MULTI_LIST_UNLOCK();
}

/*
 * Update host report group timer.
 * Will update the global pending timer flags.
 */
static void
mld_v1_process_group_timer(struct in6_multi_head *inmh, struct in6_multi *inm)
{
	int report_timer_expired;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	if (inm->in6m_timer == 0) {
		report_timer_expired = 0;
	} else if (--inm->in6m_timer == 0) {
		report_timer_expired = 1;
	} else {
		V_current_state_timers_running6 = 1;
		return;
	}

	switch (inm->in6m_state) {
	case MLD_NOT_MEMBER:
	case MLD_SILENT_MEMBER:
	case MLD_IDLE_MEMBER:
	case MLD_LAZY_MEMBER:
	case MLD_SLEEPING_MEMBER:
	case MLD_AWAKENING_MEMBER:
		break;
	case MLD_REPORTING_MEMBER:
		if (report_timer_expired) {
			inm->in6m_state = MLD_IDLE_MEMBER;
			SLIST_INSERT_HEAD(inmh, inm, in6m_defer);
		}
		break;
	case MLD_G_QUERY_PENDING_MEMBER:
	case MLD_SG_QUERY_PENDING_MEMBER:
	case MLD_LEAVING_MEMBER:
		break;
	}
}

/*
 * Update a group's timers for MLDv2.
 * Will update the global pending timer flags.
 * Note: Unlocked read from mli.
 */
static void
mld_v2_process_group_timers(struct in6_multi_head *inmh,
    struct mbufq *qrq, struct mbufq *scq,
    struct in6_multi *inm, const int uri_fasthz)
{
	int query_response_timer_expired;
	int state_change_retransmit_timer_expired;
#ifdef KTR
	char ip6tbuf[INET6_ADDRSTRLEN];
#endif

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	query_response_timer_expired = 0;
	state_change_retransmit_timer_expired = 0;

	/*
	 * During a transition from compatibility mode back to MLDv2,
	 * a group record in REPORTING state may still have its group
	 * timer active. This is a no-op in this function; it is easier
	 * to deal with it here than to complicate the slow-timeout path.
	 */
	if (inm->in6m_timer == 0) {
		query_response_timer_expired = 0;
	} else if (--inm->in6m_timer == 0) {
		query_response_timer_expired = 1;
	} else {
		V_current_state_timers_running6 = 1;
	}

	if (inm->in6m_sctimer == 0) {
		state_change_retransmit_timer_expired = 0;
	} else if (--inm->in6m_sctimer == 0) {
		state_change_retransmit_timer_expired = 1;
	} else {
		V_state_change_timers_running6 = 1;
	}

	/* We are in fasttimo, so be quick about it. */
	if (!state_change_retransmit_timer_expired &&
	    !query_response_timer_expired)
		return;

	switch (inm->in6m_state) {
	case MLD_NOT_MEMBER:
	case MLD_SILENT_MEMBER:
	case MLD_SLEEPING_MEMBER:
	case MLD_LAZY_MEMBER:
	case MLD_AWAKENING_MEMBER:
	case MLD_IDLE_MEMBER:
		break;
	case MLD_G_QUERY_PENDING_MEMBER:
	case MLD_SG_QUERY_PENDING_MEMBER:
		/*
		 * Respond to a previously pending Group-Specific
		 * or Group-and-Source-Specific query by enqueueing
		 * the appropriate Current-State report for
		 * immediate transmission.
		 */
		if (query_response_timer_expired) {
			int retval;

			retval = mld_v2_enqueue_group_record(qrq, inm, 0, 1,
			    (inm->in6m_state == MLD_SG_QUERY_PENDING_MEMBER),
			    0);
			CTR2(KTR_MLD, "%s: enqueue record = %d",
			    __func__, retval);
			inm->in6m_state = MLD_REPORTING_MEMBER;
			in6m_clear_recorded(inm);
		}
		/* FALLTHROUGH */
	case MLD_REPORTING_MEMBER:
	case MLD_LEAVING_MEMBER:
		if (state_change_retransmit_timer_expired) {
			/*
			 * State-change retransmission timer fired.
			 * If there are any further pending retransmissions,
			 * set the global pending state-change flag, and
			 * reset the timer.
			 */
			if (--inm->in6m_scrv > 0) {
				inm->in6m_sctimer = uri_fasthz;
				V_state_change_timers_running6 = 1;
			}
			/*
			 * Retransmit the previously computed state-change
			 * report. If there are no further pending
			 * retransmissions, the mbuf queue will be consumed.
			 * Update T0 state to T1 as we have now sent
			 * a state-change.
			 */
			(void)mld_v2_merge_state_changes(inm, scq);

			in6m_commit(inm);
			CTR3(KTR_MLD, "%s: T1 -> T0 for %s/%s", __func__,
			    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
			    if_name(inm->in6m_ifp));

			/*
			 * If we are leaving the group for good, make sure
			 * we release MLD's reference to it.
			 * This release must be deferred using a SLIST,
			 * as we are called from a loop which traverses
			 * the in_ifmultiaddr TAILQ.
			 */
			if (inm->in6m_state == MLD_LEAVING_MEMBER &&
			    inm->in6m_scrv == 0) {
				inm->in6m_state = MLD_NOT_MEMBER;
				in6m_disconnect_locked(inmh, inm);
				in6m_rele_locked(inmh, inm);
			}
		}
		break;
	}
}

/*
 * Switch to a different version on the given interface,
 * as per Section 9.12.
 */
static void
mld_set_version(struct mld_ifsoftc *mli, const int version)
{
	int old_version_timer;

	MLD_LOCK_ASSERT();

	CTR4(KTR_MLD, "%s: switching to v%d on ifp %p(%s)", __func__,
	    version, mli->mli_ifp, if_name(mli->mli_ifp));

	if (version == MLD_VERSION_1) {
		/*
		 * Compute the "Older Version Querier Present" timer as per
		 * Section 9.12.
		 */
		old_version_timer = (mli->mli_rv * mli->mli_qi) + mli->mli_qri;
		old_version_timer *= PR_SLOWHZ;
		mli->mli_v1_timer = old_version_timer;
	}

	if (mli->mli_v1_timer > 0 && mli->mli_version != MLD_VERSION_1) {
		mli->mli_version = MLD_VERSION_1;
		mld_v2_cancel_link_timers(mli);
	}
}

/*
 * Cancel pending MLDv2 timers for the given link and all groups
 * joined on it; state-change, general-query, and group-query timers.
 */
static void
mld_v2_cancel_link_timers(struct mld_ifsoftc *mli)
{
	struct epoch_tracker	 et;
	struct in6_multi_head	 inmh;
	struct ifmultiaddr	*ifma;
	struct ifnet		*ifp;
	struct in6_multi	*inm;

	CTR3(KTR_MLD, "%s: cancel v2 timers on ifp %p(%s)", __func__,
	    mli->mli_ifp, if_name(mli->mli_ifp));

	SLIST_INIT(&inmh);
	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	/*
	 * Fast-track this potentially expensive operation
	 * by checking all the global 'timer pending' flags.
	 */
	if (!V_interface_timers_running6 &&
	    !V_state_change_timers_running6 &&
	    !V_current_state_timers_running6)
		return;

	mli->mli_v2_timer = 0;

	ifp = mli->mli_ifp;

	IF_ADDR_WLOCK(ifp);
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		inm = in6m_ifmultiaddr_get_inm(ifma);
		if (inm == NULL)
			continue;
		switch (inm->in6m_state) {
		case MLD_NOT_MEMBER:
		case MLD_SILENT_MEMBER:
		case MLD_IDLE_MEMBER:
		case MLD_LAZY_MEMBER:
		case MLD_SLEEPING_MEMBER:
		case MLD_AWAKENING_MEMBER:
			break;
		case MLD_LEAVING_MEMBER:
			/*
			 * If we are leaving the group and switching
			 * version, we need to release the final
			 * reference held for issuing the INCLUDE {}.
			 */
			in6m_rele_locked(&inmh, inm);
			/* FALLTHROUGH */
		case MLD_G_QUERY_PENDING_MEMBER:
		case MLD_SG_QUERY_PENDING_MEMBER:
			in6m_clear_recorded(inm);
			/* FALLTHROUGH */
		case MLD_REPORTING_MEMBER:
			inm->in6m_sctimer = 0;
			inm->in6m_timer = 0;
			inm->in6m_state = MLD_REPORTING_MEMBER;
			/*
			 * Free any pending MLDv2 state-change records.
			 */
			mbufq_drain(&inm->in6m_scq);
			break;
		}
	}
	NET_EPOCH_EXIT(et);
	IF_ADDR_WUNLOCK(ifp);
	in6m_release_list_deferred(&inmh);
}

/*
 * Global slowtimo handler.
 * VIMAGE: Timeout handlers are expected to service all vimages.
 */
void
mld_slowtimo(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		mld_slowtimo_vnet();
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * Per-vnet slowtimo handler.
 */
static void
mld_slowtimo_vnet(void)
{
	struct mld_ifsoftc *mli;

	MLD_LOCK();

	LIST_FOREACH(mli, &V_mli_head, mli_link) {
		mld_v1_process_querier_timers(mli);
	}

	MLD_UNLOCK();
}

/*
 * Update the Older Version Querier Present timers for a link.
 * See Section 9.12 of RFC 3810.
 */
static void
mld_v1_process_querier_timers(struct mld_ifsoftc *mli)
{

	MLD_LOCK_ASSERT();

	if (mli->mli_version != MLD_VERSION_2 && --mli->mli_v1_timer == 0) {
		/*
		 * MLDv1 Querier Present timer expired; revert to MLDv2.
		 */
		CTR5(KTR_MLD,
		    "%s: transition from v%d -> v%d on %p(%s)",
		    __func__, mli->mli_version, MLD_VERSION_2,
		    mli->mli_ifp, if_name(mli->mli_ifp));
		mli->mli_version = MLD_VERSION_2;
	}
}

/*
 * Transmit an MLDv1 report immediately.
 */
static int
mld_v1_transmit_report(struct in6_multi *in6m, const int type)
{
	struct ifnet		*ifp;
	struct in6_ifaddr	*ia;
	struct ip6_hdr		*ip6;
	struct mbuf		*mh, *md;
	struct mld_hdr		*mld;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();
	
	ifp = in6m->in6m_ifp;
	/* in process of being freed */
	if (ifp == NULL)
		return (0);
	ia = in6ifa_ifpforlinklocal(ifp, IN6_IFF_NOTREADY|IN6_IFF_ANYCAST);
	/* ia may be NULL if link-local address is tentative. */

	mh = m_gethdr(M_NOWAIT, MT_DATA);
	if (mh == NULL) {
		if (ia != NULL)
			ifa_free(&ia->ia_ifa);
		return (ENOMEM);
	}
	md = m_get(M_NOWAIT, MT_DATA);
	if (md == NULL) {
		m_free(mh);
		if (ia != NULL)
			ifa_free(&ia->ia_ifa);
		return (ENOMEM);
	}
	mh->m_next = md;

	/*
	 * FUTURE: Consider increasing alignment by ETHER_HDR_LEN, so
	 * that ether_output() does not need to allocate another mbuf
	 * for the header in the most common case.
	 */
	M_ALIGN(mh, sizeof(struct ip6_hdr));
	mh->m_pkthdr.len = sizeof(struct ip6_hdr) + sizeof(struct mld_hdr);
	mh->m_len = sizeof(struct ip6_hdr);

	ip6 = mtod(mh, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_src = ia ? ia->ia_addr.sin6_addr : in6addr_any;
	ip6->ip6_dst = in6m->in6m_addr;

	md->m_len = sizeof(struct mld_hdr);
	mld = mtod(md, struct mld_hdr *);
	mld->mld_type = type;
	mld->mld_code = 0;
	mld->mld_cksum = 0;
	mld->mld_maxdelay = 0;
	mld->mld_reserved = 0;
	mld->mld_addr = in6m->in6m_addr;
	in6_clearscope(&mld->mld_addr);
	mld->mld_cksum = in6_cksum(mh, IPPROTO_ICMPV6,
	    sizeof(struct ip6_hdr), sizeof(struct mld_hdr));

	mld_save_context(mh, ifp);
	mh->m_flags |= M_MLDV1;

	mld_dispatch_packet(mh);

	if (ia != NULL)
		ifa_free(&ia->ia_ifa);
	return (0);
}

/*
 * Process a state change from the upper layer for the given IPv6 group.
 *
 * Each socket holds a reference on the in_multi in its own ip_moptions.
 * The socket layer will have made the necessary updates to.the group
 * state, it is now up to MLD to issue a state change report if there
 * has been any change between T0 (when the last state-change was issued)
 * and T1 (now).
 *
 * We use the MLDv2 state machine at group level. The MLd module
 * however makes the decision as to which MLD protocol version to speak.
 * A state change *from* INCLUDE {} always means an initial join.
 * A state change *to* INCLUDE {} always means a final leave.
 *
 * If delay is non-zero, and the state change is an initial multicast
 * join, the state change report will be delayed by 'delay' ticks
 * in units of PR_FASTHZ if MLDv1 is active on the link; otherwise
 * the initial MLDv2 state change report will be delayed by whichever
 * is sooner, a pending state-change timer or delay itself.
 *
 * VIMAGE: curvnet should have been set by caller, as this routine
 * is called from the socket option handlers.
 */
int
mld_change_state(struct in6_multi *inm, const int delay)
{
	struct mld_ifsoftc *mli;
	struct ifnet *ifp;
	int error;

	IN6_MULTI_LIST_LOCK_ASSERT();

	error = 0;

	/*
	 * Check if the in6_multi has already been disconnected.
	 */
	if (inm->in6m_ifp == NULL) {
		CTR1(KTR_MLD, "%s: inm is disconnected", __func__);
		return (0);
	}

	/*
	 * Try to detect if the upper layer just asked us to change state
	 * for an interface which has now gone away.
	 */
	KASSERT(inm->in6m_ifma != NULL, ("%s: no ifma", __func__));
	ifp = inm->in6m_ifma->ifma_ifp;
	if (ifp == NULL)
		return (0);
	/*
	 * Sanity check that netinet6's notion of ifp is the
	 * same as net's.
	 */
	KASSERT(inm->in6m_ifp == ifp, ("%s: bad ifp", __func__));

	MLD_LOCK();
	mli = MLD_IFINFO(ifp);
	KASSERT(mli != NULL, ("%s: no mld_ifsoftc for ifp %p", __func__, ifp));

	/*
	 * If we detect a state transition to or from MCAST_UNDEFINED
	 * for this group, then we are starting or finishing an MLD
	 * life cycle for this group.
	 */
	if (inm->in6m_st[1].iss_fmode != inm->in6m_st[0].iss_fmode) {
		CTR3(KTR_MLD, "%s: inm transition %d -> %d", __func__,
		    inm->in6m_st[0].iss_fmode, inm->in6m_st[1].iss_fmode);
		if (inm->in6m_st[0].iss_fmode == MCAST_UNDEFINED) {
			CTR1(KTR_MLD, "%s: initial join", __func__);
			error = mld_initial_join(inm, mli, delay);
			goto out_locked;
		} else if (inm->in6m_st[1].iss_fmode == MCAST_UNDEFINED) {
			CTR1(KTR_MLD, "%s: final leave", __func__);
			mld_final_leave(inm, mli);
			goto out_locked;
		}
	} else {
		CTR1(KTR_MLD, "%s: filter set change", __func__);
	}

	error = mld_handle_state_change(inm, mli);

out_locked:
	MLD_UNLOCK();
	return (error);
}

/*
 * Perform the initial join for an MLD group.
 *
 * When joining a group:
 *  If the group should have its MLD traffic suppressed, do nothing.
 *  MLDv1 starts sending MLDv1 host membership reports.
 *  MLDv2 will schedule an MLDv2 state-change report containing the
 *  initial state of the membership.
 *
 * If the delay argument is non-zero, then we must delay sending the
 * initial state change for delay ticks (in units of PR_FASTHZ).
 */
static int
mld_initial_join(struct in6_multi *inm, struct mld_ifsoftc *mli,
    const int delay)
{
	struct ifnet		*ifp;
	struct mbufq		*mq;
	int			 error, retval, syncstates;
	int			 odelay;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	CTR4(KTR_MLD, "%s: initial join %s on ifp %p(%s)",
	    __func__, ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    inm->in6m_ifp, if_name(inm->in6m_ifp));

	error = 0;
	syncstates = 1;

	ifp = inm->in6m_ifp;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	KASSERT(mli && mli->mli_ifp == ifp, ("%s: inconsistent ifp", __func__));

	/*
	 * Groups joined on loopback or marked as 'not reported',
	 * enter the MLD_SILENT_MEMBER state and
	 * are never reported in any protocol exchanges.
	 * All other groups enter the appropriate state machine
	 * for the version in use on this link.
	 * A link marked as MLIF_SILENT causes MLD to be completely
	 * disabled for the link.
	 */
	if ((ifp->if_flags & IFF_LOOPBACK) ||
	    (mli->mli_flags & MLIF_SILENT) ||
	    !mld_is_addr_reported(&inm->in6m_addr)) {
		CTR1(KTR_MLD,
"%s: not kicking state machine for silent group", __func__);
		inm->in6m_state = MLD_SILENT_MEMBER;
		inm->in6m_timer = 0;
	} else {
		/*
		 * Deal with overlapping in_multi lifecycle.
		 * If this group was LEAVING, then make sure
		 * we drop the reference we picked up to keep the
		 * group around for the final INCLUDE {} enqueue.
		 */
		if (mli->mli_version == MLD_VERSION_2 &&
		    inm->in6m_state == MLD_LEAVING_MEMBER) {
			inm->in6m_refcount--;
			MPASS(inm->in6m_refcount > 0);
		}
		inm->in6m_state = MLD_REPORTING_MEMBER;

		switch (mli->mli_version) {
		case MLD_VERSION_1:
			/*
			 * If a delay was provided, only use it if
			 * it is greater than the delay normally
			 * used for an MLDv1 state change report,
			 * and delay sending the initial MLDv1 report
			 * by not transitioning to the IDLE state.
			 */
			odelay = MLD_RANDOM_DELAY(MLD_V1_MAX_RI * PR_FASTHZ);
			if (delay) {
				inm->in6m_timer = max(delay, odelay);
				V_current_state_timers_running6 = 1;
			} else {
				inm->in6m_state = MLD_IDLE_MEMBER;
				error = mld_v1_transmit_report(inm,
				     MLD_LISTENER_REPORT);
				if (error == 0) {
					inm->in6m_timer = odelay;
					V_current_state_timers_running6 = 1;
				}
			}
			break;

		case MLD_VERSION_2:
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
			mq = &inm->in6m_scq;
			mbufq_drain(mq);
			retval = mld_v2_enqueue_group_record(mq, inm, 1,
			    0, 0, (mli->mli_flags & MLIF_USEALLOW));
			CTR2(KTR_MLD, "%s: enqueue record = %d",
			    __func__, retval);
			if (retval <= 0) {
				error = retval * -1;
				break;
			}

			/*
			 * Schedule transmission of pending state-change
			 * report up to RV times for this link. The timer
			 * will fire at the next mld_fasttimo (~200ms),
			 * giving us an opportunity to merge the reports.
			 *
			 * If a delay was provided to this function, only
			 * use this delay if sooner than the existing one.
			 */
			KASSERT(mli->mli_rv > 1,
			   ("%s: invalid robustness %d", __func__,
			    mli->mli_rv));
			inm->in6m_scrv = mli->mli_rv;
			if (delay) {
				if (inm->in6m_sctimer > 1) {
					inm->in6m_sctimer =
					    min(inm->in6m_sctimer, delay);
				} else
					inm->in6m_sctimer = delay;
			} else
				inm->in6m_sctimer = 1;
			V_state_change_timers_running6 = 1;

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
		in6m_commit(inm);
		CTR3(KTR_MLD, "%s: T1 -> T0 for %s/%s", __func__,
		    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
		    if_name(inm->in6m_ifp));
	}

	return (error);
}

/*
 * Issue an intermediate state change during the life-cycle.
 */
static int
mld_handle_state_change(struct in6_multi *inm, struct mld_ifsoftc *mli)
{
	struct ifnet		*ifp;
	int			 retval;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	CTR4(KTR_MLD, "%s: state change for %s on ifp %p(%s)",
	    __func__, ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    inm->in6m_ifp, if_name(inm->in6m_ifp));

	ifp = inm->in6m_ifp;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	KASSERT(mli && mli->mli_ifp == ifp,
	    ("%s: inconsistent ifp", __func__));

	if ((ifp->if_flags & IFF_LOOPBACK) ||
	    (mli->mli_flags & MLIF_SILENT) ||
	    !mld_is_addr_reported(&inm->in6m_addr) ||
	    (mli->mli_version != MLD_VERSION_2)) {
		if (!mld_is_addr_reported(&inm->in6m_addr)) {
			CTR1(KTR_MLD,
"%s: not kicking state machine for silent group", __func__);
		}
		CTR1(KTR_MLD, "%s: nothing to do", __func__);
		in6m_commit(inm);
		CTR3(KTR_MLD, "%s: T1 -> T0 for %s/%s", __func__,
		    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
		    if_name(inm->in6m_ifp));
		return (0);
	}

	mbufq_drain(&inm->in6m_scq);

	retval = mld_v2_enqueue_group_record(&inm->in6m_scq, inm, 1, 0, 0,
	    (mli->mli_flags & MLIF_USEALLOW));
	CTR2(KTR_MLD, "%s: enqueue record = %d", __func__, retval);
	if (retval <= 0)
		return (-retval);

	/*
	 * If record(s) were enqueued, start the state-change
	 * report timer for this group.
	 */
	inm->in6m_scrv = mli->mli_rv;
	inm->in6m_sctimer = 1;
	V_state_change_timers_running6 = 1;

	return (0);
}

/*
 * Perform the final leave for a multicast address.
 *
 * When leaving a group:
 *  MLDv1 sends a DONE message, if and only if we are the reporter.
 *  MLDv2 enqueues a state-change report containing a transition
 *  to INCLUDE {} for immediate transmission.
 */
static void
mld_final_leave(struct in6_multi *inm, struct mld_ifsoftc *mli)
{
	int syncstates;
#ifdef KTR
	char ip6tbuf[INET6_ADDRSTRLEN];
#endif

	syncstates = 1;

	CTR4(KTR_MLD, "%s: final leave %s on ifp %p(%s)",
	    __func__, ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    inm->in6m_ifp, if_name(inm->in6m_ifp));

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	switch (inm->in6m_state) {
	case MLD_NOT_MEMBER:
	case MLD_SILENT_MEMBER:
	case MLD_LEAVING_MEMBER:
		/* Already leaving or left; do nothing. */
		CTR1(KTR_MLD,
"%s: not kicking state machine for silent group", __func__);
		break;
	case MLD_REPORTING_MEMBER:
	case MLD_IDLE_MEMBER:
	case MLD_G_QUERY_PENDING_MEMBER:
	case MLD_SG_QUERY_PENDING_MEMBER:
		if (mli->mli_version == MLD_VERSION_1) {
#ifdef INVARIANTS
			if (inm->in6m_state == MLD_G_QUERY_PENDING_MEMBER ||
			    inm->in6m_state == MLD_SG_QUERY_PENDING_MEMBER)
			panic("%s: MLDv2 state reached, not MLDv2 mode",
			     __func__);
#endif
			mld_v1_transmit_report(inm, MLD_LISTENER_DONE);
			inm->in6m_state = MLD_NOT_MEMBER;
			V_current_state_timers_running6 = 1;
		} else if (mli->mli_version == MLD_VERSION_2) {
			/*
			 * Stop group timer and all pending reports.
			 * Immediately enqueue a state-change report
			 * TO_IN {} to be sent on the next fast timeout,
			 * giving us an opportunity to merge reports.
			 */
			mbufq_drain(&inm->in6m_scq);
			inm->in6m_timer = 0;
			inm->in6m_scrv = mli->mli_rv;
			CTR4(KTR_MLD, "%s: Leaving %s/%s with %d "
			    "pending retransmissions.", __func__,
			    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
			    if_name(inm->in6m_ifp), inm->in6m_scrv);
			if (inm->in6m_scrv == 0) {
				inm->in6m_state = MLD_NOT_MEMBER;
				inm->in6m_sctimer = 0;
			} else {
				int retval;

				in6m_acquire_locked(inm);

				retval = mld_v2_enqueue_group_record(
				    &inm->in6m_scq, inm, 1, 0, 0,
				    (mli->mli_flags & MLIF_USEALLOW));
				KASSERT(retval != 0,
				    ("%s: enqueue record = %d", __func__,
				     retval));

				inm->in6m_state = MLD_LEAVING_MEMBER;
				inm->in6m_sctimer = 1;
				V_state_change_timers_running6 = 1;
				syncstates = 0;
			}
			break;
		}
		break;
	case MLD_LAZY_MEMBER:
	case MLD_SLEEPING_MEMBER:
	case MLD_AWAKENING_MEMBER:
		/* Our reports are suppressed; do nothing. */
		break;
	}

	if (syncstates) {
		in6m_commit(inm);
		CTR3(KTR_MLD, "%s: T1 -> T0 for %s/%s", __func__,
		    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
		    if_name(inm->in6m_ifp));
		inm->in6m_st[1].iss_fmode = MCAST_UNDEFINED;
		CTR3(KTR_MLD, "%s: T1 now MCAST_UNDEFINED for %p/%s",
		    __func__, &inm->in6m_addr, if_name(inm->in6m_ifp));
	}
}

/*
 * Enqueue an MLDv2 group record to the given output queue.
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
 * If use_block_allow is non-zero, state change reports for initial join
 * and final leave, on an inclusive mode group with a source list, will be
 * rewritten to use the ALLOW_NEW and BLOCK_OLD record types, respectively.
 *
 * The function will attempt to allocate leading space in the packet
 * for the IPv6+ICMP headers to be prepended without fragmenting the chain.
 *
 * If successful the size of all data appended to the queue is returned,
 * otherwise an error code less than zero is returned, or zero if
 * no record(s) were appended.
 */
static int
mld_v2_enqueue_group_record(struct mbufq *mq, struct in6_multi *inm,
    const int is_state_change, const int is_group_query,
    const int is_source_query, const int use_block_allow)
{
	struct mldv2_record	 mr;
	struct mldv2_record	*pmr;
	struct ifnet		*ifp;
	struct ip6_msource	*ims, *nims;
	struct mbuf		*m0, *m, *md;
	int			 is_filter_list_change;
	int			 minrec0len, m0srcs, msrcs, nbytes, off;
	int			 record_has_sources;
	int			 now;
	int			 type;
	uint8_t			 mode;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	IN6_MULTI_LIST_LOCK_ASSERT();

	ifp = inm->in6m_ifp;
	is_filter_list_change = 0;
	m = NULL;
	m0 = NULL;
	m0srcs = 0;
	msrcs = 0;
	nbytes = 0;
	nims = NULL;
	record_has_sources = 1;
	pmr = NULL;
	type = MLD_DO_NOTHING;
	mode = inm->in6m_st[1].iss_fmode;

	/*
	 * If we did not transition out of ASM mode during t0->t1,
	 * and there are no source nodes to process, we can skip
	 * the generation of source records.
	 */
	if (inm->in6m_st[0].iss_asm > 0 && inm->in6m_st[1].iss_asm > 0 &&
	    inm->in6m_nsrc == 0)
		record_has_sources = 0;

	if (is_state_change) {
		/*
		 * Queue a state change record.
		 * If the mode did not change, and there are non-ASM
		 * listeners or source filters present,
		 * we potentially need to issue two records for the group.
		 * If there are ASM listeners, and there was no filter
		 * mode transition of any kind, do nothing.
		 *
		 * If we are transitioning to MCAST_UNDEFINED, we need
		 * not send any sources. A transition to/from this state is
		 * considered inclusive with some special treatment.
		 *
		 * If we are rewriting initial joins/leaves to use
		 * ALLOW/BLOCK, and the group's membership is inclusive,
		 * we need to send sources in all cases.
		 */
		if (mode != inm->in6m_st[0].iss_fmode) {
			if (mode == MCAST_EXCLUDE) {
				CTR1(KTR_MLD, "%s: change to EXCLUDE",
				    __func__);
				type = MLD_CHANGE_TO_EXCLUDE_MODE;
			} else {
				CTR1(KTR_MLD, "%s: change to INCLUDE",
				    __func__);
				if (use_block_allow) {
					/*
					 * XXX
					 * Here we're interested in state
					 * edges either direction between
					 * MCAST_UNDEFINED and MCAST_INCLUDE.
					 * Perhaps we should just check
					 * the group state, rather than
					 * the filter mode.
					 */
					if (mode == MCAST_UNDEFINED) {
						type = MLD_BLOCK_OLD_SOURCES;
					} else {
						type = MLD_ALLOW_NEW_SOURCES;
					}
				} else {
					type = MLD_CHANGE_TO_INCLUDE_MODE;
					if (mode == MCAST_UNDEFINED)
						record_has_sources = 0;
				}
			}
		} else {
			if (record_has_sources) {
				is_filter_list_change = 1;
			} else {
				type = MLD_DO_NOTHING;
			}
		}
	} else {
		/*
		 * Queue a current state record.
		 */
		if (mode == MCAST_EXCLUDE) {
			type = MLD_MODE_IS_EXCLUDE;
		} else if (mode == MCAST_INCLUDE) {
			type = MLD_MODE_IS_INCLUDE;
			KASSERT(inm->in6m_st[1].iss_asm == 0,
			    ("%s: inm %p is INCLUDE but ASM count is %d",
			     __func__, inm, inm->in6m_st[1].iss_asm));
		}
	}

	/*
	 * Generate the filter list changes using a separate function.
	 */
	if (is_filter_list_change)
		return (mld_v2_enqueue_filter_change(mq, inm));

	if (type == MLD_DO_NOTHING) {
		CTR3(KTR_MLD, "%s: nothing to do for %s/%s",
		    __func__, ip6_sprintf(ip6tbuf, &inm->in6m_addr),
		    if_name(inm->in6m_ifp));
		return (0);
	}

	/*
	 * If any sources are present, we must be able to fit at least
	 * one in the trailing space of the tail packet's mbuf,
	 * ideally more.
	 */
	minrec0len = sizeof(struct mldv2_record);
	if (record_has_sources)
		minrec0len += sizeof(struct in6_addr);

	CTR4(KTR_MLD, "%s: queueing %s for %s/%s", __func__,
	    mld_rec_type_to_str(type),
	    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    if_name(inm->in6m_ifp));

	/*
	 * Check if we have a packet in the tail of the queue for this
	 * group into which the first group record for this group will fit.
	 * Otherwise allocate a new packet.
	 * Always allocate leading space for IP6+RA+ICMPV6+REPORT.
	 * Note: Group records for G/GSR query responses MUST be sent
	 * in their own packet.
	 */
	m0 = mbufq_last(mq);
	if (!is_group_query &&
	    m0 != NULL &&
	    (m0->m_pkthdr.PH_vt.vt_nrecs + 1 <= MLD_V2_REPORT_MAXRECS) &&
	    (m0->m_pkthdr.len + minrec0len) <
	     (ifp->if_mtu - MLD_MTUSPACE)) {
		m0srcs = (ifp->if_mtu - m0->m_pkthdr.len -
			    sizeof(struct mldv2_record)) /
			    sizeof(struct in6_addr);
		m = m0;
		CTR1(KTR_MLD, "%s: use existing packet", __func__);
	} else {
		if (mbufq_full(mq)) {
			CTR1(KTR_MLD, "%s: outbound queue full", __func__);
			return (-ENOMEM);
		}
		m = NULL;
		m0srcs = (ifp->if_mtu - MLD_MTUSPACE -
		    sizeof(struct mldv2_record)) / sizeof(struct in6_addr);
		if (!is_state_change && !is_group_query)
			m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL)
			return (-ENOMEM);

		mld_save_context(m, ifp);

		CTR1(KTR_MLD, "%s: allocated first packet", __func__);
	}

	/*
	 * Append group record.
	 * If we have sources, we don't know how many yet.
	 */
	mr.mr_type = type;
	mr.mr_datalen = 0;
	mr.mr_numsrc = 0;
	mr.mr_addr = inm->in6m_addr;
	in6_clearscope(&mr.mr_addr);
	if (!m_append(m, sizeof(struct mldv2_record), (void *)&mr)) {
		if (m != m0)
			m_freem(m);
		CTR1(KTR_MLD, "%s: m_append() failed.", __func__);
		return (-ENOMEM);
	}
	nbytes += sizeof(struct mldv2_record);

	/*
	 * Append as many sources as will fit in the first packet.
	 * If we are appending to a new packet, the chain allocation
	 * may potentially use clusters; use m_getptr() in this case.
	 * If we are appending to an existing packet, we need to obtain
	 * a pointer to the group record after m_append(), in case a new
	 * mbuf was allocated.
	 *
	 * Only append sources which are in-mode at t1. If we are
	 * transitioning to MCAST_UNDEFINED state on the group, and
	 * use_block_allow is zero, do not include source entries.
	 * Otherwise, we need to include this source in the report.
	 *
	 * Only report recorded sources in our filter set when responding
	 * to a group-source query.
	 */
	if (record_has_sources) {
		if (m == m0) {
			md = m_last(m);
			pmr = (struct mldv2_record *)(mtod(md, uint8_t *) +
			    md->m_len - nbytes);
		} else {
			md = m_getptr(m, 0, &off);
			pmr = (struct mldv2_record *)(mtod(md, uint8_t *) +
			    off);
		}
		msrcs = 0;
		RB_FOREACH_SAFE(ims, ip6_msource_tree, &inm->in6m_srcs,
		    nims) {
			CTR2(KTR_MLD, "%s: visit node %s", __func__,
			    ip6_sprintf(ip6tbuf, &ims->im6s_addr));
			now = im6s_get_mode(inm, ims, 1);
			CTR2(KTR_MLD, "%s: node is %d", __func__, now);
			if ((now != mode) ||
			    (now == mode &&
			     (!use_block_allow && mode == MCAST_UNDEFINED))) {
				CTR1(KTR_MLD, "%s: skip node", __func__);
				continue;
			}
			if (is_source_query && ims->im6s_stp == 0) {
				CTR1(KTR_MLD, "%s: skip unrecorded node",
				    __func__);
				continue;
			}
			CTR1(KTR_MLD, "%s: append node", __func__);
			if (!m_append(m, sizeof(struct in6_addr),
			    (void *)&ims->im6s_addr)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_MLD, "%s: m_append() failed.",
				    __func__);
				return (-ENOMEM);
			}
			nbytes += sizeof(struct in6_addr);
			++msrcs;
			if (msrcs == m0srcs)
				break;
		}
		CTR2(KTR_MLD, "%s: msrcs is %d this packet", __func__,
		    msrcs);
		pmr->mr_numsrc = htons(msrcs);
		nbytes += (msrcs * sizeof(struct in6_addr));
	}

	if (is_source_query && msrcs == 0) {
		CTR1(KTR_MLD, "%s: no recorded sources to report", __func__);
		if (m != m0)
			m_freem(m);
		return (0);
	}

	/*
	 * We are good to go with first packet.
	 */
	if (m != m0) {
		CTR1(KTR_MLD, "%s: enqueueing first packet", __func__);
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
			CTR1(KTR_MLD, "%s: outbound queue full", __func__);
			return (-ENOMEM);
		}
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL)
			return (-ENOMEM);
		mld_save_context(m, ifp);
		md = m_getptr(m, 0, &off);
		pmr = (struct mldv2_record *)(mtod(md, uint8_t *) + off);
		CTR1(KTR_MLD, "%s: allocated next packet", __func__);

		if (!m_append(m, sizeof(struct mldv2_record), (void *)&mr)) {
			if (m != m0)
				m_freem(m);
			CTR1(KTR_MLD, "%s: m_append() failed.", __func__);
			return (-ENOMEM);
		}
		m->m_pkthdr.PH_vt.vt_nrecs = 1;
		nbytes += sizeof(struct mldv2_record);

		m0srcs = (ifp->if_mtu - MLD_MTUSPACE -
		    sizeof(struct mldv2_record)) / sizeof(struct in6_addr);

		msrcs = 0;
		RB_FOREACH_FROM(ims, ip6_msource_tree, nims) {
			CTR2(KTR_MLD, "%s: visit node %s",
			    __func__, ip6_sprintf(ip6tbuf, &ims->im6s_addr));
			now = im6s_get_mode(inm, ims, 1);
			if ((now != mode) ||
			    (now == mode &&
			     (!use_block_allow && mode == MCAST_UNDEFINED))) {
				CTR1(KTR_MLD, "%s: skip node", __func__);
				continue;
			}
			if (is_source_query && ims->im6s_stp == 0) {
				CTR1(KTR_MLD, "%s: skip unrecorded node",
				    __func__);
				continue;
			}
			CTR1(KTR_MLD, "%s: append node", __func__);
			if (!m_append(m, sizeof(struct in6_addr),
			    (void *)&ims->im6s_addr)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_MLD, "%s: m_append() failed.",
				    __func__);
				return (-ENOMEM);
			}
			++msrcs;
			if (msrcs == m0srcs)
				break;
		}
		pmr->mr_numsrc = htons(msrcs);
		nbytes += (msrcs * sizeof(struct in6_addr));

		CTR1(KTR_MLD, "%s: enqueueing next packet", __func__);
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
 * Enqueue an MLDv2 filter list change to the given output queue.
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
mld_v2_enqueue_filter_change(struct mbufq *mq, struct in6_multi *inm)
{
	static const int MINRECLEN =
	    sizeof(struct mldv2_record) + sizeof(struct in6_addr);
	struct ifnet		*ifp;
	struct mldv2_record	 mr;
	struct mldv2_record	*pmr;
	struct ip6_msource	*ims, *nims;
	struct mbuf		*m, *m0, *md;
	int			 m0srcs, nbytes, npbytes, off, rsrcs, schanged;
	int			 nallow, nblock;
	uint8_t			 mode, now, then;
	rectype_t		 crt, drt, nrt;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	IN6_MULTI_LIST_LOCK_ASSERT();

	if (inm->in6m_nsrc == 0 ||
	    (inm->in6m_st[0].iss_asm > 0 && inm->in6m_st[1].iss_asm > 0))
		return (0);

	ifp = inm->in6m_ifp;			/* interface */
	mode = inm->in6m_st[1].iss_fmode;	/* filter mode at t1 */
	crt = REC_NONE;	/* current group record type */
	drt = REC_NONE;	/* mask of completed group record types */
	nrt = REC_NONE;	/* record type for current node */
	m0srcs = 0;	/* # source which will fit in current mbuf chain */
	npbytes = 0;	/* # of bytes appended this packet */
	nbytes = 0;	/* # of bytes appended to group's state-change queue */
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
			     MLD_V2_REPORT_MAXRECS) &&
			    (m0->m_pkthdr.len + MINRECLEN) <
			     (ifp->if_mtu - MLD_MTUSPACE)) {
				m = m0;
				m0srcs = (ifp->if_mtu - m0->m_pkthdr.len -
					    sizeof(struct mldv2_record)) /
					    sizeof(struct in6_addr);
				CTR1(KTR_MLD,
				    "%s: use previous packet", __func__);
			} else {
				m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
				if (m == NULL)
					m = m_gethdr(M_NOWAIT, MT_DATA);
				if (m == NULL) {
					CTR1(KTR_MLD,
					    "%s: m_get*() failed", __func__);
					return (-ENOMEM);
				}
				m->m_pkthdr.PH_vt.vt_nrecs = 0;
				mld_save_context(m, ifp);
				m0srcs = (ifp->if_mtu - MLD_MTUSPACE -
				    sizeof(struct mldv2_record)) /
				    sizeof(struct in6_addr);
				npbytes = 0;
				CTR1(KTR_MLD,
				    "%s: allocated new packet", __func__);
			}
			/*
			 * Append the MLD group record header to the
			 * current packet's data area.
			 * Recalculate pointer to free space for next
			 * group record, in case m_append() allocated
			 * a new mbuf or cluster.
			 */
			memset(&mr, 0, sizeof(mr));
			mr.mr_addr = inm->in6m_addr;
			in6_clearscope(&mr.mr_addr);
			if (!m_append(m, sizeof(mr), (void *)&mr)) {
				if (m != m0)
					m_freem(m);
				CTR1(KTR_MLD,
				    "%s: m_append() failed", __func__);
				return (-ENOMEM);
			}
			npbytes += sizeof(struct mldv2_record);
			if (m != m0) {
				/* new packet; offset in chain */
				md = m_getptr(m, npbytes -
				    sizeof(struct mldv2_record), &off);
				pmr = (struct mldv2_record *)(mtod(md,
				    uint8_t *) + off);
			} else {
				/* current packet; offset from last append */
				md = m_last(m);
				pmr = (struct mldv2_record *)(mtod(md,
				    uint8_t *) + md->m_len -
				    sizeof(struct mldv2_record));
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
			if (nims == NULL) {
				nims = RB_MIN(ip6_msource_tree,
				    &inm->in6m_srcs);
			}
			RB_FOREACH_FROM(ims, ip6_msource_tree, nims) {
				CTR2(KTR_MLD, "%s: visit node %s", __func__,
				    ip6_sprintf(ip6tbuf, &ims->im6s_addr));
				now = im6s_get_mode(inm, ims, 1);
				then = im6s_get_mode(inm, ims, 0);
				CTR3(KTR_MLD, "%s: mode: t0 %d, t1 %d",
				    __func__, then, now);
				if (now == then) {
					CTR1(KTR_MLD,
					    "%s: skip unchanged", __func__);
					continue;
				}
				if (mode == MCAST_EXCLUDE &&
				    now == MCAST_INCLUDE) {
					CTR1(KTR_MLD,
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
				if (!m_append(m, sizeof(struct in6_addr),
				    (void *)&ims->im6s_addr)) {
					if (m != m0)
						m_freem(m);
					CTR1(KTR_MLD,
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
				npbytes -= sizeof(struct mldv2_record);
				if (m != m0) {
					CTR1(KTR_MLD,
					    "%s: m_free(m)", __func__);
					m_freem(m);
				} else {
					CTR1(KTR_MLD,
					    "%s: m_adj(m, -mr)", __func__);
					m_adj(m, -((int)sizeof(
					    struct mldv2_record)));
				}
				continue;
			}
			npbytes += (rsrcs * sizeof(struct in6_addr));
			if (crt == REC_ALLOW)
				pmr->mr_type = MLD_ALLOW_NEW_SOURCES;
			else if (crt == REC_BLOCK)
				pmr->mr_type = MLD_BLOCK_OLD_SOURCES;
			pmr->mr_numsrc = htons(rsrcs);
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

	CTR3(KTR_MLD, "%s: queued %d ALLOW_NEW, %d BLOCK_OLD", __func__,
	    nallow, nblock);

	return (nbytes);
}

static int
mld_v2_merge_state_changes(struct in6_multi *inm, struct mbufq *scq)
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

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	/*
	 * If there are further pending retransmissions, make a writable
	 * copy of each queued state-change message before merging.
	 */
	if (inm->in6m_scrv > 0)
		docopy = 1;

	gq = &inm->in6m_scq;
#ifdef KTR
	if (mbufq_first(gq) == NULL) {
		CTR2(KTR_MLD, "%s: WARNING: queue for inm %p is empty",
		    __func__, inm);
	}
#endif

	m = mbufq_first(gq);
	while (m != NULL) {
		/*
		 * Only merge the report into the current packet if
		 * there is sufficient space to do so; an MLDv2 report
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
			    MLD_V2_REPORT_MAXRECS) &&
			    (mt->m_pkthdr.len + recslen <=
			    (inm->in6m_ifp->if_mtu - MLD_MTUSPACE)))
				domerge = 1;
		}

		if (!domerge && mbufq_full(gq)) {
			CTR2(KTR_MLD,
			    "%s: outbound queue full, skipping whole packet %p",
			    __func__, m);
			mt = m->m_nextpkt;
			if (!docopy)
				m_freem(m);
			m = mt;
			continue;
		}

		if (!docopy) {
			CTR2(KTR_MLD, "%s: dequeueing %p", __func__, m);
			m0 = mbufq_dequeue(gq);
			m = m0->m_nextpkt;
		} else {
			CTR2(KTR_MLD, "%s: copying %p", __func__, m);
			m0 = m_dup(m, M_NOWAIT);
			if (m0 == NULL)
				return (ENOMEM);
			m0->m_nextpkt = NULL;
			m = m->m_nextpkt;
		}

		if (!domerge) {
			CTR3(KTR_MLD, "%s: queueing %p to scq %p)",
			    __func__, m0, scq);
			mbufq_enqueue(scq, m0);
		} else {
			struct mbuf *mtl;	/* last mbuf of packet mt */

			CTR3(KTR_MLD, "%s: merging %p with ifscq tail %p)",
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
 * Respond to a pending MLDv2 General Query.
 */
static void
mld_v2_dispatch_general_query(struct mld_ifsoftc *mli)
{
	struct epoch_tracker	 et;
	struct ifmultiaddr	*ifma;
	struct ifnet		*ifp;
	struct in6_multi	*inm;
	int			 retval;

	IN6_MULTI_LIST_LOCK_ASSERT();
	MLD_LOCK_ASSERT();

	KASSERT(mli->mli_version == MLD_VERSION_2,
	    ("%s: called when version %d", __func__, mli->mli_version));

	/*
	 * Check that there are some packets queued. If so, send them first.
	 * For large number of groups the reply to general query can take
	 * many packets, we should finish sending them before starting of
	 * queuing the new reply.
	 */
	if (mbufq_len(&mli->mli_gq) != 0)
		goto send;

	ifp = mli->mli_ifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		inm = in6m_ifmultiaddr_get_inm(ifma);
		if (inm == NULL)
			continue;
		KASSERT(ifp == inm->in6m_ifp,
		    ("%s: inconsistent ifp", __func__));

		switch (inm->in6m_state) {
		case MLD_NOT_MEMBER:
		case MLD_SILENT_MEMBER:
			break;
		case MLD_REPORTING_MEMBER:
		case MLD_IDLE_MEMBER:
		case MLD_LAZY_MEMBER:
		case MLD_SLEEPING_MEMBER:
		case MLD_AWAKENING_MEMBER:
			inm->in6m_state = MLD_REPORTING_MEMBER;
			retval = mld_v2_enqueue_group_record(&mli->mli_gq,
			    inm, 0, 0, 0, 0);
			CTR2(KTR_MLD, "%s: enqueue record = %d",
			    __func__, retval);
			break;
		case MLD_G_QUERY_PENDING_MEMBER:
		case MLD_SG_QUERY_PENDING_MEMBER:
		case MLD_LEAVING_MEMBER:
			break;
		}
	}
	NET_EPOCH_EXIT(et);

send:
	mld_dispatch_queue(&mli->mli_gq, MLD_MAX_RESPONSE_BURST);

	/*
	 * Slew transmission of bursts over 500ms intervals.
	 */
	if (mbufq_first(&mli->mli_gq) != NULL) {
		mli->mli_v2_timer = 1 + MLD_RANDOM_DELAY(
		    MLD_RESPONSE_BURST_INTERVAL);
		V_interface_timers_running6 = 1;
	}
}

/*
 * Transmit the next pending message in the output queue.
 *
 * VIMAGE: Needs to store/restore vnet pointer on a per-mbuf-chain basis.
 * MRT: Nothing needs to be done, as MLD traffic is always local to
 * a link and uses a link-scope multicast address.
 */
static void
mld_dispatch_packet(struct mbuf *m)
{
	struct ip6_moptions	 im6o;
	struct ifnet		*ifp;
	struct ifnet		*oifp;
	struct mbuf		*m0;
	struct mbuf		*md;
	struct ip6_hdr		*ip6;
	struct mld_hdr		*mld;
	int			 error;
	int			 off;
	int			 type;
	uint32_t		 ifindex;

	CTR2(KTR_MLD, "%s: transmit %p", __func__, m);

	/*
	 * Set VNET image pointer from enqueued mbuf chain
	 * before doing anything else. Whilst we use interface
	 * indexes to guard against interface detach, they are
	 * unique to each VIMAGE and must be retrieved.
	 */
	ifindex = mld_restore_context(m);

	/*
	 * Check if the ifnet still exists. This limits the scope of
	 * any race in the absence of a global ifp lock for low cost
	 * (an array lookup).
	 */
	ifp = ifnet_byindex(ifindex);
	if (ifp == NULL) {
		CTR3(KTR_MLD, "%s: dropped %p as ifindex %u went away.",
		    __func__, m, ifindex);
		m_freem(m);
		IP6STAT_INC(ip6s_noroute);
		goto out;
	}

	im6o.im6o_multicast_hlim  = 1;
	im6o.im6o_multicast_loop = (V_ip6_mrouter != NULL);
	im6o.im6o_multicast_ifp = ifp;

	if (m->m_flags & M_MLDV1) {
		m0 = m;
	} else {
		m0 = mld_v2_encap_report(ifp, m);
		if (m0 == NULL) {
			CTR2(KTR_MLD, "%s: dropped %p", __func__, m);
			IP6STAT_INC(ip6s_odropped);
			goto out;
		}
	}

	mld_scrub_context(m0);
	m_clrprotoflags(m);
	m0->m_pkthdr.rcvif = V_loif;

	ip6 = mtod(m0, struct ip6_hdr *);
#if 0
	(void)in6_setscope(&ip6->ip6_dst, ifp, NULL);	/* XXX LOR */
#else
	/*
	 * XXX XXX Break some KPI rules to prevent an LOR which would
	 * occur if we called in6_setscope() at transmission.
	 * See comments at top of file.
	 */
	MLD_EMBEDSCOPE(&ip6->ip6_dst, ifp->if_index);
#endif

	/*
	 * Retrieve the ICMPv6 type before handoff to ip6_output(),
	 * so we can bump the stats.
	 */
	md = m_getptr(m0, sizeof(struct ip6_hdr), &off);
	mld = (struct mld_hdr *)(mtod(md, uint8_t *) + off);
	type = mld->mld_type;

	error = ip6_output(m0, &mld_po, NULL, IPV6_UNSPECSRC, &im6o,
	    &oifp, NULL);
	if (error) {
		CTR3(KTR_MLD, "%s: ip6_output(%p) = %d", __func__, m0, error);
		goto out;
	}
	ICMP6STAT_INC(icp6s_outhist[type]);
	if (oifp != NULL) {
		icmp6_ifstat_inc(oifp, ifs6_out_msg);
		switch (type) {
		case MLD_LISTENER_REPORT:
		case MLDV2_LISTENER_REPORT:
			icmp6_ifstat_inc(oifp, ifs6_out_mldreport);
			break;
		case MLD_LISTENER_DONE:
			icmp6_ifstat_inc(oifp, ifs6_out_mlddone);
			break;
		}
	}
out:
	return;
}

/*
 * Encapsulate an MLDv2 report.
 *
 * KAME IPv6 requires that hop-by-hop options be passed separately,
 * and that the IPv6 header be prepended in a separate mbuf.
 *
 * Returns a pointer to the new mbuf chain head, or NULL if the
 * allocation failed.
 */
static struct mbuf *
mld_v2_encap_report(struct ifnet *ifp, struct mbuf *m)
{
	struct mbuf		*mh;
	struct mldv2_report	*mld;
	struct ip6_hdr		*ip6;
	struct in6_ifaddr	*ia;
	int			 mldreclen;

	KASSERT(ifp != NULL, ("%s: null ifp", __func__));
	KASSERT((m->m_flags & M_PKTHDR),
	    ("%s: mbuf chain %p is !M_PKTHDR", __func__, m));

	/*
	 * RFC3590: OK to send as :: or tentative during DAD.
	 */
	ia = in6ifa_ifpforlinklocal(ifp, IN6_IFF_NOTREADY|IN6_IFF_ANYCAST);
	if (ia == NULL)
		CTR1(KTR_MLD, "%s: warning: ia is NULL", __func__);

	mh = m_gethdr(M_NOWAIT, MT_DATA);
	if (mh == NULL) {
		if (ia != NULL)
			ifa_free(&ia->ia_ifa);
		m_freem(m);
		return (NULL);
	}
	M_ALIGN(mh, sizeof(struct ip6_hdr) + sizeof(struct mldv2_report));

	mldreclen = m_length(m, NULL);
	CTR2(KTR_MLD, "%s: mldreclen is %d", __func__, mldreclen);

	mh->m_len = sizeof(struct ip6_hdr) + sizeof(struct mldv2_report);
	mh->m_pkthdr.len = sizeof(struct ip6_hdr) +
	    sizeof(struct mldv2_report) + mldreclen;

	ip6 = mtod(mh, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_src = ia ? ia->ia_addr.sin6_addr : in6addr_any;
	if (ia != NULL)
		ifa_free(&ia->ia_ifa);
	ip6->ip6_dst = in6addr_linklocal_allv2routers;
	/* scope ID will be set in netisr */

	mld = (struct mldv2_report *)(ip6 + 1);
	mld->mld_type = MLDV2_LISTENER_REPORT;
	mld->mld_code = 0;
	mld->mld_cksum = 0;
	mld->mld_v2_reserved = 0;
	mld->mld_v2_numrecs = htons(m->m_pkthdr.PH_vt.vt_nrecs);
	m->m_pkthdr.PH_vt.vt_nrecs = 0;

	mh->m_next = m;
	mld->mld_cksum = in6_cksum(mh, IPPROTO_ICMPV6,
	    sizeof(struct ip6_hdr), sizeof(struct mldv2_report) + mldreclen);
	return (mh);
}

#ifdef KTR
static char *
mld_rec_type_to_str(const int type)
{

	switch (type) {
		case MLD_CHANGE_TO_EXCLUDE_MODE:
			return "TO_EX";
			break;
		case MLD_CHANGE_TO_INCLUDE_MODE:
			return "TO_IN";
			break;
		case MLD_MODE_IS_EXCLUDE:
			return "MODE_EX";
			break;
		case MLD_MODE_IS_INCLUDE:
			return "MODE_IN";
			break;
		case MLD_ALLOW_NEW_SOURCES:
			return "ALLOW_NEW";
			break;
		case MLD_BLOCK_OLD_SOURCES:
			return "BLOCK_OLD";
			break;
		default:
			break;
	}
	return "unknown";
}
#endif

static void
mld_init(void *unused __unused)
{

	CTR1(KTR_MLD, "%s: initializing", __func__);
	MLD_LOCK_INIT();

	ip6_initpktopts(&mld_po);
	mld_po.ip6po_hlim = 1;
	mld_po.ip6po_hbh = &mld_ra.hbh;
	mld_po.ip6po_prefer_tempaddr = IP6PO_TEMPADDR_NOTPREFER;
	mld_po.ip6po_flags = IP6PO_DONTFRAG;
}
SYSINIT(mld_init, SI_SUB_PROTO_MC, SI_ORDER_MIDDLE, mld_init, NULL);

static void
mld_uninit(void *unused __unused)
{

	CTR1(KTR_MLD, "%s: tearing down", __func__);
	MLD_LOCK_DESTROY();
}
SYSUNINIT(mld_uninit, SI_SUB_PROTO_MC, SI_ORDER_MIDDLE, mld_uninit, NULL);

static void
vnet_mld_init(const void *unused __unused)
{

	CTR1(KTR_MLD, "%s: initializing", __func__);

	LIST_INIT(&V_mli_head);
}
VNET_SYSINIT(vnet_mld_init, SI_SUB_PROTO_MC, SI_ORDER_ANY, vnet_mld_init,
    NULL);

static void
vnet_mld_uninit(const void *unused __unused)
{

	/* This can happen if we shutdown the network stack. */
	CTR1(KTR_MLD, "%s: tearing down", __func__);
}
VNET_SYSUNINIT(vnet_mld_uninit, SI_SUB_PROTO_MC, SI_ORDER_ANY, vnet_mld_uninit,
    NULL);

static int
mld_modevent(module_t mod, int type, void *unused __unused)
{

    switch (type) {
    case MOD_LOAD:
    case MOD_UNLOAD:
	break;
    default:
	return (EOPNOTSUPP);
    }
    return (0);
}

static moduledata_t mld_mod = {
    "mld",
    mld_modevent,
    0
};
DECLARE_MODULE(mld, mld_mod, SI_SUB_PROTO_MC, SI_ORDER_ANY);
