/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/devpts_fs.h
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H

#include <linux/errno.h>

#ifdef CONFIG_UNIX98_PTYS

struct pts_fs_info;

struct pts_fs_info *devpts_acquire(struct file *);
void devpts_release(struct pts_fs_info *);

int devpts_new_index(struct pts_fs_info *);
void devpts_kill_index(struct pts_fs_info *, int);

/* mknod in devpts */
struct dentry *devpts_pty_new(struct pts_fs_info *, int, void *);
/* get private structure */
void *devpts_get_priv(struct dentry *);
/* unlink */
void devpts_pty_kill(struct dentry *);

#endif


#endif /* _LINUX_DEVPTS_FS_H */
