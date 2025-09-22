/*	$OpenBSD: if.c,v 1.741 2025/09/09 09:16:18 bluhm Exp $	*/
/*	$NetBSD: if.c,v 1.35 1996/05/07 05:26:04 thorpej Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

/*
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
 *	@(#)if.c	8.3 (Berkeley) 1/4/94
 */

#include "bpfilter.h"
#include "bridge.h"
#include "carp.h"
#include "ether.h"
#include "pf.h"
#include "ppp.h"
#include "pppoe.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/task.h>
#include <sys/atomic.h>
#include <sys/percpu.h>
#include <sys/stdint.h>	/* uintptr_t */
#include <sys/rwlock.h>
#include <sys/smr.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/igmp.h>
#ifdef MROUTING
#include <netinet/ip_mroute.h>
#endif
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_var.h>
#endif

#ifdef MPLS
#include <netmpls/mpls.h>
#endif

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NBRIDGE > 0
#include <net/if_bridge.h>
#endif

#if NPF > 0
#include <net/pfvar.h>
#endif

#include <sys/device.h>

void	if_attachsetup(struct ifnet *);
void	if_attach_common(struct ifnet *);
void	if_remove(struct ifnet *);
int	if_createrdomain(int, struct ifnet *);
int	if_setrdomain(struct ifnet *, int);
void	if_slowtimo(void *);

void	if_detached_qstart(struct ifqueue *);
int	if_detached_ioctl(struct ifnet *, u_long, caddr_t);

int	ifioctl_get(u_long, caddr_t);
int	ifconf(caddr_t);
static int
	if_sffpage_check(const caddr_t);

int	if_getgroup(caddr_t, struct ifnet *);
int	if_getgroupmembers(caddr_t);
int	if_getgroupattribs(caddr_t);
int	if_setgroupattribs(caddr_t);
int	if_getgrouplist(caddr_t);

void	if_linkstate(struct ifnet *);
void	if_linkstate_task(void *);

int	if_clone_list(struct if_clonereq *);
struct if_clone	*if_clone_lookup(const char *, int *);

int	if_group_egress_build(void);

void	if_watchdog_task(void *);

void	if_netisr(void *);

#ifdef DDB
void	ifa_print_all(void);
#endif

void	if_qstart_compat(struct ifqueue *);

struct softnet *
	net_sn(unsigned int);

/*
 * interface index map
 *
 * the kernel maintains a mapping of interface indexes to struct ifnet
 * pointers.
 *
 * the map is an array of struct ifnet pointers prefixed by an if_map
 * structure. the if_map structure stores the length of its array.
 *
 * as interfaces are attached to the system, the map is grown on demand
 * up to USHRT_MAX entries.
 *
 * interface index 0 is reserved and represents no interface. this
 * supports the use of the interface index as the scope for IPv6 link
 * local addresses, where scope 0 means no scope has been specified.
 * it also supports the use of interface index as the unique identifier
 * for network interfaces in SNMP applications as per RFC2863. therefore
 * if_get(0) returns NULL.
 */

/*
 * struct if_idxmap
 *
 * infrastructure to manage updates and accesses to the current if_map.
 *
 * interface index 0 is special and represents "no interface", so we
 * use the 0th slot in map to store the length of the array.
 */

struct if_idxmap {
	unsigned int		  serial;
	unsigned int		  count;
	struct ifnet		**map;		/* SMR protected */
	struct rwlock		  lock;
	unsigned char		 *usedidx;	/* bitmap of indices in use */
};

struct if_idxmap_dtor {
	struct smr_entry	  smr;
	struct ifnet		**map;
};

void	if_idxmap_init(unsigned int);
void	if_idxmap_free(void *);
void	if_idxmap_alloc(struct ifnet *);
void	if_idxmap_insert(struct ifnet *);
void	if_idxmap_remove(struct ifnet *);

TAILQ_HEAD(, ifg_group) ifg_head =
    TAILQ_HEAD_INITIALIZER(ifg_head);	/* [N] list of interface groups */

LIST_HEAD(, if_clone) if_cloners =
    LIST_HEAD_INITIALIZER(if_cloners);	/* [I] list of clonable interfaces */
int if_cloners_count;	/* [I] number of clonable interfaces */

struct rwlock if_cloners_lock = RWLOCK_INITIALIZER("clonelk");
struct rwlock if_tmplist_lock = RWLOCK_INITIALIZER("iftmplk");

/* hooks should only be added, deleted, and run from a process context */
struct mutex if_hooks_mtx = MUTEX_INITIALIZER(IPL_NONE);
void	if_hooks_run(struct task_list *);

int		ifq_congestion;
int		netisr;

struct softnet {
	char		 sn_name[16];
	struct taskq	*sn_taskq;
	struct netstack	 sn_netstack;
} __aligned(64);
#ifdef MULTIPROCESSOR
#define NET_TASKQ	8
#else
#define NET_TASKQ	1
#endif
struct softnet	softnets[NET_TASKQ];

struct task	if_input_task_locked = TASK_INITIALIZER(if_netisr, NULL);

/*
 * Serialize socket operations to ensure no new sleeping points
 * are introduced in IP output paths.
 */
struct rwlock netlock = RWLOCK_INITIALIZER_TRACE("netlock",
    DT_RWLOCK_IDX_NETLOCK);

/*
 * Network interface utility routines.
 */
void
ifinit(void)
{
	/*
	 * most machines boot with 4 or 5 interfaces, so size the initial map
	 * to accommodate this
	 */
	if_idxmap_init(8); /* 8 is a nice power of 2 for malloc */
}

void
softnet_init(void)
{
	unsigned int i;

	/* Number of CPU is unknown, but driver attach needs softnet tasks. */
	for (i = 0; i < NET_TASKQ; i++) {
		struct softnet *sn = &softnets[i];

		snprintf(sn->sn_name, sizeof(sn->sn_name), "softnet%u", i);
		sn->sn_taskq = taskq_create(sn->sn_name, 1, IPL_NET,
		    TASKQ_MPSAFE);
		if (sn->sn_taskq == NULL)
			panic("unable to create network taskq %d", i);
	}
}

void
softnet_percpu(void)
{
#ifdef MULTIPROCESSOR
	unsigned int i;

	/* After attaching all CPUs and interfaces, remove useless threads. */
	for (i = softnet_count(); i < NET_TASKQ; i++) {
		struct softnet *sn = &softnets[i];

		taskq_destroy(sn->sn_taskq);
		sn->sn_taskq = NULL;
	}
#endif /* MULTIPROCESSOR */
}

static struct if_idxmap if_idxmap;

/*
 * XXXSMP: For `ifnetlist' modification both kernel and net locks
 * should be taken. For read-only access only one lock of them required.
 */
struct ifnet_head ifnetlist = TAILQ_HEAD_INITIALIZER(ifnetlist);

static inline unsigned int
if_idxmap_limit(struct ifnet **if_map)
{
	return ((uintptr_t)if_map[0]);
}

static inline size_t
if_idxmap_usedidx_size(unsigned int limit)
{
	return (max(howmany(limit, NBBY), sizeof(struct if_idxmap_dtor)));
}

void
if_idxmap_init(unsigned int limit)
{
	struct ifnet **if_map;

	rw_init(&if_idxmap.lock, "idxmaplk");
	if_idxmap.serial = 1; /* skip ifidx 0 */

	if_map = mallocarray(limit, sizeof(*if_map), M_IFADDR,
	    M_WAITOK | M_ZERO);

	if_map[0] = (struct ifnet *)(uintptr_t)limit;

	if_idxmap.usedidx = malloc(if_idxmap_usedidx_size(limit),
	    M_IFADDR, M_WAITOK | M_ZERO);
	setbit(if_idxmap.usedidx, 0); /* blacklist ifidx 0 */

	/* this is called early so there's nothing to race with */
	SMR_PTR_SET_LOCKED(&if_idxmap.map, if_map);
}

void
if_idxmap_alloc(struct ifnet *ifp)
{
	struct ifnet **if_map;
	unsigned int limit;
	unsigned int index, i;

	refcnt_init(&ifp->if_refcnt);

	rw_enter_write(&if_idxmap.lock);

	if (++if_idxmap.count >= USHRT_MAX)
		panic("too many interfaces");

	if_map = SMR_PTR_GET_LOCKED(&if_idxmap.map);
	limit = if_idxmap_limit(if_map);

	index = if_idxmap.serial++ & USHRT_MAX;

	if (index >= limit) {
		struct if_idxmap_dtor *dtor;
		struct ifnet **oif_map;
		unsigned int olimit;
		unsigned char *nusedidx;

		oif_map = if_map;
		olimit = limit;

		limit = olimit * 2;
		if_map = mallocarray(limit, sizeof(*if_map), M_IFADDR,
		    M_WAITOK | M_ZERO);
		if_map[0] = (struct ifnet *)(uintptr_t)limit;
		
		for (i = 1; i < olimit; i++) {
			struct ifnet *oifp = SMR_PTR_GET_LOCKED(&oif_map[i]);
			if (oifp == NULL)
				continue;

			/*
			 * nif_map isn't visible yet, so don't need
			 * SMR_PTR_SET_LOCKED and its membar.
			 */
			if_map[i] = if_ref(oifp);
		}

		nusedidx = malloc(if_idxmap_usedidx_size(limit),
		    M_IFADDR, M_WAITOK | M_ZERO);
		memcpy(nusedidx, if_idxmap.usedidx, howmany(olimit, NBBY));

		/* use the old usedidx bitmap as an smr_entry for the if_map */
		dtor = (struct if_idxmap_dtor *)if_idxmap.usedidx;
		if_idxmap.usedidx = nusedidx;

		SMR_PTR_SET_LOCKED(&if_idxmap.map, if_map);

		dtor->map = oif_map;
		smr_init(&dtor->smr);
		smr_call(&dtor->smr, if_idxmap_free, dtor);
	}

	/* pick the next free index */
	for (i = 0; i < USHRT_MAX; i++) {
		if (index != 0 && isclr(if_idxmap.usedidx, index))
			break;

		index = if_idxmap.serial++ & USHRT_MAX;
	}
	KASSERT(index != 0 && index < limit);
	KASSERT(isclr(if_idxmap.usedidx, index));

	setbit(if_idxmap.usedidx, index);
	ifp->if_index = index;

	rw_exit_write(&if_idxmap.lock);
}

void
if_idxmap_free(void *arg)
{
	struct if_idxmap_dtor *dtor = arg;
	struct ifnet **oif_map = dtor->map;
	unsigned int olimit = if_idxmap_limit(oif_map);
	unsigned int i;

	for (i = 1; i < olimit; i++)
		if_put(oif_map[i]);

	free(oif_map, M_IFADDR, olimit * sizeof(*oif_map));
	free(dtor, M_IFADDR, if_idxmap_usedidx_size(olimit));
}

void
if_idxmap_insert(struct ifnet *ifp)
{
	struct ifnet **if_map;
	unsigned int index = ifp->if_index;

	rw_enter_write(&if_idxmap.lock);

	if_map = SMR_PTR_GET_LOCKED(&if_idxmap.map);

	KASSERTMSG(index != 0 && index < if_idxmap_limit(if_map),
	    "%s(%p) index %u vs limit %u", ifp->if_xname, ifp, index,
	    if_idxmap_limit(if_map));
	KASSERT(SMR_PTR_GET_LOCKED(&if_map[index]) == NULL);
	KASSERT(isset(if_idxmap.usedidx, index));

	/* commit */
	SMR_PTR_SET_LOCKED(&if_map[index], if_ref(ifp));

	rw_exit_write(&if_idxmap.lock);
}

void
if_idxmap_remove(struct ifnet *ifp)
{
	struct ifnet **if_map;
	unsigned int index = ifp->if_index;

	rw_enter_write(&if_idxmap.lock);

	if_map = SMR_PTR_GET_LOCKED(&if_idxmap.map);

	KASSERT(index != 0 && index < if_idxmap_limit(if_map));
	KASSERT(SMR_PTR_GET_LOCKED(&if_map[index]) == ifp);
	KASSERT(isset(if_idxmap.usedidx, index));

	SMR_PTR_SET_LOCKED(&if_map[index], NULL);

	if_idxmap.count--;
	clrbit(if_idxmap.usedidx, index);
	/* end of if_idxmap modifications */

	rw_exit_write(&if_idxmap.lock);

	smr_barrier();
	if_put(ifp);
}

/*
 * Attach an interface to the
 * list of "active" interfaces.
 */
void
if_attachsetup(struct ifnet *ifp)
{
	unsigned long ifidx;

	NET_ASSERT_LOCKED();

	if_addgroup(ifp, IFG_ALL);

#ifdef INET6
	nd6_ifattach(ifp);
#endif

#if NPF > 0
	pfi_attach_ifnet(ifp);
#endif

	timeout_set(&ifp->if_slowtimo, if_slowtimo, ifp);
	if_slowtimo(ifp);

	if_idxmap_insert(ifp);
	KASSERT(if_get(0) == NULL);

	ifidx = ifp->if_index;

	task_set(&ifp->if_watchdogtask, if_watchdog_task, (void *)ifidx);
	task_set(&ifp->if_linkstatetask, if_linkstate_task, (void *)ifidx);

	/* Announce the interface. */
	rtm_ifannounce(ifp, IFAN_ARRIVAL);
}

