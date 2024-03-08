/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 *
 * Ported the filesystem routines to 2.5.
 * 2003-02-10 Petr Baudis <pasky@ucw.cz>
 */

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/statfs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/writeback.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include "hostfs.h"
#include <init.h>
#include <kern.h>

struct hostfs_ianalde_info {
	int fd;
	fmode_t mode;
	struct ianalde vfs_ianalde;
	struct mutex open_mutex;
	dev_t dev;
};

static inline struct hostfs_ianalde_info *HOSTFS_I(struct ianalde *ianalde)
{
	return list_entry(ianalde, struct hostfs_ianalde_info, vfs_ianalde);
}

#define FILE_HOSTFS_I(file) HOSTFS_I(file_ianalde(file))

static struct kmem_cache *hostfs_ianalde_cache;

/* Changed in hostfs_args before the kernel starts running */
static char *root_ianal = "";
static int append = 0;

static const struct ianalde_operations hostfs_iops;
static const struct ianalde_operations hostfs_dir_iops;
static const struct ianalde_operations hostfs_link_iops;

#ifndef MODULE
static int __init hostfs_args(char *options, int *add)
{
	char *ptr;

	ptr = strchr(options, ',');
	if (ptr != NULL)
		*ptr++ = '\0';
	if (*options != '\0')
		root_ianal = options;

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
	char *p = dentry_path_raw(dentry, name, PATH_MAX);
	char *root;
	size_t len;

	root = dentry->d_sb->s_fs_info;
	len = strlen(root);
	if (IS_ERR(p)) {
		__putname(name);
		return NULL;
	}

	/*
	 * This function relies on the fact that dentry_path_raw() will place
	 * the path name at the end of the provided buffer.
	 */
	BUG_ON(p + strlen(p) + 1 != name + PATH_MAX);

	strscpy(name, root, PATH_MAX);
	if (len > p - name) {
		__putname(name);
		return NULL;
	}

	if (p > name + len)
		strcpy(name + len, p);

	return name;
}

static char *dentry_name(struct dentry *dentry)
{
	char *name = __getname();
	if (!name)
		return NULL;

	return __dentry_name(dentry, name);
}

static char *ianalde_name(struct ianalde *ianal)
{
	struct dentry *dentry;
	char *name;

	dentry = d_find_alias(ianal);
	if (!dentry)
		return NULL;

	name = dentry_name(dentry);

	dput(dentry);

	return name;
}

static char *follow_link(char *link)
{
	char *name, *resolved, *end;
	int n;

	name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!name) {
		n = -EANALMEM;
		goto out_free;
	}

	n = hostfs_do_readlink(link, name, PATH_MAX);
	if (n < 0)
		goto out_free;
	else if (n == PATH_MAX) {
		n = -E2BIG;
		goto out_free;
	}

	if (*name == '/')
		return name;

	end = strrchr(link, '/');
	if (end == NULL)
		return name;

	*(end + 1) = '\0';

	resolved = kasprintf(GFP_KERNEL, "%s%s", link, name);
	if (resolved == NULL) {
		n = -EANALMEM;
		goto out_free;
	}

	kfree(name);
	return resolved;

 out_free:
	kfree(name);
	return ERR_PTR(n);
}

static int hostfs_statfs(struct dentry *dentry, struct kstatfs *sf)
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

static struct ianalde *hostfs_alloc_ianalde(struct super_block *sb)
{
	struct hostfs_ianalde_info *hi;

	hi = alloc_ianalde_sb(sb, hostfs_ianalde_cache, GFP_KERNEL_ACCOUNT);
	if (hi == NULL)
		return NULL;
	hi->fd = -1;
	hi->mode = 0;
	hi->dev = 0;
	ianalde_init_once(&hi->vfs_ianalde);
	mutex_init(&hi->open_mutex);
	return &hi->vfs_ianalde;
}

static void hostfs_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	if (HOSTFS_I(ianalde)->fd != -1) {
		close_file(&HOSTFS_I(ianalde)->fd);
		HOSTFS_I(ianalde)->fd = -1;
		HOSTFS_I(ianalde)->dev = 0;
	}
}

static void hostfs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(hostfs_ianalde_cache, HOSTFS_I(ianalde));
}

