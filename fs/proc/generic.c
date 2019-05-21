// SPDX-License-Identifier: GPL-2.0-only
/*
 * proc/fs/generic.c --- generic routines for the proc-fs
 *
 * This file contains generic proc-fs routines for handling
 * directories and files.
 * 
 * Copyright (C) 1991, 1992 Linus Torvalds.
 * Copyright (C) 1997 Theodore Ts'o
 */

#include <linux/cache.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include "internal.h"

static DEFINE_RWLOCK(proc_subdir_lock);

struct kmem_cache *proc_dir_entry_cache __ro_after_init;

void pde_free(struct proc_dir_entry *pde)
{
	if (S_ISLNK(pde->mode))
		kfree(pde->data);
	if (pde->name != pde->inline_name)
		kfree(pde->name);
	kmem_cache_free(proc_dir_entry_cache, pde);
}

static int proc_match(const char *name, struct proc_dir_entry *de, unsigned int len)
{
	if (len < de->namelen)
		return -1;
	if (len > de->namelen)
		return 1;

	return memcmp(name, de->name, len);
}

static struct proc_dir_entry *pde_subdir_first(struct proc_dir_entry *dir)
{
	return rb_entry_safe(rb_first(&dir->subdir), struct proc_dir_entry,
			     subdir_node);
}

static struct proc_dir_entry *pde_subdir_next(struct proc_dir_entry *dir)
{
	return rb_entry_safe(rb_next(&dir->subdir_node), struct proc_dir_entry,
			     subdir_node);
}

static struct proc_dir_entry *pde_subdir_find(struct proc_dir_entry *dir,
					      const char *name,
					      unsigned int len)
{
	struct rb_node *node = dir->subdir.rb_node;

	while (node) {
		struct proc_dir_entry *de = rb_entry(node,
						     struct proc_dir_entry,
						     subdir_node);
		int result = proc_match(name, de, len);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return de;
	}
	return NULL;
}

static bool pde_subdir_insert(struct proc_dir_entry *dir,
			      struct proc_dir_entry *de)
{
	struct rb_root *root = &dir->subdir;
	struct rb_node **new = &root->rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct proc_dir_entry *this = rb_entry(*new,
						       struct proc_dir_entry,
						       subdir_node);
		int result = proc_match(de->name, this, de->namelen);

		parent = *new;
		if (result < 0)
			new = &(*new)->rb_left;
		else if (result > 0)
			new = &(*new)->rb_right;
		else
			return false;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&de->subdir_node, parent, new);
	rb_insert_color(&de->subdir_node, root);
	return true;
}

static int proc_notify_change(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	struct proc_dir_entry *de = PDE(inode);
	int error;

	error = setattr_prepare(dentry, iattr);
	if (error)
		return error;

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

	proc_set_user(de, inode->i_uid, inode->i_gid);
	de->mode = inode->i_mode;
	return 0;
}

static int proc_getattr(const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct proc_dir_entry *de = PDE(inode);
	if (de && de->nlink)
		set_nlink(inode, de->nlink);

	generic_fillattr(inode, stat);
	return 0;
}

static const struct inode_operations proc_file_inode_operations = {
	.setattr	= proc_notify_change,
};

/*
 * This function parses a name such as "tty/driver/serial", and
 * returns the struct proc_dir_entry for "/proc/tty/driver", and
 * returns "serial" in residual.
 */
static int __xlate_proc_name(const char *name, struct proc_dir_entry **ret,
			     const char **residual)
{
	const char     		*cp = name, *next;
	struct proc_dir_entry	*de;
	unsigned int		len;

	de = *ret;
	if (!de)
		de = &proc_root;

	while (1) {
		next = strchr(cp, '/');
		if (!next)
			break;

		len = next - cp;
		de = pde_subdir_find(de, cp, len);
		if (!de) {
			WARN(1, "name '%s'\n", name);
			return -ENOENT;
		}
		cp += len + 1;
	}
	*residual = cp;
	*ret = de;
	return 0;
}

