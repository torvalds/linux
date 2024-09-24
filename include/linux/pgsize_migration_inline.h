/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_SIZE_MIGRATION_INLINE_H
#define _LINUX_PAGE_SIZE_MIGRATION_INLINE_H

/*
 * Page Size Migration
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 *
 * This file contains inline APIs for mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 */

#include <linux/mm_types.h>
#include <linux/sizes.h>

#include <asm/page.h>

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
#define VM_PAD_MASK		(VM_TOTAL_PAD_PAGES << VM_PAD_SHIFT)
#define VMA_PAD_START(vma)	(vma->vm_end - (vma_pad_pages(vma) << PAGE_SHIFT))

#if PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT)
/*
 * Sets the correct padding bits / flags for a VMA split.
 */
static inline unsigned long vma_pad_fixup_flags(struct vm_area_struct *vma,
						unsigned long newflags)
{
	if (newflags & VM_PAD_MASK)
		return (newflags & ~VM_PAD_MASK) | (vma->vm_flags & VM_PAD_MASK);
	else
		return newflags;
}
#else /* PAGE_SIZE != SZ_4K || !defined(CONFIG_64BIT) */
static inline unsigned long vma_pad_fixup_flags(struct vm_area_struct *vma,
						unsigned long newflags)
{
	return newflags;
}
#endif /* PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT) */

#endif /* _LINUX_PAGE_SIZE_MIGRATION_INLINE_H */