static int hostfs_show_options(struct seq_file *seq, struct dentry *root)
{
	const char *root_path = root->d_sb->s_fs_info;
	size_t offset = strlen(root_ianal) + 1;

	if (strlen(root_path) > offset)
		seq_show_option(seq, root_path + offset, NULL);

	if (append)
		seq_puts(seq, ",append");

	return 0;
}

static const struct super_operations hostfs_sbops = {
	.alloc_ianalde	= hostfs_alloc_ianalde,
	.free_ianalde	= hostfs_free_ianalde,
	.drop_ianalde	= generic_delete_ianalde,
	.evict_ianalde	= hostfs_evict_ianalde,
	.statfs		= hostfs_statfs,
	.show_options	= hostfs_show_options,
};

static int hostfs_readdir(struct file *file, struct dir_context *ctx)
{
	void *dir;
	char *name;
	unsigned long long next, ianal;
	int error, len;
	unsigned int type;

	name = dentry_name(file->f_path.dentry);
	if (name == NULL)
		return -EANALMEM;
	dir = open_dir(name, &error);
	__putname(name);
	if (dir == NULL)
		return -error;
	next = ctx->pos;
	seek_dir(dir, next);
	while ((name = read_dir(dir, &next, &ianal, &len, &type)) != NULL) {
		if (!dir_emit(ctx, name, len, ianal, type))
			break;
		ctx->pos = next;
	}
	close_dir(dir);
	return 0;
}

static int hostfs_open(struct ianalde *ianal, struct file *file)
{
	char *name;
	fmode_t mode;
	int err;
	int r, w, fd;

	mode = file->f_mode & (FMODE_READ | FMODE_WRITE);
	if ((mode & HOSTFS_I(ianal)->mode) == mode)
		return 0;

	mode |= HOSTFS_I(ianal)->mode;

retry:
	r = w = 0;

	if (mode & FMODE_READ)
		r = 1;
	if (mode & FMODE_WRITE)
		r = w = 1;

	name = dentry_name(file_dentry(file));
	if (name == NULL)
		return -EANALMEM;

	fd = open_file(name, r, w, append);
	__putname(name);
	if (fd < 0)
		return fd;

	mutex_lock(&HOSTFS_I(ianal)->open_mutex);
	/* somebody else had handled it first? */
	if ((mode & HOSTFS_I(ianal)->mode) == mode) {
		mutex_unlock(&HOSTFS_I(ianal)->open_mutex);
		close_file(&fd);
		return 0;
	}
	if ((mode | HOSTFS_I(ianal)->mode) != mode) {
		mode |= HOSTFS_I(ianal)->mode;
		mutex_unlock(&HOSTFS_I(ianal)->open_mutex);
		close_file(&fd);
		goto retry;
	}
	if (HOSTFS_I(ianal)->fd == -1) {
		HOSTFS_I(ianal)->fd = fd;
	} else {
		err = replace_file(fd, HOSTFS_I(ianal)->fd);
		close_file(&fd);
		if (err < 0) {
			mutex_unlock(&HOSTFS_I(ianal)->open_mutex);
			return err;
		}
	}
	HOSTFS_I(ianal)->mode = mode;
	mutex_unlock(&HOSTFS_I(ianal)->open_mutex);

	return 0;
}

static int hostfs_file_release(struct ianalde *ianalde, struct file *file)
{
	filemap_write_and_wait(ianalde->i_mapping);

	return 0;
}

static int hostfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	int ret;

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;

	ianalde_lock(ianalde);
	ret = fsync_file(HOSTFS_I(ianalde)->fd, datasync);
	ianalde_unlock(ianalde);

	return ret;
}

static const struct file_operations hostfs_file_fops = {
	.llseek		= generic_file_llseek,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.open		= hostfs_open,
	.release	= hostfs_file_release,
	.fsync		= hostfs_fsync,
};

static const struct file_operations hostfs_dir_fops = {
	.llseek		= generic_file_llseek,
	.iterate_shared	= hostfs_readdir,
	.read		= generic_read_dir,
	.open		= hostfs_open,
	.fsync		= hostfs_fsync,
};

