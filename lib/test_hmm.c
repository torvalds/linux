// SPDX-License-Identifier: GPL-2.0
/*
 * This is a module to test the HMM (Heterogeneous Memory Management)
 * mirror and zone device private memory migration APIs of the kernel.
 * Userspace programs can register with the driver to mirror their own address
 * space and can use the device to read/write any valid virtual address.
 */
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/memremap.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/pagemap.h>
#include <linux/hmm.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/sched/mm.h>
#include <linux/platform_device.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>

#include "test_hmm_uapi.h"

#define DMIRROR_NDEVICES		4
#define DMIRROR_RANGE_FAULT_TIMEOUT	1000
#define DEVMEM_CHUNK_SIZE		(256 * 1024 * 1024U)
#define DEVMEM_CHUNKS_RESERVE		16

/*
 * For device_private pages, dpage is just a dummy struct page
 * representing a piece of device memory. dmirror_devmem_alloc_page
 * allocates a real system memory page as backing storage to fake a
 * real device. zone_device_data points to that backing page. But
 * for device_coherent memory, the struct page represents real
 * physical CPU-accessible memory that we can use directly.
 */
#define BACKING_PAGE(page) (is_device_private_page((page)) ? \
			   (page)->zone_device_data : (page))

static unsigned long spm_addr_dev0;
module_param(spm_addr_dev0, long, 0644);
MODULE_PARM_DESC(spm_addr_dev0,
		"Specify start address for SPM (special purpose memory) used for device 0. By setting this Coherent device type will be used. Make sure spm_addr_dev1 is set too. Minimum SPM size should be DEVMEM_CHUNK_SIZE.");

static unsigned long spm_addr_dev1;
module_param(spm_addr_dev1, long, 0644);
MODULE_PARM_DESC(spm_addr_dev1,
		"Specify start address for SPM (special purpose memory) used for device 1. By setting this Coherent device type will be used. Make sure spm_addr_dev0 is set too. Minimum SPM size should be DEVMEM_CHUNK_SIZE.");

static const struct dev_pagemap_ops dmirror_devmem_ops;
static const struct mmu_interval_notifier_ops dmirror_min_ops;
static dev_t dmirror_dev;

struct dmirror_device;

struct dmirror_bounce {
	void			*ptr;
	unsigned long		size;
	unsigned long		addr;
	unsigned long		cpages;
};

#define DPT_XA_TAG_ATOMIC 1UL
#define DPT_XA_TAG_WRITE 3UL

/*
 * Data structure to track address ranges and register for mmu interval
 * notifier updates.
 */
struct dmirror_interval {
	struct mmu_interval_notifier	notifier;
	struct dmirror			*dmirror;
};

/*
 * Data attached to the open device file.
 * Note that it might be shared after a fork().
 */
struct dmirror {
	struct dmirror_device		*mdevice;
	struct xarray			pt;
	struct mmu_interval_notifier	notifier;
	struct mutex			mutex;
};

/*
 * ZONE_DEVICE pages for migration and simulating device memory.
 */
struct dmirror_chunk {
	struct dev_pagemap	pagemap;
	struct dmirror_device	*mdevice;
	bool remove;
};

/*
 * Per device data.
 */
struct dmirror_device {
	struct cdev		cdevice;
	unsigned int            zone_device_type;
	struct device		device;

	unsigned int		devmem_capacity;
	unsigned int		devmem_count;
	struct dmirror_chunk	**devmem_chunks;
	struct mutex		devmem_lock;	/* protects the above */

	unsigned long		calloc;
	unsigned long		cfree;
	struct page		*free_pages;
	spinlock_t		lock;		/* protects the above */
};

static struct dmirror_device dmirror_devices[DMIRROR_NDEVICES];

static int dmirror_bounce_init(struct dmirror_bounce *bounce,
			       unsigned long addr,
			       unsigned long size)
{
	bounce->addr = addr;
	bounce->size = size;
	bounce->cpages = 0;
	bounce->ptr = vmalloc(size);
	if (!bounce->ptr)
		return -ENOMEM;
	return 0;
}

static bool dmirror_is_private_zone(struct dmirror_device *mdevice)
{
	return (mdevice->zone_device_type ==
		HMM_DMIRROR_MEMORY_DEVICE_PRIVATE) ? true : false;
}

static enum migrate_vma_direction
dmirror_select_device(struct dmirror *dmirror)
{
	return (dmirror->mdevice->zone_device_type ==
		HMM_DMIRROR_MEMORY_DEVICE_PRIVATE) ?
		MIGRATE_VMA_SELECT_DEVICE_PRIVATE :
		MIGRATE_VMA_SELECT_DEVICE_COHERENT;
}

static void dmirror_bounce_fini(struct dmirror_bounce *bounce)
{
	vfree(bounce->ptr);
}

static int dmirror_fops_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct dmirror *dmirror;
	int ret;

	/* Mirror this process address space */
	dmirror = kzalloc(sizeof(*dmirror), GFP_KERNEL);
	if (dmirror == NULL)
		return -ENOMEM;

	dmirror->mdevice = container_of(cdev, struct dmirror_device, cdevice);
	mutex_init(&dmirror->mutex);
	xa_init(&dmirror->pt);

	ret = mmu_interval_notifier_insert(&dmirror->notifier, current->mm,
				0, ULONG_MAX & PAGE_MASK, &dmirror_min_ops);
	if (ret) {
		kfree(dmirror);
		return ret;
	}

	filp->private_data = dmirror;
	return 0;
}

static int dmirror_fops_release(struct inode *inode, struct file *filp)
{
	struct dmirror *dmirror = filp->private_data;

	mmu_interval_notifier_remove(&dmirror->notifier);
	xa_destroy(&dmirror->pt);
	kfree(dmirror);
	return 0;
}

static struct dmirror_chunk *dmirror_page_to_chunk(struct page *page)
{
	return container_of(page_pgmap(page), struct dmirror_chunk,
			    pagemap);
}

static struct dmirror_device *dmirror_page_to_device(struct page *page)

