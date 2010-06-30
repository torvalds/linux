/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 *
 * Ported the filesystem routines to 2.5.
 * 2003-02-10 Petr Baudis <pasky@ucw.cz>
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/statfs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include "hostfs.h"
#include "init.h"
#include "kern.h"

struct hostfs_inode_info {
	char *host_filename;
	int fd;
	fmode_t mode;
	struct inode vfs_inode;
};

static inline struct hostfs_inode_info *HOSTFS_I(struct inode *inode)
{
	return list_entry(inode, struct hostfs_inode_info, vfs_inode);
}

#define FILE_HOSTFS_I(file) HOSTFS_I((file)->f_path.dentry->d_inode)

static int hostfs_d_delete(struct dentry *dentry)
{
	return 1;
}

static const struct dentry_operations hostfs_dentry_ops = {
	.d_delete		= hostfs_d_delete,
};

/* Changed in hostfs_args before the kernel starts running */
static char *root_ino = "";
static int append = 0;

#define HOSTFS_SUPER_MAGIC 0x00c0ffee

static const struct inode_operations hostfs_iops;
static const struct inode_operations hostfs_dir_iops;
static const struct address_space_operations hostfs_link_aops;

#ifndef MODULE
static int __init hostfs_args(char *options, int *add)
{
	char *ptr;

	ptr = strchr(options, ',');
	if (ptr != NULL)
		*ptr++ = '\0';
	if (*options != '\0')
		root_ino = options;

	options = ptr;
	while (options) {
		ptr = strchr(options, ',');
		if (ptr != NULL)
			*ptr++ = '\0';
		if (*options != '\0') {
			if (!strcmp(options, "append"))
				append = 1;
			else printf("hostfs_args - unsupported option - %s\n",
				    options);
		}
		options = ptr;
	}
	return 0;
}

__uml_setup("hostfs=", hostfs_args,
"hostfs=<root dir>,<flags>,...\n"
"    This is used to set hostfs parameters.  The root directory argument\n"
"    is used to confine all hostfs mounts to within the specified directory\n"
"    tree on the host.  If this isn't specified, then a user inside UML can\n"
"    mount anything on the host that's accessible to the user that's running\n"
"    it.\n"
"    The only flag currently supported is 'append', which specifies that all\n"
"    files opened by hostfs will be opened in append mode.\n\n"
);
#endif

static char *dentry_name(struct dentry *dentry, int extra)
{
	struct dentry *parent;
	char *root, *name;
	int len;

	len = 0;
	parent = dentry;
	while (parent->d_parent != parent) {
		len += parent->d_name.len + 1;
		parent = parent->d_parent;
	}

	root = HOSTFS_I(parent->d_inode)->host_filename;
	len += strlen(root);
	name = kmalloc(len + extra + 1, GFP_KERNEL);
	if (name == NULL)
		return NULL;

	name[len] = '\0';
	parent = dentry;
	while (parent->d_parent != parent) {
		len -= parent->d_name.len + 1;
		name[len] = '/';
		strncpy(&name[len + 1], parent->d_name.name,
			parent->d_name.len);
		parent = parent->d_parent;
	}
	strncpy(name, root, strlen(root));
	return name;
}

static char *inode_name(struct inode *ino, int extra)
{
	struct dentry *dentry;

	dentry = list_entry(ino->i_dentry.next, struct dentry, d_alias);
	return dentry_name(dentry, extra);
}

static int read_name(struct inode *ino, char *name)
{
	/*
	 * The non-int inode fields are copied into ints by stat_file and
	 * then copied into the inode because passing the actual pointers
	 * in and having them treated as int * breaks on big-endian machines
	 */
	int err;
	int i_mode, i_nlink, i_blksize;
	unsigned long long i_size;
	unsigned long long i_ino;
	unsigned long long i_blocks;

	err = stat_file(name, &i_ino, &i_mode, &i_nlink, &ino->i_uid,
			&ino->i_gid, &i_size, &ino->i_atime, &ino->i_mtime,
			&ino->i_ctime, &i_blksize, &i_blocks, -1);
	if (err)
		return err;

	ino->i_ino = i_ino;
	ino->i_mode = i_mode;
	ino->i_nlink = i_nlink;
	ino->i_size = i_size;
	ino->i_blocks = i_blocks;
	return 0;
}

