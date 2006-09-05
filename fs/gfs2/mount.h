/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __MOUNT_DOT_H__
#define __MOUNT_DOT_H__

struct gfs2_sbd;

int gfs2_mount_args(struct gfs2_sbd *sdp, char *data_arg, int remount);

#endif /* __MOUNT_DOT_H__ */
