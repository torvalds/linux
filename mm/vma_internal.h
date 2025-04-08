/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vma_internal.h
 *
 * Headers required by vma.c, which can be substituted accordingly when testing
 * VMA functionality.
 */

#ifndef __MM_VMA_INTERNAL_H
#define __MM_VMA_INTERNAL_H

#include <linux/backing-dev.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cacheflush.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/huge_mm.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_inline.h>
#include <linux/kernel.h>
#include <linux/ksm.h>
#include <linux/khugepaged.h>
#include <linux/list.h>
#include <linux/maple_tree.h>
#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/mmap_lock.h>
#include <linux/mmdebug.h>
#include <linux/mmu_context.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/perf_event.h>
#include <linux/personality.h>
#include <linux/pfn.h>
#include <linux/rcupdate.h>
#include <linux/rmap.h>
#include <linux/rwsem.h>
#include <linux/sched/signal.h>
#include <linux/security.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>
#include <linux/uprobes.h>
#include <linux/userfaultfd_k.h>

#include <asm/current.h>
#include <asm/tlb.h>

#include "internal.h"

#endif	/* __MM_VMA_INTERNAL_H */
