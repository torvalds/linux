/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2017, Michael Ellerman, IBM Corporation.
 */
#ifndef _LINUX_SET_MEMORY_H_
#define _LINUX_SET_MEMORY_H_

/**
 * DOC: Kernel page table permissions
 *
 * The set_memory() and set_direct_map() APIs update permissions of existing
 * kernel mappings.
 *
 * The set_memory() functions operate on a range of kernel virtual addresses,
 * the set_direct_map() functions operate on the direct map.
 *
 * The updates are not atomic: when a call fails, an arbitrary prefix of the
 * range may have been updated already and there is no automatic rollback.
 * A caller must restore the required permissions before reusing or freeing
 * the memory.
 *
 * When an architecture does not implement these APIs they succeed without
 * doing anything, so a return value of 0 does not mean that the permissions
 * were actually changed.
 *
 * Callers that depend on the permissions being applied must ensure that the
 * architecture supports the required operation for the target addresses. The
 * Kconfig symbols alone do not guarantee this.
 *
 * See Documentation/mm/kernel-page-tables.rst for the details and for the
 * differences between the architecture implementations.
 */

#ifdef CONFIG_ARCH_HAS_SET_MEMORY
#include <asm/set_memory.h>
#else
/**
 * set_memory_ro - make a kernel mapping read-only
 * @addr: page aligned start of the kernel virtual address range
 * @numpages: number of pages in the range
 *
 * Flushes the TLB for the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int __must_check set_memory_ro(unsigned long addr, int numpages) { return 0; }

/**
 * set_memory_rw - make a kernel mapping writable
 * @addr: page aligned start of the kernel virtual address range
 * @numpages: number of pages in the range
 *
 * Flushes the TLB for the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int __must_check set_memory_rw(unsigned long addr, int numpages) { return 0; }

/**
 * set_memory_x - make a kernel mapping executable
 * @addr: page aligned start of the kernel virtual address range
 * @numpages: number of pages in the range
 *
 * Flushes the TLB for the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int __must_check set_memory_x(unsigned long addr,  int numpages) { return 0; }

/**
 * set_memory_nx - make a kernel mapping non-executable
 * @addr: page aligned start of the kernel virtual address range
 * @numpages: number of pages in the range
 *
 * Flushes the TLB for the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int __must_check set_memory_nx(unsigned long addr, int numpages) { return 0; }
#endif

#ifndef set_memory_rox
/**
 * set_memory_rox - make a kernel mapping read-only and executable
 * @addr: page aligned start of the kernel virtual address range
 * @numpages: number of pages in the range
 *
 * A failure may leave the range read-only but not executable.
 *
 * Flushes the TLB for the range.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int set_memory_rox(unsigned long addr, int numpages)
{
	int ret = set_memory_ro(addr, numpages);
	if (ret)
		return ret;
	return set_memory_x(addr, numpages);
}
#endif

#ifndef CONFIG_ARCH_HAS_SET_DIRECT_MAP
/**
 * set_direct_map_invalid_noflush - remove pages from the direct map
 * @page: first page to update
 * @nr: number of pages to update
 *
 * Makes the direct mapping of @nr pages starting at @page not present.
 * The caller is responsible for any required TLB flushing.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int set_direct_map_invalid_noflush(struct page *page,
						 unsigned int nr)
{
	return 0;
}

/**
 * set_direct_map_default_noflush - restore the direct map of pages
 * @page: first page to update
 * @nr: number of pages to update
 *
 * Restores the default kernel permissions of the direct mapping of @nr
 * pages starting at @page.
 * The caller is responsible for any required TLB flushing.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int set_direct_map_default_noflush(struct page *page,
						 unsigned int nr)
{
	return 0;
}

static inline bool kernel_page_present(struct page *page)
{
	return true;
}
#else /* CONFIG_ARCH_HAS_SET_DIRECT_MAP */
/*
 * Some architectures, e.g. ARM64 can disable direct map modifications at
 * boot time. Let them overrive this query.
 */
#ifndef can_set_direct_map
/**
 * can_set_direct_map - check if the direct map can be modified
 *
 * Available with CONFIG_ARCH_HAS_SET_DIRECT_MAP. Architectures may override
 * this to report whether direct map updates are enabled at runtime.
 * Even though the generic implementation returns true this does not guarantee
 * that every address can be updated.
 *
 * See Documentation/mm/kernel-page-tables.rst for the details
 *
 * Return: true unless the architecture reports direct map updates disabled.
 */
static inline bool can_set_direct_map(void)
{
	return true;
}
#define can_set_direct_map can_set_direct_map
#endif
#endif /* CONFIG_ARCH_HAS_SET_DIRECT_MAP */

#ifdef CONFIG_X86_64
int set_mce_nospec(unsigned long pfn);
int clear_mce_nospec(unsigned long pfn);
#else
static inline int set_mce_nospec(unsigned long pfn)
{
	return 0;
}
static inline int clear_mce_nospec(unsigned long pfn)
{
	return 0;
}
#endif

#ifndef CONFIG_ARCH_HAS_MEM_ENCRYPT
static inline int set_memory_encrypted(unsigned long addr, int numpages)
{
	return 0;
}

static inline int set_memory_decrypted(unsigned long addr, int numpages)
{
	return 0;
}
#endif /* CONFIG_ARCH_HAS_MEM_ENCRYPT */

#endif /* _LINUX_SET_MEMORY_H_ */
