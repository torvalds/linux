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
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include "overlayfs.h"

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Overlay filesystem");
MODULE_LICENSE("GPL");

#define OVERLAYFS_SUPER_MAGIC 0x794c764f

struct ovl_config {
	char *lowerdir;
	char *upperdir;
};

/* private information held for overlayfs's superblock */
struct ovl_fs {
	struct vfsmount *upper_mnt;
	struct vfsmount *lower_mnt;
	long lower_namelen;
	/* pathnames of lower and upper dirs, for show_options */
	struct ovl_config config;
};

/* private information held for every overlayfs dentry */
struct ovl_entry {
	/*
	 * Keep "double reference" on upper dentries, so that
	 * d_delete() doesn't think it's OK to reset d_inode to NULL.
	 */
	struct dentry *__upperdentry;
	struct dentry *lowerdentry;
	union {
		struct {
			u64 version;
			bool opaque;
		};
		struct rcu_head rcu;
	};
};

const char *ovl_whiteout_xattr = "trusted.overlay.whiteout";
const char *ovl_opaque_xattr = "trusted.overlay.opaque";


enum ovl_path_type ovl_path_type(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe->__upperdentry) {
		if (oe->lowerdentry && S_ISDIR(dentry->d_inode->i_mode))
			return OVL_PATH_MERGE;
		else
			return OVL_PATH_UPPER;
	} else {
		return OVL_PATH_LOWER;
	}
}

static struct dentry *ovl_upperdentry_dereference(struct ovl_entry *oe)
{
	struct dentry *upperdentry = ACCESS_ONCE(oe->__upperdentry);
	smp_read_barrier_depends();
	return upperdentry;
}

void ovl_path_upper(struct dentry *dentry, struct path *path)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct ovl_entry *oe = dentry->d_fsdata;

	path->mnt = ofs->upper_mnt;
	path->dentry = ovl_upperdentry_dereference(oe);
}

void ovl_path_lower(struct dentry *dentry, struct path *path)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct ovl_entry *oe = dentry->d_fsdata;

	path->mnt = ofs->lower_mnt;
	path->dentry = oe->lowerdentry;
}

enum ovl_path_type ovl_path_real(struct dentry *dentry, struct path *path)
{

	enum ovl_path_type type = ovl_path_type(dentry);

	if (type == OVL_PATH_LOWER)
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

	return oe->lowerdentry;
}

struct dentry *ovl_dentry_real(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	struct dentry *realdentry;

	realdentry = ovl_upperdentry_dereference(oe);
	if (!realdentry)
		realdentry = oe->lowerdentry;

	return realdentry;
}

struct dentry *ovl_entry_real(struct ovl_entry *oe, bool *is_upper)
{
	struct dentry *realdentry;

	realdentry = ovl_upperdentry_dereference(oe);
	if (realdentry) {
		*is_upper = true;
	} else {
		realdentry = oe->lowerdentry;
		*is_upper = false;
	}
	return realdentry;
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
	smp_wmb();
	oe->__upperdentry = dget(upperdentry);
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
	int res;
	char val;

	if (!dentry)
		return false;
	if (!dentry->d_inode)
		return false;
	if (!S_ISLNK(dentry->d_inode->i_mode))
		return false;

