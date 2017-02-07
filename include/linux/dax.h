#ifndef _LINUX_DAX_H
#define _LINUX_DAX_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/radix-tree.h>
#include <asm/pgtable.h>

struct iomap_ops;

/*
 * We use lowest available bit in exceptional entry for locking, one bit for
 * the entry size (PMD) and two more to tell us if the entry is a huge zero
 * page (HZP) or an empty entry that is just used for locking.  In total four
 * special bits.
 *
 * If the PMD bit isn't set the entry has size PAGE_SIZE, and if the HZP and
 * EMPTY bits aren't set the entry is a normal DAX entry with a filesystem
 * block allocation.
 */
#define RADIX_DAX_SHIFT	(RADIX_TREE_EXCEPTIONAL_SHIFT + 4)
#define RADIX_DAX_ENTRY_LOCK (1 << RADIX_TREE_EXCEPTIONAL_SHIFT)
#define RADIX_DAX_PMD (1 << (RADIX_TREE_EXCEPTIONAL_SHIFT + 1))
#define RADIX_DAX_HZP (1 << (RADIX_TREE_EXCEPTIONAL_SHIFT + 2))
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
		struct iomap_ops *ops);
int dax_iomap_fault(struct vm_area_struct *vma, struct vm_fault *vmf,
			struct iomap_ops *ops);
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry(struct address_space *mapping, pgoff_t index);
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index);
void dax_wake_mapping_entry_waiter(struct address_space *mapping,
		pgoff_t index, void *entry, bool wake_all);

#ifdef CONFIG_FS_DAX
struct page *read_dax_sector(struct block_device *bdev, sector_t n);
int __dax_zero_page_range(struct block_device *bdev, sector_t sector,
		unsigned int offset, unsigned int length);
#else
static inline struct page *read_dax_sector(struct block_device *bdev,
		sector_t n)
{
	return ERR_PTR(-ENXIO);
}
static inline int __dax_zero_page_range(struct block_device *bdev,
		sector_t sector, unsigned int offset, unsigned int length)
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
int dax_iomap_pmd_fault(struct vm_fault *vmf, struct iomap_ops *ops);
#else
static inline unsigned int dax_radix_order(void *entry)
{
	return 0;
}
static inline int dax_iomap_pmd_fault(struct vm_fault *vmf,
		struct iomap_ops *ops)
{
	return VM_FAULT_FALLBACK;
}
#endif
int dax_pfn_mkwrite(struct vm_area_struct *, struct vm_fault *);

static inline bool vma_is_dax(struct vm_area_struct *vma)
{
	return vma->vm_file && IS_DAX(vma->vm_file->f_mapping->host);
}

static inline bool dax_mapping(struct address_space *mapping)
{
	return mapping->host && IS_DAX(mapping->host);
}

struct writeback_control;
int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc);
#endif