static int hostfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct address_space *mapping = page->mapping;
	struct ianalde *ianalde = mapping->host;
	char *buffer;
	loff_t base = page_offset(page);
	int count = PAGE_SIZE;
	int end_index = ianalde->i_size >> PAGE_SHIFT;
	int err;

	if (page->index >= end_index)
		count = ianalde->i_size & (PAGE_SIZE-1);

	buffer = kmap_local_page(page);

	err = write_file(HOSTFS_I(ianalde)->fd, &base, buffer, count);
	if (err != count) {
		if (err >= 0)
			err = -EIO;
		mapping_set_error(mapping, err);
		goto out;
	}

	if (base > ianalde->i_size)
		ianalde->i_size = base;

	err = 0;

 out:
	kunmap_local(buffer);
	unlock_page(page);

	return err;
}

static int hostfs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	char *buffer;
	loff_t start = page_offset(page);
	int bytes_read, ret = 0;

	buffer = kmap_local_page(page);
	bytes_read = read_file(FILE_HOSTFS_I(file)->fd, &start, buffer,
			PAGE_SIZE);
	if (bytes_read < 0) {
		ClearPageUptodate(page);
		SetPageError(page);
		ret = bytes_read;
		goto out;
	}

	memset(buffer + bytes_read, 0, PAGE_SIZE - bytes_read);

	ClearPageError(page);
	SetPageUptodate(page);

 out:
	flush_dcache_page(page);
	kunmap_local(buffer);
	unlock_page(page);

	return ret;
}

static int hostfs_write_begin(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned len,
			      struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_SHIFT;

	*pagep = grab_cache_page_write_begin(mapping, index);
	if (!*pagep)
		return -EANALMEM;
	return 0;
}

static int hostfs_write_end(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned copied,
			    struct page *page, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	void *buffer;
	unsigned from = pos & (PAGE_SIZE - 1);
	int err;

	buffer = kmap_local_page(page);
	err = write_file(FILE_HOSTFS_I(file)->fd, &pos, buffer + from, copied);
	kunmap_local(buffer);

	if (!PageUptodate(page) && err == PAGE_SIZE)
		SetPageUptodate(page);

	/*
	 * If err > 0, write_file has added err to pos, so we are comparing
	 * i_size against the last byte written.
	 */
	if (err > 0 && (pos > ianalde->i_size))
		ianalde->i_size = pos;
	unlock_page(page);
	put_page(page);

	return err;
}

static const struct address_space_operations hostfs_aops = {
	.writepage 	= hostfs_writepage,
	.read_folio	= hostfs_read_folio,
	.dirty_folio	= filemap_dirty_folio,
	.write_begin	= hostfs_write_begin,
	.write_end	= hostfs_write_end,
};

static int hostfs_ianalde_update(struct ianalde *ianal, const struct hostfs_stat *st)
{
	set_nlink(ianal, st->nlink);
	i_uid_write(ianal, st->uid);
	i_gid_write(ianal, st->gid);
	ianalde_set_atime_to_ts(ianal, (struct timespec64){
			st->atime.tv_sec,
			st->atime.tv_nsec,
		});
	ianalde_set_mtime_to_ts(ianal, (struct timespec64){
			st->mtime.tv_sec,
			st->mtime.tv_nsec,
		});
	ianalde_set_ctime(ianal, st->ctime.tv_sec, st->ctime.tv_nsec);
	ianal->i_size = st->size;
	ianal->i_blocks = st->blocks;
	return 0;
}

static int hostfs_ianalde_set(struct ianalde *ianal, void *data)
{
	struct hostfs_stat *st = data;
	dev_t rdev;

	/* Reencode maj and min with the kernel encoding.*/
	rdev = MKDEV(st->maj, st->min);

	switch (st->mode & S_IFMT) {
	case S_IFLNK:
		ianal->i_op = &hostfs_link_iops;
		break;
	case S_IFDIR:
		ianal->i_op = &hostfs_dir_iops;
		ianal->i_fop = &hostfs_dir_fops;
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		init_special_ianalde(ianal, st->mode & S_IFMT, rdev);
		ianal->i_op = &hostfs_iops;
		break;
	case S_IFREG:
		ianal->i_op = &hostfs_iops;
		ianal->i_fop = &hostfs_file_fops;
		ianal->i_mapping->a_ops = &hostfs_aops;
		break;
	default:
		return -EIO;
	}

	HOSTFS_I(ianal)->dev = st->dev;
	ianal->i_ianal = st->ianal;
	ianal->i_mode = st->mode;
	return hostfs_ianalde_update(ianal, st);
}

