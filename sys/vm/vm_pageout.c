/*-
 * SPDX-License-Identifier: (BSD-4-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 * Copyright (c) 2005 Yahoo! Technologies Norway AS
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_pageout.c	7.4 (Berkeley) 5/7/91
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 *	The proverbial page-out daemon.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/ktr.h>
#include <sys/mount.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/swap_pager.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

/*
 * System initialization
 */

/* the kernel process "vm_pageout"*/
static void vm_pageout(void);
static void vm_pageout_init(void);
static int vm_pageout_clean(vm_page_t m, int *numpagedout);
static int vm_pageout_cluster(vm_page_t m);
static void vm_pageout_mightbe_oom(struct vm_domain *vmd, int page_shortage,
    int starting_page_shortage);

SYSINIT(pagedaemon_init, SI_SUB_KTHREAD_PAGE, SI_ORDER_FIRST, vm_pageout_init,
    NULL);

struct proc *pageproc;

static struct kproc_desc page_kp = {
	"pagedaemon",
	vm_pageout,
	&pageproc
};
SYSINIT(pagedaemon, SI_SUB_KTHREAD_PAGE, SI_ORDER_SECOND, kproc_start,
    &page_kp);

SDT_PROVIDER_DEFINE(vm);
SDT_PROBE_DEFINE(vm, , , vm__lowmem_scan);

/* Pagedaemon activity rates, in subdivisions of one second. */
#define	VM_LAUNDER_RATE		10
#define	VM_INACT_SCAN_RATE	10

static int vm_pageout_oom_seq = 12;

static int vm_pageout_update_period;
static int disable_swap_pageouts;
static int lowmem_period = 10;
static int swapdev_enabled;

static int vm_panic_on_oom = 0;

SYSCTL_INT(_vm, OID_AUTO, panic_on_oom,
	CTLFLAG_RWTUN, &vm_panic_on_oom, 0,
	"panic on out of memory instead of killing the largest process");

SYSCTL_INT(_vm, OID_AUTO, pageout_update_period,
	CTLFLAG_RWTUN, &vm_pageout_update_period, 0,
	"Maximum active LRU update period");
  
SYSCTL_INT(_vm, OID_AUTO, lowmem_period, CTLFLAG_RWTUN, &lowmem_period, 0,
	"Low memory callback period");

SYSCTL_INT(_vm, OID_AUTO, disable_swapspace_pageouts,
	CTLFLAG_RWTUN, &disable_swap_pageouts, 0, "Disallow swapout of dirty pages");

static int pageout_lock_miss;
SYSCTL_INT(_vm, OID_AUTO, pageout_lock_miss,
	CTLFLAG_RD, &pageout_lock_miss, 0, "vget() lock misses during pageout");

SYSCTL_INT(_vm, OID_AUTO, pageout_oom_seq,
	CTLFLAG_RWTUN, &vm_pageout_oom_seq, 0,
	"back-to-back calls to oom detector to start OOM");

static int act_scan_laundry_weight = 3;
SYSCTL_INT(_vm, OID_AUTO, act_scan_laundry_weight, CTLFLAG_RWTUN,
    &act_scan_laundry_weight, 0,
    "weight given to clean vs. dirty pages in active queue scans");

static u_int vm_background_launder_rate = 4096;
SYSCTL_UINT(_vm, OID_AUTO, background_launder_rate, CTLFLAG_RWTUN,
    &vm_background_launder_rate, 0,
    "background laundering rate, in kilobytes per second");

static u_int vm_background_launder_max = 20 * 1024;
SYSCTL_UINT(_vm, OID_AUTO, background_launder_max, CTLFLAG_RWTUN,
    &vm_background_launder_max, 0, "background laundering cap, in kilobytes");

int vm_pageout_page_count = 32;

int vm_page_max_wired;		/* XXX max # of wired pages system-wide */
SYSCTL_INT(_vm, OID_AUTO, max_wired,
	CTLFLAG_RW, &vm_page_max_wired, 0, "System-wide limit to wired page count");

static u_int isqrt(u_int num);
static int vm_pageout_launder(struct vm_domain *vmd, int launder,
    bool in_shortfall);
static void vm_pageout_laundry_worker(void *arg);

struct scan_state {
	struct vm_batchqueue bq;
	struct vm_pagequeue *pq;
	vm_page_t	marker;
	int		maxscan;
	int		scanned;
};

static void
vm_pageout_init_scan(struct scan_state *ss, struct vm_pagequeue *pq,
    vm_page_t marker, vm_page_t after, int maxscan)
{

	vm_pagequeue_assert_locked(pq);
	KASSERT((marker->aflags & PGA_ENQUEUED) == 0,
	    ("marker %p already enqueued", marker));

	if (after == NULL)
		TAILQ_INSERT_HEAD(&pq->pq_pl, marker, plinks.q);
	else
		TAILQ_INSERT_AFTER(&pq->pq_pl, after, marker, plinks.q);
	vm_page_aflag_set(marker, PGA_ENQUEUED);

	vm_batchqueue_init(&ss->bq);
	ss->pq = pq;
	ss->marker = marker;
	ss->maxscan = maxscan;
	ss->scanned = 0;
	vm_pagequeue_unlock(pq);
}

static void
vm_pageout_end_scan(struct scan_state *ss)
{
	struct vm_pagequeue *pq;

	pq = ss->pq;
	vm_pagequeue_assert_locked(pq);
	KASSERT((ss->marker->aflags & PGA_ENQUEUED) != 0,
	    ("marker %p not enqueued", ss->marker));

	TAILQ_REMOVE(&pq->pq_pl, ss->marker, plinks.q);
	vm_page_aflag_clear(ss->marker, PGA_ENQUEUED);
	pq->pq_pdpages += ss->scanned;
}

/*
 * Add a small number of queued pages to a batch queue for later processing
 * without the corresponding queue lock held.  The caller must have enqueued a
 * marker page at the desired start point for the scan.  Pages will be
 * physically dequeued if the caller so requests.  Otherwise, the returned
 * batch may contain marker pages, and it is up to the caller to handle them.
 *
 * When processing the batch queue, vm_page_queue() must be used to
 * determine whether the page has been logically dequeued by another thread.
 * Once this check is performed, the page lock guarantees that the page will
 * not be disassociated from the queue.
 */
static __always_inline void
vm_pageout_collect_batch(struct scan_state *ss, const bool dequeue)
{
	struct vm_pagequeue *pq;
	vm_page_t m, marker;

	marker = ss->marker;
	pq = ss->pq;

	KASSERT((marker->aflags & PGA_ENQUEUED) != 0,
	    ("marker %p not enqueued", ss->marker));

	vm_pagequeue_lock(pq);
	for (m = TAILQ_NEXT(marker, plinks.q); m != NULL &&
	    ss->scanned < ss->maxscan && ss->bq.bq_cnt < VM_BATCHQUEUE_SIZE;
	    m = TAILQ_NEXT(m, plinks.q), ss->scanned++) {
		if ((m->flags & PG_MARKER) == 0) {
			KASSERT((m->aflags & PGA_ENQUEUED) != 0,
			    ("page %p not enqueued", m));
			KASSERT((m->flags & PG_FICTITIOUS) == 0,
			    ("Fictitious page %p cannot be in page queue", m));
			KASSERT((m->oflags & VPO_UNMANAGED) == 0,
			    ("Unmanaged page %p cannot be in page queue", m));
		} else if (dequeue)
			continue;

		(void)vm_batchqueue_insert(&ss->bq, m);
		if (dequeue) {
			TAILQ_REMOVE(&pq->pq_pl, m, plinks.q);
			vm_page_aflag_clear(m, PGA_ENQUEUED);
		}
	}
	TAILQ_REMOVE(&pq->pq_pl, marker, plinks.q);
	if (__predict_true(m != NULL))
		TAILQ_INSERT_BEFORE(m, marker, plinks.q);
	else
		TAILQ_INSERT_TAIL(&pq->pq_pl, marker, plinks.q);
	if (dequeue)
		vm_pagequeue_cnt_add(pq, -ss->bq.bq_cnt);
	vm_pagequeue_unlock(pq);
}

/* Return the next page to be scanned, or NULL if the scan is complete. */
static __always_inline vm_page_t
vm_pageout_next(struct scan_state *ss, const bool dequeue)
{

	if (ss->bq.bq_cnt == 0)
		vm_pageout_collect_batch(ss, dequeue);
	return (vm_batchqueue_pop(&ss->bq));
}

/*
 * Scan for pages at adjacent offsets within the given page's object that are
 * eligible for laundering, form a cluster of these pages and the given page,
 * and launder that cluster.
 */