static int xlate_proc_name(const char *name, struct proc_dir_entry **ret,
			   const char **residual)
{
	int rv;

	read_lock(&proc_subdir_lock);
	rv = __xlate_proc_name(name, ret, residual);
	read_unlock(&proc_subdir_lock);
	return rv;
}

static DEFINE_IDA(proc_inum_ida);

#define PROC_DYNAMIC_FIRST 0xF0000000U

/*
 * Return an inode number between PROC_DYNAMIC_FIRST and
 * 0xffffffff, or zero on failure.
 */
int proc_alloc_inum(unsigned int *inum)
{
	int i;

	i = ida_simple_get(&proc_inum_ida, 0, UINT_MAX - PROC_DYNAMIC_FIRST + 1,
			   GFP_KERNEL);
	if (i < 0)
		return i;

	*inum = PROC_DYNAMIC_FIRST + (unsigned int)i;
	return 0;
}

void proc_free_inum(unsigned int inum)
{
	ida_simple_remove(&proc_inum_ida, inum - PROC_DYNAMIC_FIRST);
}

static int proc_misc_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (atomic_read(&PDE(d_inode(dentry))->in_use) < 0)
		return 0; /* revalidate */
	return 1;
}

static int proc_misc_d_delete(const struct dentry *dentry)
{
	return atomic_read(&PDE(d_inode(dentry))->in_use) < 0;
}

static const struct dentry_operations proc_misc_dentry_ops = {
	.d_revalidate	= proc_misc_d_revalidate,
	.d_delete	= proc_misc_d_delete,
};

/*
 * Don't create negative dentries here, return -ENOENT by hand
 * instead.
 */
struct dentry *proc_lookup_de(struct inode *dir, struct dentry *dentry,
			      struct proc_dir_entry *de)
{
	struct inode *inode;

	read_lock(&proc_subdir_lock);
	de = pde_subdir_find(de, dentry->d_name.name, dentry->d_name.len);
	if (de) {
		pde_get(de);
		read_unlock(&proc_subdir_lock);
		inode = proc_get_inode(dir->i_sb, de);
		if (!inode)
			return ERR_PTR(-ENOMEM);
		d_set_d_op(dentry, de->proc_dops);
		return d_splice_alias(inode, dentry);
	}
	read_unlock(&proc_subdir_lock);
	return ERR_PTR(-ENOENT);
}

struct dentry *proc_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	return proc_lookup_de(dir, dentry, PDE(dir));
}

/*
 * This returns non-zero if at EOF, so that the /proc
 * root directory can use this and check if it should
 * continue with the <pid> entries..
 *
 * Note that the VFS-layer doesn't care about the return
 * value of the readdir() call, as long as it's non-negative
 * for success..
 */
int proc_readdir_de(struct file *file, struct dir_context *ctx,
		    struct proc_dir_entry *de)
{
	int i;

	if (!dir_emit_dots(file, ctx))
		return 0;

	i = ctx->pos - 2;
	read_lock(&proc_subdir_lock);
	de = pde_subdir_first(de);
	for (;;) {
		if (!de) {
			read_unlock(&proc_subdir_lock);
			return 0;
		}
		if (!i)
			break;
		de = pde_subdir_next(de);
		i--;
	}

	do {
		struct proc_dir_entry *next;
		pde_get(de);
		read_unlock(&proc_subdir_lock);
		if (!dir_emit(ctx, de->name, de->namelen,
			    de->low_ino, de->mode >> 12)) {
			pde_put(de);
			return 0;
		}
		ctx->pos++;
		read_lock(&proc_subdir_lock);
		next = pde_subdir_next(de);
		pde_put(de);
		de = next;
	} while (de);
	read_unlock(&proc_subdir_lock);
	return 1;
}

int proc_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);

	return proc_readdir_de(file, ctx, PDE(inode));
}

/*
 * These are the generic /proc directory operations. They
 * use the in-memory "struct proc_dir_entry" tree to parse
 * the /proc directory.
 */