{
	return dmirror_page_to_chunk(page)->mdevice;
}

static int dmirror_do_fault(struct dmirror *dmirror, struct hmm_range *range)
{
	unsigned long *pfns = range->hmm_pfns;
	unsigned long pfn;

	for (pfn = (range->start >> PAGE_SHIFT);
	     pfn < (range->end >> PAGE_SHIFT);
	     pfn++, pfns++) {
		struct page *page;
		void *entry;

		/*
		 * Since we asked for hmm_range_fault() to populate pages,
		 * it shouldn't return an error entry on success.
		 */
		WARN_ON(*pfns & HMM_PFN_ERROR);
		WARN_ON(!(*pfns & HMM_PFN_VALID));

		page = hmm_pfn_to_page(*pfns);
		WARN_ON(!page);

		entry = page;
		if (*pfns & HMM_PFN_WRITE)
			entry = xa_tag_pointer(entry, DPT_XA_TAG_WRITE);
		else if (WARN_ON(range->default_flags & HMM_PFN_WRITE))
			return -EFAULT;
		entry = xa_store(&dmirror->pt, pfn, entry, GFP_ATOMIC);
		if (xa_is_err(entry))
			return xa_err(entry);
	}

	return 0;
}

static void dmirror_do_update(struct dmirror *dmirror, unsigned long start,
			      unsigned long end)
{
	unsigned long pfn;
	void *entry;

	/*
	 * The XArray doesn't hold references to pages since it relies on
	 * the mmu notifier to clear page pointers when they become stale.
	 * Therefore, it is OK to just clear the entry.
	 */
	xa_for_each_range(&dmirror->pt, pfn, entry, start >> PAGE_SHIFT,
			  end >> PAGE_SHIFT)
		xa_erase(&dmirror->pt, pfn);
}

static bool dmirror_interval_invalidate(struct mmu_interval_notifier *mni,
				const struct mmu_notifier_range *range,
				unsigned long cur_seq)
{
	struct dmirror *dmirror = container_of(mni, struct dmirror, notifier);

	/*
	 * Ignore invalidation callbacks for device private pages since
	 * the invalidation is handled as part of the migration process.
	 */
	if (range->event == MMU_NOTIFY_MIGRATE &&
	    range->owner == dmirror->mdevice)
		return true;

	if (mmu_notifier_range_blockable(range))
		mutex_lock(&dmirror->mutex);
	else if (!mutex_trylock(&dmirror->mutex))
		return false;

	mmu_interval_set_seq(mni, cur_seq);
	dmirror_do_update(dmirror, range->start, range->end);

	mutex_unlock(&dmirror->mutex);
	return true;
}

static const struct mmu_interval_notifier_ops dmirror_min_ops = {
	.invalidate = dmirror_interval_invalidate,
};

static int dmirror_range_fault(struct dmirror *dmirror,
				struct hmm_range *range)
{
	struct mm_struct *mm = dmirror->notifier.mm;
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	int ret;

	while (true) {
		if (time_after(jiffies, timeout)) {
			ret = -EBUSY;
			goto out;
		}

		range->notifier_seq = mmu_interval_read_begin(range->notifier);
		mmap_read_lock(mm);
		ret = hmm_range_fault(range);
		mmap_read_unlock(mm);
		if (ret) {
			if (ret == -EBUSY)
				continue;
			goto out;
		}

		mutex_lock(&dmirror->mutex);
		if (mmu_interval_read_retry(range->notifier,
					    range->notifier_seq)) {
			mutex_unlock(&dmirror->mutex);
			continue;
		}
		break;
	}

	ret = dmirror_do_fault(dmirror, range);

	mutex_unlock(&dmirror->mutex);
out:
	return ret;
}

static int dmirror_fault(struct dmirror *dmirror, unsigned long start,
			 unsigned long end, bool write)
{
	struct mm_struct *mm = dmirror->notifier.mm;
	unsigned long addr;
	unsigned long pfns[64];
	struct hmm_range range = {
		.notifier = &dmirror->notifier,
		.hmm_pfns = pfns,
		.pfn_flags_mask = 0,
		.default_flags =
			HMM_PFN_REQ_FAULT | (write ? HMM_PFN_REQ_WRITE : 0),
		.dev_private_owner = dmirror->mdevice,
	};
	int ret = 0;

	/* Since the mm is for the mirrored process, get a reference first. */
	if (!mmget_not_zero(mm))
		return 0;

	for (addr = start; addr < end; addr = range.end) {
		range.start = addr;
		range.end = min(addr + (ARRAY_SIZE(pfns) << PAGE_SHIFT), end);

		ret = dmirror_range_fault(dmirror, &range);
		if (ret)
			break;
	}

	mmput(mm);
	return ret;
}

static int dmirror_do_read(struct dmirror *dmirror, unsigned long start,
			   unsigned long end, struct dmirror_bounce *bounce)
{
	unsigned long pfn;
	void *ptr;

	ptr = bounce->ptr + ((start - bounce->addr) & PAGE_MASK);

	for (pfn = start >> PAGE_SHIFT; pfn < (end >> PAGE_SHIFT); pfn++) {
		void *entry;
		struct page *page;

		entry = xa_load(&dmirror->pt, pfn);
		page = xa_untag_pointer(entry);
		if (!page)
			return -ENOENT;

		memcpy_from_page(ptr, page, 0, PAGE_SIZE);

		ptr += PAGE_SIZE;
		bounce->cpages++;
	}

	return 0;
}

static int dmirror_read(struct dmirror *dmirror, struct hmm_dmirror_cmd *cmd)
{
	struct dmirror_bounce bounce;
	unsigned long start, end;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	int ret;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	ret = dmirror_bounce_init(&bounce, start, size);
	if (ret)
		return ret;

	while (1) {
		mutex_lock(&dmirror->mutex);
		ret = dmirror_do_read(dmirror, start, end, &bounce);
		mutex_unlock(&dmirror->mutex);
		if (ret != -ENOENT)
			break;

		start = cmd->addr + (bounce.cpages << PAGE_SHIFT);
		ret = dmirror_fault(dmirror, start, end, false);
		if (ret)
			break;
		cmd->faults++;
	}

	if (ret == 0) {
		if (copy_to_user(u64_to_user_ptr(cmd->ptr), bounce.ptr,
				 bounce.size))
			ret = -EFAULT;
	}
	cmd->cpages = bounce.cpages;
	dmirror_bounce_fini(&bounce);
	return ret;
}

