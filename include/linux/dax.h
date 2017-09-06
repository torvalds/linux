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
	/* flush: optional driver-specific cache management after writes */
	void (*flush)(struct dax_device *, pgoff_t, void *, size_t);
};

extern struct attribute_group dax_attribute_group;

#if IS_ENABLED(CONFIG_DAX)
struct dax_device *dax_get_by_host(const char *host);
void put_dax(struct dax_device *dax_dev);
#else
static inline struct dax_device *dax_get_by_host(const char *host)
{
	return NULL;
}

static inline void put_dax(struct dax_device *dax_dev)
{
}
#endif

int bdev_dax_pgoff(struct block_device *, sector_t, size_t, pgoff_t *pgoff);
#if IS_ENABLED(CONFIG_FS_DAX)
int __bdev_dax_supported(struct super_block *sb, int blocksize);
static inline int bdev_dax_supported(struct super_block *sb, int blocksize)
{
	return __bdev_dax_supported(sb, blocksize);
}

static inline struct dax_device *fs_dax_get_by_host(const char *host)
{
	return dax_get_by_host(host);
}

static inline void fs_put_dax(struct dax_device *dax_dev)
{
	put_dax(dax_dev);
}

#else
static inline int bdev_dax_supported(struct super_block *sb, int blocksize)
{
	return -EOPNOTSUPP;
}

static inline struct dax_device *fs_dax_get_by_host(const char *host)
{
	return NULL;
}

static inline void fs_put_dax(struct dax_device *dax_dev)
{
}
#endif

int dax_read_lock(void);
void dax_read_unlock(int id);
struct dax_device *alloc_dax(void *private, const char *host,
		const struct dax_operations *ops);
bool dax_alive(struct dax_device *dax_dev);
void kill_dax(struct dax_device *dax_dev);
void *dax_get_private(struct dax_device *dax_dev);
long dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff, long nr_pages,
		void **kaddr, pfn_t *pfn);
size_t dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t bytes, struct iov_iter *i);
void dax_flush(struct dax_device *dax_dev, pgoff_t pgoff, void *addr,
		size_t size);
void dax_write_cache(struct dax_device *dax_dev, bool wc);
bool dax_write_cache_enabled(struct dax_device *dax_dev);

/*
 * We use lowest available bit in exceptional entry for locking, one bit for
 * the entry size (PMD) and two more to tell us if the entry is a zero page or
 * an empty entry that is just used for locking.  In total four special bits.
 *
 * If the PMD bit isn't set the entry has size PAGE_SIZE, and if the ZERO_PAGE
 * and EMPTY bits aren't set the entry is a normal DAX entry with a filesystem
 * block allocation.
 */
#define RADIX_DAX_SHIFT	(RADIX_TREE_EXCEPTIONAL_SHIFT + 4)
#define RADIX_DAX_ENTRY_LOCK (1 << RADIX_TREE_EXCEPTIONAL_SHIFT)
#define RADIX_DAX_PMD (1 << (RADIX_TREE_EXCEPTIONAL_SHIFT + 1))
#define RADIX_DAX_ZERO_PAGE (1 << (RADIX_TREE_EXCEPTIONAL_SHIFT + 2))
#define RADIX_DAX_EMPTY (1 << (RADIX_TREE_EXCEPTIONAL_SHIFT + 3))

static inline unsigned long dax_radix_sector(void *entry)
{
	return (unsigned long)entry >> RADIX_DAX_SHIFT;
}

static inline void *dax_radix_locked_entry(sector_t sector, unsigned long flags)
{
	return (void *)(RADIX_TREE_EXCEPTIONAL_ENTRY | flags |
			((unsigned long)sector << RADIX_DAX_SHIFT) |
			RADIX_DAX_ENTRY_LOCK);
}

ssize_t dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops);
int dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    const struct iomap_ops *ops);
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index);
void dax_wake_mapping_entry_waiter(struct address_space *mapping,
		pgoff_t index, void *entry, bool wake_all);

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

#ifdef CONFIG_FS_DAX_PMD
static inline unsigned int dax_radix_order(void *entry)
{
	if ((unsigned long)entry & RADIX_DAX_PMD)
		return PMD_SHIFT - PAGE_SHIFT;
	return 0;
}
#else
static inline unsigned int dax_radix_order(void *entry)
{
	return 0;
}
#endif

static inline bool dax_mapping(struct address_space *mapping)
{
	return mapping->host && IS_DAX(mapping->host);
}

struct writeback_control;
int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc);
#endif
