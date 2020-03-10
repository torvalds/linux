// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */

#include <linux/blkdev.h>
#include <linux/cred.h>
#include <linux/eventpoll.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>
#include <linux/namei.h>
#include <linux/parser.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/xattr.h>

#include <uapi/linux/incrementalfs.h>

#include "vfs.h"
#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "internal.h"

#define INCFS_PENDING_READS_INODE 2
#define INCFS_LOG_INODE 3
#define INCFS_START_INO_RANGE 10
#define READ_FILE_MODE 0444
#define READ_EXEC_FILE_MODE 0555
#define READ_WRITE_FILE_MODE 0666

static int incfs_remount_fs(struct super_block *sb, int *flags, char *data);

static int dentry_revalidate(struct dentry *dentry, unsigned int flags);
static void dentry_release(struct dentry *d);

static int iterate_incfs_dir(struct file *file, struct dir_context *ctx);
static struct dentry *dir_lookup(struct inode *dir_inode,
		struct dentry *dentry, unsigned int flags);
static int dir_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int dir_unlink(struct inode *dir, struct dentry *dentry);
static int dir_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry);
static int dir_rmdir(struct inode *dir, struct dentry *dentry);
static int dir_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry);

static int file_open(struct inode *inode, struct file *file);
static int file_release(struct inode *inode, struct file *file);
static int read_single_page(struct file *f, struct page *page);
static long dispatch_ioctl(struct file *f, unsigned int req, unsigned long arg);

static ssize_t pending_reads_read(struct file *f, char __user *buf, size_t len,
			    loff_t *ppos);
static __poll_t pending_reads_poll(struct file *file, poll_table *wait);
static int pending_reads_open(struct inode *inode, struct file *file);
static int pending_reads_release(struct inode *, struct file *);

static ssize_t log_read(struct file *f, char __user *buf, size_t len,
			    loff_t *ppos);
static __poll_t log_poll(struct file *file, poll_table *wait);
static int log_open(struct inode *inode, struct file *file);
static int log_release(struct inode *, struct file *);

static struct inode *alloc_inode(struct super_block *sb);
static void free_inode(struct inode *inode);
static void evict_inode(struct inode *inode);

static ssize_t incfs_getxattr(struct dentry *d, const char *name,
			void *value, size_t size);
static ssize_t incfs_setxattr(struct dentry *d, const char *name,
			const void *value, size_t size, int flags);
static ssize_t incfs_listxattr(struct dentry *d, char *list, size_t size);

static int show_options(struct seq_file *, struct dentry *);

static const struct super_operations incfs_super_ops = {
	.statfs = simple_statfs,
	.remount_fs = incfs_remount_fs,
	.alloc_inode	= alloc_inode,
	.destroy_inode	= free_inode,
	.evict_inode = evict_inode,
	.show_options = show_options
};

static int dir_rename_wrap(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry,
		unsigned int flags)
{
	return dir_rename(old_dir, old_dentry, new_dir, new_dentry);
}

static const struct inode_operations incfs_dir_inode_ops = {
	.lookup = dir_lookup,
	.mkdir = dir_mkdir,
	.rename = dir_rename_wrap,
	.unlink = dir_unlink,
	.link = dir_link,
	.rmdir = dir_rmdir
};

static const struct file_operations incfs_dir_fops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate = iterate_incfs_dir,
	.open = file_open,
	.release = file_release,
	.unlocked_ioctl = dispatch_ioctl,
	.compat_ioctl = dispatch_ioctl
};

static const struct dentry_operations incfs_dentry_ops = {
	.d_revalidate = dentry_revalidate,
	.d_release = dentry_release
};

static const struct address_space_operations incfs_address_space_ops = {
	.readpage = read_single_page,
	/* .readpages = readpages */
};

static const struct file_operations incfs_file_ops = {
	.open = file_open,
	.release = file_release,
	.read_iter = generic_file_read_iter,
	.mmap = generic_file_mmap,
	.splice_read = generic_file_splice_read,
	.llseek = generic_file_llseek,
	.unlocked_ioctl = dispatch_ioctl,
	.compat_ioctl = dispatch_ioctl
};

static const struct file_operations incfs_pending_read_file_ops = {
	.read = pending_reads_read,
	.poll = pending_reads_poll,
	.open = pending_reads_open,
	.release = pending_reads_release,
	.llseek = noop_llseek,
	.unlocked_ioctl = dispatch_ioctl,
	.compat_ioctl = dispatch_ioctl
};

static const struct file_operations incfs_log_file_ops = {
	.read = log_read,
	.poll = log_poll,
	.open = log_open,
	.release = log_release,
	.llseek = noop_llseek,
	.unlocked_ioctl = dispatch_ioctl,
	.compat_ioctl = dispatch_ioctl
};

static const struct inode_operations incfs_file_inode_ops = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
	.listxattr = incfs_listxattr
};

static int incfs_handler_getxattr(const struct xattr_handler *xh,
				  struct dentry *d, struct inode *inode,
				  const char *name, void *buffer, size_t size)
{
	return incfs_getxattr(d, name, buffer, size);
}

static int incfs_handler_setxattr(const struct xattr_handler *xh,
				  struct dentry *d, struct inode *inode,
				  const char *name, const void *buffer,
				  size_t size, int flags)
{
	return incfs_setxattr(d, name, buffer, size, flags);
}

static const struct xattr_handler incfs_xattr_handler = {
	.prefix = "",	/* AKA all attributes */
	.get = incfs_handler_getxattr,
	.set = incfs_handler_setxattr,
};

static const struct xattr_handler *incfs_xattr_ops[] = {
	&incfs_xattr_handler,
	NULL,
};

/* State of an open .pending_reads file, unique for each file descriptor. */
struct pending_reads_state {
	/* A serial number of the last pending read obtained from this file. */
	int last_pending_read_sn;
};

/* State of an open .log file, unique for each file descriptor. */
struct log_file_state {
	struct read_log_state state;
};

struct inode_search {
	unsigned long ino;

	struct dentry *backing_dentry;
};

enum parse_parameter {
	Opt_read_timeout,
	Opt_readahead_pages,
	Opt_no_backing_file_cache,
	Opt_no_backing_file_readahead,
	Opt_rlog_pages,
	Opt_rlog_wakeup_cnt,
	Opt_err
};

static const char pending_reads_file_name[] = INCFS_PENDING_READS_FILENAME;
static struct mem_range pending_reads_file_name_range = {
	.data = (u8 *)pending_reads_file_name,
	.len = ARRAY_SIZE(pending_reads_file_name) - 1
};

static const char log_file_name[] = INCFS_LOG_FILENAME;
static struct mem_range log_file_name_range = {
	.data = (u8 *)log_file_name,
	.len = ARRAY_SIZE(log_file_name) - 1
};

static const match_table_t option_tokens = {
	{ Opt_read_timeout, "read_timeout_ms=%u" },
	{ Opt_readahead_pages, "readahead=%u" },
	{ Opt_no_backing_file_cache, "no_bf_cache=%u" },
	{ Opt_no_backing_file_readahead, "no_bf_readahead=%u" },
	{ Opt_rlog_pages, "rlog_pages=%u" },
	{ Opt_rlog_wakeup_cnt, "rlog_wakeup_cnt=%u" },
	{ Opt_err, NULL }
};

