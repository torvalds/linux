/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_IOMAP_H
#define LINUX_IOMAP_H 1

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/blkdev.h>

struct address_space;
struct fiemap_extent_info;
struct inode;
struct iov_iter;
struct kiocb;
struct page;
struct vm_area_struct;
struct vm_fault;

/*
 * Types of block ranges for iomap mappings:
 */
#define IOMAP_HOLE	0x01	/* no blocks allocated, need allocation */
#define IOMAP_DELALLOC	0x02	/* delayed allocation blocks */
#define IOMAP_MAPPED	0x03	/* blocks allocated at @addr */
#define IOMAP_UNWRITTEN	0x04	/* blocks allocated at @addr in unwritten state */
#define IOMAP_INLINE	0x05	/* data inline in the inode */

/*
 * Flags for all iomap mappings:
 *
 * IOMAP_F_DIRTY indicates the inode has uncommitted metadata needed to access
 * written data and requires fdatasync to commit them to persistent storage.
 */
#define IOMAP_F_NEW		0x01	/* blocks have been newly allocated */
#define IOMAP_F_DIRTY		0x02	/* uncommitted metadata */
#define IOMAP_F_BUFFER_HEAD	0x04	/* file system requires buffer heads */
#define IOMAP_F_SIZE_CHANGED	0x08	/* file size has changed */

/*
 * Flags that only need to be reported for IOMAP_REPORT requests:
 */
#define IOMAP_F_MERGED		0x10	/* contains multiple blocks/extents */
#define IOMAP_F_SHARED		0x20	/* block shared with another file */

/*
 * Flags from 0x1000 up are for file system specific usage:
 */
#define IOMAP_F_PRIVATE		0x1000


/*
 * Magic value for addr:
 */
#define IOMAP_NULL_ADDR -1ULL	/* addr is not valid */

struct iomap_page_ops;

struct iomap {
	u64			addr; /* disk offset of mapping, bytes */
	loff_t			offset;	/* file offset of mapping, bytes */
	u64			length;	/* length of mapping, bytes */
	u16			type;	/* type of mapping */
	u16			flags;	/* flags for mapping */
	struct block_device	*bdev;	/* block device for I/O */
	struct dax_device	*dax_dev; /* dax_dev for dax operations */
	void			*inline_data;
	void			*private; /* filesystem private */
	const struct iomap_page_ops *page_ops;
};

static inline sector_t
iomap_sector(struct iomap *iomap, loff_t pos)
{
	return (iomap->addr + pos - iomap->offset) >> SECTOR_SHIFT;
}

/*
 * When a filesystem sets page_ops in an iomap mapping it returns, page_prepare
 * and page_done will be called for each page written to.  This only applies to
 * buffered writes as unbuffered writes will not typically have pages
 * associated with them.
 *
 * When page_prepare succeeds, page_done will always be called to do any
 * cleanup work necessary.  In that page_done call, @page will be NULL if the
 * associated page could not be obtained.
 */
struct iomap_page_ops {
	int (*page_prepare)(struct inode *inode, loff_t pos, unsigned len,
			struct iomap *iomap);
	void (*page_done)(struct inode *inode, loff_t pos, unsigned copied,
			struct page *page, struct iomap *iomap);
};

/*
 * Flags for iomap_begin / iomap_end.  No flag implies a read.
 */
#define IOMAP_WRITE		(1 << 0) /* writing, must allocate blocks */
#define IOMAP_ZERO		(1 << 1) /* zeroing operation, may skip holes */
#define IOMAP_REPORT		(1 << 2) /* report extent status, e.g. FIEMAP */
#define IOMAP_FAULT		(1 << 3) /* mapping for page fault */
#define IOMAP_DIRECT		(1 << 4) /* direct I/O */
#define IOMAP_NOWAIT		(1 << 5) /* do not block */

