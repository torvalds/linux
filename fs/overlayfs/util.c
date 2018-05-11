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

	if (!ofs->numlowerfs)
		return ofs->upper_mnt->mnt_sb;
	else if (ofs->numlowerfs == 1 && !ofs->upper_mnt)
		return ofs->lower_fs[0].sb;
	else
		return NULL;
}

/*
 * Check if underlying fs supports file handles and try to determine encoding
 * type, in order to deduce maximum inode number used by fs.
 *
 * Return 0 if file handles are not supported.
 * Return 1 (FILEID_INO32_GEN) if fs uses the default 32bit inode encoding.
 * Return -1 if fs uses a non default encoding with unknown inode size.
 */
int ovl_can_decode_fh(struct super_block *sb)
{
	if (!sb->s_export_op || !sb->s_export_op->fh_to_dentry ||
	    uuid_is_null(&sb->s_uuid))
		return 0;

	return sb->s_export_op->encode_fh ? -1 : FILEID_INO32_GEN;
}

struct dentry *ovl_indexdir(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->indexdir;
}

/* Index all files on copy up. For now only enabled for NFS export */
bool ovl_index_all(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->config.nfs_export && ofs->config.index;
}

/* Verify lower origin on lookup. For now only enabled for NFS export */
bool ovl_verify_lower(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->config.nfs_export && ofs->config.index;
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
			if (ovl_test_flag(OVL_CONST_INO, d_inode(dentry)))
				type |= __OVL_PATH_ORIGIN;
			if (d_is_dir(dentry) ||
			    !ovl_has_upperdata(d_inode(dentry)))
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

void ovl_path_lowerdata(struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (oe->numlower) {
		path->mnt = oe->lowerstack[oe->numlower - 1].layer->mnt;
		path->dentry = oe->lowerstack[oe->numlower - 1].dentry;
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

struct ovl_layer *ovl_layer_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->numlower ? oe->lowerstack[0].layer : NULL;
}

/*
 * ovl_dentry_lower() could return either a data dentry or metacopy dentry
 * dependig on what is stored in lowerstack[0]. At times we need to find
 * lower dentry which has data (and not metacopy dentry). This helper
 * returns the lower data dentry.
 */
struct dentry *ovl_dentry_lowerdata(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->numlower ? oe->lowerstack[oe->numlower - 1].dentry : NULL;
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

/* Return inode which contains lower data. Do not return metacopy */
struct inode *ovl_inode_lowerdata(struct inode *inode)
{
	if (WARN_ON(!S_ISREG(inode->i_mode)))
		return NULL;

	return OVL_I(inode)->lowerdata ?: ovl_inode_lower(inode);
}

/* Return real inode which contains data. Does not return metacopy inode */
struct inode *ovl_inode_realdata(struct inode *inode)
{
	struct inode *upperinode;

	upperinode = ovl_inode_upper(inode);
	if (upperinode && ovl_has_upperdata(inode))
		return upperinode;

	return ovl_inode_lowerdata(inode);
}

struct ovl_dir_cache *ovl_dir_cache(struct inode *inode)
{
	return OVL_I(inode)->cache;
}

void ovl_set_dir_cache(struct inode *inode, struct ovl_dir_cache *cache)
{
	OVL_I(inode)->cache = cache;
}

void ovl_dentry_set_flag(unsigned long flag, struct dentry *dentry)
{
	set_bit(flag, &OVL_E(dentry)->flags);
}

void ovl_dentry_clear_flag(unsigned long flag, struct dentry *dentry)
{
	clear_bit(flag, &OVL_E(dentry)->flags);
}

bool ovl_dentry_test_flag(unsigned long flag, struct dentry *dentry)
{
	return test_bit(flag, &OVL_E(dentry)->flags);
}

bool ovl_dentry_is_opaque(struct dentry *dentry)
{
	return ovl_dentry_test_flag(OVL_E_OPAQUE, dentry);
}

bool ovl_dentry_is_whiteout(struct dentry *dentry)
{
	return !dentry->d_inode && ovl_dentry_is_opaque(dentry);
}

void ovl_dentry_set_opaque(struct dentry *dentry)
{
	ovl_dentry_set_flag(OVL_E_OPAQUE, dentry);
}

/*
 * For hard links and decoded file handles, it's possible for ovl_dentry_upper()
 * to return positive, while there's no actual upper alias for the inode.
 * Copy up code needs to know about the existence of the upper alias, so it
 * can't use ovl_dentry_upper().
 */
bool ovl_dentry_has_upper_alias(struct dentry *dentry)
{
	return ovl_dentry_test_flag(OVL_E_UPPER_ALIAS, dentry);
}

void ovl_dentry_set_upper_alias(struct dentry *dentry)
{
	ovl_dentry_set_flag(OVL_E_UPPER_ALIAS, dentry);
}

static bool ovl_should_check_upperdata(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return false;

	if (!ovl_inode_lower(inode))
		return false;

	return true;
}

bool ovl_has_upperdata(struct inode *inode)
{
	if (!ovl_should_check_upperdata(inode))
		return true;

	if (!ovl_test_flag(OVL_UPPERDATA, inode))
		return false;
	/*
	 * Pairs with smp_wmb() in ovl_set_upperdata(). Main user of
	 * ovl_has_upperdata() is ovl_copy_up_meta_inode_data(). Make sure
	 * if setting of OVL_UPPERDATA is visible, then effects of writes
	 * before that are visible too.
	 */
	smp_rmb();
	return true;
}

void ovl_set_upperdata(struct inode *inode)
{
	/*
	 * Pairs with smp_rmb() in ovl_has_upperdata(). Make sure
	 * if OVL_UPPERDATA flag is visible, then effects of write operations
	 * before it are visible as well.
	 */
	smp_wmb();
	ovl_set_flag(OVL_UPPERDATA, inode);
}

/* Caller should hold ovl_inode->lock */
bool ovl_dentry_needs_data_copy_up_locked(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_test_flag(OVL_UPPERDATA, d_inode(dentry));
}

bool ovl_dentry_needs_data_copy_up(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_has_upperdata(d_inode(dentry));
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
		    struct dentry *lowerdentry, struct dentry *lowerdata)
{
	struct inode *realinode = d_inode(upperdentry ?: lowerdentry);

	if (upperdentry)
		OVL_I(inode)->__upperdentry = upperdentry;
	if (lowerdentry)
		OVL_I(inode)->lower = igrab(d_inode(lowerdentry));
	if (lowerdata)
		OVL_I(inode)->lowerdata = igrab(d_inode(lowerdata));

	ovl_copyattr(realinode, inode);
	ovl_copyflags(realinode, inode);
	if (!inode->i_ino)
		inode->i_ino = realinode->i_ino;
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
		if (!inode->i_ino)
			inode->i_ino = upperinode->i_ino;
		inode->i_private = upperinode;
		__insert_inode_hash(inode, (unsigned long) upperinode);
	}
}

static void ovl_dentry_version_inc(struct dentry *dentry, bool impurity)
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

void ovl_dir_modified(struct dentry *dentry, bool impurity)
{
	/* Copy mtime/ctime */
	ovl_copyattr(d_inode(ovl_dentry_upper(dentry)), d_inode(dentry));

	ovl_dentry_version_inc(dentry, impurity);
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

/* Caller should hold ovl_inode->lock */
static bool ovl_already_copied_up_locked(struct dentry *dentry, int flags)
{
	bool disconnected = dentry->d_flags & DCACHE_DISCONNECTED;

	if (ovl_dentry_upper(dentry) &&
	    (ovl_dentry_has_upper_alias(dentry) || disconnected) &&
	    !ovl_dentry_needs_data_copy_up_locked(dentry, flags))
		return true;

	return false;
}

bool ovl_already_copied_up(struct dentry *dentry, int flags)
{
	bool disconnected = dentry->d_flags & DCACHE_DISCONNECTED;

	/*
	 * Check if copy-up has happened as well as for upper alias (in
	 * case of hard links) is there.
	 *
	 * Both checks are lockless:
	 *  - false negatives: will recheck under oi->lock
	 *  - false positives:
	 *    + ovl_dentry_upper() uses memory barriers to ensure the
	 *      upper dentry is up-to-date
	 *    + ovl_dentry_has_upper_alias() relies on locking of
	 *      upper parent i_rwsem to prevent reordering copy-up
	 *      with rename.
	 */
	if (ovl_dentry_upper(dentry) &&
	    (ovl_dentry_has_upper_alias(dentry) || disconnected) &&
	    !ovl_dentry_needs_data_copy_up(dentry, flags))
		return true;

	return false;
}

int ovl_copy_up_start(struct dentry *dentry, int flags)
{
	struct ovl_inode *oi = OVL_I(d_inode(dentry));
	int err;

	err = mutex_lock_interruptible(&oi->lock);
	if (!err && ovl_already_copied_up_locked(dentry, flags)) {
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

/*
 * Does this overlay dentry need to be indexed on copy up?
 */
bool ovl_need_index(struct dentry *dentry)
{
	struct dentry *lower = ovl_dentry_lower(dentry);

	if (!lower || !ovl_indexdir(dentry->d_sb))
		return false;

	/* Index all files for NFS export and consistency verification */
	if (ovl_index_all(dentry->d_sb))
		return true;

	/* Index only lower hardlinks on copy up */
	if (!d_is_dir(lower) && d_inode(lower)->i_nlink > 1)
		return true;

	return false;
}

/* Caller must hold OVL_I(inode)->lock */
static void ovl_cleanup_index(struct dentry *dentry)
{
	struct dentry *indexdir = ovl_indexdir(dentry->d_sb);
	struct inode *dir = indexdir->d_inode;
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
	if (!S_ISDIR(inode->i_mode) && inode->i_nlink != 1) {
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
	index = lookup_one_len(name.name, indexdir, name.len);
	err = PTR_ERR(index);
	if (IS_ERR(index)) {
		index = NULL;
	} else if (ovl_index_all(dentry->d_sb)) {
		/* Whiteout orphan index to block future open by handle */
		err = ovl_cleanup_and_whiteout(indexdir, dir, index);
	} else {
		/* Cleanup orphan index entries */
		err = ovl_cleanup(dir, index);
	}

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

	if (!d_inode(dentry))
		return 0;

	/*
	 * With inodes index is enabled, we store the union overlay nlink
	 * in an xattr on the index inode. When whiting out an indexed lower,
	 * we need to decrement the overlay persistent nlink, but before the
	 * first copy up, we have no upper index inode to store the xattr.
	 *
	 * As a workaround, before whiteout/rename over an indexed lower,
	 * copy up to create the upper index. Creating the upper index will
	 * initialize the overlay nlink, so it could be dropped if unlink
	 * or rename succeeds.
	 *
	 * TODO: implement metadata only index copy up when called with
	 *       ovl_copy_up_flags(dentry, O_PATH).
	 */
	if (ovl_need_index(dentry) && !ovl_dentry_has_upper_alias(dentry)) {
		err = ovl_copy_up(dentry);
		if (err)
			return err;
	}

	err = mutex_lock_interruptible(&oi->lock);
	if (err)
		return err;

	if (d_is_dir(dentry) || !ovl_test_flag(OVL_INDEX, d_inode(dentry)))
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

/* err < 0, 0 if no metacopy xattr, 1 if metacopy xattr found */
int ovl_check_metacopy_xattr(struct dentry *dentry)
{
	int res;

	/* Only regular files can have metacopy xattr */
	if (!S_ISREG(d_inode(dentry)->i_mode))
		return 0;

	res = vfs_getxattr(dentry, OVL_XATTR_METACOPY, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return 0;
		goto out;
	}

	return 1;
out:
	pr_warn_ratelimited("overlayfs: failed to get metacopy (%i)\n", res);
	return res;
}

bool ovl_is_metacopy_dentry(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	if (!d_is_reg(dentry))
		return false;

	if (ovl_dentry_upper(dentry)) {
		if (!ovl_has_upperdata(d_inode(dentry)))
			return true;
		return false;
	}

	return (oe->numlower > 1);
}
