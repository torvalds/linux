/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Bruce Simpson.
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
 */

/*
 * IPv6 multicast socket, group, and socket option processing module.
 * Normative references: RFC 2292, RFC 3492, RFC 3542, RFC 3678, RFC 3810.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/gtaskqueue.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/priv.h>
#include <sys/ktr.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/vnet.h>


#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/udp_var.h>
#include <netinet6/in6_fib.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp_var.h>
#include <netinet6/nd6.h>
#include <netinet6/mld6_var.h>
#include <netinet6/scope6_var.h>

#ifndef KTR_MLD
#define KTR_MLD KTR_INET6
#endif

#ifndef __SOCKUNION_DECLARED
union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
	struct sockaddr_in6	sin6;
};
typedef union sockunion sockunion_t;
#define __SOCKUNION_DECLARED
#endif /* __SOCKUNION_DECLARED */

static MALLOC_DEFINE(M_IN6MFILTER, "in6_mfilter",
    "IPv6 multicast PCB-layer source filter");
MALLOC_DEFINE(M_IP6MADDR, "in6_multi", "IPv6 multicast group");
static MALLOC_DEFINE(M_IP6MOPTS, "ip6_moptions", "IPv6 multicast options");
static MALLOC_DEFINE(M_IP6MSOURCE, "ip6_msource",
    "IPv6 multicast MLD-layer source filter");

RB_GENERATE(ip6_msource_tree, ip6_msource, im6s_link, ip6_msource_cmp);

/*
 * Locking:
 * - Lock order is: Giant, INP_WLOCK, IN6_MULTI_LOCK, MLD_LOCK, IF_ADDR_LOCK.
 * - The IF_ADDR_LOCK is implicitly taken by in6m_lookup() earlier, however
 *   it can be taken by code in net/if.c also.
 * - ip6_moptions and in6_mfilter are covered by the INP_WLOCK.
 *
 * struct in6_multi is covered by IN6_MULTI_LOCK. There isn't strictly
 * any need for in6_multi itself to be virtualized -- it is bound to an ifp
 * anyway no matter what happens.
 */
struct mtx in6_multi_list_mtx;
MTX_SYSINIT(in6_multi_mtx, &in6_multi_list_mtx, "in6_multi_list_mtx", MTX_DEF);

struct mtx in6_multi_free_mtx;
MTX_SYSINIT(in6_multi_free_mtx, &in6_multi_free_mtx, "in6_multi_free_mtx", MTX_DEF);

struct sx in6_multi_sx;
SX_SYSINIT(in6_multi_sx, &in6_multi_sx, "in6_multi_sx");



static void	im6f_commit(struct in6_mfilter *);
static int	im6f_get_source(struct in6_mfilter *imf,
		    const struct sockaddr_in6 *psin,
		    struct in6_msource **);
static struct in6_msource *
		im6f_graft(struct in6_mfilter *, const uint8_t,
		    const struct sockaddr_in6 *);
static void	im6f_leave(struct in6_mfilter *);
static int	im6f_prune(struct in6_mfilter *, const struct sockaddr_in6 *);
static void	im6f_purge(struct in6_mfilter *);
static void	im6f_rollback(struct in6_mfilter *);
static void	im6f_reap(struct in6_mfilter *);
static int	im6o_grow(struct ip6_moptions *);
static size_t	im6o_match_group(const struct ip6_moptions *,
		    const struct ifnet *, const struct sockaddr *);
static struct in6_msource *
		im6o_match_source(const struct ip6_moptions *, const size_t,
		    const struct sockaddr *);
static void	im6s_merge(struct ip6_msource *ims,
		    const struct in6_msource *lims, const int rollback);
static int	in6_getmulti(struct ifnet *, const struct in6_addr *,
		    struct in6_multi **);
static int	in6m_get_source(struct in6_multi *inm,
		    const struct in6_addr *addr, const int noalloc,
		    struct ip6_msource **pims);
#ifdef KTR
static int	in6m_is_ifp_detached(const struct in6_multi *);
#endif
static int	in6m_merge(struct in6_multi *, /*const*/ struct in6_mfilter *);
static void	in6m_purge(struct in6_multi *);
static void	in6m_reap(struct in6_multi *);
static struct ip6_moptions *
		in6p_findmoptions(struct inpcb *);
static int	in6p_get_source_filters(struct inpcb *, struct sockopt *);
static int	in6p_join_group(struct inpcb *, struct sockopt *);
static int	in6p_leave_group(struct inpcb *, struct sockopt *);
static struct ifnet *
		in6p_lookup_mcast_ifp(const struct inpcb *,
		    const struct sockaddr_in6 *);
static int	in6p_block_unblock_source(struct inpcb *, struct sockopt *);
static int	in6p_set_multicast_if(struct inpcb *, struct sockopt *);
static int	in6p_set_source_filters(struct inpcb *, struct sockopt *);
static int	sysctl_ip6_mcast_filters(SYSCTL_HANDLER_ARGS);

SYSCTL_DECL(_net_inet6_ip6);	/* XXX Not in any common header. */

static SYSCTL_NODE(_net_inet6_ip6, OID_AUTO, mcast, CTLFLAG_RW, 0,
    "IPv6 multicast");

static u_long in6_mcast_maxgrpsrc = IPV6_MAX_GROUP_SRC_FILTER;
SYSCTL_ULONG(_net_inet6_ip6_mcast, OID_AUTO, maxgrpsrc,
    CTLFLAG_RWTUN, &in6_mcast_maxgrpsrc, 0,
    "Max source filters per group");

static u_long in6_mcast_maxsocksrc = IPV6_MAX_SOCK_SRC_FILTER;
SYSCTL_ULONG(_net_inet6_ip6_mcast, OID_AUTO, maxsocksrc,
    CTLFLAG_RWTUN, &in6_mcast_maxsocksrc, 0,
    "Max source filters per socket");

/* TODO Virtualize this switch. */
int in6_mcast_loop = IPV6_DEFAULT_MULTICAST_LOOP;
SYSCTL_INT(_net_inet6_ip6_mcast, OID_AUTO, loop, CTLFLAG_RWTUN,
    &in6_mcast_loop, 0, "Loopback multicast datagrams by default");

static SYSCTL_NODE(_net_inet6_ip6_mcast, OID_AUTO, filters,
    CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_ip6_mcast_filters,
    "Per-interface stack-wide source filters");

#ifdef KTR
/*
 * Inline function which wraps assertions for a valid ifp.
 * The ifnet layer will set the ifma's ifp pointer to NULL if the ifp
 * is detached.
 */
static int __inline
in6m_is_ifp_detached(const struct in6_multi *inm)
{
	struct ifnet *ifp;

	KASSERT(inm->in6m_ifma != NULL, ("%s: no ifma", __func__));
	ifp = inm->in6m_ifma->ifma_ifp;
	if (ifp != NULL) {
		/*
		 * Sanity check that network-layer notion of ifp is the
		 * same as that of link-layer.
		 */
		KASSERT(inm->in6m_ifp == ifp, ("%s: bad ifp", __func__));
	}

	return (ifp == NULL);
}
#endif

/*
 * Initialize an in6_mfilter structure to a known state at t0, t1
 * with an empty source filter list.
 */
static __inline void
im6f_init(struct in6_mfilter *imf, const int st0, const int st1)
{
	memset(imf, 0, sizeof(struct in6_mfilter));
	RB_INIT(&imf->im6f_sources);
	imf->im6f_st[0] = st0;
	imf->im6f_st[1] = st1;
}

/*
 * Resize the ip6_moptions vector to the next power-of-two minus 1.
 * May be called with locks held; do not sleep.
 */
static int
im6o_grow(struct ip6_moptions *imo)
{
	struct in6_multi	**nmships;
	struct in6_multi	**omships;
	struct in6_mfilter	 *nmfilters;
	struct in6_mfilter	 *omfilters;
	size_t			  idx;
	size_t			  newmax;
	size_t			  oldmax;

	nmships = NULL;
	nmfilters = NULL;
	omships = imo->im6o_membership;
	omfilters = imo->im6o_mfilters;
	oldmax = imo->im6o_max_memberships;
	newmax = ((oldmax + 1) * 2) - 1;

	if (newmax <= IPV6_MAX_MEMBERSHIPS) {
		nmships = (struct in6_multi **)realloc(omships,
		    sizeof(struct in6_multi *) * newmax, M_IP6MOPTS, M_NOWAIT);
		nmfilters = (struct in6_mfilter *)realloc(omfilters,
		    sizeof(struct in6_mfilter) * newmax, M_IN6MFILTER,
		    M_NOWAIT);
		if (nmships != NULL && nmfilters != NULL) {
			/* Initialize newly allocated source filter heads. */
			for (idx = oldmax; idx < newmax; idx++) {
				im6f_init(&nmfilters[idx], MCAST_UNDEFINED,
				    MCAST_EXCLUDE);
			}
			imo->im6o_max_memberships = newmax;
			imo->im6o_membership = nmships;
			imo->im6o_mfilters = nmfilters;
		}
	}

	if (nmships == NULL || nmfilters == NULL) {
		if (nmships != NULL)
			free(nmships, M_IP6MOPTS);
		if (nmfilters != NULL)
			free(nmfilters, M_IN6MFILTER);
		return (ETOOMANYREFS);
	}

	return (0);
}

/*
 * Find an IPv6 multicast group entry for this ip6_moptions instance
 * which matches the specified group, and optionally an interface.
 * Return its index into the array, or -1 if not found.
 */
static size_t
im6o_match_group(const struct ip6_moptions *imo, const struct ifnet *ifp,
    const struct sockaddr *group)
{
	const struct sockaddr_in6 *gsin6;
	struct in6_multi	**pinm;
	int		  idx;
	int		  nmships;

	gsin6 = (const struct sockaddr_in6 *)group;

	/* The im6o_membership array may be lazy allocated. */
	if (imo->im6o_membership == NULL || imo->im6o_num_memberships == 0)
		return (-1);

	nmships = imo->im6o_num_memberships;
	pinm = &imo->im6o_membership[0];
	for (idx = 0; idx < nmships; idx++, pinm++) {
		if (*pinm == NULL)
			continue;
		if ((ifp == NULL || ((*pinm)->in6m_ifp == ifp)) &&
		    IN6_ARE_ADDR_EQUAL(&(*pinm)->in6m_addr,
		    &gsin6->sin6_addr)) {
			break;
		}
	}
	if (idx >= nmships)
		idx = -1;

	return (idx);
}

/*
 * Find an IPv6 multicast source entry for this imo which matches
 * the given group index for this socket, and source address.
 *
 * XXX TODO: The scope ID, if present in src, is stripped before
 * any comparison. We SHOULD enforce scope/zone checks where the source
 * filter entry has a link scope.
 *
 * NOTE: This does not check if the entry is in-mode, merely if
 * it exists, which may not be the desired behaviour.
 */
static struct in6_msource *
im6o_match_source(const struct ip6_moptions *imo, const size_t gidx,
    const struct sockaddr *src)
{
	struct ip6_msource	 find;
	struct in6_mfilter	*imf;
	struct ip6_msource	*ims;
	const sockunion_t	*psa;

	KASSERT(src->sa_family == AF_INET6, ("%s: !AF_INET6", __func__));
	KASSERT(gidx != -1 && gidx < imo->im6o_num_memberships,
	    ("%s: invalid index %d\n", __func__, (int)gidx));

	/* The im6o_mfilters array may be lazy allocated. */
	if (imo->im6o_mfilters == NULL)
		return (NULL);
	imf = &imo->im6o_mfilters[gidx];

	psa = (const sockunion_t *)src;
	find.im6s_addr = psa->sin6.sin6_addr;
	in6_clearscope(&find.im6s_addr);		/* XXX */
	ims = RB_FIND(ip6_msource_tree, &imf->im6f_sources, &find);

	return ((struct in6_msource *)ims);
}

/*
 * Perform filtering for multicast datagrams on a socket by group and source.
 *
 * Returns 0 if a datagram should be allowed through, or various error codes
 * if the socket was not a member of the group, or the source was muted, etc.
 */
