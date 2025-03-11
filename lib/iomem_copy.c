// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Kalray, Inc.  All Rights Reserved.
 */

#include <linux/align.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#ifndef memset_io
/**
 * memset_io() - Set a range of I/O memory to a constant value
 * @addr: The beginning of the I/O-memory range to set
 * @val: The value to set the memory to
 * @count: The number of bytes to set
 *
 * Set a range of I/O memory to a given value.
 */
void memset_io(volatile void __iomem *addr, int val, size_t count)
{
	long qc = (u8)val;

	qc *= ~0UL / 0xff;

	while (count && !IS_ALIGNED((long)addr, sizeof(long))) {
		__raw_writeb(val, addr);
		addr++;
		count--;
	}

	while (count >= sizeof(long)) {
#ifdef CONFIG_64BIT
		__raw_writeq(qc, addr);
#else
		__raw_writel(qc, addr);
#endif

		addr += sizeof(long);
		count -= sizeof(long);
	}

	while (count) {
		__raw_writeb(val, addr);
		addr++;
		count--;
	}
}
EXPORT_SYMBOL(memset_io);
#endif

#ifndef memcpy_fromio
/**
 * memcpy_fromio() - Copy a block of data from I/O memory
 * @dst: The (RAM) destination for the copy
 * @src: The (I/O memory) source for the data
 * @count: The number of bytes to copy
 *
 * Copy a block of data from I/O memory.
 */
void memcpy_fromio(void *dst, const volatile void __iomem *src, size_t count)
{
	while (count && !IS_ALIGNED((long)src, sizeof(long))) {
		*(u8 *)dst = __raw_readb(src);
		src++;
		dst++;
		count--;
	}

	while (count >= sizeof(long)) {
#ifdef CONFIG_64BIT
		long val = __raw_readq(src);
#else
		long val = __raw_readl(src);
#endif
		put_unaligned(val, (long *)dst);


		src += sizeof(long);
		dst += sizeof(long);
		count -= sizeof(long);
	}

	while (count) {
		*(u8 *)dst = __raw_readb(src);
		src++;
		dst++;
		count--;
	}
}
EXPORT_SYMBOL(memcpy_fromio);
#endif

#ifndef memcpy_toio
/**
 * memcpy_toio() -Copy a block of data into I/O memory
 * @dst: The (I/O memory) destination for the copy
 * @src: The (RAM) source for the data
 * @count: The number of bytes to copy
 *
 * Copy a block of data to I/O memory.
 */
void memcpy_toio(volatile void __iomem *dst, const void *src, size_t count)
{
	while (count && !IS_ALIGNED((long)dst, sizeof(long))) {
		__raw_writeb(*(u8 *)src, dst);
		src++;
		dst++;
		count--;
	}

	while (count >= sizeof(long)) {
		long val = get_unaligned((long *)src);
#ifdef CONFIG_64BIT
		__raw_writeq(val, dst);
#else
		__raw_writel(val, dst);
#endif

		src += sizeof(long);
		dst += sizeof(long);
		count -= sizeof(long);
	}

	while (count) {
		__raw_writeb(*(u8 *)src, dst);
		src++;
		dst++;
		count--;
	}
}
EXPORT_SYMBOL(memcpy_toio);
#endif


