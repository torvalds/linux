/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __GLOPS_DOT_H__
#define __GLOPS_DOT_H__

#include "incore.h"

extern const struct gfs2_glock_operations gfs2_meta_glops;
extern const struct gfs2_glock_operations gfs2_inode_glops;
extern const struct gfs2_glock_operations gfs2_rgrp_glops;
extern const struct gfs2_glock_operations gfs2_trans_glops;
extern const struct gfs2_glock_operations gfs2_iopen_glops;
extern const struct gfs2_glock_operations gfs2_flock_glops;
extern const struct gfs2_glock_operations gfs2_nondisk_glops;
extern const struct gfs2_glock_operations gfs2_quota_glops;
extern const struct gfs2_glock_operations gfs2_journal_glops;

#endif /* __GLOPS_DOT_H__ */