static int
vm_pageout_cluster(vm_page_t m)
{
	vm_object_t object;
	vm_page_t mc[2 * vm_pageout_page_count], p, pb, ps;
	vm_pindex_t pindex;
	int ib, is, page_base, pageout_count;

	vm_page_assert_locked(m);
	object = m->object;
	VM_OBJECT_ASSERT_WLOCKED(object);
	pindex = m->pindex;

	vm_page_assert_unbusied(m);
	KASSERT(!vm_page_held(m), ("page %p is held", m));

	pmap_remove_write(m);
	vm_page_unlock(m);

	mc[vm_pageout_page_count] = pb = ps = m;
	pageout_count = 1;
	page_base = vm_pageout_page_count;
	ib = 1;
	is = 1;

	/*
	 * We can cluster only if the page is not clean, busy, or held, and
	 * the page is in the laundry queue.
	 *
	 * During heavy mmap/modification loads the pageout
	 * daemon can really fragment the underlying file
	 * due to flushing pages out of order and not trying to
	 * align the clusters (which leaves sporadic out-of-order
	 * holes).  To solve this problem we do the reverse scan
	 * first and attempt to align our cluster, then do a 
	 * forward scan if room remains.
	 */
more:
	while (ib != 0 && pageout_count < vm_pageout_page_count) {
		if (ib > pindex) {
			ib = 0;
			break;
		}
		if ((p = vm_page_prev(pb)) == NULL || vm_page_busied(p)) {
			ib = 0;
			break;
		}
		vm_page_test_dirty(p);
		if (p->dirty == 0) {
			ib = 0;
			break;
		}
		vm_page_lock(p);
		if (vm_page_held(p) || !vm_page_in_laundry(p)) {
			vm_page_unlock(p);
			ib = 0;
			break;
		}
		pmap_remove_write(p);
		vm_page_unlock(p);
		mc[--page_base] = pb = p;
		++pageout_count;
		++ib;

		/*
		 * We are at an alignment boundary.  Stop here, and switch
		 * directions.  Do not clear ib.
		 */
		if ((pindex - (ib - 1)) % vm_pageout_page_count == 0)
			break;
	}
	while (pageout_count < vm_pageout_page_count && 
	    pindex + is < object->size) {
		if ((p = vm_page_next(ps)) == NULL || vm_page_busied(p))
			break;
		vm_page_test_dirty(p);
		if (p->dirty == 0)
			break;
		vm_page_lock(p);
		if (vm_page_held(p) || !vm_page_in_laundry(p)) {
			vm_page_unlock(p);
			break;
		}
		pmap_remove_write(p);
		vm_page_unlock(p);
		mc[page_base + pageout_count] = ps = p;
		++pageout_count;
		++is;
	}

	/*
	 * If we exhausted our forward scan, continue with the reverse scan
	 * when possible, even past an alignment boundary.  This catches
	 * boundary conditions.
	 */
	if (ib != 0 && pageout_count < vm_pageout_page_count)
		goto more;

	return (vm_pageout_flush(&mc[page_base], pageout_count,
	    VM_PAGER_PUT_NOREUSE, 0, NULL, NULL));
}

/*
 * vm_pageout_flush() - launder the given pages
 *
 *	The given pages are laundered.  Note that we setup for the start of
 *	I/O ( i.e. busy the page ), mark it read-only, and bump the object
 *	reference count all in here rather then in the parent.  If we want
 *	the parent to do more sophisticated things we may have to change
 *	the ordering.
 *
 *	Returned runlen is the count of pages between mreq and first
 *	page after mreq with status VM_PAGER_AGAIN.
 *	*eio is set to TRUE if pager returned VM_PAGER_ERROR or VM_PAGER_FAIL
 *	for any page in runlen set.
 */
int
vm_pageout_flush(vm_page_t *mc, int count, int flags, int mreq, int *prunlen,
    boolean_t *eio)
{
	vm_object_t object = mc[0]->object;
	int pageout_status[count];
	int numpagedout = 0;
	int i, runlen;

	VM_OBJECT_ASSERT_WLOCKED(object);

	/*
	 * Initiate I/O.  Mark the pages busy and verify that they're valid
	 * and read-only.
	 *
	 * We do not have to fixup the clean/dirty bits here... we can
	 * allow the pager to do it after the I/O completes.
	 *
	 * NOTE! mc[i]->dirty may be partial or fragmented due to an
	 * edge case with file fragments.
	 */
	for (i = 0; i < count; i++) {
		KASSERT(mc[i]->valid == VM_PAGE_BITS_ALL,
		    ("vm_pageout_flush: partially invalid page %p index %d/%d",
			mc[i], i, count));
		KASSERT((mc[i]->aflags & PGA_WRITEABLE) == 0,
		    ("vm_pageout_flush: writeable page %p", mc[i]));
		vm_page_sbusy(mc[i]);
	}
	vm_object_pip_add(object, count);

	vm_pager_put_pages(object, mc, count, flags, pageout_status);

	runlen = count - mreq;
	if (eio != NULL)
		*eio = FALSE;
	for (i = 0; i < count; i++) {
		vm_page_t mt = mc[i];

		KASSERT(pageout_status[i] == VM_PAGER_PEND ||
		    !pmap_page_is_write_mapped(mt),
		    ("vm_pageout_flush: page %p is not write protected", mt));
		switch (pageout_status[i]) {
		case VM_PAGER_OK:
			vm_page_lock(mt);
			if (vm_page_in_laundry(mt))
				vm_page_deactivate_noreuse(mt);
			vm_page_unlock(mt);
			/* FALLTHROUGH */
		case VM_PAGER_PEND:
			numpagedout++;
			break;
		case VM_PAGER_BAD:
			/*
			 * The page is outside the object's range.  We pretend
			 * that the page out worked and clean the page, so the
			 * changes will be lost if the page is reclaimed by
			 * the page daemon.
			 */
			vm_page_undirty(mt);
			vm_page_lock(mt);
			if (vm_page_in_laundry(mt))
				vm_page_deactivate_noreuse(mt);
			vm_page_unlock(mt);
			break;
		case VM_PAGER_ERROR:
		case VM_PAGER_FAIL:
			/*
			 * If the page couldn't be paged out to swap because the
			 * pager wasn't able to find space, place the page in
			 * the PQ_UNSWAPPABLE holding queue.  This is an
			 * optimization that prevents the page daemon from
			 * wasting CPU cycles on pages that cannot be reclaimed
			 * becase no swap device is configured.
			 *
			 * Otherwise, reactivate the page so that it doesn't
			 * clog the laundry and inactive queues.  (We will try
			 * paging it out again later.)
			 */
			vm_page_lock(mt);
			if (object->type == OBJT_SWAP &&
			    pageout_status[i] == VM_PAGER_FAIL) {
				vm_page_unswappable(mt);
				numpagedout++;
			} else
				vm_page_activate(mt);
			vm_page_unlock(mt);
			if (eio != NULL && i >= mreq && i - mreq < runlen)
				*eio = TRUE;
			break;
		case VM_PAGER_AGAIN:
			if (i >= mreq && i - mreq < runlen)
				runlen = i - mreq;
			break;
		}

		/*
		 * If the operation is still going, leave the page busy to
		 * block all other accesses. Also, leave the paging in
		 * progress indicator set so that we don't attempt an object
		 * collapse.
		 */
		if (pageout_status[i] != VM_PAGER_PEND) {
			vm_object_pip_wakeup(object);
			vm_page_sunbusy(mt);
		}
	}
	if (prunlen != NULL)
		*prunlen = runlen;
	return (numpagedout);
}

static void
vm_pageout_swapon(void *arg __unused, struct swdevt *sp __unused)
{

	atomic_store_rel_int(&swapdev_enabled, 1);
}

static void
vm_pageout_swapoff(void *arg __unused, struct swdevt *sp __unused)
{

	if (swap_pager_nswapdev() == 1)
		atomic_store_rel_int(&swapdev_enabled, 0);
}

/*
 * Attempt to acquire all of the necessary locks to launder a page and
 * then call through the clustering layer to PUTPAGES.  Wait a short
 * time for a vnode lock.
 *
 * Requires the page and object lock on entry, releases both before return.
 * Returns 0 on success and an errno otherwise.
 */
