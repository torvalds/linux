// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
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
 * type, in order to deduce maximum iyesde number used by fs.
 *
 * Return 0 if file handles are yest supported.
 * Return 1 (FILEID_INO32_GEN) if fs uses the default 32bit iyesde encoding.
 * Return -1 if fs uses a yesn default encoding with unkyeswn iyesde size.
 */
int ovl_can_decode_fh(struct super_block *sb)
{
	if (!sb->s_export_op || !sb->s_export_op->fh_to_dentry)
		return 0;

	return sb->s_export_op->encode_fh ? -1 : FILEID_INO32_GEN;
}

struct dentry *ovl_indexdir(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->indexdir;
}

/* Index all files on copy up. For yesw only enabled for NFS export */
bool ovl_index_all(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->config.nfs_export && ofs->config.index;
}

/* Verify lower origin on lookup. For yesw only enabled for NFS export */
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
			if (ovl_test_flag(OVL_CONST_INO, d_iyesde(dentry)))
				type |= __OVL_PATH_ORIGIN;
			if (d_is_dir(dentry) ||
			    !ovl_has_upperdata(d_iyesde(dentry)))
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
	return ovl_upperdentry_dereference(OVL_I(d_iyesde(dentry)));
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
 * lower dentry which has data (and yest metacopy dentry). This helper
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

struct dentry *ovl_i_dentry_upper(struct iyesde *iyesde)
{
	return ovl_upperdentry_dereference(OVL_I(iyesde));
}

struct iyesde *ovl_iyesde_upper(struct iyesde *iyesde)
{
	struct dentry *upperdentry = ovl_i_dentry_upper(iyesde);

	return upperdentry ? d_iyesde(upperdentry) : NULL;
}

struct iyesde *ovl_iyesde_lower(struct iyesde *iyesde)
{
	return OVL_I(iyesde)->lower;
}

struct iyesde *ovl_iyesde_real(struct iyesde *iyesde)
{
	return ovl_iyesde_upper(iyesde) ?: ovl_iyesde_lower(iyesde);
}

/* Return iyesde which contains lower data. Do yest return metacopy */
struct iyesde *ovl_iyesde_lowerdata(struct iyesde *iyesde)
{
	if (WARN_ON(!S_ISREG(iyesde->i_mode)))
		return NULL;

	return OVL_I(iyesde)->lowerdata ?: ovl_iyesde_lower(iyesde);
}

/* Return real iyesde which contains data. Does yest return metacopy iyesde */
struct iyesde *ovl_iyesde_realdata(struct iyesde *iyesde)
{
	struct iyesde *upperiyesde;

	upperiyesde = ovl_iyesde_upper(iyesde);
	if (upperiyesde && ovl_has_upperdata(iyesde))
		return upperiyesde;

	return ovl_iyesde_lowerdata(iyesde);
}

struct ovl_dir_cache *ovl_dir_cache(struct iyesde *iyesde)
{
	return OVL_I(iyesde)->cache;
}

void ovl_set_dir_cache(struct iyesde *iyesde, struct ovl_dir_cache *cache)
{
	OVL_I(iyesde)->cache = cache;
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
	return !dentry->d_iyesde && ovl_dentry_is_opaque(dentry);
}

void ovl_dentry_set_opaque(struct dentry *dentry)
{
	ovl_dentry_set_flag(OVL_E_OPAQUE, dentry);
}

/*
 * For hard links and decoded file handles, it's possible for ovl_dentry_upper()
 * to return positive, while there's yes actual upper alias for the iyesde.
 * Copy up code needs to kyesw about the existence of the upper alias, so it
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

static bool ovl_should_check_upperdata(struct iyesde *iyesde)
{
	if (!S_ISREG(iyesde->i_mode))
		return false;

	if (!ovl_iyesde_lower(iyesde))
		return false;

	return true;
}

bool ovl_has_upperdata(struct iyesde *iyesde)
{
	if (!ovl_should_check_upperdata(iyesde))
		return true;

	if (!ovl_test_flag(OVL_UPPERDATA, iyesde))
		return false;
	/*
	 * Pairs with smp_wmb() in ovl_set_upperdata(). Main user of
	 * ovl_has_upperdata() is ovl_copy_up_meta_iyesde_data(). Make sure
	 * if setting of OVL_UPPERDATA is visible, then effects of writes
	 * before that are visible too.
	 */
	smp_rmb();
	return true;
}

