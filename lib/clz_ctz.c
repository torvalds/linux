/*
 * lib/clz_ctz.c
 *
 * Copyright (C) 2013 Chanho Min <chanho.min@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * __c[lt]z[sd]i2 can be overridden by linking arch-specific versions.
 */

#include <linux/export.h>
#include <linux/kernel.h>

int __weak __ctzsi2(int val)
{
	return __ffs(val);
}
EXPORT_SYMBOL(__ctzsi2);

int __weak __clzsi2(int val)
{
	return 32 - fls(val);
}
EXPORT_SYMBOL(__clzsi2);

#if BITS_PER_LONG == 32

int __weak __clzdi2(long val)
{
	return 32 - fls((int)val);
}
EXPORT_SYMBOL(__clzdi2);

int __weak __ctzdi2(long val)
{
	return __ffs((u32)val);
}
EXPORT_SYMBOL(__ctzdi2);

#elif BITS_PER_LONG == 64

int __weak __clzdi2(long val)
{
	return 64 - fls64((u64)val);
}
EXPORT_SYMBOL(__clzdi2);

int __weak __ctzdi2(long val)
{
	return __ffs64((u64)val);
}
EXPORT_SYMBOL(__ctzdi2);

#else
#error BITS_PER_LONG not 32 or 64
#endif
