/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_SECRETMEM_H
#define _LINUX_SECRETMEM_H

#ifdef CONFIG_SECRETMEM

extern const struct address_space_operations secretmem_aops;

static inline bool page_is_secretmem(struct page *page)
{
	struct address_space *mapping;

	/*
	 * Using page_mapping() is quite slow because of the actual call
	 * instruction and repeated compound_head(page) inside the
	 * page_mapping() function.
	 * We know that secretmem pages are not compound and LRU so we can
	 * save a couple of cycles here.
	 */
	if (PageCompound(page) || !PageLRU(page))
		return false;

	mapping = (struct address_space *)
		((unsigned long)page->mapping & ~PAGE_MAPPING_FLAGS);

	if (!mapping || mapping != page->mapping)
		return false;

	return mapping->a_ops == &secretmem_aops;
}

bool vma_is_secretmem(struct vm_area_struct *vma);
bool secretmem_active(void);

#else

static inline bool vma_is_secretmem(struct vm_area_struct *vma)
{
	return false;
}

static inline bool page_is_secretmem(struct page *page)
{
	return false;
}

static inline bool secretmem_active(void)
{
	return false;
}

#endif /* CONFIG_SECRETMEM */

#endif /* _LINUX_SECRETMEM_H */
