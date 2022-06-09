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

#define DMIRROR_NDEVICES		2
#define DMIRROR_RANGE_FAULT_TIMEOUT	1000
#define DEVMEM_CHUNK_SIZE		(256 * 1024 * 1024U)
#define DEVMEM_CHUNKS_RESERVE		16

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
};

/*
 * Per device data.
 */
struct dmirror_device {
	struct cdev		cdevice;
	struct hmm_devmem	*devmem;

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

static struct dmirror_device *dmirror_page_to_device(struct page *page)

{
	return container_of(page->pgmap, struct dmirror_chunk,
			    pagemap)->mdevice;
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
		void *tmp;

		entry = xa_load(&dmirror->pt, pfn);
		page = xa_untag_pointer(entry);
		if (!page)
			return -ENOENT;

		tmp = kmap(page);
		memcpy(ptr, tmp, PAGE_SIZE);
		kunmap(page);

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
		void *tmp;

		entry = xa_load(&dmirror->pt, pfn);
		page = xa_untag_pointer(entry);
		if (!page || xa_pointer_tag(entry) != DPT_XA_TAG_WRITE)
			return -ENOENT;

		tmp = kmap(page);
		memcpy(tmp, ptr, PAGE_SIZE);
		kunmap(page);

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

static bool dmirror_allocate_chunk(struct dmirror_device *mdevice,
				   struct page **ppage)
{
	struct dmirror_chunk *devmem;
	struct resource *res;
	unsigned long pfn;
	unsigned long pfn_first;
	unsigned long pfn_last;
	void *ptr;

	devmem = kzalloc(sizeof(*devmem), GFP_KERNEL);
	if (!devmem)
		return false;

	res = request_free_mem_region(&iomem_resource, DEVMEM_CHUNK_SIZE,
				      "hmm_dmirror");
	if (IS_ERR(res))
		goto err_devmem;

	devmem->pagemap.type = MEMORY_DEVICE_PRIVATE;
	devmem->pagemap.range.start = res->start;
	devmem->pagemap.range.end = res->end;
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
	if (IS_ERR(ptr))
		goto err_release;

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

	return true;

err_release:
	mutex_unlock(&mdevice->devmem_lock);
	release_mem_region(devmem->pagemap.range.start, range_len(&devmem->pagemap.range));
err_devmem:
	kfree(devmem);

	return false;
}

static struct page *dmirror_devmem_alloc_page(struct dmirror_device *mdevice)
{
	struct page *dpage = NULL;
	struct page *rpage;

	/*
	 * This is a fake device so we alloc real system memory to store
	 * our device memory.
	 */
	rpage = alloc_page(GFP_HIGHUSER);
	if (!rpage)
		return NULL;

	spin_lock(&mdevice->lock);

	if (mdevice->free_pages) {
		dpage = mdevice->free_pages;
		mdevice->free_pages = dpage->zone_device_data;
		mdevice->calloc++;
		spin_unlock(&mdevice->lock);
	} else {
		spin_unlock(&mdevice->lock);
		if (!dmirror_allocate_chunk(mdevice, &dpage))
			goto error;
	}

	dpage->zone_device_data = rpage;
	lock_page(dpage);
	return dpage;

error:
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

		dpage = dmirror_devmem_alloc_page(mdevice);
		if (!dpage)
			continue;

		rpage = dpage->zone_device_data;
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

static int dmirror_atomic_map(unsigned long start, unsigned long end,
			      struct page **pages, struct dmirror *dmirror)
{
	unsigned long pfn, mapped = 0;
	int i;

	/* Map the migrated pages into the device's page tables. */
	mutex_lock(&dmirror->mutex);

	for (i = 0, pfn = start >> PAGE_SHIFT; pfn < (end >> PAGE_SHIFT); pfn++, i++) {
		void *entry;

		if (!pages[i])
			continue;

		entry = pages[i];
		entry = xa_tag_pointer(entry, DPT_XA_TAG_ATOMIC);
		entry = xa_store(&dmirror->pt, pfn, entry, GFP_ATOMIC);
		if (xa_is_err(entry)) {
			mutex_unlock(&dmirror->mutex);
			return xa_err(entry);
		}

		mapped++;
	}

	mutex_unlock(&dmirror->mutex);
	return mapped;
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

		/*
		 * Store the page that holds the data so the page table
		 * doesn't have to deal with ZONE_DEVICE private pages.
		 */
		entry = dpage->zone_device_data;
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
	struct page *pages[64];
	struct dmirror_bounce bounce;
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
		unsigned long mapped = 0;
		int i;

