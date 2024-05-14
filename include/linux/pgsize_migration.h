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
#include <linux/seq_file.h>
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
#define VM_PAD_MASK		(VM_TOTAL_PAD_PAGES << VM_PAD_SHIFT)
#define VMA_PAD_START(vma)	(vma->vm_end - (vma_pad_pages(vma) << PAGE_SHIFT))

#if PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT)
extern void vma_set_pad_pages(struct vm_area_struct *vma,
			      unsigned long nr_pages);

extern unsigned long vma_pad_pages(struct vm_area_struct *vma);

extern void madvise_vma_pad_pages(struct vm_area_struct *vma,
				  unsigned long start, unsigned long end);

extern struct vm_area_struct *get_pad_vma(struct vm_area_struct *vma);

extern struct vm_area_struct *get_data_vma(struct vm_area_struct *vma);

extern void show_map_pad_vma(struct vm_area_struct *vma,
			     struct vm_area_struct *pad,
			     struct seq_file *m, void *func, bool smaps);

extern void split_pad_vma(struct vm_area_struct *vma, struct vm_area_struct *new,
			  unsigned long addr, int new_below);
#else /* PAGE_SIZE != SZ_4K || !defined(CONFIG_64BIT) */
static inline void vma_set_pad_pages(struct vm_area_struct *vma,
				     unsigned long nr_pages)
{
}

static inline unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
	return 0;
}

static inline void madvise_vma_pad_pages(struct vm_area_struct *vma,
					 unsigned long start, unsigned long end)
{
}

static inline struct vm_area_struct *get_pad_vma(struct vm_area_struct *vma)
{
	return NULL;
}

static inline struct vm_area_struct *get_data_vma(struct vm_area_struct *vma)
{
	return vma;
}

static inline void show_map_pad_vma(struct vm_area_struct *vma,
				    struct vm_area_struct *pad,
				    struct seq_file *m, void *func, bool smaps)
{
}

static inline void split_pad_vma(struct vm_area_struct *vma, struct vm_area_struct *new,
				 unsigned long addr, int new_below)
{
}
#endif /* PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT) */

static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma) - vma_pad_pages(vma);
}

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

/*
 * Merging of padding VMAs is uncommon, as padding is only allowed
 * from the linker context.
 *
 * To simplify the semantics, adjacent VMAs with padding are not
 * allowed to merge.
 */
static inline bool is_mergable_pad_vma(struct vm_area_struct *vma,
				       unsigned long vm_flags)
{
	/* Padding VMAs cannot be merged with other padding or real VMAs */
	return !((vma->vm_flags | vm_flags) & VM_PAD_MASK);
}
#endif /* _LINUX_PAGE_SIZE_MIGRATION_H */
