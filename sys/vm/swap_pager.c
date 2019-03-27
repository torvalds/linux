/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1998 Matthew Dillon,
 * Copyright (c) 1994 John S. Dyson
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *				New Swap System
 *				Matthew Dillon
 *
 * Radix Bitmap 'blists'.
 *
 *	- The new swapper uses the new radix bitmap code.  This should scale
 *	  to arbitrarily small or arbitrarily large swap spaces and an almost
 *	  arbitrary degree of fragmentation.
 *
 * Features:
 *
 *	- on the fly reallocation of swap during putpages.  The new system
 *	  does not try to keep previously allocated swap blocks for dirty
 *	  pages.
 *
 *	- on the fly deallocation of swap
 *
 *	- No more garbage collection required.  Unnecessarily allocated swap
 *	  blocks only exist for dirty vm_page_t's now and these are already
 *	  cycled (in a high-load system) by the pager.  We also do on-the-fly
 *	  removal of invalidated swap blocks when a page is destroyed
 *	  or renamed.
 *
 * from: Utah $Hdr: swap_pager.c 1.4 91/04/30$
 *
 *	@(#)swap_pager.c	8.9 (Berkeley) 3/21/94
 *	@(#)vm_swap.c	8.5 (Berkeley) 2/17/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/pctrie.h>
#include <sys/racct.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/blist.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/vmmeter.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_pageout.h>
#include <vm/vm_param.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

#include <geom/geom.h>

/*
 * MAX_PAGEOUT_CLUSTER must be a power of 2 between 1 and 64.
 * The 64-page limit is due to the radix code (kern/subr_blist.c).
 */
#ifndef MAX_PAGEOUT_CLUSTER
#define	MAX_PAGEOUT_CLUSTER	32
#endif

#if !defined(SWB_NPAGES)
#define SWB_NPAGES	MAX_PAGEOUT_CLUSTER
#endif

#define	SWAP_META_PAGES		PCTRIE_COUNT

/*
 * A swblk structure maps each page index within a
 * SWAP_META_PAGES-aligned and sized range to the address of an
 * on-disk swap block (or SWAPBLK_NONE). The collection of these
 * mappings for an entire vm object is implemented as a pc-trie.
 */
struct swblk {
	vm_pindex_t	p;
	daddr_t		d[SWAP_META_PAGES];
};

static MALLOC_DEFINE(M_VMPGDATA, "vm_pgdata", "swap pager private data");
static struct mtx sw_dev_mtx;
static TAILQ_HEAD(, swdevt) swtailq = TAILQ_HEAD_INITIALIZER(swtailq);
static struct swdevt *swdevhd;	/* Allocate from here next */
static int nswapdev;		/* Number of swap devices */
int swap_pager_avail;
static struct sx swdev_syscall_lock;	/* serialize swap(on|off) */

static u_long swap_reserved;
static u_long swap_total;
static int sysctl_page_shift(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, swap_reserved, CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &swap_reserved, 0, sysctl_page_shift, "A", 
    "Amount of swap storage needed to back all allocated anonymous memory.");
SYSCTL_PROC(_vm, OID_AUTO, swap_total, CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &swap_total, 0, sysctl_page_shift, "A", 
    "Total amount of available swap storage.");

static int overcommit = 0;
SYSCTL_INT(_vm, VM_OVERCOMMIT, overcommit, CTLFLAG_RW, &overcommit, 0,
    "Configure virtual memory overcommit behavior. See tuning(7) "
    "for details.");
static unsigned long swzone;
SYSCTL_ULONG(_vm, OID_AUTO, swzone, CTLFLAG_RD, &swzone, 0,
    "Actual size of swap metadata zone");
static unsigned long swap_maxpages;
SYSCTL_ULONG(_vm, OID_AUTO, swap_maxpages, CTLFLAG_RD, &swap_maxpages, 0,
    "Maximum amount of swap supported");

/* bits from overcommit */
#define	SWAP_RESERVE_FORCE_ON		(1 << 0)
#define	SWAP_RESERVE_RLIMIT_ON		(1 << 1)
#define	SWAP_RESERVE_ALLOW_NONWIRED	(1 << 2)

static int
sysctl_page_shift(SYSCTL_HANDLER_ARGS)
{
	uint64_t newval;
	u_long value = *(u_long *)arg1;

	newval = ((uint64_t)value) << PAGE_SHIFT;
	return (sysctl_handle_64(oidp, &newval, 0, req));
}

int
swap_reserve(vm_ooffset_t incr)
{

	return (swap_reserve_by_cred(incr, curthread->td_ucred));
}

int
swap_reserve_by_cred(vm_ooffset_t incr, struct ucred *cred)
{
	u_long r, s, prev, pincr;
	int res, error;
	static int curfail;
	static struct timeval lastfail;
	struct uidinfo *uip;

	uip = cred->cr_ruidinfo;

	KASSERT((incr & PAGE_MASK) == 0, ("%s: incr: %ju & PAGE_MASK", __func__,
	    (uintmax_t)incr));

#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(curproc);
		error = racct_add(curproc, RACCT_SWAP, incr);
		PROC_UNLOCK(curproc);
		if (error != 0)
			return (0);
	}
#endif

	pincr = atop(incr);
	res = 0;
	prev = atomic_fetchadd_long(&swap_reserved, pincr);
	r = prev + pincr;
	if (overcommit & SWAP_RESERVE_ALLOW_NONWIRED) {
		s = vm_cnt.v_page_count - vm_cnt.v_free_reserved -
		    vm_wire_count();
	} else
		s = 0;
	s += swap_total;
	if ((overcommit & SWAP_RESERVE_FORCE_ON) == 0 || r <= s ||
	    (error = priv_check(curthread, PRIV_VM_SWAP_NOQUOTA)) == 0) {
		res = 1;
	} else {
		prev = atomic_fetchadd_long(&swap_reserved, -pincr);
		if (prev < pincr)
			panic("swap_reserved < incr on overcommit fail");
	}
	if (res) {
		prev = atomic_fetchadd_long(&uip->ui_vmsize, pincr);
		if ((overcommit & SWAP_RESERVE_RLIMIT_ON) != 0 &&
		    prev + pincr > lim_cur(curthread, RLIMIT_SWAP) &&
		    priv_check(curthread, PRIV_VM_SWAP_NORLIMIT)) {
			res = 0;
			prev = atomic_fetchadd_long(&uip->ui_vmsize, -pincr);
			if (prev < pincr)
				panic("uip->ui_vmsize < incr on overcommit fail");
		}
	}
	if (!res && ppsratecheck(&lastfail, &curfail, 1)) {
		printf("uid %d, pid %d: swap reservation for %jd bytes failed\n",
		    uip->ui_uid, curproc->p_pid, incr);
	}

#ifdef RACCT
	if (racct_enable && !res) {
		PROC_LOCK(curproc);
		racct_sub(curproc, RACCT_SWAP, incr);
		PROC_UNLOCK(curproc);
	}
#endif

	return (res);
}

void
swap_reserve_force(vm_ooffset_t incr)
{
	struct uidinfo *uip;
	u_long pincr;

	KASSERT((incr & PAGE_MASK) == 0, ("%s: incr: %ju & PAGE_MASK", __func__,
	    (uintmax_t)incr));

	PROC_LOCK(curproc);
#ifdef RACCT
	if (racct_enable)
		racct_add_force(curproc, RACCT_SWAP, incr);
#endif
	pincr = atop(incr);
	atomic_add_long(&swap_reserved, pincr);
	uip = curproc->p_ucred->cr_ruidinfo;
	atomic_add_long(&uip->ui_vmsize, pincr);
	PROC_UNLOCK(curproc);
}

void
swap_release(vm_ooffset_t decr)
{
	struct ucred *cred;

	PROC_LOCK(curproc);
	cred = curproc->p_ucred;
	swap_release_by_cred(decr, cred);
	PROC_UNLOCK(curproc);
}

void
swap_release_by_cred(vm_ooffset_t decr, struct ucred *cred)
{
	u_long prev, pdecr;
 	struct uidinfo *uip;

	uip = cred->cr_ruidinfo;

	KASSERT((decr & PAGE_MASK) == 0, ("%s: decr: %ju & PAGE_MASK", __func__,
	    (uintmax_t)decr));

	pdecr = atop(decr);
	prev = atomic_fetchadd_long(&swap_reserved, -pdecr);
	if (prev < pdecr)
		panic("swap_reserved < decr");

	prev = atomic_fetchadd_long(&uip->ui_vmsize, -pdecr);
	if (prev < pdecr)
		printf("negative vmsize for uid = %d\n", uip->ui_uid);
#ifdef RACCT
	if (racct_enable)
		racct_sub_cred(cred, RACCT_SWAP, decr);
#endif
}

#define SWM_POP		0x01	/* pop out			*/

static int swap_pager_full = 2;	/* swap space exhaustion (task killing) */
static int swap_pager_almost_full = 1; /* swap space exhaustion (w/hysteresis)*/
static struct mtx swbuf_mtx;	/* to sync nsw_wcount_async */
static int nsw_wcount_async;	/* limit async write buffers */
static int nsw_wcount_async_max;/* assigned maximum			*/
static int nsw_cluster_max;	/* maximum VOP I/O allowed		*/

static int sysctl_swap_async_max(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, swap_async_max, CTLTYPE_INT | CTLFLAG_RW |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_swap_async_max, "I",
    "Maximum running async swap ops");
static int sysctl_swap_fragmentation(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, swap_fragmentation, CTLTYPE_STRING | CTLFLAG_RD |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_swap_fragmentation, "A",
    "Swap Fragmentation Info");

static struct sx sw_alloc_sx;

/*
 * "named" and "unnamed" anon region objects.  Try to reduce the overhead
 * of searching a named list by hashing it just a little.
 */

#define NOBJLISTS		8

#define NOBJLIST(handle)	\
	(&swap_pager_object_list[((int)(intptr_t)handle >> 4) & (NOBJLISTS-1)])

static struct pagerlst	swap_pager_object_list[NOBJLISTS];
static uma_zone_t swwbuf_zone;
static uma_zone_t swrbuf_zone;
static uma_zone_t swblk_zone;
static uma_zone_t swpctrie_zone;

/*
 * pagerops for OBJT_SWAP - "swap pager".  Some ops are also global procedure
 * calls hooked from other parts of the VM system and do not appear here.
 * (see vm/swap_pager.h).
 */
static vm_object_t
		swap_pager_alloc(void *handle, vm_ooffset_t size,
		    vm_prot_t prot, vm_ooffset_t offset, struct ucred *);
static void	swap_pager_dealloc(vm_object_t object);
static int	swap_pager_getpages(vm_object_t, vm_page_t *, int, int *,
    int *);
static int	swap_pager_getpages_async(vm_object_t, vm_page_t *, int, int *,
    int *, pgo_getpages_iodone_t, void *);
static void	swap_pager_putpages(vm_object_t, vm_page_t *, int, boolean_t, int *);
static boolean_t
		swap_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *before, int *after);
static void	swap_pager_init(void);
static void	swap_pager_unswapped(vm_page_t);
static void	swap_pager_swapoff(struct swdevt *sp);

struct pagerops swappagerops = {
	.pgo_init =	swap_pager_init,	/* early system initialization of pager	*/
	.pgo_alloc =	swap_pager_alloc,	/* allocate an OBJT_SWAP object		*/
	.pgo_dealloc =	swap_pager_dealloc,	/* deallocate an OBJT_SWAP object	*/
	.pgo_getpages =	swap_pager_getpages,	/* pagein				*/
	.pgo_getpages_async = swap_pager_getpages_async, /* pagein (async)		*/
	.pgo_putpages =	swap_pager_putpages,	/* pageout				*/
	.pgo_haspage =	swap_pager_haspage,	/* get backing store status for page	*/
	.pgo_pageunswapped = swap_pager_unswapped,	/* remove swap related to page		*/
};

