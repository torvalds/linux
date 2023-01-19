/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DAX_H
#define _LINUX_DAX_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>

typedef unsigned long dax_entry_t;

struct dax_device;
struct gendisk;
struct iomap_ops;
struct iomap_iter;
struct iomap;

enum dax_access_mode {
	DAX_ACCESS,
	DAX_RECOVERY_WRITE,
};

struct dax_operations {
	/*
	 * direct_access: translate a device-relative
	 * logical-page-offset into an absolute physical pfn. Return the
	 * number of pages available for DAX at that pfn.
	 */
	long (*direct_access)(struct dax_device *, pgoff_t, long,
			enum dax_access_mode, void **, pfn_t *);
	/*
	 * Validate whether this device is usable as an fsdax backing
	 * device.
	 */
	bool (*dax_supported)(struct dax_device *, struct block_device *, int,
			sector_t, sector_t);
	/* zero_page_range: required operation. Zero page range   */
	int (*zero_page_range)(struct dax_device *, pgoff_t, size_t);
	/*
	 * recovery_write: recover a poisoned range by DAX device driver
	 * capable of clearing poison.
	 */
	size_t (*recovery_write)(struct dax_device *dax_dev, pgoff_t pgoff,
			void *addr, size_t bytes, struct iov_iter *iter);
};

struct dax_holder_operations {
	/*
	 * notify_failure - notify memory failure into inner holder device
	 * @dax_dev: the dax device which contains the holder
	 * @offset: offset on this dax device where memory failure occurs
	 * @len: length of this memory failure event
	 * @flags: action flags for memory failure handler
	 */
	int (*notify_failure)(struct dax_device *dax_dev, u64 offset,
			u64 len, int mf_flags);
};

#if IS_ENABLED(CONFIG_DAX)
struct dax_device *alloc_dax(void *private, const struct dax_operations *ops);
void *dax_holder(struct dax_device *dax_dev);
void put_dax(struct dax_device *dax_dev);
void kill_dax(struct dax_device *dax_dev);
void dax_write_cache(struct dax_device *dax_dev, bool wc);
bool dax_write_cache_enabled(struct dax_device *dax_dev);
bool dax_synchronous(struct dax_device *dax_dev);
void set_dax_synchronous(struct dax_device *dax_dev);
size_t dax_recovery_write(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i);
/*
 * Check if given mapping is supported by the file / underlying device.
 */
static inline bool daxdev_mapping_supported(struct vm_area_struct *vma,
					     struct dax_device *dax_dev)
{
	if (!(vma->vm_flags & VM_SYNC))
		return true;
	if (!IS_DAX(file_inode(vma->vm_file)))
		return false;
	return dax_synchronous(dax_dev);
}
#else
static inline void *dax_holder(struct dax_device *dax_dev)
{
	return NULL;
}
static inline struct dax_device *alloc_dax(void *private,
		const struct dax_operations *ops)
{
	/*
	 * Callers should check IS_ENABLED(CONFIG_DAX) to know if this
	 * NULL is an error or expected.
	 */
	return NULL;
}
static inline void put_dax(struct dax_device *dax_dev)
{
}
static inline void kill_dax(struct dax_device *dax_dev)
{
}
static inline void dax_write_cache(struct dax_device *dax_dev, bool wc)
{
}
static inline bool dax_write_cache_enabled(struct dax_device *dax_dev)
{
	return false;
}
static inline bool dax_synchronous(struct dax_device *dax_dev)
{
	return true;
}
static inline void set_dax_synchronous(struct dax_device *dax_dev)
{
}
static inline bool daxdev_mapping_supported(struct vm_area_struct *vma,
				struct dax_device *dax_dev)
{
	return !(vma->vm_flags & VM_SYNC);
}
static inline size_t dax_recovery_write(struct dax_device *dax_dev,
		pgoff_t pgoff, void *addr, size_t bytes, struct iov_iter *i)
{
	return 0;
}
#endif

void set_dax_nocache(struct dax_device *dax_dev);
void set_dax_nomc(struct dax_device *dax_dev);

struct writeback_control;
#if defined(CONFIG_BLOCK) && defined(CONFIG_FS_DAX)
int dax_add_host(struct dax_device *dax_dev, struct gendisk *disk);
void dax_remove_host(struct gendisk *disk);
struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev, u64 *start_off,
		void *holder, const struct dax_holder_operations *ops);
