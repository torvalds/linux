/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HUGETLB_INLINE_H
#define _LINUX_HUGETLB_INLINE_H

#include <linux/mm.h>

#ifdef CONFIG_HUGETLB_PAGE

static inline bool is_vma_hugetlb_flags(const vma_flags_t *flags)
{
	return vma_flags_test(flags, VMA_HUGETLB_BIT);
}

#else

static inline bool is_vma_hugetlb_flags(const vma_flags_t *flags)
{
	return false;
}

#endif

static inline bool is_vm_hugetlb_page(const struct vm_area_struct *vma)
{
	return is_vma_hugetlb_flags(&vma->flags);
}

#endif