static int
vm_pageout_clean(vm_page_t m, int *numpagedout)
{
	struct vnode *vp;
	struct mount *mp;
	vm_object_t object;
	vm_pindex_t pindex;
	int error, lockmode;

	vm_page_assert_locked(m);
	object = m->object;
	VM_OBJECT_ASSERT_WLOCKED(object);
	error = 0;
	vp = NULL;
	mp = NULL;

	/*
	 * The object is already known NOT to be dead.   It
	 * is possible for the vget() to block the whole
	 * pageout daemon, but the new low-memory handling
	 * code should prevent it.
	 *
	 * We can't wait forever for the vnode lock, we might
	 * deadlock due to a vn_read() getting stuck in
	 * vm_wait while holding this vnode.  We skip the 
	 * vnode if we can't get it in a reasonable amount
	 * of time.
	 */
	if (object->type == OBJT_VNODE) {
		vm_page_unlock(m);
		vp = object->handle;
		if (vp->v_type == VREG &&
		    vn_start_write(vp, &mp, V_NOWAIT) != 0) {
			mp = NULL;
			error = EDEADLK;
			goto unlock_all;
		}
		KASSERT(mp != NULL,
		    ("vp %p with NULL v_mount", vp));
		vm_object_reference_locked(object);
		pindex = m->pindex;
		VM_OBJECT_WUNLOCK(object);
		lockmode = MNT_SHARED_WRITES(vp->v_mount) ?
		    LK_SHARED : LK_EXCLUSIVE;
		if (vget(vp, lockmode | LK_TIMELOCK, curthread)) {
			vp = NULL;
			error = EDEADLK;
			goto unlock_mp;
		}
		VM_OBJECT_WLOCK(object);

		/*
		 * Ensure that the object and vnode were not disassociated
		 * while locks were dropped.
		 */
		if (vp->v_object != object) {
			error = ENOENT;
			goto unlock_all;
		}
		vm_page_lock(m);

		/*
		 * While the object and page were unlocked, the page
		 * may have been:
		 * (1) moved to a different queue,
		 * (2) reallocated to a different object,
		 * (3) reallocated to a different offset, or
		 * (4) cleaned.
		 */
		if (!vm_page_in_laundry(m) || m->object != object ||
		    m->pindex != pindex || m->dirty == 0) {
			vm_page_unlock(m);
			error = ENXIO;
			goto unlock_all;
		}

		/*
		 * The page may have been busied or referenced while the object
		 * and page locks were released.
		 */
		if (vm_page_busied(m) || vm_page_held(m)) {
			vm_page_unlock(m);
			error = EBUSY;
			goto unlock_all;
		}
	}

	/*
	 * If a page is dirty, then it is either being washed
	 * (but not yet cleaned) or it is still in the
	 * laundry.  If it is still in the laundry, then we
	 * start the cleaning operation. 
	 */
	if ((*numpagedout = vm_pageout_cluster(m)) == 0)
		error = EIO;

unlock_all:
	VM_OBJECT_WUNLOCK(object);

unlock_mp:
	vm_page_lock_assert(m, MA_NOTOWNED);
	if (mp != NULL) {
		if (vp != NULL)
			vput(vp);
		vm_object_deallocate(object);
		vn_finished_write(mp);
	}

	return (error);
}

/*
 * Attempt to launder the specified number of pages.
 *
 * Returns the number of pages successfully laundered.
 */
static int
vm_pageout_launder(struct vm_domain *vmd, int launder, bool in_shortfall)
{
	struct scan_state ss;
	struct vm_pagequeue *pq;
	struct mtx *mtx;
	vm_object_t object;
	vm_page_t m, marker;
	int act_delta, error, numpagedout, queue, starting_target;
	int vnodes_skipped;
	bool pageout_ok;

	mtx = NULL;
	object = NULL;
	starting_target = launder;
	vnodes_skipped = 0;

	/*
	 * Scan the laundry queues for pages eligible to be laundered.  We stop
	 * once the target number of dirty pages have been laundered, or once
	 * we've reached the end of the queue.  A single iteration of this loop
	 * may cause more than one page to be laundered because of clustering.
	 *
	 * As an optimization, we avoid laundering from PQ_UNSWAPPABLE when no
	 * swap devices are configured.
	 */
	if (atomic_load_acq_int(&swapdev_enabled))
		queue = PQ_UNSWAPPABLE;
	else
		queue = PQ_LAUNDRY;

scan:
	marker = &vmd->vmd_markers[queue];
	pq = &vmd->vmd_pagequeues[queue];
	vm_pagequeue_lock(pq);
	vm_pageout_init_scan(&ss, pq, marker, NULL, pq->pq_cnt);
	while (launder > 0 && (m = vm_pageout_next(&ss, false)) != NULL) {
		if (__predict_false((m->flags & PG_MARKER) != 0))
			continue;

		vm_page_change_lock(m, &mtx);

recheck:
		/*
		 * The page may have been disassociated from the queue
		 * while locks were dropped.
		 */
		if (vm_page_queue(m) != queue)
			continue;

		/*
		 * A requeue was requested, so this page gets a second
		 * chance.
		 */
		if ((m->aflags & PGA_REQUEUE) != 0) {
			vm_page_requeue(m);
			continue;
		}

		/*
		 * Held pages are essentially stuck in the queue.
		 *
		 * Wired pages may not be freed.  Complete their removal
		 * from the queue now to avoid needless revisits during
		 * future scans.
		 */
		if (m->hold_count != 0)
			continue;
		if (m->wire_count != 0) {
			vm_page_dequeue_deferred(m);
			continue;
		}

		if (object != m->object) {
			if (object != NULL)
				VM_OBJECT_WUNLOCK(object);
			object = m->object;
			if (!VM_OBJECT_TRYWLOCK(object)) {
				mtx_unlock(mtx);
				/* Depends on type-stability. */
				VM_OBJECT_WLOCK(object);
				mtx_lock(mtx);
				goto recheck;
			}
		}

		if (vm_page_busied(m))
			continue;

		/*
		 * Invalid pages can be easily freed.  They cannot be
		 * mapped; vm_page_free() asserts this.
		 */
		if (m->valid == 0)
			goto free_page;

		/*
		 * If the page has been referenced and the object is not dead,
		 * reactivate or requeue the page depending on whether the
		 * object is mapped.
		 *
		 * Test PGA_REFERENCED after calling pmap_ts_referenced() so
		 * that a reference from a concurrently destroyed mapping is
		 * observed here and now.
		 */
		if (object->ref_count != 0)
			act_delta = pmap_ts_referenced(m);
		else {
			KASSERT(!pmap_page_is_mapped(m),
			    ("page %p is mapped", m));
			act_delta = 0;
		}
		if ((m->aflags & PGA_REFERENCED) != 0) {
			vm_page_aflag_clear(m, PGA_REFERENCED);
			act_delta++;
		}
		if (act_delta != 0) {
			if (object->ref_count != 0) {
				VM_CNT_INC(v_reactivated);
				vm_page_activate(m);

				/*
				 * Increase the activation count if the page
				 * was referenced while in the laundry queue.
				 * This makes it less likely that the page will
				 * be returned prematurely to the inactive
				 * queue.
 				 */
				m->act_count += act_delta + ACT_ADVANCE;

				/*
				 * If this was a background laundering, count
				 * activated pages towards our target.  The
				 * purpose of background laundering is to ensure
				 * that pages are eventually cycled through the
				 * laundry queue, and an activation is a valid
				 * way out.
				 */
				if (!in_shortfall)
					launder--;
				continue;
			} else if ((object->flags & OBJ_DEAD) == 0) {
				vm_page_requeue(m);
				continue;
			}
		}

		/*
		 * If the page appears to be clean at the machine-independent
		 * layer, then remove all of its mappings from the pmap in
		 * anticipation of freeing it.  If, however, any of the page's
		 * mappings allow write access, then the page may still be
		 * modified until the last of those mappings are removed.
		 */
		if (object->ref_count != 0) {
			vm_page_test_dirty(m);
			if (m->dirty == 0)
				pmap_remove_all(m);
		}

		/*
		 * Clean pages are freed, and dirty pages are paged out unless
		 * they belong to a dead object.  Requeueing dirty pages from
		 * dead objects is pointless, as they are being paged out and
		 * freed by the thread that destroyed the object.
		 */
		if (m->dirty == 0) {
free_page:
			vm_page_free(m);
			VM_CNT_INC(v_dfree);
		} else if ((object->flags & OBJ_DEAD) == 0) {
			if (object->type != OBJT_SWAP &&
			    object->type != OBJT_DEFAULT)
				pageout_ok = true;
			else if (disable_swap_pageouts)
				pageout_ok = false;
			else
				pageout_ok = true;
			if (!pageout_ok) {
				vm_page_requeue(m);
				continue;
			}

			/*
			 * Form a cluster with adjacent, dirty pages from the
			 * same object, and page out that entire cluster.
			 *
			 * The adjacent, dirty pages must also be in the
			 * laundry.  However, their mappings are not checked
			 * for new references.  Consequently, a recently
			 * referenced page may be paged out.  However, that
			 * page will not be prematurely reclaimed.  After page
			 * out, the page will be placed in the inactive queue,
			 * where any new references will be detected and the
			 * page reactivated.
			 */
			error = vm_pageout_clean(m, &numpagedout);
			if (error == 0) {
				launder -= numpagedout;
				ss.scanned += numpagedout;
			} else if (error == EDEADLK) {
				pageout_lock_miss++;
				vnodes_skipped++;
			}
			mtx = NULL;
			object = NULL;
		}
	}
	if (mtx != NULL) {
		mtx_unlock(mtx);
		mtx = NULL;
	}
	if (object != NULL) {
		VM_OBJECT_WUNLOCK(object);
		object = NULL;
	}
	vm_pagequeue_lock(pq);
	vm_pageout_end_scan(&ss);
	vm_pagequeue_unlock(pq);

	if (launder > 0 && queue == PQ_UNSWAPPABLE) {
		queue = PQ_LAUNDRY;
		goto scan;
	}

	/*
	 * Wakeup the sync daemon if we skipped a vnode in a writeable object
	 * and we didn't launder enough pages.
	 */
	if (vnodes_skipped > 0 && launder > 0)
		(void)speedup_syncer();

	return (starting_target - launder);
}