/*
 * Allocate the link level name for the specified interface.  This
 * is an attachment helper.  It must be called after ifp->if_addrlen
 * is initialized, which may not be the case when if_attach() is
 * called.
 */
void
if_alloc_sadl(struct ifnet *ifp)
{
	unsigned int socksize;
	int namelen, masklen;
	struct sockaddr_dl *sdl;

	/*
	 * If the interface already has a link name, release it
	 * now.  This is useful for interfaces that can change
	 * link types, and thus switch link names often.
	 */
	if_free_sadl(ifp);

	namelen = strlen(ifp->if_xname);
	masklen = offsetof(struct sockaddr_dl, sdl_data[0]) + namelen;
	socksize = masklen + ifp->if_addrlen;
#define ROUNDUP(a) (1 + (((a) - 1) | (sizeof(long) - 1)))
	if (socksize < sizeof(*sdl))
		socksize = sizeof(*sdl);
	socksize = ROUNDUP(socksize);
	sdl = malloc(socksize, M_IFADDR, M_WAITOK|M_ZERO);
	sdl->sdl_len = socksize;
	sdl->sdl_family = AF_LINK;
	bcopy(ifp->if_xname, sdl->sdl_data, namelen);
	sdl->sdl_nlen = namelen;
	sdl->sdl_alen = ifp->if_addrlen;
	sdl->sdl_index = ifp->if_index;
	sdl->sdl_type = ifp->if_type;
	ifp->if_sadl = sdl;
}

/*
 * Free the link level name for the specified interface.  This is
 * a detach helper.  This is called from if_detach() or from
 * link layer type specific detach functions.
 */
void
if_free_sadl(struct ifnet *ifp)
{
	if (ifp->if_sadl == NULL)
		return;

	free(ifp->if_sadl, M_IFADDR, ifp->if_sadl->sdl_len);
	ifp->if_sadl = NULL;
}

void
if_attachhead(struct ifnet *ifp)
{
	if_attach_common(ifp);
	NET_LOCK();
	TAILQ_INSERT_HEAD(&ifnetlist, ifp, if_list);
	if_attachsetup(ifp);
	NET_UNLOCK();
}

void
if_attach(struct ifnet *ifp)
{
	if_attach_common(ifp);
	NET_LOCK();
	TAILQ_INSERT_TAIL(&ifnetlist, ifp, if_list);
	if_attachsetup(ifp);
	NET_UNLOCK();
}

void
if_attach_queues(struct ifnet *ifp, unsigned int nqs)
{
	struct ifqueue **map;
	struct ifqueue *ifq;
	int i;

	KASSERT(ifp->if_ifqs == ifp->if_snd.ifq_ifqs);
	KASSERT(nqs != 0);

	map = mallocarray(sizeof(*map), nqs, M_DEVBUF, M_WAITOK);

	ifp->if_snd.ifq_softc = NULL;
	map[0] = &ifp->if_snd;

	for (i = 1; i < nqs; i++) {
		ifq = malloc(sizeof(*ifq), M_DEVBUF, M_WAITOK|M_ZERO);
		ifq_init_maxlen(ifq, ifp->if_snd.ifq_maxlen);
		ifq_init(ifq, ifp, i);
		map[i] = ifq;
	}

	ifp->if_ifqs = map;
	ifp->if_nifqs = nqs;
}

void
if_attach_iqueues(struct ifnet *ifp, unsigned int niqs)
{
	struct ifiqueue **map;
	struct ifiqueue *ifiq;
	unsigned int i;

	KASSERT(niqs != 0);

	map = mallocarray(niqs, sizeof(*map), M_DEVBUF, M_WAITOK);

	ifp->if_rcv.ifiq_softc = NULL;
	map[0] = &ifp->if_rcv;

	for (i = 1; i < niqs; i++) {
		ifiq = malloc(sizeof(*ifiq), M_DEVBUF, M_WAITOK|M_ZERO);
		ifiq_init(ifiq, ifp, i);
		map[i] = ifiq;
	}

	ifp->if_iqs = map;
	ifp->if_niqs = niqs;
}

void
if_attach_common(struct ifnet *ifp)
{
	KASSERT(ifp->if_ioctl != NULL);

	TAILQ_INIT(&ifp->if_addrlist);
	TAILQ_INIT(&ifp->if_maddrlist);
	TAILQ_INIT(&ifp->if_groups);

	if (!ISSET(ifp->if_xflags, IFXF_MPSAFE)) {
		KASSERTMSG(ifp->if_qstart == NULL,
		    "%s: if_qstart set without MPSAFE set", ifp->if_xname);
		ifp->if_qstart = if_qstart_compat;
	} else {
		KASSERTMSG(ifp->if_start == NULL,
		    "%s: if_start set with MPSAFE set", ifp->if_xname);
		KASSERTMSG(ifp->if_qstart != NULL,
		    "%s: if_qstart not set with MPSAFE set", ifp->if_xname);
	}

	if_idxmap_alloc(ifp);

	ifq_init(&ifp->if_snd, ifp, 0);

	ifp->if_snd.ifq_ifqs[0] = &ifp->if_snd;
	ifp->if_ifqs = ifp->if_snd.ifq_ifqs;
	ifp->if_nifqs = 1;
	if (ifp->if_txmit == 0)
		ifp->if_txmit = IF_TXMIT_DEFAULT;

	ifiq_init(&ifp->if_rcv, ifp, 0);

	ifp->if_rcv.ifiq_ifiqs[0] = &ifp->if_rcv;
	ifp->if_iqs = ifp->if_rcv.ifiq_ifiqs;
	ifp->if_niqs = 1;

	TAILQ_INIT(&ifp->if_addrhooks);
	TAILQ_INIT(&ifp->if_linkstatehooks);
	TAILQ_INIT(&ifp->if_detachhooks);

	if (ifp->if_rtrequest == NULL)
		ifp->if_rtrequest = if_rtrequest_dummy;
	if (ifp->if_enqueue == NULL)
		ifp->if_enqueue = if_enqueue_ifq;
#if NBPFILTER > 0
	if (ifp->if_bpf_mtap == NULL)
		ifp->if_bpf_mtap = bpf_mtap_ether;
#endif
	ifp->if_llprio = IFQ_DEFPRIO;
}

void
if_attach_ifq(struct ifnet *ifp, const struct ifq_ops *newops, void *args)
{
	/*
	 * only switch the ifq_ops on the first ifq on an interface.
	 *
	 * the only ifq_ops we provide priq and hfsc, and hfsc only
	 * works on a single ifq. because the code uses the ifq_ops
	 * on the first ifq (if_snd) to select a queue for an mbuf,
	 * by switching only the first one we change both the algorithm
	 * and force the routing of all new packets to it.
	 */
	ifq_attach(&ifp->if_snd, newops, args);
}

void
if_start(struct ifnet *ifp)
{
	KASSERT(ifp->if_qstart == if_qstart_compat);
	if_qstart_compat(&ifp->if_snd);
}
void
if_qstart_compat(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	int s;

	/*
	 * the stack assumes that an interface can have multiple
	 * transmit rings, but a lot of drivers are still written
	 * so that interfaces and send rings have a 1:1 mapping.
	 * this provides compatibility between the stack and the older
	 * drivers by translating from the only queue they have
	 * (ifp->if_snd) back to the interface and calling if_start.
	 */

	KERNEL_LOCK();
	s = splnet();
	(*ifp->if_start)(ifp);
	splx(s);
	KERNEL_UNLOCK();
}

int
if_enqueue(struct ifnet *ifp, struct mbuf *m)
{
	CLR(m->m_pkthdr.csum_flags, M_TIMESTAMP);

#if NPF > 0
	if (m->m_pkthdr.pf.delay > 0)
		return (pf_delay_pkt(m, ifp->if_index));
#endif

#if NBRIDGE > 0
	if (ifp->if_bridgeidx && (m->m_flags & M_PROTO1) == 0) {
		int error;

		error = bridge_enqueue(ifp, m);
		return (error);
	}
#endif

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif	/* NPF > 0 */

	return ((*ifp->if_enqueue)(ifp, m));
}

int
if_enqueue_ifq(struct ifnet *ifp, struct mbuf *m)
{
	struct ifqueue *ifq = &ifp->if_snd;
	int error;

	if (ifp->if_nifqs > 1) {
		unsigned int idx;

		/*
		 * use the operations on the first ifq to pick which of
		 * the array gets this mbuf.
		 */

		idx = ifq_idx(&ifp->if_snd, ifp->if_nifqs, m);
		ifq = ifp->if_ifqs[idx];
	}

	error = ifq_enqueue(ifq, m);
	if (error)
		return (error);

	ifq_start(ifq);

	return (0);
}

void
if_input(struct ifnet *ifp, struct mbuf_list *ml)
{
	ifiq_input(&ifp->if_rcv, ml);
}

int
if_input_local(struct ifnet *ifp, struct mbuf *m, sa_family_t af,
    struct netstack *ns)
{
	int keepflags, keepcksum;
	uint16_t keepmss;
	uint16_t keepflowid;

#if NBPFILTER > 0
	/*
	 * Only send packets to bpf if they are destined to local
	 * addresses.
	 *
	 * if_input_local() is also called for SIMPLEX interfaces to
	 * duplicate packets for local use.  But don't dup them to bpf.
	 */
	if (ifp->if_flags & IFF_LOOPBACK) {
		caddr_t if_bpf = ifp->if_bpf;

		if (if_bpf)
			bpf_mtap_af(if_bpf, af, m, BPF_DIRECTION_OUT);
	}
#endif
	keepflags = m->m_flags & (M_BCAST|M_MCAST);
	/*
	 * Preserve outgoing checksum flags, in case the packet is
	 * forwarded to another interface.  Then the checksum, which
	 * is now incorrect, will be calculated before sending.
	 */
	keepcksum = m->m_pkthdr.csum_flags & (M_IPV4_CSUM_OUT |
	    M_TCP_CSUM_OUT | M_UDP_CSUM_OUT | M_ICMP_CSUM_OUT |
	    M_TCP_TSO | M_FLOWID);
	keepmss = m->m_pkthdr.ph_mss;
	keepflowid = m->m_pkthdr.ph_flowid;
	m_resethdr(m);
	m->m_flags |= M_LOOP | keepflags;
	m->m_pkthdr.csum_flags = keepcksum;
	m->m_pkthdr.ph_mss = keepmss;
	m->m_pkthdr.ph_flowid = keepflowid;
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

	if (ISSET(keepcksum, M_TCP_TSO) && m->m_pkthdr.len > ifp->if_mtu) {
		if (ifp->if_mtu > 0 &&
		    ((af == AF_INET &&
		    ISSET(ifp->if_capabilities, IFCAP_TSOv4)) ||
		    (af == AF_INET6 &&
		    ISSET(ifp->if_capabilities, IFCAP_TSOv6)))) {
			tcpstat_inc(tcps_inhwlro);
		} else {
			tcpstat_inc(tcps_inbadlro);
			m_freem(m);
			return (EPROTONOSUPPORT);
		}
	}

	if (ISSET(keepcksum, M_TCP_CSUM_OUT))
		m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
	if (ISSET(keepcksum, M_UDP_CSUM_OUT))
		m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
	if (ISSET(keepcksum, M_ICMP_CSUM_OUT))
		m->m_pkthdr.csum_flags |= M_ICMP_CSUM_IN_OK;

	/* do not count multicast loopback and simplex interfaces */
	if (ISSET(ifp->if_flags, IFF_LOOPBACK)) {
		counters_pkt(ifp->if_counters, ifc_opackets, ifc_obytes,
		    m->m_pkthdr.len);
	}

	switch (af) {
	case AF_INET:
		if (ISSET(keepcksum, M_IPV4_CSUM_OUT))
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
		ipv4_input(ifp, m, ns);
		break;
#ifdef INET6
	case AF_INET6:
		ipv6_input(ifp, m, ns);
		break;
#endif /* INET6 */
#ifdef MPLS
	case AF_MPLS:
		mpls_input(ifp, m, ns);
		break;
#endif /* MPLS */
	default:
		printf("%s: can't handle af%d\n", ifp->if_xname, af);
		m_freem(m);
		return (EAFNOSUPPORT);
	}

	return (0);
}

int
if_output_ml(struct ifnet *ifp, struct mbuf_list *ml,
    struct sockaddr *dst, struct rtentry *rt)
{
	struct mbuf *m;
	int error = 0;

	while ((m = ml_dequeue(ml)) != NULL) {
		error = ifp->if_output(ifp, m, dst, rt);
		if (error)
			break;
	}
	if (error)
		ml_purge(ml);

	return error;
}

int
if_output_tso(struct ifnet *ifp, struct mbuf **mp, struct sockaddr *dst,
    struct rtentry *rt, u_int mtu)
{
	uint32_t ifcap;
	int error;

	switch (dst->sa_family) {
	case AF_INET:
		ifcap = IFCAP_TSOv4;
		break;
#ifdef INET6
	case AF_INET6:
		ifcap = IFCAP_TSOv6;
		break;
#endif
	default:
		unhandled_af(dst->sa_family);
	}

