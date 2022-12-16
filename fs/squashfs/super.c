// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * super.c
 */

/*
 * This file implements code to read the superblock, read and initialise
 * in-memory structures at mount time, and all the VFS glue code to register
 * the filesystem.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
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

enum Opt_errors {
	Opt_errors_continue,
	Opt_errors_panic,
};

enum squashfs_param {
	Opt_errors,
	Opt_threads,
};

struct squashfs_mount_opts {
	enum Opt_errors errors;
	const struct squashfs_decompressor_thread_ops *thread_ops;
	int thread_num;
};

static const struct constant_table squashfs_param_errors[] = {
	{"continue",   Opt_errors_continue },
	{"panic",      Opt_errors_panic },
	{}
};

static const struct fs_parameter_spec squashfs_fs_parameters[] = {
	fsparam_enum("errors", Opt_errors, squashfs_param_errors),
	fsparam_string("threads", Opt_threads),
	{}
};


static int squashfs_parse_param_threads_str(const char *str, struct squashfs_mount_opts *opts)
{
#ifdef CONFIG_SQUASHFS_CHOICE_DECOMP_BY_MOUNT
	if (strcmp(str, "single") == 0) {
		opts->thread_ops = &squashfs_decompressor_single;
		return 0;
	}
	if (strcmp(str, "multi") == 0) {
		opts->thread_ops = &squashfs_decompressor_multi;
		return 0;
	}
	if (strcmp(str, "percpu") == 0) {
		opts->thread_ops = &squashfs_decompressor_percpu;
		return 0;
	}
#endif
	return -EINVAL;
}

static int squashfs_parse_param_threads_num(const char *str, struct squashfs_mount_opts *opts)
{
#ifdef CONFIG_SQUASHFS_MOUNT_DECOMP_THREADS
	int ret;
	unsigned long num;

	ret = kstrtoul(str, 0, &num);
	if (ret != 0)
		return -EINVAL;
	if (num > 1) {
		opts->thread_ops = &squashfs_decompressor_multi;
		if (num > opts->thread_ops->max_decompressors())
			return -EINVAL;
		opts->thread_num = (int)num;
		return 0;
	}
#ifdef CONFIG_SQUASHFS_DECOMP_SINGLE
	if (num == 1) {
		opts->thread_ops = &squashfs_decompressor_single;
		opts->thread_num = 1;
		return 0;
	}
#endif
#endif /* !CONFIG_SQUASHFS_MOUNT_DECOMP_THREADS */
	return -EINVAL;
}

static int squashfs_parse_param_threads(const char *str, struct squashfs_mount_opts *opts)
{
	int ret = squashfs_parse_param_threads_str(str, opts);

	if (ret == 0)
		return ret;
	return squashfs_parse_param_threads_num(str, opts);
}

