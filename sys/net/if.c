/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1986, 1993
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
 *	@(#)if.c	8.5 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#include "opt_inet6.h"
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/bus.h>
#include <sys/epoch.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/refcount.h>
#include <sys/module.h>
#include <sys/rwlock.h>
#include <sys/sockio.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/taskqueue.h>
#include <sys/domain.h>
#include <sys/jail.h>
#include <sys/priv.h>

#include <machine/stdarg.h>
#include <vm/uma.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_clone.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>
#include <net/radix.h>
#include <net/route.h>
#include <net/vnet.h>

#if defined(INET) || defined(INET6)
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_carp.h>
#ifdef INET
#include <netinet/if_ether.h>
#include <netinet/netdump/netdump.h>
#endif /* INET */
#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#endif /* INET6 */
#endif /* INET || INET6 */

#include <security/mac/mac_framework.h>

/*
 * Consumers of struct ifreq such as tcpdump assume no pad between ifr_name
 * and ifr_ifru when it is used in SIOCGIFCONF.
 */
_Static_assert(sizeof(((struct ifreq *)0)->ifr_name) ==
    offsetof(struct ifreq, ifr_ifru), "gap between ifr_name and ifr_ifru");

__read_mostly epoch_t net_epoch_preempt;
__read_mostly epoch_t net_epoch;
#ifdef COMPAT_FREEBSD32
#include <sys/mount.h>
#include <compat/freebsd32/freebsd32.h>

struct ifreq_buffer32 {
	uint32_t	length;		/* (size_t) */
	uint32_t	buffer;		/* (void *) */
};

/*
 * Interface request structure used for socket
 * ioctl's.  All interface ioctl's must have parameter
 * definitions which begin with ifr_name.  The
 * remainder may be interface specific.
 */
struct ifreq32 {
	char	ifr_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	union {
		struct sockaddr	ifru_addr;
		struct sockaddr	ifru_dstaddr;
		struct sockaddr	ifru_broadaddr;
		struct ifreq_buffer32 ifru_buffer;
		short		ifru_flags[2];
		short		ifru_index;
		int		ifru_jid;
		int		ifru_metric;
		int		ifru_mtu;
		int		ifru_phys;
		int		ifru_media;
		uint32_t	ifru_data;
		int		ifru_cap[2];
		u_int		ifru_fib;
		u_char		ifru_vlan_pcp;
	} ifr_ifru;
};
CTASSERT(sizeof(struct ifreq) == sizeof(struct ifreq32));
CTASSERT(__offsetof(struct ifreq, ifr_ifru) ==
    __offsetof(struct ifreq32, ifr_ifru));

struct ifgroupreq32 {
	char	ifgr_name[IFNAMSIZ];
	u_int	ifgr_len;
	union {
		char		ifgru_group[IFNAMSIZ];
		uint32_t	ifgru_groups;
	} ifgr_ifgru;
};

struct ifmediareq32 {
	char		ifm_name[IFNAMSIZ];
	int		ifm_current;
	int		ifm_mask;
	int		ifm_status;
	int		ifm_active;
	int		ifm_count;
	uint32_t	ifm_ulist;	/* (int *) */
};
#define	SIOCGIFMEDIA32	_IOC_NEWTYPE(SIOCGIFMEDIA, struct ifmediareq32)
#define	SIOCGIFXMEDIA32	_IOC_NEWTYPE(SIOCGIFXMEDIA, struct ifmediareq32)

#define	_CASE_IOC_IFGROUPREQ_32(cmd)				\
    _IOC_NEWTYPE((cmd), struct ifgroupreq32): case
#else /* !COMPAT_FREEBSD32 */
#define _CASE_IOC_IFGROUPREQ_32(cmd)
#endif /* !COMPAT_FREEBSD32 */

#define CASE_IOC_IFGROUPREQ(cmd)	\
    _CASE_IOC_IFGROUPREQ_32(cmd)	\
    (cmd)

union ifreq_union {
	struct ifreq	ifr;
#ifdef COMPAT_FREEBSD32
	struct ifreq32	ifr32;
#endif
};

union ifgroupreq_union {
	struct ifgroupreq ifgr;
#ifdef COMPAT_FREEBSD32
	struct ifgroupreq32 ifgr32;
#endif
};

SYSCTL_NODE(_net, PF_LINK, link, CTLFLAG_RW, 0, "Link layers");
SYSCTL_NODE(_net_link, 0, generic, CTLFLAG_RW, 0, "Generic link-management");

SYSCTL_INT(_net_link, OID_AUTO, ifqmaxlen, CTLFLAG_RDTUN,
    &ifqmaxlen, 0, "max send queue size");

/* Log link state change events */
static int log_link_state_change = 1;

SYSCTL_INT(_net_link, OID_AUTO, log_link_state_change, CTLFLAG_RW,
	&log_link_state_change, 0,
	"log interface link state change events");

/* Log promiscuous mode change events */
static int log_promisc_mode_change = 1;

SYSCTL_INT(_net_link, OID_AUTO, log_promisc_mode_change, CTLFLAG_RDTUN,
	&log_promisc_mode_change, 1,
	"log promiscuous mode change events");

/* Interface description */
static unsigned int ifdescr_maxlen = 1024;
SYSCTL_UINT(_net, OID_AUTO, ifdescr_maxlen, CTLFLAG_RW,
	&ifdescr_maxlen, 0,
	"administrative maximum length for interface description");

static MALLOC_DEFINE(M_IFDESCR, "ifdescr", "ifnet descriptions");

/* global sx for non-critical path ifdescr */
static struct sx ifdescr_sx;
SX_SYSINIT(ifdescr_sx, &ifdescr_sx, "ifnet descr");

void	(*ng_ether_link_state_p)(struct ifnet *ifp, int state);
void	(*lagg_linkstate_p)(struct ifnet *ifp, int state);
/* These are external hooks for CARP. */
void	(*carp_linkstate_p)(struct ifnet *ifp);
void	(*carp_demote_adj_p)(int, char *);
int	(*carp_master_p)(struct ifaddr *);
#if defined(INET) || defined(INET6)
int	(*carp_forus_p)(struct ifnet *ifp, u_char *dhost);
int	(*carp_output_p)(struct ifnet *ifp, struct mbuf *m,
    const struct sockaddr *sa);
int	(*carp_ioctl_p)(struct ifreq *, u_long, struct thread *);   
int	(*carp_attach_p)(struct ifaddr *, int);
void	(*carp_detach_p)(struct ifaddr *, bool);
#endif
#ifdef INET
int	(*carp_iamatch_p)(struct ifaddr *, uint8_t **);
#endif
#ifdef INET6
struct ifaddr *(*carp_iamatch6_p)(struct ifnet *ifp, struct in6_addr *taddr6);
caddr_t	(*carp_macmatch6_p)(struct ifnet *ifp, struct mbuf *m,
    const struct in6_addr *taddr);
#endif

struct mbuf *(*tbr_dequeue_ptr)(struct ifaltq *, int) = NULL;

/*
 * XXX: Style; these should be sorted alphabetically, and unprototyped
 * static functions should be prototyped. Currently they are sorted by
 * declaration order.
 */
static void	if_attachdomain(void *);
static void	if_attachdomain1(struct ifnet *);
static int	ifconf(u_long, caddr_t);
static void	*if_grow(void);
static void	if_input_default(struct ifnet *, struct mbuf *);
static int	if_requestencap_default(struct ifnet *, struct if_encap_req *);
static void	if_route(struct ifnet *, int flag, int fam);
static int	if_setflag(struct ifnet *, int, int, int *, int);
static int	if_transmit(struct ifnet *ifp, struct mbuf *m);
static void	if_unroute(struct ifnet *, int flag, int fam);
static void	link_rtrequest(int, struct rtentry *, struct rt_addrinfo *);
static int	if_delmulti_locked(struct ifnet *, struct ifmultiaddr *, int);
static void	do_link_state_change(void *, int);
static int	if_getgroup(struct ifgroupreq *, struct ifnet *);
static int	if_getgroupmembers(struct ifgroupreq *);
static void	if_delgroups(struct ifnet *);
static void	if_attach_internal(struct ifnet *, int, struct if_clone *);
static int	if_detach_internal(struct ifnet *, int, struct if_clone **);
#ifdef VIMAGE
static void	if_vmove(struct ifnet *, struct vnet *);
#endif

#ifdef INET6
/*
 * XXX: declare here to avoid to include many inet6 related files..
 * should be more generalized?
 */
extern void	nd6_setmtu(struct ifnet *);
#endif

/* ipsec helper hooks */
VNET_DEFINE(struct hhook_head *, ipsec_hhh_in[HHOOK_IPSEC_COUNT]);
VNET_DEFINE(struct hhook_head *, ipsec_hhh_out[HHOOK_IPSEC_COUNT]);

VNET_DEFINE(int, if_index);
int	ifqmaxlen = IFQ_MAXLEN;
VNET_DEFINE(struct ifnethead, ifnet);	/* depend on static init XXX */
VNET_DEFINE(struct ifgrouphead, ifg_head);

VNET_DEFINE_STATIC(int, if_indexlim) = 8;

/* Table of ifnet by index. */
VNET_DEFINE(struct ifnet **, ifindex_table);

#define	V_if_indexlim		VNET(if_indexlim)
#define	V_ifindex_table		VNET(ifindex_table)

/*
 * The global network interface list (V_ifnet) and related state (such as
 * if_index, if_indexlim, and ifindex_table) are protected by an sxlock and
 * an rwlock.  Either may be acquired shared to stablize the list, but both
 * must be acquired writable to modify the list.  This model allows us to
 * both stablize the interface list during interrupt thread processing, but
 * also to stablize it over long-running ioctls, without introducing priority
 * inversions and deadlocks.
 */
struct rwlock ifnet_rwlock;
RW_SYSINIT_FLAGS(ifnet_rw, &ifnet_rwlock, "ifnet_rw", RW_RECURSE);
struct sx ifnet_sxlock;
SX_SYSINIT_FLAGS(ifnet_sx, &ifnet_sxlock, "ifnet_sx", SX_RECURSE);

/*
 * The allocation of network interfaces is a rather non-atomic affair; we
 * need to select an index before we are ready to expose the interface for
 * use, so will use this pointer value to indicate reservation.
 */
#define	IFNET_HOLD	(void *)(uintptr_t)(-1)

static	if_com_alloc_t *if_com_alloc[256];
static	if_com_free_t *if_com_free[256];

static MALLOC_DEFINE(M_IFNET, "ifnet", "interface internals");
MALLOC_DEFINE(M_IFADDR, "ifaddr", "interface address");
MALLOC_DEFINE(M_IFMADDR, "ether_multi", "link-level multicast address");

struct ifnet *
ifnet_byindex_locked(u_short idx)
{

	if (idx > V_if_index)
		return (NULL);
	if (V_ifindex_table[idx] == IFNET_HOLD)
		return (NULL);
	return (V_ifindex_table[idx]);
}

struct ifnet *
ifnet_byindex(u_short idx)
{
	struct ifnet *ifp;

	ifp = ifnet_byindex_locked(idx);
	return (ifp);
}

struct ifnet *
ifnet_byindex_ref(u_short idx)
{
	struct epoch_tracker et;
	struct ifnet *ifp;

	NET_EPOCH_ENTER(et);
	ifp = ifnet_byindex_locked(idx);
	if (ifp == NULL || (ifp->if_flags & IFF_DYING)) {
		NET_EPOCH_EXIT(et);
		return (NULL);
	}
	if_ref(ifp);
	NET_EPOCH_EXIT(et);
	return (ifp);
}

/*
 * Allocate an ifindex array entry; return 0 on success or an error on
 * failure.
 */
static u_short
ifindex_alloc(void **old)
{
	u_short idx;

	IFNET_WLOCK_ASSERT();
	/*
	 * Try to find an empty slot below V_if_index.  If we fail, take the
	 * next slot.
	 */
	for (idx = 1; idx <= V_if_index; idx++) {
		if (V_ifindex_table[idx] == NULL)
			break;
	}

	/* Catch if_index overflow. */
	if (idx >= V_if_indexlim) {
		*old = if_grow();
		return (USHRT_MAX);
	}
	if (idx > V_if_index)
		V_if_index = idx;
	return (idx);
}

static void
ifindex_free_locked(u_short idx)
{

	IFNET_WLOCK_ASSERT();

	V_ifindex_table[idx] = NULL;
	while (V_if_index > 0 &&
	    V_ifindex_table[V_if_index] == NULL)
		V_if_index--;
}

static void
ifindex_free(u_short idx)
{

	IFNET_WLOCK();
	ifindex_free_locked(idx);
	IFNET_WUNLOCK();
}

static void
ifnet_setbyindex(u_short idx, struct ifnet *ifp)
{

	V_ifindex_table[idx] = ifp;
}

struct ifaddr *
ifaddr_byindex(u_short idx)
{
	struct epoch_tracker et;
	struct ifnet *ifp;
	struct ifaddr *ifa = NULL;

	NET_EPOCH_ENTER(et);
	ifp = ifnet_byindex_locked(idx);
	if (ifp != NULL && (ifa = ifp->if_addr) != NULL)
		ifa_ref(ifa);
	NET_EPOCH_EXIT(et);
	return (ifa);
}

/*
 * Network interface utility routines.
 *
 * Routines with ifa_ifwith* names take sockaddr *'s as
 * parameters.
 */

static void
vnet_if_init(const void *unused __unused)
{
	void *old;

	CK_STAILQ_INIT(&V_ifnet);
	CK_STAILQ_INIT(&V_ifg_head);
	IFNET_WLOCK();
	old = if_grow();				/* create initial table */
	IFNET_WUNLOCK();
	epoch_wait_preempt(net_epoch_preempt);
	free(old, M_IFNET);
	vnet_if_clone_init();
}
VNET_SYSINIT(vnet_if_init, SI_SUB_INIT_IF, SI_ORDER_SECOND, vnet_if_init,
    NULL);

#ifdef VIMAGE
static void
vnet_if_uninit(const void *unused __unused)
{

	VNET_ASSERT(CK_STAILQ_EMPTY(&V_ifnet), ("%s:%d tailq &V_ifnet=%p "
	    "not empty", __func__, __LINE__, &V_ifnet));
	VNET_ASSERT(CK_STAILQ_EMPTY(&V_ifg_head), ("%s:%d tailq &V_ifg_head=%p "
	    "not empty", __func__, __LINE__, &V_ifg_head));

	free((caddr_t)V_ifindex_table, M_IFNET);
}
VNET_SYSUNINIT(vnet_if_uninit, SI_SUB_INIT_IF, SI_ORDER_FIRST,
    vnet_if_uninit, NULL);

static void
vnet_if_return(const void *unused __unused)
{
	struct ifnet *ifp, *nifp;

	/* Return all inherited interfaces to their parent vnets. */
	CK_STAILQ_FOREACH_SAFE(ifp, &V_ifnet, if_link, nifp) {
		if (ifp->if_home_vnet != ifp->if_vnet)
			if_vmove(ifp, ifp->if_home_vnet);
	}
}
VNET_SYSUNINIT(vnet_if_return, SI_SUB_VNET_DONE, SI_ORDER_ANY,
    vnet_if_return, NULL);
#endif


static void *
if_grow(void)
{
	int oldlim;
	u_int n;
	struct ifnet **e;
	void *old;

	old = NULL;
	IFNET_WLOCK_ASSERT();
	oldlim = V_if_indexlim;
	IFNET_WUNLOCK();
	n = (oldlim << 1) * sizeof(*e);
	e = malloc(n, M_IFNET, M_WAITOK | M_ZERO);
	IFNET_WLOCK();
	if (V_if_indexlim != oldlim) {
		free(e, M_IFNET);
		return (NULL);
	}
	if (V_ifindex_table != NULL) {
		memcpy((caddr_t)e, (caddr_t)V_ifindex_table, n/2);
		old = V_ifindex_table;
	}
	V_if_indexlim <<= 1;
	V_ifindex_table = e;
	return (old);
}

/*
 * Allocate a struct ifnet and an index for an interface.  A layer 2
 * common structure will also be allocated if an allocation routine is
 * registered for the passed type.
 */
struct ifnet *
if_alloc(u_char type)
{
	struct ifnet *ifp;
	u_short idx;
	void *old;

	ifp = malloc(sizeof(struct ifnet), M_IFNET, M_WAITOK|M_ZERO);
 restart:
	IFNET_WLOCK();
	idx = ifindex_alloc(&old);
	if (__predict_false(idx == USHRT_MAX)) {
		IFNET_WUNLOCK();
		epoch_wait_preempt(net_epoch_preempt);
		free(old, M_IFNET);
		goto restart;
	}
	ifnet_setbyindex(idx, IFNET_HOLD);
	IFNET_WUNLOCK();
	ifp->if_index = idx;
	ifp->if_type = type;
	ifp->if_alloctype = type;
#ifdef VIMAGE
	ifp->if_vnet = curvnet;
#endif
	if (if_com_alloc[type] != NULL) {
		ifp->if_l2com = if_com_alloc[type](type, ifp);
		if (ifp->if_l2com == NULL) {
			free(ifp, M_IFNET);
			ifindex_free(idx);
			return (NULL);
		}
	}

	IF_ADDR_LOCK_INIT(ifp);
	TASK_INIT(&ifp->if_linktask, 0, do_link_state_change, ifp);
	ifp->if_afdata_initialized = 0;
	IF_AFDATA_LOCK_INIT(ifp);
	CK_STAILQ_INIT(&ifp->if_addrhead);
	CK_STAILQ_INIT(&ifp->if_multiaddrs);
	CK_STAILQ_INIT(&ifp->if_groups);
#ifdef MAC
	mac_ifnet_init(ifp);
#endif
	ifq_init(&ifp->if_snd, ifp);

	refcount_init(&ifp->if_refcount, 1);	/* Index reference. */
	for (int i = 0; i < IFCOUNTERS; i++)
		ifp->if_counters[i] = counter_u64_alloc(M_WAITOK);
	ifp->if_get_counter = if_get_counter_default;
	ifp->if_pcp = IFNET_PCP_NONE;
	ifnet_setbyindex(ifp->if_index, ifp);
	return (ifp);
}

/*
 * Do the actual work of freeing a struct ifnet, and layer 2 common
 * structure.  This call is made when the last reference to an
 * interface is released.
 */
static void
if_free_internal(struct ifnet *ifp)
{

	KASSERT((ifp->if_flags & IFF_DYING),
	    ("if_free_internal: interface not dying"));

	if (if_com_free[ifp->if_alloctype] != NULL)
		if_com_free[ifp->if_alloctype](ifp->if_l2com,
		    ifp->if_alloctype);

#ifdef MAC
	mac_ifnet_destroy(ifp);
#endif /* MAC */
	IF_AFDATA_DESTROY(ifp);
	IF_ADDR_LOCK_DESTROY(ifp);
	ifq_delete(&ifp->if_snd);

	for (int i = 0; i < IFCOUNTERS; i++)
		counter_u64_free(ifp->if_counters[i]);

	free(ifp->if_description, M_IFDESCR);
	free(ifp->if_hw_addr, M_IFADDR);
	free(ifp, M_IFNET);
}

static void
if_destroy(epoch_context_t ctx)
{
	struct ifnet *ifp;

	ifp = __containerof(ctx, struct ifnet, if_epoch_ctx);
	if_free_internal(ifp);
}

/*
 * Deregister an interface and free the associated storage.
 */
void
if_free(struct ifnet *ifp)
{

	ifp->if_flags |= IFF_DYING;			/* XXX: Locking */

	CURVNET_SET_QUIET(ifp->if_vnet);
	IFNET_WLOCK();
	KASSERT(ifp == ifnet_byindex_locked(ifp->if_index),
	    ("%s: freeing unallocated ifnet", ifp->if_xname));

	ifindex_free_locked(ifp->if_index);
	IFNET_WUNLOCK();

	if (refcount_release(&ifp->if_refcount))
		epoch_call(net_epoch_preempt, &ifp->if_epoch_ctx, if_destroy);
	CURVNET_RESTORE();
}

/*
 * Interfaces to keep an ifnet type-stable despite the possibility of the
 * driver calling if_free().  If there are additional references, we defer
 * freeing the underlying data structure.
 */
void
if_ref(struct ifnet *ifp)
{

	/* We don't assert the ifnet list lock here, but arguably should. */
	refcount_acquire(&ifp->if_refcount);
}

void
if_rele(struct ifnet *ifp)
{

	if (!refcount_release(&ifp->if_refcount))
		return;
	epoch_call(net_epoch_preempt, &ifp->if_epoch_ctx, if_destroy);
}

void
ifq_init(struct ifaltq *ifq, struct ifnet *ifp)
{
	
	mtx_init(&ifq->ifq_mtx, ifp->if_xname, "if send queue", MTX_DEF);

	if (ifq->ifq_maxlen == 0) 
		ifq->ifq_maxlen = ifqmaxlen;

	ifq->altq_type = 0;
	ifq->altq_disc = NULL;
	ifq->altq_flags &= ALTQF_CANTCHANGE;
	ifq->altq_tbr  = NULL;
	ifq->altq_ifp  = ifp;
}

void
ifq_delete(struct ifaltq *ifq)
{
	mtx_destroy(&ifq->ifq_mtx);
}

/*
 * Perform generic interface initialization tasks and attach the interface
 * to the list of "active" interfaces.  If vmove flag is set on entry
 * to if_attach_internal(), perform only a limited subset of initialization
 * tasks, given that we are moving from one vnet to another an ifnet which
 * has already been fully initialized.
 *
 * Note that if_detach_internal() removes group membership unconditionally
 * even when vmove flag is set, and if_attach_internal() adds only IFG_ALL.
 * Thus, when if_vmove() is applied to a cloned interface, group membership
 * is lost while a cloned one always joins a group whose name is
 * ifc->ifc_name.  To recover this after if_detach_internal() and
 * if_attach_internal(), the cloner should be specified to
 * if_attach_internal() via ifc.  If it is non-NULL, if_attach_internal()
 * attempts to join a group whose name is ifc->ifc_name.
 *
 * XXX:
 *  - The decision to return void and thus require this function to
 *    succeed is questionable.
 *  - We should probably do more sanity checking.  For instance we don't
 *    do anything to insure if_xname is unique or non-empty.
 */
void
if_attach(struct ifnet *ifp)
{

	if_attach_internal(ifp, 0, NULL);
}

/*
 * Compute the least common TSO limit.
 */
void
if_hw_tsomax_common(if_t ifp, struct ifnet_hw_tsomax *pmax)
{
	/*
	 * 1) If there is no limit currently, take the limit from
	 * the network adapter.
	 *
	 * 2) If the network adapter has a limit below the current
	 * limit, apply it.
	 */
	if (pmax->tsomaxbytes == 0 || (ifp->if_hw_tsomax != 0 &&
	    ifp->if_hw_tsomax < pmax->tsomaxbytes)) {
		pmax->tsomaxbytes = ifp->if_hw_tsomax;
	}
	if (pmax->tsomaxsegcount == 0 || (ifp->if_hw_tsomaxsegcount != 0 &&
	    ifp->if_hw_tsomaxsegcount < pmax->tsomaxsegcount)) {
		pmax->tsomaxsegcount = ifp->if_hw_tsomaxsegcount;
	}
	if (pmax->tsomaxsegsize == 0 || (ifp->if_hw_tsomaxsegsize != 0 &&
	    ifp->if_hw_tsomaxsegsize < pmax->tsomaxsegsize)) {
		pmax->tsomaxsegsize = ifp->if_hw_tsomaxsegsize;
	}
}

/*
 * Update TSO limit of a network adapter.
 *
 * Returns zero if no change. Else non-zero.
 */
int
if_hw_tsomax_update(if_t ifp, struct ifnet_hw_tsomax *pmax)
{
	int retval = 0;
	if (ifp->if_hw_tsomax != pmax->tsomaxbytes) {
		ifp->if_hw_tsomax = pmax->tsomaxbytes;
		retval++;
	}
	if (ifp->if_hw_tsomaxsegsize != pmax->tsomaxsegsize) {
		ifp->if_hw_tsomaxsegsize = pmax->tsomaxsegsize;
		retval++;
	}
	if (ifp->if_hw_tsomaxsegcount != pmax->tsomaxsegcount) {
		ifp->if_hw_tsomaxsegcount = pmax->tsomaxsegcount;
		retval++;
	}
	return (retval);
}

static void
if_attach_internal(struct ifnet *ifp, int vmove, struct if_clone *ifc)
{
	unsigned socksize, ifasize;
	int namelen, masklen;
	struct sockaddr_dl *sdl;
	struct ifaddr *ifa;

	if (ifp->if_index == 0 || ifp != ifnet_byindex(ifp->if_index))
		panic ("%s: BUG: if_attach called without if_alloc'd input()\n",
		    ifp->if_xname);

#ifdef VIMAGE
	ifp->if_vnet = curvnet;
	if (ifp->if_home_vnet == NULL)
		ifp->if_home_vnet = curvnet;
#endif

	if_addgroup(ifp, IFG_ALL);

	/* Restore group membership for cloned interfaces. */
	if (vmove && ifc != NULL)
		if_clone_addgroup(ifp, ifc);

	getmicrotime(&ifp->if_lastchange);
	ifp->if_epoch = time_uptime;

	KASSERT((ifp->if_transmit == NULL && ifp->if_qflush == NULL) ||
	    (ifp->if_transmit != NULL && ifp->if_qflush != NULL),
	    ("transmit and qflush must both either be set or both be NULL"));
	if (ifp->if_transmit == NULL) {
		ifp->if_transmit = if_transmit;
		ifp->if_qflush = if_qflush;
	}
	if (ifp->if_input == NULL)
		ifp->if_input = if_input_default;

	if (ifp->if_requestencap == NULL)
		ifp->if_requestencap = if_requestencap_default;

	if (!vmove) {
#ifdef MAC
		mac_ifnet_create(ifp);
#endif

		/*
		 * Create a Link Level name for this device.
		 */
		namelen = strlen(ifp->if_xname);
		/*
		 * Always save enough space for any possiable name so we
		 * can do a rename in place later.
		 */
		masklen = offsetof(struct sockaddr_dl, sdl_data[0]) + IFNAMSIZ;
		socksize = masklen + ifp->if_addrlen;
		if (socksize < sizeof(*sdl))
			socksize = sizeof(*sdl);
		socksize = roundup2(socksize, sizeof(long));
		ifasize = sizeof(*ifa) + 2 * socksize;
		ifa = ifa_alloc(ifasize, M_WAITOK);
		sdl = (struct sockaddr_dl *)(ifa + 1);
		sdl->sdl_len = socksize;
		sdl->sdl_family = AF_LINK;
		bcopy(ifp->if_xname, sdl->sdl_data, namelen);
		sdl->sdl_nlen = namelen;
		sdl->sdl_index = ifp->if_index;
		sdl->sdl_type = ifp->if_type;
		ifp->if_addr = ifa;
		ifa->ifa_ifp = ifp;
		ifa->ifa_rtrequest = link_rtrequest;
		ifa->ifa_addr = (struct sockaddr *)sdl;
		sdl = (struct sockaddr_dl *)(socksize + (caddr_t)sdl);
		ifa->ifa_netmask = (struct sockaddr *)sdl;
		sdl->sdl_len = masklen;
		while (namelen != 0)
			sdl->sdl_data[--namelen] = 0xff;
		CK_STAILQ_INSERT_HEAD(&ifp->if_addrhead, ifa, ifa_link);
		/* Reliably crash if used uninitialized. */
		ifp->if_broadcastaddr = NULL;

		if (ifp->if_type == IFT_ETHER) {
			ifp->if_hw_addr = malloc(ifp->if_addrlen, M_IFADDR,
			    M_WAITOK | M_ZERO);
		}

#if defined(INET) || defined(INET6)
		/* Use defaults for TSO, if nothing is set */
		if (ifp->if_hw_tsomax == 0 &&
		    ifp->if_hw_tsomaxsegcount == 0 &&
		    ifp->if_hw_tsomaxsegsize == 0) {
			/*
			 * The TSO defaults needs to be such that an
			 * NFS mbuf list of 35 mbufs totalling just
			 * below 64K works and that a chain of mbufs
			 * can be defragged into at most 32 segments:
			 */
			ifp->if_hw_tsomax = min(IP_MAXPACKET, (32 * MCLBYTES) -
			    (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN));
			ifp->if_hw_tsomaxsegcount = 35;
			ifp->if_hw_tsomaxsegsize = 2048;	/* 2K */

			/* XXX some drivers set IFCAP_TSO after ethernet attach */
			if (ifp->if_capabilities & IFCAP_TSO) {
				if_printf(ifp, "Using defaults for TSO: %u/%u/%u\n",
				    ifp->if_hw_tsomax,
				    ifp->if_hw_tsomaxsegcount,
				    ifp->if_hw_tsomaxsegsize);
			}
		}
#endif
	}
#ifdef VIMAGE
	else {
		/*
		 * Update the interface index in the link layer address
		 * of the interface.
		 */
		for (ifa = ifp->if_addr; ifa != NULL;
		    ifa = CK_STAILQ_NEXT(ifa, ifa_link)) {
			if (ifa->ifa_addr->sa_family == AF_LINK) {
				sdl = (struct sockaddr_dl *)ifa->ifa_addr;
				sdl->sdl_index = ifp->if_index;
			}
		}
	}
#endif

	IFNET_WLOCK();
	CK_STAILQ_INSERT_TAIL(&V_ifnet, ifp, if_link);
#ifdef VIMAGE
	curvnet->vnet_ifcnt++;
#endif
	IFNET_WUNLOCK();

	if (domain_init_status >= 2)
		if_attachdomain1(ifp);

	EVENTHANDLER_INVOKE(ifnet_arrival_event, ifp);
	if (IS_DEFAULT_VNET(curvnet))
		devctl_notify("IFNET", ifp->if_xname, "ATTACH", NULL);

	/* Announce the interface. */
	rt_ifannouncemsg(ifp, IFAN_ARRIVAL);
}

static void
if_epochalloc(void *dummy __unused)
{

	net_epoch_preempt = epoch_alloc(EPOCH_PREEMPT);
	net_epoch = epoch_alloc(0);
}
SYSINIT(ifepochalloc, SI_SUB_TASKQ + 1, SI_ORDER_ANY,
    if_epochalloc, NULL);

static void
if_attachdomain(void *dummy)
{
	struct ifnet *ifp;

	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link)
		if_attachdomain1(ifp);
}
SYSINIT(domainifattach, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_SECOND,
    if_attachdomain, NULL);