static int dmirror_do_write(struct dmirror *dmirror, unsigned long start,
			    unsigned long end, struct dmirror_bounce *bounce)
{
	unsigned long pfn;
	void *ptr;

	ptr = bounce->ptr + ((start - bounce->addr) & PAGE_MASK);

	for (pfn = start >> PAGE_SHIFT; pfn < (end >> PAGE_SHIFT); pfn++) {
		void *entry;
		struct page *page;

		entry = xa_load(&dmirror->pt, pfn);
		page = xa_untag_pointer(entry);
		if (!page || xa_pointer_tag(entry) != DPT_XA_TAG_WRITE)
			return -ENOENT;

		memcpy_to_page(page, 0, ptr, PAGE_SIZE);

		ptr += PAGE_SIZE;
		bounce->cpages++;
	}

	return 0;
}

static int dmirror_write(struct dmirror *dmirror, struct hmm_dmirror_cmd *cmd)
{
	struct dmirror_bounce bounce;
	unsigned long start, end;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	int ret;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	ret = dmirror_bounce_init(&bounce, start, size);
	if (ret)
		return ret;
	if (copy_from_user(bounce.ptr, u64_to_user_ptr(cmd->ptr),
			   bounce.size)) {
		ret = -EFAULT;
		goto fini;
	}

	while (1) {
		mutex_lock(&dmirror->mutex);
		ret = dmirror_do_write(dmirror, start, end, &bounce);
		mutex_unlock(&dmirror->mutex);
		if (ret != -ENOENT)
			break;

		start = cmd->addr + (bounce.cpages << PAGE_SHIFT);
		ret = dmirror_fault(dmirror, start, end, true);
		if (ret)
			break;
		cmd->faults++;
	}

fini:
	cmd->cpages = bounce.cpages;
	dmirror_bounce_fini(&bounce);
	return ret;
}

static int dmirror_allocate_chunk(struct dmirror_device *mdevice,
				   struct page **ppage)
{
	struct dmirror_chunk *devmem;
	struct resource *res = NULL;
	unsigned long pfn;
	unsigned long pfn_first;
	unsigned long pfn_last;
	void *ptr;
	int ret = -ENOMEM;

	devmem = kzalloc(sizeof(*devmem), GFP_KERNEL);
	if (!devmem)
		return ret;

	switch (mdevice->zone_device_type) {
	case HMM_DMIRROR_MEMORY_DEVICE_PRIVATE:
		res = request_free_mem_region(&iomem_resource, DEVMEM_CHUNK_SIZE,
					      "hmm_dmirror");
		if (IS_ERR_OR_NULL(res))
			goto err_devmem;
		devmem->pagemap.range.start = res->start;
		devmem->pagemap.range.end = res->end;
		devmem->pagemap.type = MEMORY_DEVICE_PRIVATE;
		break;
	case HMM_DMIRROR_MEMORY_DEVICE_COHERENT:
		devmem->pagemap.range.start = (MINOR(mdevice->cdevice.dev) - 2) ?
							spm_addr_dev0 :
							spm_addr_dev1;
		devmem->pagemap.range.end = devmem->pagemap.range.start +
					    DEVMEM_CHUNK_SIZE - 1;
		devmem->pagemap.type = MEMORY_DEVICE_COHERENT;
		break;
	default:
		ret = -EINVAL;
		goto err_devmem;
	}

	devmem->pagemap.nr_range = 1;
	devmem->pagemap.ops = &dmirror_devmem_ops;
	devmem->pagemap.owner = mdevice;

	mutex_lock(&mdevice->devmem_lock);

	if (mdevice->devmem_count == mdevice->devmem_capacity) {
		struct dmirror_chunk **new_chunks;
		unsigned int new_capacity;

		new_capacity = mdevice->devmem_capacity +
				DEVMEM_CHUNKS_RESERVE;
		new_chunks = krealloc(mdevice->devmem_chunks,
				sizeof(new_chunks[0]) * new_capacity,
				GFP_KERNEL);
		if (!new_chunks)
			goto err_release;
		mdevice->devmem_capacity = new_capacity;
		mdevice->devmem_chunks = new_chunks;
	}
	ptr = memremap_pages(&devmem->pagemap, numa_node_id());
	if (IS_ERR_OR_NULL(ptr)) {
		if (ptr)
			ret = PTR_ERR(ptr);
		else
			ret = -EFAULT;
		goto err_release;
	}

	devmem->mdevice = mdevice;
	pfn_first = devmem->pagemap.range.start >> PAGE_SHIFT;
	pfn_last = pfn_first + (range_len(&devmem->pagemap.range) >> PAGE_SHIFT);
	mdevice->devmem_chunks[mdevice->devmem_count++] = devmem;

	mutex_unlock(&mdevice->devmem_lock);

	pr_info("added new %u MB chunk (total %u chunks, %u MB) PFNs [0x%lx 0x%lx)\n",
		DEVMEM_CHUNK_SIZE / (1024 * 1024),
		mdevice->devmem_count,
		mdevice->devmem_count * (DEVMEM_CHUNK_SIZE / (1024 * 1024)),
		pfn_first, pfn_last);

	spin_lock(&mdevice->lock);
	for (pfn = pfn_first; pfn < pfn_last; pfn++) {
		struct page *page = pfn_to_page(pfn);

		page->zone_device_data = mdevice->free_pages;
		mdevice->free_pages = page;
	}
	if (ppage) {
		*ppage = mdevice->free_pages;
		mdevice->free_pages = (*ppage)->zone_device_data;
		mdevice->calloc++;
	}
	spin_unlock(&mdevice->lock);