static int parse_options(struct mount_options *opts, char *str)
{
	substring_t args[MAX_OPT_ARGS];
	int value;
	char *position;

	if (opts == NULL)
		return -EFAULT;

	opts->read_timeout_ms = 1000; /* Default: 1s */
	opts->readahead_pages = 10;
	opts->read_log_pages = 2;
	opts->read_log_wakeup_count = 10;
	opts->no_backing_file_cache = false;
	opts->no_backing_file_readahead = false;
	if (str == NULL || *str == 0)
		return 0;

	while ((position = strsep(&str, ",")) != NULL) {
		int token;

		if (!*position)
			continue;

		token = match_token(position, option_tokens, args);

		switch (token) {
		case Opt_read_timeout:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->read_timeout_ms = value;
			break;
		case Opt_readahead_pages:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->readahead_pages = value;
			break;
		case Opt_no_backing_file_cache:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->no_backing_file_cache = (value != 0);
			break;
		case Opt_no_backing_file_readahead:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->no_backing_file_readahead = (value != 0);
			break;
		case Opt_rlog_pages:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->read_log_pages = value;
			break;
		case Opt_rlog_wakeup_cnt:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->read_log_wakeup_count = value;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static struct super_block *file_superblock(struct file *f)
{
	struct inode *inode = file_inode(f);

	return inode->i_sb;
}

static struct mount_info *get_mount_info(struct super_block *sb)
{
	struct mount_info *result = sb->s_fs_info;

	WARN_ON(!result);
	return result;
}

/* Read file size from the attribute. Quicker than reading the header */
static u64 read_size_attr(struct dentry *backing_dentry)
{
	__le64 attr_value;
	ssize_t bytes_read;

	bytes_read = vfs_getxattr(backing_dentry, INCFS_XATTR_SIZE_NAME,
			(char *)&attr_value, sizeof(attr_value));

	if (bytes_read != sizeof(attr_value))
		return 0;

	return le64_to_cpu(attr_value);
}

static int inode_test(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;
	struct inode_info *node = get_incfs_node(inode);

	if (!node)
		return 0;

	if (search->backing_dentry) {
		struct inode *backing_inode = d_inode(search->backing_dentry);

		return (node->n_backing_inode == backing_inode) &&
			inode->i_ino == search->ino;
	}
	return 1;
}

static int inode_set(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;
	struct inode_info *node = get_incfs_node(inode);

	if (search->backing_dentry) {
		/* It's a regular inode that has corresponding backing inode */
		struct dentry *backing_dentry = search->backing_dentry;
		struct inode *backing_inode = d_inode(backing_dentry);

		fsstack_copy_attr_all(inode, backing_inode);
		if (S_ISREG(inode->i_mode)) {
			u64 size = read_size_attr(backing_dentry);

			inode->i_size = size;
			inode->i_blocks = get_blocks_count_for_size(size);
			inode->i_mapping->a_ops = &incfs_address_space_ops;
			inode->i_op = &incfs_file_inode_ops;
			inode->i_fop = &incfs_file_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_size = 0;
			inode->i_blocks = 1;
			inode->i_mapping->a_ops = &incfs_address_space_ops;
			inode->i_op = &incfs_dir_inode_ops;
			inode->i_fop = &incfs_dir_fops;
		} else {
			pr_warn_once("incfs: Unexpected inode type\n");
			return -EBADF;
		}

		ihold(backing_inode);
		node->n_backing_inode = backing_inode;
		node->n_mount_info = get_mount_info(inode->i_sb);
		inode->i_ctime = backing_inode->i_ctime;
		inode->i_mtime = backing_inode->i_mtime;
		inode->i_atime = backing_inode->i_atime;
		inode->i_ino = backing_inode->i_ino;
		if (backing_inode->i_ino < INCFS_START_INO_RANGE) {
			pr_warn("incfs: ino conflict with backing FS %ld\n",
				backing_inode->i_ino);
		}

		return 0;
	} else if (search->ino == INCFS_PENDING_READS_INODE) {
		/* It's an inode for .pending_reads pseudo file. */

		inode->i_ctime = (struct timespec64){};
		inode->i_mtime = inode->i_ctime;
		inode->i_atime = inode->i_ctime;
		inode->i_size = 0;
		inode->i_ino = INCFS_PENDING_READS_INODE;
		inode->i_private = NULL;

		inode_init_owner(inode, NULL, S_IFREG | READ_WRITE_FILE_MODE);

		inode->i_op = &incfs_file_inode_ops;
		inode->i_fop = &incfs_pending_read_file_ops;

	} else if (search->ino == INCFS_LOG_INODE) {
		/* It's an inode for .log pseudo file. */

		inode->i_ctime = (struct timespec64){};
		inode->i_mtime = inode->i_ctime;
		inode->i_atime = inode->i_ctime;
		inode->i_size = 0;
		inode->i_ino = INCFS_LOG_INODE;
		inode->i_private = NULL;

		inode_init_owner(inode, NULL, S_IFREG | READ_WRITE_FILE_MODE);

		inode->i_op = &incfs_file_inode_ops;
		inode->i_fop = &incfs_log_file_ops;

	} else {
		/* Unknown inode requested. */
		return -EINVAL;
	}

	return 0;
}

static struct inode *fetch_regular_inode(struct super_block *sb,
					struct dentry *backing_dentry)
{
	struct inode *backing_inode = d_inode(backing_dentry);
	struct inode_search search = {
		.ino = backing_inode->i_ino,
		.backing_dentry = backing_dentry
	};
	struct inode *inode = iget5_locked(sb, search.ino, inode_test,
				inode_set, &search);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);

	return inode;
}

static ssize_t pending_reads_read(struct file *f, char __user *buf, size_t len,
			    loff_t *ppos)
{
	struct pending_reads_state *pr_state = f->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(f));
	struct incfs_pending_read_info *reads_buf = NULL;
	size_t reads_to_collect = len / sizeof(*reads_buf);
	int last_known_read_sn = READ_ONCE(pr_state->last_pending_read_sn);
	int new_max_sn = last_known_read_sn;
	int reads_collected = 0;
	ssize_t result = 0;
	int i = 0;

	if (!incfs_fresh_pending_reads_exist(mi, last_known_read_sn))
		return 0;

	reads_buf = (struct incfs_pending_read_info *)get_zeroed_page(GFP_NOFS);
	if (!reads_buf)
		return -ENOMEM;

	reads_to_collect =
		min_t(size_t, PAGE_SIZE / sizeof(*reads_buf), reads_to_collect);

	reads_collected = incfs_collect_pending_reads(
		mi, last_known_read_sn, reads_buf, reads_to_collect);
	if (reads_collected < 0) {
		result = reads_collected;
		goto out;
	}

	for (i = 0; i < reads_collected; i++)
		if (reads_buf[i].serial_number > new_max_sn)
			new_max_sn = reads_buf[i].serial_number;

	/*
	 * Just to make sure that we don't accidentally copy more data
	 * to reads buffer than userspace can handle.
	 */
	reads_collected = min_t(size_t, reads_collected, reads_to_collect);
	result = reads_collected * sizeof(*reads_buf);

	/* Copy reads info to the userspace buffer */
	if (copy_to_user(buf, reads_buf, result)) {
		result = -EFAULT;
		goto out;
	}

	WRITE_ONCE(pr_state->last_pending_read_sn, new_max_sn);
	*ppos = 0;
out:
	if (reads_buf)
		free_page((unsigned long)reads_buf);
	return result;
}


static __poll_t pending_reads_poll(struct file *file, poll_table *wait)
{
	struct pending_reads_state *state = file->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(file));
	__poll_t ret = 0;

	poll_wait(file, &mi->mi_pending_reads_notif_wq, wait);
	if (incfs_fresh_pending_reads_exist(mi,
					    state->last_pending_read_sn))
		ret = EPOLLIN | EPOLLRDNORM;

	return ret;
}

static int pending_reads_open(struct inode *inode, struct file *file)
{
	struct pending_reads_state *state = NULL;

	state = kzalloc(sizeof(*state), GFP_NOFS);
	if (!state)
		return -ENOMEM;

	file->private_data = state;
	return 0;
}

static int pending_reads_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static struct inode *fetch_pending_reads_inode(struct super_block *sb)
{
	struct inode_search search = {
		.ino = INCFS_PENDING_READS_INODE
	};
	struct inode *inode = iget5_locked(sb, search.ino, inode_test,
				inode_set, &search);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);

	return inode;
}

static int log_open(struct inode *inode, struct file *file)
{
	struct log_file_state *log_state = NULL;
	struct mount_info *mi = get_mount_info(file_superblock(file));

	log_state = kzalloc(sizeof(*log_state), GFP_NOFS);
	if (!log_state)
		return -ENOMEM;

	log_state->state = incfs_get_log_state(mi);
	file->private_data = log_state;
	return 0;
}

