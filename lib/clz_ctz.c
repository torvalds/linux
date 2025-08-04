// SPDX-License-Identifier: GPL-2.0-only
/*
 * lib/clz_ctz.c
 *
 * Copyright (C) 2013 Chanho Min <chanho.min@lge.com>
 *
 * The functions in this file aren't called directly, but are required by
 * GCC builtins such as __builtin_ctz, and therefore they can't be removed
 * despite appearing unreferenced in kernel source.
 *
 * __c[lt]z[sd]i2 can be overridden by linking arch-specific versions.
 */

#include <linux/export.h>
#include <linux/kernel.h>

int __weak __ctzsi2(int val);
int __weak __attribute_const__ __ctzsi2(int val)
{
	return __ffs(val);
}
EXPORT_SYMBOL(__ctzsi2);

int __weak __clzsi2(int val);
int __weak __attribute_const__ __clzsi2(int val)
{
	return 32 - fls(val);
}
EXPORT_SYMBOL(__clzsi2);

int __weak __clzdi2(u64 val);
int __weak __attribute_const__ __clzdi2(u64 val)
{
	return 64 - fls64(val);
}
EXPORT_SYMBOL(__clzdi2);

int __weak __ctzdi2(u64 val);
int __weak __attribute_const__ __ctzdi2(u64 val)
{
	return __ffs64(val);
}
EXPORT_SYMBOL(__ctzdi2);