static char *follow_link(char *link)
{
	int len, n;
	char *name, *resolved, *end;

	len = 64;
	while (1) {
		n = -ENOMEM;
		name = kmalloc(len, GFP_KERNEL);
		if (name == NULL)
			goto out;

		n = hostfs_do_readlink(link, name, len);
		if (n < len)
			break;
		len *= 2;
		kfree(name);
	}
	if (n < 0)
		goto out_free;

	if (*name == '/')
		return name;

	end = strrchr(link, '/');
	if (end == NULL)
		return name;

	*(end + 1) = '\0';
	len = strlen(link) + strlen(name) + 1;

	resolved = kmalloc(len, GFP_KERNEL);
	if (resolved == NULL) {
		n = -ENOMEM;
		goto out_free;
	}

	sprintf(resolved, "%s%s", link, name);
	kfree(name);
	kfree(link);
	return resolved;

 out_free:
	kfree(name);
 out:
	return ERR_PTR(n);
}

static int hostfs_read_inode(struct inode *ino)
{
	char *name;
	int err = 0;

	/*
	 * Unfortunately, we are called from iget() when we don't have a dentry
	 * allocated yet.
	 */
	if (list_empty(&ino->i_dentry))
		goto out;

	err = -ENOMEM;
	name = inode_name(ino, 0);
	if (name == NULL)
		goto out;

	if (file_type(name, NULL, NULL) == OS_TYPE_SYMLINK) {
		name = follow_link(name);
		if (IS_ERR(name)) {
			err = PTR_ERR(name);
			goto out;
		}
	}

	err = read_name(ino, name);
	kfree(name);
 out:
	return err;
}

static struct inode *hostfs_iget(struct super_block *sb)
{
	struct inode *inode;
	long ret;

	inode = iget_locked(sb, 0);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (inode->i_state & I_NEW) {
		ret = hostfs_read_inode(inode);
		if (ret < 0) {
			iget_failed(inode);
			return ERR_PTR(ret);
		}
		unlock_new_inode(inode);
	}
	return inode;
}

int hostfs_statfs(struct dentry *dentry, struct kstatfs *sf)
{
	/*
	 * do_statfs uses struct statfs64 internally, but the linux kernel
	 * struct statfs still has 32-bit versions for most of these fields,
	 * so we convert them here
	 */
	int err;
	long long f_blocks;
	long long f_bfree;
	long long f_bavail;
	long long f_files;
	long long f_ffree;

	err = do_statfs(HOSTFS_I(dentry->d_sb->s_root->d_inode)->host_filename,
			&sf->f_bsize, &f_blocks, &f_bfree, &f_bavail, &f_files,
			&f_ffree, &sf->f_fsid, sizeof(sf->f_fsid),
			&sf->f_namelen, sf->f_spare);
	if (err)
		return err;
	sf->f_blocks = f_blocks;
	sf->f_bfree = f_bfree;
	sf->f_bavail = f_bavail;
	sf->f_files = f_files;
	sf->f_ffree = f_ffree;
	sf->f_type = HOSTFS_SUPER_MAGIC;
	return 0;
}

static struct inode *hostfs_alloc_inode(struct super_block *sb)
{
	struct hostfs_inode_info *hi;

	hi = kmalloc(sizeof(*hi), GFP_KERNEL);
	if (hi == NULL)
		return NULL;

	*hi = ((struct hostfs_inode_info) { .host_filename	= NULL,
					    .fd			= -1,
					    .mode		= 0 });
	inode_init_once(&hi->vfs_inode);
	return &hi->vfs_inode;
}

