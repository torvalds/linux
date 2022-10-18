/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H

#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>

#ifdef CONFIG_MMU

/*
 * swapcache pages are stored in the swapper_space radix tree.  We want to
 * get good packing density in that tree, so the index should be dense in
 * the low-order bits.
 *
 * We arrange the `type' and `offset' fields so that `type' is at the six
 * high-order bits of the swp_entry_t and `offset' is right-aligned in the
 * remaining bits.  Although `type' itself needs only five bits, we allow for
 * shmem/tmpfs to shift it all up a further one bit: see swp_to_radix_entry().
 *
 * swp_entry_t's are *never* stored anywhere in their arch-dependent format.
 */
#define SWP_TYPE_SHIFT	(BITS_PER_XA_VALUE - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK	((1UL << SWP_TYPE_SHIFT) - 1)

/* Clear all flags but only keep swp_entry_t related information */
static inline pte_t pte_swp_clear_flags(pte_t pte)
{
	if (pte_swp_exclusive(pte))
		pte = pte_swp_clear_exclusive(pte);
	if (pte_swp_soft_dirty(pte))
		pte = pte_swp_clear_soft_dirty(pte);
	if (pte_swp_uffd_wp(pte))
		pte = pte_swp_clear_uffd_wp(pte);
	return pte;
}

/*
 * Store a type+offset into a swp_entry_t in an arch-independent format
 */
static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;

	ret.val = (type << SWP_TYPE_SHIFT) | (offset & SWP_OFFSET_MASK);
	return ret;
}

/*
 * Extract the `type' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline unsigned swp_type(swp_entry_t entry)
{
	return (entry.val >> SWP_TYPE_SHIFT);
}

/*
 * Extract the `offset' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline pgoff_t swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK;
}

/* check whether a pte points to a swap entry */
static inline int is_swap_pte(pte_t pte)
{
	return !pte_none(pte) && !pte_present(pte);
}

/*
 * Convert the arch-dependent pte representation of a swp_entry_t into an
 * arch-independent swp_entry_t.
 */
static inline swp_entry_t pte_to_swp_entry(pte_t pte)
{
	swp_entry_t arch_entry;

	pte = pte_swp_clear_flags(pte);
	arch_entry = __pte_to_swp_entry(pte);
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

/*
 * Convert the arch-independent representation of a swp_entry_t into the
 * arch-dependent pte representation.
 */
static inline pte_t swp_entry_to_pte(swp_entry_t entry)
{
	swp_entry_t arch_entry;

	arch_entry = __swp_entry(swp_type(entry), swp_offset(entry));
	return __swp_entry_to_pte(arch_entry);
}

static inline swp_entry_t radix_to_swp_entry(void *arg)
{
	swp_entry_t entry;

	entry.val = xa_to_value(arg);
	return entry;
}

static inline void *swp_to_radix_entry(swp_entry_t entry)
{
	return xa_mk_value(entry.val);
}

static inline swp_entry_t make_swapin_error_entry(struct page *page)
{
	return swp_entry(SWP_SWAPIN_ERROR, page_to_pfn(page));
}

static inline int is_swapin_error_entry(swp_entry_t entry)
{
	return swp_type(entry) == SWP_SWAPIN_ERROR;
}

#if IS_ENABLED(CONFIG_DEVICE_PRIVATE)
static inline swp_entry_t make_readable_device_private_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_READ, offset);
}

static inline swp_entry_t make_writable_device_private_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_WRITE, offset);
}

static inline bool is_device_private_entry(swp_entry_t entry)
{
	int type = swp_type(entry);
	return type == SWP_DEVICE_READ || type == SWP_DEVICE_WRITE;
}

static inline bool is_writable_device_private_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_DEVICE_WRITE);
}

static inline swp_entry_t make_readable_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_EXCLUSIVE_READ, offset);
}

static inline swp_entry_t make_writable_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_EXCLUSIVE_WRITE, offset);
}

static inline bool is_device_exclusive_entry(swp_entry_t entry)
{
	return swp_type(entry) == SWP_DEVICE_EXCLUSIVE_READ ||
		swp_type(entry) == SWP_DEVICE_EXCLUSIVE_WRITE;
}

static inline bool is_writable_device_exclusive_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_DEVICE_EXCLUSIVE_WRITE);
}
#else /* CONFIG_DEVICE_PRIVATE */
static inline swp_entry_t make_readable_device_private_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline swp_entry_t make_writable_device_private_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline bool is_device_private_entry(swp_entry_t entry)
{
	return false;
}

static inline bool is_writable_device_private_entry(swp_entry_t entry)
{
	return false;
}