static int log_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t log_read(struct file *f, char __user *buf, size_t len,
			loff_t *ppos)
{
	struct log_file_state *log_state = f->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(f));
	struct incfs_pending_read_info *reads_buf =
		(struct incfs_pending_read_info *)__get_free_page(GFP_NOFS);
	size_t reads_to_collect = len / sizeof(*reads_buf);
	size_t reads_per_page = PAGE_SIZE / sizeof(*reads_buf);
	int total_reads_collected = 0;
	ssize_t result = 0;

	if (!reads_buf)
		return -ENOMEM;

	reads_to_collect = min_t(size_t, mi->mi_log.rl_size, reads_to_collect);
	while (reads_to_collect > 0) {
		struct read_log_state next_state = READ_ONCE(log_state->state);
		int reads_collected = incfs_collect_logged_reads(
			mi, &next_state, reads_buf,
			min_t(size_t, reads_to_collect, reads_per_page));
		if (reads_collected <= 0) {
			result = total_reads_collected ?
					 total_reads_collected *
						 sizeof(*reads_buf) :
					 reads_collected;
			goto out;
		}
		if (copy_to_user(buf, reads_buf,
				 reads_collected * sizeof(*reads_buf))) {
			result = total_reads_collected ?
					 total_reads_collected *
						 sizeof(*reads_buf) :
					 -EFAULT;
			goto out;
		}

		WRITE_ONCE(log_state->state, next_state);
		total_reads_collected += reads_collected;
		buf += reads_collected * sizeof(*reads_buf);
		reads_to_collect -= reads_collected;
	}

	result = total_reads_collected * sizeof(*reads_buf);
	*ppos = 0;
out:
	if (reads_buf)
		free_page((unsigned long)reads_buf);
	return result;
}

static __poll_t log_poll(struct file *file, poll_table *wait)
{
	struct log_file_state *log_state = file->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(file));
	int count;
	__poll_t ret = 0;

	poll_wait(file, &mi->mi_log.ml_notif_wq, wait);
	count = incfs_get_uncollected_logs_count(mi, log_state->state);
	if (count >= mi->mi_options.read_log_wakeup_count)
		ret = EPOLLIN | EPOLLRDNORM;

	return ret;
}

static struct inode *fetch_log_inode(struct super_block *sb)
{
	struct inode_search search = {
		.ino = INCFS_LOG_INODE
	};
	struct inode *inode = iget5_locked(sb, search.ino, inode_test,
				inode_set, &search);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);

	return inode;
}

static int iterate_incfs_dir(struct file *file, struct dir_context *ctx)
{
	struct dir_file *dir = get_incfs_dir_file(file);
	int error = 0;
	struct mount_info *mi = get_mount_info(file_superblock(file));
	bool root;

	if (!dir) {
		error = -EBADF;
		goto out;
	}

	root = dir->backing_dir->f_inode
			== d_inode(mi->mi_backing_dir_path.dentry);

	if (root && ctx->pos == 0) {
		if (!dir_emit(ctx, pending_reads_file_name,
			      ARRAY_SIZE(pending_reads_file_name) - 1,
			      INCFS_PENDING_READS_INODE, DT_REG)) {
			error = -EINVAL;
			goto out;
		}
		ctx->pos++;
	}

	if (root && ctx->pos == 1) {
		if (!dir_emit(ctx, log_file_name,
			      ARRAY_SIZE(log_file_name) - 1,
			      INCFS_LOG_INODE, DT_REG)) {
			error = -EINVAL;
			goto out;
		}
		ctx->pos++;
	}

	ctx->pos -= 2;
	error = iterate_dir(dir->backing_dir, ctx);
	ctx->pos += 2;
	file->f_pos = dir->backing_dir->f_pos;
out:
	if (error)
		pr_warn("incfs: %s %s %d\n", __func__,
			file->f_path.dentry->d_name.name, error);
	return error;
}

static int incfs_init_dentry(struct dentry *dentry, struct path *path)
{
	struct dentry_info *d_info = NULL;

	if (!dentry || !path)
		return -EFAULT;

	d_info = kzalloc(sizeof(*d_info), GFP_NOFS);
	if (!d_info)
		return -ENOMEM;

	d_info->backing_path = *path;
	path_get(path);

	dentry->d_fsdata = d_info;
	return 0;
}

static struct dentry *incfs_lookup_dentry(struct dentry *parent,
						const char *name)
{
	struct inode *inode;
	struct dentry *result = NULL;

	if (!parent)
		return ERR_PTR(-EFAULT);

	inode = d_inode(parent);
	inode_lock_nested(inode, I_MUTEX_PARENT);
	result = lookup_one_len(name, parent, strlen(name));
	inode_unlock(inode);

	if (IS_ERR(result))
		pr_warn("%s err:%ld\n", __func__, PTR_ERR(result));

	return result;
}

static struct dentry *open_or_create_index_dir(struct dentry *backing_dir)
{
	static const char name[] = ".index";
	struct dentry *index_dentry;
	struct inode *backing_inode = d_inode(backing_dir);
	int err = 0;

	index_dentry = incfs_lookup_dentry(backing_dir, name);
	if (!index_dentry) {
		return ERR_PTR(-EINVAL);
	} else if (IS_ERR(index_dentry)) {
		return index_dentry;
	} else if (d_really_is_positive(index_dentry)) {
		/* Index already exists. */
		return index_dentry;
	}

	/* Index needs to be created. */
	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_mkdir(backing_inode, index_dentry, 0777);
	inode_unlock(backing_inode);

	if (err)
		return ERR_PTR(err);

	if (!d_really_is_positive(index_dentry)) {
		dput(index_dentry);
		return ERR_PTR(-EINVAL);
	}

	return index_dentry;
}

static int read_single_page(struct file *f, struct page *page)
{
	loff_t offset = 0;
	loff_t size = 0;
	ssize_t bytes_to_read = 0;
	ssize_t read_result = 0;
	struct data_file *df = get_incfs_data_file(f);
	int result = 0;
	void *page_start = kmap(page);
	int block_index;
	int timeout_ms;

	if (!df)
		return -EBADF;

	offset = page_offset(page);
	block_index = offset / INCFS_DATA_FILE_BLOCK_SIZE;
	size = df->df_size;
	timeout_ms = df->df_mount_info->mi_options.read_timeout_ms;

	if (offset < size) {
		struct mem_range tmp = {
			.len = 2 * INCFS_DATA_FILE_BLOCK_SIZE
		};

		tmp.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(tmp.len));
		bytes_to_read = min_t(loff_t, size - offset, PAGE_SIZE);
		read_result = incfs_read_data_file_block(
			range(page_start, bytes_to_read), df, block_index,
			timeout_ms, tmp);

		free_pages((unsigned long)tmp.data, get_order(tmp.len));
	} else {
		bytes_to_read = 0;
		read_result = 0;
	}

	if (read_result < 0)
		result = read_result;
	else if (read_result < PAGE_SIZE)
		zero_user(page, read_result, PAGE_SIZE - read_result);

	if (result == 0)
		SetPageUptodate(page);
	else
		SetPageError(page);

	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);
	return result;
}

static char *file_id_to_str(incfs_uuid_t id)
{
	char *result = kmalloc(1 + sizeof(id.bytes) * 2, GFP_NOFS);
	char *end;

	if (!result)
		return NULL;

	end = bin2hex(result, id.bytes, sizeof(id.bytes));
	*end = 0;
	return result;
}

static struct signature_info *incfs_copy_signature_info_from_user(
		struct incfs_file_signature_info __user *original)
{
	struct incfs_file_signature_info usr_si;
	struct signature_info *result;
	int error;

	if (!original)
		return NULL;

	if (copy_from_user(&usr_si, original, sizeof(usr_si)) > 0)
		return ERR_PTR(-EFAULT);

