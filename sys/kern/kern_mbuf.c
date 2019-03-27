/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2005,
 *	Bosko Milekic <bmilekic@FreeBSD.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_param.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/domainset.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/uma.h>
#include <vm/uma_dbg.h>

/*
 * In FreeBSD, Mbufs and Mbuf Clusters are allocated from UMA
 * Zones.
 *
 * Mbuf Clusters (2K, contiguous) are allocated from the Cluster
 * Zone.  The Zone can be capped at kern.ipc.nmbclusters, if the
 * administrator so desires.
 *
 * Mbufs are allocated from a UMA Master Zone called the Mbuf
 * Zone.
 *
 * Additionally, FreeBSD provides a Packet Zone, which it
 * configures as a Secondary Zone to the Mbuf Master Zone,
 * thus sharing backend Slab kegs with the Mbuf Master Zone.
 *
 * Thus common-case allocations and locking are simplified:
 *
 *  m_clget()                m_getcl()
 *    |                         |
 *    |   .------------>[(Packet Cache)]    m_get(), m_gethdr()
 *    |   |             [     Packet   ]            |
 *  [(Cluster Cache)]   [    Secondary ]   [ (Mbuf Cache)     ]
 *  [ Cluster Zone  ]   [     Zone     ]   [ Mbuf Master Zone ]
 *        |                       \________         |
 *  [ Cluster Keg   ]                      \       /
 *        |	                         [ Mbuf Keg   ]
 *  [ Cluster Slabs ]                         |
 *        |                              [ Mbuf Slabs ]
 *         \____________(VM)_________________/
 *
 *
 * Whenever an object is allocated with uma_zalloc() out of
 * one of the Zones its _ctor_ function is executed.  The same
 * for any deallocation through uma_zfree() the _dtor_ function
 * is executed.
 *
 * Caches are per-CPU and are filled from the Master Zone.
 *
 * Whenever an object is allocated from the underlying global
 * memory pool it gets pre-initialized with the _zinit_ functions.
 * When the Keg's are overfull objects get decommissioned with
 * _zfini_ functions and free'd back to the global memory pool.
 *
 */

int nmbufs;			/* limits number of mbufs */
int nmbclusters;		/* limits number of mbuf clusters */
int nmbjumbop;			/* limits number of page size jumbo clusters */
int nmbjumbo9;			/* limits number of 9k jumbo clusters */
int nmbjumbo16;			/* limits number of 16k jumbo clusters */

static quad_t maxmbufmem;	/* overall real memory limit for all mbufs */

SYSCTL_QUAD(_kern_ipc, OID_AUTO, maxmbufmem, CTLFLAG_RDTUN | CTLFLAG_NOFETCH, &maxmbufmem, 0,
    "Maximum real memory allocatable to various mbuf types");

/*
 * tunable_mbinit() has to be run before any mbuf allocations are done.
 */
static void
tunable_mbinit(void *dummy)
{
	quad_t realmem;

	/*
	 * The default limit for all mbuf related memory is 1/2 of all
	 * available kernel memory (physical or kmem).
	 * At most it can be 3/4 of available kernel memory.
	 */
	realmem = qmin((quad_t)physmem * PAGE_SIZE, vm_kmem_size);
	maxmbufmem = realmem / 2;
	TUNABLE_QUAD_FETCH("kern.ipc.maxmbufmem", &maxmbufmem);
	if (maxmbufmem > realmem / 4 * 3)
		maxmbufmem = realmem / 4 * 3;

	TUNABLE_INT_FETCH("kern.ipc.nmbclusters", &nmbclusters);
	if (nmbclusters == 0)
		nmbclusters = maxmbufmem / MCLBYTES / 4;

	TUNABLE_INT_FETCH("kern.ipc.nmbjumbop", &nmbjumbop);
	if (nmbjumbop == 0)
		nmbjumbop = maxmbufmem / MJUMPAGESIZE / 4;

	TUNABLE_INT_FETCH("kern.ipc.nmbjumbo9", &nmbjumbo9);
	if (nmbjumbo9 == 0)
		nmbjumbo9 = maxmbufmem / MJUM9BYTES / 6;

	TUNABLE_INT_FETCH("kern.ipc.nmbjumbo16", &nmbjumbo16);
	if (nmbjumbo16 == 0)
		nmbjumbo16 = maxmbufmem / MJUM16BYTES / 6;

	/*
	 * We need at least as many mbufs as we have clusters of
	 * the various types added together.
	 */
	TUNABLE_INT_FETCH("kern.ipc.nmbufs", &nmbufs);
	if (nmbufs < nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16)
		nmbufs = lmax(maxmbufmem / MSIZE / 5,
		    nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16);
}
SYSINIT(tunable_mbinit, SI_SUB_KMEM, SI_ORDER_MIDDLE, tunable_mbinit, NULL);

