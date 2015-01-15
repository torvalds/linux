/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include "overlayfs.h"

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Overlay filesystem");
MODULE_LICENSE("GPL");

#define OVERLAYFS_SUPER_MAGIC 0x794c7630

struct ovl_config {
	char *lowerdir;
	char *upperdir;
	char *workdir;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	struct vfsmount *upper_mnt;
	unsigned numlower;
	struct vfsmount **lower_mnt;
	struct dentry *workdir;
	long lower_namelen;
	/* pathnames of lower and upper dirs, for show_options */
	struct ovl_config config;
};

struct ovl_dir_cache;

/* private information held for every overlayfs dentry */
struct ovl_entry {
	struct dentry *__upperdentry;
	struct ovl_dir_cache *cache;
	union {
		struct {
			u64 version;
			bool opaque;
		};
		struct rcu_head rcu;
	};
	unsigned numlower;
	struct path lowerstack[];
};

#define OVL_MAX_STACK 500

static struct dentry *__ovl_dentry_lower(struct ovl_entry *oe)
{
	return oe->numlower ? oe->lowerstack[0].dentry : NULL;
}

enum ovl_path_type ovl_path_type(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	enum ovl_path_type type = 0;

	if (oe->__upperdentry) {
		type = __OVL_PATH_UPPER;

		if (oe->numlower) {
			if (S_ISDIR(dentry->d_inode->i_mode))
				type |= __OVL_PATH_MERGE;
		} else if (!oe->opaque) {
			type |= __OVL_PATH_PURE;
		}
	} else {
		if (oe->numlower > 1)
			type |= __OVL_PATH_MERGE;
	}
	return type;
}

static struct dentry *ovl_upperdentry_dereference(struct ovl_entry *oe)
{
	return lockless_dereference(oe->__upperdentry);
}

void ovl_path_upper(struct dentry *dentry, struct path *path)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct ovl_entry *oe = dentry->d_fsdata;

	path->mnt = ofs->upper_mnt;
	path->dentry = ovl_upperdentry_dereference(oe);
}

enum ovl_path_type ovl_path_real(struct dentry *dentry, struct path *path)
{
	enum ovl_path_type type = ovl_path_type(dentry);

	if (!OVL_TYPE_UPPER(type))
		ovl_path_lower(dentry, path);
	else
		ovl_path_upper(dentry, path);

	return type;
}

struct dentry *ovl_dentry_upper(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return ovl_upperdentry_dereference(oe);
}

struct dentry *ovl_dentry_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return __ovl_dentry_lower(oe);
}

struct dentry *ovl_dentry_real(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	struct dentry *realdentry;

	realdentry = ovl_upperdentry_dereference(oe);
	if (!realdentry)
		realdentry = __ovl_dentry_lower(oe);

	return realdentry;
}

struct dentry *ovl_entry_real(struct ovl_entry *oe, bool *is_upper)
{
	struct dentry *realdentry;

	realdentry = ovl_upperdentry_dereference(oe);
	if (realdentry) {
		*is_upper = true;
	} else {
		realdentry = __ovl_dentry_lower(oe);
		*is_upper = false;
	}
	return realdentry;
}

struct ovl_dir_cache *ovl_dir_cache(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->cache;
}

void ovl_set_dir_cache(struct dentry *dentry, struct ovl_dir_cache *cache)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	oe->cache = cache;
}

void ovl_path_lower(struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	*path = oe->numlower ? oe->lowerstack[0] : (struct path) { NULL, NULL };
}

int ovl_want_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	return mnt_want_write(ofs->upper_mnt);
}

void ovl_drop_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	mnt_drop_write(ofs->upper_mnt);
}

struct dentry *ovl_workdir(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	return ofs->workdir;
}

bool ovl_dentry_is_opaque(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	return oe->opaque;
}

void ovl_dentry_set_opaque(struct dentry *dentry, bool opaque)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	oe->opaque = opaque;
}

void ovl_dentry_update(struct dentry *dentry, struct dentry *upperdentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	WARN_ON(!mutex_is_locked(&upperdentry->d_parent->d_inode->i_mutex));
	WARN_ON(oe->__upperdentry);
	BUG_ON(!upperdentry->d_inode);
	/*
	 * Make sure upperdentry is consistent before making it visible to
	 * ovl_upperdentry_dereference().
	 */
	smp_wmb();
	oe->__upperdentry = upperdentry;
}

