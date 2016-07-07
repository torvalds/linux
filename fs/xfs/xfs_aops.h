/*
 * Copyright (c) 2005-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_AOPS_H__
#define __XFS_AOPS_H__

extern struct bio_set *xfs_ioend_bioset;

/*
 * Types of I/O for bmap clustering and I/O completion tracking.
 */
enum {
	XFS_IO_INVALID,		/* initial state */
	XFS_IO_DELALLOC,	/* covers delalloc region */
	XFS_IO_UNWRITTEN,	/* covers allocated but uninitialized data */
	XFS_IO_OVERWRITE,	/* covers already allocated extent */
};

#define XFS_IO_TYPES \
	{ XFS_IO_INVALID,		"invalid" }, \
	{ XFS_IO_DELALLOC,		"delalloc" }, \
	{ XFS_IO_UNWRITTEN,		"unwritten" }, \
	{ XFS_IO_OVERWRITE,		"overwrite" }

/*
 * Structure for buffered I/O completions.
 */
struct xfs_ioend {
	struct list_head	io_list;	/* next ioend in chain */
	unsigned int		io_type;	/* delalloc / unwritten */
	struct inode		*io_inode;	/* file being written to */
	size_t			io_size;	/* size of the extent */
	xfs_off_t		io_offset;	/* offset in the file */
	struct work_struct	io_work;	/* xfsdatad work queue */
	struct xfs_trans	*io_append_trans;/* xact. for size update */
	struct bio		*io_bio;	/* bio being built */
	struct bio		io_inline_bio;	/* MUST BE LAST! */
};

extern const struct address_space_operations xfs_address_space_operations;

int	xfs_get_blocks(struct inode *inode, sector_t offset,
		       struct buffer_head *map_bh, int create);
int	xfs_get_blocks_direct(struct inode *inode, sector_t offset,
			      struct buffer_head *map_bh, int create);
int	xfs_get_blocks_dax_fault(struct inode *inode, sector_t offset,
			         struct buffer_head *map_bh, int create);

extern void xfs_count_page_state(struct page *, int *, int *);
extern struct block_device *xfs_find_bdev_for_inode(struct inode *);

#endif /* __XFS_AOPS_H__ */