static const struct file_operations proc_dir_operations = {
	.llseek			= generic_file_llseek,
	.read			= generic_read_dir,
	.iterate_shared		= proc_readdir,
};

/*
 * proc directories can do almost nothing..
 */
static const struct inode_operations proc_dir_inode_operations = {
	.lookup		= proc_lookup,
	.getattr	= proc_getattr,
	.setattr	= proc_notify_change,
};

/* returns the registered entry, or frees dp and returns NULL on failure */
struct proc_dir_entry *proc_register(struct proc_dir_entry *dir,
		struct proc_dir_entry *dp)
{
	if (proc_alloc_inum(&dp->low_ino))
		goto out_free_entry;

	write_lock(&proc_subdir_lock);
	dp->parent = dir;
	if (pde_subdir_insert(dir, dp) == false) {
		WARN(1, "proc_dir_entry '%s/%s' already registered\n",
		     dir->name, dp->name);
		write_unlock(&proc_subdir_lock);
		goto out_free_inum;
	}
	write_unlock(&proc_subdir_lock);

	return dp;
out_free_inum:
	proc_free_inum(dp->low_ino);
out_free_entry:
	pde_free(dp);
	return NULL;
}

static struct proc_dir_entry *__proc_create(struct proc_dir_entry **parent,
					  const char *name,
					  umode_t mode,
					  nlink_t nlink)
{
	struct proc_dir_entry *ent = NULL;
	const char *fn;
	struct qstr qstr;

	if (xlate_proc_name(name, parent, &fn) != 0)
		goto out;
	qstr.name = fn;
	qstr.len = strlen(fn);
	if (qstr.len == 0 || qstr.len >= 256) {
		WARN(1, "name len %u\n", qstr.len);
		return NULL;
	}
	if (qstr.len == 1 && fn[0] == '.') {
		WARN(1, "name '.'\n");
		return NULL;
	}
	if (qstr.len == 2 && fn[0] == '.' && fn[1] == '.') {
		WARN(1, "name '..'\n");
		return NULL;
	}
	if (*parent == &proc_root && name_to_int(&qstr) != ~0U) {
		WARN(1, "create '/proc/%s' by hand\n", qstr.name);
		return NULL;
	}
	if (is_empty_pde(*parent)) {
		WARN(1, "attempt to add to permanently empty directory");
		return NULL;
	}

	ent = kmem_cache_zalloc(proc_dir_entry_cache, GFP_KERNEL);
	if (!ent)
		goto out;

	if (qstr.len + 1 <= SIZEOF_PDE_INLINE_NAME) {
		ent->name = ent->inline_name;
	} else {
		ent->name = kmalloc(qstr.len + 1, GFP_KERNEL);
		if (!ent->name) {
			pde_free(ent);
			return NULL;
		}
	}

	memcpy(ent->name, fn, qstr.len + 1);
	ent->namelen = qstr.len;
	ent->mode = mode;
	ent->nlink = nlink;
	ent->subdir = RB_ROOT;
	refcount_set(&ent->refcnt, 1);
	spin_lock_init(&ent->pde_unload_lock);
	INIT_LIST_HEAD(&ent->pde_openers);
	proc_set_user(ent, (*parent)->uid, (*parent)->gid);

	ent->proc_dops = &proc_misc_dentry_ops;

out:
	return ent;
}

struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent, const char *dest)
{
	struct proc_dir_entry *ent;

	ent = __proc_create(&parent, name,
			  (S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO),1);

	if (ent) {
		ent->data = kmalloc((ent->size=strlen(dest))+1, GFP_KERNEL);
		if (ent->data) {
			strcpy((char*)ent->data,dest);
			ent->proc_iops = &proc_link_inode_operations;
			ent = proc_register(parent, ent);
		} else {
			pde_free(ent);
			ent = NULL;
		}
	}
	return ent;
}
EXPORT_SYMBOL(proc_symlink);