	return 0;

err_release:
	mutex_unlock(&mdevice->devmem_lock);
	if (res && devmem->pagemap.type == MEMORY_DEVICE_PRIVATE)
		release_mem_region(devmem->pagemap.range.start,
				   range_len(&devmem->pagemap.range));
err_devmem:
	kfree(devmem);

	return ret;
}

static struct page *dmirror_devmem_alloc_page(struct dmirror_device *mdevice)
{
	struct page *dpage = NULL;
	struct page *rpage = NULL;

	/*
	 * For ZONE_DEVICE private type, this is a fake device so we allocate
	 * real system memory to store our device memory.
	 * For ZONE_DEVICE coherent type we use the actual dpage to store the
	 * data and ignore rpage.
	 */
	if (dmirror_is_private_zone(mdevice)) {
		rpage = alloc_page(GFP_HIGHUSER);
		if (!rpage)
			return NULL;
	}
	spin_lock(&mdevice->lock);

	if (mdevice->free_pages) {
		dpage = mdevice->free_pages;
		mdevice->free_pages = dpage->zone_device_data;
		mdevice->calloc++;
		spin_unlock(&mdevice->lock);
	} else {
		spin_unlock(&mdevice->lock);
		if (dmirror_allocate_chunk(mdevice, &dpage))
			goto error;
	}

	zone_device_page_init(dpage);
	dpage->zone_device_data = rpage;
	return dpage;

error:
	if (rpage)
		__free_page(rpage);
	return NULL;
}

static void dmirror_migrate_alloc_and_copy(struct migrate_vma *args,
					   struct dmirror *dmirror)
{
	struct dmirror_device *mdevice = dmirror->mdevice;
	const unsigned long *src = args->src;
	unsigned long *dst = args->dst;
	unsigned long addr;

	for (addr = args->start; addr < args->end; addr += PAGE_SIZE,
						   src++, dst++) {
		struct page *spage;
		struct page *dpage;
		struct page *rpage;

		if (!(*src & MIGRATE_PFN_MIGRATE))
			continue;

		/*
		 * Note that spage might be NULL which is OK since it is an
		 * unallocated pte_none() or read-only zero page.
		 */
		spage = migrate_pfn_to_page(*src);
		if (WARN(spage && is_zone_device_page(spage),
		     "page already in device spage pfn: 0x%lx\n",
		     page_to_pfn(spage)))
			continue;

		dpage = dmirror_devmem_alloc_page(mdevice);
		if (!dpage)
			continue;

		rpage = BACKING_PAGE(dpage);
		if (spage)
			copy_highpage(rpage, spage);
		else
			clear_highpage(rpage);

		/*
		 * Normally, a device would use the page->zone_device_data to
		 * point to the mirror but here we use it to hold the page for
		 * the simulated device memory and that page holds the pointer
		 * to the mirror.
		 */
		rpage->zone_device_data = dmirror;

		pr_debug("migrating from sys to dev pfn src: 0x%lx pfn dst: 0x%lx\n",
			 page_to_pfn(spage), page_to_pfn(dpage));
		*dst = migrate_pfn(page_to_pfn(dpage));
		if ((*src & MIGRATE_PFN_WRITE) ||
		    (!spage && args->vma->vm_flags & VM_WRITE))
			*dst |= MIGRATE_PFN_WRITE;
	}
}

static int dmirror_check_atomic(struct dmirror *dmirror, unsigned long start,
			     unsigned long end)
{
	unsigned long pfn;

	for (pfn = start >> PAGE_SHIFT; pfn < (end >> PAGE_SHIFT); pfn++) {
		void *entry;

		entry = xa_load(&dmirror->pt, pfn);
		if (xa_pointer_tag(entry) == DPT_XA_TAG_ATOMIC)
			return -EPERM;
	}

	return 0;
}

static int dmirror_atomic_map(unsigned long addr, struct page *page,
		struct dmirror *dmirror)
{
	void *entry;

	/* Map the migrated pages into the device's page tables. */
	mutex_lock(&dmirror->mutex);

	entry = xa_tag_pointer(page, DPT_XA_TAG_ATOMIC);
	entry = xa_store(&dmirror->pt, addr >> PAGE_SHIFT, entry, GFP_ATOMIC);
	if (xa_is_err(entry)) {
		mutex_unlock(&dmirror->mutex);
		return xa_err(entry);
	}

	mutex_unlock(&dmirror->mutex);
	return 0;
}

static int dmirror_migrate_finalize_and_map(struct migrate_vma *args,
					    struct dmirror *dmirror)
{
	unsigned long start = args->start;
	unsigned long end = args->end;
	const unsigned long *src = args->src;
	const unsigned long *dst = args->dst;
	unsigned long pfn;

	/* Map the migrated pages into the device's page tables. */
	mutex_lock(&dmirror->mutex);

	for (pfn = start >> PAGE_SHIFT; pfn < (end >> PAGE_SHIFT); pfn++,
								src++, dst++) {
		struct page *dpage;
		void *entry;

		if (!(*src & MIGRATE_PFN_MIGRATE))
			continue;

		dpage = migrate_pfn_to_page(*dst);
		if (!dpage)
			continue;

		entry = BACKING_PAGE(dpage);
		if (*dst & MIGRATE_PFN_WRITE)
			entry = xa_tag_pointer(entry, DPT_XA_TAG_WRITE);
		entry = xa_store(&dmirror->pt, pfn, entry, GFP_ATOMIC);
		if (xa_is_err(entry)) {
			mutex_unlock(&dmirror->mutex);
			return xa_err(entry);
		}
	}

	mutex_unlock(&dmirror->mutex);
	return 0;
}

static int dmirror_exclusive(struct dmirror *dmirror,
			     struct hmm_dmirror_cmd *cmd)
{
	unsigned long start, end, addr;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	struct mm_struct *mm = dmirror->notifier.mm;
	struct dmirror_bounce bounce;
	int ret = 0;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	/* Since the mm is for the mirrored process, get a reference first. */
	if (!mmget_not_zero(mm))
		return -EINVAL;