/*
 * swap_*() routines are externally accessible.  swp_*() routines are
 * internal.
 */
static int nswap_lowat = 128;	/* in pages, swap_pager_almost_full warn */
static int nswap_hiwat = 512;	/* in pages, swap_pager_almost_full warn */

SYSCTL_INT(_vm, OID_AUTO, dmmax, CTLFLAG_RD, &nsw_cluster_max, 0,
    "Maximum size of a swap block in pages");

static void	swp_sizecheck(void);
static void	swp_pager_async_iodone(struct buf *bp);
static bool	swp_pager_swblk_empty(struct swblk *sb, int start, int limit);
static int	swapongeom(struct vnode *);
static int	swaponvp(struct thread *, struct vnode *, u_long);
static int	swapoff_one(struct swdevt *sp, struct ucred *cred);

/*
 * Swap bitmap functions
 */
static void	swp_pager_freeswapspace(daddr_t blk, daddr_t npages);
static daddr_t	swp_pager_getswapspace(int npages);

/*
 * Metadata functions
 */
static daddr_t swp_pager_meta_build(vm_object_t, vm_pindex_t, daddr_t);
static void swp_pager_meta_free(vm_object_t, vm_pindex_t, vm_pindex_t);
static void swp_pager_meta_free_all(vm_object_t);
static daddr_t swp_pager_meta_ctl(vm_object_t, vm_pindex_t, int);

static void
swp_pager_init_freerange(daddr_t *start, daddr_t *num)
{

	*start = SWAPBLK_NONE;
	*num = 0;
}

static void
swp_pager_update_freerange(daddr_t *start, daddr_t *num, daddr_t addr)
{

	if (*start + *num == addr) {
		(*num)++;
	} else {
		swp_pager_freeswapspace(*start, *num);
		*start = addr;
		*num = 1;
	}
}

static void *
swblk_trie_alloc(struct pctrie *ptree)
{

	return (uma_zalloc(swpctrie_zone, M_NOWAIT | (curproc == pageproc ?
	    M_USE_RESERVE : 0)));
}

static void
swblk_trie_free(struct pctrie *ptree, void *node)
{

	uma_zfree(swpctrie_zone, node);
}

PCTRIE_DEFINE(SWAP, swblk, p, swblk_trie_alloc, swblk_trie_free);

/*
 * SWP_SIZECHECK() -	update swap_pager_full indication
 *
 *	update the swap_pager_almost_full indication and warn when we are
 *	about to run out of swap space, using lowat/hiwat hysteresis.
 *
 *	Clear swap_pager_full ( task killing ) indication when lowat is met.
 *
 *	No restrictions on call
 *	This routine may not block.
 */
static void
swp_sizecheck(void)
{

	if (swap_pager_avail < nswap_lowat) {
		if (swap_pager_almost_full == 0) {
			printf("swap_pager: out of swap space\n");
			swap_pager_almost_full = 1;
		}
	} else {
		swap_pager_full = 0;
		if (swap_pager_avail > nswap_hiwat)
			swap_pager_almost_full = 0;
	}
}

/*
 * SWAP_PAGER_INIT() -	initialize the swap pager!
 *
 *	Expected to be started from system init.  NOTE:  This code is run
 *	before much else so be careful what you depend on.  Most of the VM
 *	system has yet to be initialized at this point.
 */
static void
swap_pager_init(void)
{
	/*
	 * Initialize object lists
	 */
	int i;

	for (i = 0; i < NOBJLISTS; ++i)
		TAILQ_INIT(&swap_pager_object_list[i]);
	mtx_init(&sw_dev_mtx, "swapdev", NULL, MTX_DEF);
	sx_init(&sw_alloc_sx, "swspsx");
	sx_init(&swdev_syscall_lock, "swsysc");
}

/*
 * SWAP_PAGER_SWAP_INIT() - swap pager initialization from pageout process
 *
 *	Expected to be started from pageout process once, prior to entering
 *	its main loop.
 */
void
swap_pager_swap_init(void)
{
	unsigned long n, n2;

	/*
	 * Number of in-transit swap bp operations.  Don't
	 * exhaust the pbufs completely.  Make sure we
	 * initialize workable values (0 will work for hysteresis
	 * but it isn't very efficient).
	 *
	 * The nsw_cluster_max is constrained by the bp->b_pages[]
	 * array (MAXPHYS/PAGE_SIZE) and our locally defined
	 * MAX_PAGEOUT_CLUSTER.   Also be aware that swap ops are
	 * constrained by the swap device interleave stripe size.
	 *
	 * Currently we hardwire nsw_wcount_async to 4.  This limit is
	 * designed to prevent other I/O from having high latencies due to
	 * our pageout I/O.  The value 4 works well for one or two active swap
	 * devices but is probably a little low if you have more.  Even so,
	 * a higher value would probably generate only a limited improvement
	 * with three or four active swap devices since the system does not
	 * typically have to pageout at extreme bandwidths.   We will want
	 * at least 2 per swap devices, and 4 is a pretty good value if you
	 * have one NFS swap device due to the command/ack latency over NFS.
	 * So it all works out pretty well.
	 */
	nsw_cluster_max = min((MAXPHYS/PAGE_SIZE), MAX_PAGEOUT_CLUSTER);

	nsw_wcount_async = 4;
	nsw_wcount_async_max = nsw_wcount_async;
	mtx_init(&swbuf_mtx, "async swbuf mutex", NULL, MTX_DEF);

	swwbuf_zone = pbuf_zsecond_create("swwbuf", nswbuf / 4);
	swrbuf_zone = pbuf_zsecond_create("swrbuf", nswbuf / 2);

	/*
	 * Initialize our zone, taking the user's requested size or
	 * estimating the number we need based on the number of pages
	 * in the system.
	 */
	n = maxswzone != 0 ? maxswzone / sizeof(struct swblk) :
	    vm_cnt.v_page_count / 2;
	swpctrie_zone = uma_zcreate("swpctrie", pctrie_node_size(), NULL, NULL,
	    pctrie_zone_init, NULL, UMA_ALIGN_PTR, UMA_ZONE_VM);
	if (swpctrie_zone == NULL)
		panic("failed to create swap pctrie zone.");
	swblk_zone = uma_zcreate("swblk", sizeof(struct swblk), NULL, NULL,
	    NULL, NULL, _Alignof(struct swblk) - 1, UMA_ZONE_VM);
	if (swblk_zone == NULL)
		panic("failed to create swap blk zone.");
	n2 = n;
	do {
		if (uma_zone_reserve_kva(swblk_zone, n))
			break;
		/*
		 * if the allocation failed, try a zone two thirds the
		 * size of the previous attempt.
		 */
		n -= ((n + 2) / 3);
	} while (n > 0);

	/*
	 * Often uma_zone_reserve_kva() cannot reserve exactly the
	 * requested size.  Account for the difference when
	 * calculating swap_maxpages.
	 */
	n = uma_zone_get_max(swblk_zone);

	if (n < n2)
		printf("Swap blk zone entries changed from %lu to %lu.\n",
		    n2, n);
	swap_maxpages = n * SWAP_META_PAGES;
	swzone = n * sizeof(struct swblk);
	if (!uma_zone_reserve_kva(swpctrie_zone, n))
		printf("Cannot reserve swap pctrie zone, "
		    "reduce kern.maxswzone.\n");
}

static vm_object_t
swap_pager_alloc_init(void *handle, struct ucred *cred, vm_ooffset_t size,
    vm_ooffset_t offset)
{
	vm_object_t object;

	if (cred != NULL) {
		if (!swap_reserve_by_cred(size, cred))
			return (NULL);
		crhold(cred);
	}

	/*
	 * The un_pager.swp.swp_blks trie is initialized by
	 * vm_object_allocate() to ensure the correct order of
	 * visibility to other threads.
	 */
	object = vm_object_allocate(OBJT_SWAP, OFF_TO_IDX(offset +
	    PAGE_MASK + size));

	object->handle = handle;
	if (cred != NULL) {
		object->cred = cred;
		object->charge = size;
	}
	return (object);
}

/*
 * SWAP_PAGER_ALLOC() -	allocate a new OBJT_SWAP VM object and instantiate
 *			its metadata structures.
 *
 *	This routine is called from the mmap and fork code to create a new
 *	OBJT_SWAP object.
 *
 *	This routine must ensure that no live duplicate is created for
 *	the named object request, which is protected against by
 *	holding the sw_alloc_sx lock in case handle != NULL.
 */
static vm_object_t
swap_pager_alloc(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t offset, struct ucred *cred)
{
	vm_object_t object;

	if (handle != NULL) {
		/*
		 * Reference existing named region or allocate new one.  There
		 * should not be a race here against swp_pager_meta_build()
		 * as called from vm_page_remove() in regards to the lookup
		 * of the handle.
		 */
		sx_xlock(&sw_alloc_sx);
		object = vm_pager_object_lookup(NOBJLIST(handle), handle);
		if (object == NULL) {
			object = swap_pager_alloc_init(handle, cred, size,
			    offset);
			if (object != NULL) {
				TAILQ_INSERT_TAIL(NOBJLIST(object->handle),
				    object, pager_object_list);
			}
		}
		sx_xunlock(&sw_alloc_sx);
	} else {
		object = swap_pager_alloc_init(handle, cred, size, offset);
	}
	return (object);
}

/*
 * SWAP_PAGER_DEALLOC() -	remove swap metadata from object
 *
 *	The swap backing for the object is destroyed.  The code is
 *	designed such that we can reinstantiate it later, but this
 *	routine is typically called only when the entire object is
 *	about to be destroyed.
 *
 *	The object must be locked.
 */
static void
swap_pager_dealloc(vm_object_t object)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT((object->flags & OBJ_DEAD) != 0, ("dealloc of reachable obj"));

	/*
	 * Remove from list right away so lookups will fail if we block for
	 * pageout completion.
	 */
	if (object->handle != NULL) {
		VM_OBJECT_WUNLOCK(object);
		sx_xlock(&sw_alloc_sx);
		TAILQ_REMOVE(NOBJLIST(object->handle), object,
		    pager_object_list);
		sx_xunlock(&sw_alloc_sx);
		VM_OBJECT_WLOCK(object);
	}

	vm_object_pip_wait(object, "swpdea");

	/*
	 * Free all remaining metadata.  We only bother to free it from
	 * the swap meta data.  We do not attempt to free swapblk's still
	 * associated with vm_page_t's for this object.  We do not care
	 * if paging is still in progress on some objects.
	 */
	swp_pager_meta_free_all(object);
	object->handle = NULL;
	object->type = OBJT_DEAD;
}

/************************************************************************
 *			SWAP PAGER BITMAP ROUTINES			*
 ************************************************************************/

/*
 * SWP_PAGER_GETSWAPSPACE() -	allocate raw swap space
 *
 *	Allocate swap for the requested number of pages.  The starting
 *	swap block number (a page index) is returned or SWAPBLK_NONE
 *	if the allocation failed.
 *
 *	Also has the side effect of advising that somebody made a mistake
 *	when they configured swap and didn't configure enough.
 *
 *	This routine may not sleep.
 *
 *	We allocate in round-robin fashion from the configured devices.
 */