int
im6o_mc_filter(const struct ip6_moptions *imo, const struct ifnet *ifp,
    const struct sockaddr *group, const struct sockaddr *src)
{
	size_t gidx;
	struct in6_msource *ims;
	int mode;

	KASSERT(ifp != NULL, ("%s: null ifp", __func__));

	gidx = im6o_match_group(imo, ifp, group);
	if (gidx == -1)
		return (MCAST_NOTGMEMBER);

	/*
	 * Check if the source was included in an (S,G) join.
	 * Allow reception on exclusive memberships by default,
	 * reject reception on inclusive memberships by default.
	 * Exclude source only if an in-mode exclude filter exists.
	 * Include source only if an in-mode include filter exists.
	 * NOTE: We are comparing group state here at MLD t1 (now)
	 * with socket-layer t0 (since last downcall).
	 */
	mode = imo->im6o_mfilters[gidx].im6f_st[1];
	ims = im6o_match_source(imo, gidx, src);

	if ((ims == NULL && mode == MCAST_INCLUDE) ||
	    (ims != NULL && ims->im6sl_st[0] != mode))
		return (MCAST_NOTSMEMBER);

	return (MCAST_PASS);
}

/*
 * Find and return a reference to an in6_multi record for (ifp, group),
 * and bump its reference count.
 * If one does not exist, try to allocate it, and update link-layer multicast
 * filters on ifp to listen for group.
 * Assumes the IN6_MULTI lock is held across the call.
 * Return 0 if successful, otherwise return an appropriate error code.
 */
static int
in6_getmulti(struct ifnet *ifp, const struct in6_addr *group,
    struct in6_multi **pinm)
{
	struct epoch_tracker	 et;
	struct sockaddr_in6	 gsin6;
	struct ifmultiaddr	*ifma;
	struct in6_multi	*inm;
	int			 error;

	error = 0;

	/*
	 * XXX: Accesses to ifma_protospec must be covered by IF_ADDR_LOCK;
	 * if_addmulti() takes this mutex itself, so we must drop and
	 * re-acquire around the call.
	 */
	IN6_MULTI_LOCK_ASSERT();
	IN6_MULTI_LIST_LOCK();
	IF_ADDR_WLOCK(ifp);
	NET_EPOCH_ENTER(et);
	inm = in6m_lookup_locked(ifp, group);
	NET_EPOCH_EXIT(et);

	if (inm != NULL) {
		/*
		 * If we already joined this group, just bump the
		 * refcount and return it.
		 */
		KASSERT(inm->in6m_refcount >= 1,
		    ("%s: bad refcount %d", __func__, inm->in6m_refcount));
		in6m_acquire_locked(inm);
		*pinm = inm;
		goto out_locked;
	}

	memset(&gsin6, 0, sizeof(gsin6));
	gsin6.sin6_family = AF_INET6;
	gsin6.sin6_len = sizeof(struct sockaddr_in6);
	gsin6.sin6_addr = *group;

	/*
	 * Check if a link-layer group is already associated
	 * with this network-layer group on the given ifnet.
	 */
	IN6_MULTI_LIST_UNLOCK();
	IF_ADDR_WUNLOCK(ifp);
	error = if_addmulti(ifp, (struct sockaddr *)&gsin6, &ifma);
	if (error != 0)
		return (error);
	IN6_MULTI_LIST_LOCK();
	IF_ADDR_WLOCK(ifp);

	/*
	 * If something other than netinet6 is occupying the link-layer
	 * group, print a meaningful error message and back out of
	 * the allocation.
	 * Otherwise, bump the refcount on the existing network-layer
	 * group association and return it.
	 */
	if (ifma->ifma_protospec != NULL) {
		inm = (struct in6_multi *)ifma->ifma_protospec;
#ifdef INVARIANTS
		KASSERT(ifma->ifma_addr != NULL, ("%s: no ifma_addr",
		    __func__));
		KASSERT(ifma->ifma_addr->sa_family == AF_INET6,
		    ("%s: ifma not AF_INET6", __func__));
		KASSERT(inm != NULL, ("%s: no ifma_protospec", __func__));
		if (inm->in6m_ifma != ifma || inm->in6m_ifp != ifp ||
		    !IN6_ARE_ADDR_EQUAL(&inm->in6m_addr, group))
			panic("%s: ifma %p is inconsistent with %p (%p)",
			    __func__, ifma, inm, group);
#endif
		in6m_acquire_locked(inm);
		*pinm = inm;
		goto out_locked;
	}

	IF_ADDR_WLOCK_ASSERT(ifp);

	/*
	 * A new in6_multi record is needed; allocate and initialize it.
	 * We DO NOT perform an MLD join as the in6_ layer may need to
	 * push an initial source list down to MLD to support SSM.
	 *
	 * The initial source filter state is INCLUDE, {} as per the RFC.
	 * Pending state-changes per group are subject to a bounds check.
	 */
	inm = malloc(sizeof(*inm), M_IP6MADDR, M_NOWAIT | M_ZERO);
	if (inm == NULL) {
		IN6_MULTI_LIST_UNLOCK();
		IF_ADDR_WUNLOCK(ifp);
		if_delmulti_ifma(ifma);
		return (ENOMEM);
	}
	inm->in6m_addr = *group;
	inm->in6m_ifp = ifp;
	inm->in6m_mli = MLD_IFINFO(ifp);
	inm->in6m_ifma = ifma;
	inm->in6m_refcount = 1;
	inm->in6m_state = MLD_NOT_MEMBER;
	mbufq_init(&inm->in6m_scq, MLD_MAX_STATE_CHANGES);

	inm->in6m_st[0].iss_fmode = MCAST_UNDEFINED;
	inm->in6m_st[1].iss_fmode = MCAST_UNDEFINED;
	RB_INIT(&inm->in6m_srcs);

	ifma->ifma_protospec = inm;
	*pinm = inm;

 out_locked:
	IN6_MULTI_LIST_UNLOCK();
	IF_ADDR_WUNLOCK(ifp);
	return (error);
}

/*
 * Drop a reference to an in6_multi record.
 *
 * If the refcount drops to 0, free the in6_multi record and
 * delete the underlying link-layer membership.
 */
static void
in6m_release(struct in6_multi *inm)
{
	struct ifmultiaddr *ifma;
	struct ifnet *ifp;

	CTR2(KTR_MLD, "%s: refcount is %d", __func__, inm->in6m_refcount);

	MPASS(inm->in6m_refcount == 0);
	CTR2(KTR_MLD, "%s: freeing inm %p", __func__, inm);

	ifma = inm->in6m_ifma;
	ifp = inm->in6m_ifp;
	MPASS(ifma->ifma_llifma == NULL);

	/* XXX this access is not covered by IF_ADDR_LOCK */
	CTR2(KTR_MLD, "%s: purging ifma %p", __func__, ifma);
	KASSERT(ifma->ifma_protospec == NULL,
	    ("%s: ifma_protospec != NULL", __func__));
	if (ifp == NULL)
		ifp = ifma->ifma_ifp;

	if (ifp != NULL) {
		CURVNET_SET(ifp->if_vnet);
		in6m_purge(inm);
		free(inm, M_IP6MADDR);
		if_delmulti_ifma_flags(ifma, 1);
		CURVNET_RESTORE();
		if_rele(ifp);
	} else {
		in6m_purge(inm);
		free(inm, M_IP6MADDR);
		if_delmulti_ifma_flags(ifma, 1);
	}
}

static struct grouptask free_gtask;
static struct in6_multi_head in6m_free_list;
static void in6m_release_task(void *arg __unused);
static void in6m_init(void)
{
	SLIST_INIT(&in6m_free_list);
	taskqgroup_config_gtask_init(NULL, &free_gtask, in6m_release_task, "in6m release task");
}

#ifdef EARLY_AP_STARTUP
SYSINIT(in6m_init, SI_SUB_SMP + 1, SI_ORDER_FIRST,
	in6m_init, NULL);
#else
SYSINIT(in6m_init, SI_SUB_ROOT_CONF - 1, SI_ORDER_SECOND,
	in6m_init, NULL);
#endif


void
in6m_release_list_deferred(struct in6_multi_head *inmh)
{
	if (SLIST_EMPTY(inmh))
		return;
	mtx_lock(&in6_multi_free_mtx);
	SLIST_CONCAT(&in6m_free_list, inmh, in6_multi, in6m_nrele);
	mtx_unlock(&in6_multi_free_mtx);
	GROUPTASK_ENQUEUE(&free_gtask);
}

void
in6m_release_wait(void)
{

	/* Wait for all jobs to complete. */
	gtaskqueue_drain_all(free_gtask.gt_taskqueue);
}

void
in6m_disconnect_locked(struct in6_multi_head *inmh, struct in6_multi *inm)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct in6_ifaddr *ifa6;
	struct in6_multi_mship *imm, *imm_tmp;
	struct ifmultiaddr *ifma, *ll_ifma;

	IN6_MULTI_LIST_LOCK_ASSERT();

	ifp = inm->in6m_ifp;
	if (ifp == NULL)
		return;		/* already called */

	inm->in6m_ifp = NULL;
	IF_ADDR_WLOCK_ASSERT(ifp);
	ifma = inm->in6m_ifma;
	if (ifma == NULL)
		return;

	if_ref(ifp);
	if (ifma->ifma_flags & IFMA_F_ENQUEUED) {
		CK_STAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifmultiaddr, ifma_link);
		ifma->ifma_flags &= ~IFMA_F_ENQUEUED;
	}
	MCDPRINTF("removed ifma: %p from %s\n", ifma, ifp->if_xname);
	if ((ll_ifma = ifma->ifma_llifma) != NULL) {
		MPASS(ifma != ll_ifma);
		ifma->ifma_llifma = NULL;
		MPASS(ll_ifma->ifma_llifma == NULL);
		MPASS(ll_ifma->ifma_ifp == ifp);
		if (--ll_ifma->ifma_refcount == 0) {
			if (ll_ifma->ifma_flags & IFMA_F_ENQUEUED) {
				CK_STAILQ_REMOVE(&ifp->if_multiaddrs, ll_ifma, ifmultiaddr, ifma_link);
				ll_ifma->ifma_flags &= ~IFMA_F_ENQUEUED;
			}
			MCDPRINTF("removed ll_ifma: %p from %s\n", ll_ifma, ifp->if_xname);
			if_freemulti(ll_ifma);
		}
	}
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		ifa6 = (void *)ifa;
		LIST_FOREACH_SAFE(imm, &ifa6->ia6_memberships,
		    i6mm_chain, imm_tmp) {
			if (inm == imm->i6mm_maddr) {
				LIST_REMOVE(imm, i6mm_chain);
				free(imm, M_IP6MADDR);
				in6m_rele_locked(inmh, inm);
			}
		}
	}
}

static void
in6m_release_task(void *arg __unused)
{
	struct in6_multi_head in6m_free_tmp;
	struct in6_multi *inm, *tinm;

	SLIST_INIT(&in6m_free_tmp);
	mtx_lock(&in6_multi_free_mtx);
	SLIST_CONCAT(&in6m_free_tmp, &in6m_free_list, in6_multi, in6m_nrele);
	mtx_unlock(&in6_multi_free_mtx);
	IN6_MULTI_LOCK();
	SLIST_FOREACH_SAFE(inm, &in6m_free_tmp, in6m_nrele, tinm) {
		SLIST_REMOVE_HEAD(&in6m_free_tmp, in6m_nrele);
		in6m_release(inm);
	}
	IN6_MULTI_UNLOCK();
}

/*
 * Clear recorded source entries for a group.
 * Used by the MLD code. Caller must hold the IN6_MULTI lock.
 * FIXME: Should reap.
 */
void
in6m_clear_recorded(struct in6_multi *inm)
{
	struct ip6_msource	*ims;

	IN6_MULTI_LIST_LOCK_ASSERT();

	RB_FOREACH(ims, ip6_msource_tree, &inm->in6m_srcs) {
		if (ims->im6s_stp) {
			ims->im6s_stp = 0;
			--inm->in6m_st[1].iss_rec;
		}
	}
	KASSERT(inm->in6m_st[1].iss_rec == 0,
	    ("%s: iss_rec %d not 0", __func__, inm->in6m_st[1].iss_rec));
}

/*
 * Record a source as pending for a Source-Group MLDv2 query.
 * This lives here as it modifies the shared tree.
 *
 * inm is the group descriptor.
 * naddr is the address of the source to record in network-byte order.
 *
 * If the net.inet6.mld.sgalloc sysctl is non-zero, we will
 * lazy-allocate a source node in response to an SG query.
 * Otherwise, no allocation is performed. This saves some memory
 * with the trade-off that the source will not be reported to the
 * router if joined in the window between the query response and
 * the group actually being joined on the local host.
 *
 * VIMAGE: XXX: Currently the mld_sgalloc feature has been removed.
 * This turns off the allocation of a recorded source entry if
 * the group has not been joined.
 *
 * Return 0 if the source didn't exist or was already marked as recorded.
 * Return 1 if the source was marked as recorded by this function.
 * Return <0 if any error occurred (negated errno code).
 */