/*
 * Compute the integer square root.
 */
static u_int
isqrt(u_int num)
{
	u_int bit, root, tmp;

	bit = 1u << ((NBBY * sizeof(u_int)) - 2);
	while (bit > num)
		bit >>= 2;
	root = 0;
	while (bit != 0) {
		tmp = root + bit;
		root >>= 1;
		if (num >= tmp) {
			num -= tmp;
			root += bit;
		}
		bit >>= 2;
	}
	return (root);
}

/*
 * Perform the work of the laundry thread: periodically wake up and determine
 * whether any pages need to be laundered.  If so, determine the number of pages
 * that need to be laundered, and launder them.
 */
static void
vm_pageout_laundry_worker(void *arg)
{
	struct vm_domain *vmd;
	struct vm_pagequeue *pq;
	uint64_t nclean, ndirty, nfreed;
	int domain, last_target, launder, shortfall, shortfall_cycle, target;
	bool in_shortfall;

	domain = (uintptr_t)arg;
	vmd = VM_DOMAIN(domain);
	pq = &vmd->vmd_pagequeues[PQ_LAUNDRY];
	KASSERT(vmd->vmd_segs != 0, ("domain without segments"));

	shortfall = 0;
	in_shortfall = false;
	shortfall_cycle = 0;
	last_target = target = 0;
	nfreed = 0;

	/*
	 * Calls to these handlers are serialized by the swap syscall lock.
	 */
	(void)EVENTHANDLER_REGISTER(swapon, vm_pageout_swapon, vmd,
	    EVENTHANDLER_PRI_ANY);
	(void)EVENTHANDLER_REGISTER(swapoff, vm_pageout_swapoff, vmd,
	    EVENTHANDLER_PRI_ANY);

	/*
	 * The pageout laundry worker is never done, so loop forever.
	 */
	for (;;) {
		KASSERT(target >= 0, ("negative target %d", target));
		KASSERT(shortfall_cycle >= 0,
		    ("negative cycle %d", shortfall_cycle));
		launder = 0;

		/*
		 * First determine whether we need to launder pages to meet a
		 * shortage of free pages.
		 */
		if (shortfall > 0) {
			in_shortfall = true;
			shortfall_cycle = VM_LAUNDER_RATE / VM_INACT_SCAN_RATE;
			target = shortfall;
		} else if (!in_shortfall)
			goto trybackground;
		else if (shortfall_cycle == 0 || vm_laundry_target(vmd) <= 0) {
			/*
			 * We recently entered shortfall and began laundering
			 * pages.  If we have completed that laundering run
			 * (and we are no longer in shortfall) or we have met
			 * our laundry target through other activity, then we
			 * can stop laundering pages.
			 */
			in_shortfall = false;
			target = 0;
			goto trybackground;
		}
		launder = target / shortfall_cycle--;
		goto dolaundry;

		/*
		 * There's no immediate need to launder any pages; see if we
		 * meet the conditions to perform background laundering:
		 *
		 * 1. The ratio of dirty to clean inactive pages exceeds the
		 *    background laundering threshold, or
		 * 2. we haven't yet reached the target of the current
		 *    background laundering run.
		 *
		 * The background laundering threshold is not a constant.
		 * Instead, it is a slowly growing function of the number of
		 * clean pages freed by the page daemon since the last
		 * background laundering.  Thus, as the ratio of dirty to
		 * clean inactive pages grows, the amount of memory pressure
		 * required to trigger laundering decreases.  We ensure
		 * that the threshold is non-zero after an inactive queue
		 * scan, even if that scan failed to free a single clean page.
		 */
trybackground:
		nclean = vmd->vmd_free_count +
		    vmd->vmd_pagequeues[PQ_INACTIVE].pq_cnt;
		ndirty = vmd->vmd_pagequeues[PQ_LAUNDRY].pq_cnt;
		if (target == 0 && ndirty * isqrt(howmany(nfreed + 1,
		    vmd->vmd_free_target - vmd->vmd_free_min)) >= nclean) {
			target = vmd->vmd_background_launder_target;
		}

		/*
		 * We have a non-zero background laundering target.  If we've
		 * laundered up to our maximum without observing a page daemon
		 * request, just stop.  This is a safety belt that ensures we
		 * don't launder an excessive amount if memory pressure is low
		 * and the ratio of dirty to clean pages is large.  Otherwise,
		 * proceed at the background laundering rate.
		 */
		if (target > 0) {
			if (nfreed > 0) {
				nfreed = 0;
				last_target = target;
			} else if (last_target - target >=
			    vm_background_launder_max * PAGE_SIZE / 1024) {
				target = 0;
			}
			launder = vm_background_launder_rate * PAGE_SIZE / 1024;
			launder /= VM_LAUNDER_RATE;
			if (launder > target)
				launder = target;
		}

dolaundry:
		if (launder > 0) {
			/*
			 * Because of I/O clustering, the number of laundered
			 * pages could exceed "target" by the maximum size of
			 * a cluster minus one. 
			 */
			target -= min(vm_pageout_launder(vmd, launder,
			    in_shortfall), target);
			pause("laundp", hz / VM_LAUNDER_RATE);
		}

		/*
		 * If we're not currently laundering pages and the page daemon
		 * hasn't posted a new request, sleep until the page daemon
		 * kicks us.
		 */
		vm_pagequeue_lock(pq);
		if (target == 0 && vmd->vmd_laundry_request == VM_LAUNDRY_IDLE)
			(void)mtx_sleep(&vmd->vmd_laundry_request,
			    vm_pagequeue_lockptr(pq), PVM, "launds", 0);

		/*
		 * If the pagedaemon has indicated that it's in shortfall, start
		 * a shortfall laundering unless we're already in the middle of
		 * one.  This may preempt a background laundering.
		 */
		if (vmd->vmd_laundry_request == VM_LAUNDRY_SHORTFALL &&
		    (!in_shortfall || shortfall_cycle == 0)) {
			shortfall = vm_laundry_target(vmd) +
			    vmd->vmd_pageout_deficit;
			target = 0;
		} else
			shortfall = 0;

		if (target == 0)
			vmd->vmd_laundry_request = VM_LAUNDRY_IDLE;
		nfreed += vmd->vmd_clean_pages_freed;
		vmd->vmd_clean_pages_freed = 0;
		vm_pagequeue_unlock(pq);
	}
}

/*
 * Compute the number of pages we want to try to move from the
 * active queue to either the inactive or laundry queue.
 *
 * When scanning active pages during a shortage, we make clean pages
 * count more heavily towards the page shortage than dirty pages.
 * This is because dirty pages must be laundered before they can be
 * reused and thus have less utility when attempting to quickly
 * alleviate a free page shortage.  However, this weighting also
 * causes the scan to deactivate dirty pages more aggressively,
 * improving the effectiveness of clustering.
 */
static int
vm_pageout_active_target(struct vm_domain *vmd)
{
	int shortage;

	shortage = vmd->vmd_inactive_target + vm_paging_target(vmd) -
	    (vmd->vmd_pagequeues[PQ_INACTIVE].pq_cnt +
	    vmd->vmd_pagequeues[PQ_LAUNDRY].pq_cnt / act_scan_laundry_weight);
	shortage *= act_scan_laundry_weight;
	return (shortage);
}