static daddr_t
swp_pager_getswapspace(int npages)
{
	daddr_t blk;
	struct swdevt *sp;
	int i;

	blk = SWAPBLK_NONE;
	mtx_lock(&sw_dev_mtx);
	sp = swdevhd;
	for (i = 0; i < nswapdev; i++) {
		if (sp == NULL)
			sp = TAILQ_FIRST(&swtailq);
		if (!(sp->sw_flags & SW_CLOSING)) {
			blk = blist_alloc(sp->sw_blist, npages);
			if (blk != SWAPBLK_NONE) {
				blk += sp->sw_first;
				sp->sw_used += npages;
				swap_pager_avail -= npages;
				swp_sizecheck();
				swdevhd = TAILQ_NEXT(sp, sw_list);
				goto done;
			}
		}
		sp = TAILQ_NEXT(sp, sw_list);
	}
	if (swap_pager_full != 2) {
		printf("swap_pager_getswapspace(%d): failed\n", npages);
		swap_pager_full = 2;
		swap_pager_almost_full = 1;
	}
	swdevhd = NULL;
done:
	mtx_unlock(&sw_dev_mtx);
	return (blk);
}

static bool
swp_pager_isondev(daddr_t blk, struct swdevt *sp)
{

	return (blk >= sp->sw_first && blk < sp->sw_end);
}

static void
swp_pager_strategy(struct buf *bp)
{
	struct swdevt *sp;

	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (swp_pager_isondev(bp->b_blkno, sp)) {
			mtx_unlock(&sw_dev_mtx);
			if ((sp->sw_flags & SW_UNMAPPED) != 0 &&
			    unmapped_buf_allowed) {
				bp->b_data = unmapped_buf;
				bp->b_offset = 0;
			} else {
				pmap_qenter((vm_offset_t)bp->b_data,
				    &bp->b_pages[0], bp->b_bcount / PAGE_SIZE);
			}
			sp->sw_strategy(bp, sp);
			return;
		}
	}
	panic("Swapdev not found");
}


/*
 * SWP_PAGER_FREESWAPSPACE() -	free raw swap space
 *
 *	This routine returns the specified swap blocks back to the bitmap.
 *
 *	This routine may not sleep.
 */
static void
swp_pager_freeswapspace(daddr_t blk, daddr_t npages)
{
	struct swdevt *sp;

	if (npages == 0)
		return;
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (swp_pager_isondev(blk, sp)) {
			sp->sw_used -= npages;
			/*
			 * If we are attempting to stop swapping on
			 * this device, we don't want to mark any
			 * blocks free lest they be reused.
			 */
			if ((sp->sw_flags & SW_CLOSING) == 0) {
				blist_free(sp->sw_blist, blk - sp->sw_first,
				    npages);
				swap_pager_avail += npages;
				swp_sizecheck();
			}
			mtx_unlock(&sw_dev_mtx);
			return;
		}
	}
	panic("Swapdev not found");
}

/*
 * SYSCTL_SWAP_FRAGMENTATION() -	produce raw swap space stats
 */
static int
sysctl_swap_fragmentation(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	struct swdevt *sp;
	const char *devname;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (vn_isdisk(sp->sw_vp, NULL))
			devname = devtoname(sp->sw_vp->v_rdev);
		else
			devname = "[file]";
		sbuf_printf(&sbuf, "\nFree space on device %s:\n", devname);
		blist_stats(sp->sw_blist, &sbuf);
	}
	mtx_unlock(&sw_dev_mtx);
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

/*
 * SWAP_PAGER_FREESPACE() -	frees swap blocks associated with a page
 *				range within an object.
 *
 *	This is a globally accessible routine.
 *
 *	This routine removes swapblk assignments from swap metadata.
 *
 *	The external callers of this routine typically have already destroyed
 *	or renamed vm_page_t's associated with this range in the object so
 *	we should be ok.
 *
 *	The object must be locked.
 */
void
swap_pager_freespace(vm_object_t object, vm_pindex_t start, vm_size_t size)
{

	swp_pager_meta_free(object, start, size);
}

/*
 * SWAP_PAGER_RESERVE() - reserve swap blocks in object
 *
 *	Assigns swap blocks to the specified range within the object.  The
 *	swap blocks are not zeroed.  Any previous swap assignment is destroyed.
 *
 *	Returns 0 on success, -1 on failure.
 */
int
swap_pager_reserve(vm_object_t object, vm_pindex_t start, vm_size_t size)
{
	int n = 0;
	daddr_t blk = SWAPBLK_NONE;
	vm_pindex_t beg = start;	/* save start index */
	daddr_t addr, n_free, s_free;

	swp_pager_init_freerange(&s_free, &n_free);
	VM_OBJECT_WLOCK(object);
	while (size) {
		if (n == 0) {
			n = BLIST_MAX_ALLOC;
			while ((blk = swp_pager_getswapspace(n)) == SWAPBLK_NONE) {
				n >>= 1;
				if (n == 0) {
					swp_pager_meta_free(object, beg, start - beg);
					VM_OBJECT_WUNLOCK(object);
					return (-1);
				}
			}
		}
		addr = swp_pager_meta_build(object, start, blk);
		if (addr != SWAPBLK_NONE)
			swp_pager_update_freerange(&s_free, &n_free, addr);
		--size;
		++start;
		++blk;
		--n;
	}
	swp_pager_freeswapspace(s_free, n_free);
	swp_pager_meta_free(object, start, n);
	VM_OBJECT_WUNLOCK(object);
	return (0);
}

/*
 * SWAP_PAGER_COPY() -  copy blocks from source pager to destination pager
 *			and destroy the source.
 *
 *	Copy any valid swapblks from the source to the destination.  In
 *	cases where both the source and destination have a valid swapblk,
 *	we keep the destination's.
 *
 *	This routine is allowed to sleep.  It may sleep allocating metadata
 *	indirectly through swp_pager_meta_build() or if paging is still in
 *	progress on the source.
 *
 *	The source object contains no vm_page_t's (which is just as well)
 *
 *	The source object is of type OBJT_SWAP.
 *
 *	The source and destination objects must be locked.
 *	Both object locks may temporarily be released.
 */
void
swap_pager_copy(vm_object_t srcobject, vm_object_t dstobject,
    vm_pindex_t offset, int destroysource)
{
	vm_pindex_t i;
	daddr_t dstaddr, n_free, s_free, srcaddr;

	VM_OBJECT_ASSERT_WLOCKED(srcobject);
	VM_OBJECT_ASSERT_WLOCKED(dstobject);

	/*
	 * If destroysource is set, we remove the source object from the
	 * swap_pager internal queue now.
	 */
	if (destroysource && srcobject->handle != NULL) {
		vm_object_pip_add(srcobject, 1);
		VM_OBJECT_WUNLOCK(srcobject);
		vm_object_pip_add(dstobject, 1);
		VM_OBJECT_WUNLOCK(dstobject);
		sx_xlock(&sw_alloc_sx);
		TAILQ_REMOVE(NOBJLIST(srcobject->handle), srcobject,
		    pager_object_list);
		sx_xunlock(&sw_alloc_sx);
		VM_OBJECT_WLOCK(dstobject);
		vm_object_pip_wakeup(dstobject);
		VM_OBJECT_WLOCK(srcobject);
		vm_object_pip_wakeup(srcobject);
	}

	/*
	 * Transfer source to destination.
	 */
	swp_pager_init_freerange(&s_free, &n_free);
	for (i = 0; i < dstobject->size; ++i) {
		srcaddr = swp_pager_meta_ctl(srcobject, i + offset, SWM_POP);
		if (srcaddr == SWAPBLK_NONE)
			continue;
		dstaddr = swp_pager_meta_ctl(dstobject, i, 0);
		if (dstaddr != SWAPBLK_NONE) {
			/*
			 * Destination has valid swapblk or it is represented
			 * by a resident page.  We destroy the source block.
			 */
			swp_pager_update_freerange(&s_free, &n_free, srcaddr);
			continue;
		}

		/*
		 * Destination has no swapblk and is not resident,
		 * copy source.
		 *
		 * swp_pager_meta_build() can sleep.
		 */
		vm_object_pip_add(srcobject, 1);
		VM_OBJECT_WUNLOCK(srcobject);
		vm_object_pip_add(dstobject, 1);
		dstaddr = swp_pager_meta_build(dstobject, i, srcaddr);
		KASSERT(dstaddr == SWAPBLK_NONE,
		    ("Unexpected destination swapblk"));
		vm_object_pip_wakeup(dstobject);
		VM_OBJECT_WLOCK(srcobject);
		vm_object_pip_wakeup(srcobject);
	}
	swp_pager_freeswapspace(s_free, n_free);

	/*
	 * Free left over swap blocks in source.
	 *
	 * We have to revert the type to OBJT_DEFAULT so we do not accidentally
	 * double-remove the object from the swap queues.
	 */
	if (destroysource) {
		swp_pager_meta_free_all(srcobject);
		/*
		 * Reverting the type is not necessary, the caller is going
		 * to destroy srcobject directly, but I'm doing it here
		 * for consistency since we've removed the object from its
		 * queues.
		 */
		srcobject->type = OBJT_DEFAULT;
	}
}

/*
 * SWAP_PAGER_HASPAGE() -	determine if we have good backing store for
 *				the requested page.
 *
 *	We determine whether good backing store exists for the requested
 *	page and return TRUE if it does, FALSE if it doesn't.
 *
 *	If TRUE, we also try to determine how much valid, contiguous backing
 *	store exists before and after the requested page.
 */
static boolean_t
swap_pager_haspage(vm_object_t object, vm_pindex_t pindex, int *before,
    int *after)
{
	daddr_t blk, blk0;
	int i;

	VM_OBJECT_ASSERT_LOCKED(object);

	/*
	 * do we have good backing store at the requested index ?
	 */
	blk0 = swp_pager_meta_ctl(object, pindex, 0);
	if (blk0 == SWAPBLK_NONE) {
		if (before)
			*before = 0;
		if (after)
			*after = 0;
		return (FALSE);
	}

	/*
	 * find backwards-looking contiguous good backing store
	 */
	if (before != NULL) {
		for (i = 1; i < SWB_NPAGES; i++) {
			if (i > pindex)
				break;
			blk = swp_pager_meta_ctl(object, pindex - i, 0);
			if (blk != blk0 - i)
				break;
		}
		*before = i - 1;
	}

	/*
	 * find forward-looking contiguous good backing store
	 */
	if (after != NULL) {
		for (i = 1; i < SWB_NPAGES; i++) {
			blk = swp_pager_meta_ctl(object, pindex + i, 0);
			if (blk != blk0 + i)
				break;
		}
		*after = i - 1;
	}
	return (TRUE);
}

/*
 * SWAP_PAGER_PAGE_UNSWAPPED() - remove swap backing store related to page
 *
 *	This removes any associated swap backing store, whether valid or
 *	not, from the page.
 *
 *	This routine is typically called when a page is made dirty, at
 *	which point any associated swap can be freed.  MADV_FREE also
 *	calls us in a special-case situation
 *
 *	NOTE!!!  If the page is clean and the swap was valid, the caller
 *	should make the page dirty before calling this routine.  This routine
 *	does NOT change the m->dirty status of the page.  Also: MADV_FREE
 *	depends on it.
 *
 *	This routine may not sleep.
 *
 *	The object containing the page must be locked.
 */
static void
swap_pager_unswapped(vm_page_t m)
{
	daddr_t srcaddr;

	srcaddr = swp_pager_meta_ctl(m->object, m->pindex, SWM_POP);
	if (srcaddr != SWAPBLK_NONE)
		swp_pager_freeswapspace(srcaddr, 1);
}

/*
 * swap_pager_getpages() - bring pages in from swap
 *
 *	Attempt to page in the pages in array "ma" of length "count".  The
 *	caller may optionally specify that additional pages preceding and
 *	succeeding the specified range be paged in.  The number of such pages
 *	is returned in the "rbehind" and "rahead" parameters, and they will
 *	be in the inactive queue upon return.
 *
 *	The pages in "ma" must be busied and will remain busied upon return.
 */
