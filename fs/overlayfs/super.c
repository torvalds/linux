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
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/posix_acl_xattr.h>
#include "overlayfs.h"

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Overlay filesystem");
MODULE_LICENSE("GPL");

struct ovl_config {
	char *lowerdir;
	char *upperdir;
	char *workdir;
	bool default_permissions;
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
	/* creds of process who forced instantiation of super block */
	const struct cred *creator_cred;
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

		/*
		 * Non-dir dentry can hold lower dentry from previous
		 * location. Its purity depends only on opaque flag.
		 */
		if (oe->numlower && S_ISDIR(dentry->d_inode->i_mode))
			type |= __OVL_PATH_MERGE;
		else if (!oe->opaque)
			type |= __OVL_PATH_PURE;
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

static void ovl_inode_init(struct inode *inode, struct inode *realinode,
			   bool is_upper)
{
	WRITE_ONCE(inode->i_private, (unsigned long) realinode |
		   (is_upper ? OVL_ISUPPER_MASK : 0));
}

struct vfsmount *ovl_entry_mnt_real(struct ovl_entry *oe, struct inode *inode,
				    bool is_upper)
{
	if (is_upper) {
		struct ovl_fs *ofs = inode->i_sb->s_fs_info;

		return ofs->upper_mnt;
	} else {
		return oe->numlower ? oe->lowerstack[0].mnt : NULL;
	}
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

	WARN_ON(!inode_is_locked(upperdentry->d_parent->d_inode));
	WARN_ON(oe->__upperdentry);
	/*
	 * Make sure upperdentry is consistent before making it visible to
	 * ovl_upperdentry_dereference().
	 */
	smp_wmb();
	oe->__upperdentry = upperdentry;
}

void ovl_inode_update(struct inode *inode, struct inode *upperinode)
{
	WARN_ON(!upperinode);
	WARN_ON(!inode_unhashed(inode));
	WRITE_ONCE(inode->i_private,
		   (unsigned long) upperinode | OVL_ISUPPER_MASK);
	if (!S_ISDIR(upperinode->i_mode))
		__insert_inode_hash(inode, (unsigned long) upperinode);
}

void ovl_dentry_version_inc(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	WARN_ON(!inode_is_locked(dentry->d_inode));
	oe->version++;
}

u64 ovl_dentry_version_get(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	WARN_ON(!inode_is_locked(dentry->d_inode));
	return oe->version;
}

bool ovl_is_whiteout(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	return inode && IS_WHITEOUT(inode);
}

const struct cred *ovl_override_creds(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return override_creds(ofs->creator_cred);
}

static bool ovl_is_opaquedir(struct dentry *dentry)
{
	int res;
	char val;

	if (!d_is_dir(dentry))
		return false;

	res = vfs_getxattr(dentry, OVL_XATTR_OPAQUE, &val, 1);
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

static struct dentry *ovl_d_real(struct dentry *dentry,
				 const struct inode *inode,
				 unsigned int open_flags)
{
	struct dentry *real;

	if (d_is_dir(dentry)) {
		if (!inode || inode == d_inode(dentry))
			return dentry;
		goto bug;
	}

	if (d_is_negative(dentry))
		return dentry;

	if (open_flags) {
		int err = ovl_open_maybe_copy_up(dentry, open_flags);

		if (err)
			return ERR_PTR(err);
	}

	real = ovl_dentry_upper(dentry);
	if (real && (!inode || inode == d_inode(real)))
		return real;

	real = ovl_dentry_lower(dentry);
	if (!real)
		goto bug;

	/* Handle recursion */
	real = d_real(real, inode, open_flags);

	if (!inode || inode == d_inode(real))
		return real;
bug:
	WARN(1, "ovl_d_real(%pd4, %s:%lu): real dentry not found\n", dentry,
	     inode ? inode->i_sb->s_id : "NULL", inode ? inode->i_ino : 0);
	return dentry;
}

static int ovl_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	unsigned int i;
	int ret = 1;

	for (i = 0; i < oe->numlower; i++) {
		struct dentry *d = oe->lowerstack[i].dentry;

		if (d->d_flags & DCACHE_OP_REVALIDATE) {
			ret = d->d_op->d_revalidate(d, flags);
			if (ret < 0)
				return ret;
			if (!ret) {
				if (!(flags & LOOKUP_RCU))
					d_invalidate(d);
				return -ESTALE;
			}
		}
	}
	return 1;
}

static int ovl_dentry_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	unsigned int i;
	int ret = 1;

