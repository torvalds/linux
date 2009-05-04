/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __SUPER_DOT_H__
#define __SUPER_DOT_H__

#include <linux/fs.h>
#include <linux/dcache.h>
#include "incore.h"

extern void gfs2_lm_unmount(struct gfs2_sbd *sdp);

static inline unsigned int gfs2_jindex_size(struct gfs2_sbd *sdp)
{
	unsigned int x;
	spin_lock(&sdp->sd_jindex_spin);
	x = sdp->sd_journals;
	spin_unlock(&sdp->sd_jindex_spin);
	return x;
}

void gfs2_jindex_free(struct gfs2_sbd *sdp);

extern int gfs2_mount_args(struct gfs2_sbd *sdp, struct gfs2_args *args, char *data);

extern struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid);
extern int gfs2_jdesc_check(struct gfs2_jdesc *jd);

extern int gfs2_lookup_in_master_dir(struct gfs2_sbd *sdp, char *filename,
				     struct gfs2_inode **ipp);

extern int gfs2_make_fs_rw(struct gfs2_sbd *sdp);

extern int gfs2_statfs_init(struct gfs2_sbd *sdp);
extern void gfs2_statfs_change(struct gfs2_sbd *sdp, s64 total, s64 free,
			       s64 dinodes);
extern int gfs2_statfs_sync(struct gfs2_sbd *sdp);

extern int gfs2_freeze_fs(struct gfs2_sbd *sdp);
extern void gfs2_unfreeze_fs(struct gfs2_sbd *sdp);

extern struct file_system_type gfs2_fs_type;
extern struct file_system_type gfs2meta_fs_type;
extern const struct export_operations gfs2_export_ops;
extern const struct super_operations gfs2_super_ops;
extern const struct dentry_operations gfs2_dops;

#endif /* __SUPER_DOT_H__ */