static int
swap_pager_getpages(vm_object_t object, vm_page_t *ma, int count, int *rbehind,
    int *rahead)
{
	struct buf *bp;
	vm_page_t bm, mpred, msucc, p;
	vm_pindex_t pindex;
	daddr_t blk;
	int i, maxahead, maxbehind, reqcount;

	reqcount = count;

	/*
	 * Determine the final number of read-behind pages and
	 * allocate them BEFORE releasing the object lock.  Otherwise,
	 * there can be a problematic race with vm_object_split().
	 * Specifically, vm_object_split() might first transfer pages
	 * that precede ma[0] in the current object to a new object,
	 * and then this function incorrectly recreates those pages as
	 * read-behind pages in the current object.
	 */
	if (!swap_pager_haspage(object, ma[0]->pindex, &maxbehind, &maxahead))
		return (VM_PAGER_FAIL);

	/*
	 * Clip the readahead and readbehind ranges to exclude resident pages.
	 */
	if (rahead != NULL) {
		KASSERT(reqcount - 1 <= maxahead,
		    ("page count %d extends beyond swap block", reqcount));
		*rahead = imin(*rahead, maxahead - (reqcount - 1));
		pindex = ma[reqcount - 1]->pindex;
		msucc = TAILQ_NEXT(ma[reqcount - 1], listq);
		if (msucc != NULL && msucc->pindex - pindex - 1 < *rahead)
			*rahead = msucc->pindex - pindex - 1;
	}
	if (rbehind != NULL) {
		*rbehind = imin(*rbehind, maxbehind);
		pindex = ma[0]->pindex;
		mpred = TAILQ_PREV(ma[0], pglist, listq);
		if (mpred != NULL && pindex - mpred->pindex - 1 < *rbehind)
			*rbehind = pindex - mpred->pindex - 1;
	}

	bm = ma[0];
	for (i = 0; i < count; i++)
		ma[i]->oflags |= VPO_SWAPINPROG;

	/*
	 * Allocate readahead and readbehind pages.
	 */
	if (rbehind != NULL) {
		for (i = 1; i <= *rbehind; i++) {
			p = vm_page_alloc(object, ma[0]->pindex - i,
			    VM_ALLOC_NORMAL);
			if (p == NULL)
				break;
			p->oflags |= VPO_SWAPINPROG;
			bm = p;
		}
		*rbehind = i - 1;
	}
	if (rahead != NULL) {
		for (i = 0; i < *rahead; i++) {
			p = vm_page_alloc(object,
			    ma[reqcount - 1]->pindex + i + 1, VM_ALLOC_NORMAL);
			if (p == NULL)
				break;
			p->oflags |= VPO_SWAPINPROG;
		}
		*rahead = i;
	}
	if (rbehind != NULL)
		count += *rbehind;
	if (rahead != NULL)
		count += *rahead;

	vm_object_pip_add(object, count);

	pindex = bm->pindex;
	blk = swp_pager_meta_ctl(object, pindex, 0);
	KASSERT(blk != SWAPBLK_NONE,
	    ("no swap blocking containing %p(%jx)", object, (uintmax_t)pindex));

	VM_OBJECT_WUNLOCK(object);
	bp = uma_zalloc(swrbuf_zone, M_WAITOK);
	/* Pages cannot leave the object while busy. */
	for (i = 0, p = bm; i < count; i++, p = TAILQ_NEXT(p, listq)) {
		MPASS(p->pindex == bm->pindex + i);
		bp->b_pages[i] = p;
	}

	bp->b_flags |= B_PAGING;
	bp->b_iocmd = BIO_READ;
	bp->b_iodone = swp_pager_async_iodone;
	bp->b_rcred = crhold(thread0.td_ucred);
	bp->b_wcred = crhold(thread0.td_ucred);
	bp->b_blkno = blk;
	bp->b_bcount = PAGE_SIZE * count;
	bp->b_bufsize = PAGE_SIZE * count;
	bp->b_npages = count;
	bp->b_pgbefore = rbehind != NULL ? *rbehind : 0;
	bp->b_pgafter = rahead != NULL ? *rahead : 0;

	VM_CNT_INC(v_swapin);
	VM_CNT_ADD(v_swappgsin, count);

	/*
	 * perform the I/O.  NOTE!!!  bp cannot be considered valid after
	 * this point because we automatically release it on completion.
	 * Instead, we look at the one page we are interested in which we
	 * still hold a lock on even through the I/O completion.
	 *
	 * The other pages in our ma[] array are also released on completion,
	 * so we cannot assume they are valid anymore either.
	 *
	 * NOTE: b_blkno is destroyed by the call to swapdev_strategy
	 */
	BUF_KERNPROC(bp);
	swp_pager_strategy(bp);

	/*
	 * Wait for the pages we want to complete.  VPO_SWAPINPROG is always
	 * cleared on completion.  If an I/O error occurs, SWAPBLK_NONE
	 * is set in the metadata for each page in the request.
	 */
	VM_OBJECT_WLOCK(object);
	while ((ma[0]->oflags & VPO_SWAPINPROG) != 0) {
		ma[0]->oflags |= VPO_SWAPSLEEP;
		VM_CNT_INC(v_intrans);
		if (VM_OBJECT_SLEEP(object, &object->paging_in_progress, PSWP,
		    "swread", hz * 20)) {
			printf(
"swap_pager: indefinite wait buffer: bufobj: %p, blkno: %jd, size: %ld\n",
			    bp->b_bufobj, (intmax_t)bp->b_blkno, bp->b_bcount);
		}
	}

	/*
	 * If we had an unrecoverable read error pages will not be valid.
	 */
	for (i = 0; i < reqcount; i++)
		if (ma[i]->valid != VM_PAGE_BITS_ALL)
			return (VM_PAGER_ERROR);

	return (VM_PAGER_OK);

	/*
	 * A final note: in a low swap situation, we cannot deallocate swap
	 * and mark a page dirty here because the caller is likely to mark
	 * the page clean when we return, causing the page to possibly revert
	 * to all-zero's later.
	 */
}

/*
 * 	swap_pager_getpages_async():
 *
 *	Right now this is emulation of asynchronous operation on top of
 *	swap_pager_getpages().
 */
static int
swap_pager_getpages_async(vm_object_t object, vm_page_t *ma, int count,
    int *rbehind, int *rahead, pgo_getpages_iodone_t iodone, void *arg)
{
	int r, error;

	r = swap_pager_getpages(object, ma, count, rbehind, rahead);
	VM_OBJECT_WUNLOCK(object);
	switch (r) {
	case VM_PAGER_OK:
		error = 0;
		break;
	case VM_PAGER_ERROR:
		error = EIO;
		break;
	case VM_PAGER_FAIL:
		error = EINVAL;
		break;
	default:
		panic("unhandled swap_pager_getpages() error %d", r);
	}
	(iodone)(arg, ma, count, error);
	VM_OBJECT_WLOCK(object);

	return (r);
}

/*
 *	swap_pager_putpages:
 *
 *	Assign swap (if necessary) and initiate I/O on the specified pages.
 *
 *	We support both OBJT_DEFAULT and OBJT_SWAP objects.  DEFAULT objects
 *	are automatically converted to SWAP objects.
 *
 *	In a low memory situation we may block in VOP_STRATEGY(), but the new
 *	vm_page reservation system coupled with properly written VFS devices
 *	should ensure that no low-memory deadlock occurs.  This is an area
 *	which needs work.
 *
 *	The parent has N vm_object_pip_add() references prior to
 *	calling us and will remove references for rtvals[] that are
 *	not set to VM_PAGER_PEND.  We need to remove the rest on I/O
 *	completion.
 *
 *	The parent has soft-busy'd the pages it passes us and will unbusy
 *	those whos rtvals[] entry is not set to VM_PAGER_PEND on return.
 *	We need to unbusy the rest on I/O completion.
 */
static void
swap_pager_putpages(vm_object_t object, vm_page_t *ma, int count,
    int flags, int *rtvals)
{
	int i, n;
	boolean_t sync;
	daddr_t addr, n_free, s_free;

	swp_pager_init_freerange(&s_free, &n_free);
	if (count && ma[0]->object != object) {
		panic("swap_pager_putpages: object mismatch %p/%p",
		    object,
		    ma[0]->object
		);
	}

	/*
	 * Step 1
	 *
	 * Turn object into OBJT_SWAP
	 * check for bogus sysops
	 * force sync if not pageout process
	 */
	if (object->type != OBJT_SWAP) {
		addr = swp_pager_meta_build(object, 0, SWAPBLK_NONE);
		KASSERT(addr == SWAPBLK_NONE,
		    ("unexpected object swap block"));
	}
	VM_OBJECT_WUNLOCK(object);

	n = 0;
	if (curproc != pageproc)
		sync = TRUE;
	else
		sync = (flags & VM_PAGER_PUT_SYNC) != 0;

	/*
	 * Step 2
	 *
	 * Assign swap blocks and issue I/O.  We reallocate swap on the fly.
	 * The page is left dirty until the pageout operation completes
	 * successfully.
	 */
	for (i = 0; i < count; i += n) {
		int j;
		struct buf *bp;
		daddr_t blk;

		/*
		 * Maximum I/O size is limited by a number of factors.
		 */
		n = min(BLIST_MAX_ALLOC, count - i);
		n = min(n, nsw_cluster_max);

		/*
		 * Get biggest block of swap we can.  If we fail, fall
		 * back and try to allocate a smaller block.  Don't go
		 * overboard trying to allocate space if it would overly
		 * fragment swap.
		 */
		while (
		    (blk = swp_pager_getswapspace(n)) == SWAPBLK_NONE &&
		    n > 4
		) {
			n >>= 1;
		}
		if (blk == SWAPBLK_NONE) {
			for (j = 0; j < n; ++j)
				rtvals[i+j] = VM_PAGER_FAIL;
			continue;
		}

		/*
		 * All I/O parameters have been satisfied, build the I/O
		 * request and assign the swap space.
		 */
		if (sync != TRUE) {
			mtx_lock(&swbuf_mtx);
			while (nsw_wcount_async == 0)
				msleep(&nsw_wcount_async, &swbuf_mtx, PVM,
				    "swbufa", 0);
			nsw_wcount_async--;
			mtx_unlock(&swbuf_mtx);
		}
		bp = uma_zalloc(swwbuf_zone, M_WAITOK);
		if (sync != TRUE)
			bp->b_flags = B_ASYNC;
		bp->b_flags |= B_PAGING;
		bp->b_iocmd = BIO_WRITE;

		bp->b_rcred = crhold(thread0.td_ucred);
		bp->b_wcred = crhold(thread0.td_ucred);
		bp->b_bcount = PAGE_SIZE * n;
		bp->b_bufsize = PAGE_SIZE * n;
		bp->b_blkno = blk;

		VM_OBJECT_WLOCK(object);
		for (j = 0; j < n; ++j) {
			vm_page_t mreq = ma[i+j];

			addr = swp_pager_meta_build(mreq->object, mreq->pindex,
			    blk + j);
			if (addr != SWAPBLK_NONE)
				swp_pager_update_freerange(&s_free, &n_free,
				    addr);
			MPASS(mreq->dirty == VM_PAGE_BITS_ALL);
			mreq->oflags |= VPO_SWAPINPROG;
			bp->b_pages[j] = mreq;
		}
		VM_OBJECT_WUNLOCK(object);
		bp->b_npages = n;
		/*
		 * Must set dirty range for NFS to work.
		 */
		bp->b_dirtyoff = 0;
		bp->b_dirtyend = bp->b_bcount;

		VM_CNT_INC(v_swapout);
		VM_CNT_ADD(v_swappgsout, bp->b_npages);

		/*
		 * We unconditionally set rtvals[] to VM_PAGER_PEND so that we
		 * can call the async completion routine at the end of a
		 * synchronous I/O operation.  Otherwise, our caller would
		 * perform duplicate unbusy and wakeup operations on the page
		 * and object, respectively.
		 */
		for (j = 0; j < n; j++)
			rtvals[i + j] = VM_PAGER_PEND;

		/*
		 * asynchronous
		 *
		 * NOTE: b_blkno is destroyed by the call to swapdev_strategy
		 */
		if (sync == FALSE) {
			bp->b_iodone = swp_pager_async_iodone;
			BUF_KERNPROC(bp);
			swp_pager_strategy(bp);
			continue;
		}

		/*
		 * synchronous
		 *
		 * NOTE: b_blkno is destroyed by the call to swapdev_strategy
		 */
		bp->b_iodone = bdone;
		swp_pager_strategy(bp);

		/*
		 * Wait for the sync I/O to complete.
		 */
		bwait(bp, PVM, "swwrt");

		/*
		 * Now that we are through with the bp, we can call the
		 * normal async completion, which frees everything up.
		 */
		swp_pager_async_iodone(bp);
	}
	VM_OBJECT_WLOCK(object);
	swp_pager_freeswapspace(s_free, n_free);
}