int
in6m_record_source(struct in6_multi *inm, const struct in6_addr *addr)
{
	struct ip6_msource	 find;
	struct ip6_msource	*ims, *nims;

	IN6_MULTI_LIST_LOCK_ASSERT();

	find.im6s_addr = *addr;
	ims = RB_FIND(ip6_msource_tree, &inm->in6m_srcs, &find);
	if (ims && ims->im6s_stp)
		return (0);
	if (ims == NULL) {
		if (inm->in6m_nsrc == in6_mcast_maxgrpsrc)
			return (-ENOSPC);
		nims = malloc(sizeof(struct ip6_msource), M_IP6MSOURCE,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (-ENOMEM);
		nims->im6s_addr = find.im6s_addr;
		RB_INSERT(ip6_msource_tree, &inm->in6m_srcs, nims);
		++inm->in6m_nsrc;
		ims = nims;
	}

	/*
	 * Mark the source as recorded and update the recorded
	 * source count.
	 */
	++ims->im6s_stp;
	++inm->in6m_st[1].iss_rec;

	return (1);
}

/*
 * Return a pointer to an in6_msource owned by an in6_mfilter,
 * given its source address.
 * Lazy-allocate if needed. If this is a new entry its filter state is
 * undefined at t0.
 *
 * imf is the filter set being modified.
 * addr is the source address.
 *
 * SMPng: May be called with locks held; malloc must not block.
 */
static int
im6f_get_source(struct in6_mfilter *imf, const struct sockaddr_in6 *psin,
    struct in6_msource **plims)
{
	struct ip6_msource	 find;
	struct ip6_msource	*ims, *nims;
	struct in6_msource	*lims;
	int			 error;

	error = 0;
	ims = NULL;
	lims = NULL;

	find.im6s_addr = psin->sin6_addr;
	ims = RB_FIND(ip6_msource_tree, &imf->im6f_sources, &find);
	lims = (struct in6_msource *)ims;
	if (lims == NULL) {
		if (imf->im6f_nsrc == in6_mcast_maxsocksrc)
			return (ENOSPC);
		nims = malloc(sizeof(struct in6_msource), M_IN6MFILTER,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (ENOMEM);
		lims = (struct in6_msource *)nims;
		lims->im6s_addr = find.im6s_addr;
		lims->im6sl_st[0] = MCAST_UNDEFINED;
		RB_INSERT(ip6_msource_tree, &imf->im6f_sources, nims);
		++imf->im6f_nsrc;
	}

	*plims = lims;

	return (error);
}

/*
 * Graft a source entry into an existing socket-layer filter set,
 * maintaining any required invariants and checking allocations.
 *
 * The source is marked as being in the new filter mode at t1.
 *
 * Return the pointer to the new node, otherwise return NULL.
 */
static struct in6_msource *
im6f_graft(struct in6_mfilter *imf, const uint8_t st1,
    const struct sockaddr_in6 *psin)
{
	struct ip6_msource	*nims;
	struct in6_msource	*lims;

	nims = malloc(sizeof(struct in6_msource), M_IN6MFILTER,
	    M_NOWAIT | M_ZERO);
	if (nims == NULL)
		return (NULL);
	lims = (struct in6_msource *)nims;
	lims->im6s_addr = psin->sin6_addr;
	lims->im6sl_st[0] = MCAST_UNDEFINED;
	lims->im6sl_st[1] = st1;
	RB_INSERT(ip6_msource_tree, &imf->im6f_sources, nims);
	++imf->im6f_nsrc;

	return (lims);
}

/*
 * Prune a source entry from an existing socket-layer filter set,
 * maintaining any required invariants and checking allocations.
 *
 * The source is marked as being left at t1, it is not freed.
 *
 * Return 0 if no error occurred, otherwise return an errno value.
 */
static int
im6f_prune(struct in6_mfilter *imf, const struct sockaddr_in6 *psin)
{
	struct ip6_msource	 find;
	struct ip6_msource	*ims;
	struct in6_msource	*lims;

	find.im6s_addr = psin->sin6_addr;
	ims = RB_FIND(ip6_msource_tree, &imf->im6f_sources, &find);
	if (ims == NULL)
		return (ENOENT);
	lims = (struct in6_msource *)ims;
	lims->im6sl_st[1] = MCAST_UNDEFINED;
	return (0);
}

/*
 * Revert socket-layer filter set deltas at t1 to t0 state.
 */
static void
im6f_rollback(struct in6_mfilter *imf)
{
	struct ip6_msource	*ims, *tims;
	struct in6_msource	*lims;

	RB_FOREACH_SAFE(ims, ip6_msource_tree, &imf->im6f_sources, tims) {
		lims = (struct in6_msource *)ims;
		if (lims->im6sl_st[0] == lims->im6sl_st[1]) {
			/* no change at t1 */
			continue;
		} else if (lims->im6sl_st[0] != MCAST_UNDEFINED) {
			/* revert change to existing source at t1 */
			lims->im6sl_st[1] = lims->im6sl_st[0];
		} else {
			/* revert source added t1 */
			CTR2(KTR_MLD, "%s: free ims %p", __func__, ims);
			RB_REMOVE(ip6_msource_tree, &imf->im6f_sources, ims);
			free(ims, M_IN6MFILTER);
			imf->im6f_nsrc--;
		}
	}
	imf->im6f_st[1] = imf->im6f_st[0];
}

/*
 * Mark socket-layer filter set as INCLUDE {} at t1.
 */
static void
im6f_leave(struct in6_mfilter *imf)
{
	struct ip6_msource	*ims;
	struct in6_msource	*lims;

	RB_FOREACH(ims, ip6_msource_tree, &imf->im6f_sources) {
		lims = (struct in6_msource *)ims;
		lims->im6sl_st[1] = MCAST_UNDEFINED;
	}
	imf->im6f_st[1] = MCAST_INCLUDE;
}

/*
 * Mark socket-layer filter set deltas as committed.
 */
static void
im6f_commit(struct in6_mfilter *imf)
{
	struct ip6_msource	*ims;
	struct in6_msource	*lims;

	RB_FOREACH(ims, ip6_msource_tree, &imf->im6f_sources) {
		lims = (struct in6_msource *)ims;
		lims->im6sl_st[0] = lims->im6sl_st[1];
	}
	imf->im6f_st[0] = imf->im6f_st[1];
}

/*
 * Reap unreferenced sources from socket-layer filter set.
 */
static void
im6f_reap(struct in6_mfilter *imf)
{
	struct ip6_msource	*ims, *tims;
	struct in6_msource	*lims;

	RB_FOREACH_SAFE(ims, ip6_msource_tree, &imf->im6f_sources, tims) {
		lims = (struct in6_msource *)ims;
		if ((lims->im6sl_st[0] == MCAST_UNDEFINED) &&
		    (lims->im6sl_st[1] == MCAST_UNDEFINED)) {
			CTR2(KTR_MLD, "%s: free lims %p", __func__, ims);
			RB_REMOVE(ip6_msource_tree, &imf->im6f_sources, ims);
			free(ims, M_IN6MFILTER);
			imf->im6f_nsrc--;
		}
	}
}

/*
 * Purge socket-layer filter set.
 */
static void
im6f_purge(struct in6_mfilter *imf)
{
	struct ip6_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip6_msource_tree, &imf->im6f_sources, tims) {
		CTR2(KTR_MLD, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip6_msource_tree, &imf->im6f_sources, ims);
		free(ims, M_IN6MFILTER);
		imf->im6f_nsrc--;
	}
	imf->im6f_st[0] = imf->im6f_st[1] = MCAST_UNDEFINED;
	KASSERT(RB_EMPTY(&imf->im6f_sources),
	    ("%s: im6f_sources not empty", __func__));
}

/*
 * Look up a source filter entry for a multicast group.
 *
 * inm is the group descriptor to work with.
 * addr is the IPv6 address to look up.
 * noalloc may be non-zero to suppress allocation of sources.
 * *pims will be set to the address of the retrieved or allocated source.
 *
 * SMPng: NOTE: may be called with locks held.
 * Return 0 if successful, otherwise return a non-zero error code.
 */
static int
in6m_get_source(struct in6_multi *inm, const struct in6_addr *addr,
    const int noalloc, struct ip6_msource **pims)
{
	struct ip6_msource	 find;
	struct ip6_msource	*ims, *nims;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	find.im6s_addr = *addr;
	ims = RB_FIND(ip6_msource_tree, &inm->in6m_srcs, &find);
	if (ims == NULL && !noalloc) {
		if (inm->in6m_nsrc == in6_mcast_maxgrpsrc)
			return (ENOSPC);
		nims = malloc(sizeof(struct ip6_msource), M_IP6MSOURCE,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (ENOMEM);
		nims->im6s_addr = *addr;
		RB_INSERT(ip6_msource_tree, &inm->in6m_srcs, nims);
		++inm->in6m_nsrc;
		ims = nims;
		CTR3(KTR_MLD, "%s: allocated %s as %p", __func__,
		    ip6_sprintf(ip6tbuf, addr), ims);
	}

	*pims = ims;
	return (0);
}

/*
 * Merge socket-layer source into MLD-layer source.
 * If rollback is non-zero, perform the inverse of the merge.
 */
static void
im6s_merge(struct ip6_msource *ims, const struct in6_msource *lims,
    const int rollback)
{
	int n = rollback ? -1 : 1;
#ifdef KTR
	char ip6tbuf[INET6_ADDRSTRLEN];

	ip6_sprintf(ip6tbuf, &lims->im6s_addr);
#endif

	if (lims->im6sl_st[0] == MCAST_EXCLUDE) {
		CTR3(KTR_MLD, "%s: t1 ex -= %d on %s", __func__, n, ip6tbuf);
		ims->im6s_st[1].ex -= n;
	} else if (lims->im6sl_st[0] == MCAST_INCLUDE) {
		CTR3(KTR_MLD, "%s: t1 in -= %d on %s", __func__, n, ip6tbuf);
		ims->im6s_st[1].in -= n;
	}

	if (lims->im6sl_st[1] == MCAST_EXCLUDE) {
		CTR3(KTR_MLD, "%s: t1 ex += %d on %s", __func__, n, ip6tbuf);
		ims->im6s_st[1].ex += n;
	} else if (lims->im6sl_st[1] == MCAST_INCLUDE) {
		CTR3(KTR_MLD, "%s: t1 in += %d on %s", __func__, n, ip6tbuf);
		ims->im6s_st[1].in += n;
	}
}

/*
 * Atomically update the global in6_multi state, when a membership's
 * filter list is being updated in any way.
 *
 * imf is the per-inpcb-membership group filter pointer.
 * A fake imf may be passed for in-kernel consumers.
 *
 * XXX This is a candidate for a set-symmetric-difference style loop
 * which would eliminate the repeated lookup from root of ims nodes,
 * as they share the same key space.
 *
 * If any error occurred this function will back out of refcounts
 * and return a non-zero value.
 */
static int
in6m_merge(struct in6_multi *inm, /*const*/ struct in6_mfilter *imf)
{
	struct ip6_msource	*ims, *nims;
	struct in6_msource	*lims;
	int			 schanged, error;
	int			 nsrc0, nsrc1;

	schanged = 0;
	error = 0;
	nsrc1 = nsrc0 = 0;
	IN6_MULTI_LIST_LOCK_ASSERT();

	/*
	 * Update the source filters first, as this may fail.
	 * Maintain count of in-mode filters at t0, t1. These are
	 * used to work out if we transition into ASM mode or not.
	 * Maintain a count of source filters whose state was
	 * actually modified by this operation.
	 */
	RB_FOREACH(ims, ip6_msource_tree, &imf->im6f_sources) {
		lims = (struct in6_msource *)ims;
		if (lims->im6sl_st[0] == imf->im6f_st[0]) nsrc0++;
		if (lims->im6sl_st[1] == imf->im6f_st[1]) nsrc1++;
		if (lims->im6sl_st[0] == lims->im6sl_st[1]) continue;
		error = in6m_get_source(inm, &lims->im6s_addr, 0, &nims);
		++schanged;
		if (error)
			break;
		im6s_merge(nims, lims, 0);
	}
	if (error) {
		struct ip6_msource *bims;

		RB_FOREACH_REVERSE_FROM(ims, ip6_msource_tree, nims) {
			lims = (struct in6_msource *)ims;
			if (lims->im6sl_st[0] == lims->im6sl_st[1])
				continue;
			(void)in6m_get_source(inm, &lims->im6s_addr, 1, &bims);
			if (bims == NULL)
				continue;
			im6s_merge(bims, lims, 1);
		}
		goto out_reap;
	}

	CTR3(KTR_MLD, "%s: imf filters in-mode: %d at t0, %d at t1",
	    __func__, nsrc0, nsrc1);

	/* Handle transition between INCLUDE {n} and INCLUDE {} on socket. */
	if (imf->im6f_st[0] == imf->im6f_st[1] &&
	    imf->im6f_st[1] == MCAST_INCLUDE) {
		if (nsrc1 == 0) {
			CTR1(KTR_MLD, "%s: --in on inm at t1", __func__);
			--inm->in6m_st[1].iss_in;
		}
	}

	/* Handle filter mode transition on socket. */
	if (imf->im6f_st[0] != imf->im6f_st[1]) {
		CTR3(KTR_MLD, "%s: imf transition %d to %d",
		    __func__, imf->im6f_st[0], imf->im6f_st[1]);

		if (imf->im6f_st[0] == MCAST_EXCLUDE) {
			CTR1(KTR_MLD, "%s: --ex on inm at t1", __func__);
			--inm->in6m_st[1].iss_ex;
		} else if (imf->im6f_st[0] == MCAST_INCLUDE) {
			CTR1(KTR_MLD, "%s: --in on inm at t1", __func__);
			--inm->in6m_st[1].iss_in;
		}

		if (imf->im6f_st[1] == MCAST_EXCLUDE) {
			CTR1(KTR_MLD, "%s: ex++ on inm at t1", __func__);
			inm->in6m_st[1].iss_ex++;
		} else if (imf->im6f_st[1] == MCAST_INCLUDE && nsrc1 > 0) {
			CTR1(KTR_MLD, "%s: in++ on inm at t1", __func__);
			inm->in6m_st[1].iss_in++;
		}
	}

	/*
	 * Track inm filter state in terms of listener counts.
	 * If there are any exclusive listeners, stack-wide
	 * membership is exclusive.
	 * Otherwise, if only inclusive listeners, stack-wide is inclusive.
	 * If no listeners remain, state is undefined at t1,
	 * and the MLD lifecycle for this group should finish.
	 */
	if (inm->in6m_st[1].iss_ex > 0) {
		CTR1(KTR_MLD, "%s: transition to EX", __func__);
		inm->in6m_st[1].iss_fmode = MCAST_EXCLUDE;
	} else if (inm->in6m_st[1].iss_in > 0) {
		CTR1(KTR_MLD, "%s: transition to IN", __func__);
		inm->in6m_st[1].iss_fmode = MCAST_INCLUDE;
	} else {
		CTR1(KTR_MLD, "%s: transition to UNDEF", __func__);
		inm->in6m_st[1].iss_fmode = MCAST_UNDEFINED;
	}

	/* Decrement ASM listener count on transition out of ASM mode. */
	if (imf->im6f_st[0] == MCAST_EXCLUDE && nsrc0 == 0) {
		if ((imf->im6f_st[1] != MCAST_EXCLUDE) ||
		    (imf->im6f_st[1] == MCAST_EXCLUDE && nsrc1 > 0)) {
			CTR1(KTR_MLD, "%s: --asm on inm at t1", __func__);
			--inm->in6m_st[1].iss_asm;
		}
	}

	/* Increment ASM listener count on transition to ASM mode. */
	if (imf->im6f_st[1] == MCAST_EXCLUDE && nsrc1 == 0) {
		CTR1(KTR_MLD, "%s: asm++ on inm at t1", __func__);
		inm->in6m_st[1].iss_asm++;
	}

	CTR3(KTR_MLD, "%s: merged imf %p to inm %p", __func__, imf, inm);
	in6m_print(inm);

out_reap:
	if (schanged > 0) {
		CTR1(KTR_MLD, "%s: sources changed; reaping", __func__);
		in6m_reap(inm);
	}
	return (error);
}

/*
 * Mark an in6_multi's filter set deltas as committed.
 * Called by MLD after a state change has been enqueued.
 */
void
in6m_commit(struct in6_multi *inm)
{
	struct ip6_msource	*ims;

	CTR2(KTR_MLD, "%s: commit inm %p", __func__, inm);
	CTR1(KTR_MLD, "%s: pre commit:", __func__);
	in6m_print(inm);

	RB_FOREACH(ims, ip6_msource_tree, &inm->in6m_srcs) {
		ims->im6s_st[0] = ims->im6s_st[1];
	}
	inm->in6m_st[0] = inm->in6m_st[1];
}

/*
 * Reap unreferenced nodes from an in6_multi's filter set.
 */
static void
in6m_reap(struct in6_multi *inm)
{
	struct ip6_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip6_msource_tree, &inm->in6m_srcs, tims) {
		if (ims->im6s_st[0].ex > 0 || ims->im6s_st[0].in > 0 ||
		    ims->im6s_st[1].ex > 0 || ims->im6s_st[1].in > 0 ||
		    ims->im6s_stp != 0)
			continue;
		CTR2(KTR_MLD, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip6_msource_tree, &inm->in6m_srcs, ims);
		free(ims, M_IP6MSOURCE);
		inm->in6m_nsrc--;
	}
}

/*
 * Purge all source nodes from an in6_multi's filter set.
 */
static void
in6m_purge(struct in6_multi *inm)
{
	struct ip6_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip6_msource_tree, &inm->in6m_srcs, tims) {
		CTR2(KTR_MLD, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip6_msource_tree, &inm->in6m_srcs, ims);
		free(ims, M_IP6MSOURCE);
		inm->in6m_nsrc--;
	}
	/* Free state-change requests that might be queued. */
	mbufq_drain(&inm->in6m_scq);
}