static int
sysctl_nmbclusters(SYSCTL_HANDLER_ARGS)
{
	int error, newnmbclusters;

	newnmbclusters = nmbclusters;
	error = sysctl_handle_int(oidp, &newnmbclusters, 0, req);
	if (error == 0 && req->newptr && newnmbclusters != nmbclusters) {
		if (newnmbclusters > nmbclusters &&
		    nmbufs >= nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16) {
			nmbclusters = newnmbclusters;
			nmbclusters = uma_zone_set_max(zone_clust, nmbclusters);
			EVENTHANDLER_INVOKE(nmbclusters_change);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, nmbclusters, CTLTYPE_INT|CTLFLAG_RW,
&nmbclusters, 0, sysctl_nmbclusters, "IU",
    "Maximum number of mbuf clusters allowed");

static int
sysctl_nmbjumbop(SYSCTL_HANDLER_ARGS)
{
	int error, newnmbjumbop;

	newnmbjumbop = nmbjumbop;
	error = sysctl_handle_int(oidp, &newnmbjumbop, 0, req);
	if (error == 0 && req->newptr && newnmbjumbop != nmbjumbop) {
		if (newnmbjumbop > nmbjumbop &&
		    nmbufs >= nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16) {
			nmbjumbop = newnmbjumbop;
			nmbjumbop = uma_zone_set_max(zone_jumbop, nmbjumbop);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, nmbjumbop, CTLTYPE_INT|CTLFLAG_RW,
&nmbjumbop, 0, sysctl_nmbjumbop, "IU",
    "Maximum number of mbuf page size jumbo clusters allowed");

static int
sysctl_nmbjumbo9(SYSCTL_HANDLER_ARGS)
{
	int error, newnmbjumbo9;

	newnmbjumbo9 = nmbjumbo9;
	error = sysctl_handle_int(oidp, &newnmbjumbo9, 0, req);
	if (error == 0 && req->newptr && newnmbjumbo9 != nmbjumbo9) {
		if (newnmbjumbo9 > nmbjumbo9 &&
		    nmbufs >= nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16) {
			nmbjumbo9 = newnmbjumbo9;
			nmbjumbo9 = uma_zone_set_max(zone_jumbo9, nmbjumbo9);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, nmbjumbo9, CTLTYPE_INT|CTLFLAG_RW,
&nmbjumbo9, 0, sysctl_nmbjumbo9, "IU",
    "Maximum number of mbuf 9k jumbo clusters allowed");

static int
sysctl_nmbjumbo16(SYSCTL_HANDLER_ARGS)
{
	int error, newnmbjumbo16;

	newnmbjumbo16 = nmbjumbo16;
	error = sysctl_handle_int(oidp, &newnmbjumbo16, 0, req);
	if (error == 0 && req->newptr && newnmbjumbo16 != nmbjumbo16) {
		if (newnmbjumbo16 > nmbjumbo16 &&
		    nmbufs >= nmbclusters + nmbjumbop + nmbjumbo9 + nmbjumbo16) {
			nmbjumbo16 = newnmbjumbo16;
			nmbjumbo16 = uma_zone_set_max(zone_jumbo16, nmbjumbo16);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, nmbjumbo16, CTLTYPE_INT|CTLFLAG_RW,
&nmbjumbo16, 0, sysctl_nmbjumbo16, "IU",
    "Maximum number of mbuf 16k jumbo clusters allowed");

static int
sysctl_nmbufs(SYSCTL_HANDLER_ARGS)
{
	int error, newnmbufs;

	newnmbufs = nmbufs;
	error = sysctl_handle_int(oidp, &newnmbufs, 0, req);
	if (error == 0 && req->newptr && newnmbufs != nmbufs) {
		if (newnmbufs > nmbufs) {
			nmbufs = newnmbufs;
			nmbufs = uma_zone_set_max(zone_mbuf, nmbufs);
			EVENTHANDLER_INVOKE(nmbufs_change);
		} else
			error = EINVAL;
	}
	return (error);
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, nmbufs, CTLTYPE_INT|CTLFLAG_RW,
&nmbufs, 0, sysctl_nmbufs, "IU",
    "Maximum number of mbufs allowed");

/*
 * Zones from which we allocate.
 */
uma_zone_t	zone_mbuf;
uma_zone_t	zone_clust;
uma_zone_t	zone_pack;
uma_zone_t	zone_jumbop;
uma_zone_t	zone_jumbo9;
uma_zone_t	zone_jumbo16;

/*
 * Local prototypes.
 */
static int	mb_ctor_mbuf(void *, int, void *, int);
static int	mb_ctor_clust(void *, int, void *, int);
static int	mb_ctor_pack(void *, int, void *, int);
static void	mb_dtor_mbuf(void *, int, void *);
static void	mb_dtor_pack(void *, int, void *);
static int	mb_zinit_pack(void *, int, int);
static void	mb_zfini_pack(void *, int);
static void	mb_reclaim(uma_zone_t, int);
static void    *mbuf_jumbo_alloc(uma_zone_t, vm_size_t, int, uint8_t *, int);

/* Ensure that MSIZE is a power of 2. */
CTASSERT((((MSIZE - 1) ^ MSIZE) + 1) >> 1 == MSIZE);

/*
 * Initialize FreeBSD Network buffer allocation.
 */
static void
mbuf_init(void *dummy)
{

	/*
	 * Configure UMA zones for Mbufs, Clusters, and Packets.
	 */
	zone_mbuf = uma_zcreate(MBUF_MEM_NAME, MSIZE,
	    mb_ctor_mbuf, mb_dtor_mbuf,
#ifdef INVARIANTS
	    trash_init, trash_fini,
#else
	    NULL, NULL,
#endif
	    MSIZE - 1, UMA_ZONE_MAXBUCKET);
	if (nmbufs > 0)
		nmbufs = uma_zone_set_max(zone_mbuf, nmbufs);
	uma_zone_set_warning(zone_mbuf, "kern.ipc.nmbufs limit reached");
	uma_zone_set_maxaction(zone_mbuf, mb_reclaim);

	zone_clust = uma_zcreate(MBUF_CLUSTER_MEM_NAME, MCLBYTES,
	    mb_ctor_clust,
#ifdef INVARIANTS
	    trash_dtor, trash_init, trash_fini,
#else
	    NULL, NULL, NULL,
#endif
	    UMA_ALIGN_PTR, 0);
	if (nmbclusters > 0)
		nmbclusters = uma_zone_set_max(zone_clust, nmbclusters);
	uma_zone_set_warning(zone_clust, "kern.ipc.nmbclusters limit reached");
	uma_zone_set_maxaction(zone_clust, mb_reclaim);

	zone_pack = uma_zsecond_create(MBUF_PACKET_MEM_NAME, mb_ctor_pack,
	    mb_dtor_pack, mb_zinit_pack, mb_zfini_pack, zone_mbuf);

	/* Make jumbo frame zone too. Page size, 9k and 16k. */
	zone_jumbop = uma_zcreate(MBUF_JUMBOP_MEM_NAME, MJUMPAGESIZE,
	    mb_ctor_clust,
#ifdef INVARIANTS
	    trash_dtor, trash_init, trash_fini,
#else
	    NULL, NULL, NULL,
#endif
	    UMA_ALIGN_PTR, 0);
	if (nmbjumbop > 0)
		nmbjumbop = uma_zone_set_max(zone_jumbop, nmbjumbop);
	uma_zone_set_warning(zone_jumbop, "kern.ipc.nmbjumbop limit reached");
	uma_zone_set_maxaction(zone_jumbop, mb_reclaim);

	zone_jumbo9 = uma_zcreate(MBUF_JUMBO9_MEM_NAME, MJUM9BYTES,
	    mb_ctor_clust,
#ifdef INVARIANTS
	    trash_dtor, trash_init, trash_fini,
#else
	    NULL, NULL, NULL,
#endif
	    UMA_ALIGN_PTR, 0);
	uma_zone_set_allocf(zone_jumbo9, mbuf_jumbo_alloc);
	if (nmbjumbo9 > 0)
		nmbjumbo9 = uma_zone_set_max(zone_jumbo9, nmbjumbo9);
	uma_zone_set_warning(zone_jumbo9, "kern.ipc.nmbjumbo9 limit reached");
	uma_zone_set_maxaction(zone_jumbo9, mb_reclaim);

	zone_jumbo16 = uma_zcreate(MBUF_JUMBO16_MEM_NAME, MJUM16BYTES,
	    mb_ctor_clust,
#ifdef INVARIANTS
	    trash_dtor, trash_init, trash_fini,
#else
	    NULL, NULL, NULL,
#endif
	    UMA_ALIGN_PTR, 0);
	uma_zone_set_allocf(zone_jumbo16, mbuf_jumbo_alloc);
	if (nmbjumbo16 > 0)
		nmbjumbo16 = uma_zone_set_max(zone_jumbo16, nmbjumbo16);
	uma_zone_set_warning(zone_jumbo16, "kern.ipc.nmbjumbo16 limit reached");
	uma_zone_set_maxaction(zone_jumbo16, mb_reclaim);

	/*
	 * Hook event handler for low-memory situation, used to
	 * drain protocols and push data back to the caches (UMA
	 * later pushes it back to VM).
	 */
	EVENTHANDLER_REGISTER(vm_lowmem, mb_reclaim, NULL,
	    EVENTHANDLER_PRI_FIRST);
}
SYSINIT(mbuf, SI_SUB_MBUF, SI_ORDER_FIRST, mbuf_init, NULL);

#ifdef NETDUMP
/*
 * netdump makes use of a pre-allocated pool of mbufs and clusters.  When
 * netdump is configured, we initialize a set of UMA cache zones which return
 * items from this pool.  At panic-time, the regular UMA zone pointers are
 * overwritten with those of the cache zones so that drivers may allocate and
 * free mbufs and clusters without attempting to allocate physical memory.
 *
 * We keep mbufs and clusters in a pair of mbuf queues.  In particular, for
 * the purpose of caching clusters, we treat them as mbufs.
 */
static struct mbufq nd_mbufq =
    { STAILQ_HEAD_INITIALIZER(nd_mbufq.mq_head), 0, INT_MAX };
static struct mbufq nd_clustq =
    { STAILQ_HEAD_INITIALIZER(nd_clustq.mq_head), 0, INT_MAX };

static int nd_clsize;
static uma_zone_t nd_zone_mbuf;
static uma_zone_t nd_zone_clust;
static uma_zone_t nd_zone_pack;

static int
nd_buf_import(void *arg, void **store, int count, int domain __unused,
    int flags)
{
	struct mbufq *q;
	struct mbuf *m;
	int i;

	q = arg;

	for (i = 0; i < count; i++) {
		m = mbufq_dequeue(q);
		if (m == NULL)
			break;
		trash_init(m, q == &nd_mbufq ? MSIZE : nd_clsize, flags);
		store[i] = m;
	}
	KASSERT((flags & M_WAITOK) == 0 || i == count,
	    ("%s: ran out of pre-allocated mbufs", __func__));
	return (i);
}

static void
nd_buf_release(void *arg, void **store, int count)
{
	struct mbufq *q;
	struct mbuf *m;
	int i;

	q = arg;

	for (i = 0; i < count; i++) {
		m = store[i];
		(void)mbufq_enqueue(q, m);
	}
}

static int
nd_pack_import(void *arg __unused, void **store, int count, int domain __unused,
    int flags __unused)
{
	struct mbuf *m;
	void *clust;
	int i;

	for (i = 0; i < count; i++) {
		m = m_get(MT_DATA, M_NOWAIT);
		if (m == NULL)
			break;
		clust = uma_zalloc(nd_zone_clust, M_NOWAIT);
		if (clust == NULL) {
			m_free(m);
			break;
		}
		mb_ctor_clust(clust, nd_clsize, m, 0);
		store[i] = m;
	}
	KASSERT((flags & M_WAITOK) == 0 || i == count,
	    ("%s: ran out of pre-allocated mbufs", __func__));
	return (i);
}

static void
nd_pack_release(void *arg __unused, void **store, int count)
{
	struct mbuf *m;
	void *clust;
	int i;

	for (i = 0; i < count; i++) {
		m = store[i];
		clust = m->m_ext.ext_buf;
		uma_zfree(nd_zone_clust, clust);
		uma_zfree(nd_zone_mbuf, m);
	}
}

/*
 * Free the pre-allocated mbufs and clusters reserved for netdump, and destroy
 * the corresponding UMA cache zones.
 */
void
netdump_mbuf_drain(void)
{
	struct mbuf *m;
	void *item;

	if (nd_zone_mbuf != NULL) {
		uma_zdestroy(nd_zone_mbuf);
		nd_zone_mbuf = NULL;
	}
	if (nd_zone_clust != NULL) {
		uma_zdestroy(nd_zone_clust);
		nd_zone_clust = NULL;
	}
	if (nd_zone_pack != NULL) {
		uma_zdestroy(nd_zone_pack);
		nd_zone_pack = NULL;
	}

	while ((m = mbufq_dequeue(&nd_mbufq)) != NULL)
		m_free(m);
	while ((item = mbufq_dequeue(&nd_clustq)) != NULL)
		uma_zfree(m_getzone(nd_clsize), item);
}

/*
 * Callback invoked immediately prior to starting a netdump.
 */
void
netdump_mbuf_dump(void)
{

	/*
	 * All cluster zones return buffers of the size requested by the
	 * drivers.  It's up to the driver to reinitialize the zones if the
	 * MTU of a netdump-enabled interface changes.
	 */
	printf("netdump: overwriting mbuf zone pointers\n");
	zone_mbuf = nd_zone_mbuf;
	zone_clust = nd_zone_clust;
	zone_pack = nd_zone_pack;
	zone_jumbop = nd_zone_clust;
	zone_jumbo9 = nd_zone_clust;
	zone_jumbo16 = nd_zone_clust;
}

/*
 * Reinitialize the netdump mbuf+cluster pool and cache zones.
 */
void
netdump_mbuf_reinit(int nmbuf, int nclust, int clsize)
{
	struct mbuf *m;
	void *item;

	netdump_mbuf_drain();

	nd_clsize = clsize;

	nd_zone_mbuf = uma_zcache_create("netdump_" MBUF_MEM_NAME,
	    MSIZE, mb_ctor_mbuf, mb_dtor_mbuf,
#ifdef INVARIANTS
	    trash_init, trash_fini,
#else
	    NULL, NULL,
#endif
	    nd_buf_import, nd_buf_release,
	    &nd_mbufq, UMA_ZONE_NOBUCKET);

	nd_zone_clust = uma_zcache_create("netdump_" MBUF_CLUSTER_MEM_NAME,
	    clsize, mb_ctor_clust,
#ifdef INVARIANTS
	    trash_dtor, trash_init, trash_fini,
#else
	    NULL, NULL, NULL,
#endif
	    nd_buf_import, nd_buf_release,
	    &nd_clustq, UMA_ZONE_NOBUCKET);

	nd_zone_pack = uma_zcache_create("netdump_" MBUF_PACKET_MEM_NAME,
	    MCLBYTES, mb_ctor_pack, mb_dtor_pack, NULL, NULL,
	    nd_pack_import, nd_pack_release,
	    NULL, UMA_ZONE_NOBUCKET);

	while (nmbuf-- > 0) {
		m = m_get(MT_DATA, M_WAITOK);
		uma_zfree(nd_zone_mbuf, m);
	}
	while (nclust-- > 0) {
		item = uma_zalloc(m_getzone(nd_clsize), M_WAITOK);
		uma_zfree(nd_zone_clust, item);
	}
}
#endif /* NETDUMP */

/*
 * UMA backend page allocator for the jumbo frame zones.
 *
 * Allocates kernel virtual memory that is backed by contiguous physical
 * pages.
 */
static void *
mbuf_jumbo_alloc(uma_zone_t zone, vm_size_t bytes, int domain, uint8_t *flags,
    int wait)
{

	/* Inform UMA that this allocator uses kernel_map/object. */
	*flags = UMA_SLAB_KERNEL;
	return ((void *)kmem_alloc_contig_domainset(DOMAINSET_FIXED(domain),
	    bytes, wait, (vm_paddr_t)0, ~(vm_paddr_t)0, 1, 0,
	    VM_MEMATTR_DEFAULT));
}

/*
 * Constructor for Mbuf master zone.
 *
 * The 'arg' pointer points to a mb_args structure which
 * contains call-specific information required to support the
 * mbuf allocation API.  See mbuf.h.
 */
static int
mb_ctor_mbuf(void *mem, int size, void *arg, int how)
{
	struct mbuf *m;
	struct mb_args *args;
	int error;
	int flags;
	short type;

#ifdef INVARIANTS
	trash_ctor(mem, size, arg, how);
#endif
	args = (struct mb_args *)arg;
	type = args->type;

	/*
	 * The mbuf is initialized later.  The caller has the
	 * responsibility to set up any MAC labels too.
	 */
	if (type == MT_NOINIT)
		return (0);

	m = (struct mbuf *)mem;
	flags = args->flags;
	MPASS((flags & M_NOFREE) == 0);

	error = m_init(m, how, type, flags);

	return (error);
}

/*
 * The Mbuf master zone destructor.
 */
static void
mb_dtor_mbuf(void *mem, int size, void *arg)
{
	struct mbuf *m;
	unsigned long flags;

	m = (struct mbuf *)mem;
	flags = (unsigned long)arg;

	KASSERT((m->m_flags & M_NOFREE) == 0, ("%s: M_NOFREE set", __func__));
	if (!(flags & MB_DTOR_SKIP) && (m->m_flags & M_PKTHDR) && !SLIST_EMPTY(&m->m_pkthdr.tags))
		m_tag_delete_chain(m, NULL);
#ifdef INVARIANTS
	trash_dtor(mem, size, arg);
#endif
}

/*
 * The Mbuf Packet zone destructor.
 */
static void
mb_dtor_pack(void *mem, int size, void *arg)
{
	struct mbuf *m;

	m = (struct mbuf *)mem;
	if ((m->m_flags & M_PKTHDR) != 0)
		m_tag_delete_chain(m, NULL);

	/* Make sure we've got a clean cluster back. */
	KASSERT((m->m_flags & M_EXT) == M_EXT, ("%s: M_EXT not set", __func__));
	KASSERT(m->m_ext.ext_buf != NULL, ("%s: ext_buf == NULL", __func__));
	KASSERT(m->m_ext.ext_free == NULL, ("%s: ext_free != NULL", __func__));
	KASSERT(m->m_ext.ext_arg1 == NULL, ("%s: ext_arg1 != NULL", __func__));
	KASSERT(m->m_ext.ext_arg2 == NULL, ("%s: ext_arg2 != NULL", __func__));
	KASSERT(m->m_ext.ext_size == MCLBYTES, ("%s: ext_size != MCLBYTES", __func__));
	KASSERT(m->m_ext.ext_type == EXT_PACKET, ("%s: ext_type != EXT_PACKET", __func__));
#ifdef INVARIANTS
	trash_dtor(m->m_ext.ext_buf, MCLBYTES, arg);
#endif
	/*
	 * If there are processes blocked on zone_clust, waiting for pages
	 * to be freed up, * cause them to be woken up by draining the
	 * packet zone.  We are exposed to a race here * (in the check for
	 * the UMA_ZFLAG_FULL) where we might miss the flag set, but that
	 * is deliberate. We don't want to acquire the zone lock for every
	 * mbuf free.
	 */
	if (uma_zone_exhausted_nolock(zone_clust))
		zone_drain(zone_pack);
}

/*
 * The Cluster and Jumbo[PAGESIZE|9|16] zone constructor.
 *
 * Here the 'arg' pointer points to the Mbuf which we
 * are configuring cluster storage for.  If 'arg' is
 * empty we allocate just the cluster without setting
 * the mbuf to it.  See mbuf.h.
 */
static int
mb_ctor_clust(void *mem, int size, void *arg, int how)
{
	struct mbuf *m;

#ifdef INVARIANTS
	trash_ctor(mem, size, arg, how);
#endif
	m = (struct mbuf *)arg;
	if (m != NULL) {
		m->m_ext.ext_buf = (char *)mem;
		m->m_data = m->m_ext.ext_buf;
		m->m_flags |= M_EXT;
		m->m_ext.ext_free = NULL;
		m->m_ext.ext_arg1 = NULL;
		m->m_ext.ext_arg2 = NULL;
		m->m_ext.ext_size = size;
		m->m_ext.ext_type = m_gettype(size);
		m->m_ext.ext_flags = EXT_FLAG_EMBREF;
		m->m_ext.ext_count = 1;
	}

	return (0);
}

/*
 * The Packet secondary zone's init routine, executed on the
 * object's transition from mbuf keg slab to zone cache.
 */
static int
mb_zinit_pack(void *mem, int size, int how)
{
	struct mbuf *m;

	m = (struct mbuf *)mem;		/* m is virgin. */
	if (uma_zalloc_arg(zone_clust, m, how) == NULL ||
	    m->m_ext.ext_buf == NULL)
		return (ENOMEM);
	m->m_ext.ext_type = EXT_PACKET;	/* Override. */
#ifdef INVARIANTS
	trash_init(m->m_ext.ext_buf, MCLBYTES, how);
#endif
	return (0);
}

/*
 * The Packet secondary zone's fini routine, executed on the
 * object's transition from zone cache to keg slab.
 */
static void
mb_zfini_pack(void *mem, int size)
{
	struct mbuf *m;

	m = (struct mbuf *)mem;
#ifdef INVARIANTS
	trash_fini(m->m_ext.ext_buf, MCLBYTES);
#endif
	uma_zfree_arg(zone_clust, m->m_ext.ext_buf, NULL);
#ifdef INVARIANTS
	trash_dtor(mem, size, NULL);
#endif
}

/*
 * The "packet" keg constructor.
 */
static int
mb_ctor_pack(void *mem, int size, void *arg, int how)
{
	struct mbuf *m;
	struct mb_args *args;
	int error, flags;
	short type;

	m = (struct mbuf *)mem;
	args = (struct mb_args *)arg;
	flags = args->flags;
	type = args->type;
	MPASS((flags & M_NOFREE) == 0);

#ifdef INVARIANTS
	trash_ctor(m->m_ext.ext_buf, MCLBYTES, arg, how);
#endif

	error = m_init(m, how, type, flags);

	/* m_ext is already initialized. */
	m->m_data = m->m_ext.ext_buf;
 	m->m_flags = (flags | M_EXT);

	return (error);
}

/*
 * This is the protocol drain routine.  Called by UMA whenever any of the
 * mbuf zones is closed to its limit.
 *
 * No locks should be held when this is called.  The drain routines have to
 * presently acquire some locks which raises the possibility of lock order
 * reversal.
 */
static void
mb_reclaim(uma_zone_t zone __unused, int pending __unused)
{
	struct domain *dp;
	struct protosw *pr;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK | WARN_PANIC, NULL, __func__);

	for (dp = domains; dp != NULL; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_drain != NULL)
				(*pr->pr_drain)();
}

/*
 * Clean up after mbufs with M_EXT storage attached to them if the
 * reference count hits 1.
 */
void
mb_free_ext(struct mbuf *m)
{
	volatile u_int *refcnt;
	struct mbuf *mref;
	int freembuf;

	KASSERT(m->m_flags & M_EXT, ("%s: M_EXT not set on %p", __func__, m));

	/* See if this is the mbuf that holds the embedded refcount. */
	if (m->m_ext.ext_flags & EXT_FLAG_EMBREF) {
		refcnt = &m->m_ext.ext_count;
		mref = m;
	} else {
		KASSERT(m->m_ext.ext_cnt != NULL,
		    ("%s: no refcounting pointer on %p", __func__, m));
		refcnt = m->m_ext.ext_cnt;
		mref = __containerof(refcnt, struct mbuf, m_ext.ext_count);
	}

	/*
	 * Check if the header is embedded in the cluster.  It is
	 * important that we can't touch any of the mbuf fields
	 * after we have freed the external storage, since mbuf
	 * could have been embedded in it.  For now, the mbufs
	 * embedded into the cluster are always of type EXT_EXTREF,
	 * and for this type we won't free the mref.
	 */
	if (m->m_flags & M_NOFREE) {
		freembuf = 0;
		KASSERT(m->m_ext.ext_type == EXT_EXTREF ||
		    m->m_ext.ext_type == EXT_RXRING,
		    ("%s: no-free mbuf %p has wrong type", __func__, m));
	} else
		freembuf = 1;

	/* Free attached storage if this mbuf is the only reference to it. */
	if (*refcnt == 1 || atomic_fetchadd_int(refcnt, -1) == 1) {
		switch (m->m_ext.ext_type) {
		case EXT_PACKET:
			/* The packet zone is special. */
			if (*refcnt == 0)
				*refcnt = 1;
			uma_zfree(zone_pack, mref);
			break;
		case EXT_CLUSTER:
			uma_zfree(zone_clust, m->m_ext.ext_buf);
			uma_zfree(zone_mbuf, mref);
			break;
		case EXT_JUMBOP:
			uma_zfree(zone_jumbop, m->m_ext.ext_buf);
			uma_zfree(zone_mbuf, mref);
			break;
		case EXT_JUMBO9:
			uma_zfree(zone_jumbo9, m->m_ext.ext_buf);
			uma_zfree(zone_mbuf, mref);
			break;
		case EXT_JUMBO16:
			uma_zfree(zone_jumbo16, m->m_ext.ext_buf);
			uma_zfree(zone_mbuf, mref);
			break;
		case EXT_SFBUF:
		case EXT_NET_DRV:
		case EXT_MOD_TYPE:
		case EXT_DISPOSABLE:
			KASSERT(mref->m_ext.ext_free != NULL,
			    ("%s: ext_free not set", __func__));
			mref->m_ext.ext_free(mref);
			uma_zfree(zone_mbuf, mref);
			break;
		case EXT_EXTREF:
			KASSERT(m->m_ext.ext_free != NULL,
			    ("%s: ext_free not set", __func__));
			m->m_ext.ext_free(m);
			break;
		case EXT_RXRING:
			KASSERT(m->m_ext.ext_free == NULL,
			    ("%s: ext_free is set", __func__));
			break;
		default:
			KASSERT(m->m_ext.ext_type == 0,
			    ("%s: unknown ext_type", __func__));
		}
	}

	if (freembuf && m != mref)
		uma_zfree(zone_mbuf, m);
}

/*
 * Official mbuf(9) allocation KPI for stack and drivers:
 *
 * m_get()	- a single mbuf without any attachments, sys/mbuf.h.
 * m_gethdr()	- a single mbuf initialized as M_PKTHDR, sys/mbuf.h.
 * m_getcl()	- an mbuf + 2k cluster, sys/mbuf.h.
 * m_clget()	- attach cluster to already allocated mbuf.
 * m_cljget()	- attach jumbo cluster to already allocated mbuf.
 * m_get2()	- allocate minimum mbuf that would fit size argument.
 * m_getm2()	- allocate a chain of mbufs/clusters.
 * m_extadd()	- attach external cluster to mbuf.
 *
 * m_free()	- free single mbuf with its tags and ext, sys/mbuf.h.
 * m_freem()	- free chain of mbufs.
 */

int
m_clget(struct mbuf *m, int how)
{

	KASSERT((m->m_flags & M_EXT) == 0, ("%s: mbuf %p has M_EXT",
	    __func__, m));
	m->m_ext.ext_buf = (char *)NULL;
	uma_zalloc_arg(zone_clust, m, how);
	/*
	 * On a cluster allocation failure, drain the packet zone and retry,
	 * we might be able to loosen a few clusters up on the drain.
	 */
	if ((how & M_NOWAIT) && (m->m_ext.ext_buf == NULL)) {
		zone_drain(zone_pack);
		uma_zalloc_arg(zone_clust, m, how);
	}
	MBUF_PROBE2(m__clget, m, how);
	return (m->m_flags & M_EXT);
}

/*
 * m_cljget() is different from m_clget() as it can allocate clusters without
 * attaching them to an mbuf.  In that case the return value is the pointer
 * to the cluster of the requested size.  If an mbuf was specified, it gets
 * the cluster attached to it and the return value can be safely ignored.
 * For size it takes MCLBYTES, MJUMPAGESIZE, MJUM9BYTES, MJUM16BYTES.
 */
void *
m_cljget(struct mbuf *m, int how, int size)
{
	uma_zone_t zone;
	void *retval;

	if (m != NULL) {
		KASSERT((m->m_flags & M_EXT) == 0, ("%s: mbuf %p has M_EXT",
		    __func__, m));
		m->m_ext.ext_buf = NULL;
	}

	zone = m_getzone(size);
	retval = uma_zalloc_arg(zone, m, how);

	MBUF_PROBE4(m__cljget, m, how, size, retval);

	return (retval);
}

/*
 * m_get2() allocates minimum mbuf that would fit "size" argument.
 */
struct mbuf *
m_get2(int size, int how, short type, int flags)
{
	struct mb_args args;
	struct mbuf *m, *n;

	args.flags = flags;
	args.type = type;

	if (size <= MHLEN || (size <= MLEN && (flags & M_PKTHDR) == 0))
		return (uma_zalloc_arg(zone_mbuf, &args, how));
	if (size <= MCLBYTES)
		return (uma_zalloc_arg(zone_pack, &args, how));

	if (size > MJUMPAGESIZE)
		return (NULL);

	m = uma_zalloc_arg(zone_mbuf, &args, how);
	if (m == NULL)
		return (NULL);

	n = uma_zalloc_arg(zone_jumbop, m, how);
	if (n == NULL) {
		uma_zfree(zone_mbuf, m);
		return (NULL);
	}

	return (m);
}

/*
 * m_getjcl() returns an mbuf with a cluster of the specified size attached.
 * For size it takes MCLBYTES, MJUMPAGESIZE, MJUM9BYTES, MJUM16BYTES.
 */
struct mbuf *
m_getjcl(int how, short type, int flags, int size)
{
	struct mb_args args;
	struct mbuf *m, *n;
	uma_zone_t zone;

	if (size == MCLBYTES)
		return m_getcl(how, type, flags);

	args.flags = flags;
	args.type = type;

	m = uma_zalloc_arg(zone_mbuf, &args, how);
	if (m == NULL)
		return (NULL);

	zone = m_getzone(size);
	n = uma_zalloc_arg(zone, m, how);
	if (n == NULL) {
		uma_zfree(zone_mbuf, m);
		return (NULL);
	}
	return (m);
}

/*
 * Allocate a given length worth of mbufs and/or clusters (whatever fits
 * best) and return a pointer to the top of the allocated chain.  If an
 * existing mbuf chain is provided, then we will append the new chain
 * to the existing one and return a pointer to the provided mbuf.
 */
struct mbuf *
m_getm2(struct mbuf *m, int len, int how, short type, int flags)
{
	struct mbuf *mb, *nm = NULL, *mtail = NULL;

	KASSERT(len >= 0, ("%s: len is < 0", __func__));

	/* Validate flags. */
	flags &= (M_PKTHDR | M_EOR);

	/* Packet header mbuf must be first in chain. */
	if ((flags & M_PKTHDR) && m != NULL)
		flags &= ~M_PKTHDR;

	/* Loop and append maximum sized mbufs to the chain tail. */
	while (len > 0) {
		if (len > MCLBYTES)
			mb = m_getjcl(how, type, (flags & M_PKTHDR),
			    MJUMPAGESIZE);
		else if (len >= MINCLSIZE)
			mb = m_getcl(how, type, (flags & M_PKTHDR));
		else if (flags & M_PKTHDR)
			mb = m_gethdr(how, type);
		else
			mb = m_get(how, type);

		/* Fail the whole operation if one mbuf can't be allocated. */
		if (mb == NULL) {
			if (nm != NULL)
				m_freem(nm);
			return (NULL);
		}

		/* Book keeping. */
		len -= M_SIZE(mb);
		if (mtail != NULL)
			mtail->m_next = mb;
		else
			nm = mb;
		mtail = mb;
		flags &= ~M_PKTHDR;	/* Only valid on the first mbuf. */
	}
	if (flags & M_EOR)
		mtail->m_flags |= M_EOR;  /* Only valid on the last mbuf. */

	/* If mbuf was supplied, append new chain to the end of it. */
	if (m != NULL) {
		for (mtail = m; mtail->m_next != NULL; mtail = mtail->m_next)
			;
		mtail->m_next = nm;
		mtail->m_flags &= ~M_EOR;
	} else
		m = nm;

	return (m);
}

/*-
 * Configure a provided mbuf to refer to the provided external storage
 * buffer and setup a reference count for said buffer.
 *
 * Arguments:
 *    mb     The existing mbuf to which to attach the provided buffer.
 *    buf    The address of the provided external storage buffer.
 *    size   The size of the provided buffer.
 *    freef  A pointer to a routine that is responsible for freeing the
 *           provided external storage buffer.
 *    args   A pointer to an argument structure (of any type) to be passed
 *           to the provided freef routine (may be NULL).
 *    flags  Any other flags to be passed to the provided mbuf.
 *    type   The type that the external storage buffer should be
 *           labeled with.
 *
 * Returns:
 *    Nothing.
 */
void
m_extadd(struct mbuf *mb, char *buf, u_int size, m_ext_free_t freef,
    void *arg1, void *arg2, int flags, int type)
{

	KASSERT(type != EXT_CLUSTER, ("%s: EXT_CLUSTER not allowed", __func__));

	mb->m_flags |= (M_EXT | flags);
	mb->m_ext.ext_buf = buf;
	mb->m_data = mb->m_ext.ext_buf;
	mb->m_ext.ext_size = size;
	mb->m_ext.ext_free = freef;
	mb->m_ext.ext_arg1 = arg1;
	mb->m_ext.ext_arg2 = arg2;
	mb->m_ext.ext_type = type;

	if (type != EXT_EXTREF) {
		mb->m_ext.ext_count = 1;
		mb->m_ext.ext_flags = EXT_FLAG_EMBREF;
	} else
		mb->m_ext.ext_flags = 0;
}

/*
 * Free an entire chain of mbufs and associated external buffers, if
 * applicable.
 */
void
m_freem(struct mbuf *mb)
{

	MBUF_PROBE1(m__freem, mb);
	while (mb != NULL)
		mb = m_free(mb);
}