struct proc_dir_entry *proc_mkdir_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent, void *data)
{
	struct proc_dir_entry *ent;

	if (mode == 0)
		mode = S_IRUGO | S_IXUGO;

	ent = __proc_create(&parent, name, S_IFDIR | mode, 2);
	if (ent) {
		ent->data = data;
		ent->proc_fops = &proc_dir_operations;
		ent->proc_iops = &proc_dir_inode_operations;
		parent->nlink++;
		ent = proc_register(parent, ent);
		if (!ent)
			parent->nlink--;
	}
	return ent;
}
EXPORT_SYMBOL_GPL(proc_mkdir_data);

struct proc_dir_entry *proc_mkdir_mode(const char *name, umode_t mode,
				       struct proc_dir_entry *parent)
{
	return proc_mkdir_data(name, mode, parent, NULL);
}
EXPORT_SYMBOL(proc_mkdir_mode);

struct proc_dir_entry *proc_mkdir(const char *name,
		struct proc_dir_entry *parent)
{
	return proc_mkdir_data(name, 0, parent, NULL);
}
EXPORT_SYMBOL(proc_mkdir);

struct proc_dir_entry *proc_create_mount_point(const char *name)
{
	umode_t mode = S_IFDIR | S_IRUGO | S_IXUGO;
	struct proc_dir_entry *ent, *parent = NULL;

	ent = __proc_create(&parent, name, mode, 2);
	if (ent) {
		ent->data = NULL;
		ent->proc_fops = NULL;
		ent->proc_iops = NULL;
		parent->nlink++;
		ent = proc_register(parent, ent);
		if (!ent)
			parent->nlink--;
	}
	return ent;
}
EXPORT_SYMBOL(proc_create_mount_point);

struct proc_dir_entry *proc_create_reg(const char *name, umode_t mode,
		struct proc_dir_entry **parent, void *data)
{
	struct proc_dir_entry *p;

	if ((mode & S_IFMT) == 0)
		mode |= S_IFREG;
	if ((mode & S_IALLUGO) == 0)
		mode |= S_IRUGO;
	if (WARN_ON_ONCE(!S_ISREG(mode)))
		return NULL;

	p = __proc_create(parent, name, mode, 1);
	if (p) {
		p->proc_iops = &proc_file_inode_operations;
		p->data = data;
	}
	return p;
}

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		const struct file_operations *proc_fops, void *data)
{
	struct proc_dir_entry *p;

	BUG_ON(proc_fops == NULL);

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	p->proc_fops = proc_fops;
	return proc_register(parent, p);
}
EXPORT_SYMBOL(proc_create_data);
 
struct proc_dir_entry *proc_create(const char *name, umode_t mode,
				   struct proc_dir_entry *parent,
				   const struct file_operations *proc_fops)
{
	return proc_create_data(name, mode, parent, proc_fops, NULL);
}
EXPORT_SYMBOL(proc_create);

static int proc_seq_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de = PDE(inode);

	if (de->state_size)
		return seq_open_private(file, de->seq_ops, de->state_size);
	return seq_open(file, de->seq_ops);
}

static int proc_seq_release(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de = PDE(inode);

	if (de->state_size)
		return seq_release_private(inode, file);
	return seq_release(inode, file);
}

static const struct file_operations proc_seq_fops = {
	.open		= proc_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= proc_seq_release,
};

struct proc_dir_entry *proc_create_seq_private(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	p->proc_fops = &proc_seq_fops;
	p->seq_ops = ops;
	p->state_size = state_size;
	return proc_register(parent, p);
}
EXPORT_SYMBOL(proc_create_seq_private);

static int proc_single_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de = PDE(inode);

	return single_open(file, de->single_show, de->data);
}

static const struct file_operations proc_single_fops = {
	.open		= proc_single_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct proc_dir_entry *proc_create_single_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	p->proc_fops = &proc_single_fops;
	p->single_show = show;
	return proc_register(parent, p);
}
EXPORT_SYMBOL(proc_create_single_data);

