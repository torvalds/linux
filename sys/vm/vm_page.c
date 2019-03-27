/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1998 Matthew Dillon.  All Rights Reserved.
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
 *	from: @(#)vm_page.c	7.4 (Berkeley) 5/7/91
 */

/*-
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
 *			GENERAL RULES ON VM_PAGE MANIPULATION
 *
 *	- A page queue lock is required when adding or removing a page from a
 *	  page queue regardless of other locks or the busy state of a page.
 *
 *		* In general, no thread besides the page daemon can acquire or
 *		  hold more than one page queue lock at a time.
 *
 *		* The page daemon can acquire and hold any pair of page queue
 *		  locks in any order.
 *
 *	- The object lock is required when inserting or removing
 *	  pages from an object (vm_page_insert() or vm_page_remove()).
 *
 */

/*
 *	Resident memory management module.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/domainset.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>
#include <vm/vm_domainset.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>
#include <vm/vm_pager.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <machine/md_var.h>

extern int	uma_startup_count(int);
extern void	uma_startup(void *, int);
extern int	vmem_startup_count(void);

struct vm_domain vm_dom[MAXMEMDOM];

DPCPU_DEFINE_STATIC(struct vm_batchqueue, pqbatch[MAXMEMDOM][PQ_COUNT]);

struct mtx_padalign __exclusive_cache_line pa_lock[PA_LOCK_COUNT];

struct mtx_padalign __exclusive_cache_line vm_domainset_lock;
/* The following fields are protected by the domainset lock. */
domainset_t __exclusive_cache_line vm_min_domains;
domainset_t __exclusive_cache_line vm_severe_domains;
static int vm_min_waiters;
static int vm_severe_waiters;
static int vm_pageproc_waiters;

/*
 * bogus page -- for I/O to/from partially complete buffers,
 * or for paging into sparsely invalid regions.
 */
vm_page_t bogus_page;

vm_page_t vm_page_array;
long vm_page_array_size;
long first_page;

static int boot_pages;
SYSCTL_INT(_vm, OID_AUTO, boot_pages, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &boot_pages, 0,
    "number of pages allocated for bootstrapping the VM system");

static int pa_tryrelock_restart;
SYSCTL_INT(_vm, OID_AUTO, tryrelock_restart, CTLFLAG_RD,
    &pa_tryrelock_restart, 0, "Number of tryrelock restarts");

static TAILQ_HEAD(, vm_page) blacklist_head;
static int sysctl_vm_page_blacklist(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, page_blacklist, CTLTYPE_STRING | CTLFLAG_RD |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_vm_page_blacklist, "A", "Blacklist pages");

static uma_zone_t fakepg_zone;

static void vm_page_alloc_check(vm_page_t m);
static void vm_page_clear_dirty_mask(vm_page_t m, vm_page_bits_t pagebits);
static void vm_page_dequeue_complete(vm_page_t m);
static void vm_page_enqueue(vm_page_t m, uint8_t queue);
static void vm_page_init(void *dummy);
static int vm_page_insert_after(vm_page_t m, vm_object_t object,
    vm_pindex_t pindex, vm_page_t mpred);
static void vm_page_insert_radixdone(vm_page_t m, vm_object_t object,
    vm_page_t mpred);
static int vm_page_reclaim_run(int req_class, int domain, u_long npages,
    vm_page_t m_run, vm_paddr_t high);
static int vm_domain_alloc_fail(struct vm_domain *vmd, vm_object_t object,
    int req);
static int vm_page_import(void *arg, void **store, int cnt, int domain,
    int flags);
static void vm_page_release(void *arg, void **store, int cnt);

SYSINIT(vm_page, SI_SUB_VM, SI_ORDER_SECOND, vm_page_init, NULL);

static void
vm_page_init(void *dummy)
{

	fakepg_zone = uma_zcreate("fakepg", sizeof(struct vm_page), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_PTR, UMA_ZONE_NOFREE | UMA_ZONE_VM);
	bogus_page = vm_page_alloc(NULL, 0, VM_ALLOC_NOOBJ |
	    VM_ALLOC_NORMAL | VM_ALLOC_WIRED);
}

/*
 * The cache page zone is initialized later since we need to be able to allocate
 * pages before UMA is fully initialized.
 */
static void
vm_page_init_cache_zones(void *dummy __unused)
{
	struct vm_domain *vmd;
	int i;

	for (i = 0; i < vm_ndomains; i++) {
		vmd = VM_DOMAIN(i);
		/*
		 * Don't allow the page cache to take up more than .25% of
		 * memory.
		 */
		if (vmd->vmd_page_count / 400 < 256 * mp_ncpus)
			continue;
		vmd->vmd_pgcache = uma_zcache_create("vm pgcache",
		    sizeof(struct vm_page), NULL, NULL, NULL, NULL,
		    vm_page_import, vm_page_release, vmd,
		    UMA_ZONE_MAXBUCKET | UMA_ZONE_VM);
		(void )uma_zone_set_maxcache(vmd->vmd_pgcache, 0);
	}
}
SYSINIT(vm_page2, SI_SUB_VM_CONF, SI_ORDER_ANY, vm_page_init_cache_zones, NULL);

/* Make sure that u_long is at least 64 bits when PAGE_SIZE is 32K. */
#if PAGE_SIZE == 32768
#ifdef CTASSERT
CTASSERT(sizeof(u_long) >= 8);
#endif
#endif

/*
 * Try to acquire a physical address lock while a pmap is locked.  If we
 * fail to trylock we unlock and lock the pmap directly and cache the
 * locked pa in *locked.  The caller should then restart their loop in case
 * the virtual to physical mapping has changed.
 */
int
vm_page_pa_tryrelock(pmap_t pmap, vm_paddr_t pa, vm_paddr_t *locked)
{
	vm_paddr_t lockpa;

	lockpa = *locked;
	*locked = pa;
	if (lockpa) {
		PA_LOCK_ASSERT(lockpa, MA_OWNED);
		if (PA_LOCKPTR(pa) == PA_LOCKPTR(lockpa))
			return (0);
		PA_UNLOCK(lockpa);
	}
	if (PA_TRYLOCK(pa))
		return (0);
	PMAP_UNLOCK(pmap);
	atomic_add_int(&pa_tryrelock_restart, 1);
	PA_LOCK(pa);
	PMAP_LOCK(pmap);
	return (EAGAIN);
}

/*
 *	vm_set_page_size:
 *
 *	Sets the page size, perhaps based upon the memory
 *	size.  Must be called before any use of page-size
 *	dependent functions.
 */
void
vm_set_page_size(void)
{
	if (vm_cnt.v_page_size == 0)
		vm_cnt.v_page_size = PAGE_SIZE;
	if (((vm_cnt.v_page_size - 1) & vm_cnt.v_page_size) != 0)
		panic("vm_set_page_size: page size not a power of two");
}

/*
 *	vm_page_blacklist_next:
 *
 *	Find the next entry in the provided string of blacklist
 *	addresses.  Entries are separated by space, comma, or newline.
 *	If an invalid integer is encountered then the rest of the
 *	string is skipped.  Updates the list pointer to the next
 *	character, or NULL if the string is exhausted or invalid.
 */
static vm_paddr_t
vm_page_blacklist_next(char **list, char *end)
{
	vm_paddr_t bad;
	char *cp, *pos;

	if (list == NULL || *list == NULL)
		return (0);
	if (**list =='\0') {
		*list = NULL;
		return (0);
	}

	/*
	 * If there's no end pointer then the buffer is coming from
	 * the kenv and we know it's null-terminated.
	 */
	if (end == NULL)
		end = *list + strlen(*list);

	/* Ensure that strtoq() won't walk off the end */
	if (*end != '\0') {
		if (*end == '\n' || *end == ' ' || *end  == ',')
			*end = '\0';
		else {
			printf("Blacklist not terminated, skipping\n");
			*list = NULL;
			return (0);
		}
	}

	for (pos = *list; *pos != '\0'; pos = cp) {
		bad = strtoq(pos, &cp, 0);
		if (*cp == '\0' || *cp == ' ' || *cp == ',' || *cp == '\n') {
			if (bad == 0) {
				if (++cp < end)
					continue;
				else
					break;
			}
		} else
			break;
		if (*cp == '\0' || ++cp >= end)
			*list = NULL;
		else
			*list = cp;
		return (trunc_page(bad));
	}
	printf("Garbage in RAM blacklist, skipping\n");
	*list = NULL;
	return (0);
}

bool
vm_page_blacklist_add(vm_paddr_t pa, bool verbose)
{
	struct vm_domain *vmd;
	vm_page_t m;
	int ret;

	m = vm_phys_paddr_to_vm_page(pa);
	if (m == NULL)
		return (true); /* page does not exist, no failure */

	vmd = vm_pagequeue_domain(m);
	vm_domain_free_lock(vmd);
	ret = vm_phys_unfree_page(m);
	vm_domain_free_unlock(vmd);
	if (ret != 0) {
		vm_domain_freecnt_inc(vmd, -1);
		TAILQ_INSERT_TAIL(&blacklist_head, m, listq);
		if (verbose)
			printf("Skipping page with pa 0x%jx\n", (uintmax_t)pa);
	}
	return (ret);
}

/*
 *	vm_page_blacklist_check:
 *
 *	Iterate through the provided string of blacklist addresses, pulling
 *	each entry out of the physical allocator free list and putting it
 *	onto a list for reporting via the vm.page_blacklist sysctl.
 */
static void
vm_page_blacklist_check(char *list, char *end)
{
	vm_paddr_t pa;
	char *next;

	next = list;
	while (next != NULL) {
		if ((pa = vm_page_blacklist_next(&next, end)) == 0)
			continue;
		vm_page_blacklist_add(pa, bootverbose);
	}
}

/*
 *	vm_page_blacklist_load:
 *
 *	Search for a special module named "ram_blacklist".  It'll be a
 *	plain text file provided by the user via the loader directive
 *	of the same name.
 */
static void
vm_page_blacklist_load(char **list, char **end)
{
	void *mod;
	u_char *ptr;
	u_int len;

	mod = NULL;
	ptr = NULL;

	mod = preload_search_by_type("ram_blacklist");
	if (mod != NULL) {
		ptr = preload_fetch_addr(mod);
		len = preload_fetch_size(mod);
        }
	*list = ptr;
	if (ptr != NULL)
		*end = ptr + len;
	else
		*end = NULL;
	return;
}

static int
sysctl_vm_page_blacklist(SYSCTL_HANDLER_ARGS)
{
	vm_page_t m;
	struct sbuf sbuf;
	int error, first;

	first = 1;
	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	TAILQ_FOREACH(m, &blacklist_head, listq) {
		sbuf_printf(&sbuf, "%s%#jx", first ? "" : ",",
		    (uintmax_t)m->phys_addr);
		first = 0;
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

/*
 * Initialize a dummy page for use in scans of the specified paging queue.
 * In principle, this function only needs to set the flag PG_MARKER.
 * Nonetheless, it write busies and initializes the hold count to one as
 * safety precautions.
 */
static void
vm_page_init_marker(vm_page_t marker, int queue, uint8_t aflags)
{

	bzero(marker, sizeof(*marker));
	marker->flags = PG_MARKER;
	marker->aflags = aflags;
	marker->busy_lock = VPB_SINGLE_EXCLUSIVER;
	marker->queue = queue;
	marker->hold_count = 1;
}

static void
vm_page_domain_init(int domain)
{
	struct vm_domain *vmd;
	struct vm_pagequeue *pq;
	int i;

	vmd = VM_DOMAIN(domain);
	bzero(vmd, sizeof(*vmd));
	*__DECONST(char **, &vmd->vmd_pagequeues[PQ_INACTIVE].pq_name) =
	    "vm inactive pagequeue";
	*__DECONST(char **, &vmd->vmd_pagequeues[PQ_ACTIVE].pq_name) =
	    "vm active pagequeue";
	*__DECONST(char **, &vmd->vmd_pagequeues[PQ_LAUNDRY].pq_name) =
	    "vm laundry pagequeue";
	*__DECONST(char **, &vmd->vmd_pagequeues[PQ_UNSWAPPABLE].pq_name) =
	    "vm unswappable pagequeue";
	vmd->vmd_domain = domain;
	vmd->vmd_page_count = 0;
	vmd->vmd_free_count = 0;
	vmd->vmd_segs = 0;
	vmd->vmd_oom = FALSE;
	for (i = 0; i < PQ_COUNT; i++) {
		pq = &vmd->vmd_pagequeues[i];
		TAILQ_INIT(&pq->pq_pl);
		mtx_init(&pq->pq_mutex, pq->pq_name, "vm pagequeue",
		    MTX_DEF | MTX_DUPOK);
		pq->pq_pdpages = 0;
		vm_page_init_marker(&vmd->vmd_markers[i], i, 0);
	}
	mtx_init(&vmd->vmd_free_mtx, "vm page free queue", NULL, MTX_DEF);
	mtx_init(&vmd->vmd_pageout_mtx, "vm pageout lock", NULL, MTX_DEF);
	snprintf(vmd->vmd_name, sizeof(vmd->vmd_name), "%d", domain);

	/*
	 * inacthead is used to provide FIFO ordering for LRU-bypassing
	 * insertions.
	 */
	vm_page_init_marker(&vmd->vmd_inacthead, PQ_INACTIVE, PGA_ENQUEUED);
	TAILQ_INSERT_HEAD(&vmd->vmd_pagequeues[PQ_INACTIVE].pq_pl,
	    &vmd->vmd_inacthead, plinks.q);

	/*
	 * The clock pages are used to implement active queue scanning without
	 * requeues.  Scans start at clock[0], which is advanced after the scan
	 * ends.  When the two clock hands meet, they are reset and scanning
	 * resumes from the head of the queue.
	 */
	vm_page_init_marker(&vmd->vmd_clock[0], PQ_ACTIVE, PGA_ENQUEUED);
	vm_page_init_marker(&vmd->vmd_clock[1], PQ_ACTIVE, PGA_ENQUEUED);
	TAILQ_INSERT_HEAD(&vmd->vmd_pagequeues[PQ_ACTIVE].pq_pl,
	    &vmd->vmd_clock[0], plinks.q);
	TAILQ_INSERT_TAIL(&vmd->vmd_pagequeues[PQ_ACTIVE].pq_pl,
	    &vmd->vmd_clock[1], plinks.q);
}

/*
 * Initialize a physical page in preparation for adding it to the free
 * lists.
 */
static void
vm_page_init_page(vm_page_t m, vm_paddr_t pa, int segind)
{

	m->object = NULL;
	m->wire_count = 0;
	m->busy_lock = VPB_UNBUSIED;
	m->hold_count = 0;
	m->flags = m->aflags = 0;
	m->phys_addr = pa;
	m->queue = PQ_NONE;
	m->psind = 0;
	m->segind = segind;
	m->order = VM_NFREEORDER;
	m->pool = VM_FREEPOOL_DEFAULT;
	m->valid = m->dirty = 0;
	pmap_page_init(m);
}

/*
 *	vm_page_startup:
 *
 *	Initializes the resident memory module.  Allocates physical memory for
 *	bootstrapping UMA and some data structures that are used to manage
 *	physical pages.  Initializes these structures, and populates the free
 *	page queues.
 */
vm_offset_t
vm_page_startup(vm_offset_t vaddr)
{
	struct vm_phys_seg *seg;
	vm_page_t m;
	char *list, *listend;
	vm_offset_t mapped;
	vm_paddr_t end, high_avail, low_avail, new_end, page_range, size;
	vm_paddr_t biggestsize, last_pa, pa;
	u_long pagecount;
	int biggestone, i, segind;
#ifdef WITNESS
	int witness_size;
#endif
#if defined(__i386__) && defined(VM_PHYSSEG_DENSE)
	long ii;
#endif

	biggestsize = 0;
	biggestone = 0;
	vaddr = round_page(vaddr);

	for (i = 0; phys_avail[i + 1]; i += 2) {
		phys_avail[i] = round_page(phys_avail[i]);
		phys_avail[i + 1] = trunc_page(phys_avail[i + 1]);
	}
	for (i = 0; phys_avail[i + 1]; i += 2) {
		size = phys_avail[i + 1] - phys_avail[i];
		if (size > biggestsize) {
			biggestone = i;
			biggestsize = size;
		}
	}

	end = phys_avail[biggestone+1];

	/*
	 * Initialize the page and queue locks.
	 */
	mtx_init(&vm_domainset_lock, "vm domainset lock", NULL, MTX_DEF);
	for (i = 0; i < PA_LOCK_COUNT; i++)
		mtx_init(&pa_lock[i], "vm page", NULL, MTX_DEF);
	for (i = 0; i < vm_ndomains; i++)
		vm_page_domain_init(i);

	/*
	 * Allocate memory for use when boot strapping the kernel memory
	 * allocator.  Tell UMA how many zones we are going to create
	 * before going fully functional.  UMA will add its zones.
	 *
	 * VM startup zones: vmem, vmem_btag, VM OBJECT, RADIX NODE, MAP,
	 * KMAP ENTRY, MAP ENTRY, VMSPACE.
	 */
	boot_pages = uma_startup_count(8);

#ifndef UMA_MD_SMALL_ALLOC
	/* vmem_startup() calls uma_prealloc(). */
	boot_pages += vmem_startup_count();
	/* vm_map_startup() calls uma_prealloc(). */
	boot_pages += howmany(MAX_KMAP,
	    UMA_SLAB_SPACE / sizeof(struct vm_map));

	/*
	 * Before going fully functional kmem_init() does allocation
	 * from "KMAP ENTRY" and vmem_create() does allocation from "vmem".
	 */
	boot_pages += 2;
#endif
	/*
	 * CTFLAG_RDTUN doesn't work during the early boot process, so we must
	 * manually fetch the value.
	 */
	TUNABLE_INT_FETCH("vm.boot_pages", &boot_pages);
	new_end = end - (boot_pages * UMA_SLAB_SIZE);
	new_end = trunc_page(new_end);
	mapped = pmap_map(&vaddr, new_end, end,
	    VM_PROT_READ | VM_PROT_WRITE);
	bzero((void *)mapped, end - new_end);
	uma_startup((void *)mapped, boot_pages);

#ifdef WITNESS
	witness_size = round_page(witness_startup_count());
	new_end -= witness_size;
	mapped = pmap_map(&vaddr, new_end, new_end + witness_size,
	    VM_PROT_READ | VM_PROT_WRITE);
	bzero((void *)mapped, witness_size);
	witness_startup((void *)mapped);
#endif

#if defined(__aarch64__) || defined(__amd64__) || defined(__arm__) || \
    defined(__i386__) || defined(__mips__) || defined(__riscv)
	/*
	 * Allocate a bitmap to indicate that a random physical page
	 * needs to be included in a minidump.
	 *
	 * The amd64 port needs this to indicate which direct map pages
	 * need to be dumped, via calls to dump_add_page()/dump_drop_page().
	 *
	 * However, i386 still needs this workspace internally within the
	 * minidump code.  In theory, they are not needed on i386, but are
	 * included should the sf_buf code decide to use them.
	 */
	last_pa = 0;
	for (i = 0; dump_avail[i + 1] != 0; i += 2)
		if (dump_avail[i + 1] > last_pa)
			last_pa = dump_avail[i + 1];
	page_range = last_pa / PAGE_SIZE;
	vm_page_dump_size = round_page(roundup2(page_range, NBBY) / NBBY);
	new_end -= vm_page_dump_size;
	vm_page_dump = (void *)(uintptr_t)pmap_map(&vaddr, new_end,
	    new_end + vm_page_dump_size, VM_PROT_READ | VM_PROT_WRITE);
	bzero((void *)vm_page_dump, vm_page_dump_size);
#else
	(void)last_pa;
#endif
#if defined(__aarch64__) || defined(__amd64__) || defined(__mips__) || \
    defined(__riscv)
	/*
	 * Include the UMA bootstrap pages, witness pages and vm_page_dump
	 * in a crash dump.  When pmap_map() uses the direct map, they are
	 * not automatically included.
	 */
	for (pa = new_end; pa < end; pa += PAGE_SIZE)
		dump_add_page(pa);
#endif
	phys_avail[biggestone + 1] = new_end;
#ifdef __amd64__
	/*
	 * Request that the physical pages underlying the message buffer be
	 * included in a crash dump.  Since the message buffer is accessed
	 * through the direct map, they are not automatically included.
	 */
	pa = DMAP_TO_PHYS((vm_offset_t)msgbufp->msg_ptr);
	last_pa = pa + round_page(msgbufsize);
	while (pa < last_pa) {
		dump_add_page(pa);
		pa += PAGE_SIZE;
	}
#endif
	/*
	 * Compute the number of pages of memory that will be available for
	 * use, taking into account the overhead of a page structure per page.
	 * In other words, solve
	 *	"available physical memory" - round_page(page_range *
	 *	    sizeof(struct vm_page)) = page_range * PAGE_SIZE 
	 * for page_range.  
	 */
	low_avail = phys_avail[0];
	high_avail = phys_avail[1];
	for (i = 0; i < vm_phys_nsegs; i++) {
		if (vm_phys_segs[i].start < low_avail)
			low_avail = vm_phys_segs[i].start;
		if (vm_phys_segs[i].end > high_avail)
			high_avail = vm_phys_segs[i].end;
	}
	/* Skip the first chunk.  It is already accounted for. */
	for (i = 2; phys_avail[i + 1] != 0; i += 2) {
		if (phys_avail[i] < low_avail)
			low_avail = phys_avail[i];
		if (phys_avail[i + 1] > high_avail)
			high_avail = phys_avail[i + 1];
	}
	first_page = low_avail / PAGE_SIZE;
#ifdef VM_PHYSSEG_SPARSE
	size = 0;
	for (i = 0; i < vm_phys_nsegs; i++)
		size += vm_phys_segs[i].end - vm_phys_segs[i].start;
	for (i = 0; phys_avail[i + 1] != 0; i += 2)
		size += phys_avail[i + 1] - phys_avail[i];
#elif defined(VM_PHYSSEG_DENSE)
	size = high_avail - low_avail;
#else
#error "Either VM_PHYSSEG_DENSE or VM_PHYSSEG_SPARSE must be defined."
#endif

#ifdef VM_PHYSSEG_DENSE
	/*
	 * In the VM_PHYSSEG_DENSE case, the number of pages can account for
	 * the overhead of a page structure per page only if vm_page_array is
	 * allocated from the last physical memory chunk.  Otherwise, we must
	 * allocate page structures representing the physical memory
	 * underlying vm_page_array, even though they will not be used.
	 */
	if (new_end != high_avail)
		page_range = size / PAGE_SIZE;
	else
#endif
	{
		page_range = size / (PAGE_SIZE + sizeof(struct vm_page));

		/*
		 * If the partial bytes remaining are large enough for
		 * a page (PAGE_SIZE) without a corresponding
		 * 'struct vm_page', then new_end will contain an
		 * extra page after subtracting the length of the VM
		 * page array.  Compensate by subtracting an extra
		 * page from new_end.
		 */
		if (size % (PAGE_SIZE + sizeof(struct vm_page)) >= PAGE_SIZE) {
			if (new_end == high_avail)
				high_avail -= PAGE_SIZE;
			new_end -= PAGE_SIZE;
		}
	}
	end = new_end;

	/*
	 * Reserve an unmapped guard page to trap access to vm_page_array[-1].
	 * However, because this page is allocated from KVM, out-of-bounds
	 * accesses using the direct map will not be trapped.
	 */
	vaddr += PAGE_SIZE;

	/*
	 * Allocate physical memory for the page structures, and map it.
	 */
	new_end = trunc_page(end - page_range * sizeof(struct vm_page));
	mapped = pmap_map(&vaddr, new_end, end,
	    VM_PROT_READ | VM_PROT_WRITE);
	vm_page_array = (vm_page_t)mapped;
	vm_page_array_size = page_range;

#if VM_NRESERVLEVEL > 0
	/*
	 * Allocate physical memory for the reservation management system's
	 * data structures, and map it.
	 */
	if (high_avail == end)
		high_avail = new_end;
	new_end = vm_reserv_startup(&vaddr, new_end, high_avail);
#endif
#if defined(__aarch64__) || defined(__amd64__) || defined(__mips__) || \
    defined(__riscv)
	/*
	 * Include vm_page_array and vm_reserv_array in a crash dump.
	 */
	for (pa = new_end; pa < end; pa += PAGE_SIZE)
		dump_add_page(pa);
#endif
	phys_avail[biggestone + 1] = new_end;

	/*
	 * Add physical memory segments corresponding to the available
	 * physical pages.
	 */
	for (i = 0; phys_avail[i + 1] != 0; i += 2)
		vm_phys_add_seg(phys_avail[i], phys_avail[i + 1]);

	/*
	 * Initialize the physical memory allocator.
	 */
	vm_phys_init();

	/*
	 * Initialize the page structures and add every available page to the
	 * physical memory allocator's free lists.
	 */
#if defined(__i386__) && defined(VM_PHYSSEG_DENSE)
	for (ii = 0; ii < vm_page_array_size; ii++) {
		m = &vm_page_array[ii];
		vm_page_init_page(m, (first_page + ii) << PAGE_SHIFT, 0);
		m->flags = PG_FICTITIOUS;
	}
#endif
	vm_cnt.v_page_count = 0;
	for (segind = 0; segind < vm_phys_nsegs; segind++) {
		seg = &vm_phys_segs[segind];
		for (m = seg->first_page, pa = seg->start; pa < seg->end;
		    m++, pa += PAGE_SIZE)
			vm_page_init_page(m, pa, segind);

		/*
		 * Add the segment to the free lists only if it is covered by
		 * one of the ranges in phys_avail.  Because we've added the
		 * ranges to the vm_phys_segs array, we can assume that each
		 * segment is either entirely contained in one of the ranges,
		 * or doesn't overlap any of them.
		 */
		for (i = 0; phys_avail[i + 1] != 0; i += 2) {
			struct vm_domain *vmd;

			if (seg->start < phys_avail[i] ||
			    seg->end > phys_avail[i + 1])
				continue;

			m = seg->first_page;
			pagecount = (u_long)atop(seg->end - seg->start);

			vmd = VM_DOMAIN(seg->domain);
			vm_domain_free_lock(vmd);
			vm_phys_free_contig(m, pagecount);
			vm_domain_free_unlock(vmd);
			vm_domain_freecnt_inc(vmd, pagecount);
			vm_cnt.v_page_count += (u_int)pagecount;

			vmd = VM_DOMAIN(seg->domain);
			vmd->vmd_page_count += (u_int)pagecount;
			vmd->vmd_segs |= 1UL << m->segind;
			break;
		}
	}

	/*
	 * Remove blacklisted pages from the physical memory allocator.
	 */
	TAILQ_INIT(&blacklist_head);
	vm_page_blacklist_load(&list, &listend);
	vm_page_blacklist_check(list, listend);

	list = kern_getenv("vm.blacklist");
	vm_page_blacklist_check(list, NULL);

	freeenv(list);
#if VM_NRESERVLEVEL > 0
	/*
	 * Initialize the reservation management system.
	 */
	vm_reserv_init();
#endif

	return (vaddr);
}

void
vm_page_reference(vm_page_t m)
{

	vm_page_aflag_set(m, PGA_REFERENCED);
}

/*
 *	vm_page_busy_downgrade:
 *
 *	Downgrade an exclusive busy page into a single shared busy page.
 */
void
vm_page_busy_downgrade(vm_page_t m)
{
	u_int x;
	bool locked;

	vm_page_assert_xbusied(m);
	locked = mtx_owned(vm_page_lockptr(m));

	for (;;) {
		x = m->busy_lock;
		x &= VPB_BIT_WAITERS;
		if (x != 0 && !locked)
			vm_page_lock(m);
		if (atomic_cmpset_rel_int(&m->busy_lock,
		    VPB_SINGLE_EXCLUSIVER | x, VPB_SHARERS_WORD(1)))
			break;
		if (x != 0 && !locked)
			vm_page_unlock(m);
	}
	if (x != 0) {
		wakeup(m);
		if (!locked)
			vm_page_unlock(m);
	}
}

/*
 *	vm_page_sbusied:
 *
 *	Return a positive value if the page is shared busied, 0 otherwise.
 */
int
vm_page_sbusied(vm_page_t m)
{
	u_int x;

	x = m->busy_lock;
	return ((x & VPB_BIT_SHARED) != 0 && x != VPB_UNBUSIED);
}

/*
 *	vm_page_sunbusy:
 *
 *	Shared unbusy a page.
 */
void
vm_page_sunbusy(vm_page_t m)
{
	u_int x;

	vm_page_lock_assert(m, MA_NOTOWNED);
	vm_page_assert_sbusied(m);

	for (;;) {
		x = m->busy_lock;
		if (VPB_SHARERS(x) > 1) {
			if (atomic_cmpset_int(&m->busy_lock, x,
			    x - VPB_ONE_SHARER))
				break;
			continue;
		}
		if ((x & VPB_BIT_WAITERS) == 0) {
			KASSERT(x == VPB_SHARERS_WORD(1),
			    ("vm_page_sunbusy: invalid lock state"));
			if (atomic_cmpset_int(&m->busy_lock,
			    VPB_SHARERS_WORD(1), VPB_UNBUSIED))
				break;
			continue;
		}
		KASSERT(x == (VPB_SHARERS_WORD(1) | VPB_BIT_WAITERS),
		    ("vm_page_sunbusy: invalid lock state for waiters"));

		vm_page_lock(m);
		if (!atomic_cmpset_int(&m->busy_lock, x, VPB_UNBUSIED)) {
			vm_page_unlock(m);
			continue;
		}
		wakeup(m);
		vm_page_unlock(m);
		break;
	}
}

/*
 *	vm_page_busy_sleep:
 *
 *	Sleep and release the page lock, using the page pointer as wchan.
 *	This is used to implement the hard-path of busying mechanism.
 *
 *	The given page must be locked.
 *
 *	If nonshared is true, sleep only if the page is xbusy.
 */
void
vm_page_busy_sleep(vm_page_t m, const char *wmesg, bool nonshared)
{
	u_int x;

	vm_page_assert_locked(m);

	x = m->busy_lock;
	if (x == VPB_UNBUSIED || (nonshared && (x & VPB_BIT_SHARED) != 0) ||
	    ((x & VPB_BIT_WAITERS) == 0 &&
	    !atomic_cmpset_int(&m->busy_lock, x, x | VPB_BIT_WAITERS))) {
		vm_page_unlock(m);
		return;
	}
	msleep(m, vm_page_lockptr(m), PVM | PDROP, wmesg, 0);
}

/*
 *	vm_page_trysbusy:
 *
 *	Try to shared busy a page.
 *	If the operation succeeds 1 is returned otherwise 0.
 *	The operation never sleeps.
 */
int
vm_page_trysbusy(vm_page_t m)
{
	u_int x;

	for (;;) {
		x = m->busy_lock;
		if ((x & VPB_BIT_SHARED) == 0)
			return (0);
		if (atomic_cmpset_acq_int(&m->busy_lock, x, x + VPB_ONE_SHARER))
			return (1);
	}
}

static void
vm_page_xunbusy_locked(vm_page_t m)
{

	vm_page_assert_xbusied(m);
	vm_page_assert_locked(m);

	atomic_store_rel_int(&m->busy_lock, VPB_UNBUSIED);
	/* There is a waiter, do wakeup() instead of vm_page_flash(). */
	wakeup(m);
}

void
vm_page_xunbusy_maybelocked(vm_page_t m)
{
	bool lockacq;

	vm_page_assert_xbusied(m);

	/*
	 * Fast path for unbusy.  If it succeeds, we know that there
	 * are no waiters, so we do not need a wakeup.
	 */
	if (atomic_cmpset_rel_int(&m->busy_lock, VPB_SINGLE_EXCLUSIVER,
	    VPB_UNBUSIED))
		return;

	lockacq = !mtx_owned(vm_page_lockptr(m));
	if (lockacq)
		vm_page_lock(m);
	vm_page_xunbusy_locked(m);
	if (lockacq)
		vm_page_unlock(m);
}

/*
 *	vm_page_xunbusy_hard:
 *
 *	Called after the first try the exclusive unbusy of a page failed.
 *	It is assumed that the waiters bit is on.
 */
void
vm_page_xunbusy_hard(vm_page_t m)
{

	vm_page_assert_xbusied(m);

	vm_page_lock(m);
	vm_page_xunbusy_locked(m);
	vm_page_unlock(m);
}

/*
 *	vm_page_flash:
 *
 *	Wakeup anyone waiting for the page.
 *	The ownership bits do not change.
 *
 *	The given page must be locked.
 */
void
vm_page_flash(vm_page_t m)
{
	u_int x;

	vm_page_lock_assert(m, MA_OWNED);

	for (;;) {
		x = m->busy_lock;
		if ((x & VPB_BIT_WAITERS) == 0)
			return;
		if (atomic_cmpset_int(&m->busy_lock, x,
		    x & (~VPB_BIT_WAITERS)))
			break;
	}
	wakeup(m);
}

/*
 * Avoid releasing and reacquiring the same page lock.
 */
void
vm_page_change_lock(vm_page_t m, struct mtx **mtx)
{
	struct mtx *mtx1;

	mtx1 = vm_page_lockptr(m);
	if (*mtx == mtx1)
		return;
	if (*mtx != NULL)
		mtx_unlock(*mtx);
	*mtx = mtx1;
	mtx_lock(mtx1);
}

/*
 * Keep page from being freed by the page daemon
 * much of the same effect as wiring, except much lower
 * overhead and should be used only for *very* temporary
 * holding ("wiring").
 */
void
vm_page_hold(vm_page_t mem)
{

	vm_page_lock_assert(mem, MA_OWNED);
        mem->hold_count++;
}

void
vm_page_unhold(vm_page_t mem)
{

	vm_page_lock_assert(mem, MA_OWNED);
	KASSERT(mem->hold_count >= 1, ("vm_page_unhold: hold count < 0!!!"));
	--mem->hold_count;
	if (mem->hold_count == 0 && (mem->flags & PG_UNHOLDFREE) != 0)
		vm_page_free_toq(mem);
}

/*
 *	vm_page_unhold_pages:
 *
 *	Unhold each of the pages that is referenced by the given array.
 */
void
vm_page_unhold_pages(vm_page_t *ma, int count)
{
	struct mtx *mtx;

	mtx = NULL;
	for (; count != 0; count--) {
		vm_page_change_lock(*ma, &mtx);
		vm_page_unhold(*ma);
		ma++;
	}
	if (mtx != NULL)
		mtx_unlock(mtx);
}

vm_page_t
PHYS_TO_VM_PAGE(vm_paddr_t pa)
{
	vm_page_t m;

#ifdef VM_PHYSSEG_SPARSE
	m = vm_phys_paddr_to_vm_page(pa);
	if (m == NULL)
		m = vm_phys_fictitious_to_vm_page(pa);
	return (m);
#elif defined(VM_PHYSSEG_DENSE)
	long pi;

	pi = atop(pa);
	if (pi >= first_page && (pi - first_page) < vm_page_array_size) {
		m = &vm_page_array[pi - first_page];
		return (m);
	}
	return (vm_phys_fictitious_to_vm_page(pa));
#else
#error "Either VM_PHYSSEG_DENSE or VM_PHYSSEG_SPARSE must be defined."
#endif
}

/*
 *	vm_page_getfake:
 *
 *	Create a fictitious page with the specified physical address and
 *	memory attribute.  The memory attribute is the only the machine-
 *	dependent aspect of a fictitious page that must be initialized.
 */
vm_page_t
vm_page_getfake(vm_paddr_t paddr, vm_memattr_t memattr)
{
	vm_page_t m;

	m = uma_zalloc(fakepg_zone, M_WAITOK | M_ZERO);
	vm_page_initfake(m, paddr, memattr);
	return (m);
}

void
vm_page_initfake(vm_page_t m, vm_paddr_t paddr, vm_memattr_t memattr)
{

	if ((m->flags & PG_FICTITIOUS) != 0) {
		/*
		 * The page's memattr might have changed since the
		 * previous initialization.  Update the pmap to the
		 * new memattr.
		 */
		goto memattr;
	}
	m->phys_addr = paddr;
	m->queue = PQ_NONE;
	/* Fictitious pages don't use "segind". */
	m->flags = PG_FICTITIOUS;
	/* Fictitious pages don't use "order" or "pool". */
	m->oflags = VPO_UNMANAGED;
	m->busy_lock = VPB_SINGLE_EXCLUSIVER;
	m->wire_count = 1;
	pmap_page_init(m);
memattr:
	pmap_page_set_memattr(m, memattr);
}

/*
 *	vm_page_putfake:
 *
 *	Release a fictitious page.
 */
void
vm_page_putfake(vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) != 0, ("managed %p", m));
	KASSERT((m->flags & PG_FICTITIOUS) != 0,
	    ("vm_page_putfake: bad page %p", m));
	uma_zfree(fakepg_zone, m);
}

/*
 *	vm_page_updatefake:
 *
 *	Update the given fictitious page to the specified physical address and
 *	memory attribute.
 */
void
vm_page_updatefake(vm_page_t m, vm_paddr_t paddr, vm_memattr_t memattr)
{

	KASSERT((m->flags & PG_FICTITIOUS) != 0,
	    ("vm_page_updatefake: bad page %p", m));
	m->phys_addr = paddr;
	pmap_page_set_memattr(m, memattr);
}

/*
 *	vm_page_free:
 *
 *	Free a page.
 */
void
vm_page_free(vm_page_t m)
{

	m->flags &= ~PG_ZERO;
	vm_page_free_toq(m);
}

/*
 *	vm_page_free_zero:
 *
 *	Free a page to the zerod-pages queue
 */
void
vm_page_free_zero(vm_page_t m)
{

	m->flags |= PG_ZERO;
	vm_page_free_toq(m);
}

/*
 * Unbusy and handle the page queueing for a page from a getpages request that
 * was optionally read ahead or behind.
 */
void
vm_page_readahead_finish(vm_page_t m)
{

	/* We shouldn't put invalid pages on queues. */
	KASSERT(m->valid != 0, ("%s: %p is invalid", __func__, m));

	/*
	 * Since the page is not the actually needed one, whether it should
	 * be activated or deactivated is not obvious.  Empirical results
	 * have shown that deactivating the page is usually the best choice,
	 * unless the page is wanted by another thread.
	 */
	vm_page_lock(m);
	if ((m->busy_lock & VPB_BIT_WAITERS) != 0)
		vm_page_activate(m);
	else
		vm_page_deactivate(m);
	vm_page_unlock(m);
	vm_page_xunbusy(m);
}

/*
 *	vm_page_sleep_if_busy:
 *
 *	Sleep and release the page queues lock if the page is busied.
 *	Returns TRUE if the thread slept.
 *
 *	The given page must be unlocked and object containing it must
 *	be locked.
 */
int
vm_page_sleep_if_busy(vm_page_t m, const char *msg)
{
	vm_object_t obj;

	vm_page_lock_assert(m, MA_NOTOWNED);
	VM_OBJECT_ASSERT_WLOCKED(m->object);

	if (vm_page_busied(m)) {
		/*
		 * The page-specific object must be cached because page
		 * identity can change during the sleep, causing the
		 * re-lock of a different object.
		 * It is assumed that a reference to the object is already
		 * held by the callers.
		 */
		obj = m->object;
		vm_page_lock(m);
		VM_OBJECT_WUNLOCK(obj);
		vm_page_busy_sleep(m, msg, false);
		VM_OBJECT_WLOCK(obj);
		return (TRUE);
	}
	return (FALSE);
}

/*
 *	vm_page_dirty_KBI:		[ internal use only ]
 *
 *	Set all bits in the page's dirty field.
 *
 *	The object containing the specified page must be locked if the
 *	call is made from the machine-independent layer.
 *
 *	See vm_page_clear_dirty_mask().
 *
 *	This function should only be called by vm_page_dirty().
 */
void
vm_page_dirty_KBI(vm_page_t m)
{

	/* Refer to this operation by its public name. */
	KASSERT(m->valid == VM_PAGE_BITS_ALL,
	    ("vm_page_dirty: page is invalid!"));
	m->dirty = VM_PAGE_BITS_ALL;
}

/*
 *	vm_page_insert:		[ internal use only ]
 *
 *	Inserts the given mem entry into the object and object list.
 *
 *	The object must be locked.
 */
int
vm_page_insert(vm_page_t m, vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t mpred;

	VM_OBJECT_ASSERT_WLOCKED(object);
	mpred = vm_radix_lookup_le(&object->rtree, pindex);
	return (vm_page_insert_after(m, object, pindex, mpred));
}

/*
 *	vm_page_insert_after:
 *
 *	Inserts the page "m" into the specified object at offset "pindex".
 *
 *	The page "mpred" must immediately precede the offset "pindex" within
 *	the specified object.
 *
 *	The object must be locked.
 */
static int
vm_page_insert_after(vm_page_t m, vm_object_t object, vm_pindex_t pindex,
    vm_page_t mpred)
{
	vm_page_t msucc;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(m->object == NULL,
	    ("vm_page_insert_after: page already inserted"));
	if (mpred != NULL) {
		KASSERT(mpred->object == object,
		    ("vm_page_insert_after: object doesn't contain mpred"));
		KASSERT(mpred->pindex < pindex,
		    ("vm_page_insert_after: mpred doesn't precede pindex"));
		msucc = TAILQ_NEXT(mpred, listq);
	} else
		msucc = TAILQ_FIRST(&object->memq);
	if (msucc != NULL)
		KASSERT(msucc->pindex > pindex,
		    ("vm_page_insert_after: msucc doesn't succeed pindex"));

	/*
	 * Record the object/offset pair in this page
	 */
	m->object = object;
	m->pindex = pindex;

	/*
	 * Now link into the object's ordered list of backed pages.
	 */
	if (vm_radix_insert(&object->rtree, m)) {
		m->object = NULL;
		m->pindex = 0;
		return (1);
	}
	vm_page_insert_radixdone(m, object, mpred);
	return (0);
}

/*
 *	vm_page_insert_radixdone:
 *
 *	Complete page "m" insertion into the specified object after the
 *	radix trie hooking.
 *
 *	The page "mpred" must precede the offset "m->pindex" within the
 *	specified object.
 *
 *	The object must be locked.
 */
static void
vm_page_insert_radixdone(vm_page_t m, vm_object_t object, vm_page_t mpred)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object != NULL && m->object == object,
	    ("vm_page_insert_radixdone: page %p has inconsistent object", m));
	if (mpred != NULL) {
		KASSERT(mpred->object == object,
		    ("vm_page_insert_after: object doesn't contain mpred"));
		KASSERT(mpred->pindex < m->pindex,
		    ("vm_page_insert_after: mpred doesn't precede pindex"));
	}

	if (mpred != NULL)
		TAILQ_INSERT_AFTER(&object->memq, mpred, m, listq);
	else
		TAILQ_INSERT_HEAD(&object->memq, m, listq);

	/*
	 * Show that the object has one more resident page.
	 */
	object->resident_page_count++;

	/*
	 * Hold the vnode until the last page is released.
	 */
	if (object->resident_page_count == 1 && object->type == OBJT_VNODE)
		vhold(object->handle);

	/*
	 * Since we are inserting a new and possibly dirty page,
	 * update the object's OBJ_MIGHTBEDIRTY flag.
	 */
	if (pmap_page_is_write_mapped(m))
		vm_object_set_writeable_dirty(object);
}

