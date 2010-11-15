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
#include <linux/namei.h>
#include "hostfs.h"
#include "init.h"
#include "kern.h"

struct hostfs_inode_info {
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
static const struct inode_operations hostfs_link_iops;

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

static char *__dentry_name(struct dentry *dentry, char *name)
{
	char *p = __dentry_path(dentry, name, PATH_MAX);
	char *root;
	size_t len;

	spin_unlock(&dcache_lock);

	root = dentry->d_sb->s_fs_info;
	len = strlen(root);
	if (IS_ERR(p)) {
		__putname(name);
		return NULL;
	}
	strlcpy(name, root, PATH_MAX);
	if (len > p - name) {
		__putname(name);
		return NULL;
	}
	if (p > name + len) {
		char *s = name + len;
		while ((*s++ = *p++) != '\0')
			;
	}
	return name;
}

static char *dentry_name(struct dentry *dentry)
{
	char *name = __getname();
	if (!name)
		return NULL;

	spin_lock(&dcache_lock);
	return __dentry_name(dentry, name); /* will unlock */
}

static char *inode_name(struct inode *ino)
{
	struct dentry *dentry;
	char *name = __getname();
	if (!name)
		return NULL;

	spin_lock(&dcache_lock);
	if (list_empty(&ino->i_dentry)) {
		spin_unlock(&dcache_lock);
		__putname(name);
		return NULL;
	}
	dentry = list_first_entry(&ino->i_dentry, struct dentry, d_alias);
	return __dentry_name(dentry, name); /* will unlock */
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

static struct inode *hostfs_iget(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
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

	err = do_statfs(dentry->d_sb->s_fs_info,
			&sf->f_bsize, &f_blocks, &f_bfree, &f_bavail, &f_files,
			&f_ffree, &sf->f_fsid, sizeof(sf->f_fsid),
			&sf->f_namelen);
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

	hi = kzalloc(sizeof(*hi), GFP_KERNEL);
	if (hi == NULL)
		return NULL;
	hi->fd = -1;
	inode_init_once(&hi->vfs_inode);
	return &hi->vfs_inode;
}

static void hostfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);
	if (HOSTFS_I(inode)->fd != -1) {
		close_file(&HOSTFS_I(inode)->fd);
		HOSTFS_I(inode)->fd = -1;
	}
}

static void hostfs_destroy_inode(struct inode *inode)
{
	kfree(HOSTFS_I(inode));
}

static int hostfs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	const char *root_path = vfs->mnt_sb->s_fs_info;
	size_t offset = strlen(root_ino) + 1;

	if (strlen(root_path) > offset)
		seq_printf(seq, ",%s", root_path + offset);

	return 0;
}

static const struct super_operations hostfs_sbops = {
	.alloc_inode	= hostfs_alloc_inode,
	.destroy_inode	= hostfs_destroy_inode,
	.evict_inode	= hostfs_evict_inode,
	.statfs		= hostfs_statfs,
	.show_options	= hostfs_show_options,
};

