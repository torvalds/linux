/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_PAGE_H
#define __VDSO_PAGE_H

#include <uapi/linux/const.h>

/*
 * PAGE_SHIFT determines the page size.
 *
 * Note: This definition is required because PAGE_SHIFT is used
 * in several places throuout the codebase.
 */
#define PAGE_SHIFT      CONFIG_PAGE_SHIFT

#define PAGE_SIZE	(_AC(1,UL) << CONFIG_PAGE_SHIFT)

#if !defined(CONFIG_64BIT)
/*
 * Applies only to 32-bit architectures.
 *
 * Subtle: (1 << CONFIG_PAGE_SHIFT) is an int, not an unsigned long.
 * So if we assign PAGE_MASK to a larger type it gets extended the
 * way we want (i.e. with 1s in the high bits) while masking a
 * 64-bit value such as phys_addr_t.
 */
#define PAGE_MASK	(~((1 << CONFIG_PAGE_SHIFT) - 1))
#else
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#endif

#endif	/* __VDSO_PAGE_H */
