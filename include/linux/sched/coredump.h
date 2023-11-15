/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_COREDUMP_H
#define _LINUX_SCHED_COREDUMP_H

#include <linux/mm_types.h>

#define SUID_DUMP_DISABLE	0	/* No setuid dumping */
#define SUID_DUMP_USER		1	/* Dump as user of process */
#define SUID_DUMP_ROOT		2	/* Dump as root */

/* mm flags */

/* for SUID_DUMP_* above */
#define MMF_DUMPABLE_BITS 2
#define MMF_DUMPABLE_MASK ((1 << MMF_DUMPABLE_BITS) - 1)

extern void set_dumpable(struct mm_struct *mm, int value);
/*
 * This returns the actual value of the suid_dumpable flag. For things
 * that are using this for checking for privilege transitions, it must
 * test against SUID_DUMP_USER rather than treating it as a boolean
 * value.
 */
static inline int __get_dumpable(unsigned long mm_flags)
{
	return mm_flags & MMF_DUMPABLE_MASK;
}

static inline int get_dumpable(struct mm_struct *mm)
{
	return __get_dumpable(mm->flags);
}

/* coredump filter bits */
#define MMF_DUMP_ANON_PRIVATE	2
#define MMF_DUMP_ANON_SHARED	3
#define MMF_DUMP_MAPPED_PRIVATE	4
#define MMF_DUMP_MAPPED_SHARED	5
#define MMF_DUMP_ELF_HEADERS	6
#define MMF_DUMP_HUGETLB_PRIVATE 7
#define MMF_DUMP_HUGETLB_SHARED  8
#define MMF_DUMP_DAX_PRIVATE	9
#define MMF_DUMP_DAX_SHARED	10

#define MMF_DUMP_FILTER_SHIFT	MMF_DUMPABLE_BITS
#define MMF_DUMP_FILTER_BITS	9
#define MMF_DUMP_FILTER_MASK \
	(((1 << MMF_DUMP_FILTER_BITS) - 1) << MMF_DUMP_FILTER_SHIFT)
#define MMF_DUMP_FILTER_DEFAULT \
	((1 << MMF_DUMP_ANON_PRIVATE) |	(1 << MMF_DUMP_ANON_SHARED) |\
	 (1 << MMF_DUMP_HUGETLB_PRIVATE) | MMF_DUMP_MASK_DEFAULT_ELF)

#ifdef CONFIG_CORE_DUMP_DEFAULT_ELF_HEADERS
# define MMF_DUMP_MASK_DEFAULT_ELF	(1 << MMF_DUMP_ELF_HEADERS)
#else
# define MMF_DUMP_MASK_DEFAULT_ELF	0
#endif
					/* leave room for more dump flags */
#define MMF_VM_MERGEABLE	16	/* KSM may merge identical pages */
#define MMF_VM_HUGEPAGE		17	/* set when mm is available for
					   khugepaged */
/*
 * This one-shot flag is dropped due to necessity of changing exe once again
 * on NFS restore
 */
//#define MMF_EXE_FILE_CHANGED	18	/* see prctl_set_mm_exe_file() */

#define MMF_HAS_UPROBES		19	/* has uprobes */
#define MMF_RECALC_UPROBES	20	/* MMF_HAS_UPROBES can be wrong */
#define MMF_OOM_SKIP		21	/* mm is of no interest for the OOM killer */
#define MMF_UNSTABLE		22	/* mm is unstable for copy_from_user */
#define MMF_HUGE_ZERO_PAGE	23      /* mm has ever used the global huge zero page */
#define MMF_DISABLE_THP		24	/* disable THP for all VMAs */
#define MMF_DISABLE_THP_MASK	(1 << MMF_DISABLE_THP)
#define MMF_OOM_REAP_QUEUED	25	/* mm was queued for oom_reaper */
#define MMF_MULTIPROCESS	26	/* mm is shared between processes */
/*
 * MMF_HAS_PINNED: Whether this mm has pinned any pages.  This can be either
 * replaced in the future by mm.pinned_vm when it becomes stable, or grow into
 * a counter on its own. We're aggresive on this bit for now: even if the
 * pinned pages were unpinned later on, we'll still keep this bit set for the
 * lifecycle of this mm, just for simplicity.
 */
#define MMF_HAS_PINNED		27	/* FOLL_PIN has run, never cleared */

#define MMF_HAS_MDWE		28
#define MMF_HAS_MDWE_MASK	(1 << MMF_HAS_MDWE)


#define MMF_HAS_MDWE_NO_INHERIT	29

#define MMF_VM_MERGE_ANY	30
#define MMF_VM_MERGE_ANY_MASK	(1 << MMF_VM_MERGE_ANY)

#define MMF_INIT_MASK		(MMF_DUMPABLE_MASK | MMF_DUMP_FILTER_MASK |\
				 MMF_DISABLE_THP_MASK | MMF_HAS_MDWE_MASK |\
				 MMF_VM_MERGE_ANY_MASK)

static inline unsigned long mmf_init_flags(unsigned long flags)
{
	if (flags & (1UL << MMF_HAS_MDWE_NO_INHERIT))
		flags &= ~((1UL << MMF_HAS_MDWE) |
			   (1UL << MMF_HAS_MDWE_NO_INHERIT));
	return flags & MMF_INIT_MASK;
}

#endif /* _LINUX_SCHED_COREDUMP_H */
