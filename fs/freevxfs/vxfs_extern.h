/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */
#ifndef _VXFS_EXTERN_H_
#define _VXFS_EXTERN_H_

/*
 * Veritas filesystem driver - external prototypes.
 *
 * This file contains prototypes for all vxfs functions used
 * outside their respective source files.
 */


struct kmem_cache;
struct super_block;
struct vxfs_inode_info;
struct inode;


/* vxfs_bmap.c */
extern daddr_t			vxfs_bmap1(struct inode *, long);

/* vxfs_fshead.c */
extern int			vxfs_read_fshead(struct super_block *);

/* vxfs_inode.c */
extern const struct address_space_operations vxfs_immed_aops;
extern void			vxfs_dumpi(struct vxfs_inode_info *, ino_t);
extern struct inode		*vxfs_blkiget(struct super_block *, u_long, ino_t);
extern struct inode		*vxfs_stiget(struct super_block *, ino_t);
extern struct inode		*vxfs_iget(struct super_block *, ino_t);
extern void			vxfs_evict_inode(struct inode *);

/* vxfs_lookup.c */
extern const struct inode_operations	vxfs_dir_inode_ops;
extern const struct file_operations	vxfs_dir_operations;

/* vxfs_olt.c */
extern int			vxfs_read_olt(struct super_block *, u_long);

/* vxfs_subr.c */
extern const struct address_space_operations vxfs_aops;
extern struct page *		vxfs_get_page(struct address_space *, u_long);
extern void			vxfs_put_page(struct page *);
extern struct buffer_head *	vxfs_bread(struct inode *, int);

#endif /* _VXFS_EXTERN_H_ */