static void
if_attachdomain1(struct ifnet *ifp)
{
	struct domain *dp;

	/*
	 * Since dp->dom_ifattach calls malloc() with M_WAITOK, we
	 * cannot lock ifp->if_afdata initialization, entirely.
	 */
	IF_AFDATA_LOCK(ifp);
	if (ifp->if_afdata_initialized >= domain_init_status) {
		IF_AFDATA_UNLOCK(ifp);
		log(LOG_WARNING, "%s called more than once on %s\n",
		    __func__, ifp->if_xname);
		return;
	}
	ifp->if_afdata_initialized = domain_init_status;
	IF_AFDATA_UNLOCK(ifp);

	/* address family dependent data region */
	bzero(ifp->if_afdata, sizeof(ifp->if_afdata));
	for (dp = domains; dp; dp = dp->dom_next) {
		if (dp->dom_ifattach)
			ifp->if_afdata[dp->dom_family] =
			    (*dp->dom_ifattach)(ifp);
	}
}

/*
 * Remove any unicast or broadcast network addresses from an interface.
 */
void
if_purgeaddrs(struct ifnet *ifp)
{
	struct ifaddr *ifa;

	while (1) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != AF_LINK)
				break;
		}
		NET_EPOCH_EXIT(et);

		if (ifa == NULL)
			break;
#ifdef INET
		/* XXX: Ugly!! ad hoc just for INET */
		if (ifa->ifa_addr->sa_family == AF_INET) {
			struct ifaliasreq ifr;

			bzero(&ifr, sizeof(ifr));
			ifr.ifra_addr = *ifa->ifa_addr;
			if (ifa->ifa_dstaddr)
				ifr.ifra_broadaddr = *ifa->ifa_dstaddr;
			if (in_control(NULL, SIOCDIFADDR, (caddr_t)&ifr, ifp,
			    NULL) == 0)
				continue;
		}
#endif /* INET */
#ifdef INET6
		if (ifa->ifa_addr->sa_family == AF_INET6) {
			in6_purgeaddr(ifa);
			/* ifp_addrhead is already updated */
			continue;
		}
#endif /* INET6 */
		IF_ADDR_WLOCK(ifp);
		CK_STAILQ_REMOVE(&ifp->if_addrhead, ifa, ifaddr, ifa_link);
		IF_ADDR_WUNLOCK(ifp);
		ifa_free(ifa);
	}
}

/*
 * Remove any multicast network addresses from an interface when an ifnet
 * is going away.
 */
static void
if_purgemaddrs(struct ifnet *ifp)
{
	struct ifmultiaddr *ifma;

	IF_ADDR_WLOCK(ifp);
	while (!CK_STAILQ_EMPTY(&ifp->if_multiaddrs)) {
		ifma = CK_STAILQ_FIRST(&ifp->if_multiaddrs);
		CK_STAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifmultiaddr, ifma_link);
		if_delmulti_locked(ifp, ifma, 1);
	}
	IF_ADDR_WUNLOCK(ifp);
}

/*
 * Detach an interface, removing it from the list of "active" interfaces.
 * If vmove flag is set on entry to if_detach_internal(), perform only a
 * limited subset of cleanup tasks, given that we are moving an ifnet from
 * one vnet to another, where it must be fully operational.
 *
 * XXXRW: There are some significant questions about event ordering, and
 * how to prevent things from starting to use the interface during detach.
 */
void
if_detach(struct ifnet *ifp)
{

	CURVNET_SET_QUIET(ifp->if_vnet);
	if_detach_internal(ifp, 0, NULL);
	CURVNET_RESTORE();
}

/*
 * The vmove flag, if set, indicates that we are called from a callpath
 * that is moving an interface to a different vnet instance.
 *
 * The shutdown flag, if set, indicates that we are called in the
 * process of shutting down a vnet instance.  Currently only the
 * vnet_if_return SYSUNINIT function sets it.  Note: we can be called
 * on a vnet instance shutdown without this flag being set, e.g., when
 * the cloned interfaces are destoyed as first thing of teardown.
 */
