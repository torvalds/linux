/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2013 Red Hat Inc.
 *
 * Authors: Jérôme Glisse <jglisse@redhat.com>
 */
/*
 * Heterogeneous Memory Management (HMM)
 *
 * See Documentation/vm/hmm.rst for reasons and overview of what HMM is and it
 * is for. Here we focus on the HMM API description, with some explanation of
 * the underlying implementation.
 *
 * Short description: HMM provides a set of helpers to share a virtual address
 * space between CPU and a device, so that the device can access any valid
 * address of the process (while still obeying memory protection). HMM also
 * provides helpers to migrate process memory to device memory, and back. Each
 * set of functionality (address space mirroring, and migration to and from
 * device memory) can be used independently of the other.
 *
 *
 * HMM address space mirroring API:
 *
 * Use HMM address space mirroring if you want to mirror a range of the CPU
 * page tables of a process into a device page table. Here, "mirror" means "keep
 * synchronized". Prerequisites: the device must provide the ability to write-
 * protect its page tables (at PAGE_SIZE granularity), and must be able to
 * recover from the resulting potential page faults.
 *
 * HMM guarantees that at any point in time, a given virtual address points to
 * either the same memory in both CPU and device page tables (that is: CPU and
 * device page tables each point to the same pages), or that one page table (CPU
 * or device) points to no entry, while the other still points to the old page
 * for the address. The latter case happens when the CPU page table update
 * happens first, and then the update is mirrored over to the device page table.
 * This does not cause any issue, because the CPU page table cannot start
 * pointing to a new page until the device page table is invalidated.
 *
 * HMM uses mmu_notifiers to monitor the CPU page tables, and forwards any
 * updates to each device driver that has registered a mirror. It also provides
 * some API calls to help with taking a snapshot of the CPU page table, and to
 * synchronize with any updates that might happen concurrently.
 *
 *
 * HMM migration to and from device memory:
 *
 * HMM provides a set of helpers to hotplug device memory as ZONE_DEVICE, with
 * a new MEMORY_DEVICE_PRIVATE type. This provides a struct page for each page
 * of the device memory, and allows the device driver to manage its memory
 * using those struct pages. Having struct pages for device memory makes
 * migration easier. Because that memory is not addressable by the CPU it must
 * never be pinned to the device; in other words, any CPU page fault can always
 * cause the device memory to be migrated (copied/moved) back to regular memory.
 *
 * A new migrate helper (migrate_vma()) has been added (see mm/migrate.c) that
 * allows use of a device DMA engine to perform the copy operation between
 * regular system memory and device memory.
 */
#ifndef LINUX_HMM_H
#define LINUX_HMM_H

#include <linux/kconfig.h>
#include <asm/pgtable.h>

#ifdef CONFIG_HMM_MIRROR

#include <linux/device.h>
#include <linux/migrate.h>
#include <linux/memremap.h>
#include <linux/completion.h>
#include <linux/mmu_notifier.h>


/*
 * struct hmm - HMM per mm struct
 *
 * @mm: mm struct this HMM struct is bound to
 * @lock: lock protecting ranges list
 * @ranges: list of range being snapshotted
 * @mirrors: list of mirrors for this mm
 * @mmu_notifier: mmu notifier to track updates to CPU page table
 * @mirrors_sem: read/write semaphore protecting the mirrors list
 * @wq: wait queue for user waiting on a range invalidation
 * @notifiers: count of active mmu notifiers
 */
struct hmm {
	struct mmu_notifier	mmu_notifier;
	spinlock_t		ranges_lock;
	struct list_head	ranges;
	struct list_head	mirrors;
	struct rw_semaphore	mirrors_sem;
	wait_queue_head_t	wq;
	long			notifiers;
};

