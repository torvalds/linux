// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 * Author: Mostafa Saleh <smostafa@google.com>
 * IOMMU API debug page alloc sanitizer
 */

#ifndef __LINUX_IOMMU_DEBUG_PAGEALLOC_H
#define __LINUX_IOMMU_DEBUG_PAGEALLOC_H

#ifdef CONFIG_IOMMU_DEBUG_PAGEALLOC
DECLARE_STATIC_KEY_FALSE(iommu_debug_initialized);

extern struct page_ext_operations page_iommu_debug_ops;

void __iommu_debug_check_unmapped(const struct page *page, int numpages);

static inline void iommu_debug_check_unmapped(const struct page *page, int numpages)
{
	if (static_branch_unlikely(&iommu_debug_initialized))
		__iommu_debug_check_unmapped(page, numpages);
}

#else
static inline void iommu_debug_check_unmapped(const struct page *page,
					      int numpages)
{
}

#endif /* CONFIG_IOMMU_DEBUG_PAGEALLOC */

#endif /* __LINUX_IOMMU_DEBUG_PAGEALLOC_H */