struct iomap_ops {
	/*
	 * Return the existing mapping at pos, or reserve space starting at
	 * pos for up to length, as long as we can do it as a single mapping.
	 * The actual length is returned in iomap->length.
	 */
	int (*iomap_begin)(struct inode *inode, loff_t pos, loff_t length,
			unsigned flags, struct iomap *iomap);

	/*
	 * Commit and/or unreserve space previous allocated using iomap_begin.
	 * Written indicates the length of the successful write operation which
	 * needs to be commited, while the rest needs to be unreserved.
	 * Written might be zero if no data was written.
	 */
	int (*iomap_end)(struct inode *inode, loff_t pos, loff_t length,
			ssize_t written, unsigned flags, struct iomap *iomap);
};

/*
 * Main iomap iterator function.
 */
typedef loff_t (*iomap_actor_t)(struct inode *inode, loff_t pos, loff_t len,
		void *data, struct iomap *iomap);

loff_t iomap_apply(struct inode *inode, loff_t pos, loff_t length,
		unsigned flags, const struct iomap_ops *ops, void *data,
		iomap_actor_t actor);

/*
 * Structure allocate for each page when block size < PAGE_SIZE to track
 * sub-page uptodate status and I/O completions.
 */
struct iomap_page {
	atomic_t		read_count;
	atomic_t		write_count;
	DECLARE_BITMAP(uptodate, PAGE_SIZE / 512);
};

static inline struct iomap_page *to_iomap_page(struct page *page)
{
	if (page_has_private(page))
		return (struct iomap_page *)page_private(page);
	return NULL;
}

ssize_t iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *from,
		const struct iomap_ops *ops);
int iomap_readpage(struct page *page, const struct iomap_ops *ops);
int iomap_readpages(struct address_space *mapping, struct list_head *pages,
		unsigned nr_pages, const struct iomap_ops *ops);
int iomap_set_page_dirty(struct page *page);
int iomap_is_partially_uptodate(struct page *page, unsigned long from,
		unsigned long count);
int iomap_releasepage(struct page *page, gfp_t gfp_mask);
void iomap_invalidatepage(struct page *page, unsigned int offset,
		unsigned int len);
#ifdef CONFIG_MIGRATION
int iomap_migrate_page(struct address_space *mapping, struct page *newpage,
		struct page *page, enum migrate_mode mode);
#else
#define iomap_migrate_page NULL
#endif
int iomap_file_dirty(struct inode *inode, loff_t pos, loff_t len,
		const struct iomap_ops *ops);
int iomap_zero_range(struct inode *inode, loff_t pos, loff_t len,
		bool *did_zero, const struct iomap_ops *ops);
int iomap_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops);
vm_fault_t iomap_page_mkwrite(struct vm_fault *vmf,
			const struct iomap_ops *ops);
int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		loff_t start, loff_t len, const struct iomap_ops *ops);
loff_t iomap_seek_hole(struct inode *inode, loff_t offset,
		const struct iomap_ops *ops);
loff_t iomap_seek_data(struct inode *inode, loff_t offset,
		const struct iomap_ops *ops);
sector_t iomap_bmap(struct address_space *mapping, sector_t bno,
		const struct iomap_ops *ops);

/*
 * Flags for direct I/O ->end_io:
 */
#define IOMAP_DIO_UNWRITTEN	(1 << 0)	/* covers unwritten extent(s) */
#define IOMAP_DIO_COW		(1 << 1)	/* covers COW extent(s) */

struct iomap_dio_ops {
	int (*end_io)(struct kiocb *iocb, ssize_t size, int error,
		      unsigned flags);
};

ssize_t iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops);
int iomap_dio_iopoll(struct kiocb *kiocb, bool spin);

#ifdef CONFIG_SWAP
struct file;
struct swap_info_struct;

int iomap_swapfile_activate(struct swap_info_struct *sis,
		struct file *swap_file, sector_t *pagespan,
		const struct iomap_ops *ops);
#else
# define iomap_swapfile_activate(sis, swapfile, pagespan, ops)	(-EIO)
#endif /* CONFIG_SWAP */

#endif /* LINUX_IOMAP_H */
