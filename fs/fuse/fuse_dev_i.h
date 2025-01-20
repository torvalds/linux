/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
 */
#ifndef _FS_FUSE_DEV_I_H
#define _FS_FUSE_DEV_I_H

#include <linux/types.h>

static inline struct fuse_dev *fuse_get_dev(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	return READ_ONCE(file->private_data);
}

void fuse_dev_end_requests(struct list_head *head);

#endif

