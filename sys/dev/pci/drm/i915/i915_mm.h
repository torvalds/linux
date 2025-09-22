/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_MM_H__
#define __I915_MM_H__

#include <linux/bug.h>
#include <linux/types.h>

#ifdef __linux__
struct vm_area_struct;
#endif
struct io_mapping;
struct scatterlist;

#ifdef notyet
#if IS_ENABLED(CONFIG_X86)
int remap_io_mapping(struct vm_area_struct *vma,
		     unsigned long addr, unsigned long pfn, unsigned long size,
		     struct io_mapping *iomap);
#else
static inline
int remap_io_mapping(struct vm_area_struct *vma,
		     unsigned long addr, unsigned long pfn, unsigned long size,
		     struct io_mapping *iomap)
{
	WARN_ONCE(1, "Architecture has no drm_cache.c support\n");
	return 0;
}
#endif
#endif /* notyet */

#ifdef __linux__
int remap_io_sg(struct vm_area_struct *vma,
		unsigned long addr, unsigned long size,
		struct scatterlist *sgl, unsigned long offset,
		resource_size_t iobase);
#endif

#endif /* __I915_MM_H__ */
