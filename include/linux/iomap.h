/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_IOMAP_H
#define LINUX_IOMAP_H 1

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/blk_types.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/blkdev.h>
#include <linux/android_kabi.h>

struct address_space;
struct fiemap_extent_info;
struct inode;
struct iomap_dio;
struct iomap_writepage_ctx;
struct iov_iter;
struct kiocb;
struct page;
struct vm_area_struct;
struct vm_fault;

/*
 * Types of block ranges for iomap mappings:
 */
#define IOMAP_HOLE	0	/* no blocks allocated, need allocation */
#define IOMAP_DELALLOC	1	/* delayed allocation blocks */
#define IOMAP_MAPPED	2	/* blocks allocated at @addr */
#define IOMAP_UNWRITTEN	3	/* blocks allocated at @addr in unwritten state */
#define IOMAP_INLINE	4	/* data inline in the inode */

/*
 * Flags reported by the file system from iomap_begin:
 *
 * IOMAP_F_NEW indicates that the blocks have been newly allocated and need
 * zeroing for areas that no data is copied to.
 *
 * IOMAP_F_DIRTY indicates the inode has uncommitted metadata needed to access
 * written data and requires fdatasync to commit them to persistent storage.
 * This needs to take into account metadata changes that *may* be made at IO
 * completion, such as file size updates from direct IO.
 *
 * IOMAP_F_SHARED indicates that the blocks are shared, and will need to be
 * unshared as part a write.
 *
 * IOMAP_F_MERGED indicates that the iomap contains the merge of multiple block
 * mappings.
 *
 * IOMAP_F_BUFFER_HEAD indicates that the file system requires the use of
 * buffer heads for this mapping.
 */
#define IOMAP_F_NEW		0x01
#define IOMAP_F_DIRTY		0x02
#define IOMAP_F_SHARED		0x04
#define IOMAP_F_MERGED		0x08
#define IOMAP_F_BUFFER_HEAD	0x10

/*
 * Flags set by the core iomap code during operations:
 *
 * IOMAP_F_SIZE_CHANGED indicates to the iomap_end method that the file size
 * has changed as the result of this write operation.
 */
#define IOMAP_F_SIZE_CHANGED	0x100

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

	ANDROID_KABI_RESERVE(1);
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
			unsigned flags, struct iomap *iomap,
			struct iomap *srcmap);

	/*
	 * Commit and/or unreserve space previous allocated using iomap_begin.
	 * Written indicates the length of the successful write operation which
	 * needs to be commited, while the rest needs to be unreserved.
	 * Written might be zero if no data was written.
	 */
	int (*iomap_end)(struct inode *inode, loff_t pos, loff_t length,
			ssize_t written, unsigned flags, struct iomap *iomap);

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};

/*
 * Main iomap iterator function.
 */
typedef loff_t (*iomap_actor_t)(struct inode *inode, loff_t pos, loff_t len,
		void *data, struct iomap *iomap, struct iomap *srcmap);

loff_t iomap_apply(struct inode *inode, loff_t pos, loff_t length,
		unsigned flags, const struct iomap_ops *ops, void *data,
		iomap_actor_t actor);

ssize_t iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *from,
		const struct iomap_ops *ops);
int iomap_readpage(struct page *page, const struct iomap_ops *ops);
void iomap_readahead(struct readahead_control *, const struct iomap_ops *ops);
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
int iomap_file_unshare(struct inode *inode, loff_t pos, loff_t len,
		const struct iomap_ops *ops);
int iomap_zero_range(struct inode *inode, loff_t pos, loff_t len,
		bool *did_zero, const struct iomap_ops *ops);
int iomap_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops);
vm_fault_t iomap_page_mkwrite(struct vm_fault *vmf,
			const struct iomap_ops *ops);
int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len, const struct iomap_ops *ops);
loff_t iomap_seek_hole(struct inode *inode, loff_t offset,
		const struct iomap_ops *ops);
loff_t iomap_seek_data(struct inode *inode, loff_t offset,
		const struct iomap_ops *ops);
sector_t iomap_bmap(struct address_space *mapping, sector_t bno,
		const struct iomap_ops *ops);

/*
 * Structure for writeback I/O completions.
 */
struct iomap_ioend {
	struct list_head	io_list;	/* next ioend in chain */
	u16			io_type;
	u16			io_flags;	/* IOMAP_F_* */
	struct inode		*io_inode;	/* file being written to */
	size_t			io_size;	/* size of the extent */
	loff_t			io_offset;	/* offset in the file */
	void			*io_private;	/* file system private data */
	struct bio		*io_bio;	/* bio being built */
	struct bio		io_inline_bio;	/* MUST BE LAST! */
};

struct iomap_writeback_ops {
	/*
	 * Required, maps the blocks so that writeback can be performed on
	 * the range starting at offset.
	 */
	int (*map_blocks)(struct iomap_writepage_ctx *wpc, struct inode *inode,
				loff_t offset);

	/*
	 * Optional, allows the file systems to perform actions just before
	 * submitting the bio and/or override the bio end_io handler for complex
	 * operations like copy on write extent manipulation or unwritten extent
	 * conversions.
	 */
	int (*prepare_ioend)(struct iomap_ioend *ioend, int status);

	/*
	 * Optional, allows the file system to discard state on a page where
	 * we failed to submit any I/O.
	 */
	void (*discard_page)(struct page *page, loff_t fileoff);
};

struct iomap_writepage_ctx {
	struct iomap		iomap;
	struct iomap_ioend	*ioend;
	const struct iomap_writeback_ops *ops;
};

void iomap_finish_ioends(struct iomap_ioend *ioend, int error);
void iomap_ioend_try_merge(struct iomap_ioend *ioend,
		struct list_head *more_ioends,
		void (*merge_private)(struct iomap_ioend *ioend,
				struct iomap_ioend *next));
void iomap_sort_ioends(struct list_head *ioend_list);
int iomap_writepage(struct page *page, struct writeback_control *wbc,
		struct iomap_writepage_ctx *wpc,
		const struct iomap_writeback_ops *ops);
int iomap_writepages(struct address_space *mapping,
		struct writeback_control *wbc, struct iomap_writepage_ctx *wpc,
		const struct iomap_writeback_ops *ops);

/*
 * Flags for direct I/O ->end_io:
 */
#define IOMAP_DIO_UNWRITTEN	(1 << 0)	/* covers unwritten extent(s) */
#define IOMAP_DIO_COW		(1 << 1)	/* covers COW extent(s) */

struct iomap_dio_ops {
	int (*end_io)(struct kiocb *iocb, ssize_t size, int error,
		      unsigned flags);
	blk_qc_t (*submit_io)(struct inode *inode, struct iomap *iomap,
			struct bio *bio, loff_t file_offset);
};

ssize_t iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		bool wait_for_completion);
struct iomap_dio *__iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		bool wait_for_completion);
ssize_t iomap_dio_complete(struct iomap_dio *dio);
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
