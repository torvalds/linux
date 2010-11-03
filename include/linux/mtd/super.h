/* MTD-based superblock handling
 *
 * Copyright © 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __MTD_SUPER_H__
#define __MTD_SUPER_H__

#ifdef __KERNEL__

#include <linux/mtd/mtd.h>
#include <linux/fs.h>
#include <linux/mount.h>

extern struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		      const char *dev_name, void *data,
		      int (*fill_super)(struct super_block *, void *, int));
extern void kill_mtd_super(struct super_block *sb);


#endif /* __KERNEL__ */

#endif /* __MTD_SUPER_H__ */
