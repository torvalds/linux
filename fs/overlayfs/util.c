/*
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/uuid.h>
#include <linux/namei.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

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

const struct cred *ovl_override_creds(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return override_creds(ofs->creator_cred);
}

struct super_block *ovl_same_sb(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->same_sb;
}

bool ovl_can_decode_fh(struct super_block *sb)
{
	return (sb->s_export_op && sb->s_export_op->fh_to_dentry &&
		!uuid_is_null(&sb->s_uuid));
}

struct dentry *ovl_indexdir(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->indexdir;
}

struct ovl_entry *ovl_alloc_entry(unsigned int numlower)
{
	size_t size = offsetof(struct ovl_entry, lowerstack[numlower]);
	struct ovl_entry *oe = kzalloc(size, GFP_KERNEL);

	if (oe)
		oe->numlower = numlower;

	return oe;
}

bool ovl_dentry_remote(struct dentry *dentry)
{
	return dentry->d_flags &
		(DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE |
		 DCACHE_OP_REAL);
}

bool ovl_dentry_weird(struct dentry *dentry)
{
	return dentry->d_flags & (DCACHE_NEED_AUTOMOUNT |
				  DCACHE_MANAGE_TRANSIT |
				  DCACHE_OP_HASH |
				  DCACHE_OP_COMPARE);
}

enum ovl_path_type ovl_path_type(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	enum ovl_path_type type = 0;

	if (ovl_dentry_upper(dentry)) {
		type = __OVL_PATH_UPPER;

		/*
		 * Non-dir dentry can hold lower dentry of its copy up origin.
		 */
		if (oe->numlower) {
			type |= __OVL_PATH_ORIGIN;
			if (d_is_dir(dentry))
				type |= __OVL_PATH_MERGE;
		}
	} else {
		if (oe->numlower > 1)
			type |= __OVL_PATH_MERGE;
	}
	return type;
}

void ovl_path_upper(struct dentry *dentry, struct path *path)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;

	path->mnt = ofs->upper_mnt;
	path->dentry = ovl_dentry_upper(dentry);
}

void ovl_path_lower(struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe->numlower) {
		path->mnt = oe->lowerstack[0].layer->mnt;
		path->dentry = oe->lowerstack[0].dentry;
	} else {
		*path = (struct path) { };
	}
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
	return ovl_upperdentry_dereference(OVL_I(d_inode(dentry)));
}

struct dentry *ovl_dentry_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->numlower ? oe->lowerstack[0].dentry : NULL;
}

struct dentry *ovl_dentry_real(struct dentry *dentry)
{
	return ovl_dentry_upper(dentry) ?: ovl_dentry_lower(dentry);
}

struct dentry *ovl_i_dentry_upper(struct inode *inode)
{
	return ovl_upperdentry_dereference(OVL_I(inode));
}

struct inode *ovl_inode_upper(struct inode *inode)
{
	struct dentry *upperdentry = ovl_i_dentry_upper(inode);

	return upperdentry ? d_inode(upperdentry) : NULL;
}

struct inode *ovl_inode_lower(struct inode *inode)
{
	return OVL_I(inode)->lower;
}

struct inode *ovl_inode_real(struct inode *inode)
{
	return ovl_inode_upper(inode) ?: ovl_inode_lower(inode);
}


struct ovl_dir_cache *ovl_dir_cache(struct inode *inode)
{
	return OVL_I(inode)->cache;
}

void ovl_set_dir_cache(struct inode *inode, struct ovl_dir_cache *cache)
{
	OVL_I(inode)->cache = cache;
}

bool ovl_dentry_is_opaque(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	return oe->opaque;
}

bool ovl_dentry_is_whiteout(struct dentry *dentry)
{
	return !dentry->d_inode && ovl_dentry_is_opaque(dentry);
}

void ovl_dentry_set_opaque(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	oe->opaque = true;
}

/*
 * For hard links it's possible for ovl_dentry_upper() to return positive, while
 * there's no actual upper alias for the inode.  Copy up code needs to know
 * about the existence of the upper alias, so it can't use ovl_dentry_upper().
 */
bool ovl_dentry_has_upper_alias(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->has_upper;
}

void ovl_dentry_set_upper_alias(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	oe->has_upper = true;
}

bool ovl_redirect_dir(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->config.redirect_dir && !ofs->noxattr;
}

const char *ovl_dentry_get_redirect(struct dentry *dentry)
{
	return OVL_I(d_inode(dentry))->redirect;
}

void ovl_dentry_set_redirect(struct dentry *dentry, const char *redirect)
{
	struct ovl_inode *oi = OVL_I(d_inode(dentry));

	kfree(oi->redirect);
	oi->redirect = redirect;
}

void ovl_inode_init(struct inode *inode, struct dentry *upperdentry,
		    struct dentry *lowerdentry)
{
	if (upperdentry)
		OVL_I(inode)->__upperdentry = upperdentry;
	if (lowerdentry)
		OVL_I(inode)->lower = igrab(d_inode(lowerdentry));

