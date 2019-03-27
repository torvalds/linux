/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1991, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2005-2009 Robert N. M. Watson
 * Copyright (c) 2008 Otto Moerbeek <otto@drijf.net> (mallocarray)
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
 *	@(#)kern_malloc.c	8.3 (Berkeley) 1/4/94
 */

/*
 * Kernel malloc(9) implementation -- general purpose kernel memory allocator
 * based on memory types.  Back end is implemented using the UMA(9) zone
 * allocator.  A set of fixed-size buckets are used for smaller allocations,
 * and a special UMA allocation interface is used for larger allocations.
 * Callers declare memory types, and statistics are maintained independently
 * for each memory type.  Statistics are maintained per-CPU for performance
 * reasons.  See malloc(9) and comments in malloc.h for a detailed
 * description.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/vmmeter.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/vmem.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_domainset.h>
#include <vm/vm_pageout.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/uma.h>
#include <vm/uma_int.h>
#include <vm/uma_dbg.h>

#ifdef DEBUG_MEMGUARD
#include <vm/memguard.h>
#endif
#ifdef DEBUG_REDZONE
#include <vm/redzone.h>
#endif

#if defined(INVARIANTS) && defined(__i386__)
#include <machine/cpu.h>
#endif

#include <ddb/ddb.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>

bool	__read_frequently			dtrace_malloc_enabled;
dtrace_malloc_probe_func_t __read_mostly	dtrace_malloc_probe;
#endif

#if defined(INVARIANTS) || defined(MALLOC_MAKE_FAILURES) ||		\
    defined(DEBUG_MEMGUARD) || defined(DEBUG_REDZONE)
#define	MALLOC_DEBUG	1
#endif

/*
 * When realloc() is called, if the new size is sufficiently smaller than
 * the old size, realloc() will allocate a new, smaller block to avoid
 * wasting memory. 'Sufficiently smaller' is defined as: newsize <=
 * oldsize / 2^n, where REALLOC_FRACTION defines the value of 'n'.
 */
#ifndef REALLOC_FRACTION
#define	REALLOC_FRACTION	1	/* new block if <= half the size */
#endif

/*
 * Centrally define some common malloc types.
 */
MALLOC_DEFINE(M_CACHE, "cache", "Various Dynamically allocated caches");
MALLOC_DEFINE(M_DEVBUF, "devbuf", "device driver memory");
MALLOC_DEFINE(M_TEMP, "temp", "misc temporary data buffers");

static struct malloc_type *kmemstatistics;
static int kmemcount;

#define KMEM_ZSHIFT	4
#define KMEM_ZBASE	16
#define KMEM_ZMASK	(KMEM_ZBASE - 1)

#define KMEM_ZMAX	65536
#define KMEM_ZSIZE	(KMEM_ZMAX >> KMEM_ZSHIFT)
static uint8_t kmemsize[KMEM_ZSIZE + 1];

#ifndef MALLOC_DEBUG_MAXZONES
#define	MALLOC_DEBUG_MAXZONES	1
#endif
static int numzones = MALLOC_DEBUG_MAXZONES;

/*
 * Small malloc(9) memory allocations are allocated from a set of UMA buckets
 * of various sizes.
 *
 * XXX: The comment here used to read "These won't be powers of two for
 * long."  It's possible that a significant amount of wasted memory could be
 * recovered by tuning the sizes of these buckets.
 */
struct {
	int kz_size;
	char *kz_name;
	uma_zone_t kz_zone[MALLOC_DEBUG_MAXZONES];
} kmemzones[] = {
	{16, "16", },
	{32, "32", },
	{64, "64", },
	{128, "128", },
	{256, "256", },
	{512, "512", },
	{1024, "1024", },
	{2048, "2048", },
	{4096, "4096", },
	{8192, "8192", },
	{16384, "16384", },
	{32768, "32768", },
	{65536, "65536", },
	{0, NULL},
};

/*
 * Zone to allocate malloc type descriptions from.  For ABI reasons, memory
 * types are described by a data structure passed by the declaring code, but
 * the malloc(9) implementation has its own data structure describing the
 * type and statistics.  This permits the malloc(9)-internal data structures
 * to be modified without breaking binary-compiled kernel modules that
 * declare malloc types.
 */
static uma_zone_t mt_zone;
static uma_zone_t mt_stats_zone;

u_long vm_kmem_size;
SYSCTL_ULONG(_vm, OID_AUTO, kmem_size, CTLFLAG_RDTUN, &vm_kmem_size, 0,
    "Size of kernel memory");

static u_long kmem_zmax = KMEM_ZMAX;
SYSCTL_ULONG(_vm, OID_AUTO, kmem_zmax, CTLFLAG_RDTUN, &kmem_zmax, 0,
    "Maximum allocation size that malloc(9) would use UMA as backend");

static u_long vm_kmem_size_min;
SYSCTL_ULONG(_vm, OID_AUTO, kmem_size_min, CTLFLAG_RDTUN, &vm_kmem_size_min, 0,
    "Minimum size of kernel memory");

static u_long vm_kmem_size_max;
SYSCTL_ULONG(_vm, OID_AUTO, kmem_size_max, CTLFLAG_RDTUN, &vm_kmem_size_max, 0,
    "Maximum size of kernel memory");

static u_int vm_kmem_size_scale;
SYSCTL_UINT(_vm, OID_AUTO, kmem_size_scale, CTLFLAG_RDTUN, &vm_kmem_size_scale, 0,
    "Scale factor for kernel memory size");

static int sysctl_kmem_map_size(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, kmem_map_size,
    CTLFLAG_RD | CTLTYPE_ULONG | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_kmem_map_size, "LU", "Current kmem allocation size");

static int sysctl_kmem_map_free(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_vm, OID_AUTO, kmem_map_free,
    CTLFLAG_RD | CTLTYPE_ULONG | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_kmem_map_free, "LU", "Free space in kmem");

/*
 * The malloc_mtx protects the kmemstatistics linked list.
 */
struct mtx malloc_mtx;

#ifdef MALLOC_PROFILE
uint64_t krequests[KMEM_ZSIZE + 1];

static int sysctl_kern_mprof(SYSCTL_HANDLER_ARGS);
#endif

static int sysctl_kern_malloc_stats(SYSCTL_HANDLER_ARGS);

/*
 * time_uptime of the last malloc(9) failure (induced or real).
 */
static time_t t_malloc_fail;

#if defined(MALLOC_MAKE_FAILURES) || (MALLOC_DEBUG_MAXZONES > 1)
static SYSCTL_NODE(_debug, OID_AUTO, malloc, CTLFLAG_RD, 0,
    "Kernel malloc debugging options");
#endif

/*
 * malloc(9) fault injection -- cause malloc failures every (n) mallocs when
 * the caller specifies M_NOWAIT.  If set to 0, no failures are caused.
 */
#ifdef MALLOC_MAKE_FAILURES
static int malloc_failure_rate;
static int malloc_nowait_count;
static int malloc_failure_count;
SYSCTL_INT(_debug_malloc, OID_AUTO, failure_rate, CTLFLAG_RWTUN,
    &malloc_failure_rate, 0, "Every (n) mallocs with M_NOWAIT will fail");
SYSCTL_INT(_debug_malloc, OID_AUTO, failure_count, CTLFLAG_RD,
    &malloc_failure_count, 0, "Number of imposed M_NOWAIT malloc failures");
#endif

static int
sysctl_kmem_map_size(SYSCTL_HANDLER_ARGS)
{
	u_long size;

	size = uma_size();
	return (sysctl_handle_long(oidp, &size, 0, req));
}

static int
sysctl_kmem_map_free(SYSCTL_HANDLER_ARGS)
{
	u_long size, limit;

	/* The sysctl is unsigned, implement as a saturation value. */
	size = uma_size();
	limit = uma_limit();
	if (size > limit)
		size = 0;
	else
		size = limit - size;
	return (sysctl_handle_long(oidp, &size, 0, req));
}

/*
 * malloc(9) uma zone separation -- sub-page buffer overruns in one
 * malloc type will affect only a subset of other malloc types.
 */
#if MALLOC_DEBUG_MAXZONES > 1
static void
tunable_set_numzones(void)
{

	TUNABLE_INT_FETCH("debug.malloc.numzones",
	    &numzones);

	/* Sanity check the number of malloc uma zones. */
	if (numzones <= 0)
		numzones = 1;
	if (numzones > MALLOC_DEBUG_MAXZONES)
		numzones = MALLOC_DEBUG_MAXZONES;
}
SYSINIT(numzones, SI_SUB_TUNABLES, SI_ORDER_ANY, tunable_set_numzones, NULL);
SYSCTL_INT(_debug_malloc, OID_AUTO, numzones, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &numzones, 0, "Number of malloc uma subzones");

/*
 * Any number that changes regularly is an okay choice for the
 * offset.  Build numbers are pretty good of you have them.
 */
static u_int zone_offset = __FreeBSD_version;
TUNABLE_INT("debug.malloc.zone_offset", &zone_offset);
SYSCTL_UINT(_debug_malloc, OID_AUTO, zone_offset, CTLFLAG_RDTUN,
    &zone_offset, 0, "Separate malloc types by examining the "
    "Nth character in the malloc type short description.");

static void
mtp_set_subzone(struct malloc_type *mtp)
{
	struct malloc_type_internal *mtip;
	const char *desc;
	size_t len;
	u_int val;

	mtip = mtp->ks_handle;
	desc = mtp->ks_shortdesc;
	if (desc == NULL || (len = strlen(desc)) == 0)
		val = 0;
	else
		val = desc[zone_offset % len];
	mtip->mti_zone = (val % numzones);
}

static inline u_int
mtp_get_subzone(struct malloc_type *mtp)
{
	struct malloc_type_internal *mtip;

	mtip = mtp->ks_handle;

	KASSERT(mtip->mti_zone < numzones,
	    ("mti_zone %u out of range %d",
	    mtip->mti_zone, numzones));
	return (mtip->mti_zone);
}
#elif MALLOC_DEBUG_MAXZONES == 0
#error "MALLOC_DEBUG_MAXZONES must be positive."
#else
static void
mtp_set_subzone(struct malloc_type *mtp)
{
	struct malloc_type_internal *mtip;

	mtip = mtp->ks_handle;
	mtip->mti_zone = 0;
}

static inline u_int
mtp_get_subzone(struct malloc_type *mtp)
{

	return (0);
}
#endif /* MALLOC_DEBUG_MAXZONES > 1 */

int
malloc_last_fail(void)
{

	return (time_uptime - t_malloc_fail);
}

/*
 * An allocation has succeeded -- update malloc type statistics for the
 * amount of bucket size.  Occurs within a critical section so that the
 * thread isn't preempted and doesn't migrate while updating per-PCU
 * statistics.
 */
static void
malloc_type_zone_allocated(struct malloc_type *mtp, unsigned long size,
    int zindx)
{
	struct malloc_type_internal *mtip;
	struct malloc_type_stats *mtsp;

	critical_enter();
	mtip = mtp->ks_handle;
	mtsp = zpcpu_get(mtip->mti_stats);
	if (size > 0) {
		mtsp->mts_memalloced += size;
		mtsp->mts_numallocs++;
	}
	if (zindx != -1)
		mtsp->mts_size |= 1 << zindx;

#ifdef KDTRACE_HOOKS
	if (__predict_false(dtrace_malloc_enabled)) {
		uint32_t probe_id = mtip->mti_probes[DTMALLOC_PROBE_MALLOC];
		if (probe_id != 0)
			(dtrace_malloc_probe)(probe_id,
			    (uintptr_t) mtp, (uintptr_t) mtip,
			    (uintptr_t) mtsp, size, zindx);
	}
#endif

	critical_exit();
}

void
malloc_type_allocated(struct malloc_type *mtp, unsigned long size)
{

	if (size > 0)
		malloc_type_zone_allocated(mtp, size, -1);
}

/*
 * A free operation has occurred -- update malloc type statistics for the
 * amount of the bucket size.  Occurs within a critical section so that the
 * thread isn't preempted and doesn't migrate while updating per-CPU
 * statistics.
 */
void
malloc_type_freed(struct malloc_type *mtp, unsigned long size)
{
	struct malloc_type_internal *mtip;
	struct malloc_type_stats *mtsp;

	critical_enter();
	mtip = mtp->ks_handle;
	mtsp = zpcpu_get(mtip->mti_stats);
	mtsp->mts_memfreed += size;
	mtsp->mts_numfrees++;

#ifdef KDTRACE_HOOKS
	if (__predict_false(dtrace_malloc_enabled)) {
		uint32_t probe_id = mtip->mti_probes[DTMALLOC_PROBE_FREE];
		if (probe_id != 0)
			(dtrace_malloc_probe)(probe_id,
			    (uintptr_t) mtp, (uintptr_t) mtip,
			    (uintptr_t) mtsp, size, 0);
	}
#endif

	critical_exit();
}

/*
 *	contigmalloc:
 *
 *	Allocate a block of physically contiguous memory.
 *
 *	If M_NOWAIT is set, this routine will not block and return NULL if
 *	the allocation fails.
 */
void *
contigmalloc(unsigned long size, struct malloc_type *type, int flags,
    vm_paddr_t low, vm_paddr_t high, unsigned long alignment,
    vm_paddr_t boundary)
{
	void *ret;

	ret = (void *)kmem_alloc_contig(size, flags, low, high, alignment,
	    boundary, VM_MEMATTR_DEFAULT);
	if (ret != NULL)
		malloc_type_allocated(type, round_page(size));
	return (ret);
}

void *
contigmalloc_domainset(unsigned long size, struct malloc_type *type,
    struct domainset *ds, int flags, vm_paddr_t low, vm_paddr_t high,
    unsigned long alignment, vm_paddr_t boundary)
{
	void *ret;

	ret = (void *)kmem_alloc_contig_domainset(ds, size, flags, low, high,
	    alignment, boundary, VM_MEMATTR_DEFAULT);
	if (ret != NULL)
		malloc_type_allocated(type, round_page(size));
	return (ret);
}

/*
 *	contigfree:
 *
 *	Free a block of memory allocated by contigmalloc.
 *
 *	This routine may not block.
 */
void
contigfree(void *addr, unsigned long size, struct malloc_type *type)
{

	kmem_free((vm_offset_t)addr, size);
	malloc_type_freed(type, round_page(size));
}

#ifdef MALLOC_DEBUG
static int
malloc_dbg(caddr_t *vap, size_t *sizep, struct malloc_type *mtp,
    int flags)
{
#ifdef INVARIANTS
	int indx;

	KASSERT(mtp->ks_magic == M_MAGIC, ("malloc: bad malloc type magic"));
	/*
	 * Check that exactly one of M_WAITOK or M_NOWAIT is specified.
	 */
	indx = flags & (M_WAITOK | M_NOWAIT);
	if (indx != M_NOWAIT && indx != M_WAITOK) {
		static	struct timeval lasterr;
		static	int curerr, once;
		if (once == 0 && ppsratecheck(&lasterr, &curerr, 1)) {
			printf("Bad malloc flags: %x\n", indx);
			kdb_backtrace();
			flags |= M_WAITOK;
			once++;
		}
	}
#endif
#ifdef MALLOC_MAKE_FAILURES
	if ((flags & M_NOWAIT) && (malloc_failure_rate != 0)) {
		atomic_add_int(&malloc_nowait_count, 1);
		if ((malloc_nowait_count % malloc_failure_rate) == 0) {
			atomic_add_int(&malloc_failure_count, 1);
			t_malloc_fail = time_uptime;
			*vap = NULL;
			return (EJUSTRETURN);
		}
	}
#endif
	if (flags & M_WAITOK) {
		KASSERT(curthread->td_intr_nesting_level == 0,
		   ("malloc(M_WAITOK) in interrupt context"));
		KASSERT(curthread->td_epochnest == 0,
			("malloc(M_WAITOK) in epoch context"));		
	}
	KASSERT(curthread->td_critnest == 0 || SCHEDULER_STOPPED(),
	    ("malloc: called with spinlock or critical section held"));

#ifdef DEBUG_MEMGUARD
	if (memguard_cmp_mtp(mtp, *sizep)) {
		*vap = memguard_alloc(*sizep, flags);
		if (*vap != NULL)
			return (EJUSTRETURN);
		/* This is unfortunate but should not be fatal. */
	}
#endif

#ifdef DEBUG_REDZONE
	*sizep = redzone_size_ntor(*sizep);
#endif

	return (0);
}
#endif

/*
 *	malloc:
 *
 *	Allocate a block of memory.
 *
 *	If M_NOWAIT is set, this routine will not block and return NULL if
 *	the allocation fails.
 */
void *
(malloc)(size_t size, struct malloc_type *mtp, int flags)
{
	int indx;
	caddr_t va;
	uma_zone_t zone;
#if defined(DEBUG_REDZONE)
	unsigned long osize = size;
#endif

#ifdef MALLOC_DEBUG
	va = NULL;
	if (malloc_dbg(&va, &size, mtp, flags) != 0)
		return (va);
#endif

	if (size <= kmem_zmax && (flags & M_EXEC) == 0) {
		if (size & KMEM_ZMASK)
			size = (size & ~KMEM_ZMASK) + KMEM_ZBASE;
		indx = kmemsize[size >> KMEM_ZSHIFT];
		zone = kmemzones[indx].kz_zone[mtp_get_subzone(mtp)];
#ifdef MALLOC_PROFILE
		krequests[size >> KMEM_ZSHIFT]++;
#endif
		va = uma_zalloc(zone, flags);
		if (va != NULL)
			size = zone->uz_size;
		malloc_type_zone_allocated(mtp, va == NULL ? 0 : size, indx);
	} else {
		size = roundup(size, PAGE_SIZE);
		zone = NULL;
		va = uma_large_malloc(size, flags);
		malloc_type_allocated(mtp, va == NULL ? 0 : size);
	}
	if (flags & M_WAITOK)
		KASSERT(va != NULL, ("malloc(M_WAITOK) returned NULL"));
	else if (va == NULL)
		t_malloc_fail = time_uptime;
#ifdef DEBUG_REDZONE
	if (va != NULL)
		va = redzone_setup(va, osize);
#endif
	return ((void *) va);
}

static void *
malloc_domain(size_t size, struct malloc_type *mtp, int domain, int flags)
{
	int indx;
	caddr_t va;
	uma_zone_t zone;
#if defined(DEBUG_REDZONE)
	unsigned long osize = size;
#endif

#ifdef MALLOC_DEBUG
	va = NULL;
	if (malloc_dbg(&va, &size, mtp, flags) != 0)
		return (va);
#endif
	if (size <= kmem_zmax && (flags & M_EXEC) == 0) {
		if (size & KMEM_ZMASK)
			size = (size & ~KMEM_ZMASK) + KMEM_ZBASE;
		indx = kmemsize[size >> KMEM_ZSHIFT];
		zone = kmemzones[indx].kz_zone[mtp_get_subzone(mtp)];
#ifdef MALLOC_PROFILE
		krequests[size >> KMEM_ZSHIFT]++;
#endif
		va = uma_zalloc_domain(zone, NULL, domain, flags);
		if (va != NULL)
			size = zone->uz_size;
		malloc_type_zone_allocated(mtp, va == NULL ? 0 : size, indx);
	} else {
		size = roundup(size, PAGE_SIZE);
		zone = NULL;
		va = uma_large_malloc_domain(size, domain, flags);
		malloc_type_allocated(mtp, va == NULL ? 0 : size);
	}
	if (flags & M_WAITOK)
		KASSERT(va != NULL, ("malloc(M_WAITOK) returned NULL"));
	else if (va == NULL)
		t_malloc_fail = time_uptime;
#ifdef DEBUG_REDZONE
	if (va != NULL)
		va = redzone_setup(va, osize);
#endif
	return ((void *) va);
}

void *
malloc_domainset(size_t size, struct malloc_type *mtp, struct domainset *ds,
    int flags)
{
	struct vm_domainset_iter di;
	void *ret;
	int domain;

	vm_domainset_iter_policy_init(&di, ds, &domain, &flags);
	do {
		ret = malloc_domain(size, mtp, domain, flags);
		if (ret != NULL)
			break;
	} while (vm_domainset_iter_policy(&di, &domain) == 0);

	return (ret);
}

void *
mallocarray(size_t nmemb, size_t size, struct malloc_type *type, int flags)
{

	if (WOULD_OVERFLOW(nmemb, size))
		panic("mallocarray: %zu * %zu overflowed", nmemb, size);

	return (malloc(size * nmemb, type, flags));
}

#ifdef INVARIANTS
static void
free_save_type(void *addr, struct malloc_type *mtp, u_long size)
{
	struct malloc_type **mtpp = addr;

	/*
	 * Cache a pointer to the malloc_type that most recently freed
	 * this memory here.  This way we know who is most likely to
	 * have stepped on it later.
	 *
	 * This code assumes that size is a multiple of 8 bytes for
	 * 64 bit machines
	 */
	mtpp = (struct malloc_type **) ((unsigned long)mtpp & ~UMA_ALIGN_PTR);
	mtpp += (size - sizeof(struct malloc_type *)) /
	    sizeof(struct malloc_type *);
	*mtpp = mtp;
}
#endif

#ifdef MALLOC_DEBUG
static int
free_dbg(void **addrp, struct malloc_type *mtp)
{
	void *addr;

	addr = *addrp;
	KASSERT(mtp->ks_magic == M_MAGIC, ("free: bad malloc type magic"));
	KASSERT(curthread->td_critnest == 0 || SCHEDULER_STOPPED(),
	    ("free: called with spinlock or critical section held"));

	/* free(NULL, ...) does nothing */
	if (addr == NULL)
		return (EJUSTRETURN);

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(addr)) {
		memguard_free(addr);
		return (EJUSTRETURN);
	}
#endif

#ifdef DEBUG_REDZONE
	redzone_check(addr);
	*addrp = redzone_addr_ntor(addr);
#endif

	return (0);
}
#endif