	/*
	 * Try to send with TSO first.  When forwarding LRO may set
	 * maximum segment size in mbuf header.  Chop TCP segment
	 * even if it would fit interface MTU to preserve maximum
	 * path MTU.
	 */
	error = tcp_if_output_tso(ifp, mp, dst, rt, ifcap, mtu);
	if (error || *mp == NULL)
		return error;

	if ((*mp)->m_pkthdr.len <= mtu) {
		switch (dst->sa_family) {
		case AF_INET:
			in_hdr_cksum_out(*mp, ifp);
			in_proto_cksum_out(*mp, ifp);
			break;
#ifdef INET6
		case AF_INET6:
			in6_proto_cksum_out(*mp, ifp);
			break;
#endif
		}
		error = ifp->if_output(ifp, *mp, dst, rt);
		*mp = NULL;
		return error;
	}

	/* mp still contains mbuf that has to be fragmented or dropped. */
	return 0;
}

int
if_output_mq(struct ifnet *ifp, struct mbuf_queue *mq, unsigned int *total,
    struct sockaddr *dst, struct rtentry *rt)
{
	struct mbuf_list ml;
	unsigned int len;
	int error;

	mq_delist(mq, &ml);
	len = ml_len(&ml);
	error = if_output_ml(ifp, &ml, dst, rt);

	/* XXXSMP we also discard if other CPU enqueues */
	if (mq_len(mq) > 0) {
		/* mbuf is back in queue. Discard. */
		atomic_sub_int(total, len + mq_purge(mq));
	} else
		atomic_sub_int(total, len);

	return error;
}

int
if_output_local(struct ifnet *ifp, struct mbuf *m, sa_family_t af)
{
	struct ifiqueue *ifiq;
	unsigned int flow = 0;

	m->m_pkthdr.ph_family = af;
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

	if (ISSET(m->m_pkthdr.csum_flags, M_FLOWID))
		flow = m->m_pkthdr.ph_flowid;

	ifiq = ifp->if_iqs[flow % ifp->if_niqs];

	return (ifiq_enqueue_qlim(ifiq, m, 8192) == 0 ? 0 : ENOBUFS);
}

void
if_input_process(struct ifnet *ifp, struct mbuf_list *ml, unsigned int idx)
{
	struct mbuf *m;
	struct softnet *sn;

	if (ml_empty(ml))
		return;

	if (!ISSET(ifp->if_xflags, IFXF_CLONED))
		enqueue_randomness(ml_len(ml) ^ (uintptr_t)MBUF_LIST_FIRST(ml));

	/*
	 * We grab the shared netlock for packet processing in the softnet
	 * threads.  Packets can regrab the exclusive lock via queues.
	 * ioctl, sysctl, and socket syscall may use shared lock if access is
	 * read only or MP safe.  Usually they hold the exclusive net lock.
	 */

	sn = net_sn(idx);
	ml_init(&sn->sn_netstack.ns_tcp_ml);
#ifdef INET6
	ml_init(&sn->sn_netstack.ns_tcp6_ml);
#endif

	NET_LOCK_SHARED();

	while ((m = ml_dequeue(ml)) != NULL)
		(*ifp->if_input)(ifp, m, &sn->sn_netstack);

	tcp_input_mlist(&sn->sn_netstack.ns_tcp_ml, AF_INET);
#ifdef INET6
	tcp_input_mlist(&sn->sn_netstack.ns_tcp6_ml, AF_INET6);
#endif

	NET_UNLOCK_SHARED();
}

void
if_vinput(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

	counters_pkt(ifp->if_counters,
	    ifc_ipackets, ifc_ibytes, m->m_pkthdr.len);

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

#if NBPFILTER > 0
	if_bpf = ifp->if_bpf;
	if (if_bpf) {
		if ((*ifp->if_bpf_mtap)(if_bpf, m, BPF_DIRECTION_IN)) {
			m_freem(m);
			return;
		}
	}
#endif

	if (__predict_true(!ISSET(ifp->if_xflags, IFXF_MONITOR)))
		(*ifp->if_input)(ifp, m, ns);
	else
		m_freem(m);
}

void
if_netisr(void *unused)
{
	int n, t = 0;

	NET_LOCK();

	while ((n = netisr) != 0) {
		/* Like sched_pause() but with a rwlock dance. */
		if (curcpu()->ci_schedstate.spc_schedflags & SPCF_SHOULDYIELD) {
			NET_UNLOCK();
			yield();
			NET_LOCK();
		}

		atomic_clearbits_int(&netisr, n);

#if NETHER > 0
		if (n & (1 << NETISR_ARP))
			arpintr();
#endif
		if (n & (1 << NETISR_IP))
			ipintr();
#ifdef INET6
		if (n & (1 << NETISR_IPV6))
			ip6intr();
#endif
#if NPPP > 0
		if (n & (1 << NETISR_PPP)) {
			KERNEL_LOCK();
			pppintr();
			KERNEL_UNLOCK();
		}
#endif
#if NBRIDGE > 0
		if (n & (1 << NETISR_BRIDGE))
			bridgeintr();
#endif
#ifdef PIPEX
		if (n & (1 << NETISR_PIPEX))
			pipexintr();
#endif
#if NPPPOE > 0
		if (n & (1 << NETISR_PPPOE)) {
			KERNEL_LOCK();
			pppoeintr();
			KERNEL_UNLOCK();
		}
#endif
		t |= n;
	}

	NET_UNLOCK();
}

void
if_hooks_run(struct task_list *hooks)
{
	struct task *t, *nt;
	struct task cursor = { .t_func = NULL };
	void (*func)(void *);
	void *arg;

	mtx_enter(&if_hooks_mtx);
	for (t = TAILQ_FIRST(hooks); t != NULL; t = nt) {
		if (t->t_func == NULL) { /* skip cursors */
			nt = TAILQ_NEXT(t, t_entry);
			continue;
		}
		func = t->t_func;
		arg = t->t_arg;

		TAILQ_INSERT_AFTER(hooks, t, &cursor, t_entry);
		mtx_leave(&if_hooks_mtx);

		(*func)(arg);

		mtx_enter(&if_hooks_mtx);
		nt = TAILQ_NEXT(&cursor, t_entry); /* avoid _Q_INVALIDATE */
		TAILQ_REMOVE(hooks, &cursor, t_entry);
	}
	mtx_leave(&if_hooks_mtx);
}

void
if_remove(struct ifnet *ifp)
{
	/* Remove the interface from the list of all interfaces. */
	NET_LOCK();
	TAILQ_REMOVE(&ifnetlist, ifp, if_list);
	NET_UNLOCK();

	/* Remove the interface from the interface index map. */
	if_idxmap_remove(ifp);

	/* Sleep until the last reference is released. */
	refcnt_finalize(&ifp->if_refcnt, "ifrm");
}

void
if_deactivate(struct ifnet *ifp)
{
	/*
	 * Call detach hooks from head to tail.  To make sure detach
	 * hooks are executed in the reverse order they were added, all
	 * the hooks have to be added to the head!
	 */

	NET_LOCK();
	if_hooks_run(&ifp->if_detachhooks);
	NET_UNLOCK();
}

void
if_detachhook_add(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_INSERT_HEAD(&ifp->if_detachhooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

void
if_detachhook_del(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_REMOVE(&ifp->if_detachhooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

/*
 * Detach an interface from everything in the kernel.  Also deallocate
 * private resources.
 */
void
if_detach(struct ifnet *ifp)
{
	struct ifaddr *ifa;
	struct ifg_list *ifg;
	int i, s;

	/* Undo pseudo-driver changes. */
	if_deactivate(ifp);

	/* Other CPUs must not have a reference before we start destroying. */
	if_remove(ifp);

	ifp->if_qstart = if_detached_qstart;

	/* Wait until the start routines finished. */
	for (i = 0; i < ifp->if_nifqs; i++) {
		ifq_barrier(ifp->if_ifqs[i]);
		ifq_clr_oactive(ifp->if_ifqs[i]);
	}

#if NBPFILTER > 0
	bpfdetach(ifp);
#endif

	NET_LOCK();
	s = splnet();
	ifp->if_ioctl = if_detached_ioctl;
	ifp->if_watchdog = NULL;

	/* Remove the watchdog timeout & task */
	timeout_del(&ifp->if_slowtimo);
	task_del(net_tq(ifp->if_index), &ifp->if_watchdogtask);

	/* Remove the link state task */
	task_del(net_tq(ifp->if_index), &ifp->if_linkstatetask);

	rti_delete(ifp);
#if NETHER > 0 && defined(NFSCLIENT)
	if (ifp->if_index == revarp_ifidx)
		revarp_ifidx = 0;
#endif
#ifdef MROUTING
	vif_delete(ifp);
#endif
	in_ifdetach(ifp);
#ifdef INET6
	in6_ifdetach(ifp);
#endif
#if NPF > 0
	pfi_detach_ifnet(ifp);
#endif

	while ((ifg = TAILQ_FIRST(&ifp->if_groups)) != NULL)
		if_delgroup(ifp, ifg->ifgl_group->ifg_group);

	if_free_sadl(ifp);

	/* We should not have any address left at this point. */
	if (!TAILQ_EMPTY(&ifp->if_addrlist)) {
#ifdef DIAGNOSTIC
		printf("%s: address list non empty\n", ifp->if_xname);
#endif
		while ((ifa = TAILQ_FIRST(&ifp->if_addrlist)) != NULL) {
			ifa_del(ifp, ifa);
			ifa->ifa_ifp = NULL;
			ifafree(ifa);
		}
	}
	splx(s);
	NET_UNLOCK();

	KASSERT(TAILQ_EMPTY(&ifp->if_addrhooks));
	KASSERT(TAILQ_EMPTY(&ifp->if_linkstatehooks));
	KASSERT(TAILQ_EMPTY(&ifp->if_detachhooks));

#ifdef INET6
	nd6_ifdetach(ifp);
#endif

	/* Announce that the interface is gone. */
	rtm_ifannounce(ifp, IFAN_DEPARTURE);

	if (ifp->if_counters != NULL)
		if_counters_free(ifp);

	for (i = 0; i < ifp->if_nifqs; i++)
		ifq_destroy(ifp->if_ifqs[i]);
	if (ifp->if_ifqs != ifp->if_snd.ifq_ifqs) {
		for (i = 1; i < ifp->if_nifqs; i++) {
			free(ifp->if_ifqs[i], M_DEVBUF,
			    sizeof(struct ifqueue));
		}
		free(ifp->if_ifqs, M_DEVBUF,
		    sizeof(struct ifqueue *) * ifp->if_nifqs);
	}

	for (i = 0; i < ifp->if_niqs; i++)
		ifiq_destroy(ifp->if_iqs[i]);
	if (ifp->if_iqs != ifp->if_rcv.ifiq_ifiqs) {
		for (i = 1; i < ifp->if_niqs; i++) {
			free(ifp->if_iqs[i], M_DEVBUF,
			    sizeof(struct ifiqueue));
		}
		free(ifp->if_iqs, M_DEVBUF,
		    sizeof(struct ifiqueue *) * ifp->if_niqs);
	}
}

/*
 * Returns true if ``ifp0'' is connected to the interface with index ``ifidx''.
 */
int
if_isconnected(const struct ifnet *ifp0, unsigned int ifidx)
{
	struct ifnet *ifp;
	int connected = 0;

	ifp = if_get(ifidx);
	if (ifp == NULL)
		return (0);

	if (ifp0->if_index == ifp->if_index)
		connected = 1;

#if NBRIDGE > 0
	if (ifp0->if_bridgeidx != 0 && ifp0->if_bridgeidx == ifp->if_bridgeidx)
		connected = 1;
#endif
#if NCARP > 0
	if ((ifp0->if_type == IFT_CARP &&
	    ifp0->if_carpdevidx == ifp->if_index) ||
	    (ifp->if_type == IFT_CARP && ifp->if_carpdevidx == ifp0->if_index))
		connected = 1;
#endif

	if_put(ifp);
	return (connected);
}

/*
 * Create a clone network interface.
 */
int
if_clone_create(const char *name, int rdomain)
{
	struct if_clone *ifc;
	struct ifnet *ifp;
	int unit, ret;

	ifc = if_clone_lookup(name, &unit);
	if (ifc == NULL)
		return (EINVAL);

	rw_enter_write(&if_cloners_lock);

	if ((ifp = if_unit(name)) != NULL) {
		ret = EEXIST;
		goto unlock;
	}

	ret = (*ifc->ifc_create)(ifc, unit);

	if (ret != 0 || (ifp = if_unit(name)) == NULL)
		goto unlock;

	NET_LOCK();
	if_addgroup(ifp, ifc->ifc_name);
	if (rdomain != 0)
		if_setrdomain(ifp, rdomain);
	NET_UNLOCK();
unlock:
	rw_exit_write(&if_cloners_lock);
	if_put(ifp);

	return (ret);
}

/*
 * Destroy a clone network interface.
 */
int
if_clone_destroy(const char *name)
{
	struct if_clone *ifc;
	struct ifnet *ifp;
	int ret;

	ifc = if_clone_lookup(name, NULL);
	if (ifc == NULL)
		return (EINVAL);

	if (ifc->ifc_destroy == NULL)
		return (EOPNOTSUPP);

	rw_enter_write(&if_cloners_lock);

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (strcmp(ifp->if_xname, name) == 0)
			break;
	}
	if (ifp == NULL) {
		rw_exit_write(&if_cloners_lock);
		return (ENXIO);
	}

	NET_LOCK();
	if (ifp->if_flags & IFF_UP) {
		int s;
		s = splnet();
		if_down(ifp);
		splx(s);
	}
	NET_UNLOCK();
	ret = (*ifc->ifc_destroy)(ifp);

	rw_exit_write(&if_cloners_lock);

	return (ret);
}

/*
 * Look up a network interface cloner.
 */
struct if_clone *
if_clone_lookup(const char *name, int *unitp)
{
	struct if_clone *ifc;
	const char *cp;
	int unit;

	/* separate interface name from unit */
	for (cp = name;
	    cp - name < IFNAMSIZ && *cp && (*cp < '0' || *cp > '9');
	    cp++)
		continue;

	if (cp == name || cp - name == IFNAMSIZ || !*cp)
		return (NULL);	/* No name or unit number */

	if (cp - name < IFNAMSIZ-1 && *cp == '0' && cp[1] != '\0')
		return (NULL);	/* unit number 0 padded */

	LIST_FOREACH(ifc, &if_cloners, ifc_list) {
		if (strlen(ifc->ifc_name) == cp - name &&
		    !strncmp(name, ifc->ifc_name, cp - name))
			break;
	}

	if (ifc == NULL)
		return (NULL);

	unit = 0;
	while (cp - name < IFNAMSIZ && *cp) {
		if (*cp < '0' || *cp > '9' ||
		    unit > (INT_MAX - (*cp - '0')) / 10) {
			/* Bogus unit number. */
			return (NULL);
		}
		unit = (unit * 10) + (*cp++ - '0');
	}

	if (unitp != NULL)
		*unitp = unit;
	return (ifc);
}

/*
 * Register a network interface cloner.
 */
void
if_clone_attach(struct if_clone *ifc)
{
	/*
	 * we are called at kernel boot by main(), when pseudo devices are
	 * being attached. The main() is the only guy which may alter the
	 * if_cloners. While system is running and main() is done with
	 * initialization, the if_cloners becomes immutable.
	 */
	KASSERT(pdevinit_done == 0);
	LIST_INSERT_HEAD(&if_cloners, ifc, ifc_list);
	if_cloners_count++;
}

/*
 * Provide list of interface cloners to userspace.
 */
int
if_clone_list(struct if_clonereq *ifcr)
{
	char outbuf[IFNAMSIZ], *dst;
	struct if_clone *ifc;
	int count, error = 0;

	if ((dst = ifcr->ifcr_buffer) == NULL) {
		/* Just asking how many there are. */
		ifcr->ifcr_total = if_cloners_count;
		return (0);
	}

	if (ifcr->ifcr_count < 0)
		return (EINVAL);

	ifcr->ifcr_total = if_cloners_count;
	count = MIN(if_cloners_count, ifcr->ifcr_count);

	LIST_FOREACH(ifc, &if_cloners, ifc_list) {
		if (count == 0)
			break;
		bzero(outbuf, sizeof outbuf);
		strlcpy(outbuf, ifc->ifc_name, IFNAMSIZ);
		error = copyout(outbuf, dst, IFNAMSIZ);
		if (error)
			break;
		count--;
		dst += IFNAMSIZ;
	}

	return (error);
}

/*
 * set queue congestion marker
 */
void
if_congestion(void)
{
	extern int ticks;

	ifq_congestion = ticks;
}

int
if_congested(void)
{
	extern int ticks;
	int diff;

	diff = ticks - ifq_congestion;
	if (diff < 0) {
		ifq_congestion = ticks - hz;
		return (0);
	}

	return (diff <= (hz / 100));
}

#define	equal(a1, a2)	\
	(bcmp((caddr_t)(a1), (caddr_t)(a2),	\
	(a1)->sa_len) == 0)

/*
 * Locate an interface based on a complete address.
 */
struct ifaddr *
ifa_ifwithaddr(const struct sockaddr *addr, u_int rtableid)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;
	u_int rdomain;

	NET_ASSERT_LOCKED();

	rdomain = rtable_l2(rtableid);
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rdomain)
			continue;

		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;

			if (equal(addr, ifa->ifa_addr)) {
				return (ifa);
			}
		}
	}
	return (NULL);
}

