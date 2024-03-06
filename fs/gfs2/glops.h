/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __GLOPS_DOT_H__
#define __GLOPS_DOT_H__

#include "incore.h"

extern struct workqueue_struct *gfs2_freeze_wq;

extern const struct gfs2_glock_operations gfs2_meta_glops;
extern const struct gfs2_glock_operations gfs2_inode_glops;
extern const struct gfs2_glock_operations gfs2_rgrp_glops;
extern const struct gfs2_glock_operations gfs2_freeze_glops;
extern const struct gfs2_glock_operations gfs2_iopen_glops;
extern const struct gfs2_glock_operations gfs2_flock_glops;
extern const struct gfs2_glock_operations gfs2_nondisk_glops;
extern const struct gfs2_glock_operations gfs2_quota_glops;
extern const struct gfs2_glock_operations gfs2_journal_glops;
extern const struct gfs2_glock_operations *gfs2_glops_list[];

int gfs2_inode_metasync(struct gfs2_glock *gl);
void gfs2_ail_flush(struct gfs2_glock *gl, bool fsync);

#endif /* __GLOPS_DOT_H__ */
