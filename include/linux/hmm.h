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
#include <asm/pgtable.h>

#if IS_ENABLED(CONFIG_HMM)

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
 * @dead: is the mm dead ?
 */
struct hmm {
	struct mm_struct	*mm;
	struct kref		kref;
	struct mutex		lock;
	struct list_head	ranges;
	struct list_head	mirrors;
	struct mmu_notifier	mmu_notifier;
	struct rw_semaphore	mirrors_sem;
	wait_queue_head_t	wq;
	long			notifiers;
	bool			dead;
};

/*
 * hmm_pfn_flag_e - HMM flag enums
 *
 * Flags:
 * HMM_PFN_VALID: pfn is valid. It has, at least, read permission.
 * HMM_PFN_WRITE: CPU page table has write permission set
 * HMM_PFN_DEVICE_PRIVATE: private device memory (ZONE_DEVICE)
 *
 * The driver provide a flags array, if driver valid bit for an entry is bit
 * 3 ie (entry & (1 << 3)) is true if entry is valid then driver must provide
 * an array in hmm_range.flags with hmm_range.flags[HMM_PFN_VALID] == 1 << 3.
 * Same logic apply to all flags. This is same idea as vm_page_prot in vma
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
 * Driver provide entry value for none entry, error entry and special entry,
 * driver can alias (ie use same value for error and special for instance). It
 * should not alias none and error or special.
 *
 * HMM pfn value returned by hmm_vma_get_pfns() or hmm_vma_fault() will be:
 * hmm_range.values[HMM_PFN_ERROR] if CPU page table entry is poisonous,
 * hmm_range.values[HMM_PFN_NONE] if there is no CPU page table
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
	struct vm_area_struct	*vma;
	struct list_head	list;
	unsigned long		start;
	unsigned long		end;
	uint64_t		*pfns;
	const uint64_t		*flags;
	const uint64_t		*values;
	uint64_t		default_flags;
	uint64_t		pfn_flags_mask;
	uint8_t			page_shift;
	uint8_t			pfn_shift;
	bool			valid;
};

/*
 * hmm_range_page_shift() - return the page shift for the range
 * @range: range being queried
 * Returns: page shift (page size = 1 << page shift) for the range
 */
static inline unsigned hmm_range_page_shift(const struct hmm_range *range)
{
	return range->page_shift;
}

/*
 * hmm_range_page_size() - return the page size for the range
 * @range: range being queried
 * Returns: page size for the range in bytes
 */
static inline unsigned long hmm_range_page_size(const struct hmm_range *range)
{
	return 1UL << hmm_range_page_shift(range);
}

/*
 * hmm_range_wait_until_valid() - wait for range to be valid
 * @range: range affected by invalidation to wait on
 * @timeout: time out for wait in ms (ie abort wait after that period of time)
 * Returns: true if the range is valid, false otherwise.
 */
static inline bool hmm_range_wait_until_valid(struct hmm_range *range,
					      unsigned long timeout)
{
	/* Check if mm is dead ? */
	if (range->hmm == NULL || range->hmm->dead || range->hmm->mm == NULL) {
		range->valid = false;
		return false;
	}
	if (range->valid)
		return true;
	wait_event_timeout(range->hmm->wq, range->valid || range->hmm->dead,
			   msecs_to_jiffies(timeout));
	/* Return current valid status just in case we get lucky */
	return range->valid;
}

/*
 * hmm_range_valid() - test if a range is valid or not
 * @range: range
 * Returns: true if the range is valid, false otherwise.
 */
static inline bool hmm_range_valid(struct hmm_range *range)
{
	return range->valid;
}

/*
 * hmm_device_entry_to_page() - return struct page pointed to by a device entry
 * @range: range use to decode device entry value
 * @entry: device entry value to get corresponding struct page from
 * Returns: struct page pointer if entry is a valid, NULL otherwise
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
 * Returns: pfn value if device entry is valid, -1UL otherwise
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
 * Returns: valid device entry for the page
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
 * Returns: valid device entry for the pfn
 */