	res = vfs_getxattr(dentry, ovl_whiteout_xattr, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
}

static bool ovl_is_opaquedir(struct dentry *dentry)
{
	int res;
	char val;

	if (!S_ISDIR(dentry->d_inode->i_mode))
		return false;

	res = vfs_getxattr(dentry, ovl_opaque_xattr, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
}

static void ovl_entry_free(struct rcu_head *head)
{
	struct ovl_entry *oe = container_of(head, struct ovl_entry, rcu);
	kfree(oe);
}

static void ovl_dentry_release(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe) {
		dput(oe->__upperdentry);
		dput(oe->__upperdentry);
		dput(oe->lowerdentry);
		call_rcu(&oe->rcu, ovl_entry_free);
	}
}

const struct dentry_operations ovl_dentry_operations = {
	.d_release = ovl_dentry_release,
};

static struct ovl_entry *ovl_alloc_entry(void)
{
	return kzalloc(sizeof(struct ovl_entry), GFP_KERNEL);
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

static int ovl_do_lookup(struct dentry *dentry)
{
	struct ovl_entry *oe;
	struct dentry *upperdir;
	struct dentry *lowerdir;
	struct dentry *upperdentry = NULL;
	struct dentry *lowerdentry = NULL;
	struct inode *inode = NULL;
	int err;

	err = -ENOMEM;
	oe = ovl_alloc_entry();
	if (!oe)
		goto out;

	upperdir = ovl_dentry_upper(dentry->d_parent);
	lowerdir = ovl_dentry_lower(dentry->d_parent);

	if (upperdir) {
		upperdentry = ovl_lookup_real(upperdir, &dentry->d_name);
		err = PTR_ERR(upperdentry);
		if (IS_ERR(upperdentry))
			goto out_put_dir;

		if (lowerdir && upperdentry &&
		    (S_ISLNK(upperdentry->d_inode->i_mode) ||
		     S_ISDIR(upperdentry->d_inode->i_mode))) {
			const struct cred *old_cred;
			struct cred *override_cred;

			err = -ENOMEM;
			override_cred = prepare_creds();
			if (!override_cred)
				goto out_dput_upper;

			/* CAP_SYS_ADMIN needed for getxattr */
			cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
			old_cred = override_creds(override_cred);

			if (ovl_is_opaquedir(upperdentry)) {
				oe->opaque = true;
			} else if (ovl_is_whiteout(upperdentry)) {
				dput(upperdentry);
				upperdentry = NULL;
				oe->opaque = true;
			}
			revert_creds(old_cred);
			put_cred(override_cred);
		}
	}
	if (lowerdir && !oe->opaque) {
		lowerdentry = ovl_lookup_real(lowerdir, &dentry->d_name);
		err = PTR_ERR(lowerdentry);
		if (IS_ERR(lowerdentry))
			goto out_dput_upper;
	}

	if (lowerdentry && upperdentry &&
	    (!S_ISDIR(upperdentry->d_inode->i_mode) ||
	     !S_ISDIR(lowerdentry->d_inode->i_mode))) {
		dput(lowerdentry);
		lowerdentry = NULL;
		oe->opaque = true;
	}

	if (lowerdentry || upperdentry) {
		struct dentry *realdentry;

		realdentry = upperdentry ? upperdentry : lowerdentry;
		err = -ENOMEM;
		inode = ovl_new_inode(dentry->d_sb, realdentry->d_inode->i_mode,
				      oe);
		if (!inode)
			goto out_dput;
		ovl_copyattr(realdentry->d_inode, inode);
	}

	if (upperdentry)
		oe->__upperdentry = dget(upperdentry);

	if (lowerdentry)
		oe->lowerdentry = lowerdentry;

	dentry->d_fsdata = oe;
	dentry->d_op = &ovl_dentry_operations;
	d_add(dentry, inode);

	return 0;

out_dput:
	dput(lowerdentry);
out_dput_upper:
	dput(upperdentry);
out_put_dir:
	kfree(oe);
out:
	return err;
}

struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags)
{
	int err = ovl_do_lookup(dentry);

	if (err)
		return ERR_PTR(err);

	return NULL;
}

struct file *ovl_path_open(struct path *path, int flags)
{
	return dentry_open(path, flags, current_cred());
}

static void ovl_put_super(struct super_block *sb)
{
	struct ovl_fs *ufs = sb->s_fs_info;

	if (!(sb->s_flags & MS_RDONLY))
		mnt_drop_write(ufs->upper_mnt);

	mntput(ufs->upper_mnt);
	mntput(ufs->lower_mnt);

	kfree(ufs->config.lowerdir);
	kfree(ufs->config.upperdir);
	kfree(ufs);
}

static int ovl_remount_fs(struct super_block *sb, int *flagsp, char *data)
{
	int flags = *flagsp;
	struct ovl_fs *ufs = sb->s_fs_info;

	/* When remounting rw or ro, we need to adjust the write access to the
	 * upper fs.
	 */
	if (((flags ^ sb->s_flags) & MS_RDONLY) == 0)
		/* No change to readonly status */
		return 0;

	if (flags & MS_RDONLY) {
		mnt_drop_write(ufs->upper_mnt);
		return 0;
	} else
		return mnt_want_write(ufs->upper_mnt);
}

/**
 * ovl_statfs
 * @sb: The overlayfs super block
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics.  As writes always target the upper layer
 * filesystem pass the statfs to the same filesystem.
 */
static int ovl_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct dentry *root_dentry = dentry->d_sb->s_root;
	struct path path;
	int err;

	ovl_path_upper(root_dentry, &path);

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
	seq_printf(m, ",upperdir=%s", ufs->config.upperdir);
	return 0;
}

static const struct super_operations ovl_super_operations = {
	.put_super	= ovl_put_super,
	.remount_fs	= ovl_remount_fs,
	.statfs		= ovl_statfs,
	.show_options	= ovl_show_options,
};

enum {
	OPT_LOWERDIR,
	OPT_UPPERDIR,
	OPT_ERR,
};

static const match_table_t ovl_tokens = {
	{OPT_LOWERDIR,			"lowerdir=%s"},
	{OPT_UPPERDIR,			"upperdir=%s"},
	{OPT_ERR,			NULL}
};