int hostfs_readdir(struct file *file, void *ent, filldir_t filldir)
{
	void *dir;
	char *name;
	unsigned long long next, ino;
	int error, len;

	name = dentry_name(file->f_path.dentry);
	if (name == NULL)
		return -ENOMEM;
	dir = open_dir(name, &error);
	__putname(name);
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
	static DEFINE_MUTEX(open_mutex);
	char *name;
	fmode_t mode = 0;
	int err;
	int r = 0, w = 0, fd;

	mode = file->f_mode & (FMODE_READ | FMODE_WRITE);
	if ((mode & HOSTFS_I(ino)->mode) == mode)
		return 0;

	mode |= HOSTFS_I(ino)->mode;

retry:
	if (mode & FMODE_READ)
		r = 1;
	if (mode & FMODE_WRITE)
		w = 1;
	if (w)
		r = 1;

	name = dentry_name(file->f_path.dentry);
	if (name == NULL)
		return -ENOMEM;

	fd = open_file(name, r, w, append);
	__putname(name);
	if (fd < 0)
		return fd;

	mutex_lock(&open_mutex);
	/* somebody else had handled it first? */
	if ((mode & HOSTFS_I(ino)->mode) == mode) {
		mutex_unlock(&open_mutex);
		return 0;
	}
	if ((mode | HOSTFS_I(ino)->mode) != mode) {
		mode |= HOSTFS_I(ino)->mode;
		mutex_unlock(&open_mutex);
		close_file(&fd);
		goto retry;
	}
	if (HOSTFS_I(ino)->fd == -1) {
		HOSTFS_I(ino)->fd = fd;
	} else {
		err = replace_file(fd, HOSTFS_I(ino)->fd);
		close_file(&fd);
		if (err < 0) {
			mutex_unlock(&open_mutex);
			return err;
		}
	}
	HOSTFS_I(ino)->mode = mode;
	mutex_unlock(&open_mutex);

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

static int read_name(struct inode *ino, char *name)
{
	dev_t rdev;
	struct hostfs_stat st;
	int err = stat_file(name, &st, -1);
	if (err)
		return err;

	/* Reencode maj and min with the kernel encoding.*/
	rdev = MKDEV(st.maj, st.min);

	switch (st.mode & S_IFMT) {
	case S_IFLNK:
		ino->i_op = &hostfs_link_iops;
		break;
	case S_IFDIR:
		ino->i_op = &hostfs_dir_iops;
		ino->i_fop = &hostfs_dir_fops;
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		init_special_inode(ino, st.mode & S_IFMT, rdev);
		ino->i_op = &hostfs_iops;
		break;

	default:
		ino->i_op = &hostfs_iops;
		ino->i_fop = &hostfs_file_fops;
		ino->i_mapping->a_ops = &hostfs_aops;
	}

	ino->i_ino = st.ino;
	ino->i_mode = st.mode;
	ino->i_nlink = st.nlink;
	ino->i_uid = st.uid;
	ino->i_gid = st.gid;
	ino->i_atime = st.atime;
	ino->i_mtime = st.mtime;
	ino->i_ctime = st.ctime;
	ino->i_size = st.size;
	ino->i_blocks = st.blocks;
	return 0;
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

	error = -ENOMEM;
	name = dentry_name(dentry);
	if (name == NULL)
		goto out_put;

	fd = file_create(name,
			 mode & S_IRUSR, mode & S_IWUSR, mode & S_IXUSR,
			 mode & S_IRGRP, mode & S_IWGRP, mode & S_IXGRP,
			 mode & S_IROTH, mode & S_IWOTH, mode & S_IXOTH);
	if (fd < 0)
		error = fd;
	else
		error = read_name(inode, name);

	__putname(name);
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

	err = -ENOMEM;
	name = dentry_name(dentry);
	if (name == NULL)
		goto out_put;

	err = read_name(inode, name);

	__putname(name);
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

int hostfs_link(struct dentry *to, struct inode *ino, struct dentry *from)
{
	char *from_name, *to_name;
	int err;

	if ((from_name = dentry_name(from)) == NULL)
		return -ENOMEM;
	to_name = dentry_name(to);
	if (to_name == NULL) {
		__putname(from_name);
		return -ENOMEM;
	}
	err = link_file(to_name, from_name);
	__putname(from_name);
	__putname(to_name);
	return err;
}

int hostfs_unlink(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;

	if (append)
		return -EPERM;

	if ((file = dentry_name(dentry)) == NULL)
		return -ENOMEM;

	err = unlink_file(file);
	__putname(file);
	return err;
}

int hostfs_symlink(struct inode *ino, struct dentry *dentry, const char *to)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -ENOMEM;
	err = make_symlink(file, to);
	__putname(file);
	return err;
}

int hostfs_mkdir(struct inode *ino, struct dentry *dentry, int mode)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -ENOMEM;
	err = do_mkdir(file, mode);
	__putname(file);
	return err;
}