/*
 *	free:
 *
 *	Free a block of memory allocated by malloc.
 *
 *	This routine may not block.
 */
void
free(void *addr, struct malloc_type *mtp)
{
	uma_slab_t slab;
	u_long size;

#ifdef MALLOC_DEBUG
	if (free_dbg(&addr, mtp) != 0)
		return;
#endif
	/* free(NULL, ...) does nothing */
	if (addr == NULL)
		return;

	slab = vtoslab((vm_offset_t)addr & (~UMA_SLAB_MASK));
	if (slab == NULL)
		panic("free: address %p(%p) has not been allocated.\n",
		    addr, (void *)((u_long)addr & (~UMA_SLAB_MASK)));

	if (!(slab->us_flags & UMA_SLAB_MALLOC)) {
		size = slab->us_keg->uk_size;
#ifdef INVARIANTS
		free_save_type(addr, mtp, size);
#endif
		uma_zfree_arg(LIST_FIRST(&slab->us_keg->uk_zones), addr, slab);
	} else {
		size = slab->us_size;
		uma_large_free(slab);
	}
	malloc_type_freed(mtp, size);
}

void
free_domain(void *addr, struct malloc_type *mtp)
{
	uma_slab_t slab;
	u_long size;

#ifdef MALLOC_DEBUG
	if (free_dbg(&addr, mtp) != 0)
		return;
#endif

	/* free(NULL, ...) does nothing */
	if (addr == NULL)
		return;

	slab = vtoslab((vm_offset_t)addr & (~UMA_SLAB_MASK));
	if (slab == NULL)
		panic("free_domain: address %p(%p) has not been allocated.\n",
		    addr, (void *)((u_long)addr & (~UMA_SLAB_MASK)));

	if (!(slab->us_flags & UMA_SLAB_MALLOC)) {
		size = slab->us_keg->uk_size;
#ifdef INVARIANTS
		free_save_type(addr, mtp, size);
#endif
		uma_zfree_domain(LIST_FIRST(&slab->us_keg->uk_zones),
		    addr, slab);
	} else {
		size = slab->us_size;
		uma_large_free(slab);
	}
	malloc_type_freed(mtp, size);
}