	for (i = 0; i < oe->numlower; i++) {
		struct dentry *d = oe->lowerstack[i].dentry;

		if (d->d_flags & DCACHE_OP_WEAK_REVALIDATE) {
			ret = d->d_op->d_weak_revalidate(d, flags);
			if (ret <= 0)
				break;
		}
	}
	return ret;
}

static const struct dentry_operations ovl_dentry_operations = {
	.d_release = ovl_dentry_release,
	.d_real = ovl_d_real,
};

static const struct dentry_operations ovl_reval_dentry_operations = {
	.d_release = ovl_dentry_release,
	.d_real = ovl_d_real,
	.d_revalidate = ovl_dentry_revalidate,
	.d_weak_revalidate = ovl_dentry_weak_revalidate,
};

static struct ovl_entry *ovl_alloc_entry(unsigned int numlower)
{
	size_t size = offsetof(struct ovl_entry, lowerstack[numlower]);
	struct ovl_entry *oe = kzalloc(size, GFP_KERNEL);

	if (oe)
		oe->numlower = numlower;

	return oe;
}

static bool ovl_dentry_remote(struct dentry *dentry)
{
	return dentry->d_flags &
		(DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE |
		 DCACHE_OP_REAL);
}

static bool ovl_dentry_weird(struct dentry *dentry)
{
	return dentry->d_flags & (DCACHE_NEED_AUTOMOUNT |
				  DCACHE_MANAGE_TRANSIT |
				  DCACHE_OP_HASH |
				  DCACHE_OP_COMPARE);
}

