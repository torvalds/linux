#ifndef LINUX_IOMAP_H
#define LINUX_IOMAP_H 1

#include <linux/types.h>

struct fiemap_extent_info;
struct inode;
struct iov_iter;
struct kiocb;
struct vm_area_struct;
struct vm_fault;

/*
 * Types of block ranges for iomap mappings:
 */
#define IOMAP_HOLE	0x01	/* no blocks allocated, need allocation */
#define IOMAP_DELALLOC	0x02	/* delayed allocation blocks */
#define IOMAP_MAPPED	0x03	/* blocks allocated @blkno */
#define IOMAP_UNWRITTEN	0x04	/* blocks allocated @blkno in unwritten state */

/*
 * Flags for all iomap mappings:
 */
#define IOMAP_F_NEW	0x01	/* blocks have been newly allocated */

/*
 * Flags that only need to be reported for IOMAP_REPORT requests:
 */
#define IOMAP_F_MERGED	0x10	/* contains multiple blocks/extents */
#define IOMAP_F_SHARED	0x20	/* block shared with another file */

/*
 * Magic value for blkno:
 */
#define IOMAP_NULL_BLOCK -1LL	/* blkno is not valid */

struct iomap {
	sector_t		blkno;	/* 1st sector of mapping, 512b units */
	loff_t			offset;	/* file offset of mapping, bytes */
	u64			length;	/* length of mapping, bytes */
	u16			type;	/* type of mapping */
	u16			flags;	/* flags for mapping */
	struct block_device	*bdev;	/* block device for I/O */
	struct dax_device	*dax_dev; /* dax_dev for dax operations */
};

/*
 * Flags for iomap_begin / iomap_end.  No flag implies a read.
 */
#define IOMAP_WRITE		(1 << 0) /* writing, must allocate blocks */
#define IOMAP_ZERO		(1 << 1) /* zeroing operation, may skip holes */
#define IOMAP_REPORT		(1 << 2) /* report extent status, e.g. FIEMAP */
#define IOMAP_FAULT		(1 << 3) /* mapping for page fault */
#define IOMAP_DIRECT		(1 << 4) /* direct I/O */

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

ssize_t iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *from,
		const struct iomap_ops *ops);
int iomap_file_dirty(struct inode *inode, loff_t pos, loff_t len,
		const struct iomap_ops *ops);
int iomap_zero_range(struct inode *inode, loff_t pos, loff_t len,
		bool *did_zero, const struct iomap_ops *ops);
int iomap_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops);
int iomap_page_mkwrite(struct vm_fault *vmf, const struct iomap_ops *ops);
int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		loff_t start, loff_t len, const struct iomap_ops *ops);

/*
 * Flags for direct I/O ->end_io:
 */
#define IOMAP_DIO_UNWRITTEN	(1 << 0)	/* covers unwritten extent(s) */
#define IOMAP_DIO_COW		(1 << 1)	/* covers COW extent(s) */
typedef int (iomap_dio_end_io_t)(struct kiocb *iocb, ssize_t ret,
		unsigned flags);
ssize_t iomap_dio_rw(struct kiocb *iocb, struct iov_iter *iter,
		const struct iomap_ops *ops, iomap_dio_end_io_t end_io);

#endif /* LINUX_IOMAP_H */