/*
 *	vm_page_remove:
 *
 *	Removes the specified page from its containing object, but does not
 *	invalidate any backing storage.
 *
 *	The object must be locked.  The page must be locked if it is managed.
 */
void
vm_page_remove(vm_page_t m)
{
	vm_object_t object;
	vm_page_t mrem;

	if ((m->oflags & VPO_UNMANAGED) == 0)
		vm_page_assert_locked(m);
	if ((object = m->object) == NULL)
		return;
	VM_OBJECT_ASSERT_WLOCKED(object);
	if (vm_page_xbusied(m))
		vm_page_xunbusy_maybelocked(m);
	mrem = vm_radix_remove(&object->rtree, m->pindex);
	KASSERT(mrem == m, ("removed page %p, expected page %p", mrem, m));

	/*
	 * Now remove from the object's list of backed pages.
	 */
	TAILQ_REMOVE(&object->memq, m, listq);

	/*
	 * And show that the object has one fewer resident page.
	 */
	object->resident_page_count--;

	/*
	 * The vnode may now be recycled.
	 */
	if (object->resident_page_count == 0 && object->type == OBJT_VNODE)
		vdrop(object->handle);

	m->object = NULL;
}

/*
 *	vm_page_lookup:
 *
 *	Returns the page associated with the object/offset
 *	pair specified; if none is found, NULL is returned.
 *
 *	The object must be locked.
 */
