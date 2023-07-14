/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __INODE_DOT_H__
#define __INODE_DOT_H__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include "util.h"

bool gfs2_release_folio(struct folio *folio, gfp_t gfp_mask);
extern int gfs2_internal_read(struct gfs2_inode *ip,
			      char *buf, loff_t *pos, unsigned size);
extern void gfs2_set_aops(struct inode *inode);

static inline int gfs2_is_stuffed(const struct gfs2_inode *ip)
{
	return !ip->i_height;
}

static inline int gfs2_is_jdata(const struct gfs2_inode *ip)
{
	return ip->i_diskflags & GFS2_DIF_JDATA;
}

static inline bool gfs2_is_ordered(const struct gfs2_sbd *sdp)
{
	return sdp->sd_args.ar_data == GFS2_DATA_ORDERED;
}

static inline bool gfs2_is_writeback(const struct gfs2_sbd *sdp)
{
	return sdp->sd_args.ar_data == GFS2_DATA_WRITEBACK;
}

static inline int gfs2_is_dir(const struct gfs2_inode *ip)
{
	return S_ISDIR(ip->i_inode.i_mode);
}

static inline void gfs2_set_inode_blocks(struct inode *inode, u64 blocks)
{
	inode->i_blocks = blocks <<
		(GFS2_SB(inode)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline u64 gfs2_get_inode_blocks(const struct inode *inode)
{
	return inode->i_blocks >>
		(GFS2_SB(inode)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline void gfs2_add_inode_blocks(struct inode *inode, s64 change)
{
	change <<= inode->i_blkbits - GFS2_BASIC_BLOCK_SHIFT;
	gfs2_assert(GFS2_SB(inode), (change >= 0 || inode->i_blocks >= -change));
	inode->i_blocks += change;
}

static inline int gfs2_check_inum(const struct gfs2_inode *ip, u64 no_addr,
				  u64 no_formal_ino)
{
	return ip->i_no_addr == no_addr && ip->i_no_formal_ino == no_formal_ino;
}

static inline void gfs2_inum_out(const struct gfs2_inode *ip,
				 struct gfs2_dirent *dent)
{
	dent->de_inum.no_formal_ino = cpu_to_be64(ip->i_no_formal_ino);
	dent->de_inum.no_addr = cpu_to_be64(ip->i_no_addr);
}

static inline int gfs2_check_internal_file_size(struct inode *inode,
						u64 minsize, u64 maxsize)
{
	u64 size = i_size_read(inode);
	if (size < minsize || size > maxsize)
		goto err;
	if (size & (BIT(inode->i_blkbits) - 1))
		goto err;
	return 0;
err:
	gfs2_consist_inode(GFS2_I(inode));
	return -EIO;
}

extern struct inode *gfs2_inode_lookup(struct super_block *sb, unsigned type, 
				       u64 no_addr, u64 no_formal_ino,
				       unsigned int blktype);
extern struct inode *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 no_addr,
					 u64 no_formal_ino,
					 unsigned int blktype);

extern int gfs2_inode_refresh(struct gfs2_inode *ip);

extern struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
				  int is_root);
extern int gfs2_permission(struct mnt_idmap *idmap,
			   struct inode *inode, int mask);
extern struct inode *gfs2_lookup_simple(struct inode *dip, const char *name);
extern void gfs2_dinode_out(const struct gfs2_inode *ip, void *buf);
extern int gfs2_open_common(struct inode *inode, struct file *file);
extern loff_t gfs2_seek_data(struct file *file, loff_t offset);
extern loff_t gfs2_seek_hole(struct file *file, loff_t offset);

extern const struct file_operations gfs2_file_fops_nolock;
extern const struct file_operations gfs2_dir_fops_nolock;

extern int gfs2_fileattr_get(struct dentry *dentry, struct fileattr *fa);
extern int gfs2_fileattr_set(struct mnt_idmap *idmap,
			     struct dentry *dentry, struct fileattr *fa);
extern void gfs2_set_inode_flags(struct inode *inode);
 
#ifdef CONFIG_GFS2_FS_LOCKING_DLM
extern const struct file_operations gfs2_file_fops;
extern const struct file_operations gfs2_dir_fops;

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return sdp->sd_args.ar_localflocks;
}
#else /* Single node only */
#define gfs2_file_fops gfs2_file_fops_nolock
#define gfs2_dir_fops gfs2_dir_fops_nolock

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return 1;
}
#endif /* CONFIG_GFS2_FS_LOCKING_DLM */

#endif /* __INODE_DOT_H__ */

