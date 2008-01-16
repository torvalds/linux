/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/parser.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "xattr.h"

#define BTRFS_SUPER_MAGIC 0x9123683E

static struct super_operations btrfs_super_ops;

static void btrfs_put_super (struct super_block * sb)
{
	struct btrfs_root *root = btrfs_sb(sb);
	struct btrfs_fs_info *fs = root->fs_info;
	int ret;

	ret = close_ctree(root);
	if (ret) {
		printk("close ctree returns %d\n", ret);
	}
	btrfs_sysfs_del_super(fs);
	sb->s_fs_info = NULL;
}

enum {
	Opt_subvol, Opt_nodatasum, Opt_nodatacow, Opt_max_extent,
	Opt_alloc_start, Opt_nobarrier, Opt_err,
};

static match_table_t tokens = {
	{Opt_subvol, "subvol=%s"},
	{Opt_nodatasum, "nodatasum"},
	{Opt_nodatacow, "nodatacow"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_max_extent, "max_extent=%s"},
	{Opt_alloc_start, "alloc_start=%s"},
	{Opt_err, NULL}
};

u64 btrfs_parse_size(char *str)
{
	u64 res;
	int mult = 1;
	char *end;
	char last;

	res = simple_strtoul(str, &end, 10);

	last = end[0];
	if (isalpha(last)) {
		last = tolower(last);
		switch (last) {
		case 'g':
			mult *= 1024;
		case 'm':
			mult *= 1024;
		case 'k':
			mult *= 1024;
		}
		res = res * mult;
	}
	return res;
}

static int parse_options (char * options,
			  struct btrfs_root *root,
			  char **subvol_name)
{
	char * p;
	struct btrfs_fs_info *info = NULL;
	substring_t args[MAX_OPT_ARGS];

	if (!options)
		return 1;

	/*
	 * strsep changes the string, duplicate it because parse_options
	 * gets called twice
	 */
	options = kstrdup(options, GFP_NOFS);
	if (!options)
		return -ENOMEM;

	if (root)
		info = root->fs_info;

	while ((p = strsep (&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_subvol:
			if (subvol_name) {
				*subvol_name = match_strdup(&args[0]);
			}
			break;
		case Opt_nodatasum:
			if (info) {
				printk("btrfs: setting nodatacsum\n");
				btrfs_set_opt(info->mount_opt, NODATASUM);
			}
			break;
		case Opt_nodatacow:
			if (info) {
				printk("btrfs: setting nodatacow\n");
				btrfs_set_opt(info->mount_opt, NODATACOW);
				btrfs_set_opt(info->mount_opt, NODATASUM);
			}
			break;
		case Opt_nobarrier:
			if (info) {
				printk("btrfs: turning off barriers\n");
				btrfs_set_opt(info->mount_opt, NOBARRIER);
			}
			break;
		case Opt_max_extent:
			if (info) {
				char *num = match_strdup(&args[0]);
				if (num) {
					info->max_extent =
						btrfs_parse_size(num);
					kfree(num);

					info->max_extent = max_t(u64,
							 info->max_extent,
							 root->sectorsize);
					printk("btrfs: max_extent at %Lu\n",
					       info->max_extent);
				}
			}
			break;
		case Opt_alloc_start:
			if (info) {
				char *num = match_strdup(&args[0]);
				if (num) {
					info->alloc_start =
						btrfs_parse_size(num);
					kfree(num);
					printk("btrfs: allocations start at "
					       "%Lu\n", info->alloc_start);
				}
			}
			break;
		default:
			break;
		}
	}
	kfree(options);
	return 1;
}

static int btrfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root_dentry;
	struct btrfs_super_block *disk_super;
	struct btrfs_root *tree_root;
	struct btrfs_inode *bi;
	int err;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_xattr = btrfs_xattr_handlers;
	sb->s_time_gran = 1;

	tree_root = open_ctree(sb);

	if (!tree_root || IS_ERR(tree_root)) {
		printk("btrfs: open_ctree failed\n");
		return -EIO;
	}
	sb->s_fs_info = tree_root;
	disk_super = &tree_root->fs_info->super_copy;
	inode = btrfs_iget_locked(sb, btrfs_super_root_dir(disk_super),
				  tree_root);
	bi = BTRFS_I(inode);
	bi->location.objectid = inode->i_ino;
	bi->location.offset = 0;
	bi->root = tree_root;

	btrfs_set_key_type(&bi->location, BTRFS_INODE_ITEM_KEY);

	if (!inode) {
		err = -ENOMEM;
		goto fail_close;
	}
	if (inode->i_state & I_NEW) {
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);
	}

	root_dentry = d_alloc_root(inode);
	if (!root_dentry) {
		iput(inode);
		err = -ENOMEM;
		goto fail_close;
	}

	parse_options((char *)data, tree_root, NULL);

	/* this does the super kobj at the same time */
	err = btrfs_sysfs_add_super(tree_root->fs_info);
	if (err)
		goto fail_close;

	sb->s_root = root_dentry;
	btrfs_transaction_queue_work(tree_root, HZ * 30);
	return 0;

fail_close:
	close_ctree(tree_root);
	return err;
}