/*
 * Scan the active queue.  If there is no shortage of inactive pages, scan a
 * small portion of the queue in order to maintain quasi-LRU.
 */
static void
vm_pageout_scan_active(struct vm_domain *vmd, int page_shortage)
{
	struct scan_state ss;
	struct mtx *mtx;
	vm_page_t m, marker;
	struct vm_pagequeue *pq;
	long min_scan;
	int act_delta, max_scan, scan_tick;

	marker = &vmd->vmd_markers[PQ_ACTIVE];
	pq = &vmd->vmd_pagequeues[PQ_ACTIVE];
	vm_pagequeue_lock(pq);

	/*
	 * If we're just idle polling attempt to visit every
	 * active page within 'update_period' seconds.
	 */
	scan_tick = ticks;
	if (vm_pageout_update_period != 0) {
		min_scan = pq->pq_cnt;
		min_scan *= scan_tick - vmd->vmd_last_active_scan;
		min_scan /= hz * vm_pageout_update_period;
	} else
		min_scan = 0;
	if (min_scan > 0 || (page_shortage > 0 && pq->pq_cnt > 0))
		vmd->vmd_last_active_scan = scan_tick;

	/*
	 * Scan the active queue for pages that can be deactivated.  Update
	 * the per-page activity counter and use it to identify deactivation
	 * candidates.  Held pages may be deactivated.
	 *
	 * To avoid requeuing each page that remains in the active queue, we
	 * implement the CLOCK algorithm.  To keep the implementation of the
	 * enqueue operation consistent for all page queues, we use two hands,
	 * represented by marker pages. Scans begin at the first hand, which
	 * precedes the second hand in the queue.  When the two hands meet,
	 * they are moved back to the head and tail of the queue, respectively,
	 * and scanning resumes.
	 */
	max_scan = page_shortage > 0 ? pq->pq_cnt : min_scan;
	mtx = NULL;
act_scan:
	vm_pageout_init_scan(&ss, pq, marker, &vmd->vmd_clock[0], max_scan);
	while ((m = vm_pageout_next(&ss, false)) != NULL) {
		if (__predict_false(m == &vmd->vmd_clock[1])) {
			vm_pagequeue_lock(pq);
			TAILQ_REMOVE(&pq->pq_pl, &vmd->vmd_clock[0], plinks.q);
			TAILQ_REMOVE(&pq->pq_pl, &vmd->vmd_clock[1], plinks.q);
			TAILQ_INSERT_HEAD(&pq->pq_pl, &vmd->vmd_clock[0],
			    plinks.q);
			TAILQ_INSERT_TAIL(&pq->pq_pl, &vmd->vmd_clock[1],
			    plinks.q);
			max_scan -= ss.scanned;
			vm_pageout_end_scan(&ss);
			goto act_scan;
		}
		if (__predict_false((m->flags & PG_MARKER) != 0))
			continue;

		vm_page_change_lock(m, &mtx);

		/*
		 * The page may have been disassociated from the queue
		 * while locks were dropped.
		 */
		if (vm_page_queue(m) != PQ_ACTIVE)
			continue;

		/*
		 * Wired pages are dequeued lazily.
		 */
		if (m->wire_count != 0) {
			vm_page_dequeue_deferred(m);
			continue;
		}

		/*
		 * Check to see "how much" the page has been used.
		 *
		 * Test PGA_REFERENCED after calling pmap_ts_referenced() so
		 * that a reference from a concurrently destroyed mapping is
		 * observed here and now.
		 *
		 * Perform an unsynchronized object ref count check.  While
		 * the page lock ensures that the page is not reallocated to
		 * another object, in particular, one with unmanaged mappings
		 * that cannot support pmap_ts_referenced(), two races are,
		 * nonetheless, possible:
		 * 1) The count was transitioning to zero, but we saw a non-
		 *    zero value.  pmap_ts_referenced() will return zero
		 *    because the page is not mapped.
		 * 2) The count was transitioning to one, but we saw zero.
		 *    This race delays the detection of a new reference.  At
		 *    worst, we will deactivate and reactivate the page.
		 */
		if (m->object->ref_count != 0)
			act_delta = pmap_ts_referenced(m);
		else
			act_delta = 0;
		if ((m->aflags & PGA_REFERENCED) != 0) {
			vm_page_aflag_clear(m, PGA_REFERENCED);
			act_delta++;
		}

		/*
		 * Advance or decay the act_count based on recent usage.
		 */
		if (act_delta != 0) {
			m->act_count += ACT_ADVANCE + act_delta;
			if (m->act_count > ACT_MAX)
				m->act_count = ACT_MAX;
		} else
			m->act_count -= min(m->act_count, ACT_DECLINE);

		if (m->act_count == 0) {
			/*
			 * When not short for inactive pages, let dirty pages go
			 * through the inactive queue before moving to the
			 * laundry queues.  This gives them some extra time to
			 * be reactivated, potentially avoiding an expensive
			 * pageout.  However, during a page shortage, the
			 * inactive queue is necessarily small, and so dirty
			 * pages would only spend a trivial amount of time in
			 * the inactive queue.  Therefore, we might as well
			 * place them directly in the laundry queue to reduce
			 * queuing overhead.
			 */
			if (page_shortage <= 0)
				vm_page_deactivate(m);
			else {
				/*
				 * Calling vm_page_test_dirty() here would
				 * require acquisition of the object's write
				 * lock.  However, during a page shortage,
				 * directing dirty pages into the laundry
				 * queue is only an optimization and not a
				 * requirement.  Therefore, we simply rely on
				 * the opportunistic updates to the page's
				 * dirty field by the pmap.
				 */
				if (m->dirty == 0) {
					vm_page_deactivate(m);
					page_shortage -=
					    act_scan_laundry_weight;
				} else {
					vm_page_launder(m);
					page_shortage--;
				}
			}
		}
	}
	if (mtx != NULL) {
		mtx_unlock(mtx);
		mtx = NULL;
	}
	vm_pagequeue_lock(pq);
	TAILQ_REMOVE(&pq->pq_pl, &vmd->vmd_clock[0], plinks.q);
	TAILQ_INSERT_AFTER(&pq->pq_pl, marker, &vmd->vmd_clock[0], plinks.q);
	vm_pageout_end_scan(&ss);
	vm_pagequeue_unlock(pq);
}

static int
vm_pageout_reinsert_inactive_page(struct scan_state *ss, vm_page_t m)
{
	struct vm_domain *vmd;

	if (m->queue != PQ_INACTIVE || (m->aflags & PGA_ENQUEUED) != 0)
		return (0);
	vm_page_aflag_set(m, PGA_ENQUEUED);
	if ((m->aflags & PGA_REQUEUE_HEAD) != 0) {
		vmd = vm_pagequeue_domain(m);
		TAILQ_INSERT_BEFORE(&vmd->vmd_inacthead, m, plinks.q);
		vm_page_aflag_clear(m, PGA_REQUEUE | PGA_REQUEUE_HEAD);
	} else if ((m->aflags & PGA_REQUEUE) != 0) {
		TAILQ_INSERT_TAIL(&ss->pq->pq_pl, m, plinks.q);
		vm_page_aflag_clear(m, PGA_REQUEUE | PGA_REQUEUE_HEAD);
	} else
		TAILQ_INSERT_BEFORE(ss->marker, m, plinks.q);
	return (1);
}

/*
 * Re-add stuck pages to the inactive queue.  We will examine them again
 * during the next scan.  If the queue state of a page has changed since
 * it was physically removed from the page queue in
 * vm_pageout_collect_batch(), don't do anything with that page.
 */
static void
vm_pageout_reinsert_inactive(struct scan_state *ss, struct vm_batchqueue *bq,
    vm_page_t m)
{
	struct vm_pagequeue *pq;
	int delta;

	delta = 0;
	pq = ss->pq;

	if (m != NULL) {
		if (vm_batchqueue_insert(bq, m))
			return;
		vm_pagequeue_lock(pq);
		delta += vm_pageout_reinsert_inactive_page(ss, m);
	} else
		vm_pagequeue_lock(pq);
	while ((m = vm_batchqueue_pop(bq)) != NULL)
		delta += vm_pageout_reinsert_inactive_page(ss, m);
	vm_pagequeue_cnt_add(pq, delta);
	vm_pagequeue_unlock(pq);
	vm_batchqueue_init(bq);
}

