/*
 * VFS-related code for RelayFS, a high-speed data relay filesystem.
 *
 * Copyright (C) 2003-2005 - Tom Zanussi <zanussi@us.ibm.com>, IBM Corp
 * Copyright (C) 2003-2005 - Karim Yaghmour <karim@opersys.com>
 *
 * Based on ramfs, Copyright (C) 2002 - Linus Torvalds
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/relayfs_fs.h>
#include "relay.h"
#include "buffers.h"

#define RELAYFS_MAGIC			0xF0B4A981

static struct vfsmount *		relayfs_mount;
static int				relayfs_mount_count;

static struct backing_dev_info		relayfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static struct inode *relayfs_get_inode(struct super_block *sb,
				       int mode,
 				       struct file_operations *fops,
				       void *data)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return NULL;

	inode->i_mode = mode;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_mapping->backing_dev_info = &relayfs_backing_dev_info;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	case S_IFREG:
		inode->i_fop = fops;
		if (data)
			inode->u.generic_ip = data;
		break;
	case S_IFDIR:
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inode->i_nlink++;
		break;
	default:
		break;
	}

	return inode;
}

/**
 *	relayfs_create_entry - create a relayfs directory or file
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@mode: mode
 *	@fops: file operations to use for the file
 *	@data: user-associated data for this file
 *
 *	Returns the new dentry, NULL on failure
 *
 *	Creates a file or directory with the specifed permissions.
 */
static struct dentry *relayfs_create_entry(const char *name,
					   struct dentry *parent,
					   int mode,
					   struct file_operations *fops,
					   void *data)
{
	struct dentry *d;
	struct inode *inode;
	int error = 0;

	BUG_ON(!name || !(S_ISREG(mode) || S_ISDIR(mode)));

	error = simple_pin_fs("relayfs", &relayfs_mount, &relayfs_mount_count);
	if (error) {
		printk(KERN_ERR "Couldn't mount relayfs: errcode %d\n", error);
		return NULL;
	}

	if (!parent && relayfs_mount && relayfs_mount->mnt_sb)
		parent = relayfs_mount->mnt_sb->s_root;

	if (!parent) {
		simple_release_fs(&relayfs_mount, &relayfs_mount_count);
		return NULL;
	}

	parent = dget(parent);
	mutex_lock(&parent->d_inode->i_mutex);
	d = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(d)) {
		d = NULL;
		goto release_mount;
	}

	if (d->d_inode) {
		d = NULL;
		goto release_mount;
	}

	inode = relayfs_get_inode(parent->d_inode->i_sb, mode, fops, data);
	if (!inode) {
		d = NULL;
		goto release_mount;
	}

	d_instantiate(d, inode);
	dget(d);	/* Extra count - pin the dentry in core */

	if (S_ISDIR(mode))
		parent->d_inode->i_nlink++;

	goto exit;

release_mount:
	simple_release_fs(&relayfs_mount, &relayfs_mount_count);

exit:
	mutex_unlock(&parent->d_inode->i_mutex);
	dput(parent);
	return d;
}

/**
 *	relayfs_create_file - create a file in the relay filesystem
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@mode: mode, if not specied the default perms are used
 *	@fops: file operations to use for the file
 *	@data: user-associated data for this file
 *
 *	Returns file dentry if successful, NULL otherwise.
 *
 *	The file will be created user r on behalf of current user.
 */
struct dentry *relayfs_create_file(const char *name,
				   struct dentry *parent,
				   int mode,
				   struct file_operations *fops,
				   void *data)
{
	BUG_ON(!fops);

	if (!mode)
		mode = S_IRUSR;
	mode = (mode & S_IALLUGO) | S_IFREG;

	return relayfs_create_entry(name, parent, mode, fops, data);
}

/**
 *	relayfs_create_dir - create a directory in the relay filesystem
 *	@name: the name of the directory to create
 *	@parent: parent directory, NULL if parent should be fs root
 *
 *	Returns directory dentry if successful, NULL otherwise.
 *
 *	The directory will be created world rwx on behalf of current user.
 */
struct dentry *relayfs_create_dir(const char *name, struct dentry *parent)
{
	int mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	return relayfs_create_entry(name, parent, mode, NULL, NULL);
}

/**
 *	relayfs_remove - remove a file or directory in the relay filesystem
 *	@dentry: file or directory dentry
 *
 *	Returns 0 if successful, negative otherwise.
 */
int relayfs_remove(struct dentry *dentry)
{
	struct dentry *parent;
	int error = 0;

	if (!dentry)
		return -EINVAL;
	parent = dentry->d_parent;
	if (!parent)
		return -EINVAL;

	parent = dget(parent);
	mutex_lock(&parent->d_inode->i_mutex);
	if (dentry->d_inode) {
		if (S_ISDIR(dentry->d_inode->i_mode))
			error = simple_rmdir(parent->d_inode, dentry);
		else
			error = simple_unlink(parent->d_inode, dentry);
		if (!error)
			d_delete(dentry);
	}
	if (!error)
		dput(dentry);
	mutex_unlock(&parent->d_inode->i_mutex);
	dput(parent);

	if (!error)
		simple_release_fs(&relayfs_mount, &relayfs_mount_count);

	return error;
}