void fs_put_dax(struct dax_device *dax_dev, void *holder);
#else
static inline int dax_add_host(struct dax_device *dax_dev, struct gendisk *disk)
{
	return 0;
}
static inline void dax_remove_host(struct gendisk *disk)
{
}
static inline struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev,
		u64 *start_off, void *holder,
		const struct dax_holder_operations *ops)
{
	return NULL;
}
static inline void fs_put_dax(struct dax_device *dax_dev, void *holder)
{
}
#endif /* CONFIG_BLOCK && CONFIG_FS_DAX */

#if IS_ENABLED(CONFIG_FS_DAX)
int dax_writeback_mapping_range(struct address_space *mapping,
		struct dax_device *dax_dev, struct writeback_control *wbc);

struct page *dax_layout_busy_page(struct address_space *mapping);
struct page *dax_layout_busy_page_range(struct address_space *mapping, loff_t start, loff_t end);
dax_entry_t dax_lock_page(struct page *page);
void dax_unlock_page(struct page *page, dax_entry_t cookie);
dax_entry_t dax_lock_mapping_entry(struct address_space *mapping,
		unsigned long index, struct page **page);
void dax_unlock_mapping_entry(struct address_space *mapping,
		unsigned long index, dax_entry_t cookie);
#else
static inline struct page *dax_layout_busy_page(struct address_space *mapping)
{
	return NULL;
}

static inline struct page *dax_layout_busy_page_range(struct address_space *mapping, pgoff_t start, pgoff_t nr_pages)
{
	return NULL;
}

static inline int dax_writeback_mapping_range(struct address_space *mapping,
		struct dax_device *dax_dev, struct writeback_control *wbc)
{
	return -EOPNOTSUPP;
}

static inline dax_entry_t dax_lock_page(struct page *page)
{
	if (IS_DAX(page->mapping->host))
		return ~0UL;
	return 0;
}

static inline void dax_unlock_page(struct page *page, dax_entry_t cookie)
{
}

static inline dax_entry_t dax_lock_mapping_entry(struct address_space *mapping,
		unsigned long index, struct page **page)
{
	return 0;
}

static inline void dax_unlock_mapping_entry(struct address_space *mapping,
		unsigned long index, dax_entry_t cookie)
{
}
#endif

int dax_file_unshare(struct inode *inode, loff_t pos, loff_t len,
		const struct iomap_ops *ops);
int dax_zero_range(struct inode *inode, loff_t pos, loff_t len, bool *did_zero,
		const struct iomap_ops *ops);
int dax_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops);

#if IS_ENABLED(CONFIG_DAX)
int dax_read_lock(void);
void dax_read_unlock(int id);
#else
static inline int dax_read_lock(void)
{
	return 0;
}

static inline void dax_read_unlock(int id)
{
}
#endif /* CONFIG_DAX */
bool dax_alive(struct dax_device *dax_dev);
void *dax_get_private(struct dax_device *dax_dev);
long dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff, long nr_pages,
		enum dax_access_mode mode, void **kaddr, pfn_t *pfn);
size_t dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
size_t dax_copy_to_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
int dax_zero_page_range(struct dax_device *dax_dev, pgoff_t pgoff,
			size_t nr_pages);
int dax_holder_notify_failure(struct dax_device *dax_dev, u64 off, u64 len,
		int mf_flags);
void dax_flush(struct dax_device *dax_dev, void *addr, size_t size);

ssize_t dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops);
vm_fault_t dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    pfn_t *pfnp, int *errp, const struct iomap_ops *ops);
vm_fault_t dax_finish_sync_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size, pfn_t pfn);
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index);
int dax_dedupe_file_range_compare(struct inode *src, loff_t srcoff,
				  struct inode *dest, loff_t destoff,
				  loff_t len, bool *is_same,
				  const struct iomap_ops *ops);
int dax_remap_file_range_prep(struct file *file_in, loff_t pos_in,
			      struct file *file_out, loff_t pos_out,
			      loff_t *len, unsigned int remap_flags,
			      const struct iomap_ops *ops);
static inline bool dax_mapping(struct address_space *mapping)
{
	return mapping->host && IS_DAX(mapping->host);
}

#ifdef CONFIG_DEV_DAX_HMEM_DEVICES
void hmem_register_device(int target_nid, struct resource *r);
#else
static inline void hmem_register_device(int target_nid, struct resource *r)
{
}
#endif

#endif