void ovl_dentry_version_inc(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	WARN_ON(!mutex_is_locked(&dentry->d_inode->i_mutex));
	oe->version++;
}

u64 ovl_dentry_version_get(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	WARN_ON(!mutex_is_locked(&dentry->d_inode->i_mutex));
	return oe->version;
}

bool ovl_is_whiteout(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	return inode && IS_WHITEOUT(inode);
}

static bool ovl_is_opaquedir(struct dentry *dentry)
{
	int res;
	char val;
	struct inode *inode = dentry->d_inode;

	if (!S_ISDIR(inode->i_mode) || !inode->i_op->getxattr)
		return false;

	res = inode->i_op->getxattr(dentry, OVL_XATTR_OPAQUE, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
}

static void ovl_dentry_release(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe) {
		unsigned int i;

		dput(oe->__upperdentry);
		for (i = 0; i < oe->numlower; i++)
			dput(oe->lowerstack[i].dentry);
		kfree_rcu(oe, rcu);
	}
}

static const struct dentry_operations ovl_dentry_operations = {
	.d_release = ovl_dentry_release,
};

static struct ovl_entry *ovl_alloc_entry(unsigned int numlower)
{
	size_t size = offsetof(struct ovl_entry, lowerstack[numlower]);
	struct ovl_entry *oe = kzalloc(size, GFP_KERNEL);

	if (oe)
		oe->numlower = numlower;

	return oe;
}

static inline struct dentry *ovl_lookup_real(struct dentry *dir,
					     struct qstr *name)
{
	struct dentry *dentry;

	mutex_lock(&dir->d_inode->i_mutex);
	dentry = lookup_one_len(name->name, dir, name->len);
	mutex_unlock(&dir->d_inode->i_mutex);

	if (IS_ERR(dentry)) {
		if (PTR_ERR(dentry) == -ENOENT)
			dentry = NULL;
	} else if (!dentry->d_inode) {
		dput(dentry);
		dentry = NULL;
	}
	return dentry;
}

/*
 * Returns next layer in stack starting from top.
 * Returns -1 if this is the last layer.
 */
int ovl_path_next(int idx, struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	BUG_ON(idx < 0);
	if (idx == 0) {
		ovl_path_upper(dentry, path);
		if (path->dentry)
			return oe->numlower ? 1 : -1;
		idx++;
	}
	BUG_ON(idx > oe->numlower);
	*path = oe->lowerstack[idx - 1];

	return (idx < oe->numlower) ? idx + 1 : -1;
}

struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags)
{
	struct ovl_entry *oe;
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	struct path *stack = NULL;
	struct dentry *upperdir, *upperdentry = NULL;
	unsigned int ctr = 0;
	struct inode *inode = NULL;
	bool upperopaque = false;
	struct dentry *this, *prev = NULL;
	unsigned int i;
	int err;

	upperdir = ovl_upperdentry_dereference(poe);
	if (upperdir) {
		this = ovl_lookup_real(upperdir, &dentry->d_name);
		err = PTR_ERR(this);
		if (IS_ERR(this))
			goto out;

		if (this) {
			if (ovl_is_whiteout(this)) {
				dput(this);
				this = NULL;
				upperopaque = true;
			} else if (poe->numlower && ovl_is_opaquedir(this)) {
				upperopaque = true;
			}
		}
		upperdentry = prev = this;
	}

	if (!upperopaque && poe->numlower) {
		err = -ENOMEM;
		stack = kcalloc(poe->numlower, sizeof(struct path), GFP_KERNEL);
		if (!stack)
			goto out_put_upper;
	}

	for (i = 0; !upperopaque && i < poe->numlower; i++) {
		bool opaque = false;
		struct path lowerpath = poe->lowerstack[i];

		this = ovl_lookup_real(lowerpath.dentry, &dentry->d_name);
		err = PTR_ERR(this);
		if (IS_ERR(this)) {
			/*
			 * If it's positive, then treat ENAMETOOLONG as ENOENT.
			 */
			if (err == -ENAMETOOLONG && (upperdentry || ctr))
				continue;
			goto out_put;
		}
		if (!this)
			continue;
		if (ovl_is_whiteout(this)) {
			dput(this);
			break;
		}
		/*
		 * Only makes sense to check opaque dir if this is not the
		 * lowermost layer.
		 */
		if (i < poe->numlower - 1 && ovl_is_opaquedir(this))
			opaque = true;

		if (prev && (!S_ISDIR(prev->d_inode->i_mode) ||
			     !S_ISDIR(this->d_inode->i_mode))) {
			/*
			 * FIXME: check for upper-opaqueness maybe better done
			 * in remove code.
			 */
			if (prev == upperdentry)
				upperopaque = true;
			dput(this);
			break;
		}
		/*
		 * If this is a non-directory then stop here.
		 */
		if (!S_ISDIR(this->d_inode->i_mode))
			opaque = true;

		stack[ctr].dentry = this;
		stack[ctr].mnt = lowerpath.mnt;
		ctr++;
		prev = this;
		if (opaque)
			break;
	}

	oe = ovl_alloc_entry(ctr);
	err = -ENOMEM;
	if (!oe)
		goto out_put;

	if (upperdentry || ctr) {
		struct dentry *realdentry;

		realdentry = upperdentry ? upperdentry : stack[0].dentry;

		err = -ENOMEM;
		inode = ovl_new_inode(dentry->d_sb, realdentry->d_inode->i_mode,
				      oe);
		if (!inode)
			goto out_free_oe;
		ovl_copyattr(realdentry->d_inode, inode);
	}

	oe->opaque = upperopaque;
	oe->__upperdentry = upperdentry;
	memcpy(oe->lowerstack, stack, sizeof(struct path) * ctr);
	kfree(stack);
	dentry->d_fsdata = oe;
	d_add(dentry, inode);

	return NULL;

out_free_oe:
	kfree(oe);
out_put:
	for (i = 0; i < ctr; i++)
		dput(stack[i].dentry);
	kfree(stack);
out_put_upper:
	dput(upperdentry);
out:
	return ERR_PTR(err);
}