static void hostfs_delete_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	if (HOSTFS_I(inode)->fd != -1) {
		close_file(&HOSTFS_I(inode)->fd);
		HOSTFS_I(inode)->fd = -1;
	}
	clear_inode(inode);
}

static void hostfs_destroy_inode(struct inode *inode)
{
	kfree(HOSTFS_I(inode)->host_filename);

	/*
	 * XXX: This should not happen, probably. The check is here for
	 * additional safety.
	 */
	if (HOSTFS_I(inode)->fd != -1) {
		close_file(&HOSTFS_I(inode)->fd);
		printk(KERN_DEBUG "Closing host fd in .destroy_inode\n");
	}

	kfree(HOSTFS_I(inode));
}

static int hostfs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct inode *root = vfs->mnt_sb->s_root->d_inode;
	const char *root_path = HOSTFS_I(root)->host_filename;
	size_t offset = strlen(root_ino) + 1;

	if (strlen(root_path) > offset)
		seq_printf(seq, ",%s", root_path + offset);

	return 0;
}

static const struct super_operations hostfs_sbops = {
	.alloc_inode	= hostfs_alloc_inode,
	.drop_inode	= generic_delete_inode,
	.delete_inode   = hostfs_delete_inode,
	.destroy_inode	= hostfs_destroy_inode,
	.statfs		= hostfs_statfs,
	.show_options	= hostfs_show_options,
};

int hostfs_readdir(struct file *file, void *ent, filldir_t filldir)
{
	void *dir;
	char *name;
	unsigned long long next, ino;
	int error, len;

	name = dentry_name(file->f_path.dentry, 0);
	if (name == NULL)
		return -ENOMEM;
	dir = open_dir(name, &error);
	kfree(name);
	if (dir == NULL)
		return -error;
	next = file->f_pos;
	while ((name = read_dir(dir, &next, &ino, &len)) != NULL) {
		error = (*filldir)(ent, name, len, file->f_pos,
				   ino, DT_UNKNOWN);
		if (error) break;
		file->f_pos = next;
	}
	close_dir(dir);
	return 0;
}

int hostfs_file_open(struct inode *ino, struct file *file)
{
	char *name;
	fmode_t mode = 0;
	int r = 0, w = 0, fd;

	mode = file->f_mode & (FMODE_READ | FMODE_WRITE);
	if ((mode & HOSTFS_I(ino)->mode) == mode)
		return 0;

	/*
	 * The file may already have been opened, but with the wrong access,
	 * so this resets things and reopens the file with the new access.
	 */
	if (HOSTFS_I(ino)->fd != -1) {
		close_file(&HOSTFS_I(ino)->fd);
		HOSTFS_I(ino)->fd = -1;
	}

	HOSTFS_I(ino)->mode |= mode;
	if (HOSTFS_I(ino)->mode & FMODE_READ)
		r = 1;
	if (HOSTFS_I(ino)->mode & FMODE_WRITE)
		w = 1;
	if (w)
		r = 1;

	name = dentry_name(file->f_path.dentry, 0);
	if (name == NULL)
		return -ENOMEM;

	fd = open_file(name, r, w, append);
	kfree(name);
	if (fd < 0)
		return fd;
	FILE_HOSTFS_I(file)->fd = fd;

	return 0;
}

int hostfs_fsync(struct file *file, int datasync)
{
	return fsync_file(HOSTFS_I(file->f_mapping->host)->fd, datasync);
}

static const struct file_operations hostfs_file_fops = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.splice_read	= generic_file_splice_read,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.write		= do_sync_write,
	.mmap		= generic_file_mmap,
	.open		= hostfs_file_open,
	.release	= NULL,
	.fsync		= hostfs_fsync,
};

static const struct file_operations hostfs_dir_fops = {
	.llseek		= generic_file_llseek,
	.readdir	= hostfs_readdir,
	.read		= generic_read_dir,
};

int hostfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	char *buffer;
	unsigned long long base;
	int count = PAGE_CACHE_SIZE;
	int end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int err;

	if (page->index >= end_index)
		count = inode->i_size & (PAGE_CACHE_SIZE-1);

	buffer = kmap(page);
	base = ((unsigned long long) page->index) << PAGE_CACHE_SHIFT;

	err = write_file(HOSTFS_I(inode)->fd, &base, buffer, count);
	if (err != count) {
		ClearPageUptodate(page);
		goto out;
	}

	if (base > inode->i_size)
		inode->i_size = base;

	if (PageError(page))
		ClearPageError(page);
	err = 0;

 out:
	kunmap(page);

	unlock_page(page);
	return err;
}

int hostfs_readpage(struct file *file, struct page *page)
{
	char *buffer;
	long long start;
	int err = 0;

	start = (long long) page->index << PAGE_CACHE_SHIFT;
	buffer = kmap(page);
	err = read_file(FILE_HOSTFS_I(file)->fd, &start, buffer,
			PAGE_CACHE_SIZE);
	if (err < 0)
		goto out;

	memset(&buffer[err], 0, PAGE_CACHE_SIZE - err);

	flush_dcache_page(page);
	SetPageUptodate(page);
	if (PageError(page)) ClearPageError(page);
	err = 0;
 out:
	kunmap(page);
	unlock_page(page);
	return err;
}

int hostfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	*pagep = grab_cache_page_write_begin(mapping, index, flags);
	if (!*pagep)
		return -ENOMEM;
	return 0;
}

int hostfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	void *buffer;
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);
	int err;

	buffer = kmap(page);
	err = write_file(FILE_HOSTFS_I(file)->fd, &pos, buffer + from, copied);
	kunmap(page);

	if (!PageUptodate(page) && err == PAGE_CACHE_SIZE)
		SetPageUptodate(page);

	/*
	 * If err > 0, write_file has added err to pos, so we are comparing
	 * i_size against the last byte written.
	 */
	if (err > 0 && (pos > inode->i_size))
		inode->i_size = pos;
	unlock_page(page);
	page_cache_release(page);

	return err;
}

static const struct address_space_operations hostfs_aops = {
	.writepage 	= hostfs_writepage,
	.readpage	= hostfs_readpage,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.write_begin	= hostfs_write_begin,
	.write_end	= hostfs_write_end,
};

static int init_inode(struct inode *inode, struct dentry *dentry)
{
	char *name;
	int type, err = -ENOMEM;
	int maj, min;
	dev_t rdev = 0;

	if (dentry) {
		name = dentry_name(dentry, 0);
		if (name == NULL)
			goto out;
		type = file_type(name, &maj, &min);
		/* Reencode maj and min with the kernel encoding.*/
		rdev = MKDEV(maj, min);
		kfree(name);
	}
	else type = OS_TYPE_DIR;

	err = 0;
	if (type == OS_TYPE_SYMLINK)
		inode->i_op = &page_symlink_inode_operations;
	else if (type == OS_TYPE_DIR)
		inode->i_op = &hostfs_dir_iops;
	else inode->i_op = &hostfs_iops;

	if (type == OS_TYPE_DIR) inode->i_fop = &hostfs_dir_fops;
	else inode->i_fop = &hostfs_file_fops;

	if (type == OS_TYPE_SYMLINK)
		inode->i_mapping->a_ops = &hostfs_link_aops;
	else inode->i_mapping->a_ops = &hostfs_aops;

	switch (type) {
	case OS_TYPE_CHARDEV:
		init_special_inode(inode, S_IFCHR, rdev);
		break;
	case OS_TYPE_BLOCKDEV:
		init_special_inode(inode, S_IFBLK, rdev);
		break;
	case OS_TYPE_FIFO:
		init_special_inode(inode, S_IFIFO, 0);
		break;
	case OS_TYPE_SOCK:
		init_special_inode(inode, S_IFSOCK, 0);
		break;
	}
 out:
	return err;
}

int hostfs_create(struct inode *dir, struct dentry *dentry, int mode,
		  struct nameidata *nd)
{
	struct inode *inode;
	char *name;
	int error, fd;