static int btrfs_sync_fs(struct super_block *sb, int wait)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	int ret;
	root = btrfs_sb(sb);

	sb->s_dirt = 0;
	if (!wait) {
		filemap_flush(root->fs_info->btree_inode->i_mapping);
		return 0;
	}
	btrfs_clean_old_snapshots(root);
	mutex_lock(&root->fs_info->fs_mutex);
	btrfs_defrag_dirty_roots(root->fs_info);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	sb->s_dirt = 0;
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

static void btrfs_write_super(struct super_block *sb)
{
	sb->s_dirt = 0;
}

/*
 * This is almost a copy of get_sb_bdev in fs/super.c.
 * We need the local copy to allow direct mounting of
 * subvolumes, but this could be easily integrated back
 * into the generic version.  --hch
 */

/* start copy & paste */
static int set_bdev_super(struct super_block *s, void *data)
{
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;
	return 0;
}

static int test_bdev_super(struct super_block *s, void *data)
{
	return (void *)s->s_bdev == data;
}

int btrfs_get_sb_bdev(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data,
	int (*fill_super)(struct super_block *, void *, int),
	struct vfsmount *mnt, const char *subvol)
{
	struct block_device *bdev = NULL;
	struct super_block *s;
	struct dentry *root;
	int error = 0;

	bdev = open_bdev_excl(dev_name, flags, fs_type);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	down(&bdev->bd_mount_sem);
	s = sget(fs_type, test_bdev_super, set_bdev_super, bdev);
	up(&bdev->bd_mount_sem);
	if (IS_ERR(s))
		goto error_s;

	if (s->s_root) {
		if ((flags ^ s->s_flags) & MS_RDONLY) {
			up_write(&s->s_umount);
			deactivate_super(s);
			error = -EBUSY;
			goto error_bdev;
		}

		close_bdev_excl(bdev);
	} else {
		char b[BDEVNAME_SIZE];

		s->s_flags = flags;
		strlcpy(s->s_id, bdevname(bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(bdev));
		error = fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			up_write(&s->s_umount);
			deactivate_super(s);
			goto error;
		}

		s->s_flags |= MS_ACTIVE;
	}

	if (subvol) {
		root = lookup_one_len(subvol, s->s_root, strlen(subvol));
		if (IS_ERR(root)) {
			up_write(&s->s_umount);
			deactivate_super(s);
			error = PTR_ERR(root);
			goto error;
		}
		if (!root->d_inode) {
			dput(root);
			up_write(&s->s_umount);
			deactivate_super(s);
			error = -ENXIO;
			goto error;
		}
	} else {
		root = dget(s->s_root);
	}

	mnt->mnt_sb = s;
	mnt->mnt_root = root;
	return 0;

error_s:
	error = PTR_ERR(s);
error_bdev:
	close_bdev_excl(bdev);
error:
	return error;
}
/* end copy & paste */

static int btrfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	int ret;
	char *subvol_name = NULL;

	parse_options((char *)data, NULL, &subvol_name);
	ret = btrfs_get_sb_bdev(fs_type, flags, dev_name, data,
			btrfs_fill_super, mnt,
			subvol_name ? subvol_name : "default");
	if (subvol_name)
		kfree(subvol_name);
	return ret;
}

static int btrfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct btrfs_root *root = btrfs_sb(dentry->d_sb);
	struct btrfs_super_block *disk_super = &root->fs_info->super_copy;
	int bits = dentry->d_sb->s_blocksize_bits;

	buf->f_namelen = BTRFS_NAME_LEN;
	buf->f_blocks = btrfs_super_total_bytes(disk_super) >> bits;
	buf->f_bfree = buf->f_blocks -
		(btrfs_super_bytes_used(disk_super) >> bits);
	buf->f_bavail = buf->f_bfree;
	buf->f_bsize = dentry->d_sb->s_blocksize;
	buf->f_type = BTRFS_SUPER_MAGIC;
	return 0;
}

static struct file_system_type btrfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.get_sb		= btrfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct super_operations btrfs_super_ops = {
	.delete_inode	= btrfs_delete_inode,
	.put_inode	= btrfs_put_inode,
	.put_super	= btrfs_put_super,
	.read_inode	= btrfs_read_locked_inode,
	.write_super	= btrfs_write_super,
	.sync_fs	= btrfs_sync_fs,
	.write_inode	= btrfs_write_inode,
	.dirty_inode	= btrfs_dirty_inode,
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_destroy_inode,
	.statfs		= btrfs_statfs,
};

static int __init init_btrfs_fs(void)
{
	int err;

	err = btrfs_init_sysfs();
	if (err)
		return err;

	btrfs_init_transaction_sys();
	err = btrfs_init_cachep();
	if (err)
		goto free_transaction_sys;
	err = extent_map_init();
	if (err)
		goto free_cachep;

	err = register_filesystem(&btrfs_fs_type);
	if (err)
		goto free_extent_map;
	return 0;

free_extent_map:
	extent_map_exit();
free_cachep:
	btrfs_destroy_cachep();
free_transaction_sys:
	btrfs_exit_transaction_sys();
	btrfs_exit_sysfs();
	return err;
}

static void __exit exit_btrfs_fs(void)
{
	btrfs_exit_transaction_sys();
	btrfs_destroy_cachep();
	extent_map_exit();
	unregister_filesystem(&btrfs_fs_type);
	btrfs_exit_sysfs();
}

module_init(init_btrfs_fs)
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
