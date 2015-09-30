/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/rbtree.h>
#include <linux/security.h>
#include <linux/cred.h>
#include "overlayfs.h"

struct ovl_cache_entry {
	const char *name;
	unsigned int len;
	unsigned int type;
	u64 ino;
	bool is_whiteout;
	struct list_head l_node;
	struct rb_node node;
};

struct ovl_readdir_data {
	struct rb_root *root;
	struct list_head *list;
	struct list_head *middle;
	struct dentry *dir;
	int count;
	int err;
};

struct ovl_dir_file {
	bool is_real;
	bool is_cached;
	struct list_head cursor;
	u64 cache_version;
	struct list_head cache;
	struct file *realfile;
};

static struct ovl_cache_entry *ovl_cache_entry_from_node(struct rb_node *n)
{
	return container_of(n, struct ovl_cache_entry, node);
}

static struct ovl_cache_entry *ovl_cache_entry_find(struct rb_root *root,
						    const char *name, int len)
{
	struct rb_node *node = root->rb_node;
	int cmp;

	while (node) {
		struct ovl_cache_entry *p = ovl_cache_entry_from_node(node);

		cmp = strncmp(name, p->name, len);
		if (cmp > 0)
			node = p->node.rb_right;
		else if (cmp < 0 || len < p->len)
			node = p->node.rb_left;
		else
			return p;
	}

	return NULL;
}

static struct ovl_cache_entry *ovl_cache_entry_new(const char *name, int len,
						   u64 ino, unsigned int d_type)
{
	struct ovl_cache_entry *p;

	p = kmalloc(sizeof(*p) + len + 1, GFP_KERNEL);
	if (p) {
		char *name_copy = (char *) (p + 1);
		memcpy(name_copy, name, len);
		name_copy[len] = '\0';
		p->name = name_copy;
		p->len = len;
		p->type = d_type;
		p->ino = ino;
		p->is_whiteout = false;
	}

	return p;
}

static int ovl_cache_entry_add_rb(struct ovl_readdir_data *rdd,
				  const char *name, int len, u64 ino,
				  unsigned int d_type)
{
	struct rb_node **newp = &rdd->root->rb_node;
	struct rb_node *parent = NULL;
	struct ovl_cache_entry *p;

	while (*newp) {
		int cmp;
		struct ovl_cache_entry *tmp;

		parent = *newp;
		tmp = ovl_cache_entry_from_node(*newp);
		cmp = strncmp(name, tmp->name, len);
		if (cmp > 0)
			newp = &tmp->node.rb_right;
		else if (cmp < 0 || len < tmp->len)
			newp = &tmp->node.rb_left;
		else
			return 0;
	}

	p = ovl_cache_entry_new(name, len, ino, d_type);
	if (p == NULL)
		return -ENOMEM;

	list_add_tail(&p->l_node, rdd->list);
	rb_link_node(&p->node, parent, newp);
	rb_insert_color(&p->node, rdd->root);

	return 0;
}

static int ovl_fill_lower(void *buf, const char *name, int namelen,
			    loff_t offset, u64 ino, unsigned int d_type)
{
	struct ovl_readdir_data *rdd = buf;
	struct ovl_cache_entry *p;

	rdd->count++;
	p = ovl_cache_entry_find(rdd->root, name, namelen);
	if (p) {
		list_move_tail(&p->l_node, rdd->middle);
	} else {
		p = ovl_cache_entry_new(name, namelen, ino, d_type);
		if (p == NULL)
			rdd->err = -ENOMEM;
		else
			list_add_tail(&p->l_node, rdd->middle);
	}

	return rdd->err;
}

static void ovl_cache_free(struct list_head *list)
{
	struct ovl_cache_entry *p;
	struct ovl_cache_entry *n;

	list_for_each_entry_safe(p, n, list, l_node)
		kfree(p);

	INIT_LIST_HEAD(list);
}

static int ovl_fill_upper(void *buf, const char *name, int namelen,
			  loff_t offset, u64 ino, unsigned int d_type)
{
	struct ovl_readdir_data *rdd = buf;

	rdd->count++;
	return ovl_cache_entry_add_rb(rdd, name, namelen, ino, d_type);
}

static inline int ovl_dir_read(struct path *realpath,
			       struct ovl_readdir_data *rdd, filldir_t filler)
{
	struct file *realfile;
	int err;

	realfile = ovl_path_open(realpath, O_RDONLY | O_DIRECTORY);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	do {
		rdd->count = 0;
		rdd->err = 0;
		err = vfs_readdir(realfile, filler, rdd);
		if (err >= 0)
			err = rdd->err;
	} while (!err && rdd->count);
	fput(realfile);

	return 0;
}

static void ovl_dir_reset(struct file *file)
{
	struct ovl_dir_file *od = file->private_data;
	enum ovl_path_type type = ovl_path_type(file->f_path.dentry);

	if (ovl_dentry_version_get(file->f_path.dentry) != od->cache_version) {
		list_del_init(&od->cursor);
		ovl_cache_free(&od->cache);
		od->is_cached = false;
	}
	WARN_ON(!od->is_real && type != OVL_PATH_MERGE);
	if (od->is_real && type == OVL_PATH_MERGE) {
		fput(od->realfile);
		od->realfile = NULL;
		od->is_real = false;
	}
}

