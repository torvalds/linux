/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * super.c
 */

/*
 * This file implements code to read the superblock, read and initialise
 * in-memory structures at mount time, and all the VFS glue code to register
 * the filesystem.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/magic.h>
#include <linux/xattr.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"
#include "decompressor.h"
#include "xattr.h"

static struct file_system_type squashfs_fs_type;
static const struct super_operations squashfs_super_ops;

static const struct squashfs_decompressor *supported_squashfs_filesystem(short
	major, short minor, short id)
{
	const struct squashfs_decompressor *decompressor;

	if (major < SQUASHFS_MAJOR) {
		ERROR("Major/Minor mismatch, older Squashfs %d.%d "
			"filesystems are unsupported\n", major, minor);
		return NULL;
	} else if (major > SQUASHFS_MAJOR || minor > SQUASHFS_MINOR) {
		ERROR("Major/Minor mismatch, trying to mount newer "
			"%d.%d filesystem\n", major, minor);
		ERROR("Please update your kernel\n");
		return NULL;
	}

	decompressor = squashfs_lookup_decompressor(id);
	if (!decompressor->supported) {
		ERROR("Filesystem uses \"%s\" compression. This is not "
			"supported\n", decompressor->name);
		return NULL;
	}

	return decompressor;
}


static int squashfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct squashfs_sb_info *msblk;
	struct squashfs_super_block *sblk = NULL;
	char b[BDEVNAME_SIZE];
	struct inode *root;
	long long root_inode;
	unsigned short flags;
	unsigned int fragments;
	u64 lookup_table_start, xattr_id_table_start;
	int err;

	TRACE("Entered squashfs_fill_superblock\n");

	sb->s_fs_info = kzalloc(sizeof(*msblk), GFP_KERNEL);
	if (sb->s_fs_info == NULL) {
		ERROR("Failed to allocate squashfs_sb_info\n");
		return -ENOMEM;
	}
	msblk = sb->s_fs_info;

	sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
	if (sblk == NULL) {
		ERROR("Failed to allocate squashfs_super_block\n");
		goto failure;
	}

	msblk->devblksize = sb_min_blocksize(sb, BLOCK_SIZE);
	msblk->devblksize_log2 = ffz(~msblk->devblksize);

	mutex_init(&msblk->read_data_mutex);
	mutex_init(&msblk->meta_index_mutex);

	/*
	 * msblk->bytes_used is checked in squashfs_read_table to ensure reads
	 * are not beyond filesystem end.  But as we're using
	 * squashfs_read_table here to read the superblock (including the value
	 * of bytes_used) we need to set it to an initial sensible dummy value
	 */
	msblk->bytes_used = sizeof(*sblk);
	err = squashfs_read_table(sb, sblk, SQUASHFS_START, sizeof(*sblk));

	if (err < 0) {
		ERROR("unable to read squashfs_super_block\n");
		goto failed_mount;
	}

	err = -EINVAL;

	/* Check it is a SQUASHFS superblock */
	sb->s_magic = le32_to_cpu(sblk->s_magic);
	if (sb->s_magic != SQUASHFS_MAGIC) {
		if (!silent)
			ERROR("Can't find a SQUASHFS superblock on %s\n",
						bdevname(sb->s_bdev, b));
		goto failed_mount;
	}

	/* Check the MAJOR & MINOR versions and lookup compression type */
	msblk->decompressor = supported_squashfs_filesystem(
			le16_to_cpu(sblk->s_major),
			le16_to_cpu(sblk->s_minor),
			le16_to_cpu(sblk->compression));
	if (msblk->decompressor == NULL)
		goto failed_mount;

	/* Check the filesystem does not extend beyond the end of the
	   block device */
	msblk->bytes_used = le64_to_cpu(sblk->bytes_used);
	if (msblk->bytes_used < 0 || msblk->bytes_used >
			i_size_read(sb->s_bdev->bd_inode))
		goto failed_mount;

	/* Check block size for sanity */
	msblk->block_size = le32_to_cpu(sblk->block_size);
	if (msblk->block_size > SQUASHFS_FILE_MAX_SIZE)
		goto failed_mount;

	/*
	 * Check the system page size is not larger than the filesystem
	 * block size (by default 128K).  This is currently not supported.
	 */
	if (PAGE_CACHE_SIZE > msblk->block_size) {
		ERROR("Page size > filesystem block size (%d).  This is "
			"currently not supported!\n", msblk->block_size);
		goto failed_mount;
	}

	msblk->block_log = le16_to_cpu(sblk->block_log);
	if (msblk->block_log > SQUASHFS_FILE_MAX_LOG)
		goto failed_mount;

	/* Check the root inode for sanity */
	root_inode = le64_to_cpu(sblk->root_inode);
	if (SQUASHFS_INODE_OFFSET(root_inode) > SQUASHFS_METADATA_SIZE)
		goto failed_mount;

	msblk->inode_table = le64_to_cpu(sblk->inode_table_start);
	msblk->directory_table = le64_to_cpu(sblk->directory_table_start);
	msblk->inodes = le32_to_cpu(sblk->inodes);
	flags = le16_to_cpu(sblk->flags);

	TRACE("Found valid superblock on %s\n", bdevname(sb->s_bdev, b));
	TRACE("Inodes are %scompressed\n", SQUASHFS_UNCOMPRESSED_INODES(flags)
				? "un" : "");
	TRACE("Data is %scompressed\n", SQUASHFS_UNCOMPRESSED_DATA(flags)
				? "un" : "");
	TRACE("Filesystem size %lld bytes\n", msblk->bytes_used);
	TRACE("Block size %d\n", msblk->block_size);
	TRACE("Number of inodes %d\n", msblk->inodes);
	TRACE("Number of fragments %d\n", le32_to_cpu(sblk->fragments));
	TRACE("Number of ids %d\n", le16_to_cpu(sblk->no_ids));
	TRACE("sblk->inode_table_start %llx\n", msblk->inode_table);
	TRACE("sblk->directory_table_start %llx\n", msblk->directory_table);
	TRACE("sblk->fragment_table_start %llx\n",
		(u64) le64_to_cpu(sblk->fragment_table_start));
	TRACE("sblk->id_table_start %llx\n",
		(u64) le64_to_cpu(sblk->id_table_start));

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_flags |= MS_RDONLY;
	sb->s_op = &squashfs_super_ops;

	err = -ENOMEM;

	msblk->block_cache = squashfs_cache_init("metadata",
			SQUASHFS_CACHED_BLKS, SQUASHFS_METADATA_SIZE);
	if (msblk->block_cache == NULL)
		goto failed_mount;

	/* Allocate read_page block */
	msblk->read_page = squashfs_cache_init("data", 1, msblk->block_size);
	if (msblk->read_page == NULL) {
		ERROR("Failed to allocate read_page block\n");
		goto failed_mount;
	}

	msblk->stream = squashfs_decompressor_init(sb, flags);
	if (IS_ERR(msblk->stream)) {
		err = PTR_ERR(msblk->stream);
		msblk->stream = NULL;
		goto failed_mount;
	}

	/* Allocate and read id index table */
	msblk->id_table = squashfs_read_id_index_table(sb,
		le64_to_cpu(sblk->id_table_start), le16_to_cpu(sblk->no_ids));
	if (IS_ERR(msblk->id_table)) {
		err = PTR_ERR(msblk->id_table);
		msblk->id_table = NULL;
		goto failed_mount;
	}

	fragments = le32_to_cpu(sblk->fragments);
	if (fragments == 0)
		goto allocate_lookup_table;

	msblk->fragment_cache = squashfs_cache_init("fragment",
		SQUASHFS_CACHED_FRAGMENTS, msblk->block_size);
	if (msblk->fragment_cache == NULL) {
		err = -ENOMEM;
		goto failed_mount;
	}

	/* Allocate and read fragment index table */
	msblk->fragment_index = squashfs_read_fragment_index_table(sb,
		le64_to_cpu(sblk->fragment_table_start), fragments);
	if (IS_ERR(msblk->fragment_index)) {
		err = PTR_ERR(msblk->fragment_index);
		msblk->fragment_index = NULL;
		goto failed_mount;
	}