	inode = hostfs_iget(dir->i_sb);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto out;
	}

	error = init_inode(inode, dentry);
	if (error)
		goto out_put;

	error = -ENOMEM;
	name = dentry_name(dentry, 0);
	if (name == NULL)
		goto out_put;

	fd = file_create(name,
			 mode & S_IRUSR, mode & S_IWUSR, mode & S_IXUSR,
			 mode & S_IRGRP, mode & S_IWGRP, mode & S_IXGRP,
			 mode & S_IROTH, mode & S_IWOTH, mode & S_IXOTH);
	if (fd < 0)
		error = fd;
	else error = read_name(inode, name);

	kfree(name);
	if (error)
		goto out_put;

	HOSTFS_I(inode)->fd = fd;
	HOSTFS_I(inode)->mode = FMODE_READ | FMODE_WRITE;
	d_instantiate(dentry, inode);
	return 0;

 out_put:
	iput(inode);
 out:
	return error;
}

struct dentry *hostfs_lookup(struct inode *ino, struct dentry *dentry,
			     struct nameidata *nd)
{
	struct inode *inode;
	char *name;
	int err;

	inode = hostfs_iget(ino->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	err = init_inode(inode, dentry);
	if (err)
		goto out_put;

	err = -ENOMEM;
	name = dentry_name(dentry, 0);
	if (name == NULL)
		goto out_put;

	err = read_name(inode, name);
	kfree(name);
	if (err == -ENOENT) {
		iput(inode);
		inode = NULL;
	}
	else if (err)
		goto out_put;

	d_add(dentry, inode);
	dentry->d_op = &hostfs_dentry_ops;
	return NULL;

 out_put:
	iput(inode);
 out:
	return ERR_PTR(err);
}

static char *inode_dentry_name(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int len;

	file = inode_name(ino, dentry->d_name.len + 1);
	if (file == NULL)
		return NULL;
	strcat(file, "/");
	len = strlen(file);
	strncat(file, dentry->d_name.name, dentry->d_name.len);
	file[len + dentry->d_name.len] = '\0';
	return file;
}

int hostfs_link(struct dentry *to, struct inode *ino, struct dentry *from)
{
	char *from_name, *to_name;
	int err;

	if ((from_name = inode_dentry_name(ino, from)) == NULL)
		return -ENOMEM;
	to_name = dentry_name(to, 0);
	if (to_name == NULL) {
		kfree(from_name);
		return -ENOMEM;
	}
	err = link_file(to_name, from_name);
	kfree(from_name);
	kfree(to_name);
	return err;
}

int hostfs_unlink(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;

	if ((file = inode_dentry_name(ino, dentry)) == NULL)
		return -ENOMEM;
	if (append)
		return -EPERM;

	err = unlink_file(file);
	kfree(file);
	return err;
}

int hostfs_symlink(struct inode *ino, struct dentry *dentry, const char *to)
{
	char *file;
	int err;

	if ((file = inode_dentry_name(ino, dentry)) == NULL)
		return -ENOMEM;
	err = make_symlink(file, to);
	kfree(file);
	return err;
}

int hostfs_mkdir(struct inode *ino, struct dentry *dentry, int mode)
{
	char *file;
	int err;

	if ((file = inode_dentry_name(ino, dentry)) == NULL)
		return -ENOMEM;
	err = do_mkdir(file, mode);
	kfree(file);
	return err;
}

int hostfs_rmdir(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;

	if ((file = inode_dentry_name(ino, dentry)) == NULL)
		return -ENOMEM;
	err = do_rmdir(file);
	kfree(file);
	return err;
}

int hostfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode *inode;
	char *name;
	int err;

	inode = hostfs_iget(dir->i_sb);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	err = init_inode(inode, dentry);
	if (err)
		goto out_put;

	err = -ENOMEM;
	name = dentry_name(dentry, 0);
	if (name == NULL)
		goto out_put;