vm_page_t
vm_page_lookup(vm_object_t object, vm_pindex_t pindex)
{

	VM_OBJECT_ASSERT_LOCKED(object);
	return (vm_radix_lookup(&object->rtree, pindex));
}

/*
 *	vm_page_find_least:
 *
 *	Returns the page associated with the object with least pindex
 *	greater than or equal to the parameter pindex, or NULL.
 *
 *	The object must be locked.
 */
vm_page_t
vm_page_find_least(vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t m;

	VM_OBJECT_ASSERT_LOCKED(object);
	if ((m = TAILQ_FIRST(&object->memq)) != NULL && m->pindex < pindex)
		m = vm_radix_lookup_ge(&object->rtree, pindex);
	return (m);
}

/*
 * Returns the given page's successor (by pindex) within the object if it is
 * resident; if none is found, NULL is returned.
 *
 * The object must be locked.
 */
vm_page_t
vm_page_next(vm_page_t m)
{
	vm_page_t next;

	VM_OBJECT_ASSERT_LOCKED(m->object);
	if ((next = TAILQ_NEXT(m, listq)) != NULL) {
		MPASS(next->object == m->object);
		if (next->pindex != m->pindex + 1)
			next = NULL;
	}
	return (next);
}

/*
 * Returns the given page's predecessor (by pindex) within the object if it is
 * resident; if none is found, NULL is returned.
 *
 * The object must be locked.
 */
vm_page_t
vm_page_prev(vm_page_t m)
{
	vm_page_t prev;

	VM_OBJECT_ASSERT_LOCKED(m->object);
	if ((prev = TAILQ_PREV(m, pglist, listq)) != NULL) {
		MPASS(prev->object == m->object);
		if (prev->pindex != m->pindex - 1)
			prev = NULL;
	}
	return (prev);
}

/*
 * Uses the page mnew as a replacement for an existing page at index
 * pindex which must be already present in the object.
 *
 * The existing page must not be on a paging queue.
 */
vm_page_t
vm_page_replace(vm_page_t mnew, vm_object_t object, vm_pindex_t pindex)
{
	vm_page_t mold;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(mnew->object == NULL,
	    ("vm_page_replace: page %p already in object", mnew));
	KASSERT(mnew->queue == PQ_NONE,
	    ("vm_page_replace: new page %p is on a paging queue", mnew));

	/*
	 * This function mostly follows vm_page_insert() and
	 * vm_page_remove() without the radix, object count and vnode
	 * dance.  Double check such functions for more comments.
	 */

	mnew->object = object;
	mnew->pindex = pindex;
	mold = vm_radix_replace(&object->rtree, mnew);
	KASSERT(mold->queue == PQ_NONE,
	    ("vm_page_replace: old page %p is on a paging queue", mold));

	/* Keep the resident page list in sorted order. */
	TAILQ_INSERT_AFTER(&object->memq, mold, mnew, listq);
	TAILQ_REMOVE(&object->memq, mold, listq);

	mold->object = NULL;
	vm_page_xunbusy_maybelocked(mold);

	/*
	 * The object's resident_page_count does not change because we have
	 * swapped one page for another, but OBJ_MIGHTBEDIRTY.
	 */
	if (pmap_page_is_write_mapped(mnew))
		vm_object_set_writeable_dirty(object);
	return (mold);
}

/*
 *	vm_page_rename:
 *
 *	Move the given memory entry from its
 *	current object to the specified target object/offset.
 *
 *	Note: swap associated with the page must be invalidated by the move.  We
 *	      have to do this for several reasons:  (1) we aren't freeing the
 *	      page, (2) we are dirtying the page, (3) the VM system is probably
 *	      moving the page from object A to B, and will then later move
 *	      the backing store from A to B and we can't have a conflict.
 *
 *	Note: we *always* dirty the page.  It is necessary both for the
 *	      fact that we moved it, and because we may be invalidating
 *	      swap.
 *
 *	The objects must be locked.
 */
int
vm_page_rename(vm_page_t m, vm_object_t new_object, vm_pindex_t new_pindex)
{
	vm_page_t mpred;
	vm_pindex_t opidx;

	VM_OBJECT_ASSERT_WLOCKED(new_object);

	mpred = vm_radix_lookup_le(&new_object->rtree, new_pindex);
	KASSERT(mpred == NULL || mpred->pindex != new_pindex,
	    ("vm_page_rename: pindex already renamed"));

	/*
	 * Create a custom version of vm_page_insert() which does not depend
	 * by m_prev and can cheat on the implementation aspects of the
	 * function.
	 */
	opidx = m->pindex;
	m->pindex = new_pindex;
	if (vm_radix_insert(&new_object->rtree, m)) {
		m->pindex = opidx;
		return (1);
	}

	/*
	 * The operation cannot fail anymore.  The removal must happen before
	 * the listq iterator is tainted.
	 */
	m->pindex = opidx;
	vm_page_lock(m);
	vm_page_remove(m);

	/* Return back to the new pindex to complete vm_page_insert(). */
	m->pindex = new_pindex;
	m->object = new_object;
	vm_page_unlock(m);
	vm_page_insert_radixdone(m, new_object, mpred);
	vm_page_dirty(m);
	return (0);
}

/*
 *	vm_page_alloc:
 *
 *	Allocate and return a page that is associated with the specified
 *	object and offset pair.  By default, this page is exclusive busied.
 *
 *	The caller must always specify an allocation class.
 *
 *	allocation classes:
 *	VM_ALLOC_NORMAL		normal process request
 *	VM_ALLOC_SYSTEM		system *really* needs a page
 *	VM_ALLOC_INTERRUPT	interrupt time request
 *
 *	optional allocation flags:
 *	VM_ALLOC_COUNT(number)	the number of additional pages that the caller
 *				intends to allocate
 *	VM_ALLOC_NOBUSY		do not exclusive busy the page
 *	VM_ALLOC_NODUMP		do not include the page in a kernel core dump
 *	VM_ALLOC_NOOBJ		page is not associated with an object and
 *				should not be exclusive busy
 *	VM_ALLOC_SBUSY		shared busy the allocated page
 *	VM_ALLOC_WIRED		wire the allocated page
 *	VM_ALLOC_ZERO		prefer a zeroed page
 */
vm_page_t
vm_page_alloc(vm_object_t object, vm_pindex_t pindex, int req)
{

	return (vm_page_alloc_after(object, pindex, req, object != NULL ?
	    vm_radix_lookup_le(&object->rtree, pindex) : NULL));
}

vm_page_t
vm_page_alloc_domain(vm_object_t object, vm_pindex_t pindex, int domain,
    int req)
{

	return (vm_page_alloc_domain_after(object, pindex, domain, req,
	    object != NULL ? vm_radix_lookup_le(&object->rtree, pindex) :
	    NULL));
}

/*
 * Allocate a page in the specified object with the given page index.  To
 * optimize insertion of the page into the object, the caller must also specifiy
 * the resident page in the object with largest index smaller than the given
 * page index, or NULL if no such page exists.
 */
vm_page_t
vm_page_alloc_after(vm_object_t object, vm_pindex_t pindex,
    int req, vm_page_t mpred)
{
	struct vm_domainset_iter di;
	vm_page_t m;
	int domain;

	vm_domainset_iter_page_init(&di, object, pindex, &domain, &req);
	do {
		m = vm_page_alloc_domain_after(object, pindex, domain, req,
		    mpred);
		if (m != NULL)
			break;
	} while (vm_domainset_iter_page(&di, object, &domain) == 0);

	return (m);
}

/*
 * Returns true if the number of free pages exceeds the minimum
 * for the request class and false otherwise.
 */
int
vm_domain_allocate(struct vm_domain *vmd, int req, int npages)
{
	u_int limit, old, new;

	req = req & VM_ALLOC_CLASS_MASK;

	/*
	 * The page daemon is allowed to dig deeper into the free page list.
	 */
	if (curproc == pageproc && req != VM_ALLOC_INTERRUPT)
		req = VM_ALLOC_SYSTEM;
	if (req == VM_ALLOC_INTERRUPT)
		limit = 0;
	else if (req == VM_ALLOC_SYSTEM)
		limit = vmd->vmd_interrupt_free_min;
	else
		limit = vmd->vmd_free_reserved;

	/*
	 * Attempt to reserve the pages.  Fail if we're below the limit.
	 */
	limit += npages;
	old = vmd->vmd_free_count;
	do {
		if (old < limit)
			return (0);
		new = old - npages;
	} while (atomic_fcmpset_int(&vmd->vmd_free_count, &old, new) == 0);

	/* Wake the page daemon if we've crossed the threshold. */
	if (vm_paging_needed(vmd, new) && !vm_paging_needed(vmd, old))
		pagedaemon_wakeup(vmd->vmd_domain);

	/* Only update bitsets on transitions. */
	if ((old >= vmd->vmd_free_min && new < vmd->vmd_free_min) ||
	    (old >= vmd->vmd_free_severe && new < vmd->vmd_free_severe))
		vm_domain_set(vmd);

	return (1);
}