struct file *ovl_path_open(struct path *path, int flags)
{
	return dentry_open(path, flags, current_cred());
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_fs *ufs = sb->s_fs_info;
	unsigned i;

	dput(ufs->workdir);
	mntput(ufs->upper_mnt);
	for (i = 0; i < ufs->numlower; i++)
		mntput(ufs->lower_mnt[i]);

	kfree(ufs->config.lowerdir);
	kfree(ufs->config.upperdir);
	kfree(ufs->config.workdir);
	kfree(ufs);
}

/**
 * ovl_statfs
 * @sb: The overlayfs super block
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics.  As writes always target the upper layer
 * filesystem pass the statfs to the upper filesystem (if it exists)
 */
static int ovl_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct dentry *root_dentry = dentry->d_sb->s_root;
	struct path path;
	int err;

	ovl_path_real(root_dentry, &path);

	err = vfs_statfs(&path, buf);
	if (!err) {
		buf->f_namelen = max(buf->f_namelen, ofs->lower_namelen);
		buf->f_type = OVERLAYFS_SUPER_MAGIC;
	}

	return err;
}

/**
 * ovl_show_options
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does not fail.
 */
static int ovl_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct ovl_fs *ufs = sb->s_fs_info;

	seq_printf(m, ",lowerdir=%s", ufs->config.lowerdir);
	if (ufs->config.upperdir) {
		seq_printf(m, ",upperdir=%s", ufs->config.upperdir);
		seq_printf(m, ",workdir=%s", ufs->config.workdir);
	}
	return 0;
}

static int ovl_remount(struct super_block *sb, int *flags, char *data)
{
	struct ovl_fs *ufs = sb->s_fs_info;

	if (!(*flags & MS_RDONLY) &&
	    (!ufs->upper_mnt || (ufs->upper_mnt->mnt_sb->s_flags & MS_RDONLY)))
		return -EROFS;

	return 0;
}

static const struct super_operations ovl_super_operations = {
	.put_super	= ovl_put_super,
	.statfs		= ovl_statfs,
	.show_options	= ovl_show_options,
	.remount_fs	= ovl_remount,
};

enum {
	OPT_LOWERDIR,
	OPT_UPPERDIR,
	OPT_WORKDIR,
	OPT_ERR,
};

static const match_table_t ovl_tokens = {
	{OPT_LOWERDIR,			"lowerdir=%s"},
	{OPT_UPPERDIR,			"upperdir=%s"},
	{OPT_WORKDIR,			"workdir=%s"},
	{OPT_ERR,			NULL}
};

