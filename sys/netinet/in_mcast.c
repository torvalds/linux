/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Bruce Simpson.
 * Copyright (c) 2005 Robert N. M. Watson.
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
 * IPv4 multicast socket, group, and socket option processing module.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>
#include <sys/taskqueue.h>
#include <sys/gtaskqueue.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/vnet.h>

#include <net/ethernet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_fib.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/igmp_var.h>

#ifndef KTR_IGMPV3
#define KTR_IGMPV3 KTR_INET
#endif

#ifndef __SOCKUNION_DECLARED
union sockunion {
	struct sockaddr_storage	ss;
	struct sockaddr		sa;
	struct sockaddr_dl	sdl;
	struct sockaddr_in	sin;
};
typedef union sockunion sockunion_t;
#define __SOCKUNION_DECLARED
#endif /* __SOCKUNION_DECLARED */

static MALLOC_DEFINE(M_INMFILTER, "in_mfilter",
    "IPv4 multicast PCB-layer source filter");
static MALLOC_DEFINE(M_IPMADDR, "in_multi", "IPv4 multicast group");
static MALLOC_DEFINE(M_IPMOPTS, "ip_moptions", "IPv4 multicast options");
static MALLOC_DEFINE(M_IPMSOURCE, "ip_msource",
    "IPv4 multicast IGMP-layer source filter");

/*
 * Locking:
 * - Lock order is: Giant, INP_WLOCK, IN_MULTI_LIST_LOCK, IGMP_LOCK, IF_ADDR_LOCK.
 * - The IF_ADDR_LOCK is implicitly taken by inm_lookup() earlier, however
 *   it can be taken by code in net/if.c also.
 * - ip_moptions and in_mfilter are covered by the INP_WLOCK.
 *
 * struct in_multi is covered by IN_MULTI_LIST_LOCK. There isn't strictly
 * any need for in_multi itself to be virtualized -- it is bound to an ifp
 * anyway no matter what happens.
 */
struct mtx in_multi_list_mtx;
MTX_SYSINIT(in_multi_mtx, &in_multi_list_mtx, "in_multi_list_mtx", MTX_DEF);

struct mtx in_multi_free_mtx;
MTX_SYSINIT(in_multi_free_mtx, &in_multi_free_mtx, "in_multi_free_mtx", MTX_DEF);

struct sx in_multi_sx;
SX_SYSINIT(in_multi_sx, &in_multi_sx, "in_multi_sx");

int ifma_restart;

/*
 * Functions with non-static linkage defined in this file should be
 * declared in in_var.h:
 *  imo_multi_filter()
 *  in_addmulti()
 *  in_delmulti()
 *  in_joingroup()
 *  in_joingroup_locked()
 *  in_leavegroup()
 *  in_leavegroup_locked()
 * and ip_var.h:
 *  inp_freemoptions()
 *  inp_getmoptions()
 *  inp_setmoptions()
 *
 * XXX: Both carp and pf need to use the legacy (*,G) KPIs in_addmulti()
 * and in_delmulti().
 */
static void	imf_commit(struct in_mfilter *);
static int	imf_get_source(struct in_mfilter *imf,
		    const struct sockaddr_in *psin,
		    struct in_msource **);
static struct in_msource *
		imf_graft(struct in_mfilter *, const uint8_t,
		    const struct sockaddr_in *);
static void	imf_leave(struct in_mfilter *);
static int	imf_prune(struct in_mfilter *, const struct sockaddr_in *);
static void	imf_purge(struct in_mfilter *);
static void	imf_rollback(struct in_mfilter *);
static void	imf_reap(struct in_mfilter *);
static int	imo_grow(struct ip_moptions *);
static size_t	imo_match_group(const struct ip_moptions *,
		    const struct ifnet *, const struct sockaddr *);
static struct in_msource *
		imo_match_source(const struct ip_moptions *, const size_t,
		    const struct sockaddr *);
static void	ims_merge(struct ip_msource *ims,
		    const struct in_msource *lims, const int rollback);
static int	in_getmulti(struct ifnet *, const struct in_addr *,
		    struct in_multi **);
static int	inm_get_source(struct in_multi *inm, const in_addr_t haddr,
		    const int noalloc, struct ip_msource **pims);
#ifdef KTR
static int	inm_is_ifp_detached(const struct in_multi *);
#endif
static int	inm_merge(struct in_multi *, /*const*/ struct in_mfilter *);
static void	inm_purge(struct in_multi *);
static void	inm_reap(struct in_multi *);
static void inm_release(struct in_multi *);
static struct ip_moptions *
		inp_findmoptions(struct inpcb *);
static int	inp_get_source_filters(struct inpcb *, struct sockopt *);
static int	inp_join_group(struct inpcb *, struct sockopt *);
static int	inp_leave_group(struct inpcb *, struct sockopt *);
static struct ifnet *
		inp_lookup_mcast_ifp(const struct inpcb *,
		    const struct sockaddr_in *, const struct in_addr);
static int	inp_block_unblock_source(struct inpcb *, struct sockopt *);
static int	inp_set_multicast_if(struct inpcb *, struct sockopt *);
static int	inp_set_source_filters(struct inpcb *, struct sockopt *);
static int	sysctl_ip_mcast_filters(SYSCTL_HANDLER_ARGS);

static SYSCTL_NODE(_net_inet_ip, OID_AUTO, mcast, CTLFLAG_RW, 0,
    "IPv4 multicast");

static u_long in_mcast_maxgrpsrc = IP_MAX_GROUP_SRC_FILTER;
SYSCTL_ULONG(_net_inet_ip_mcast, OID_AUTO, maxgrpsrc,
    CTLFLAG_RWTUN, &in_mcast_maxgrpsrc, 0,
    "Max source filters per group");

static u_long in_mcast_maxsocksrc = IP_MAX_SOCK_SRC_FILTER;
SYSCTL_ULONG(_net_inet_ip_mcast, OID_AUTO, maxsocksrc,
    CTLFLAG_RWTUN, &in_mcast_maxsocksrc, 0,
    "Max source filters per socket");

int in_mcast_loop = IP_DEFAULT_MULTICAST_LOOP;
SYSCTL_INT(_net_inet_ip_mcast, OID_AUTO, loop, CTLFLAG_RWTUN,
    &in_mcast_loop, 0, "Loopback multicast datagrams by default");

static SYSCTL_NODE(_net_inet_ip_mcast, OID_AUTO, filters,
    CTLFLAG_RD | CTLFLAG_MPSAFE, sysctl_ip_mcast_filters,
    "Per-interface stack-wide source filters");

#ifdef KTR
/*
 * Inline function which wraps assertions for a valid ifp.
 * The ifnet layer will set the ifma's ifp pointer to NULL if the ifp
 * is detached.
 */
static int __inline
inm_is_ifp_detached(const struct in_multi *inm)
{
	struct ifnet *ifp;

	KASSERT(inm->inm_ifma != NULL, ("%s: no ifma", __func__));
	ifp = inm->inm_ifma->ifma_ifp;
	if (ifp != NULL) {
		/*
		 * Sanity check that netinet's notion of ifp is the
		 * same as net's.
		 */
		KASSERT(inm->inm_ifp == ifp, ("%s: bad ifp", __func__));
	}

	return (ifp == NULL);
}
#endif

static struct grouptask free_gtask;
static struct in_multi_head inm_free_list;
static void inm_release_task(void *arg __unused);
static void inm_init(void)
{
	SLIST_INIT(&inm_free_list);
	taskqgroup_config_gtask_init(NULL, &free_gtask, inm_release_task, "inm release task");
}

#ifdef EARLY_AP_STARTUP
SYSINIT(inm_init, SI_SUB_SMP + 1, SI_ORDER_FIRST,
	inm_init, NULL);
#else
SYSINIT(inm_init, SI_SUB_ROOT_CONF - 1, SI_ORDER_FIRST,
	inm_init, NULL);
#endif


void
inm_release_list_deferred(struct in_multi_head *inmh)
{

	if (SLIST_EMPTY(inmh))
		return;
	mtx_lock(&in_multi_free_mtx);
	SLIST_CONCAT(&inm_free_list, inmh, in_multi, inm_nrele);
	mtx_unlock(&in_multi_free_mtx);
	GROUPTASK_ENQUEUE(&free_gtask);
}

void
inm_disconnect(struct in_multi *inm)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma, *ll_ifma;

	ifp = inm->inm_ifp;
	IF_ADDR_WLOCK_ASSERT(ifp);
	ifma = inm->inm_ifma;

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
			ifma_restart = true;
		}
	}
}

void
inm_release_deferred(struct in_multi *inm)
{
	struct in_multi_head tmp;

	IN_MULTI_LIST_LOCK_ASSERT();
	MPASS(inm->inm_refcount > 0);
	if (--inm->inm_refcount == 0) {
		SLIST_INIT(&tmp);
		inm_disconnect(inm);
		inm->inm_ifma->ifma_protospec = NULL;
		SLIST_INSERT_HEAD(&tmp, inm, inm_nrele);
		inm_release_list_deferred(&tmp);
	}
}

static void
inm_release_task(void *arg __unused)
{
	struct in_multi_head inm_free_tmp;
	struct in_multi *inm, *tinm;

	SLIST_INIT(&inm_free_tmp);
	mtx_lock(&in_multi_free_mtx);
	SLIST_CONCAT(&inm_free_tmp, &inm_free_list, in_multi, inm_nrele);
	mtx_unlock(&in_multi_free_mtx);
	IN_MULTI_LOCK();
	SLIST_FOREACH_SAFE(inm, &inm_free_tmp, inm_nrele, tinm) {
		SLIST_REMOVE_HEAD(&inm_free_tmp, inm_nrele);
		MPASS(inm);
		inm_release(inm);
	}
	IN_MULTI_UNLOCK();
}

/*
 * Initialize an in_mfilter structure to a known state at t0, t1
 * with an empty source filter list.
 */
static __inline void
imf_init(struct in_mfilter *imf, const int st0, const int st1)
{
	memset(imf, 0, sizeof(struct in_mfilter));
	RB_INIT(&imf->imf_sources);
	imf->imf_st[0] = st0;
	imf->imf_st[1] = st1;
}

/*
 * Function for looking up an in_multi record for an IPv4 multicast address
 * on a given interface. ifp must be valid. If no record found, return NULL.
 * The IN_MULTI_LIST_LOCK and IF_ADDR_LOCK on ifp must be held.
 */
struct in_multi *
inm_lookup_locked(struct ifnet *ifp, const struct in_addr ina)
{
	struct ifmultiaddr *ifma;
	struct in_multi *inm;

	IN_MULTI_LIST_LOCK_ASSERT();
	IF_ADDR_LOCK_ASSERT(ifp);

	inm = NULL;
	CK_STAILQ_FOREACH(ifma, &((ifp)->if_multiaddrs), ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
			ifma->ifma_protospec == NULL)
			continue;
		inm = (struct in_multi *)ifma->ifma_protospec;
		if (inm->inm_addr.s_addr == ina.s_addr)
			break;
		inm = NULL;
	}
	return (inm);
}

/*
 * Wrapper for inm_lookup_locked().
 * The IF_ADDR_LOCK will be taken on ifp and released on return.
 */
struct in_multi *
inm_lookup(struct ifnet *ifp, const struct in_addr ina)
{
	struct epoch_tracker et;
	struct in_multi *inm;

	IN_MULTI_LIST_LOCK_ASSERT();
	NET_EPOCH_ENTER(et);
	inm = inm_lookup_locked(ifp, ina);
	NET_EPOCH_EXIT(et);

	return (inm);
}

/*
 * Resize the ip_moptions vector to the next power-of-two minus 1.
 * May be called with locks held; do not sleep.
 */