static inline uint64_t hmm_device_entry_from_pfn(const struct hmm_range *range,
						 unsigned long pfn)
{
	return (pfn << range->pfn_shift) |
		range->flags[HMM_PFN_VALID];
}

/*
 * Old API:
 * hmm_pfn_to_page()
 * hmm_pfn_to_pfn()
 * hmm_pfn_from_page()
 * hmm_pfn_from_pfn()
 *
 * This are the OLD API please use new API, it is here to avoid cross-tree
 * merge painfullness ie we convert things to new API in stages.
 */
static inline struct page *hmm_pfn_to_page(const struct hmm_range *range,
					   uint64_t pfn)
{
	return hmm_device_entry_to_page(range, pfn);
}

static inline unsigned long hmm_pfn_to_pfn(const struct hmm_range *range,
					   uint64_t pfn)
{
	return hmm_device_entry_to_pfn(range, pfn);
}

static inline uint64_t hmm_pfn_from_page(const struct hmm_range *range,
					 struct page *page)
{
	return hmm_device_entry_from_page(range, page);
}

static inline uint64_t hmm_pfn_from_pfn(const struct hmm_range *range,
					unsigned long pfn)
{
	return hmm_device_entry_from_pfn(range, pfn);
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
 * enum hmm_update_event - type of update
 * @HMM_UPDATE_INVALIDATE: invalidate range (no indication as to why)
 */
enum hmm_update_event {
	HMM_UPDATE_INVALIDATE,
};

/*
 * struct hmm_update - HMM update informations for callback
 *
 * @start: virtual start address of the range to update
 * @end: virtual end address of the range to update
 * @event: event triggering the update (what is happening)
 * @blockable: can the callback block/sleep ?
 */
struct hmm_update {
	unsigned long start;
	unsigned long end;
	enum hmm_update_event event;
	bool blockable;
};

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
	 * This is called when the mm_struct is being released.
	 * The callback should make sure no references to the mirror occur
	 * after the callback returns.
	 */
	void (*release)(struct hmm_mirror *mirror);

	/* sync_cpu_device_pagetables() - synchronize page tables
	 *
	 * @mirror: pointer to struct hmm_mirror
	 * @update: update informations (see struct hmm_update)
	 * Returns: -EAGAIN if update.blockable false and callback need to
	 *          block, 0 otherwise.
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
	int (*sync_cpu_device_pagetables)(struct hmm_mirror *mirror,
					  const struct hmm_update *update);
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
 * hmm_mirror_mm_is_alive() - test if mm is still alive
 * @mirror: the HMM mm mirror for which we want to lock the mmap_sem
 * Returns: false if the mm is dead, true otherwise
 *
 * This is an optimization it will not accurately always return -EINVAL if the
 * mm is dead ie there can be false negative (process is being kill but HMM is
 * not yet inform of that). It is only intented to be use to optimize out case
 * where driver is about to do something time consuming and it would be better
 * to skip it if the mm is dead.
 */
static inline bool hmm_mirror_mm_is_alive(struct hmm_mirror *mirror)
{
	struct mm_struct *mm;

	if (!mirror || !mirror->hmm)
		return false;
	mm = READ_ONCE(mirror->hmm->mm);
	if (mirror->hmm->dead || !mm)
		return false;

	return true;
}


/*
 * Please see Documentation/vm/hmm.rst for how to use the range API.
 */
int hmm_range_register(struct hmm_range *range,
		       struct mm_struct *mm,
		       unsigned long start,
		       unsigned long end,
		       unsigned page_shift);
void hmm_range_unregister(struct hmm_range *range);
long hmm_range_snapshot(struct hmm_range *range);
long hmm_range_fault(struct hmm_range *range, bool block);
long hmm_range_dma_map(struct hmm_range *range,
		       struct device *device,
		       dma_addr_t *daddrs,
		       bool block);
long hmm_range_dma_unmap(struct hmm_range *range,
			 struct vm_area_struct *vma,
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

/* This is a temporary helper to avoid merge conflict between trees. */
static inline bool hmm_vma_range_done(struct hmm_range *range)
{
	bool ret = hmm_range_valid(range);

	hmm_range_unregister(range);
	return ret;
}

/* This is a temporary helper to avoid merge conflict between trees. */
static inline int hmm_vma_fault(struct hmm_range *range, bool block)
{
	long ret;

	/*
	 * With the old API the driver must set each individual entries with
	 * the requested flags (valid, write, ...). So here we set the mask to
	 * keep intact the entries provided by the driver and zero out the
	 * default_flags.
	 */
	range->default_flags = 0;
	range->pfn_flags_mask = -1UL;

	ret = hmm_range_register(range, range->vma->vm_mm,
				 range->start, range->end,
				 PAGE_SHIFT);
	if (ret)
		return (int)ret;

	if (!hmm_range_wait_until_valid(range, HMM_RANGE_DEFAULT_TIMEOUT)) {
		/*
		 * The mmap_sem was taken by driver we release it here and
		 * returns -EAGAIN which correspond to mmap_sem have been
		 * drop in the old API.
		 */
		up_read(&range->vma->vm_mm->mmap_sem);
		return -EAGAIN;
	}

	ret = hmm_range_fault(range, block);
	if (ret <= 0) {
		if (ret == -EBUSY || !ret) {
			/* Same as above  drop mmap_sem to match old API. */
			up_read(&range->vma->vm_mm->mmap_sem);
			ret = -EBUSY;
		} else if (ret == -EAGAIN)
			ret = -EBUSY;
		hmm_range_unregister(range);
		return ret;
	}
	return 0;
}

/* Below are for HMM internal use only! Not to be used by device driver! */
void hmm_mm_destroy(struct mm_struct *mm);

static inline void hmm_mm_init(struct mm_struct *mm)
{
	mm->hmm = NULL;
}
#else /* IS_ENABLED(CONFIG_HMM_MIRROR) */
static inline void hmm_mm_destroy(struct mm_struct *mm) {}
static inline void hmm_mm_init(struct mm_struct *mm) {}
#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */

#if IS_ENABLED(CONFIG_DEVICE_PRIVATE)
struct hmm_devmem;

struct page *hmm_vma_alloc_locked_page(struct vm_area_struct *vma,
				       unsigned long addr);

/*
 * struct hmm_devmem_ops - callback for ZONE_DEVICE memory events
 *
 * @free: call when refcount on page reach 1 and thus is no longer use
 * @fault: call when there is a page fault to unaddressable memory
 *
 * Both callback happens from page_free() and page_fault() callback of struct
 * dev_pagemap respectively. See include/linux/memremap.h for more details on
 * those.
 *
 * The hmm_devmem_ops callback are just here to provide a coherent and
 * uniq API to device driver and device driver should not register their
 * own page_free() or page_fault() but rely on the hmm_devmem_ops call-
 * back.
 */
struct hmm_devmem_ops {
	/*
	 * free() - free a device page
	 * @devmem: device memory structure (see struct hmm_devmem)
	 * @page: pointer to struct page being freed
	 *
	 * Call back occurs whenever a device page refcount reach 1 which
	 * means that no one is holding any reference on the page anymore
	 * (ZONE_DEVICE page have an elevated refcount of 1 as default so
	 * that they are not release to the general page allocator).
	 *
	 * Note that callback has exclusive ownership of the page (as no
	 * one is holding any reference).
	 */
	void (*free)(struct hmm_devmem *devmem, struct page *page);
	/*
	 * fault() - CPU page fault or get user page (GUP)
	 * @devmem: device memory structure (see struct hmm_devmem)
	 * @vma: virtual memory area containing the virtual address
	 * @addr: virtual address that faulted or for which there is a GUP
	 * @page: pointer to struct page backing virtual address (unreliable)
	 * @flags: FAULT_FLAG_* (see include/linux/mm.h)
	 * @pmdp: page middle directory
	 * Returns: VM_FAULT_MINOR/MAJOR on success or one of VM_FAULT_ERROR
	 *   on error
	 *
	 * The callback occurs whenever there is a CPU page fault or GUP on a
	 * virtual address. This means that the device driver must migrate the
	 * page back to regular memory (CPU accessible).
	 *
	 * The device driver is free to migrate more than one page from the
	 * fault() callback as an optimization. However if device decide to
	 * migrate more than one page it must always priotirize the faulting
	 * address over the others.
	 *
	 * The struct page pointer is only given as an hint to allow quick
	 * lookup of internal device driver data. A concurrent migration
	 * might have already free that page and the virtual address might
	 * not longer be back by it. So it should not be modified by the
	 * callback.
	 *
	 * Note that mmap semaphore is held in read mode at least when this
	 * callback occurs, hence the vma is valid upon callback entry.
	 */
	vm_fault_t (*fault)(struct hmm_devmem *devmem,
		     struct vm_area_struct *vma,
		     unsigned long addr,
		     const struct page *page,
		     unsigned int flags,
		     pmd_t *pmdp);
};

/*
 * struct hmm_devmem - track device memory
 *
 * @completion: completion object for device memory
 * @pfn_first: first pfn for this resource (set by hmm_devmem_add())
 * @pfn_last: last pfn for this resource (set by hmm_devmem_add())
 * @resource: IO resource reserved for this chunk of memory
 * @pagemap: device page map for that chunk
 * @device: device to bind resource to
 * @ops: memory operations callback
 * @ref: per CPU refcount
 * @page_fault: callback when CPU fault on an unaddressable device page
 *
 * This an helper structure for device drivers that do not wish to implement
 * the gory details related to hotplugging new memoy and allocating struct
 * pages.
 *
 * Device drivers can directly use ZONE_DEVICE memory on their own if they
 * wish to do so.
 *
 * The page_fault() callback must migrate page back, from device memory to
 * system memory, so that the CPU can access it. This might fail for various
 * reasons (device issues,  device have been unplugged, ...). When such error
 * conditions happen, the page_fault() callback must return VM_FAULT_SIGBUS and
 * set the CPU page table entry to "poisoned".
 *
 * Note that because memory cgroup charges are transferred to the device memory,
 * this should never fail due to memory restrictions. However, allocation
 * of a regular system page might still fail because we are out of memory. If
 * that happens, the page_fault() callback must return VM_FAULT_OOM.
 *
 * The page_fault() callback can also try to migrate back multiple pages in one
 * chunk, as an optimization. It must, however, prioritize the faulting address
 * over all the others.
 */

struct hmm_devmem {
	struct completion		completion;
	unsigned long			pfn_first;
	unsigned long			pfn_last;
	struct resource			*resource;
	struct device			*device;
	struct dev_pagemap		pagemap;
	const struct hmm_devmem_ops	*ops;
	struct percpu_ref		ref;
};

/*
 * To add (hotplug) device memory, HMM assumes that there is no real resource
 * that reserves a range in the physical address space (this is intended to be
 * use by unaddressable device memory). It will reserve a physical range big
 * enough and allocate struct page for it.
 *
 * The device driver can wrap the hmm_devmem struct inside a private device
 * driver struct.
 */
struct hmm_devmem *hmm_devmem_add(const struct hmm_devmem_ops *ops,
				  struct device *device,
				  unsigned long size);

/*
 * hmm_devmem_page_set_drvdata - set per-page driver data field
 *
 * @page: pointer to struct page
 * @data: driver data value to set
 *
 * Because page can not be on lru we have an unsigned long that driver can use
 * to store a per page field. This just a simple helper to do that.
 */
static inline void hmm_devmem_page_set_drvdata(struct page *page,
					       unsigned long data)
{
	page->hmm_data = data;
}

/*
 * hmm_devmem_page_get_drvdata - get per page driver data field
 *
 * @page: pointer to struct page
 * Return: driver data value
 */
static inline unsigned long hmm_devmem_page_get_drvdata(const struct page *page)
{
	return page->hmm_data;
}
#endif /* CONFIG_DEVICE_PRIVATE */
#else /* IS_ENABLED(CONFIG_HMM) */
static inline void hmm_mm_destroy(struct mm_struct *mm) {}
static inline void hmm_mm_init(struct mm_struct *mm) {}
#endif /* IS_ENABLED(CONFIG_HMM) */

#endif /* LINUX_HMM_H */
