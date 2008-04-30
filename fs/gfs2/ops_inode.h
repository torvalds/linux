/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __OPS_INODE_DOT_H__
#define __OPS_INODE_DOT_H__

#include <linux/fs.h>

extern const struct inode_operations gfs2_file_iops;
extern const struct inode_operations gfs2_dir_iops;
extern const struct inode_operations gfs2_symlink_iops;
extern const struct file_operations gfs2_file_fops;
extern const struct file_operations gfs2_dir_fops;
extern const struct file_operations gfs2_file_fops_nolock;
extern const struct file_operations gfs2_dir_fops_nolock;

extern void gfs2_set_inode_flags(struct inode *inode);

#endif /* __OPS_INODE_DOT_H__ */
