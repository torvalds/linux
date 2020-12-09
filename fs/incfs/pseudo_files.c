// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/syscalls.h>

#include <uapi/linux/incrementalfs.h>

#include "pseudo_files.h"

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "vfs.h"

#define INCFS_PENDING_READS_INODE 2
#define INCFS_LOG_INODE 3
#define INCFS_BLOCKS_WRITTEN_INODE 4
#define READ_WRITE_FILE_MODE 0666

/*******************************************************************************
 * .log pseudo file definition
 ******************************************************************************/
static const char log_file_name[] = INCFS_LOG_FILENAME;
static const struct mem_range log_file_name_range = {
	.data = (u8 *)log_file_name,
	.len = ARRAY_SIZE(log_file_name) - 1
};

/* State of an open .log file, unique for each file descriptor. */
struct log_file_state {
	struct read_log_state state;
};

static ssize_t log_read(struct file *f, char __user *buf, size_t len,
			loff_t *ppos)
{
	struct log_file_state *log_state = f->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(f));
	int total_reads_collected = 0;
	int rl_size;
	ssize_t result = 0;
	bool report_uid;
	unsigned long page = 0;
	struct incfs_pending_read_info *reads_buf = NULL;
	struct incfs_pending_read_info2 *reads_buf2 = NULL;
	size_t record_size;
	ssize_t reads_to_collect;
	ssize_t reads_per_page;

	if (!mi)
		return -EFAULT;

	report_uid = mi->mi_options.report_uid;
	record_size = report_uid ? sizeof(*reads_buf2) : sizeof(*reads_buf);
	reads_to_collect = len / record_size;
	reads_per_page = PAGE_SIZE / record_size;

	rl_size = READ_ONCE(mi->mi_log.rl_size);
	if (rl_size == 0)
		return 0;

	page = __get_free_page(GFP_NOFS);
	if (!page)
		return -ENOMEM;

	if (report_uid)
		reads_buf2 = (struct incfs_pending_read_info2 *) page;
	else
		reads_buf = (struct incfs_pending_read_info *) page;

	reads_to_collect = min_t(ssize_t, rl_size, reads_to_collect);
	while (reads_to_collect > 0) {
		struct read_log_state next_state;
		int reads_collected;

		memcpy(&next_state, &log_state->state, sizeof(next_state));
		reads_collected = incfs_collect_logged_reads(
			mi, &next_state, reads_buf, reads_buf2,
			min_t(ssize_t, reads_to_collect, reads_per_page));
		if (reads_collected <= 0) {
			result = total_reads_collected ?
					 total_reads_collected * record_size :
					 reads_collected;
			goto out;
		}
		if (copy_to_user(buf, (void *) page,
				 reads_collected * record_size)) {
			result = total_reads_collected ?
					 total_reads_collected * record_size :
					 -EFAULT;
			goto out;
		}

		memcpy(&log_state->state, &next_state, sizeof(next_state));
		total_reads_collected += reads_collected;
		buf += reads_collected * record_size;
		reads_to_collect -= reads_collected;
	}

	result = total_reads_collected * record_size;
	*ppos = 0;
out:
	free_page(page);
	return result;
}

static __poll_t log_poll(struct file *file, poll_table *wait)
{
	struct log_file_state *log_state = file->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(file));
	int count;
	__poll_t ret = 0;

	poll_wait(file, &mi->mi_log.ml_notif_wq, wait);
	count = incfs_get_uncollected_logs_count(mi, &log_state->state);
	if (count >= mi->mi_options.read_log_wakeup_count)
		ret = EPOLLIN | EPOLLRDNORM;

	return ret;
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

static const struct file_operations incfs_log_file_ops = {
	.read = log_read,
	.poll = log_poll,
	.open = log_open,
	.release = log_release,
	.llseek = noop_llseek,
};

/*******************************************************************************
 * .pending_reads pseudo file definition
 ******************************************************************************/
