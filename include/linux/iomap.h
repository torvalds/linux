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

struct address_space;
struct fiemap_extent_info;
struct inode;
struct iomap_iter;
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
 *
 * IOMAP_F_XATTR indicates that the iomap is for an extended attribute extent
 * rather than a file data extent.
 */
#define IOMAP_F_NEW		(1U << 0)
#define IOMAP_F_DIRTY		(1U << 1)
#define IOMAP_F_SHARED		(1U << 2)
#define IOMAP_F_MERGED		(1U << 3)
#ifdef CONFIG_BUFFER_HEAD
#define IOMAP_F_BUFFER_HEAD	(1U << 4)
#else
#define IOMAP_F_BUFFER_HEAD	0
#endif /* CONFIG_BUFFER_HEAD */
#define IOMAP_F_XATTR		(1U << 5)

/*
 * Flags set by the core iomap code during operations:
 *
 * IOMAP_F_SIZE_CHANGED indicates to the iomap_end method that the file size
 * has changed as the result of this write operation.
 *
 * IOMAP_F_STALE indicates that the iomap is not valid any longer and the file
 * range it covers needs to be remapped by the high level before the operation
 * can proceed.
 */
#define IOMAP_F_SIZE_CHANGED	(1U << 8)
#define IOMAP_F_STALE		(1U << 9)

/*
 * Flags from 0x1000 up are for file system specific usage:
 */
#define IOMAP_F_PRIVATE		(1U << 12)


/*
 * Magic value for addr:
 */
#define IOMAP_NULL_ADDR -1ULL	/* addr is not valid */

struct iomap_folio_ops;

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
	const struct iomap_folio_ops *folio_ops;
	u64			validity_cookie; /* used with .iomap_valid() */
};

static inline sector_t iomap_sector(const struct iomap *iomap, loff_t pos)
{
	return (iomap->addr + pos - iomap->offset) >> SECTOR_SHIFT;
}

/*
 * Returns the inline data pointer for logical offset @pos.
 */
static inline void *iomap_inline_data(const struct iomap *iomap, loff_t pos)
{
	return iomap->inline_data + pos - iomap->offset;
}

/*
 * Check if the mapping's length is within the valid range for inline data.
 * This is used to guard against accessing data beyond the page inline_data
 * points at.
 */
static inline bool iomap_inline_data_valid(const struct iomap *iomap)
{
	return iomap->length <= PAGE_SIZE - offset_in_page(iomap->inline_data);
}

/*
 * When a filesystem sets folio_ops in an iomap mapping it returns, get_folio
 * and put_folio will be called for each folio written to.  This only applies
 * to buffered writes as unbuffered writes will not typically have folios
 * associated with them.
 *
 * When get_folio succeeds, put_folio will always be called to do any
 * cleanup work necessary.  put_folio is responsible for unlocking and putting
 * @folio.
 */
struct iomap_folio_ops {
	struct folio *(*get_folio)(struct iomap_iter *iter, loff_t pos,
			unsigned len);
	void (*put_folio)(struct inode *inode, loff_t pos, unsigned copied,
			struct folio *folio);

	/*
	 * Check that the cached iomap still maps correctly to the filesystem's
	 * internal extent map. FS internal extent maps can change while iomap
	 * is iterating a cached iomap, so this hook allows iomap to detect that
	 * the iomap needs to be refreshed during a long running write
	 * operation.
	 *
	 * The filesystem can store internal state (e.g. a sequence number) in
	 * iomap->validity_cookie when the iomap is first mapped to be able to
	 * detect changes between mapping time and whenever .iomap_valid() is
	 * called.
	 *
	 * This is called with the folio over the specified file position held
	 * locked by the iomap code.
	 */
	bool (*iomap_valid)(struct inode *inode, const struct iomap *iomap);
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
#define IOMAP_OVERWRITE_ONLY	(1 << 6) /* only pure overwrites allowed */
#define IOMAP_UNSHARE		(1 << 7) /* unshare_file_range */
#ifdef CONFIG_FS_DAX
#define IOMAP_DAX		(1 << 8) /* DAX mapping */
#else
#define IOMAP_DAX		0
#endif /* CONFIG_FS_DAX */

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
};

/**
 * struct iomap_iter - Iterate through a range of a file
 * @inode: Set at the start of the iteration and should not change.
 * @pos: The current file position we are operating on.  It is updated by
 *	calls to iomap_iter().  Treat as read-only in the body.
 * @len: The remaining length of the file segment we're operating on.
 *	It is updated at the same time as @pos.
 * @processed: The number of bytes processed by the body in the most recent
 *	iteration, or a negative errno. 0 causes the iteration to stop.
 * @flags: Zero or more of the iomap_begin flags above.
 * @iomap: Map describing the I/O iteration
 * @srcmap: Source map for COW operations
 */
