/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Special rules for ignoring entire classes of data-racy memory accesses. None
 * of the rules here imply that such data races are generally safe!
 *
 * All rules in this file can be configured via CONFIG_KCSAN_PERMISSIVE. Keep
 * them separate from core code to make it easier to audit.
 *
 * Copyright (C) 2019, Google LLC.
 */

#ifndef _KERNEL_KCSAN_PERMISSIVE_H
#define _KERNEL_KCSAN_PERMISSIVE_H

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/types.h>

/*
 * Access ignore rules based on address.
 */
static __always_inline bool kcsan_ignore_address(const volatile void *ptr)
{
	if (!IS_ENABLED(CONFIG_KCSAN_PERMISSIVE))
		return false;

	/*
	 * Data-racy bitops on current->flags are too common, ignore completely
	 * for now.
	 */
	return ptr == &current->flags;
}

/*
 * Data race ignore rules based on access type and value change patterns.
 */
static bool
kcsan_ignore_data_race(size_t size, int type, u64 old, u64 new, u64 diff)
{
	if (!IS_ENABLED(CONFIG_KCSAN_PERMISSIVE))
		return false;

	/*
	 * Rules here are only for plain read accesses, so that we still report
	 * data races between plain read-write accesses.
	 */
	if (type || size > sizeof(long))
		return false;

	/*
	 * A common pattern is checking/setting just 1 bit in a variable; for
	 * example:
	 *
	 *	if (flags & SOME_FLAG) { ... }
	 *
	 * and elsewhere flags is updated concurrently:
	 *
	 *	flags |= SOME_OTHER_FLAG; // just 1 bit
	 *
	 * While it is still recommended that such accesses be marked
	 * appropriately, in many cases these types of data races are so common
	 * that marking them all is often unrealistic and left to maintainer
	 * preference.
	 *
	 * The assumption in all cases is that with all known compiler
	 * optimizations (including those that tear accesses), because no more
	 * than 1 bit changed, the plain accesses are safe despite the presence
	 * of data races.
	 *
	 * The rules here will ignore the data races if we observe no more than
	 * 1 bit changed.
	 *
	 * Of course many operations can effecively change just 1 bit, but the
	 * general assuption that data races involving 1-bit changes can be
	 * tolerated still applies.
	 *
	 * And in case a true bug is missed, the bug likely manifests as a
	 * reportable data race elsewhere.
	 */
	if (hweight64(diff) == 1) {
		/*
		 * Exception: Report data races where the values look like
		 * ordinary booleans (one of them was 0 and the 0th bit was
		 * changed) More often than not, they come with interesting
		 * memory ordering requirements, so let's report them.
		 */
		if (!((!old || !new) && diff == 1))
			return true;
	}

	return false;
}

#endif /* _KERNEL_KCSAN_PERMISSIVE_H */