vm_page_t
vm_page_alloc_domain_after(vm_object_t object, vm_pindex_t pindex, int domain,
    int req, vm_page_t mpred)
{
	struct vm_domain *vmd;
	vm_page_t m;
	int flags;

	KASSERT((object != NULL) == ((req & VM_ALLOC_NOOBJ) == 0) &&
	    (object != NULL || (req & VM_ALLOC_SBUSY) == 0) &&
	    ((req & (VM_ALLOC_NOBUSY | VM_ALLOC_SBUSY)) !=
	    (VM_ALLOC_NOBUSY | VM_ALLOC_SBUSY)),
	    ("inconsistent object(%p)/req(%x)", object, req));
	KASSERT(object == NULL || (req & VM_ALLOC_WAITOK) == 0,
	    ("Can't sleep and retry object insertion."));
	KASSERT(mpred == NULL || mpred->pindex < pindex,
	    ("mpred %p doesn't precede pindex 0x%jx", mpred,
	    (uintmax_t)pindex));
	if (object != NULL)
		VM_OBJECT_ASSERT_WLOCKED(object);

again:
	m = NULL;
#if VM_NRESERVLEVEL > 0
	/*
	 * Can we allocate the page from a reservation?
	 */
	if (vm_object_reserv(object) &&
	    ((m = vm_reserv_extend(req, object, pindex, domain, mpred)) != NULL ||
	    (m = vm_reserv_alloc_page(req, object, pindex, domain, mpred)) != NULL)) {
		domain = vm_phys_domain(m);
		vmd = VM_DOMAIN(domain);
		goto found;
	}
#endif
	vmd = VM_DOMAIN(domain);
	if (object != NULL && vmd->vmd_pgcache != NULL) {
		m = uma_zalloc(vmd->vmd_pgcache, M_NOWAIT);
		if (m != NULL)
			goto found;
	}
	if (vm_domain_allocate(vmd, req, 1)) {
		/*
		 * If not, allocate it from the free page queues.
		 */
		vm_domain_free_lock(vmd);
		m = vm_phys_alloc_pages(domain, object != NULL ?
		    VM_FREEPOOL_DEFAULT : VM_FREEPOOL_DIRECT, 0);
		vm_domain_free_unlock(vmd);
		if (m == NULL) {
			vm_domain_freecnt_inc(vmd, 1);
#if VM_NRESERVLEVEL > 0
			if (vm_reserv_reclaim_inactive(domain))
				goto again;
#endif
		}
	}
	if (m == NULL) {
		/*
		 * Not allocatable, give up.
		 */
		if (vm_domain_alloc_fail(vmd, object, req))
			goto again;
		return (NULL);
	}

	/*
	 *  At this point we had better have found a good page.
	 */
	KASSERT(m != NULL, ("missing page"));

found:
	vm_page_dequeue(m);
	vm_page_alloc_check(m);

	/*
	 * Initialize the page.  Only the PG_ZERO flag is inherited.
	 */
	flags = 0;
	if ((req & VM_ALLOC_ZERO) != 0)
		flags = PG_ZERO;
	flags &= m->flags;
	if ((req & VM_ALLOC_NODUMP) != 0)
		flags |= PG_NODUMP;
	m->flags = flags;
	m->aflags = 0;
	m->oflags = object == NULL || (object->flags & OBJ_UNMANAGED) != 0 ?
	    VPO_UNMANAGED : 0;
	m->busy_lock = VPB_UNBUSIED;
	if ((req & (VM_ALLOC_NOBUSY | VM_ALLOC_NOOBJ | VM_ALLOC_SBUSY)) == 0)
		m->busy_lock = VPB_SINGLE_EXCLUSIVER;
	if ((req & VM_ALLOC_SBUSY) != 0)
		m->busy_lock = VPB_SHARERS_WORD(1);
	if (req & VM_ALLOC_WIRED) {
		/*
		 * The page lock is not required for wiring a page until that
		 * page is inserted into the object.
		 */
		vm_wire_add(1);
		m->wire_count = 1;
	}
	m->act_count = 0;

	if (object != NULL) {
		if (vm_page_insert_after(m, object, pindex, mpred)) {
			if (req & VM_ALLOC_WIRED) {
				vm_wire_sub(1);
				m->wire_count = 0;
			}
			KASSERT(m->object == NULL, ("page %p has object", m));
			m->oflags = VPO_UNMANAGED;
			m->busy_lock = VPB_UNBUSIED;
			/* Don't change PG_ZERO. */
			vm_page_free_toq(m);
			if (req & VM_ALLOC_WAITFAIL) {
				VM_OBJECT_WUNLOCK(object);
				vm_radix_wait();
				VM_OBJECT_WLOCK(object);
			}
			return (NULL);
		}

		/* Ignore device objects; the pager sets "memattr" for them. */
		if (object->memattr != VM_MEMATTR_DEFAULT &&
		    (object->flags & OBJ_FICTITIOUS) == 0)
			pmap_page_set_memattr(m, object->memattr);
	} else
		m->pindex = pindex;

	return (m);
}

/*
 *	vm_page_alloc_contig:
 *
 *	Allocate a contiguous set of physical pages of the given size "npages"
 *	from the free lists.  All of the physical pages must be at or above
 *	the given physical address "low" and below the given physical address
 *	"high".  The given value "alignment" determines the alignment of the
 *	first physical page in the set.  If the given value "boundary" is
 *	non-zero, then the set of physical pages cannot cross any physical
 *	address boundary that is a multiple of that value.  Both "alignment"
 *	and "boundary" must be a power of two.
 *
 *	If the specified memory attribute, "memattr", is VM_MEMATTR_DEFAULT,
 *	then the memory attribute setting for the physical pages is configured
 *	to the object's memory attribute setting.  Otherwise, the memory
 *	attribute setting for the physical pages is configured to "memattr",
 *	overriding the object's memory attribute setting.  However, if the
 *	object's memory attribute setting is not VM_MEMATTR_DEFAULT, then the
 *	memory attribute setting for the physical pages cannot be configured
 *	to VM_MEMATTR_DEFAULT.
 *
 *	The specified object may not contain fictitious pages.
 *
 *	The caller must always specify an allocation class.
 *
 *	allocation classes:
 *	VM_ALLOC_NORMAL		normal process request
 *	VM_ALLOC_SYSTEM		system *really* needs a page
 *	VM_ALLOC_INTERRUPT	interrupt time request
 *
 *	optional allocation flags:
 *	VM_ALLOC_NOBUSY		do not exclusive busy the page
 *	VM_ALLOC_NODUMP		do not include the page in a kernel core dump
 *	VM_ALLOC_NOOBJ		page is not associated with an object and
 *				should not be exclusive busy
 *	VM_ALLOC_SBUSY		shared busy the allocated page
 *	VM_ALLOC_WIRED		wire the allocated page
 *	VM_ALLOC_ZERO		prefer a zeroed page
 */
vm_page_t
vm_page_alloc_contig(vm_object_t object, vm_pindex_t pindex, int req,
    u_long npages, vm_paddr_t low, vm_paddr_t high, u_long alignment,
    vm_paddr_t boundary, vm_memattr_t memattr)
{
	struct vm_domainset_iter di;
	vm_page_t m;
	int domain;

	vm_domainset_iter_page_init(&di, object, pindex, &domain, &req);
	do {
		m = vm_page_alloc_contig_domain(object, pindex, domain, req,
		    npages, low, high, alignment, boundary, memattr);
		if (m != NULL)
			break;
	} while (vm_domainset_iter_page(&di, object, &domain) == 0);

	return (m);
}

vm_page_t
vm_page_alloc_contig_domain(vm_object_t object, vm_pindex_t pindex, int domain,
    int req, u_long npages, vm_paddr_t low, vm_paddr_t high, u_long alignment,
    vm_paddr_t boundary, vm_memattr_t memattr)
{
	struct vm_domain *vmd;
	vm_page_t m, m_ret, mpred;
	u_int busy_lock, flags, oflags;

	mpred = NULL;	/* XXX: pacify gcc */
	KASSERT((object != NULL) == ((req & VM_ALLOC_NOOBJ) == 0) &&
	    (object != NULL || (req & VM_ALLOC_SBUSY) == 0) &&
	    ((req & (VM_ALLOC_NOBUSY | VM_ALLOC_SBUSY)) !=
	    (VM_ALLOC_NOBUSY | VM_ALLOC_SBUSY)),
	    ("vm_page_alloc_contig: inconsistent object(%p)/req(%x)", object,
	    req));
	KASSERT(object == NULL || (req & VM_ALLOC_WAITOK) == 0,
	    ("Can't sleep and retry object insertion."));
	if (object != NULL) {
		VM_OBJECT_ASSERT_WLOCKED(object);
		KASSERT((object->flags & OBJ_FICTITIOUS) == 0,
		    ("vm_page_alloc_contig: object %p has fictitious pages",
		    object));
	}
	KASSERT(npages > 0, ("vm_page_alloc_contig: npages is zero"));

	if (object != NULL) {
		mpred = vm_radix_lookup_le(&object->rtree, pindex);
		KASSERT(mpred == NULL || mpred->pindex != pindex,
		    ("vm_page_alloc_contig: pindex already allocated"));
	}

	/*
	 * Can we allocate the pages without the number of free pages falling
	 * below the lower bound for the allocation class?
	 */
again:
#if VM_NRESERVLEVEL > 0
	/*
	 * Can we allocate the pages from a reservation?
	 */
	if (vm_object_reserv(object) &&
	    ((m_ret = vm_reserv_extend_contig(req, object, pindex, domain,
	    npages, low, high, alignment, boundary, mpred)) != NULL ||
	    (m_ret = vm_reserv_alloc_contig(req, object, pindex, domain,
	    npages, low, high, alignment, boundary, mpred)) != NULL)) {
		domain = vm_phys_domain(m_ret);
		vmd = VM_DOMAIN(domain);
		goto found;
	}
#endif
	m_ret = NULL;
	vmd = VM_DOMAIN(domain);
	if (vm_domain_allocate(vmd, req, npages)) {
		/*
		 * allocate them from the free page queues.
		 */
		vm_domain_free_lock(vmd);
		m_ret = vm_phys_alloc_contig(domain, npages, low, high,
		    alignment, boundary);
		vm_domain_free_unlock(vmd);
		if (m_ret == NULL) {
			vm_domain_freecnt_inc(vmd, npages);
#if VM_NRESERVLEVEL > 0
			if (vm_reserv_reclaim_contig(domain, npages, low,
			    high, alignment, boundary))
				goto again;
#endif
		}
	}
	if (m_ret == NULL) {
		if (vm_domain_alloc_fail(vmd, object, req))
			goto again;
		return (NULL);
	}
#if VM_NRESERVLEVEL > 0
found:
#endif
	for (m = m_ret; m < &m_ret[npages]; m++) {
		vm_page_dequeue(m);
		vm_page_alloc_check(m);
	}

	/*
	 * Initialize the pages.  Only the PG_ZERO flag is inherited.
	 */
	flags = 0;
	if ((req & VM_ALLOC_ZERO) != 0)
		flags = PG_ZERO;
	if ((req & VM_ALLOC_NODUMP) != 0)
		flags |= PG_NODUMP;
	oflags = object == NULL || (object->flags & OBJ_UNMANAGED) != 0 ?
	    VPO_UNMANAGED : 0;
	busy_lock = VPB_UNBUSIED;
	if ((req & (VM_ALLOC_NOBUSY | VM_ALLOC_NOOBJ | VM_ALLOC_SBUSY)) == 0)
		busy_lock = VPB_SINGLE_EXCLUSIVER;
	if ((req & VM_ALLOC_SBUSY) != 0)
		busy_lock = VPB_SHARERS_WORD(1);
	if ((req & VM_ALLOC_WIRED) != 0)
		vm_wire_add(npages);
	if (object != NULL) {
		if (object->memattr != VM_MEMATTR_DEFAULT &&
		    memattr == VM_MEMATTR_DEFAULT)
			memattr = object->memattr;
	}
	for (m = m_ret; m < &m_ret[npages]; m++) {
		m->aflags = 0;
		m->flags = (m->flags | PG_NODUMP) & flags;
		m->busy_lock = busy_lock;
		if ((req & VM_ALLOC_WIRED) != 0)
			m->wire_count = 1;
		m->act_count = 0;
		m->oflags = oflags;
		if (object != NULL) {
			if (vm_page_insert_after(m, object, pindex, mpred)) {
				if ((req & VM_ALLOC_WIRED) != 0)
					vm_wire_sub(npages);
				KASSERT(m->object == NULL,
				    ("page %p has object", m));
				mpred = m;
				for (m = m_ret; m < &m_ret[npages]; m++) {
					if (m <= mpred &&
					    (req & VM_ALLOC_WIRED) != 0)
						m->wire_count = 0;
					m->oflags = VPO_UNMANAGED;
					m->busy_lock = VPB_UNBUSIED;
					/* Don't change PG_ZERO. */
					vm_page_free_toq(m);
				}
				if (req & VM_ALLOC_WAITFAIL) {
					VM_OBJECT_WUNLOCK(object);
					vm_radix_wait();
					VM_OBJECT_WLOCK(object);
				}
				return (NULL);
			}
			mpred = m;
		} else
			m->pindex = pindex;
		if (memattr != VM_MEMATTR_DEFAULT)
			pmap_page_set_memattr(m, memattr);
		pindex++;
	}
	return (m_ret);
}

/*
 * Check a page that has been freshly dequeued from a freelist.
 */
static void
vm_page_alloc_check(vm_page_t m)
{

	KASSERT(m->object == NULL, ("page %p has object", m));
	KASSERT(m->queue == PQ_NONE && (m->aflags & PGA_QUEUE_STATE_MASK) == 0,
	    ("page %p has unexpected queue %d, flags %#x",
	    m, m->queue, (m->aflags & PGA_QUEUE_STATE_MASK)));
	KASSERT(!vm_page_held(m), ("page %p is held", m));
	KASSERT(!vm_page_busied(m), ("page %p is busy", m));
	KASSERT(m->dirty == 0, ("page %p is dirty", m));
	KASSERT(pmap_page_get_memattr(m) == VM_MEMATTR_DEFAULT,
	    ("page %p has unexpected memattr %d",
	    m, pmap_page_get_memattr(m)));
	KASSERT(m->valid == 0, ("free page %p is valid", m));
}

/*
 * 	vm_page_alloc_freelist:
 *
 *	Allocate a physical page from the specified free page list.
 *
 *	The caller must always specify an allocation class.
 *
 *	allocation classes:
 *	VM_ALLOC_NORMAL		normal process request
 *	VM_ALLOC_SYSTEM		system *really* needs a page
 *	VM_ALLOC_INTERRUPT	interrupt time request
 *
 *	optional allocation flags:
 *	VM_ALLOC_COUNT(number)	the number of additional pages that the caller
 *				intends to allocate
 *	VM_ALLOC_WIRED		wire the allocated page
 *	VM_ALLOC_ZERO		prefer a zeroed page
 */
vm_page_t
vm_page_alloc_freelist(int freelist, int req)
{
	struct vm_domainset_iter di;
	vm_page_t m;
	int domain;

	vm_domainset_iter_page_init(&di, NULL, 0, &domain, &req);
	do {
		m = vm_page_alloc_freelist_domain(domain, freelist, req);
		if (m != NULL)
			break;
	} while (vm_domainset_iter_page(&di, NULL, &domain) == 0);

	return (m);
}

vm_page_t
vm_page_alloc_freelist_domain(int domain, int freelist, int req)
{
	struct vm_domain *vmd;
	vm_page_t m;
	u_int flags;

	m = NULL;
	vmd = VM_DOMAIN(domain);
again:
	if (vm_domain_allocate(vmd, req, 1)) {
		vm_domain_free_lock(vmd);
		m = vm_phys_alloc_freelist_pages(domain, freelist,
		    VM_FREEPOOL_DIRECT, 0);
		vm_domain_free_unlock(vmd);
		if (m == NULL)
			vm_domain_freecnt_inc(vmd, 1);
	}
	if (m == NULL) {
		if (vm_domain_alloc_fail(vmd, NULL, req))
			goto again;
		return (NULL);
	}
	vm_page_dequeue(m);
	vm_page_alloc_check(m);

	/*
	 * Initialize the page.  Only the PG_ZERO flag is inherited.
	 */
	m->aflags = 0;
	flags = 0;
	if ((req & VM_ALLOC_ZERO) != 0)
		flags = PG_ZERO;
	m->flags &= flags;
	if ((req & VM_ALLOC_WIRED) != 0) {
		/*
		 * The page lock is not required for wiring a page that does
		 * not belong to an object.
		 */
		vm_wire_add(1);
		m->wire_count = 1;
	}
	/* Unmanaged pages don't use "act_count". */
	m->oflags = VPO_UNMANAGED;
	return (m);
}

static int
vm_page_import(void *arg, void **store, int cnt, int domain, int flags)
{
	struct vm_domain *vmd;
	int i;

	vmd = arg;
	/* Only import if we can bring in a full bucket. */
	if (cnt == 1 || !vm_domain_allocate(vmd, VM_ALLOC_NORMAL, cnt))
		return (0);
	domain = vmd->vmd_domain;
	vm_domain_free_lock(vmd);
	i = vm_phys_alloc_npages(domain, VM_FREEPOOL_DEFAULT, cnt,
	    (vm_page_t *)store);
	vm_domain_free_unlock(vmd);
	if (cnt != i)
		vm_domain_freecnt_inc(vmd, cnt - i);

	return (i);
}

static void
vm_page_release(void *arg, void **store, int cnt)
{
	struct vm_domain *vmd;
	vm_page_t m;
	int i;

	vmd = arg;
	vm_domain_free_lock(vmd);
	for (i = 0; i < cnt; i++) {
		m = (vm_page_t)store[i];
		vm_phys_free_pages(m, 0);
	}
	vm_domain_free_unlock(vmd);
	vm_domain_freecnt_inc(vmd, cnt);
}

#define	VPSC_ANY	0	/* No restrictions. */
#define	VPSC_NORESERV	1	/* Skip reservations; implies VPSC_NOSUPER. */
#define	VPSC_NOSUPER	2	/* Skip superpages. */

/*
 *	vm_page_scan_contig:
 *
 *	Scan vm_page_array[] between the specified entries "m_start" and
 *	"m_end" for a run of contiguous physical pages that satisfy the
 *	specified conditions, and return the lowest page in the run.  The
 *	specified "alignment" determines the alignment of the lowest physical
 *	page in the run.  If the specified "boundary" is non-zero, then the
 *	run of physical pages cannot span a physical address that is a
 *	multiple of "boundary".
 *
 *	"m_end" is never dereferenced, so it need not point to a vm_page
 *	structure within vm_page_array[].
 *
 *	"npages" must be greater than zero.  "m_start" and "m_end" must not
 *	span a hole (or discontiguity) in the physical address space.  Both
 *	"alignment" and "boundary" must be a power of two.
 */