/*
 * Locate the point to point interface with a given destination address.
 */
struct ifaddr *
ifa_ifwithdstaddr(const struct sockaddr *addr, u_int rdomain)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	NET_ASSERT_LOCKED();

	rdomain = rtable_l2(rdomain);
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (ifp->if_rdomain != rdomain)
			continue;
		if (ifp->if_flags & IFF_POINTOPOINT) {
			TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
				if (ifa->ifa_addr->sa_family !=
				    addr->sa_family || ifa->ifa_dstaddr == NULL)
					continue;
				if (equal(addr, ifa->ifa_dstaddr)) {
					return (ifa);
				}
			}
		}
	}
	return (NULL);
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
	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr->sa_family != af)
			continue;
		if (ifa_maybe == NULL)
			ifa_maybe = ifa;
		if (ifa->ifa_netmask == 0 || ifp->if_flags & IFF_POINTOPOINT) {
			if (equal(addr, ifa->ifa_addr) ||
			    (ifa->ifa_dstaddr && equal(addr, ifa->ifa_dstaddr)))
				return (ifa);
			continue;
		}
		cp = addr->sa_data;
		cp2 = ifa->ifa_addr->sa_data;
		cp3 = ifa->ifa_netmask->sa_data;
		cplim = ifa->ifa_netmask->sa_len + (char *)ifa->ifa_netmask;
		for (; cp3 < cplim; cp3++)
			if ((*cp++ ^ *cp2++) & *cp3)
				break;
		if (cp3 == cplim)
			return (ifa);
	}
	return (ifa_maybe);
}

void
if_rtrequest_dummy(struct ifnet *ifp, int req, struct rtentry *rt)
{
}

/*
 * Default action when installing a local route on a point-to-point
 * interface.
 */
void
p2p_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	struct ifnet *lo0ifp;
	struct ifaddr *ifa, *lo0ifa;

	switch (req) {
	case RTM_ADD:
		if (!ISSET(rt->rt_flags, RTF_LOCAL))
			break;

		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (memcmp(rt_key(rt), ifa->ifa_addr,
			    rt_key(rt)->sa_len) == 0)
				break;
		}

		if (ifa == NULL)
			break;

		KASSERT(ifa == rt->rt_ifa);

		lo0ifp = if_get(rtable_loindex(ifp->if_rdomain));
		KASSERT(lo0ifp != NULL);
		TAILQ_FOREACH(lo0ifa, &lo0ifp->if_addrlist, ifa_list) {
			if (lo0ifa->ifa_addr->sa_family ==
			    ifa->ifa_addr->sa_family)
				break;
		}
		if_put(lo0ifp);

		if (lo0ifa == NULL)
			break;

		rt->rt_flags &= ~RTF_LLINFO;
		break;
	case RTM_DELETE:
	case RTM_RESOLVE:
	default:
		break;
	}
}

int
p2p_bpf_mtap(caddr_t if_bpf, const struct mbuf *m, u_int dir)
{
#if NBPFILTER > 0
	return (bpf_mtap_af(if_bpf, m->m_pkthdr.ph_family, m, dir));
#else
	return (0);
#endif
}

void
p2p_input(struct ifnet *ifp, struct mbuf *m, struct netstack *ns)
{
	void (*input)(struct ifnet *, struct mbuf *, struct netstack *);

	switch (m->m_pkthdr.ph_family) {
	case AF_INET:
		input = ipv4_input;
		break;
#ifdef INET6
	case AF_INET6:
		input = ipv6_input;
		break;
#endif
#ifdef MPLS
	case AF_MPLS:
		input = mpls_input;
		break;
#endif
	default:
		m_freem(m);
		return;
	}

	(*input)(ifp, m, ns);
}

/*
 * Bring down all interfaces
 */
void
if_downall(void)
{
	struct ifreq ifrq;	/* XXX only partly built */
	struct ifnet *ifp;

	NET_LOCK();
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if ((ifp->if_flags & IFF_UP) == 0)
			continue;
		if_down(ifp);
		ifrq.ifr_flags = ifp->if_flags;
		(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifrq);
	}
	NET_UNLOCK();
}

/*
 * Mark an interface down and notify protocols of
 * the transition.
 */
void
if_down(struct ifnet *ifp)
{
	NET_ASSERT_LOCKED();

	ifp->if_flags &= ~IFF_UP;
	getmicrotime(&ifp->if_lastchange);
	ifq_purge(&ifp->if_snd);

	if_linkstate(ifp);
}

/*
 * Mark an interface up and notify protocols of
 * the transition.
 */
void
if_up(struct ifnet *ifp)
{
	NET_ASSERT_LOCKED();

	ifp->if_flags |= IFF_UP;
	getmicrotime(&ifp->if_lastchange);

#ifdef INET6
	/* Userland expects the kernel to set ::1 on default lo(4). */
	if (ifp->if_index == rtable_loindex(ifp->if_rdomain))
		in6_ifattach(ifp);
#endif

	if_linkstate(ifp);
}

/*
 * Notify userland, the routing table and hooks owner of
 * a link-state transition.
 */
void
if_linkstate_task(void *xifidx)
{
	unsigned int ifidx = (unsigned long)xifidx;
	struct ifnet *ifp;

	NET_LOCK();
	KERNEL_LOCK();

	ifp = if_get(ifidx);
	if (ifp != NULL)
		if_linkstate(ifp);
	if_put(ifp);

	KERNEL_UNLOCK();
	NET_UNLOCK();
}

void
if_linkstate(struct ifnet *ifp)
{
	NET_ASSERT_LOCKED();

	if (panicstr == NULL) {
		rtm_ifchg(ifp);
		rt_if_track(ifp);
	}

	if_hooks_run(&ifp->if_linkstatehooks);
}