/*
 *	swp_pager_async_iodone:
 *
 *	Completion routine for asynchronous reads and writes from/to swap.
 *	Also called manually by synchronous code to finish up a bp.
 *
 *	This routine may not sleep.
 */
static void
swp_pager_async_iodone(struct buf *bp)
{
	int i;
	vm_object_t object = NULL;

	/*
	 * Report error - unless we ran out of memory, in which case
	 * we've already logged it in swapgeom_strategy().
	 */
	if (bp->b_ioflags & BIO_ERROR && bp->b_error != ENOMEM) {
		printf(
		    "swap_pager: I/O error - %s failed; blkno %ld,"
			"size %ld, error %d\n",
		    ((bp->b_iocmd == BIO_READ) ? "pagein" : "pageout"),
		    (long)bp->b_blkno,
		    (long)bp->b_bcount,
		    bp->b_error
		);
	}

	/*
	 * remove the mapping for kernel virtual
	 */
	if (buf_mapped(bp))
		pmap_qremove((vm_offset_t)bp->b_data, bp->b_npages);
	else
		bp->b_data = bp->b_kvabase;

	if (bp->b_npages) {
		object = bp->b_pages[0]->object;
		VM_OBJECT_WLOCK(object);
	}

	/*
	 * cleanup pages.  If an error occurs writing to swap, we are in
	 * very serious trouble.  If it happens to be a disk error, though,
	 * we may be able to recover by reassigning the swap later on.  So
	 * in this case we remove the m->swapblk assignment for the page
	 * but do not free it in the rlist.  The errornous block(s) are thus
	 * never reallocated as swap.  Redirty the page and continue.
	 */
	for (i = 0; i < bp->b_npages; ++i) {
		vm_page_t m = bp->b_pages[i];

		m->oflags &= ~VPO_SWAPINPROG;
		if (m->oflags & VPO_SWAPSLEEP) {
			m->oflags &= ~VPO_SWAPSLEEP;
			wakeup(&object->paging_in_progress);
		}

		if (bp->b_ioflags & BIO_ERROR) {
			/*
			 * If an error occurs I'd love to throw the swapblk
			 * away without freeing it back to swapspace, so it
			 * can never be used again.  But I can't from an
			 * interrupt.
			 */
			if (bp->b_iocmd == BIO_READ) {
				/*
				 * NOTE: for reads, m->dirty will probably
				 * be overridden by the original caller of
				 * getpages so don't play cute tricks here.
				 */
				m->valid = 0;
			} else {
				/*
				 * If a write error occurs, reactivate page
				 * so it doesn't clog the inactive list,
				 * then finish the I/O.
				 */
				MPASS(m->dirty == VM_PAGE_BITS_ALL);
				vm_page_lock(m);
				vm_page_activate(m);
				vm_page_unlock(m);
				vm_page_sunbusy(m);
			}
		} else if (bp->b_iocmd == BIO_READ) {
			/*
			 * NOTE: for reads, m->dirty will probably be
			 * overridden by the original caller of getpages so
			 * we cannot set them in order to free the underlying
			 * swap in a low-swap situation.  I don't think we'd
			 * want to do that anyway, but it was an optimization
			 * that existed in the old swapper for a time before
			 * it got ripped out due to precisely this problem.
			 */
			KASSERT(!pmap_page_is_mapped(m),
			    ("swp_pager_async_iodone: page %p is mapped", m));
			KASSERT(m->dirty == 0,
			    ("swp_pager_async_iodone: page %p is dirty", m));

			m->valid = VM_PAGE_BITS_ALL;
			if (i < bp->b_pgbefore ||
			    i >= bp->b_npages - bp->b_pgafter)
				vm_page_readahead_finish(m);
		} else {
			/*
			 * For write success, clear the dirty
			 * status, then finish the I/O ( which decrements the
			 * busy count and possibly wakes waiter's up ).
			 * A page is only written to swap after a period of
			 * inactivity.  Therefore, we do not expect it to be
			 * reused.
			 */
			KASSERT(!pmap_page_is_write_mapped(m),
			    ("swp_pager_async_iodone: page %p is not write"
			    " protected", m));
			vm_page_undirty(m);
			vm_page_lock(m);
			vm_page_deactivate_noreuse(m);
			vm_page_unlock(m);
			vm_page_sunbusy(m);
		}
	}

	/*
	 * adjust pip.  NOTE: the original parent may still have its own
	 * pip refs on the object.
	 */
	if (object != NULL) {
		vm_object_pip_wakeupn(object, bp->b_npages);
		VM_OBJECT_WUNLOCK(object);
	}

	/*
	 * swapdev_strategy() manually sets b_vp and b_bufobj before calling
	 * bstrategy(). Set them back to NULL now we're done with it, or we'll
	 * trigger a KASSERT in relpbuf().
	 */
	if (bp->b_vp) {
		    bp->b_vp = NULL;
		    bp->b_bufobj = NULL;
	}
	/*
	 * release the physical I/O buffer
	 */
	if (bp->b_flags & B_ASYNC) {
		mtx_lock(&swbuf_mtx);
		if (++nsw_wcount_async == 1)
			wakeup(&nsw_wcount_async);
		mtx_unlock(&swbuf_mtx);
	}
	uma_zfree((bp->b_iocmd == BIO_READ) ? swrbuf_zone : swwbuf_zone, bp);
}

int
swap_pager_nswapdev(void)
{

	return (nswapdev);
}

/*
 * SWP_PAGER_FORCE_PAGEIN() - force a swap block to be paged in
 *
 *	This routine dissociates the page at the given index within an object
 *	from its backing store, paging it in if it does not reside in memory.
 *	If the page is paged in, it is marked dirty and placed in the laundry
 *	queue.  The page is marked dirty because it no longer has backing
 *	store.  It is placed in the laundry queue because it has not been
 *	accessed recently.  Otherwise, it would already reside in memory.
 *
 *	We also attempt to swap in all other pages in the swap block.
 *	However, we only guarantee that the one at the specified index is
 *	paged in.
 *
 *	XXX - The code to page the whole block in doesn't work, so we
 *	      revert to the one-by-one behavior for now.  Sigh.
 */
static inline void
swp_pager_force_pagein(vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t m;

	vm_object_pip_add(object, 1);
	m = vm_page_grab(object, pindex, VM_ALLOC_NORMAL);
	if (m->valid == VM_PAGE_BITS_ALL) {
		vm_object_pip_wakeup(object);
		vm_page_dirty(m);
#ifdef INVARIANTS
		vm_page_lock(m);
		if (m->wire_count == 0 && m->queue == PQ_NONE)
			panic("page %p is neither wired nor queued", m);
		vm_page_unlock(m);
#endif
		vm_page_xunbusy(m);
		vm_pager_page_unswapped(m);
		return;
	}

	if (swap_pager_getpages(object, &m, 1, NULL, NULL) != VM_PAGER_OK)
		panic("swap_pager_force_pagein: read from swap failed");/*XXX*/
	vm_object_pip_wakeup(object);
	vm_page_dirty(m);
	vm_page_lock(m);
	vm_page_launder(m);
	vm_page_unlock(m);
	vm_page_xunbusy(m);
	vm_pager_page_unswapped(m);
}

/*
 *	swap_pager_swapoff:
 *
 *	Page in all of the pages that have been paged out to the
 *	given device.  The corresponding blocks in the bitmap must be
 *	marked as allocated and the device must be flagged SW_CLOSING.
 *	There may be no processes swapped out to the device.
 *
 *	This routine may block.
 */
static void
swap_pager_swapoff(struct swdevt *sp)
{
	struct swblk *sb;
	vm_object_t object;
	vm_pindex_t pi;
	int i, retries;

	sx_assert(&swdev_syscall_lock, SA_XLOCKED);

	retries = 0;
full_rescan:
	mtx_lock(&vm_object_list_mtx);
	TAILQ_FOREACH(object, &vm_object_list, object_list) {
		if (object->type != OBJT_SWAP)
			continue;
		mtx_unlock(&vm_object_list_mtx);
		/* Depends on type-stability. */
		VM_OBJECT_WLOCK(object);

		/*
		 * Dead objects are eventually terminated on their own.
		 */
		if ((object->flags & OBJ_DEAD) != 0)
			goto next_obj;

		/*
		 * Sync with fences placed after pctrie
		 * initialization.  We must not access pctrie below
		 * unless we checked that our object is swap and not
		 * dead.
		 */
		atomic_thread_fence_acq();
		if (object->type != OBJT_SWAP)
			goto next_obj;

		for (pi = 0; (sb = SWAP_PCTRIE_LOOKUP_GE(
		    &object->un_pager.swp.swp_blks, pi)) != NULL; ) {
			pi = sb->p + SWAP_META_PAGES;
			for (i = 0; i < SWAP_META_PAGES; i++) {
				if (sb->d[i] == SWAPBLK_NONE)
					continue;
				if (swp_pager_isondev(sb->d[i], sp))
					swp_pager_force_pagein(object,
					    sb->p + i);
			}
		}
next_obj:
		VM_OBJECT_WUNLOCK(object);
		mtx_lock(&vm_object_list_mtx);
	}
	mtx_unlock(&vm_object_list_mtx);

	if (sp->sw_used) {
		/*
		 * Objects may be locked or paging to the device being
		 * removed, so we will miss their pages and need to
		 * make another pass.  We have marked this device as
		 * SW_CLOSING, so the activity should finish soon.
		 */
		retries++;
		if (retries > 100) {
			panic("swapoff: failed to locate %d swap blocks",
			    sp->sw_used);
		}
		pause("swpoff", hz / 20);
		goto full_rescan;
	}
	EVENTHANDLER_INVOKE(swapoff, sp);
}

/************************************************************************
 *				SWAP META DATA 				*
 ************************************************************************
 *
 *	These routines manipulate the swap metadata stored in the
 *	OBJT_SWAP object.
 *
 *	Swap metadata is implemented with a global hash and not directly
 *	linked into the object.  Instead the object simply contains
 *	appropriate tracking counters.
 */

/*
 * SWP_PAGER_SWBLK_EMPTY() - is a range of blocks free?
 */
static bool
swp_pager_swblk_empty(struct swblk *sb, int start, int limit)
{
	int i;

	MPASS(0 <= start && start <= limit && limit <= SWAP_META_PAGES);
	for (i = start; i < limit; i++) {
		if (sb->d[i] != SWAPBLK_NONE)
			return (false);
	}
	return (true);
}
   