static int
if_detach_internal(struct ifnet *ifp, int vmove, struct if_clone **ifcp)
{
	struct ifaddr *ifa;
	int i;
	struct domain *dp;
 	struct ifnet *iter;
 	int found = 0;
#ifdef VIMAGE
	int shutdown;

	shutdown = (ifp->if_vnet->vnet_state > SI_SUB_VNET &&
		 ifp->if_vnet->vnet_state < SI_SUB_VNET_DONE) ? 1 : 0;
#endif
	IFNET_WLOCK();
	CK_STAILQ_FOREACH(iter, &V_ifnet, if_link)
		if (iter == ifp) {
			CK_STAILQ_REMOVE(&V_ifnet, ifp, ifnet, if_link);
			if (!vmove)
				ifp->if_flags |= IFF_DYING;
			found = 1;
			break;
		}
	IFNET_WUNLOCK();
	if (!found) {
		/*
		 * While we would want to panic here, we cannot
		 * guarantee that the interface is indeed still on
		 * the list given we don't hold locks all the way.
		 */
		return (ENOENT);
#if 0
		if (vmove)
			panic("%s: ifp=%p not on the ifnet tailq %p",
			    __func__, ifp, &V_ifnet);
		else
			return; /* XXX this should panic as well? */
#endif
	}

	/*
	 * At this point we know the interface still was on the ifnet list
	 * and we removed it so we are in a stable state.
	 */
#ifdef VIMAGE
	curvnet->vnet_ifcnt--;
#endif
	epoch_wait_preempt(net_epoch_preempt);
	/*
	 * In any case (destroy or vmove) detach us from the groups
	 * and remove/wait for pending events on the taskq.
	 * XXX-BZ in theory an interface could still enqueue a taskq change?
	 */
	if_delgroups(ifp);

	taskqueue_drain(taskqueue_swi, &ifp->if_linktask);

	/*
	 * Check if this is a cloned interface or not. Must do even if
	 * shutting down as a if_vmove_reclaim() would move the ifp and
	 * the if_clone_addgroup() will have a corrupted string overwise
	 * from a gibberish pointer.
	 */
	if (vmove && ifcp != NULL)
		*ifcp = if_clone_findifc(ifp);

	if_down(ifp);

#ifdef VIMAGE
	/*
	 * On VNET shutdown abort here as the stack teardown will do all
	 * the work top-down for us.
	 */
	if (shutdown) {
		/* Give interface users the chance to clean up. */
		EVENTHANDLER_INVOKE(ifnet_departure_event, ifp);

		/*
		 * In case of a vmove we are done here without error.
		 * If we would signal an error it would lead to the same
		 * abort as if we did not find the ifnet anymore.
		 * if_detach() calls us in void context and does not care
		 * about an early abort notification, so life is splendid :)
		 */
		goto finish_vnet_shutdown;
	}
#endif

	/*
	 * At this point we are not tearing down a VNET and are either
	 * going to destroy or vmove the interface and have to cleanup
	 * accordingly.
	 */

	/*
	 * Remove routes and flush queues.
	 */
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		altq_disable(&ifp->if_snd);
	if (ALTQ_IS_ATTACHED(&ifp->if_snd))
		altq_detach(&ifp->if_snd);
#endif

	if_purgeaddrs(ifp);

#ifdef INET
	in_ifdetach(ifp);
#endif

#ifdef INET6
	/*
	 * Remove all IPv6 kernel structs related to ifp.  This should be done
	 * before removing routing entries below, since IPv6 interface direct
	 * routes are expected to be removed by the IPv6-specific kernel API.
	 * Otherwise, the kernel will detect some inconsistency and bark it.
	 */
	in6_ifdetach(ifp);
#endif
	if_purgemaddrs(ifp);

	/* Announce that the interface is gone. */
	rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
	EVENTHANDLER_INVOKE(ifnet_departure_event, ifp);
	if (IS_DEFAULT_VNET(curvnet))
		devctl_notify("IFNET", ifp->if_xname, "DETACH", NULL);

	if (!vmove) {
		/*
		 * Prevent further calls into the device driver via ifnet.
		 */
		if_dead(ifp);

		/*
		 * Clean up all addresses.
		 */
		IF_ADDR_WLOCK(ifp);
		if (!CK_STAILQ_EMPTY(&ifp->if_addrhead)) {
			ifa = CK_STAILQ_FIRST(&ifp->if_addrhead);
			CK_STAILQ_REMOVE(&ifp->if_addrhead, ifa, ifaddr, ifa_link);
			IF_ADDR_WUNLOCK(ifp);
			ifa_free(ifa);
		} else
			IF_ADDR_WUNLOCK(ifp);
	}

	rt_flushifroutes(ifp);

#ifdef VIMAGE
finish_vnet_shutdown:
#endif
	/*
	 * We cannot hold the lock over dom_ifdetach calls as they might
	 * sleep, for example trying to drain a callout, thus open up the
	 * theoretical race with re-attaching.
	 */
	IF_AFDATA_LOCK(ifp);
	i = ifp->if_afdata_initialized;
	ifp->if_afdata_initialized = 0;
	IF_AFDATA_UNLOCK(ifp);
	for (dp = domains; i > 0 && dp; dp = dp->dom_next) {
		if (dp->dom_ifdetach && ifp->if_afdata[dp->dom_family]) {
			(*dp->dom_ifdetach)(ifp,
			    ifp->if_afdata[dp->dom_family]);
			ifp->if_afdata[dp->dom_family] = NULL;
		}
	}

	return (0);
}

#ifdef VIMAGE
/*
 * if_vmove() performs a limited version of if_detach() in current
 * vnet and if_attach()es the ifnet to the vnet specified as 2nd arg.
 * An attempt is made to shrink if_index in current vnet, find an
 * unused if_index in target vnet and calls if_grow() if necessary,
 * and finally find an unused if_xname for the target vnet.
 */
static void
if_vmove(struct ifnet *ifp, struct vnet *new_vnet)
{
	struct if_clone *ifc;
	u_int bif_dlt, bif_hdrlen;
	void *old;
	int rc;

 	/*
	 * if_detach_internal() will call the eventhandler to notify
	 * interface departure.  That will detach if_bpf.  We need to
	 * safe the dlt and hdrlen so we can re-attach it later.
	 */
	bpf_get_bp_params(ifp->if_bpf, &bif_dlt, &bif_hdrlen);

	/*
	 * Detach from current vnet, but preserve LLADDR info, do not
	 * mark as dead etc. so that the ifnet can be reattached later.
	 * If we cannot find it, we lost the race to someone else.
	 */
	rc = if_detach_internal(ifp, 1, &ifc);
	if (rc != 0)
		return;

	/*
	 * Unlink the ifnet from ifindex_table[] in current vnet, and shrink
	 * the if_index for that vnet if possible.
	 *
	 * NOTE: IFNET_WLOCK/IFNET_WUNLOCK() are assumed to be unvirtualized,
	 * or we'd lock on one vnet and unlock on another.
	 */
	IFNET_WLOCK();
	ifindex_free_locked(ifp->if_index);
	IFNET_WUNLOCK();

	/*
	 * Perform interface-specific reassignment tasks, if provided by
	 * the driver.
	 */
	if (ifp->if_reassign != NULL)
		ifp->if_reassign(ifp, new_vnet, NULL);

	/*
	 * Switch to the context of the target vnet.
	 */
	CURVNET_SET_QUIET(new_vnet);
 restart:
	IFNET_WLOCK();
	ifp->if_index = ifindex_alloc(&old);
	if (__predict_false(ifp->if_index == USHRT_MAX)) {
		IFNET_WUNLOCK();
		epoch_wait_preempt(net_epoch_preempt);
		free(old, M_IFNET);
		goto restart;
	}
	ifnet_setbyindex(ifp->if_index, ifp);
	IFNET_WUNLOCK();

	if_attach_internal(ifp, 1, ifc);

	if (ifp->if_bpf == NULL)
		bpfattach(ifp, bif_dlt, bif_hdrlen);

	CURVNET_RESTORE();
}

/*
 * Move an ifnet to or from another child prison/vnet, specified by the jail id.
 */
static int
if_vmove_loan(struct thread *td, struct ifnet *ifp, char *ifname, int jid)
{
	struct prison *pr;
	struct ifnet *difp;
	int shutdown;

	/* Try to find the prison within our visibility. */
	sx_slock(&allprison_lock);
	pr = prison_find_child(td->td_ucred->cr_prison, jid);
	sx_sunlock(&allprison_lock);
	if (pr == NULL)
		return (ENXIO);
	prison_hold_locked(pr);
	mtx_unlock(&pr->pr_mtx);

	/* Do not try to move the iface from and to the same prison. */
	if (pr->pr_vnet == ifp->if_vnet) {
		prison_free(pr);
		return (EEXIST);
	}

	/* Make sure the named iface does not exists in the dst. prison/vnet. */
	/* XXX Lock interfaces to avoid races. */
	CURVNET_SET_QUIET(pr->pr_vnet);
	difp = ifunit(ifname);
	if (difp != NULL) {
		CURVNET_RESTORE();
		prison_free(pr);
		return (EEXIST);
	}

	/* Make sure the VNET is stable. */
	shutdown = (ifp->if_vnet->vnet_state > SI_SUB_VNET &&
		 ifp->if_vnet->vnet_state < SI_SUB_VNET_DONE) ? 1 : 0;
	if (shutdown) {
		CURVNET_RESTORE();
		prison_free(pr);
		return (EBUSY);
	}
	CURVNET_RESTORE();

	/* Move the interface into the child jail/vnet. */
	if_vmove(ifp, pr->pr_vnet);

	/* Report the new if_xname back to the userland. */
	sprintf(ifname, "%s", ifp->if_xname);

	prison_free(pr);
	return (0);
}

static int
if_vmove_reclaim(struct thread *td, char *ifname, int jid)
{
	struct prison *pr;
	struct vnet *vnet_dst;
	struct ifnet *ifp;
 	int shutdown;

	/* Try to find the prison within our visibility. */
	sx_slock(&allprison_lock);
	pr = prison_find_child(td->td_ucred->cr_prison, jid);
	sx_sunlock(&allprison_lock);
	if (pr == NULL)
		return (ENXIO);
	prison_hold_locked(pr);
	mtx_unlock(&pr->pr_mtx);

	/* Make sure the named iface exists in the source prison/vnet. */
	CURVNET_SET(pr->pr_vnet);
	ifp = ifunit(ifname);		/* XXX Lock to avoid races. */
	if (ifp == NULL) {
		CURVNET_RESTORE();
		prison_free(pr);
		return (ENXIO);
	}

	/* Do not try to move the iface from and to the same prison. */
	vnet_dst = TD_TO_VNET(td);
	if (vnet_dst == ifp->if_vnet) {
		CURVNET_RESTORE();
		prison_free(pr);
		return (EEXIST);
	}

	/* Make sure the VNET is stable. */
	shutdown = (ifp->if_vnet->vnet_state > SI_SUB_VNET &&
		 ifp->if_vnet->vnet_state < SI_SUB_VNET_DONE) ? 1 : 0;
	if (shutdown) {
		CURVNET_RESTORE();
		prison_free(pr);
		return (EBUSY);
	}

	/* Get interface back from child jail/vnet. */
	if_vmove(ifp, vnet_dst);
	CURVNET_RESTORE();

	/* Report the new if_xname back to the userland. */
	sprintf(ifname, "%s", ifp->if_xname);

	prison_free(pr);
	return (0);
}
#endif /* VIMAGE */

/*
 * Add a group to an interface
 */
int
if_addgroup(struct ifnet *ifp, const char *groupname)
{
	struct ifg_list		*ifgl;
	struct ifg_group	*ifg = NULL;
	struct ifg_member	*ifgm;
	int 			 new = 0;

	if (groupname[0] && groupname[strlen(groupname) - 1] >= '0' &&
	    groupname[strlen(groupname) - 1] <= '9')
		return (EINVAL);

	IFNET_WLOCK();
	CK_STAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
		if (!strcmp(ifgl->ifgl_group->ifg_group, groupname)) {
			IFNET_WUNLOCK();
			return (EEXIST);
		}

	if ((ifgl = (struct ifg_list *)malloc(sizeof(struct ifg_list), M_TEMP,
	    M_NOWAIT)) == NULL) {
	    	IFNET_WUNLOCK();
		return (ENOMEM);
	}

	if ((ifgm = (struct ifg_member *)malloc(sizeof(struct ifg_member),
	    M_TEMP, M_NOWAIT)) == NULL) {
		free(ifgl, M_TEMP);
		IFNET_WUNLOCK();
		return (ENOMEM);
	}

	CK_STAILQ_FOREACH(ifg, &V_ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, groupname))
			break;

	if (ifg == NULL) {
		if ((ifg = (struct ifg_group *)malloc(sizeof(struct ifg_group),
		    M_TEMP, M_NOWAIT)) == NULL) {
			free(ifgl, M_TEMP);
			free(ifgm, M_TEMP);
			IFNET_WUNLOCK();
			return (ENOMEM);
		}
		strlcpy(ifg->ifg_group, groupname, sizeof(ifg->ifg_group));
		ifg->ifg_refcnt = 0;
		CK_STAILQ_INIT(&ifg->ifg_members);
		CK_STAILQ_INSERT_TAIL(&V_ifg_head, ifg, ifg_next);
		new = 1;
	}

	ifg->ifg_refcnt++;
	ifgl->ifgl_group = ifg;
	ifgm->ifgm_ifp = ifp;

	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_INSERT_TAIL(&ifg->ifg_members, ifgm, ifgm_next);
	CK_STAILQ_INSERT_TAIL(&ifp->if_groups, ifgl, ifgl_next);
	IF_ADDR_WUNLOCK(ifp);

	IFNET_WUNLOCK();

	if (new)
		EVENTHANDLER_INVOKE(group_attach_event, ifg);
	EVENTHANDLER_INVOKE(group_change_event, groupname);

	return (0);
}

/*
 * Remove a group from an interface
 */
int
if_delgroup(struct ifnet *ifp, const char *groupname)
{
	struct ifg_list		*ifgl;
	struct ifg_member	*ifgm;
	int freeifgl;

	IFNET_WLOCK();
	CK_STAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
		if (!strcmp(ifgl->ifgl_group->ifg_group, groupname))
			break;
	if (ifgl == NULL) {
		IFNET_WUNLOCK();
		return (ENOENT);
	}

	freeifgl = 0;
	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_REMOVE(&ifp->if_groups, ifgl, ifg_list, ifgl_next);
	IF_ADDR_WUNLOCK(ifp);

	CK_STAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next)
		if (ifgm->ifgm_ifp == ifp)
			break;

	if (ifgm != NULL)
		CK_STAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm, ifg_member, ifgm_next);

	if (--ifgl->ifgl_group->ifg_refcnt == 0) {
		CK_STAILQ_REMOVE(&V_ifg_head, ifgl->ifgl_group, ifg_group, ifg_next);
		freeifgl = 1;
	}
	IFNET_WUNLOCK();

	epoch_wait_preempt(net_epoch_preempt);
	if (freeifgl) {
		EVENTHANDLER_INVOKE(group_detach_event, ifgl->ifgl_group);
		free(ifgl->ifgl_group, M_TEMP);
	}
	free(ifgm, M_TEMP);
	free(ifgl, M_TEMP);

	EVENTHANDLER_INVOKE(group_change_event, groupname);

	return (0);
}

/*
 * Remove an interface from all groups
 */
static void
if_delgroups(struct ifnet *ifp)
{
	struct ifg_list		*ifgl;
	struct ifg_member	*ifgm;
	char groupname[IFNAMSIZ];
	int ifglfree;

	IFNET_WLOCK();
	while (!CK_STAILQ_EMPTY(&ifp->if_groups)) {
		ifgl = CK_STAILQ_FIRST(&ifp->if_groups);

		strlcpy(groupname, ifgl->ifgl_group->ifg_group, IFNAMSIZ);

		IF_ADDR_WLOCK(ifp);
		CK_STAILQ_REMOVE(&ifp->if_groups, ifgl, ifg_list, ifgl_next);
		IF_ADDR_WUNLOCK(ifp);

		CK_STAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next)
			if (ifgm->ifgm_ifp == ifp)
				break;

		if (ifgm != NULL)
			CK_STAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm, ifg_member,
			    ifgm_next);
		ifglfree = 0;
		if (--ifgl->ifgl_group->ifg_refcnt == 0) {
			CK_STAILQ_REMOVE(&V_ifg_head, ifgl->ifgl_group, ifg_group, ifg_next);
			ifglfree = 1;
		}

		IFNET_WUNLOCK();
		epoch_wait_preempt(net_epoch_preempt);
		free(ifgm, M_TEMP);
		if (ifglfree) {
			EVENTHANDLER_INVOKE(group_detach_event,
								ifgl->ifgl_group);
			free(ifgl->ifgl_group, M_TEMP);
		}
		EVENTHANDLER_INVOKE(group_change_event, groupname);

		IFNET_WLOCK();
	}
	IFNET_WUNLOCK();
}

static char *
ifgr_group_get(void *ifgrp)
{
	union ifgroupreq_union *ifgrup;

	ifgrup = ifgrp;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return (&ifgrup->ifgr32.ifgr_ifgru.ifgru_group[0]);
#endif
	return (&ifgrup->ifgr.ifgr_ifgru.ifgru_group[0]);
}