static int ovl_dir_mark_whiteouts(struct ovl_readdir_data *rdd)
{
	struct ovl_cache_entry *p;
	struct dentry *dentry;
	const struct cred *old_cred;
	struct cred *override_cred;

	override_cred = prepare_creds();
	if (!override_cred) {
		ovl_cache_free(rdd->list);
		return -ENOMEM;
	}

	/*
	 * CAP_SYS_ADMIN for getxattr
	 * CAP_DAC_OVERRIDE for lookup
	 */
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	cap_raise(override_cred->cap_effective, CAP_DAC_OVERRIDE);
	old_cred = override_creds(override_cred);

	mutex_lock(&rdd->dir->d_inode->i_mutex);
	list_for_each_entry(p, rdd->list, l_node) {
		if (p->type != DT_LNK)
			continue;

		dentry = lookup_one_len(p->name, rdd->dir, p->len);
		if (IS_ERR(dentry))
			continue;

		p->is_whiteout = ovl_is_whiteout(dentry);
		dput(dentry);
	}
	mutex_unlock(&rdd->dir->d_inode->i_mutex);

	revert_creds(old_cred);
	put_cred(override_cred);

	return 0;
}

static inline int ovl_dir_read_merged(struct path *upperpath,
				      struct path *lowerpath,
				      struct ovl_readdir_data *rdd)
{
	int err;
	struct rb_root root = RB_ROOT;
	struct list_head middle;

	rdd->root = &root;
	if (upperpath->dentry) {
		rdd->dir = upperpath->dentry;
		err = ovl_dir_read(upperpath, rdd, ovl_fill_upper);
		if (err)
			goto out;

		err = ovl_dir_mark_whiteouts(rdd);
		if (err)
			goto out;
	}
	/*
	 * Insert lowerpath entries before upperpath ones, this allows
	 * offsets to be reasonably constant
	 */
	list_add(&middle, rdd->list);
	rdd->middle = &middle;
	err = ovl_dir_read(lowerpath, rdd, ovl_fill_lower);
	list_del(&middle);
out:
	rdd->root = NULL;

	return err;
}

static void ovl_seek_cursor(struct ovl_dir_file *od, loff_t pos)
{
	struct list_head *l;
	loff_t off;

	l = od->cache.next;
	for (off = 0; off < pos; off++) {
		if (l == &od->cache)
			break;
		l = l->next;
	}
	list_move_tail(&od->cursor, l);
}

static int ovl_readdir(struct file *file, void *buf, filldir_t filler)
{
	struct ovl_dir_file *od = file->private_data;
	int res;

	if (!file->f_pos)
		ovl_dir_reset(file);

	if (od->is_real) {
		res = vfs_readdir(od->realfile, filler, buf);
		file->f_pos = od->realfile->f_pos;

		return res;
	}

	if (!od->is_cached) {
		struct path lowerpath;
		struct path upperpath;
		struct ovl_readdir_data rdd = { .list = &od->cache };

		ovl_path_lower(file->f_path.dentry, &lowerpath);
		ovl_path_upper(file->f_path.dentry, &upperpath);

		res = ovl_dir_read_merged(&upperpath, &lowerpath, &rdd);
		if (res) {
			ovl_cache_free(rdd.list);
			return res;
		}

		od->cache_version = ovl_dentry_version_get(file->f_path.dentry);
		od->is_cached = true;

		ovl_seek_cursor(od, file->f_pos);
	}

	while (od->cursor.next != &od->cache) {
		int over;
		loff_t off;
		struct ovl_cache_entry *p;

		p = list_entry(od->cursor.next, struct ovl_cache_entry, l_node);
		off = file->f_pos;
		if (!p->is_whiteout) {
			over = filler(buf, p->name, p->len, off, p->ino,
				      p->type);
			if (over)
				break;
		}
		file->f_pos++;
		list_move(&od->cursor, &p->l_node);
	}

	return 0;
}

static loff_t ovl_dir_llseek(struct file *file, loff_t offset, int origin)
{
	loff_t res;
	struct ovl_dir_file *od = file->private_data;

	mutex_lock(&file_inode(file)->i_mutex);
	if (!file->f_pos)
		ovl_dir_reset(file);

	if (od->is_real) {
		res = vfs_llseek(od->realfile, offset, origin);
		file->f_pos = od->realfile->f_pos;
	} else {
		res = -EINVAL;

		switch (origin) {
		case SEEK_CUR:
			offset += file->f_pos;
			break;
		case SEEK_SET:
			break;
		default:
			goto out_unlock;
		}
		if (offset < 0)
			goto out_unlock;

		if (offset != file->f_pos) {
			file->f_pos = offset;
			if (od->is_cached)
				ovl_seek_cursor(od, offset);
		}
		res = offset;
	}
out_unlock:
	mutex_unlock(&file_inode(file)->i_mutex);

	return res;
}