/*
 * SWP_PAGER_META_BUILD() -	add swap block to swap meta data for object
 *
 *	We first convert the object to a swap object if it is a default
 *	object.
 *
 *	The specified swapblk is added to the object's swap metadata.  If
 *	the swapblk is not valid, it is freed instead.  Any previously
 *	assigned swapblk is returned.
 */
static daddr_t
swp_pager_meta_build(vm_object_t object, vm_pindex_t pindex, daddr_t swapblk)
{
	static volatile int swblk_zone_exhausted, swpctrie_zone_exhausted;
	struct swblk *sb, *sb1;
	vm_pindex_t modpi, rdpi;
	daddr_t prev_swapblk;
	int error, i;

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * Convert default object to swap object if necessary
	 */
	if (object->type != OBJT_SWAP) {
		pctrie_init(&object->un_pager.swp.swp_blks);

		/*
		 * Ensure that swap_pager_swapoff()'s iteration over
		 * object_list does not see a garbage pctrie.
		 */
		atomic_thread_fence_rel();

		object->type = OBJT_SWAP;
		KASSERT(object->handle == NULL, ("default pager with handle"));
	}

	rdpi = rounddown(pindex, SWAP_META_PAGES);
	sb = SWAP_PCTRIE_LOOKUP(&object->un_pager.swp.swp_blks, rdpi);
	if (sb == NULL) {
		if (swapblk == SWAPBLK_NONE)
			return (SWAPBLK_NONE);
		for (;;) {
			sb = uma_zalloc(swblk_zone, M_NOWAIT | (curproc ==
			    pageproc ? M_USE_RESERVE : 0));
			if (sb != NULL) {
				sb->p = rdpi;
				for (i = 0; i < SWAP_META_PAGES; i++)
					sb->d[i] = SWAPBLK_NONE;
				if (atomic_cmpset_int(&swblk_zone_exhausted,
				    1, 0))
					printf("swblk zone ok\n");
				break;
			}
			VM_OBJECT_WUNLOCK(object);
			if (uma_zone_exhausted(swblk_zone)) {
				if (atomic_cmpset_int(&swblk_zone_exhausted,
				    0, 1))
					printf("swap blk zone exhausted, "
					    "increase kern.maxswzone\n");
				vm_pageout_oom(VM_OOM_SWAPZ);
				pause("swzonxb", 10);
			} else
				uma_zwait(swblk_zone);
			VM_OBJECT_WLOCK(object);
			sb = SWAP_PCTRIE_LOOKUP(&object->un_pager.swp.swp_blks,
			    rdpi);
			if (sb != NULL)
				/*
				 * Somebody swapped out a nearby page,
				 * allocating swblk at the rdpi index,
				 * while we dropped the object lock.
				 */
				goto allocated;
		}
		for (;;) {
			error = SWAP_PCTRIE_INSERT(
			    &object->un_pager.swp.swp_blks, sb);
			if (error == 0) {
				if (atomic_cmpset_int(&swpctrie_zone_exhausted,
				    1, 0))
					printf("swpctrie zone ok\n");
				break;
			}
			VM_OBJECT_WUNLOCK(object);
			if (uma_zone_exhausted(swpctrie_zone)) {
				if (atomic_cmpset_int(&swpctrie_zone_exhausted,
				    0, 1))
					printf("swap pctrie zone exhausted, "
					    "increase kern.maxswzone\n");
				vm_pageout_oom(VM_OOM_SWAPZ);
				pause("swzonxp", 10);
			} else
				uma_zwait(swpctrie_zone);
			VM_OBJECT_WLOCK(object);
			sb1 = SWAP_PCTRIE_LOOKUP(&object->un_pager.swp.swp_blks,
			    rdpi);
			if (sb1 != NULL) {
				uma_zfree(swblk_zone, sb);
				sb = sb1;
				goto allocated;
			}
		}
	}
allocated:
	MPASS(sb->p == rdpi);

	modpi = pindex % SWAP_META_PAGES;
	/* Return prior contents of metadata. */
	prev_swapblk = sb->d[modpi];
	/* Enter block into metadata. */
	sb->d[modpi] = swapblk;

	/*
	 * Free the swblk if we end up with the empty page run.
	 */
	if (swapblk == SWAPBLK_NONE &&
	    swp_pager_swblk_empty(sb, 0, SWAP_META_PAGES)) {
		SWAP_PCTRIE_REMOVE(&object->un_pager.swp.swp_blks, rdpi);
		uma_zfree(swblk_zone, sb);
	}
	return (prev_swapblk);
}

/*
 * SWP_PAGER_META_FREE() - free a range of blocks in the object's swap metadata
 *
 *	The requested range of blocks is freed, with any associated swap
 *	returned to the swap bitmap.
 *
 *	This routine will free swap metadata structures as they are cleaned
 *	out.  This routine does *NOT* operate on swap metadata associated
 *	with resident pages.
 */
static void
swp_pager_meta_free(vm_object_t object, vm_pindex_t pindex, vm_pindex_t count)
{
	struct swblk *sb;
	daddr_t n_free, s_free;
	vm_pindex_t last;
	int i, limit, start;

	VM_OBJECT_ASSERT_WLOCKED(object);
	if (object->type != OBJT_SWAP || count == 0)
		return;

	swp_pager_init_freerange(&s_free, &n_free);
	last = pindex + count;
	for (;;) {
		sb = SWAP_PCTRIE_LOOKUP_GE(&object->un_pager.swp.swp_blks,
		    rounddown(pindex, SWAP_META_PAGES));
		if (sb == NULL || sb->p >= last)
			break;
		start = pindex > sb->p ? pindex - sb->p : 0;
		limit = last - sb->p < SWAP_META_PAGES ? last - sb->p :
		    SWAP_META_PAGES;
		for (i = start; i < limit; i++) {
			if (sb->d[i] == SWAPBLK_NONE)
				continue;
			swp_pager_update_freerange(&s_free, &n_free, sb->d[i]);
			sb->d[i] = SWAPBLK_NONE;
		}
		pindex = sb->p + SWAP_META_PAGES;
		if (swp_pager_swblk_empty(sb, 0, start) &&
		    swp_pager_swblk_empty(sb, limit, SWAP_META_PAGES)) {
			SWAP_PCTRIE_REMOVE(&object->un_pager.swp.swp_blks,
			    sb->p);
			uma_zfree(swblk_zone, sb);
		}
	}
	swp_pager_freeswapspace(s_free, n_free);
}

/*
 * SWP_PAGER_META_FREE_ALL() - destroy all swap metadata associated with object
 *
 *	This routine locates and destroys all swap metadata associated with
 *	an object.
 */
static void
swp_pager_meta_free_all(vm_object_t object)
{
	struct swblk *sb;
	daddr_t n_free, s_free;
	vm_pindex_t pindex;
	int i;

	VM_OBJECT_ASSERT_WLOCKED(object);
	if (object->type != OBJT_SWAP)
		return;

	swp_pager_init_freerange(&s_free, &n_free);
	for (pindex = 0; (sb = SWAP_PCTRIE_LOOKUP_GE(
	    &object->un_pager.swp.swp_blks, pindex)) != NULL;) {
		pindex = sb->p + SWAP_META_PAGES;
		for (i = 0; i < SWAP_META_PAGES; i++) {
			if (sb->d[i] == SWAPBLK_NONE)
				continue;
			swp_pager_update_freerange(&s_free, &n_free, sb->d[i]);
		}
		SWAP_PCTRIE_REMOVE(&object->un_pager.swp.swp_blks, sb->p);
		uma_zfree(swblk_zone, sb);
	}
	swp_pager_freeswapspace(s_free, n_free);
}

/*
 * SWP_PAGER_METACTL() -  misc control of swap meta data.
 *
 *	This routine is capable of looking up, or removing swapblk
 *	assignments in the swap meta data.  It returns the swapblk being
 *	looked-up, popped, or SWAPBLK_NONE if the block was invalid.
 *
 *	When acting on a busy resident page and paging is in progress, we
 *	have to wait until paging is complete but otherwise can act on the
 *	busy page.
 *
 *	SWM_POP		remove from meta data but do not free it
 */
static daddr_t
swp_pager_meta_ctl(vm_object_t object, vm_pindex_t pindex, int flags)
{
	struct swblk *sb;
	daddr_t r1;

	if ((flags & SWM_POP) != 0)
		VM_OBJECT_ASSERT_WLOCKED(object);
	else
		VM_OBJECT_ASSERT_LOCKED(object);

	/*
	 * The meta data only exists if the object is OBJT_SWAP
	 * and even then might not be allocated yet.
	 */
	if (object->type != OBJT_SWAP)
		return (SWAPBLK_NONE);

	sb = SWAP_PCTRIE_LOOKUP(&object->un_pager.swp.swp_blks,
	    rounddown(pindex, SWAP_META_PAGES));
	if (sb == NULL)
		return (SWAPBLK_NONE);
	r1 = sb->d[pindex % SWAP_META_PAGES];
	if (r1 == SWAPBLK_NONE)
		return (SWAPBLK_NONE);
	if ((flags & SWM_POP) != 0) {
		sb->d[pindex % SWAP_META_PAGES] = SWAPBLK_NONE;
		if (swp_pager_swblk_empty(sb, 0, SWAP_META_PAGES)) {
			SWAP_PCTRIE_REMOVE(&object->un_pager.swp.swp_blks,
			    rounddown(pindex, SWAP_META_PAGES));
			uma_zfree(swblk_zone, sb);
		}
	}
	return (r1);
}

/*
 * Returns the least page index which is greater than or equal to the
 * parameter pindex and for which there is a swap block allocated.
 * Returns object's size if the object's type is not swap or if there
 * are no allocated swap blocks for the object after the requested
 * pindex.
 */
vm_pindex_t
swap_pager_find_least(vm_object_t object, vm_pindex_t pindex)
{
	struct swblk *sb;
	int i;

	VM_OBJECT_ASSERT_LOCKED(object);
	if (object->type != OBJT_SWAP)
		return (object->size);

	sb = SWAP_PCTRIE_LOOKUP_GE(&object->un_pager.swp.swp_blks,
	    rounddown(pindex, SWAP_META_PAGES));
	if (sb == NULL)
		return (object->size);
	if (sb->p < pindex) {
		for (i = pindex % SWAP_META_PAGES; i < SWAP_META_PAGES; i++) {
			if (sb->d[i] != SWAPBLK_NONE)
				return (sb->p + i);
		}
		sb = SWAP_PCTRIE_LOOKUP_GE(&object->un_pager.swp.swp_blks,
		    roundup(pindex, SWAP_META_PAGES));
		if (sb == NULL)
			return (object->size);
	}
	for (i = 0; i < SWAP_META_PAGES; i++) {
		if (sb->d[i] != SWAPBLK_NONE)
			return (sb->p + i);
	}

	/*
	 * We get here if a swblk is present in the trie but it
	 * doesn't map any blocks.
	 */
	MPASS(0);
	return (object->size);
}

/*
 * System call swapon(name) enables swapping on device name,
 * which must be in the swdevsw.  Return EBUSY
 * if already swapping on this device.
 */
#ifndef _SYS_SYSPROTO_H_
struct swapon_args {
	char *name;
};
#endif

/*
 * MPSAFE
 */