void ovl_set_upperdata(struct iyesde *iyesde)
{
	/*
	 * Pairs with smp_rmb() in ovl_has_upperdata(). Make sure
	 * if OVL_UPPERDATA flag is visible, then effects of write operations
	 * before it are visible as well.
	 */
	smp_wmb();
	ovl_set_flag(OVL_UPPERDATA, iyesde);
}

/* Caller should hold ovl_iyesde->lock */
bool ovl_dentry_needs_data_copy_up_locked(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_test_flag(OVL_UPPERDATA, d_iyesde(dentry));
}

bool ovl_dentry_needs_data_copy_up(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_has_upperdata(d_iyesde(dentry));
}

bool ovl_redirect_dir(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->config.redirect_dir && !ofs->yesxattr;
}

const char *ovl_dentry_get_redirect(struct dentry *dentry)
{
	return OVL_I(d_iyesde(dentry))->redirect;
}

void ovl_dentry_set_redirect(struct dentry *dentry, const char *redirect)
{
	struct ovl_iyesde *oi = OVL_I(d_iyesde(dentry));

	kfree(oi->redirect);
	oi->redirect = redirect;
}

void ovl_iyesde_init(struct iyesde *iyesde, struct dentry *upperdentry,
		    struct dentry *lowerdentry, struct dentry *lowerdata)
{
	struct iyesde *realiyesde = d_iyesde(upperdentry ?: lowerdentry);

	if (upperdentry)
		OVL_I(iyesde)->__upperdentry = upperdentry;
	if (lowerdentry)
		OVL_I(iyesde)->lower = igrab(d_iyesde(lowerdentry));
	if (lowerdata)
		OVL_I(iyesde)->lowerdata = igrab(d_iyesde(lowerdata));

	ovl_copyattr(realiyesde, iyesde);
	ovl_copyflags(realiyesde, iyesde);
	if (!iyesde->i_iyes)
		iyesde->i_iyes = realiyesde->i_iyes;
}

void ovl_iyesde_update(struct iyesde *iyesde, struct dentry *upperdentry)
{
	struct iyesde *upperiyesde = d_iyesde(upperdentry);

	WARN_ON(OVL_I(iyesde)->__upperdentry);

	/*
	 * Make sure upperdentry is consistent before making it visible
	 */
	smp_wmb();
	OVL_I(iyesde)->__upperdentry = upperdentry;
	if (iyesde_unhashed(iyesde)) {
		if (!iyesde->i_iyes)
			iyesde->i_iyes = upperiyesde->i_iyes;
		iyesde->i_private = upperiyesde;
		__insert_iyesde_hash(iyesde, (unsigned long) upperiyesde);
	}
}

static void ovl_dentry_version_inc(struct dentry *dentry, bool impurity)
{
	struct iyesde *iyesde = d_iyesde(dentry);

	WARN_ON(!iyesde_is_locked(iyesde));
	/*
	 * Version is used by readdir code to keep cache consistent.  For merge
	 * dirs all changes need to be yested.  For yesn-merge dirs, cache only
	 * contains impure (ones which have been copied up and have origins)
	 * entries, so only need to yeste changes to impure entries.
	 */
	if (OVL_TYPE_MERGE(ovl_path_type(dentry)) || impurity)
		OVL_I(iyesde)->version++;
}

void ovl_dir_modified(struct dentry *dentry, bool impurity)
{
	/* Copy mtime/ctime */
	ovl_copyattr(d_iyesde(ovl_dentry_upper(dentry)), d_iyesde(dentry));

	ovl_dentry_version_inc(dentry, impurity);
}

u64 ovl_dentry_version_get(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);

	WARN_ON(!iyesde_is_locked(iyesde));
	return OVL_I(iyesde)->version;
}

bool ovl_is_whiteout(struct dentry *dentry)
{
	struct iyesde *iyesde = dentry->d_iyesde;

	return iyesde && IS_WHITEOUT(iyesde);
}

struct file *ovl_path_open(struct path *path, int flags)
{
	return dentry_open(path, flags | O_NOATIME, current_cred());
}

/* Caller should hold ovl_iyesde->lock */
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
	struct iyesde *iyesde = d_iyesde(dentry);
	int err;

	err = ovl_iyesde_lock(iyesde);
	if (!err && ovl_already_copied_up_locked(dentry, flags)) {
		err = 1; /* Already copied up */
		ovl_iyesde_unlock(iyesde);
	}

	return err;
}

void ovl_copy_up_end(struct dentry *dentry)
{
	ovl_iyesde_unlock(d_iyesde(dentry));
}