int hostfs_rmdir(struct inode *ino, struct dentry *dentry)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -ENOMEM;
	err = do_rmdir(file);
	__putname(file);
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

	err = -ENOMEM;
	name = dentry_name(dentry);
	if (name == NULL)
		goto out_put;

	init_special_inode(inode, mode, dev);
	err = do_mknod(name, mode, MAJOR(dev), MINOR(dev));
	if (!err)
		goto out_free;

	err = read_name(inode, name);
	__putname(name);
	if (err)
		goto out_put;
	if (err)
		goto out_put;

	d_instantiate(dentry, inode);
	return 0;

 out_free:
	__putname(name);
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

	if ((from_name = dentry_name(from)) == NULL)
		return -ENOMEM;
	if ((to_name = dentry_name(to)) == NULL) {
		__putname(from_name);
		return -ENOMEM;
	}
	err = rename_file(from_name, to_name);
	__putname(from_name);
	__putname(to_name);
	return err;
}

int hostfs_permission(struct inode *ino, int desired)
{
	char *name;
	int r = 0, w = 0, x = 0, err;

	if (desired & MAY_READ) r = 1;
	if (desired & MAY_WRITE) w = 1;
	if (desired & MAY_EXEC) x = 1;
	name = inode_name(ino);
	if (name == NULL)
		return -ENOMEM;

	if (S_ISCHR(ino->i_mode) || S_ISBLK(ino->i_mode) ||
	    S_ISFIFO(ino->i_mode) || S_ISSOCK(ino->i_mode))
		err = 0;
	else
		err = access_file(name, r, w, x);
	__putname(name);
	if (!err)
		err = generic_permission(ino, desired, NULL);
	return err;
}

int hostfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct hostfs_iattr attrs;
	char *name;
	int err;

	int fd = HOSTFS_I(inode)->fd;

	err = inode_change_ok(inode, attr);
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
	name = dentry_name(dentry);
	if (name == NULL)
		return -ENOMEM;
	err = set_attr(name, &attrs, fd);
	__putname(name);
	if (err)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		int error;

		error = vmtruncate(inode, attr->ia_size);
		if (err)
			return err;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
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

static void *hostfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *link = __getname();
	if (link) {
		char *path = dentry_name(dentry);
		int err = -ENOMEM;
		if (path) {
			err = hostfs_do_readlink(path, link, PATH_MAX);
			if (err == PATH_MAX)
				err = -E2BIG;
			__putname(path);
		}
		if (err < 0) {
			__putname(link);
			link = ERR_PTR(err);
		}
	} else {
		link = ERR_PTR(-ENOMEM);
	}

	nd_set_link(nd, link);
	return NULL;
}

static void hostfs_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
{
	char *s = nd_get_link(nd);
	if (!IS_ERR(s))
		__putname(s);
}

static const struct inode_operations hostfs_link_iops = {
	.readlink	= generic_readlink,
	.follow_link	= hostfs_follow_link,
	.put_link	= hostfs_put_link,
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
	sb->s_fs_info = host_root_path =
		kmalloc(strlen(root_ino) + strlen(req_root) + 2, GFP_KERNEL);
	if (host_root_path == NULL)
		goto out;

	sprintf(host_root_path, "%s/%s", root_ino, req_root);

	root_inode = new_inode(sb);
	if (!root_inode)
		goto out;

	err = read_name(root_inode, host_root_path);
	if (err)
		goto out_put;

	if (S_ISLNK(root_inode->i_mode)) {
		char *name = follow_link(host_root_path);
		if (IS_ERR(name))
			err = PTR_ERR(name);
		else
			err = read_name(root_inode, name);
		kfree(name);
		if (err)
			goto out_put;
	}

	err = -ENOMEM;
	sb->s_root = d_alloc_root(root_inode);
	if (sb->s_root == NULL)
		goto out_put;

	return 0;

out_put:
	iput(root_inode);
out:
	return err;
}

static struct dentry *hostfs_read_sb(struct file_system_type *type,
			  int flags, const char *dev_name,
			  void *data)
{
	return mount_nodev(type, flags, data, hostfs_fill_sb_common);
}

static void hostfs_kill_sb(struct super_block *s)
{
	kill_anon_super(s);
	kfree(s->s_fs_info);
}

static struct file_system_type hostfs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "hostfs",
	.mount	 	= hostfs_read_sb,
	.kill_sb	= hostfs_kill_sb,
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