/*
 * Attempt to reclaim the requested number of pages from the inactive queue.
 * Returns true if the shortage was addressed.
 */
static int
vm_pageout_scan_inactive(struct vm_domain *vmd, int shortage,
    int *addl_shortage)
{
	struct scan_state ss;
	struct vm_batchqueue rq;
	struct mtx *mtx;
	vm_page_t m, marker;
	struct vm_pagequeue *pq;
	vm_object_t object;
	int act_delta, addl_page_shortage, deficit, page_shortage;
	int starting_page_shortage;

	/*
	 * The addl_page_shortage is an estimate of the number of temporarily
	 * stuck pages in the inactive queue.  In other words, the
	 * number of pages from the inactive count that should be
	 * discounted in setting the target for the active queue scan.
	 */
	addl_page_shortage = 0;

	/*
	 * vmd_pageout_deficit counts the number of pages requested in
	 * allocations that failed because of a free page shortage.  We assume
	 * that the allocations will be reattempted and thus include the deficit
	 * in our scan target.
	 */
	deficit = atomic_readandclear_int(&vmd->vmd_pageout_deficit);
	starting_page_shortage = page_shortage = shortage + deficit;

	mtx = NULL;
	object = NULL;
	vm_batchqueue_init(&rq);

	/*
	 * Start scanning the inactive queue for pages that we can free.  The
	 * scan will stop when we reach the target or we have scanned the
	 * entire queue.  (Note that m->act_count is not used to make
	 * decisions for the inactive queue, only for the active queue.)
	 */
	marker = &vmd->vmd_markers[PQ_INACTIVE];
	pq = &vmd->vmd_pagequeues[PQ_INACTIVE];
	vm_pagequeue_lock(pq);
	vm_pageout_init_scan(&ss, pq, marker, NULL, pq->pq_cnt);
	while (page_shortage > 0 && (m = vm_pageout_next(&ss, true)) != NULL) {
		KASSERT((m->flags & PG_MARKER) == 0,
		    ("marker page %p was dequeued", m));

		vm_page_change_lock(m, &mtx);

recheck:
		/*
		 * The page may have been disassociated from the queue
		 * while locks were dropped.
		 */
		if (vm_page_queue(m) != PQ_INACTIVE) {
			addl_page_shortage++;
			continue;
		}

		/*
		 * The page was re-enqueued after the page queue lock was
		 * dropped, or a requeue was requested.  This page gets a second
		 * chance.
		 */
		if ((m->aflags & (PGA_ENQUEUED | PGA_REQUEUE |
		    PGA_REQUEUE_HEAD)) != 0)
			goto reinsert;

		/*
		 * Held pages are essentially stuck in the queue.  So,
		 * they ought to be discounted from the inactive count.
		 * See the description of addl_page_shortage above.
		 *
		 * Wired pages may not be freed.  Complete their removal
		 * from the queue now to avoid needless revisits during
		 * future scans.
		 */
		if (m->hold_count != 0) {
			addl_page_shortage++;
			goto reinsert;
		}
		if (m->wire_count != 0) {
			vm_page_dequeue_deferred(m);
			continue;
		}

		if (object != m->object) {
			if (object != NULL)
				VM_OBJECT_WUNLOCK(object);
			object = m->object;
			if (!VM_OBJECT_TRYWLOCK(object)) {
				mtx_unlock(mtx);
				/* Depends on type-stability. */
				VM_OBJECT_WLOCK(object);
				mtx_lock(mtx);
				goto recheck;
			}
		}

		if (vm_page_busied(m)) {
			/*
			 * Don't mess with busy pages.  Leave them at
			 * the front of the queue.  Most likely, they
			 * are being paged out and will leave the
			 * queue shortly after the scan finishes.  So,
			 * they ought to be discounted from the
			 * inactive count.
			 */
			addl_page_shortage++;
			goto reinsert;
		}

		/*
		 * Invalid pages can be easily freed. They cannot be
		 * mapped, vm_page_free() asserts this.
		 */
		if (m->valid == 0)
			goto free_page;

		/*
		 * If the page has been referenced and the object is not dead,
		 * reactivate or requeue the page depending on whether the
		 * object is mapped.
		 *
		 * Test PGA_REFERENCED after calling pmap_ts_referenced() so
		 * that a reference from a concurrently destroyed mapping is
		 * observed here and now.
		 */
		if (object->ref_count != 0)
			act_delta = pmap_ts_referenced(m);
		else {
			KASSERT(!pmap_page_is_mapped(m),
			    ("page %p is mapped", m));
			act_delta = 0;
		}
		if ((m->aflags & PGA_REFERENCED) != 0) {
			vm_page_aflag_clear(m, PGA_REFERENCED);
			act_delta++;
		}
		if (act_delta != 0) {
			if (object->ref_count != 0) {
				VM_CNT_INC(v_reactivated);
				vm_page_activate(m);

				/*
				 * Increase the activation count if the page
				 * was referenced while in the inactive queue.
				 * This makes it less likely that the page will
				 * be returned prematurely to the inactive
				 * queue.
 				 */
				m->act_count += act_delta + ACT_ADVANCE;
				continue;
			} else if ((object->flags & OBJ_DEAD) == 0) {
				vm_page_aflag_set(m, PGA_REQUEUE);
				goto reinsert;
			}
		}

		/*
		 * If the page appears to be clean at the machine-independent
		 * layer, then remove all of its mappings from the pmap in
		 * anticipation of freeing it.  If, however, any of the page's
		 * mappings allow write access, then the page may still be
		 * modified until the last of those mappings are removed.
		 */
		if (object->ref_count != 0) {
			vm_page_test_dirty(m);
			if (m->dirty == 0)
				pmap_remove_all(m);
		}

		/*
		 * Clean pages can be freed, but dirty pages must be sent back
		 * to the laundry, unless they belong to a dead object.
		 * Requeueing dirty pages from dead objects is pointless, as
		 * they are being paged out and freed by the thread that
		 * destroyed the object.
		 */
		if (m->dirty == 0) {
free_page:
			/*
			 * Because we dequeued the page and have already
			 * checked for concurrent dequeue and enqueue
			 * requests, we can safely disassociate the page
			 * from the inactive queue.
			 */
			KASSERT((m->aflags & PGA_QUEUE_STATE_MASK) == 0,
			    ("page %p has queue state", m));
			m->queue = PQ_NONE;
			vm_page_free(m);
			page_shortage--;
		} else if ((object->flags & OBJ_DEAD) == 0)
			vm_page_launder(m);
		continue;
reinsert:
		vm_pageout_reinsert_inactive(&ss, &rq, m);
	}
	if (mtx != NULL)
		mtx_unlock(mtx);
	if (object != NULL)
		VM_OBJECT_WUNLOCK(object);
	vm_pageout_reinsert_inactive(&ss, &rq, NULL);
	vm_pageout_reinsert_inactive(&ss, &ss.bq, NULL);
	vm_pagequeue_lock(pq);
	vm_pageout_end_scan(&ss);
	vm_pagequeue_unlock(pq);

	VM_CNT_ADD(v_dfree, starting_page_shortage - page_shortage);

	/*
	 * Wake up the laundry thread so that it can perform any needed
	 * laundering.  If we didn't meet our target, we're in shortfall and
	 * need to launder more aggressively.  If PQ_LAUNDRY is empty and no
	 * swap devices are configured, the laundry thread has no work to do, so
	 * don't bother waking it up.
	 *
	 * The laundry thread uses the number of inactive queue scans elapsed
	 * since the last laundering to determine whether to launder again, so
	 * keep count.
	 */
	if (starting_page_shortage > 0) {
		pq = &vmd->vmd_pagequeues[PQ_LAUNDRY];
		vm_pagequeue_lock(pq);
		if (vmd->vmd_laundry_request == VM_LAUNDRY_IDLE &&
		    (pq->pq_cnt > 0 || atomic_load_acq_int(&swapdev_enabled))) {
			if (page_shortage > 0) {
				vmd->vmd_laundry_request = VM_LAUNDRY_SHORTFALL;
				VM_CNT_INC(v_pdshortfalls);
			} else if (vmd->vmd_laundry_request !=
			    VM_LAUNDRY_SHORTFALL)
				vmd->vmd_laundry_request =
				    VM_LAUNDRY_BACKGROUND;
			wakeup(&vmd->vmd_laundry_request);
		}
		vmd->vmd_clean_pages_freed +=
		    starting_page_shortage - page_shortage;
		vm_pagequeue_unlock(pq);
	}

	/*
	 * Wakeup the swapout daemon if we didn't free the targeted number of
	 * pages.
	 */
	if (page_shortage > 0)
		vm_swapout_run();

	/*
	 * If the inactive queue scan fails repeatedly to meet its
	 * target, kill the largest process.
	 */
	vm_pageout_mightbe_oom(vmd, page_shortage, starting_page_shortage);

	/*
	 * Reclaim pages by swapping out idle processes, if configured to do so.
	 */
	vm_swapout_run_idle();

	/*
	 * See the description of addl_page_shortage above.
	 */
	*addl_shortage = addl_page_shortage + deficit;

	return (page_shortage <= 0);
}

