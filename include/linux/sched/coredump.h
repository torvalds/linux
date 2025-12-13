/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_COREDUMP_H
#define _LINUX_SCHED_COREDUMP_H

#include <linux/mm_types.h>

#define SUID_DUMP_DISABLE	0	/* No setuid dumping */
#define SUID_DUMP_USER		1	/* Dump as user of process */
#define SUID_DUMP_ROOT		2	/* Dump as root */

static inline unsigned long __mm_flags_get_dumpable(struct mm_struct *mm)
{
	/*
	 * By convention, dumpable bits are contained in first 32 bits of the
	 * bitmap, so we can simply access this first unsigned long directly.
	 */
	return __mm_flags_get_word(mm);
}

static inline void __mm_flags_set_mask_dumpable(struct mm_struct *mm, int value)
{
	__mm_flags_set_mask_bits_word(mm, MMF_DUMPABLE_MASK, value);
}

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
	unsigned long flags = __mm_flags_get_dumpable(mm);

	return __get_dumpable(flags);
}

#endif /* _LINUX_SCHED_COREDUMP_H */