/* ARGSUSED */
int
sys_swapon(struct thread *td, struct swapon_args *uap)
{
	struct vattr attr;
	struct vnode *vp;
	struct nameidata nd;
	int error;

	error = priv_check(td, PRIV_SWAPON);
	if (error)
		return (error);

	sx_xlock(&swdev_syscall_lock);

	/*
	 * Swap metadata may not fit in the KVM if we have physical
	 * memory of >1GB.
	 */
	if (swblk_zone == NULL) {
		error = ENOMEM;
		goto done;
	}

	NDINIT(&nd, LOOKUP, ISOPEN | FOLLOW | AUDITVNODE1, UIO_USERSPACE,
	    uap->name, td);
	error = namei(&nd);
	if (error)
		goto done;

	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;

	if (vn_isdisk(vp, &error)) {
		error = swapongeom(vp);
	} else if (vp->v_type == VREG &&
	    (vp->v_mount->mnt_vfc->vfc_flags & VFCF_NETWORK) != 0 &&
	    (error = VOP_GETATTR(vp, &attr, td->td_ucred)) == 0) {
		/*
		 * Allow direct swapping to NFS regular files in the same
		 * way that nfs_mountroot() sets up diskless swapping.
		 */
		error = swaponvp(td, vp, attr.va_size / DEV_BSIZE);
	}

	if (error)
		vrele(vp);
done:
	sx_xunlock(&swdev_syscall_lock);
	return (error);
}

/*
 * Check that the total amount of swap currently configured does not
 * exceed half the theoretical maximum.  If it does, print a warning
 * message.
 */
static void
swapon_check_swzone(void)
{
	unsigned long maxpages, npages;

	npages = swap_total;
	/* absolute maximum we can handle assuming 100% efficiency */
	maxpages = uma_zone_get_max(swblk_zone) * SWAP_META_PAGES;

	/* recommend using no more than half that amount */
	if (npages > maxpages / 2) {
		printf("warning: total configured swap (%lu pages) "
		    "exceeds maximum recommended amount (%lu pages).\n",
		    npages, maxpages / 2);
		printf("warning: increase kern.maxswzone "
		    "or reduce amount of swap.\n");
	}
}

static void
swaponsomething(struct vnode *vp, void *id, u_long nblks,
    sw_strategy_t *strategy, sw_close_t *close, dev_t dev, int flags)
{
	struct swdevt *sp, *tsp;
	swblk_t dvbase;
	u_long mblocks;

	/*
	 * nblks is in DEV_BSIZE'd chunks, convert to PAGE_SIZE'd chunks.
	 * First chop nblks off to page-align it, then convert.
	 *
	 * sw->sw_nblks is in page-sized chunks now too.
	 */
	nblks &= ~(ctodb(1) - 1);
	nblks = dbtoc(nblks);

	/*
	 * If we go beyond this, we get overflows in the radix
	 * tree bitmap code.
	 */
	mblocks = 0x40000000 / BLIST_META_RADIX;
	if (nblks > mblocks) {
		printf(
    "WARNING: reducing swap size to maximum of %luMB per unit\n",
		    mblocks / 1024 / 1024 * PAGE_SIZE);
		nblks = mblocks;
	}

	sp = malloc(sizeof *sp, M_VMPGDATA, M_WAITOK | M_ZERO);
	sp->sw_vp = vp;
	sp->sw_id = id;
	sp->sw_dev = dev;
	sp->sw_flags = 0;
	sp->sw_nblks = nblks;
	sp->sw_used = 0;
	sp->sw_strategy = strategy;
	sp->sw_close = close;
	sp->sw_flags = flags;

	sp->sw_blist = blist_create(nblks, M_WAITOK);
	/*
	 * Do not free the first two block in order to avoid overwriting
	 * any bsd label at the front of the partition
	 */
	blist_free(sp->sw_blist, 2, nblks - 2);

	dvbase = 0;
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(tsp, &swtailq, sw_list) {
		if (tsp->sw_end >= dvbase) {
			/*
			 * We put one uncovered page between the devices
			 * in order to definitively prevent any cross-device
			 * I/O requests
			 */
			dvbase = tsp->sw_end + 1;
		}
	}
	sp->sw_first = dvbase;
	sp->sw_end = dvbase + nblks;
	TAILQ_INSERT_TAIL(&swtailq, sp, sw_list);
	nswapdev++;
	swap_pager_avail += nblks - 2;
	swap_total += nblks;
	swapon_check_swzone();
	swp_sizecheck();
	mtx_unlock(&sw_dev_mtx);
	EVENTHANDLER_INVOKE(swapon, sp);
}

/*
 * SYSCALL: swapoff(devname)
 *
 * Disable swapping on the given device.
 *
 * XXX: Badly designed system call: it should use a device index
 * rather than filename as specification.  We keep sw_vp around
 * only to make this work.
 */
#ifndef _SYS_SYSPROTO_H_
struct swapoff_args {
	char *name;
};
#endif

/*
 * MPSAFE
 */
/* ARGSUSED */
int
sys_swapoff(struct thread *td, struct swapoff_args *uap)
{
	struct vnode *vp;
	struct nameidata nd;
	struct swdevt *sp;
	int error;

	error = priv_check(td, PRIV_SWAPOFF);
	if (error)
		return (error);

	sx_xlock(&swdev_syscall_lock);

	NDINIT(&nd, LOOKUP, FOLLOW | AUDITVNODE1, UIO_USERSPACE, uap->name,
	    td);
	error = namei(&nd);
	if (error)
		goto done;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;

	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (sp->sw_vp == vp)
			break;
	}
	mtx_unlock(&sw_dev_mtx);
	if (sp == NULL) {
		error = EINVAL;
		goto done;
	}
	error = swapoff_one(sp, td->td_ucred);
done:
	sx_xunlock(&swdev_syscall_lock);
	return (error);
}

static int
swapoff_one(struct swdevt *sp, struct ucred *cred)
{
	u_long nblks;
#ifdef MAC
	int error;
#endif

	sx_assert(&swdev_syscall_lock, SA_XLOCKED);
#ifdef MAC
	(void) vn_lock(sp->sw_vp, LK_EXCLUSIVE | LK_RETRY);
	error = mac_system_check_swapoff(cred, sp->sw_vp);
	(void) VOP_UNLOCK(sp->sw_vp, 0);
	if (error != 0)
		return (error);
#endif
	nblks = sp->sw_nblks;

	/*
	 * We can turn off this swap device safely only if the
	 * available virtual memory in the system will fit the amount
	 * of data we will have to page back in, plus an epsilon so
	 * the system doesn't become critically low on swap space.
	 */
	if (vm_free_count() + swap_pager_avail < nblks + nswap_lowat)
		return (ENOMEM);

	/*
	 * Prevent further allocations on this device.
	 */
	mtx_lock(&sw_dev_mtx);
	sp->sw_flags |= SW_CLOSING;
	swap_pager_avail -= blist_fill(sp->sw_blist, 0, nblks);
	swap_total -= nblks;
	mtx_unlock(&sw_dev_mtx);

	/*
	 * Page in the contents of the device and close it.
	 */
	swap_pager_swapoff(sp);

	sp->sw_close(curthread, sp);
	mtx_lock(&sw_dev_mtx);
	sp->sw_id = NULL;
	TAILQ_REMOVE(&swtailq, sp, sw_list);
	nswapdev--;
	if (nswapdev == 0) {
		swap_pager_full = 2;
		swap_pager_almost_full = 1;
	}
	if (swdevhd == sp)
		swdevhd = NULL;
	mtx_unlock(&sw_dev_mtx);
	blist_destroy(sp->sw_blist);
	free(sp, M_VMPGDATA);
	return (0);
}

void
swapoff_all(void)
{
	struct swdevt *sp, *spt;
	const char *devname;
	int error;

	sx_xlock(&swdev_syscall_lock);

	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH_SAFE(sp, &swtailq, sw_list, spt) {
		mtx_unlock(&sw_dev_mtx);
		if (vn_isdisk(sp->sw_vp, NULL))
			devname = devtoname(sp->sw_vp->v_rdev);
		else
			devname = "[file]";
		error = swapoff_one(sp, thread0.td_ucred);
		if (error != 0) {
			printf("Cannot remove swap device %s (error=%d), "
			    "skipping.\n", devname, error);
		} else if (bootverbose) {
			printf("Swap device %s removed.\n", devname);
		}
		mtx_lock(&sw_dev_mtx);
	}
	mtx_unlock(&sw_dev_mtx);

	sx_xunlock(&swdev_syscall_lock);
}

void
swap_pager_status(int *total, int *used)
{
	struct swdevt *sp;

	*total = 0;
	*used = 0;
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		*total += sp->sw_nblks;
		*used += sp->sw_used;
	}
	mtx_unlock(&sw_dev_mtx);
}

int
swap_dev_info(int name, struct xswdev *xs, char *devname, size_t len)
{
	struct swdevt *sp;
	const char *tmp_devname;
	int error, n;

	n = 0;
	error = ENOENT;
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (n != name) {
			n++;
			continue;
		}
		xs->xsw_version = XSWDEV_VERSION;
		xs->xsw_dev = sp->sw_dev;
		xs->xsw_flags = sp->sw_flags;
		xs->xsw_nblks = sp->sw_nblks;
		xs->xsw_used = sp->sw_used;
		if (devname != NULL) {
			if (vn_isdisk(sp->sw_vp, NULL))
				tmp_devname = devtoname(sp->sw_vp->v_rdev);
			else
				tmp_devname = "[file]";
			strncpy(devname, tmp_devname, len);
		}
		error = 0;
		break;
	}
	mtx_unlock(&sw_dev_mtx);
	return (error);
}

#if defined(COMPAT_FREEBSD11)
#define XSWDEV_VERSION_11	1
struct xswdev11 {
	u_int	xsw_version;
	uint32_t xsw_dev;
	int	xsw_flags;
	int	xsw_nblks;
	int     xsw_used;
};
#endif

#if defined(__amd64__) && defined(COMPAT_FREEBSD32)
struct xswdev32 {
	u_int	xsw_version;
	u_int	xsw_dev1, xsw_dev2;
	int	xsw_flags;
	int	xsw_nblks;
	int     xsw_used;
};
#endif

static int
sysctl_vm_swap_info(SYSCTL_HANDLER_ARGS)
{
	struct xswdev xs;
#if defined(__amd64__) && defined(COMPAT_FREEBSD32)
	struct xswdev32 xs32;
#endif
#if defined(COMPAT_FREEBSD11)
	struct xswdev11 xs11;
#endif
	int error;

	if (arg2 != 1)			/* name length */
		return (EINVAL);
	error = swap_dev_info(*(int *)arg1, &xs, NULL, 0);
	if (error != 0)
		return (error);
#if defined(__amd64__) && defined(COMPAT_FREEBSD32)
	if (req->oldlen == sizeof(xs32)) {
		xs32.xsw_version = XSWDEV_VERSION;
		xs32.xsw_dev1 = xs.xsw_dev;
		xs32.xsw_dev2 = xs.xsw_dev >> 32;
		xs32.xsw_flags = xs.xsw_flags;
		xs32.xsw_nblks = xs.xsw_nblks;
		xs32.xsw_used = xs.xsw_used;
		error = SYSCTL_OUT(req, &xs32, sizeof(xs32));
		return (error);
	}
#endif
#if defined(COMPAT_FREEBSD11)
	if (req->oldlen == sizeof(xs11)) {
		xs11.xsw_version = XSWDEV_VERSION_11;
		xs11.xsw_dev = xs.xsw_dev; /* truncation */
		xs11.xsw_flags = xs.xsw_flags;
		xs11.xsw_nblks = xs.xsw_nblks;
		xs11.xsw_used = xs.xsw_used;
		error = SYSCTL_OUT(req, &xs11, sizeof(xs11));
		return (error);
	}
#endif
	error = SYSCTL_OUT(req, &xs, sizeof(xs));
	return (error);
}

SYSCTL_INT(_vm, OID_AUTO, nswapdev, CTLFLAG_RD, &nswapdev, 0,
    "Number of swap devices");