/*
 *	realloc: change the size of a memory block
 */
void *
realloc(void *addr, size_t size, struct malloc_type *mtp, int flags)
{
	uma_slab_t slab;
	unsigned long alloc;
	void *newaddr;

	KASSERT(mtp->ks_magic == M_MAGIC,
	    ("realloc: bad malloc type magic"));
	KASSERT(curthread->td_critnest == 0 || SCHEDULER_STOPPED(),
	    ("realloc: called with spinlock or critical section held"));

	/* realloc(NULL, ...) is equivalent to malloc(...) */
	if (addr == NULL)
		return (malloc(size, mtp, flags));

	/*
	 * XXX: Should report free of old memory and alloc of new memory to
	 * per-CPU stats.
	 */

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(addr))
		return (memguard_realloc(addr, size, mtp, flags));
#endif

#ifdef DEBUG_REDZONE
	slab = NULL;
	alloc = redzone_get_size(addr);
#else
	slab = vtoslab((vm_offset_t)addr & ~(UMA_SLAB_MASK));

	/* Sanity check */
	KASSERT(slab != NULL,
	    ("realloc: address %p out of range", (void *)addr));

	/* Get the size of the original block */
	if (!(slab->us_flags & UMA_SLAB_MALLOC))
		alloc = slab->us_keg->uk_size;
	else
		alloc = slab->us_size;

	/* Reuse the original block if appropriate */
	if (size <= alloc
	    && (size > (alloc >> REALLOC_FRACTION) || alloc == MINALLOCSIZE))
		return (addr);