void
if_linkstatehook_add(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_INSERT_HEAD(&ifp->if_linkstatehooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

void
if_linkstatehook_del(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_REMOVE(&ifp->if_linkstatehooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

/*
 * Schedule a link state change task.
 */
void
if_link_state_change(struct ifnet *ifp)
{
	task_add(net_tq(ifp->if_index), &ifp->if_linkstatetask);
}

/*
 * Handle interface watchdog timer routine.  Called
 * from softclock, we decrement timer (if set) and
 * call the appropriate interface routine on expiration.
 */
void
if_slowtimo(void *arg)
{
	struct ifnet *ifp = arg;
	int s = splnet();

	if (ifp->if_watchdog) {
		if (ifp->if_timer > 0 && --ifp->if_timer == 0)
			task_add(net_tq(ifp->if_index), &ifp->if_watchdogtask);
		timeout_add_sec(&ifp->if_slowtimo, IFNET_SLOWTIMO);
	}
	splx(s);
}

void
if_watchdog_task(void *xifidx)
{
	unsigned int ifidx = (unsigned long)xifidx;
	struct ifnet *ifp;
	int s;

	ifp = if_get(ifidx);
	if (ifp == NULL)
		return;

	KERNEL_LOCK();
	s = splnet();
	if (ifp->if_watchdog)
		(*ifp->if_watchdog)(ifp);
	splx(s);
	KERNEL_UNLOCK();

	if_put(ifp);
}

/*
 * Map interface name to interface structure pointer.
 */
struct ifnet *
if_unit(const char *name)
{
	struct ifnet *ifp;

	KERNEL_ASSERT_LOCKED();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (strcmp(ifp->if_xname, name) == 0) {
			if_ref(ifp);
			return (ifp);
		}
	}

	return (NULL);
}

/*
 * Map interface index to interface structure pointer.
 */
struct ifnet *
if_get(unsigned int index)
{
	struct ifnet **if_map;
	struct ifnet *ifp = NULL;

	if (index == 0)
		return (NULL);

	smr_read_enter();
	if_map = SMR_PTR_GET(&if_idxmap.map);
	if (index < if_idxmap_limit(if_map)) {
		ifp = SMR_PTR_GET(&if_map[index]);
		if (ifp != NULL) {
			KASSERT(ifp->if_index == index);
			if_ref(ifp);
		}
	}
	smr_read_leave();

	return (ifp);
}

struct ifnet *
if_ref(struct ifnet *ifp)
{
	refcnt_take(&ifp->if_refcnt);

	return (ifp);
}

void
if_put(struct ifnet *ifp)
{
	if (ifp == NULL)
		return;

	refcnt_rele_wake(&ifp->if_refcnt);
}

int
if_setlladdr(struct ifnet *ifp, const uint8_t *lladdr)
{
	if (ifp->if_sadl == NULL)
		return (EINVAL);

	memcpy(((struct arpcom *)ifp)->ac_enaddr, lladdr, ETHER_ADDR_LEN);
	memcpy(LLADDR(ifp->if_sadl), lladdr, ETHER_ADDR_LEN);

	return (0);
}

int
if_createrdomain(int rdomain, struct ifnet *ifp)
{
	int error;
	struct ifnet *loifp;
	char loifname[IFNAMSIZ];
	unsigned int unit = rdomain;

	if ((error = rtable_add(rdomain)) != 0)
		return (error);
	if (!rtable_empty(rdomain))
		return (EEXIST);

	/* Create rdomain including its loopback if with unit == rdomain */
	snprintf(loifname, sizeof(loifname), "lo%u", unit);
	error = if_clone_create(loifname, 0);
	if ((loifp = if_unit(loifname)) == NULL)
		return (ENXIO);
	if (error && (ifp != loifp || error != EEXIST)) {
		if_put(loifp);
		return (error);
	}

	rtable_l2set(rdomain, rdomain, loifp->if_index);
	loifp->if_rdomain = rdomain;
	if_put(loifp);

	return (0);
}

int
if_setrdomain(struct ifnet *ifp, int rdomain)
{
	struct ifreq ifr;
	int error, up = 0, s;

	if (rdomain < 0 || rdomain > RT_TABLEID_MAX)
		return (EINVAL);

	if (rdomain != ifp->if_rdomain &&
	    (ifp->if_flags & IFF_LOOPBACK) &&
	    (ifp->if_index == rtable_loindex(ifp->if_rdomain)))
		return (EPERM);

	if (!rtable_exists(rdomain))
		return (ESRCH);

	/* make sure that the routing table is a real rdomain */
	if (rdomain != rtable_l2(rdomain))
		return (EINVAL);

	if (rdomain != ifp->if_rdomain) {
		s = splnet();
		/*
		 * We are tearing down the world.
		 * Take down the IF so:
		 * 1. everything that cares gets a message
		 * 2. the automagic IPv6 bits are recreated
		 */
		if (ifp->if_flags & IFF_UP) {
			up = 1;
			if_down(ifp);
		}
		rti_delete(ifp);
#ifdef MROUTING
		vif_delete(ifp);
#endif
		in_ifdetach(ifp);
#ifdef INET6
		in6_ifdetach(ifp);
#endif
		splx(s);
	}

	/* Let devices like enc(4) or mpe(4) know about the change */
	ifr.ifr_rdomainid = rdomain;
	if ((error = (*ifp->if_ioctl)(ifp, SIOCSIFRDOMAIN,
	    (caddr_t)&ifr)) != ENOTTY)
		return (error);
	error = 0;

	/* Add interface to the specified rdomain */
	ifp->if_rdomain = rdomain;

	/* If we took down the IF, bring it back */
	if (up) {
		s = splnet();
		if_up(ifp);
		splx(s);
	}

	return (0);
}

/*
 * Interface ioctls.
 */
int
ifioctl(struct socket *so, u_long cmd, caddr_t data, struct proc *p)
{
	struct ifnet *ifp;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifgroupreq *ifgr = (struct ifgroupreq *)data;
	struct if_afreq *ifar = (struct if_afreq *)data;
	char ifdescrbuf[IFDESCRSIZE];
	char ifrtlabelbuf[RTLABEL_LEN];
	int s, error = 0, oif_xflags;
	size_t bytesdone;
	unsigned short oif_flags;

	switch (cmd) {
	case SIOCIFCREATE:
		if ((error = suser(p)) != 0)
			return (error);
		KERNEL_LOCK();
		error = if_clone_create(ifr->ifr_name, 0);
		KERNEL_UNLOCK();
		return (error);
	case SIOCIFDESTROY:
		if ((error = suser(p)) != 0)
			return (error);
		KERNEL_LOCK();
		error = if_clone_destroy(ifr->ifr_name);
		KERNEL_UNLOCK();
		return (error);
	case SIOCSIFGATTR:
		if ((error = suser(p)) != 0)
			return (error);
		KERNEL_LOCK();
		NET_LOCK();
		error = if_setgroupattribs(data);
		NET_UNLOCK();
		KERNEL_UNLOCK();
		return (error);
	case SIOCGIFCONF:
	case SIOCIFGCLONERS:
	case SIOCGIFGMEMB:
	case SIOCGIFGATTR:
	case SIOCGIFGLIST:
	case SIOCGIFFLAGS:
	case SIOCGIFXFLAGS:
	case SIOCGIFMETRIC:
	case SIOCGIFMTU:
	case SIOCGIFHARDMTU:
	case SIOCGIFDATA:
	case SIOCGIFDESCR:
	case SIOCGIFRTLABEL:
	case SIOCGIFPRIORITY:
	case SIOCGIFRDOMAIN:
	case SIOCGIFGROUP:
	case SIOCGIFLLPRIO:
		error = ifioctl_get(cmd, data);
		return (error);
	}

	KERNEL_LOCK();

	ifp = if_unit(ifr->ifr_name);
	if (ifp == NULL) {
		KERNEL_UNLOCK();
		return (ENXIO);
	}
	oif_flags = ifp->if_flags;
	oif_xflags = ifp->if_xflags;

	switch (cmd) {
	case SIOCIFAFATTACH:
	case SIOCIFAFDETACH:
		if ((error = suser(p)) != 0)
			break;
		NET_LOCK();
		switch (ifar->ifar_af) {
		case AF_INET:
			/* attach is a noop for AF_INET */
			if (cmd == SIOCIFAFDETACH)
				in_ifdetach(ifp);
			break;
#ifdef INET6
		case AF_INET6:
			if (cmd == SIOCIFAFATTACH)
				error = in6_ifattach(ifp);
			else
				in6_ifdetach(ifp);
			break;
#endif /* INET6 */
		default:
			error = EAFNOSUPPORT;
		}
		NET_UNLOCK();
		break;

	case SIOCSIFXFLAGS:
		if ((error = suser(p)) != 0)
			break;

		NET_LOCK();
#ifdef INET6
		if ((ISSET(ifr->ifr_flags, IFXF_AUTOCONF6) ||
		    ISSET(ifr->ifr_flags, IFXF_AUTOCONF6TEMP)) &&
		    !ISSET(ifp->if_xflags, IFXF_AUTOCONF6) &&
		    !ISSET(ifp->if_xflags, IFXF_AUTOCONF6TEMP)) {
			error = in6_ifattach(ifp);
			if (error != 0) {
				NET_UNLOCK();
				break;
			}
		}

		if (ISSET(ifr->ifr_flags, IFXF_INET6_NOSOII) &&
		    !ISSET(ifp->if_xflags, IFXF_INET6_NOSOII))
			ifp->if_xflags |= IFXF_INET6_NOSOII;

		if (!ISSET(ifr->ifr_flags, IFXF_INET6_NOSOII) &&
		    ISSET(ifp->if_xflags, IFXF_INET6_NOSOII))
			ifp->if_xflags &= ~IFXF_INET6_NOSOII;

#endif	/* INET6 */

#ifdef MPLS
		if (ISSET(ifr->ifr_flags, IFXF_MPLS) &&
		    !ISSET(ifp->if_xflags, IFXF_MPLS)) {
			s = splnet();
			ifp->if_xflags |= IFXF_MPLS;
			ifp->if_ll_output = ifp->if_output;
			ifp->if_output = mpls_output;
			splx(s);
		}
		if (ISSET(ifp->if_xflags, IFXF_MPLS) &&
		    !ISSET(ifr->ifr_flags, IFXF_MPLS)) {
			s = splnet();
			ifp->if_xflags &= ~IFXF_MPLS;
			ifp->if_output = ifp->if_ll_output;
			ifp->if_ll_output = NULL;
			splx(s);
		}
#endif	/* MPLS */

#ifndef SMALL_KERNEL
		if (ifp->if_capabilities & IFCAP_WOL) {
			if (ISSET(ifr->ifr_flags, IFXF_WOL) &&
			    !ISSET(ifp->if_xflags, IFXF_WOL)) {
				s = splnet();
				ifp->if_xflags |= IFXF_WOL;
				error = ifp->if_wol(ifp, 1);
				splx(s);
			}
			if (ISSET(ifp->if_xflags, IFXF_WOL) &&
			    !ISSET(ifr->ifr_flags, IFXF_WOL)) {
				s = splnet();
				ifp->if_xflags &= ~IFXF_WOL;
				error = ifp->if_wol(ifp, 0);
				splx(s);
			}
		} else if (ISSET(ifr->ifr_flags, IFXF_WOL)) {
			ifr->ifr_flags &= ~IFXF_WOL;
			error = ENOTSUP;
		}
#endif
		if (ISSET(ifr->ifr_flags, IFXF_LRO) !=
		    ISSET(ifp->if_xflags, IFXF_LRO))
			error = ifsetlro(ifp, ISSET(ifr->ifr_flags, IFXF_LRO));

		if (error == 0)
			ifp->if_xflags = (ifp->if_xflags & IFXF_CANTCHANGE) |
				(ifr->ifr_flags & ~IFXF_CANTCHANGE);

		if (!ISSET(ifp->if_flags, IFF_UP) &&
		    ((!ISSET(oif_xflags, IFXF_AUTOCONF4) &&
		    ISSET(ifp->if_xflags, IFXF_AUTOCONF4)) ||
		    (!ISSET(oif_xflags, IFXF_AUTOCONF6) &&
		    ISSET(ifp->if_xflags, IFXF_AUTOCONF6)) ||
		    (!ISSET(oif_xflags, IFXF_AUTOCONF6TEMP) &&
		    ISSET(ifp->if_xflags, IFXF_AUTOCONF6TEMP)))) {
			ifr->ifr_flags = ifp->if_flags | IFF_UP;
			goto forceup;
		}

		NET_UNLOCK();
		break;

	case SIOCSIFFLAGS:
		if ((error = suser(p)) != 0)
			break;

		NET_LOCK();
forceup:
		ifp->if_flags = (ifp->if_flags & IFF_CANTCHANGE) |
			(ifr->ifr_flags & ~IFF_CANTCHANGE);
		error = (*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, data);
		if (error != 0) {
			ifp->if_flags = oif_flags;
			if (cmd == SIOCSIFXFLAGS)
				ifp->if_xflags = oif_xflags;
		} else if (ISSET(oif_flags ^ ifp->if_flags, IFF_UP)) {
			s = splnet();
			if (ISSET(ifp->if_flags, IFF_UP))
				if_up(ifp);
			else
				if_down(ifp);
			splx(s);
		}
		NET_UNLOCK();
		break;

	case SIOCSIFMETRIC:
		if ((error = suser(p)) != 0)
			break;
		NET_LOCK();
		ifp->if_metric = ifr->ifr_metric;
		NET_UNLOCK();
		break;

	case SIOCSIFMTU:
		if ((error = suser(p)) != 0)
			break;
		NET_LOCK();
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		NET_UNLOCK();
		if (error == 0)
			rtm_ifchg(ifp);
		break;

	case SIOCSIFDESCR:
		if ((error = suser(p)) != 0)
			break;
		error = copyinstr(ifr->ifr_data, ifdescrbuf,
		    IFDESCRSIZE, &bytesdone);
		if (error == 0) {
			(void)memset(ifp->if_description, 0, IFDESCRSIZE);
			strlcpy(ifp->if_description, ifdescrbuf, IFDESCRSIZE);
		}
		break;

	case SIOCSIFRTLABEL:
		if ((error = suser(p)) != 0)
			break;
		error = copyinstr(ifr->ifr_data, ifrtlabelbuf,
		    RTLABEL_LEN, &bytesdone);
		if (error == 0) {
			rtlabel_unref(ifp->if_rtlabelid);
			ifp->if_rtlabelid = rtlabel_name2id(ifrtlabelbuf);
		}
		break;

	case SIOCSIFPRIORITY:
		if ((error = suser(p)) != 0)
			break;
		if (ifr->ifr_metric < 0 || ifr->ifr_metric > 15) {
			error = EINVAL;
			break;
		}
		ifp->if_priority = ifr->ifr_metric;
		break;

	case SIOCSIFRDOMAIN:
		if ((error = suser(p)) != 0)
			break;
		error = if_createrdomain(ifr->ifr_rdomainid, ifp);
		if (!error || error == EEXIST) {
			NET_LOCK();
			error = if_setrdomain(ifp, ifr->ifr_rdomainid);
			NET_UNLOCK();
		}
		break;

	case SIOCAIFGROUP:
		if ((error = suser(p)))
			break;
		NET_LOCK();
		error = if_addgroup(ifp, ifgr->ifgr_group);
		if (error == 0) {
			error = (*ifp->if_ioctl)(ifp, cmd, data);
			if (error == ENOTTY)
				error = 0;
		}
		NET_UNLOCK();
		break;

	case SIOCDIFGROUP:
		if ((error = suser(p)))
			break;
		NET_LOCK();
		error = (*ifp->if_ioctl)(ifp, cmd, data);
		if (error == ENOTTY)
			error = 0;
		if (error == 0)
			error = if_delgroup(ifp, ifgr->ifgr_group);
		NET_UNLOCK();
		break;

	case SIOCSIFLLADDR:
		if ((error = suser(p)))
			break;
		if ((ifp->if_sadl == NULL) ||
		    (ifr->ifr_addr.sa_len != ETHER_ADDR_LEN) ||
		    (ETHER_IS_MULTICAST(ifr->ifr_addr.sa_data))) {
			error = EINVAL;
			break;
		}
		NET_LOCK();
		switch (ifp->if_type) {
		case IFT_ETHER:
		case IFT_CARP:
		case IFT_XETHER:
		case IFT_ISO88025:
			error = (*ifp->if_ioctl)(ifp, cmd, data);
			if (error == ENOTTY)
				error = 0;
			if (error == 0)
				error = if_setlladdr(ifp,
				    ifr->ifr_addr.sa_data);
			break;
		default:
			error = ENODEV;
		}

		if (error == 0)
			ifnewlladdr(ifp);
		NET_UNLOCK();
		if (error == 0)
			rtm_ifchg(ifp);
		break;

	case SIOCSIFLLPRIO:
		if ((error = suser(p)))
			break;
		if (ifr->ifr_llprio < IFQ_MINPRIO ||
		    ifr->ifr_llprio > IFQ_MAXPRIO) {
			error = EINVAL;
			break;
		}
		NET_LOCK();
		ifp->if_llprio = ifr->ifr_llprio;
		NET_UNLOCK();
		break;

	case SIOCGIFSFFPAGE:
		error = suser(p);
		if (error != 0)
			break;

		error = if_sffpage_check(data);
		if (error != 0)
			break;

		/* don't take NET_LOCK because i2c reads take a long time */
		error = ((*ifp->if_ioctl)(ifp, cmd, data));
		break;

	case SIOCSIFMEDIA:
		if ((error = suser(p)) != 0)
			break;
		/* FALLTHROUGH */
	case SIOCGIFMEDIA:
		/* net lock is not needed */
		error = ((*ifp->if_ioctl)(ifp, cmd, data));
		break;

	case SIOCSETKALIVE:
	case SIOCDIFPHYADDR:
	case SIOCSLIFPHYADDR:
	case SIOCSLIFPHYRTABLE:
	case SIOCSLIFPHYTTL:
	case SIOCSLIFPHYDF:
	case SIOCSLIFPHYECN:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCSVNETID:
	case SIOCDVNETID:
	case SIOCSVNETFLOWID:
	case SIOCSTXHPRIO:
	case SIOCSRXHPRIO:
	case SIOCSIFPAIR:
	case SIOCSIFPARENT:
	case SIOCDIFPARENT:
	case SIOCSETMPWCFG:
	case SIOCSETLABEL:
	case SIOCDELLABEL:
	case SIOCSPWE3CTRLWORD:
	case SIOCSPWE3FAT:
	case SIOCSPWE3NEIGHBOR:
	case SIOCDPWE3NEIGHBOR:
#if NBRIDGE > 0
	case SIOCBRDGADD:
	case SIOCBRDGDEL:
	case SIOCBRDGSIFFLGS:
	case SIOCBRDGSCACHE:
	case SIOCBRDGADDS:
	case SIOCBRDGDELS:
	case SIOCBRDGSADDR:
	case SIOCBRDGSTO:
	case SIOCBRDGDADDR:
	case SIOCBRDGFLUSH:
	case SIOCBRDGADDL:
	case SIOCBRDGSIFPROT:
	case SIOCBRDGARL:
	case SIOCBRDGFRL:
	case SIOCBRDGSPRI:
	case SIOCBRDGSHT:
	case SIOCBRDGSFD:
	case SIOCBRDGSMA:
	case SIOCBRDGSIFPRIO:
	case SIOCBRDGSIFCOST:
	case SIOCBRDGSTXHC:
	case SIOCBRDGSPROTO:
#endif
		if ((error = suser(p)) != 0)
			break;
		/* FALLTHROUGH */
	default:
		error = pru_control(so, cmd, data, ifp);
		if (error != EOPNOTSUPP)
			break;
		switch (cmd) {
		case SIOCAIFADDR:
		case SIOCDIFADDR:
		case SIOCSIFADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFDSTADDR:
		case SIOCSIFBRDADDR:
#ifdef INET6
		case SIOCAIFADDR_IN6:
		case SIOCDIFADDR_IN6:
#endif
			error = suser(p);
			break;
		default:
			error = 0;
			break;
		}
		if (error)
			break;
		NET_LOCK();
		error = ((*ifp->if_ioctl)(ifp, cmd, data));
		NET_UNLOCK();
		break;
	}

	if (oif_flags != ifp->if_flags || oif_xflags != ifp->if_xflags) {
		/* if_up() and if_down() already sent an update, skip here */
		if (((oif_flags ^ ifp->if_flags) & IFF_UP) == 0)
			rtm_ifchg(ifp);
	}

	if (((oif_flags ^ ifp->if_flags) & IFF_UP) != 0)
		getmicrotime(&ifp->if_lastchange);

	KERNEL_UNLOCK();

	if_put(ifp);

	return (error);
}

int
ifioctl_get(u_long cmd, caddr_t data)
{
	struct ifnet *ifp;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;
	size_t bytesdone;

	switch(cmd) {
	case SIOCGIFCONF:
		NET_LOCK_SHARED();
		error = ifconf(data);
		NET_UNLOCK_SHARED();
		return (error);
	case SIOCIFGCLONERS:
		error = if_clone_list((struct if_clonereq *)data);
		return (error);
	case SIOCGIFGMEMB:
		error = if_getgroupmembers(data);
		return (error);
	case SIOCGIFGATTR:
		NET_LOCK_SHARED();
		error = if_getgroupattribs(data);
		NET_UNLOCK_SHARED();
		return (error);
	case SIOCGIFGLIST:
		error = if_getgrouplist(data);
		return (error);
	}

	KERNEL_LOCK();
	ifp = if_unit(ifr->ifr_name);
	KERNEL_UNLOCK();

	if (ifp == NULL)
		return (ENXIO);

	switch(cmd) {
	case SIOCGIFFLAGS:
		ifr->ifr_flags = ifp->if_flags;
		if (ifq_is_oactive(&ifp->if_snd))
			ifr->ifr_flags |= IFF_OACTIVE;
		break;

	case SIOCGIFXFLAGS:
		ifr->ifr_flags = ifp->if_xflags & ~(IFXF_MPSAFE|IFXF_CLONED);
		break;

	case SIOCGIFMETRIC:
		ifr->ifr_metric = ifp->if_metric;
		break;

	case SIOCGIFMTU:
		ifr->ifr_mtu = ifp->if_mtu;
		break;

	case SIOCGIFHARDMTU:
		ifr->ifr_hardmtu = ifp->if_hardmtu;
		break;

	case SIOCGIFDATA: {
		struct if_data ifdata;

		NET_LOCK_SHARED();
		KERNEL_LOCK();
		if_getdata(ifp, &ifdata);
		KERNEL_UNLOCK();
		NET_UNLOCK_SHARED();

		error = copyout(&ifdata, ifr->ifr_data, sizeof(ifdata));
		break;
	}

	case SIOCGIFDESCR: {
		char ifdescrbuf[IFDESCRSIZE];
		KERNEL_LOCK();
		strlcpy(ifdescrbuf, ifp->if_description, IFDESCRSIZE);
		KERNEL_UNLOCK();

		error = copyoutstr(ifdescrbuf, ifr->ifr_data, IFDESCRSIZE,
		    &bytesdone);
		break;
	}
	case SIOCGIFRTLABEL: {
		char ifrtlabelbuf[RTLABEL_LEN];
		u_short rtlabelid = READ_ONCE(ifp->if_rtlabelid);

		if (rtlabelid && rtlabel_id2name(rtlabelid,
		    ifrtlabelbuf, RTLABEL_LEN) != NULL) {
			error = copyoutstr(ifrtlabelbuf, ifr->ifr_data,
			    RTLABEL_LEN, &bytesdone);
		} else
			error = ENOENT;
		break;
	}
	case SIOCGIFPRIORITY:
		ifr->ifr_metric = ifp->if_priority;
		break;

	case SIOCGIFRDOMAIN:
		ifr->ifr_rdomainid = ifp->if_rdomain;
		break;

	case SIOCGIFGROUP:
		error = if_getgroup(data, ifp);
		break;

	case SIOCGIFLLPRIO:
		ifr->ifr_llprio = ifp->if_llprio;
		break;

	default:
		panic("invalid ioctl %lu", cmd);
	}

	if_put(ifp);

	return (error);
}

static int
if_sffpage_check(const caddr_t data)
{
	const struct if_sffpage *sff = (const struct if_sffpage *)data;

	switch (sff->sff_addr) {
	case IFSFF_ADDR_EEPROM:
	case IFSFF_ADDR_DDM:
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

int
if_txhprio_l2_check(int hdrprio)
{
	switch (hdrprio) {
	case IF_HDRPRIO_PACKET:
		return (0);
	default:
		if (hdrprio >= IF_HDRPRIO_MIN && hdrprio <= IF_HDRPRIO_MAX)
			return (0);
		break;
	}

	return (EINVAL);
}

int
if_txhprio_l3_check(int hdrprio)
{
	switch (hdrprio) {
	case IF_HDRPRIO_PACKET:
	case IF_HDRPRIO_PAYLOAD:
		return (0);
	default:
		if (hdrprio >= IF_HDRPRIO_MIN && hdrprio <= IF_HDRPRIO_MAX)
			return (0);
		break;
	}

	return (EINVAL);
}

int
if_rxhprio_l2_check(int hdrprio)
{
	switch (hdrprio) {
	case IF_HDRPRIO_PACKET:
	case IF_HDRPRIO_OUTER:
		return (0);
	default:
		if (hdrprio >= IF_HDRPRIO_MIN && hdrprio <= IF_HDRPRIO_MAX)
			return (0);
		break;
	}

	return (EINVAL);
}

int
if_rxhprio_l3_check(int hdrprio)
{
	switch (hdrprio) {
	case IF_HDRPRIO_PACKET:
	case IF_HDRPRIO_PAYLOAD:
	case IF_HDRPRIO_OUTER:
		return (0);
	default:
		if (hdrprio >= IF_HDRPRIO_MIN && hdrprio <= IF_HDRPRIO_MAX)
			return (0);
		break;
	}

	return (EINVAL);
}

/*
 * Return interface configuration
 * of system.  List may be used
 * in later ioctl's (above) to get
 * other information.
 */
int
ifconf(caddr_t data)
{
	struct ifconf *ifc = (struct ifconf *)data;
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct ifreq ifr, *ifrp;
	int space = ifc->ifc_len, error = 0;

	/* If ifc->ifc_len is 0, fill it in with the needed size and return. */
	if (space == 0) {
		TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
			struct sockaddr *sa;

			if (TAILQ_EMPTY(&ifp->if_addrlist))
				space += sizeof (ifr);
			else
				TAILQ_FOREACH(ifa,
				    &ifp->if_addrlist, ifa_list) {
					sa = ifa->ifa_addr;
					if (sa->sa_len > sizeof(*sa))
						space += sa->sa_len -
						    sizeof(*sa);
					space += sizeof(ifr);
				}
		}
		ifc->ifc_len = space;
		return (0);
	}

	ifrp = ifc->ifc_req;
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (space < sizeof(ifr))
			break;
		bcopy(ifp->if_xname, ifr.ifr_name, IFNAMSIZ);
		if (TAILQ_EMPTY(&ifp->if_addrlist)) {
			bzero((caddr_t)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			error = copyout((caddr_t)&ifr, (caddr_t)ifrp,
			    sizeof(ifr));
			if (error)
				break;
			space -= sizeof (ifr), ifrp++;
		} else
			TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
				struct sockaddr *sa = ifa->ifa_addr;

				if (space < sizeof(ifr))
					break;
				if (sa->sa_len <= sizeof(*sa)) {
					ifr.ifr_addr = *sa;
					error = copyout((caddr_t)&ifr,
					    (caddr_t)ifrp, sizeof (ifr));
					ifrp++;
				} else {
					space -= sa->sa_len - sizeof(*sa);
					if (space < sizeof (ifr))
						break;
					error = copyout((caddr_t)&ifr,
					    (caddr_t)ifrp,
					    sizeof(ifr.ifr_name));
					if (error == 0)
						error = copyout((caddr_t)sa,
						    (caddr_t)&ifrp->ifr_addr,
						    sa->sa_len);
					ifrp = (struct ifreq *)(sa->sa_len +
					    (caddr_t)&ifrp->ifr_addr);
				}
				if (error)
					break;
				space -= sizeof (ifr);
			}
	}
	ifc->ifc_len -= space;
	return (error);
}

void
if_counters_alloc(struct ifnet *ifp)
{
	KASSERT(ifp->if_counters == NULL);

	ifp->if_counters = counters_alloc(ifc_ncounters);
}

void
if_counters_free(struct ifnet *ifp)
{
	KASSERT(ifp->if_counters != NULL);

	counters_free(ifp->if_counters, ifc_ncounters);
	ifp->if_counters = NULL;
}

void
if_getdata(struct ifnet *ifp, struct if_data *data)
{
	unsigned int i;

	data->ifi_type = ifp->if_type;
	data->ifi_addrlen = ifp->if_addrlen;
	data->ifi_hdrlen = ifp->if_hdrlen;
	data->ifi_link_state = ifp->if_link_state;
	data->ifi_mtu = ifp->if_mtu;
	data->ifi_metric = ifp->if_metric;
	data->ifi_baudrate = ifp->if_baudrate;
	data->ifi_capabilities = ifp->if_capabilities;
	data->ifi_rdomain = ifp->if_rdomain;
	data->ifi_lastchange = ifp->if_lastchange;

	data->ifi_ipackets = ifp->if_data_counters[ifc_ipackets];
	data->ifi_ierrors = ifp->if_data_counters[ifc_ierrors];
	data->ifi_opackets = ifp->if_data_counters[ifc_opackets];
	data->ifi_oerrors = ifp->if_data_counters[ifc_oerrors];
	data->ifi_collisions = ifp->if_data_counters[ifc_collisions];
	data->ifi_ibytes = ifp->if_data_counters[ifc_ibytes];
	data->ifi_obytes = ifp->if_data_counters[ifc_obytes];
	data->ifi_imcasts = ifp->if_data_counters[ifc_imcasts];
	data->ifi_omcasts = ifp->if_data_counters[ifc_omcasts];
	data->ifi_iqdrops = ifp->if_data_counters[ifc_iqdrops];
	data->ifi_oqdrops = ifp->if_data_counters[ifc_oqdrops];
	data->ifi_noproto = ifp->if_data_counters[ifc_noproto];

	if (ifp->if_counters != NULL) {
		uint64_t counters[ifc_ncounters];

		counters_read(ifp->if_counters, counters, nitems(counters),
		    NULL);

		data->ifi_ipackets += counters[ifc_ipackets];
		data->ifi_ierrors += counters[ifc_ierrors];
		data->ifi_opackets += counters[ifc_opackets];
		data->ifi_oerrors += counters[ifc_oerrors];
		data->ifi_collisions += counters[ifc_collisions];
		data->ifi_ibytes += counters[ifc_ibytes];
		data->ifi_obytes += counters[ifc_obytes];
		data->ifi_imcasts += counters[ifc_imcasts];
		data->ifi_omcasts += counters[ifc_omcasts];
		data->ifi_iqdrops += counters[ifc_iqdrops];
		data->ifi_oqdrops += counters[ifc_oqdrops];
		data->ifi_noproto += counters[ifc_noproto];
	}

	for (i = 0; i < ifp->if_nifqs; i++) {
		struct ifqueue *ifq = ifp->if_ifqs[i];

		ifq_add_data(ifq, data);
	}

	for (i = 0; i < ifp->if_niqs; i++) {
		struct ifiqueue *ifiq = ifp->if_iqs[i];

		ifiq_add_data(ifiq, data);
	}
}

/*
 * Dummy functions replaced in ifnet during detach (if protocols decide to
 * fiddle with the if during detach.
 */
void
if_detached_qstart(struct ifqueue *ifq)
{
	ifq_purge(ifq);
}

int
if_detached_ioctl(struct ifnet *ifp, u_long a, caddr_t b)
{
	return ENODEV;
}

static inline void
ifgroup_icref(struct ifg_group *ifg)
{
	refcnt_take(&ifg->ifg_tmprefcnt);
}

static inline void
ifgroup_icrele(struct ifg_group *ifg)
{
	if (refcnt_rele(&ifg->ifg_tmprefcnt) != 0)
		free(ifg, M_IFGROUP, sizeof(*ifg));
}

/*
 * Create interface group without members
 */
struct ifg_group *
if_creategroup(const char *groupname)
{
	struct ifg_group	*ifg;

	if ((ifg = malloc(sizeof(*ifg), M_IFGROUP, M_NOWAIT)) == NULL)
		return (NULL);

	strlcpy(ifg->ifg_group, groupname, sizeof(ifg->ifg_group));
	ifg->ifg_refcnt = 1;
	ifg->ifg_carp_demoted = 0;
	TAILQ_INIT(&ifg->ifg_members);
	refcnt_init(&ifg->ifg_tmprefcnt);
#if NPF > 0
	pfi_attach_ifgroup(ifg);
#endif
	TAILQ_INSERT_TAIL(&ifg_head, ifg, ifg_next);

	return (ifg);
}

/*
 * Add a group to an interface
 */
int
if_addgroup(struct ifnet *ifp, const char *groupname)
{
	struct ifg_list		*ifgl;
	struct ifg_group	*ifg = NULL;
	struct ifg_member	*ifgm;
	size_t			 namelen;

	namelen = strlen(groupname);
	if (namelen == 0 || namelen >= IFNAMSIZ ||
	    (groupname[namelen - 1] >= '0' && groupname[namelen - 1] <= '9'))
		return (EINVAL);

	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
		if (!strcmp(ifgl->ifgl_group->ifg_group, groupname))
			return (EEXIST);

	if ((ifgl = malloc(sizeof(*ifgl), M_IFGROUP, M_NOWAIT)) == NULL)
		return (ENOMEM);

	if ((ifgm = malloc(sizeof(*ifgm), M_IFGROUP, M_NOWAIT)) == NULL) {
		free(ifgl, M_IFGROUP, sizeof(*ifgl));
		return (ENOMEM);
	}

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, groupname))
			break;

	if (ifg == NULL) {
		ifg = if_creategroup(groupname);
		if (ifg == NULL) {
			free(ifgl, M_IFGROUP, sizeof(*ifgl));
			free(ifgm, M_IFGROUP, sizeof(*ifgm));
			return (ENOMEM);
		}
	} else
		ifg->ifg_refcnt++;
	KASSERT(ifg->ifg_refcnt != 0);

	ifgl->ifgl_group = ifg;
	ifgm->ifgm_ifp = ifp;

	TAILQ_INSERT_TAIL(&ifg->ifg_members, ifgm, ifgm_next);
	TAILQ_INSERT_TAIL(&ifp->if_groups, ifgl, ifgl_next);

#if NPF > 0
	pfi_group_addmember(groupname);
#endif

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

	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
		if (!strcmp(ifgl->ifgl_group->ifg_group, groupname))
			break;
	if (ifgl == NULL)
		return (ENOENT);

	TAILQ_REMOVE(&ifp->if_groups, ifgl, ifgl_next);

	TAILQ_FOREACH(ifgm, &ifgl->ifgl_group->ifg_members, ifgm_next)
		if (ifgm->ifgm_ifp == ifp)
			break;

	if (ifgm != NULL) {
		TAILQ_REMOVE(&ifgl->ifgl_group->ifg_members, ifgm, ifgm_next);
		free(ifgm, M_IFGROUP, sizeof(*ifgm));
	}

#if NPF > 0
	pfi_group_delmember(groupname);
#endif

	KASSERT(ifgl->ifgl_group->ifg_refcnt != 0);
	if (--ifgl->ifgl_group->ifg_refcnt == 0) {
		TAILQ_REMOVE(&ifg_head, ifgl->ifgl_group, ifg_next);
#if NPF > 0
		pfi_detach_ifgroup(ifgl->ifgl_group);
#endif
		ifgroup_icrele(ifgl->ifgl_group);
	}

	free(ifgl, M_IFGROUP, sizeof(*ifgl));

	return (0);
}

/*
 * Stores all groups from an interface in memory pointed
 * to by data
 */
int
if_getgroup(caddr_t data, struct ifnet *ifp)
{
	TAILQ_HEAD(, ifg_group)	 ifg_tmplist =
	    TAILQ_HEAD_INITIALIZER(ifg_tmplist);
	struct ifg_list		*ifgl;
	struct ifg_req		 ifgrq, *ifgp;
	struct ifgroupreq	*ifgr = (struct ifgroupreq *)data;
	struct ifg_group	 *ifg;
	int			 len, error = 0;

	if (ifgr->ifgr_len == 0) {
		NET_LOCK_SHARED();
		TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next)
			ifgr->ifgr_len += sizeof(struct ifg_req);
		NET_UNLOCK_SHARED();
		return (0);
	}

	len = ifgr->ifgr_len;
	ifgp = ifgr->ifgr_groups;

	rw_enter_write(&if_tmplist_lock);

	NET_LOCK_SHARED();
	TAILQ_FOREACH(ifgl, &ifp->if_groups, ifgl_next) {
		ifgroup_icref(ifgl->ifgl_group);
		TAILQ_INSERT_TAIL(&ifg_tmplist, ifgl->ifgl_group, ifg_tmplist);
	}
	NET_UNLOCK_SHARED();

	TAILQ_FOREACH(ifg, &ifg_tmplist, ifg_tmplist) {
		if (len < sizeof(ifgrq)) {
			error = EINVAL;
			break;
		}
		bzero(&ifgrq, sizeof ifgrq);
		strlcpy(ifgrq.ifgrq_group, ifg->ifg_group,
		    sizeof(ifgrq.ifgrq_group));
		if ((error = copyout((caddr_t)&ifgrq, (caddr_t)ifgp,
		    sizeof(struct ifg_req))))
			break;
		len -= sizeof(ifgrq);
		ifgp++;
	}

	while ((ifg = TAILQ_FIRST(&ifg_tmplist))){
		TAILQ_REMOVE(&ifg_tmplist, ifg, ifg_tmplist);
		ifgroup_icrele(ifg);
	}

	rw_exit_write(&if_tmplist_lock);

	return (error);
}

