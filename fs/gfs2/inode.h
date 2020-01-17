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

extern int gfs2_releasepage(struct page *page, gfp_t gfp_mask);
extern int gfs2_internal_read(struct gfs2_iyesde *ip,
			      char *buf, loff_t *pos, unsigned size);
extern void gfs2_set_aops(struct iyesde *iyesde);

static inline int gfs2_is_stuffed(const struct gfs2_iyesde *ip)
{
	return !ip->i_height;
}

static inline int gfs2_is_jdata(const struct gfs2_iyesde *ip)
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

static inline int gfs2_is_dir(const struct gfs2_iyesde *ip)
{
	return S_ISDIR(ip->i_iyesde.i_mode);
}

static inline void gfs2_set_iyesde_blocks(struct iyesde *iyesde, u64 blocks)
{
	iyesde->i_blocks = blocks <<
		(GFS2_SB(iyesde)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline u64 gfs2_get_iyesde_blocks(const struct iyesde *iyesde)
{
	return iyesde->i_blocks >>
		(GFS2_SB(iyesde)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);
}

static inline void gfs2_add_iyesde_blocks(struct iyesde *iyesde, s64 change)
{
	change <<= iyesde->i_blkbits - GFS2_BASIC_BLOCK_SHIFT;
	gfs2_assert(GFS2_SB(iyesde), (change >= 0 || iyesde->i_blocks >= -change));
	iyesde->i_blocks += change;
}

static inline int gfs2_check_inum(const struct gfs2_iyesde *ip, u64 yes_addr,
				  u64 yes_formal_iyes)
{
	return ip->i_yes_addr == yes_addr && ip->i_yes_formal_iyes == yes_formal_iyes;
}

static inline void gfs2_inum_out(const struct gfs2_iyesde *ip,
				 struct gfs2_dirent *dent)
{
	dent->de_inum.yes_formal_iyes = cpu_to_be64(ip->i_yes_formal_iyes);
	dent->de_inum.yes_addr = cpu_to_be64(ip->i_yes_addr);
}

static inline int gfs2_check_internal_file_size(struct iyesde *iyesde,
						u64 minsize, u64 maxsize)
{
	u64 size = i_size_read(iyesde);
	if (size < minsize || size > maxsize)
		goto err;
	if (size & (BIT(iyesde->i_blkbits) - 1))
		goto err;
	return 0;
err:
	gfs2_consist_iyesde(GFS2_I(iyesde));
	return -EIO;
}

extern struct iyesde *gfs2_iyesde_lookup(struct super_block *sb, unsigned type, 
				       u64 yes_addr, u64 yes_formal_iyes,
				       unsigned int blktype);
extern struct iyesde *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 yes_addr,
					 u64 *yes_formal_iyes,
					 unsigned int blktype);

extern int gfs2_iyesde_refresh(struct gfs2_iyesde *ip);

extern struct iyesde *gfs2_lookupi(struct iyesde *dir, const struct qstr *name,
				  int is_root);
extern int gfs2_permission(struct iyesde *iyesde, int mask);
extern int gfs2_setattr_simple(struct iyesde *iyesde, struct iattr *attr);
extern struct iyesde *gfs2_lookup_simple(struct iyesde *dip, const char *name);
extern void gfs2_diyesde_out(const struct gfs2_iyesde *ip, void *buf);
extern int gfs2_open_common(struct iyesde *iyesde, struct file *file);
extern loff_t gfs2_seek_data(struct file *file, loff_t offset);
extern loff_t gfs2_seek_hole(struct file *file, loff_t offset);

extern const struct iyesde_operations gfs2_file_iops;
extern const struct iyesde_operations gfs2_dir_iops;
extern const struct iyesde_operations gfs2_symlink_iops;
extern const struct file_operations gfs2_file_fops_yeslock;
extern const struct file_operations gfs2_dir_fops_yeslock;

extern void gfs2_set_iyesde_flags(struct iyesde *iyesde);
 
#ifdef CONFIG_GFS2_FS_LOCKING_DLM
extern const struct file_operations gfs2_file_fops;
extern const struct file_operations gfs2_dir_fops;

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return sdp->sd_args.ar_localflocks;
}
#else /* Single yesde only */
#define gfs2_file_fops gfs2_file_fops_yeslock
#define gfs2_dir_fops gfs2_dir_fops_yeslock

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return 1;
}
#endif /* CONFIG_GFS2_FS_LOCKING_DLM */

#endif /* __INODE_DOT_H__ */