SYSCTL_NODE(_vm, OID_AUTO, swap_info, CTLFLAG_RD | CTLFLAG_MPSAFE,
    sysctl_vm_swap_info,
    "Swap statistics by device");

/*
 * Count the approximate swap usage in pages for a vmspace.  The
 * shadowed or not yet copied on write swap blocks are not accounted.
 * The map must be locked.
 */
long
vmspace_swap_count(struct vmspace *vmspace)
{
	vm_map_t map;
	vm_map_entry_t cur;
	vm_object_t object;
	struct swblk *sb;
	vm_pindex_t e, pi;
	long count;
	int i;

	map = &vmspace->vm_map;
	count = 0;

	for (cur = map->header.next; cur != &map->header; cur = cur->next) {
		if ((cur->eflags & MAP_ENTRY_IS_SUB_MAP) != 0)
			continue;
		object = cur->object.vm_object;
		if (object == NULL || object->type != OBJT_SWAP)
			continue;
		VM_OBJECT_RLOCK(object);
		if (object->type != OBJT_SWAP)
			goto unlock;
		pi = OFF_TO_IDX(cur->offset);
		e = pi + OFF_TO_IDX(cur->end - cur->start);
		for (;; pi = sb->p + SWAP_META_PAGES) {
			sb = SWAP_PCTRIE_LOOKUP_GE(
			    &object->un_pager.swp.swp_blks, pi);
			if (sb == NULL || sb->p >= e)
				break;
			for (i = 0; i < SWAP_META_PAGES; i++) {
				if (sb->p + i < e &&
				    sb->d[i] != SWAPBLK_NONE)
					count++;
			}
		}
unlock:
		VM_OBJECT_RUNLOCK(object);
	}
	return (count);
}

/*
 * GEOM backend
 *
 * Swapping onto disk devices.
 *
 */

static g_orphan_t swapgeom_orphan;

static struct g_class g_swap_class = {
	.name = "SWAP",
	.version = G_VERSION,
	.orphan = swapgeom_orphan,
};

DECLARE_GEOM_CLASS(g_swap_class, g_class);


static void
swapgeom_close_ev(void *arg, int flags)
{
	struct g_consumer *cp;

	cp = arg;
	g_access(cp, -1, -1, 0);
	g_detach(cp);
	g_destroy_consumer(cp);
}

/*
 * Add a reference to the g_consumer for an inflight transaction.
 */
static void
swapgeom_acquire(struct g_consumer *cp)
{

	mtx_assert(&sw_dev_mtx, MA_OWNED);
	cp->index++;
}

/*
 * Remove a reference from the g_consumer.  Post a close event if all
 * references go away, since the function might be called from the
 * biodone context.
 */
static void
swapgeom_release(struct g_consumer *cp, struct swdevt *sp)
{

	mtx_assert(&sw_dev_mtx, MA_OWNED);
	cp->index--;
	if (cp->index == 0) {
		if (g_post_event(swapgeom_close_ev, cp, M_NOWAIT, NULL) == 0)
			sp->sw_id = NULL;
	}
}

static void
swapgeom_done(struct bio *bp2)
{
	struct swdevt *sp;
	struct buf *bp;
	struct g_consumer *cp;

	bp = bp2->bio_caller2;
	cp = bp2->bio_from;
	bp->b_ioflags = bp2->bio_flags;
	if (bp2->bio_error)
		bp->b_ioflags |= BIO_ERROR;
	bp->b_resid = bp->b_bcount - bp2->bio_completed;
	bp->b_error = bp2->bio_error;
	bp->b_caller1 = NULL;
	bufdone(bp);
	sp = bp2->bio_caller1;
	mtx_lock(&sw_dev_mtx);
	swapgeom_release(cp, sp);
	mtx_unlock(&sw_dev_mtx);
	g_destroy_bio(bp2);
}

static void
swapgeom_strategy(struct buf *bp, struct swdevt *sp)
{
	struct bio *bio;
	struct g_consumer *cp;

	mtx_lock(&sw_dev_mtx);
	cp = sp->sw_id;
	if (cp == NULL) {
		mtx_unlock(&sw_dev_mtx);
		bp->b_error = ENXIO;
		bp->b_ioflags |= BIO_ERROR;
		bufdone(bp);
		return;
	}
	swapgeom_acquire(cp);
	mtx_unlock(&sw_dev_mtx);
	if (bp->b_iocmd == BIO_WRITE)
		bio = g_new_bio();
	else
		bio = g_alloc_bio();
	if (bio == NULL) {
		mtx_lock(&sw_dev_mtx);
		swapgeom_release(cp, sp);
		mtx_unlock(&sw_dev_mtx);
		bp->b_error = ENOMEM;
		bp->b_ioflags |= BIO_ERROR;
		printf("swap_pager: cannot allocate bio\n");
		bufdone(bp);
		return;
	}

	bp->b_caller1 = bio;
	bio->bio_caller1 = sp;
	bio->bio_caller2 = bp;
	bio->bio_cmd = bp->b_iocmd;
	bio->bio_offset = (bp->b_blkno - sp->sw_first) * PAGE_SIZE;
	bio->bio_length = bp->b_bcount;
	bio->bio_done = swapgeom_done;
	if (!buf_mapped(bp)) {
		bio->bio_ma = bp->b_pages;
		bio->bio_data = unmapped_buf;
		bio->bio_ma_offset = (vm_offset_t)bp->b_offset & PAGE_MASK;
		bio->bio_ma_n = bp->b_npages;
		bio->bio_flags |= BIO_UNMAPPED;
	} else {
		bio->bio_data = bp->b_data;
		bio->bio_ma = NULL;
	}
	g_io_request(bio, cp);
	return;
}

static void
swapgeom_orphan(struct g_consumer *cp)
{
	struct swdevt *sp;
	int destroy;

	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (sp->sw_id == cp) {
			sp->sw_flags |= SW_CLOSING;
			break;
		}
	}
	/*
	 * Drop reference we were created with. Do directly since we're in a
	 * special context where we don't have to queue the call to
	 * swapgeom_close_ev().
	 */
	cp->index--;
	destroy = ((sp != NULL) && (cp->index == 0));
	if (destroy)
		sp->sw_id = NULL;
	mtx_unlock(&sw_dev_mtx);
	if (destroy)
		swapgeom_close_ev(cp, 0);
}

static void
swapgeom_close(struct thread *td, struct swdevt *sw)
{
	struct g_consumer *cp;

	mtx_lock(&sw_dev_mtx);
	cp = sw->sw_id;
	sw->sw_id = NULL;
	mtx_unlock(&sw_dev_mtx);

	/*
	 * swapgeom_close() may be called from the biodone context,
	 * where we cannot perform topology changes.  Delegate the
	 * work to the events thread.
	 */
	if (cp != NULL)
		g_waitfor_event(swapgeom_close_ev, cp, M_WAITOK, NULL);
}

static int
swapongeom_locked(struct cdev *dev, struct vnode *vp)
{
	struct g_provider *pp;
	struct g_consumer *cp;
	static struct g_geom *gp;
	struct swdevt *sp;
	u_long nblks;
	int error;

	pp = g_dev_getprovider(dev);
	if (pp == NULL)
		return (ENODEV);
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		cp = sp->sw_id;
		if (cp != NULL && cp->provider == pp) {
			mtx_unlock(&sw_dev_mtx);
			return (EBUSY);
		}
	}
	mtx_unlock(&sw_dev_mtx);
	if (gp == NULL)
		gp = g_new_geomf(&g_swap_class, "swap");
	cp = g_new_consumer(gp);
	cp->index = 1;	/* Number of active I/Os, plus one for being active. */
	cp->flags |=  G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	g_attach(cp, pp);
	/*
	 * XXX: Every time you think you can improve the margin for
	 * footshooting, somebody depends on the ability to do so:
	 * savecore(8) wants to write to our swapdev so we cannot
	 * set an exclusive count :-(
	 */
	error = g_access(cp, 1, 1, 0);
	if (error != 0) {
		g_detach(cp);
		g_destroy_consumer(cp);
		return (error);
	}
	nblks = pp->mediasize / DEV_BSIZE;
	swaponsomething(vp, cp, nblks, swapgeom_strategy,
	    swapgeom_close, dev2udev(dev),
	    (pp->flags & G_PF_ACCEPT_UNMAPPED) != 0 ? SW_UNMAPPED : 0);
	return (0);
}

static int
swapongeom(struct vnode *vp)
{
	int error;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (vp->v_type != VCHR || (vp->v_iflag & VI_DOOMED) != 0) {
		error = ENOENT;
	} else {
		g_topology_lock();
		error = swapongeom_locked(vp->v_rdev, vp);
		g_topology_unlock();
	}
	VOP_UNLOCK(vp, 0);
	return (error);
}

/*
 * VNODE backend
 *
 * This is used mainly for network filesystem (read: probably only tested
 * with NFS) swapfiles.
 *
 */

static void
swapdev_strategy(struct buf *bp, struct swdevt *sp)
{
	struct vnode *vp2;

	bp->b_blkno = ctodb(bp->b_blkno - sp->sw_first);

	vp2 = sp->sw_id;
	vhold(vp2);
	if (bp->b_iocmd == BIO_WRITE) {
		if (bp->b_bufobj)
			bufobj_wdrop(bp->b_bufobj);
		bufobj_wref(&vp2->v_bufobj);
	}
	if (bp->b_bufobj != &vp2->v_bufobj)
		bp->b_bufobj = &vp2->v_bufobj;
	bp->b_vp = vp2;
	bp->b_iooffset = dbtob(bp->b_blkno);
	bstrategy(bp);
	return;
}

static void
swapdev_close(struct thread *td, struct swdevt *sp)
{

	VOP_CLOSE(sp->sw_vp, FREAD | FWRITE, td->td_ucred, td);
	vrele(sp->sw_vp);
}


static int
swaponvp(struct thread *td, struct vnode *vp, u_long nblks)
{
	struct swdevt *sp;
	int error;

	if (nblks == 0)
		return (ENXIO);
	mtx_lock(&sw_dev_mtx);
	TAILQ_FOREACH(sp, &swtailq, sw_list) {
		if (sp->sw_id == vp) {
			mtx_unlock(&sw_dev_mtx);
			return (EBUSY);
		}
	}
	mtx_unlock(&sw_dev_mtx);

	(void) vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
#ifdef MAC
	error = mac_system_check_swapon(td->td_ucred, vp);
	if (error == 0)
#endif
		error = VOP_OPEN(vp, FREAD | FWRITE, td->td_ucred, td, NULL);
	(void) VOP_UNLOCK(vp, 0);
	if (error)
		return (error);

	swaponsomething(vp, vp, nblks, swapdev_strategy, swapdev_close,
	    NODEV, 0);
	return (0);
}

static int
sysctl_swap_async_max(SYSCTL_HANDLER_ARGS)
{
	int error, new, n;

	new = nsw_wcount_async_max;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new > nswbuf / 2 || new < 1)
		return (EINVAL);

	mtx_lock(&swbuf_mtx);
	while (nsw_wcount_async_max != new) {
		/*
		 * Adjust difference.  If the current async count is too low,
		 * we will need to sqeeze our update slowly in.  Sleep with a
		 * higher priority than getpbuf() to finish faster.
		 */
		n = new - nsw_wcount_async_max;
		if (nsw_wcount_async + n >= 0) {
			nsw_wcount_async += n;
			nsw_wcount_async_max += n;
			wakeup(&nsw_wcount_async);
		} else {
			nsw_wcount_async_max -= nsw_wcount_async;
			nsw_wcount_async = 0;
			msleep(&nsw_wcount_async, &swbuf_mtx, PSWP,
			    "swpsysctl", 0);
		}
	}
	mtx_unlock(&swbuf_mtx);

	return (0);
}