/*
 * Stores all members of a group in memory pointed to by data
 */
int
if_getgroupmembers(caddr_t data)
{
	TAILQ_HEAD(, ifnet)	if_tmplist =
	    TAILQ_HEAD_INITIALIZER(if_tmplist);
	struct ifnet		*ifp;
	struct ifgroupreq	*ifgr = (struct ifgroupreq *)data;
	struct ifg_group	*ifg;
	struct ifg_member	*ifgm;
	struct ifg_req		 ifgrq, *ifgp;
	int			 len, error = 0;

	rw_enter_write(&if_tmplist_lock);
	NET_LOCK_SHARED();

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, ifgr->ifgr_name))
			break;
	if (ifg == NULL) {
		error = ENOENT;
		goto unlock;
	}

	if (ifgr->ifgr_len == 0) {
		TAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next)
			ifgr->ifgr_len += sizeof(ifgrq);
		goto unlock;
	}

	TAILQ_FOREACH (ifgm, &ifg->ifg_members, ifgm_next) {
		if_ref(ifgm->ifgm_ifp);
		TAILQ_INSERT_TAIL(&if_tmplist, ifgm->ifgm_ifp, if_tmplist);
	}
	NET_UNLOCK_SHARED();

	len = ifgr->ifgr_len;
	ifgp = ifgr->ifgr_groups;

	TAILQ_FOREACH (ifp, &if_tmplist, if_tmplist) {
		if (len < sizeof(ifgrq)) {
			error = EINVAL;
			break;
		}
		bzero(&ifgrq, sizeof ifgrq);
		strlcpy(ifgrq.ifgrq_member, ifp->if_xname,
		    sizeof(ifgrq.ifgrq_member));
		if ((error = copyout((caddr_t)&ifgrq, (caddr_t)ifgp,
		    sizeof(struct ifg_req))))
			break;
		len -= sizeof(ifgrq);
		ifgp++;
	}

	while ((ifp = TAILQ_FIRST(&if_tmplist))) {
		TAILQ_REMOVE(&if_tmplist, ifp, if_tmplist);
		if_put(ifp);
	}
	rw_exit_write(&if_tmplist_lock);

	return (error);

