/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_MM_H
#define _TOOLS_LINUX_MM_H

#include <linux/mmzone.h>
#include <uapi/linux/const.h>

#define PAGE_SHIFT		12
#define PAGE_SIZE		(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))

#define PHYS_ADDR_MAX	(~(phys_addr_t)0)

#define __ALIGN_KERNEL(x, a)		__ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define ALIGN(x, a)			__ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)		__ALIGN_KERNEL((x) - ((a) - 1), (a))

#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

#define __va(x) ((void *)((unsigned long)(x)))
#define __pa(x) ((unsigned long)(x))

#define pfn_to_page(pfn) ((void *)((pfn) * PAGE_SIZE))

#define phys_to_virt phys_to_virt
static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

void reserve_bootmem_region(phys_addr_t start, phys_addr_t end);

static inline void totalram_pages_inc(void)
{
}

static inline void totalram_pages_add(long count)
{
}

#endif
