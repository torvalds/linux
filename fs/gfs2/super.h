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

#include "incore.h"

void gfs2_lm_unmount(struct gfs2_sbd *sdp);

static inline unsigned int gfs2_jindex_size(struct gfs2_sbd *sdp)
{
	unsigned int x;
	spin_lock(&sdp->sd_jindex_spin);
	x = sdp->sd_journals;
	spin_unlock(&sdp->sd_jindex_spin);
	return x;
}

int gfs2_jindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ji_gh);
void gfs2_jindex_free(struct gfs2_sbd *sdp);

struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid);
void gfs2_jdesc_make_dirty(struct gfs2_sbd *sdp, unsigned int jid);
struct gfs2_jdesc *gfs2_jdesc_find_dirty(struct gfs2_sbd *sdp);
int gfs2_jdesc_check(struct gfs2_jdesc *jd);

int gfs2_lookup_in_master_dir(struct gfs2_sbd *sdp, char *filename,
			      struct gfs2_inode **ipp);

int gfs2_make_fs_rw(struct gfs2_sbd *sdp);

int gfs2_statfs_init(struct gfs2_sbd *sdp);
void gfs2_statfs_change(struct gfs2_sbd *sdp,
			s64 total, s64 free, s64 dinodes);
int gfs2_statfs_sync(struct gfs2_sbd *sdp);
int gfs2_statfs_i(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc);
int gfs2_statfs_slow(struct gfs2_sbd *sdp, struct gfs2_statfs_change_host *sc);

int gfs2_freeze_fs(struct gfs2_sbd *sdp);
void gfs2_unfreeze_fs(struct gfs2_sbd *sdp);

#endif /* __SUPER_DOT_H__ */

