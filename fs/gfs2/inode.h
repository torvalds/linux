/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __INODE_DOT_H__
#define __INODE_DOT_H__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include "util.h"

extern int gfs2_releasepage(struct page *page, gfp_t gfp_mask);
extern int gfs2_internal_read(struct gfs2_inode *ip,
			      struct file_ra_state *ra_state,
			      char *buf, loff_t *pos, unsigned size);
extern void gfs2_page_add_databufs(struct gfs2_inode *ip, struct page *page,
				   unsigned int from, unsigned int to);
extern void gfs2_set_aops(struct inode *inode);

static inline int gfs2_is_stuffed(const struct gfs2_inode *ip)
{
	return !ip->i_height;
}

static inline int gfs2_is_jdata(const struct gfs2_inode *ip)
{
	return ip->i_diskflags & GFS2_DIF_JDATA;
}

static inline int gfs2_is_writeback(const struct gfs2_inode *ip)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	return (sdp->sd_args.ar_data == GFS2_DATA_WRITEBACK) && !gfs2_is_jdata(ip);
}

static inline int gfs2_is_ordered(const struct gfs2_inode *ip)
{
	const struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	return (sdp->sd_args.ar_data == GFS2_DATA_ORDERED) && !gfs2_is_jdata(ip);
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
	gfs2_assert(GFS2_SB(inode), (change >= 0 || inode->i_blocks > -change));
	change *= (GFS2_SB(inode)->sd_sb.sb_bsize/GFS2_BASIC_BLOCK);
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
	if (size & ((1 << inode->i_blkbits) - 1))
		goto err;
	return 0;
err:
	gfs2_consist_inode(GFS2_I(inode));
	return -EIO;
}

extern void gfs2_set_iop(struct inode *inode);
extern struct inode *gfs2_inode_lookup(struct super_block *sb, unsigned type, 
				       u64 no_addr, u64 no_formal_ino);
extern struct inode *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 no_addr,
					 u64 *no_formal_ino,
					 unsigned int blktype);
extern struct inode *gfs2_ilookup(struct super_block *sb, u64 no_addr);

extern int gfs2_inode_refresh(struct gfs2_inode *ip);

extern int gfs2_dinode_dealloc(struct gfs2_inode *inode);
extern int gfs2_change_nlink(struct gfs2_inode *ip, int diff);
extern struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
				  int is_root);
extern struct inode *gfs2_createi(struct gfs2_holder *ghs,
				  const struct qstr *name,
				  unsigned int mode, dev_t dev);
extern int gfs2_permission(struct inode *inode, int mask, unsigned int flags);
extern int gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr);
extern struct inode *gfs2_lookup_simple(struct inode *dip, const char *name);
extern void gfs2_dinode_out(const struct gfs2_inode *ip, void *buf);
extern void gfs2_dinode_print(const struct gfs2_inode *ip);

extern const struct inode_operations gfs2_file_iops;
extern const struct inode_operations gfs2_dir_iops;
extern const struct inode_operations gfs2_symlink_iops;
extern const struct file_operations gfs2_file_fops_nolock;
extern const struct file_operations gfs2_dir_fops_nolock;

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

