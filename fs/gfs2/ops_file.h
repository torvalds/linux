/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __OPS_FILE_DOT_H__
#define __OPS_FILE_DOT_H__

#include <linux/fs.h>
struct gfs2_inode;

extern struct file gfs2_internal_file_sentinel;
extern int gfs2_internal_read(struct gfs2_inode *ip,
			      struct file_ra_state *ra_state,
			      char *buf, loff_t *pos, unsigned size);
extern void gfs2_set_inode_flags(struct inode *inode);
extern const struct file_operations gfs2_file_fops;
extern const struct file_operations gfs2_dir_fops;

#endif /* __OPS_FILE_DOT_H__ */