void proc_set_size(struct proc_dir_entry *de, loff_t size)
{
	de->size = size;
}
EXPORT_SYMBOL(proc_set_size);

void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid)
{
	de->uid = uid;
	de->gid = gid;
}
EXPORT_SYMBOL(proc_set_user);

void pde_put(struct proc_dir_entry *pde)
{
	if (refcount_dec_and_test(&pde->refcnt)) {
		proc_free_inum(pde->low_ino);
		pde_free(pde);
	}
}

/*
 * Remove a /proc entry and free it if it's not currently in use.
 */
void remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *de = NULL;
	const char *fn = name;
	unsigned int len;

	write_lock(&proc_subdir_lock);
	if (__xlate_proc_name(name, &parent, &fn) != 0) {
		write_unlock(&proc_subdir_lock);
		return;
	}
	len = strlen(fn);

	de = pde_subdir_find(parent, fn, len);
	if (de)
		rb_erase(&de->subdir_node, &parent->subdir);
	write_unlock(&proc_subdir_lock);
	if (!de) {
		WARN(1, "name '%s'\n", name);
		return;
	}

	proc_entry_rundown(de);

	if (S_ISDIR(de->mode))
		parent->nlink--;
	de->nlink = 0;
	WARN(pde_subdir_first(de),
	     "%s: removing non-empty directory '%s/%s', leaking at least '%s'\n",
	     __func__, de->parent->name, de->name, pde_subdir_first(de)->name);
	pde_put(de);
}
EXPORT_SYMBOL(remove_proc_entry);

int remove_proc_subtree(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *root = NULL, *de, *next;
	const char *fn = name;
	unsigned int len;

	write_lock(&proc_subdir_lock);
	if (__xlate_proc_name(name, &parent, &fn) != 0) {
		write_unlock(&proc_subdir_lock);
		return -ENOENT;
	}
	len = strlen(fn);

	root = pde_subdir_find(parent, fn, len);
	if (!root) {
		write_unlock(&proc_subdir_lock);
		return -ENOENT;
	}
	rb_erase(&root->subdir_node, &parent->subdir);

	de = root;
	while (1) {
		next = pde_subdir_first(de);
		if (next) {
			rb_erase(&next->subdir_node, &de->subdir);
			de = next;
			continue;
		}
		write_unlock(&proc_subdir_lock);

		proc_entry_rundown(de);
		next = de->parent;
		if (S_ISDIR(de->mode))
			next->nlink--;
		de->nlink = 0;
		if (de == root)
			break;
		pde_put(de);

		write_lock(&proc_subdir_lock);
		de = next;
	}
	pde_put(root);
	return 0;
}
EXPORT_SYMBOL(remove_proc_subtree);

void *proc_get_parent_data(const struct inode *inode)
{
	struct proc_dir_entry *de = PDE(inode);
	return de->parent->data;
}
EXPORT_SYMBOL_GPL(proc_get_parent_data);

void proc_remove(struct proc_dir_entry *de)
{
	if (de)
		remove_proc_subtree(de->name, de->parent);
}
EXPORT_SYMBOL(proc_remove);

void *PDE_DATA(const struct inode *inode)
{
	return __PDE_DATA(inode);
}
EXPORT_SYMBOL(PDE_DATA);

/*
 * Pull a user buffer into memory and pass it to the file's write handler if
 * one is supplied.  The ->write() method is permitted to modify the
 * kernel-side buffer.
 */
ssize_t proc_simple_write(struct file *f, const char __user *ubuf, size_t size,
			  loff_t *_pos)
{
	struct proc_dir_entry *pde = PDE(file_inode(f));
	char *buf;
	int ret;

	if (!pde->write)
		return -EACCES;
	if (size == 0 || size > PAGE_SIZE - 1)
		return -EINVAL;
	buf = memdup_user_nul(ubuf, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	ret = pde->write(f, buf, size);
	kfree(buf);
	return ret == 0 ? size : ret;
}