unlock:
	NET_UNLOCK_SHARED();
	rw_exit_write(&if_tmplist_lock);

	return (error);
}

int
if_getgroupattribs(caddr_t data)
{
	struct ifgroupreq	*ifgr = (struct ifgroupreq *)data;
	struct ifg_group	*ifg;

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, ifgr->ifgr_name))
			break;
	if (ifg == NULL)
		return (ENOENT);

	ifgr->ifgr_attrib.ifg_carp_demoted = ifg->ifg_carp_demoted;

	return (0);
}

int
if_setgroupattribs(caddr_t data)
{
	struct ifgroupreq	*ifgr = (struct ifgroupreq *)data;
	struct ifg_group	*ifg;
	struct ifg_member	*ifgm;
	int			 demote;

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, ifgr->ifgr_name))
			break;
	if (ifg == NULL)
		return (ENOENT);

	demote = ifgr->ifgr_attrib.ifg_carp_demoted;
	if (demote + ifg->ifg_carp_demoted > 0xff ||
	    demote + ifg->ifg_carp_demoted < 0)
		return (EINVAL);

	ifg->ifg_carp_demoted += demote;

	TAILQ_FOREACH(ifgm, &ifg->ifg_members, ifgm_next)
		ifgm->ifgm_ifp->if_ioctl(ifgm->ifgm_ifp, SIOCSIFGATTR, data);

	return (0);
}

/*
 * Stores all groups in memory pointed to by data
 */