/*
 * hmm_pfn_flag_e - HMM flag enums
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid. It has, at least, read permission.
 * HMM_PFN_WRITE: CPU page table has write permission set
 * HMM_PFN_DEVICE_PRIVATE: private device memory (ZONE_DEVICE)
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
	HMM_PFN_DEVICE_PRIVATE,
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
 * @hmm: the core HMM structure this range is active against
 * @vma: the vm area struct for the range
 * @list: all range lock are on a list
 * @start: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @pfns: array of pfns (big enough for the range)
 * @flags: pfn flags to match device driver page table
 * @values: pfn value for some special case (none, special, error, ...)
 * @default_flags: default flags for the range (write, read, ... see hmm doc)
 * @pfn_flags_mask: allows to mask pfn flags so that only default_flags matter
 * @pfn_shifts: pfn shift value (should be <= PAGE_SHIFT)
 * @valid: pfns array did not change since it has been fill by an HMM function
 */
struct hmm_range {
	struct hmm		*hmm;
	struct list_head	list;
	unsigned long		start;
	unsigned long		end;
	uint64_t		*pfns;
	const uint64_t		*flags;
	const uint64_t		*values;
	uint64_t		default_flags;
	uint64_t		pfn_flags_mask;
	uint8_t			pfn_shift;
	bool			valid;
};

/*
 * hmm_range_wait_until_valid() - wait for range to be valid
 * @range: range affected by invalidation to wait on
 * @timeout: time out for wait in ms (ie abort wait after that period of time)
 * Return: true if the range is valid, false otherwise.
 */
static inline bool hmm_range_wait_until_valid(struct hmm_range *range,
					      unsigned long timeout)
{
	return wait_event_timeout(range->hmm->wq, range->valid,
				  msecs_to_jiffies(timeout)) != 0;
}

/*
 * hmm_range_valid() - test if a range is valid or not
 * @range: range
 * Return: true if the range is valid, false otherwise.
 */
static inline bool hmm_range_valid(struct hmm_range *range)
{
	return range->valid;
}

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
 * hmm_device_entry_to_pfn() - return pfn value store in a device entry
 * @range: range use to decode device entry value
 * @entry: device entry to extract pfn from
 * Return: pfn value if device entry is valid, -1UL otherwise
 */
static inline unsigned long
hmm_device_entry_to_pfn(const struct hmm_range *range, uint64_t pfn)
{
	if (pfn == range->values[HMM_PFN_NONE])
		return -1UL;
	if (pfn == range->values[HMM_PFN_ERROR])
		return -1UL;
	if (pfn == range->values[HMM_PFN_SPECIAL])
		return -1UL;
	if (!(pfn & range->flags[HMM_PFN_VALID]))
		return -1UL;
	return (pfn >> range->pfn_shift);
}

/*
 * hmm_device_entry_from_page() - create a valid device entry for a page
 * @range: range use to encode HMM pfn value
 * @page: page for which to create the device entry
 * Return: valid device entry for the page
 */
static inline uint64_t hmm_device_entry_from_page(const struct hmm_range *range,
						  struct page *page)
{
	return (page_to_pfn(page) << range->pfn_shift) |
		range->flags[HMM_PFN_VALID];
}

/*
 * hmm_device_entry_from_pfn() - create a valid device entry value from pfn
 * @range: range use to encode HMM pfn value
 * @pfn: pfn value for which to create the device entry
 * Return: valid device entry for the pfn
 */
static inline uint64_t hmm_device_entry_from_pfn(const struct hmm_range *range,
						 unsigned long pfn)
{
	return (pfn << range->pfn_shift) |
		range->flags[HMM_PFN_VALID];
}

/*
 * Mirroring: how to synchronize device page table with CPU page table.
 *
 * A device driver that is participating in HMM mirroring must always
 * synchronize with CPU page table updates. For this, device drivers can either
 * directly use mmu_notifier APIs or they can use the hmm_mirror API. Device
 * drivers can decide to register one mirror per device per process, or just
 * one mirror per process for a group of devices. The pattern is:
 *
 *      int device_bind_address_space(..., struct mm_struct *mm, ...)
 *      {
 *          struct device_address_space *das;
 *
 *          // Device driver specific initialization, and allocation of das
 *          // which contains an hmm_mirror struct as one of its fields.
 *          ...
 *
 *          ret = hmm_mirror_register(&das->mirror, mm, &device_mirror_ops);
 *          if (ret) {
 *              // Cleanup on error
 *              return ret;
 *          }
 *
 *          // Other device driver specific initialization
 *          ...
 *      }
 *
 * Once an hmm_mirror is registered for an address space, the device driver
 * will get callbacks through sync_cpu_device_pagetables() operation (see
 * hmm_mirror_ops struct).
 *
 * Device driver must not free the struct containing the hmm_mirror struct
 * before calling hmm_mirror_unregister(). The expected usage is to do that when
 * the device driver is unbinding from an address space.
 *
 *
 *      void device_unbind_address_space(struct device_address_space *das)
 *      {
 *          // Device driver specific cleanup
 *          ...
 *
 *          hmm_mirror_unregister(&das->mirror);
 *
 *          // Other device driver specific cleanup, and now das can be freed
 *          ...
 *      }
 */

