/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __SUPER_DOT_H__
#define __SUPER_DOT_H__

#include <linux/fs.h>
#include <linux/dcache.h>
#include "incore.h"

/* Supported fs format version range */
#define GFS2_FS_FORMAT_MIN (1801)
#define GFS2_FS_FORMAT_MAX (1802)

extern void gfs2_lm_unmount(struct gfs2_sbd *sdp);

static inline unsigned int gfs2_jindex_size(struct gfs2_sbd *sdp)
{
	unsigned int x;
	spin_lock(&sdp->sd_jindex_spin);
	x = sdp->sd_journals;
	spin_unlock(&sdp->sd_jindex_spin);
	return x;
}

extern void gfs2_jindex_free(struct gfs2_sbd *sdp);

extern struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid);
extern int gfs2_jdesc_check(struct gfs2_jdesc *jd);
extern int gfs2_lookup_in_master_dir(struct gfs2_sbd *sdp, char *filename,
				     struct gfs2_inode **ipp);

extern int gfs2_make_fs_rw(struct gfs2_sbd *sdp);
extern void gfs2_make_fs_ro(struct gfs2_sbd *sdp);
extern void gfs2_online_uevent(struct gfs2_sbd *sdp);
extern void gfs2_destroy_threads(struct gfs2_sbd *sdp);
extern int gfs2_statfs_init(struct gfs2_sbd *sdp);
extern void gfs2_statfs_change(struct gfs2_sbd *sdp, s64 total, s64 free,
			       s64 dinodes);
extern void gfs2_statfs_change_in(struct gfs2_statfs_change_host *sc,
				  const void *buf);
extern void gfs2_statfs_change_out(const struct gfs2_statfs_change_host *sc,
				   void *buf);
extern void update_statfs(struct gfs2_sbd *sdp, struct buffer_head *m_bh);
extern int gfs2_statfs_sync(struct super_block *sb, int type);
extern void gfs2_freeze_func(struct work_struct *work);
extern void gfs2_thaw_freeze_initiator(struct super_block *sb);

extern void free_local_statfs_inodes(struct gfs2_sbd *sdp);
extern struct inode *find_local_statfs_inode(struct gfs2_sbd *sdp,
					     unsigned int index);
extern void free_sbd(struct gfs2_sbd *sdp);

extern struct file_system_type gfs2_fs_type;
extern struct file_system_type gfs2meta_fs_type;
extern const struct export_operations gfs2_export_ops;
extern const struct super_operations gfs2_super_ops;
extern const struct dentry_operations gfs2_dops;

extern const struct xattr_handler *gfs2_xattr_handlers_max[];
extern const struct xattr_handler **gfs2_xattr_handlers_min;

#endif /* __SUPER_DOT_H__ */