	mmap_read_lock(mm);
	for (addr = start; !ret && addr < end; addr += PAGE_SIZE) {
		struct folio *folio;
		struct page *page;

		page = make_device_exclusive(mm, addr, NULL, &folio);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			break;
		}

		ret = dmirror_atomic_map(addr, page, dmirror);
		folio_unlock(folio);
		folio_put(folio);
	}
	mmap_read_unlock(mm);
	mmput(mm);

	if (ret)
		return ret;

	/* Return the migrated data for verification. */
	ret = dmirror_bounce_init(&bounce, start, size);
	if (ret)
		return ret;
	mutex_lock(&dmirror->mutex);
	ret = dmirror_do_read(dmirror, start, end, &bounce);
	mutex_unlock(&dmirror->mutex);
	if (ret == 0) {
		if (copy_to_user(u64_to_user_ptr(cmd->ptr), bounce.ptr,
				 bounce.size))
			ret = -EFAULT;
	}

	cmd->cpages = bounce.cpages;
	dmirror_bounce_fini(&bounce);
	return ret;
}

static vm_fault_t dmirror_devmem_fault_alloc_and_copy(struct migrate_vma *args,
						      struct dmirror *dmirror)
{
	const unsigned long *src = args->src;
	unsigned long *dst = args->dst;
	unsigned long start = args->start;
	unsigned long end = args->end;
	unsigned long addr;

	for (addr = start; addr < end; addr += PAGE_SIZE,
				       src++, dst++) {
		struct page *dpage, *spage;

		spage = migrate_pfn_to_page(*src);
		if (!spage || !(*src & MIGRATE_PFN_MIGRATE))
			continue;

		if (WARN_ON(!is_device_private_page(spage) &&
			    !is_device_coherent_page(spage)))
			continue;
		spage = BACKING_PAGE(spage);
		dpage = alloc_page_vma(GFP_HIGHUSER_MOVABLE, args->vma, addr);
		if (!dpage)
			continue;
		pr_debug("migrating from dev to sys pfn src: 0x%lx pfn dst: 0x%lx\n",
			 page_to_pfn(spage), page_to_pfn(dpage));

		lock_page(dpage);
		xa_erase(&dmirror->pt, addr >> PAGE_SHIFT);
		copy_highpage(dpage, spage);
		*dst = migrate_pfn(page_to_pfn(dpage));
		if (*src & MIGRATE_PFN_WRITE)
			*dst |= MIGRATE_PFN_WRITE;
	}
	return 0;
}

static unsigned long
dmirror_successful_migrated_pages(struct migrate_vma *migrate)
{
	unsigned long cpages = 0;
	unsigned long i;

	for (i = 0; i < migrate->npages; i++) {
		if (migrate->src[i] & MIGRATE_PFN_VALID &&
		    migrate->src[i] & MIGRATE_PFN_MIGRATE)
			cpages++;
	}
	return cpages;
}

static int dmirror_migrate_to_system(struct dmirror *dmirror,
				     struct hmm_dmirror_cmd *cmd)
{
	unsigned long start, end, addr;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	struct mm_struct *mm = dmirror->notifier.mm;
	struct vm_area_struct *vma;
	unsigned long src_pfns[64] = { 0 };
	unsigned long dst_pfns[64] = { 0 };
	struct migrate_vma args = { 0 };
	unsigned long next;
	int ret;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	/* Since the mm is for the mirrored process, get a reference first. */
	if (!mmget_not_zero(mm))
		return -EINVAL;

	cmd->cpages = 0;
	mmap_read_lock(mm);
	for (addr = start; addr < end; addr = next) {
		vma = vma_lookup(mm, addr);
		if (!vma || !(vma->vm_flags & VM_READ)) {
			ret = -EINVAL;
			goto out;
		}
		next = min(end, addr + (ARRAY_SIZE(src_pfns) << PAGE_SHIFT));
		if (next > vma->vm_end)
			next = vma->vm_end;

		args.vma = vma;
		args.src = src_pfns;
		args.dst = dst_pfns;
		args.start = addr;
		args.end = next;
		args.pgmap_owner = dmirror->mdevice;
		args.flags = dmirror_select_device(dmirror);

		ret = migrate_vma_setup(&args);
		if (ret)
			goto out;

		pr_debug("Migrating from device mem to sys mem\n");
		dmirror_devmem_fault_alloc_and_copy(&args, dmirror);

		migrate_vma_pages(&args);
		cmd->cpages += dmirror_successful_migrated_pages(&args);
		migrate_vma_finalize(&args);
	}
out:
	mmap_read_unlock(mm);
	mmput(mm);

	return ret;
}

static int dmirror_migrate_to_device(struct dmirror *dmirror,
				struct hmm_dmirror_cmd *cmd)
{
	unsigned long start, end, addr;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	struct mm_struct *mm = dmirror->notifier.mm;
	struct vm_area_struct *vma;
	unsigned long src_pfns[64] = { 0 };
	unsigned long dst_pfns[64] = { 0 };
	struct dmirror_bounce bounce;
	struct migrate_vma args = { 0 };
	unsigned long next;
	int ret;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	/* Since the mm is for the mirrored process, get a reference first. */
	if (!mmget_not_zero(mm))
		return -EINVAL;

	mmap_read_lock(mm);
	for (addr = start; addr < end; addr = next) {
		vma = vma_lookup(mm, addr);
		if (!vma || !(vma->vm_flags & VM_READ)) {
			ret = -EINVAL;
			goto out;
		}
		next = min(end, addr + (ARRAY_SIZE(src_pfns) << PAGE_SHIFT));
		if (next > vma->vm_end)
			next = vma->vm_end;

		args.vma = vma;
		args.src = src_pfns;
		args.dst = dst_pfns;
		args.start = addr;
		args.end = next;
		args.pgmap_owner = dmirror->mdevice;
		args.flags = MIGRATE_VMA_SELECT_SYSTEM;
		ret = migrate_vma_setup(&args);
		if (ret)
			goto out;

		pr_debug("Migrating from sys mem to device mem\n");
		dmirror_migrate_alloc_and_copy(&args, dmirror);
		migrate_vma_pages(&args);
		dmirror_migrate_finalize_and_map(&args, dmirror);
		migrate_vma_finalize(&args);
	}
	mmap_read_unlock(mm);
	mmput(mm);