static struct ifg_req *
ifgr_groups_get(void *ifgrp)
{
	union ifgroupreq_union *ifgrup;

	ifgrup = ifgrp;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return ((struct ifg_req *)(uintptr_t)
		    ifgrup->ifgr32.ifgr_ifgru.ifgru_groups);
#endif
	return (ifgrup->ifgr.ifgr_ifgru.ifgru_groups);
}

/*
 * Stores all groups from an interface in memory pointed to by ifgr.
 */
static int
if_getgroup(struct ifgroupreq *ifgr, struct ifnet *ifp)
{
	struct epoch_tracker	 et;
	int			 len, error;
	struct ifg_list		*ifgl;
	struct ifg_req		 ifgrq, *ifgp;

	if (ifgr->ifgr_len == 0) {
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
			ifgr->ifgr_len += sizeof(struct ifg_req);
		NET_EPOCH_EXIT(et);
		return (0);
	}

	len = ifgr->ifgr_len;
	ifgp = ifgr_groups_get(ifgr);
	/* XXX: wire */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		if (len < sizeof(ifgrq)) {
			NET_EPOCH_EXIT(et);
			return (EINVAL);
		}
		bzero(&ifgrq, sizeof ifgrq);
		strlcpy(ifgrq.ifgrq_group, ifgl->ifgl_group->ifg_group,
		    sizeof(ifgrq.ifgrq_group));
		if ((error = copyout(&ifgrq, ifgp, sizeof(struct ifg_req)))) {
		    	NET_EPOCH_EXIT(et);
			return (error);
		}
		len -= sizeof(ifgrq);
		ifgp++;
	}
	NET_EPOCH_EXIT(et);

	return (0);
}

/*
 * Stores all members of a group in memory pointed to by igfr
 */
static int
if_getgroupmembers(struct ifgroupreq *ifgr)
{
	struct ifg_group	*ifg;
	struct ifg_member	*ifgm;
	struct ifg_req		 ifgrq, *ifgp;
	int			 len, error;

	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifg, &V_ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, ifgr->ifgr_name))
			break;
	if (ifg == NULL) {
		IFNET_RUNLOCK();
		return (ENOENT);
	}

	if (ifgr->ifgr_len == 0) {
		CK_STAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next)
			ifgr->ifgr_len += sizeof(ifgrq);
		IFNET_RUNLOCK();
		return (0);
	}

	len = ifgr->ifgr_len;
	ifgp = ifgr_groups_get(ifgr);
	CK_STAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next) {
		if (len < sizeof(ifgrq)) {
			IFNET_RUNLOCK();
			return (EINVAL);
		}
		bzero(&ifgrq, sizeof ifgrq);
		strlcpy(ifgrq.ifgrq_member, ifgm->ifgm_ifp->if_xname,
		    sizeof(ifgrq.ifgrq_member));
		if ((error = copyout(&ifgrq, ifgp, sizeof(struct ifg_req)))) {
			IFNET_RUNLOCK();
			return (error);
		}
		len -= sizeof(ifgrq);
		ifgp++;
	}
	IFNET_RUNLOCK();

	return (0);
}

/*
 * Return counter values from counter(9)s stored in ifnet.
 */
uint64_t
if_get_counter_default(struct ifnet *ifp, ift_counter cnt)
{

	KASSERT(cnt < IFCOUNTERS, ("%s: invalid cnt %d", __func__, cnt));

	return (counter_u64_fetch(ifp->if_counters[cnt]));
}

/*
 * Increase an ifnet counter. Usually used for counters shared
 * between the stack and a driver, but function supports them all.
 */
void
if_inc_counter(struct ifnet *ifp, ift_counter cnt, int64_t inc)
{

	KASSERT(cnt < IFCOUNTERS, ("%s: invalid cnt %d", __func__, cnt));

	counter_u64_add(ifp->if_counters[cnt], inc);
}

/*
 * Copy data from ifnet to userland API structure if_data.
 */
void
if_data_copy(struct ifnet *ifp, struct if_data *ifd)
{

	ifd->ifi_type = ifp->if_type;
	ifd->ifi_physical = 0;
	ifd->ifi_addrlen = ifp->if_addrlen;
	ifd->ifi_hdrlen = ifp->if_hdrlen;
	ifd->ifi_link_state = ifp->if_link_state;
	ifd->ifi_vhid = 0;
	ifd->ifi_datalen = sizeof(struct if_data);
	ifd->ifi_mtu = ifp->if_mtu;
	ifd->ifi_metric = ifp->if_metric;
	ifd->ifi_baudrate = ifp->if_baudrate;
	ifd->ifi_hwassist = ifp->if_hwassist;
	ifd->ifi_epoch = ifp->if_epoch;
	ifd->ifi_lastchange = ifp->if_lastchange;

	ifd->ifi_ipackets = ifp->if_get_counter(ifp, IFCOUNTER_IPACKETS);
	ifd->ifi_ierrors = ifp->if_get_counter(ifp, IFCOUNTER_IERRORS);
	ifd->ifi_opackets = ifp->if_get_counter(ifp, IFCOUNTER_OPACKETS);
	ifd->ifi_oerrors = ifp->if_get_counter(ifp, IFCOUNTER_OERRORS);
	ifd->ifi_collisions = ifp->if_get_counter(ifp, IFCOUNTER_COLLISIONS);
	ifd->ifi_ibytes = ifp->if_get_counter(ifp, IFCOUNTER_IBYTES);
	ifd->ifi_obytes = ifp->if_get_counter(ifp, IFCOUNTER_OBYTES);
	ifd->ifi_imcasts = ifp->if_get_counter(ifp, IFCOUNTER_IMCASTS);
	ifd->ifi_omcasts = ifp->if_get_counter(ifp, IFCOUNTER_OMCASTS);
	ifd->ifi_iqdrops = ifp->if_get_counter(ifp, IFCOUNTER_IQDROPS);
	ifd->ifi_oqdrops = ifp->if_get_counter(ifp, IFCOUNTER_OQDROPS);
	ifd->ifi_noproto = ifp->if_get_counter(ifp, IFCOUNTER_NOPROTO);
}

/*
 * Wrapper functions for struct ifnet address list locking macros.  These are
 * used by kernel modules to avoid encoding programming interface or binary
 * interface assumptions that may be violated when kernel-internal locking
 * approaches change.
 */
void
if_addr_rlock(struct ifnet *ifp)
{

	epoch_enter_preempt(net_epoch_preempt, curthread->td_et);
}

void
if_addr_runlock(struct ifnet *ifp)
{

	epoch_exit_preempt(net_epoch_preempt, curthread->td_et);
}

void
if_maddr_rlock(if_t ifp)
{

	epoch_enter_preempt(net_epoch_preempt, curthread->td_et);
}

void
if_maddr_runlock(if_t ifp)
{

	epoch_exit_preempt(net_epoch_preempt, curthread->td_et);
}

/*
 * Initialization, destruction and refcounting functions for ifaddrs.
 */
struct ifaddr *
ifa_alloc(size_t size, int flags)
{
	struct ifaddr *ifa;

	KASSERT(size >= sizeof(struct ifaddr),
	    ("%s: invalid size %zu", __func__, size));

	ifa = malloc(size, M_IFADDR, M_ZERO | flags);
	if (ifa == NULL)
		return (NULL);

	if ((ifa->ifa_opackets = counter_u64_alloc(flags)) == NULL)
		goto fail;
	if ((ifa->ifa_ipackets = counter_u64_alloc(flags)) == NULL)
		goto fail;
	if ((ifa->ifa_obytes = counter_u64_alloc(flags)) == NULL)
		goto fail;
	if ((ifa->ifa_ibytes = counter_u64_alloc(flags)) == NULL)
		goto fail;

	refcount_init(&ifa->ifa_refcnt, 1);

	return (ifa);

fail:
	/* free(NULL) is okay */
	counter_u64_free(ifa->ifa_opackets);
	counter_u64_free(ifa->ifa_ipackets);
	counter_u64_free(ifa->ifa_obytes);
	counter_u64_free(ifa->ifa_ibytes);
	free(ifa, M_IFADDR);

	return (NULL);
}

void
ifa_ref(struct ifaddr *ifa)
{

	refcount_acquire(&ifa->ifa_refcnt);
}

static void
ifa_destroy(epoch_context_t ctx)
{
	struct ifaddr *ifa;

	ifa = __containerof(ctx, struct ifaddr, ifa_epoch_ctx);
	counter_u64_free(ifa->ifa_opackets);
	counter_u64_free(ifa->ifa_ipackets);
	counter_u64_free(ifa->ifa_obytes);
	counter_u64_free(ifa->ifa_ibytes);
	free(ifa, M_IFADDR);
}

void
ifa_free(struct ifaddr *ifa)
{

	if (refcount_release(&ifa->ifa_refcnt))
		epoch_call(net_epoch_preempt, &ifa->ifa_epoch_ctx, ifa_destroy);
}


static int
ifa_maintain_loopback_route(int cmd, const char *otype, struct ifaddr *ifa,
    struct sockaddr *ia)
{
	int error;
	struct rt_addrinfo info;
	struct sockaddr_dl null_sdl;
	struct ifnet *ifp;

	ifp = ifa->ifa_ifp;

	bzero(&info, sizeof(info));
	if (cmd != RTM_DELETE)
		info.rti_ifp = V_loif;
	info.rti_flags = ifa->ifa_flags | RTF_HOST | RTF_STATIC | RTF_PINNED;
	info.rti_info[RTAX_DST] = ia;
	info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&null_sdl;
	link_init_sdl(ifp, (struct sockaddr *)&null_sdl, ifp->if_type);

	error = rtrequest1_fib(cmd, &info, NULL, ifp->if_fib);

	if (error != 0 &&
	    !(cmd == RTM_ADD && error == EEXIST) &&
	    !(cmd == RTM_DELETE && error == ENOENT))
		if_printf(ifp, "%s failed: %d\n", otype, error);

	return (error);
}

int
ifa_add_loopback_route(struct ifaddr *ifa, struct sockaddr *ia)
{

	return (ifa_maintain_loopback_route(RTM_ADD, "insertion", ifa, ia));
}

int
ifa_del_loopback_route(struct ifaddr *ifa, struct sockaddr *ia)
{

	return (ifa_maintain_loopback_route(RTM_DELETE, "deletion", ifa, ia));
}

int
ifa_switch_loopback_route(struct ifaddr *ifa, struct sockaddr *ia)
{

	return (ifa_maintain_loopback_route(RTM_CHANGE, "switch", ifa, ia));
}

/*
 * XXX: Because sockaddr_dl has deeper structure than the sockaddr
 * structs used to represent other address families, it is necessary
 * to perform a different comparison.
 */

#define	sa_dl_equal(a1, a2)	\
	((((const struct sockaddr_dl *)(a1))->sdl_len ==		\
	 ((const struct sockaddr_dl *)(a2))->sdl_len) &&		\
	 (bcmp(CLLADDR((const struct sockaddr_dl *)(a1)),		\
	       CLLADDR((const struct sockaddr_dl *)(a2)),		\
	       ((const struct sockaddr_dl *)(a1))->sdl_alen) == 0))

/*
 * Locate an interface based on a complete address.
 */
/*ARGSUSED*/
struct ifaddr *
ifa_ifwithaddr(const struct sockaddr *addr)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	MPASS(in_epoch(net_epoch_preempt));
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if (sa_equal(addr, ifa->ifa_addr)) {
				goto done;
			}
			/* IP6 doesn't have broadcast */
			if ((ifp->if_flags & IFF_BROADCAST) &&
			    ifa->ifa_broadaddr &&
			    ifa->ifa_broadaddr->sa_len != 0 &&
			    sa_equal(ifa->ifa_broadaddr, addr)) {
				goto done;
			}
		}
	}
	ifa = NULL;
done:
	return (ifa);
}

int
ifa_ifwithaddr_check(const struct sockaddr *addr)
{
	struct epoch_tracker et;
	int rc;

	NET_EPOCH_ENTER(et);
	rc = (ifa_ifwithaddr(addr) != NULL);
	NET_EPOCH_EXIT(et);
	return (rc);
}

/*
 * Locate an interface based on the broadcast address.
 */
/* ARGSUSED */
struct ifaddr *
ifa_ifwithbroadaddr(const struct sockaddr *addr, int fibnum)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	MPASS(in_epoch(net_epoch_preempt));
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if ((fibnum != RT_ALL_FIBS) && (ifp->if_fib != fibnum))
			continue;
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if ((ifp->if_flags & IFF_BROADCAST) &&
			    ifa->ifa_broadaddr &&
			    ifa->ifa_broadaddr->sa_len != 0 &&
			    sa_equal(ifa->ifa_broadaddr, addr)) {
				goto done;
			}
		}
	}
	ifa = NULL;
done:
	return (ifa);
}

/*
 * Locate the point to point interface with a given destination address.
 */
/*ARGSUSED*/
struct ifaddr *
ifa_ifwithdstaddr(const struct sockaddr *addr, int fibnum)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	MPASS(in_epoch(net_epoch_preempt));
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0)
			continue;
		if ((fibnum != RT_ALL_FIBS) && (ifp->if_fib != fibnum))
			continue;
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if (ifa->ifa_dstaddr != NULL &&
			    sa_equal(addr, ifa->ifa_dstaddr)) {
				goto done;
			}
		}
	}
	ifa = NULL;
done:
	return (ifa);
}

/*
 * Find an interface on a specific network.  If many, choice
 * is most specific found.
 */
struct ifaddr *
ifa_ifwithnet(const struct sockaddr *addr, int ignore_ptp, int fibnum)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct ifaddr *ifa_maybe = NULL;
	u_int af = addr->sa_family;
	const char *addr_data = addr->sa_data, *cplim;

	MPASS(in_epoch(net_epoch_preempt));
	/*
	 * AF_LINK addresses can be looked up directly by their index number,
	 * so do that if we can.
	 */
	if (af == AF_LINK) {
	    const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)addr;
	    if (sdl->sdl_index && sdl->sdl_index <= V_if_index)
		return (ifaddr_byindex(sdl->sdl_index));
	}

	/*
	 * Scan though each interface, looking for ones that have addresses
	 * in this address family and the requested fib.
	 */
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if ((fibnum != RT_ALL_FIBS) && (ifp->if_fib != fibnum))
			continue;
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			const char *cp, *cp2, *cp3;

			if (ifa->ifa_addr->sa_family != af)
next:				continue;
			if (af == AF_INET && 
			    ifp->if_flags & IFF_POINTOPOINT && !ignore_ptp) {
				/*
				 * This is a bit broken as it doesn't
				 * take into account that the remote end may
				 * be a single node in the network we are
				 * looking for.
				 * The trouble is that we don't know the
				 * netmask for the remote end.
				 */
				if (ifa->ifa_dstaddr != NULL &&
				    sa_equal(addr, ifa->ifa_dstaddr)) {
					goto done;
				}
			} else {
				/*
				 * Scan all the bits in the ifa's address.
				 * If a bit dissagrees with what we are
				 * looking for, mask it with the netmask
				 * to see if it really matters.
				 * (A byte at a time)
				 */
				if (ifa->ifa_netmask == 0)
					continue;
				cp = addr_data;
				cp2 = ifa->ifa_addr->sa_data;
				cp3 = ifa->ifa_netmask->sa_data;
				cplim = ifa->ifa_netmask->sa_len
					+ (char *)ifa->ifa_netmask;
				while (cp3 < cplim)
					if ((*cp++ ^ *cp2++) & *cp3++)
						goto next; /* next address! */
				/*
				 * If the netmask of what we just found
				 * is more specific than what we had before
				 * (if we had one), or if the virtual status
				 * of new prefix is better than of the old one,
				 * then remember the new one before continuing
				 * to search for an even better one.
				 */
				if (ifa_maybe == NULL ||
				    ifa_preferred(ifa_maybe, ifa) ||
				    rn_refines((caddr_t)ifa->ifa_netmask,
				    (caddr_t)ifa_maybe->ifa_netmask)) {
					ifa_maybe = ifa;
				}
			}
		}
	}
	ifa = ifa_maybe;
	ifa_maybe = NULL;
done:
	return (ifa);
}

/*
 * Find an interface address specific to an interface best matching
 * a given address.
 */
struct ifaddr *
ifaof_ifpforaddr(const struct sockaddr *addr, struct ifnet *ifp)
{
	struct ifaddr *ifa;
	const char *cp, *cp2, *cp3;
	char *cplim;
	struct ifaddr *ifa_maybe = NULL;
	u_int af = addr->sa_family;

	if (af >= AF_MAX)
		return (NULL);

	MPASS(in_epoch(net_epoch_preempt));
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != af)
			continue;
		if (ifa_maybe == NULL)
			ifa_maybe = ifa;
		if (ifa->ifa_netmask == 0) {
			if (sa_equal(addr, ifa->ifa_addr) ||
			    (ifa->ifa_dstaddr &&
			    sa_equal(addr, ifa->ifa_dstaddr)))
				goto done;
			continue;
		}
		if (ifp->if_flags & IFF_POINTOPOINT) {
			if (sa_equal(addr, ifa->ifa_dstaddr))
				goto done;
		} else {
			cp = addr->sa_data;
			cp2 = ifa->ifa_addr->sa_data;
			cp3 = ifa->ifa_netmask->sa_data;
			cplim = ifa->ifa_netmask->sa_len + (char *)ifa->ifa_netmask;
			for (; cp3 < cplim; cp3++)
				if ((*cp++ ^ *cp2++) & *cp3)
					break;
			if (cp3 == cplim)
				goto done;
		}
	}
	ifa = ifa_maybe;
