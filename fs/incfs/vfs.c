// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */

#include <linux/blkdev.h>
#include <linux/compat.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>
#include <linux/fsnotify.h>
#include <linux/fsverity.h>
#include <linux/mmap_lock.h>
#include <linux/namei.h>
#include <linux/parser.h>
#include <linux/seq_file.h>

#include <uapi/linux/incrementalfs.h>

#include "vfs.h"

#include "data_mgmt.h"
#include "format.h"
#include "internal.h"
#include "pseudo_files.h"
#include "verity.h"

static int incfs_remount_fs(struct super_block *sb, int *flags, char *data);

static int dentry_revalidate(struct dentry *dentry, unsigned int flags);
static void dentry_release(struct dentry *d);

static int iterate_incfs_dir(struct file *file, struct dir_context *ctx);
static struct dentry *dir_lookup(struct inode *dir_inode,
		struct dentry *dentry, unsigned int flags);
static int dir_mkdir(struct user_namespace *ns, struct inode *dir,
		     struct dentry *dentry, umode_t mode);
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

#ifdef CONFIG_COMPAT
static long incfs_compat_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg);
#endif

static struct inode *alloc_inode(struct super_block *sb);
static void free_inode(struct inode *inode);
static void evict_inode(struct inode *inode);

static int incfs_setattr(struct user_namespace *ns, struct dentry *dentry,
			 struct iattr *ia);
static int incfs_getattr(struct user_namespace *ns, const struct path *path,
			 struct kstat *stat, u32 request_mask,
			 unsigned int query_flags);
static ssize_t incfs_getxattr(struct dentry *d, const char *name,
			void *value, size_t size);
static ssize_t incfs_setxattr(struct user_namespace *ns, struct dentry *d,
			      const char *name, const void *value, size_t size,
			      int flags);
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

static int dir_rename_wrap(struct user_namespace *ns, struct inode *old_dir,
			   struct dentry *old_dentry, struct inode *new_dir,
			   struct dentry *new_dentry, unsigned int flags)
{
	return dir_rename(old_dir, old_dentry, new_dir, new_dentry);
}

static const struct inode_operations incfs_dir_inode_ops = {
	.lookup = dir_lookup,
	.mkdir = dir_mkdir,
	.rename = dir_rename_wrap,
	.unlink = dir_unlink,
	.link = dir_link,
	.rmdir = dir_rmdir,
	.setattr = incfs_setattr,
};

static const struct file_operations incfs_dir_fops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate = iterate_incfs_dir,
	.open = file_open,
	.release = file_release,
};

static const struct dentry_operations incfs_dentry_ops = {
	.d_revalidate = dentry_revalidate,
	.d_release = dentry_release
};

static const struct address_space_operations incfs_address_space_ops = {
	.readpage = read_single_page,
	/* .readpages = readpages */
};

static vm_fault_t incfs_fault(struct vm_fault *vmf)
{
	vmf->flags &= ~FAULT_FLAG_ALLOW_RETRY;
	return filemap_fault(vmf);
}

static const struct vm_operations_struct incfs_file_vm_ops = {
	.fault		= incfs_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= filemap_page_mkwrite,
};

/* This is used for a general mmap of a disk file */

static int incfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	file_accessed(file);
	vma->vm_ops = &incfs_file_vm_ops;
	return 0;
}

const struct file_operations incfs_file_ops = {
	.open = file_open,
	.release = file_release,
	.read_iter = generic_file_read_iter,
	.mmap = incfs_file_mmap,
	.splice_read = generic_file_splice_read,
	.llseek = generic_file_llseek,
	.unlocked_ioctl = dispatch_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = incfs_compat_ioctl,
#endif
};

const struct inode_operations incfs_file_inode_ops = {
	.setattr = incfs_setattr,
	.getattr = incfs_getattr,
	.listxattr = incfs_listxattr
};

static int incfs_handler_getxattr(const struct xattr_handler *xh,
				  struct dentry *d, struct inode *inode,
				  const char *name, void *buffer, size_t size)
{
	return incfs_getxattr(d, name, buffer, size);
}