static int squashfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct squashfs_mount_opts *opts = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, squashfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_errors:
		opts->errors = result.uint_32;
		break;
	case Opt_threads:
		if (squashfs_parse_param_threads(param->string, opts) != 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct squashfs_decompressor *supported_squashfs_filesystem(
	struct fs_context *fc,
	short major, short minor, short id)
{
	const struct squashfs_decompressor *decompressor;

	if (major < SQUASHFS_MAJOR) {
		errorf(fc, "Major/Minor mismatch, older Squashfs %d.%d "
		       "filesystems are unsupported", major, minor);
		return NULL;
	} else if (major > SQUASHFS_MAJOR || minor > SQUASHFS_MINOR) {
		errorf(fc, "Major/Minor mismatch, trying to mount newer "
		       "%d.%d filesystem", major, minor);
		errorf(fc, "Please update your kernel");
		return NULL;
	}

	decompressor = squashfs_lookup_decompressor(id);
	if (!decompressor->supported) {
		errorf(fc, "Filesystem uses \"%s\" compression. This is not supported",
		       decompressor->name);
		return NULL;
	}

	return decompressor;
}


static int squashfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct squashfs_mount_opts *opts = fc->fs_private;
	struct squashfs_sb_info *msblk;
	struct squashfs_super_block *sblk = NULL;
	struct inode *root;
	long long root_inode;
	unsigned short flags;
	unsigned int fragments;
	u64 lookup_table_start, xattr_id_table_start, next_table;
	int err;

	TRACE("Entered squashfs_fill_superblock\n");

	sb->s_fs_info = kzalloc(sizeof(*msblk), GFP_KERNEL);
	if (sb->s_fs_info == NULL) {
		ERROR("Failed to allocate squashfs_sb_info\n");
		return -ENOMEM;
	}
	msblk = sb->s_fs_info;
	msblk->thread_ops = opts->thread_ops;

	msblk->panic_on_errors = (opts->errors == Opt_errors_panic);

	msblk->devblksize = sb_min_blocksize(sb, SQUASHFS_DEVBLK_SIZE);
	msblk->devblksize_log2 = ffz(~msblk->devblksize);

	mutex_init(&msblk->meta_index_mutex);

	/*
	 * msblk->bytes_used is checked in squashfs_read_table to ensure reads
	 * are not beyond filesystem end.  But as we're using
	 * squashfs_read_table here to read the superblock (including the value
	 * of bytes_used) we need to set it to an initial sensible dummy value
	 */
	msblk->bytes_used = sizeof(*sblk);
	sblk = squashfs_read_table(sb, SQUASHFS_START, sizeof(*sblk));

	if (IS_ERR(sblk)) {
		errorf(fc, "unable to read squashfs_super_block");
		err = PTR_ERR(sblk);
		sblk = NULL;
		goto failed_mount;
	}

	err = -EINVAL;

	/* Check it is a SQUASHFS superblock */
	sb->s_magic = le32_to_cpu(sblk->s_magic);
	if (sb->s_magic != SQUASHFS_MAGIC) {
		if (!(fc->sb_flags & SB_SILENT))
			errorf(fc, "Can't find a SQUASHFS superblock on %pg",
			       sb->s_bdev);
		goto failed_mount;
	}

	if (opts->thread_num == 0) {
		msblk->max_thread_num = msblk->thread_ops->max_decompressors();
	} else {
		msblk->max_thread_num = opts->thread_num;
	}

	/* Check the MAJOR & MINOR versions and lookup compression type */
	msblk->decompressor = supported_squashfs_filesystem(
			fc,
			le16_to_cpu(sblk->s_major),
			le16_to_cpu(sblk->s_minor),
			le16_to_cpu(sblk->compression));
	if (msblk->decompressor == NULL)
		goto failed_mount;

	/* Check the filesystem does not extend beyond the end of the
	   block device */
	msblk->bytes_used = le64_to_cpu(sblk->bytes_used);
	if (msblk->bytes_used < 0 ||
	    msblk->bytes_used > bdev_nr_bytes(sb->s_bdev))
		goto failed_mount;

	/* Check block size for sanity */
	msblk->block_size = le32_to_cpu(sblk->block_size);
	if (msblk->block_size > SQUASHFS_FILE_MAX_SIZE)
		goto insanity;

	/*
	 * Check the system page size is not larger than the filesystem
	 * block size (by default 128K).  This is currently not supported.
	 */
	if (PAGE_SIZE > msblk->block_size) {
		errorf(fc, "Page size > filesystem block size (%d).  This is "
		       "currently not supported!", msblk->block_size);
		goto failed_mount;
	}

	/* Check block log for sanity */
	msblk->block_log = le16_to_cpu(sblk->block_log);
	if (msblk->block_log > SQUASHFS_FILE_MAX_LOG)
		goto failed_mount;

	/* Check that block_size and block_log match */
	if (msblk->block_size != (1 << msblk->block_log))
		goto insanity;

	/* Check the root inode for sanity */
	root_inode = le64_to_cpu(sblk->root_inode);
	if (SQUASHFS_INODE_OFFSET(root_inode) > SQUASHFS_METADATA_SIZE)
		goto insanity;

	msblk->inode_table = le64_to_cpu(sblk->inode_table_start);
	msblk->directory_table = le64_to_cpu(sblk->directory_table_start);
	msblk->inodes = le32_to_cpu(sblk->inodes);
	msblk->fragments = le32_to_cpu(sblk->fragments);
	msblk->ids = le16_to_cpu(sblk->no_ids);
	flags = le16_to_cpu(sblk->flags);

	TRACE("Found valid superblock on %pg\n", sb->s_bdev);
	TRACE("Inodes are %scompressed\n", SQUASHFS_UNCOMPRESSED_INODES(flags)
				? "un" : "");
	TRACE("Data is %scompressed\n", SQUASHFS_UNCOMPRESSED_DATA(flags)
				? "un" : "");
	TRACE("Filesystem size %lld bytes\n", msblk->bytes_used);
	TRACE("Block size %d\n", msblk->block_size);
	TRACE("Number of inodes %d\n", msblk->inodes);
	TRACE("Number of fragments %d\n", msblk->fragments);
	TRACE("Number of ids %d\n", msblk->ids);
	TRACE("sblk->inode_table_start %llx\n", msblk->inode_table);
	TRACE("sblk->directory_table_start %llx\n", msblk->directory_table);
	TRACE("sblk->fragment_table_start %llx\n",
		(u64) le64_to_cpu(sblk->fragment_table_start));
	TRACE("sblk->id_table_start %llx\n",
		(u64) le64_to_cpu(sblk->id_table_start));

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_min = 0;
	sb->s_time_max = U32_MAX;
	sb->s_flags |= SB_RDONLY;
	sb->s_op = &squashfs_super_ops;

	err = -ENOMEM;

	msblk->block_cache = squashfs_cache_init("metadata",
			SQUASHFS_CACHED_BLKS, SQUASHFS_METADATA_SIZE);
	if (msblk->block_cache == NULL)
		goto failed_mount;

	/* Allocate read_page block */
	msblk->read_page = squashfs_cache_init("data",
		msblk->max_thread_num, msblk->block_size);
	if (msblk->read_page == NULL) {
		errorf(fc, "Failed to allocate read_page block");
		goto failed_mount;
	}

	msblk->stream = squashfs_decompressor_setup(sb, flags);
	if (IS_ERR(msblk->stream)) {
		err = PTR_ERR(msblk->stream);
		msblk->stream = NULL;
		goto insanity;
	}

	/* Handle xattrs */
	sb->s_xattr = squashfs_xattr_handlers;
	xattr_id_table_start = le64_to_cpu(sblk->xattr_id_table_start);
	if (xattr_id_table_start == SQUASHFS_INVALID_BLK) {
		next_table = msblk->bytes_used;
		goto allocate_id_index_table;
	}

	/* Allocate and read xattr id lookup table */
	msblk->xattr_id_table = squashfs_read_xattr_id_table(sb,
		xattr_id_table_start, &msblk->xattr_table, &msblk->xattr_ids);
	if (IS_ERR(msblk->xattr_id_table)) {
		errorf(fc, "unable to read xattr id index table");
		err = PTR_ERR(msblk->xattr_id_table);
		msblk->xattr_id_table = NULL;
		if (err != -ENOTSUPP)
			goto failed_mount;
	}
	next_table = msblk->xattr_table;

allocate_id_index_table:
	/* Allocate and read id index table */
	msblk->id_table = squashfs_read_id_index_table(sb,
		le64_to_cpu(sblk->id_table_start), next_table, msblk->ids);
	if (IS_ERR(msblk->id_table)) {
		errorf(fc, "unable to read id index table");
		err = PTR_ERR(msblk->id_table);
		msblk->id_table = NULL;
		goto failed_mount;
	}
	next_table = le64_to_cpu(msblk->id_table[0]);

	/* Handle inode lookup table */
	lookup_table_start = le64_to_cpu(sblk->lookup_table_start);
	if (lookup_table_start == SQUASHFS_INVALID_BLK)
		goto handle_fragments;

	/* Allocate and read inode lookup table */
	msblk->inode_lookup_table = squashfs_read_inode_lookup_table(sb,
		lookup_table_start, next_table, msblk->inodes);
	if (IS_ERR(msblk->inode_lookup_table)) {
		errorf(fc, "unable to read inode lookup table");
		err = PTR_ERR(msblk->inode_lookup_table);
		msblk->inode_lookup_table = NULL;
		goto failed_mount;
	}
	next_table = le64_to_cpu(msblk->inode_lookup_table[0]);

	sb->s_export_op = &squashfs_export_ops;

handle_fragments:
	fragments = msblk->fragments;
	if (fragments == 0)
		goto check_directory_table;

	msblk->fragment_cache = squashfs_cache_init("fragment",
		SQUASHFS_CACHED_FRAGMENTS, msblk->block_size);
	if (msblk->fragment_cache == NULL) {
		err = -ENOMEM;
		goto failed_mount;
	}

	/* Allocate and read fragment index table */
	msblk->fragment_index = squashfs_read_fragment_index_table(sb,
		le64_to_cpu(sblk->fragment_table_start), next_table, fragments);
	if (IS_ERR(msblk->fragment_index)) {
		errorf(fc, "unable to read fragment index table");
		err = PTR_ERR(msblk->fragment_index);
		msblk->fragment_index = NULL;
		goto failed_mount;
	}
	next_table = le64_to_cpu(msblk->fragment_index[0]);

check_directory_table:
	/* Sanity check directory_table */
	if (msblk->directory_table > next_table) {
		err = -EINVAL;
		goto insanity;
	}

	/* Sanity check inode_table */
	if (msblk->inode_table >= msblk->directory_table) {
		err = -EINVAL;
		goto insanity;
	}

	/* allocate root */
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

	sb->s_root = d_make_root(root);
	if (sb->s_root == NULL) {
		ERROR("Root inode create failed\n");
		err = -ENOMEM;
		goto failed_mount;
	}

	TRACE("Leaving squashfs_fill_super\n");
	kfree(sblk);
	return 0;

insanity:
	errorf(fc, "squashfs image failed sanity check");
failed_mount:
	squashfs_cache_delete(msblk->block_cache);
	squashfs_cache_delete(msblk->fragment_cache);
	squashfs_cache_delete(msblk->read_page);
	msblk->thread_ops->destroy(msblk);
	kfree(msblk->inode_lookup_table);
	kfree(msblk->fragment_index);
	kfree(msblk->id_table);
	kfree(msblk->xattr_id_table);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	kfree(sblk);
	return err;
}