done:
	return (ifa);
}

/*
 * See whether new ifa is better than current one:
 * 1) A non-virtual one is preferred over virtual.
 * 2) A virtual in master state preferred over any other state.
 *
 * Used in several address selecting functions.
 */
int
ifa_preferred(struct ifaddr *cur, struct ifaddr *next)
{

	return (cur->ifa_carp && (!next->ifa_carp ||
	    ((*carp_master_p)(next) && !(*carp_master_p)(cur))));
}

#include <net/if_llatbl.h>

/*
 * Default action when installing a route with a Link Level gateway.
 * Lookup an appropriate real ifa to point to.
 * This should be moved to /sys/net/link.c eventually.
 */
static void
link_rtrequest(int cmd, struct rtentry *rt, struct rt_addrinfo *info)
{
	struct epoch_tracker et;
	struct ifaddr *ifa, *oifa;
	struct sockaddr *dst;
	struct ifnet *ifp;

	if (cmd != RTM_ADD || ((ifa = rt->rt_ifa) == NULL) ||
	    ((ifp = ifa->ifa_ifp) == NULL) || ((dst = rt_key(rt)) == NULL))
		return;
	NET_EPOCH_ENTER(et);
	ifa = ifaof_ifpforaddr(dst, ifp);
	if (ifa) {
		oifa = rt->rt_ifa;
		if (oifa != ifa) {
			ifa_free(oifa);
			ifa_ref(ifa);
		}
		rt->rt_ifa = ifa;
		if (ifa->ifa_rtrequest && ifa->ifa_rtrequest != link_rtrequest)
			ifa->ifa_rtrequest(cmd, rt, info);
	}
	NET_EPOCH_EXIT(et);
}

struct sockaddr_dl *
link_alloc_sdl(size_t size, int flags)
{

	return (malloc(size, M_TEMP, flags));
}

void
link_free_sdl(struct sockaddr *sa)
{
	free(sa, M_TEMP);
}

/*
 * Fills in given sdl with interface basic info.
 * Returns pointer to filled sdl.
 */
struct sockaddr_dl *
link_init_sdl(struct ifnet *ifp, struct sockaddr *paddr, u_char iftype)
{
	struct sockaddr_dl *sdl;

	sdl = (struct sockaddr_dl *)paddr;
	memset(sdl, 0, sizeof(struct sockaddr_dl));
	sdl->sdl_len = sizeof(struct sockaddr_dl);
	sdl->sdl_family = AF_LINK;
	sdl->sdl_index = ifp->if_index;
	sdl->sdl_type = iftype;

	return (sdl);
}

/*
 * Mark an interface down and notify protocols of
 * the transition.
 */
static void
if_unroute(struct ifnet *ifp, int flag, int fam)
{
	struct ifaddr *ifa;

	KASSERT(flag == IFF_UP, ("if_unroute: flag != IFF_UP"));

	ifp->if_flags &= ~flag;
	getmicrotime(&ifp->if_lastchange);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (fam == PF_UNSPEC || (fam == ifa->ifa_addr->sa_family))
			pfctlinput(PRC_IFDOWN, ifa->ifa_addr);
	ifp->if_qflush(ifp);

	if (ifp->if_carp)
		(*carp_linkstate_p)(ifp);
	rt_ifmsg(ifp);
}

/*
 * Mark an interface up and notify protocols of
 * the transition.
 */
static void
if_route(struct ifnet *ifp, int flag, int fam)
{
	struct ifaddr *ifa;

	KASSERT(flag == IFF_UP, ("if_route: flag != IFF_UP"));

	ifp->if_flags |= flag;
	getmicrotime(&ifp->if_lastchange);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (fam == PF_UNSPEC || (fam == ifa->ifa_addr->sa_family))
			pfctlinput(PRC_IFUP, ifa->ifa_addr);
	if (ifp->if_carp)
		(*carp_linkstate_p)(ifp);
	rt_ifmsg(ifp);
#ifdef INET6
	in6_if_up(ifp);
#endif
}

void	(*vlan_link_state_p)(struct ifnet *);	/* XXX: private from if_vlan */
void	(*vlan_trunk_cap_p)(struct ifnet *);		/* XXX: private from if_vlan */
struct ifnet *(*vlan_trunkdev_p)(struct ifnet *);
struct	ifnet *(*vlan_devat_p)(struct ifnet *, uint16_t);
int	(*vlan_tag_p)(struct ifnet *, uint16_t *);
int	(*vlan_pcp_p)(struct ifnet *, uint16_t *);
int	(*vlan_setcookie_p)(struct ifnet *, void *);
void	*(*vlan_cookie_p)(struct ifnet *);

/*
 * Handle a change in the interface link state. To avoid LORs
 * between driver lock and upper layer locks, as well as possible
 * recursions, we post event to taskqueue, and all job
 * is done in static do_link_state_change().
 */
void
if_link_state_change(struct ifnet *ifp, int link_state)
{
	/* Return if state hasn't changed. */
	if (ifp->if_link_state == link_state)
		return;

	ifp->if_link_state = link_state;

	taskqueue_enqueue(taskqueue_swi, &ifp->if_linktask);
}

static void
do_link_state_change(void *arg, int pending)
{
	struct ifnet *ifp = (struct ifnet *)arg;
	int link_state = ifp->if_link_state;
	CURVNET_SET(ifp->if_vnet);

	/* Notify that the link state has changed. */
	rt_ifmsg(ifp);
	if (ifp->if_vlantrunk != NULL)
		(*vlan_link_state_p)(ifp);

	if ((ifp->if_type == IFT_ETHER || ifp->if_type == IFT_L2VLAN) &&
	    ifp->if_l2com != NULL)
		(*ng_ether_link_state_p)(ifp, link_state);
	if (ifp->if_carp)
		(*carp_linkstate_p)(ifp);
	if (ifp->if_bridge)
		ifp->if_bridge_linkstate(ifp);
	if (ifp->if_lagg)
		(*lagg_linkstate_p)(ifp, link_state);

	if (IS_DEFAULT_VNET(curvnet))
		devctl_notify("IFNET", ifp->if_xname,
		    (link_state == LINK_STATE_UP) ? "LINK_UP" : "LINK_DOWN",
		    NULL);
	if (pending > 1)
		if_printf(ifp, "%d link states coalesced\n", pending);
	if (log_link_state_change)
		if_printf(ifp, "link state changed to %s\n",
		    (link_state == LINK_STATE_UP) ? "UP" : "DOWN" );
	EVENTHANDLER_INVOKE(ifnet_link_event, ifp, link_state);
	CURVNET_RESTORE();
}

/*
 * Mark an interface down and notify protocols of
 * the transition.
 */
void
if_down(struct ifnet *ifp)
{

	EVENTHANDLER_INVOKE(ifnet_event, ifp, IFNET_EVENT_DOWN);
	if_unroute(ifp, IFF_UP, AF_UNSPEC);
}

/*
 * Mark an interface up and notify protocols of
 * the transition.
 */
void
if_up(struct ifnet *ifp)
{

	if_route(ifp, IFF_UP, AF_UNSPEC);
	EVENTHANDLER_INVOKE(ifnet_event, ifp, IFNET_EVENT_UP);
}

/*
 * Flush an interface queue.
 */
void
if_qflush(struct ifnet *ifp)
{
	struct mbuf *m, *n;
	struct ifaltq *ifq;
	
	ifq = &ifp->if_snd;
	IFQ_LOCK(ifq);
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(ifq))
		ALTQ_PURGE(ifq);
#endif
	n = ifq->ifq_head;
	while ((m = n) != NULL) {
		n = m->m_nextpkt;
		m_freem(m);
	}
	ifq->ifq_head = 0;
	ifq->ifq_tail = 0;
	ifq->ifq_len = 0;
	IFQ_UNLOCK(ifq);
}

/*
 * Map interface name to interface structure pointer, with or without
 * returning a reference.
 */
struct ifnet *
ifunit_ref(const char *name)
{
	struct epoch_tracker et;
	struct ifnet *ifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (strncmp(name, ifp->if_xname, IFNAMSIZ) == 0 &&
		    !(ifp->if_flags & IFF_DYING))
			break;
	}
	if (ifp != NULL)
		if_ref(ifp);
	NET_EPOCH_EXIT(et);
	return (ifp);
}

struct ifnet *
ifunit(const char *name)
{
	struct epoch_tracker et;
	struct ifnet *ifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (strncmp(name, ifp->if_xname, IFNAMSIZ) == 0)
			break;
	}
	NET_EPOCH_EXIT(et);
	return (ifp);
}

static void *
ifr_buffer_get_buffer(void *data)
{
	union ifreq_union *ifrup;

	ifrup = data;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return ((void *)(uintptr_t)
		    ifrup->ifr32.ifr_ifru.ifru_buffer.buffer);
#endif
	return (ifrup->ifr.ifr_ifru.ifru_buffer.buffer);
}

static void
ifr_buffer_set_buffer_null(void *data)
{
	union ifreq_union *ifrup;

	ifrup = data;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		ifrup->ifr32.ifr_ifru.ifru_buffer.buffer = 0;
	else
#endif
		ifrup->ifr.ifr_ifru.ifru_buffer.buffer = NULL;
}

static size_t
ifr_buffer_get_length(void *data)
{
	union ifreq_union *ifrup;

	ifrup = data;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return (ifrup->ifr32.ifr_ifru.ifru_buffer.length);
#endif
	return (ifrup->ifr.ifr_ifru.ifru_buffer.length);
}

static void
ifr_buffer_set_length(void *data, size_t len)
{
	union ifreq_union *ifrup;

	ifrup = data;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		ifrup->ifr32.ifr_ifru.ifru_buffer.length = len;
	else
#endif
		ifrup->ifr.ifr_ifru.ifru_buffer.length = len;
}

void *
ifr_data_get_ptr(void *ifrp)
{
	union ifreq_union *ifrup;

	ifrup = ifrp;
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return ((void *)(uintptr_t)
		    ifrup->ifr32.ifr_ifru.ifru_data);
#endif
		return (ifrup->ifr.ifr_ifru.ifru_data);
}

/*
 * Hardware specific interface ioctls.
 */