static int hostfs_ianalde_test(struct ianalde *ianalde, void *data)
{
	const struct hostfs_stat *st = data;

	return ianalde->i_ianal == st->ianal && HOSTFS_I(ianalde)->dev == st->dev;
}

static struct ianalde *hostfs_iget(struct super_block *sb, char *name)
{
	struct ianalde *ianalde;
	struct hostfs_stat st;
	int err = stat_file(name, &st, -1);

	if (err)
		return ERR_PTR(err);

	ianalde = iget5_locked(sb, st.ianal, hostfs_ianalde_test, hostfs_ianalde_set,
			     &st);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	if (ianalde->i_state & I_NEW) {
		unlock_new_ianalde(ianalde);
	} else {
		spin_lock(&ianalde->i_lock);
		hostfs_ianalde_update(ianalde, &st);
		spin_unlock(&ianalde->i_lock);
	}

	return ianalde;
}

static int hostfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, umode_t mode, bool excl)
{
	struct ianalde *ianalde;
	char *name;
	int fd;

	name = dentry_name(dentry);
	if (name == NULL)
		return -EANALMEM;

	fd = file_create(name, mode & 0777);
	if (fd < 0) {
		__putname(name);
		return fd;
	}

	ianalde = hostfs_iget(dir->i_sb, name);
	__putname(name);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	HOSTFS_I(ianalde)->fd = fd;
	HOSTFS_I(ianalde)->mode = FMODE_READ | FMODE_WRITE;
	d_instantiate(dentry, ianalde);
	return 0;
}

static struct dentry *hostfs_lookup(struct ianalde *ianal, struct dentry *dentry,
				    unsigned int flags)
{
	struct ianalde *ianalde = NULL;
	char *name;

	name = dentry_name(dentry);
	if (name == NULL)
		return ERR_PTR(-EANALMEM);

	ianalde = hostfs_iget(ianal->i_sb, name);
	__putname(name);
	if (ianalde == ERR_PTR(-EANALENT))
		ianalde = NULL;

	return d_splice_alias(ianalde, dentry);
}

static int hostfs_link(struct dentry *to, struct ianalde *ianal,
		       struct dentry *from)
{
	char *from_name, *to_name;
	int err;

	if ((from_name = dentry_name(from)) == NULL)
		return -EANALMEM;
	to_name = dentry_name(to);
	if (to_name == NULL) {
		__putname(from_name);
		return -EANALMEM;
	}
	err = link_file(to_name, from_name);
	__putname(from_name);
	__putname(to_name);
	return err;
}

static int hostfs_unlink(struct ianalde *ianal, struct dentry *dentry)
{
	char *file;
	int err;

	if (append)
		return -EPERM;

	if ((file = dentry_name(dentry)) == NULL)
		return -EANALMEM;

	err = unlink_file(file);
	__putname(file);
	return err;
}

static int hostfs_symlink(struct mnt_idmap *idmap, struct ianalde *ianal,
			  struct dentry *dentry, const char *to)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -EANALMEM;
	err = make_symlink(file, to);
	__putname(file);
	return err;
}

static int hostfs_mkdir(struct mnt_idmap *idmap, struct ianalde *ianal,
			struct dentry *dentry, umode_t mode)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -EANALMEM;
	err = do_mkdir(file, mode);
	__putname(file);
	return err;
}

static int hostfs_rmdir(struct ianalde *ianal, struct dentry *dentry)
{
	char *file;
	int err;

	if ((file = dentry_name(dentry)) == NULL)
		return -EANALMEM;
	err = hostfs_do_rmdir(file);
	__putname(file);
	return err;
}

