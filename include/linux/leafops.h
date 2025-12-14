/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Describes operations that can be performed on software-defined page table
 * leaf entries. These are abstracted from the hardware page table entries
 * themselves by the softleaf_t type, see mm_types.h.
 */
#ifndef _LINUX_LEAFOPS_H
#define _LINUX_LEAFOPS_H

#include <linux/mm_types.h>
#include <linux/swapops.h>
#include <linux/swap.h>

#ifdef CONFIG_MMU

/* Temporary until swp_entry_t eliminated. */
#define LEAF_TYPE_SHIFT SWP_TYPE_SHIFT

enum softleaf_type {
	/* Fundamental types. */
	SOFTLEAF_NONE,
	SOFTLEAF_SWAP,
	/* Migration types. */
	SOFTLEAF_MIGRATION_READ,
	SOFTLEAF_MIGRATION_READ_EXCLUSIVE,
	SOFTLEAF_MIGRATION_WRITE,
	/* Device types. */
	SOFTLEAF_DEVICE_PRIVATE_READ,
	SOFTLEAF_DEVICE_PRIVATE_WRITE,
	SOFTLEAF_DEVICE_EXCLUSIVE,
	/* H/W posion types. */
	SOFTLEAF_HWPOISON,
	/* Marker types. */
	SOFTLEAF_MARKER,
};

/**
 * softleaf_mk_none() - Create an empty ('none') leaf entry.
 * Returns: empty leaf entry.
 */
static inline softleaf_t softleaf_mk_none(void)
{
	return ((softleaf_t) { 0 });
}

/**
 * softleaf_from_pte() - Obtain a leaf entry from a PTE entry.
 * @pte: PTE entry.
 *
 * If @pte is present (therefore not a leaf entry) the function returns an empty
 * leaf entry. Otherwise, it returns a leaf entry.
 *
 * Returns: Leaf entry.
 */