static int vm_pageout_oom_vote;

/*
 * The pagedaemon threads randlomly select one to perform the
 * OOM.  Trying to kill processes before all pagedaemons
 * failed to reach free target is premature.
 */
static void
vm_pageout_mightbe_oom(struct vm_domain *vmd, int page_shortage,
    int starting_page_shortage)
{
	int old_vote;

	if (starting_page_shortage <= 0 || starting_page_shortage !=
	    page_shortage)
		vmd->vmd_oom_seq = 0;
	else
		vmd->vmd_oom_seq++;
	if (vmd->vmd_oom_seq < vm_pageout_oom_seq) {
		if (vmd->vmd_oom) {
			vmd->vmd_oom = FALSE;
			atomic_subtract_int(&vm_pageout_oom_vote, 1);
		}
		return;
	}

	/*
	 * Do not follow the call sequence until OOM condition is
	 * cleared.
	 */
	vmd->vmd_oom_seq = 0;

	if (vmd->vmd_oom)
		return;

	vmd->vmd_oom = TRUE;
	old_vote = atomic_fetchadd_int(&vm_pageout_oom_vote, 1);
	if (old_vote != vm_ndomains - 1)
		return;

	/*
	 * The current pagedaemon thread is the last in the quorum to
	 * start OOM.  Initiate the selection and signaling of the
	 * victim.
	 */
	vm_pageout_oom(VM_OOM_MEM);

	/*
	 * After one round of OOM terror, recall our vote.  On the
	 * next pass, current pagedaemon would vote again if the low
	 * memory condition is still there, due to vmd_oom being
	 * false.
	 */
	vmd->vmd_oom = FALSE;
	atomic_subtract_int(&vm_pageout_oom_vote, 1);
}

/*
 * The OOM killer is the page daemon's action of last resort when
 * memory allocation requests have been stalled for a prolonged period
 * of time because it cannot reclaim memory.  This function computes
 * the approximate number of physical pages that could be reclaimed if
 * the specified address space is destroyed.
 *
 * Private, anonymous memory owned by the address space is the
 * principal resource that we expect to recover after an OOM kill.
 * Since the physical pages mapped by the address space's COW entries
 * are typically shared pages, they are unlikely to be released and so
 * they are not counted.
 *
 * To get to the point where the page daemon runs the OOM killer, its
 * efforts to write-back vnode-backed pages may have stalled.  This
 * could be caused by a memory allocation deadlock in the write path
 * that might be resolved by an OOM kill.  Therefore, physical pages
 * belonging to vnode-backed objects are counted, because they might
 * be freed without being written out first if the address space holds
 * the last reference to an unlinked vnode.
 *
 * Similarly, physical pages belonging to OBJT_PHYS objects are
 * counted because the address space might hold the last reference to
 * the object.
 */
static long
vm_pageout_oom_pagecount(struct vmspace *vmspace)
{
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t obj;
	long res;

	map = &vmspace->vm_map;
	KASSERT(!map->system_map, ("system map"));
	sx_assert(&map->lock, SA_LOCKED);
	res = 0;
	for (entry = map->header.next; entry != &map->header;
	    entry = entry->next) {
		if ((entry->eflags & MAP_ENTRY_IS_SUB_MAP) != 0)
			continue;
		obj = entry->object.vm_object;
		if (obj == NULL)
			continue;
		if ((entry->eflags & MAP_ENTRY_NEEDS_COPY) != 0 &&
		    obj->ref_count != 1)
			continue;
		switch (obj->type) {
		case OBJT_DEFAULT:
		case OBJT_SWAP:
		case OBJT_PHYS:
		case OBJT_VNODE:
			res += obj->resident_page_count;
			break;
		}
	}
	return (res);
}

void
vm_pageout_oom(int shortage)
{
	struct proc *p, *bigproc;
	vm_offset_t size, bigsize;
	struct thread *td;
	struct vmspace *vm;
	bool breakout;

	/*
	 * We keep the process bigproc locked once we find it to keep anyone
	 * from messing with it; however, there is a possibility of
	 * deadlock if process B is bigproc and one of its child processes
	 * attempts to propagate a signal to B while we are waiting for A's
	 * lock while walking this list.  To avoid this, we don't block on
	 * the process lock but just skip a process if it is already locked.
	 */
	bigproc = NULL;
	bigsize = 0;
	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);

		/*
		 * If this is a system, protected or killed process, skip it.
		 */
		if (p->p_state != PRS_NORMAL || (p->p_flag & (P_INEXEC |
		    P_PROTECTED | P_SYSTEM | P_WEXIT)) != 0 ||
		    p->p_pid == 1 || P_KILLED(p) ||
		    (p->p_pid < 48 && swap_pager_avail != 0)) {
			PROC_UNLOCK(p);
			continue;
		}
		/*
		 * If the process is in a non-running type state,
		 * don't touch it.  Check all the threads individually.
		 */
		breakout = false;
		FOREACH_THREAD_IN_PROC(p, td) {
			thread_lock(td);
			if (!TD_ON_RUNQ(td) &&
			    !TD_IS_RUNNING(td) &&
			    !TD_IS_SLEEPING(td) &&
			    !TD_IS_SUSPENDED(td) &&
			    !TD_IS_SWAPPED(td)) {
				thread_unlock(td);
				breakout = true;
				break;
			}
			thread_unlock(td);
		}
		if (breakout) {
			PROC_UNLOCK(p);
			continue;
		}
		/*
		 * get the process size
		 */
		vm = vmspace_acquire_ref(p);
		if (vm == NULL) {
			PROC_UNLOCK(p);
			continue;
		}
		_PHOLD_LITE(p);
		PROC_UNLOCK(p);
		sx_sunlock(&allproc_lock);
		if (!vm_map_trylock_read(&vm->vm_map)) {
			vmspace_free(vm);
			sx_slock(&allproc_lock);
			PRELE(p);
			continue;
		}
		size = vmspace_swap_count(vm);
		if (shortage == VM_OOM_MEM)
			size += vm_pageout_oom_pagecount(vm);
		vm_map_unlock_read(&vm->vm_map);
		vmspace_free(vm);
		sx_slock(&allproc_lock);

		/*
		 * If this process is bigger than the biggest one,
		 * remember it.
		 */
		if (size > bigsize) {
			if (bigproc != NULL)
				PRELE(bigproc);
			bigproc = p;
			bigsize = size;
		} else {
			PRELE(p);
		}
	}
	sx_sunlock(&allproc_lock);
	if (bigproc != NULL) {
		if (vm_panic_on_oom != 0)
			panic("out of swap space");
		PROC_LOCK(bigproc);
		killproc(bigproc, "out of swap space");
		sched_nice(bigproc, PRIO_MIN);
		_PRELE(bigproc);
		PROC_UNLOCK(bigproc);
	}
}

static bool
vm_pageout_lowmem(void)
{
	static int lowmem_ticks = 0;
	int last;

	last = atomic_load_int(&lowmem_ticks);
	while ((u_int)(ticks - last) / hz >= lowmem_period) {
		if (atomic_fcmpset_int(&lowmem_ticks, &last, ticks) == 0)
			continue;

		/*
		 * Decrease registered cache sizes.
		 */
		SDT_PROBE0(vm, , , vm__lowmem_scan);
		EVENTHANDLER_INVOKE(vm_lowmem, VM_LOW_PAGES);

		/*
		 * We do this explicitly after the caches have been
		 * drained above.
		 */
		uma_reclaim();
		return (true);
	}
	return (false);
}