#endif /* !DEBUG_REDZONE */

	/* Allocate a new, bigger (or smaller) block */
	if ((newaddr = malloc(size, mtp, flags)) == NULL)
		return (NULL);

	/* Copy over original contents */
	bcopy(addr, newaddr, min(size, alloc));
	free(addr, mtp);
	return (newaddr);
}

/*
 *	reallocf: same as realloc() but free memory on failure.
 */
void *
reallocf(void *addr, size_t size, struct malloc_type *mtp, int flags)
{
	void *mem;

	if ((mem = realloc(addr, size, mtp, flags)) == NULL)
		free(addr, mtp);
	return (mem);
}

#ifndef __sparc64__
CTASSERT(VM_KMEM_SIZE_SCALE >= 1);
#endif

/*
 * Initialize the kernel memory (kmem) arena.
 */
void
kmeminit(void)
{
	u_long mem_size;
	u_long tmp;

#ifdef VM_KMEM_SIZE
	if (vm_kmem_size == 0)
		vm_kmem_size = VM_KMEM_SIZE;
#endif
#ifdef VM_KMEM_SIZE_MIN
	if (vm_kmem_size_min == 0)
		vm_kmem_size_min = VM_KMEM_SIZE_MIN;
#endif
#ifdef VM_KMEM_SIZE_MAX
	if (vm_kmem_size_max == 0)
		vm_kmem_size_max = VM_KMEM_SIZE_MAX;
#endif
	/*
	 * Calculate the amount of kernel virtual address (KVA) space that is
	 * preallocated to the kmem arena.  In order to support a wide range
	 * of machines, it is a function of the physical memory size,
	 * specifically,
	 *
	 *	min(max(physical memory size / VM_KMEM_SIZE_SCALE,
	 *	    VM_KMEM_SIZE_MIN), VM_KMEM_SIZE_MAX)
	 *
	 * Every architecture must define an integral value for
	 * VM_KMEM_SIZE_SCALE.  However, the definitions of VM_KMEM_SIZE_MIN
	 * and VM_KMEM_SIZE_MAX, which represent respectively the floor and
	 * ceiling on this preallocation, are optional.  Typically,
	 * VM_KMEM_SIZE_MAX is itself a function of the available KVA space on
	 * a given architecture.
	 */
	mem_size = vm_cnt.v_page_count;
	if (mem_size <= 32768) /* delphij XXX 128MB */
		kmem_zmax = PAGE_SIZE;

	if (vm_kmem_size_scale < 1)
		vm_kmem_size_scale = VM_KMEM_SIZE_SCALE;

	/*
	 * Check if we should use defaults for the "vm_kmem_size"
	 * variable:
	 */
	if (vm_kmem_size == 0) {
		vm_kmem_size = mem_size / vm_kmem_size_scale;
		vm_kmem_size = vm_kmem_size * PAGE_SIZE < vm_kmem_size ?
		    vm_kmem_size_max : vm_kmem_size * PAGE_SIZE;
		if (vm_kmem_size_min > 0 && vm_kmem_size < vm_kmem_size_min)
			vm_kmem_size = vm_kmem_size_min;
		if (vm_kmem_size_max > 0 && vm_kmem_size >= vm_kmem_size_max)
			vm_kmem_size = vm_kmem_size_max;
	}
	if (vm_kmem_size == 0)
		panic("Tune VM_KMEM_SIZE_* for the platform");

	/*
	 * The amount of KVA space that is preallocated to the
	 * kmem arena can be set statically at compile-time or manually
	 * through the kernel environment.  However, it is still limited to
	 * twice the physical memory size, which has been sufficient to handle
	 * the most severe cases of external fragmentation in the kmem arena. 
	 */
	if (vm_kmem_size / 2 / PAGE_SIZE > mem_size)
		vm_kmem_size = 2 * mem_size * PAGE_SIZE;

	vm_kmem_size = round_page(vm_kmem_size);
#ifdef DEBUG_MEMGUARD
	tmp = memguard_fudge(vm_kmem_size, kernel_map);
#else
	tmp = vm_kmem_size;
#endif
	uma_set_limit(tmp);

#ifdef DEBUG_MEMGUARD
	/*
	 * Initialize MemGuard if support compiled in.  MemGuard is a
	 * replacement allocator used for detecting tamper-after-free
	 * scenarios as they occur.  It is only used for debugging.
	 */
	memguard_init(kernel_arena);
#endif
}