struct iomap_iter {
	struct inode *inode;
	loff_t pos;
	u64 len;
	s64 processed;
	unsigned flags;
	struct iomap iomap;
	struct iomap srcmap;
	void *private;
};

int iomap_iter(struct iomap_iter *iter, const struct iomap_ops *ops);

/**
 * iomap_length - length of the current iomap iteration
 * @iter: iteration structure
 *
 * Returns the length that the operation applies to for the current iteration.
 */
static inline u64 iomap_length(const struct iomap_iter *iter)
{
	u64 end = iter->iomap.offset + iter->iomap.length;

	if (iter->srcmap.type != IOMAP_HOLE)
		end = min(end, iter->srcmap.offset + iter->srcmap.length);
	return min(iter->len, end - iter->pos);
}

/**
 * iomap_iter_srcmap - return the source map for the current iomap iteration
 * @i: iteration structure
 *
 * Write operations on file systems with reflink support might require a
 * source and a destination map.  This function retourns the source map
 * for a given operation, which may or may no be identical to the destination
 * map in &i->iomap.
 */
static inline const struct iomap *iomap_iter_srcmap(const struct iomap_iter *i)
{
	if (i->srcmap.type != IOMAP_HOLE)
		return &i->srcmap;
	return &i->iomap;
}

ssize_t iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *from,
		const struct iomap_ops *ops);
int iomap_file_buffered_write_punch_delalloc(struct inode *inode,
		struct iomap *iomap, loff_t pos, loff_t length, ssize_t written,
		int (*punch)(struct inode *inode, loff_t pos, loff_t length));

int iomap_read_folio(struct folio *folio, const struct iomap_ops *ops);
void iomap_readahead(struct readahead_control *, const struct iomap_ops *ops);
bool iomap_is_partially_uptodate(struct folio *, size_t from, size_t count);
struct folio *iomap_get_folio(struct iomap_iter *iter, loff_t pos, size_t len);
bool iomap_release_folio(struct folio *folio, gfp_t gfp_flags);
void iomap_invalidate_folio(struct folio *folio, size_t offset, size_t len);
bool iomap_dirty_folio(struct address_space *mapping, struct folio *folio);
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
	u32			io_folios;	/* folios added to ioend */
	struct inode		*io_inode;	/* file being written to */
	size_t			io_size;	/* size of the extent */
	loff_t			io_offset;	/* offset in the file */
	sector_t		io_sector;	/* start sector of ioend */
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
	void (*discard_folio)(struct folio *folio, loff_t pos);
};

struct iomap_writepage_ctx {
	struct iomap		iomap;
	struct iomap_ioend	*ioend;
	const struct iomap_writeback_ops *ops;
};

void iomap_finish_ioends(struct iomap_ioend *ioend, int error);
void iomap_ioend_try_merge(struct iomap_ioend *ioend,
		struct list_head *more_ioends);
void iomap_sort_ioends(struct list_head *ioend_list);
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
	void (*submit_io)(const struct iomap_iter *iter, struct bio *bio,
		          loff_t file_offset);

	/*
	 * Filesystems wishing to attach private information to a direct io bio
	 * must provide a ->submit_io method that attaches the additional
	 * information to the bio and changes the ->bi_end_io callback to a
	 * custom function.  This function should, at a minimum, perform any
	 * relevant post-processing of the bio and end with a call to
	 * iomap_dio_bio_end_io.
	 */
	struct bio_set *bio_set;
};

/*
 * Wait for the I/O to complete in iomap_dio_rw even if the kiocb is not
 * synchronous.
 */
#define IOMAP_DIO_FORCE_WAIT	(1 << 0)

/*
 * Do not allocate blocks or zero partial blocks, but instead fall back to
 * the caller by returning -EAGAIN.  Used to optimize direct I/O writes that
 * are not aligned to the file system block size.
  */
#define IOMAP_DIO_OVERWRITE_ONLY	(1 << 1)

/*
 * When a page fault occurs, return a partial synchronous result and allow
 * the caller to retry the rest of the operation after dealing with the page
 * fault.
 */
#define IOMAP_DIO_PARTIAL		(1 << 2)

ssize_t iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		unsigned int dio_flags, void *private, size_t done_before);
struct iomap_dio *__iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, const struct iomap_dio_ops *dops,
		unsigned int dio_flags, void *private, size_t done_before);
ssize_t iomap_dio_complete(struct iomap_dio *dio);
void iomap_dio_bio_end_io(struct bio *bio);

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