int
if_getgrouplist(caddr_t data)
{
	TAILQ_HEAD(, ifg_group)	 ifg_tmplist =
	    TAILQ_HEAD_INITIALIZER(ifg_tmplist);
	struct ifgroupreq	*ifgr = (struct ifgroupreq *)data;
	struct ifg_group	*ifg;
	struct ifg_req		 ifgrq, *ifgp;
	int			 len, error = 0;

	if (ifgr->ifgr_len == 0) {
		NET_LOCK_SHARED();
		TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
			ifgr->ifgr_len += sizeof(ifgrq);
		NET_UNLOCK_SHARED();
		return (0);
	}

	len = ifgr->ifgr_len;
	ifgp = ifgr->ifgr_groups;

	rw_enter_write(&if_tmplist_lock);

	NET_LOCK_SHARED();
	TAILQ_FOREACH(ifg, &ifg_head, ifg_next) {
		ifgroup_icref(ifg);
		TAILQ_INSERT_TAIL(&ifg_tmplist, ifg, ifg_tmplist);
	}
	NET_UNLOCK_SHARED();

	TAILQ_FOREACH(ifg, &ifg_tmplist, ifg_tmplist) {
		if (len < sizeof(ifgrq)) {
			error = EINVAL;
			break;
		}
		bzero(&ifgrq, sizeof ifgrq);
		strlcpy(ifgrq.ifgrq_group, ifg->ifg_group,
		    sizeof(ifgrq.ifgrq_group));
		if ((error = copyout((caddr_t)&ifgrq, (caddr_t)ifgp,
		    sizeof(struct ifg_req))))
			break;
		len -= sizeof(ifgrq);
		ifgp++;
	}

	while ((ifg = TAILQ_FIRST(&ifg_tmplist))){
		TAILQ_REMOVE(&ifg_tmplist, ifg, ifg_tmplist);
		ifgroup_icrele(ifg);
	}

	rw_exit_write(&if_tmplist_lock);

	return (error);
}

void
if_group_routechange(const struct sockaddr *dst, const struct sockaddr *mask)
{
	switch (dst->sa_family) {
	case AF_INET:
		if (satosin_const(dst)->sin_addr.s_addr == INADDR_ANY &&
		    mask && (mask->sa_len == 0 ||
		    satosin_const(mask)->sin_addr.s_addr == INADDR_ANY))
			if_group_egress_build();
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_ARE_ADDR_EQUAL(&(satosin6_const(dst))->sin6_addr,
		    &in6addr_any) && mask && (mask->sa_len == 0 ||
		    IN6_ARE_ADDR_EQUAL(&(satosin6_const(mask))->sin6_addr,
		    &in6addr_any)))
			if_group_egress_build();
		break;
#endif
	}
}

int
if_group_egress_build(void)
{
	struct ifnet		*ifp;
	struct ifg_group	*ifg;
	struct ifg_member	*ifgm, *next;
	struct sockaddr_in	 sa_in;
#ifdef INET6
	struct sockaddr_in6	 sa_in6;
#endif
	struct rtentry		*rt;

	TAILQ_FOREACH(ifg, &ifg_head, ifg_next)
		if (!strcmp(ifg->ifg_group, IFG_EGRESS))
			break;

	if (ifg != NULL)
		TAILQ_FOREACH_SAFE(ifgm, &ifg->ifg_members, ifgm_next, next)
			if_delgroup(ifgm->ifgm_ifp, IFG_EGRESS);

	bzero(&sa_in, sizeof(sa_in));
	sa_in.sin_len = sizeof(sa_in);
	sa_in.sin_family = AF_INET;
	rt = rtable_lookup(0, sintosa(&sa_in), sintosa(&sa_in), NULL, RTP_ANY);
	for (; rt != NULL; rt = rtable_iterate(rt)) {
		if (ISSET(rt->rt_flags, RTF_REJECT | RTF_BLACKHOLE))
			continue;
		ifp = if_get(rt->rt_ifidx);
		if (ifp != NULL) {
			if_addgroup(ifp, IFG_EGRESS);
			if_put(ifp);
		}
	}

#ifdef INET6
	bcopy(&sa6_any, &sa_in6, sizeof(sa_in6));
	rt = rtable_lookup(0, sin6tosa(&sa_in6), sin6tosa(&sa_in6), NULL,
	    RTP_ANY);
	for (; rt != NULL; rt = rtable_iterate(rt)) {
		if (ISSET(rt->rt_flags, RTF_REJECT | RTF_BLACKHOLE))
			continue;
		ifp = if_get(rt->rt_ifidx);
		if (ifp != NULL) {
			if_addgroup(ifp, IFG_EGRESS);
			if_put(ifp);
		}
	}
#endif /* INET6 */

	return (0);
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
	struct ifreq ifr;
	unsigned short oif_flags;
	int oif_pcount, error;

	NET_ASSERT_LOCKED(); /* modifying if_flags and if_pcount */

	oif_flags = ifp->if_flags;
	oif_pcount = ifp->if_pcount;
	if (pswitch) {
		if (ifp->if_pcount++ != 0)
			return (0);
		ifp->if_flags |= IFF_PROMISC;
	} else {
		if (--ifp->if_pcount > 0)
			return (0);
		ifp->if_flags &= ~IFF_PROMISC;
	}

	if ((ifp->if_flags & IFF_UP) == 0)
		return (0);

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = ifp->if_flags;
	error = ((*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifr));
	if (error) {
		ifp->if_flags = oif_flags;
		ifp->if_pcount = oif_pcount;
	}

	return (error);
}

/* Set/clear LRO flag and restart interface if needed. */
int
ifsetlro(struct ifnet *ifp, int on)
{
	struct ifreq ifr;
	int error, s = splnet();

	NET_ASSERT_LOCKED();	/* for ioctl */
	KERNEL_ASSERT_LOCKED();	/* for if_flags */

	memset(&ifr, 0, sizeof ifr);
	if (on)
		SET(ifr.ifr_flags, IFXF_LRO);

	error = ((*ifp->if_ioctl)(ifp, SIOCSIFXFLAGS, (caddr_t)&ifr));
	if (error == 0)
		goto out;
	error = 0;

	if (!ISSET(ifp->if_capabilities, IFCAP_LRO)) {
		error = ENOTSUP;
		goto out;
	}

	if (on && !ISSET(ifp->if_xflags, IFXF_LRO)) {
		if (ifp->if_type == IFT_ETHER && ether_brport_isset(ifp)) {
			error = EBUSY;
			goto out;
		}
		SET(ifp->if_xflags, IFXF_LRO);
	} else if (!on && ISSET(ifp->if_xflags, IFXF_LRO))
		CLR(ifp->if_xflags, IFXF_LRO);

 out:
	splx(s);

	return error;
}

void
ifa_add(struct ifnet *ifp, struct ifaddr *ifa)
{
	NET_ASSERT_LOCKED_EXCLUSIVE();
	TAILQ_INSERT_TAIL(&ifp->if_addrlist, ifa, ifa_list);
}

void
ifa_del(struct ifnet *ifp, struct ifaddr *ifa)
{
	NET_ASSERT_LOCKED_EXCLUSIVE();
	TAILQ_REMOVE(&ifp->if_addrlist, ifa, ifa_list);
}

void
ifa_update_broadaddr(struct ifnet *ifp, struct ifaddr *ifa, struct sockaddr *sa)
{
	if (ifa->ifa_broadaddr->sa_len != sa->sa_len)
		panic("ifa_update_broadaddr does not support dynamic length");
	bcopy(sa, ifa->ifa_broadaddr, sa->sa_len);
}

#ifdef DDB
/* debug function, can be called from ddb> */
void
ifa_print_all(void)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			char addr[INET6_ADDRSTRLEN];

			switch (ifa->ifa_addr->sa_family) {
			case AF_INET:
				printf("%s", inet_ntop(AF_INET,
				    &satosin(ifa->ifa_addr)->sin_addr,
				    addr, sizeof(addr)));
				break;
#ifdef INET6
			case AF_INET6:
				printf("%s", inet_ntop(AF_INET6,
				    &(satosin6(ifa->ifa_addr))->sin6_addr,
				    addr, sizeof(addr)));
				break;
#endif
			}
			printf(" on %s\n", ifp->if_xname);
		}
	}
}
#endif /* DDB */

void
ifnewlladdr(struct ifnet *ifp)
{
#ifdef INET6
	struct ifaddr *ifa;
	int i_am_router = (atomic_load_int(&ip6_forwarding) != 0);
#endif
	struct ifreq ifrq;
	short up;

	NET_ASSERT_LOCKED();	/* for ioctl and in6 */
	KERNEL_ASSERT_LOCKED();	/* for if_flags */

	up = ifp->if_flags & IFF_UP;

	if (up) {
		/* go down for a moment... */
		ifp->if_flags &= ~IFF_UP;
		ifrq.ifr_flags = ifp->if_flags;
		(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifrq);
	}

	ifp->if_flags |= IFF_UP;
	ifrq.ifr_flags = ifp->if_flags;
	(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifrq);

#ifdef INET6
	/*
	 * Update the link-local address.  Don't do it if we're
	 * a router to avoid confusing hosts on the network.
	 */
	if (!i_am_router) {
		ifa = &in6ifa_ifpforlinklocal(ifp, 0)->ia_ifa;
		if (ifa) {
			in6_purgeaddr(ifa);
			if_hooks_run(&ifp->if_addrhooks);
			in6_ifattach(ifp);
		}
	}
#endif
	if (!up) {
		/* go back down */
		ifp->if_flags &= ~IFF_UP;
		ifrq.ifr_flags = ifp->if_flags;
		(*ifp->if_ioctl)(ifp, SIOCSIFFLAGS, (caddr_t)&ifrq);
	}
}

void
if_addrhook_add(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_INSERT_TAIL(&ifp->if_addrhooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

void
if_addrhook_del(struct ifnet *ifp, struct task *t)
{
	mtx_enter(&if_hooks_mtx);
	TAILQ_REMOVE(&ifp->if_addrhooks, t, t_entry);
	mtx_leave(&if_hooks_mtx);
}

void
if_addrhooks_run(struct ifnet *ifp)
{
	if_hooks_run(&ifp->if_addrhooks);
}

void
if_rxr_init(struct if_rxring *rxr, u_int lwm, u_int hwm)
{
	extern int ticks;

	memset(rxr, 0, sizeof(*rxr));

	rxr->rxr_adjusted = ticks;
	rxr->rxr_cwm = rxr->rxr_lwm = lwm;
	rxr->rxr_hwm = hwm;
}

static inline void
if_rxr_adjust_cwm(struct if_rxring *rxr)
{
	extern int ticks;

	if (rxr->rxr_alive >= rxr->rxr_lwm)
		return;
	else if (rxr->rxr_cwm < rxr->rxr_hwm)
		rxr->rxr_cwm++;

	rxr->rxr_adjusted = ticks;
}

void
if_rxr_livelocked(struct if_rxring *rxr)
{
	extern int ticks;

	if (ticks - rxr->rxr_adjusted >= 1) {
		if (rxr->rxr_cwm > rxr->rxr_lwm)
			rxr->rxr_cwm--;

		rxr->rxr_adjusted = ticks;
	}
}

u_int
if_rxr_get(struct if_rxring *rxr, u_int max)
{
	extern int ticks;
	u_int diff;

	if (ticks - rxr->rxr_adjusted >= 1) {
		/* we're free to try for an adjustment */
		if_rxr_adjust_cwm(rxr);
	}

	if (rxr->rxr_alive >= rxr->rxr_cwm)
		return (0);

	diff = min(rxr->rxr_cwm - rxr->rxr_alive, max);
	rxr->rxr_alive += diff;

	return (diff);
}

int
if_rxr_info_ioctl(struct if_rxrinfo *uifri, u_int t, struct if_rxring_info *e)
{
	struct if_rxrinfo kifri;
	int error;
	u_int n;

	error = copyin(uifri, &kifri, sizeof(kifri));
	if (error)
		return (error);

	n = min(t, kifri.ifri_total);
	kifri.ifri_total = t;

	if (n > 0) {
		error = copyout(e, kifri.ifri_entries, sizeof(*e) * n);
		if (error)
			return (error);
	}

	return (copyout(&kifri, uifri, sizeof(kifri)));
}

int
if_rxr_ioctl(struct if_rxrinfo *ifri, const char *name, u_int size,
    struct if_rxring *rxr)
{
	struct if_rxring_info ifr;

	memset(&ifr, 0, sizeof(ifr));

	if (name != NULL)
		strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	ifr.ifr_size = size;
	ifr.ifr_info = *rxr;

	return (if_rxr_info_ioctl(ifri, 1, &ifr));
}

/*
 * Network stack input queues.
 */

int
niq_enqueue(struct niqueue *niq, struct mbuf *m)
{
	int rv;

	rv = mq_enqueue(&niq->ni_q, m);
	if (rv == 0)
		schednetisr(niq->ni_isr);
	else
		if_congestion();

	return (rv);
}

__dead void
unhandled_af(int af)
{
	panic("unhandled af %d", af);
}

unsigned int
softnet_count(void)
{
	static unsigned int nsoftnets;

	if (nsoftnets == 0)
		nsoftnets = min(NET_TASKQ, ncpus);

	return (nsoftnets);
}

struct softnet *
net_sn(unsigned int ifindex)
{
	return (&softnets[ifindex % softnet_count()]);
}

struct taskq *
net_tq(unsigned int ifindex)
{
	return (net_sn(ifindex)->sn_taskq);
}

void
net_tq_barriers(const char *wmesg)
{
	struct task barriers[NET_TASKQ];
	struct refcnt r = REFCNT_INITIALIZER();
	int i;

	for (i = 0; i < softnet_count(); i++) {
		task_set(&barriers[i], (void (*)(void *))refcnt_rele_wake, &r);
		refcnt_take(&r);
		task_add(softnets[i].sn_taskq, &barriers[i]);
	}
 
	refcnt_finalize(&r, wmesg);
}