/*
 * Initialize the kernel memory allocator
 */
/* ARGSUSED*/
static void
mallocinit(void *dummy)
{
	int i;
	uint8_t indx;

	mtx_init(&malloc_mtx, "malloc", NULL, MTX_DEF);

	kmeminit();

	if (kmem_zmax < PAGE_SIZE || kmem_zmax > KMEM_ZMAX)
		kmem_zmax = KMEM_ZMAX;

	mt_stats_zone = uma_zcreate("mt_stats_zone",
	    sizeof(struct malloc_type_stats), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_PCPU);
	mt_zone = uma_zcreate("mt_zone", sizeof(struct malloc_type_internal),
#ifdef INVARIANTS
	    mtrash_ctor, mtrash_dtor, mtrash_init, mtrash_fini,
#else
	    NULL, NULL, NULL, NULL,
#endif
	    UMA_ALIGN_PTR, UMA_ZONE_MALLOC);
	for (i = 0, indx = 0; kmemzones[indx].kz_size != 0; indx++) {
		int size = kmemzones[indx].kz_size;
		char *name = kmemzones[indx].kz_name;
		int subzone;

		for (subzone = 0; subzone < numzones; subzone++) {
			kmemzones[indx].kz_zone[subzone] =
			    uma_zcreate(name, size,
#ifdef INVARIANTS
			    mtrash_ctor, mtrash_dtor, mtrash_init, mtrash_fini,
#else
			    NULL, NULL, NULL, NULL,
#endif
			    UMA_ALIGN_PTR, UMA_ZONE_MALLOC);
		}		    
		for (;i <= size; i+= KMEM_ZBASE)
			kmemsize[i >> KMEM_ZSHIFT] = indx;

	}
}
SYSINIT(kmem, SI_SUB_KMEM, SI_ORDER_SECOND, mallocinit, NULL);