	result = kzalloc(sizeof(*result), GFP_NOFS);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->hash_alg = usr_si.hash_tree_alg;

	if (result->hash_alg) {
		void *p = kzalloc(INCFS_MAX_HASH_SIZE, GFP_NOFS);

		if (!p) {
			error = -ENOMEM;
			goto err;
		}

		/* TODO this sets the root_hash length to MAX_HASH_SIZE not
		 * the actual size. Fix, then set INCFS_MAX_HASH_SIZE back
		 * to 64
		 */
		result->root_hash = range(p, INCFS_MAX_HASH_SIZE);
		if (copy_from_user(p, u64_to_user_ptr(usr_si.root_hash),
				result->root_hash.len) > 0) {
			error = -EFAULT;
			goto err;
		}
	}

	if (usr_si.additional_data_size > INCFS_MAX_FILE_ATTR_SIZE) {
		error = -E2BIG;
		goto err;
	}

	if (usr_si.additional_data && usr_si.additional_data_size) {
		void *p = kzalloc(usr_si.additional_data_size, GFP_NOFS);

		if (!p) {
			error = -ENOMEM;
			goto err;
		}
		result->additional_data = range(p,
					usr_si.additional_data_size);
		if (copy_from_user(p, u64_to_user_ptr(usr_si.additional_data),
				result->additional_data.len) > 0) {
			error = -EFAULT;
			goto err;
		}
	}

	if (usr_si.signature_size > INCFS_MAX_SIGNATURE_SIZE) {
		error = -E2BIG;
		goto err;
	}

	if (usr_si.signature && usr_si.signature_size) {
		void *p = kzalloc(usr_si.signature_size, GFP_NOFS);

		if (!p) {
			error = -ENOMEM;
			goto err;
		}
		result->signature = range(p, usr_si.signature_size);
		if (copy_from_user(p, u64_to_user_ptr(usr_si.signature),
				result->signature.len) > 0) {
			error = -EFAULT;
			goto err;
		}
	}

	return result;

err:
	incfs_free_signature_info(result);
	return ERR_PTR(-error);
}

static int init_new_file(struct mount_info *mi, struct dentry *dentry,
		incfs_uuid_t *uuid, u64 size, struct mem_range attr,
		struct incfs_file_signature_info __user *fsi)
{
	struct path path = {};
	struct file *new_file;
	int error = 0;
	struct backing_file_context *bfc = NULL;
	u32 block_count;
	struct mem_range mem_range = {NULL};
	struct signature_info *si = NULL;
	struct mtree *hash_tree = NULL;

	if (!mi || !dentry || !uuid)
		return -EFAULT;

	/* Resize newly created file to its true size. */
	path = (struct path) {
		.mnt = mi->mi_backing_dir_path.mnt,
		.dentry = dentry
	};
	new_file = dentry_open(&path, O_RDWR | O_NOATIME, mi->mi_owner);

	if (IS_ERR(new_file)) {
		error = PTR_ERR(new_file);
		goto out;
	}

	bfc = incfs_alloc_bfc(new_file);
	if (IS_ERR(bfc)) {
		error = PTR_ERR(bfc);
		bfc = NULL;
		goto out;
	}

	mutex_lock(&bfc->bc_mutex);
	error = incfs_write_fh_to_backing_file(bfc, uuid, size);
	if (error)
		goto out;

	block_count = (u32)get_blocks_count_for_size(size);
	error = incfs_write_blockmap_to_backing_file(bfc, block_count, NULL);
	if (error)
		goto out;

	/* This fill has data, reserve space for the block map. */
	if (block_count > 0) {
		error = incfs_write_blockmap_to_backing_file(
			bfc, block_count, NULL);
		if (error)
			goto out;
	}

	if (attr.data && attr.len) {
		error = incfs_write_file_attr_to_backing_file(bfc,
							attr, NULL);
		if (error)
			goto out;
	}

	if (fsi) {
		si = incfs_copy_signature_info_from_user(fsi);

		if (IS_ERR(si)) {
			error = PTR_ERR(si);
			si = NULL;
			goto out;
		}

		if (si->hash_alg) {
			hash_tree = incfs_alloc_mtree(si->hash_alg, block_count,
						      si->root_hash);
			if (IS_ERR(hash_tree)) {
				error = PTR_ERR(hash_tree);
				hash_tree = NULL;
				goto out;
			}

			/* TODO This code seems wrong when len is zero - we
			 * should error out??
			 */
			if (si->signature.len > 0)
				error = incfs_validate_pkcs7_signature(
						si->signature,
						si->root_hash,
						si->additional_data);
			if (error)
				goto out;

			error = incfs_write_signature_to_backing_file(bfc,
					si->hash_alg,
					hash_tree->hash_tree_area_size,
					si->root_hash, si->additional_data,
					si->signature);

			if (error)
				goto out;
		}
	}

out:
	if (bfc) {
		mutex_unlock(&bfc->bc_mutex);
		incfs_free_bfc(bfc);
	}
	incfs_free_mtree(hash_tree);
	incfs_free_signature_info(si);
	kfree(mem_range.data);

	if (error)
		pr_debug("incfs: %s error: %d\n", __func__, error);
	return error;
}

static int incfs_link(struct dentry *what, struct dentry *where)
{
	struct dentry *parent_dentry = dget_parent(where);
	struct inode *pinode = d_inode(parent_dentry);
	int error = 0;

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	error = vfs_link(what, pinode, where, NULL);
	inode_unlock(pinode);

	dput(parent_dentry);
	return error;
}

static int incfs_unlink(struct dentry *dentry)
{
	struct dentry *parent_dentry = dget_parent(dentry);
	struct inode *pinode = d_inode(parent_dentry);
	int error = 0;

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	error = vfs_unlink(pinode, dentry, NULL);
	inode_unlock(pinode);

	dput(parent_dentry);
	return error;
}

static int incfs_rmdir(struct dentry *dentry)
{
	struct dentry *parent_dentry = dget_parent(dentry);
	struct inode *pinode = d_inode(parent_dentry);
	int error = 0;

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	error = vfs_rmdir(pinode, dentry);
	inode_unlock(pinode);

	dput(parent_dentry);
	return error;
}

static int dir_relative_path_resolve(
			struct mount_info *mi,
			const char __user *relative_path,
			struct path *result_path)
{
	struct path *base_path = &mi->mi_backing_dir_path;
	int dir_fd = get_unused_fd_flags(0);
	struct file *dir_f = NULL;
	int error = 0;

	if (dir_fd < 0)
		return dir_fd;

	dir_f = dentry_open(base_path, O_RDONLY | O_NOATIME, mi->mi_owner);

	if (IS_ERR(dir_f)) {
		error = PTR_ERR(dir_f);
		goto out;
	}
	fd_install(dir_fd, dir_f);

	if (!relative_path) {
		/* No relative path given, just return the base dir. */
		*result_path = *base_path;
		path_get(result_path);
		goto out;
	}

	error = user_path_at_empty(dir_fd, relative_path,
		LOOKUP_FOLLOW | LOOKUP_DIRECTORY, result_path, NULL);

out:
	ksys_close(dir_fd);
	if (error)
		pr_debug("incfs: %s %d\n", __func__, error);
	return error;
}

static int validate_name(char *file_name)
{
	struct mem_range name = range(file_name, strlen(file_name));
	int i = 0;

	if (name.len > INCFS_MAX_NAME_LEN)
		return -ENAMETOOLONG;

	if (incfs_equal_ranges(pending_reads_file_name_range, name))
		return -EINVAL;

	for (i = 0; i < name.len; i++)
		if (name.data[i] == '/')
			return -EINVAL;

	return 0;
}

static int chmod(struct dentry *dentry, umode_t mode)
{
	struct inode *inode = dentry->d_inode;
	struct inode *delegated_inode = NULL;
	struct iattr newattrs;
	int error;

retry_deleg:
	inode_lock(inode);
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	error = notify_change(dentry, &newattrs, &delegated_inode);
	inode_unlock(inode);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	return error;
}

