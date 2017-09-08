/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: JÃ©rÃ´me Glisse <jglisse@redhat.com>
 */
/*
 * Heterogeneous Memory Management (HMM)
 *
 * See Documentation/vm/hmm.txt for reasons and overview of what HMM is and it
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
 * Use HMM address space mirroring if you want to mirror range of the CPU page
 * table of a process into a device page table. Here, "mirror" means "keep
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

#if IS_ENABLED(CONFIG_HMM)

struct hmm;

/*
 * hmm_pfn_t - HMM uses its own pfn type to keep several flags per page
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid
 * HMM_PFN_WRITE: CPU page table has write permission set
 */
typedef unsigned long hmm_pfn_t;

#define HMM_PFN_VALID (1 << 0)
#define HMM_PFN_WRITE (1 << 1)
#define HMM_PFN_SHIFT 2

/*
 * hmm_pfn_t_to_page() - return struct page pointed to by a valid hmm_pfn_t
 * @pfn: hmm_pfn_t to convert to struct page
 * Returns: struct page pointer if pfn is a valid hmm_pfn_t, NULL otherwise
 *
 * If the hmm_pfn_t is valid (ie valid flag set) then return the struct page
 * matching the pfn value stored in the hmm_pfn_t. Otherwise return NULL.
 */
static inline struct page *hmm_pfn_t_to_page(hmm_pfn_t pfn)
{
	if (!(pfn & HMM_PFN_VALID))
		return NULL;
	return pfn_to_page(pfn >> HMM_PFN_SHIFT);
}

/*
 * hmm_pfn_t_to_pfn() - return pfn value store in a hmm_pfn_t
 * @pfn: hmm_pfn_t to extract pfn from
 * Returns: pfn value if hmm_pfn_t is valid, -1UL otherwise
 */
static inline unsigned long hmm_pfn_t_to_pfn(hmm_pfn_t pfn)
{
	if (!(pfn & HMM_PFN_VALID))
		return -1UL;
	return (pfn >> HMM_PFN_SHIFT);
}

/*
 * hmm_pfn_t_from_page() - create a valid hmm_pfn_t value from struct page
 * @page: struct page pointer for which to create the hmm_pfn_t
 * Returns: valid hmm_pfn_t for the page
 */
static inline hmm_pfn_t hmm_pfn_t_from_page(struct page *page)
{
	return (page_to_pfn(page) << HMM_PFN_SHIFT) | HMM_PFN_VALID;
}

/*
 * hmm_pfn_t_from_pfn() - create a valid hmm_pfn_t value from pfn
 * @pfn: pfn value for which to create the hmm_pfn_t
 * Returns: valid hmm_pfn_t for the pfn
 */
static inline hmm_pfn_t hmm_pfn_t_from_pfn(unsigned long pfn)
{
	return (pfn << HMM_PFN_SHIFT) | HMM_PFN_VALID;
}


#if IS_ENABLED(CONFIG_HMM_MIRROR)
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
 * enum hmm_update_type - type of update
 * @HMM_UPDATE_INVALIDATE: invalidate range (no indication as to why)
 */
enum hmm_update_type {
	HMM_UPDATE_INVALIDATE,
};

/*
 * struct hmm_mirror_ops - HMM mirror device operations callback
 *
 * @update: callback to update range on a device
 */
struct hmm_mirror_ops {
	/* sync_cpu_device_pagetables() - synchronize page tables
	 *
	 * @mirror: pointer to struct hmm_mirror
	 * @update_type: type of update that occurred to the CPU page table
	 * @start: virtual start address of the range to update
	 * @end: virtual end address of the range to update
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
	void (*sync_cpu_device_pagetables)(struct hmm_mirror *mirror,
					   enum hmm_update_type update_type,
					   unsigned long start,
					   unsigned long end);
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
#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */


/* Below are for HMM internal use only! Not to be used by device driver! */
void hmm_mm_destroy(struct mm_struct *mm);

static inline void hmm_mm_init(struct mm_struct *mm)
{
	mm->hmm = NULL;
}

#else /* IS_ENABLED(CONFIG_HMM) */

/* Below are for HMM internal use only! Not to be used by device driver! */
static inline void hmm_mm_destroy(struct mm_struct *mm) {}
static inline void hmm_mm_init(struct mm_struct *mm) {}

#endif /* IS_ENABLED(CONFIG_HMM) */
#endif /* LINUX_HMM_H */