static int incfs_handler_setxattr(const struct xattr_handler *xh,
				  struct user_namespace *ns,
				  struct dentry *d, struct inode *inode,
				  const char *name, const void *buffer,
				  size_t size, int flags)
{
	return incfs_setxattr(ns, d, name, buffer, size, flags);
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

struct inode_search {
	unsigned long ino;

	struct dentry *backing_dentry;

	size_t size;

	bool verity;
};

enum parse_parameter {
	Opt_read_timeout,
	Opt_readahead_pages,
	Opt_rlog_pages,
	Opt_rlog_wakeup_cnt,
	Opt_report_uid,
	Opt_err
};

static const match_table_t option_tokens = {
	{ Opt_read_timeout, "read_timeout_ms=%u" },
	{ Opt_readahead_pages, "readahead=%u" },
	{ Opt_rlog_pages, "rlog_pages=%u" },
	{ Opt_rlog_wakeup_cnt, "rlog_wakeup_cnt=%u" },
	{ Opt_report_uid, "report_uid" },
	{ Opt_err, NULL }
};

static int parse_options(struct mount_options *opts, char *str)
{
	substring_t args[MAX_OPT_ARGS];
	int value;
	char *position;

	if (opts == NULL)
		return -EFAULT;

	*opts = (struct mount_options) {
		.read_timeout_ms = 1000, /* Default: 1s */
		.readahead_pages = 10,
		.read_log_pages = 2,
		.read_log_wakeup_count = 10,
	};

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
			if (value > 3600000)
				return -EINVAL;
			opts->read_timeout_ms = value;
			break;
		case Opt_readahead_pages:
			if (match_int(&args[0], &value))
				return -EINVAL;
			opts->readahead_pages = value;
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
		case Opt_report_uid:
			opts->report_uid = true;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

/* Read file size from the attribute. Quicker than reading the header */
static u64 read_size_attr(struct dentry *backing_dentry)
{
	__le64 attr_value;
	ssize_t bytes_read;

	bytes_read = vfs_getxattr(&init_user_ns, backing_dentry, INCFS_XATTR_SIZE_NAME,
			(char *)&attr_value, sizeof(attr_value));

	if (bytes_read != sizeof(attr_value))
		return 0;

	return le64_to_cpu(attr_value);
}

/* Read verity flag from the attribute. Quicker than reading the header */
static bool read_verity_attr(struct dentry *backing_dentry)
{
	return vfs_getxattr(&init_user_ns, backing_dentry, INCFS_XATTR_VERITY_NAME, NULL, 0)
		>= 0;
}

static int inode_test(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;
	struct inode_info *node = get_incfs_node(inode);
	struct inode *backing_inode = d_inode(search->backing_dentry);

	if (!node)
		return 0;

	return node->n_backing_inode == backing_inode &&
		inode->i_ino == search->ino;
}

static int inode_set(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;
	struct inode_info *node = get_incfs_node(inode);
	struct dentry *backing_dentry = search->backing_dentry;
	struct inode *backing_inode = d_inode(backing_dentry);

	fsstack_copy_attr_all(inode, backing_inode);
	if (S_ISREG(inode->i_mode)) {
		u64 size = search->size;

		inode->i_size = size;
		inode->i_blocks = get_blocks_count_for_size(size);
		inode->i_mapping->a_ops = &incfs_address_space_ops;
		inode->i_op = &incfs_file_inode_ops;
		inode->i_fop = &incfs_file_ops;
		inode->i_mode &= ~0222;
		if (search->verity)
			inode_set_flags(inode, S_VERITY, S_VERITY);
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
}

static struct inode *fetch_regular_inode(struct super_block *sb,
					struct dentry *backing_dentry)
{
	struct inode *backing_inode = d_inode(backing_dentry);
	struct inode_search search = {
		.ino = backing_inode->i_ino,
		.backing_dentry = backing_dentry,
		.size = read_size_attr(backing_dentry),
		.verity = read_verity_attr(backing_dentry),
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

	if (root) {
		error = emit_pseudo_files(ctx);
		if (error)
			goto out;
	}

	ctx->pos -= PSEUDO_FILE_COUNT;
	error = iterate_dir(dir->backing_dir, ctx);
	ctx->pos += PSEUDO_FILE_COUNT;
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

static struct dentry *open_or_create_special_dir(struct dentry *backing_dir,
						 const char *name)
{
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
	err = vfs_mkdir(&init_user_ns, backing_inode, index_dentry, 0777);
	inode_unlock(backing_inode);

	if (err)
		return ERR_PTR(err);

	if (!d_really_is_positive(index_dentry) ||
		unlikely(d_unhashed(index_dentry))) {
		dput(index_dentry);
		return ERR_PTR(-EINVAL);
	}

	return index_dentry;
}

static int read_single_page_timeouts(struct data_file *df, struct file *f,
				     int block_index, struct mem_range range,
				     struct mem_range tmp)
{
	struct mount_info *mi = df->df_mount_info;
	u32 min_time_us = 0;
	u32 min_pending_time_us = 0;
	u32 max_pending_time_us = U32_MAX;
	int uid = current_uid().val;
	int i;

	spin_lock(&mi->mi_per_uid_read_timeouts_lock);
	for (i = 0; i < mi->mi_per_uid_read_timeouts_size /
		sizeof(*mi->mi_per_uid_read_timeouts); ++i) {
		struct incfs_per_uid_read_timeouts *t =
			&mi->mi_per_uid_read_timeouts[i];

		if(t->uid == uid) {
			min_time_us = t->min_time_us;
			min_pending_time_us = t->min_pending_time_us;
			max_pending_time_us = t->max_pending_time_us;
			break;
		}
	}
	spin_unlock(&mi->mi_per_uid_read_timeouts_lock);
	if (max_pending_time_us == U32_MAX) {
		u64 read_timeout_us = (u64)mi->mi_options.read_timeout_ms *
					1000;

		max_pending_time_us = read_timeout_us <= U32_MAX ?
					read_timeout_us : U32_MAX;
	}

	return incfs_read_data_file_block(range, f, block_index,
		min_time_us, min_pending_time_us, max_pending_time_us,
		tmp);
}

static int read_single_page(struct file *f, struct page *page)
{
	loff_t offset = 0;
	loff_t size = 0;
	ssize_t bytes_to_read = 0;
	ssize_t read_result = 0;
	struct data_file *df = get_incfs_data_file(f);
	int result = 0;
	void *page_start;
	int block_index;

	if (!df) {
		SetPageError(page);
		unlock_page(page);
		return -EBADF;
	}

	page_start = kmap(page);
	offset = page_offset(page);
	block_index = (offset + df->df_mapped_offset) /
		INCFS_DATA_FILE_BLOCK_SIZE;
	size = df->df_size;

	if (offset < size) {
		struct mem_range tmp = {
			.len = 2 * INCFS_DATA_FILE_BLOCK_SIZE
		};
		tmp.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(tmp.len));
		if (!tmp.data) {
			read_result = -ENOMEM;
			goto err;
		}
		bytes_to_read = min_t(loff_t, size - offset, PAGE_SIZE);

		read_result = read_single_page_timeouts(df, f, block_index,
					range(page_start, bytes_to_read), tmp);

		free_pages((unsigned long)tmp.data, get_order(tmp.len));
	} else {
		bytes_to_read = 0;
		read_result = 0;
	}

err:
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

int incfs_link(struct dentry *what, struct dentry *where)
{
	struct dentry *parent_dentry = dget_parent(where);
	struct inode *pinode = d_inode(parent_dentry);
	int error = 0;

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	error = vfs_link(what, &init_user_ns, pinode, where, NULL);
	inode_unlock(pinode);

	dput(parent_dentry);
	return error;
}

int incfs_unlink(struct dentry *dentry)
{
	struct dentry *parent_dentry = dget_parent(dentry);
	struct inode *pinode = d_inode(parent_dentry);
	int error = 0;

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	error = vfs_unlink(&init_user_ns, pinode, dentry, NULL);
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
	error = vfs_rmdir(&init_user_ns, pinode, dentry);
	inode_unlock(pinode);

	dput(parent_dentry);
	return error;
}

static void notify_unlink(struct dentry *dentry, const char *file_id_str,
			  const char *special_directory)
{
	struct dentry *root = dentry;
	struct dentry *file = NULL;
	struct dentry *dir = NULL;
	int error = 0;
	bool take_lock = root->d_parent != root->d_parent->d_parent;

	while (root != root->d_parent)
		root = root->d_parent;

	if (take_lock)
		dir = incfs_lookup_dentry(root, special_directory);
	else
		dir = lookup_one_len(special_directory, root,
				     strlen(special_directory));

	if (IS_ERR(dir)) {
		error = PTR_ERR(dir);
		goto out;
	}
	if (d_is_negative(dir)) {
		error = -ENOENT;
		goto out;
	}

	file = incfs_lookup_dentry(dir, file_id_str);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto out;
	}
	if (d_is_negative(file)) {
		error = -ENOENT;
		goto out;
	}

	fsnotify_unlink(d_inode(dir), file);
	d_delete(file);

out:
	if (error)
		pr_warn("%s failed with error %d\n", __func__, error);

	dput(dir);
	dput(file);
}

static void maybe_delete_incomplete_file(struct file *f,
					 struct data_file *df)
{
	struct backing_file_context *bfc;
	struct mount_info *mi = df->df_mount_info;
	char *file_id_str = NULL;
	struct dentry *incomplete_file_dentry = NULL;
	const struct cred *old_cred = override_creds(mi->mi_owner);
	int error;

	if (atomic_read(&df->df_data_blocks_written) < df->df_data_block_count)
		goto out;

	/* Truncate file to remove any preallocated space */
	bfc = df->df_backing_file_context;
	if (bfc) {
		struct file *f = bfc->bc_file;

		if (f) {
			loff_t size = i_size_read(file_inode(f));

			error = vfs_truncate(&f->f_path, size);
			if (error)
				/* No useful action on failure */
				pr_warn("incfs: Failed to truncate complete file: %d\n",
					error);
		}
	}

	/* This is best effort - there is no useful action to take on failure */
	file_id_str = file_id_to_str(df->df_id);
	if (!file_id_str)
		goto out;

	incomplete_file_dentry = incfs_lookup_dentry(
					df->df_mount_info->mi_incomplete_dir,
					file_id_str);
	if (!incomplete_file_dentry || IS_ERR(incomplete_file_dentry)) {
		incomplete_file_dentry = NULL;
		goto out;
	}

	if (!d_really_is_positive(incomplete_file_dentry))
		goto out;

	vfs_fsync(df->df_backing_file_context->bc_file, 0);
	error = incfs_unlink(incomplete_file_dentry);
	if (error) {
		pr_warn("incfs: Deleting incomplete file failed: %d\n", error);
		goto out;
	}

	notify_unlink(f->f_path.dentry, file_id_str, INCFS_INCOMPLETE_NAME);

out:
	dput(incomplete_file_dentry);
	kfree(file_id_str);
	revert_creds(old_cred);
}

static long ioctl_fill_blocks(struct file *f, void __user *arg)
{
	struct incfs_fill_blocks __user *usr_fill_blocks = arg;
	struct incfs_fill_blocks fill_blocks;
	struct incfs_fill_block __user *usr_fill_block_array;
	struct data_file *df = get_incfs_data_file(f);
	struct incfs_file_data *fd = f->private_data;
	const ssize_t data_buf_size = 2 * INCFS_DATA_FILE_BLOCK_SIZE;
	u8 *data_buf = NULL;
	ssize_t error = 0;
	int i = 0;

	if (!df)
		return -EBADF;

	if (!fd || fd->fd_fill_permission != CAN_FILL)
		return -EPERM;

	if (copy_from_user(&fill_blocks, usr_fill_blocks, sizeof(fill_blocks)))
		return -EFAULT;

	usr_fill_block_array = u64_to_user_ptr(fill_blocks.fill_blocks);
	data_buf = (u8 *)__get_free_pages(GFP_NOFS | __GFP_COMP,
					  get_order(data_buf_size));
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

	maybe_delete_incomplete_file(f, df);

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

	sig_buffer = kzalloc(sig_buf_size, GFP_NOFS | __GFP_COMP);
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

static long ioctl_get_filled_blocks(struct file *f, void __user *arg)
{
	struct incfs_get_filled_blocks_args __user *args_usr_ptr = arg;
	struct incfs_get_filled_blocks_args args = {};
	struct data_file *df = get_incfs_data_file(f);
	struct incfs_file_data *fd = f->private_data;
	int error;

	if (!df || !fd)
		return -EINVAL;

	if (fd->fd_fill_permission != CAN_FILL)
		return -EPERM;

	if (copy_from_user(&args, args_usr_ptr, sizeof(args)) > 0)
		return -EINVAL;

	error = incfs_get_filled_blocks(df, fd, &args);

	if (copy_to_user(args_usr_ptr, &args, sizeof(args)))
		return -EFAULT;

	return error;
}

static long ioctl_get_block_count(struct file *f, void __user *arg)
{
	struct incfs_get_block_count_args __user *args_usr_ptr = arg;
	struct incfs_get_block_count_args args = {};
	struct data_file *df = get_incfs_data_file(f);

	if (!df)
		return -EINVAL;

	args.total_data_blocks_out = df->df_data_block_count;
	args.filled_data_blocks_out = atomic_read(&df->df_data_blocks_written);
	args.total_hash_blocks_out = df->df_total_block_count -
		df->df_data_block_count;
	args.filled_hash_blocks_out = atomic_read(&df->df_hash_blocks_written);

	if (copy_to_user(args_usr_ptr, &args, sizeof(args)))
		return -EFAULT;

	return 0;
}

static int incfs_ioctl_get_flags(struct file *f, void __user *arg)
{
	u32 flags = IS_VERITY(file_inode(f)) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

static long dispatch_ioctl(struct file *f, unsigned int req, unsigned long arg)
{
	switch (req) {
	case INCFS_IOC_FILL_BLOCKS:
		return ioctl_fill_blocks(f, (void __user *)arg);
	case INCFS_IOC_READ_FILE_SIGNATURE:
		return ioctl_read_file_signature(f, (void __user *)arg);
	case INCFS_IOC_GET_FILLED_BLOCKS:
		return ioctl_get_filled_blocks(f, (void __user *)arg);
	case INCFS_IOC_GET_BLOCK_COUNT:
		return ioctl_get_block_count(f, (void __user *)arg);
	case FS_IOC_ENABLE_VERITY:
		return incfs_ioctl_enable_verity(f, (const void __user *)arg);
	case FS_IOC_GETFLAGS:
		return incfs_ioctl_get_flags(f, (void __user *) arg);
	case FS_IOC_MEASURE_VERITY:
		return incfs_ioctl_measure_verity(f, (void __user *)arg);
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static long incfs_compat_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case INCFS_IOC_FILL_BLOCKS:
	case INCFS_IOC_READ_FILE_SIGNATURE:
	case INCFS_IOC_GET_FILLED_BLOCKS:
	case INCFS_IOC_GET_BLOCK_COUNT:
	case FS_IOC_ENABLE_VERITY:
	case FS_IOC_MEASURE_VERITY:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return dispatch_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

static struct dentry *dir_lookup(struct inode *dir_inode, struct dentry *dentry,
				 unsigned int flags)
{
	struct mount_info *mi = get_mount_info(dir_inode->i_sb);
	struct dentry *dir_dentry = NULL;
	struct dentry *backing_dentry = NULL;
	struct path dir_backing_path = {};
	struct inode_info *dir_info = get_incfs_node(dir_inode);
	int err = 0;

	if (!mi || !dir_info || !dir_info->n_backing_inode)
		return ERR_PTR(-EBADF);

	if (d_inode(mi->mi_backing_dir_path.dentry) ==
		dir_info->n_backing_inode) {
		/* We do lookup in the FS root. Show pseudo files. */
		err = dir_lookup_pseudo_files(dir_inode->i_sb, dentry);
		if (err != -ENOENT)
			goto out;
		err = 0;
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

static int dir_mkdir(struct user_namespace *ns, struct inode *dir, struct dentry *dentry, umode_t mode)
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
		goto path_err;
	}

	if (backing_dentry->d_parent == mi->mi_index_dir) {
		/* Can't create a subdir inside .index */
		err = -EBUSY;
		goto out;
	}

	if (backing_dentry->d_parent == mi->mi_incomplete_dir) {
		/* Can't create a subdir inside .incomplete */
		err = -EBUSY;
		goto out;
	}
	inode_lock_nested(dir_node->n_backing_inode, I_MUTEX_PARENT);
	err = vfs_mkdir(ns, dir_node->n_backing_inode, backing_dentry, mode | 0222);
	inode_unlock(dir_node->n_backing_inode);
	if (!err) {
		struct inode *inode = NULL;

		if (d_really_is_negative(backing_dentry) ||
			unlikely(d_unhashed(backing_dentry))) {
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

path_err:
	mutex_unlock(&mi->mi_dir_struct_mutex);
	if (err)
		pr_debug("incfs: %s err:%d\n", __func__, err);
	return err;
}

/*
 * Delete file referenced by backing_dentry and if appropriate its hardlink
 * from .index and .incomplete
 */
static int file_delete(struct mount_info *mi, struct dentry *dentry,
			struct dentry *backing_dentry, int nlink)
{
	struct dentry *index_file_dentry = NULL;
	struct dentry *incomplete_file_dentry = NULL;
	/* 2 chars per byte of file ID + 1 char for \0 */
	char file_id_str[2 * sizeof(incfs_uuid_t) + 1] = {0};
	ssize_t uuid_size = 0;
	int error = 0;

	WARN_ON(!mutex_is_locked(&mi->mi_dir_struct_mutex));

	if (nlink > 3)
		goto just_unlink;

	uuid_size = vfs_getxattr(&init_user_ns, backing_dentry, INCFS_XATTR_ID_NAME,
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
		index_file_dentry = NULL;
		goto out;
	}

	if (d_really_is_positive(index_file_dentry) && nlink > 0)
		nlink--;

	if (nlink > 2)
		goto just_unlink;

	incomplete_file_dentry = incfs_lookup_dentry(mi->mi_incomplete_dir,
						     file_id_str);
	if (IS_ERR(incomplete_file_dentry)) {
		error = PTR_ERR(incomplete_file_dentry);
		incomplete_file_dentry = NULL;
		goto out;
	}

	if (d_really_is_positive(incomplete_file_dentry) && nlink > 0)
		nlink--;

	if (nlink > 1)
		goto just_unlink;

	if (d_really_is_positive(index_file_dentry)) {
		error = incfs_unlink(index_file_dentry);
		if (error)
			goto out;
		notify_unlink(dentry, file_id_str, INCFS_INDEX_NAME);
	}

	if (d_really_is_positive(incomplete_file_dentry)) {
		error = incfs_unlink(incomplete_file_dentry);
		if (error)
			goto out;
		notify_unlink(dentry, file_id_str, INCFS_INCOMPLETE_NAME);
	}

just_unlink:
	error = incfs_unlink(backing_dentry);

out:
	dput(index_file_dentry);
	dput(incomplete_file_dentry);
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

	if (!mi)
		return -EBADF;

	err = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (err)
		return err;

	get_incfs_backing_path(dentry, &backing_path);
	if (!backing_path.dentry) {
		err = -EBADF;
		goto path_err;
	}

	if (backing_path.dentry->d_parent == mi->mi_index_dir) {
		/* Direct unlink from .index are not allowed. */
		err = -EBUSY;
		goto out;
	}

	if (backing_path.dentry->d_parent == mi->mi_incomplete_dir) {
		/* Direct unlink from .incomplete are not allowed. */
		err = -EBUSY;
		goto out;
	}

	err = vfs_getattr(&backing_path, &stat, STATX_NLINK,
			  AT_STATX_SYNC_AS_STAT);
	if (err)
		goto out;

	err = file_delete(mi, dentry, backing_path.dentry, stat.nlink);

	d_drop(dentry);
out:
	path_put(&backing_path);
path_err:
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

	if (!mi)
		return -EBADF;

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

	if (backing_new_path.dentry->d_parent == mi->mi_incomplete_dir) {
		/* Can't link to .incomplete */
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

	if (!mi)
		return -EBADF;

	err = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (err)
		return err;

	get_incfs_backing_path(dentry, &backing_path);
	if (!backing_path.dentry) {
		err = -EBADF;
		goto path_err;
	}

	if (backing_path.dentry == mi->mi_index_dir) {
		/* Can't delete .index */
		err = -EBUSY;
		goto out;
	}

	if (backing_path.dentry == mi->mi_incomplete_dir) {
		/* Can't delete .incomplete */
		err = -EBUSY;
		goto out;
	}

	err = incfs_rmdir(backing_path.dentry);
	if (!err)
		d_drop(dentry);
out:
	path_put(&backing_path);

path_err:
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
	struct renamedata rd = {};
	int error = 0;

	error = mutex_lock_interruptible(&mi->mi_dir_struct_mutex);
	if (error)
		return error;

	backing_old_dentry = get_incfs_dentry(old_dentry)->backing_path.dentry;

	if (!backing_old_dentry || backing_old_dentry == mi->mi_index_dir ||
	    backing_old_dentry == mi->mi_incomplete_dir) {
		/* Renaming .index or .incomplete not allowed */
		error = -EBUSY;
		goto exit;
	}

	backing_new_dentry = get_incfs_dentry(new_dentry)->backing_path.dentry;
	dget(backing_old_dentry);
	dget(backing_new_dentry);

	backing_old_dir_dentry = dget_parent(backing_old_dentry);
	backing_new_dir_dentry = dget_parent(backing_new_dentry);
	target_inode = d_inode(new_dentry);

	if (backing_old_dir_dentry == mi->mi_index_dir ||
	    backing_old_dir_dentry == mi->mi_incomplete_dir) {
		/* Direct moves from .index or .incomplete are not allowed. */
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

	rd.old_dir	= d_inode(backing_old_dir_dentry);
	rd.old_dentry	= backing_old_dentry;
	rd.new_dir	= d_inode(backing_new_dir_dentry);
	rd.new_dentry	= backing_new_dentry;
	error = vfs_rename(&rd);
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

exit:
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
	int flags = O_NOATIME | O_LARGEFILE |
		(S_ISDIR(inode->i_mode) ? O_RDONLY : O_RDWR);
	const struct cred *old_cred;

	WARN_ON(file->private_data);

	if (!mi)
		return -EBADF;

	get_incfs_backing_path(file->f_path.dentry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	old_cred = override_creds(mi->mi_owner);
	backing_file = dentry_open(&backing_path, flags, current_cred());
	revert_creds(old_cred);
	path_put(&backing_path);

	if (IS_ERR(backing_file)) {
		err = PTR_ERR(backing_file);
		backing_file = NULL;
		goto out;
	}

	if (S_ISREG(inode->i_mode)) {
		struct incfs_file_data *fd = kzalloc(sizeof(*fd), GFP_NOFS);

		if (!fd) {
			err = -ENOMEM;
			goto out;
		}

		*fd = (struct incfs_file_data) {
			.fd_fill_permission = CANT_FILL,
		};
		file->private_data = fd;

		err = make_inode_ready_for_data_ops(mi, inode, backing_file);
		if (err)
			goto out;

		err = incfs_fsverity_file_open(inode, file);
		if (err)
			goto out;
	} else if (S_ISDIR(inode->i_mode)) {
		struct dir_file *dir = NULL;

		dir = incfs_open_dir_file(mi, backing_file);
		if (IS_ERR(dir))
			err = PTR_ERR(dir);
		else
			file->private_data = dir;
	} else
		err = -EBADF;

out:
	if (err) {
		pr_debug("name:%s err: %d\n",
			 file->f_path.dentry->d_name.name, err);
		if (S_ISREG(inode->i_mode))
			kfree(file->private_data);
		else if (S_ISDIR(inode->i_mode))
			incfs_free_dir_file(file->private_data);

		file->private_data = NULL;
	}

	if (backing_file)
		fput(backing_file);
	return err;
}

static int file_release(struct inode *inode, struct file *file)
{
	if (S_ISREG(inode->i_mode)) {
		kfree(file->private_data);
		file->private_data = NULL;
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
	kfree(d->d_fsdata);
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

static int incfs_setattr(struct user_namespace *ns, struct dentry *dentry,
			 struct iattr *ia)
{
	struct dentry_info *di = get_incfs_dentry(dentry);
	struct dentry *backing_dentry;
	struct inode *backing_inode;
	int error;

	if (ia->ia_valid & ATTR_SIZE)
		return -EINVAL;

	if (!di)
		return -EINVAL;
	backing_dentry = di->backing_path.dentry;
	if (!backing_dentry)
		return -EINVAL;

	backing_inode = d_inode(backing_dentry);

	/* incfs files are readonly, but the backing files must be writeable */
	if (S_ISREG(backing_inode->i_mode)) {
		if ((ia->ia_valid & ATTR_MODE) && (ia->ia_mode & 0222))
			return -EINVAL;

		ia->ia_mode |= 0222;
	}

	inode_lock(d_inode(backing_dentry));
	error = notify_change(ns, backing_dentry, ia, NULL);
	inode_unlock(d_inode(backing_dentry));

	if (error)
		return error;

	if (S_ISREG(backing_inode->i_mode))
		ia->ia_mode &= ~0222;

	return simple_setattr(ns, dentry, ia);
}


static int incfs_getattr(struct user_namespace *ns, const struct path *path,
			 struct kstat *stat, u32 request_mask,
			 unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);

	if (IS_VERITY(inode))
		stat->attributes |= STATX_ATTR_VERITY;
	stat->attributes_mask |= STATX_ATTR_VERITY;
	generic_fillattr(ns, inode, stat);

	/*
	 * TODO: stat->blocks is wrong at this point. It should be number of
	 * blocks in the backing file. But that information is not (necessarily)
	 * available yet - incfs_open_dir_file may not have been called.
	 * Solution is probably to store the backing file block count in an
	 * xattr whenever it's changed.
	 */

	return 0;
}

static ssize_t incfs_getxattr(struct dentry *d, const char *name,
			void *value, size_t size)
{
	struct dentry_info *di = get_incfs_dentry(d);
	struct mount_info *mi = get_mount_info(d->d_sb);
	char *stored_value;
	size_t stored_size;
	int i;

	if (di && di->backing_path.dentry)
		return vfs_getxattr(&init_user_ns, di->backing_path.dentry, name, value, size);

	if (strcmp(name, "security.selinux"))
		return -ENODATA;

	for (i = 0; i < PSEUDO_FILE_COUNT; ++i)
		if (!strcmp(d->d_iname, incfs_pseudo_file_names[i].data))
			break;
	if (i == PSEUDO_FILE_COUNT)
		return -ENODATA;

	stored_value = mi->pseudo_file_xattr[i].data;
	stored_size = mi->pseudo_file_xattr[i].len;
	if (!stored_value)
		return -ENODATA;

	if (stored_size > size)
		return -E2BIG;

	memcpy(value, stored_value, stored_size);
	return stored_size;
}


static ssize_t incfs_setxattr(struct user_namespace *ns, struct dentry *d,
			      const char *name, const void *value, size_t size,
			      int flags)
{
	struct dentry_info *di = get_incfs_dentry(d);
	struct mount_info *mi = get_mount_info(d->d_sb);
	u8 **stored_value;
	size_t *stored_size;
	int i;

	if (di && di->backing_path.dentry)
		return vfs_setxattr(ns, di->backing_path.dentry, name, value,
				    size, flags);

	if (strcmp(name, "security.selinux"))
		return -ENODATA;

	if (size > INCFS_MAX_FILE_ATTR_SIZE)
		return -E2BIG;

	for (i = 0; i < PSEUDO_FILE_COUNT; ++i)
		if (!strcmp(d->d_iname, incfs_pseudo_file_names[i].data))
			break;
	if (i == PSEUDO_FILE_COUNT)
		return -ENODATA;

	stored_value = &mi->pseudo_file_xattr[i].data;
	stored_size = &mi->pseudo_file_xattr[i].len;
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
	struct dentry *index_dir = NULL;
	struct dentry *incomplete_dir = NULL;
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

	index_dir = open_or_create_special_dir(backing_dir_path.dentry,
					       INCFS_INDEX_NAME);
	if (IS_ERR_OR_NULL(index_dir)) {
		error = PTR_ERR(index_dir);
		pr_err("incfs: Can't find or create .index dir in %s\n",
			dev_name);
		/* No need to null index_dir since we don't put it */
		goto err;
	}
	mi->mi_index_dir = index_dir;

	incomplete_dir = open_or_create_special_dir(backing_dir_path.dentry,
						    INCFS_INCOMPLETE_NAME);
	if (IS_ERR_OR_NULL(incomplete_dir)) {
		error = PTR_ERR(incomplete_dir);
		pr_err("incfs: Can't find or create .incomplete dir in %s\n",
			dev_name);
		/* No need to null incomplete_dir since we don't put it */
		goto err;
	}
	mi->mi_incomplete_dir = incomplete_dir;

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

	pr_debug("incfs: mount\n");
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

	if (options.report_uid != mi->mi_options.report_uid) {
		pr_err("incfs: Can't change report_uid mount option on remount\n");
		return -EOPNOTSUPP;
	}

	err = incfs_realloc_mount_info(mi, &options);
	if (err)
		return err;

	pr_debug("incfs: remount\n");
	return 0;
}

void incfs_kill_sb(struct super_block *sb)
{
	struct mount_info *mi = sb->s_fs_info;

	pr_debug("incfs: unmount\n");
	generic_shutdown_super(sb);
	incfs_free_mount_info(mi);
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
	if (mi->mi_options.report_uid)
		seq_puts(m, ",report_uid");
	return 0;
}