struct hmm_mirror;

/*
 * struct hmm_mirror_ops - HMM mirror device operations callback
 *
 * @update: callback to update range on a device
 */
struct hmm_mirror_ops {
	/* release() - release hmm_mirror
	 *
	 * @mirror: pointer to struct hmm_mirror
	 *
	 * This is called when the mm_struct is being released.  The callback
	 * must ensure that all access to any pages obtained from this mirror
	 * is halted before the callback returns. All future access should
	 * fault.
	 */
	void (*release)(struct hmm_mirror *mirror);

	/* sync_cpu_device_pagetables() - synchronize page tables
	 *
	 * @mirror: pointer to struct hmm_mirror
	 * @update: update information (see struct mmu_notifier_range)
	 * Return: -EAGAIN if mmu_notifier_range_blockable(update) is false
	 * and callback needs to block, 0 otherwise.
	 *
	 * This callback ultimately originates from mmu_notifiers when the CPU
	 * page table is updated. The device driver must update its page table
	 * in response to this callback. The update argument tells what action
	 * to perform.
	 *
	 * The device driver must not return from this callback until the device
	 * page tables are completely updated (TLBs flushed, etc); this is a
	 * synchronous call.
	 */
	int (*sync_cpu_device_pagetables)(
		struct hmm_mirror *mirror,
		const struct mmu_notifier_range *update);
};

/*
 * struct hmm_mirror - mirror struct for a device driver
 *
 * @hmm: pointer to struct hmm (which is unique per mm_struct)
 * @ops: device driver callback for HMM mirror operations
 * @list: for list of mirrors of a given mm
 *
 * Each address space (mm_struct) being mirrored by a device must register one
 * instance of an hmm_mirror struct with HMM. HMM will track the list of all
 * mirrors for each mm_struct.
 */
struct hmm_mirror {
	struct hmm			*hmm;
	const struct hmm_mirror_ops	*ops;
	struct list_head		list;
};

int hmm_mirror_register(struct hmm_mirror *mirror, struct mm_struct *mm);
void hmm_mirror_unregister(struct hmm_mirror *mirror);

/*
 * Please see Documentation/vm/hmm.rst for how to use the range API.
 */
int hmm_range_register(struct hmm_range *range, struct hmm_mirror *mirror);
void hmm_range_unregister(struct hmm_range *range);

/*
 * Retry fault if non-blocking, drop mmap_sem and return -EAGAIN in that case.
 */
#define HMM_FAULT_ALLOW_RETRY		(1 << 0)

/* Don't fault in missing PTEs, just snapshot the current state. */
#define HMM_FAULT_SNAPSHOT		(1 << 1)

long hmm_range_fault(struct hmm_range *range, unsigned int flags);

long hmm_range_dma_map(struct hmm_range *range,
		       struct device *device,
		       dma_addr_t *daddrs,
		       unsigned int flags);
long hmm_range_dma_unmap(struct hmm_range *range,
			 struct device *device,
			 dma_addr_t *daddrs,
			 bool dirty);

/*
 * HMM_RANGE_DEFAULT_TIMEOUT - default timeout (ms) when waiting for a range
 *
 * When waiting for mmu notifiers we need some kind of time out otherwise we
 * could potentialy wait for ever, 1000ms ie 1s sounds like a long time to
 * wait already.
 */
#define HMM_RANGE_DEFAULT_TIMEOUT 1000

#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */

#endif /* LINUX_HMM_H */
