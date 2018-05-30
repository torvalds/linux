/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DAX_H
#define _LINUX_DAX_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>
#include <asm/pgtable.h>

struct iomap_ops;
struct dax_device;
struct dax_operations {
	/*
	 * direct_access: translate a device-relative
	 * logical-page-offset into an absolute physical pfn. Return the
	 * number of pages available for DAX at that pfn.
	 */
	long (*direct_access)(struct dax_device *, pgoff_t, long,
			void **, pfn_t *);
	/* copy_from_iter: required operation for fs-dax direct-i/o */
	size_t (*copy_from_iter)(struct dax_device *, pgoff_t, void *, size_t,
			struct iov_iter *);
};

extern struct attribute_group dax_attribute_group;

#if IS_ENABLED(CONFIG_DAX)
struct dax_device *dax_get_by_host(const char *host);
struct dax_device *alloc_dax(void *private, const char *host,
		const struct dax_operations *ops);
void put_dax(struct dax_device *dax_dev);
void kill_dax(struct dax_device *dax_dev);
void dax_write_cache(struct dax_device *dax_dev, bool wc);
bool dax_write_cache_enabled(struct dax_device *dax_dev);
#else
static inline struct dax_device *dax_get_by_host(const char *host)
{
	return NULL;
}
static inline struct dax_device *alloc_dax(void *private, const char *host,
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
#endif

struct writeback_control;
int bdev_dax_pgoff(struct block_device *, sector_t, size_t, pgoff_t *pgoff);
#if IS_ENABLED(CONFIG_FS_DAX)
bool __bdev_dax_supported(struct block_device *bdev, int blocksize);
static inline bool bdev_dax_supported(struct block_device *bdev, int blocksize)
{
	return __bdev_dax_supported(bdev, blocksize);
}

static inline struct dax_device *fs_dax_get_by_host(const char *host)
{
	return dax_get_by_host(host);
}

static inline void fs_put_dax(struct dax_device *dax_dev)
{
	put_dax(dax_dev);
}

struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev);
int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc);
#else
static inline bool bdev_dax_supported(struct block_device *bdev,
		int blocksize)
{
	return false;
}

static inline struct dax_device *fs_dax_get_by_host(const char *host)
{
	return NULL;
}

static inline void fs_put_dax(struct dax_device *dax_dev)
{
}

static inline struct dax_device *fs_dax_get_by_bdev(struct block_device *bdev)
{
	return NULL;
}

static inline int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc)
{
	return -EOPNOTSUPP;
}
#endif

int dax_read_lock(void);
void dax_read_unlock(int id);
bool dax_alive(struct dax_device *dax_dev);
void *dax_get_private(struct dax_device *dax_dev);
long dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff, long nr_pages,
		void **kaddr, pfn_t *pfn);
size_t dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
void dax_flush(struct dax_device *dax_dev, void *addr, size_t size);

ssize_t dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops);
int dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    pfn_t *pfnp, int *errp, const struct iomap_ops *ops);
int dax_finish_sync_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
			  pfn_t pfn);
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index);

#ifdef CONFIG_FS_DAX
int __dax_zero_page_range(struct block_device *bdev,
		struct dax_device *dax_dev, sector_t sector,
		unsigned int offset, unsigned int length);
#else
static inline int __dax_zero_page_range(struct block_device *bdev,
		struct dax_device *dax_dev, sector_t sector,
		unsigned int offset, unsigned int length)
{
	return -ENXIO;
}
#endif

static inline bool dax_mapping(struct address_space *mapping)
{
	return mapping->host && IS_DAX(mapping->host);
}

#endif