/**
 *	relayfs_remove_file - remove a file from relay filesystem
 *	@dentry: directory dentry
 *
 *	Returns 0 if successful, negative otherwise.
 */
int relayfs_remove_file(struct dentry *dentry)
{
	return relayfs_remove(dentry);
}

/**
 *	relayfs_remove_dir - remove a directory in the relay filesystem
 *	@dentry: directory dentry
 *
 *	Returns 0 if successful, negative otherwise.
 */
int relayfs_remove_dir(struct dentry *dentry)
{
	return relayfs_remove(dentry);
}

/**
 *	relay_file_open - open file op for relay files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Increments the channel buffer refcount.
 */
static int relay_file_open(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = inode->u.generic_ip;
	kref_get(&buf->kref);
	filp->private_data = buf;

	return 0;
}

/**
 *	relay_file_mmap - mmap file op for relay files
 *	@filp: the file
 *	@vma: the vma describing what to map
 *
 *	Calls upon relay_mmap_buf to map the file into user space.
 */
static int relay_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct rchan_buf *buf = filp->private_data;
	return relay_mmap_buf(buf, vma);
}

/**
 *	relay_file_poll - poll file op for relay files
 *	@filp: the file
 *	@wait: poll table
 *
 *	Poll implemention.
 */
static unsigned int relay_file_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct rchan_buf *buf = filp->private_data;

	if (buf->finalized)
		return POLLERR;

	if (filp->f_mode & FMODE_READ) {
		poll_wait(filp, &buf->read_wait, wait);
		if (!relay_buf_empty(buf))
			mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

/**
 *	relay_file_release - release file op for relay files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Decrements the channel refcount, as the filesystem is
 *	no longer using it.
 */
static int relay_file_release(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = filp->private_data;
	kref_put(&buf->kref, relay_remove_buf);

	return 0;
}

/**
 *	relay_file_read_consume - update the consumed count for the buffer
 */
static void relay_file_read_consume(struct rchan_buf *buf,
				    size_t read_pos,
				    size_t bytes_consumed)
{
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t read_subbuf;

	if (buf->bytes_consumed + bytes_consumed > subbuf_size) {
		relay_subbufs_consumed(buf->chan, buf->cpu, 1);
		buf->bytes_consumed = 0;
	}

	buf->bytes_consumed += bytes_consumed;
	read_subbuf = read_pos / buf->chan->subbuf_size;
	if (buf->bytes_consumed + buf->padding[read_subbuf] == subbuf_size) {
		if ((read_subbuf == buf->subbufs_produced % n_subbufs) &&
		    (buf->offset == subbuf_size))
			return;
		relay_subbufs_consumed(buf->chan, buf->cpu, 1);
		buf->bytes_consumed = 0;
	}
}

/**
 *	relay_file_read_avail - boolean, are there unconsumed bytes available?
 */
static int relay_file_read_avail(struct rchan_buf *buf, size_t read_pos)
{
	size_t bytes_produced, bytes_consumed, write_offset;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;
	size_t produced = buf->subbufs_produced % n_subbufs;
	size_t consumed = buf->subbufs_consumed % n_subbufs;

	write_offset = buf->offset > subbuf_size ? subbuf_size : buf->offset;

	if (consumed > produced) {
		if ((produced > n_subbufs) &&
		    (produced + n_subbufs - consumed <= n_subbufs))
			produced += n_subbufs;
	} else if (consumed == produced) {
		if (buf->offset > subbuf_size) {
			produced += n_subbufs;
			if (buf->subbufs_produced == buf->subbufs_consumed)
				consumed += n_subbufs;
		}
	}

	if (buf->offset > subbuf_size)
		bytes_produced = (produced - 1) * subbuf_size + write_offset;
	else
		bytes_produced = produced * subbuf_size + write_offset;
	bytes_consumed = consumed * subbuf_size + buf->bytes_consumed;

	if (bytes_produced == bytes_consumed)
		return 0;

	relay_file_read_consume(buf, read_pos, 0);

	return 1;
}

/**
 *	relay_file_read_subbuf_avail - return bytes available in sub-buffer
 */
static size_t relay_file_read_subbuf_avail(size_t read_pos,
					   struct rchan_buf *buf)
{
	size_t padding, avail = 0;
	size_t read_subbuf, read_offset, write_subbuf, write_offset;
	size_t subbuf_size = buf->chan->subbuf_size;

	write_subbuf = (buf->data - buf->start) / subbuf_size;
	write_offset = buf->offset > subbuf_size ? subbuf_size : buf->offset;
	read_subbuf = read_pos / subbuf_size;
	read_offset = read_pos % subbuf_size;
	padding = buf->padding[read_subbuf];

	if (read_subbuf == write_subbuf) {
		if (read_offset + padding < write_offset)
			avail = write_offset - (read_offset + padding);
	} else
		avail = (subbuf_size - padding) - read_offset;

	return avail;
}

/**
 *	relay_file_read_start_pos - find the first available byte to read
 *
 *	If the read_pos is in the middle of padding, return the
 *	position of the first actually available byte, otherwise
 *	return the original value.
 */
static size_t relay_file_read_start_pos(size_t read_pos,
					struct rchan_buf *buf)
{
	size_t read_subbuf, padding, padding_start, padding_end;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;

	read_subbuf = read_pos / subbuf_size;
	padding = buf->padding[read_subbuf];
	padding_start = (read_subbuf + 1) * subbuf_size - padding;
	padding_end = (read_subbuf + 1) * subbuf_size;
	if (read_pos >= padding_start && read_pos < padding_end) {
		read_subbuf = (read_subbuf + 1) % n_subbufs;
		read_pos = read_subbuf * subbuf_size;
	}

	return read_pos;
}

/**
 *	relay_file_read_end_pos - return the new read position
 */
static size_t relay_file_read_end_pos(struct rchan_buf *buf,
				      size_t read_pos,
				      size_t count)
{
	size_t read_subbuf, padding, end_pos;
	size_t subbuf_size = buf->chan->subbuf_size;
	size_t n_subbufs = buf->chan->n_subbufs;

	read_subbuf = read_pos / subbuf_size;
	padding = buf->padding[read_subbuf];
	if (read_pos % subbuf_size + count + padding == subbuf_size)
		end_pos = (read_subbuf + 1) * subbuf_size;
	else
		end_pos = read_pos + count;
	if (end_pos >= subbuf_size * n_subbufs)
		end_pos = 0;

	return end_pos;
}

/**
 *	relay_file_read - read file op for relay files
 *	@filp: the file
 *	@buffer: the userspace buffer
 *	@count: number of bytes to read
 *	@ppos: position to read from
 *
 *	Reads count bytes or the number of bytes available in the
 *	current sub-buffer being read, whichever is smaller.
 */
static ssize_t relay_file_read(struct file *filp,
			       char __user *buffer,
			       size_t count,
			       loff_t *ppos)
{
	struct rchan_buf *buf = filp->private_data;
	struct inode *inode = filp->f_dentry->d_inode;
	size_t read_start, avail;
	ssize_t ret = 0;
	void *from;

	mutex_lock(&inode->i_mutex);
	if(!relay_file_read_avail(buf, *ppos))
		goto out;

	read_start = relay_file_read_start_pos(*ppos, buf);
	avail = relay_file_read_subbuf_avail(read_start, buf);
	if (!avail)
		goto out;

	from = buf->start + read_start;
	ret = count = min(count, avail);
	if (copy_to_user(buffer, from, count)) {
		ret = -EFAULT;
		goto out;
	}
	relay_file_read_consume(buf, read_start, count);
	*ppos = relay_file_read_end_pos(buf, read_start, count);
out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

struct file_operations relay_file_operations = {
	.open		= relay_file_open,
	.poll		= relay_file_poll,
	.mmap		= relay_file_mmap,
	.read		= relay_file_read,
	.llseek		= no_llseek,
	.release	= relay_file_release,
};

static struct super_operations relayfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static int relayfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode *inode;
	struct dentry *root;
	int mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RELAYFS_MAGIC;
	sb->s_op = &relayfs_ops;
	inode = relayfs_get_inode(sb, mode, NULL, NULL);

	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	return 0;
}

static struct super_block * relayfs_get_sb(struct file_system_type *fs_type,
					   int flags, const char *dev_name,
					   void *data)
{
	return get_sb_single(fs_type, flags, data, relayfs_fill_super);
}

static struct file_system_type relayfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "relayfs",
	.get_sb		= relayfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_relayfs_fs(void)
{
	return register_filesystem(&relayfs_fs_type);
}

static void __exit exit_relayfs_fs(void)
{





	unregister_filesystem(&relayfs_fs_type);
}

module_init(init_relayfs_fs)
module_exit(exit_relayfs_fs)

EXPORT_SYMBOL_GPL(relay_file_operations);
EXPORT_SYMBOL_GPL(relayfs_create_dir);
EXPORT_SYMBOL_GPL(relayfs_remove_dir);
EXPORT_SYMBOL_GPL(relayfs_create_file);
EXPORT_SYMBOL_GPL(relayfs_remove_file);

MODULE_AUTHOR("Tom Zanussi <zanussi@us.ibm.com> and Karim Yaghmour <karim@opersys.com>");
MODULE_DESCRIPTION("Relay Filesystem");
MODULE_LICENSE("GPL");

