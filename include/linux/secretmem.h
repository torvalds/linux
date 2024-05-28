/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_SECRETMEM_H
#define _LINUX_SECRETMEM_H

#ifdef CONFIG_SECRETMEM

extern const struct address_space_operations secretmem_aops;

static inline bool folio_is_secretmem(struct folio *folio)
{
	struct address_space *mapping;

	/*
	 * Using folio_mapping() is quite slow because of the actual call
	 * instruction.
	 * We know that secretmem pages are not compound, so we can
	 * save a couple of cycles here.
	 */
	if (folio_test_large(folio))
		return false;

	mapping = (struct address_space *)
		((unsigned long)folio->mapping & ~PAGE_MAPPING_FLAGS);

	if (!mapping || mapping != folio->mapping)
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

static inline bool folio_is_secretmem(struct folio *folio)
{
	return false;
}

static inline bool secretmem_active(void)
{
	return false;
}

#endif /* CONFIG_SECRETMEM */

#endif /* _LINUX_SECRETMEM_H */
