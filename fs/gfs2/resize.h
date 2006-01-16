/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __RESIZE_DOT_H__
#define __RESIZE_DOT_H__

int gfs2_resize_add_rgrps(struct gfs2_sbd *sdp, char __user *buf,
			  unsigned int size);
int gfs2_rename2system(struct gfs2_inode *ip,
		       struct gfs2_inode *old_dip, char *old_name,
		       struct gfs2_inode *new_dip, char *new_name);

#endif /* __RESIZE_DOT_H__ */