static int hostfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct ianalde *ianalde;
	char *name;
	int err;

	name = dentry_name(dentry);
	if (name == NULL)
		return -EANALMEM;

	err = do_mkanald(name, mode, MAJOR(dev), MIANALR(dev));
	if (err) {
		__putname(name);
		return err;
	}

	ianalde = hostfs_iget(dir->i_sb, name);
	__putname(name);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	d_instantiate(dentry, ianalde);
	return 0;
}

static int hostfs_rename2(struct mnt_idmap *idmap,
			  struct ianalde *old_dir, struct dentry *old_dentry,
			  struct ianalde *new_dir, struct dentry *new_dentry,
			  unsigned int flags)
{
	char *old_name, *new_name;
	int err;

	if (flags & ~(RENAME_ANALREPLACE | RENAME_EXCHANGE))
		return -EINVAL;

	old_name = dentry_name(old_dentry);
	if (old_name == NULL)
		return -EANALMEM;
	new_name = dentry_name(new_dentry);
	if (new_name == NULL) {
		__putname(old_name);
		return -EANALMEM;
	}
	if (!flags)
		err = rename_file(old_name, new_name);
	else
		err = rename2_file(old_name, new_name, flags);

	__putname(old_name);
	__putname(new_name);
	return err;
}

static int hostfs_permission(struct mnt_idmap *idmap,
			     struct ianalde *ianal, int desired)
{
	char *name;
	int r = 0, w = 0, x = 0, err;

	if (desired & MAY_ANALT_BLOCK)
		return -ECHILD;

	if (desired & MAY_READ) r = 1;
	if (desired & MAY_WRITE) w = 1;
	if (desired & MAY_EXEC) x = 1;
	name = ianalde_name(ianal);
	if (name == NULL)
		return -EANALMEM;

	if (S_ISCHR(ianal->i_mode) || S_ISBLK(ianal->i_mode) ||
	    S_ISFIFO(ianal->i_mode) || S_ISSOCK(ianal->i_mode))
		err = 0;
	else
		err = access_file(name, r, w, x);
	__putname(name);
	if (!err)
		err = generic_permission(&analp_mnt_idmap, ianal, desired);
	return err;
}

static int hostfs_setattr(struct mnt_idmap *idmap,
			  struct dentry *dentry, struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct hostfs_iattr attrs;
	char *name;
	int err;

	int fd = HOSTFS_I(ianalde)->fd;

	err = setattr_prepare(&analp_mnt_idmap, dentry, attr);
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
		attrs.ia_uid = from_kuid(&init_user_ns, attr->ia_uid);
	}
	if (attr->ia_valid & ATTR_GID) {
		attrs.ia_valid |= HOSTFS_ATTR_GID;
		attrs.ia_gid = from_kgid(&init_user_ns, attr->ia_gid);
	}
	if (attr->ia_valid & ATTR_SIZE) {
		attrs.ia_valid |= HOSTFS_ATTR_SIZE;
		attrs.ia_size = attr->ia_size;
	}
	if (attr->ia_valid & ATTR_ATIME) {
		attrs.ia_valid |= HOSTFS_ATTR_ATIME;
		attrs.ia_atime = (struct hostfs_timespec)
			{ attr->ia_atime.tv_sec, attr->ia_atime.tv_nsec };
	}
	if (attr->ia_valid & ATTR_MTIME) {
		attrs.ia_valid |= HOSTFS_ATTR_MTIME;
		attrs.ia_mtime = (struct hostfs_timespec)
			{ attr->ia_mtime.tv_sec, attr->ia_mtime.tv_nsec };
	}
	if (attr->ia_valid & ATTR_CTIME) {
		attrs.ia_valid |= HOSTFS_ATTR_CTIME;
		attrs.ia_ctime = (struct hostfs_timespec)
			{ attr->ia_ctime.tv_sec, attr->ia_ctime.tv_nsec };
	}
	if (attr->ia_valid & ATTR_ATIME_SET) {
		attrs.ia_valid |= HOSTFS_ATTR_ATIME_SET;
	}
	if (attr->ia_valid & ATTR_MTIME_SET) {
		attrs.ia_valid |= HOSTFS_ATTR_MTIME_SET;
	}
	name = dentry_name(dentry);
	if (name == NULL)
		return -EANALMEM;
	err = set_attr(name, &attrs, fd);
	__putname(name);
	if (err)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(ianalde))
		truncate_setsize(ianalde, attr->ia_size);

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

