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

#include <linux/pgsize_migration_inline.h>
#include <linux/seq_file.h>
#include <linux/mm.h>

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

extern bool is_mergable_pad_vma(struct vm_area_struct *vma,
				unsigned long vm_flags);

extern unsigned long vma_data_pages(struct vm_area_struct *vma);
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

static inline bool is_mergable_pad_vma(struct vm_area_struct *vma,
				       unsigned long vm_flags)
{
	return true;
}

static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma);
}
#endif /* PAGE_SIZE == SZ_4K && defined(CONFIG_64BIT) */
#endif /* _LINUX_PAGE_SIZE_MIGRATION_H */
