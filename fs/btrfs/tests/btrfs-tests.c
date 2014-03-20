/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/magic.h>
#include "btrfs-tests.h"
#include "../ctree.h"

static struct vfsmount *test_mnt = NULL;

static const struct super_operations btrfs_test_super_ops = {
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_test_destroy_inode,
};

static struct dentry *btrfs_test_mount(struct file_system_type *fs_type,
				       int flags, const char *dev_name,
				       void *data)
{
	return mount_pseudo(fs_type, "btrfs_test:", &btrfs_test_super_ops,
			    NULL, BTRFS_TEST_MAGIC);
}

static struct file_system_type test_type = {
	.name		= "btrfs_test_fs",
	.mount		= btrfs_test_mount,
	.kill_sb	= kill_anon_super,
};

struct inode *btrfs_new_test_inode(void)
{
	return new_inode(test_mnt->mnt_sb);
}

int btrfs_init_test_fs(void)
{
	int ret;

	ret = register_filesystem(&test_type);
	if (ret) {
		printk(KERN_ERR "btrfs: cannot register test file system\n");
		return ret;
	}

	test_mnt = kern_mount(&test_type);
	if (IS_ERR(test_mnt)) {
		printk(KERN_ERR "btrfs: cannot mount test file system\n");
		unregister_filesystem(&test_type);
		return ret;
	}
	return 0;
}

void btrfs_destroy_test_fs(void)
{
	kern_unmount(test_mnt);
	unregister_filesystem(&test_type);
}