vm_page_t
vm_page_scan_contig(u_long npages, vm_page_t m_start, vm_page_t m_end,
    u_long alignment, vm_paddr_t boundary, int options)
{
	struct mtx *m_mtx;
	vm_object_t object;
	vm_paddr_t pa;
	vm_page_t m, m_run;
#if VM_NRESERVLEVEL > 0
	int level;
#endif
	int m_inc, order, run_ext, run_len;

	KASSERT(npages > 0, ("npages is 0"));
	KASSERT(powerof2(alignment), ("alignment is not a power of 2"));
	KASSERT(powerof2(boundary), ("boundary is not a power of 2"));
	m_run = NULL;
	run_len = 0;
	m_mtx = NULL;
	for (m = m_start; m < m_end && run_len < npages; m += m_inc) {
		KASSERT((m->flags & PG_MARKER) == 0,
		    ("page %p is PG_MARKER", m));
		KASSERT((m->flags & PG_FICTITIOUS) == 0 || m->wire_count == 1,
		    ("fictitious page %p has invalid wire count", m));

		/*
		 * If the current page would be the start of a run, check its
		 * physical address against the end, alignment, and boundary
		 * conditions.  If it doesn't satisfy these conditions, either
		 * terminate the scan or advance to the next page that
		 * satisfies the failed condition.
		 */
		if (run_len == 0) {
			KASSERT(m_run == NULL, ("m_run != NULL"));
			if (m + npages > m_end)
				break;
			pa = VM_PAGE_TO_PHYS(m);
			if ((pa & (alignment - 1)) != 0) {
				m_inc = atop(roundup2(pa, alignment) - pa);
				continue;
			}
			if (rounddown2(pa ^ (pa + ptoa(npages) - 1),
			    boundary) != 0) {
				m_inc = atop(roundup2(pa, boundary) - pa);
				continue;
			}
		} else
			KASSERT(m_run != NULL, ("m_run == NULL"));

		vm_page_change_lock(m, &m_mtx);
		m_inc = 1;
retry:
		if (vm_page_held(m))
			run_ext = 0;
#if VM_NRESERVLEVEL > 0
		else if ((level = vm_reserv_level(m)) >= 0 &&
		    (options & VPSC_NORESERV) != 0) {
			run_ext = 0;
			/* Advance to the end of the reservation. */
			pa = VM_PAGE_TO_PHYS(m);
			m_inc = atop(roundup2(pa + 1, vm_reserv_size(level)) -
			    pa);
		}
#endif
		else if ((object = m->object) != NULL) {
			/*
			 * The page is considered eligible for relocation if
			 * and only if it could be laundered or reclaimed by
			 * the page daemon.
			 */
			if (!VM_OBJECT_TRYRLOCK(object)) {
				mtx_unlock(m_mtx);
				VM_OBJECT_RLOCK(object);
				mtx_lock(m_mtx);
				if (m->object != object) {
					/*
					 * The page may have been freed.
					 */
					VM_OBJECT_RUNLOCK(object);
					goto retry;
				} else if (vm_page_held(m)) {
					run_ext = 0;
					goto unlock;
				}
			}
			KASSERT((m->flags & PG_UNHOLDFREE) == 0,
			    ("page %p is PG_UNHOLDFREE", m));
			/* Don't care: PG_NODUMP, PG_ZERO. */
			if (object->type != OBJT_DEFAULT &&
			    object->type != OBJT_SWAP &&
			    object->type != OBJT_VNODE) {
				run_ext = 0;
#if VM_NRESERVLEVEL > 0
			} else if ((options & VPSC_NOSUPER) != 0 &&
			    (level = vm_reserv_level_iffullpop(m)) >= 0) {
				run_ext = 0;
				/* Advance to the end of the superpage. */
				pa = VM_PAGE_TO_PHYS(m);
				m_inc = atop(roundup2(pa + 1,
				    vm_reserv_size(level)) - pa);
#endif
			} else if (object->memattr == VM_MEMATTR_DEFAULT &&
			    vm_page_queue(m) != PQ_NONE && !vm_page_busied(m)) {
				/*
				 * The page is allocated but eligible for
				 * relocation.  Extend the current run by one
				 * page.
				 */
				KASSERT(pmap_page_get_memattr(m) ==
				    VM_MEMATTR_DEFAULT,
				    ("page %p has an unexpected memattr", m));
				KASSERT((m->oflags & (VPO_SWAPINPROG |
				    VPO_SWAPSLEEP | VPO_UNMANAGED)) == 0,
				    ("page %p has unexpected oflags", m));
				/* Don't care: VPO_NOSYNC. */
				run_ext = 1;
			} else
				run_ext = 0;
unlock:
			VM_OBJECT_RUNLOCK(object);
#if VM_NRESERVLEVEL > 0
		} else if (level >= 0) {
			/*
			 * The page is reserved but not yet allocated.  In
			 * other words, it is still free.  Extend the current
			 * run by one page.
			 */
			run_ext = 1;
#endif
		} else if ((order = m->order) < VM_NFREEORDER) {
			/*
			 * The page is enqueued in the physical memory
			 * allocator's free page queues.  Moreover, it is the
			 * first page in a power-of-two-sized run of
			 * contiguous free pages.  Add these pages to the end
			 * of the current run, and jump ahead.
			 */
			run_ext = 1 << order;
			m_inc = 1 << order;
		} else {
			/*
			 * Skip the page for one of the following reasons: (1)
			 * It is enqueued in the physical memory allocator's
			 * free page queues.  However, it is not the first
			 * page in a run of contiguous free pages.  (This case
			 * rarely occurs because the scan is performed in
			 * ascending order.) (2) It is not reserved, and it is
			 * transitioning from free to allocated.  (Conversely,
			 * the transition from allocated to free for managed
			 * pages is blocked by the page lock.) (3) It is
			 * allocated but not contained by an object and not
			 * wired, e.g., allocated by Xen's balloon driver.
			 */
			run_ext = 0;
		}

		/*
		 * Extend or reset the current run of pages.
		 */
		if (run_ext > 0) {
			if (run_len == 0)
				m_run = m;
			run_len += run_ext;
		} else {
			if (run_len > 0) {
				m_run = NULL;
				run_len = 0;
			}
		}
	}
	if (m_mtx != NULL)
		mtx_unlock(m_mtx);
	if (run_len >= npages)
		return (m_run);
	return (NULL);
}

/*
 *	vm_page_reclaim_run:
 *
 *	Try to relocate each of the allocated virtual pages within the
 *	specified run of physical pages to a new physical address.  Free the
 *	physical pages underlying the relocated virtual pages.  A virtual page
 *	is relocatable if and only if it could be laundered or reclaimed by
 *	the page daemon.  Whenever possible, a virtual page is relocated to a
 *	physical address above "high".
 *
 *	Returns 0 if every physical page within the run was already free or
 *	just freed by a successful relocation.  Otherwise, returns a non-zero
 *	value indicating why the last attempt to relocate a virtual page was
 *	unsuccessful.
 *
 *	"req_class" must be an allocation class.
 */
static int
vm_page_reclaim_run(int req_class, int domain, u_long npages, vm_page_t m_run,
    vm_paddr_t high)
{
	struct vm_domain *vmd;
	struct mtx *m_mtx;
	struct spglist free;
	vm_object_t object;
	vm_paddr_t pa;
	vm_page_t m, m_end, m_new;
	int error, order, req;

	KASSERT((req_class & VM_ALLOC_CLASS_MASK) == req_class,
	    ("req_class is not an allocation class"));
	SLIST_INIT(&free);
	error = 0;
	m = m_run;
	m_end = m_run + npages;
	m_mtx = NULL;
	for (; error == 0 && m < m_end; m++) {
		KASSERT((m->flags & (PG_FICTITIOUS | PG_MARKER)) == 0,
		    ("page %p is PG_FICTITIOUS or PG_MARKER", m));

		/*
		 * Avoid releasing and reacquiring the same page lock.
		 */
		vm_page_change_lock(m, &m_mtx);
retry:
		if (vm_page_held(m))
			error = EBUSY;
		else if ((object = m->object) != NULL) {
			/*
			 * The page is relocated if and only if it could be
			 * laundered or reclaimed by the page daemon.
			 */
			if (!VM_OBJECT_TRYWLOCK(object)) {
				mtx_unlock(m_mtx);
				VM_OBJECT_WLOCK(object);
				mtx_lock(m_mtx);
				if (m->object != object) {
					/*
					 * The page may have been freed.
					 */
					VM_OBJECT_WUNLOCK(object);
					goto retry;
				} else if (vm_page_held(m)) {
					error = EBUSY;
					goto unlock;
				}
			}
			KASSERT((m->flags & PG_UNHOLDFREE) == 0,
			    ("page %p is PG_UNHOLDFREE", m));
			/* Don't care: PG_NODUMP, PG_ZERO. */
			if (object->type != OBJT_DEFAULT &&
			    object->type != OBJT_SWAP &&
			    object->type != OBJT_VNODE)
				error = EINVAL;
			else if (object->memattr != VM_MEMATTR_DEFAULT)
				error = EINVAL;
			else if (vm_page_queue(m) != PQ_NONE &&
			    !vm_page_busied(m)) {
				KASSERT(pmap_page_get_memattr(m) ==
				    VM_MEMATTR_DEFAULT,
				    ("page %p has an unexpected memattr", m));
				KASSERT((m->oflags & (VPO_SWAPINPROG |
				    VPO_SWAPSLEEP | VPO_UNMANAGED)) == 0,
				    ("page %p has unexpected oflags", m));
				/* Don't care: VPO_NOSYNC. */
				if (m->valid != 0) {
					/*
					 * First, try to allocate a new page
					 * that is above "high".  Failing
					 * that, try to allocate a new page
					 * that is below "m_run".  Allocate
					 * the new page between the end of
					 * "m_run" and "high" only as a last
					 * resort.
					 */
					req = req_class | VM_ALLOC_NOOBJ;
					if ((m->flags & PG_NODUMP) != 0)
						req |= VM_ALLOC_NODUMP;
					if (trunc_page(high) !=
					    ~(vm_paddr_t)PAGE_MASK) {
						m_new = vm_page_alloc_contig(
						    NULL, 0, req, 1,
						    round_page(high),
						    ~(vm_paddr_t)0,
						    PAGE_SIZE, 0,
						    VM_MEMATTR_DEFAULT);
					} else
						m_new = NULL;
					if (m_new == NULL) {
						pa = VM_PAGE_TO_PHYS(m_run);
						m_new = vm_page_alloc_contig(
						    NULL, 0, req, 1,
						    0, pa - 1, PAGE_SIZE, 0,
						    VM_MEMATTR_DEFAULT);
					}
					if (m_new == NULL) {
						pa += ptoa(npages);
						m_new = vm_page_alloc_contig(
						    NULL, 0, req, 1,
						    pa, high, PAGE_SIZE, 0,
						    VM_MEMATTR_DEFAULT);
					}
					if (m_new == NULL) {
						error = ENOMEM;
						goto unlock;
					}
					KASSERT(m_new->wire_count == 0,
					    ("page %p is wired", m_new));

					/*
					 * Replace "m" with the new page.  For
					 * vm_page_replace(), "m" must be busy
					 * and dequeued.  Finally, change "m"
					 * as if vm_page_free() was called.
					 */
					if (object->ref_count != 0)
						pmap_remove_all(m);
					m_new->aflags = m->aflags &
					    ~PGA_QUEUE_STATE_MASK;
					KASSERT(m_new->oflags == VPO_UNMANAGED,
					    ("page %p is managed", m_new));
					m_new->oflags = m->oflags & VPO_NOSYNC;
					pmap_copy_page(m, m_new);
					m_new->valid = m->valid;
					m_new->dirty = m->dirty;
					m->flags &= ~PG_ZERO;
					vm_page_xbusy(m);
					vm_page_dequeue(m);
					vm_page_replace_checked(m_new, object,
					    m->pindex, m);
					if (vm_page_free_prep(m))
						SLIST_INSERT_HEAD(&free, m,
						    plinks.s.ss);

					/*
					 * The new page must be deactivated
					 * before the object is unlocked.
					 */
					vm_page_change_lock(m_new, &m_mtx);
					vm_page_deactivate(m_new);
				} else {
					m->flags &= ~PG_ZERO;
					vm_page_dequeue(m);
					vm_page_remove(m);
					if (vm_page_free_prep(m))
						SLIST_INSERT_HEAD(&free, m,
						    plinks.s.ss);
					KASSERT(m->dirty == 0,
					    ("page %p is dirty", m));
				}
			} else
				error = EBUSY;
unlock:
			VM_OBJECT_WUNLOCK(object);
		} else {
			MPASS(vm_phys_domain(m) == domain);
			vmd = VM_DOMAIN(domain);
			vm_domain_free_lock(vmd);
			order = m->order;
			if (order < VM_NFREEORDER) {
				/*
				 * The page is enqueued in the physical memory
				 * allocator's free page queues.  Moreover, it
				 * is the first page in a power-of-two-sized
				 * run of contiguous free pages.  Jump ahead
				 * to the last page within that run, and
				 * continue from there.
				 */
				m += (1 << order) - 1;
			}
#if VM_NRESERVLEVEL > 0
			else if (vm_reserv_is_page_free(m))
				order = 0;
#endif
			vm_domain_free_unlock(vmd);
			if (order == VM_NFREEORDER)
				error = EINVAL;
		}
	}
	if (m_mtx != NULL)
		mtx_unlock(m_mtx);
	if ((m = SLIST_FIRST(&free)) != NULL) {
		int cnt;

		vmd = VM_DOMAIN(domain);
		cnt = 0;
		vm_domain_free_lock(vmd);
		do {
			MPASS(vm_phys_domain(m) == domain);
			SLIST_REMOVE_HEAD(&free, plinks.s.ss);
			vm_phys_free_pages(m, 0);
			cnt++;
		} while ((m = SLIST_FIRST(&free)) != NULL);
		vm_domain_free_unlock(vmd);
		vm_domain_freecnt_inc(vmd, cnt);
	}
	return (error);
}

#define	NRUNS	16

CTASSERT(powerof2(NRUNS));

#define	RUN_INDEX(count)	((count) & (NRUNS - 1))

#define	MIN_RECLAIM	8

/*
 *	vm_page_reclaim_contig:
 *
 *	Reclaim allocated, contiguous physical memory satisfying the specified
 *	conditions by relocating the virtual pages using that physical memory.
 *	Returns true if reclamation is successful and false otherwise.  Since
 *	relocation requires the allocation of physical pages, reclamation may
 *	fail due to a shortage of free pages.  When reclamation fails, callers
 *	are expected to perform vm_wait() before retrying a failed allocation
 *	operation, e.g., vm_page_alloc_contig().
 *
 *	The caller must always specify an allocation class through "req".
 *
 *	allocation classes:
 *	VM_ALLOC_NORMAL		normal process request
 *	VM_ALLOC_SYSTEM		system *really* needs a page
 *	VM_ALLOC_INTERRUPT	interrupt time request
 *
 *	The optional allocation flags are ignored.
 *
 *	"npages" must be greater than zero.  Both "alignment" and "boundary"
 *	must be a power of two.
 */
bool
vm_page_reclaim_contig_domain(int domain, int req, u_long npages,
    vm_paddr_t low, vm_paddr_t high, u_long alignment, vm_paddr_t boundary)
{
	struct vm_domain *vmd;
	vm_paddr_t curr_low;
	vm_page_t m_run, m_runs[NRUNS];
	u_long count, reclaimed;
	int error, i, options, req_class;

	KASSERT(npages > 0, ("npages is 0"));
	KASSERT(powerof2(alignment), ("alignment is not a power of 2"));
	KASSERT(powerof2(boundary), ("boundary is not a power of 2"));
	req_class = req & VM_ALLOC_CLASS_MASK;

	/*
	 * The page daemon is allowed to dig deeper into the free page list.
	 */
	if (curproc == pageproc && req_class != VM_ALLOC_INTERRUPT)
		req_class = VM_ALLOC_SYSTEM;

	/*
	 * Return if the number of free pages cannot satisfy the requested
	 * allocation.
	 */
	vmd = VM_DOMAIN(domain);
	count = vmd->vmd_free_count;
	if (count < npages + vmd->vmd_free_reserved || (count < npages +
	    vmd->vmd_interrupt_free_min && req_class == VM_ALLOC_SYSTEM) ||
	    (count < npages && req_class == VM_ALLOC_INTERRUPT))
		return (false);

	/*
	 * Scan up to three times, relaxing the restrictions ("options") on
	 * the reclamation of reservations and superpages each time.
	 */
	for (options = VPSC_NORESERV;;) {
		/*
		 * Find the highest runs that satisfy the given constraints
		 * and restrictions, and record them in "m_runs".
		 */
		curr_low = low;
		count = 0;
		for (;;) {
			m_run = vm_phys_scan_contig(domain, npages, curr_low,
			    high, alignment, boundary, options);
			if (m_run == NULL)
				break;
			curr_low = VM_PAGE_TO_PHYS(m_run) + ptoa(npages);
			m_runs[RUN_INDEX(count)] = m_run;
			count++;
		}

		/*
		 * Reclaim the highest runs in LIFO (descending) order until
		 * the number of reclaimed pages, "reclaimed", is at least
		 * MIN_RECLAIM.  Reset "reclaimed" each time because each
		 * reclamation is idempotent, and runs will (likely) recur
		 * from one scan to the next as restrictions are relaxed.
		 */
		reclaimed = 0;
		for (i = 0; count > 0 && i < NRUNS; i++) {
			count--;
			m_run = m_runs[RUN_INDEX(count)];
			error = vm_page_reclaim_run(req_class, domain, npages,
			    m_run, high);
			if (error == 0) {
				reclaimed += npages;
				if (reclaimed >= MIN_RECLAIM)
					return (true);
			}
		}

		/*
		 * Either relax the restrictions on the next scan or return if
		 * the last scan had no restrictions.
		 */
		if (options == VPSC_NORESERV)
			options = VPSC_NOSUPER;
		else if (options == VPSC_NOSUPER)
			options = VPSC_ANY;
		else if (options == VPSC_ANY)
			return (reclaimed != 0);
	}
}

bool
vm_page_reclaim_contig(int req, u_long npages, vm_paddr_t low, vm_paddr_t high,
    u_long alignment, vm_paddr_t boundary)
{
	struct vm_domainset_iter di;
	int domain;
	bool ret;

	vm_domainset_iter_page_init(&di, NULL, 0, &domain, &req);
	do {
		ret = vm_page_reclaim_contig_domain(domain, req, npages, low,
		    high, alignment, boundary);
		if (ret)
			break;
	} while (vm_domainset_iter_page(&di, NULL, &domain) == 0);

	return (ret);
}

/*
 * Set the domain in the appropriate page level domainset.
 */
void
vm_domain_set(struct vm_domain *vmd)
{

	mtx_lock(&vm_domainset_lock);
	if (!vmd->vmd_minset && vm_paging_min(vmd)) {
		vmd->vmd_minset = 1;
		DOMAINSET_SET(vmd->vmd_domain, &vm_min_domains);
	}
	if (!vmd->vmd_severeset && vm_paging_severe(vmd)) {
		vmd->vmd_severeset = 1;
		DOMAINSET_SET(vmd->vmd_domain, &vm_severe_domains);
	}
	mtx_unlock(&vm_domainset_lock);
}