		if (end < addr + (ARRAY_SIZE(pages) << PAGE_SHIFT))
			next = end;
		else
			next = addr + (ARRAY_SIZE(pages) << PAGE_SHIFT);

		ret = make_device_exclusive_range(mm, addr, next, pages, NULL);
		/*
		 * Do dmirror_atomic_map() iff all pages are marked for
		 * exclusive access to avoid accessing uninitialized
		 * fields of pages.
		 */
		if (ret == (next - addr) >> PAGE_SHIFT)
			mapped = dmirror_atomic_map(addr, next, pages, dmirror);
		for (i = 0; i < ret; i++) {
			if (pages[i]) {
				unlock_page(pages[i]);
				put_page(pages[i]);
			}
		}

		if (addr + (mapped << PAGE_SHIFT) < next) {
			mmap_read_unlock(mm);
			mmput(mm);
			return -EBUSY;
		}
	}
	mmap_read_unlock(mm);
	mmput(mm);

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

static int dmirror_migrate(struct dmirror *dmirror,
			   struct hmm_dmirror_cmd *cmd)
{
	unsigned long start, end, addr;
	unsigned long size = cmd->npages << PAGE_SHIFT;
	struct mm_struct *mm = dmirror->notifier.mm;
	struct vm_area_struct *vma;
	unsigned long src_pfns[64];
	unsigned long dst_pfns[64];
	struct dmirror_bounce bounce;
	struct migrate_vma args;
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

		dmirror_migrate_alloc_and_copy(&args, dmirror);
		migrate_vma_pages(&args);
		dmirror_migrate_finalize_and_map(&args, dmirror);
		migrate_vma_finalize(&args);
	}
	mmap_read_unlock(mm);
	mmput(mm);

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

	case HMM_DMIRROR_MIGRATE:
		ret = dmirror_migrate(dmirror, &cmd);
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
	struct page *rpage = page->zone_device_data;
	struct dmirror_device *mdevice;

	if (rpage)
		__free_page(rpage);

	mdevice = dmirror_page_to_device(page);

	spin_lock(&mdevice->lock);
	mdevice->cfree++;
	page->zone_device_data = mdevice->free_pages;
	mdevice->free_pages = page;
	spin_unlock(&mdevice->lock);
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
		spage = spage->zone_device_data;

		dpage = alloc_page_vma(GFP_HIGHUSER_MOVABLE, args->vma, addr);
		if (!dpage)
			continue;

		lock_page(dpage);
		xa_erase(&dmirror->pt, addr >> PAGE_SHIFT);
		copy_highpage(dpage, spage);
		*dst = migrate_pfn(page_to_pfn(dpage));
		if (*src & MIGRATE_PFN_WRITE)
			*dst |= MIGRATE_PFN_WRITE;
	}
	return 0;
}

static vm_fault_t dmirror_devmem_fault(struct vm_fault *vmf)
{
	struct migrate_vma args;
	unsigned long src_pfns;
	unsigned long dst_pfns;
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
	args.flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;

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
	ret = cdev_add(&mdevice->cdevice, dev, 1);
	if (ret)
		return ret;

	/* Build a list of free ZONE_DEVICE private struct pages */
	dmirror_allocate_chunk(mdevice, NULL);

	return 0;
}

static void dmirror_device_remove(struct dmirror_device *mdevice)
{
	unsigned int i;

	if (mdevice->devmem_chunks) {
		for (i = 0; i < mdevice->devmem_count; i++) {
			struct dmirror_chunk *devmem =
				mdevice->devmem_chunks[i];

			memunmap_pages(&devmem->pagemap);
			release_mem_region(devmem->pagemap.range.start,
					   range_len(&devmem->pagemap.range));
			kfree(devmem);
		}
		kfree(mdevice->devmem_chunks);
	}

	cdev_del(&mdevice->cdevice);
}

static int __init hmm_dmirror_init(void)
{
	int ret;
	int id;

	ret = alloc_chrdev_region(&dmirror_dev, 0, DMIRROR_NDEVICES,
				  "HMM_DMIRROR");
	if (ret)
		goto err_unreg;

	for (id = 0; id < DMIRROR_NDEVICES; id++) {
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
		dmirror_device_remove(dmirror_devices + id);
	unregister_chrdev_region(dmirror_dev, DMIRROR_NDEVICES);
}

module_init(hmm_dmirror_init);
module_exit(hmm_dmirror_exit);
MODULE_LICENSE("GPL");