static inline swp_entry_t make_readable_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline swp_entry_t make_writable_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline bool is_device_exclusive_entry(swp_entry_t entry)
{
	return false;
}

static inline bool is_writable_device_exclusive_entry(swp_entry_t entry)
{
	return false;
}
#endif /* CONFIG_DEVICE_PRIVATE */

#ifdef CONFIG_MIGRATION
static inline int is_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_READ ||
			swp_type(entry) == SWP_MIGRATION_READ_EXCLUSIVE ||
			swp_type(entry) == SWP_MIGRATION_WRITE);
}

static inline int is_writable_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_WRITE);
}

static inline int is_readable_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_READ);
}

static inline int is_readable_exclusive_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_READ_EXCLUSIVE);
}

static inline swp_entry_t make_readable_migration_entry(pgoff_t offset)
{
	return swp_entry(SWP_MIGRATION_READ, offset);
}

static inline swp_entry_t make_readable_exclusive_migration_entry(pgoff_t offset)
{
	return swp_entry(SWP_MIGRATION_READ_EXCLUSIVE, offset);
}

static inline swp_entry_t make_writable_migration_entry(pgoff_t offset)
{
	return swp_entry(SWP_MIGRATION_WRITE, offset);
}

extern void __migration_entry_wait(struct mm_struct *mm, pte_t *ptep,
					spinlock_t *ptl);
extern void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					unsigned long address);
#ifdef CONFIG_HUGETLB_PAGE
extern void __migration_entry_wait_huge(pte_t *ptep, spinlock_t *ptl);
extern void migration_entry_wait_huge(struct vm_area_struct *vma, pte_t *pte);
#endif
#else
static inline swp_entry_t make_readable_migration_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline swp_entry_t make_readable_exclusive_migration_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline swp_entry_t make_writable_migration_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

static inline int is_migration_entry(swp_entry_t swp)
{
	return 0;
}

static inline void __migration_entry_wait(struct mm_struct *mm, pte_t *ptep,
					spinlock_t *ptl) { }
static inline void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					 unsigned long address) { }
#ifdef CONFIG_HUGETLB_PAGE
static inline void __migration_entry_wait_huge(pte_t *ptep, spinlock_t *ptl) { }
static inline void migration_entry_wait_huge(struct vm_area_struct *vma, pte_t *pte) { }
#endif
static inline int is_writable_migration_entry(swp_entry_t entry)
{
	return 0;
}
static inline int is_readable_migration_entry(swp_entry_t entry)
{
	return 0;
}

#endif

typedef unsigned long pte_marker;

#define  PTE_MARKER_UFFD_WP  BIT(0)
#define  PTE_MARKER_MASK     (PTE_MARKER_UFFD_WP)

#ifdef CONFIG_PTE_MARKER

static inline swp_entry_t make_pte_marker_entry(pte_marker marker)
{
	return swp_entry(SWP_PTE_MARKER, marker);
}

static inline bool is_pte_marker_entry(swp_entry_t entry)
{
	return swp_type(entry) == SWP_PTE_MARKER;
}

static inline pte_marker pte_marker_get(swp_entry_t entry)
{
	return swp_offset(entry) & PTE_MARKER_MASK;
}

static inline bool is_pte_marker(pte_t pte)
{
	return is_swap_pte(pte) && is_pte_marker_entry(pte_to_swp_entry(pte));
}

#else /* CONFIG_PTE_MARKER */

static inline swp_entry_t make_pte_marker_entry(pte_marker marker)
{
	/* This should never be called if !CONFIG_PTE_MARKER */
	WARN_ON_ONCE(1);
	return swp_entry(0, 0);
}

static inline bool is_pte_marker_entry(swp_entry_t entry)
{
	return false;
}

static inline pte_marker pte_marker_get(swp_entry_t entry)
{
	return 0;
}

static inline bool is_pte_marker(pte_t pte)
{
	return false;
}

#endif /* CONFIG_PTE_MARKER */

static inline pte_t make_pte_marker(pte_marker marker)
{
	return swp_entry_to_pte(make_pte_marker_entry(marker));
}

/*
 * This is a special version to check pte_none() just to cover the case when
 * the pte is a pte marker.  It existed because in many cases the pte marker
 * should be seen as a none pte; it's just that we have stored some information
 * onto the none pte so it becomes not-none any more.
 *
 * It should be used when the pte is file-backed, ram-based and backing
 * userspace pages, like shmem.  It is not needed upon pgtables that do not
 * support pte markers at all.  For example, it's not needed on anonymous
 * memory, kernel-only memory (including when the system is during-boot),
 * non-ram based generic file-system.  It's fine to be used even there, but the
 * extra pte marker check will be pure overhead.
 *
 * For systems configured with !CONFIG_PTE_MARKER this will be automatically
 * optimized to pte_none().
 */