/*
 * Join a multicast address w/o sources.
 * KAME compatibility entry point.
 *
 * SMPng: Assume no mc locks held by caller.
 */
int
in6_joingroup(struct ifnet *ifp, const struct in6_addr *mcaddr,
    /*const*/ struct in6_mfilter *imf, struct in6_multi **pinm,
    const int delay)
{
	int error;

	IN6_MULTI_LOCK();
	error = in6_joingroup_locked(ifp, mcaddr, NULL, pinm, delay);
	IN6_MULTI_UNLOCK();
	return (error);
}

/*
 * Join a multicast group; real entry point.
 *
 * Only preserves atomicity at inm level.
 * NOTE: imf argument cannot be const due to sys/tree.h limitations.
 *
 * If the MLD downcall fails, the group is not joined, and an error
 * code is returned.
 */
int
in6_joingroup_locked(struct ifnet *ifp, const struct in6_addr *mcaddr,
    /*const*/ struct in6_mfilter *imf, struct in6_multi **pinm,
    const int delay)
{
	struct in6_multi_head    inmh;
	struct in6_mfilter	 timf;
	struct in6_multi	*inm;
	struct ifmultiaddr *ifma;
	int			 error;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	/*
	 * Sanity: Check scope zone ID was set for ifp, if and
	 * only if group is scoped to an interface.
	 */
	KASSERT(IN6_IS_ADDR_MULTICAST(mcaddr),
	    ("%s: not a multicast address", __func__));
	if (IN6_IS_ADDR_MC_LINKLOCAL(mcaddr) ||
	    IN6_IS_ADDR_MC_INTFACELOCAL(mcaddr)) {
		KASSERT(mcaddr->s6_addr16[1] != 0,
		    ("%s: scope zone ID not set", __func__));
	}

	IN6_MULTI_LOCK_ASSERT();
	IN6_MULTI_LIST_UNLOCK_ASSERT();

	CTR4(KTR_MLD, "%s: join %s on %p(%s))", __func__,
	    ip6_sprintf(ip6tbuf, mcaddr), ifp, if_name(ifp));

	error = 0;
	inm = NULL;

	/*
	 * If no imf was specified (i.e. kernel consumer),
	 * fake one up and assume it is an ASM join.
	 */
	if (imf == NULL) {
		im6f_init(&timf, MCAST_UNDEFINED, MCAST_EXCLUDE);
		imf = &timf;
	}
	error = in6_getmulti(ifp, mcaddr, &inm);
	if (error) {
		CTR1(KTR_MLD, "%s: in6_getmulti() failure", __func__);
		return (error);
	}

	IN6_MULTI_LIST_LOCK();
	CTR1(KTR_MLD, "%s: merge inm state", __func__);
	error = in6m_merge(inm, imf);
	if (error) {
		CTR1(KTR_MLD, "%s: failed to merge inm state", __func__);
		goto out_in6m_release;
	}

	CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
	error = mld_change_state(inm, delay);
	if (error) {
		CTR1(KTR_MLD, "%s: failed to update source", __func__);
		goto out_in6m_release;
	}

out_in6m_release:
	SLIST_INIT(&inmh);
	if (error) {
		struct epoch_tracker et;

		CTR2(KTR_MLD, "%s: dropping ref on %p", __func__, inm);
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_protospec == inm) {
				ifma->ifma_protospec = NULL;
				break;
			}
		}
		in6m_disconnect_locked(&inmh, inm);
		in6m_rele_locked(&inmh, inm);
		NET_EPOCH_EXIT(et);
	} else {
		*pinm = inm;
	}
	IN6_MULTI_LIST_UNLOCK();
	in6m_release_list_deferred(&inmh);
	return (error);
}

/*
 * Leave a multicast group; unlocked entry point.
 */
int
in6_leavegroup(struct in6_multi *inm, /*const*/ struct in6_mfilter *imf)
{
	int error;

	IN6_MULTI_LOCK();
	error = in6_leavegroup_locked(inm, imf);
	IN6_MULTI_UNLOCK();
	return (error);
}

/*
 * Leave a multicast group; real entry point.
 * All source filters will be expunged.
 *
 * Only preserves atomicity at inm level.
 *
 * Holding the write lock for the INP which contains imf
 * is highly advisable. We can't assert for it as imf does not
 * contain a back-pointer to the owning inp.
 *
 * Note: This is not the same as in6m_release(*) as this function also
 * makes a state change downcall into MLD.
 */
int
in6_leavegroup_locked(struct in6_multi *inm, /*const*/ struct in6_mfilter *imf)
{
	struct in6_multi_head	 inmh;
	struct in6_mfilter	 timf;
	struct ifnet *ifp;
	int			 error;
#ifdef KTR
	char			 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	error = 0;

	IN6_MULTI_LOCK_ASSERT();

	CTR5(KTR_MLD, "%s: leave inm %p, %s/%s, imf %p", __func__,
	    inm, ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    (in6m_is_ifp_detached(inm) ? "null" : if_name(inm->in6m_ifp)),
	    imf);

	/*
	 * If no imf was specified (i.e. kernel consumer),
	 * fake one up and assume it is an ASM join.
	 */
	if (imf == NULL) {
		im6f_init(&timf, MCAST_EXCLUDE, MCAST_UNDEFINED);
		imf = &timf;
	}

	/*
	 * Begin state merge transaction at MLD layer.
	 *
	 * As this particular invocation should not cause any memory
	 * to be allocated, and there is no opportunity to roll back
	 * the transaction, it MUST NOT fail.
	 */

	ifp = inm->in6m_ifp;
	IN6_MULTI_LIST_LOCK();
	CTR1(KTR_MLD, "%s: merge inm state", __func__);
	error = in6m_merge(inm, imf);
	KASSERT(error == 0, ("%s: failed to merge inm state", __func__));

	CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
	error = 0;
	if (ifp)
		error = mld_change_state(inm, 0);
	if (error)
		CTR1(KTR_MLD, "%s: failed mld downcall", __func__);

	CTR2(KTR_MLD, "%s: dropping ref on %p", __func__, inm);
	if (ifp)
		IF_ADDR_WLOCK(ifp);

	SLIST_INIT(&inmh);
	if (inm->in6m_refcount == 1)
		in6m_disconnect_locked(&inmh, inm);
	in6m_rele_locked(&inmh, inm);
	if (ifp)
		IF_ADDR_WUNLOCK(ifp);
	IN6_MULTI_LIST_UNLOCK();
	in6m_release_list_deferred(&inmh);
	return (error);
}


/*
 * Block or unblock an ASM multicast source on an inpcb.
 * This implements the delta-based API described in RFC 3678.
 *
 * The delta-based API applies only to exclusive-mode memberships.
 * An MLD downcall will be performed.
 *
 * SMPng: NOTE: Must take Giant as a join may create a new ifma.
 *
 * Return 0 if successful, otherwise return an appropriate error code.
 */
