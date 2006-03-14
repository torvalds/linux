/*
 * Copyright 2006 PathScale, Inc.  All Rights Reserved.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/io.h>

/**
 * __iowrite32_copy - copy data to MMIO space, in 32-bit units
 * @to: destination, in MMIO space (must be 32-bit aligned)
 * @from: source (must be 32-bit aligned)
 * @count: number of 32-bit quantities to copy
 *
 * Copy data from kernel space to MMIO space, in units of 32 bits at a
 * time.  Order of access is not guaranteed, nor is a memory barrier
 * performed afterwards.
 */
void __attribute__((weak)) __iowrite32_copy(void __iomem *to,
					    const void *from,
					    size_t count)
{
	u32 __iomem *dst = to;
	const u32 *src = from;
	const u32 *end = src + count;

	while (src < end)
		__raw_writel(*src++, dst++);
}
EXPORT_SYMBOL_GPL(__iowrite32_copy);