static char *ovl_next_opt(char **s)
{
	char *sbegin = *s;
	char *p;

	if (sbegin == NULL)
		return NULL;

	for (p = sbegin; *p; p++) {
		if (*p == '\\') {
			p++;
			if (!*p)
				break;
		} else if (*p == ',') {
			*p = '\0';
			*s = p + 1;
			return sbegin;
		}
	}
	*s = NULL;
	return sbegin;
}

static int ovl_parse_opt(char *opt, struct ovl_config *config)
{
	char *p;

	while ((p = ovl_next_opt(&opt)) != NULL) {
		int token;
		substring_t args[MAX_OPT_ARGS];

		if (!*p)
			continue;

		token = match_token(p, ovl_tokens, args);
		switch (token) {
		case OPT_UPPERDIR:
			kfree(config->upperdir);
			config->upperdir = match_strdup(&args[0]);
			if (!config->upperdir)
				return -ENOMEM;
			break;

		case OPT_LOWERDIR:
			kfree(config->lowerdir);
			config->lowerdir = match_strdup(&args[0]);
			if (!config->lowerdir)
				return -ENOMEM;
			break;

		case OPT_WORKDIR:
			kfree(config->workdir);
			config->workdir = match_strdup(&args[0]);
			if (!config->workdir)
				return -ENOMEM;
			break;

		default:
			pr_err("overlayfs: unrecognized mount option \"%s\" or missing value\n", p);
			return -EINVAL;
		}
	}
	return 0;
}

#define OVL_WORKDIR_NAME "work"

static struct dentry *ovl_workdir_create(struct vfsmount *mnt,
					 struct dentry *dentry)
{
	struct inode *dir = dentry->d_inode;
	struct dentry *work;
	int err;
	bool retried = false;

	err = mnt_want_write(mnt);
	if (err)
		return ERR_PTR(err);

	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);
retry:
	work = lookup_one_len(OVL_WORKDIR_NAME, dentry,
			      strlen(OVL_WORKDIR_NAME));

	if (!IS_ERR(work)) {
		struct kstat stat = {
			.mode = S_IFDIR | 0,
		};

		if (work->d_inode) {
			err = -EEXIST;
			if (retried)
				goto out_dput;

			retried = true;
			ovl_cleanup(dir, work);
			dput(work);
			goto retry;
		}

		err = ovl_create_real(dir, work, &stat, NULL, NULL, true);
		if (err)
			goto out_dput;
	}
out_unlock:
	mutex_unlock(&dir->i_mutex);
	mnt_drop_write(mnt);

	return work;

out_dput:
	dput(work);
	work = ERR_PTR(err);
	goto out_unlock;
}

static void ovl_unescape(char *s)
{
	char *d = s;

	for (;; s++, d++) {
		if (*s == '\\')
			s++;
		*d = *s;
		if (!*s)
			break;
	}
}

static bool ovl_is_allowed_fs_type(struct dentry *root)
{
	const struct dentry_operations *dop = root->d_op;

	/*
	 * We don't support:
	 *  - automount filesystems
	 *  - filesystems with revalidate (FIXME for lower layer)
	 *  - filesystems with case insensitive names
	 */
	if (dop &&
	    (dop->d_manage || dop->d_automount ||
	     dop->d_revalidate || dop->d_weak_revalidate ||
	     dop->d_compare || dop->d_hash)) {
		return false;
	}
	return true;
}

static int ovl_mount_dir_noesc(const char *name, struct path *path)
{
	int err = -EINVAL;

	if (!*name) {
		pr_err("overlayfs: empty lowerdir\n");
		goto out;
	}
	err = kern_path(name, LOOKUP_FOLLOW, path);
	if (err) {
		pr_err("overlayfs: failed to resolve '%s': %i\n", name, err);
		goto out;
	}
	err = -EINVAL;
	if (!ovl_is_allowed_fs_type(path->dentry)) {
		pr_err("overlayfs: filesystem on '%s' not supported\n", name);
		goto out_put;
	}
	if (!S_ISDIR(path->dentry->d_inode->i_mode)) {
		pr_err("overlayfs: '%s' not a directory\n", name);
		goto out_put;
	}
	return 0;

out_put:
	path_put(path);
out:
	return err;
}

static int ovl_mount_dir(const char *name, struct path *path)
{
	int err = -ENOMEM;
	char *tmp = kstrdup(name, GFP_KERNEL);

	if (tmp) {
		ovl_unescape(tmp);
		err = ovl_mount_dir_noesc(tmp, path);
		kfree(tmp);
	}
	return err;
}