static long ioctl_create_file(struct mount_info *mi,
			struct incfs_new_file_args __user *usr_args)
{
	struct incfs_new_file_args args;
	char *file_id_str = NULL;
	struct dentry *index_file_dentry = NULL;
	struct dentry *named_file_dentry = NULL;
	struct path parent_dir_path = {};
	struct inode *index_dir_inode = NULL;
	__le64 size_attr_value = 0;
	char *file_name = NULL;
	char *attr_value = NULL;
	int error = 0;
	bool locked = false;

	if (!mi || !mi->mi_index_dir) {
		error = -EFAULT;
		goto out;
	}

	if (copy_from_user(&args, usr_args, sizeof(args)) > 0) {
		error = -EFAULT;
		goto out;
	}

	file_name = strndup_user(u64_to_user_ptr(args.file_name), PATH_MAX);
	if (IS_ERR(file_name)) {
		error = PTR_ERR(file_name);
		file_name = NULL;
		goto out;
	}

	error = validate_name(file_name);
	if (error)
		goto out;

	file_id_str = file_id_to_str(args.file_id);
	if (!file_id_str) {
		error = -ENOMEM;
		goto out;
	}

	error = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (error)
		goto out;
	locked = true;

	/* Find a directory to put the file into. */
	error = dir_relative_path_resolve(mi,
			u64_to_user_ptr(args.directory_path),
			&parent_dir_path);
	if (error)
		goto out;

	if (parent_dir_path.dentry == mi->mi_index_dir) {
		/* Can't create a file directly inside .index */
		error = -EBUSY;
		goto out;
	}

	/* Look up a dentry in the parent dir. It should be negative. */
	named_file_dentry = incfs_lookup_dentry(parent_dir_path.dentry,
					file_name);
	if (!named_file_dentry) {
		error = -EFAULT;
		goto out;
	}
	if (IS_ERR(named_file_dentry)) {
		error = PTR_ERR(named_file_dentry);
		named_file_dentry = NULL;
		goto out;
	}
	if (d_really_is_positive(named_file_dentry)) {
		/* File with this path already exists. */
		error = -EEXIST;
		goto out;
	}
	/* Look up a dentry in the .index dir. It should be negative. */
	index_file_dentry = incfs_lookup_dentry(mi->mi_index_dir, file_id_str);
	if (!index_file_dentry) {
		error = -EFAULT;
		goto out;
	}
	if (IS_ERR(index_file_dentry)) {
		error = PTR_ERR(index_file_dentry);
		index_file_dentry = NULL;
		goto out;
	}
	if (d_really_is_positive(index_file_dentry)) {
		/* File with this ID already exists in index. */
		error = -EEXIST;
		goto out;
	}

	/* Creating a file in the .index dir. */
	index_dir_inode = d_inode(mi->mi_index_dir);
	inode_lock_nested(index_dir_inode, I_MUTEX_PARENT);
	error = vfs_create(index_dir_inode, index_file_dentry, args.mode | 0222,
			   true);
	inode_unlock(index_dir_inode);

	if (error)
		goto out;
	if (!d_really_is_positive(index_file_dentry)) {
		error = -EINVAL;
		goto out;
	}

	error = chmod(index_file_dentry, args.mode | 0222);
	if (error) {
		pr_debug("incfs: chmod err: %d\n", error);
		goto delete_index_file;
	}

	/* Save the file's ID as an xattr for easy fetching in future. */
	error = vfs_setxattr(index_file_dentry, INCFS_XATTR_ID_NAME,
		file_id_str, strlen(file_id_str), XATTR_CREATE);
	if (error) {
		pr_debug("incfs: vfs_setxattr err:%d\n", error);
		goto delete_index_file;
	}

	/* Save the file's size as an xattr for easy fetching in future. */
	size_attr_value = cpu_to_le64(args.size);
	error = vfs_setxattr(index_file_dentry, INCFS_XATTR_SIZE_NAME,
		(char *)&size_attr_value, sizeof(size_attr_value),
		XATTR_CREATE);
	if (error) {
		pr_debug("incfs: vfs_setxattr err:%d\n", error);
		goto delete_index_file;
	}

	/* Save the file's attrubute as an xattr */
	if (args.file_attr_len && args.file_attr) {
		if (args.file_attr_len > INCFS_MAX_FILE_ATTR_SIZE) {
			error = -E2BIG;
			goto delete_index_file;
		}

		attr_value = kmalloc(args.file_attr_len, GFP_NOFS);
		if (!attr_value) {
			error = -ENOMEM;
			goto delete_index_file;
		}

		if (copy_from_user(attr_value,
				u64_to_user_ptr(args.file_attr),
				args.file_attr_len) > 0) {
			error = -EFAULT;
			goto delete_index_file;
		}

		error = vfs_setxattr(index_file_dentry,
				INCFS_XATTR_METADATA_NAME,
				attr_value, args.file_attr_len,
				XATTR_CREATE);

		if (error)
			goto delete_index_file;
	}

	/* Initializing a newly created file. */
	error = init_new_file(mi, index_file_dentry, &args.file_id, args.size,
			range(attr_value, args.file_attr_len),
			(struct incfs_file_signature_info __user *)
				args.signature_info);
	if (error)
		goto delete_index_file;

	/* Linking a file with it's real name from the requested dir. */
	error = incfs_link(index_file_dentry, named_file_dentry);

	if (!error)
		goto out;

delete_index_file:
	incfs_unlink(index_file_dentry);

out:
	if (error)
		pr_debug("incfs: %s err:%d\n", __func__, error);

	kfree(file_id_str);
	kfree(file_name);
	kfree(attr_value);
	dput(named_file_dentry);
	dput(index_file_dentry);
	path_put(&parent_dir_path);
	if (locked)
		mutex_unlock(&mi->mi_dir_struct_mutex);
	return error;
}

static long ioctl_fill_blocks(struct file *f, void __user *arg)
{
	struct incfs_fill_blocks __user *usr_fill_blocks = arg;
	struct incfs_fill_blocks fill_blocks;
	struct incfs_fill_block *usr_fill_block_array;
	struct data_file *df = get_incfs_data_file(f);
	const ssize_t data_buf_size = 2 * INCFS_DATA_FILE_BLOCK_SIZE;
	u8 *data_buf = NULL;
	ssize_t error = 0;
	int i = 0;

	if (!df)
		return -EBADF;

	if (copy_from_user(&fill_blocks, usr_fill_blocks, sizeof(fill_blocks)))
		return -EFAULT;

	usr_fill_block_array = u64_to_user_ptr(fill_blocks.fill_blocks);
	data_buf = (u8 *)__get_free_pages(GFP_NOFS, get_order(data_buf_size));
	if (!data_buf)
		return -ENOMEM;

	for (i = 0; i < fill_blocks.count; i++) {
		struct incfs_fill_block fill_block = {};

		if (copy_from_user(&fill_block, &usr_fill_block_array[i],
				   sizeof(fill_block)) > 0) {
			error = -EFAULT;
			break;
		}

		if (fill_block.data_len > data_buf_size) {
			error = -E2BIG;
			break;
		}

		if (copy_from_user(data_buf, u64_to_user_ptr(fill_block.data),
				   fill_block.data_len) > 0) {
			error = -EFAULT;
			break;
		}
		fill_block.data = 0; /* To make sure nobody uses it. */
		if (fill_block.flags & INCFS_BLOCK_FLAGS_HASH) {
			error = incfs_process_new_hash_block(df, &fill_block,
							     data_buf);
		} else {
			error = incfs_process_new_data_block(df, &fill_block,
							     data_buf);
		}
		if (error)
			break;
	}

	if (data_buf)
		free_pages((unsigned long)data_buf, get_order(data_buf_size));

	/*
	 * Only report the error if no records were processed, otherwise
	 * just return how many were processed successfully.
	 */
	if (i == 0)
		return error;

	return i;
}