	ovl_copyattr(d_inode(upperdentry ?: lowerdentry), inode);
}

void ovl_inode_update(struct inode *inode, struct dentry *upperdentry)
{
	struct inode *upperinode = d_inode(upperdentry);

	WARN_ON(OVL_I(inode)->__upperdentry);

	/*
	 * Make sure upperdentry is consistent before making it visible
	 */
	smp_wmb();
	OVL_I(inode)->__upperdentry = upperdentry;
	if (inode_unhashed(inode)) {
		inode->i_private = upperinode;
		__insert_inode_hash(inode, (unsigned long) upperinode);
	}
}

void ovl_dentry_version_inc(struct dentry *dentry, bool impurity)
{
	struct inode *inode = d_inode(dentry);

	WARN_ON(!inode_is_locked(inode));
	/*
	 * Version is used by readdir code to keep cache consistent.  For merge
	 * dirs all changes need to be noted.  For non-merge dirs, cache only
	 * contains impure (ones which have been copied up and have origins)
	 * entries, so only need to note changes to impure entries.
	 */
	if (OVL_TYPE_MERGE(ovl_path_type(dentry)) || impurity)
		OVL_I(inode)->version++;
}

u64 ovl_dentry_version_get(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	WARN_ON(!inode_is_locked(inode));
	return OVL_I(inode)->version;
}

bool ovl_is_whiteout(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	return inode && IS_WHITEOUT(inode);
}

struct file *ovl_path_open(struct path *path, int flags)
{
	return dentry_open(path, flags | O_NOATIME, current_cred());
}

int ovl_copy_up_start(struct dentry *dentry)
{
	struct ovl_inode *oi = OVL_I(d_inode(dentry));
	int err;

	err = mutex_lock_interruptible(&oi->lock);
	if (!err && ovl_dentry_has_upper_alias(dentry)) {
		err = 1; /* Already copied up */
		mutex_unlock(&oi->lock);
	}

	return err;
}

void ovl_copy_up_end(struct dentry *dentry)
{
	mutex_unlock(&OVL_I(d_inode(dentry))->lock);
}

bool ovl_check_origin_xattr(struct dentry *dentry)
{
	int res;

	res = vfs_getxattr(dentry, OVL_XATTR_ORIGIN, NULL, 0);

	/* Zero size value means "copied up but origin unknown" */
	if (res >= 0)
		return true;

	return false;
}