int
ifhwioctl(u_long cmd, struct ifnet *ifp, caddr_t data, struct thread *td)
{
	struct ifreq *ifr;
	int error = 0, do_ifup = 0;
	int new_flags, temp_flags;
	size_t namelen, onamelen;
	size_t descrlen;
	char *descrbuf, *odescrbuf;
	char new_name[IFNAMSIZ];
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;

	ifr = (struct ifreq *)data;
	switch (cmd) {
	case SIOCGIFINDEX:
		ifr->ifr_index = ifp->if_index;
		break;

	case SIOCGIFFLAGS:
		temp_flags = ifp->if_flags | ifp->if_drv_flags;
		ifr->ifr_flags = temp_flags & 0xffff;
		ifr->ifr_flagshigh = temp_flags >> 16;
		break;

	case SIOCGIFCAP:
		ifr->ifr_reqcap = ifp->if_capabilities;
		ifr->ifr_curcap = ifp->if_capenable;
		break;

#ifdef MAC
	case SIOCGIFMAC:
		error = mac_ifnet_ioctl_get(td->td_ucred, ifr, ifp);
		break;
#endif

	case SIOCGIFMETRIC:
		ifr->ifr_metric = ifp->if_metric;
		break;

	case SIOCGIFMTU:
		ifr->ifr_mtu = ifp->if_mtu;
		break;

	case SIOCGIFPHYS:
		/* XXXGL: did this ever worked? */
		ifr->ifr_phys = 0;
		break;

	case SIOCGIFDESCR:
		error = 0;
		sx_slock(&ifdescr_sx);
		if (ifp->if_description == NULL)
			error = ENOMSG;
		else {
			/* space for terminating nul */
			descrlen = strlen(ifp->if_description) + 1;
			if (ifr_buffer_get_length(ifr) < descrlen)
				ifr_buffer_set_buffer_null(ifr);
			else
				error = copyout(ifp->if_description,
				    ifr_buffer_get_buffer(ifr), descrlen);
			ifr_buffer_set_length(ifr, descrlen);
		}
		sx_sunlock(&ifdescr_sx);
		break;

	case SIOCSIFDESCR:
		error = priv_check(td, PRIV_NET_SETIFDESCR);
		if (error)
			return (error);

		/*
		 * Copy only (length-1) bytes to make sure that
		 * if_description is always nul terminated.  The
		 * length parameter is supposed to count the
		 * terminating nul in.
		 */
		if (ifr_buffer_get_length(ifr) > ifdescr_maxlen)
			return (ENAMETOOLONG);
		else if (ifr_buffer_get_length(ifr) == 0)
			descrbuf = NULL;
		else {
			descrbuf = malloc(ifr_buffer_get_length(ifr),
			    M_IFDESCR, M_WAITOK | M_ZERO);
			error = copyin(ifr_buffer_get_buffer(ifr), descrbuf,
			    ifr_buffer_get_length(ifr) - 1);
			if (error) {
				free(descrbuf, M_IFDESCR);
				break;
			}
		}

		sx_xlock(&ifdescr_sx);
		odescrbuf = ifp->if_description;
		ifp->if_description = descrbuf;
		sx_xunlock(&ifdescr_sx);

		getmicrotime(&ifp->if_lastchange);
		free(odescrbuf, M_IFDESCR);
		break;

	case SIOCGIFFIB:
		ifr->ifr_fib = ifp->if_fib;
		break;

	case SIOCSIFFIB:
		error = priv_check(td, PRIV_NET_SETIFFIB);
		if (error)
			return (error);
		if (ifr->ifr_fib >= rt_numfibs)
			return (EINVAL);

		ifp->if_fib = ifr->ifr_fib;
		break;

	case SIOCSIFFLAGS:
		error = priv_check(td, PRIV_NET_SETIFFLAGS);
		if (error)
			return (error);
		/*
		 * Currently, no driver owned flags pass the IFF_CANTCHANGE
		 * check, so we don't need special handling here yet.
		 */
		new_flags = (ifr->ifr_flags & 0xffff) |
		    (ifr->ifr_flagshigh << 16);
		if (ifp->if_flags & IFF_UP &&
		    (new_flags & IFF_UP) == 0) {
			if_down(ifp);
		} else if (new_flags & IFF_UP &&
		    (ifp->if_flags & IFF_UP) == 0) {
			do_ifup = 1;
		}
		/* See if permanently promiscuous mode bit is about to flip */
		if ((ifp->if_flags ^ new_flags) & IFF_PPROMISC) {
			if (new_flags & IFF_PPROMISC)
				ifp->if_flags |= IFF_PROMISC;
			else if (ifp->if_pcount == 0)
				ifp->if_flags &= ~IFF_PROMISC;
			if (log_promisc_mode_change)
                                if_printf(ifp, "permanently promiscuous mode %s\n",
                                    ((new_flags & IFF_PPROMISC) ?
                                     "enabled" : "disabled"));
		}
		ifp->if_flags = (ifp->if_flags & IFF_CANTCHANGE) |
			(new_flags &~ IFF_CANTCHANGE);
		if (ifp->if_ioctl) {
			(void) (*ifp->if_ioctl)(ifp, cmd, data);
		}
		if (do_ifup)
			if_up(ifp);
		getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFCAP:
		error = priv_check(td, PRIV_NET_SETIFCAP);
		if (error)
			return (error);
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		if (ifr->ifr_reqcap & ~ifp->if_capabilities)
			return (EINVAL);
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

#ifdef MAC
	case SIOCSIFMAC:
		error = mac_ifnet_ioctl_set(td->td_ucred, ifr, ifp);
		break;
#endif

	case SIOCSIFNAME:
		error = priv_check(td, PRIV_NET_SETIFNAME);
		if (error)
			return (error);
		error = copyinstr(ifr_data_get_ptr(ifr), new_name, IFNAMSIZ,
		    NULL);
		if (error != 0)
			return (error);
		if (new_name[0] == '\0')
			return (EINVAL);
		if (new_name[IFNAMSIZ-1] != '\0') {
			new_name[IFNAMSIZ-1] = '\0';
			if (strlen(new_name) == IFNAMSIZ-1)
				return (EINVAL);
		}
		if (ifunit(new_name) != NULL)
			return (EEXIST);

		/*
		 * XXX: Locking.  Nothing else seems to lock if_flags,
		 * and there are numerous other races with the
		 * ifunit() checks not being atomic with namespace
		 * changes (renames, vmoves, if_attach, etc).
		 */
		ifp->if_flags |= IFF_RENAMING;
		
		/* Announce the departure of the interface. */
		rt_ifannouncemsg(ifp, IFAN_DEPARTURE);
		EVENTHANDLER_INVOKE(ifnet_departure_event, ifp);

		if_printf(ifp, "changing name to '%s'\n", new_name);

		IF_ADDR_WLOCK(ifp);
		strlcpy(ifp->if_xname, new_name, sizeof(ifp->if_xname));
		ifa = ifp->if_addr;
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		namelen = strlen(new_name);
		onamelen = sdl->sdl_nlen;
		/*
		 * Move the address if needed.  This is safe because we
		 * allocate space for a name of length IFNAMSIZ when we
		 * create this in if_attach().
		 */
		if (namelen != onamelen) {
			bcopy(sdl->sdl_data + onamelen,
			    sdl->sdl_data + namelen, sdl->sdl_alen);
		}
		bcopy(new_name, sdl->sdl_data, namelen);
		sdl->sdl_nlen = namelen;
		sdl = (struct sockaddr_dl *)ifa->ifa_netmask;
		bzero(sdl->sdl_data, onamelen);
		while (namelen != 0)
			sdl->sdl_data[--namelen] = 0xff;
		IF_ADDR_WUNLOCK(ifp);

		EVENTHANDLER_INVOKE(ifnet_arrival_event, ifp);
		/* Announce the return of the interface. */
		rt_ifannouncemsg(ifp, IFAN_ARRIVAL);

		ifp->if_flags &= ~IFF_RENAMING;
		break;

#ifdef VIMAGE
	case SIOCSIFVNET:
		error = priv_check(td, PRIV_NET_SETIFVNET);
		if (error)
			return (error);
		error = if_vmove_loan(td, ifp, ifr->ifr_name, ifr->ifr_jid);
		break;
#endif

	case SIOCSIFMETRIC:
		error = priv_check(td, PRIV_NET_SETIFMETRIC);
		if (error)
			return (error);
		ifp->if_metric = ifr->ifr_metric;
		getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFPHYS:
		error = priv_check(td, PRIV_NET_SETIFPHYS);
		if (error)
			return (error);
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFMTU:
	{
		u_long oldmtu = ifp->if_mtu;

		error = priv_check(td, PRIV_NET_SETIFMTU);
		if (error)
			return (error);
		if (ifr->ifr_mtu < IF_MINMTU || ifr->ifr_mtu > IF_MAXMTU)
			return (EINVAL);
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		if (error == 0) {
			getmicrotime(&ifp->if_lastchange);
			rt_ifmsg(ifp);
#ifdef INET
			NETDUMP_REINIT(ifp);
#endif
		}
		/*
		 * If the link MTU changed, do network layer specific procedure.
		 */
		if (ifp->if_mtu != oldmtu) {
#ifdef INET6
			nd6_setmtu(ifp);
#endif
			rt_updatemtu(ifp);
		}
		break;
	}

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (cmd == SIOCADDMULTI)
			error = priv_check(td, PRIV_NET_ADDMULTI);
		else
			error = priv_check(td, PRIV_NET_DELMULTI);
		if (error)
			return (error);

		/* Don't allow group membership on non-multicast interfaces. */
		if ((ifp->if_flags & IFF_MULTICAST) == 0)
			return (EOPNOTSUPP);

		/* Don't let users screw up protocols' entries. */
		if (ifr->ifr_addr.sa_family != AF_LINK)
			return (EINVAL);

		if (cmd == SIOCADDMULTI) {
			struct epoch_tracker et;
			struct ifmultiaddr *ifma;

			/*
			 * Userland is only permitted to join groups once
			 * via the if_addmulti() KPI, because it cannot hold
			 * struct ifmultiaddr * between calls. It may also
			 * lose a race while we check if the membership
			 * already exists.
			 */
			NET_EPOCH_ENTER(et);
			ifma = if_findmulti(ifp, &ifr->ifr_addr);
			NET_EPOCH_EXIT(et);
			if (ifma != NULL)
				error = EADDRINUSE;
			else
				error = if_addmulti(ifp, &ifr->ifr_addr, &ifma);
		} else {
			error = if_delmulti(ifp, &ifr->ifr_addr);
		}
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCSIFPHYADDR:
	case SIOCDIFPHYADDR:
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
#endif
	case SIOCSIFMEDIA:
	case SIOCSIFGENERIC:
		error = priv_check(td, PRIV_NET_HWIOCTL);
		if (error)
			return (error);
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		if (error == 0)
			getmicrotime(&ifp->if_lastchange);
		break;

	case SIOCGIFSTATUS:
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA:
	case SIOCGIFGENERIC:
	case SIOCGIFRSSKEY:
	case SIOCGIFRSSHASH:
		if (ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		break;

	case SIOCSIFLLADDR:
		error = priv_check(td, PRIV_NET_SETLLADDR);
		if (error)
			return (error);
		error = if_setlladdr(ifp,
		    ifr->ifr_addr.sa_data, ifr->ifr_addr.sa_len);
		break;

	case SIOCGHWADDR:
		error = if_gethwaddr(ifp, ifr);
		break;

	case CASE_IOC_IFGROUPREQ(SIOCAIFGROUP):
		error = priv_check(td, PRIV_NET_ADDIFGROUP);
		if (error)
			return (error);
		if ((error = if_addgroup(ifp,
		    ifgr_group_get((struct ifgroupreq *)data))))
			return (error);
		break;

	case CASE_IOC_IFGROUPREQ(SIOCGIFGROUP):
		if ((error = if_getgroup((struct ifgroupreq *)data, ifp)))
			return (error);
		break;

	case CASE_IOC_IFGROUPREQ(SIOCDIFGROUP):
		error = priv_check(td, PRIV_NET_DELIFGROUP);
		if (error)
			return (error);
		if ((error = if_delgroup(ifp,
		    ifgr_group_get((struct ifgroupreq *)data))))
			return (error);
		break;

	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}

#ifdef COMPAT_FREEBSD32
struct ifconf32 {
	int32_t	ifc_len;
	union {
		uint32_t	ifcu_buf;
		uint32_t	ifcu_req;
	} ifc_ifcu;
};
#define	SIOCGIFCONF32	_IOWR('i', 36, struct ifconf32)
#endif

#ifdef COMPAT_FREEBSD32
static void
ifmr_init(struct ifmediareq *ifmr, caddr_t data)
{
	struct ifmediareq32 *ifmr32;

	ifmr32 = (struct ifmediareq32 *)data;
	memcpy(ifmr->ifm_name, ifmr32->ifm_name,
	    sizeof(ifmr->ifm_name));
	ifmr->ifm_current = ifmr32->ifm_current;
	ifmr->ifm_mask = ifmr32->ifm_mask;
	ifmr->ifm_status = ifmr32->ifm_status;
	ifmr->ifm_active = ifmr32->ifm_active;
	ifmr->ifm_count = ifmr32->ifm_count;
	ifmr->ifm_ulist = (int *)(uintptr_t)ifmr32->ifm_ulist;
}

static void
ifmr_update(const struct ifmediareq *ifmr, caddr_t data)
{
	struct ifmediareq32 *ifmr32;

	ifmr32 = (struct ifmediareq32 *)data;
	ifmr32->ifm_current = ifmr->ifm_current;
	ifmr32->ifm_mask = ifmr->ifm_mask;
	ifmr32->ifm_status = ifmr->ifm_status;
	ifmr32->ifm_active = ifmr->ifm_active;
	ifmr32->ifm_count = ifmr->ifm_count;
}
#endif

/*
 * Interface ioctls.
 */
int
ifioctl(struct socket *so, u_long cmd, caddr_t data, struct thread *td)
{
#ifdef COMPAT_FREEBSD32
	caddr_t saved_data = NULL;
	struct ifmediareq ifmr;
	struct ifmediareq *ifmrp;
#endif
	struct ifnet *ifp;
	struct ifreq *ifr;
	int error;
	int oif_flags;
#ifdef VIMAGE
	int shutdown;
#endif

	CURVNET_SET(so->so_vnet);
#ifdef VIMAGE
	/* Make sure the VNET is stable. */
	shutdown = (so->so_vnet->vnet_state > SI_SUB_VNET &&
		 so->so_vnet->vnet_state < SI_SUB_VNET_DONE) ? 1 : 0;
	if (shutdown) {
		CURVNET_RESTORE();
		return (EBUSY);
	}
#endif


	switch (cmd) {
	case SIOCGIFCONF:
		error = ifconf(cmd, data);
		CURVNET_RESTORE();
		return (error);

#ifdef COMPAT_FREEBSD32
	case SIOCGIFCONF32:
		{
			struct ifconf32 *ifc32;
			struct ifconf ifc;

			ifc32 = (struct ifconf32 *)data;
			ifc.ifc_len = ifc32->ifc_len;
			ifc.ifc_buf = PTRIN(ifc32->ifc_buf);

			error = ifconf(SIOCGIFCONF, (void *)&ifc);
			CURVNET_RESTORE();
			if (error == 0)
				ifc32->ifc_len = ifc.ifc_len;
			return (error);
		}
#endif
	}

#ifdef COMPAT_FREEBSD32
	ifmrp = NULL;
	switch (cmd) {
	case SIOCGIFMEDIA32:
	case SIOCGIFXMEDIA32:
		ifmrp = &ifmr;
		ifmr_init(ifmrp, data);
		cmd = _IOC_NEWTYPE(cmd, struct ifmediareq);
		saved_data = data;
		data = (caddr_t)ifmrp;
	}
#endif

	ifr = (struct ifreq *)data;
	switch (cmd) {
#ifdef VIMAGE
	case SIOCSIFRVNET:
		error = priv_check(td, PRIV_NET_SETIFVNET);
		if (error == 0)
			error = if_vmove_reclaim(td, ifr->ifr_name,
			    ifr->ifr_jid);
		goto out_noref;
#endif
	case SIOCIFCREATE:
	case SIOCIFCREATE2:
		error = priv_check(td, PRIV_NET_IFCREATE);
		if (error == 0)
			error = if_clone_create(ifr->ifr_name,
			    sizeof(ifr->ifr_name), cmd == SIOCIFCREATE2 ?
			    ifr_data_get_ptr(ifr) : NULL);
		goto out_noref;
	case SIOCIFDESTROY:
		error = priv_check(td, PRIV_NET_IFDESTROY);
		if (error == 0)
			error = if_clone_destroy(ifr->ifr_name);
		goto out_noref;

	case SIOCIFGCLONERS:
		error = if_clone_list((struct if_clonereq *)data);
		goto out_noref;

	case CASE_IOC_IFGROUPREQ(SIOCGIFGMEMB):
		error = if_getgroupmembers((struct ifgroupreq *)data);
		goto out_noref;

#if defined(INET) || defined(INET6)
	case SIOCSVH:
	case SIOCGVH:
		if (carp_ioctl_p == NULL)
			error = EPROTONOSUPPORT;
		else
			error = (*carp_ioctl_p)(ifr, cmd, td);
		goto out_noref;
#endif
	}

	ifp = ifunit_ref(ifr->ifr_name);
	if (ifp == NULL) {
		error = ENXIO;
		goto out_noref;
	}

	error = ifhwioctl(cmd, ifp, data, td);
	if (error != ENOIOCTL)
		goto out_ref;

	oif_flags = ifp->if_flags;
	if (so->so_proto == NULL) {
		error = EOPNOTSUPP;
		goto out_ref;
	}

	/*
	 * Pass the request on to the socket control method, and if the
	 * latter returns EOPNOTSUPP, directly to the interface.
	 *
	 * Make an exception for the legacy SIOCSIF* requests.  Drivers
	 * trust SIOCSIFADDR et al to come from an already privileged
	 * layer, and do not perform any credentials checks or input
	 * validation.
	 */
	error = ((*so->so_proto->pr_usrreqs->pru_control)(so, cmd, data,
	    ifp, td));
	if (error == EOPNOTSUPP && ifp != NULL && ifp->if_ioctl != NULL &&
	    cmd != SIOCSIFADDR && cmd != SIOCSIFBRDADDR &&
	    cmd != SIOCSIFDSTADDR && cmd != SIOCSIFNETMASK)
		error = (*ifp->if_ioctl)(ifp, cmd, data);

	if ((oif_flags ^ ifp->if_flags) & IFF_UP) {
#ifdef INET6
		if (ifp->if_flags & IFF_UP)
			in6_if_up(ifp);
#endif
	}

out_ref:
	if_rele(ifp);
out_noref:
#ifdef COMPAT_FREEBSD32
	if (ifmrp != NULL) {
		KASSERT((cmd == SIOCGIFMEDIA || cmd == SIOCGIFXMEDIA),
		    ("ifmrp non-NULL, but cmd is not an ifmedia req 0x%lx",
		     cmd));
		data = saved_data;
		ifmr_update(ifmrp, data);
	}
#endif
	CURVNET_RESTORE();
	return (error);
}

/*
 * The code common to handling reference counted flags,
 * e.g., in ifpromisc() and if_allmulti().
 * The "pflag" argument can specify a permanent mode flag to check,
 * such as IFF_PPROMISC for promiscuous mode; should be 0 if none.
 *
 * Only to be used on stack-owned flags, not driver-owned flags.
 */
static int
if_setflag(struct ifnet *ifp, int flag, int pflag, int *refcount, int onswitch)
{
	struct ifreq ifr;
	int error;
	int oldflags, oldcount;

	/* Sanity checks to catch programming errors */
	KASSERT((flag & (IFF_DRV_OACTIVE|IFF_DRV_RUNNING)) == 0,
	    ("%s: setting driver-owned flag %d", __func__, flag));

	if (onswitch)
		KASSERT(*refcount >= 0,
		    ("%s: increment negative refcount %d for flag %d",
		    __func__, *refcount, flag));
	else
		KASSERT(*refcount > 0,
		    ("%s: decrement non-positive refcount %d for flag %d",
		    __func__, *refcount, flag));

	/* In case this mode is permanent, just touch refcount */
	if (ifp->if_flags & pflag) {
		*refcount += onswitch ? 1 : -1;
		return (0);
	}

	/* Save ifnet parameters for if_ioctl() may fail */
	oldcount = *refcount;
	oldflags = ifp->if_flags;
	
	/*
	 * See if we aren't the only and touching refcount is enough.
	 * Actually toggle interface flag if we are the first or last.
	 */
	if (onswitch) {
		if ((*refcount)++)
			return (0);
		ifp->if_flags |= flag;
	} else {
		if (--(*refcount))
			return (0);
		ifp->if_flags &= ~flag;
	}

	/* Call down the driver since we've changed interface flags */
	if (ifp->if_ioctl == NULL) {
		error = EOPNOTSUPP;
		goto recover;
	}
	ifr.ifr_flags = ifp->if_flags & 0xffff;
	ifr.ifr_flagshigh = ifp->if_flags >> 16;
	error = (*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifr);
	if (error)
		goto recover;
	/* Notify userland that interface flags have changed */
	rt_ifmsg(ifp);
	return (0);

recover:
	/* Recover after driver error */
	*refcount = oldcount;
	ifp->if_flags = oldflags;
	return (error);
}

/*
 * Set/clear promiscuous mode on interface ifp based on the truth value
 * of pswitch.  The calls are reference counted so that only the first
 * "on" request actually has an effect, as does the final "off" request.
 * Results are undefined if the "off" and "on" requests are not matched.
 */
int
ifpromisc(struct ifnet *ifp, int pswitch)
{
	int error;
	int oldflags = ifp->if_flags;

	error = if_setflag(ifp, IFF_PROMISC, IFF_PPROMISC,
			   &ifp->if_pcount, pswitch);
	/* If promiscuous mode status has changed, log a message */
	if (error == 0 && ((ifp->if_flags ^ oldflags) & IFF_PROMISC) &&
            log_promisc_mode_change)
		if_printf(ifp, "promiscuous mode %s\n",
		    (ifp->if_flags & IFF_PROMISC) ? "enabled" : "disabled");
	return (error);
}

/*
 * Return interface configuration
 * of system.  List may be used
 * in later ioctl's (above) to get
 * other information.
 */
/*ARGSUSED*/
static int
ifconf(u_long cmd, caddr_t data)
{
	struct ifconf *ifc = (struct ifconf *)data;
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct ifreq ifr;
	struct sbuf *sb;
	int error, full = 0, valid_len, max_len;

	/* Limit initial buffer size to MAXPHYS to avoid DoS from userspace. */
	max_len = MAXPHYS - 1;

	/* Prevent hostile input from being able to crash the system */
	if (ifc->ifc_len <= 0)
		return (EINVAL);

again:
	if (ifc->ifc_len <= max_len) {
		max_len = ifc->ifc_len;
		full = 1;
	}
	sb = sbuf_new(NULL, NULL, max_len + 1, SBUF_FIXEDLEN);
	max_len = 0;
	valid_len = 0;

	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		struct epoch_tracker et;
		int addrs;

		/*
		 * Zero the ifr to make sure we don't disclose the contents
		 * of the stack.
		 */
		memset(&ifr, 0, sizeof(ifr));

		if (strlcpy(ifr.ifr_name, ifp->if_xname, sizeof(ifr.ifr_name))
		    >= sizeof(ifr.ifr_name)) {
			sbuf_delete(sb);
			IFNET_RUNLOCK();
			return (ENAMETOOLONG);
		}

		addrs = 0;
		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			struct sockaddr *sa = ifa->ifa_addr;

			if (prison_if(curthread->td_ucred, sa) != 0)
				continue;
			addrs++;
			if (sa->sa_len <= sizeof(*sa)) {
				if (sa->sa_len < sizeof(*sa)) {
					memset(&ifr.ifr_ifru.ifru_addr, 0,
					    sizeof(ifr.ifr_ifru.ifru_addr));
					memcpy(&ifr.ifr_ifru.ifru_addr, sa,
					    sa->sa_len);
				} else
					ifr.ifr_ifru.ifru_addr = *sa;
				sbuf_bcat(sb, &ifr, sizeof(ifr));
				max_len += sizeof(ifr);
			} else {
				sbuf_bcat(sb, &ifr,
				    offsetof(struct ifreq, ifr_addr));
				max_len += offsetof(struct ifreq, ifr_addr);
				sbuf_bcat(sb, sa, sa->sa_len);
				max_len += sa->sa_len;
			}

			if (sbuf_error(sb) == 0)
				valid_len = sbuf_len(sb);
		}
		NET_EPOCH_EXIT(et);
		if (addrs == 0) {
			sbuf_bcat(sb, &ifr, sizeof(ifr));
			max_len += sizeof(ifr);

			if (sbuf_error(sb) == 0)
				valid_len = sbuf_len(sb);
		}
	}
	IFNET_RUNLOCK();

	/*
	 * If we didn't allocate enough space (uncommon), try again.  If
	 * we have already allocated as much space as we are allowed,
	 * return what we've got.
	 */
	if (valid_len != max_len && !full) {
		sbuf_delete(sb);
		goto again;
	}

	ifc->ifc_len = valid_len;
	sbuf_finish(sb);
	error = copyout(sbuf_data(sb), ifc->ifc_req, ifc->ifc_len);
	sbuf_delete(sb);
	return (error);
}

