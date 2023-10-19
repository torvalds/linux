/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_ACCESS_OK_H__
#define __ASM_GENERIC_ACCESS_OK_H__

/*
 * Checking whether a pointer is valid for user space access.
 * These definitions work on most architectures, but overrides can
 * be used where necessary.
 */

/*
 * architectures with compat tasks have a variable TASK_SIZE and should
 * override this to a constant.
 */
#ifndef TASK_SIZE_MAX
#define TASK_SIZE_MAX			TASK_SIZE
#endif

#ifndef __access_ok
/*
 * 'size' is a compile-time constant for most callers, so optimize for
 * this case to turn the check into a single comparison against a constant
 * limit and catch all possible overflows.
 * On architectures with separate user address space (m68k, s390, parisc,
 * sparc64) or those without an MMU, this should always return true.
 *
 * This version was originally contributed by Jonas Bonn for the
 * OpenRISC architecture, and was found to be the most efficient
 * for constant 'size' and 'limit' values.
 */
static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	if (IS_ENABLED(CONFIG_ALTERNATE_USER_ADDRESS_SPACE) ||
	    !IS_ENABLED(CONFIG_MMU))
		return true;

	return (size <= limit) && (addr <= (limit - size));
}
#endif

#ifndef access_ok
#define access_ok(addr, size) likely(__access_ok(addr, size))
#endif

#endif