static int
imo_grow(struct ip_moptions *imo)
{
	struct in_multi		**nmships;
	struct in_multi		**omships;
	struct in_mfilter	 *nmfilters;
	struct in_mfilter	 *omfilters;
	size_t			  idx;
	size_t			  newmax;
	size_t			  oldmax;

	nmships = NULL;
	nmfilters = NULL;
	omships = imo->imo_membership;
	omfilters = imo->imo_mfilters;
	oldmax = imo->imo_max_memberships;
	newmax = ((oldmax + 1) * 2) - 1;

	if (newmax <= IP_MAX_MEMBERSHIPS) {
		nmships = (struct in_multi **)realloc(omships,
		    sizeof(struct in_multi *) * newmax, M_IPMOPTS, M_NOWAIT);
		nmfilters = (struct in_mfilter *)realloc(omfilters,
		    sizeof(struct in_mfilter) * newmax, M_INMFILTER, M_NOWAIT);
		if (nmships != NULL && nmfilters != NULL) {
			/* Initialize newly allocated source filter heads. */
			for (idx = oldmax; idx < newmax; idx++) {
				imf_init(&nmfilters[idx], MCAST_UNDEFINED,
				    MCAST_EXCLUDE);
			}
			imo->imo_max_memberships = newmax;
			imo->imo_membership = nmships;
			imo->imo_mfilters = nmfilters;
		}
	}

	if (nmships == NULL || nmfilters == NULL) {
		if (nmships != NULL)
			free(nmships, M_IPMOPTS);
		if (nmfilters != NULL)
			free(nmfilters, M_INMFILTER);
		return (ETOOMANYREFS);
	}

	return (0);
}

/*
 * Find an IPv4 multicast group entry for this ip_moptions instance
 * which matches the specified group, and optionally an interface.
 * Return its index into the array, or -1 if not found.
 */
static size_t
imo_match_group(const struct ip_moptions *imo, const struct ifnet *ifp,
    const struct sockaddr *group)
{
	const struct sockaddr_in *gsin;
	struct in_multi	**pinm;
	int		  idx;
	int		  nmships;

	gsin = (const struct sockaddr_in *)group;

	/* The imo_membership array may be lazy allocated. */
	if (imo->imo_membership == NULL || imo->imo_num_memberships == 0)
		return (-1);

	nmships = imo->imo_num_memberships;
	pinm = &imo->imo_membership[0];
	for (idx = 0; idx < nmships; idx++, pinm++) {
		if (*pinm == NULL)
			continue;
		if ((ifp == NULL || ((*pinm)->inm_ifp == ifp)) &&
		    in_hosteq((*pinm)->inm_addr, gsin->sin_addr)) {
			break;
		}
	}
	if (idx >= nmships)
		idx = -1;

	return (idx);
}

/*
 * Find an IPv4 multicast source entry for this imo which matches
 * the given group index for this socket, and source address.
 *
 * NOTE: This does not check if the entry is in-mode, merely if
 * it exists, which may not be the desired behaviour.
 */
static struct in_msource *
imo_match_source(const struct ip_moptions *imo, const size_t gidx,
    const struct sockaddr *src)
{
	struct ip_msource	 find;
	struct in_mfilter	*imf;
	struct ip_msource	*ims;
	const sockunion_t	*psa;

	KASSERT(src->sa_family == AF_INET, ("%s: !AF_INET", __func__));
	KASSERT(gidx != -1 && gidx < imo->imo_num_memberships,
	    ("%s: invalid index %d\n", __func__, (int)gidx));

	/* The imo_mfilters array may be lazy allocated. */
	if (imo->imo_mfilters == NULL)
		return (NULL);
	imf = &imo->imo_mfilters[gidx];

	/* Source trees are keyed in host byte order. */
	psa = (const sockunion_t *)src;
	find.ims_haddr = ntohl(psa->sin.sin_addr.s_addr);
	ims = RB_FIND(ip_msource_tree, &imf->imf_sources, &find);

	return ((struct in_msource *)ims);
}

/*
 * Perform filtering for multicast datagrams on a socket by group and source.
 *
 * Returns 0 if a datagram should be allowed through, or various error codes
 * if the socket was not a member of the group, or the source was muted, etc.
 */
int
imo_multi_filter(const struct ip_moptions *imo, const struct ifnet *ifp,
    const struct sockaddr *group, const struct sockaddr *src)
{
	size_t gidx;
	struct in_msource *ims;
	int mode;

	KASSERT(ifp != NULL, ("%s: null ifp", __func__));

	gidx = imo_match_group(imo, ifp, group);
	if (gidx == -1)
		return (MCAST_NOTGMEMBER);

	/*
	 * Check if the source was included in an (S,G) join.
	 * Allow reception on exclusive memberships by default,
	 * reject reception on inclusive memberships by default.
	 * Exclude source only if an in-mode exclude filter exists.
	 * Include source only if an in-mode include filter exists.
	 * NOTE: We are comparing group state here at IGMP t1 (now)
	 * with socket-layer t0 (since last downcall).
	 */
	mode = imo->imo_mfilters[gidx].imf_st[1];
	ims = imo_match_source(imo, gidx, src);

	if ((ims == NULL && mode == MCAST_INCLUDE) ||
	    (ims != NULL && ims->imsl_st[0] != mode))
		return (MCAST_NOTSMEMBER);

	return (MCAST_PASS);
}

/*
 * Find and return a reference to an in_multi record for (ifp, group),
 * and bump its reference count.
 * If one does not exist, try to allocate it, and update link-layer multicast
 * filters on ifp to listen for group.
 * Assumes the IN_MULTI lock is held across the call.
 * Return 0 if successful, otherwise return an appropriate error code.
 */
static int
in_getmulti(struct ifnet *ifp, const struct in_addr *group,
    struct in_multi **pinm)
{
	struct sockaddr_in	 gsin;
	struct ifmultiaddr	*ifma;
	struct in_ifinfo	*ii;
	struct in_multi		*inm;
	int error;

	IN_MULTI_LOCK_ASSERT();

	ii = (struct in_ifinfo *)ifp->if_afdata[AF_INET];
	IN_MULTI_LIST_LOCK();
	inm = inm_lookup(ifp, *group);
	if (inm != NULL) {
		/*
		 * If we already joined this group, just bump the
		 * refcount and return it.
		 */
		KASSERT(inm->inm_refcount >= 1,
		    ("%s: bad refcount %d", __func__, inm->inm_refcount));
		inm_acquire_locked(inm);
		*pinm = inm;
	}
	IN_MULTI_LIST_UNLOCK();
	if (inm != NULL)
		return (0);
	
	memset(&gsin, 0, sizeof(gsin));
	gsin.sin_family = AF_INET;
	gsin.sin_len = sizeof(struct sockaddr_in);
	gsin.sin_addr = *group;

	/*
	 * Check if a link-layer group is already associated
	 * with this network-layer group on the given ifnet.
	 */
	error = if_addmulti(ifp, (struct sockaddr *)&gsin, &ifma);
	if (error != 0)
		return (error);

	/* XXX ifma_protospec must be covered by IF_ADDR_LOCK */
	IN_MULTI_LIST_LOCK();
	IF_ADDR_WLOCK(ifp);

	/*
	 * If something other than netinet is occupying the link-layer
	 * group, print a meaningful error message and back out of
	 * the allocation.
	 * Otherwise, bump the refcount on the existing network-layer
	 * group association and return it.
	 */
	if (ifma->ifma_protospec != NULL) {
		inm = (struct in_multi *)ifma->ifma_protospec;
#ifdef INVARIANTS
		KASSERT(ifma->ifma_addr != NULL, ("%s: no ifma_addr",
		    __func__));
		KASSERT(ifma->ifma_addr->sa_family == AF_INET,
		    ("%s: ifma not AF_INET", __func__));
		KASSERT(inm != NULL, ("%s: no ifma_protospec", __func__));
		if (inm->inm_ifma != ifma || inm->inm_ifp != ifp ||
		    !in_hosteq(inm->inm_addr, *group)) {
			char addrbuf[INET_ADDRSTRLEN];

			panic("%s: ifma %p is inconsistent with %p (%s)",
			    __func__, ifma, inm, inet_ntoa_r(*group, addrbuf));
		}
#endif
		inm_acquire_locked(inm);
		*pinm = inm;
		goto out_locked;
	}

	IF_ADDR_WLOCK_ASSERT(ifp);

	/*
	 * A new in_multi record is needed; allocate and initialize it.
	 * We DO NOT perform an IGMP join as the in_ layer may need to
	 * push an initial source list down to IGMP to support SSM.
	 *
	 * The initial source filter state is INCLUDE, {} as per the RFC.
	 */
	inm = malloc(sizeof(*inm), M_IPMADDR, M_NOWAIT | M_ZERO);
	if (inm == NULL) {
		IF_ADDR_WUNLOCK(ifp);
		IN_MULTI_LIST_UNLOCK();
		if_delmulti_ifma(ifma);
		return (ENOMEM);
	}
	inm->inm_addr = *group;
	inm->inm_ifp = ifp;
	inm->inm_igi = ii->ii_igmp;
	inm->inm_ifma = ifma;
	inm->inm_refcount = 1;
	inm->inm_state = IGMP_NOT_MEMBER;
	mbufq_init(&inm->inm_scq, IGMP_MAX_STATE_CHANGES);
	inm->inm_st[0].iss_fmode = MCAST_UNDEFINED;
	inm->inm_st[1].iss_fmode = MCAST_UNDEFINED;
	RB_INIT(&inm->inm_srcs);

	ifma->ifma_protospec = inm;

	*pinm = inm;
 out_locked:
	IF_ADDR_WUNLOCK(ifp);
	IN_MULTI_LIST_UNLOCK();
	return (0);
}

/*
 * Drop a reference to an in_multi record.
 *
 * If the refcount drops to 0, free the in_multi record and
 * delete the underlying link-layer membership.
 */
static void
inm_release(struct in_multi *inm)
{
	struct ifmultiaddr *ifma;
	struct ifnet *ifp;

	CTR2(KTR_IGMPV3, "%s: refcount is %d", __func__, inm->inm_refcount);
	MPASS(inm->inm_refcount == 0);
	CTR2(KTR_IGMPV3, "%s: freeing inm %p", __func__, inm);

	ifma = inm->inm_ifma;
	ifp = inm->inm_ifp;

	/* XXX this access is not covered by IF_ADDR_LOCK */
	CTR2(KTR_IGMPV3, "%s: purging ifma %p", __func__, ifma);
	if (ifp != NULL) {
		CURVNET_SET(ifp->if_vnet);
		inm_purge(inm);
		free(inm, M_IPMADDR);
		if_delmulti_ifma_flags(ifma, 1);
		CURVNET_RESTORE();
		if_rele(ifp);
	} else {
		inm_purge(inm);
		free(inm, M_IPMADDR);
		if_delmulti_ifma_flags(ifma, 1);
	}
}

/*
 * Clear recorded source entries for a group.
 * Used by the IGMP code. Caller must hold the IN_MULTI lock.
 * FIXME: Should reap.
 */
void
inm_clear_recorded(struct in_multi *inm)
{
	struct ip_msource	*ims;

	IN_MULTI_LIST_LOCK_ASSERT();

	RB_FOREACH(ims, ip_msource_tree, &inm->inm_srcs) {
		if (ims->ims_stp) {
			ims->ims_stp = 0;
			--inm->inm_st[1].iss_rec;
		}
	}
	KASSERT(inm->inm_st[1].iss_rec == 0,
	    ("%s: iss_rec %d not 0", __func__, inm->inm_st[1].iss_rec));
}

/*
 * Record a source as pending for a Source-Group IGMPv3 query.
 * This lives here as it modifies the shared tree.
 *
 * inm is the group descriptor.
 * naddr is the address of the source to record in network-byte order.
 *
 * If the net.inet.igmp.sgalloc sysctl is non-zero, we will
 * lazy-allocate a source node in response to an SG query.
 * Otherwise, no allocation is performed. This saves some memory
 * with the trade-off that the source will not be reported to the
 * router if joined in the window between the query response and
 * the group actually being joined on the local host.
 *
 * VIMAGE: XXX: Currently the igmp_sgalloc feature has been removed.
 * This turns off the allocation of a recorded source entry if
 * the group has not been joined.
 *
 * Return 0 if the source didn't exist or was already marked as recorded.
 * Return 1 if the source was marked as recorded by this function.
 * Return <0 if any error occurred (negated errno code).
 */