static inline struct dentry *ovl_lookup_real(struct dentry *dir,
					     const struct qstr *name)
{
	struct dentry *dentry;

	dentry = lookup_one_len_unlocked(name->name, dir, name->len);

	if (IS_ERR(dentry)) {
		if (PTR_ERR(dentry) == -ENOENT)
			dentry = NULL;
	} else if (!dentry->d_inode) {
		dput(dentry);
		dentry = NULL;
	} else if (ovl_dentry_weird(dentry)) {
		dput(dentry);
		/* Don't support traversing automounts and other weirdness */
		dentry = ERR_PTR(-EREMOTE);
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
	const struct cred *old_cred;
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	struct path *stack = NULL;
	struct dentry *upperdir, *upperdentry = NULL;
	unsigned int ctr = 0;
	struct inode *inode = NULL;
	bool upperopaque = false;
	struct dentry *this, *prev = NULL;
	unsigned int i;
	int err;

	old_cred = ovl_override_creds(dentry->d_sb);
	upperdir = ovl_upperdentry_dereference(poe);
	if (upperdir) {
		this = ovl_lookup_real(upperdir, &dentry->d_name);
		err = PTR_ERR(this);
		if (IS_ERR(this))
			goto out;

		if (this) {
			if (unlikely(ovl_dentry_remote(this))) {
				dput(this);
				err = -EREMOTE;
				goto out;
			}
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
		struct inode *realinode;

		realdentry = upperdentry ? upperdentry : stack[0].dentry;
		realinode = d_inode(realdentry);

		err = -ENOMEM;
		if (upperdentry && !d_is_dir(upperdentry)) {
			inode = ovl_get_inode(dentry->d_sb, realinode);
		} else {
			inode = ovl_new_inode(dentry->d_sb, realinode->i_mode);
			if (inode)
				ovl_inode_init(inode, realinode, !!upperdentry);
		}
		if (!inode)
			goto out_free_oe;
		ovl_copyattr(realdentry->d_inode, inode);
	}

	revert_creds(old_cred);
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
	revert_creds(old_cred);
	return ERR_PTR(err);
}

struct file *ovl_path_open(struct path *path, int flags)
{
	return dentry_open(path, flags | O_NOATIME, current_cred());
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_fs *ufs = sb->s_fs_info;
	unsigned i;

	dput(ufs->workdir);
	mntput(ufs->upper_mnt);
	for (i = 0; i < ufs->numlower; i++)
		mntput(ufs->lower_mnt[i]);
	kfree(ufs->lower_mnt);

	kfree(ufs->config.lowerdir);
	kfree(ufs->config.upperdir);
	kfree(ufs->config.workdir);
	put_cred(ufs->creator_cred);
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

	seq_show_option(m, "lowerdir", ufs->config.lowerdir);
	if (ufs->config.upperdir) {
		seq_show_option(m, "upperdir", ufs->config.upperdir);
		seq_show_option(m, "workdir", ufs->config.workdir);
	}
	if (ufs->config.default_permissions)
		seq_puts(m, ",default_permissions");
	return 0;
}

static int ovl_remount(struct super_block *sb, int *flags, char *data)
{
	struct ovl_fs *ufs = sb->s_fs_info;

	if (!(*flags & MS_RDONLY) && (!ufs->upper_mnt || !ufs->workdir))
		return -EROFS;

	return 0;
}

static const struct super_operations ovl_super_operations = {
	.put_super	= ovl_put_super,
	.statfs		= ovl_statfs,
	.show_options	= ovl_show_options,
	.remount_fs	= ovl_remount,
	.drop_inode	= generic_delete_inode,
};

enum {
	OPT_LOWERDIR,
	OPT_UPPERDIR,
	OPT_WORKDIR,
	OPT_DEFAULT_PERMISSIONS,
	OPT_ERR,
};

static const match_table_t ovl_tokens = {
	{OPT_LOWERDIR,			"lowerdir=%s"},
	{OPT_UPPERDIR,			"upperdir=%s"},
	{OPT_WORKDIR,			"workdir=%s"},
	{OPT_DEFAULT_PERMISSIONS,	"default_permissions"},
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

		case OPT_DEFAULT_PERMISSIONS:
			config->default_permissions = true;
			break;

		default:
			pr_err("overlayfs: unrecognized mount option \"%s\" or missing value\n", p);
			return -EINVAL;
		}
	}

	/* Workdir is useless in non-upper mount */
	if (!config->upperdir && config->workdir) {
		pr_info("overlayfs: option \"workdir=%s\" is useless in a non-upper mount, ignore\n",
			config->workdir);
		kfree(config->workdir);
		config->workdir = NULL;
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

	inode_lock_nested(dir, I_MUTEX_PARENT);
retry:
	work = lookup_one_len(OVL_WORKDIR_NAME, dentry,
			      strlen(OVL_WORKDIR_NAME));

	if (!IS_ERR(work)) {
		struct kstat stat = {
			.mode = S_IFDIR | 0,
		};
		struct iattr attr = {
			.ia_valid = ATTR_MODE,
			.ia_mode = stat.mode,
		};

		if (work->d_inode) {
			err = -EEXIST;
			if (retried)
				goto out_dput;

			retried = true;
			ovl_workdir_cleanup(dir, mnt, work, 0);
			dput(work);
			goto retry;
		}

		err = ovl_create_real(dir, work, &stat, NULL, NULL, true);
		if (err)
			goto out_dput;

		/*
		 * Try to remove POSIX ACL xattrs from workdir.  We are good if:
		 *
		 * a) success (there was a POSIX ACL xattr and was removed)
		 * b) -ENODATA (there was no POSIX ACL xattr)
		 * c) -EOPNOTSUPP (POSIX ACL xattrs are not supported)
		 *
		 * There are various other error values that could effectively
		 * mean that the xattr doesn't exist (e.g. -ERANGE is returned
		 * if the xattr name is too long), but the set of filesystems
		 * allowed as upper are limited to "normal" ones, where checking
		 * for the above two errors is sufficient.
		 */
		err = vfs_removexattr(work, XATTR_NAME_POSIX_ACL_DEFAULT);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		err = vfs_removexattr(work, XATTR_NAME_POSIX_ACL_ACCESS);
		if (err && err != -ENODATA && err != -EOPNOTSUPP)
			goto out_dput;

		/* Clear any inherited mode bits */
		inode_lock(work->d_inode);
		err = notify_change(work, &attr, NULL);
		inode_unlock(work->d_inode);
		if (err)
			goto out_dput;
	}
out_unlock:
	inode_unlock(dir);
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
	if (ovl_dentry_weird(path->dentry)) {
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

		if (!err)
			if (ovl_dentry_remote(path->dentry)) {
				pr_err("overlayfs: filesystem on '%s' not supported as upperdir\n",
				       tmp);
				path_put(path);
				err = -EINVAL;
			}
		kfree(tmp);
	}
	return err;
}

static int ovl_lower_dir(const char *name, struct path *path, long *namelen,
			 int *stack_depth, bool *remote)
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

	if (ovl_dentry_remote(path->dentry))
		*remote = true;

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

static int __maybe_unused
ovl_posix_acl_xattr_get(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, handler->name, buffer, size);
}

