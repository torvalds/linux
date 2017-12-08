/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_VMACACHE_H
#define __LINUX_VMACACHE_H

#include <linux/sched.h>
#include <linux/mm.h>

/*
 * Hash based on the page number. Provides a good hit rate for
 * workloads with good locality and those with random accesses as well.
 */
#define VMACACHE_HASH(addr) ((addr >> PAGE_SHIFT) & VMACACHE_MASK)

static inline void vmacache_flush(struct task_struct *tsk)
{
	memset(tsk->vmacache.vmas, 0, sizeof(tsk->vmacache.vmas));
}

extern void vmacache_flush_all(struct mm_struct *mm);
extern void vmacache_update(unsigned long addr, struct vm_area_struct *newvma);
extern struct vm_area_struct *vmacache_find(struct mm_struct *mm,
						    unsigned long addr);

#ifndef CONFIG_MMU
extern struct vm_area_struct *vmacache_find_exact(struct mm_struct *mm,
						  unsigned long start,
						  unsigned long end);
#endif

static inline void vmacache_invalidate(struct mm_struct *mm)
{
	mm->vmacache_seqnum++;

	/* deal with overflows */
	if (unlikely(mm->vmacache_seqnum == 0))
		vmacache_flush_all(mm);
}

#endif /* __LINUX_VMACACHE_H */