allocate_lookup_table:
	lookup_table_start = le64_to_cpu(sblk->lookup_table_start);
	if (lookup_table_start == SQUASHFS_INVALID_BLK)
		goto allocate_xattr_table;

	/* Allocate and read inode lookup table */
	msblk->inode_lookup_table = squashfs_read_inode_lookup_table(sb,
		lookup_table_start, msblk->inodes);
	if (IS_ERR(msblk->inode_lookup_table)) {
		err = PTR_ERR(msblk->inode_lookup_table);
		msblk->inode_lookup_table = NULL;
		goto failed_mount;
	}

	sb->s_export_op = &squashfs_export_ops;

allocate_xattr_table:
	sb->s_xattr = squashfs_xattr_handlers;
	xattr_id_table_start = le64_to_cpu(sblk->xattr_id_table_start);
	if (xattr_id_table_start == SQUASHFS_INVALID_BLK)
		goto allocate_root;

	/* Allocate and read xattr id lookup table */
	msblk->xattr_id_table = squashfs_read_xattr_id_table(sb,
		xattr_id_table_start, &msblk->xattr_table, &msblk->xattr_ids);
	if (IS_ERR(msblk->xattr_id_table)) {
		err = PTR_ERR(msblk->xattr_id_table);
		msblk->xattr_id_table = NULL;
		if (err != -ENOTSUPP)
			goto failed_mount;
	}