static int __maybe_unused
ovl_posix_acl_xattr_set(const struct xattr_handler *handler,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *value,
			size_t size, int flags)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct inode *realinode = ovl_inode_real(inode, NULL);
	struct posix_acl *acl = NULL;
	int err;

	/* Check that everything is OK before copy-up */
	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	err = -EOPNOTSUPP;
	if (!IS_POSIXACL(d_inode(workdir)))
		goto out_acl_release;
	if (!realinode->i_op->set_acl)
		goto out_acl_release;
	if (handler->flags == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode)) {
		err = acl ? -EACCES : 0;
		goto out_acl_release;
	}
	err = -EPERM;
	if (!inode_owner_or_capable(inode))
		goto out_acl_release;

	posix_acl_release(acl);

	/*
	 * Check if sgid bit needs to be cleared (actual setacl operation will
	 * be done with mounter's capabilities and so that won't do it for us).
	 */
	if (unlikely(inode->i_mode & S_ISGID) &&
	    handler->flags == ACL_TYPE_ACCESS &&
	    !in_group_p(inode->i_gid) &&
	    !capable_wrt_inode_uidgid(inode, CAP_FSETID)) {
		struct iattr iattr = { .ia_valid = ATTR_KILL_SGID };

		err = ovl_setattr(dentry, &iattr);
		if (err)
			return err;
	}

	err = ovl_xattr_set(dentry, handler->name, value, size, flags);
	if (!err)
		ovl_copyattr(ovl_inode_real(inode, NULL), inode);

	return err;

out_acl_release:
	posix_acl_release(acl);
	return err;
}

static int ovl_own_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *buffer, size_t size)
{
	return -EPERM;
}

static int ovl_own_xattr_set(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, const void *value,
			     size_t size, int flags)
{
	return -EPERM;
}

static int ovl_other_xattr_get(const struct xattr_handler *handler,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, name, buffer, size);
}

static int ovl_other_xattr_set(const struct xattr_handler *handler,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, const void *value,
			       size_t size, int flags)
{
	return ovl_xattr_set(dentry, name, value, size, flags);
}

static const struct xattr_handler __maybe_unused
ovl_posix_acl_access_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_ACCESS,
	.flags = ACL_TYPE_ACCESS,
	.get = ovl_posix_acl_xattr_get,
	.set = ovl_posix_acl_xattr_set,
};

static const struct xattr_handler __maybe_unused
ovl_posix_acl_default_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_DEFAULT,
	.flags = ACL_TYPE_DEFAULT,
	.get = ovl_posix_acl_xattr_get,
	.set = ovl_posix_acl_xattr_set,
};