bool ovl_check_dir_xattr(struct dentry *dentry, const char *name)
{
	int res;
	char val;

	if (!d_is_dir(dentry))
		return false;

	res = vfs_getxattr(dentry, name, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
}

int ovl_check_setxattr(struct dentry *dentry, struct dentry *upperdentry,
		       const char *name, const void *value, size_t size,
		       int xerr)
{
	int err;
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;

	if (ofs->noxattr)
		return xerr;

	err = ovl_do_setxattr(upperdentry, name, value, size, 0);

	if (err == -EOPNOTSUPP) {
		pr_warn("overlayfs: cannot set %s xattr on upper\n", name);
		ofs->noxattr = true;
		return xerr;
	}

	return err;
}

int ovl_set_impure(struct dentry *dentry, struct dentry *upperdentry)
{
	int err;

	if (ovl_test_flag(OVL_IMPURE, d_inode(dentry)))
		return 0;

	/*
	 * Do not fail when upper doesn't support xattrs.
	 * Upper inodes won't have origin nor redirect xattr anyway.
	 */
	err = ovl_check_setxattr(dentry, upperdentry, OVL_XATTR_IMPURE,
				 "y", 1, 0);
	if (!err)
		ovl_set_flag(OVL_IMPURE, d_inode(dentry));

	return err;
}

void ovl_set_flag(unsigned long flag, struct inode *inode)
{
	set_bit(flag, &OVL_I(inode)->flags);
}

void ovl_clear_flag(unsigned long flag, struct inode *inode)
{
	clear_bit(flag, &OVL_I(inode)->flags);
}

bool ovl_test_flag(unsigned long flag, struct inode *inode)
{
	return test_bit(flag, &OVL_I(inode)->flags);
}

/**
 * Caller must hold a reference to inode to prevent it from being freed while
 * it is marked inuse.
 */
bool ovl_inuse_trylock(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	bool locked = false;

	spin_lock(&inode->i_lock);
	if (!(inode->i_state & I_OVL_INUSE)) {
		inode->i_state |= I_OVL_INUSE;
		locked = true;
	}
	spin_unlock(&inode->i_lock);

	return locked;
}

void ovl_inuse_unlock(struct dentry *dentry)
{
	if (dentry) {
		struct inode *inode = d_inode(dentry);

		spin_lock(&inode->i_lock);
		WARN_ON(!(inode->i_state & I_OVL_INUSE));
		inode->i_state &= ~I_OVL_INUSE;
		spin_unlock(&inode->i_lock);
	}
}

/* Caller must hold OVL_I(inode)->lock */
static void ovl_cleanup_index(struct dentry *dentry)
{
	struct inode *dir = ovl_indexdir(dentry->d_sb)->d_inode;
	struct dentry *lowerdentry = ovl_dentry_lower(dentry);
	struct dentry *upperdentry = ovl_dentry_upper(dentry);
	struct dentry *index = NULL;
	struct inode *inode;
	struct qstr name;
	int err;

	err = ovl_get_index_name(lowerdentry, &name);
	if (err)
		goto fail;

	inode = d_inode(upperdentry);
	if (inode->i_nlink != 1) {
		pr_warn_ratelimited("overlayfs: cleanup linked index (%pd2, ino=%lu, nlink=%u)\n",
				    upperdentry, inode->i_ino, inode->i_nlink);
		/*
		 * We either have a bug with persistent union nlink or a lower
		 * hardlink was added while overlay is mounted. Adding a lower
		 * hardlink and then unlinking all overlay hardlinks would drop
		 * overlay nlink to zero before all upper inodes are unlinked.
		 * As a safety measure, when that situation is detected, set
		 * the overlay nlink to the index inode nlink minus one for the
		 * index entry itself.
		 */
		set_nlink(d_inode(dentry), inode->i_nlink - 1);
		ovl_set_nlink_upper(dentry);
		goto out;
	}

	inode_lock_nested(dir, I_MUTEX_PARENT);
	/* TODO: whiteout instead of cleanup to block future open by handle */
	index = lookup_one_len(name.name, ovl_indexdir(dentry->d_sb), name.len);
	err = PTR_ERR(index);
	if (!IS_ERR(index))
		err = ovl_cleanup(dir, index);
	else
		index = NULL;

	inode_unlock(dir);
	if (err)
		goto fail;

out:
	dput(index);
	return;

fail:
	pr_err("overlayfs: cleanup index of '%pd2' failed (%i)\n", dentry, err);
	goto out;
}

/*
 * Operations that change overlay inode and upper inode nlink need to be
 * synchronized with copy up for persistent nlink accounting.
 */
int ovl_nlink_start(struct dentry *dentry, bool *locked)
{
	struct ovl_inode *oi = OVL_I(d_inode(dentry));
	const struct cred *old_cred;
	int err;

	if (!d_inode(dentry) || d_is_dir(dentry))
		return 0;

	/*
	 * With inodes index is enabled, we store the union overlay nlink
	 * in an xattr on the index inode. When whiting out lower hardlinks
	 * we need to decrement the overlay persistent nlink, but before the
	 * first copy up, we have no upper index inode to store the xattr.
	 *
	 * As a workaround, before whiteout/rename over of a lower hardlink,
	 * copy up to create the upper index. Creating the upper index will
	 * initialize the overlay nlink, so it could be dropped if unlink
	 * or rename succeeds.
	 *
	 * TODO: implement metadata only index copy up when called with
	 *       ovl_copy_up_flags(dentry, O_PATH).
	 */
	if (ovl_indexdir(dentry->d_sb) && !ovl_dentry_has_upper_alias(dentry) &&
	    d_inode(ovl_dentry_lower(dentry))->i_nlink > 1) {
		err = ovl_copy_up(dentry);
		if (err)
			return err;
	}

	err = mutex_lock_interruptible(&oi->lock);
	if (err)
		return err;

	if (!ovl_test_flag(OVL_INDEX, d_inode(dentry)))
		goto out;

	old_cred = ovl_override_creds(dentry->d_sb);
	/*
	 * The overlay inode nlink should be incremented/decremented IFF the
	 * upper operation succeeds, along with nlink change of upper inode.
	 * Therefore, before link/unlink/rename, we store the union nlink
	 * value relative to the upper inode nlink in an upper inode xattr.
	 */
	err = ovl_set_nlink_upper(dentry);
	revert_creds(old_cred);

out:
	if (err)
		mutex_unlock(&oi->lock);
	else
		*locked = true;

	return err;
}

void ovl_nlink_end(struct dentry *dentry, bool locked)
{
	if (locked) {
		if (ovl_test_flag(OVL_INDEX, d_inode(dentry)) &&
		    d_inode(dentry)->i_nlink == 0) {
			const struct cred *old_cred;

			old_cred = ovl_override_creds(dentry->d_sb);
			ovl_cleanup_index(dentry);
			revert_creds(old_cred);
		}

		mutex_unlock(&OVL_I(d_inode(dentry))->lock);
	}
}

int ovl_lock_rename_workdir(struct dentry *workdir, struct dentry *upperdir)
{
	/* Workdir should not be the same as upperdir */
	if (workdir == upperdir)
		goto err;

	/* Workdir should not be subdir of upperdir and vice versa */
	if (lock_rename(workdir, upperdir) != NULL)
		goto err_unlock;

	return 0;

err_unlock:
	unlock_rename(workdir, upperdir);
err:
	pr_err("overlayfs: failed to lock workdir+upperdir\n");
	return -EIO;
}