	init_special_inode(inode, mode, dev);
	err = do_mknod(name, mode, MAJOR(dev), MINOR(dev));
	if (err)
		goto out_free;

	err = read_name(inode, name);
	kfree(name);
	if (err)
		goto out_put;

	d_instantiate(dentry, inode);
	return 0;

 out_free:
	kfree(name);
 out_put:
	iput(inode);
 out:
	return err;
}

int hostfs_rename(struct inode *from_ino, struct dentry *from,
		  struct inode *to_ino, struct dentry *to)
{
	char *from_name, *to_name;
	int err;

	if ((from_name = inode_dentry_name(from_ino, from)) == NULL)
		return -ENOMEM;
	if ((to_name = inode_dentry_name(to_ino, to)) == NULL) {
		kfree(from_name);
		return -ENOMEM;
	}
	err = rename_file(from_name, to_name);
	kfree(from_name);
	kfree(to_name);
	return err;
}

int hostfs_permission(struct inode *ino, int desired)
{
	char *name;
	int r = 0, w = 0, x = 0, err;

	if (desired & MAY_READ) r = 1;
	if (desired & MAY_WRITE) w = 1;
	if (desired & MAY_EXEC) x = 1;
	name = inode_name(ino, 0);
	if (name == NULL)
		return -ENOMEM;

	if (S_ISCHR(ino->i_mode) || S_ISBLK(ino->i_mode) ||
	    S_ISFIFO(ino->i_mode) || S_ISSOCK(ino->i_mode))
		err = 0;
	else
		err = access_file(name, r, w, x);
	kfree(name);
	if (!err)
		err = generic_permission(ino, desired, NULL);
	return err;
}

int hostfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct hostfs_iattr attrs;
	char *name;
	int err;

	int fd = HOSTFS_I(dentry->d_inode)->fd;

	err = inode_change_ok(dentry->d_inode, attr);
	if (err)
		return err;

	if (append)
		attr->ia_valid &= ~ATTR_SIZE;

	attrs.ia_valid = 0;
	if (attr->ia_valid & ATTR_MODE) {
		attrs.ia_valid |= HOSTFS_ATTR_MODE;
		attrs.ia_mode = attr->ia_mode;
	}
	if (attr->ia_valid & ATTR_UID) {
		attrs.ia_valid |= HOSTFS_ATTR_UID;
		attrs.ia_uid = attr->ia_uid;
	}
	if (attr->ia_valid & ATTR_GID) {
		attrs.ia_valid |= HOSTFS_ATTR_GID;
		attrs.ia_gid = attr->ia_gid;
	}
	if (attr->ia_valid & ATTR_SIZE) {
		attrs.ia_valid |= HOSTFS_ATTR_SIZE;
		attrs.ia_size = attr->ia_size;
	}
	if (attr->ia_valid & ATTR_ATIME) {
		attrs.ia_valid |= HOSTFS_ATTR_ATIME;
		attrs.ia_atime = attr->ia_atime;
	}
	if (attr->ia_valid & ATTR_MTIME) {
		attrs.ia_valid |= HOSTFS_ATTR_MTIME;
		attrs.ia_mtime = attr->ia_mtime;
	}
	if (attr->ia_valid & ATTR_CTIME) {
		attrs.ia_valid |= HOSTFS_ATTR_CTIME;
		attrs.ia_ctime = attr->ia_ctime;
	}
	if (attr->ia_valid & ATTR_ATIME_SET) {
		attrs.ia_valid |= HOSTFS_ATTR_ATIME_SET;
	}
	if (attr->ia_valid & ATTR_MTIME_SET) {
		attrs.ia_valid |= HOSTFS_ATTR_MTIME_SET;
	}
	name = dentry_name(dentry, 0);
	if (name == NULL)
		return -ENOMEM;
	err = set_attr(name, &attrs, fd);
	kfree(name);
	if (err)
		return err;

	return inode_setattr(dentry->d_inode, attr);
}