static inline int pte_none_mostly(pte_t pte)
{
	return pte_none(pte) || is_pte_marker(pte);
}

static inline struct page *pfn_swap_entry_to_page(swp_entry_t entry)
{
	struct page *p = pfn_to_page(swp_offset(entry));

	/*
	 * Any use of migration entries may only occur while the
	 * corresponding page is locked
	 */
	BUG_ON(is_migration_entry(entry) && !PageLocked(p));

	return p;
}

/*
 * A pfn swap entry is a special type of swap entry that always has a pfn stored
 * in the swap offset. They are used to represent unaddressable device memory
 * and to restrict access to a page undergoing migration.
 */
static inline bool is_pfn_swap_entry(swp_entry_t entry)
{
	return is_migration_entry(entry) || is_device_private_entry(entry) ||
	       is_device_exclusive_entry(entry);
}

struct page_vma_mapped_walk;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
extern int set_pmd_migration_entry(struct page_vma_mapped_walk *pvmw,
		struct page *page);

extern void remove_migration_pmd(struct page_vma_mapped_walk *pvmw,
		struct page *new);

extern void pmd_migration_entry_wait(struct mm_struct *mm, pmd_t *pmd);

static inline swp_entry_t pmd_to_swp_entry(pmd_t pmd)
{
	swp_entry_t arch_entry;

	if (pmd_swp_soft_dirty(pmd))
		pmd = pmd_swp_clear_soft_dirty(pmd);
	if (pmd_swp_uffd_wp(pmd))
		pmd = pmd_swp_clear_uffd_wp(pmd);
	arch_entry = __pmd_to_swp_entry(pmd);
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

static inline pmd_t swp_entry_to_pmd(swp_entry_t entry)
{
	swp_entry_t arch_entry;

	arch_entry = __swp_entry(swp_type(entry), swp_offset(entry));
	return __swp_entry_to_pmd(arch_entry);
}

static inline int is_pmd_migration_entry(pmd_t pmd)
{
	return is_swap_pmd(pmd) && is_migration_entry(pmd_to_swp_entry(pmd));
}
#else
static inline int set_pmd_migration_entry(struct page_vma_mapped_walk *pvmw,
		struct page *page)
{
	BUILD_BUG();
}

static inline void remove_migration_pmd(struct page_vma_mapped_walk *pvmw,
		struct page *new)
{
	BUILD_BUG();
}

static inline void pmd_migration_entry_wait(struct mm_struct *m, pmd_t *p) { }

static inline swp_entry_t pmd_to_swp_entry(pmd_t pmd)
{
	return swp_entry(0, 0);
}

static inline pmd_t swp_entry_to_pmd(swp_entry_t entry)
{
	return __pmd(0);
}

static inline int is_pmd_migration_entry(pmd_t pmd)
{
	return 0;
}
#endif

#ifdef CONFIG_MEMORY_FAILURE

extern atomic_long_t num_poisoned_pages __read_mostly;

/*
 * Support for hardware poisoned pages
 */
static inline swp_entry_t make_hwpoison_entry(struct page *page)
{
	BUG_ON(!PageLocked(page));
	return swp_entry(SWP_HWPOISON, page_to_pfn(page));
}

static inline int is_hwpoison_entry(swp_entry_t entry)
{
	return swp_type(entry) == SWP_HWPOISON;
}

static inline unsigned long hwpoison_entry_to_pfn(swp_entry_t entry)
{
	return swp_offset(entry);
}

static inline void num_poisoned_pages_inc(void)
{
	atomic_long_inc(&num_poisoned_pages);
}

static inline void num_poisoned_pages_dec(void)
{
	atomic_long_dec(&num_poisoned_pages);
}

static inline void num_poisoned_pages_sub(long i)
{
	atomic_long_sub(i, &num_poisoned_pages);
}

#else

static inline swp_entry_t make_hwpoison_entry(struct page *page)
{
	return swp_entry(0, 0);
}

static inline int is_hwpoison_entry(swp_entry_t swp)
{
	return 0;
}

static inline void num_poisoned_pages_inc(void)
{
}

static inline void num_poisoned_pages_sub(long i)
{
}
#endif

static inline int non_swap_entry(swp_entry_t entry)
{
	return swp_type(entry) >= MAX_SWAPFILES;
}

#endif /* CONFIG_MMU */
#endif /* _LINUX_SWAPOPS_H */