static int squashfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, squashfs_fill_super);
}

static int squashfs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	struct squashfs_mount_opts *opts = fc->fs_private;

	sync_filesystem(fc->root->d_sb);
	fc->sb_flags |= SB_RDONLY;

	msblk->panic_on_errors = (opts->errors == Opt_errors_panic);

	return 0;
}

static void squashfs_free_fs_context(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations squashfs_context_ops = {
	.get_tree	= squashfs_get_tree,
	.free		= squashfs_free_fs_context,
	.parse_param	= squashfs_parse_param,
	.reconfigure	= squashfs_reconfigure,
};

static int squashfs_show_options(struct seq_file *s, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct squashfs_sb_info *msblk = sb->s_fs_info;

	if (msblk->panic_on_errors)
		seq_puts(s, ",errors=panic");
	else
		seq_puts(s, ",errors=continue");

#ifdef CONFIG_SQUASHFS_CHOICE_DECOMP_BY_MOUNT
	if (msblk->thread_ops == &squashfs_decompressor_single) {
		seq_puts(s, ",threads=single");
		return 0;
	}
	if (msblk->thread_ops == &squashfs_decompressor_percpu) {
		seq_puts(s, ",threads=percpu");
		return 0;
	}
#endif
#ifdef CONFIG_SQUASHFS_MOUNT_DECOMP_THREADS
	seq_printf(s, ",threads=%d", msblk->max_thread_num);
#endif
	return 0;
}

static int squashfs_init_fs_context(struct fs_context *fc)
{
	struct squashfs_mount_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

#ifdef CONFIG_SQUASHFS_DECOMP_SINGLE
	opts->thread_ops = &squashfs_decompressor_single;
#elif defined(CONFIG_SQUASHFS_DECOMP_MULTI)
	opts->thread_ops = &squashfs_decompressor_multi;
#elif defined(CONFIG_SQUASHFS_DECOMP_MULTI_PERCPU)
	opts->thread_ops = &squashfs_decompressor_percpu;
#else
#error "fail: unknown squashfs decompression thread mode?"
#endif
	opts->thread_num = 0;
	fc->fs_private = opts;
	fc->ops = &squashfs_context_ops;
	return 0;
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
	buf->f_fsid = u64_to_fsid(id);

	return 0;
}


static void squashfs_put_super(struct super_block *sb)
{
	if (sb->s_fs_info) {
		struct squashfs_sb_info *sbi = sb->s_fs_info;
		squashfs_cache_delete(sbi->block_cache);
		squashfs_cache_delete(sbi->fragment_cache);
		squashfs_cache_delete(sbi->read_page);
		sbi->thread_ops->destroy(sbi);
		kfree(sbi->id_table);
		kfree(sbi->fragment_index);
		kfree(sbi->meta_index);
		kfree(sbi->inode_lookup_table);
		kfree(sbi->xattr_id_table);
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
	}
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
		SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|SLAB_ACCOUNT,
		init_once);

	return squashfs_inode_cachep ? 0 : -ENOMEM;
}


static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
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

	pr_info("version 4.0 (2009/01/31) Phillip Lougher\n");

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
		alloc_inode_sb(sb, squashfs_inode_cachep, GFP_KERNEL);

	return ei ? &ei->vfs_inode : NULL;
}


static void squashfs_free_inode(struct inode *inode)
{
	kmem_cache_free(squashfs_inode_cachep, squashfs_i(inode));
}

static struct file_system_type squashfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "squashfs",
	.init_fs_context = squashfs_init_fs_context,
	.parameters = squashfs_fs_parameters,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("squashfs");

static const struct super_operations squashfs_super_ops = {
	.alloc_inode = squashfs_alloc_inode,
	.free_inode = squashfs_free_inode,
	.statfs = squashfs_statfs,
	.put_super = squashfs_put_super,
	.show_options = squashfs_show_options,
};

module_init(init_squashfs_fs);
module_exit(exit_squashfs_fs);
MODULE_DESCRIPTION("squashfs 4.0, a compressed read-only filesystem");
MODULE_AUTHOR("Phillip Lougher <phillip@squashfs.org.uk>");
MODULE_LICENSE("GPL");