static const struct inode_operations hostfs_iops = {
	.create		= hostfs_create,
	.link		= hostfs_link,
	.unlink		= hostfs_unlink,
	.symlink	= hostfs_symlink,
	.mkdir		= hostfs_mkdir,
	.rmdir		= hostfs_rmdir,
	.mknod		= hostfs_mknod,
	.rename		= hostfs_rename,
	.permission	= hostfs_permission,
	.setattr	= hostfs_setattr,
};

static const struct inode_operations hostfs_dir_iops = {
	.create		= hostfs_create,
	.lookup		= hostfs_lookup,
	.link		= hostfs_link,
	.unlink		= hostfs_unlink,
	.symlink	= hostfs_symlink,
	.mkdir		= hostfs_mkdir,
	.rmdir		= hostfs_rmdir,
	.mknod		= hostfs_mknod,
	.rename		= hostfs_rename,
	.permission	= hostfs_permission,
	.setattr	= hostfs_setattr,
};

int hostfs_link_readpage(struct file *file, struct page *page)
{
	char *buffer, *name;
	int err;

	buffer = kmap(page);
	name = inode_name(page->mapping->host, 0);
	if (name == NULL)
		return -ENOMEM;
	err = hostfs_do_readlink(name, buffer, PAGE_CACHE_SIZE);
	kfree(name);
	if (err == PAGE_CACHE_SIZE)
		err = -E2BIG;
	else if (err > 0) {
		flush_dcache_page(page);
		SetPageUptodate(page);
		if (PageError(page)) ClearPageError(page);
		err = 0;
	}
	kunmap(page);
	unlock_page(page);
	return err;
}

static const struct address_space_operations hostfs_link_aops = {
	.readpage	= hostfs_link_readpage,
};

static int hostfs_fill_sb_common(struct super_block *sb, void *d, int silent)
{
	struct inode *root_inode;
	char *host_root_path, *req_root = d;
	int err;

	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;
	sb->s_magic = HOSTFS_SUPER_MAGIC;
	sb->s_op = &hostfs_sbops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	/* NULL is printed as <NULL> by sprintf: avoid that. */
	if (req_root == NULL)
		req_root = "";

	err = -ENOMEM;
	host_root_path = kmalloc(strlen(root_ino) + 1
				 + strlen(req_root) + 1, GFP_KERNEL);
	if (host_root_path == NULL)
		goto out;

	sprintf(host_root_path, "%s/%s", root_ino, req_root);

	root_inode = hostfs_iget(sb);
	if (IS_ERR(root_inode)) {
		err = PTR_ERR(root_inode);
		goto out_free;
	}

	err = init_inode(root_inode, NULL);
	if (err)
		goto out_put;

	HOSTFS_I(root_inode)->host_filename = host_root_path;
	/*
	 * Avoid that in the error path, iput(root_inode) frees again
	 * host_root_path through hostfs_destroy_inode!
	 */
	host_root_path = NULL;

	err = -ENOMEM;
	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL)
		goto out_put;

	err = hostfs_read_inode(root_inode);
	if (err) {
		/* No iput in this case because the dput does that for us */
		dput(sb->s_root);
		sb->s_root = NULL;
		goto out;
	}

	return 0;

out_put:
	iput(root_inode);
out_free:
	kfree(host_root_path);
out:
	return err;
}

static int hostfs_read_sb(struct file_system_type *type,
			  int flags, const char *dev_name,
			  void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(type, flags, data, hostfs_fill_sb_common, mnt);
}

static struct file_system_type hostfs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "hostfs",
	.get_sb 	= hostfs_read_sb,
	.kill_sb	= kill_anon_super,
	.fs_flags 	= 0,
};

static int __init init_hostfs(void)
{
	return register_filesystem(&hostfs_type);
}

static void __exit exit_hostfs(void)
{
	unregister_filesystem(&hostfs_type);
}

module_init(init_hostfs)
module_exit(exit_hostfs)
MODULE_LICENSE("GPL");