static int
in6p_block_unblock_source(struct inpcb *inp, struct sockopt *sopt)
{
	struct group_source_req		 gsr;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in6_mfilter		*imf;
	struct ip6_moptions		*imo;
	struct in6_msource		*ims;
	struct in6_multi			*inm;
	size_t				 idx;
	uint16_t			 fmode;
	int				 error, doblock;
#ifdef KTR
	char				 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	ifp = NULL;
	error = 0;
	doblock = 0;

	memset(&gsr, 0, sizeof(struct group_source_req));
	gsa = (sockunion_t *)&gsr.gsr_group;
	ssa = (sockunion_t *)&gsr.gsr_source;

	switch (sopt->sopt_name) {
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		error = sooptcopyin(sopt, &gsr,
		    sizeof(struct group_source_req),
		    sizeof(struct group_source_req));
		if (error)
			return (error);

		if (gsa->sin6.sin6_family != AF_INET6 ||
		    gsa->sin6.sin6_len != sizeof(struct sockaddr_in6))
			return (EINVAL);

		if (ssa->sin6.sin6_family != AF_INET6 ||
		    ssa->sin6.sin6_len != sizeof(struct sockaddr_in6))
			return (EINVAL);

		if (gsr.gsr_interface == 0 || V_if_index < gsr.gsr_interface)
			return (EADDRNOTAVAIL);

		ifp = ifnet_byindex(gsr.gsr_interface);

		if (sopt->sopt_name == MCAST_BLOCK_SOURCE)
			doblock = 1;
		break;

	default:
		CTR2(KTR_MLD, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (!IN6_IS_ADDR_MULTICAST(&gsa->sin6.sin6_addr))
		return (EINVAL);

	(void)in6_setscope(&gsa->sin6.sin6_addr, ifp, NULL);

	/*
	 * Check if we are actually a member of this group.
	 */
	imo = in6p_findmoptions(inp);
	idx = im6o_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->im6o_mfilters == NULL) {
		error = EADDRNOTAVAIL;
		goto out_in6p_locked;
	}

	KASSERT(imo->im6o_mfilters != NULL,
	    ("%s: im6o_mfilters not allocated", __func__));
	imf = &imo->im6o_mfilters[idx];
	inm = imo->im6o_membership[idx];

	/*
	 * Attempting to use the delta-based API on an
	 * non exclusive-mode membership is an error.
	 */
	fmode = imf->im6f_st[0];
	if (fmode != MCAST_EXCLUDE) {
		error = EINVAL;
		goto out_in6p_locked;
	}

	/*
	 * Deal with error cases up-front:
	 *  Asked to block, but already blocked; or
	 *  Asked to unblock, but nothing to unblock.
	 * If adding a new block entry, allocate it.
	 */
	ims = im6o_match_source(imo, idx, &ssa->sa);
	if ((ims != NULL && doblock) || (ims == NULL && !doblock)) {
		CTR3(KTR_MLD, "%s: source %s %spresent", __func__,
		    ip6_sprintf(ip6tbuf, &ssa->sin6.sin6_addr),
		    doblock ? "" : "not ");
		error = EADDRNOTAVAIL;
		goto out_in6p_locked;
	}

	INP_WLOCK_ASSERT(inp);

	/*
	 * Begin state merge transaction at socket layer.
	 */
	if (doblock) {
		CTR2(KTR_MLD, "%s: %s source", __func__, "block");
		ims = im6f_graft(imf, fmode, &ssa->sin6);
		if (ims == NULL)
			error = ENOMEM;
	} else {
		CTR2(KTR_MLD, "%s: %s source", __func__, "allow");
		error = im6f_prune(imf, &ssa->sin6);
	}

	if (error) {
		CTR1(KTR_MLD, "%s: merge imf state failed", __func__);
		goto out_im6f_rollback;
	}

	/*
	 * Begin state merge transaction at MLD layer.
	 */
	IN6_MULTI_LIST_LOCK();
	CTR1(KTR_MLD, "%s: merge inm state", __func__);
	error = in6m_merge(inm, imf);
	if (error)
		CTR1(KTR_MLD, "%s: failed to merge inm state", __func__);
	else {
		CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
		error = mld_change_state(inm, 0);
		if (error)
			CTR1(KTR_MLD, "%s: failed mld downcall", __func__);
	}

	IN6_MULTI_LIST_UNLOCK();

out_im6f_rollback:
	if (error)
		im6f_rollback(imf);
	else
		im6f_commit(imf);

	im6f_reap(imf);

out_in6p_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Given an inpcb, return its multicast options structure pointer.  Accepts
 * an unlocked inpcb pointer, but will return it locked.  May sleep.
 *
 * SMPng: NOTE: Potentially calls malloc(M_WAITOK) with Giant held.
 * SMPng: NOTE: Returns with the INP write lock held.
 */
static struct ip6_moptions *
in6p_findmoptions(struct inpcb *inp)
{
	struct ip6_moptions	 *imo;
	struct in6_multi		**immp;
	struct in6_mfilter	 *imfp;
	size_t			  idx;

	INP_WLOCK(inp);
	if (inp->in6p_moptions != NULL)
		return (inp->in6p_moptions);

	INP_WUNLOCK(inp);

	imo = malloc(sizeof(*imo), M_IP6MOPTS, M_WAITOK);
	immp = malloc(sizeof(*immp) * IPV6_MIN_MEMBERSHIPS, M_IP6MOPTS,
	    M_WAITOK | M_ZERO);
	imfp = malloc(sizeof(struct in6_mfilter) * IPV6_MIN_MEMBERSHIPS,
	    M_IN6MFILTER, M_WAITOK);

	imo->im6o_multicast_ifp = NULL;
	imo->im6o_multicast_hlim = V_ip6_defmcasthlim;
	imo->im6o_multicast_loop = in6_mcast_loop;
	imo->im6o_num_memberships = 0;
	imo->im6o_max_memberships = IPV6_MIN_MEMBERSHIPS;
	imo->im6o_membership = immp;

	/* Initialize per-group source filters. */
	for (idx = 0; idx < IPV6_MIN_MEMBERSHIPS; idx++)
		im6f_init(&imfp[idx], MCAST_UNDEFINED, MCAST_EXCLUDE);
	imo->im6o_mfilters = imfp;

	INP_WLOCK(inp);
	if (inp->in6p_moptions != NULL) {
		free(imfp, M_IN6MFILTER);
		free(immp, M_IP6MOPTS);
		free(imo, M_IP6MOPTS);
		return (inp->in6p_moptions);
	}
	inp->in6p_moptions = imo;
	return (imo);
}

/*
 * Discard the IPv6 multicast options (and source filters).
 *
 * SMPng: NOTE: assumes INP write lock is held.
 *
 * XXX can all be safely deferred to epoch_call
 *
 */

static void
inp_gcmoptions(struct ip6_moptions *imo)
{
	struct in6_mfilter	*imf;
	struct in6_multi *inm;
	struct ifnet *ifp;
	size_t			 idx, nmships;

	nmships = imo->im6o_num_memberships;
	for (idx = 0; idx < nmships; ++idx) {
		imf = imo->im6o_mfilters ? &imo->im6o_mfilters[idx] : NULL;
		if (imf)
			im6f_leave(imf);
		inm = imo->im6o_membership[idx];
		ifp = inm->in6m_ifp;
		if (ifp != NULL) {
			CURVNET_SET(ifp->if_vnet);
			(void)in6_leavegroup(inm, imf);
			CURVNET_RESTORE();
		} else {
			(void)in6_leavegroup(inm, imf);
		}
		if (imf)
			im6f_purge(imf);
	}

	if (imo->im6o_mfilters)
		free(imo->im6o_mfilters, M_IN6MFILTER);
	free(imo->im6o_membership, M_IP6MOPTS);
	free(imo, M_IP6MOPTS);
}

void
ip6_freemoptions(struct ip6_moptions *imo)
{
	if (imo == NULL)
		return;
	inp_gcmoptions(imo);
}

/*
 * Atomically get source filters on a socket for an IPv6 multicast group.
 * Called with INP lock held; returns with lock released.
 */
static int
in6p_get_source_filters(struct inpcb *inp, struct sockopt *sopt)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*gsa;
	struct ifnet		*ifp;
	struct ip6_moptions	*imo;
	struct in6_mfilter	*imf;
	struct ip6_msource	*ims;
	struct in6_msource	*lims;
	struct sockaddr_in6	*psin;
	struct sockaddr_storage	*ptss;
	struct sockaddr_storage	*tss;
	int			 error;
	size_t			 idx, nsrcs, ncsrcs;

	INP_WLOCK_ASSERT(inp);

	imo = inp->in6p_moptions;
	KASSERT(imo != NULL, ("%s: null ip6_moptions", __func__));

	INP_WUNLOCK(inp);

	error = sooptcopyin(sopt, &msfr, sizeof(struct __msfilterreq),
	    sizeof(struct __msfilterreq));
	if (error)
		return (error);

	if (msfr.msfr_group.ss_family != AF_INET6 ||
	    msfr.msfr_group.ss_len != sizeof(struct sockaddr_in6))
		return (EINVAL);

	gsa = (sockunion_t *)&msfr.msfr_group;
	if (!IN6_IS_ADDR_MULTICAST(&gsa->sin6.sin6_addr))
		return (EINVAL);

	if (msfr.msfr_ifindex == 0 || V_if_index < msfr.msfr_ifindex)
		return (EADDRNOTAVAIL);
	ifp = ifnet_byindex(msfr.msfr_ifindex);
	if (ifp == NULL)
		return (EADDRNOTAVAIL);
	(void)in6_setscope(&gsa->sin6.sin6_addr, ifp, NULL);

	INP_WLOCK(inp);

	/*
	 * Lookup group on the socket.
	 */
	idx = im6o_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->im6o_mfilters == NULL) {
		INP_WUNLOCK(inp);
		return (EADDRNOTAVAIL);
	}
	imf = &imo->im6o_mfilters[idx];

	/*
	 * Ignore memberships which are in limbo.
	 */
	if (imf->im6f_st[1] == MCAST_UNDEFINED) {
		INP_WUNLOCK(inp);
		return (EAGAIN);
	}
	msfr.msfr_fmode = imf->im6f_st[1];

	/*
	 * If the user specified a buffer, copy out the source filter
	 * entries to userland gracefully.
	 * We only copy out the number of entries which userland
	 * has asked for, but we always tell userland how big the
	 * buffer really needs to be.
	 */
	if (msfr.msfr_nsrcs > in6_mcast_maxsocksrc)
		msfr.msfr_nsrcs = in6_mcast_maxsocksrc;
	tss = NULL;
	if (msfr.msfr_srcs != NULL && msfr.msfr_nsrcs > 0) {
		tss = malloc(sizeof(struct sockaddr_storage) * msfr.msfr_nsrcs,
		    M_TEMP, M_NOWAIT | M_ZERO);
		if (tss == NULL) {
			INP_WUNLOCK(inp);
			return (ENOBUFS);
		}
	}

	/*
	 * Count number of sources in-mode at t0.
	 * If buffer space exists and remains, copy out source entries.
	 */
	nsrcs = msfr.msfr_nsrcs;
	ncsrcs = 0;
	ptss = tss;
	RB_FOREACH(ims, ip6_msource_tree, &imf->im6f_sources) {
		lims = (struct in6_msource *)ims;
		if (lims->im6sl_st[0] == MCAST_UNDEFINED ||
		    lims->im6sl_st[0] != imf->im6f_st[0])
			continue;
		++ncsrcs;
		if (tss != NULL && nsrcs > 0) {
			psin = (struct sockaddr_in6 *)ptss;
			psin->sin6_family = AF_INET6;
			psin->sin6_len = sizeof(struct sockaddr_in6);
			psin->sin6_addr = lims->im6s_addr;
			psin->sin6_port = 0;
			--nsrcs;
			++ptss;
		}
	}

	INP_WUNLOCK(inp);

	if (tss != NULL) {
		error = copyout(tss, msfr.msfr_srcs,
		    sizeof(struct sockaddr_storage) * msfr.msfr_nsrcs);
		free(tss, M_TEMP);
		if (error)
			return (error);
	}

	msfr.msfr_nsrcs = ncsrcs;
	error = sooptcopyout(sopt, &msfr, sizeof(struct __msfilterreq));

	return (error);
}

/*
 * Return the IP multicast options in response to user getsockopt().
 */