	/*
	 * Return the migrated data for verification.
	 * Only for pages in device zone
	 */
	ret = dmirror_bounce_init(&bounce, start, size);
	if (ret)
		return ret;
	mutex_lock(&dmirror->mutex);
	ret = dmirror_do_read(dmirror, start, end, &bounce);
	mutex_unlock(&dmirror->mutex);
	if (ret == 0) {
		if (copy_to_user(u64_to_user_ptr(cmd->ptr), bounce.ptr,
				 bounce.size))
			ret = -EFAULT;
	}
	cmd->cpages = bounce.cpages;
	dmirror_bounce_fini(&bounce);
	return ret;

out:
	mmap_read_unlock(mm);
	mmput(mm);
	return ret;
}

static void dmirror_mkentry(struct dmirror *dmirror, struct hmm_range *range,
			    unsigned char *perm, unsigned long entry)
{
	struct page *page;

	if (entry & HMM_PFN_ERROR) {
		*perm = HMM_DMIRROR_PROT_ERROR;
		return;
	}
	if (!(entry & HMM_PFN_VALID)) {
		*perm = HMM_DMIRROR_PROT_NONE;
		return;
	}

	page = hmm_pfn_to_page(entry);
	if (is_device_private_page(page)) {
		/* Is the page migrated to this device or some other? */
		if (dmirror->mdevice == dmirror_page_to_device(page))
			*perm = HMM_DMIRROR_PROT_DEV_PRIVATE_LOCAL;
		else
			*perm = HMM_DMIRROR_PROT_DEV_PRIVATE_REMOTE;
	} else if (is_device_coherent_page(page)) {
		/* Is the page migrated to this device or some other? */
		if (dmirror->mdevice == dmirror_page_to_device(page))
			*perm = HMM_DMIRROR_PROT_DEV_COHERENT_LOCAL;
		else
			*perm = HMM_DMIRROR_PROT_DEV_COHERENT_REMOTE;
	} else if (is_zero_pfn(page_to_pfn(page)))
		*perm = HMM_DMIRROR_PROT_ZERO;
	else
		*perm = HMM_DMIRROR_PROT_NONE;
	if (entry & HMM_PFN_WRITE)
		*perm |= HMM_DMIRROR_PROT_WRITE;
	else
		*perm |= HMM_DMIRROR_PROT_READ;
	if (hmm_pfn_to_map_order(entry) + PAGE_SHIFT == PMD_SHIFT)
		*perm |= HMM_DMIRROR_PROT_PMD;
	else if (hmm_pfn_to_map_order(entry) + PAGE_SHIFT == PUD_SHIFT)
		*perm |= HMM_DMIRROR_PROT_PUD;
}

static bool dmirror_snapshot_invalidate(struct mmu_interval_notifier *mni,
				const struct mmu_notifier_range *range,
				unsigned long cur_seq)
{
	struct dmirror_interval *dmi =
		container_of(mni, struct dmirror_interval, notifier);
	struct dmirror *dmirror = dmi->dmirror;

	if (mmu_notifier_range_blockable(range))
		mutex_lock(&dmirror->mutex);
	else if (!mutex_trylock(&dmirror->mutex))
		return false;

	/*
	 * Snapshots only need to set the sequence number since any
	 * invalidation in the interval invalidates the whole snapshot.
	 */
	mmu_interval_set_seq(mni, cur_seq);

	mutex_unlock(&dmirror->mutex);
	return true;
}

static const struct mmu_interval_notifier_ops dmirror_mrn_ops = {
	.invalidate = dmirror_snapshot_invalidate,
};

static int dmirror_range_snapshot(struct dmirror *dmirror,
				  struct hmm_range *range,
				  unsigned char *perm)
{
	struct mm_struct *mm = dmirror->notifier.mm;
	struct dmirror_interval notifier;
	unsigned long timeout =
		jiffies + msecs_to_jiffies(HMM_RANGE_DEFAULT_TIMEOUT);
	unsigned long i;
	unsigned long n;
	int ret = 0;

	notifier.dmirror = dmirror;
	range->notifier = &notifier.notifier;

	ret = mmu_interval_notifier_insert(range->notifier, mm,
			range->start, range->end - range->start,
			&dmirror_mrn_ops);
	if (ret)
		return ret;

	while (true) {
		if (time_after(jiffies, timeout)) {
			ret = -EBUSY;
			goto out;
		}

		range->notifier_seq = mmu_interval_read_begin(range->notifier);

		mmap_read_lock(mm);
		ret = hmm_range_fault(range);
		mmap_read_unlock(mm);
		if (ret) {
			if (ret == -EBUSY)
				continue;
			goto out;
		}

		mutex_lock(&dmirror->mutex);
		if (mmu_interval_read_retry(range->notifier,
					    range->notifier_seq)) {
			mutex_unlock(&dmirror->mutex);
			continue;
		}
		break;
	}

	n = (range->end - range->start) >> PAGE_SHIFT;
	for (i = 0; i < n; i++)
		dmirror_mkentry(dmirror, range, perm + i, range->hmm_pfns[i]);

	mutex_unlock(&dmirror->mutex);
out:
	mmu_interval_notifier_remove(range->notifier);
	return ret;
}

static int dmirror_snapshot(struct dmirror *dmirror,
			    struct hmm_dmirror_cmd *cmd)
{
	struct mm_struct *mm = dmirror->notifier.mm;
	unsigned long start, end;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	unsigned long addr;
	unsigned long next;
	unsigned long pfns[64];
	unsigned char perm[64];
	char __user *uptr;
	struct hmm_range range = {
		.hmm_pfns = pfns,
		.dev_private_owner = dmirror->mdevice,
	};
	int ret = 0;

	start = cmd->addr;
	end = start + size;
	if (end < start)
		return -EINVAL;

	/* Since the mm is for the mirrored process, get a reference first. */
	if (!mmget_not_zero(mm))
		return -EINVAL;

