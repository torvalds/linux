/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
 */
#ifndef _FS_FUSE_DEV_I_H
#define _FS_FUSE_DEV_I_H

#include <linux/types.h>

void fuse_dev_end_requests(struct list_head *head);

#endif