int
ip6_getmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	struct ip6_moptions	*im6o;
	int			 error;
	u_int			 optval;

	INP_WLOCK(inp);
	im6o = inp->in6p_moptions;
	/*
	 * If socket is neither of type SOCK_RAW or SOCK_DGRAM,
	 * or is a divert socket, reject it.
	 */
	if (inp->inp_socket->so_proto->pr_protocol == IPPROTO_DIVERT ||
	    (inp->inp_socket->so_proto->pr_type != SOCK_RAW &&
	    inp->inp_socket->so_proto->pr_type != SOCK_DGRAM)) {
		INP_WUNLOCK(inp);
		return (EOPNOTSUPP);
	}

	error = 0;
	switch (sopt->sopt_name) {
	case IPV6_MULTICAST_IF:
		if (im6o == NULL || im6o->im6o_multicast_ifp == NULL) {
			optval = 0;
		} else {
			optval = im6o->im6o_multicast_ifp->if_index;
		}
		INP_WUNLOCK(inp);
		error = sooptcopyout(sopt, &optval, sizeof(u_int));
		break;

	case IPV6_MULTICAST_HOPS:
		if (im6o == NULL)
			optval = V_ip6_defmcasthlim;
		else
			optval = im6o->im6o_multicast_hlim;
		INP_WUNLOCK(inp);
		error = sooptcopyout(sopt, &optval, sizeof(u_int));
		break;

	case IPV6_MULTICAST_LOOP:
		if (im6o == NULL)
			optval = in6_mcast_loop; /* XXX VIMAGE */
		else
			optval = im6o->im6o_multicast_loop;
		INP_WUNLOCK(inp);
		error = sooptcopyout(sopt, &optval, sizeof(u_int));
		break;

	case IPV6_MSFILTER:
		if (im6o == NULL) {
			error = EADDRNOTAVAIL;
			INP_WUNLOCK(inp);
		} else {
			error = in6p_get_source_filters(inp, sopt);
		}
		break;

	default:
		INP_WUNLOCK(inp);
		error = ENOPROTOOPT;
		break;
	}

	INP_UNLOCK_ASSERT(inp);

	return (error);
}

/*
 * Look up the ifnet to use for a multicast group membership,
 * given the address of an IPv6 group.
 *
 * This routine exists to support legacy IPv6 multicast applications.
 *
 * If inp is non-NULL, use this socket's current FIB number for any
 * required FIB lookup. Look up the group address in the unicast FIB,
 * and use its ifp; usually, this points to the default next-hop.
 * If the FIB lookup fails, return NULL.
 *
 * FUTURE: Support multiple forwarding tables for IPv6.
 *
 * Returns NULL if no ifp could be found.
 */
static struct ifnet *
in6p_lookup_mcast_ifp(const struct inpcb *in6p,
    const struct sockaddr_in6 *gsin6)
{
	struct nhop6_basic	nh6;
	struct in6_addr		dst;
	uint32_t		scopeid;
	uint32_t		fibnum;

	KASSERT(in6p->inp_vflag & INP_IPV6,
	    ("%s: not INP_IPV6 inpcb", __func__));
	KASSERT(gsin6->sin6_family == AF_INET6,
	    ("%s: not AF_INET6 group", __func__));

	in6_splitscope(&gsin6->sin6_addr, &dst, &scopeid);
	fibnum = in6p ? in6p->inp_inc.inc_fibnum : RT_DEFAULT_FIB;
	if (fib6_lookup_nh_basic(fibnum, &dst, scopeid, 0, 0, &nh6) != 0)
		return (NULL);

	return (nh6.nh_ifp);
}

/*
 * Join an IPv6 multicast group, possibly with a source.
 *
 * FIXME: The KAME use of the unspecified address (::)
 * to join *all* multicast groups is currently unsupported.
 */
