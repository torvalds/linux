// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2005-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_AOPS_H__
#define __XFS_AOPS_H__

extern struct bio_set xfs_ioend_bioset;

/*
 * Structure for buffered I/O completions.
 */
struct xfs_ioend {
	struct list_head	io_list;	/* next ioend in chain */
	int			io_fork;	/* inode fork written back */
	xfs_exntst_t		io_state;	/* extent state */
	struct inode		*io_inode;	/* file being written to */
	size_t			io_size;	/* size of the extent */
	xfs_off_t		io_offset;	/* offset in the file */
	struct work_struct	io_work;	/* xfsdatad work queue */
	struct xfs_trans	*io_append_trans;/* xact. for size update */
	struct bio		*io_bio;	/* bio being built */
	struct bio		io_inline_bio;	/* MUST BE LAST! */
};

extern const struct address_space_operations xfs_address_space_operations;
extern const struct address_space_operations xfs_dax_aops;

int	xfs_setfilesize(struct xfs_inode *ip, xfs_off_t offset, size_t size);

extern void xfs_count_page_state(struct page *, int *, int *);
extern struct block_device *xfs_find_bdev_for_inode(struct inode *);
extern struct dax_device *xfs_find_daxdev_for_inode(struct inode *);

#endif /* __XFS_AOPS_H__ */
