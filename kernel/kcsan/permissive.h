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

#include <linux/types.h>

/*
 * Access ignore rules based on address.
 */
static __always_inline bool kcsan_ignore_address(const volatile void *ptr)
{
	if (!IS_ENABLED(CONFIG_KCSAN_PERMISSIVE))
		return false;

	return false;
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

	return false;
}

#endif /* _KERNEL_KCSAN_PERMISSIVE_H */