	/*
	 * Register a temporary notifier to detect invalidations even if it
	 * overlaps with other mmu_interval_notifiers.
	 */
	uptr = u64_to_user_ptr(cmd->ptr);
	for (addr = start; addr < end; addr = next) {
		unsigned long n;

		next = min(addr + (ARRAY_SIZE(pfns) << PAGE_SHIFT), end);
		range.start = addr;
		range.end = next;

		ret = dmirror_range_snapshot(dmirror, &range, perm);
		if (ret)
			break;

		n = (range.end - range.start) >> PAGE_SHIFT;
		if (copy_to_user(uptr, perm, n)) {
			ret = -EFAULT;
			break;
		}

		cmd->cpages += n;
		uptr += n;
	}
	mmput(mm);

	return ret;
}

static void dmirror_device_evict_chunk(struct dmirror_chunk *chunk)
{
	unsigned long start_pfn = chunk->pagemap.range.start >> PAGE_SHIFT;
	unsigned long end_pfn = chunk->pagemap.range.end >> PAGE_SHIFT;
	unsigned long npages = end_pfn - start_pfn + 1;
	unsigned long i;
	unsigned long *src_pfns;
	unsigned long *dst_pfns;

	src_pfns = kvcalloc(npages, sizeof(*src_pfns), GFP_KERNEL | __GFP_NOFAIL);
	dst_pfns = kvcalloc(npages, sizeof(*dst_pfns), GFP_KERNEL | __GFP_NOFAIL);

	migrate_device_range(src_pfns, start_pfn, npages);
	for (i = 0; i < npages; i++) {
		struct page *dpage, *spage;

		spage = migrate_pfn_to_page(src_pfns[i]);
		if (!spage || !(src_pfns[i] & MIGRATE_PFN_MIGRATE))
			continue;

		if (WARN_ON(!is_device_private_page(spage) &&
			    !is_device_coherent_page(spage)))
			continue;
		spage = BACKING_PAGE(spage);
		dpage = alloc_page(GFP_HIGHUSER_MOVABLE | __GFP_NOFAIL);
		lock_page(dpage);
		copy_highpage(dpage, spage);
		dst_pfns[i] = migrate_pfn(page_to_pfn(dpage));
		if (src_pfns[i] & MIGRATE_PFN_WRITE)
			dst_pfns[i] |= MIGRATE_PFN_WRITE;
	}
	migrate_device_pages(src_pfns, dst_pfns, npages);
	migrate_device_finalize(src_pfns, dst_pfns, npages);
	kvfree(src_pfns);
	kvfree(dst_pfns);
}

/* Removes free pages from the free list so they can't be re-allocated */
static void dmirror_remove_free_pages(struct dmirror_chunk *devmem)
{
	struct dmirror_device *mdevice = devmem->mdevice;
	struct page *page;

	for (page = mdevice->free_pages; page; page = page->zone_device_data)
		if (dmirror_page_to_chunk(page) == devmem)
			mdevice->free_pages = page->zone_device_data;
}

static void dmirror_device_remove_chunks(struct dmirror_device *mdevice)
{
	unsigned int i;

	mutex_lock(&mdevice->devmem_lock);
	if (mdevice->devmem_chunks) {
		for (i = 0; i < mdevice->devmem_count; i++) {
			struct dmirror_chunk *devmem =
				mdevice->devmem_chunks[i];

			spin_lock(&mdevice->lock);
			devmem->remove = true;
			dmirror_remove_free_pages(devmem);
			spin_unlock(&mdevice->lock);

			dmirror_device_evict_chunk(devmem);
			memunmap_pages(&devmem->pagemap);
			if (devmem->pagemap.type == MEMORY_DEVICE_PRIVATE)
				release_mem_region(devmem->pagemap.range.start,
						   range_len(&devmem->pagemap.range));
			kfree(devmem);
		}
		mdevice->devmem_count = 0;
		mdevice->devmem_capacity = 0;
		mdevice->free_pages = NULL;
		kfree(mdevice->devmem_chunks);
		mdevice->devmem_chunks = NULL;
	}
	mutex_unlock(&mdevice->devmem_lock);
}

static long dmirror_fops_unlocked_ioctl(struct file *filp,
					unsigned int command,
					unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct hmm_dmirror_cmd cmd;
	struct dmirror *dmirror;
	int ret;

	dmirror = filp->private_data;
	if (!dmirror)
		return -EINVAL;

	if (copy_from_user(&cmd, uarg, sizeof(cmd)))
		return -EFAULT;

	if (cmd.addr & ~PAGE_MASK)
		return -EINVAL;
	if (cmd.addr >= (cmd.addr + (cmd.npages << PAGE_SHIFT)))
		return -EINVAL;

	cmd.cpages = 0;
	cmd.faults = 0;

	switch (command) {
	case HMM_DMIRROR_READ:
		ret = dmirror_read(dmirror, &cmd);
		break;

	case HMM_DMIRROR_WRITE:
		ret = dmirror_write(dmirror, &cmd);
		break;

	case HMM_DMIRROR_MIGRATE_TO_DEV:
		ret = dmirror_migrate_to_device(dmirror, &cmd);
		break;

	case HMM_DMIRROR_MIGRATE_TO_SYS:
		ret = dmirror_migrate_to_system(dmirror, &cmd);
		break;

	case HMM_DMIRROR_EXCLUSIVE:
		ret = dmirror_exclusive(dmirror, &cmd);
		break;

	case HMM_DMIRROR_CHECK_EXCLUSIVE:
		ret = dmirror_check_atomic(dmirror, cmd.addr,
					cmd.addr + (cmd.npages << PAGE_SHIFT));
		break;

	case HMM_DMIRROR_SNAPSHOT:
		ret = dmirror_snapshot(dmirror, &cmd);
		break;

	case HMM_DMIRROR_RELEASE:
		dmirror_device_remove_chunks(dmirror->mdevice);
		ret = 0;
		break;

	default:
		return -EINVAL;
	}
	if (ret)
		return ret;

	if (copy_to_user(uarg, &cmd, sizeof(cmd)))
		return -EFAULT;

	return 0;
}