static int ovl_lower_dir(const char *name, struct path *path, long *namelen,
			 int *stack_depth)
{
	int err;
	struct kstatfs statfs;

	err = ovl_mount_dir_noesc(name, path);
	if (err)
		goto out;

	err = vfs_statfs(path, &statfs);
	if (err) {
		pr_err("overlayfs: statfs failed on '%s'\n", name);
		goto out_put;
	}
	*namelen = max(*namelen, statfs.f_namelen);
	*stack_depth = max(*stack_depth, path->mnt->mnt_sb->s_stack_depth);

	return 0;

out_put:
	path_put(path);
out:
	return err;
}

/* Workdir should not be subdir of upperdir and vice versa */
static bool ovl_workdir_ok(struct dentry *workdir, struct dentry *upperdir)
{
	bool ok = false;

	if (workdir != upperdir) {
		ok = (lock_rename(workdir, upperdir) == NULL);
		unlock_rename(workdir, upperdir);
	}
	return ok;
}

static unsigned int ovl_split_lowerdirs(char *str)
{
	unsigned int ctr = 1;
	char *s, *d;

	for (s = d = str;; s++, d++) {
		if (*s == '\\') {
			s++;
		} else if (*s == ':') {
			*d = '\0';
			ctr++;
			continue;
		}
		*d = *s;
		if (!*s)
			break;
	}
	return ctr;
}

static int ovl_fill_super(struct super_block *sb, void *data, int silent)
{
	struct path upperpath = { NULL, NULL };
	struct path workpath = { NULL, NULL };
	struct dentry *root_dentry;
	struct ovl_entry *oe;
	struct ovl_fs *ufs;
	struct path *stack = NULL;
	char *lowertmp;
	char *lower;
	unsigned int numlower;
	unsigned int stacklen = 0;
	unsigned int i;
	int err;

	err = -ENOMEM;
	ufs = kzalloc(sizeof(struct ovl_fs), GFP_KERNEL);
	if (!ufs)
		goto out;

	err = ovl_parse_opt((char *) data, &ufs->config);
	if (err)
		goto out_free_config;

	err = -EINVAL;
	if (!ufs->config.lowerdir) {
		pr_err("overlayfs: missing 'lowerdir'\n");
		goto out_free_config;
	}

	sb->s_stack_depth = 0;
	if (ufs->config.upperdir) {
		/* FIXME: workdir is not needed for a R/O mount */
		if (!ufs->config.workdir) {
			pr_err("overlayfs: missing 'workdir'\n");
			goto out_free_config;
		}

		err = ovl_mount_dir(ufs->config.upperdir, &upperpath);
		if (err)
			goto out_free_config;

		err = ovl_mount_dir(ufs->config.workdir, &workpath);
		if (err)
			goto out_put_upperpath;

		err = -EINVAL;
		if (upperpath.mnt != workpath.mnt) {
			pr_err("overlayfs: workdir and upperdir must reside under the same mount\n");
			goto out_put_workpath;
		}
		if (!ovl_workdir_ok(workpath.dentry, upperpath.dentry)) {
			pr_err("overlayfs: workdir and upperdir must be separate subtrees\n");
			goto out_put_workpath;
		}
		sb->s_stack_depth = upperpath.mnt->mnt_sb->s_stack_depth;
	}
	err = -ENOMEM;
	lowertmp = kstrdup(ufs->config.lowerdir, GFP_KERNEL);
	if (!lowertmp)
		goto out_put_workpath;

	err = -EINVAL;
	stacklen = ovl_split_lowerdirs(lowertmp);
	if (stacklen > OVL_MAX_STACK) {
		pr_err("overlayfs: too many lower directries, limit is %d\n",
		       OVL_MAX_STACK);
		goto out_free_lowertmp;
	} else if (!ufs->config.upperdir && stacklen == 1) {
		pr_err("overlayfs: at least 2 lowerdir are needed while upperdir nonexistent\n");
		goto out_free_lowertmp;
	}

	stack = kcalloc(stacklen, sizeof(struct path), GFP_KERNEL);
	if (!stack)
		goto out_free_lowertmp;

	lower = lowertmp;
	for (numlower = 0; numlower < stacklen; numlower++) {
		err = ovl_lower_dir(lower, &stack[numlower],
				    &ufs->lower_namelen, &sb->s_stack_depth);
		if (err)
			goto out_put_lowerpath;

		lower = strchr(lower, '\0') + 1;
	}

	err = -EINVAL;
	sb->s_stack_depth++;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("overlayfs: maximum fs stacking depth exceeded\n");
		goto out_put_lowerpath;
	}

	if (ufs->config.upperdir) {
		ufs->upper_mnt = clone_private_mount(&upperpath);
		err = PTR_ERR(ufs->upper_mnt);
		if (IS_ERR(ufs->upper_mnt)) {
			pr_err("overlayfs: failed to clone upperpath\n");
			goto out_put_lowerpath;
		}

		ufs->workdir = ovl_workdir_create(ufs->upper_mnt, workpath.dentry);
		err = PTR_ERR(ufs->workdir);
		if (IS_ERR(ufs->workdir)) {
			pr_err("overlayfs: failed to create directory %s/%s\n",
			       ufs->config.workdir, OVL_WORKDIR_NAME);
			goto out_put_upper_mnt;
		}
	}

	err = -ENOMEM;
	ufs->lower_mnt = kcalloc(numlower, sizeof(struct vfsmount *), GFP_KERNEL);
	if (ufs->lower_mnt == NULL)
		goto out_put_workdir;
	for (i = 0; i < numlower; i++) {
		struct vfsmount *mnt = clone_private_mount(&stack[i]);

		err = PTR_ERR(mnt);
		if (IS_ERR(mnt)) {
			pr_err("overlayfs: failed to clone lowerpath\n");
			goto out_put_lower_mnt;
		}
		/*
		 * Make lower_mnt R/O.  That way fchmod/fchown on lower file
		 * will fail instead of modifying lower fs.
		 */
		mnt->mnt_flags |= MNT_READONLY;

		ufs->lower_mnt[ufs->numlower] = mnt;
		ufs->numlower++;
	}

	/* If the upper fs is r/o or nonexistent, we mark overlayfs r/o too */
	if (!ufs->upper_mnt || (ufs->upper_mnt->mnt_sb->s_flags & MS_RDONLY))
		sb->s_flags |= MS_RDONLY;

	sb->s_d_op = &ovl_dentry_operations;

	err = -ENOMEM;
	oe = ovl_alloc_entry(numlower);
	if (!oe)
		goto out_put_lower_mnt;

	root_dentry = d_make_root(ovl_new_inode(sb, S_IFDIR, oe));
	if (!root_dentry)
		goto out_free_oe;

	mntput(upperpath.mnt);
	for (i = 0; i < numlower; i++)
		mntput(stack[i].mnt);
	path_put(&workpath);
	kfree(lowertmp);

	oe->__upperdentry = upperpath.dentry;
	for (i = 0; i < numlower; i++) {
		oe->lowerstack[i].dentry = stack[i].dentry;
		oe->lowerstack[i].mnt = ufs->lower_mnt[i];
	}

	root_dentry->d_fsdata = oe;

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_op = &ovl_super_operations;
	sb->s_root = root_dentry;
	sb->s_fs_info = ufs;

	return 0;