static long ioctl_read_file_signature(struct file *f, void __user *arg)
{
	struct incfs_get_file_sig_args __user *args_usr_ptr = arg;
	struct incfs_get_file_sig_args args = {};
	u8 *sig_buffer = NULL;
	size_t sig_buf_size = 0;
	int error = 0;
	int read_result = 0;
	struct data_file *df = get_incfs_data_file(f);

	if (!df)
		return -EINVAL;

	if (copy_from_user(&args, args_usr_ptr, sizeof(args)) > 0)
		return -EINVAL;

	sig_buf_size = args.file_signature_buf_size;
	if (sig_buf_size > INCFS_MAX_SIGNATURE_SIZE)
		return -E2BIG;

	sig_buffer = kzalloc(sig_buf_size, GFP_NOFS);
	if (!sig_buffer)
		return -ENOMEM;

	read_result = incfs_read_file_signature(df,
			range(sig_buffer, sig_buf_size));

	if (read_result < 0) {
		error = read_result;
		goto out;
	}

	if (copy_to_user(u64_to_user_ptr(args.file_signature), sig_buffer,
			read_result)) {
		error = -EFAULT;
		goto out;
	}

	args.file_signature_len_out = read_result;
	if (copy_to_user(args_usr_ptr, &args, sizeof(args)))
		error = -EFAULT;

out:
	kfree(sig_buffer);

	return error;
}

static long dispatch_ioctl(struct file *f, unsigned int req, unsigned long arg)
{
	struct mount_info *mi = get_mount_info(file_superblock(f));

	switch (req) {
	case INCFS_IOC_CREATE_FILE:
		return ioctl_create_file(mi, (void __user *)arg);
	case INCFS_IOC_FILL_BLOCKS:
		return ioctl_fill_blocks(f, (void __user *)arg);
	case INCFS_IOC_READ_FILE_SIGNATURE:
		return ioctl_read_file_signature(f, (void __user *)arg);
	default:
		return -EINVAL;
	}
}

static struct dentry *dir_lookup(struct inode *dir_inode, struct dentry *dentry,
				 unsigned int flags)
{
	struct mount_info *mi = get_mount_info(dir_inode->i_sb);
	struct dentry *dir_dentry = NULL;
	struct dentry *backing_dentry = NULL;
	struct path dir_backing_path = {};
	struct inode_info *dir_info = get_incfs_node(dir_inode);
	struct mem_range name_range =
			range((u8 *)dentry->d_name.name, dentry->d_name.len);
	int err = 0;

	if (d_inode(mi->mi_backing_dir_path.dentry) ==
		dir_info->n_backing_inode) {
		/* We do lookup in the FS root. Show pseudo files. */

		if (incfs_equal_ranges(pending_reads_file_name_range,
								name_range)) {
			struct inode *inode = fetch_pending_reads_inode(
				dir_inode->i_sb);

			if (IS_ERR(inode)) {
				err = PTR_ERR(inode);
				goto out;
			}

			d_add(dentry, inode);
			goto out;
		}

		if (incfs_equal_ranges(log_file_name_range, name_range)) {
			struct inode *inode = fetch_log_inode(
				dir_inode->i_sb);

			if (IS_ERR(inode)) {
				err = PTR_ERR(inode);
				goto out;
			}

			d_add(dentry, inode);
			goto out;
		}
	}

	dir_dentry = dget_parent(dentry);
	get_incfs_backing_path(dir_dentry, &dir_backing_path);
	backing_dentry = incfs_lookup_dentry(dir_backing_path.dentry,
						dentry->d_name.name);

	if (!backing_dentry || IS_ERR(backing_dentry)) {
		err = IS_ERR(backing_dentry)
			? PTR_ERR(backing_dentry)
			: -EFAULT;
		backing_dentry = NULL;
		goto out;
	} else {
		struct inode *inode = NULL;
		struct path backing_path = {
			.mnt = dir_backing_path.mnt,
			.dentry = backing_dentry
		};

		err = incfs_init_dentry(dentry, &backing_path);
		if (err)
			goto out;

		if (!d_really_is_positive(backing_dentry)) {
			/*
			 * No such entry found in the backing dir.
			 * Create a negative entry.
			 */
			d_add(dentry, NULL);
			err = 0;
			goto out;
		}

		if (d_inode(backing_dentry)->i_sb !=
				dir_info->n_backing_inode->i_sb) {
			/*
			 * Somehow after the path lookup we ended up in a
			 * different fs mount. If we keep going it's going
			 * to end badly.
			 */
			err = -EXDEV;
			goto out;
		}

		inode = fetch_regular_inode(dir_inode->i_sb, backing_dentry);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto out;
		}

		d_add(dentry, inode);
	}

out:
	dput(dir_dentry);
	dput(backing_dentry);
	path_put(&dir_backing_path);
	if (err)
		pr_debug("incfs: %s %s %d\n", __func__,
			 dentry->d_name.name, err);
	return ERR_PTR(err);
}

static int dir_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct mount_info *mi = get_mount_info(dir->i_sb);
	struct inode_info *dir_node = get_incfs_node(dir);
	struct dentry *backing_dentry = NULL;
	struct path backing_path = {};
	int err = 0;


	if (!mi || !dir_node || !dir_node->n_backing_inode)
		return -EBADF;

	err = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (err)
		return err;

	get_incfs_backing_path(dentry, &backing_path);
	backing_dentry = backing_path.dentry;

	if (!backing_dentry) {
		err = -EBADF;
		goto out;
	}

	if (backing_dentry->d_parent == mi->mi_index_dir) {
		/* Can't create a subdir inside .index */
		err = -EBUSY;
		goto out;
	}

	inode_lock_nested(dir_node->n_backing_inode, I_MUTEX_PARENT);
	err = vfs_mkdir(dir_node->n_backing_inode, backing_dentry, mode | 0222);
	inode_unlock(dir_node->n_backing_inode);
	if (!err) {
		struct inode *inode = NULL;

		if (d_really_is_negative(backing_dentry)) {
			err = -EINVAL;
			goto out;
		}

		inode = fetch_regular_inode(dir->i_sb, backing_dentry);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			goto out;
		}
		d_instantiate(dentry, inode);
	}

out:
	if (d_really_is_negative(dentry))
		d_drop(dentry);
	path_put(&backing_path);
	mutex_unlock(&mi->mi_dir_struct_mutex);
	if (err)
		pr_debug("incfs: %s err:%d\n", __func__, err);
	return err;
}

/* Delete file referenced by backing_dentry and also its hardlink from .index */
static int final_file_delete(struct mount_info *mi,
			struct dentry *backing_dentry)
{
	struct dentry *index_file_dentry = NULL;
	/* 2 chars per byte of file ID + 1 char for \0 */
	char file_id_str[2 * sizeof(incfs_uuid_t) + 1] = {0};
	ssize_t uuid_size = 0;
	int error = 0;

	WARN_ON(!mutex_is_locked(&mi->mi_dir_struct_mutex));
	uuid_size = vfs_getxattr(backing_dentry, INCFS_XATTR_ID_NAME,
			file_id_str, 2 * sizeof(incfs_uuid_t));
	if (uuid_size < 0) {
		error = uuid_size;
		goto out;
	}

	if (uuid_size != 2 * sizeof(incfs_uuid_t)) {
		error = -EBADMSG;
		goto out;
	}

	index_file_dentry = incfs_lookup_dentry(mi->mi_index_dir, file_id_str);
	if (IS_ERR(index_file_dentry)) {
		error = PTR_ERR(index_file_dentry);
		goto out;
	}

	error = incfs_unlink(backing_dentry);
	if (error)
		goto out;

	if (d_really_is_positive(index_file_dentry))
		error = incfs_unlink(index_file_dentry);
out:
	if (error)
		pr_debug("incfs: delete_file_from_index err:%d\n", error);
	return error;
}