void
malloc_init(void *data)
{
	struct malloc_type_internal *mtip;
	struct malloc_type *mtp;

	KASSERT(vm_cnt.v_page_count != 0, ("malloc_register before vm_init"));

	mtp = data;
	if (mtp->ks_magic != M_MAGIC)
		panic("malloc_init: bad malloc type magic");

	mtip = uma_zalloc(mt_zone, M_WAITOK | M_ZERO);
	mtip->mti_stats = uma_zalloc_pcpu(mt_stats_zone, M_WAITOK | M_ZERO);
	mtp->ks_handle = mtip;
	mtp_set_subzone(mtp);

	mtx_lock(&malloc_mtx);
	mtp->ks_next = kmemstatistics;
	kmemstatistics = mtp;
	kmemcount++;
	mtx_unlock(&malloc_mtx);
}

void
malloc_uninit(void *data)
{
	struct malloc_type_internal *mtip;
	struct malloc_type_stats *mtsp;
	struct malloc_type *mtp, *temp;
	uma_slab_t slab;
	long temp_allocs, temp_bytes;
	int i;

	mtp = data;
	KASSERT(mtp->ks_magic == M_MAGIC,
	    ("malloc_uninit: bad malloc type magic"));
	KASSERT(mtp->ks_handle != NULL, ("malloc_deregister: cookie NULL"));

	mtx_lock(&malloc_mtx);
	mtip = mtp->ks_handle;
	mtp->ks_handle = NULL;
	if (mtp != kmemstatistics) {
		for (temp = kmemstatistics; temp != NULL;
		    temp = temp->ks_next) {
			if (temp->ks_next == mtp) {
				temp->ks_next = mtp->ks_next;
				break;
			}
		}
		KASSERT(temp,
		    ("malloc_uninit: type '%s' not found", mtp->ks_shortdesc));
	} else
		kmemstatistics = mtp->ks_next;
	kmemcount--;
	mtx_unlock(&malloc_mtx);

	/*
	 * Look for memory leaks.
	 */
	temp_allocs = temp_bytes = 0;
	for (i = 0; i <= mp_maxid; i++) {
		mtsp = zpcpu_get_cpu(mtip->mti_stats, i);
		temp_allocs += mtsp->mts_numallocs;
		temp_allocs -= mtsp->mts_numfrees;
		temp_bytes += mtsp->mts_memalloced;
		temp_bytes -= mtsp->mts_memfreed;
	}
	if (temp_allocs > 0 || temp_bytes > 0) {
		printf("Warning: memory type %s leaked memory on destroy "
		    "(%ld allocations, %ld bytes leaked).\n", mtp->ks_shortdesc,
		    temp_allocs, temp_bytes);
	}

	slab = vtoslab((vm_offset_t) mtip & (~UMA_SLAB_MASK));
	uma_zfree_pcpu(mt_stats_zone, mtip->mti_stats);
	uma_zfree_arg(mt_zone, mtip, slab);
}

