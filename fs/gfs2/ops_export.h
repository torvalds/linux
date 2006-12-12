/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __OPS_EXPORT_DOT_H__
#define __OPS_EXPORT_DOT_H__

#define GFS2_SMALL_FH_SIZE 4
#define GFS2_LARGE_FH_SIZE 10

extern struct export_operations gfs2_export_ops;
struct gfs2_fh_obj {
	struct gfs2_inum_host this;
	__u32            imode;
};

#endif /* __OPS_EXPORT_DOT_H__ */