allocate_root:
	root = new_inode(sb);
	if (!root) {
		err = -ENOMEM;
		goto failed_mount;
	}

	err = squashfs_read_inode(root, root_inode);
	if (err) {
		make_bad_inode(root);
		iput(root);
		goto failed_mount;
	}
	insert_inode_hash(root);

	sb->s_root = d_alloc_root(root);
	if (sb->s_root == NULL) {
		ERROR("Root inode create failed\n");
		err = -ENOMEM;
		iput(root);
		goto failed_mount;
	}

	TRACE("Leaving squashfs_fill_super\n");
	kfree(sblk);
	return 0;

failed_mount:
	squashfs_cache_delete(msblk->block_cache);
	squashfs_cache_delete(msblk->fragment_cache);
	squashfs_cache_delete(msblk->read_page);
	squashfs_decompressor_free(msblk, msblk->stream);
	kfree(msblk->inode_lookup_table);
	kfree(msblk->fragment_index);
	kfree(msblk->id_table);
	kfree(msblk->xattr_id_table);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	kfree(sblk);
	return err;

failure:
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	return -ENOMEM;
}


static int squashfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct squashfs_sb_info *msblk = dentry->d_sb->s_fs_info;
	u64 id = huge_encode_dev(dentry->d_sb->s_bdev->bd_dev);

	TRACE("Entered squashfs_statfs\n");

	buf->f_type = SQUASHFS_MAGIC;
	buf->f_bsize = msblk->block_size;
	buf->f_blocks = ((msblk->bytes_used - 1) >> msblk->block_log) + 1;
	buf->f_bfree = buf->f_bavail = 0;
	buf->f_files = msblk->inodes;
	buf->f_ffree = 0;
	buf->f_namelen = SQUASHFS_NAME_LEN;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}


static int squashfs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_RDONLY;
	return 0;
}


static void squashfs_put_super(struct super_block *sb)
{
	if (sb->s_fs_info) {
		struct squashfs_sb_info *sbi = sb->s_fs_info;
		squashfs_cache_delete(sbi->block_cache);
		squashfs_cache_delete(sbi->fragment_cache);
		squashfs_cache_delete(sbi->read_page);
		squashfs_decompressor_free(sbi, sbi->stream);
		kfree(sbi->id_table);
		kfree(sbi->fragment_index);
		kfree(sbi->meta_index);
		kfree(sbi->inode_lookup_table);
		kfree(sbi->xattr_id_table);
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
	}
}


static struct dentry *squashfs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, squashfs_fill_super);
}


static struct kmem_cache *squashfs_inode_cachep;


static void init_once(void *foo)
{
	struct squashfs_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}


static int __init init_inodecache(void)
{
	squashfs_inode_cachep = kmem_cache_create("squashfs_inode_cache",
		sizeof(struct squashfs_inode_info), 0,
		SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT, init_once);

	return squashfs_inode_cachep ? 0 : -ENOMEM;
}


static void destroy_inodecache(void)
{
	kmem_cache_destroy(squashfs_inode_cachep);
}


static int __init init_squashfs_fs(void)
{
	int err = init_inodecache();

	if (err)
		return err;

	err = register_filesystem(&squashfs_fs_type);
	if (err) {
		destroy_inodecache();
		return err;
	}

	printk(KERN_INFO "squashfs: version 4.0 (2009/01/31) "
		"Phillip Lougher\n");

	return 0;
}


static void __exit exit_squashfs_fs(void)
{
	unregister_filesystem(&squashfs_fs_type);
	destroy_inodecache();
}


static struct inode *squashfs_alloc_inode(struct super_block *sb)
{
	struct squashfs_inode_info *ei =
		kmem_cache_alloc(squashfs_inode_cachep, GFP_KERNEL);

	return ei ? &ei->vfs_inode : NULL;
}


static void squashfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	INIT_LIST_HEAD(&inode->i_dentry);
	kmem_cache_free(squashfs_inode_cachep, squashfs_i(inode));
}

static void squashfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, squashfs_i_callback);
}


static struct file_system_type squashfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "squashfs",
	.mount = squashfs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
};

static const struct super_operations squashfs_super_ops = {
	.alloc_inode = squashfs_alloc_inode,
	.destroy_inode = squashfs_destroy_inode,
	.statfs = squashfs_statfs,
	.put_super = squashfs_put_super,
	.remount_fs = squashfs_remount
};

module_init(init_squashfs_fs);
module_exit(exit_squashfs_fs);
MODULE_DESCRIPTION("squashfs 4.0, a compressed read-only filesystem");
MODULE_AUTHOR("Phillip Lougher <phillip@lougher.demon.co.uk>");
MODULE_LICENSE("GPL");