static int ovl_dir_fsync(struct file *file, loff_t start, loff_t end,
			 int datasync)
{
	struct ovl_dir_file *od = file->private_data;

	/* May need to reopen directory if it got copied up */
	if (!od->realfile) {
		struct path upperpath;

		ovl_path_upper(file->f_path.dentry, &upperpath);
		od->realfile = ovl_path_open(&upperpath, O_RDONLY);
		if (IS_ERR(od->realfile))
			return PTR_ERR(od->realfile);
	}

	return vfs_fsync_range(od->realfile, start, end, datasync);
}

static int ovl_dir_release(struct inode *inode, struct file *file)
{
	struct ovl_dir_file *od = file->private_data;

	list_del(&od->cursor);
	ovl_cache_free(&od->cache);
	if (od->realfile)
		fput(od->realfile);
	kfree(od);

	return 0;
}

static int ovl_dir_open(struct inode *inode, struct file *file)
{
	struct path realpath;
	struct file *realfile;
	struct ovl_dir_file *od;
	enum ovl_path_type type;

	od = kzalloc(sizeof(struct ovl_dir_file), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	type = ovl_path_real(file->f_path.dentry, &realpath);
	realfile = ovl_path_open(&realpath, file->f_flags);
	if (IS_ERR(realfile)) {
		kfree(od);
		return PTR_ERR(realfile);
	}
	INIT_LIST_HEAD(&od->cache);
	INIT_LIST_HEAD(&od->cursor);
	od->is_cached = false;
	od->realfile = realfile;
	od->is_real = (type != OVL_PATH_MERGE);
	file->private_data = od;

	return 0;
}

const struct file_operations ovl_dir_operations = {
	.read		= generic_read_dir,
	.open		= ovl_dir_open,
	.readdir	= ovl_readdir,
	.llseek		= ovl_dir_llseek,
	.fsync		= ovl_dir_fsync,
	.release	= ovl_dir_release,
};

static int ovl_check_empty_dir(struct dentry *dentry, struct list_head *list)
{
	int err;
	struct path lowerpath;
	struct path upperpath;
	struct ovl_cache_entry *p;
	struct ovl_readdir_data rdd = { .list = list };

	ovl_path_upper(dentry, &upperpath);
	ovl_path_lower(dentry, &lowerpath);

	err = ovl_dir_read_merged(&upperpath, &lowerpath, &rdd);
	if (err)
		return err;

	err = 0;

	list_for_each_entry(p, list, l_node) {
		if (p->is_whiteout)
			continue;

		if (p->name[0] == '.') {
			if (p->len == 1)
				continue;
			if (p->len == 2 && p->name[1] == '.')
				continue;
		}
		err = -ENOTEMPTY;
		break;
	}

	return err;
}

static int ovl_remove_whiteouts(struct dentry *dir, struct list_head *list)
{
	struct path upperpath;
	struct dentry *upperdir;
	struct ovl_cache_entry *p;
	const struct cred *old_cred;
	struct cred *override_cred;
	int err;

	ovl_path_upper(dir, &upperpath);
	upperdir = upperpath.dentry;

	override_cred = prepare_creds();
	if (!override_cred)
		return -ENOMEM;

	/*
	 * CAP_DAC_OVERRIDE for lookup and unlink
	 * CAP_SYS_ADMIN for setxattr of "trusted" namespace
	 * CAP_FOWNER for unlink in sticky directory
	 */
	cap_raise(override_cred->cap_effective, CAP_DAC_OVERRIDE);
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	cap_raise(override_cred->cap_effective, CAP_FOWNER);
	old_cred = override_creds(override_cred);

	err = vfs_setxattr(upperdir, ovl_opaque_xattr, "y", 1, 0);
	if (err)
		goto out_revert_creds;

	mutex_lock_nested(&upperdir->d_inode->i_mutex, I_MUTEX_PARENT);
	list_for_each_entry(p, list, l_node) {
		struct dentry *dentry;
		int ret;

		if (!p->is_whiteout)
			continue;

		dentry = lookup_one_len(p->name, upperdir, p->len);
		if (IS_ERR(dentry)) {
			pr_warn(
			    "overlayfs: failed to lookup whiteout %.*s: %li\n",
			    p->len, p->name, PTR_ERR(dentry));
			continue;
		}
		ret = vfs_unlink(upperdir->d_inode, dentry);
		dput(dentry);
		if (ret)
			pr_warn(
			    "overlayfs: failed to unlink whiteout %.*s: %i\n",
			    p->len, p->name, ret);
	}
	mutex_unlock(&upperdir->d_inode->i_mutex);

out_revert_creds:
	revert_creds(old_cred);
	put_cred(override_cred);

	return err;
}

int ovl_check_empty_and_clear(struct dentry *dentry, enum ovl_path_type type)
{
	int err;
	LIST_HEAD(list);

	err = ovl_check_empty_dir(dentry, &list);
	if (!err && type == OVL_PATH_MERGE)
		err = ovl_remove_whiteouts(dentry, &list);

	ovl_cache_free(&list);

	return err;
}