static int ovl_parse_opt(char *opt, struct ovl_config *config)
{
	char *p;

	config->upperdir = NULL;
	config->lowerdir = NULL;

	while ((p = strsep(&opt, ",")) != NULL) {
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

		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int ovl_fill_super(struct super_block *sb, void *data, int silent)
{
	struct path lowerpath;
	struct path upperpath;
	struct inode *root_inode;
	struct dentry *root_dentry;
	struct ovl_entry *oe;
	struct ovl_fs *ufs;
	struct kstatfs statfs;
	int err;

	err = -ENOMEM;
	ufs = kmalloc(sizeof(struct ovl_fs), GFP_KERNEL);
	if (!ufs)
		goto out;

	err = ovl_parse_opt((char *) data, &ufs->config);
	if (err)
		goto out_free_ufs;

	err = -EINVAL;
	if (!ufs->config.upperdir || !ufs->config.lowerdir) {
		pr_err("overlayfs: missing upperdir or lowerdir\n");
		goto out_free_config;
	}

	oe = ovl_alloc_entry();
	if (oe == NULL)
		goto out_free_config;

	err = kern_path(ufs->config.upperdir, LOOKUP_FOLLOW, &upperpath);
	if (err)
		goto out_free_oe;

	err = kern_path(ufs->config.lowerdir, LOOKUP_FOLLOW, &lowerpath);
	if (err)
		goto out_put_upperpath;

	err = -ENOTDIR;
	if (!S_ISDIR(upperpath.dentry->d_inode->i_mode) ||
	    !S_ISDIR(lowerpath.dentry->d_inode->i_mode))
		goto out_put_lowerpath;

	err = vfs_statfs(&lowerpath, &statfs);
	if (err) {
		pr_err("overlayfs: statfs failed on lowerpath\n");
		goto out_put_lowerpath;
	}
	ufs->lower_namelen = statfs.f_namelen;

	sb->s_stack_depth = max(upperpath.mnt->mnt_sb->s_stack_depth,
				lowerpath.mnt->mnt_sb->s_stack_depth) + 1;

	err = -EINVAL;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("overlayfs: maximum fs stacking depth exceeded\n");
		goto out_put_lowerpath;
	}


	ufs->upper_mnt = clone_private_mount(&upperpath);
	err = PTR_ERR(ufs->upper_mnt);
	if (IS_ERR(ufs->upper_mnt)) {
		pr_err("overlayfs: failed to clone upperpath\n");
		goto out_put_lowerpath;
	}

	ufs->lower_mnt = clone_private_mount(&lowerpath);
	err = PTR_ERR(ufs->lower_mnt);
	if (IS_ERR(ufs->lower_mnt)) {
		pr_err("overlayfs: failed to clone lowerpath\n");
		goto out_put_upper_mnt;
	}

	/*
	 * Make lower_mnt R/O.  That way fchmod/fchown on lower file
	 * will fail instead of modifying lower fs.
	 */
	ufs->lower_mnt->mnt_flags |= MNT_READONLY;

	/* If the upper fs is r/o, we mark overlayfs r/o too */
	if (ufs->upper_mnt->mnt_sb->s_flags & MS_RDONLY)
		sb->s_flags |= MS_RDONLY;

	if (!(sb->s_flags & MS_RDONLY)) {
		err = mnt_want_write(ufs->upper_mnt);
		if (err)
			goto out_put_lower_mnt;
	}

	err = -ENOMEM;
	root_inode = ovl_new_inode(sb, S_IFDIR, oe);
	if (!root_inode)
		goto out_drop_write;

	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		goto out_drop_write;

	mntput(upperpath.mnt);
	mntput(lowerpath.mnt);

	oe->__upperdentry = dget(upperpath.dentry);
	oe->lowerdentry = lowerpath.dentry;

	root_dentry->d_fsdata = oe;
	root_dentry->d_op = &ovl_dentry_operations;

	sb->s_magic = OVERLAYFS_SUPER_MAGIC;
	sb->s_op = &ovl_super_operations;
	sb->s_root = root_dentry;
	sb->s_fs_info = ufs;

	return 0;

out_drop_write:
	if (!(sb->s_flags & MS_RDONLY))
		mnt_drop_write(ufs->upper_mnt);
out_put_lower_mnt:
	mntput(ufs->lower_mnt);
out_put_upper_mnt:
	mntput(ufs->upper_mnt);
out_put_lowerpath:
	path_put(&lowerpath);
out_put_upperpath:
	path_put(&upperpath);
out_free_oe:
	kfree(oe);
out_free_config:
	kfree(ufs->config.lowerdir);
	kfree(ufs->config.upperdir);
out_free_ufs:
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
	.name		= "overlayfs",
	.mount		= ovl_mount,
	.kill_sb	= kill_anon_super,
};
MODULE_ALIAS_FS("overlayfs");

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