static int dir_unlink(struct inode *dir, struct dentry *dentry)
{
	struct mount_info *mi = get_mount_info(dir->i_sb);
	struct path backing_path = {};
	struct kstat stat;
	int err = 0;

	err = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (err)
		return err;

	get_incfs_backing_path(dentry, &backing_path);
	if (!backing_path.dentry) {
		err = -EBADF;
		goto out;
	}

	if (backing_path.dentry->d_parent == mi->mi_index_dir) {
		/* Direct unlink from .index are not allowed. */
		err = -EBUSY;
		goto out;
	}

	err = vfs_getattr(&backing_path, &stat, STATX_NLINK,
			  AT_STATX_SYNC_AS_STAT);
	if (err)
		goto out;

	if (stat.nlink == 2) {
		/*
		 * This is the last named link to this file. The only one left
		 * is in .index. Remove them both now.
		 */
		err = final_file_delete(mi, backing_path.dentry);
	} else {
		/* There are other links to this file. Remove just this one. */
		err = incfs_unlink(backing_path.dentry);
	}

	d_drop(dentry);
out:
	path_put(&backing_path);
	if (err)
		pr_debug("incfs: %s err:%d\n", __func__, err);
	mutex_unlock(&mi->mi_dir_struct_mutex);
	return err;
}

static int dir_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	struct mount_info *mi = get_mount_info(dir->i_sb);
	struct path backing_old_path = {};
	struct path backing_new_path = {};
	int error = 0;

	error = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (error)
		return error;

	get_incfs_backing_path(old_dentry, &backing_old_path);
	get_incfs_backing_path(new_dentry, &backing_new_path);

	if (backing_new_path.dentry->d_parent == mi->mi_index_dir) {
		/* Can't link to .index */
		error = -EBUSY;
		goto out;
	}

	error = incfs_link(backing_old_path.dentry, backing_new_path.dentry);
	if (!error) {
		struct inode *inode = NULL;
		struct dentry *bdentry = backing_new_path.dentry;

		if (d_really_is_negative(bdentry)) {
			error = -EINVAL;
			goto out;
		}

		inode = fetch_regular_inode(dir->i_sb, bdentry);
		if (IS_ERR(inode)) {
			error = PTR_ERR(inode);
			goto out;
		}
		d_instantiate(new_dentry, inode);
	}

out:
	path_put(&backing_old_path);
	path_put(&backing_new_path);
	if (error)
		pr_debug("incfs: %s err:%d\n", __func__, error);
	mutex_unlock(&mi->mi_dir_struct_mutex);
	return error;
}

static int dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct mount_info *mi = get_mount_info(dir->i_sb);
	struct path backing_path = {};
	int err = 0;

	err = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (err)
		return err;

	get_incfs_backing_path(dentry, &backing_path);
	if (!backing_path.dentry) {
		err = -EBADF;
		goto out;
	}

	if (backing_path.dentry == mi->mi_index_dir) {
		/* Can't delete .index */
		err = -EBUSY;
		goto out;
	}

	err = incfs_rmdir(backing_path.dentry);
	if (!err)
		d_drop(dentry);
out:
	path_put(&backing_path);
	if (err)
		pr_debug("incfs: %s err:%d\n", __func__, err);
	mutex_unlock(&mi->mi_dir_struct_mutex);
	return err;
}

static int dir_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	struct mount_info *mi = get_mount_info(old_dir->i_sb);
	struct dentry *backing_old_dentry;
	struct dentry *backing_new_dentry;
	struct dentry *backing_old_dir_dentry;
	struct dentry *backing_new_dir_dentry;
	struct inode *target_inode;
	struct dentry *trap;
	int error = 0;

	error = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (error)
		return error;

	backing_old_dentry = get_incfs_dentry(old_dentry)->backing_path.dentry;
	backing_new_dentry = get_incfs_dentry(new_dentry)->backing_path.dentry;
	dget(backing_old_dentry);
	dget(backing_new_dentry);

	backing_old_dir_dentry = dget_parent(backing_old_dentry);
	backing_new_dir_dentry = dget_parent(backing_new_dentry);
	target_inode = d_inode(new_dentry);

	if (backing_old_dir_dentry == mi->mi_index_dir) {
		/* Direct moves from .index are not allowed. */
		error = -EBUSY;
		goto out;
	}

	trap = lock_rename(backing_old_dir_dentry, backing_new_dir_dentry);

	if (trap == backing_old_dentry) {
		error = -EINVAL;
		goto unlock_out;
	}
	if (trap == backing_new_dentry) {
		error = -ENOTEMPTY;
		goto unlock_out;
	}

	error = vfs_rename(d_inode(backing_old_dir_dentry), backing_old_dentry,
			d_inode(backing_new_dir_dentry), backing_new_dentry,
			NULL, 0);
	if (error)
		goto unlock_out;
	if (target_inode)
		fsstack_copy_attr_all(target_inode,
			get_incfs_node(target_inode)->n_backing_inode);
	fsstack_copy_attr_all(new_dir, d_inode(backing_new_dir_dentry));
	if (new_dir != old_dir)
		fsstack_copy_attr_all(old_dir, d_inode(backing_old_dir_dentry));

unlock_out:
	unlock_rename(backing_old_dir_dentry, backing_new_dir_dentry);

out:
	dput(backing_new_dir_dentry);
	dput(backing_old_dir_dentry);
	dput(backing_new_dentry);
	dput(backing_old_dentry);

	mutex_unlock(&mi->mi_dir_struct_mutex);
	if (error)
		pr_debug("incfs: %s err:%d\n", __func__, error);
	return error;
}


static int file_open(struct inode *inode, struct file *file)
{
	struct mount_info *mi = get_mount_info(inode->i_sb);
	struct file *backing_file = NULL;
	struct path backing_path = {};
	int err = 0;

	get_incfs_backing_path(file->f_path.dentry, &backing_path);
	backing_file = dentry_open(&backing_path, O_RDWR | O_NOATIME,
				mi->mi_owner);
	path_put(&backing_path);

	if (IS_ERR(backing_file)) {
		err = PTR_ERR(backing_file);
		backing_file = NULL;
		goto out;
	}

	if (S_ISREG(inode->i_mode))
		err = make_inode_ready_for_data_ops(mi, inode, backing_file);
	else if (S_ISDIR(inode->i_mode)) {
		struct dir_file *dir = NULL;

		dir = incfs_open_dir_file(mi, backing_file);
		if (IS_ERR(dir))
			err = PTR_ERR(dir);
		else
			file->private_data = dir;
	} else
		err = -EBADF;

out:
	if (err)
		pr_debug("incfs: %s name:%s err: %d\n", __func__,
			file->f_path.dentry->d_name.name, err);
	if (backing_file)
		fput(backing_file);
	return err;
}

static int file_release(struct inode *inode, struct file *file)
{
	if (S_ISREG(inode->i_mode)) {
		/* Do nothing.
		 * data_file is released only by inode eviction.
		 */
	} else if (S_ISDIR(inode->i_mode)) {
		struct dir_file *dir = get_incfs_dir_file(file);

		incfs_free_dir_file(dir);
	}

	return 0;
}

static int dentry_revalidate(struct dentry *d, unsigned int flags)
{
	struct path backing_path = {};
	struct inode_info *info = get_incfs_node(d_inode(d));
	struct inode *binode = (info == NULL) ? NULL : info->n_backing_inode;
	struct dentry *backing_dentry = NULL;
	int result = 0;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	get_incfs_backing_path(d, &backing_path);
	backing_dentry = backing_path.dentry;
	if (!backing_dentry)
		goto out;

	if (d_inode(backing_dentry) != binode) {
		/*
		 * Backing inodes obtained via dentry and inode don't match.
		 * It indicates that most likely backing dir has changed
		 * directly bypassing Incremental FS interface.
		 */
		goto out;
	}

	if (backing_dentry->d_flags & DCACHE_OP_REVALIDATE) {
		result = backing_dentry->d_op->d_revalidate(backing_dentry,
				flags);
	} else
		result = 1;

out:
	path_put(&backing_path);
	return result;
}

static void dentry_release(struct dentry *d)
{
	struct dentry_info *di = get_incfs_dentry(d);

	if (di)
		path_put(&di->backing_path);
	d->d_fsdata = NULL;
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct inode_info *node = kzalloc(sizeof(*node), GFP_NOFS);

	/* TODO: add a slab-based cache here. */
	if (!node)
		return NULL;
	inode_init_once(&node->n_vfs_inode);
	return &node->n_vfs_inode;
}

