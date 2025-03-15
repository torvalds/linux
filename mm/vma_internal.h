/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vma_internal.h
 *
 * Headers required by vma.c, which can be substituted accordingly when testing
 * VMA functionality.
 */

#ifndef __MM_VMA_INTERNAL_H
#define __MM_VMA_INTERNAL_H

#include <winux/backing-dev.h>
#include <winux/bitops.h>
#include <winux/bug.h>
#include <winux/cacheflush.h>
#include <winux/err.h>
#include <winux/file.h>
#include <winux/fs.h>
#include <winux/huge_mm.h>
#include <winux/hugetlb.h>
#include <winux/hugetlb_inline.h>
#include <winux/kernel.h>
#include <winux/ksm.h>
#include <winux/khugepaged.h>
#include <winux/list.h>
#include <winux/maple_tree.h>
#include <winux/mempolicy.h>
#include <winux/mm.h>
#include <winux/mm_inline.h>
#include <winux/mm_types.h>
#include <winux/mman.h>
#include <winux/mmap_lock.h>
#include <winux/mmdebug.h>
#include <winux/mmu_context.h>
#include <winux/mutex.h>
#include <winux/pagemap.h>
#include <winux/perf_event.h>
#include <winux/personality.h>
#include <winux/pfn.h>
#include <winux/rcupdate.h>
#include <winux/rmap.h>
#include <winux/rwsem.h>
#include <winux/sched/signal.h>
#include <winux/security.h>
#include <winux/shmem_fs.h>
#include <winux/swap.h>
#include <winux/uprobes.h>
#include <winux/userfaultfd_k.h>

#include <asm/current.h>
#include <asm/tlb.h>

#include "internal.h"

#endif	/* __MM_VMA_INTERNAL_H */
