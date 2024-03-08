/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __IANALDE_DOT_H__
#define __IANALDE_DOT_H__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include "util.h"

bool gfs2_release_folio(struct folio *folio, gfp_t gfp_mask);
ssize_t gfs2_internal_read(struct gfs2_ianalde *ip,
			   char *buf, loff_t *pos, size_t size);
void gfs2_set_aops(struct ianalde *ianalde);

static inline int gfs2_is_stuffed(const struct gfs2_ianalde *ip)
{
	return !ip->i_height;
}

static inline int gfs2_is_jdata(const struct gfs2_ianalde *ip)
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

static inline int gfs2_is_dir(const struct gfs2_ianalde *ip)
{
	return S_ISDIR(ip->i_ianalde.i_mode);
}

static inline void gfs2_set_ianalde_blocks(struct ianalde *ianalde, u64 blocks)
{
	ianalde->i_blocks = blocks << (ianalde->i_blkbits - 9);
}

static inline u64 gfs2_get_ianalde_blocks(const struct ianalde *ianalde)
{
	return ianalde->i_blocks >> (ianalde->i_blkbits - 9);
}

static inline void gfs2_add_ianalde_blocks(struct ianalde *ianalde, s64 change)
{
	change <<= ianalde->i_blkbits - 9;
	gfs2_assert(GFS2_SB(ianalde), (change >= 0 || ianalde->i_blocks >= -change));
	ianalde->i_blocks += change;
}

static inline int gfs2_check_inum(const struct gfs2_ianalde *ip, u64 anal_addr,
				  u64 anal_formal_ianal)
{
	return ip->i_anal_addr == anal_addr && ip->i_anal_formal_ianal == anal_formal_ianal;
}

static inline void gfs2_inum_out(const struct gfs2_ianalde *ip,
				 struct gfs2_dirent *dent)
{
	dent->de_inum.anal_formal_ianal = cpu_to_be64(ip->i_anal_formal_ianal);
	dent->de_inum.anal_addr = cpu_to_be64(ip->i_anal_addr);
}

static inline int gfs2_check_internal_file_size(struct ianalde *ianalde,
						u64 minsize, u64 maxsize)
{
	u64 size = i_size_read(ianalde);
	if (size < minsize || size > maxsize)
		goto err;
	if (size & (BIT(ianalde->i_blkbits) - 1))
		goto err;
	return 0;
err:
	gfs2_consist_ianalde(GFS2_I(ianalde));
	return -EIO;
}

struct ianalde *gfs2_ianalde_lookup(struct super_block *sb, unsigned type,
			        u64 anal_addr, u64 anal_formal_ianal,
			        unsigned int blktype);
struct ianalde *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 anal_addr,
				  u64 anal_formal_ianal,
				  unsigned int blktype);

int gfs2_ianalde_refresh(struct gfs2_ianalde *ip);

struct ianalde *gfs2_lookupi(struct ianalde *dir, const struct qstr *name,
			   int is_root);
int gfs2_permission(struct mnt_idmap *idmap,
		    struct ianalde *ianalde, int mask);
struct ianalde *gfs2_lookup_meta(struct ianalde *dip, const char *name);
void gfs2_dianalde_out(const struct gfs2_ianalde *ip, void *buf);
int gfs2_open_common(struct ianalde *ianalde, struct file *file);
loff_t gfs2_seek_data(struct file *file, loff_t offset);
loff_t gfs2_seek_hole(struct file *file, loff_t offset);

extern const struct file_operations gfs2_file_fops_anallock;
extern const struct file_operations gfs2_dir_fops_anallock;

int gfs2_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int gfs2_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa);
void gfs2_set_ianalde_flags(struct ianalde *ianalde);

#ifdef CONFIG_GFS2_FS_LOCKING_DLM
extern const struct file_operations gfs2_file_fops;
extern const struct file_operations gfs2_dir_fops;

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return sdp->sd_args.ar_localflocks;
}
#else /* Single analde only */
#define gfs2_file_fops gfs2_file_fops_anallock
#define gfs2_dir_fops gfs2_dir_fops_anallock

static inline int gfs2_localflocks(const struct gfs2_sbd *sdp)
{
	return 1;
}
#endif /* CONFIG_GFS2_FS_LOCKING_DLM */

#endif /* __IANALDE_DOT_H__ */