out_free_oe:
	kfree(oe);
out_put_lower_mnt:
	for (i = 0; i < ufs->numlower; i++)
		mntput(ufs->lower_mnt[i]);
	kfree(ufs->lower_mnt);
out_put_workdir:
	dput(ufs->workdir);
out_put_upper_mnt:
	mntput(ufs->upper_mnt);
out_put_lowerpath:
	for (i = 0; i < numlower; i++)
		path_put(&stack[i]);
	kfree(stack);
out_free_lowertmp:
	kfree(lowertmp);
out_put_workpath:
	path_put(&workpath);
out_put_upperpath:
	path_put(&upperpath);
out_free_config:
	kfree(ufs->config.lowerdir);
	kfree(ufs->config.upperdir);
	kfree(ufs->config.workdir);
	kfree(ufs);
out:
	return err;
}

static struct dentry *ovl_mount(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *raw_data)
{
	return mount_nodev(fs_type, flags, raw_data, ovl_fill_super);
}

static struct file_system_type ovl_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "overlay",
	.mount		= ovl_mount,
	.kill_sb	= kill_anon_super,
};
MODULE_ALIAS_FS("overlay");

static int __init ovl_init(void)
{
	return register_filesystem(&ovl_fs_type);
}

static void __exit ovl_exit(void)
{
	unregister_filesystem(&ovl_fs_type);
}

module_init(ovl_init);
module_exit(ovl_exit);