int
inm_record_source(struct in_multi *inm, const in_addr_t naddr)
{
	struct ip_msource	 find;
	struct ip_msource	*ims, *nims;

	IN_MULTI_LIST_LOCK_ASSERT();

	find.ims_haddr = ntohl(naddr);
	ims = RB_FIND(ip_msource_tree, &inm->inm_srcs, &find);
	if (ims && ims->ims_stp)
		return (0);
	if (ims == NULL) {
		if (inm->inm_nsrc == in_mcast_maxgrpsrc)
			return (-ENOSPC);
		nims = malloc(sizeof(struct ip_msource), M_IPMSOURCE,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (-ENOMEM);
		nims->ims_haddr = find.ims_haddr;
		RB_INSERT(ip_msource_tree, &inm->inm_srcs, nims);
		++inm->inm_nsrc;
		ims = nims;
	}

	/*
	 * Mark the source as recorded and update the recorded
	 * source count.
	 */
	++ims->ims_stp;
	++inm->inm_st[1].iss_rec;

	return (1);
}

/*
 * Return a pointer to an in_msource owned by an in_mfilter,
 * given its source address.
 * Lazy-allocate if needed. If this is a new entry its filter state is
 * undefined at t0.
 *
 * imf is the filter set being modified.
 * haddr is the source address in *host* byte-order.
 *
 * SMPng: May be called with locks held; malloc must not block.
 */
static int
imf_get_source(struct in_mfilter *imf, const struct sockaddr_in *psin,
    struct in_msource **plims)
{
	struct ip_msource	 find;
	struct ip_msource	*ims, *nims;
	struct in_msource	*lims;
	int			 error;

	error = 0;
	ims = NULL;
	lims = NULL;

	/* key is host byte order */
	find.ims_haddr = ntohl(psin->sin_addr.s_addr);
	ims = RB_FIND(ip_msource_tree, &imf->imf_sources, &find);
	lims = (struct in_msource *)ims;
	if (lims == NULL) {
		if (imf->imf_nsrc == in_mcast_maxsocksrc)
			return (ENOSPC);
		nims = malloc(sizeof(struct in_msource), M_INMFILTER,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (ENOMEM);
		lims = (struct in_msource *)nims;
		lims->ims_haddr = find.ims_haddr;
		lims->imsl_st[0] = MCAST_UNDEFINED;
		RB_INSERT(ip_msource_tree, &imf->imf_sources, nims);
		++imf->imf_nsrc;
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
static struct in_msource *
imf_graft(struct in_mfilter *imf, const uint8_t st1,
    const struct sockaddr_in *psin)
{
	struct ip_msource	*nims;
	struct in_msource	*lims;

	nims = malloc(sizeof(struct in_msource), M_INMFILTER,
	    M_NOWAIT | M_ZERO);
	if (nims == NULL)
		return (NULL);
	lims = (struct in_msource *)nims;
	lims->ims_haddr = ntohl(psin->sin_addr.s_addr);
	lims->imsl_st[0] = MCAST_UNDEFINED;
	lims->imsl_st[1] = st1;
	RB_INSERT(ip_msource_tree, &imf->imf_sources, nims);
	++imf->imf_nsrc;

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
imf_prune(struct in_mfilter *imf, const struct sockaddr_in *psin)
{
	struct ip_msource	 find;
	struct ip_msource	*ims;
	struct in_msource	*lims;

	/* key is host byte order */
	find.ims_haddr = ntohl(psin->sin_addr.s_addr);
	ims = RB_FIND(ip_msource_tree, &imf->imf_sources, &find);
	if (ims == NULL)
		return (ENOENT);
	lims = (struct in_msource *)ims;
	lims->imsl_st[1] = MCAST_UNDEFINED;
	return (0);
}

/*
 * Revert socket-layer filter set deltas at t1 to t0 state.
 */
static void
imf_rollback(struct in_mfilter *imf)
{
	struct ip_msource	*ims, *tims;
	struct in_msource	*lims;

	RB_FOREACH_SAFE(ims, ip_msource_tree, &imf->imf_sources, tims) {
		lims = (struct in_msource *)ims;
		if (lims->imsl_st[0] == lims->imsl_st[1]) {
			/* no change at t1 */
			continue;
		} else if (lims->imsl_st[0] != MCAST_UNDEFINED) {
			/* revert change to existing source at t1 */
			lims->imsl_st[1] = lims->imsl_st[0];
		} else {
			/* revert source added t1 */
			CTR2(KTR_IGMPV3, "%s: free ims %p", __func__, ims);
			RB_REMOVE(ip_msource_tree, &imf->imf_sources, ims);
			free(ims, M_INMFILTER);
			imf->imf_nsrc--;
		}
	}
	imf->imf_st[1] = imf->imf_st[0];
}

/*
 * Mark socket-layer filter set as INCLUDE {} at t1.
 */
static void
imf_leave(struct in_mfilter *imf)
{
	struct ip_msource	*ims;
	struct in_msource	*lims;

	RB_FOREACH(ims, ip_msource_tree, &imf->imf_sources) {
		lims = (struct in_msource *)ims;
		lims->imsl_st[1] = MCAST_UNDEFINED;
	}
	imf->imf_st[1] = MCAST_INCLUDE;
}

/*
 * Mark socket-layer filter set deltas as committed.
 */
static void
imf_commit(struct in_mfilter *imf)
{
	struct ip_msource	*ims;
	struct in_msource	*lims;

	RB_FOREACH(ims, ip_msource_tree, &imf->imf_sources) {
		lims = (struct in_msource *)ims;
		lims->imsl_st[0] = lims->imsl_st[1];
	}
	imf->imf_st[0] = imf->imf_st[1];
}

/*
 * Reap unreferenced sources from socket-layer filter set.
 */
static void
imf_reap(struct in_mfilter *imf)
{
	struct ip_msource	*ims, *tims;
	struct in_msource	*lims;

	RB_FOREACH_SAFE(ims, ip_msource_tree, &imf->imf_sources, tims) {
		lims = (struct in_msource *)ims;
		if ((lims->imsl_st[0] == MCAST_UNDEFINED) &&
		    (lims->imsl_st[1] == MCAST_UNDEFINED)) {
			CTR2(KTR_IGMPV3, "%s: free lims %p", __func__, ims);
			RB_REMOVE(ip_msource_tree, &imf->imf_sources, ims);
			free(ims, M_INMFILTER);
			imf->imf_nsrc--;
		}
	}
}

/*
 * Purge socket-layer filter set.
 */
static void
imf_purge(struct in_mfilter *imf)
{
	struct ip_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip_msource_tree, &imf->imf_sources, tims) {
		CTR2(KTR_IGMPV3, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip_msource_tree, &imf->imf_sources, ims);
		free(ims, M_INMFILTER);
		imf->imf_nsrc--;
	}
	imf->imf_st[0] = imf->imf_st[1] = MCAST_UNDEFINED;
	KASSERT(RB_EMPTY(&imf->imf_sources),
	    ("%s: imf_sources not empty", __func__));
}

/*
 * Look up a source filter entry for a multicast group.
 *
 * inm is the group descriptor to work with.
 * haddr is the host-byte-order IPv4 address to look up.
 * noalloc may be non-zero to suppress allocation of sources.
 * *pims will be set to the address of the retrieved or allocated source.
 *
 * SMPng: NOTE: may be called with locks held.
 * Return 0 if successful, otherwise return a non-zero error code.
 */
static int
inm_get_source(struct in_multi *inm, const in_addr_t haddr,
    const int noalloc, struct ip_msource **pims)
{
	struct ip_msource	 find;
	struct ip_msource	*ims, *nims;

	find.ims_haddr = haddr;
	ims = RB_FIND(ip_msource_tree, &inm->inm_srcs, &find);
	if (ims == NULL && !noalloc) {
		if (inm->inm_nsrc == in_mcast_maxgrpsrc)
			return (ENOSPC);
		nims = malloc(sizeof(struct ip_msource), M_IPMSOURCE,
		    M_NOWAIT | M_ZERO);
		if (nims == NULL)
			return (ENOMEM);
		nims->ims_haddr = haddr;
		RB_INSERT(ip_msource_tree, &inm->inm_srcs, nims);
		++inm->inm_nsrc;
		ims = nims;
#ifdef KTR
		CTR3(KTR_IGMPV3, "%s: allocated 0x%08x as %p", __func__,
		    haddr, ims);
#endif
	}

	*pims = ims;
	return (0);
}

/*
 * Merge socket-layer source into IGMP-layer source.
 * If rollback is non-zero, perform the inverse of the merge.
 */
static void
ims_merge(struct ip_msource *ims, const struct in_msource *lims,
    const int rollback)
{
	int n = rollback ? -1 : 1;

	if (lims->imsl_st[0] == MCAST_EXCLUDE) {
		CTR3(KTR_IGMPV3, "%s: t1 ex -= %d on 0x%08x",
		    __func__, n, ims->ims_haddr);
		ims->ims_st[1].ex -= n;
	} else if (lims->imsl_st[0] == MCAST_INCLUDE) {
		CTR3(KTR_IGMPV3, "%s: t1 in -= %d on 0x%08x",
		    __func__, n, ims->ims_haddr);
		ims->ims_st[1].in -= n;
	}

	if (lims->imsl_st[1] == MCAST_EXCLUDE) {
		CTR3(KTR_IGMPV3, "%s: t1 ex += %d on 0x%08x",
		    __func__, n, ims->ims_haddr);
		ims->ims_st[1].ex += n;
	} else if (lims->imsl_st[1] == MCAST_INCLUDE) {
		CTR3(KTR_IGMPV3, "%s: t1 in += %d on 0x%08x",
		    __func__, n, ims->ims_haddr);
		ims->ims_st[1].in += n;
	}
}

/*
 * Atomically update the global in_multi state, when a membership's
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
inm_merge(struct in_multi *inm, /*const*/ struct in_mfilter *imf)
{
	struct ip_msource	*ims, *nims;
	struct in_msource	*lims;
	int			 schanged, error;
	int			 nsrc0, nsrc1;

	schanged = 0;
	error = 0;
	nsrc1 = nsrc0 = 0;
	IN_MULTI_LIST_LOCK_ASSERT();

	/*
	 * Update the source filters first, as this may fail.
	 * Maintain count of in-mode filters at t0, t1. These are
	 * used to work out if we transition into ASM mode or not.
	 * Maintain a count of source filters whose state was
	 * actually modified by this operation.
	 */
	RB_FOREACH(ims, ip_msource_tree, &imf->imf_sources) {
		lims = (struct in_msource *)ims;
		if (lims->imsl_st[0] == imf->imf_st[0]) nsrc0++;
		if (lims->imsl_st[1] == imf->imf_st[1]) nsrc1++;
		if (lims->imsl_st[0] == lims->imsl_st[1]) continue;
		error = inm_get_source(inm, lims->ims_haddr, 0, &nims);
		++schanged;
		if (error)
			break;
		ims_merge(nims, lims, 0);
	}
	if (error) {
		struct ip_msource *bims;

		RB_FOREACH_REVERSE_FROM(ims, ip_msource_tree, nims) {
			lims = (struct in_msource *)ims;
			if (lims->imsl_st[0] == lims->imsl_st[1])
				continue;
			(void)inm_get_source(inm, lims->ims_haddr, 1, &bims);
			if (bims == NULL)
				continue;
			ims_merge(bims, lims, 1);
		}
		goto out_reap;
	}

	CTR3(KTR_IGMPV3, "%s: imf filters in-mode: %d at t0, %d at t1",
	    __func__, nsrc0, nsrc1);

	/* Handle transition between INCLUDE {n} and INCLUDE {} on socket. */
	if (imf->imf_st[0] == imf->imf_st[1] &&
	    imf->imf_st[1] == MCAST_INCLUDE) {
		if (nsrc1 == 0) {
			CTR1(KTR_IGMPV3, "%s: --in on inm at t1", __func__);
			--inm->inm_st[1].iss_in;
		}
	}

	/* Handle filter mode transition on socket. */
	if (imf->imf_st[0] != imf->imf_st[1]) {
		CTR3(KTR_IGMPV3, "%s: imf transition %d to %d",
		    __func__, imf->imf_st[0], imf->imf_st[1]);

		if (imf->imf_st[0] == MCAST_EXCLUDE) {
			CTR1(KTR_IGMPV3, "%s: --ex on inm at t1", __func__);
			--inm->inm_st[1].iss_ex;
		} else if (imf->imf_st[0] == MCAST_INCLUDE) {
			CTR1(KTR_IGMPV3, "%s: --in on inm at t1", __func__);
			--inm->inm_st[1].iss_in;
		}

		if (imf->imf_st[1] == MCAST_EXCLUDE) {
			CTR1(KTR_IGMPV3, "%s: ex++ on inm at t1", __func__);
			inm->inm_st[1].iss_ex++;
		} else if (imf->imf_st[1] == MCAST_INCLUDE && nsrc1 > 0) {
			CTR1(KTR_IGMPV3, "%s: in++ on inm at t1", __func__);
			inm->inm_st[1].iss_in++;
		}
	}

	/*
	 * Track inm filter state in terms of listener counts.
	 * If there are any exclusive listeners, stack-wide
	 * membership is exclusive.
	 * Otherwise, if only inclusive listeners, stack-wide is inclusive.
	 * If no listeners remain, state is undefined at t1,
	 * and the IGMP lifecycle for this group should finish.
	 */
	if (inm->inm_st[1].iss_ex > 0) {
		CTR1(KTR_IGMPV3, "%s: transition to EX", __func__);
		inm->inm_st[1].iss_fmode = MCAST_EXCLUDE;
	} else if (inm->inm_st[1].iss_in > 0) {
		CTR1(KTR_IGMPV3, "%s: transition to IN", __func__);
		inm->inm_st[1].iss_fmode = MCAST_INCLUDE;
	} else {
		CTR1(KTR_IGMPV3, "%s: transition to UNDEF", __func__);
		inm->inm_st[1].iss_fmode = MCAST_UNDEFINED;
	}

	/* Decrement ASM listener count on transition out of ASM mode. */
	if (imf->imf_st[0] == MCAST_EXCLUDE && nsrc0 == 0) {
		if ((imf->imf_st[1] != MCAST_EXCLUDE) ||
		    (imf->imf_st[1] == MCAST_EXCLUDE && nsrc1 > 0)) {
			CTR1(KTR_IGMPV3, "%s: --asm on inm at t1", __func__);
			--inm->inm_st[1].iss_asm;
		}
	}

	/* Increment ASM listener count on transition to ASM mode. */
	if (imf->imf_st[1] == MCAST_EXCLUDE && nsrc1 == 0) {
		CTR1(KTR_IGMPV3, "%s: asm++ on inm at t1", __func__);
		inm->inm_st[1].iss_asm++;
	}

	CTR3(KTR_IGMPV3, "%s: merged imf %p to inm %p", __func__, imf, inm);
	inm_print(inm);

out_reap:
	if (schanged > 0) {
		CTR1(KTR_IGMPV3, "%s: sources changed; reaping", __func__);
		inm_reap(inm);
	}
	return (error);
}

/*
 * Mark an in_multi's filter set deltas as committed.
 * Called by IGMP after a state change has been enqueued.
 */
void
inm_commit(struct in_multi *inm)
{
	struct ip_msource	*ims;

	CTR2(KTR_IGMPV3, "%s: commit inm %p", __func__, inm);
	CTR1(KTR_IGMPV3, "%s: pre commit:", __func__);
	inm_print(inm);

	RB_FOREACH(ims, ip_msource_tree, &inm->inm_srcs) {
		ims->ims_st[0] = ims->ims_st[1];
	}
	inm->inm_st[0] = inm->inm_st[1];
}

/*
 * Reap unreferenced nodes from an in_multi's filter set.
 */
static void
inm_reap(struct in_multi *inm)
{
	struct ip_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip_msource_tree, &inm->inm_srcs, tims) {
		if (ims->ims_st[0].ex > 0 || ims->ims_st[0].in > 0 ||
		    ims->ims_st[1].ex > 0 || ims->ims_st[1].in > 0 ||
		    ims->ims_stp != 0)
			continue;
		CTR2(KTR_IGMPV3, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip_msource_tree, &inm->inm_srcs, ims);
		free(ims, M_IPMSOURCE);
		inm->inm_nsrc--;
	}
}

/*
 * Purge all source nodes from an in_multi's filter set.
 */
static void
inm_purge(struct in_multi *inm)
{
	struct ip_msource	*ims, *tims;

	RB_FOREACH_SAFE(ims, ip_msource_tree, &inm->inm_srcs, tims) {
		CTR2(KTR_IGMPV3, "%s: free ims %p", __func__, ims);
		RB_REMOVE(ip_msource_tree, &inm->inm_srcs, ims);
		free(ims, M_IPMSOURCE);
		inm->inm_nsrc--;
	}
}

/*
 * Join a multicast group; unlocked entry point.
 *
 * SMPng: XXX: in_joingroup() is called from in_control() when Giant
 * is not held. Fortunately, ifp is unlikely to have been detached
 * at this point, so we assume it's OK to recurse.
 */
int
in_joingroup(struct ifnet *ifp, const struct in_addr *gina,
    /*const*/ struct in_mfilter *imf, struct in_multi **pinm)
{
	int error;

	IN_MULTI_LOCK();
	error = in_joingroup_locked(ifp, gina, imf, pinm);
	IN_MULTI_UNLOCK();

	return (error);
}

/*
 * Join a multicast group; real entry point.
 *
 * Only preserves atomicity at inm level.
 * NOTE: imf argument cannot be const due to sys/tree.h limitations.
 *
 * If the IGMP downcall fails, the group is not joined, and an error
 * code is returned.
 */
int
in_joingroup_locked(struct ifnet *ifp, const struct in_addr *gina,
    /*const*/ struct in_mfilter *imf, struct in_multi **pinm)
{
	struct in_mfilter	 timf;
	struct in_multi		*inm;
	int			 error;

	IN_MULTI_LOCK_ASSERT();
	IN_MULTI_LIST_UNLOCK_ASSERT();

	CTR4(KTR_IGMPV3, "%s: join 0x%08x on %p(%s))", __func__,
	    ntohl(gina->s_addr), ifp, ifp->if_xname);

	error = 0;
	inm = NULL;

	/*
	 * If no imf was specified (i.e. kernel consumer),
	 * fake one up and assume it is an ASM join.
	 */
	if (imf == NULL) {
		imf_init(&timf, MCAST_UNDEFINED, MCAST_EXCLUDE);
		imf = &timf;
	}

	error = in_getmulti(ifp, gina, &inm);
	if (error) {
		CTR1(KTR_IGMPV3, "%s: in_getmulti() failure", __func__);
		return (error);
	}
	IN_MULTI_LIST_LOCK();
	CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
	error = inm_merge(inm, imf);
	if (error) {
		CTR1(KTR_IGMPV3, "%s: failed to merge inm state", __func__);
		goto out_inm_release;
	}

	CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
	error = igmp_change_state(inm);
	if (error) {
		CTR1(KTR_IGMPV3, "%s: failed to update source", __func__);
		goto out_inm_release;
	}

 out_inm_release:
	if (error) {

		CTR2(KTR_IGMPV3, "%s: dropping ref on %p", __func__, inm);
		inm_release_deferred(inm);
	} else {
		*pinm = inm;
	}
	IN_MULTI_LIST_UNLOCK();

	return (error);
}

/*
 * Leave a multicast group; unlocked entry point.
 */
int
in_leavegroup(struct in_multi *inm, /*const*/ struct in_mfilter *imf)
{
	int error;

	IN_MULTI_LOCK();
	error = in_leavegroup_locked(inm, imf);
	IN_MULTI_UNLOCK();

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
 * Note: This is not the same as inm_release(*) as this function also
 * makes a state change downcall into IGMP.
 */
int
in_leavegroup_locked(struct in_multi *inm, /*const*/ struct in_mfilter *imf)
{
	struct in_mfilter	 timf;
	int			 error;

	error = 0;

	IN_MULTI_LOCK_ASSERT();
	IN_MULTI_LIST_UNLOCK_ASSERT();

	CTR5(KTR_IGMPV3, "%s: leave inm %p, 0x%08x/%s, imf %p", __func__,
	    inm, ntohl(inm->inm_addr.s_addr),
	    (inm_is_ifp_detached(inm) ? "null" : inm->inm_ifp->if_xname),
	    imf);

	/*
	 * If no imf was specified (i.e. kernel consumer),
	 * fake one up and assume it is an ASM join.
	 */
	if (imf == NULL) {
		imf_init(&timf, MCAST_EXCLUDE, MCAST_UNDEFINED);
		imf = &timf;
	}

	/*
	 * Begin state merge transaction at IGMP layer.
	 *
	 * As this particular invocation should not cause any memory
	 * to be allocated, and there is no opportunity to roll back
	 * the transaction, it MUST NOT fail.
	 */
	CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
	IN_MULTI_LIST_LOCK();
	error = inm_merge(inm, imf);
	KASSERT(error == 0, ("%s: failed to merge inm state", __func__));

	CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
	CURVNET_SET(inm->inm_ifp->if_vnet);
	error = igmp_change_state(inm);
	IF_ADDR_WLOCK(inm->inm_ifp);
	inm_release_deferred(inm);
	IF_ADDR_WUNLOCK(inm->inm_ifp);
	IN_MULTI_LIST_UNLOCK();
	CURVNET_RESTORE();
	if (error)
		CTR1(KTR_IGMPV3, "%s: failed igmp downcall", __func__);

	CTR2(KTR_IGMPV3, "%s: dropping ref on %p", __func__, inm);

	return (error);
}

/*#ifndef BURN_BRIDGES*/
/*
 * Join an IPv4 multicast group in (*,G) exclusive mode.
 * The group must be a 224.0.0.0/24 link-scope group.
 * This KPI is for legacy kernel consumers only.
 */
struct in_multi *
in_addmulti(struct in_addr *ap, struct ifnet *ifp)
{
	struct in_multi *pinm;
	int error;
#ifdef INVARIANTS
	char addrbuf[INET_ADDRSTRLEN];
#endif

	KASSERT(IN_LOCAL_GROUP(ntohl(ap->s_addr)),
	    ("%s: %s not in 224.0.0.0/24", __func__,
	    inet_ntoa_r(*ap, addrbuf)));

	error = in_joingroup(ifp, ap, NULL, &pinm);
	if (error != 0)
		pinm = NULL;

	return (pinm);
}

/*
 * Block or unblock an ASM multicast source on an inpcb.
 * This implements the delta-based API described in RFC 3678.
 *
 * The delta-based API applies only to exclusive-mode memberships.
 * An IGMP downcall will be performed.
 *
 * SMPng: NOTE: Must take Giant as a join may create a new ifma.
 *
 * Return 0 if successful, otherwise return an appropriate error code.
 */
static int
inp_block_unblock_source(struct inpcb *inp, struct sockopt *sopt)
{
	struct group_source_req		 gsr;
	struct rm_priotracker		 in_ifa_tracker;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in_mfilter		*imf;
	struct ip_moptions		*imo;
	struct in_msource		*ims;
	struct in_multi			*inm;
	size_t				 idx;
	uint16_t			 fmode;
	int				 error, doblock;

	ifp = NULL;
	error = 0;
	doblock = 0;

	memset(&gsr, 0, sizeof(struct group_source_req));
	gsa = (sockunion_t *)&gsr.gsr_group;
	ssa = (sockunion_t *)&gsr.gsr_source;

	switch (sopt->sopt_name) {
	case IP_BLOCK_SOURCE:
	case IP_UNBLOCK_SOURCE: {
		struct ip_mreq_source	 mreqs;

		error = sooptcopyin(sopt, &mreqs,
		    sizeof(struct ip_mreq_source),
		    sizeof(struct ip_mreq_source));
		if (error)
			return (error);

		gsa->sin.sin_family = AF_INET;
		gsa->sin.sin_len = sizeof(struct sockaddr_in);
		gsa->sin.sin_addr = mreqs.imr_multiaddr;

		ssa->sin.sin_family = AF_INET;
		ssa->sin.sin_len = sizeof(struct sockaddr_in);
		ssa->sin.sin_addr = mreqs.imr_sourceaddr;

		if (!in_nullhost(mreqs.imr_interface)) {
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			INADDR_TO_IFP(mreqs.imr_interface, ifp);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		}
		if (sopt->sopt_name == IP_BLOCK_SOURCE)
			doblock = 1;

		CTR3(KTR_IGMPV3, "%s: imr_interface = 0x%08x, ifp = %p",
		    __func__, ntohl(mreqs.imr_interface.s_addr), ifp);
		break;
	    }

	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		error = sooptcopyin(sopt, &gsr,
		    sizeof(struct group_source_req),
		    sizeof(struct group_source_req));
		if (error)
			return (error);

		if (gsa->sin.sin_family != AF_INET ||
		    gsa->sin.sin_len != sizeof(struct sockaddr_in))
			return (EINVAL);

		if (ssa->sin.sin_family != AF_INET ||
		    ssa->sin.sin_len != sizeof(struct sockaddr_in))
			return (EINVAL);

		if (gsr.gsr_interface == 0 || V_if_index < gsr.gsr_interface)
			return (EADDRNOTAVAIL);

		ifp = ifnet_byindex(gsr.gsr_interface);

		if (sopt->sopt_name == MCAST_BLOCK_SOURCE)
			doblock = 1;
		break;

	default:
		CTR2(KTR_IGMPV3, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
		return (EINVAL);

	/*
	 * Check if we are actually a member of this group.
	 */
	imo = inp_findmoptions(inp);
	idx = imo_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->imo_mfilters == NULL) {
		error = EADDRNOTAVAIL;
		goto out_inp_locked;
	}

	KASSERT(imo->imo_mfilters != NULL,
	    ("%s: imo_mfilters not allocated", __func__));
	imf = &imo->imo_mfilters[idx];
	inm = imo->imo_membership[idx];

	/*
	 * Attempting to use the delta-based API on an
	 * non exclusive-mode membership is an error.
	 */
	fmode = imf->imf_st[0];
	if (fmode != MCAST_EXCLUDE) {
		error = EINVAL;
		goto out_inp_locked;
	}

	/*
	 * Deal with error cases up-front:
	 *  Asked to block, but already blocked; or
	 *  Asked to unblock, but nothing to unblock.
	 * If adding a new block entry, allocate it.
	 */
	ims = imo_match_source(imo, idx, &ssa->sa);
	if ((ims != NULL && doblock) || (ims == NULL && !doblock)) {
		CTR3(KTR_IGMPV3, "%s: source 0x%08x %spresent", __func__,
		    ntohl(ssa->sin.sin_addr.s_addr), doblock ? "" : "not ");
		error = EADDRNOTAVAIL;
		goto out_inp_locked;
	}

	INP_WLOCK_ASSERT(inp);

	/*
	 * Begin state merge transaction at socket layer.
	 */
	if (doblock) {
		CTR2(KTR_IGMPV3, "%s: %s source", __func__, "block");
		ims = imf_graft(imf, fmode, &ssa->sin);
		if (ims == NULL)
			error = ENOMEM;
	} else {
		CTR2(KTR_IGMPV3, "%s: %s source", __func__, "allow");
		error = imf_prune(imf, &ssa->sin);
	}

	if (error) {
		CTR1(KTR_IGMPV3, "%s: merge imf state failed", __func__);
		goto out_imf_rollback;
	}

	/*
	 * Begin state merge transaction at IGMP layer.
	 */
	IN_MULTI_LOCK();
	CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
	IN_MULTI_LIST_LOCK();
	error = inm_merge(inm, imf);
	if (error) {
		CTR1(KTR_IGMPV3, "%s: failed to merge inm state", __func__);
		IN_MULTI_LIST_UNLOCK();
		goto out_in_multi_locked;
	}

	CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
	error = igmp_change_state(inm);
	IN_MULTI_LIST_UNLOCK();
	if (error)
		CTR1(KTR_IGMPV3, "%s: failed igmp downcall", __func__);

out_in_multi_locked:

	IN_MULTI_UNLOCK();
out_imf_rollback:
	if (error)
		imf_rollback(imf);
	else
		imf_commit(imf);

	imf_reap(imf);

out_inp_locked:
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
static struct ip_moptions *
inp_findmoptions(struct inpcb *inp)
{
	struct ip_moptions	 *imo;
	struct in_multi		**immp;
	struct in_mfilter	 *imfp;
	size_t			  idx;

	INP_WLOCK(inp);
	if (inp->inp_moptions != NULL)
		return (inp->inp_moptions);

	INP_WUNLOCK(inp);

	imo = malloc(sizeof(*imo), M_IPMOPTS, M_WAITOK);
	immp = malloc(sizeof(*immp) * IP_MIN_MEMBERSHIPS, M_IPMOPTS,
	    M_WAITOK | M_ZERO);
	imfp = malloc(sizeof(struct in_mfilter) * IP_MIN_MEMBERSHIPS,
	    M_INMFILTER, M_WAITOK);

	imo->imo_multicast_ifp = NULL;
	imo->imo_multicast_addr.s_addr = INADDR_ANY;
	imo->imo_multicast_vif = -1;
	imo->imo_multicast_ttl = IP_DEFAULT_MULTICAST_TTL;
	imo->imo_multicast_loop = in_mcast_loop;
	imo->imo_num_memberships = 0;
	imo->imo_max_memberships = IP_MIN_MEMBERSHIPS;
	imo->imo_membership = immp;

	/* Initialize per-group source filters. */
	for (idx = 0; idx < IP_MIN_MEMBERSHIPS; idx++)
		imf_init(&imfp[idx], MCAST_UNDEFINED, MCAST_EXCLUDE);
	imo->imo_mfilters = imfp;

	INP_WLOCK(inp);
	if (inp->inp_moptions != NULL) {
		free(imfp, M_INMFILTER);
		free(immp, M_IPMOPTS);
		free(imo, M_IPMOPTS);
		return (inp->inp_moptions);
	}
	inp->inp_moptions = imo;
	return (imo);
}

static void
inp_gcmoptions(struct ip_moptions *imo)
{
	struct in_mfilter	*imf;
	struct in_multi *inm;
	struct ifnet *ifp;
	size_t			 idx, nmships;

	nmships = imo->imo_num_memberships;
	for (idx = 0; idx < nmships; ++idx) {
		imf = imo->imo_mfilters ? &imo->imo_mfilters[idx] : NULL;
		if (imf)
			imf_leave(imf);
		inm = imo->imo_membership[idx];
		ifp = inm->inm_ifp;
		if (ifp != NULL) {
			CURVNET_SET(ifp->if_vnet);
			(void)in_leavegroup(inm, imf);
			CURVNET_RESTORE();
		} else {
			(void)in_leavegroup(inm, imf);
		}
		if (imf)
			imf_purge(imf);
	}

	if (imo->imo_mfilters)
		free(imo->imo_mfilters, M_INMFILTER);
	free(imo->imo_membership, M_IPMOPTS);
	free(imo, M_IPMOPTS);
}

/*
 * Discard the IP multicast options (and source filters).  To minimize
 * the amount of work done while holding locks such as the INP's
 * pcbinfo lock (which is used in the receive path), the free
 * operation is deferred to the epoch callback task.
 */
void
inp_freemoptions(struct ip_moptions *imo)
{
	if (imo == NULL)
		return;
	inp_gcmoptions(imo);
}

/*
 * Atomically get source filters on a socket for an IPv4 multicast group.
 * Called with INP lock held; returns with lock released.
 */
static int
inp_get_source_filters(struct inpcb *inp, struct sockopt *sopt)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*gsa;
	struct ifnet		*ifp;
	struct ip_moptions	*imo;
	struct in_mfilter	*imf;
	struct ip_msource	*ims;
	struct in_msource	*lims;
	struct sockaddr_in	*psin;
	struct sockaddr_storage	*ptss;
	struct sockaddr_storage	*tss;
	int			 error;
	size_t			 idx, nsrcs, ncsrcs;

	INP_WLOCK_ASSERT(inp);

	imo = inp->inp_moptions;
	KASSERT(imo != NULL, ("%s: null ip_moptions", __func__));

	INP_WUNLOCK(inp);

	error = sooptcopyin(sopt, &msfr, sizeof(struct __msfilterreq),
	    sizeof(struct __msfilterreq));
	if (error)
		return (error);

	if (msfr.msfr_ifindex == 0 || V_if_index < msfr.msfr_ifindex)
		return (EINVAL);

	ifp = ifnet_byindex(msfr.msfr_ifindex);
	if (ifp == NULL)
		return (EINVAL);

	INP_WLOCK(inp);

	/*
	 * Lookup group on the socket.
	 */
	gsa = (sockunion_t *)&msfr.msfr_group;
	idx = imo_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->imo_mfilters == NULL) {
		INP_WUNLOCK(inp);
		return (EADDRNOTAVAIL);
	}
	imf = &imo->imo_mfilters[idx];

	/*
	 * Ignore memberships which are in limbo.
	 */
	if (imf->imf_st[1] == MCAST_UNDEFINED) {
		INP_WUNLOCK(inp);
		return (EAGAIN);
	}
	msfr.msfr_fmode = imf->imf_st[1];

	/*
	 * If the user specified a buffer, copy out the source filter
	 * entries to userland gracefully.
	 * We only copy out the number of entries which userland
	 * has asked for, but we always tell userland how big the
	 * buffer really needs to be.
	 */
	if (msfr.msfr_nsrcs > in_mcast_maxsocksrc)
		msfr.msfr_nsrcs = in_mcast_maxsocksrc;
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
	RB_FOREACH(ims, ip_msource_tree, &imf->imf_sources) {
		lims = (struct in_msource *)ims;
		if (lims->imsl_st[0] == MCAST_UNDEFINED ||
		    lims->imsl_st[0] != imf->imf_st[0])
			continue;
		++ncsrcs;
		if (tss != NULL && nsrcs > 0) {
			psin = (struct sockaddr_in *)ptss;
			psin->sin_family = AF_INET;
			psin->sin_len = sizeof(struct sockaddr_in);
			psin->sin_addr.s_addr = htonl(lims->ims_haddr);
			psin->sin_port = 0;
			++ptss;
			--nsrcs;
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
inp_getmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	struct rm_priotracker	 in_ifa_tracker;
	struct ip_mreqn		 mreqn;
	struct ip_moptions	*imo;
	struct ifnet		*ifp;
	struct in_ifaddr	*ia;
	int			 error, optval;
	u_char			 coptval;

	INP_WLOCK(inp);
	imo = inp->inp_moptions;
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
	case IP_MULTICAST_VIF:
		if (imo != NULL)
			optval = imo->imo_multicast_vif;
		else
			optval = -1;
		INP_WUNLOCK(inp);
		error = sooptcopyout(sopt, &optval, sizeof(int));
		break;

	case IP_MULTICAST_IF:
		memset(&mreqn, 0, sizeof(struct ip_mreqn));
		if (imo != NULL) {
			ifp = imo->imo_multicast_ifp;
			if (!in_nullhost(imo->imo_multicast_addr)) {
				mreqn.imr_address = imo->imo_multicast_addr;
			} else if (ifp != NULL) {
				struct epoch_tracker et;

				mreqn.imr_ifindex = ifp->if_index;
				NET_EPOCH_ENTER(et);
				IFP_TO_IA(ifp, ia, &in_ifa_tracker);
				if (ia != NULL)
					mreqn.imr_address =
					    IA_SIN(ia)->sin_addr;
				NET_EPOCH_EXIT(et);
			}
		}
		INP_WUNLOCK(inp);
		if (sopt->sopt_valsize == sizeof(struct ip_mreqn)) {
			error = sooptcopyout(sopt, &mreqn,
			    sizeof(struct ip_mreqn));
		} else {
			error = sooptcopyout(sopt, &mreqn.imr_address,
			    sizeof(struct in_addr));
		}
		break;

	case IP_MULTICAST_TTL:
		if (imo == NULL)
			optval = coptval = IP_DEFAULT_MULTICAST_TTL;
		else
			optval = coptval = imo->imo_multicast_ttl;
		INP_WUNLOCK(inp);
		if (sopt->sopt_valsize == sizeof(u_char))
			error = sooptcopyout(sopt, &coptval, sizeof(u_char));
		else
			error = sooptcopyout(sopt, &optval, sizeof(int));
		break;

	case IP_MULTICAST_LOOP:
		if (imo == NULL)
			optval = coptval = IP_DEFAULT_MULTICAST_LOOP;
		else
			optval = coptval = imo->imo_multicast_loop;
		INP_WUNLOCK(inp);
		if (sopt->sopt_valsize == sizeof(u_char))
			error = sooptcopyout(sopt, &coptval, sizeof(u_char));
		else
			error = sooptcopyout(sopt, &optval, sizeof(int));
		break;

	case IP_MSFILTER:
		if (imo == NULL) {
			error = EADDRNOTAVAIL;
			INP_WUNLOCK(inp);
		} else {
			error = inp_get_source_filters(inp, sopt);
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
 * given the IPv4 address of an interface, and the IPv4 group address.
 *
 * This routine exists to support legacy multicast applications
 * which do not understand that multicast memberships are scoped to
 * specific physical links in the networking stack, or which need
 * to join link-scope groups before IPv4 addresses are configured.
 *
 * If inp is non-NULL, use this socket's current FIB number for any
 * required FIB lookup.
 * If ina is INADDR_ANY, look up the group address in the unicast FIB,
 * and use its ifp; usually, this points to the default next-hop.
 *
 * If the FIB lookup fails, attempt to use the first non-loopback
 * interface with multicast capability in the system as a
 * last resort. The legacy IPv4 ASM API requires that we do
 * this in order to allow groups to be joined when the routing
 * table has not yet been populated during boot.
 *
 * Returns NULL if no ifp could be found.
 *
 * FUTURE: Implement IPv4 source-address selection.
 */
static struct ifnet *
inp_lookup_mcast_ifp(const struct inpcb *inp,
    const struct sockaddr_in *gsin, const struct in_addr ina)
{
	struct rm_priotracker in_ifa_tracker;
	struct ifnet *ifp;
	struct nhop4_basic nh4;
	uint32_t fibnum;

	KASSERT(gsin->sin_family == AF_INET, ("%s: not AF_INET", __func__));
	KASSERT(IN_MULTICAST(ntohl(gsin->sin_addr.s_addr)),
	    ("%s: not multicast", __func__));

	ifp = NULL;
	if (!in_nullhost(ina)) {
		IN_IFADDR_RLOCK(&in_ifa_tracker);
		INADDR_TO_IFP(ina, ifp);
		IN_IFADDR_RUNLOCK(&in_ifa_tracker);
	} else {
		fibnum = inp ? inp->inp_inc.inc_fibnum : 0;
		if (fib4_lookup_nh_basic(fibnum, gsin->sin_addr, 0, 0, &nh4)==0)
			ifp = nh4.nh_ifp;
		else {
			struct in_ifaddr *ia;
			struct ifnet *mifp;

			mifp = NULL;
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			CK_STAILQ_FOREACH(ia, &V_in_ifaddrhead, ia_link) {
				mifp = ia->ia_ifp;
				if (!(mifp->if_flags & IFF_LOOPBACK) &&
				     (mifp->if_flags & IFF_MULTICAST)) {
					ifp = mifp;
					break;
				}
			}
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		}
	}

	return (ifp);
}

/*
 * Join an IPv4 multicast group, possibly with a source.
 */
static int
inp_join_group(struct inpcb *inp, struct sockopt *sopt)
{
	struct group_source_req		 gsr;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in_mfilter		*imf;
	struct ip_moptions		*imo;
	struct in_multi			*inm;
	struct in_msource		*lims;
	size_t				 idx;
	int				 error, is_new;

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

	switch (sopt->sopt_name) {
	case IP_ADD_MEMBERSHIP: {
		struct ip_mreqn mreqn;

		if (sopt->sopt_valsize == sizeof(struct ip_mreqn))
			error = sooptcopyin(sopt, &mreqn,
			    sizeof(struct ip_mreqn), sizeof(struct ip_mreqn));
		else
			error = sooptcopyin(sopt, &mreqn,
			    sizeof(struct ip_mreq), sizeof(struct ip_mreq));
		if (error)
			return (error);

		gsa->sin.sin_family = AF_INET;
		gsa->sin.sin_len = sizeof(struct sockaddr_in);
		gsa->sin.sin_addr = mreqn.imr_multiaddr;
		if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
			return (EINVAL);

		if (sopt->sopt_valsize == sizeof(struct ip_mreqn) &&
		    mreqn.imr_ifindex != 0)
			ifp = ifnet_byindex(mreqn.imr_ifindex);
		else
			ifp = inp_lookup_mcast_ifp(inp, &gsa->sin,
			    mreqn.imr_address);
		break;
	}
	case IP_ADD_SOURCE_MEMBERSHIP: {
		struct ip_mreq_source	 mreqs;

		error = sooptcopyin(sopt, &mreqs, sizeof(struct ip_mreq_source),
			    sizeof(struct ip_mreq_source));
		if (error)
			return (error);

		gsa->sin.sin_family = ssa->sin.sin_family = AF_INET;
		gsa->sin.sin_len = ssa->sin.sin_len =
		    sizeof(struct sockaddr_in);

		gsa->sin.sin_addr = mreqs.imr_multiaddr;
		if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
			return (EINVAL);

		ssa->sin.sin_addr = mreqs.imr_sourceaddr;

		ifp = inp_lookup_mcast_ifp(inp, &gsa->sin,
		    mreqs.imr_interface);
		CTR3(KTR_IGMPV3, "%s: imr_interface = 0x%08x, ifp = %p",
		    __func__, ntohl(mreqs.imr_interface.s_addr), ifp);
		break;
	}

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

		if (gsa->sin.sin_family != AF_INET ||
		    gsa->sin.sin_len != sizeof(struct sockaddr_in))
			return (EINVAL);

		/*
		 * Overwrite the port field if present, as the sockaddr
		 * being copied in may be matched with a binary comparison.
		 */
		gsa->sin.sin_port = 0;
		if (sopt->sopt_name == MCAST_JOIN_SOURCE_GROUP) {
			if (ssa->sin.sin_family != AF_INET ||
			    ssa->sin.sin_len != sizeof(struct sockaddr_in))
				return (EINVAL);
			ssa->sin.sin_port = 0;
		}

		if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
			return (EINVAL);

		if (gsr.gsr_interface == 0 || V_if_index < gsr.gsr_interface)
			return (EADDRNOTAVAIL);
		ifp = ifnet_byindex(gsr.gsr_interface);
		break;

	default:
		CTR2(KTR_IGMPV3, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (ifp == NULL || (ifp->if_flags & IFF_MULTICAST) == 0)
		return (EADDRNOTAVAIL);

	imo = inp_findmoptions(inp);
	idx = imo_match_group(imo, ifp, &gsa->sa);
	if (idx == -1) {
		is_new = 1;
	} else {
		inm = imo->imo_membership[idx];
		imf = &imo->imo_mfilters[idx];
		if (ssa->ss.ss_family != AF_UNSPEC) {
			/*
			 * MCAST_JOIN_SOURCE_GROUP on an exclusive membership
			 * is an error. On an existing inclusive membership,
			 * it just adds the source to the filter list.
			 */
			if (imf->imf_st[1] != MCAST_INCLUDE) {
				error = EINVAL;
				goto out_inp_locked;
			}
			/*
			 * Throw out duplicates.
			 *
			 * XXX FIXME: This makes a naive assumption that
			 * even if entries exist for *ssa in this imf,
			 * they will be rejected as dupes, even if they
			 * are not valid in the current mode (in-mode).
			 *
			 * in_msource is transactioned just as for anything
			 * else in SSM -- but note naive use of inm_graft()
			 * below for allocating new filter entries.
			 *
			 * This is only an issue if someone mixes the
			 * full-state SSM API with the delta-based API,
			 * which is discouraged in the relevant RFCs.
			 */
			lims = imo_match_source(imo, idx, &ssa->sa);
			if (lims != NULL /*&&
			    lims->imsl_st[1] == MCAST_INCLUDE*/) {
				error = EADDRNOTAVAIL;
				goto out_inp_locked;
			}
		} else {
			/*
			 * MCAST_JOIN_GROUP on an existing exclusive
			 * membership is an error; return EADDRINUSE
			 * to preserve 4.4BSD API idempotence, and
			 * avoid tedious detour to code below.
			 * NOTE: This is bending RFC 3678 a bit.
			 *
			 * On an existing inclusive membership, this is also
			 * an error; if you want to change filter mode,
			 * you must use the userland API setsourcefilter().
			 * XXX We don't reject this for imf in UNDEFINED
			 * state at t1, because allocation of a filter
			 * is atomic with allocation of a membership.
			 */
			error = EINVAL;
			if (imf->imf_st[1] == MCAST_EXCLUDE)
				error = EADDRINUSE;
			goto out_inp_locked;
		}
	}

	/*
	 * Begin state merge transaction at socket layer.
	 */
	INP_WLOCK_ASSERT(inp);

	if (is_new) {
		if (imo->imo_num_memberships == imo->imo_max_memberships) {
			error = imo_grow(imo);
			if (error)
				goto out_inp_locked;
		}
		/*
		 * Allocate the new slot upfront so we can deal with
		 * grafting the new source filter in same code path
		 * as for join-source on existing membership.
		 */
		idx = imo->imo_num_memberships;
		imo->imo_membership[idx] = NULL;
		imo->imo_num_memberships++;
		KASSERT(imo->imo_mfilters != NULL,
		    ("%s: imf_mfilters vector was not allocated", __func__));
		imf = &imo->imo_mfilters[idx];
		KASSERT(RB_EMPTY(&imf->imf_sources),
		    ("%s: imf_sources not empty", __func__));
	}

	/*
	 * Graft new source into filter list for this inpcb's
	 * membership of the group. The in_multi may not have
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
			CTR1(KTR_IGMPV3, "%s: new join w/source", __func__);
			imf_init(imf, MCAST_UNDEFINED, MCAST_INCLUDE);
		} else {
			CTR2(KTR_IGMPV3, "%s: %s source", __func__, "allow");
		}
		lims = imf_graft(imf, MCAST_INCLUDE, &ssa->sin);
		if (lims == NULL) {
			CTR1(KTR_IGMPV3, "%s: merge imf state failed",
			    __func__);
			error = ENOMEM;
			goto out_imo_free;
		}
	} else {
		/* No address specified; Membership starts in EX mode */
		if (is_new) {
			CTR1(KTR_IGMPV3, "%s: new join w/o source", __func__);
			imf_init(imf, MCAST_UNDEFINED, MCAST_EXCLUDE);
		}
	}

	/*
	 * Begin state merge transaction at IGMP layer.
	 */
	in_pcbref(inp);
	INP_WUNLOCK(inp);
	IN_MULTI_LOCK();

	if (is_new) {
		error = in_joingroup_locked(ifp, &gsa->sin.sin_addr, imf,
		    &inm);
		if (error) {
                        CTR1(KTR_IGMPV3, "%s: in_joingroup_locked failed", 
                            __func__);
                        IN_MULTI_LIST_UNLOCK();
			goto out_imo_free;
		}
		inm_acquire(inm);
		imo->imo_membership[idx] = inm;
	} else {
		CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
		IN_MULTI_LIST_LOCK();
		error = inm_merge(inm, imf);
		if (error) {
			CTR1(KTR_IGMPV3, "%s: failed to merge inm state",
				 __func__);
			IN_MULTI_LIST_UNLOCK();
			goto out_in_multi_locked;
		}
		CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
		error = igmp_change_state(inm);
		IN_MULTI_LIST_UNLOCK();
		if (error) {
			CTR1(KTR_IGMPV3, "%s: failed igmp downcall",
			    __func__);
			goto out_in_multi_locked;
		}
	}

out_in_multi_locked:

	IN_MULTI_UNLOCK();
	INP_WLOCK(inp);
	if (in_pcbrele_wlocked(inp))
		return (ENXIO);
	if (error) {
		imf_rollback(imf);
		if (is_new)
			imf_purge(imf);
		else
			imf_reap(imf);
	} else {
		imf_commit(imf);
	}

out_imo_free:
	if (error && is_new) {
		inm = imo->imo_membership[idx];
		if (inm != NULL) {
			IN_MULTI_LIST_LOCK();
			inm_release_deferred(inm);
			IN_MULTI_LIST_UNLOCK();
		}
		imo->imo_membership[idx] = NULL;
		--imo->imo_num_memberships;
	}

out_inp_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Leave an IPv4 multicast group on an inpcb, possibly with a source.
 */
static int
inp_leave_group(struct inpcb *inp, struct sockopt *sopt)
{
	struct group_source_req		 gsr;
	struct ip_mreq_source		 mreqs;
	struct rm_priotracker		 in_ifa_tracker;
	sockunion_t			*gsa, *ssa;
	struct ifnet			*ifp;
	struct in_mfilter		*imf;
	struct ip_moptions		*imo;
	struct in_msource		*ims;
	struct in_multi			*inm;
	size_t				 idx;
	int				 error, is_final;

	ifp = NULL;
	error = 0;
	is_final = 1;

	memset(&gsr, 0, sizeof(struct group_source_req));
	gsa = (sockunion_t *)&gsr.gsr_group;
	gsa->ss.ss_family = AF_UNSPEC;
	ssa = (sockunion_t *)&gsr.gsr_source;
	ssa->ss.ss_family = AF_UNSPEC;

	switch (sopt->sopt_name) {
	case IP_DROP_MEMBERSHIP:
	case IP_DROP_SOURCE_MEMBERSHIP:
		if (sopt->sopt_name == IP_DROP_MEMBERSHIP) {
			error = sooptcopyin(sopt, &mreqs,
			    sizeof(struct ip_mreq),
			    sizeof(struct ip_mreq));
			/*
			 * Swap interface and sourceaddr arguments,
			 * as ip_mreq and ip_mreq_source are laid
			 * out differently.
			 */
			mreqs.imr_interface = mreqs.imr_sourceaddr;
			mreqs.imr_sourceaddr.s_addr = INADDR_ANY;
		} else if (sopt->sopt_name == IP_DROP_SOURCE_MEMBERSHIP) {
			error = sooptcopyin(sopt, &mreqs,
			    sizeof(struct ip_mreq_source),
			    sizeof(struct ip_mreq_source));
		}
		if (error)
			return (error);

		gsa->sin.sin_family = AF_INET;
		gsa->sin.sin_len = sizeof(struct sockaddr_in);
		gsa->sin.sin_addr = mreqs.imr_multiaddr;

		if (sopt->sopt_name == IP_DROP_SOURCE_MEMBERSHIP) {
			ssa->sin.sin_family = AF_INET;
			ssa->sin.sin_len = sizeof(struct sockaddr_in);
			ssa->sin.sin_addr = mreqs.imr_sourceaddr;
		}

		/*
		 * Attempt to look up hinted ifp from interface address.
		 * Fallthrough with null ifp iff lookup fails, to
		 * preserve 4.4BSD mcast API idempotence.
		 * XXX NOTE WELL: The RFC 3678 API is preferred because
		 * using an IPv4 address as a key is racy.
		 */
		if (!in_nullhost(mreqs.imr_interface)) {
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			INADDR_TO_IFP(mreqs.imr_interface, ifp);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
		}
		CTR3(KTR_IGMPV3, "%s: imr_interface = 0x%08x, ifp = %p",
		    __func__, ntohl(mreqs.imr_interface.s_addr), ifp);

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

		if (gsa->sin.sin_family != AF_INET ||
		    gsa->sin.sin_len != sizeof(struct sockaddr_in))
			return (EINVAL);

		if (sopt->sopt_name == MCAST_LEAVE_SOURCE_GROUP) {
			if (ssa->sin.sin_family != AF_INET ||
			    ssa->sin.sin_len != sizeof(struct sockaddr_in))
				return (EINVAL);
		}

		if (gsr.gsr_interface == 0 || V_if_index < gsr.gsr_interface)
			return (EADDRNOTAVAIL);

		ifp = ifnet_byindex(gsr.gsr_interface);

		if (ifp == NULL)
			return (EADDRNOTAVAIL);
		break;

	default:
		CTR2(KTR_IGMPV3, "%s: unknown sopt_name %d",
		    __func__, sopt->sopt_name);
		return (EOPNOTSUPP);
		break;
	}

	if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
		return (EINVAL);

	/*
	 * Find the membership in the membership array.
	 */
	imo = inp_findmoptions(inp);
	idx = imo_match_group(imo, ifp, &gsa->sa);
	if (idx == -1) {
		error = EADDRNOTAVAIL;
		goto out_inp_locked;
	}
	inm = imo->imo_membership[idx];
	imf = &imo->imo_mfilters[idx];

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
		imf_leave(imf);
	} else {
		if (imf->imf_st[0] == MCAST_EXCLUDE) {
			error = EADDRNOTAVAIL;
			goto out_inp_locked;
		}
		ims = imo_match_source(imo, idx, &ssa->sa);
		if (ims == NULL) {
			CTR3(KTR_IGMPV3, "%s: source 0x%08x %spresent",
			    __func__, ntohl(ssa->sin.sin_addr.s_addr), "not ");
			error = EADDRNOTAVAIL;
			goto out_inp_locked;
		}
		CTR2(KTR_IGMPV3, "%s: %s source", __func__, "block");
		error = imf_prune(imf, &ssa->sin);
		if (error) {
			CTR1(KTR_IGMPV3, "%s: merge imf state failed",
			    __func__);
			goto out_inp_locked;
		}
	}

	/*
	 * Begin state merge transaction at IGMP layer.
	 */
	in_pcbref(inp);
	INP_WUNLOCK(inp);
	IN_MULTI_LOCK();

	if (is_final) {
		/*
		 * Give up the multicast address record to which
		 * the membership points.
		 */
		(void)in_leavegroup_locked(inm, imf);
	} else {
		CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
		IN_MULTI_LIST_LOCK();
		error = inm_merge(inm, imf);
		if (error) {
			CTR1(KTR_IGMPV3, "%s: failed to merge inm state",
			    __func__);
			IN_MULTI_LIST_UNLOCK();
			goto out_in_multi_locked;
		}

		CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
		error = igmp_change_state(inm);
		IN_MULTI_LIST_UNLOCK();
		if (error) {
			CTR1(KTR_IGMPV3, "%s: failed igmp downcall",
			    __func__);
		}
	}

out_in_multi_locked:

	IN_MULTI_UNLOCK();
	INP_WLOCK(inp);
	if (in_pcbrele_wlocked(inp))
		return (ENXIO);

	if (error)
		imf_rollback(imf);
	else
		imf_commit(imf);

	imf_reap(imf);

	if (is_final) {
		/* Remove the gap in the membership and filter array. */
		for (++idx; idx < imo->imo_num_memberships; ++idx) {
			imo->imo_membership[idx-1] = imo->imo_membership[idx];
			imo->imo_mfilters[idx-1] = imo->imo_mfilters[idx];
		}
		imo->imo_num_memberships--;
	}

out_inp_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Select the interface for transmitting IPv4 multicast datagrams.
 *
 * Either an instance of struct in_addr or an instance of struct ip_mreqn
 * may be passed to this socket option. An address of INADDR_ANY or an
 * interface index of 0 is used to remove a previous selection.
 * When no interface is selected, one is chosen for every send.
 */
static int
inp_set_multicast_if(struct inpcb *inp, struct sockopt *sopt)
{
	struct rm_priotracker	 in_ifa_tracker;
	struct in_addr		 addr;
	struct ip_mreqn		 mreqn;
	struct ifnet		*ifp;
	struct ip_moptions	*imo;
	int			 error;

	if (sopt->sopt_valsize == sizeof(struct ip_mreqn)) {
		/*
		 * An interface index was specified using the
		 * Linux-derived ip_mreqn structure.
		 */
		error = sooptcopyin(sopt, &mreqn, sizeof(struct ip_mreqn),
		    sizeof(struct ip_mreqn));
		if (error)
			return (error);

		if (mreqn.imr_ifindex < 0 || V_if_index < mreqn.imr_ifindex)
			return (EINVAL);

		if (mreqn.imr_ifindex == 0) {
			ifp = NULL;
		} else {
			ifp = ifnet_byindex(mreqn.imr_ifindex);
			if (ifp == NULL)
				return (EADDRNOTAVAIL);
		}
	} else {
		/*
		 * An interface was specified by IPv4 address.
		 * This is the traditional BSD usage.
		 */
		error = sooptcopyin(sopt, &addr, sizeof(struct in_addr),
		    sizeof(struct in_addr));
		if (error)
			return (error);
		if (in_nullhost(addr)) {
			ifp = NULL;
		} else {
			IN_IFADDR_RLOCK(&in_ifa_tracker);
			INADDR_TO_IFP(addr, ifp);
			IN_IFADDR_RUNLOCK(&in_ifa_tracker);
			if (ifp == NULL)
				return (EADDRNOTAVAIL);
		}
		CTR3(KTR_IGMPV3, "%s: ifp = %p, addr = 0x%08x", __func__, ifp,
		    ntohl(addr.s_addr));
	}

	/* Reject interfaces which do not support multicast. */
	if (ifp != NULL && (ifp->if_flags & IFF_MULTICAST) == 0)
		return (EOPNOTSUPP);

	imo = inp_findmoptions(inp);
	imo->imo_multicast_ifp = ifp;
	imo->imo_multicast_addr.s_addr = INADDR_ANY;
	INP_WUNLOCK(inp);

	return (0);
}

/*
 * Atomically set source filters on a socket for an IPv4 multicast group.
 *
 * SMPng: NOTE: Potentially calls malloc(M_WAITOK) with Giant held.
 */
static int
inp_set_source_filters(struct inpcb *inp, struct sockopt *sopt)
{
	struct __msfilterreq	 msfr;
	sockunion_t		*gsa;
	struct ifnet		*ifp;
	struct in_mfilter	*imf;
	struct ip_moptions	*imo;
	struct in_multi		*inm;
	size_t			 idx;
	int			 error;

	error = sooptcopyin(sopt, &msfr, sizeof(struct __msfilterreq),
	    sizeof(struct __msfilterreq));
	if (error)
		return (error);

	if (msfr.msfr_nsrcs > in_mcast_maxsocksrc)
		return (ENOBUFS);

	if ((msfr.msfr_fmode != MCAST_EXCLUDE &&
	     msfr.msfr_fmode != MCAST_INCLUDE))
		return (EINVAL);

	if (msfr.msfr_group.ss_family != AF_INET ||
	    msfr.msfr_group.ss_len != sizeof(struct sockaddr_in))
		return (EINVAL);

	gsa = (sockunion_t *)&msfr.msfr_group;
	if (!IN_MULTICAST(ntohl(gsa->sin.sin_addr.s_addr)))
		return (EINVAL);

	gsa->sin.sin_port = 0;	/* ignore port */

	if (msfr.msfr_ifindex == 0 || V_if_index < msfr.msfr_ifindex)
		return (EADDRNOTAVAIL);

	ifp = ifnet_byindex(msfr.msfr_ifindex);
	if (ifp == NULL)
		return (EADDRNOTAVAIL);

	/*
	 * Take the INP write lock.
	 * Check if this socket is a member of this group.
	 */
	imo = inp_findmoptions(inp);
	idx = imo_match_group(imo, ifp, &gsa->sa);
	if (idx == -1 || imo->imo_mfilters == NULL) {
		error = EADDRNOTAVAIL;
		goto out_inp_locked;
	}
	inm = imo->imo_membership[idx];
	imf = &imo->imo_mfilters[idx];

	/*
	 * Begin state merge transaction at socket layer.
	 */
	INP_WLOCK_ASSERT(inp);

	imf->imf_st[1] = msfr.msfr_fmode;

	/*
	 * Apply any new source filters, if present.
	 * Make a copy of the user-space source vector so
	 * that we may copy them with a single copyin. This
	 * allows us to deal with page faults up-front.
	 */
	if (msfr.msfr_nsrcs > 0) {
		struct in_msource	*lims;
		struct sockaddr_in	*psin;
		struct sockaddr_storage	*kss, *pkss;
		int			 i;

		INP_WUNLOCK(inp);
 
		CTR2(KTR_IGMPV3, "%s: loading %lu source list entries",
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
		 * Restore new group filter mode, as imf_leave()
		 * will set it to INCLUDE.
		 */
		imf_leave(imf);
		imf->imf_st[1] = msfr.msfr_fmode;

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
			psin = (struct sockaddr_in *)pkss;
			if (psin->sin_family != AF_INET) {
				error = EAFNOSUPPORT;
				break;
			}
			if (psin->sin_len != sizeof(struct sockaddr_in)) {
				error = EINVAL;
				break;
			}
			error = imf_get_source(imf, psin, &lims);
			if (error)
				break;
			lims->imsl_st[1] = imf->imf_st[1];
		}
		free(kss, M_TEMP);
	}

	if (error)
		goto out_imf_rollback;

	INP_WLOCK_ASSERT(inp);
	IN_MULTI_LOCK();

	/*
	 * Begin state merge transaction at IGMP layer.
	 */
	CTR1(KTR_IGMPV3, "%s: merge inm state", __func__);
	IN_MULTI_LIST_LOCK();
	error = inm_merge(inm, imf);
	if (error) {
		CTR1(KTR_IGMPV3, "%s: failed to merge inm state", __func__);
		IN_MULTI_LIST_UNLOCK();
		goto out_in_multi_locked;
	}

	CTR1(KTR_IGMPV3, "%s: doing igmp downcall", __func__);
	error = igmp_change_state(inm);
	IN_MULTI_LIST_UNLOCK();
	if (error)
		CTR1(KTR_IGMPV3, "%s: failed igmp downcall", __func__);

out_in_multi_locked:

	IN_MULTI_UNLOCK();

out_imf_rollback:
	if (error)
		imf_rollback(imf);
	else
		imf_commit(imf);

	imf_reap(imf);

out_inp_locked:
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Set the IP multicast options in response to user setsockopt().
 *
 * Many of the socket options handled in this function duplicate the
 * functionality of socket options in the regular unicast API. However,
 * it is not possible to merge the duplicate code, because the idempotence
 * of the IPv4 multicast part of the BSD Sockets API must be preserved;
 * the effects of these options must be treated as separate and distinct.
 *
 * SMPng: XXX: Unlocked read of inp_socket believed OK.
 * FUTURE: The IP_MULTICAST_VIF option may be eliminated if MROUTING
 * is refactored to no longer use vifs.
 */
int
inp_setmoptions(struct inpcb *inp, struct sockopt *sopt)
{
	struct ip_moptions	*imo;
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
	case IP_MULTICAST_VIF: {
		int vifi;
		/*
		 * Select a multicast VIF for transmission.
		 * Only useful if multicast forwarding is active.
		 */
		if (legal_vif_num == NULL) {
			error = EOPNOTSUPP;
			break;
		}
		error = sooptcopyin(sopt, &vifi, sizeof(int), sizeof(int));
		if (error)
			break;
		if (!legal_vif_num(vifi) && (vifi != -1)) {
			error = EINVAL;
			break;
		}
		imo = inp_findmoptions(inp);
		imo->imo_multicast_vif = vifi;
		INP_WUNLOCK(inp);
		break;
	}

	case IP_MULTICAST_IF:
		error = inp_set_multicast_if(inp, sopt);
		break;

	case IP_MULTICAST_TTL: {
		u_char ttl;

		/*
		 * Set the IP time-to-live for outgoing multicast packets.
		 * The original multicast API required a char argument,
		 * which is inconsistent with the rest of the socket API.
		 * We allow either a char or an int.
		 */
		if (sopt->sopt_valsize == sizeof(u_char)) {
			error = sooptcopyin(sopt, &ttl, sizeof(u_char),
			    sizeof(u_char));
			if (error)
				break;
		} else {
			u_int ittl;

			error = sooptcopyin(sopt, &ittl, sizeof(u_int),
			    sizeof(u_int));
			if (error)
				break;
			if (ittl > 255) {
				error = EINVAL;
				break;
			}
			ttl = (u_char)ittl;
		}
		imo = inp_findmoptions(inp);
		imo->imo_multicast_ttl = ttl;
		INP_WUNLOCK(inp);
		break;
	}

	case IP_MULTICAST_LOOP: {
		u_char loop;

		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.  The original multicast API required a
		 * char argument, which is inconsistent with the rest
		 * of the socket API.  We allow either a char or an int.
		 */
		if (sopt->sopt_valsize == sizeof(u_char)) {
			error = sooptcopyin(sopt, &loop, sizeof(u_char),
			    sizeof(u_char));
			if (error)
				break;
		} else {
			u_int iloop;

			error = sooptcopyin(sopt, &iloop, sizeof(u_int),
					    sizeof(u_int));
			if (error)
				break;
			loop = (u_char)iloop;
		}
		imo = inp_findmoptions(inp);
		imo->imo_multicast_loop = !!loop;
		INP_WUNLOCK(inp);
		break;
	}

	case IP_ADD_MEMBERSHIP:
	case IP_ADD_SOURCE_MEMBERSHIP:
	case MCAST_JOIN_GROUP:
	case MCAST_JOIN_SOURCE_GROUP:
		error = inp_join_group(inp, sopt);
		break;

	case IP_DROP_MEMBERSHIP:
	case IP_DROP_SOURCE_MEMBERSHIP:
	case MCAST_LEAVE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
		error = inp_leave_group(inp, sopt);
		break;

	case IP_BLOCK_SOURCE:
	case IP_UNBLOCK_SOURCE:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		error = inp_block_unblock_source(inp, sopt);
		break;

	case IP_MSFILTER:
		error = inp_set_source_filters(inp, sopt);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	INP_UNLOCK_ASSERT(inp);

	return (error);
}

/*
 * Expose IGMP's multicast filter mode and source list(s) to userland,
 * keyed by (ifindex, group).
 * The filter mode is written out as a uint32_t, followed by
 * 0..n of struct in_addr.
 * For use by ifmcstat(8).
 * SMPng: NOTE: unlocked read of ifindex space.
 */
static int
sysctl_ip_mcast_filters(SYSCTL_HANDLER_ARGS)
{
	struct in_addr			 src, group;
	struct epoch_tracker		 et;
	struct ifnet			*ifp;
	struct ifmultiaddr		*ifma;
	struct in_multi			*inm;
	struct ip_msource		*ims;
	int				*name;
	int				 retval;
	u_int				 namelen;
	uint32_t			 fmode, ifindex;

	name = (int *)arg1;
	namelen = arg2;

	if (req->newptr != NULL)
		return (EPERM);

	if (namelen != 2)
		return (EINVAL);

	ifindex = name[0];
	if (ifindex <= 0 || ifindex > V_if_index) {
		CTR2(KTR_IGMPV3, "%s: ifindex %u out of range",
		    __func__, ifindex);
		return (ENOENT);
	}

	group.s_addr = name[1];
	if (!IN_MULTICAST(ntohl(group.s_addr))) {
		CTR2(KTR_IGMPV3, "%s: group 0x%08x is not multicast",
		    __func__, ntohl(group.s_addr));
		return (EINVAL);
	}

	ifp = ifnet_byindex(ifindex);
	if (ifp == NULL) {
		CTR2(KTR_IGMPV3, "%s: no ifp for ifindex %u",
		    __func__, ifindex);
		return (ENOENT);
	}

	retval = sysctl_wire_old_buffer(req,
	    sizeof(uint32_t) + (in_mcast_maxgrpsrc * sizeof(struct in_addr)));
	if (retval)
		return (retval);

	IN_MULTI_LIST_LOCK();

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_INET ||
		    ifma->ifma_protospec == NULL)
			continue;
		inm = (struct in_multi *)ifma->ifma_protospec;
		if (!in_hosteq(inm->inm_addr, group))
			continue;
		fmode = inm->inm_st[1].iss_fmode;
		retval = SYSCTL_OUT(req, &fmode, sizeof(uint32_t));
		if (retval != 0)
			break;
		RB_FOREACH(ims, ip_msource_tree, &inm->inm_srcs) {
			CTR2(KTR_IGMPV3, "%s: visit node 0x%08x", __func__,
			    ims->ims_haddr);
			/*
			 * Only copy-out sources which are in-mode.
			 */
			if (fmode != ims_get_mode(inm, ims, 1)) {
				CTR1(KTR_IGMPV3, "%s: skip non-in-mode",
				    __func__);
				continue;
			}
			src.s_addr = htonl(ims->ims_haddr);
			retval = SYSCTL_OUT(req, &src, sizeof(struct in_addr));
			if (retval != 0)
				break;
		}
	}
	NET_EPOCH_EXIT(et);

	IN_MULTI_LIST_UNLOCK();

	return (retval);
}

#if defined(KTR) && (KTR_COMPILE & KTR_IGMPV3)

static const char *inm_modestrs[] = { "un", "in", "ex" };

static const char *
inm_mode_str(const int mode)
{

	if (mode >= MCAST_UNDEFINED && mode <= MCAST_EXCLUDE)
		return (inm_modestrs[mode]);
	return ("??");
}

static const char *inm_statestrs[] = {
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
inm_state_str(const int state)
{

	if (state >= IGMP_NOT_MEMBER && state <= IGMP_LEAVING_MEMBER)
		return (inm_statestrs[state]);
	return ("??");
}

/*
 * Dump an in_multi structure to the console.
 */
void
inm_print(const struct in_multi *inm)
{
	int t;
	char addrbuf[INET_ADDRSTRLEN];

	if ((ktr_mask & KTR_IGMPV3) == 0)
		return;

	printf("%s: --- begin inm %p ---\n", __func__, inm);
	printf("addr %s ifp %p(%s) ifma %p\n",
	    inet_ntoa_r(inm->inm_addr, addrbuf),
	    inm->inm_ifp,
	    inm->inm_ifp->if_xname,
	    inm->inm_ifma);
	printf("timer %u state %s refcount %u scq.len %u\n",
	    inm->inm_timer,
	    inm_state_str(inm->inm_state),
	    inm->inm_refcount,
	    inm->inm_scq.mq_len);
	printf("igi %p nsrc %lu sctimer %u scrv %u\n",
	    inm->inm_igi,
	    inm->inm_nsrc,
	    inm->inm_sctimer,
	    inm->inm_scrv);
	for (t = 0; t < 2; t++) {
		printf("t%d: fmode %s asm %u ex %u in %u rec %u\n", t,
		    inm_mode_str(inm->inm_st[t].iss_fmode),
		    inm->inm_st[t].iss_asm,
		    inm->inm_st[t].iss_ex,
		    inm->inm_st[t].iss_in,
		    inm->inm_st[t].iss_rec);
	}
	printf("%s: --- end inm %p ---\n", __func__, inm);
}

#else /* !KTR || !(KTR_COMPILE & KTR_IGMPV3) */

void
inm_print(const struct in_multi *inm)
{

}

#endif /* KTR && (KTR_COMPILE & KTR_IGMPV3) */

RB_GENERATE(ip_msource_tree, ip_msource, ims_link, ip_msource_cmp);