/*
 * Clear the domain from the appropriate page level domainset.
 */
void
vm_domain_clear(struct vm_domain *vmd)
{

	mtx_lock(&vm_domainset_lock);
	if (vmd->vmd_minset && !vm_paging_min(vmd)) {
		vmd->vmd_minset = 0;
		DOMAINSET_CLR(vmd->vmd_domain, &vm_min_domains);
		if (vm_min_waiters != 0) {
			vm_min_waiters = 0;
			wakeup(&vm_min_domains);
		}
	}
	if (vmd->vmd_severeset && !vm_paging_severe(vmd)) {
		vmd->vmd_severeset = 0;
		DOMAINSET_CLR(vmd->vmd_domain, &vm_severe_domains);
		if (vm_severe_waiters != 0) {
			vm_severe_waiters = 0;
			wakeup(&vm_severe_domains);
		}
	}

	/*
	 * If pageout daemon needs pages, then tell it that there are
	 * some free.
	 */
	if (vmd->vmd_pageout_pages_needed &&
	    vmd->vmd_free_count >= vmd->vmd_pageout_free_min) {
		wakeup(&vmd->vmd_pageout_pages_needed);
		vmd->vmd_pageout_pages_needed = 0;
	}

	/* See comments in vm_wait_doms(). */
	if (vm_pageproc_waiters) {
		vm_pageproc_waiters = 0;
		wakeup(&vm_pageproc_waiters);
	}
	mtx_unlock(&vm_domainset_lock);
}

/*
 * Wait for free pages to exceed the min threshold globally.
 */
void
vm_wait_min(void)
{

	mtx_lock(&vm_domainset_lock);
	while (vm_page_count_min()) {
		vm_min_waiters++;
		msleep(&vm_min_domains, &vm_domainset_lock, PVM, "vmwait", 0);
	}
	mtx_unlock(&vm_domainset_lock);
}

/*
 * Wait for free pages to exceed the severe threshold globally.
 */
void
vm_wait_severe(void)
{

	mtx_lock(&vm_domainset_lock);
	while (vm_page_count_severe()) {
		vm_severe_waiters++;
		msleep(&vm_severe_domains, &vm_domainset_lock, PVM,
		    "vmwait", 0);
	}
	mtx_unlock(&vm_domainset_lock);
}

u_int
vm_wait_count(void)
{

	return (vm_severe_waiters + vm_min_waiters + vm_pageproc_waiters);
}

void
vm_wait_doms(const domainset_t *wdoms)
{

	/*
	 * We use racey wakeup synchronization to avoid expensive global
	 * locking for the pageproc when sleeping with a non-specific vm_wait.
	 * To handle this, we only sleep for one tick in this instance.  It
	 * is expected that most allocations for the pageproc will come from
	 * kmem or vm_page_grab* which will use the more specific and
	 * race-free vm_wait_domain().
	 */
	if (curproc == pageproc) {
		mtx_lock(&vm_domainset_lock);
		vm_pageproc_waiters++;
		msleep(&vm_pageproc_waiters, &vm_domainset_lock, PVM | PDROP,
		    "pageprocwait", 1);
	} else {
		/*
		 * XXX Ideally we would wait only until the allocation could
		 * be satisfied.  This condition can cause new allocators to
		 * consume all freed pages while old allocators wait.
		 */
		mtx_lock(&vm_domainset_lock);
		if (vm_page_count_min_set(wdoms)) {
			vm_min_waiters++;
			msleep(&vm_min_domains, &vm_domainset_lock,
			    PVM | PDROP, "vmwait", 0);
		} else
			mtx_unlock(&vm_domainset_lock);
	}
}

/*
 *	vm_wait_domain:
 *
 *	Sleep until free pages are available for allocation.
 *	- Called in various places after failed memory allocations.
 */
void
vm_wait_domain(int domain)
{
	struct vm_domain *vmd;
	domainset_t wdom;

	vmd = VM_DOMAIN(domain);
	vm_domain_free_assert_unlocked(vmd);

	if (curproc == pageproc) {
		mtx_lock(&vm_domainset_lock);
		if (vmd->vmd_free_count < vmd->vmd_pageout_free_min) {
			vmd->vmd_pageout_pages_needed = 1;
			msleep(&vmd->vmd_pageout_pages_needed,
			    &vm_domainset_lock, PDROP | PSWP, "VMWait", 0);
		} else
			mtx_unlock(&vm_domainset_lock);
	} else {
		if (pageproc == NULL)
			panic("vm_wait in early boot");
		DOMAINSET_ZERO(&wdom);
		DOMAINSET_SET(vmd->vmd_domain, &wdom);
		vm_wait_doms(&wdom);
	}
}

/*
 *	vm_wait:
 *
 *	Sleep until free pages are available for allocation in the
 *	affinity domains of the obj.  If obj is NULL, the domain set
 *	for the calling thread is used.
 *	Called in various places after failed memory allocations.
 */
void
vm_wait(vm_object_t obj)
{
	struct domainset *d;

	d = NULL;

	/*
	 * Carefully fetch pointers only once: the struct domainset
	 * itself is ummutable but the pointer might change.
	 */
	if (obj != NULL)
		d = obj->domain.dr_policy;
	if (d == NULL)
		d = curthread->td_domain.dr_policy;

	vm_wait_doms(&d->ds_mask);
}

/*
 *	vm_domain_alloc_fail:
 *
 *	Called when a page allocation function fails.  Informs the
 *	pagedaemon and performs the requested wait.  Requires the
 *	domain_free and object lock on entry.  Returns with the
 *	object lock held and free lock released.  Returns an error when
 *	retry is necessary.
 *
 */
static int
vm_domain_alloc_fail(struct vm_domain *vmd, vm_object_t object, int req)
{

	vm_domain_free_assert_unlocked(vmd);

	atomic_add_int(&vmd->vmd_pageout_deficit,
	    max((u_int)req >> VM_ALLOC_COUNT_SHIFT, 1));
	if (req & (VM_ALLOC_WAITOK | VM_ALLOC_WAITFAIL)) {
		if (object != NULL) 
			VM_OBJECT_WUNLOCK(object);
		vm_wait_domain(vmd->vmd_domain);
		if (object != NULL) 
			VM_OBJECT_WLOCK(object);
		if (req & VM_ALLOC_WAITOK)
			return (EAGAIN);
	}

	return (0);
}

/*
 *	vm_waitpfault:
 *
 *	Sleep until free pages are available for allocation.
 *	- Called only in vm_fault so that processes page faulting
 *	  can be easily tracked.
 *	- Sleeps at a lower priority than vm_wait() so that vm_wait()ing
 *	  processes will be able to grab memory first.  Do not change
 *	  this balance without careful testing first.
 */
void
vm_waitpfault(struct domainset *dset)
{

	/*
	 * XXX Ideally we would wait only until the allocation could
	 * be satisfied.  This condition can cause new allocators to
	 * consume all freed pages while old allocators wait.
	 */
	mtx_lock(&vm_domainset_lock);
	if (vm_page_count_min_set(&dset->ds_mask)) {
		vm_min_waiters++;
		msleep(&vm_min_domains, &vm_domainset_lock, PUSER | PDROP,
		    "pfault", 0);
	} else
		mtx_unlock(&vm_domainset_lock);
}

struct vm_pagequeue *
vm_page_pagequeue(vm_page_t m)
{

	return (&vm_pagequeue_domain(m)->vmd_pagequeues[m->queue]);
}

static struct mtx *
vm_page_pagequeue_lockptr(vm_page_t m)
{
	uint8_t queue;

	if ((queue = atomic_load_8(&m->queue)) == PQ_NONE)
		return (NULL);
	return (&vm_pagequeue_domain(m)->vmd_pagequeues[queue].pq_mutex);
}

static inline void
vm_pqbatch_process_page(struct vm_pagequeue *pq, vm_page_t m)
{
	struct vm_domain *vmd;
	uint8_t qflags;

	CRITICAL_ASSERT(curthread);
	vm_pagequeue_assert_locked(pq);

	/*
	 * The page daemon is allowed to set m->queue = PQ_NONE without
	 * the page queue lock held.  In this case it is about to free the page,
	 * which must not have any queue state.
	 */
	qflags = atomic_load_8(&m->aflags) & PGA_QUEUE_STATE_MASK;
	KASSERT(pq == vm_page_pagequeue(m) || qflags == 0,
	    ("page %p doesn't belong to queue %p but has queue state %#x",
	    m, pq, qflags));

	if ((qflags & PGA_DEQUEUE) != 0) {
		if (__predict_true((qflags & PGA_ENQUEUED) != 0)) {
			TAILQ_REMOVE(&pq->pq_pl, m, plinks.q);
			vm_pagequeue_cnt_dec(pq);
		}
		vm_page_dequeue_complete(m);
	} else if ((qflags & (PGA_REQUEUE | PGA_REQUEUE_HEAD)) != 0) {
		if ((qflags & PGA_ENQUEUED) != 0)
			TAILQ_REMOVE(&pq->pq_pl, m, plinks.q);
		else {
			vm_pagequeue_cnt_inc(pq);
			vm_page_aflag_set(m, PGA_ENQUEUED);
		}
		if ((qflags & PGA_REQUEUE_HEAD) != 0) {
			KASSERT(m->queue == PQ_INACTIVE,
			    ("head enqueue not supported for page %p", m));
			vmd = vm_pagequeue_domain(m);
			TAILQ_INSERT_BEFORE(&vmd->vmd_inacthead, m, plinks.q);
		} else
			TAILQ_INSERT_TAIL(&pq->pq_pl, m, plinks.q);

		/*
		 * PGA_REQUEUE and PGA_REQUEUE_HEAD must be cleared after
		 * setting PGA_ENQUEUED in order to synchronize with the
		 * page daemon.
		 */
		vm_page_aflag_clear(m, PGA_REQUEUE | PGA_REQUEUE_HEAD);
	}
}

static void
vm_pqbatch_process(struct vm_pagequeue *pq, struct vm_batchqueue *bq,
    uint8_t queue)
{
	vm_page_t m;
	int i;

	for (i = 0; i < bq->bq_cnt; i++) {
		m = bq->bq_pa[i];
		if (__predict_false(m->queue != queue))
			continue;
		vm_pqbatch_process_page(pq, m);
	}
	vm_batchqueue_init(bq);
}

static void
vm_pqbatch_submit_page(vm_page_t m, uint8_t queue)
{
	struct vm_batchqueue *bq;
	struct vm_pagequeue *pq;
	int domain;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("page %p is unmanaged", m));
	KASSERT(mtx_owned(vm_page_lockptr(m)) ||
	    (m->object == NULL && (m->aflags & PGA_DEQUEUE) != 0),
	    ("missing synchronization for page %p", m));
	KASSERT(queue < PQ_COUNT, ("invalid queue %d", queue));

	domain = vm_phys_domain(m);
	pq = &vm_pagequeue_domain(m)->vmd_pagequeues[queue];

	critical_enter();
	bq = DPCPU_PTR(pqbatch[domain][queue]);
	if (vm_batchqueue_insert(bq, m)) {
		critical_exit();
		return;
	}
	if (!vm_pagequeue_trylock(pq)) {
		critical_exit();
		vm_pagequeue_lock(pq);
		critical_enter();
		bq = DPCPU_PTR(pqbatch[domain][queue]);
	}
	vm_pqbatch_process(pq, bq, queue);

	/*
	 * The page may have been logically dequeued before we acquired the
	 * page queue lock.  In this case, since we either hold the page lock
	 * or the page is being freed, a different thread cannot be concurrently
	 * enqueuing the page.
	 */
	if (__predict_true(m->queue == queue))
		vm_pqbatch_process_page(pq, m);
	else {
		KASSERT(m->queue == PQ_NONE,
		    ("invalid queue transition for page %p", m));
		KASSERT((m->aflags & PGA_ENQUEUED) == 0,
		    ("page %p is enqueued with invalid queue index", m));
		vm_page_aflag_clear(m, PGA_QUEUE_STATE_MASK);
	}
	vm_pagequeue_unlock(pq);
	critical_exit();
}

/*
 *	vm_page_drain_pqbatch:		[ internal use only ]
 *
 *	Force all per-CPU page queue batch queues to be drained.  This is
 *	intended for use in severe memory shortages, to ensure that pages
 *	do not remain stuck in the batch queues.
 */
void
vm_page_drain_pqbatch(void)
{
	struct thread *td;
	struct vm_domain *vmd;
	struct vm_pagequeue *pq;
	int cpu, domain, queue;

	td = curthread;
	CPU_FOREACH(cpu) {
		thread_lock(td);
		sched_bind(td, cpu);
		thread_unlock(td);

		for (domain = 0; domain < vm_ndomains; domain++) {
			vmd = VM_DOMAIN(domain);
			for (queue = 0; queue < PQ_COUNT; queue++) {
				pq = &vmd->vmd_pagequeues[queue];
				vm_pagequeue_lock(pq);
				critical_enter();
				vm_pqbatch_process(pq,
				    DPCPU_PTR(pqbatch[domain][queue]), queue);
				critical_exit();
				vm_pagequeue_unlock(pq);
			}
		}
	}
	thread_lock(td);
	sched_unbind(td);
	thread_unlock(td);
}

/*
 * Complete the logical removal of a page from a page queue.  We must be
 * careful to synchronize with the page daemon, which may be concurrently
 * examining the page with only the page lock held.  The page must not be
 * in a state where it appears to be logically enqueued.
 */
static void
vm_page_dequeue_complete(vm_page_t m)
{

	m->queue = PQ_NONE;
	atomic_thread_fence_rel();
	vm_page_aflag_clear(m, PGA_QUEUE_STATE_MASK);
}

/*
 *	vm_page_dequeue_deferred:	[ internal use only ]
 *
 *	Request removal of the given page from its current page
 *	queue.  Physical removal from the queue may be deferred
 *	indefinitely.
 *
 *	The page must be locked.
 */
void
vm_page_dequeue_deferred(vm_page_t m)
{
	uint8_t queue;

	vm_page_assert_locked(m);

	if ((queue = vm_page_queue(m)) == PQ_NONE)
		return;
	vm_page_aflag_set(m, PGA_DEQUEUE);
	vm_pqbatch_submit_page(m, queue);
}

/*
 * A variant of vm_page_dequeue_deferred() that does not assert the page
 * lock and is only to be called from vm_page_free_prep().  It is just an
 * open-coded implementation of vm_page_dequeue_deferred().  Because the
 * page is being freed, we can assume that nothing else is scheduling queue
 * operations on this page, so we get for free the mutual exclusion that
 * is otherwise provided by the page lock.
 */
static void
vm_page_dequeue_deferred_free(vm_page_t m)
{
	uint8_t queue;

	KASSERT(m->object == NULL, ("page %p has an object reference", m));

	if ((m->aflags & PGA_DEQUEUE) != 0)
		return;
	atomic_thread_fence_acq();
	if ((queue = m->queue) == PQ_NONE)
		return;
	vm_page_aflag_set(m, PGA_DEQUEUE);
	vm_pqbatch_submit_page(m, queue);
}

/*
 *	vm_page_dequeue:
 *
 *	Remove the page from whichever page queue it's in, if any.
 *	The page must either be locked or unallocated.  This constraint
 *	ensures that the queue state of the page will remain consistent
 *	after this function returns.
 */
void
vm_page_dequeue(vm_page_t m)
{
	struct mtx *lock, *lock1;
	struct vm_pagequeue *pq;
	uint8_t aflags;

	KASSERT(mtx_owned(vm_page_lockptr(m)) || m->order == VM_NFREEORDER,
	    ("page %p is allocated and unlocked", m));

	for (;;) {
		lock = vm_page_pagequeue_lockptr(m);
		if (lock == NULL) {
			/*
			 * A thread may be concurrently executing
			 * vm_page_dequeue_complete().  Ensure that all queue
			 * state is cleared before we return.
			 */
			aflags = atomic_load_8(&m->aflags);
			if ((aflags & PGA_QUEUE_STATE_MASK) == 0)
				return;
			KASSERT((aflags & PGA_DEQUEUE) != 0,
			    ("page %p has unexpected queue state flags %#x",
			    m, aflags));

			/*
			 * Busy wait until the thread updating queue state is
			 * finished.  Such a thread must be executing in a
			 * critical section.
			 */
			cpu_spinwait();
			continue;
		}
		mtx_lock(lock);
		if ((lock1 = vm_page_pagequeue_lockptr(m)) == lock)
			break;
		mtx_unlock(lock);
		lock = lock1;
	}
	KASSERT(lock == vm_page_pagequeue_lockptr(m),
	    ("%s: page %p migrated directly between queues", __func__, m));
	KASSERT((m->aflags & PGA_DEQUEUE) != 0 ||
	    mtx_owned(vm_page_lockptr(m)),
	    ("%s: queued unlocked page %p", __func__, m));

	if ((m->aflags & PGA_ENQUEUED) != 0) {
		pq = vm_page_pagequeue(m);
		TAILQ_REMOVE(&pq->pq_pl, m, plinks.q);
		vm_pagequeue_cnt_dec(pq);
	}
	vm_page_dequeue_complete(m);
	mtx_unlock(lock);
}

/*
 * Schedule the given page for insertion into the specified page queue.
 * Physical insertion of the page may be deferred indefinitely.
 */
static void
vm_page_enqueue(vm_page_t m, uint8_t queue)
{

	vm_page_assert_locked(m);
	KASSERT(m->queue == PQ_NONE && (m->aflags & PGA_QUEUE_STATE_MASK) == 0,
	    ("%s: page %p is already enqueued", __func__, m));

	m->queue = queue;
	if ((m->aflags & PGA_REQUEUE) == 0)
		vm_page_aflag_set(m, PGA_REQUEUE);
	vm_pqbatch_submit_page(m, queue);
}

/*
 *	vm_page_requeue:		[ internal use only ]
 *
 *	Schedule a requeue of the given page.
 *
 *	The page must be locked.
 */
void
vm_page_requeue(vm_page_t m)
{

	vm_page_assert_locked(m);
	KASSERT(vm_page_queue(m) != PQ_NONE,
	    ("%s: page %p is not logically enqueued", __func__, m));

	if ((m->aflags & PGA_REQUEUE) == 0)
		vm_page_aflag_set(m, PGA_REQUEUE);
	vm_pqbatch_submit_page(m, atomic_load_8(&m->queue));
}

/*
 *	vm_page_activate:
 *
 *	Put the specified page on the active list (if appropriate).
 *	Ensure that act_count is at least ACT_INIT but do not otherwise
 *	mess with it.
 *
 *	The page must be locked.
 */