static const struct xattr_handler ovl_own_xattr_handler = {
	.prefix	= OVL_XATTR_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_other_xattr_handler = {
	.prefix	= "", /* catch all */
	.get = ovl_other_xattr_get,
	.set = ovl_other_xattr_set,
};

static const struct xattr_handler *ovl_xattr_handlers[] = {
#ifdef CONFIG_FS_POSIX_ACL
	&ovl_posix_acl_access_xattr_handler,
	&ovl_posix_acl_default_xattr_handler,
#endif
	&ovl_own_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static int ovl_fill_super(struct super_block *sb, void *data, int silent)
{
	struct path upperpath = { NULL, NULL };
	struct path workpath = { NULL, NULL };
	struct dentry *root_dentry;
	struct inode *realinode;
	struct ovl_entry *oe;
	struct ovl_fs *ufs;
	struct path *stack = NULL;
	char *lowertmp;
	char *lower;
	unsigned int numlower;
	unsigned int stacklen = 0;
	unsigned int i;
	bool remote = false;
	struct cred *cred;
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
		if (!silent)
			pr_err("overlayfs: missing 'lowerdir'\n");
		goto out_free_config;
	}

	sb->s_stack_depth = 0;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	if (ufs->config.upperdir) {
		if (!ufs->config.workdir) {
			pr_err("overlayfs: missing 'workdir'\n");
			goto out_free_config;
		}

		err = ovl_mount_dir(ufs->config.upperdir, &upperpath);
		if (err)
			goto out_free_config;

		/* Upper fs should not be r/o */
		if (upperpath.mnt->mnt_sb->s_flags & MS_RDONLY) {
			pr_err("overlayfs: upper fs is r/o, try multi-lower layers mount\n");
			err = -EINVAL;
			goto out_put_upperpath;
		}

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
		pr_err("overlayfs: too many lower directories, limit is %d\n",
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
				    &ufs->lower_namelen, &sb->s_stack_depth,
				    &remote);
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
		/* Don't inherit atime flags */
		ufs->upper_mnt->mnt_flags &= ~(MNT_NOATIME | MNT_NODIRATIME | MNT_RELATIME);

		sb->s_time_gran = ufs->upper_mnt->mnt_sb->s_time_gran;

		ufs->workdir = ovl_workdir_create(ufs->upper_mnt, workpath.dentry);
		err = PTR_ERR(ufs->workdir);
		if (IS_ERR(ufs->workdir)) {
			pr_warn("overlayfs: failed to create directory %s/%s (errno: %i); mounting read-only\n",
				ufs->config.workdir, OVL_WORKDIR_NAME, -err);
			sb->s_flags |= MS_RDONLY;
			ufs->workdir = NULL;
		}

		/*
		 * Upper should support d_type, else whiteouts are visible.
		 * Given workdir and upper are on same fs, we can do
		 * iterate_dir() on workdir. This check requires successful
		 * creation of workdir in previous step.
		 */
		if (ufs->workdir) {
			err = ovl_check_d_type_supported(&workpath);
			if (err < 0)
				goto out_put_workdir;

			/*
			 * We allowed this configuration and don't want to
			 * break users over kernel upgrade. So warn instead
			 * of erroring out.
			 */
			if (!err)
				pr_warn("overlayfs: upper fs needs to support d_type.\n");
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
		mnt->mnt_flags |= MNT_READONLY | MNT_NOATIME;

		ufs->lower_mnt[ufs->numlower] = mnt;
		ufs->numlower++;
	}

	/* If the upper fs is nonexistent, we mark overlayfs r/o too */
	if (!ufs->upper_mnt)
		sb->s_flags |= MS_RDONLY;

	if (remote)
		sb->s_d_op = &ovl_reval_dentry_operations;
	else
		sb->s_d_op = &ovl_dentry_operations;

	err = -ENOMEM;
	ufs->creator_cred = cred = prepare_creds();
	if (!cred)
		goto out_put_lower_mnt;

	/* Never override disk quota limits or use reserved space */
	cap_lower(cred->cap_effective, CAP_SYS_RESOURCE);

	err = -ENOMEM;
	oe = ovl_alloc_entry(numlower);
	if (!oe)
		goto out_put_cred;

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_op = &ovl_super_operations;
	sb->s_xattr = ovl_xattr_handlers;
	sb->s_fs_info = ufs;
	sb->s_flags |= MS_POSIXACL | MS_NOREMOTELOCK;

	root_dentry = d_make_root(ovl_new_inode(sb, S_IFDIR));
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
	kfree(stack);

	root_dentry->d_fsdata = oe;

	realinode = d_inode(ovl_dentry_real(root_dentry));
	ovl_inode_init(d_inode(root_dentry), realinode, !!upperpath.dentry);
	ovl_copyattr(realinode, d_inode(root_dentry));

	sb->s_root = root_dentry;

	return 0;

out_free_oe:
	kfree(oe);
out_put_cred:
	put_cred(ufs->creator_cred);
out_put_lower_mnt:
	for (i = 0; i < ufs->numlower; i++)
		mntput(ufs->lower_mnt[i]);
	kfree(ufs->lower_mnt);
out_put_workdir:
	dput(ufs->workdir);
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