struct malloc_type *
malloc_desc2type(const char *desc)
{
	struct malloc_type *mtp;

	mtx_assert(&malloc_mtx, MA_OWNED);
	for (mtp = kmemstatistics; mtp != NULL; mtp = mtp->ks_next) {
		if (strcmp(mtp->ks_shortdesc, desc) == 0)
			return (mtp);
	}
	return (NULL);
}

static int
sysctl_kern_malloc_stats(SYSCTL_HANDLER_ARGS)
{
	struct malloc_type_stream_header mtsh;
	struct malloc_type_internal *mtip;
	struct malloc_type_stats *mtsp, zeromts;
	struct malloc_type_header mth;
	struct malloc_type *mtp;
	int error, i;
	struct sbuf sbuf;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	sbuf_clear_flags(&sbuf, SBUF_INCLUDENUL);
	mtx_lock(&malloc_mtx);

	bzero(&zeromts, sizeof(zeromts));

	/*
	 * Insert stream header.
	 */
	bzero(&mtsh, sizeof(mtsh));
	mtsh.mtsh_version = MALLOC_TYPE_STREAM_VERSION;
	mtsh.mtsh_maxcpus = MAXCPU;
	mtsh.mtsh_count = kmemcount;
	(void)sbuf_bcat(&sbuf, &mtsh, sizeof(mtsh));

	/*
	 * Insert alternating sequence of type headers and type statistics.
	 */
	for (mtp = kmemstatistics; mtp != NULL; mtp = mtp->ks_next) {
		mtip = (struct malloc_type_internal *)mtp->ks_handle;

		/*
		 * Insert type header.
		 */
		bzero(&mth, sizeof(mth));
		strlcpy(mth.mth_name, mtp->ks_shortdesc, MALLOC_MAX_NAME);
		(void)sbuf_bcat(&sbuf, &mth, sizeof(mth));

		/*
		 * Insert type statistics for each CPU.
		 */
		for (i = 0; i <= mp_maxid; i++) {
			mtsp = zpcpu_get_cpu(mtip->mti_stats, i);
			(void)sbuf_bcat(&sbuf, mtsp, sizeof(*mtsp));
		}
		/*
		 * Fill in the missing CPUs.
		 */
		for (; i < MAXCPU; i++) {
			(void)sbuf_bcat(&sbuf, &zeromts, sizeof(zeromts));
		}

	}
	mtx_unlock(&malloc_mtx);
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, malloc_stats, CTLFLAG_RD|CTLTYPE_STRUCT,
    0, 0, sysctl_kern_malloc_stats, "s,malloc_type_ustats",
    "Return malloc types");

