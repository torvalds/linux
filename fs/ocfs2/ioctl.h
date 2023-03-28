/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ioctl.h
 *
 * Function prototypes
 *
 * Copyright (C) 2006 Herbert Poetzl
 *
 */

#ifndef OCFS2_IOCTL_PROTO_H
#define OCFS2_IOCTL_PROTO_H

int ocfs2_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int ocfs2_fileattr_set(struct mnt_idmap *idmap,
		       struct dentry *dentry, struct fileattr *fa);
long ocfs2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long ocfs2_compat_ioctl(struct file *file, unsigned cmd, unsigned long arg);

#endif /* OCFS2_IOCTL_PROTO_H */
