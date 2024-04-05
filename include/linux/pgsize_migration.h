/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_SIZE_MIGRATION_H
#define _LINUX_PAGE_SIZE_MIGRATION_H

/*
 * Page Size Migration
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 *
 * This file contains the APIs for mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 */

#include <linux/mm.h>
#include <linux/sizes.h>

/*
 * vm_flags representation of VMA padding pages.
 *
 * This allows the kernel to identify the portion of an ELF LOAD segment VMA
 * that is padding.
 *
 * 4 high bits of vm_flags [63,60] are used to represent ELF segment padding
 * up to 60kB, which is sufficient for ELFs of both 16kB and 64kB segment
 * alignment (p_align).
 *
 * The representation is illustrated below.
 *
 *                    63        62        61        60
 *                _________ _________ _________ _________
 *               |  Bit 3  |  Bit 2  |  Bit 1  |  Bit 0  |
 *               | of  4kB | of  4kB | of  4kB | of  4kB |
 *               |  chunks |  chunks |  chunks |  chunks |
 *               |_________|_________|_________|_________|
 */

#define VM_PAD_WIDTH		4
#define VM_PAD_SHIFT		(BITS_PER_LONG - VM_PAD_WIDTH)
#define VM_TOTAL_PAD_PAGES	((1ULL << VM_PAD_WIDTH) - 1)

#if PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT)
extern void vma_set_pad_pages(struct vm_area_struct *vma,
			      unsigned long nr_pages);

extern unsigned long vma_pad_pages(struct vm_area_struct *vma);
#else /* PAGE_SIZE != SZ_4K || !defined(CONFIG_64BIT) */
static inline void vma_set_pad_pages(struct vm_area_struct *vma,
				     unsigned long nr_pages)
{
}

static inline unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
	return 0;
}
#endif /* PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT) */

static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma) - vma_pad_pages(vma);
}
#endif /* _LINUX_PAGE_SIZE_MIGRATION_H */