static int dmirror_fops_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long addr;

	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
		struct page *page;
		int ret;

		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			return -ENOMEM;

		ret = vm_insert_page(vma, addr, page);
		if (ret) {
			__free_page(page);
			return ret;
		}
		put_page(page);
	}

	return 0;
}

static const struct file_operations dmirror_fops = {
	.open		= dmirror_fops_open,
	.release	= dmirror_fops_release,
	.mmap		= dmirror_fops_mmap,
	.unlocked_ioctl = dmirror_fops_unlocked_ioctl,
	.llseek		= default_llseek,
	.owner		= THIS_MODULE,
};

static void dmirror_devmem_free(struct page *page)
{
	struct page *rpage = BACKING_PAGE(page);
	struct dmirror_device *mdevice;

	if (rpage != page)
		__free_page(rpage);

	mdevice = dmirror_page_to_device(page);
	spin_lock(&mdevice->lock);

	/* Return page to our allocator if not freeing the chunk */
	if (!dmirror_page_to_chunk(page)->remove) {
		mdevice->cfree++;
		page->zone_device_data = mdevice->free_pages;
		mdevice->free_pages = page;
	}
	spin_unlock(&mdevice->lock);
}

static vm_fault_t dmirror_devmem_fault(struct vm_fault *vmf)
{
	struct migrate_vma args = { 0 };
	unsigned long src_pfns = 0;
	unsigned long dst_pfns = 0;
	struct page *rpage;
	struct dmirror *dmirror;
	vm_fault_t ret;

	/*
	 * Normally, a device would use the page->zone_device_data to point to
	 * the mirror but here we use it to hold the page for the simulated
	 * device memory and that page holds the pointer to the mirror.
	 */
	rpage = vmf->page->zone_device_data;
	dmirror = rpage->zone_device_data;

	/* FIXME demonstrate how we can adjust migrate range */
	args.vma = vmf->vma;
	args.start = vmf->address;
	args.end = args.start + PAGE_SIZE;
	args.src = &src_pfns;
	args.dst = &dst_pfns;
	args.pgmap_owner = dmirror->mdevice;
	args.flags = dmirror_select_device(dmirror);
	args.fault_page = vmf->page;

	if (migrate_vma_setup(&args))
		return VM_FAULT_SIGBUS;

	ret = dmirror_devmem_fault_alloc_and_copy(&args, dmirror);
	if (ret)
		return ret;
	migrate_vma_pages(&args);
	/*
	 * No device finalize step is needed since
	 * dmirror_devmem_fault_alloc_and_copy() will have already
	 * invalidated the device page table.
	 */
	migrate_vma_finalize(&args);
	return 0;
}

static const struct dev_pagemap_ops dmirror_devmem_ops = {
	.page_free	= dmirror_devmem_free,
	.migrate_to_ram	= dmirror_devmem_fault,
};

static int dmirror_device_init(struct dmirror_device *mdevice, int id)
{
	dev_t dev;
	int ret;

	dev = MKDEV(MAJOR(dmirror_dev), id);
	mutex_init(&mdevice->devmem_lock);
	spin_lock_init(&mdevice->lock);

	cdev_init(&mdevice->cdevice, &dmirror_fops);
	mdevice->cdevice.owner = THIS_MODULE;
	device_initialize(&mdevice->device);
	mdevice->device.devt = dev;

	ret = dev_set_name(&mdevice->device, "hmm_dmirror%u", id);
	if (ret)
		return ret;

	ret = cdev_device_add(&mdevice->cdevice, &mdevice->device);
	if (ret)
		return ret;

	/* Build a list of free ZONE_DEVICE struct pages */
	return dmirror_allocate_chunk(mdevice, NULL);
}

static void dmirror_device_remove(struct dmirror_device *mdevice)
{
	dmirror_device_remove_chunks(mdevice);
	cdev_device_del(&mdevice->cdevice, &mdevice->device);
}

static int __init hmm_dmirror_init(void)
{
	int ret;
	int id = 0;
	int ndevices = 0;

	ret = alloc_chrdev_region(&dmirror_dev, 0, DMIRROR_NDEVICES,
				  "HMM_DMIRROR");
	if (ret)
		goto err_unreg;

	memset(dmirror_devices, 0, DMIRROR_NDEVICES * sizeof(dmirror_devices[0]));
	dmirror_devices[ndevices++].zone_device_type =
				HMM_DMIRROR_MEMORY_DEVICE_PRIVATE;
	dmirror_devices[ndevices++].zone_device_type =
				HMM_DMIRROR_MEMORY_DEVICE_PRIVATE;
	if (spm_addr_dev0 && spm_addr_dev1) {
		dmirror_devices[ndevices++].zone_device_type =
					HMM_DMIRROR_MEMORY_DEVICE_COHERENT;
		dmirror_devices[ndevices++].zone_device_type =
					HMM_DMIRROR_MEMORY_DEVICE_COHERENT;
	}
	for (id = 0; id < ndevices; id++) {
		ret = dmirror_device_init(dmirror_devices + id, id);
		if (ret)
			goto err_chrdev;
	}

	pr_info("HMM test module loaded. This is only for testing HMM.\n");
	return 0;

err_chrdev:
	while (--id >= 0)
		dmirror_device_remove(dmirror_devices + id);
	unregister_chrdev_region(dmirror_dev, DMIRROR_NDEVICES);
err_unreg:
	return ret;
}

static void __exit hmm_dmirror_exit(void)
{
	int id;

	for (id = 0; id < DMIRROR_NDEVICES; id++)
		if (dmirror_devices[id].zone_device_type)
			dmirror_device_remove(dmirror_devices + id);
	unregister_chrdev_region(dmirror_dev, DMIRROR_NDEVICES);
}

module_init(hmm_dmirror_init);
module_exit(hmm_dmirror_exit);
MODULE_DESCRIPTION("HMM (Heterogeneous Memory Management) test module");
MODULE_LICENSE("GPL");