SYSCTL_INT(_kern, OID_AUTO, malloc_count, CTLFLAG_RD, &kmemcount, 0,
    "Count of kernel malloc types");

void
malloc_type_list(malloc_type_list_func_t *func, void *arg)
{
	struct malloc_type *mtp, **bufmtp;
	int count, i;
	size_t buflen;

	mtx_lock(&malloc_mtx);
restart:
	mtx_assert(&malloc_mtx, MA_OWNED);
	count = kmemcount;
	mtx_unlock(&malloc_mtx);

	buflen = sizeof(struct malloc_type *) * count;
	bufmtp = malloc(buflen, M_TEMP, M_WAITOK);

	mtx_lock(&malloc_mtx);

	if (count < kmemcount) {
		free(bufmtp, M_TEMP);
		goto restart;
	}

	for (mtp = kmemstatistics, i = 0; mtp != NULL; mtp = mtp->ks_next, i++)
		bufmtp[i] = mtp;

	mtx_unlock(&malloc_mtx);

	for (i = 0; i < count; i++)
		(func)(bufmtp[i], arg);

	free(bufmtp, M_TEMP);
}

#ifdef DDB
DB_SHOW_COMMAND(malloc, db_show_malloc)
{
	struct malloc_type_internal *mtip;
	struct malloc_type_stats *mtsp;
	struct malloc_type *mtp;
	uint64_t allocs, frees;
	uint64_t alloced, freed;
	int i;

	db_printf("%18s %12s  %12s %12s\n", "Type", "InUse", "MemUse",
	    "Requests");
	for (mtp = kmemstatistics; mtp != NULL; mtp = mtp->ks_next) {
		mtip = (struct malloc_type_internal *)mtp->ks_handle;
		allocs = 0;
		frees = 0;
		alloced = 0;
		freed = 0;
		for (i = 0; i <= mp_maxid; i++) {
			mtsp = zpcpu_get_cpu(mtip->mti_stats, i);
			allocs += mtsp->mts_numallocs;
			frees += mtsp->mts_numfrees;
			alloced += mtsp->mts_memalloced;
			freed += mtsp->mts_memfreed;
		}
		db_printf("%18s %12ju %12juK %12ju\n",
		    mtp->ks_shortdesc, allocs - frees,
		    (alloced - freed + 1023) / 1024, allocs);
		if (db_pager_quit)
			break;
	}
}

#if MALLOC_DEBUG_MAXZONES > 1
DB_SHOW_COMMAND(multizone_matches, db_show_multizone_matches)
{
	struct malloc_type_internal *mtip;
	struct malloc_type *mtp;
	u_int subzone;

	if (!have_addr) {
		db_printf("Usage: show multizone_matches <malloc type/addr>\n");
		return;
	}
	mtp = (void *)addr;
	if (mtp->ks_magic != M_MAGIC) {
		db_printf("Magic %lx does not match expected %x\n",
		    mtp->ks_magic, M_MAGIC);
		return;
	}

	mtip = mtp->ks_handle;
	subzone = mtip->mti_zone;

	for (mtp = kmemstatistics; mtp != NULL; mtp = mtp->ks_next) {
		mtip = mtp->ks_handle;
		if (mtip->mti_zone != subzone)
			continue;
		db_printf("%s\n", mtp->ks_shortdesc);
		if (db_pager_quit)
			break;
	}
}
#endif /* MALLOC_DEBUG_MAXZONES > 1 */
#endif /* DDB */

#ifdef MALLOC_PROFILE

static int
sysctl_kern_mprof(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sbuf;
	uint64_t count;
	uint64_t waste;
	uint64_t mem;
	int error;
	int rsize;
	int size;
	int i;

	waste = 0;
	mem = 0;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	sbuf_printf(&sbuf, 
	    "\n  Size                    Requests  Real Size\n");
	for (i = 0; i < KMEM_ZSIZE; i++) {
		size = i << KMEM_ZSHIFT;
		rsize = kmemzones[kmemsize[i]].kz_size;
		count = (long long unsigned)krequests[i];

		sbuf_printf(&sbuf, "%6d%28llu%11d\n", size,
		    (unsigned long long)count, rsize);

		if ((rsize * count) > (size * count))
			waste += (rsize * count) - (size * count);
		mem += (rsize * count);
	}
	sbuf_printf(&sbuf,
	    "\nTotal memory used:\t%30llu\nTotal Memory wasted:\t%30llu\n",
	    (unsigned long long)mem, (unsigned long long)waste);
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

SYSCTL_OID(_kern, OID_AUTO, mprof, CTLTYPE_STRING|CTLFLAG_RD,
    NULL, 0, sysctl_kern_mprof, "A", "Malloc Profiling");
#endif /* MALLOC_PROFILE */
