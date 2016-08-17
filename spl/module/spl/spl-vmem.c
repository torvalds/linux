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

#include <sys/debug.h>
#include <sys/vmem.h>
#include <linux/mm_compat.h>
#include <linux/module.h>

vmem_t *heap_arena = NULL;
EXPORT_SYMBOL(heap_arena);

vmem_t *zio_alloc_arena = NULL;
EXPORT_SYMBOL(zio_alloc_arena);

vmem_t *zio_arena = NULL;
EXPORT_SYMBOL(zio_arena);

size_t
vmem_size(vmem_t *vmp, int typemask)
{
	ASSERT3P(vmp, ==, NULL);
	ASSERT3S(typemask & VMEM_ALLOC, ==, VMEM_ALLOC);
	ASSERT3S(typemask & VMEM_FREE, ==, VMEM_FREE);

	return (VMALLOC_TOTAL);
}
EXPORT_SYMBOL(vmem_size);

/*
 * Public vmem_alloc(), vmem_zalloc() and vmem_free() interfaces.
 */
void *
spl_vmem_alloc(size_t size, int flags, const char *func, int line)
{
	ASSERT0(flags & ~KM_PUBLIC_MASK);

	flags |= KM_VMEM;

#if !defined(DEBUG_KMEM)
	return (spl_kmem_alloc_impl(size, flags, NUMA_NO_NODE));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_alloc_debug(size, flags, NUMA_NO_NODE));
#else
	return (spl_kmem_alloc_track(size, flags, func, line, NUMA_NO_NODE));
#endif
}
EXPORT_SYMBOL(spl_vmem_alloc);

void *
spl_vmem_zalloc(size_t size, int flags, const char *func, int line)
{
	ASSERT0(flags & ~KM_PUBLIC_MASK);

	flags |= (KM_VMEM | KM_ZERO);

#if !defined(DEBUG_KMEM)
	return (spl_kmem_alloc_impl(size, flags, NUMA_NO_NODE));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_alloc_debug(size, flags, NUMA_NO_NODE));
#else
	return (spl_kmem_alloc_track(size, flags, func, line, NUMA_NO_NODE));
#endif
}
EXPORT_SYMBOL(spl_vmem_zalloc);

void
spl_vmem_free(const void *buf, size_t size)
{
#if !defined(DEBUG_KMEM)
	return (spl_kmem_free_impl(buf, size));
#elif !defined(DEBUG_KMEM_TRACKING)
	return (spl_kmem_free_debug(buf, size));
#else
	return (spl_kmem_free_track(buf, size));
#endif
}
EXPORT_SYMBOL(spl_vmem_free);

int
spl_vmem_init(void)
{
	return (0);
}

void
spl_vmem_fini(void)
{
}