static void free_inode(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);

	kfree(node);
}

static void evict_inode(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);

	if (node) {
		if (node->n_backing_inode) {
			iput(node->n_backing_inode);
			node->n_backing_inode = NULL;
		}
		if (node->n_file) {
			incfs_free_data_file(node->n_file);
			node->n_file = NULL;
		}
	}

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
}

static ssize_t incfs_getxattr(struct dentry *d, const char *name,
			void *value, size_t size)
{
	struct dentry_info *di = get_incfs_dentry(d);
	struct mount_info *mi = get_mount_info(d->d_sb);
	char *stored_value;
	size_t stored_size;

	if (di && di->backing_path.dentry)
		return vfs_getxattr(di->backing_path.dentry, name, value, size);

	if (strcmp(name, "security.selinux"))
		return -ENODATA;

	if (!strcmp(d->d_iname, INCFS_PENDING_READS_FILENAME)) {
		stored_value = mi->pending_read_xattr;
		stored_size = mi->pending_read_xattr_size;
	} else if (!strcmp(d->d_iname, INCFS_LOG_FILENAME)) {
		stored_value = mi->log_xattr;
		stored_size = mi->log_xattr_size;
	} else {
		return -ENODATA;
	}

	if (!stored_value)
		return -ENODATA;

	if (stored_size > size)
		return -E2BIG;

	memcpy(value, stored_value, stored_size);
	return stored_size;

}


static ssize_t incfs_setxattr(struct dentry *d, const char *name,
			const void *value, size_t size, int flags)
{
	struct dentry_info *di = get_incfs_dentry(d);
	struct mount_info *mi = get_mount_info(d->d_sb);
	void **stored_value;
	size_t *stored_size;

	if (di && di->backing_path.dentry)
		return vfs_setxattr(di->backing_path.dentry, name, value, size,
				    flags);

	if (strcmp(name, "security.selinux"))
		return -ENODATA;

	if (size > INCFS_MAX_FILE_ATTR_SIZE)
		return -E2BIG;

	if (!strcmp(d->d_iname, INCFS_PENDING_READS_FILENAME)) {
		stored_value = &mi->pending_read_xattr;
		stored_size = &mi->pending_read_xattr_size;
	} else if (!strcmp(d->d_iname, INCFS_LOG_FILENAME)) {
		stored_value = &mi->log_xattr;
		stored_size = &mi->log_xattr_size;
	} else {
		return -ENODATA;
	}

	kfree (*stored_value);
	*stored_value = kzalloc(size, GFP_NOFS);
	if (!*stored_value)
		return -ENOMEM;

	memcpy(*stored_value, value, size);
	*stored_size = size;
	return 0;
}

static ssize_t incfs_listxattr(struct dentry *d, char *list, size_t size)
{
	struct dentry_info *di = get_incfs_dentry(d);

	if (!di || !di->backing_path.dentry)
		return -ENODATA;

	return vfs_listxattr(di->backing_path.dentry, list, size);
}

struct dentry *incfs_mount_fs(struct file_system_type *type, int flags,
			      const char *dev_name, void *data)
{
	struct mount_options options = {};
	struct mount_info *mi = NULL;
	struct path backing_dir_path = {};
	struct dentry *index_dir;
	struct super_block *src_fs_sb = NULL;
	struct inode *root_inode = NULL;
	struct super_block *sb = sget(type, NULL, set_anon_super, flags, NULL);
	int error = 0;

	if (IS_ERR(sb))
		return ERR_CAST(sb);

	sb->s_op = &incfs_super_ops;
	sb->s_d_op = &incfs_dentry_ops;
	sb->s_flags |= S_NOATIME;
	sb->s_magic = INCFS_MAGIC_NUMBER;
	sb->s_time_gran = 1;
	sb->s_blocksize = INCFS_DATA_FILE_BLOCK_SIZE;
	sb->s_blocksize_bits = blksize_bits(sb->s_blocksize);
	sb->s_xattr = incfs_xattr_ops;

	BUILD_BUG_ON(PAGE_SIZE != INCFS_DATA_FILE_BLOCK_SIZE);

	error = parse_options(&options, (char *)data);
	if (error != 0) {
		pr_err("incfs: Options parsing error. %d\n", error);
		goto err;
	}

	sb->s_bdi->ra_pages = options.readahead_pages;
	if (!dev_name) {
		pr_err("incfs: Backing dir is not set, filesystem can't be mounted.\n");
		error = -ENOENT;
		goto err;
	}

	error = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&backing_dir_path);
	if (error || backing_dir_path.dentry == NULL ||
		!d_really_is_positive(backing_dir_path.dentry)) {
		pr_err("incfs: Error accessing: %s.\n",
			dev_name);
		goto err;
	}
	src_fs_sb = backing_dir_path.dentry->d_sb;
	sb->s_maxbytes = src_fs_sb->s_maxbytes;

	mi = incfs_alloc_mount_info(sb, &options, &backing_dir_path);

	if (IS_ERR_OR_NULL(mi)) {
		error = PTR_ERR(mi);
		pr_err("incfs: Error allocating mount info. %d\n", error);
		mi = NULL;
		goto err;
	}

	index_dir = open_or_create_index_dir(backing_dir_path.dentry);
	if (IS_ERR_OR_NULL(index_dir)) {
		error = PTR_ERR(index_dir);
		pr_err("incfs: Can't find or create .index dir in %s\n",
			dev_name);
		goto err;
	}
	mi->mi_index_dir = index_dir;

	sb->s_fs_info = mi;
	root_inode = fetch_regular_inode(sb, backing_dir_path.dentry);
	if (IS_ERR(root_inode)) {
		error = PTR_ERR(root_inode);
		goto err;
	}

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		error = -ENOMEM;
		goto err;
	}
	error = incfs_init_dentry(sb->s_root, &backing_dir_path);
	if (error)
		goto err;

	path_put(&backing_dir_path);
	sb->s_flags |= SB_ACTIVE;

	pr_debug("infs: mount\n");
	return dget(sb->s_root);
err:
	sb->s_fs_info = NULL;
	path_put(&backing_dir_path);
	incfs_free_mount_info(mi);
	deactivate_locked_super(sb);
	return ERR_PTR(error);
}

static int incfs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct mount_options options;
	struct mount_info *mi = get_mount_info(sb);
	int err = 0;

	sync_filesystem(sb);
	err = parse_options(&options, (char *)data);
	if (err)
		return err;

	if (mi->mi_options.read_timeout_ms != options.read_timeout_ms) {
		mi->mi_options.read_timeout_ms = options.read_timeout_ms;
		pr_debug("incfs: new timeout_ms=%d", options.read_timeout_ms);
	}

	pr_debug("infs: remount\n");
	return 0;
}

void incfs_kill_sb(struct super_block *sb)
{
	struct mount_info *mi = sb->s_fs_info;

	pr_debug("infs: unmount\n");
	incfs_free_mount_info(mi);
	generic_shutdown_super(sb);
}

static int show_options(struct seq_file *m, struct dentry *root)
{
	struct mount_info *mi = get_mount_info(root->d_sb);

	seq_printf(m, ",read_timeout_ms=%u", mi->mi_options.read_timeout_ms);
	seq_printf(m, ",readahead=%u", mi->mi_options.readahead_pages);
	if (mi->mi_options.read_log_pages != 0) {
		seq_printf(m, ",rlog_pages=%u", mi->mi_options.read_log_pages);
		seq_printf(m, ",rlog_wakeup_cnt=%u",
			   mi->mi_options.read_log_wakeup_count);
	}
	if (mi->mi_options.no_backing_file_cache)
		seq_puts(m, ",no_bf_cache");
	if (mi->mi_options.no_backing_file_readahead)
		seq_puts(m, ",no_bf_readahead");
	return 0;
}
