/*
 * ioctl.h
 *
 * Function prototypes
 *
 * Copyright (C) 2006 Herbert Poetzl
 *
 */

#ifndef OCFS2_IOCTL_H
#define OCFS2_IOCTL_H

int ocfs2_ioctl(struct inode * inode, struct file * filp,
	unsigned int cmd, unsigned long arg);
long ocfs2_compat_ioctl(struct file *file, unsigned cmd, unsigned long arg);

#endif /* OCFS2_IOCTL_H */