static int
in6p_join_group(struct inpcb *inp, struct sockopt *sopt)
{
	struct in6_multi_head		 inmh;
	struct group_source_req		 gsr;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in6_mfilter		*imf;
	struct ip6_moptions		*imo;
	struct in6_multi		*inm;
	struct in6_msource		*lims;
	size_t				 idx;
	int				 error, is_new;

	SLIST_INIT(&inmh);
	ifp = NULL;
	imf = NULL;
	lims = NULL;
	error = 0;
	is_new = 0;

	memset(&gsr, 0, sizeof(struct group_source_req));
	gsa = (sockunion_t *)&gsr.gsr_group;
	gsa->ss.ss_family = AF_UNSPEC;
	ssa = (sockunion_t *)&gsr.gsr_source;
	ssa->ss.ss_family = AF_UNSPEC;

	/*
	 * Chew everything into struct group_source_req.
	 * Overwrite the port field if present, as the sockaddr
	 * being copied in may be matched with a binary comparison.
	 * Ignore passed-in scope ID.
	 */
	switch (sopt->sopt_name) {
	case IPV6_JOIN_GROUP: {
		struct ipv6_mreq mreq;

		error = sooptcopyin(sopt, &mreq, sizeof(struct ipv6_mreq),
		    sizeof(struct ipv6_mreq));
		if (error)
			return (error);

		gsa->sin6.sin6_family = AF_INET6;
		gsa->sin6.sin6_len = sizeof(struct sockaddr_in6);
		gsa->sin6.sin6_addr = mreq.ipv6mr_multiaddr;

		if (mreq.ipv6mr_interface == 0) {
			ifp = in6p_lookup_mcast_ifp(inp, &gsa->sin6);
		} else {
			if (V_if_index < mreq.ipv6mr_interface)
				return (EADDRNOTAVAIL);
			ifp = ifnet_byindex(mreq.ipv6mr_interface);
		}
		CTR3(KTR_MLD, "%s: ipv6mr_interface = %d, ifp = %p",
		    __func__, mreq.ipv6mr_interface, ifp);
	} break;

	case MCAST_JOIN_GROUP:
	case MCAST_JOIN_SOURCE_GROUP:
		if (sopt->sopt_name == MCAST_JOIN_GROUP) {
			error = sooptcopyin(sopt, &gsr,
			    sizeof(struct group_req),
			    sizeof(struct group_req));
		} else if (sopt->sopt_name == MCAST_JOIN_SOURCE_GROUP) {
			error = sooptcopyin(sopt, &gsr,
			    sizeof(struct group_source_req),
			    sizeof(struct group_source_req));
		}
		if (error)
			return (error);

		if (gsa->sin6.sin6_family != AF_INET6 ||
		    gsa->sin6.sin6_len != sizeof(struct sockaddr_in6))
			return (EINVAL);

		if (sopt->sopt_name == MCAST_JOIN_SOURCE_GROUP) {
			if (ssa->sin6.sin6_family != AF_INET6 ||
			    ssa->sin6.sin6_len != sizeof(struct sockaddr_in6))
				return (EINVAL);
			if (IN6_IS_ADDR_MULTICAST(&ssa->sin6.sin6_addr))
				return (EINVAL);
			/*
			 * TODO: Validate embedded scope ID in source
			 * list entry against passed-in ifp, if and only
			 * if source list filter entry is iface or node local.
			 */
			in6_clearscope(&ssa->sin6.sin6_addr);
			ssa->sin6.sin6_port = 0;
			ssa->sin6.sin6_scope_id = 0;
		}

		if (gsr.gsr_interface == 0 || V_if_index < gsr.gsr_interface)
			return (EADDRNOTAVAIL);
		ifp = ifnet_byindex(gsr.gsr_interface);
		break;

	default:
		CTR2(KTR_MLD, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (!IN6_IS_ADDR_MULTICAST(&gsa->sin6.sin6_addr))
		return (EINVAL);

	if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0)
		return (EADDRNOTAVAIL);

	gsa->sin6.sin6_port = 0;
	gsa->sin6.sin6_scope_id = 0;

	/*
	 * Always set the scope zone ID on memberships created from userland.
	 * Use the passed-in ifp to do this.
	 * XXX The in6_setscope() return value is meaningless.
	 * XXX SCOPE6_LOCK() is taken by in6_setscope().
	 */
	(void)in6_setscope(&gsa->sin6.sin6_addr, ifp, NULL);

	imo = in6p_findmoptions(inp);
	idx = im6o_match_group(imo, ifp, &gsa->sa);
	if (idx == -1) {
		is_new = 1;
	} else {
		inm = imo->im6o_membership[idx];
		imf = &imo->im6o_mfilters[idx];
		if (ssa->ss.ss_family != AF_UNSPEC) {
			/*
			 * MCAST_JOIN_SOURCE_GROUP on an exclusive membership
			 * is an error. On an existing inclusive membership,
			 * it just adds the source to the filter list.
			 */
			if (imf->im6f_st[1] != MCAST_INCLUDE) {
				error = EINVAL;
				goto out_in6p_locked;
			}
			/*
			 * Throw out duplicates.
			 *
			 * XXX FIXME: This makes a naive assumption that
			 * even if entries exist for *ssa in this imf,
			 * they will be rejected as dupes, even if they
			 * are not valid in the current mode (in-mode).
			 *
			 * in6_msource is transactioned just as for anything
			 * else in SSM -- but note naive use of in6m_graft()
			 * below for allocating new filter entries.
			 *
			 * This is only an issue if someone mixes the
			 * full-state SSM API with the delta-based API,
			 * which is discouraged in the relevant RFCs.
			 */
			lims = im6o_match_source(imo, idx, &ssa->sa);
			if (lims != NULL /*&&
			    lims->im6sl_st[1] == MCAST_INCLUDE*/) {
				error = EADDRNOTAVAIL;
				goto out_in6p_locked;
			}
		} else {
			/*
			 * MCAST_JOIN_GROUP alone, on any existing membership,
			 * is rejected, to stop the same inpcb tying up
			 * multiple refs to the in_multi.
			 * On an existing inclusive membership, this is also
			 * an error; if you want to change filter mode,
			 * you must use the userland API setsourcefilter().
			 * XXX We don't reject this for imf in UNDEFINED
			 * state at t1, because allocation of a filter
			 * is atomic with allocation of a membership.
			 */
			error = EINVAL;
			goto out_in6p_locked;
		}
	}

	/*
	 * Begin state merge transaction at socket layer.
	 */
	INP_WLOCK_ASSERT(inp);

	if (is_new) {
		if (imo->im6o_num_memberships == imo->im6o_max_memberships) {
			error = im6o_grow(imo);
			if (error)
				goto out_in6p_locked;
		}
		/*
		 * Allocate the new slot upfront so we can deal with
		 * grafting the new source filter in same code path
		 * as for join-source on existing membership.
		 */
		idx = imo->im6o_num_memberships;
		imo->im6o_membership[idx] = NULL;
		imo->im6o_num_memberships++;
		KASSERT(imo->im6o_mfilters != NULL,
		    ("%s: im6f_mfilters vector was not allocated", __func__));
		imf = &imo->im6o_mfilters[idx];
		KASSERT(RB_EMPTY(&imf->im6f_sources),
		    ("%s: im6f_sources not empty", __func__));
	}

	/*
	 * Graft new source into filter list for this inpcb's
	 * membership of the group. The in6_multi may not have
	 * been allocated yet if this is a new membership, however,
	 * the in_mfilter slot will be allocated and must be initialized.
	 *
	 * Note: Grafting of exclusive mode filters doesn't happen
	 * in this path.
	 * XXX: Should check for non-NULL lims (node exists but may
	 * not be in-mode) for interop with full-state API.
	 */
	if (ssa->ss.ss_family != AF_UNSPEC) {
		/* Membership starts in IN mode */
		if (is_new) {
			CTR1(KTR_MLD, "%s: new join w/source", __func__);
			im6f_init(imf, MCAST_UNDEFINED, MCAST_INCLUDE);
		} else {
			CTR2(KTR_MLD, "%s: %s source", __func__, "allow");
		}
		lims = im6f_graft(imf, MCAST_INCLUDE, &ssa->sin6);
		if (lims == NULL) {
			CTR1(KTR_MLD, "%s: merge imf state failed",
			    __func__);
			error = ENOMEM;
			goto out_im6o_free;
		}
	} else {
		/* No address specified; Membership starts in EX mode */
		if (is_new) {
			CTR1(KTR_MLD, "%s: new join w/o source", __func__);
			im6f_init(imf, MCAST_UNDEFINED, MCAST_EXCLUDE);
		}
	}

	/*
	 * Begin state merge transaction at MLD layer.
	 */
	in_pcbref(inp);
	INP_WUNLOCK(inp);
	IN6_MULTI_LOCK();

	if (is_new) {
		error = in6_joingroup_locked(ifp, &gsa->sin6.sin6_addr, imf,
		    &inm, 0);
		if (error) {
			IN6_MULTI_UNLOCK();
			goto out_im6o_free;
		}
		/*
		 * NOTE: Refcount from in6_joingroup_locked()
		 * is protecting membership.
		 */
		imo->im6o_membership[idx] = inm;
	} else {
		CTR1(KTR_MLD, "%s: merge inm state", __func__);
		IN6_MULTI_LIST_LOCK();
		error = in6m_merge(inm, imf);
		if (error)
			CTR1(KTR_MLD, "%s: failed to merge inm state",
			    __func__);
		else {
			CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
			error = mld_change_state(inm, 0);
			if (error)
				CTR1(KTR_MLD, "%s: failed mld downcall",
				    __func__);
		}
		IN6_MULTI_LIST_UNLOCK();
	}

	IN6_MULTI_UNLOCK();
	INP_WLOCK(inp);
	if (in_pcbrele_wlocked(inp))
		return (ENXIO);
	if (error) {
		im6f_rollback(imf);
		if (is_new)
			im6f_purge(imf);
		else
			im6f_reap(imf);
	} else {
		im6f_commit(imf);
	}

out_im6o_free:
	if (error && is_new) {
		inm = imo->im6o_membership[idx];
		if (inm != NULL) {
			IN6_MULTI_LIST_LOCK();
			in6m_rele_locked(&inmh, inm);
			IN6_MULTI_LIST_UNLOCK();
		}
		imo->im6o_membership[idx] = NULL;
		--imo->im6o_num_memberships;
	}

out_in6p_locked:
	INP_WUNLOCK(inp);
	in6m_release_list_deferred(&inmh);
	return (error);
}

/*
 * Leave an IPv6 multicast group on an inpcb, possibly with a source.
 */
static int
in6p_leave_group(struct inpcb *inp, struct sockopt *sopt)
{
	struct ipv6_mreq		 mreq;
	struct group_source_req		 gsr;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in6_mfilter		*imf;
	struct ip6_moptions		*imo;
	struct in6_msource		*ims;
	struct in6_multi		*inm;
	uint32_t			 ifindex;
	size_t				 idx;
	int				 error, is_final;
#ifdef KTR
	char				 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	ifp = NULL;
	ifindex = 0;
	error = 0;
	is_final = 1;

	memset(&gsr, 0, sizeof(struct group_source_req));
	gsa = (sockunion_t *)&gsr.gsr_group;
	gsa->ss.ss_family = AF_UNSPEC;
	ssa = (sockunion_t *)&gsr.gsr_source;
	ssa->ss.ss_family = AF_UNSPEC;

	/*
	 * Chew everything passed in up into a struct group_source_req
	 * as that is easier to process.
	 * Note: Any embedded scope ID in the multicast group passed
	 * in by userland is ignored, the interface index is the recommended
	 * mechanism to specify an interface; see below.
	 */
	switch (sopt->sopt_name) {
	case IPV6_LEAVE_GROUP:
		error = sooptcopyin(sopt, &mreq, sizeof(struct ipv6_mreq),
		    sizeof(struct ipv6_mreq));
		if (error)
			return (error);
		gsa->sin6.sin6_family = AF_INET6;
		gsa->sin6.sin6_len = sizeof(struct sockaddr_in6);
		gsa->sin6.sin6_addr = mreq.ipv6mr_multiaddr;
		gsa->sin6.sin6_port = 0;
		gsa->sin6.sin6_scope_id = 0;
		ifindex = mreq.ipv6mr_interface;
		break;

	case MCAST_LEAVE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
		if (sopt->sopt_name == MCAST_LEAVE_GROUP) {
			error = sooptcopyin(sopt, &gsr,
			    sizeof(struct group_req),
			    sizeof(struct group_req));
		} else if (sopt->sopt_name == MCAST_LEAVE_SOURCE_GROUP) {
			error = sooptcopyin(sopt, &gsr,
			    sizeof(struct group_source_req),
			    sizeof(struct group_source_req));
		}
		if (error)
			return (error);

		if (gsa->sin6.sin6_family != AF_INET6 ||
		    gsa->sin6.sin6_len != sizeof(struct sockaddr_in6))
			return (EINVAL);
		if (sopt->sopt_name == MCAST_LEAVE_SOURCE_GROUP) {
			if (ssa->sin6.sin6_family != AF_INET6 ||
			    ssa->sin6.sin6_len != sizeof(struct sockaddr_in6))
				return (EINVAL);
			if (IN6_IS_ADDR_MULTICAST(&ssa->sin6.sin6_addr))
				return (EINVAL);
			/*
			 * TODO: Validate embedded scope ID in source
			 * list entry against passed-in ifp, if and only
			 * if source list filter entry is iface or node local.
			 */
			in6_clearscope(&ssa->sin6.sin6_addr);
		}
		gsa->sin6.sin6_port = 0;
		gsa->sin6.sin6_scope_id = 0;
		ifindex = gsr.gsr_interface;
		break;

	default:
		CTR2(KTR_MLD, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (!IN6_IS_ADDR_MULTICAST(&gsa->sin6.sin6_addr))
		return (EINVAL);

	/*
	 * Validate interface index if provided. If no interface index
	 * was provided separately, attempt to look the membership up
	 * from the default scope as a last resort to disambiguate
	 * the membership we are being asked to leave.
	 * XXX SCOPE6 lock potentially taken here.
	 */
	if (ifindex != 0) {
		if (V_if_index < ifindex)
			return (EADDRNOTAVAIL);
		ifp = ifnet_byindex(ifindex);
		if (ifp == NULL)
			return (EADDRNOTAVAIL);
		(void)in6_setscope(&gsa->sin6.sin6_addr, ifp, NULL);
	} else {
		error = sa6_embedscope(&gsa->sin6, V_ip6_use_defzone);
		if (error)
			return (EADDRNOTAVAIL);
		/*
		 * Some badly behaved applications don't pass an ifindex
		 * or a scope ID, which is an API violation. In this case,
		 * perform a lookup as per a v6 join.
		 *
		 * XXX For now, stomp on zone ID for the corner case.
		 * This is not the 'KAME way', but we need to see the ifp
		 * directly until such time as this implementation is
		 * refactored, assuming the scope IDs are the way to go.
		 */
		ifindex = ntohs(gsa->sin6.sin6_addr.s6_addr16[1]);
		if (ifindex == 0) {
			CTR2(KTR_MLD, "%s: warning: no ifindex, looking up "
			    "ifp for group %s.", __func__,
			    ip6_sprintf(ip6tbuf, &gsa->sin6.sin6_addr));
			ifp = in6p_lookup_mcast_ifp(inp, &gsa->sin6);
		} else {
			ifp = ifnet_byindex(ifindex);
		}
		if (ifp == NULL)
			return (EADDRNOTAVAIL);
	}

	CTR2(KTR_MLD, "%s: ifp = %p", __func__, ifp);
	KASSERT(ifp != NULL, ("%s: ifp did not resolve", __func__));

	/*
	 * Find the membership in the membership array.
	 */
	imo = in6p_findmoptions(inp);
	idx = im6o_match_group(imo, ifp, &gsa->sa);
	if (idx == -1) {
		error = EADDRNOTAVAIL;
		goto out_in6p_locked;
	}
	inm = imo->im6o_membership[idx];
	imf = &imo->im6o_mfilters[idx];

	if (ssa->ss.ss_family != AF_UNSPEC)
		is_final = 0;

	/*
	 * Begin state merge transaction at socket layer.
	 */
	INP_WLOCK_ASSERT(inp);

	/*
	 * If we were instructed only to leave a given source, do so.
	 * MCAST_LEAVE_SOURCE_GROUP is only valid for inclusive memberships.
	 */
	if (is_final) {
		im6f_leave(imf);
	} else {
		if (imf->im6f_st[0] == MCAST_EXCLUDE) {
			error = EADDRNOTAVAIL;
			goto out_in6p_locked;
		}
		ims = im6o_match_source(imo, idx, &ssa->sa);
		if (ims == NULL) {
			CTR3(KTR_MLD, "%s: source %p %spresent", __func__,
			    ip6_sprintf(ip6tbuf, &ssa->sin6.sin6_addr),
			    "not ");
			error = EADDRNOTAVAIL;
			goto out_in6p_locked;
		}
		CTR2(KTR_MLD, "%s: %s source", __func__, "block");
		error = im6f_prune(imf, &ssa->sin6);
		if (error) {
			CTR1(KTR_MLD, "%s: merge imf state failed",
			    __func__);
			goto out_in6p_locked;
		}
	}

	/*
	 * Begin state merge transaction at MLD layer.
	 */
	in_pcbref(inp);
	INP_WUNLOCK(inp);
	IN6_MULTI_LOCK();

	if (is_final) {
		/*
		 * Give up the multicast address record to which
		 * the membership points.
		 */
		(void)in6_leavegroup_locked(inm, imf);
	} else {
		CTR1(KTR_MLD, "%s: merge inm state", __func__);
		IN6_MULTI_LIST_LOCK();
		error = in6m_merge(inm, imf);
		if (error)
			CTR1(KTR_MLD, "%s: failed to merge inm state",
			    __func__);
		else {
			CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
			error = mld_change_state(inm, 0);
			if (error)
				CTR1(KTR_MLD, "%s: failed mld downcall",
				    __func__);
		}
		IN6_MULTI_LIST_UNLOCK();
	}

	IN6_MULTI_UNLOCK();
	INP_WLOCK(inp);
	if (in_pcbrele_wlocked(inp))
		return (ENXIO);

	if (error)
		im6f_rollback(imf);
	else
		im6f_commit(imf);

	im6f_reap(imf);

	if (is_final) {
		/* Remove the gap in the membership array. */
		for (++idx; idx < imo->im6o_num_memberships; ++idx) {
			imo->im6o_membership[idx-1] = imo->im6o_membership[idx];
			imo->im6o_mfilters[idx-1] = imo->im6o_mfilters[idx];
		}
		imo->im6o_num_memberships--;
	}

out_in6p_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Select the interface for transmitting IPv6 multicast datagrams.
 *
 * Either an instance of struct in6_addr or an instance of struct ipv6_mreqn
 * may be passed to this socket option. An address of in6addr_any or an
 * interface index of 0 is used to remove a previous selection.
 * When no interface is selected, one is chosen for every send.
 */
static int
in6p_set_multicast_if(struct inpcb *inp, struct sockopt *sopt)
{
	struct ifnet		*ifp;
	struct ip6_moptions	*imo;
	u_int			 ifindex;
	int			 error;

	if (sopt->sopt_valsize != sizeof(u_int))
		return (EINVAL);

	error = sooptcopyin(sopt, &ifindex, sizeof(u_int), sizeof(u_int));
	if (error)
		return (error);
	if (V_if_index < ifindex)
		return (EINVAL);
	if (ifindex == 0)
		ifp = NULL;
	else {
		ifp = ifnet_byindex(ifindex);
		if (ifp == NULL)
			return (EINVAL);
		if ((ifp->if_flags & IFF_MULTICAST) == 0)
			return (EADDRNOTAVAIL);
	}
	imo = in6p_findmoptions(inp);
	imo->im6o_multicast_ifp = ifp;
	INP_WUNLOCK(inp);

	return (0);
}

/*
 * Atomically set source filters on a socket for an IPv6 multicast group.
 *
 * SMPng: NOTE: Potentially calls malloc(M_WAITOK) with Giant held.
 */
static int
in6p_set_source_filters(struct inpcb *inp, struct sockopt *sopt)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*gsa;
	struct ifnet		*ifp;
	struct in6_mfilter	*imf;
	struct ip6_moptions	*imo;
	struct in6_multi		*inm;
	size_t			 idx;
	int			 error;

	error = sooptcopyin(sopt, &msfr, sizeof(struct __msfilterreq),
	    sizeof(struct __msfilterreq));
	if (error)
		return (error);

	if (msfr.msfr_nsrcs > in6_mcast_maxsocksrc)
		return (ENOBUFS);

	if (msfr.msfr_fmode != MCAST_EXCLUDE &&
	    msfr.msfr_fmode != MCAST_INCLUDE)
		return (EINVAL);

	if (msfr.msfr_group.ss_family != AF_INET6 ||
	    msfr.msfr_group.ss_len != sizeof(struct sockaddr_in6))
		return (EINVAL);

	gsa = (sockunion_t *)&msfr.msfr_group;
	if (!IN6_IS_ADDR_MULTICAST(&gsa->sin6.sin6_addr))
		return (EINVAL);

	gsa->sin6.sin6_port = 0;	/* ignore port */

	if (msfr.msfr_ifindex == 0 || V_if_index < msfr.msfr_ifindex)
		return (EADDRNOTAVAIL);
	ifp = ifnet_byindex(msfr.msfr_ifindex);
	if (ifp == NULL)
		return (EADDRNOTAVAIL);
	(void)in6_setscope(&gsa->sin6.sin6_addr, ifp, NULL);

	/*
	 * Take the INP write lock.
	 * Check if this socket is a member of this group.
	 */
	imo = in6p_findmoptions(inp);
	idx = im6o_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->im6o_mfilters == NULL) {
		error = EADDRNOTAVAIL;
		goto out_in6p_locked;
	}
	inm = imo->im6o_membership[idx];
	imf = &imo->im6o_mfilters[idx];

	/*
	 * Begin state merge transaction at socket layer.
	 */
	INP_WLOCK_ASSERT(inp);

	imf->im6f_st[1] = msfr.msfr_fmode;

	/*
	 * Apply any new source filters, if present.
	 * Make a copy of the user-space source vector so
	 * that we may copy them with a single copyin. This
	 * allows us to deal with page faults up-front.
	 */
	if (msfr.msfr_nsrcs > 0) {
		struct in6_msource	*lims;
		struct sockaddr_in6	*psin;
		struct sockaddr_storage	*kss, *pkss;
		int			 i;

		INP_WUNLOCK(inp);
 
		CTR2(KTR_MLD, "%s: loading %lu source list entries",
		    __func__, (unsigned long)msfr.msfr_nsrcs);
		kss = malloc(sizeof(struct sockaddr_storage) * msfr.msfr_nsrcs,
		    M_TEMP, M_WAITOK);
		error = copyin(msfr.msfr_srcs, kss,
		    sizeof(struct sockaddr_storage) * msfr.msfr_nsrcs);
		if (error) {
			free(kss, M_TEMP);
			return (error);
		}

		INP_WLOCK(inp);

		/*
		 * Mark all source filters as UNDEFINED at t1.
		 * Restore new group filter mode, as im6f_leave()
		 * will set it to INCLUDE.
		 */
		im6f_leave(imf);
		imf->im6f_st[1] = msfr.msfr_fmode;

		/*
		 * Update socket layer filters at t1, lazy-allocating
		 * new entries. This saves a bunch of memory at the
		 * cost of one RB_FIND() per source entry; duplicate
		 * entries in the msfr_nsrcs vector are ignored.
		 * If we encounter an error, rollback transaction.
		 *
		 * XXX This too could be replaced with a set-symmetric
		 * difference like loop to avoid walking from root
		 * every time, as the key space is common.
		 */
		for (i = 0, pkss = kss; i < msfr.msfr_nsrcs; i++, pkss++) {
			psin = (struct sockaddr_in6 *)pkss;
			if (psin->sin6_family != AF_INET6) {
				error = EAFNOSUPPORT;
				break;
			}
			if (psin->sin6_len != sizeof(struct sockaddr_in6)) {
				error = EINVAL;
				break;
			}
			if (IN6_IS_ADDR_MULTICAST(&psin->sin6_addr)) {
				error = EINVAL;
				break;
			}
			/*
			 * TODO: Validate embedded scope ID in source
			 * list entry against passed-in ifp, if and only
			 * if source list filter entry is iface or node local.
			 */
			in6_clearscope(&psin->sin6_addr);
			error = im6f_get_source(imf, psin, &lims);
			if (error)
				break;
			lims->im6sl_st[1] = imf->im6f_st[1];
		}
		free(kss, M_TEMP);
	}

	if (error)
		goto out_im6f_rollback;

	INP_WLOCK_ASSERT(inp);
	IN6_MULTI_LIST_LOCK();

	/*
	 * Begin state merge transaction at MLD layer.
	 */
	CTR1(KTR_MLD, "%s: merge inm state", __func__);
	error = in6m_merge(inm, imf);
	if (error)
		CTR1(KTR_MLD, "%s: failed to merge inm state", __func__);
	else {
		CTR1(KTR_MLD, "%s: doing mld downcall", __func__);
		error = mld_change_state(inm, 0);
		if (error)
			CTR1(KTR_MLD, "%s: failed mld downcall", __func__);
	}

	IN6_MULTI_LIST_UNLOCK();

out_im6f_rollback:
	if (error)
		im6f_rollback(imf);
	else
		im6f_commit(imf);

	im6f_reap(imf);

out_in6p_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Set the IP multicast options in response to user setsockopt().
 *
 * Many of the socket options handled in this function duplicate the
 * functionality of socket options in the regular unicast API. However,
 * it is not possible to merge the duplicate code, because the idempotence
 * of the IPv6 multicast part of the BSD Sockets API must be preserved;
 * the effects of these options must be treated as separate and distinct.
 *
 * SMPng: XXX: Unlocked read of inp_socket believed OK.
 */
int
ip6_setmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	struct ip6_moptions	*im6o;
	int			 error;

	error = 0;

	/*
	 * If socket is neither of type SOCK_RAW or SOCK_DGRAM,
	 * or is a divert socket, reject it.
	 */
	if (inp->inp_socket->so_proto->pr_protocol == IPPROTO_DIVERT ||
	    (inp->inp_socket->so_proto->pr_type != SOCK_RAW &&
	     inp->inp_socket->so_proto->pr_type != SOCK_DGRAM))
		return (EOPNOTSUPP);

	switch (sopt->sopt_name) {
	case IPV6_MULTICAST_IF:
		error = in6p_set_multicast_if(inp, sopt);
		break;

	case IPV6_MULTICAST_HOPS: {
		int hlim;

		if (sopt->sopt_valsize != sizeof(int)) {
			error = EINVAL;
			break;
		}
		error = sooptcopyin(sopt, &hlim, sizeof(hlim), sizeof(int));
		if (error)
			break;
		if (hlim < -1 || hlim > 255) {
			error = EINVAL;
			break;
		} else if (hlim == -1) {
			hlim = V_ip6_defmcasthlim;
		}
		im6o = in6p_findmoptions(inp);
		im6o->im6o_multicast_hlim = hlim;
		INP_WUNLOCK(inp);
		break;
	}

	case IPV6_MULTICAST_LOOP: {
		u_int loop;

		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.
		 */
		if (sopt->sopt_valsize != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		error = sooptcopyin(sopt, &loop, sizeof(u_int), sizeof(u_int));
		if (error)
			break;
		if (loop > 1) {
			error = EINVAL;
			break;
		}
		im6o = in6p_findmoptions(inp);
		im6o->im6o_multicast_loop = loop;
		INP_WUNLOCK(inp);
		break;
	}

	case IPV6_JOIN_GROUP:
	case MCAST_JOIN_GROUP:
	case MCAST_JOIN_SOURCE_GROUP:
		error = in6p_join_group(inp, sopt);
		break;

	case IPV6_LEAVE_GROUP:
	case MCAST_LEAVE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
		error = in6p_leave_group(inp, sopt);
		break;

	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		error = in6p_block_unblock_source(inp, sopt);
		break;

	case IPV6_MSFILTER:
		error = in6p_set_source_filters(inp, sopt);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	INP_UNLOCK_ASSERT(inp);

	return (error);
}

/*
 * Expose MLD's multicast filter mode and source list(s) to userland,
 * keyed by (ifindex, group).
 * The filter mode is written out as a uint32_t, followed by
 * 0..n of struct in6_addr.
 * For use by ifmcstat(8).
 * SMPng: NOTE: unlocked read of ifindex space.
 */
static int
sysctl_ip6_mcast_filters(SYSCTL_HANDLER_ARGS)
{
	struct in6_addr			 mcaddr;
	struct in6_addr			 src;
	struct epoch_tracker		 et;
	struct ifnet			*ifp;
	struct ifmultiaddr		*ifma;
	struct in6_multi		*inm;
	struct ip6_msource		*ims;
	int				*name;
	int				 retval;
	u_int				 namelen;
	uint32_t			 fmode, ifindex;
#ifdef KTR
	char				 ip6tbuf[INET6_ADDRSTRLEN];
#endif

	name = (int *)arg1;
	namelen = arg2;

	if (req->newptr != NULL)
		return (EPERM);

	/* int: ifindex + 4 * 32 bits of IPv6 address */
	if (namelen != 5)
		return (EINVAL);

	ifindex = name[0];
	if (ifindex <= 0 || ifindex > V_if_index) {
		CTR2(KTR_MLD, "%s: ifindex %u out of range",
		    __func__, ifindex);
		return (ENOENT);
	}

	memcpy(&mcaddr, &name[1], sizeof(struct in6_addr));
	if (!IN6_IS_ADDR_MULTICAST(&mcaddr)) {
		CTR2(KTR_MLD, "%s: group %s is not multicast",
		    __func__, ip6_sprintf(ip6tbuf, &mcaddr));
		return (EINVAL);
	}

	ifp = ifnet_byindex(ifindex);
	if (ifp == NULL) {
		CTR2(KTR_MLD, "%s: no ifp for ifindex %u",
		    __func__, ifindex);
		return (ENOENT);
	}
	/*
	 * Internal MLD lookups require that scope/zone ID is set.
	 */
	(void)in6_setscope(&mcaddr, ifp, NULL);

	retval = sysctl_wire_old_buffer(req,
	    sizeof(uint32_t) + (in6_mcast_maxgrpsrc * sizeof(struct in6_addr)));
	if (retval)
		return (retval);

	IN6_MULTI_LOCK();
	IN6_MULTI_LIST_LOCK();
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		inm = in6m_ifmultiaddr_get_inm(ifma);
		if (inm == NULL)
			continue;
		if (!IN6_ARE_ADDR_EQUAL(&inm->in6m_addr, &mcaddr))
			continue;
		fmode = inm->in6m_st[1].iss_fmode;
		retval = SYSCTL_OUT(req, &fmode, sizeof(uint32_t));
		if (retval != 0)
			break;
		RB_FOREACH(ims, ip6_msource_tree, &inm->in6m_srcs) {
			CTR2(KTR_MLD, "%s: visit node %p", __func__, ims);
			/*
			 * Only copy-out sources which are in-mode.
			 */
			if (fmode != im6s_get_mode(inm, ims, 1)) {
				CTR1(KTR_MLD, "%s: skip non-in-mode",
				    __func__);
				continue;
			}
			src = ims->im6s_addr;
			retval = SYSCTL_OUT(req, &src,
			    sizeof(struct in6_addr));
			if (retval != 0)
				break;
		}
	}
	NET_EPOCH_EXIT(et);

	IN6_MULTI_LIST_UNLOCK();
	IN6_MULTI_UNLOCK();

	return (retval);
}

#ifdef KTR

static const char *in6m_modestrs[] = { "un", "in", "ex" };

static const char *
in6m_mode_str(const int mode)
{

	if (mode >= MCAST_UNDEFINED && mode <= MCAST_EXCLUDE)
		return (in6m_modestrs[mode]);
	return ("??");
}

static const char *in6m_statestrs[] = {
	"not-member",
	"silent",
	"idle",
	"lazy",
	"sleeping",
	"awakening",
	"query-pending",
	"sg-query-pending",
	"leaving"
};

static const char *
in6m_state_str(const int state)
{

	if (state >= MLD_NOT_MEMBER && state <= MLD_LEAVING_MEMBER)
		return (in6m_statestrs[state]);
	return ("??");
}

/*
 * Dump an in6_multi structure to the console.
 */
void
in6m_print(const struct in6_multi *inm)
{
	int t;
	char ip6tbuf[INET6_ADDRSTRLEN];

	if ((ktr_mask & KTR_MLD) == 0)
		return;

	printf("%s: --- begin in6m %p ---\n", __func__, inm);
	printf("addr %s ifp %p(%s) ifma %p\n",
	    ip6_sprintf(ip6tbuf, &inm->in6m_addr),
	    inm->in6m_ifp,
	    if_name(inm->in6m_ifp),
	    inm->in6m_ifma);
	printf("timer %u state %s refcount %u scq.len %u\n",
	    inm->in6m_timer,
	    in6m_state_str(inm->in6m_state),
	    inm->in6m_refcount,
	    mbufq_len(&inm->in6m_scq));
	printf("mli %p nsrc %lu sctimer %u scrv %u\n",
	    inm->in6m_mli,
	    inm->in6m_nsrc,
	    inm->in6m_sctimer,
	    inm->in6m_scrv);
	for (t = 0; t < 2; t++) {
		printf("t%d: fmode %s asm %u ex %u in %u rec %u\n", t,
		    in6m_mode_str(inm->in6m_st[t].iss_fmode),
		    inm->in6m_st[t].iss_asm,
		    inm->in6m_st[t].iss_ex,
		    inm->in6m_st[t].iss_in,
		    inm->in6m_st[t].iss_rec);
	}
	printf("%s: --- end in6m %p ---\n", __func__, inm);
}

#else /* !KTR */

void
in6m_print(const struct in6_multi *inm)
{

}

#endif /* KTR */