void
vm_page_activate(vm_page_t m)
{

	vm_page_assert_locked(m);

	if (m->wire_count > 0 || (m->oflags & VPO_UNMANAGED) != 0)
		return;
	if (vm_page_queue(m) == PQ_ACTIVE) {
		if (m->act_count < ACT_INIT)
			m->act_count = ACT_INIT;
		return;
	}

	vm_page_dequeue(m);
	if (m->act_count < ACT_INIT)
		m->act_count = ACT_INIT;
	vm_page_enqueue(m, PQ_ACTIVE);
}

/*
 *	vm_page_free_prep:
 *
 *	Prepares the given page to be put on the free list,
 *	disassociating it from any VM object. The caller may return
 *	the page to the free list only if this function returns true.
 *
 *	The object must be locked.  The page must be locked if it is
 *	managed.
 */
bool
vm_page_free_prep(vm_page_t m)
{

#if defined(DIAGNOSTIC) && defined(PHYS_TO_DMAP)
	if (PMAP_HAS_DMAP && (m->flags & PG_ZERO) != 0) {
		uint64_t *p;
		int i;
		p = (uint64_t *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));
		for (i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++, p++)
			KASSERT(*p == 0, ("vm_page_free_prep %p PG_ZERO %d %jx",
			    m, i, (uintmax_t)*p));
	}
#endif
	if ((m->oflags & VPO_UNMANAGED) == 0) {
		vm_page_lock_assert(m, MA_OWNED);
		KASSERT(!pmap_page_is_mapped(m),
		    ("vm_page_free_prep: freeing mapped page %p", m));
	} else
		KASSERT(m->queue == PQ_NONE,
		    ("vm_page_free_prep: unmanaged page %p is queued", m));
	VM_CNT_INC(v_tfree);

	if (vm_page_sbusied(m))
		panic("vm_page_free_prep: freeing busy page %p", m);

	vm_page_remove(m);

	/*
	 * If fictitious remove object association and
	 * return.
	 */
	if ((m->flags & PG_FICTITIOUS) != 0) {
		KASSERT(m->wire_count == 1,
		    ("fictitious page %p is not wired", m));
		KASSERT(m->queue == PQ_NONE,
		    ("fictitious page %p is queued", m));
		return (false);
	}

	/*
	 * Pages need not be dequeued before they are returned to the physical
	 * memory allocator, but they must at least be marked for a deferred
	 * dequeue.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0)
		vm_page_dequeue_deferred_free(m);

	m->valid = 0;
	vm_page_undirty(m);

	if (m->wire_count != 0)
		panic("vm_page_free_prep: freeing wired page %p", m);
	if (m->hold_count != 0) {
		m->flags &= ~PG_ZERO;
		KASSERT((m->flags & PG_UNHOLDFREE) == 0,
		    ("vm_page_free_prep: freeing PG_UNHOLDFREE page %p", m));
		m->flags |= PG_UNHOLDFREE;
		return (false);
	}

	/*
	 * Restore the default memory attribute to the page.
	 */
	if (pmap_page_get_memattr(m) != VM_MEMATTR_DEFAULT)
		pmap_page_set_memattr(m, VM_MEMATTR_DEFAULT);

#if VM_NRESERVLEVEL > 0
	if (vm_reserv_free_page(m))
		return (false);
#endif

	return (true);
}

/*
 *	vm_page_free_toq:
 *
 *	Returns the given page to the free list, disassociating it
 *	from any VM object.
 *
 *	The object must be locked.  The page must be locked if it is
 *	managed.
 */
void
vm_page_free_toq(vm_page_t m)
{
	struct vm_domain *vmd;

	if (!vm_page_free_prep(m))
		return;

	vmd = vm_pagequeue_domain(m);
	if (m->pool == VM_FREEPOOL_DEFAULT && vmd->vmd_pgcache != NULL) {
		uma_zfree(vmd->vmd_pgcache, m);
		return;
	}
	vm_domain_free_lock(vmd);
	vm_phys_free_pages(m, 0);
	vm_domain_free_unlock(vmd);
	vm_domain_freecnt_inc(vmd, 1);
}

/*
 *	vm_page_free_pages_toq:
 *
 *	Returns a list of pages to the free list, disassociating it
 *	from any VM object.  In other words, this is equivalent to
 *	calling vm_page_free_toq() for each page of a list of VM objects.
 *
 *	The objects must be locked.  The pages must be locked if it is
 *	managed.
 */
void
vm_page_free_pages_toq(struct spglist *free, bool update_wire_count)
{
	vm_page_t m;
	int count;

	if (SLIST_EMPTY(free))
		return;

	count = 0;
	while ((m = SLIST_FIRST(free)) != NULL) {
		count++;
		SLIST_REMOVE_HEAD(free, plinks.s.ss);
		vm_page_free_toq(m);
	}

	if (update_wire_count)
		vm_wire_sub(count);
}

/*
 *	vm_page_wire:
 *
 * Mark this page as wired down.  If the page is fictitious, then
 * its wire count must remain one.
 *
 * The page must be locked.
 */
void
vm_page_wire(vm_page_t m)
{

	vm_page_assert_locked(m);
	if ((m->flags & PG_FICTITIOUS) != 0) {
		KASSERT(m->wire_count == 1,
		    ("vm_page_wire: fictitious page %p's wire count isn't one",
		    m));
		return;
	}
	if (m->wire_count == 0) {
		KASSERT((m->oflags & VPO_UNMANAGED) == 0 ||
		    m->queue == PQ_NONE,
		    ("vm_page_wire: unmanaged page %p is queued", m));
		vm_wire_add(1);
	}
	m->wire_count++;
	KASSERT(m->wire_count != 0, ("vm_page_wire: wire_count overflow m=%p", m));
}

/*
 * vm_page_unwire:
 *
 * Release one wiring of the specified page, potentially allowing it to be
 * paged out.  Returns TRUE if the number of wirings transitions to zero and
 * FALSE otherwise.
 *
 * Only managed pages belonging to an object can be paged out.  If the number
 * of wirings transitions to zero and the page is eligible for page out, then
 * the page is added to the specified paging queue (unless PQ_NONE is
 * specified, in which case the page is dequeued if it belongs to a paging
 * queue).
 *
 * If a page is fictitious, then its wire count must always be one.
 *
 * A managed page must be locked.
 */
bool
vm_page_unwire(vm_page_t m, uint8_t queue)
{
	bool unwired;

	KASSERT(queue < PQ_COUNT || queue == PQ_NONE,
	    ("vm_page_unwire: invalid queue %u request for page %p",
	    queue, m));
	if ((m->oflags & VPO_UNMANAGED) == 0)
		vm_page_assert_locked(m);

	unwired = vm_page_unwire_noq(m);
	if (!unwired || (m->oflags & VPO_UNMANAGED) != 0 || m->object == NULL)
		return (unwired);

	if (vm_page_queue(m) == queue) {
		if (queue == PQ_ACTIVE)
			vm_page_reference(m);
		else if (queue != PQ_NONE)
			vm_page_requeue(m);
	} else {
		vm_page_dequeue(m);
		if (queue != PQ_NONE) {
			vm_page_enqueue(m, queue);
			if (queue == PQ_ACTIVE)
				/* Initialize act_count. */
				vm_page_activate(m);
		}
	}
	return (unwired);
}

/*
 *
 * vm_page_unwire_noq:
 *
 * Unwire a page without (re-)inserting it into a page queue.  It is up
 * to the caller to enqueue, requeue, or free the page as appropriate.
 * In most cases, vm_page_unwire() should be used instead.
 */
bool
vm_page_unwire_noq(vm_page_t m)
{

	if ((m->oflags & VPO_UNMANAGED) == 0)
		vm_page_assert_locked(m);
	if ((m->flags & PG_FICTITIOUS) != 0) {
		KASSERT(m->wire_count == 1,
	    ("vm_page_unwire: fictitious page %p's wire count isn't one", m));
		return (false);
	}
	if (m->wire_count == 0)
		panic("vm_page_unwire: page %p's wire count is zero", m);
	m->wire_count--;
	if (m->wire_count == 0) {
		vm_wire_sub(1);
		return (true);
	} else
		return (false);
}

/*
 * Move the specified page to the tail of the inactive queue, or requeue
 * the page if it is already in the inactive queue.
 *
 * The page must be locked.
 */
void
vm_page_deactivate(vm_page_t m)
{

	vm_page_assert_locked(m);

	if (m->wire_count > 0 || (m->oflags & VPO_UNMANAGED) != 0)
		return;

	if (!vm_page_inactive(m)) {
		vm_page_dequeue(m);
		vm_page_enqueue(m, PQ_INACTIVE);
	} else
		vm_page_requeue(m);
}

/*
 * Move the specified page close to the head of the inactive queue,
 * bypassing LRU.  A marker page is used to maintain FIFO ordering.
 * As with regular enqueues, we use a per-CPU batch queue to reduce
 * contention on the page queue lock.
 *
 * The page must be locked.
 */
void
vm_page_deactivate_noreuse(vm_page_t m)
{

	vm_page_assert_locked(m);

	if (m->wire_count > 0 || (m->oflags & VPO_UNMANAGED) != 0)
		return;

	if (!vm_page_inactive(m)) {
		vm_page_dequeue(m);
		m->queue = PQ_INACTIVE;
	}
	if ((m->aflags & PGA_REQUEUE_HEAD) == 0)
		vm_page_aflag_set(m, PGA_REQUEUE_HEAD);
	vm_pqbatch_submit_page(m, PQ_INACTIVE);
}

/*
 * vm_page_launder
 *
 * 	Put a page in the laundry, or requeue it if it is already there.
 */
void
vm_page_launder(vm_page_t m)
{

	vm_page_assert_locked(m);
	if (m->wire_count > 0 || (m->oflags & VPO_UNMANAGED) != 0)
		return;

	if (vm_page_in_laundry(m))
		vm_page_requeue(m);
	else {
		vm_page_dequeue(m);
		vm_page_enqueue(m, PQ_LAUNDRY);
	}
}

/*
 * vm_page_unswappable
 *
 *	Put a page in the PQ_UNSWAPPABLE holding queue.
 */
void
vm_page_unswappable(vm_page_t m)
{

	vm_page_assert_locked(m);
	KASSERT(m->wire_count == 0 && (m->oflags & VPO_UNMANAGED) == 0,
	    ("page %p already unswappable", m));

	vm_page_dequeue(m);
	vm_page_enqueue(m, PQ_UNSWAPPABLE);
}

/*
 * Attempt to free the page.  If it cannot be freed, do nothing.  Returns true
 * if the page is freed and false otherwise.
 *
 * The page must be managed.  The page and its containing object must be
 * locked.
 */
bool
vm_page_try_to_free(vm_page_t m)
{

	vm_page_assert_locked(m);
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT((m->oflags & VPO_UNMANAGED) == 0, ("page %p is unmanaged", m));
	if (m->dirty != 0 || vm_page_held(m) || vm_page_busied(m))
		return (false);
	if (m->object->ref_count != 0) {
		pmap_remove_all(m);
		if (m->dirty != 0)
			return (false);
	}
	vm_page_free(m);
	return (true);
}

/*
 * vm_page_advise
 *
 * 	Apply the specified advice to the given page.
 *
 *	The object and page must be locked.
 */
void
vm_page_advise(vm_page_t m, int advice)
{

	vm_page_assert_locked(m);
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (advice == MADV_FREE)
		/*
		 * Mark the page clean.  This will allow the page to be freed
		 * without first paging it out.  MADV_FREE pages are often
		 * quickly reused by malloc(3), so we do not do anything that
		 * would result in a page fault on a later access.
		 */
		vm_page_undirty(m);
	else if (advice != MADV_DONTNEED) {
		if (advice == MADV_WILLNEED)
			vm_page_activate(m);
		return;
	}

	/*
	 * Clear any references to the page.  Otherwise, the page daemon will
	 * immediately reactivate the page.
	 */
	vm_page_aflag_clear(m, PGA_REFERENCED);

	if (advice != MADV_FREE && m->dirty == 0 && pmap_is_modified(m))
		vm_page_dirty(m);

	/*
	 * Place clean pages near the head of the inactive queue rather than
	 * the tail, thus defeating the queue's LRU operation and ensuring that
	 * the page will be reused quickly.  Dirty pages not already in the
	 * laundry are moved there.
	 */
	if (m->dirty == 0)
		vm_page_deactivate_noreuse(m);
	else if (!vm_page_in_laundry(m))
		vm_page_launder(m);
}

/*
 * Grab a page, waiting until we are waken up due to the page
 * changing state.  We keep on waiting, if the page continues
 * to be in the object.  If the page doesn't exist, first allocate it
 * and then conditionally zero it.
 *
 * This routine may sleep.
 *
 * The object must be locked on entry.  The lock will, however, be released
 * and reacquired if the routine sleeps.
 */
vm_page_t
vm_page_grab(vm_object_t object, vm_pindex_t pindex, int allocflags)
{
	vm_page_t m;
	int sleep;
	int pflags;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT((allocflags & VM_ALLOC_SBUSY) == 0 ||
	    (allocflags & VM_ALLOC_IGN_SBUSY) != 0,
	    ("vm_page_grab: VM_ALLOC_SBUSY/VM_ALLOC_IGN_SBUSY mismatch"));
	pflags = allocflags &
	    ~(VM_ALLOC_NOWAIT | VM_ALLOC_WAITOK | VM_ALLOC_WAITFAIL);
	if ((allocflags & VM_ALLOC_NOWAIT) == 0)
		pflags |= VM_ALLOC_WAITFAIL;
retrylookup:
	if ((m = vm_page_lookup(object, pindex)) != NULL) {
		sleep = (allocflags & VM_ALLOC_IGN_SBUSY) != 0 ?
		    vm_page_xbusied(m) : vm_page_busied(m);
		if (sleep) {
			if ((allocflags & VM_ALLOC_NOWAIT) != 0)
				return (NULL);
			/*
			 * Reference the page before unlocking and
			 * sleeping so that the page daemon is less
			 * likely to reclaim it.
			 */
			vm_page_aflag_set(m, PGA_REFERENCED);
			vm_page_lock(m);
			VM_OBJECT_WUNLOCK(object);
			vm_page_busy_sleep(m, "pgrbwt", (allocflags &
			    VM_ALLOC_IGN_SBUSY) != 0);
			VM_OBJECT_WLOCK(object);
			goto retrylookup;
		} else {
			if ((allocflags & VM_ALLOC_WIRED) != 0) {
				vm_page_lock(m);
				vm_page_wire(m);
				vm_page_unlock(m);
			}
			if ((allocflags &
			    (VM_ALLOC_NOBUSY | VM_ALLOC_SBUSY)) == 0)
				vm_page_xbusy(m);
			if ((allocflags & VM_ALLOC_SBUSY) != 0)
				vm_page_sbusy(m);
			return (m);
		}
	}
	m = vm_page_alloc(object, pindex, pflags);
	if (m == NULL) {
		if ((allocflags & VM_ALLOC_NOWAIT) != 0)
			return (NULL);
		goto retrylookup;
	}
	if (allocflags & VM_ALLOC_ZERO && (m->flags & PG_ZERO) == 0)
		pmap_zero_page(m);
	return (m);
}

/*
 * Return the specified range of pages from the given object.  For each
 * page offset within the range, if a page already exists within the object
 * at that offset and it is busy, then wait for it to change state.  If,
 * instead, the page doesn't exist, then allocate it.
 *
 * The caller must always specify an allocation class.
 *
 * allocation classes:
 *	VM_ALLOC_NORMAL		normal process request
 *	VM_ALLOC_SYSTEM		system *really* needs the pages
 *
 * The caller must always specify that the pages are to be busied and/or
 * wired.
 *
 * optional allocation flags:
 *	VM_ALLOC_IGN_SBUSY	do not sleep on soft busy pages
 *	VM_ALLOC_NOBUSY		do not exclusive busy the page
 *	VM_ALLOC_NOWAIT		do not sleep
 *	VM_ALLOC_SBUSY		set page to sbusy state
 *	VM_ALLOC_WIRED		wire the pages
 *	VM_ALLOC_ZERO		zero and validate any invalid pages
 *
 * If VM_ALLOC_NOWAIT is not specified, this routine may sleep.  Otherwise, it
 * may return a partial prefix of the requested range.
 */
int
vm_page_grab_pages(vm_object_t object, vm_pindex_t pindex, int allocflags,
    vm_page_t *ma, int count)
{
	vm_page_t m, mpred;
	int pflags;
	int i;
	bool sleep;

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(((u_int)allocflags >> VM_ALLOC_COUNT_SHIFT) == 0,
	    ("vm_page_grap_pages: VM_ALLOC_COUNT() is not allowed"));
	KASSERT((allocflags & VM_ALLOC_NOBUSY) == 0 ||
	    (allocflags & VM_ALLOC_WIRED) != 0,
	    ("vm_page_grab_pages: the pages must be busied or wired"));
	KASSERT((allocflags & VM_ALLOC_SBUSY) == 0 ||
	    (allocflags & VM_ALLOC_IGN_SBUSY) != 0,
	    ("vm_page_grab_pages: VM_ALLOC_SBUSY/IGN_SBUSY mismatch"));
	if (count == 0)
		return (0);
	pflags = allocflags & ~(VM_ALLOC_NOWAIT | VM_ALLOC_WAITOK |
	    VM_ALLOC_WAITFAIL | VM_ALLOC_IGN_SBUSY);
	if ((allocflags & VM_ALLOC_NOWAIT) == 0)
		pflags |= VM_ALLOC_WAITFAIL;
	i = 0;
retrylookup:
	m = vm_radix_lookup_le(&object->rtree, pindex + i);
	if (m == NULL || m->pindex != pindex + i) {
		mpred = m;
		m = NULL;
	} else
		mpred = TAILQ_PREV(m, pglist, listq);
	for (; i < count; i++) {
		if (m != NULL) {
			sleep = (allocflags & VM_ALLOC_IGN_SBUSY) != 0 ?
			    vm_page_xbusied(m) : vm_page_busied(m);
			if (sleep) {
				if ((allocflags & VM_ALLOC_NOWAIT) != 0)
					break;
				/*
				 * Reference the page before unlocking and
				 * sleeping so that the page daemon is less
				 * likely to reclaim it.
				 */
				vm_page_aflag_set(m, PGA_REFERENCED);
				vm_page_lock(m);
				VM_OBJECT_WUNLOCK(object);
				vm_page_busy_sleep(m, "grbmaw", (allocflags &
				    VM_ALLOC_IGN_SBUSY) != 0);
				VM_OBJECT_WLOCK(object);
				goto retrylookup;
			}
			if ((allocflags & VM_ALLOC_WIRED) != 0) {
				vm_page_lock(m);
				vm_page_wire(m);
				vm_page_unlock(m);
			}
			if ((allocflags & (VM_ALLOC_NOBUSY |
			    VM_ALLOC_SBUSY)) == 0)
				vm_page_xbusy(m);
			if ((allocflags & VM_ALLOC_SBUSY) != 0)
				vm_page_sbusy(m);
		} else {
			m = vm_page_alloc_after(object, pindex + i,
			    pflags | VM_ALLOC_COUNT(count - i), mpred);
			if (m == NULL) {
				if ((allocflags & VM_ALLOC_NOWAIT) != 0)
					break;
				goto retrylookup;
			}
		}
		if (m->valid == 0 && (allocflags & VM_ALLOC_ZERO) != 0) {
			if ((m->flags & PG_ZERO) == 0)
				pmap_zero_page(m);
			m->valid = VM_PAGE_BITS_ALL;
		}
		ma[i] = mpred = m;
		m = vm_page_next(m);
	}
	return (i);
}

