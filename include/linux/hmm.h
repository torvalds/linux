/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2013 Red Hat Inc.
 *
 * Authors: Jérôme Glisse <jglisse@redhat.com>
 *
 * See Documentation/vm/hmm.rst for reasons and overview of what HMM is.
 */
#ifndef LINUX_HMM_H
#define LINUX_HMM_H

#include <linux/kconfig.h>
#include <asm/pgtable.h>

#include <linux/device.h>
#include <linux/migrate.h>
#include <linux/memremap.h>
#include <linux/completion.h>
#include <linux/mmu_notifier.h>

/*
 * hmm_pfn_flag_e - HMM flag enums
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid. It has, at least, read permission.
 * HMM_PFN_WRITE: CPU page table has write permission set
 *
 * The driver provides a flags array for mapping page protections to device
 * PTE bits. If the driver valid bit for an entry is bit 3,
 * i.e., (entry & (1 << 3)), then the driver must provide
 * an array in hmm_range.flags with hmm_range.flags[HMM_PFN_VALID] == 1 << 3.
 * Same logic apply to all flags. This is the same idea as vm_page_prot in vma
 * except that this is per device driver rather than per architecture.
 */
enum hmm_pfn_flag_e {
	HMM_PFN_VALID = 0,
	HMM_PFN_WRITE,
	HMM_PFN_FLAG_MAX
};

/*
 * hmm_pfn_value_e - HMM pfn special value
 *
 * Flags:
 * HMM_PFN_ERROR: corresponding CPU page table entry points to poisoned memory
 * HMM_PFN_NONE: corresponding CPU page table entry is pte_none()
 * HMM_PFN_SPECIAL: corresponding CPU page table entry is special; i.e., the
 *      result of vmf_insert_pfn() or vm_insert_page(). Therefore, it should not
 *      be mirrored by a device, because the entry will never have HMM_PFN_VALID
 *      set and the pfn value is undefined.
 *
 * Driver provides values for none entry, error entry, and special entry.
 * Driver can alias (i.e., use same value) error and special, but
 * it should not alias none with error or special.
 *
 * HMM pfn value returned by hmm_vma_get_pfns() or hmm_vma_fault() will be:
 * hmm_range.values[HMM_PFN_ERROR] if CPU page table entry is poisonous,
 * hmm_range.values[HMM_PFN_NONE] if there is no CPU page table entry,
 * hmm_range.values[HMM_PFN_SPECIAL] if CPU page table entry is a special one
 */
enum hmm_pfn_value_e {
	HMM_PFN_ERROR,
	HMM_PFN_NONE,
	HMM_PFN_SPECIAL,
	HMM_PFN_VALUE_MAX
};

/*
 * struct hmm_range - track invalidation lock on virtual address range
 *
 * @notifier: a mmu_interval_notifier that includes the start/end
 * @notifier_seq: result of mmu_interval_read_begin()
 * @start: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @pfns: array of pfns (big enough for the range)
 * @flags: pfn flags to match device driver page table
 * @values: pfn value for some special case (none, special, error, ...)
 * @default_flags: default flags for the range (write, read, ... see hmm doc)
 * @pfn_flags_mask: allows to mask pfn flags so that only default_flags matter
 * @pfn_shift: pfn shift value (should be <= PAGE_SHIFT)
 * @dev_private_owner: owner of device private pages
 */
struct hmm_range {
	struct mmu_interval_notifier *notifier;
	unsigned long		notifier_seq;
	unsigned long		start;
	unsigned long		end;
	uint64_t		*pfns;
	const uint64_t		*flags;
	const uint64_t		*values;
	uint64_t		default_flags;
	uint64_t		pfn_flags_mask;
	uint8_t			pfn_shift;
	void			*dev_private_owner;
};

/*
 * hmm_device_entry_to_page() - return struct page pointed to by a device entry
 * @range: range use to decode device entry value
 * @entry: device entry value to get corresponding struct page from
 * Return: struct page pointer if entry is a valid, NULL otherwise
 *
 * If the device entry is valid (ie valid flag set) then return the struct page
 * matching the entry value. Otherwise return NULL.
 */
static inline struct page *hmm_device_entry_to_page(const struct hmm_range *range,
						    uint64_t entry)
{
	if (entry == range->values[HMM_PFN_NONE])
		return NULL;
	if (entry == range->values[HMM_PFN_ERROR])
		return NULL;
	if (entry == range->values[HMM_PFN_SPECIAL])
		return NULL;
	if (!(entry & range->flags[HMM_PFN_VALID]))
		return NULL;
	return pfn_to_page(entry >> range->pfn_shift);
}

/*
 * Please see Documentation/vm/hmm.rst for how to use the range API.
 */
long hmm_range_fault(struct hmm_range *range);

/*
 * HMM_RANGE_DEFAULT_TIMEOUT - default timeout (ms) when waiting for a range
 *
 * When waiting for mmu notifiers we need some kind of time out otherwise we
 * could potentialy wait for ever, 1000ms ie 1s sounds like a long time to
 * wait already.
 */
#define HMM_RANGE_DEFAULT_TIMEOUT 1000

#endif /* LINUX_HMM_H */