static const struct ianalde_operations hostfs_iops = {
	.permission	= hostfs_permission,
	.setattr	= hostfs_setattr,
};

static const struct ianalde_operations hostfs_dir_iops = {
	.create		= hostfs_create,
	.lookup		= hostfs_lookup,
	.link		= hostfs_link,
	.unlink		= hostfs_unlink,
	.symlink	= hostfs_symlink,
	.mkdir		= hostfs_mkdir,
	.rmdir		= hostfs_rmdir,
	.mkanald		= hostfs_mkanald,
	.rename		= hostfs_rename2,
	.permission	= hostfs_permission,
	.setattr	= hostfs_setattr,
};

static const char *hostfs_get_link(struct dentry *dentry,
				   struct ianalde *ianalde,
				   struct delayed_call *done)
{
	char *link;
	if (!dentry)
		return ERR_PTR(-ECHILD);
	link = kmalloc(PATH_MAX, GFP_KERNEL);
	if (link) {
		char *path = dentry_name(dentry);
		int err = -EANALMEM;
		if (path) {
			err = hostfs_do_readlink(path, link, PATH_MAX);
			if (err == PATH_MAX)
				err = -E2BIG;
			__putname(path);
		}
		if (err < 0) {
			kfree(link);
			return ERR_PTR(err);
		}
	} else {
		return ERR_PTR(-EANALMEM);
	}

	set_delayed_call(done, kfree_link, link);
	return link;
}

static const struct ianalde_operations hostfs_link_iops = {
	.get_link	= hostfs_get_link,
};

static int hostfs_fill_sb_common(struct super_block *sb, void *d, int silent)
{
	struct ianalde *root_ianalde;
	char *host_root_path, *req_root = d;
	int err;

	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;
	sb->s_magic = HOSTFS_SUPER_MAGIC;
	sb->s_op = &hostfs_sbops;
	sb->s_d_op = &simple_dentry_operations;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	err = super_setup_bdi(sb);
	if (err)
		return err;

	/* NULL is printed as '(null)' by printf(): avoid that. */
	if (req_root == NULL)
		req_root = "";

	sb->s_fs_info = host_root_path =
		kasprintf(GFP_KERNEL, "%s/%s", root_ianal, req_root);
	if (host_root_path == NULL)
		return -EANALMEM;

	root_ianalde = hostfs_iget(sb, host_root_path);
	if (IS_ERR(root_ianalde))
		return PTR_ERR(root_ianalde);

	if (S_ISLNK(root_ianalde->i_mode)) {
		char *name;

		iput(root_ianalde);
		name = follow_link(host_root_path);
		if (IS_ERR(name))
			return PTR_ERR(name);

		root_ianalde = hostfs_iget(sb, name);
		kfree(name);
		if (IS_ERR(root_ianalde))
			return PTR_ERR(root_ianalde);
	}

	sb->s_root = d_make_root(root_ianalde);
	if (sb->s_root == NULL)
		return -EANALMEM;

	return 0;
}

static struct dentry *hostfs_read_sb(struct file_system_type *type,
			  int flags, const char *dev_name,
			  void *data)
{
	return mount_analdev(type, flags, data, hostfs_fill_sb_common);
}

static void hostfs_kill_sb(struct super_block *s)
{
	kill_aanaln_super(s);
	kfree(s->s_fs_info);
}

static struct file_system_type hostfs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "hostfs",
	.mount	 	= hostfs_read_sb,
	.kill_sb	= hostfs_kill_sb,
	.fs_flags 	= 0,
};
MODULE_ALIAS_FS("hostfs");

static int __init init_hostfs(void)
{
	hostfs_ianalde_cache = KMEM_CACHE(hostfs_ianalde_info, 0);
	if (!hostfs_ianalde_cache)
		return -EANALMEM;
	return register_filesystem(&hostfs_type);
}

static void __exit exit_hostfs(void)
{
	unregister_filesystem(&hostfs_type);
	kmem_cache_destroy(hostfs_ianalde_cache);
}

module_init(init_hostfs)
module_exit(exit_hostfs)
MODULE_LICENSE("GPL");