/*
 * Just like ifpromisc(), but for all-multicast-reception mode.
 */
int
if_allmulti(struct ifnet *ifp, int onswitch)
{

	return (if_setflag(ifp, IFF_ALLMULTI, 0, &ifp->if_amcount, onswitch));
}

struct ifmultiaddr *
if_findmulti(struct ifnet *ifp, const struct sockaddr *sa)
{
	struct ifmultiaddr *ifma;

	IF_ADDR_LOCK_ASSERT(ifp);

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (sa->sa_family == AF_LINK) {
			if (sa_dl_equal(ifma->ifma_addr, sa))
				break;
		} else {
			if (sa_equal(ifma->ifma_addr, sa))
				break;
		}
	}

	return ifma;
}

/*
 * Allocate a new ifmultiaddr and initialize based on passed arguments.  We
 * make copies of passed sockaddrs.  The ifmultiaddr will not be added to
 * the ifnet multicast address list here, so the caller must do that and
 * other setup work (such as notifying the device driver).  The reference
 * count is initialized to 1.
 */
static struct ifmultiaddr *
if_allocmulti(struct ifnet *ifp, struct sockaddr *sa, struct sockaddr *llsa,
    int mflags)
{
	struct ifmultiaddr *ifma;
	struct sockaddr *dupsa;

	ifma = malloc(sizeof *ifma, M_IFMADDR, mflags |
	    M_ZERO);
	if (ifma == NULL)
		return (NULL);

	dupsa = malloc(sa->sa_len, M_IFMADDR, mflags);
	if (dupsa == NULL) {
		free(ifma, M_IFMADDR);
		return (NULL);
	}
	bcopy(sa, dupsa, sa->sa_len);
	ifma->ifma_addr = dupsa;

	ifma->ifma_ifp = ifp;
	ifma->ifma_refcount = 1;
	ifma->ifma_protospec = NULL;

	if (llsa == NULL) {
		ifma->ifma_lladdr = NULL;
		return (ifma);
	}

	dupsa = malloc(llsa->sa_len, M_IFMADDR, mflags);
	if (dupsa == NULL) {
		free(ifma->ifma_addr, M_IFMADDR);
		free(ifma, M_IFMADDR);
		return (NULL);
	}
	bcopy(llsa, dupsa, llsa->sa_len);
	ifma->ifma_lladdr = dupsa;

	return (ifma);
}

/*
 * if_freemulti: free ifmultiaddr structure and possibly attached related
 * addresses.  The caller is responsible for implementing reference
 * counting, notifying the driver, handling routing messages, and releasing
 * any dependent link layer state.
 */
#ifdef MCAST_VERBOSE
extern void kdb_backtrace(void);
#endif
static void
if_freemulti_internal(struct ifmultiaddr *ifma)
{

	KASSERT(ifma->ifma_refcount == 0, ("if_freemulti: refcount %d",
	    ifma->ifma_refcount));

	if (ifma->ifma_lladdr != NULL)
		free(ifma->ifma_lladdr, M_IFMADDR);
#ifdef MCAST_VERBOSE
	kdb_backtrace();
	printf("%s freeing ifma: %p\n", __func__, ifma);
#endif
	free(ifma->ifma_addr, M_IFMADDR);
	free(ifma, M_IFMADDR);
}

static void
if_destroymulti(epoch_context_t ctx)
{
	struct ifmultiaddr *ifma;

	ifma = __containerof(ctx, struct ifmultiaddr, ifma_epoch_ctx);
	if_freemulti_internal(ifma);
}

void
if_freemulti(struct ifmultiaddr *ifma)
{
	KASSERT(ifma->ifma_refcount == 0, ("if_freemulti_epoch: refcount %d",
	    ifma->ifma_refcount));

	epoch_call(net_epoch_preempt, &ifma->ifma_epoch_ctx, if_destroymulti);
}


/*
 * Register an additional multicast address with a network interface.
 *
 * - If the address is already present, bump the reference count on the
 *   address and return.
 * - If the address is not link-layer, look up a link layer address.
 * - Allocate address structures for one or both addresses, and attach to the
 *   multicast address list on the interface.  If automatically adding a link
 *   layer address, the protocol address will own a reference to the link
 *   layer address, to be freed when it is freed.
 * - Notify the network device driver of an addition to the multicast address
 *   list.
 *
 * 'sa' points to caller-owned memory with the desired multicast address.
 *
 * 'retifma' will be used to return a pointer to the resulting multicast
 * address reference, if desired.
 */
int
if_addmulti(struct ifnet *ifp, struct sockaddr *sa,
    struct ifmultiaddr **retifma)
{
	struct ifmultiaddr *ifma, *ll_ifma;
	struct sockaddr *llsa;
	struct sockaddr_dl sdl;
	int error;

#ifdef INET
	IN_MULTI_LIST_UNLOCK_ASSERT();
#endif
#ifdef INET6
	IN6_MULTI_LIST_UNLOCK_ASSERT();
#endif
	/*
	 * If the address is already present, return a new reference to it;
	 * otherwise, allocate storage and set up a new address.
	 */
	IF_ADDR_WLOCK(ifp);
	ifma = if_findmulti(ifp, sa);
	if (ifma != NULL) {
		ifma->ifma_refcount++;
		if (retifma != NULL)
			*retifma = ifma;
		IF_ADDR_WUNLOCK(ifp);
		return (0);
	}

	/*
	 * The address isn't already present; resolve the protocol address
	 * into a link layer address, and then look that up, bump its
	 * refcount or allocate an ifma for that also.
	 * Most link layer resolving functions returns address data which
	 * fits inside default sockaddr_dl structure. However callback
	 * can allocate another sockaddr structure, in that case we need to
	 * free it later.
	 */
	llsa = NULL;
	ll_ifma = NULL;
	if (ifp->if_resolvemulti != NULL) {
		/* Provide called function with buffer size information */
		sdl.sdl_len = sizeof(sdl);
		llsa = (struct sockaddr *)&sdl;
		error = ifp->if_resolvemulti(ifp, &llsa, sa);
		if (error)
			goto unlock_out;
	}

	/*
	 * Allocate the new address.  Don't hook it up yet, as we may also
	 * need to allocate a link layer multicast address.
	 */
	ifma = if_allocmulti(ifp, sa, llsa, M_NOWAIT);
	if (ifma == NULL) {
		error = ENOMEM;
		goto free_llsa_out;
	}

	/*
	 * If a link layer address is found, we'll need to see if it's
	 * already present in the address list, or allocate is as well.
	 * When this block finishes, the link layer address will be on the
	 * list.
	 */
	if (llsa != NULL) {
		ll_ifma = if_findmulti(ifp, llsa);
		if (ll_ifma == NULL) {
			ll_ifma = if_allocmulti(ifp, llsa, NULL, M_NOWAIT);
			if (ll_ifma == NULL) {
				--ifma->ifma_refcount;
				if_freemulti(ifma);
				error = ENOMEM;
				goto free_llsa_out;
			}
			ll_ifma->ifma_flags |= IFMA_F_ENQUEUED;
			CK_STAILQ_INSERT_HEAD(&ifp->if_multiaddrs, ll_ifma,
			    ifma_link);
		} else
			ll_ifma->ifma_refcount++;
		ifma->ifma_llifma = ll_ifma;
	}

	/*
	 * We now have a new multicast address, ifma, and possibly a new or
	 * referenced link layer address.  Add the primary address to the
	 * ifnet address list.
	 */
	ifma->ifma_flags |= IFMA_F_ENQUEUED;
	CK_STAILQ_INSERT_HEAD(&ifp->if_multiaddrs, ifma, ifma_link);

	if (retifma != NULL)
		*retifma = ifma;

	/*
	 * Must generate the message while holding the lock so that 'ifma'
	 * pointer is still valid.
	 */
	rt_newmaddrmsg(RTM_NEWMADDR, ifma);
	IF_ADDR_WUNLOCK(ifp);

	/*
	 * We are certain we have added something, so call down to the
	 * interface to let them know about it.
	 */
	if (ifp->if_ioctl != NULL) {
		(void) (*ifp->if_ioctl)(ifp, SIOCADDMULTI, 0);
	}

	if ((llsa != NULL) && (llsa != (struct sockaddr *)&sdl))
		link_free_sdl(llsa);

	return (0);

free_llsa_out:
	if ((llsa != NULL) && (llsa != (struct sockaddr *)&sdl))
		link_free_sdl(llsa);

unlock_out:
	IF_ADDR_WUNLOCK(ifp);
	return (error);
}

/*
 * Delete a multicast group membership by network-layer group address.
 *
 * Returns ENOENT if the entry could not be found. If ifp no longer
 * exists, results are undefined. This entry point should only be used
 * from subsystems which do appropriate locking to hold ifp for the
 * duration of the call.
 * Network-layer protocol domains must use if_delmulti_ifma().
 */
int
if_delmulti(struct ifnet *ifp, struct sockaddr *sa)
{
	struct ifmultiaddr *ifma;
	int lastref;
#ifdef INVARIANTS
	struct epoch_tracker et;
	struct ifnet *oifp;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(oifp, &V_ifnet, if_link)
		if (ifp == oifp)
			break;
	if (ifp != oifp)
		ifp = NULL;
	NET_EPOCH_EXIT(et);

	KASSERT(ifp != NULL, ("%s: ifnet went away", __func__));
#endif
	if (ifp == NULL)
		return (ENOENT);

	IF_ADDR_WLOCK(ifp);
	lastref = 0;
	ifma = if_findmulti(ifp, sa);
	if (ifma != NULL)
		lastref = if_delmulti_locked(ifp, ifma, 0);
	IF_ADDR_WUNLOCK(ifp);

	if (ifma == NULL)
		return (ENOENT);

	if (lastref && ifp->if_ioctl != NULL) {
		(void)(*ifp->if_ioctl)(ifp, SIOCDELMULTI, 0);
	}

	return (0);
}

/*
 * Delete all multicast group membership for an interface.
 * Should be used to quickly flush all multicast filters.
 */
void
if_delallmulti(struct ifnet *ifp)
{
	struct ifmultiaddr *ifma;
	struct ifmultiaddr *next;

	IF_ADDR_WLOCK(ifp);
	CK_STAILQ_FOREACH_SAFE(ifma, &ifp->if_multiaddrs, ifma_link, next)
		if_delmulti_locked(ifp, ifma, 0);
	IF_ADDR_WUNLOCK(ifp);
}

void
if_delmulti_ifma(struct ifmultiaddr *ifma)
{
	if_delmulti_ifma_flags(ifma, 0);
}

/*
 * Delete a multicast group membership by group membership pointer.
 * Network-layer protocol domains must use this routine.
 *
 * It is safe to call this routine if the ifp disappeared.
 */
void
if_delmulti_ifma_flags(struct ifmultiaddr *ifma, int flags)
{
	struct ifnet *ifp;
	int lastref;
	MCDPRINTF("%s freeing ifma: %p\n", __func__, ifma);
#ifdef INET
	IN_MULTI_LIST_UNLOCK_ASSERT();
#endif
	ifp = ifma->ifma_ifp;
#ifdef DIAGNOSTIC
	if (ifp == NULL) {
		printf("%s: ifma_ifp seems to be detached\n", __func__);
	} else {
		struct epoch_tracker et;
		struct ifnet *oifp;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(oifp, &V_ifnet, if_link)
			if (ifp == oifp)
				break;
		if (ifp != oifp)
			ifp = NULL;
		NET_EPOCH_EXIT(et);
	}
#endif
	/*
	 * If and only if the ifnet instance exists: Acquire the address lock.
	 */
	if (ifp != NULL)
		IF_ADDR_WLOCK(ifp);

	lastref = if_delmulti_locked(ifp, ifma, flags);

	if (ifp != NULL) {
		/*
		 * If and only if the ifnet instance exists:
		 *  Release the address lock.
		 *  If the group was left: update the hardware hash filter.
		 */
		IF_ADDR_WUNLOCK(ifp);
		if (lastref && ifp->if_ioctl != NULL) {
			(void)(*ifp->if_ioctl)(ifp, SIOCDELMULTI, 0);
		}
	}
}

/*
 * Perform deletion of network-layer and/or link-layer multicast address.
 *
 * Return 0 if the reference count was decremented.
 * Return 1 if the final reference was released, indicating that the
 * hardware hash filter should be reprogrammed.
 */
static int
if_delmulti_locked(struct ifnet *ifp, struct ifmultiaddr *ifma, int detaching)
{
	struct ifmultiaddr *ll_ifma;

	if (ifp != NULL && ifma->ifma_ifp != NULL) {
		KASSERT(ifma->ifma_ifp == ifp,
		    ("%s: inconsistent ifp %p", __func__, ifp));
		IF_ADDR_WLOCK_ASSERT(ifp);
	}

	ifp = ifma->ifma_ifp;
	MCDPRINTF("%s freeing %p from %s \n", __func__, ifma, ifp ? ifp->if_xname : "");

	/*
	 * If the ifnet is detaching, null out references to ifnet,
	 * so that upper protocol layers will notice, and not attempt
	 * to obtain locks for an ifnet which no longer exists. The
	 * routing socket announcement must happen before the ifnet
	 * instance is detached from the system.
	 */
	if (detaching) {
#ifdef DIAGNOSTIC
		printf("%s: detaching ifnet instance %p\n", __func__, ifp);
#endif
		/*
		 * ifp may already be nulled out if we are being reentered
		 * to delete the ll_ifma.
		 */
		if (ifp != NULL) {
			rt_newmaddrmsg(RTM_DELMADDR, ifma);
			ifma->ifma_ifp = NULL;
		}
	}

	if (--ifma->ifma_refcount > 0)
		return 0;

	if (ifp != NULL && detaching == 0 && (ifma->ifma_flags & IFMA_F_ENQUEUED)) {
		CK_STAILQ_REMOVE(&ifp->if_multiaddrs, ifma, ifmultiaddr, ifma_link);
		ifma->ifma_flags &= ~IFMA_F_ENQUEUED;
	}
	/*
	 * If this ifma is a network-layer ifma, a link-layer ifma may
	 * have been associated with it. Release it first if so.
	 */
	ll_ifma = ifma->ifma_llifma;
	if (ll_ifma != NULL) {
		KASSERT(ifma->ifma_lladdr != NULL,
		    ("%s: llifma w/o lladdr", __func__));
		if (detaching)
			ll_ifma->ifma_ifp = NULL;	/* XXX */
		if (--ll_ifma->ifma_refcount == 0) {
			if (ifp != NULL) {
				if (ll_ifma->ifma_flags & IFMA_F_ENQUEUED) {
					CK_STAILQ_REMOVE(&ifp->if_multiaddrs, ll_ifma, ifmultiaddr,
						ifma_link);
					ll_ifma->ifma_flags &= ~IFMA_F_ENQUEUED;
				}
			}
			if_freemulti(ll_ifma);
		}
	}
#ifdef INVARIANTS
	if (ifp) {
		struct ifmultiaddr *ifmatmp;

		CK_STAILQ_FOREACH(ifmatmp, &ifp->if_multiaddrs, ifma_link)
			MPASS(ifma != ifmatmp);
	}
#endif
	if_freemulti(ifma);
	/*
	 * The last reference to this instance of struct ifmultiaddr
	 * was released; the hardware should be notified of this change.
	 */
	return 1;
}

/*
 * Set the link layer address on an interface.
 *
 * At this time we only support certain types of interfaces,
 * and we don't allow the length of the address to change.
 *
 * Set noinline to be dtrace-friendly
 */
__noinline int
if_setlladdr(struct ifnet *ifp, const u_char *lladdr, int len)
{
	struct sockaddr_dl *sdl;
	struct ifaddr *ifa;
	struct ifreq ifr;
	struct epoch_tracker et;
	int rc;

	rc = 0;
	NET_EPOCH_ENTER(et);
	ifa = ifp->if_addr;
	if (ifa == NULL) {
		rc = EINVAL;
		goto out;
	}

	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
	if (sdl == NULL) {
		rc = EINVAL;
		goto out;
	}
	if (len != sdl->sdl_alen) {	/* don't allow length to change */
		rc = EINVAL;
		goto out;
	}
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_XETHER:
	case IFT_L2VLAN:
	case IFT_BRIDGE:
	case IFT_IEEE8023ADLAG:
		bcopy(lladdr, LLADDR(sdl), len);
		break;
	default:
		rc = ENODEV;
		goto out;
	}

	/*
	 * If the interface is already up, we need
	 * to re-init it in order to reprogram its
	 * address filter.
	 */
	NET_EPOCH_EXIT(et);
	if ((ifp->if_flags & IFF_UP) != 0) {
		if (ifp->if_ioctl) {
			ifp->if_flags &= ~IFF_UP;
			ifr.ifr_flags = ifp->if_flags & 0xffff;
			ifr.ifr_flagshigh = ifp->if_flags >> 16;
			(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifr);
			ifp->if_flags |= IFF_UP;
			ifr.ifr_flags = ifp->if_flags & 0xffff;
			ifr.ifr_flagshigh = ifp->if_flags >> 16;
			(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifr);
		}
	}
	EVENTHANDLER_INVOKE(iflladdr_event, ifp);
	return (0);
 out:
	NET_EPOCH_EXIT(et);
	return (rc);
}