static inline softleaf_t softleaf_from_pte(pte_t pte)
{
	softleaf_t arch_entry;

	if (pte_present(pte) || pte_none(pte))
		return softleaf_mk_none();

	pte = pte_swp_clear_flags(pte);
	arch_entry = __pte_to_swp_entry(pte);

	/* Temporary until swp_entry_t eliminated. */
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

/**
 * softleaf_to_pte() - Obtain a PTE entry from a leaf entry.
 * @entry: Leaf entry.
 *
 * This generates an architecture-specific PTE entry that can be utilised to
 * encode the metadata the leaf entry encodes.
 *
 * Returns: Architecture-specific PTE entry encoding leaf entry.
 */
static inline pte_t softleaf_to_pte(softleaf_t entry)
{
	/* Temporary until swp_entry_t eliminated. */
	return swp_entry_to_pte(entry);
}

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
/**
 * softleaf_from_pmd() - Obtain a leaf entry from a PMD entry.
 * @pmd: PMD entry.
 *
 * If @pmd is present (therefore not a leaf entry) the function returns an empty
 * leaf entry. Otherwise, it returns a leaf entry.
 *
 * Returns: Leaf entry.
 */
static inline softleaf_t softleaf_from_pmd(pmd_t pmd)
{
	softleaf_t arch_entry;

	if (pmd_present(pmd) || pmd_none(pmd))
		return softleaf_mk_none();

	if (pmd_swp_soft_dirty(pmd))
		pmd = pmd_swp_clear_soft_dirty(pmd);
	if (pmd_swp_uffd_wp(pmd))
		pmd = pmd_swp_clear_uffd_wp(pmd);
	arch_entry = __pmd_to_swp_entry(pmd);

	/* Temporary until swp_entry_t eliminated. */
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

#else

static inline softleaf_t softleaf_from_pmd(pmd_t pmd)
{
	return softleaf_mk_none();
}

#endif

/**
 * softleaf_is_none() - Is the leaf entry empty?
 * @entry: Leaf entry.
 *
 * Empty entries are typically the result of a 'none' page table leaf entry
 * being converted to a leaf entry.
 *
 * Returns: true if the entry is empty, false otherwise.
 */
static inline bool softleaf_is_none(softleaf_t entry)
{
	return entry.val == 0;
}

/**
 * softleaf_type() - Identify the type of leaf entry.
 * @entry: Leaf entry.
 *
 * Returns: the leaf entry type associated with @entry.
 */
static inline enum softleaf_type softleaf_type(softleaf_t entry)
{
	unsigned int type_num;

	if (softleaf_is_none(entry))
		return SOFTLEAF_NONE;

	type_num = entry.val >> LEAF_TYPE_SHIFT;

	if (type_num < MAX_SWAPFILES)
		return SOFTLEAF_SWAP;

	switch (type_num) {
#ifdef CONFIG_MIGRATION
	case SWP_MIGRATION_READ:
		return SOFTLEAF_MIGRATION_READ;
	case SWP_MIGRATION_READ_EXCLUSIVE:
		return SOFTLEAF_MIGRATION_READ_EXCLUSIVE;
	case SWP_MIGRATION_WRITE:
		return SOFTLEAF_MIGRATION_WRITE;
#endif
#ifdef CONFIG_DEVICE_PRIVATE
	case SWP_DEVICE_WRITE:
		return SOFTLEAF_DEVICE_PRIVATE_WRITE;
	case SWP_DEVICE_READ:
		return SOFTLEAF_DEVICE_PRIVATE_READ;
	case SWP_DEVICE_EXCLUSIVE:
		return SOFTLEAF_DEVICE_EXCLUSIVE;
#endif
#ifdef CONFIG_MEMORY_FAILURE
	case SWP_HWPOISON:
		return SOFTLEAF_HWPOISON;
#endif
	case SWP_PTE_MARKER:
		return SOFTLEAF_MARKER;
	}

	/* Unknown entry type. */
	VM_WARN_ON_ONCE(1);
	return SOFTLEAF_NONE;
}

/**
 * softleaf_is_swap() - Is this leaf entry a swap entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a swap entry, otherwise false.
 */
static inline bool softleaf_is_swap(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_SWAP;
}

/**
 * softleaf_is_migration_write() - Is this leaf entry a writable migration entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a writable migration entry, otherwise
 * false.
 */
static inline bool softleaf_is_migration_write(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_MIGRATION_WRITE;
}

/**
 * softleaf_is_migration_read() - Is this leaf entry a readable migration entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a readable migration entry, otherwise
 * false.
 */
static inline bool softleaf_is_migration_read(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_MIGRATION_READ;
}

/**
 * softleaf_is_migration_read_exclusive() - Is this leaf entry an exclusive
 * readable migration entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is an exclusive readable migration entry,
 * otherwise false.
 */
static inline bool softleaf_is_migration_read_exclusive(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_MIGRATION_READ_EXCLUSIVE;
}

/**
 * softleaf_is_migration() - Is this leaf entry a migration entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a migration entry, otherwise false.
 */
static inline bool softleaf_is_migration(softleaf_t entry)
{
	switch (softleaf_type(entry)) {
	case SOFTLEAF_MIGRATION_READ:
	case SOFTLEAF_MIGRATION_READ_EXCLUSIVE:
	case SOFTLEAF_MIGRATION_WRITE:
		return true;
	default:
		return false;
	}
}

/**
 * softleaf_is_device_private_write() - Is this leaf entry a device private
 * writable entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a device private writable entry, otherwise
 * false.
 */
static inline bool softleaf_is_device_private_write(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_DEVICE_PRIVATE_WRITE;
}

/**
 * softleaf_is_device_private() - Is this leaf entry a device private entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a device private entry, otherwise false.
 */
static inline bool softleaf_is_device_private(softleaf_t entry)
{
	switch (softleaf_type(entry)) {
	case SOFTLEAF_DEVICE_PRIVATE_WRITE:
	case SOFTLEAF_DEVICE_PRIVATE_READ:
		return true;
	default:
		return false;
	}
}

/**
 * softleaf_is_device_exclusive() - Is this leaf entry a device-exclusive entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a device-exclusive entry, otherwise false.
 */
static inline bool softleaf_is_device_exclusive(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_DEVICE_EXCLUSIVE;
}

/**
 * softleaf_is_hwpoison() - Is this leaf entry a hardware poison entry?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a hardware poison entry, otherwise false.
 */
static inline bool softleaf_is_hwpoison(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_HWPOISON;
}

/**
 * softleaf_is_marker() - Is this leaf entry a marker?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a marker entry, otherwise false.
 */
static inline bool softleaf_is_marker(softleaf_t entry)
{
	return softleaf_type(entry) == SOFTLEAF_MARKER;
}

/**
 * softleaf_to_marker() - Obtain marker associated with leaf entry.
 * @entry: Leaf entry, softleaf_is_marker(@entry) must return true.
 *
 * Returns: Marker associated with the leaf entry.
 */
static inline pte_marker softleaf_to_marker(softleaf_t entry)
{
	VM_WARN_ON_ONCE(!softleaf_is_marker(entry));

	return swp_offset(entry) & PTE_MARKER_MASK;
}

/**
 * softleaf_has_pfn() - Does this leaf entry encode a valid PFN number?
 * @entry: Leaf entry.
 *
 * A pfn swap entry is a special type of swap entry that always has a pfn stored
 * in the swap offset. They can either be used to represent unaddressable device
 * memory, to restrict access to a page undergoing migration or to represent a
 * pfn which has been hwpoisoned and unmapped.
 *
 * Returns: true if the leaf entry encodes a PFN, otherwise false.
 */
static inline bool softleaf_has_pfn(softleaf_t entry)
{
	/* Make sure the swp offset can always store the needed fields. */
	BUILD_BUG_ON(SWP_TYPE_SHIFT < SWP_PFN_BITS);

	if (softleaf_is_migration(entry))
		return true;
	if (softleaf_is_device_private(entry))
		return true;
	if (softleaf_is_device_exclusive(entry))
		return true;
	if (softleaf_is_hwpoison(entry))
		return true;

	return false;
}

/**
 * softleaf_to_pfn() - Obtain PFN encoded within leaf entry.
 * @entry: Leaf entry, softleaf_has_pfn(@entry) must return true.
 *
 * Returns: The PFN associated with the leaf entry.
 */
static inline unsigned long softleaf_to_pfn(softleaf_t entry)
{
	VM_WARN_ON_ONCE(!softleaf_has_pfn(entry));

	/* Temporary until swp_entry_t eliminated. */
	return swp_offset(entry) & SWP_PFN_MASK;
}

/**
 * softleaf_to_page() - Obtains struct page for PFN encoded within leaf entry.
 * @entry: Leaf entry, softleaf_has_pfn(@entry) must return true.
 *
 * Returns: Pointer to the struct page associated with the leaf entry's PFN.
 */
static inline struct page *softleaf_to_page(softleaf_t entry)
{
	struct page *page = pfn_to_page(softleaf_to_pfn(entry));

	VM_WARN_ON_ONCE(!softleaf_has_pfn(entry));
	/*
	 * Any use of migration entries may only occur while the
	 * corresponding page is locked
	 */
	VM_WARN_ON_ONCE(softleaf_is_migration(entry) && !PageLocked(page));

	return page;
}

/**
 * softleaf_to_folio() - Obtains struct folio for PFN encoded within leaf entry.
 * @entry: Leaf entry, softleaf_has_pfn(@entry) must return true.
 *
 * Returns: Pointer to the struct folio associated with the leaf entry's PFN.
 */
static inline struct folio *softleaf_to_folio(softleaf_t entry)
{
	struct folio *folio = pfn_folio(softleaf_to_pfn(entry));

	VM_WARN_ON_ONCE(!softleaf_has_pfn(entry));
	/*
	 * Any use of migration entries may only occur while the
	 * corresponding folio is locked.
	 */
	VM_WARN_ON_ONCE(softleaf_is_migration(entry) &&
			!folio_test_locked(folio));

	return folio;
}

/**
 * softleaf_is_poison_marker() - Is this leaf entry a poison marker?
 * @entry: Leaf entry.
 *
 * The poison marker is set via UFFDIO_POISON. Userfaultfd-specific.
 *
 * Returns: true if the leaf entry is a poison marker, otherwise false.
 */
static inline bool softleaf_is_poison_marker(softleaf_t entry)
{
	if (!softleaf_is_marker(entry))
		return false;

	return softleaf_to_marker(entry) & PTE_MARKER_POISONED;
}

/**
 * softleaf_is_guard_marker() - Is this leaf entry a guard region marker?
 * @entry: Leaf entry.
 *
 * Returns: true if the leaf entry is a guard marker, otherwise false.
 */
static inline bool softleaf_is_guard_marker(softleaf_t entry)
{
	if (!softleaf_is_marker(entry))
		return false;

	return softleaf_to_marker(entry) & PTE_MARKER_GUARD;
}

/**
 * softleaf_is_uffd_wp_marker() - Is this leaf entry a userfautlfd write protect
 * marker?
 * @entry: Leaf entry.
 *
 * Userfaultfd-specific.
 *
 * Returns: true if the leaf entry is a UFFD WP marker, otherwise false.
 */
static inline bool softleaf_is_uffd_wp_marker(softleaf_t entry)
{
	if (!softleaf_is_marker(entry))
		return false;

	return softleaf_to_marker(entry) & PTE_MARKER_UFFD_WP;
}

#ifdef CONFIG_MIGRATION

/**
 * softleaf_is_migration_young() - Does this migration entry contain an accessed
 * bit?
 * @entry: Leaf entry.
 *
 * If the architecture can support storing A/D bits in migration entries, this
 * determines whether the accessed (or 'young') bit was set on the migrated page
 * table entry.
 *
 * Returns: true if the entry contains an accessed bit, otherwise false.
 */
static inline bool softleaf_is_migration_young(softleaf_t entry)
{
	VM_WARN_ON_ONCE(!softleaf_is_migration(entry));

	if (migration_entry_supports_ad())
		return swp_offset(entry) & SWP_MIG_YOUNG;
	/* Keep the old behavior of aging page after migration */
	return false;
}

/**
 * softleaf_is_migration_dirty() - Does this migration entry contain a dirty bit?
 * @entry: Leaf entry.
 *
 * If the architecture can support storing A/D bits in migration entries, this
 * determines whether the dirty bit was set on the migrated page table entry.
 *
 * Returns: true if the entry contains a dirty bit, otherwise false.
 */
static inline bool softleaf_is_migration_dirty(softleaf_t entry)
{
	VM_WARN_ON_ONCE(!softleaf_is_migration(entry));

	if (migration_entry_supports_ad())
		return swp_offset(entry) & SWP_MIG_DIRTY;
	/* Keep the old behavior of clean page after migration */
	return false;
}

#else /* CONFIG_MIGRATION */

static inline bool softleaf_is_migration_young(softleaf_t entry)
{
	return false;
}

static inline bool softleaf_is_migration_dirty(softleaf_t entry)
{
	return false;
}
#endif /* CONFIG_MIGRATION */

/**
 * pte_is_marker() - Does the PTE entry encode a marker leaf entry?
 * @pte: PTE entry.
 *
 * Returns: true if this PTE is a marker leaf entry, otherwise false.
 */
static inline bool pte_is_marker(pte_t pte)
{
	return softleaf_is_marker(softleaf_from_pte(pte));
}

/**
 * pte_is_uffd_wp_marker() - Does this PTE entry encode a userfaultfd write
 * protect marker leaf entry?
 * @pte: PTE entry.
 *
 * Returns: true if this PTE is a UFFD WP marker leaf entry, otherwise false.
 */
static inline bool pte_is_uffd_wp_marker(pte_t pte)
{
	const softleaf_t entry = softleaf_from_pte(pte);

	return softleaf_is_uffd_wp_marker(entry);
}

/**
 * pte_is_uffd_marker() - Does this PTE entry encode a userfault-specific marker
 * leaf entry?
 * @pte: PTE entry.
 *
 * It's useful to be able to determine which leaf entries encode UFFD-specific
 * markers so we can handle these correctly.
 *
 * Returns: true if this PTE entry is a UFFD-specific marker, otherwise false.
 */
static inline bool pte_is_uffd_marker(pte_t pte)
{
	const softleaf_t entry = softleaf_from_pte(pte);

	if (!softleaf_is_marker(entry))
		return false;

	/* UFFD WP, poisoned swap entries are UFFD-handled. */
	if (softleaf_is_uffd_wp_marker(entry))
		return true;
	if (softleaf_is_poison_marker(entry))
		return true;

	return false;
}

#if defined(CONFIG_ZONE_DEVICE) && defined(CONFIG_ARCH_ENABLE_THP_MIGRATION)

/**
 * pmd_is_device_private_entry() - Check if PMD contains a device private swap
 * entry.
 * @pmd: The PMD to check.
 *
 * Returns true if the PMD contains a swap entry that represents a device private
 * page mapping. This is used for zone device private pages that have been
 * swapped out but still need special handling during various memory management
 * operations.
 *
 * Return: true if PMD contains device private entry, false otherwise
 */
static inline bool pmd_is_device_private_entry(pmd_t pmd)
{
	return softleaf_is_device_private(softleaf_from_pmd(pmd));
}

#else  /* CONFIG_ZONE_DEVICE && CONFIG_ARCH_ENABLE_THP_MIGRATION */

static inline bool pmd_is_device_private_entry(pmd_t pmd)
{
	return false;
}

#endif /* CONFIG_ZONE_DEVICE && CONFIG_ARCH_ENABLE_THP_MIGRATION */

/**
 * pmd_is_migration_entry() - Does this PMD entry encode a migration entry?
 * @pmd: PMD entry.
 *
 * Returns: true if the PMD encodes a migration entry, otherwise false.
 */
static inline bool pmd_is_migration_entry(pmd_t pmd)
{
	return softleaf_is_migration(softleaf_from_pmd(pmd));
}

/**
 * pmd_is_valid_softleaf() - Is this PMD entry a valid leaf entry?
 * @pmd: PMD entry.
 *
 * PMD leaf entries are valid only if they are device private or migration
 * entries. This function asserts that a PMD leaf entry is valid in this
 * respect.
 *
 * Returns: true if the PMD entry is a valid leaf entry, otherwise false.
 */
static inline bool pmd_is_valid_softleaf(pmd_t pmd)
{
	const softleaf_t entry = softleaf_from_pmd(pmd);

	/* Only device private, migration entries valid for PMD. */
	return softleaf_is_device_private(entry) ||
		softleaf_is_migration(entry);
}

#endif  /* CONFIG_MMU */
#endif  /* _LINUX_LEAFOPS_H */
