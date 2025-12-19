/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SWAPOPS_H
#define _LINUX_SWAPOPS_H

#include <linux/radix-tree.h>
#include <linux/bug.h>
#include <linux/mm_types.h>

#ifdef CONFIG_MMU

#ifdef CONFIG_SWAP
#include <linux/swapfile.h>
#endif	/* CONFIG_SWAP */

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

/*
 * Definitions only for PFN swap entries (see leafeant_has_pfn()).  To
 * store PFN, we only need SWP_PFN_BITS bits.  Each of the pfn swap entries
 * can use the extra bits to store other information besides PFN.
 */
#ifdef MAX_PHYSMEM_BITS
#define SWP_PFN_BITS		(MAX_PHYSMEM_BITS - PAGE_SHIFT)
#else  /* MAX_PHYSMEM_BITS */
#define SWP_PFN_BITS		min_t(int, \
				      sizeof(phys_addr_t) * 8 - PAGE_SHIFT, \
				      SWP_TYPE_SHIFT)
#endif	/* MAX_PHYSMEM_BITS */
#define SWP_PFN_MASK		(BIT(SWP_PFN_BITS) - 1)

/**
 * Migration swap entry specific bitfield definitions.  Layout:
 *
 *   |----------+--------------------|
 *   | swp_type | swp_offset         |
 *   |----------+--------+-+-+-------|
 *   |          | resv   |D|A|  PFN  |
 *   |----------+--------+-+-+-------|
 *
 * @SWP_MIG_YOUNG_BIT: Whether the page used to have young bit set (bit A)
 * @SWP_MIG_DIRTY_BIT: Whether the page used to have dirty bit set (bit D)
 *
 * Note: A/D bits will be stored in migration entries iff there're enough
 * free bits in arch specific swp offset.  By default we'll ignore A/D bits
 * when migrating a page.  Please refer to migration_entry_supports_ad()
 * for more information.  If there're more bits besides PFN and A/D bits,
 * they should be reserved and always be zeros.
 */
#define SWP_MIG_YOUNG_BIT		(SWP_PFN_BITS)
#define SWP_MIG_DIRTY_BIT		(SWP_PFN_BITS + 1)
#define SWP_MIG_TOTAL_BITS		(SWP_PFN_BITS + 2)

#define SWP_MIG_YOUNG			BIT(SWP_MIG_YOUNG_BIT)
#define SWP_MIG_DIRTY			BIT(SWP_MIG_DIRTY_BIT)

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

#if IS_ENABLED(CONFIG_DEVICE_PRIVATE)
static inline swp_entry_t make_readable_device_private_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_READ, offset);
}

static inline swp_entry_t make_writable_device_private_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_WRITE, offset);
}

static inline swp_entry_t make_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(SWP_DEVICE_EXCLUSIVE, offset);
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

static inline swp_entry_t make_device_exclusive_entry(pgoff_t offset)
{
	return swp_entry(0, 0);
}

#endif /* CONFIG_DEVICE_PRIVATE */

#ifdef CONFIG_MIGRATION

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

/*
 * Returns whether the host has large enough swap offset field to support
 * carrying over pgtable A/D bits for page migrations.  The result is
 * pretty much arch specific.
 */
static inline bool migration_entry_supports_ad(void)
{
#ifdef CONFIG_SWAP
	return swap_migration_ad_supported;
#else  /* CONFIG_SWAP */
	return false;
#endif	/* CONFIG_SWAP */
}

static inline swp_entry_t make_migration_entry_young(swp_entry_t entry)
{
	if (migration_entry_supports_ad())
		return swp_entry(swp_type(entry),
				 swp_offset(entry) | SWP_MIG_YOUNG);
	return entry;
}

static inline swp_entry_t make_migration_entry_dirty(swp_entry_t entry)
{
	if (migration_entry_supports_ad())
		return swp_entry(swp_type(entry),
				 swp_offset(entry) | SWP_MIG_DIRTY);
	return entry;
}

extern void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					unsigned long address);
extern void migration_entry_wait_huge(struct vm_area_struct *vma, unsigned long addr, pte_t *pte);
#else  /* CONFIG_MIGRATION */
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

static inline void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					unsigned long address) { }
static inline void migration_entry_wait_huge(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *pte) { }

static inline swp_entry_t make_migration_entry_young(swp_entry_t entry)
{
	return entry;
}

static inline swp_entry_t make_migration_entry_dirty(swp_entry_t entry)
{
	return entry;
}

#endif	/* CONFIG_MIGRATION */

#ifdef CONFIG_MEMORY_FAILURE

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

#else

static inline swp_entry_t make_hwpoison_entry(struct page *page)
{
	return swp_entry(0, 0);
}

static inline int is_hwpoison_entry(swp_entry_t swp)
{
	return 0;
}
#endif

typedef unsigned long pte_marker;

#define  PTE_MARKER_UFFD_WP			BIT(0)
/*
 * "Poisoned" here is meant in the very general sense of "future accesses are
 * invalid", instead of referring very specifically to hardware memory errors.
 * This marker is meant to represent any of various different causes of this.
 *
 * Note that, when encountered by the faulting logic, PTEs with this marker will
 * result in VM_FAULT_HWPOISON and thus regardless trigger hardware memory error
 * logic.
 */
#define  PTE_MARKER_POISONED			BIT(1)
/*
 * Indicates that, on fault, this PTE will case a SIGSEGV signal to be
 * sent. This means guard markers behave in effect as if the region were mapped
 * PROT_NONE, rather than if they were a memory hole or equivalent.
 */
#define  PTE_MARKER_GUARD			BIT(2)
#define  PTE_MARKER_MASK			(BIT(3) - 1)

static inline swp_entry_t make_pte_marker_entry(pte_marker marker)
{
	return swp_entry(SWP_PTE_MARKER, marker);
}

static inline pte_t make_pte_marker(pte_marker marker)
{
	return swp_entry_to_pte(make_pte_marker_entry(marker));
}

static inline swp_entry_t make_poisoned_swp_entry(void)
{
	return make_pte_marker_entry(PTE_MARKER_POISONED);
}

static inline swp_entry_t make_guard_swp_entry(void)
{
	return make_pte_marker_entry(PTE_MARKER_GUARD);
}

struct page_vma_mapped_walk;

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
extern int set_pmd_migration_entry(struct page_vma_mapped_walk *pvmw,
		struct page *page);

extern void remove_migration_pmd(struct page_vma_mapped_walk *pvmw,
		struct page *new);

extern void pmd_migration_entry_wait(struct mm_struct *mm, pmd_t *pmd);

static inline pmd_t swp_entry_to_pmd(swp_entry_t entry)
{
	swp_entry_t arch_entry;

	arch_entry = __swp_entry(swp_type(entry), swp_offset(entry));
	return __swp_entry_to_pmd(arch_entry);
}

#else  /* CONFIG_ARCH_ENABLE_THP_MIGRATION */
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

static inline pmd_t swp_entry_to_pmd(swp_entry_t entry)
{
	return __pmd(0);
}

#endif  /* CONFIG_ARCH_ENABLE_THP_MIGRATION */

#endif /* CONFIG_MMU */
#endif /* _LINUX_SWAPOPS_H */