/*
 * Compat function for handling basic encapsulation requests.
 * Not converted stacks (FDDI, IB, ..) supports traditional
 * output model: ARP (and other similar L2 protocols) are handled
 * inside output routine, arpresolve/nd6_resolve() returns MAC
 * address instead of full prepend.
 *
 * This function creates calculated header==MAC for IPv4/IPv6 and
 * returns EAFNOSUPPORT (which is then handled in ARP code) for other
 * address families.
 */
static int
if_requestencap_default(struct ifnet *ifp, struct if_encap_req *req)
{

	if (req->rtype != IFENCAP_LL)
		return (EOPNOTSUPP);

	if (req->bufsize < req->lladdr_len)
		return (ENOMEM);

	switch (req->family) {
	case AF_INET:
	case AF_INET6:
		break;
	default:
		return (EAFNOSUPPORT);
	}

	/* Copy lladdr to storage as is */
	memmove(req->buf, req->lladdr, req->lladdr_len);
	req->bufsize = req->lladdr_len;
	req->lladdr_off = 0;

	return (0);
}

/*
 * Tunnel interfaces can nest, also they may cause infinite recursion
 * calls when misconfigured. We'll prevent this by detecting loops.
 * High nesting level may cause stack exhaustion. We'll prevent this
 * by introducing upper limit.
 *
 * Return 0, if tunnel nesting count is equal or less than limit.
 */
int
if_tunnel_check_nesting(struct ifnet *ifp, struct mbuf *m, uint32_t cookie,
    int limit)
{
	struct m_tag *mtag;
	int count;

	count = 1;
	mtag = NULL;
	while ((mtag = m_tag_locate(m, cookie, 0, mtag)) != NULL) {
		if (*(struct ifnet **)(mtag + 1) == ifp) {
			log(LOG_NOTICE, "%s: loop detected\n", if_name(ifp));
			return (EIO);
		}
		count++;
	}
	if (count > limit) {
		log(LOG_NOTICE,
		    "%s: if_output recursively called too many times(%d)\n",
		    if_name(ifp), count);
		return (EIO);
	}
	mtag = m_tag_alloc(cookie, 0, sizeof(struct ifnet *), M_NOWAIT);
	if (mtag == NULL)
		return (ENOMEM);
	*(struct ifnet **)(mtag + 1) = ifp;
	m_tag_prepend(m, mtag);
	return (0);
}

/*
 * Get the link layer address that was read from the hardware at attach.
 *
 * This is only set by Ethernet NICs (IFT_ETHER), but laggX interfaces re-type
 * their component interfaces as IFT_IEEE8023ADLAG.
 */
int
if_gethwaddr(struct ifnet *ifp, struct ifreq *ifr)
{

	if (ifp->if_hw_addr == NULL)
		return (ENODEV);

	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_IEEE8023ADLAG:
		bcopy(ifp->if_hw_addr, ifr->ifr_addr.sa_data, ifp->if_addrlen);
		return (0);
	default:
		return (ENODEV);
	}
}

/*
 * The name argument must be a pointer to storage which will last as
 * long as the interface does.  For physical devices, the result of
 * device_get_name(dev) is a good choice and for pseudo-devices a
 * static string works well.
 */
void
if_initname(struct ifnet *ifp, const char *name, int unit)
{
	ifp->if_dname = name;
	ifp->if_dunit = unit;
	if (unit != IF_DUNIT_NONE)
		snprintf(ifp->if_xname, IFNAMSIZ, "%s%d", name, unit);
	else
		strlcpy(ifp->if_xname, name, IFNAMSIZ);
}

int
if_printf(struct ifnet *ifp, const char *fmt, ...)
{
	char if_fmt[256];
	va_list ap;

	snprintf(if_fmt, sizeof(if_fmt), "%s: %s", ifp->if_xname, fmt);
	va_start(ap, fmt);
	vlog(LOG_INFO, if_fmt, ap);
	va_end(ap);
	return (0);
}

void
if_start(struct ifnet *ifp)
{

	(*(ifp)->if_start)(ifp);
}

/*
 * Backwards compatibility interface for drivers 
 * that have not implemented it
 */
static int
if_transmit(struct ifnet *ifp, struct mbuf *m)
{
	int error;

	IFQ_HANDOFF(ifp, m, error);
	return (error);
}

static void
if_input_default(struct ifnet *ifp __unused, struct mbuf *m)
{

	m_freem(m);
}

int
if_handoff(struct ifqueue *ifq, struct mbuf *m, struct ifnet *ifp, int adjust)
{
	int active = 0;

	IF_LOCK(ifq);
	if (_IF_QFULL(ifq)) {
		IF_UNLOCK(ifq);
		if_inc_counter(ifp, IFCOUNTER_OQDROPS, 1);
		m_freem(m);
		return (0);
	}
	if (ifp != NULL) {
		if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len + adjust);
		if (m->m_flags & (M_BCAST|M_MCAST))
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
		active = ifp->if_drv_flags & IFF_DRV_OACTIVE;
	}
	_IF_ENQUEUE(ifq, m);
	IF_UNLOCK(ifq);
	if (ifp != NULL && !active)
		(*(ifp)->if_start)(ifp);
	return (1);
}

void
if_register_com_alloc(u_char type,
    if_com_alloc_t *a, if_com_free_t *f)
{
	
	KASSERT(if_com_alloc[type] == NULL,
	    ("if_register_com_alloc: %d already registered", type));
	KASSERT(if_com_free[type] == NULL,
	    ("if_register_com_alloc: %d free already registered", type));

	if_com_alloc[type] = a;
	if_com_free[type] = f;
}

void
if_deregister_com_alloc(u_char type)
{
	
	KASSERT(if_com_alloc[type] != NULL,
	    ("if_deregister_com_alloc: %d not registered", type));
	KASSERT(if_com_free[type] != NULL,
	    ("if_deregister_com_alloc: %d free not registered", type));
	if_com_alloc[type] = NULL;
	if_com_free[type] = NULL;
}

/* API for driver access to network stack owned ifnet.*/
uint64_t
if_setbaudrate(struct ifnet *ifp, uint64_t baudrate)
{
	uint64_t oldbrate;

	oldbrate = ifp->if_baudrate;
	ifp->if_baudrate = baudrate;
	return (oldbrate);
}

uint64_t
if_getbaudrate(if_t ifp)
{

	return (((struct ifnet *)ifp)->if_baudrate);
}

int
if_setcapabilities(if_t ifp, int capabilities)
{
	((struct ifnet *)ifp)->if_capabilities = capabilities;
	return (0);
}

int
if_setcapabilitiesbit(if_t ifp, int setbit, int clearbit)
{
	((struct ifnet *)ifp)->if_capabilities |= setbit;
	((struct ifnet *)ifp)->if_capabilities &= ~clearbit;

	return (0);
}

int
if_getcapabilities(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_capabilities;
}

int 
if_setcapenable(if_t ifp, int capabilities)
{
	((struct ifnet *)ifp)->if_capenable = capabilities;
	return (0);
}

int 
if_setcapenablebit(if_t ifp, int setcap, int clearcap)
{
	if(setcap) 
		((struct ifnet *)ifp)->if_capenable |= setcap;
	if(clearcap)
		((struct ifnet *)ifp)->if_capenable &= ~clearcap;

	return (0);
}

const char *
if_getdname(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_dname;
}

int 
if_togglecapenable(if_t ifp, int togglecap)
{
	((struct ifnet *)ifp)->if_capenable ^= togglecap;
	return (0);
}

int
if_getcapenable(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_capenable;
}

/*
 * This is largely undesirable because it ties ifnet to a device, but does
 * provide flexiblity for an embedded product vendor. Should be used with
 * the understanding that it violates the interface boundaries, and should be
 * a last resort only.
 */
int
if_setdev(if_t ifp, void *dev)
{
	return (0);
}

int
if_setdrvflagbits(if_t ifp, int set_flags, int clear_flags)
{
	((struct ifnet *)ifp)->if_drv_flags |= set_flags;
	((struct ifnet *)ifp)->if_drv_flags &= ~clear_flags;

	return (0);
}

int
if_getdrvflags(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_drv_flags;
}
 
int
if_setdrvflags(if_t ifp, int flags)
{
	((struct ifnet *)ifp)->if_drv_flags = flags;
	return (0);
}


int
if_setflags(if_t ifp, int flags)
{
	((struct ifnet *)ifp)->if_flags = flags;
	return (0);
}

int
if_setflagbits(if_t ifp, int set, int clear)
{
	((struct ifnet *)ifp)->if_flags |= set;
	((struct ifnet *)ifp)->if_flags &= ~clear;

	return (0);
}

int
if_getflags(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_flags;
}

int
if_clearhwassist(if_t ifp)
{
	((struct ifnet *)ifp)->if_hwassist = 0;
	return (0);
}

int
if_sethwassistbits(if_t ifp, int toset, int toclear)
{
	((struct ifnet *)ifp)->if_hwassist |= toset;
	((struct ifnet *)ifp)->if_hwassist &= ~toclear;

	return (0);
}

int
if_sethwassist(if_t ifp, int hwassist_bit)
{
	((struct ifnet *)ifp)->if_hwassist = hwassist_bit;
	return (0);
}

int
if_gethwassist(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_hwassist;
}

int
if_setmtu(if_t ifp, int mtu)
{
	((struct ifnet *)ifp)->if_mtu = mtu;
	return (0);
}

int
if_getmtu(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_mtu;
}

int
if_getmtu_family(if_t ifp, int family)
{
	struct domain *dp;

	for (dp = domains; dp; dp = dp->dom_next) {
		if (dp->dom_family == family && dp->dom_ifmtu != NULL)
			return (dp->dom_ifmtu((struct ifnet *)ifp));
	}

	return (((struct ifnet *)ifp)->if_mtu);
}

int
if_setsoftc(if_t ifp, void *softc)
{
	((struct ifnet *)ifp)->if_softc = softc;
	return (0);
}

void *
if_getsoftc(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_softc;
}

void 
if_setrcvif(struct mbuf *m, if_t ifp)
{
	m->m_pkthdr.rcvif = (struct ifnet *)ifp;
}

void 
if_setvtag(struct mbuf *m, uint16_t tag)
{
	m->m_pkthdr.ether_vtag = tag;	
}

uint16_t
if_getvtag(struct mbuf *m)
{

	return (m->m_pkthdr.ether_vtag);
}

int
if_sendq_empty(if_t ifp)
{
	return IFQ_DRV_IS_EMPTY(&((struct ifnet *)ifp)->if_snd);
}

struct ifaddr *
if_getifaddr(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_addr;
}

int
if_getamcount(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_amcount;
}


int
if_setsendqready(if_t ifp)
{
	IFQ_SET_READY(&((struct ifnet *)ifp)->if_snd);
	return (0);
}

int
if_setsendqlen(if_t ifp, int tx_desc_count)
{
	IFQ_SET_MAXLEN(&((struct ifnet *)ifp)->if_snd, tx_desc_count);
	((struct ifnet *)ifp)->if_snd.ifq_drv_maxlen = tx_desc_count;

	return (0);
}

int
if_vlantrunkinuse(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_vlantrunk != NULL?1:0;
}

int
if_input(if_t ifp, struct mbuf* sendmp)
{
	(*((struct ifnet *)ifp)->if_input)((struct ifnet *)ifp, sendmp);
	return (0);

}

/* XXX */
#ifndef ETH_ADDR_LEN
#define ETH_ADDR_LEN 6
#endif

int 
if_setupmultiaddr(if_t ifp, void *mta, int *cnt, int max)
{
	struct ifmultiaddr *ifma;
	uint8_t *lmta = (uint8_t *)mta;
	int mcnt = 0;

	CK_STAILQ_FOREACH(ifma, &((struct ifnet *)ifp)->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == max)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &lmta[mcnt * ETH_ADDR_LEN], ETH_ADDR_LEN);
		mcnt++;
	}
	*cnt = mcnt;

	return (0);
}

int
if_multiaddr_array(if_t ifp, void *mta, int *cnt, int max)
{
	int error;

	if_maddr_rlock(ifp);
	error = if_setupmultiaddr(ifp, mta, cnt, max);
	if_maddr_runlock(ifp);
	return (error);
}

int
if_multiaddr_count(if_t ifp, int max)
{
	struct ifmultiaddr *ifma;
	int count;

	count = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &((struct ifnet *)ifp)->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		count++;
		if (count == max)
			break;
	}
	if_maddr_runlock(ifp);
	return (count);
}

int
if_multi_apply(struct ifnet *ifp, int (*filter)(void *, struct ifmultiaddr *, int), void *arg)
{
	struct ifmultiaddr *ifma;
	int cnt = 0;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
		cnt += filter(arg, ifma, cnt);
	if_maddr_runlock(ifp);
	return (cnt);
}

struct mbuf *
if_dequeue(if_t ifp)
{
	struct mbuf *m;
	IFQ_DRV_DEQUEUE(&((struct ifnet *)ifp)->if_snd, m);

	return (m);
}

int
if_sendq_prepend(if_t ifp, struct mbuf *m)
{
	IFQ_DRV_PREPEND(&((struct ifnet *)ifp)->if_snd, m);
	return (0);
}

int
if_setifheaderlen(if_t ifp, int len)
{
	((struct ifnet *)ifp)->if_hdrlen = len;
	return (0);
}

caddr_t
if_getlladdr(if_t ifp)
{
	return (IF_LLADDR((struct ifnet *)ifp));
}

void *
if_gethandle(u_char type)
{
	return (if_alloc(type));
}

void
if_bpfmtap(if_t ifh, struct mbuf *m)
{
	struct ifnet *ifp = (struct ifnet *)ifh;

	BPF_MTAP(ifp, m);
}

void
if_etherbpfmtap(if_t ifh, struct mbuf *m)
{
	struct ifnet *ifp = (struct ifnet *)ifh;

	ETHER_BPF_MTAP(ifp, m);
}

void
if_vlancap(if_t ifh)
{
	struct ifnet *ifp = (struct ifnet *)ifh;
	VLAN_CAPABILITIES(ifp);
}

int
if_sethwtsomax(if_t ifp, u_int if_hw_tsomax)
{

	((struct ifnet *)ifp)->if_hw_tsomax = if_hw_tsomax;
        return (0);
}

int
if_sethwtsomaxsegcount(if_t ifp, u_int if_hw_tsomaxsegcount)
{

	((struct ifnet *)ifp)->if_hw_tsomaxsegcount = if_hw_tsomaxsegcount;
        return (0);
}

int
if_sethwtsomaxsegsize(if_t ifp, u_int if_hw_tsomaxsegsize)
{

	((struct ifnet *)ifp)->if_hw_tsomaxsegsize = if_hw_tsomaxsegsize;
        return (0);
}

u_int
if_gethwtsomax(if_t ifp)
{

	return (((struct ifnet *)ifp)->if_hw_tsomax);
}

u_int
if_gethwtsomaxsegcount(if_t ifp)
{

	return (((struct ifnet *)ifp)->if_hw_tsomaxsegcount);
}

u_int
if_gethwtsomaxsegsize(if_t ifp)
{

	return (((struct ifnet *)ifp)->if_hw_tsomaxsegsize);
}

void
if_setinitfn(if_t ifp, void (*init_fn)(void *))
{
	((struct ifnet *)ifp)->if_init = init_fn;
}

void
if_setioctlfn(if_t ifp, int (*ioctl_fn)(if_t, u_long, caddr_t))
{
	((struct ifnet *)ifp)->if_ioctl = (void *)ioctl_fn;
}

void
if_setstartfn(if_t ifp, void (*start_fn)(if_t))
{
	((struct ifnet *)ifp)->if_start = (void *)start_fn;
}

void
if_settransmitfn(if_t ifp, if_transmit_fn_t start_fn)
{
	((struct ifnet *)ifp)->if_transmit = start_fn;
}

void if_setqflushfn(if_t ifp, if_qflush_fn_t flush_fn)
{
	((struct ifnet *)ifp)->if_qflush = flush_fn;
	
}

void
if_setgetcounterfn(if_t ifp, if_get_counter_t fn)
{

	ifp->if_get_counter = fn;
}

/* Revisit these - These are inline functions originally. */
int
drbr_inuse_drv(if_t ifh, struct buf_ring *br)
{
	return drbr_inuse(ifh, br);
}

struct mbuf*
drbr_dequeue_drv(if_t ifh, struct buf_ring *br)
{
	return drbr_dequeue(ifh, br);
}

int
drbr_needs_enqueue_drv(if_t ifh, struct buf_ring *br)
{
	return drbr_needs_enqueue(ifh, br);
}

int
drbr_enqueue_drv(if_t ifh, struct buf_ring *br, struct mbuf *m)
{
	return drbr_enqueue(ifh, br, m);

}