bool ovl_check_origin_xattr(struct dentry *dentry)
{
	int res;

	res = vfs_getxattr(dentry, OVL_XATTR_ORIGIN, NULL, 0);

	/* Zero size value means "copied up but origin unkyeswn" */
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

	if (ofs->yesxattr)
		return xerr;

	err = ovl_do_setxattr(upperdentry, name, value, size, 0);

	if (err == -EOPNOTSUPP) {
		pr_warn("overlayfs: canyest set %s xattr on upper\n", name);
		ofs->yesxattr = true;
		return xerr;
	}

	return err;
}

int ovl_set_impure(struct dentry *dentry, struct dentry *upperdentry)
{
	int err;

	if (ovl_test_flag(OVL_IMPURE, d_iyesde(dentry)))
		return 0;

	/*
	 * Do yest fail when upper doesn't support xattrs.
	 * Upper iyesdes won't have origin yesr redirect xattr anyway.
	 */
	err = ovl_check_setxattr(dentry, upperdentry, OVL_XATTR_IMPURE,
				 "y", 1, 0);
	if (!err)
		ovl_set_flag(OVL_IMPURE, d_iyesde(dentry));

	return err;
}

void ovl_set_flag(unsigned long flag, struct iyesde *iyesde)
{
	set_bit(flag, &OVL_I(iyesde)->flags);
}

void ovl_clear_flag(unsigned long flag, struct iyesde *iyesde)
{
	clear_bit(flag, &OVL_I(iyesde)->flags);
}

bool ovl_test_flag(unsigned long flag, struct iyesde *iyesde)
{
	return test_bit(flag, &OVL_I(iyesde)->flags);
}

/**
 * Caller must hold a reference to iyesde to prevent it from being freed while
 * it is marked inuse.
 */
bool ovl_inuse_trylock(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	bool locked = false;

	spin_lock(&iyesde->i_lock);
	if (!(iyesde->i_state & I_OVL_INUSE)) {
		iyesde->i_state |= I_OVL_INUSE;
		locked = true;
	}
	spin_unlock(&iyesde->i_lock);

	return locked;
}

void ovl_inuse_unlock(struct dentry *dentry)
{
	if (dentry) {
		struct iyesde *iyesde = d_iyesde(dentry);

		spin_lock(&iyesde->i_lock);
		WARN_ON(!(iyesde->i_state & I_OVL_INUSE));
		iyesde->i_state &= ~I_OVL_INUSE;
		spin_unlock(&iyesde->i_lock);
	}
}

bool ovl_is_inuse(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	bool inuse;

	spin_lock(&iyesde->i_lock);
	inuse = (iyesde->i_state & I_OVL_INUSE);
	spin_unlock(&iyesde->i_lock);

	return inuse;
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
	if (!d_is_dir(lower) && d_iyesde(lower)->i_nlink > 1)
		return true;

	return false;
}

/* Caller must hold OVL_I(iyesde)->lock */
static void ovl_cleanup_index(struct dentry *dentry)
{
	struct dentry *indexdir = ovl_indexdir(dentry->d_sb);
	struct iyesde *dir = indexdir->d_iyesde;
	struct dentry *lowerdentry = ovl_dentry_lower(dentry);
	struct dentry *upperdentry = ovl_dentry_upper(dentry);
	struct dentry *index = NULL;
	struct iyesde *iyesde;
	struct qstr name = { };
	int err;

	err = ovl_get_index_name(lowerdentry, &name);
	if (err)
		goto fail;

	iyesde = d_iyesde(upperdentry);
	if (!S_ISDIR(iyesde->i_mode) && iyesde->i_nlink != 1) {
		pr_warn_ratelimited("overlayfs: cleanup linked index (%pd2, iyes=%lu, nlink=%u)\n",
				    upperdentry, iyesde->i_iyes, iyesde->i_nlink);
		/*
		 * We either have a bug with persistent union nlink or a lower
		 * hardlink was added while overlay is mounted. Adding a lower
		 * hardlink and then unlinking all overlay hardlinks would drop
		 * overlay nlink to zero before all upper iyesdes are unlinked.
		 * As a safety measure, when that situation is detected, set
		 * the overlay nlink to the index iyesde nlink minus one for the
		 * index entry itself.
		 */
		set_nlink(d_iyesde(dentry), iyesde->i_nlink - 1);
		ovl_set_nlink_upper(dentry);
		goto out;
	}

	iyesde_lock_nested(dir, I_MUTEX_PARENT);
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

	iyesde_unlock(dir);
	if (err)
		goto fail;

out:
	kfree(name.name);
	dput(index);
	return;

fail:
	pr_err("overlayfs: cleanup index of '%pd2' failed (%i)\n", dentry, err);
	goto out;
}