static void
vm_pageout_worker(void *arg)
{
	struct vm_domain *vmd;
	u_int ofree;
	int addl_shortage, domain, shortage;
	bool target_met;

	domain = (uintptr_t)arg;
	vmd = VM_DOMAIN(domain);
	shortage = 0;
	target_met = true;

	/*
	 * XXXKIB It could be useful to bind pageout daemon threads to
	 * the cores belonging to the domain, from which vm_page_array
	 * is allocated.
	 */

	KASSERT(vmd->vmd_segs != 0, ("domain without segments"));
	vmd->vmd_last_active_scan = ticks;

	/*
	 * The pageout daemon worker is never done, so loop forever.
	 */
	while (TRUE) {
		vm_domain_pageout_lock(vmd);

		/*
		 * We need to clear wanted before we check the limits.  This
		 * prevents races with wakers who will check wanted after they
		 * reach the limit.
		 */
		atomic_store_int(&vmd->vmd_pageout_wanted, 0);

		/*
		 * Might the page daemon need to run again?
		 */
		if (vm_paging_needed(vmd, vmd->vmd_free_count)) {
			/*
			 * Yes.  If the scan failed to produce enough free
			 * pages, sleep uninterruptibly for some time in the
			 * hope that the laundry thread will clean some pages.
			 */
			vm_domain_pageout_unlock(vmd);
			if (!target_met)
				pause("pwait", hz / VM_INACT_SCAN_RATE);
		} else {
			/*
			 * No, sleep until the next wakeup or until pages
			 * need to have their reference stats updated.
			 */
			if (mtx_sleep(&vmd->vmd_pageout_wanted,
			    vm_domain_pageout_lockptr(vmd), PDROP | PVM,
			    "psleep", hz / VM_INACT_SCAN_RATE) == 0)
				VM_CNT_INC(v_pdwakeups);
		}

		/* Prevent spurious wakeups by ensuring that wanted is set. */
		atomic_store_int(&vmd->vmd_pageout_wanted, 1);

		/*
		 * Use the controller to calculate how many pages to free in
		 * this interval, and scan the inactive queue.  If the lowmem
		 * handlers appear to have freed up some pages, subtract the
		 * difference from the inactive queue scan target.
		 */
		shortage = pidctrl_daemon(&vmd->vmd_pid, vmd->vmd_free_count);
		if (shortage > 0) {
			ofree = vmd->vmd_free_count;
			if (vm_pageout_lowmem() && vmd->vmd_free_count > ofree)
				shortage -= min(vmd->vmd_free_count - ofree,
				    (u_int)shortage);
			target_met = vm_pageout_scan_inactive(vmd, shortage,
			    &addl_shortage);
		} else
			addl_shortage = 0;

		/*
		 * Scan the active queue.  A positive value for shortage
		 * indicates that we must aggressively deactivate pages to avoid
		 * a shortfall.
		 */
		shortage = vm_pageout_active_target(vmd) + addl_shortage;
		vm_pageout_scan_active(vmd, shortage);
	}
}

/*
 *	vm_pageout_init initialises basic pageout daemon settings.
 */
static void
vm_pageout_init_domain(int domain)
{
	struct vm_domain *vmd;
	struct sysctl_oid *oid;

	vmd = VM_DOMAIN(domain);
	vmd->vmd_interrupt_free_min = 2;

	/*
	 * v_free_reserved needs to include enough for the largest
	 * swap pager structures plus enough for any pv_entry structs
	 * when paging. 
	 */
	if (vmd->vmd_page_count > 1024)
		vmd->vmd_free_min = 4 + (vmd->vmd_page_count - 1024) / 200;
	else
		vmd->vmd_free_min = 4;
	vmd->vmd_pageout_free_min = (2*MAXBSIZE)/PAGE_SIZE +
	    vmd->vmd_interrupt_free_min;
	vmd->vmd_free_reserved = vm_pageout_page_count +
	    vmd->vmd_pageout_free_min + (vmd->vmd_page_count / 768);
	vmd->vmd_free_severe = vmd->vmd_free_min / 2;
	vmd->vmd_free_target = 4 * vmd->vmd_free_min + vmd->vmd_free_reserved;
	vmd->vmd_free_min += vmd->vmd_free_reserved;
	vmd->vmd_free_severe += vmd->vmd_free_reserved;
	vmd->vmd_inactive_target = (3 * vmd->vmd_free_target) / 2;
	if (vmd->vmd_inactive_target > vmd->vmd_free_count / 3)
		vmd->vmd_inactive_target = vmd->vmd_free_count / 3;

	/*
	 * Set the default wakeup threshold to be 10% below the paging
	 * target.  This keeps the steady state out of shortfall.
	 */
	vmd->vmd_pageout_wakeup_thresh = (vmd->vmd_free_target / 10) * 9;

	/*
	 * Target amount of memory to move out of the laundry queue during a
	 * background laundering.  This is proportional to the amount of system
	 * memory.
	 */
	vmd->vmd_background_launder_target = (vmd->vmd_free_target -
	    vmd->vmd_free_min) / 10;

	/* Initialize the pageout daemon pid controller. */
	pidctrl_init(&vmd->vmd_pid, hz / VM_INACT_SCAN_RATE,
	    vmd->vmd_free_target, PIDCTRL_BOUND,
	    PIDCTRL_KPD, PIDCTRL_KID, PIDCTRL_KDD);
	oid = SYSCTL_ADD_NODE(NULL, SYSCTL_CHILDREN(vmd->vmd_oid), OID_AUTO,
	    "pidctrl", CTLFLAG_RD, NULL, "");
	pidctrl_init_sysctl(&vmd->vmd_pid, SYSCTL_CHILDREN(oid));
}

static void
vm_pageout_init(void)
{
	u_int freecount;
	int i;

	/*
	 * Initialize some paging parameters.
	 */
	if (vm_cnt.v_page_count < 2000)
		vm_pageout_page_count = 8;

	freecount = 0;
	for (i = 0; i < vm_ndomains; i++) {
		struct vm_domain *vmd;

		vm_pageout_init_domain(i);
		vmd = VM_DOMAIN(i);
		vm_cnt.v_free_reserved += vmd->vmd_free_reserved;
		vm_cnt.v_free_target += vmd->vmd_free_target;
		vm_cnt.v_free_min += vmd->vmd_free_min;
		vm_cnt.v_inactive_target += vmd->vmd_inactive_target;
		vm_cnt.v_pageout_free_min += vmd->vmd_pageout_free_min;
		vm_cnt.v_interrupt_free_min += vmd->vmd_interrupt_free_min;
		vm_cnt.v_free_severe += vmd->vmd_free_severe;
		freecount += vmd->vmd_free_count;
	}

	/*
	 * Set interval in seconds for active scan.  We want to visit each
	 * page at least once every ten minutes.  This is to prevent worst
	 * case paging behaviors with stale active LRU.
	 */
	if (vm_pageout_update_period == 0)
		vm_pageout_update_period = 600;

	if (vm_page_max_wired == 0)
		vm_page_max_wired = freecount / 3;
}

/*
 *     vm_pageout is the high level pageout daemon.
 */
static void
vm_pageout(void)
{
	struct proc *p;
	struct thread *td;
	int error, first, i;

	p = curproc;
	td = curthread;

	swap_pager_swap_init();
	for (first = -1, i = 0; i < vm_ndomains; i++) {
		if (VM_DOMAIN_EMPTY(i)) {
			if (bootverbose)
				printf("domain %d empty; skipping pageout\n",
				    i);
			continue;
		}
		if (first == -1)
			first = i;
		else {
			error = kthread_add(vm_pageout_worker,
			    (void *)(uintptr_t)i, p, NULL, 0, 0, "dom%d", i);
			if (error != 0)
				panic("starting pageout for domain %d: %d\n",
				    i, error);
		}
		error = kthread_add(vm_pageout_laundry_worker,
		    (void *)(uintptr_t)i, p, NULL, 0, 0, "laundry: dom%d", i);
		if (error != 0)
			panic("starting laundry for domain %d: %d", i, error);
	}
	error = kthread_add(uma_reclaim_worker, NULL, p, NULL, 0, 0, "uma");
	if (error != 0)
		panic("starting uma_reclaim helper, error %d\n", error);

	snprintf(td->td_name, sizeof(td->td_name), "dom%d", first);
	vm_pageout_worker((void *)(uintptr_t)first);
}

/*
 * Perform an advisory wakeup of the page daemon.
 */
void
pagedaemon_wakeup(int domain)
{
	struct vm_domain *vmd;

	vmd = VM_DOMAIN(domain);
	vm_domain_pageout_assert_unlocked(vmd);
	if (curproc == pageproc)
		return;

	if (atomic_fetchadd_int(&vmd->vmd_pageout_wanted, 1) == 0) {
		vm_domain_pageout_lock(vmd);
		atomic_store_int(&vmd->vmd_pageout_wanted, 1);
		wakeup(&vmd->vmd_pageout_wanted);
		vm_domain_pageout_unlock(vmd);
	}
}