static const char pending_reads_file_name[] = INCFS_PENDING_READS_FILENAME;
static const struct mem_range pending_reads_file_name_range = {
	.data = (u8 *)pending_reads_file_name,
	.len = ARRAY_SIZE(pending_reads_file_name) - 1
};

/* State of an open .pending_reads file, unique for each file descriptor. */
struct pending_reads_state {
	/* A serial number of the last pending read obtained from this file. */
	int last_pending_read_sn;
};

static ssize_t pending_reads_read(struct file *f, char __user *buf, size_t len,
			    loff_t *ppos)
{
	struct pending_reads_state *pr_state = f->private_data;
	struct mount_info *mi = get_mount_info(file_superblock(f));
	bool report_uid;
	unsigned long page = 0;
	struct incfs_pending_read_info *reads_buf = NULL;
	struct incfs_pending_read_info2 *reads_buf2 = NULL;
	size_t record_size;
	size_t reads_to_collect;
	int last_known_read_sn = READ_ONCE(pr_state->last_pending_read_sn);
	int new_max_sn = last_known_read_sn;
	int reads_collected = 0;
	ssize_t result = 0;

	if (!mi)
		return -EFAULT;

	report_uid = mi->mi_options.report_uid;
	record_size = report_uid ? sizeof(*reads_buf2) : sizeof(*reads_buf);
	reads_to_collect = len / record_size;

	if (!incfs_fresh_pending_reads_exist(mi, last_known_read_sn))
		return 0;

	page = get_zeroed_page(GFP_NOFS);
	if (!page)
		return -ENOMEM;

	if (report_uid)
		reads_buf2 = (struct incfs_pending_read_info2 *) page;
	else
		reads_buf = (struct incfs_pending_read_info *) page;

	reads_to_collect =
		min_t(size_t, PAGE_SIZE / record_size, reads_to_collect);

	reads_collected = incfs_collect_pending_reads(mi, last_known_read_sn,
				reads_buf, reads_buf2, reads_to_collect,
				&new_max_sn);

	if (reads_collected < 0) {
		result = reads_collected;
		goto out;
	}

	/*
	 * Just to make sure that we don't accidentally copy more data
	 * to reads buffer than userspace can handle.
	 */
	reads_collected = min_t(size_t, reads_collected, reads_to_collect);
	result = reads_collected * record_size;

	/* Copy reads info to the userspace buffer */
	if (copy_to_user(buf, (void *)page, result)) {
		result = -EFAULT;
		goto out;
	}

	WRITE_ONCE(pr_state->last_pending_read_sn, new_max_sn);
	*ppos = 0;

out:
	free_page(page);
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

static long ioctl_permit_fill(struct file *f, void __user *arg)
{
	struct incfs_permit_fill __user *usr_permit_fill = arg;
	struct incfs_permit_fill permit_fill;
	long error = 0;
	struct file *file = NULL;
	struct incfs_file_data *fd;

	if (copy_from_user(&permit_fill, usr_permit_fill, sizeof(permit_fill)))
		return -EFAULT;

	file = fget(permit_fill.file_descriptor);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (file->f_op != &incfs_file_ops) {
		error = -EPERM;
		goto out;
	}

	if (file->f_inode->i_sb != f->f_inode->i_sb) {
		error = -EPERM;
		goto out;
	}

	fd = file->private_data;

	switch (fd->fd_fill_permission) {
	case CANT_FILL:
		fd->fd_fill_permission = CAN_FILL;
		break;

	case CAN_FILL:
		pr_debug("CAN_FILL already set");
		break;

	default:
		pr_warn("Invalid file private data");
		error = -EFAULT;
		goto out;
	}

out:
	fput(file);
	return error;
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

static bool incfs_equal_ranges(struct mem_range lhs, struct mem_range rhs)
{
	if (lhs.len != rhs.len)
		return false;
	return memcmp(lhs.data, rhs.data, lhs.len) == 0;
}

static bool is_pseudo_filename(struct mem_range name)
{
	if (incfs_equal_ranges(pending_reads_file_name_range, name))
		return true;
	if (incfs_equal_ranges(log_file_name_range, name))
		return true;

	return false;
}

static int validate_name(char *file_name)
{
	struct mem_range name = range(file_name, strlen(file_name));
	int i = 0;

	if (name.len > INCFS_MAX_NAME_LEN)
		return -ENAMETOOLONG;

	if (is_pseudo_filename(name))
		return -EINVAL;

	for (i = 0; i < name.len; i++)
		if (name.data[i] == '/')
			return -EINVAL;

	return 0;
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

static struct mem_range incfs_copy_signature_info_from_user(u8 __user *original,
							    u64 size)
{
	u8 *result;

	if (!original)
		return range(NULL, 0);

	if (size > INCFS_MAX_SIGNATURE_SIZE)
		return range(ERR_PTR(-EFAULT), 0);

	result = kzalloc(size, GFP_NOFS | __GFP_COMP);
	if (!result)
		return range(ERR_PTR(-ENOMEM), 0);

	if (copy_from_user(result, original, size)) {
		kfree(result);
		return range(ERR_PTR(-EFAULT), 0);
	}

	return range(result, size);
}

static int init_new_file(struct mount_info *mi, struct dentry *dentry,
			 incfs_uuid_t *uuid, u64 size, struct mem_range attr,
			 u8 __user *user_signature_info, u64 signature_size)
{
	struct path path = {};
	struct file *new_file;
	int error = 0;
	struct backing_file_context *bfc = NULL;
	u32 block_count;
	struct mem_range raw_signature = { NULL };
	struct mtree *hash_tree = NULL;

	if (!mi || !dentry || !uuid)
		return -EFAULT;

	/* Resize newly created file to its true size. */
	path = (struct path) {
		.mnt = mi->mi_backing_dir_path.mnt,
		.dentry = dentry
	};
	new_file = dentry_open(&path, O_RDWR | O_NOATIME | O_LARGEFILE,
			       mi->mi_owner);

	if (IS_ERR(new_file)) {
		error = PTR_ERR(new_file);
		goto out;
	}

	bfc = incfs_alloc_bfc(mi, new_file);
	fput(new_file);
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

	if (user_signature_info) {
		raw_signature = incfs_copy_signature_info_from_user(
			user_signature_info, signature_size);

		if (IS_ERR(raw_signature.data)) {
			error = PTR_ERR(raw_signature.data);
			raw_signature.data = NULL;
			goto out;
		}

		hash_tree = incfs_alloc_mtree(raw_signature, block_count);
		if (IS_ERR(hash_tree)) {
			error = PTR_ERR(hash_tree);
			hash_tree = NULL;
			goto out;
		}

		error = incfs_write_signature_to_backing_file(
			bfc, raw_signature, hash_tree->hash_tree_area_size);
		if (error)
			goto out;

		block_count += get_blocks_count_for_size(
			hash_tree->hash_tree_area_size);
	}

	if (block_count)
		error = incfs_write_blockmap_to_backing_file(bfc, block_count);

	if (error)
		goto out;
out:
	if (bfc) {
		mutex_unlock(&bfc->bc_mutex);
		incfs_free_bfc(bfc);
	}
	incfs_free_mtree(hash_tree);
	kfree(raw_signature.data);

	if (error)
		pr_debug("incfs: %s error: %d\n", __func__, error);
	return error;
}

static long ioctl_create_file(struct mount_info *mi,
			struct incfs_new_file_args __user *usr_args)
{
	struct incfs_new_file_args args;
	char *file_id_str = NULL;
	struct dentry *index_file_dentry = NULL;
	struct dentry *named_file_dentry = NULL;
	struct dentry *incomplete_file_dentry = NULL;
	struct path parent_dir_path = {};
	struct inode *index_dir_inode = NULL;
	__le64 size_attr_value = 0;
	char *file_name = NULL;
	char *attr_value = NULL;
	int error = 0;
	bool locked = false;
	bool index_linked = false;
	bool name_linked = false;
	bool incomplete_linked = false;

	if (!mi || !mi->mi_index_dir || !mi->mi_incomplete_dir) {
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

	if (parent_dir_path.dentry == mi->mi_incomplete_dir) {
		/* Can't create a file directly inside .incomplete */
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

	/* Look up a dentry in the incomplete dir. It should be negative. */
	incomplete_file_dentry = incfs_lookup_dentry(mi->mi_incomplete_dir,
					file_id_str);
	if (!incomplete_file_dentry) {
		error = -EFAULT;
		goto out;
	}
	if (IS_ERR(incomplete_file_dentry)) {
		error = PTR_ERR(incomplete_file_dentry);
		incomplete_file_dentry = NULL;
		goto out;
	}
	if (d_really_is_positive(incomplete_file_dentry)) {
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
		goto out;
	}

	/* Save the file's ID as an xattr for easy fetching in future. */
	error = vfs_setxattr(index_file_dentry, INCFS_XATTR_ID_NAME,
		file_id_str, strlen(file_id_str), XATTR_CREATE);
	if (error) {
		pr_debug("incfs: vfs_setxattr err:%d\n", error);
		goto out;
	}

	/* Save the file's size as an xattr for easy fetching in future. */
	size_attr_value = cpu_to_le64(args.size);
	error = vfs_setxattr(index_file_dentry, INCFS_XATTR_SIZE_NAME,
		(char *)&size_attr_value, sizeof(size_attr_value),
		XATTR_CREATE);
	if (error) {
		pr_debug("incfs: vfs_setxattr err:%d\n", error);
		goto out;
	}

	/* Save the file's attribute as an xattr */
	if (args.file_attr_len && args.file_attr) {
		if (args.file_attr_len > INCFS_MAX_FILE_ATTR_SIZE) {
			error = -E2BIG;
			goto out;
		}

		attr_value = kmalloc(args.file_attr_len, GFP_NOFS);
		if (!attr_value) {
			error = -ENOMEM;
			goto out;
		}

		if (copy_from_user(attr_value,
				u64_to_user_ptr(args.file_attr),
				args.file_attr_len) > 0) {
			error = -EFAULT;
			goto out;
		}

		error = vfs_setxattr(index_file_dentry,
				INCFS_XATTR_METADATA_NAME,
				attr_value, args.file_attr_len,
				XATTR_CREATE);

		if (error)
			goto out;
	}

	/* Initializing a newly created file. */
	error = init_new_file(mi, index_file_dentry, &args.file_id, args.size,
			      range(attr_value, args.file_attr_len),
			      (u8 __user *)args.signature_info,
			      args.signature_size);
	if (error)
		goto out;
	index_linked = true;

	/* Linking a file with its real name from the requested dir. */
	error = incfs_link(index_file_dentry, named_file_dentry);
	if (error)
		goto out;
	name_linked = true;

	if (args.size) {
		/* Linking a file with its incomplete entry */
		error = incfs_link(index_file_dentry, incomplete_file_dentry);
		if (error)
			goto out;
		incomplete_linked = true;
	}

out:
	if (error) {
		pr_debug("incfs: %s err:%d\n", __func__, error);
		if (index_linked)
			incfs_unlink(index_file_dentry);
		if (name_linked)
			incfs_unlink(named_file_dentry);
		if (incomplete_linked)
			incfs_unlink(incomplete_file_dentry);
	}

	kfree(file_id_str);
	kfree(file_name);
	kfree(attr_value);
	dput(named_file_dentry);
	dput(index_file_dentry);
	dput(incomplete_file_dentry);
	path_put(&parent_dir_path);
	if (locked)
		mutex_unlock(&mi->mi_dir_struct_mutex);
	return error;
}

static int init_new_mapped_file(struct mount_info *mi, struct dentry *dentry,
			 incfs_uuid_t *uuid, u64 size, u64 offset)
{
	struct path path = {};
	struct file *new_file;
	int error = 0;
	struct backing_file_context *bfc = NULL;

	if (!mi || !dentry || !uuid)
		return -EFAULT;

	/* Resize newly created file to its true size. */
	path = (struct path) {
		.mnt = mi->mi_backing_dir_path.mnt,
		.dentry = dentry
	};
	new_file = dentry_open(&path, O_RDWR | O_NOATIME | O_LARGEFILE,
			       mi->mi_owner);

	if (IS_ERR(new_file)) {
		error = PTR_ERR(new_file);
		goto out;
	}

	bfc = incfs_alloc_bfc(mi, new_file);
	fput(new_file);
	if (IS_ERR(bfc)) {
		error = PTR_ERR(bfc);
		bfc = NULL;
		goto out;
	}

	mutex_lock(&bfc->bc_mutex);
	error = incfs_write_mapping_fh_to_backing_file(bfc, uuid, size, offset);
	if (error)
		goto out;

out:
	if (bfc) {
		mutex_unlock(&bfc->bc_mutex);
		incfs_free_bfc(bfc);
	}

	if (error)
		pr_debug("incfs: %s error: %d\n", __func__, error);
	return error;
}

static long ioctl_create_mapped_file(struct mount_info *mi, void __user *arg)
{
	struct incfs_create_mapped_file_args __user *args_usr_ptr = arg;
	struct incfs_create_mapped_file_args args = {};
	char *file_name;
	int error = 0;
	struct path parent_dir_path = {};
	char *source_file_name = NULL;
	struct dentry *source_file_dentry = NULL;
	u64 source_file_size;
	struct dentry *file_dentry = NULL;
	struct inode *parent_inode;
	__le64 size_attr_value;

	if (copy_from_user(&args, args_usr_ptr, sizeof(args)) > 0)
		return -EINVAL;

	file_name = strndup_user(u64_to_user_ptr(args.file_name), PATH_MAX);
	if (IS_ERR(file_name)) {
		error = PTR_ERR(file_name);
		file_name = NULL;
		goto out;
	}

	error = validate_name(file_name);
	if (error)
		goto out;

	if (args.source_offset % INCFS_DATA_FILE_BLOCK_SIZE) {
		error = -EINVAL;
		goto out;
	}

	/* Validate file mapping is in range */
	source_file_name = file_id_to_str(args.source_file_id);
	if (!source_file_name) {
		pr_warn("Failed to alloc source_file_name\n");
		error = -ENOMEM;
		goto out;
	}

	source_file_dentry = incfs_lookup_dentry(mi->mi_index_dir,
						       source_file_name);
	if (!source_file_dentry) {
		pr_warn("Source file does not exist\n");
		error = -EINVAL;
		goto out;
	}
	if (IS_ERR(source_file_dentry)) {
		pr_warn("Error opening source file\n");
		error = PTR_ERR(source_file_dentry);
		source_file_dentry = NULL;
		goto out;
	}
	if (!d_really_is_positive(source_file_dentry)) {
		pr_warn("Source file dentry negative\n");
		error = -EINVAL;
		goto out;
	}

	error = vfs_getxattr(source_file_dentry, INCFS_XATTR_SIZE_NAME,
			     (char *)&size_attr_value, sizeof(size_attr_value));
	if (error < 0)
		goto out;

	if (error != sizeof(size_attr_value)) {
		pr_warn("Mapped file has no size attr\n");
		error = -EINVAL;
		goto out;
	}

	source_file_size = le64_to_cpu(size_attr_value);
	if (args.source_offset + args.size > source_file_size) {
		pr_warn("Mapped file out of range\n");
		error = -EINVAL;
		goto out;
	}

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
	file_dentry = incfs_lookup_dentry(parent_dir_path.dentry,
					file_name);
	if (!file_dentry) {
		error = -EFAULT;
		goto out;
	}
	if (IS_ERR(file_dentry)) {
		error = PTR_ERR(file_dentry);
		file_dentry = NULL;
		goto out;
	}
	if (d_really_is_positive(file_dentry)) {
		error = -EEXIST;
		goto out;
	}

	parent_inode = d_inode(parent_dir_path.dentry);
	inode_lock_nested(parent_inode, I_MUTEX_PARENT);
	error = vfs_create(parent_inode, file_dentry, args.mode | 0222, true);
	inode_unlock(parent_inode);
	if (error)
		goto out;

	/* Save the file's size as an xattr for easy fetching in future. */
	size_attr_value = cpu_to_le64(args.size);
	error = vfs_setxattr(file_dentry, INCFS_XATTR_SIZE_NAME,
		(char *)&size_attr_value, sizeof(size_attr_value),
		XATTR_CREATE);
	if (error) {
		pr_debug("incfs: vfs_setxattr err:%d\n", error);
		goto delete_file;
	}

	error = init_new_mapped_file(mi, file_dentry, &args.source_file_id,
			args.size, args.source_offset);
	if (error)
		goto delete_file;

	goto out;

delete_file:
	incfs_unlink(file_dentry);

out:
	dput(file_dentry);
	dput(source_file_dentry);
	path_put(&parent_dir_path);
	kfree(file_name);
	kfree(source_file_name);
	return error;
}

static long ioctl_get_read_timeouts(struct mount_info *mi, void __user *arg)
{
	struct incfs_get_read_timeouts_args __user *args_usr_ptr = arg;
	struct incfs_get_read_timeouts_args args = {};
	int error = 0;
	struct incfs_per_uid_read_timeouts *buffer;
	int size;

	if (copy_from_user(&args, args_usr_ptr, sizeof(args)))
		return -EINVAL;

	if (args.timeouts_array_size_out > INCFS_DATA_FILE_BLOCK_SIZE)
		return -EINVAL;

	buffer = kzalloc(args.timeouts_array_size_out, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	spin_lock(&mi->mi_per_uid_read_timeouts_lock);
	size = mi->mi_per_uid_read_timeouts_size;
	if (args.timeouts_array_size < size)
		error = -E2BIG;
	else if (size)
		memcpy(buffer, mi->mi_per_uid_read_timeouts, size);
	spin_unlock(&mi->mi_per_uid_read_timeouts_lock);

	args.timeouts_array_size_out = size;
	if (!error && size)
		if (copy_to_user(u64_to_user_ptr(args.timeouts_array), buffer,
				 size))
			error = -EFAULT;

	if (!error || error == -E2BIG)
		if (copy_to_user(args_usr_ptr, &args, sizeof(args)) > 0)
			error = -EFAULT;

	kfree(buffer);
	return error;
}

static long ioctl_set_read_timeouts(struct mount_info *mi, void __user *arg)
{
	struct incfs_set_read_timeouts_args __user *args_usr_ptr = arg;
	struct incfs_set_read_timeouts_args args = {};
	int error = 0;
	int size;
	struct incfs_per_uid_read_timeouts *buffer = NULL, *tmp;
	int i;

	if (copy_from_user(&args, args_usr_ptr, sizeof(args)))
		return -EINVAL;

	size = args.timeouts_array_size;
	if (size) {
		if (size > INCFS_DATA_FILE_BLOCK_SIZE ||
		    size % sizeof(*buffer) != 0)
			return -EINVAL;

		buffer = kzalloc(size, GFP_NOFS);
		if (!buffer)
			return -ENOMEM;

		if (copy_from_user(buffer, u64_to_user_ptr(args.timeouts_array),
				   size)) {
			error = -EINVAL;
			goto out;
		}

		for (i = 0; i < size / sizeof(*buffer); ++i) {
			struct incfs_per_uid_read_timeouts *t = &buffer[i];

			if (t->min_pending_time_us > t->max_pending_time_us) {
				error = -EINVAL;
				goto out;
			}
		}
	}

	spin_lock(&mi->mi_per_uid_read_timeouts_lock);
	mi->mi_per_uid_read_timeouts_size = size;
	tmp = mi->mi_per_uid_read_timeouts;
	mi->mi_per_uid_read_timeouts = buffer;
	buffer = tmp;
	spin_unlock(&mi->mi_per_uid_read_timeouts_lock);

out:
	kfree(buffer);
	return error;
}

static long pending_reads_dispatch_ioctl(struct file *f, unsigned int req,
					unsigned long arg)
{
	struct mount_info *mi = get_mount_info(file_superblock(f));

	switch (req) {
	case INCFS_IOC_CREATE_FILE:
		return ioctl_create_file(mi, (void __user *)arg);
	case INCFS_IOC_PERMIT_FILL:
		return ioctl_permit_fill(f, (void __user *)arg);
	case INCFS_IOC_CREATE_MAPPED_FILE:
		return ioctl_create_mapped_file(mi, (void __user *)arg);
	case INCFS_IOC_GET_READ_TIMEOUTS:
		return ioctl_get_read_timeouts(mi, (void __user *)arg);
	case INCFS_IOC_SET_READ_TIMEOUTS:
		return ioctl_set_read_timeouts(mi, (void __user *)arg);
	default:
		return -EINVAL;
	}
}

static const struct file_operations incfs_pending_read_file_ops = {
	.read = pending_reads_read,
	.poll = pending_reads_poll,
	.open = pending_reads_open,
	.release = pending_reads_release,
	.llseek = noop_llseek,
	.unlocked_ioctl = pending_reads_dispatch_ioctl,
	.compat_ioctl = pending_reads_dispatch_ioctl
};

/*******************************************************************************
 * .blocks_written pseudo file definition
 ******************************************************************************/
static const char blocks_written_file_name[] = INCFS_BLOCKS_WRITTEN_FILENAME;
static const struct mem_range blocks_written_file_name_range = {
	.data = (u8 *)blocks_written_file_name,
	.len = ARRAY_SIZE(blocks_written_file_name) - 1
};

/* State of an open .blocks_written file, unique for each file descriptor. */
struct blocks_written_file_state {
	unsigned long blocks_written;
};

static ssize_t blocks_written_read(struct file *f, char __user *buf, size_t len,
			loff_t *ppos)
{
	struct mount_info *mi = get_mount_info(file_superblock(f));
	struct blocks_written_file_state *state = f->private_data;
	unsigned long blocks_written;
	char string[21];
	int result = 0;

	if (!mi)
		return -EFAULT;

	blocks_written = atomic_read(&mi->mi_blocks_written);
	if (state->blocks_written == blocks_written)
		return 0;

	result = snprintf(string, sizeof(string), "%lu", blocks_written);
	if (result > len)
		result = len;
	if (copy_to_user(buf, string, result))
		return -EFAULT;

	state->blocks_written = blocks_written;
	return result;
}

static __poll_t blocks_written_poll(struct file *f, poll_table *wait)
{
	struct mount_info *mi = get_mount_info(file_superblock(f));
	struct blocks_written_file_state *state = f->private_data;
	unsigned long blocks_written;

	if (!mi)
		return 0;

	poll_wait(f, &mi->mi_blocks_written_notif_wq, wait);
	blocks_written = atomic_read(&mi->mi_blocks_written);
	if (state->blocks_written == blocks_written)
		return 0;

	return EPOLLIN | EPOLLRDNORM;
}

static int blocks_written_open(struct inode *inode, struct file *file)
{
	struct blocks_written_file_state *state =
		kzalloc(sizeof(*state), GFP_NOFS);

	if (!state)
		return -ENOMEM;

	state->blocks_written = -1;
	file->private_data = state;
	return 0;
}

static int blocks_written_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations incfs_blocks_written_file_ops = {
	.read = blocks_written_read,
	.poll = blocks_written_poll,
	.open = blocks_written_open,
	.release = blocks_written_release,
	.llseek = noop_llseek,
};

/*******************************************************************************
 * Generic inode lookup functionality
 ******************************************************************************/
static bool get_pseudo_inode(int ino, struct inode *inode)
{
	inode->i_ctime = (struct timespec64){};
	inode->i_mtime = inode->i_ctime;
	inode->i_atime = inode->i_ctime;
	inode->i_size = 0;
	inode->i_ino = ino;
	inode->i_private = NULL;
	inode_init_owner(inode, NULL, S_IFREG | READ_WRITE_FILE_MODE);
	inode->i_op = &incfs_file_inode_ops;

	switch (ino) {
	case INCFS_PENDING_READS_INODE:
		inode->i_fop = &incfs_pending_read_file_ops;
		return true;

	case INCFS_LOG_INODE:
		inode->i_fop = &incfs_log_file_ops;
		return true;

	case INCFS_BLOCKS_WRITTEN_INODE:
		inode->i_fop = &incfs_blocks_written_file_ops;
		return true;

	default:
		return false;
	}
}

struct inode_search {
	unsigned long ino;
};

static int inode_test(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;

	return inode->i_ino == search->ino;
}

static int inode_set(struct inode *inode, void *opaque)
{
	struct inode_search *search = opaque;

	if (get_pseudo_inode(search->ino, inode))
		return 0;

	/* Unknown inode requested. */
	return -EINVAL;
}

static struct inode *fetch_inode(struct super_block *sb, unsigned long ino)
{
	struct inode_search search = {
		.ino = ino
	};
	struct inode *inode = iget5_locked(sb, search.ino, inode_test,
				inode_set, &search);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);

	return inode;
}

int dir_lookup_pseudo_files(struct super_block *sb, struct dentry *dentry)
{
	struct mem_range name_range =
			range((u8 *)dentry->d_name.name, dentry->d_name.len);
	unsigned long ino;
	struct inode *inode;

	if (incfs_equal_ranges(pending_reads_file_name_range, name_range))
		ino = INCFS_PENDING_READS_INODE;
	else if (incfs_equal_ranges(log_file_name_range, name_range))
		ino = INCFS_LOG_INODE;
	else if (incfs_equal_ranges(blocks_written_file_name_range, name_range))
		ino = INCFS_BLOCKS_WRITTEN_INODE;
	else
		return -ENOENT;

	inode = fetch_inode(sb, ino);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	d_add(dentry, inode);
	return 0;
}

int emit_pseudo_files(struct dir_context *ctx)
{
	if (ctx->pos == 0) {
		if (!dir_emit(ctx, pending_reads_file_name,
			      ARRAY_SIZE(pending_reads_file_name) - 1,
			      INCFS_PENDING_READS_INODE, DT_REG))
			return -EINVAL;

		ctx->pos++;
	}

	if (ctx->pos == 1) {
		if (!dir_emit(ctx, log_file_name,
			      ARRAY_SIZE(log_file_name) - 1,
			      INCFS_LOG_INODE, DT_REG))
			return -EINVAL;

		ctx->pos++;
	}

	if (ctx->pos == 2) {
		if (!dir_emit(ctx, blocks_written_file_name,
			      ARRAY_SIZE(blocks_written_file_name) - 1,
			      INCFS_BLOCKS_WRITTEN_INODE, DT_REG))
			return -EINVAL;

		ctx->pos++;
	}

	return 0;
}