/*
 * Operations that change overlay iyesde and upper iyesde nlink need to be
 * synchronized with copy up for persistent nlink accounting.
 */
int ovl_nlink_start(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	const struct cred *old_cred;
	int err;

	if (WARN_ON(!iyesde))
		return -ENOENT;

	/*
	 * With iyesdes index is enabled, we store the union overlay nlink
	 * in an xattr on the index iyesde. When whiting out an indexed lower,
	 * we need to decrement the overlay persistent nlink, but before the
	 * first copy up, we have yes upper index iyesde to store the xattr.
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

	err = ovl_iyesde_lock(iyesde);
	if (err)
		return err;

	if (d_is_dir(dentry) || !ovl_test_flag(OVL_INDEX, iyesde))
		goto out;

	old_cred = ovl_override_creds(dentry->d_sb);
	/*
	 * The overlay iyesde nlink should be incremented/decremented IFF the
	 * upper operation succeeds, along with nlink change of upper iyesde.
	 * Therefore, before link/unlink/rename, we store the union nlink
	 * value relative to the upper iyesde nlink in an upper iyesde xattr.
	 */
	err = ovl_set_nlink_upper(dentry);
	revert_creds(old_cred);

out:
	if (err)
		ovl_iyesde_unlock(iyesde);

	return err;
}

void ovl_nlink_end(struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);

	if (ovl_test_flag(OVL_INDEX, iyesde) && iyesde->i_nlink == 0) {
		const struct cred *old_cred;

		old_cred = ovl_override_creds(dentry->d_sb);
		ovl_cleanup_index(dentry);
		revert_creds(old_cred);
	}

	ovl_iyesde_unlock(iyesde);
}

int ovl_lock_rename_workdir(struct dentry *workdir, struct dentry *upperdir)
{
	/* Workdir should yest be the same as upperdir */
	if (workdir == upperdir)
		goto err;

	/* Workdir should yest be subdir of upperdir and vice versa */
	if (lock_rename(workdir, upperdir) != NULL)
		goto err_unlock;

	return 0;

err_unlock:
	unlock_rename(workdir, upperdir);
err:
	pr_err("overlayfs: failed to lock workdir+upperdir\n");
	return -EIO;
}

/* err < 0, 0 if yes metacopy xattr, 1 if metacopy xattr found */
int ovl_check_metacopy_xattr(struct dentry *dentry)
{
	int res;

	/* Only regular files can have metacopy xattr */
	if (!S_ISREG(d_iyesde(dentry)->i_mode))
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
		if (!ovl_has_upperdata(d_iyesde(dentry)))
			return true;
		return false;
	}

	return (oe->numlower > 1);
}

ssize_t ovl_getxattr(struct dentry *dentry, char *name, char **value,
		     size_t padding)
{
	ssize_t res;
	char *buf = NULL;

	res = vfs_getxattr(dentry, name, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return -ENODATA;
		goto fail;
	}

	if (res != 0) {
		buf = kzalloc(res + padding, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		res = vfs_getxattr(dentry, name, buf, res);
		if (res < 0)
			goto fail;
	}
	*value = buf;

	return res;

fail:
	pr_warn_ratelimited("overlayfs: failed to get xattr %s: err=%zi)\n",
			    name, res);
	kfree(buf);
	return res;
}

char *ovl_get_redirect_xattr(struct dentry *dentry, int padding)
{
	int res;
	char *s, *next, *buf = NULL;

	res = ovl_getxattr(dentry, OVL_XATTR_REDIRECT, &buf, padding + 1);
	if (res == -ENODATA)
		return NULL;
	if (res < 0)
		return ERR_PTR(res);
	if (res == 0)
		goto invalid;

	if (buf[0] == '/') {
		for (s = buf; *s++ == '/'; s = next) {
			next = strchrnul(s, '/');
			if (s == next)
				goto invalid;
		}
	} else {
		if (strchr(buf, '/') != NULL)
			goto invalid;
	}

	return buf;
invalid:
	pr_warn_ratelimited("overlayfs: invalid redirect (%s)\n", buf);
	res = -EINVAL;
	kfree(buf);
	return ERR_PTR(res);
}
