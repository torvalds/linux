/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_VMEM_H
#define	_SPL_VMEM_H

#include <sys/kmem.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

typedef struct vmem { } vmem_t;

extern vmem_t *heap_arena;
extern vmem_t *zio_alloc_arena;
extern vmem_t *zio_arena;

extern size_t vmem_size(vmem_t *vmp, int typemask);

/*
 * Memory allocation interfaces
 */
#define	VMEM_ALLOC	0x01
#define	VMEM_FREE	0x02

#ifndef VMALLOC_TOTAL
#define	VMALLOC_TOTAL	(VMALLOC_END - VMALLOC_START)
#endif

/*
 * vmem_* is an interface to a low level arena-based memory allocator on
 * Illumos that is used to allocate virtual address space. The kmem SLAB
 * allocator allocates slabs from it. Then the generic allocation functions
 * kmem_{alloc,zalloc,free}() are layered on top of SLAB allocators.
 *
 * On Linux, the primary means of doing allocations is via kmalloc(), which
 * is similarly layered on top of something called the buddy allocator. The
 * buddy allocator is not available to kernel modules, it uses physical
 * memory addresses rather than virtual memory addresses and is prone to
 * fragmentation.
 *
 * Linux sets aside a relatively small address space for in-kernel virtual
 * memory from which allocations can be done using vmalloc().  It might seem
 * like a good idea to use vmalloc() to implement something similar to
 * Illumos' allocator. However, this has the following problems:
 *
 * 1. Page directory table allocations are hard coded to use GFP_KERNEL.
 *    Consequently, any KM_PUSHPAGE or KM_NOSLEEP allocations done using
 *    vmalloc() will not have proper semantics.
 *
 * 2. Address space exhaustion is a real issue on 32-bit platforms where
 *    only a few 100MB are available. The kernel will handle it by spinning
 *    when it runs out of address space.
 *
 * 3. All vmalloc() allocations and frees are protected by a single global
 *    lock which serializes all allocations.
 *
 * 4. Accessing /proc/meminfo and /proc/vmallocinfo will iterate the entire
 *    list. The former will sum the allocations while the latter will print
 *    them to user space in a way that user space can keep the lock held
 *    indefinitely.  When the total number of mapped allocations is large
 *    (several 100,000) a large amount of time will be spent waiting on locks.
 *
 * 5. Linux has a wait_on_bit() locking primitive that assumes physical
 *    memory is used, it simply does not work on virtual memory.  Certain
 *    Linux structures (e.g. the superblock) use them and might be embedded
 *    into a structure from Illumos.  This makes using Linux virtual memory
 *    unsafe in certain situations.
 *
 * It follows that we cannot obtain identical semantics to those on Illumos.
 * Consequently, we implement the kmem_{alloc,zalloc,free}() functions in
 * such a way that they can be used as drop-in replacements for small vmem_*
 * allocations (8MB in size or smaller) and map vmem_{alloc,zalloc,free}()
 * to them.
 */

#define	vmem_alloc(sz, fl)	spl_vmem_alloc((sz), (fl), __func__, __LINE__)
#define	vmem_zalloc(sz, fl)	spl_vmem_zalloc((sz), (fl), __func__, __LINE__)
#define	vmem_free(ptr, sz)	spl_vmem_free((ptr), (sz))
#define	vmem_qcache_reap(ptr)	((void)0)

extern void *spl_vmem_alloc(size_t sz, int fl, const char *func, int line);
extern void *spl_vmem_zalloc(size_t sz, int fl, const char *func, int line);
extern void spl_vmem_free(const void *ptr, size_t sz);

int spl_vmem_init(void);
void spl_vmem_fini(void);

#endif	/* _SPL_VMEM_H */