/*
 * Mapping function for valid or dirty bits in a page.
 *
 * Inputs are required to range within a page.
 */
vm_page_bits_t
vm_page_bits(int base, int size)
{
	int first_bit;
	int last_bit;

	KASSERT(
	    base + size <= PAGE_SIZE,
	    ("vm_page_bits: illegal base/size %d/%d", base, size)
	);

	if (size == 0)		/* handle degenerate case */
		return (0);

	first_bit = base >> DEV_BSHIFT;
	last_bit = (base + size - 1) >> DEV_BSHIFT;

	return (((vm_page_bits_t)2 << last_bit) -
	    ((vm_page_bits_t)1 << first_bit));
}

/*
 *	vm_page_set_valid_range:
 *
 *	Sets portions of a page valid.  The arguments are expected
 *	to be DEV_BSIZE aligned but if they aren't the bitmap is inclusive
 *	of any partial chunks touched by the range.  The invalid portion of
 *	such chunks will be zeroed.
 *
 *	(base + size) must be less then or equal to PAGE_SIZE.
 */
void
vm_page_set_valid_range(vm_page_t m, int base, int size)
{
	int endoff, frag;

	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (size == 0)	/* handle degenerate case */
		return;

	/*
	 * If the base is not DEV_BSIZE aligned and the valid
	 * bit is clear, we have to zero out a portion of the
	 * first block.
	 */
	if ((frag = rounddown2(base, DEV_BSIZE)) != base &&
	    (m->valid & (1 << (base >> DEV_BSHIFT))) == 0)
		pmap_zero_page_area(m, frag, base - frag);

	/*
	 * If the ending offset is not DEV_BSIZE aligned and the
	 * valid bit is clear, we have to zero out a portion of
	 * the last block.
	 */
	endoff = base + size;
	if ((frag = rounddown2(endoff, DEV_BSIZE)) != endoff &&
	    (m->valid & (1 << (endoff >> DEV_BSHIFT))) == 0)
		pmap_zero_page_area(m, endoff,
		    DEV_BSIZE - (endoff & (DEV_BSIZE - 1)));

	/*
	 * Assert that no previously invalid block that is now being validated
	 * is already dirty.
	 */
	KASSERT((~m->valid & vm_page_bits(base, size) & m->dirty) == 0,
	    ("vm_page_set_valid_range: page %p is dirty", m));

	/*
	 * Set valid bits inclusive of any overlap.
	 */
	m->valid |= vm_page_bits(base, size);
}

/*
 * Clear the given bits from the specified page's dirty field.
 */
static __inline void
vm_page_clear_dirty_mask(vm_page_t m, vm_page_bits_t pagebits)
{
	uintptr_t addr;
#if PAGE_SIZE < 16384
	int shift;
#endif

	/*
	 * If the object is locked and the page is neither exclusive busy nor
	 * write mapped, then the page's dirty field cannot possibly be
	 * set by a concurrent pmap operation.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && !pmap_page_is_write_mapped(m))
		m->dirty &= ~pagebits;
	else {
		/*
		 * The pmap layer can call vm_page_dirty() without
		 * holding a distinguished lock.  The combination of
		 * the object's lock and an atomic operation suffice
		 * to guarantee consistency of the page dirty field.
		 *
		 * For PAGE_SIZE == 32768 case, compiler already
		 * properly aligns the dirty field, so no forcible
		 * alignment is needed. Only require existence of
		 * atomic_clear_64 when page size is 32768.
		 */
		addr = (uintptr_t)&m->dirty;
#if PAGE_SIZE == 32768
		atomic_clear_64((uint64_t *)addr, pagebits);
#elif PAGE_SIZE == 16384
		atomic_clear_32((uint32_t *)addr, pagebits);
#else		/* PAGE_SIZE <= 8192 */
		/*
		 * Use a trick to perform a 32-bit atomic on the
		 * containing aligned word, to not depend on the existence
		 * of atomic_clear_{8, 16}.
		 */
		shift = addr & (sizeof(uint32_t) - 1);
#if BYTE_ORDER == BIG_ENDIAN
		shift = (sizeof(uint32_t) - sizeof(m->dirty) - shift) * NBBY;
#else
		shift *= NBBY;
#endif
		addr &= ~(sizeof(uint32_t) - 1);
		atomic_clear_32((uint32_t *)addr, pagebits << shift);
#endif		/* PAGE_SIZE */
	}
}

/*
 *	vm_page_set_validclean:
 *
 *	Sets portions of a page valid and clean.  The arguments are expected
 *	to be DEV_BSIZE aligned but if they aren't the bitmap is inclusive
 *	of any partial chunks touched by the range.  The invalid portion of
 *	such chunks will be zero'd.
 *
 *	(base + size) must be less then or equal to PAGE_SIZE.
 */
void
vm_page_set_validclean(vm_page_t m, int base, int size)
{
	vm_page_bits_t oldvalid, pagebits;
	int endoff, frag;

	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (size == 0)	/* handle degenerate case */
		return;

	/*
	 * If the base is not DEV_BSIZE aligned and the valid
	 * bit is clear, we have to zero out a portion of the
	 * first block.
	 */
	if ((frag = rounddown2(base, DEV_BSIZE)) != base &&
	    (m->valid & ((vm_page_bits_t)1 << (base >> DEV_BSHIFT))) == 0)
		pmap_zero_page_area(m, frag, base - frag);

	/*
	 * If the ending offset is not DEV_BSIZE aligned and the
	 * valid bit is clear, we have to zero out a portion of
	 * the last block.
	 */
	endoff = base + size;
	if ((frag = rounddown2(endoff, DEV_BSIZE)) != endoff &&
	    (m->valid & ((vm_page_bits_t)1 << (endoff >> DEV_BSHIFT))) == 0)
		pmap_zero_page_area(m, endoff,
		    DEV_BSIZE - (endoff & (DEV_BSIZE - 1)));

	/*
	 * Set valid, clear dirty bits.  If validating the entire
	 * page we can safely clear the pmap modify bit.  We also
	 * use this opportunity to clear the VPO_NOSYNC flag.  If a process
	 * takes a write fault on a MAP_NOSYNC memory area the flag will
	 * be set again.
	 *
	 * We set valid bits inclusive of any overlap, but we can only
	 * clear dirty bits for DEV_BSIZE chunks that are fully within
	 * the range.
	 */
	oldvalid = m->valid;
	pagebits = vm_page_bits(base, size);
	m->valid |= pagebits;
#if 0	/* NOT YET */
	if ((frag = base & (DEV_BSIZE - 1)) != 0) {
		frag = DEV_BSIZE - frag;
		base += frag;
		size -= frag;
		if (size < 0)
			size = 0;
	}
	pagebits = vm_page_bits(base, size & (DEV_BSIZE - 1));
#endif
	if (base == 0 && size == PAGE_SIZE) {
		/*
		 * The page can only be modified within the pmap if it is
		 * mapped, and it can only be mapped if it was previously
		 * fully valid.
		 */
		if (oldvalid == VM_PAGE_BITS_ALL)
			/*
			 * Perform the pmap_clear_modify() first.  Otherwise,
			 * a concurrent pmap operation, such as
			 * pmap_protect(), could clear a modification in the
			 * pmap and set the dirty field on the page before
			 * pmap_clear_modify() had begun and after the dirty
			 * field was cleared here.
			 */
			pmap_clear_modify(m);
		m->dirty = 0;
		m->oflags &= ~VPO_NOSYNC;
	} else if (oldvalid != VM_PAGE_BITS_ALL)
		m->dirty &= ~pagebits;
	else
		vm_page_clear_dirty_mask(m, pagebits);
}

void
vm_page_clear_dirty(vm_page_t m, int base, int size)
{

	vm_page_clear_dirty_mask(m, vm_page_bits(base, size));
}

/*
 *	vm_page_set_invalid:
 *
 *	Invalidates DEV_BSIZE'd chunks within a page.  Both the
 *	valid and dirty bits for the effected areas are cleared.
 */
void
vm_page_set_invalid(vm_page_t m, int base, int size)
{
	vm_page_bits_t bits;
	vm_object_t object;

	object = m->object;
	VM_OBJECT_ASSERT_WLOCKED(object);
	if (object->type == OBJT_VNODE && base == 0 && IDX_TO_OFF(m->pindex) +
	    size >= object->un_pager.vnp.vnp_size)
		bits = VM_PAGE_BITS_ALL;
	else
		bits = vm_page_bits(base, size);
	if (object->ref_count != 0 && m->valid == VM_PAGE_BITS_ALL &&
	    bits != 0)
		pmap_remove_all(m);
	KASSERT((bits == 0 && m->valid == VM_PAGE_BITS_ALL) ||
	    !pmap_page_is_mapped(m),
	    ("vm_page_set_invalid: page %p is mapped", m));
	m->valid &= ~bits;
	m->dirty &= ~bits;
}

/*
 * vm_page_zero_invalid()
 *
 *	The kernel assumes that the invalid portions of a page contain
 *	garbage, but such pages can be mapped into memory by user code.
 *	When this occurs, we must zero out the non-valid portions of the
 *	page so user code sees what it expects.
 *
 *	Pages are most often semi-valid when the end of a file is mapped
 *	into memory and the file's size is not page aligned.
 */
void
vm_page_zero_invalid(vm_page_t m, boolean_t setvalid)
{
	int b;
	int i;

	VM_OBJECT_ASSERT_WLOCKED(m->object);
	/*
	 * Scan the valid bits looking for invalid sections that
	 * must be zeroed.  Invalid sub-DEV_BSIZE'd areas ( where the
	 * valid bit may be set ) have already been zeroed by
	 * vm_page_set_validclean().
	 */
	for (b = i = 0; i <= PAGE_SIZE / DEV_BSIZE; ++i) {
		if (i == (PAGE_SIZE / DEV_BSIZE) ||
		    (m->valid & ((vm_page_bits_t)1 << i))) {
			if (i > b) {
				pmap_zero_page_area(m,
				    b << DEV_BSHIFT, (i - b) << DEV_BSHIFT);
			}
			b = i + 1;
		}
	}

	/*
	 * setvalid is TRUE when we can safely set the zero'd areas
	 * as being valid.  We can do this if there are no cache consistancy
	 * issues.  e.g. it is ok to do with UFS, but not ok to do with NFS.
	 */
	if (setvalid)
		m->valid = VM_PAGE_BITS_ALL;
}

/*
 *	vm_page_is_valid:
 *
 *	Is (partial) page valid?  Note that the case where size == 0
 *	will return FALSE in the degenerate case where the page is
 *	entirely invalid, and TRUE otherwise.
 */
int
vm_page_is_valid(vm_page_t m, int base, int size)
{
	vm_page_bits_t bits;

	VM_OBJECT_ASSERT_LOCKED(m->object);
	bits = vm_page_bits(base, size);
	return (m->valid != 0 && (m->valid & bits) == bits);
}

/*
 * Returns true if all of the specified predicates are true for the entire
 * (super)page and false otherwise.
 */
bool
vm_page_ps_test(vm_page_t m, int flags, vm_page_t skip_m)
{
	vm_object_t object;
	int i, npages;

	object = m->object;
	if (skip_m != NULL && skip_m->object != object)
		return (false);
	VM_OBJECT_ASSERT_LOCKED(object);
	npages = atop(pagesizes[m->psind]);

	/*
	 * The physically contiguous pages that make up a superpage, i.e., a
	 * page with a page size index ("psind") greater than zero, will
	 * occupy adjacent entries in vm_page_array[].
	 */
	for (i = 0; i < npages; i++) {
		/* Always test object consistency, including "skip_m". */
		if (m[i].object != object)
			return (false);
		if (&m[i] == skip_m)
			continue;
		if ((flags & PS_NONE_BUSY) != 0 && vm_page_busied(&m[i]))
			return (false);
		if ((flags & PS_ALL_DIRTY) != 0) {
			/*
			 * Calling vm_page_test_dirty() or pmap_is_modified()
			 * might stop this case from spuriously returning
			 * "false".  However, that would require a write lock
			 * on the object containing "m[i]".
			 */
			if (m[i].dirty != VM_PAGE_BITS_ALL)
				return (false);
		}
		if ((flags & PS_ALL_VALID) != 0 &&
		    m[i].valid != VM_PAGE_BITS_ALL)
			return (false);
	}
	return (true);
}

/*
 * Set the page's dirty bits if the page is modified.
 */
void
vm_page_test_dirty(vm_page_t m)
{

	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (m->dirty != VM_PAGE_BITS_ALL && pmap_is_modified(m))
		vm_page_dirty(m);
}

void
vm_page_lock_KBI(vm_page_t m, const char *file, int line)
{

	mtx_lock_flags_(vm_page_lockptr(m), 0, file, line);
}

void
vm_page_unlock_KBI(vm_page_t m, const char *file, int line)
{

	mtx_unlock_flags_(vm_page_lockptr(m), 0, file, line);
}

int
vm_page_trylock_KBI(vm_page_t m, const char *file, int line)
{

	return (mtx_trylock_flags_(vm_page_lockptr(m), 0, file, line));
}

#if defined(INVARIANTS) || defined(INVARIANT_SUPPORT)
void
vm_page_assert_locked_KBI(vm_page_t m, const char *file, int line)
{

	vm_page_lock_assert_KBI(m, MA_OWNED, file, line);
}

void
vm_page_lock_assert_KBI(vm_page_t m, int a, const char *file, int line)
{

	mtx_assert_(vm_page_lockptr(m), a, file, line);
}
#endif

#ifdef INVARIANTS
void
vm_page_object_lock_assert(vm_page_t m)
{

	/*
	 * Certain of the page's fields may only be modified by the
	 * holder of the containing object's lock or the exclusive busy.
	 * holder.  Unfortunately, the holder of the write busy is
	 * not recorded, and thus cannot be checked here.
	 */
	if (m->object != NULL && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_WLOCKED(m->object);
}

void
vm_page_assert_pga_writeable(vm_page_t m, uint8_t bits)
{

	if ((bits & PGA_WRITEABLE) == 0)
		return;

	/*
	 * The PGA_WRITEABLE flag can only be set if the page is
	 * managed, is exclusively busied or the object is locked.
	 * Currently, this flag is only set by pmap_enter().
	 */
	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("PGA_WRITEABLE on unmanaged page"));
	if (!vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);
}
#endif

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>

#include <ddb/ddb.h>

DB_SHOW_COMMAND(page, vm_page_print_page_info)
{

	db_printf("vm_cnt.v_free_count: %d\n", vm_free_count());
	db_printf("vm_cnt.v_inactive_count: %d\n", vm_inactive_count());
	db_printf("vm_cnt.v_active_count: %d\n", vm_active_count());
	db_printf("vm_cnt.v_laundry_count: %d\n", vm_laundry_count());
	db_printf("vm_cnt.v_wire_count: %d\n", vm_wire_count());
	db_printf("vm_cnt.v_free_reserved: %d\n", vm_cnt.v_free_reserved);
	db_printf("vm_cnt.v_free_min: %d\n", vm_cnt.v_free_min);
	db_printf("vm_cnt.v_free_target: %d\n", vm_cnt.v_free_target);
	db_printf("vm_cnt.v_inactive_target: %d\n", vm_cnt.v_inactive_target);
}

DB_SHOW_COMMAND(pageq, vm_page_print_pageq_info)
{
	int dom;

	db_printf("pq_free %d\n", vm_free_count());
	for (dom = 0; dom < vm_ndomains; dom++) {
		db_printf(
    "dom %d page_cnt %d free %d pq_act %d pq_inact %d pq_laund %d pq_unsw %d\n",
		    dom,
		    vm_dom[dom].vmd_page_count,
		    vm_dom[dom].vmd_free_count,
		    vm_dom[dom].vmd_pagequeues[PQ_ACTIVE].pq_cnt,
		    vm_dom[dom].vmd_pagequeues[PQ_INACTIVE].pq_cnt,
		    vm_dom[dom].vmd_pagequeues[PQ_LAUNDRY].pq_cnt,
		    vm_dom[dom].vmd_pagequeues[PQ_UNSWAPPABLE].pq_cnt);
	}
}

DB_SHOW_COMMAND(pginfo, vm_page_print_pginfo)
{
	vm_page_t m;
	boolean_t phys, virt;

	if (!have_addr) {
		db_printf("show pginfo addr\n");
		return;
	}

	phys = strchr(modif, 'p') != NULL;
	virt = strchr(modif, 'v') != NULL;
	if (virt)
		m = PHYS_TO_VM_PAGE(pmap_kextract(addr));
	else if (phys)
		m = PHYS_TO_VM_PAGE(addr);
	else
		m = (vm_page_t)addr;
	db_printf(
    "page %p obj %p pidx 0x%jx phys 0x%jx q %d hold %d wire %d\n"
    "  af 0x%x of 0x%x f 0x%x act %d busy %x valid 0x%x dirty 0x%x\n",
	    m, m->object, (uintmax_t)m->pindex, (uintmax_t)m->phys_addr,
	    m->queue, m->hold_count, m->wire_count, m->aflags, m->oflags,
	    m->flags, m->act_count, m->busy_lock, m->valid, m->dirty);
}
#endif /* DDB */
