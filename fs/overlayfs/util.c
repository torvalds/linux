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
#include <linux/fileattr.h>
#include <linux/uuid.h>
#include <linux/namei.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

int ovl_want_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	return mnt_want_write(ovl_upper_mnt(ofs));
}

void ovl_drop_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	mnt_drop_write(ovl_upper_mnt(ofs));
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
	if (!capable(CAP_DAC_READ_SEARCH))
		return 0;

	if (!sb->s_export_op || !sb->s_export_op->fh_to_dentry)
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
		(DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE);
}

void ovl_dentry_update_reval(struct dentry *dentry, struct dentry *upperdentry,
			     unsigned int mask)
{
	struct ovl_entry *oe = OVL_E(dentry);
	unsigned int i, flags = 0;

	if (upperdentry)
		flags |= upperdentry->d_flags;
	for (i = 0; i < oe->numlower; i++)
		flags |= oe->lowerstack[i].dentry->d_flags;

	spin_lock(&dentry->d_lock);
	dentry->d_flags &= ~mask;
	dentry->d_flags |= flags & mask;
	spin_unlock(&dentry->d_lock);
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

	path->mnt = ovl_upper_mnt(ofs);
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

enum ovl_path_type ovl_path_realdata(struct dentry *dentry, struct path *path)
{
	enum ovl_path_type type = ovl_path_type(dentry);

	WARN_ON_ONCE(d_is_dir(dentry));

	if (!OVL_TYPE_UPPER(type) || OVL_TYPE_MERGE(type))
		ovl_path_lowerdata(dentry, path);
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

const struct ovl_layer *ovl_layer_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	return oe->numlower ? oe->lowerstack[0].layer : NULL;
}

/*
 * ovl_dentry_lower() could return either a data dentry or metacopy dentry
 * depending on what is stored in lowerstack[0]. At times we need to find
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

void ovl_i_path_real(struct inode *inode, struct path *path)
{
	path->dentry = ovl_i_dentry_upper(inode);
	if (!path->dentry) {
		path->dentry = OVL_I(inode)->lowerpath.dentry;
		path->mnt = OVL_I(inode)->lowerpath.layer->mnt;
	} else {
		path->mnt = ovl_upper_mnt(OVL_FS(inode->i_sb));
	}
}

struct inode *ovl_inode_upper(struct inode *inode)
{
	struct dentry *upperdentry = ovl_i_dentry_upper(inode);

	return upperdentry ? d_inode(upperdentry) : NULL;
}

struct inode *ovl_inode_lower(struct inode *inode)
{
	struct dentry *lowerdentry = OVL_I(inode)->lowerpath.dentry;

	return lowerdentry ? d_inode(lowerdentry) : NULL;
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

static void ovl_dir_version_inc(struct dentry *dentry, bool impurity)
{
	struct inode *inode = d_inode(dentry);

	WARN_ON(!inode_is_locked(inode));
	WARN_ON(!d_is_dir(dentry));
	/*
	 * Version is used by readdir code to keep cache consistent.
	 * For merge dirs (or dirs with origin) all changes need to be noted.
	 * For non-merge dirs, cache contains only impure entries (i.e. ones
	 * which have been copied up and have origins), so only need to note
	 * changes to impure entries.
	 */
	if (!ovl_dir_is_real(dentry) || impurity)
		OVL_I(inode)->version++;
}

void ovl_dir_modified(struct dentry *dentry, bool impurity)
{
	/* Copy mtime/ctime */
	ovl_copyattr(d_inode(dentry));

	ovl_dir_version_inc(dentry, impurity);
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

struct file *ovl_path_open(const struct path *path, int flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct user_namespace *real_mnt_userns = mnt_user_ns(path->mnt);
	int err, acc_mode;

	if (flags & ~(O_ACCMODE | O_LARGEFILE))
		BUG();

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		acc_mode = MAY_READ;
		break;
	case O_WRONLY:
		acc_mode = MAY_WRITE;
		break;
	default:
		BUG();
	}

	err = inode_permission(real_mnt_userns, inode, acc_mode | MAY_OPEN);
	if (err)
		return ERR_PTR(err);

	/* O_NOATIME is an optimization, don't fail if not permitted */
	if (inode_owner_or_capable(real_mnt_userns, inode))
		flags |= O_NOATIME;

	return dentry_open(path, flags, current_cred());
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
	struct inode *inode = d_inode(dentry);
	int err;

	err = ovl_inode_lock_interruptible(inode);
	if (!err && ovl_already_copied_up_locked(dentry, flags)) {
		err = 1; /* Already copied up */
		ovl_inode_unlock(inode);
	}

	return err;
}

void ovl_copy_up_end(struct dentry *dentry)
{
	ovl_inode_unlock(d_inode(dentry));
}

bool ovl_path_check_origin_xattr(struct ovl_fs *ofs, const struct path *path)
{
	int res;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_ORIGIN, NULL, 0);

	/* Zero size value means "copied up but origin unknown" */
	if (res >= 0)
		return true;

	return false;
}

bool ovl_path_check_dir_xattr(struct ovl_fs *ofs, const struct path *path,
			       enum ovl_xattr ox)
{
	int res;
	char val;

	if (!d_is_dir(path->dentry))
		return false;

	res = ovl_path_getxattr(ofs, path, ox, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
}

#define OVL_XATTR_OPAQUE_POSTFIX	"opaque"
#define OVL_XATTR_REDIRECT_POSTFIX	"redirect"
#define OVL_XATTR_ORIGIN_POSTFIX	"origin"
#define OVL_XATTR_IMPURE_POSTFIX	"impure"
#define OVL_XATTR_NLINK_POSTFIX		"nlink"
#define OVL_XATTR_UPPER_POSTFIX		"upper"
#define OVL_XATTR_METACOPY_POSTFIX	"metacopy"
#define OVL_XATTR_PROTATTR_POSTFIX	"protattr"

#define OVL_XATTR_TAB_ENTRY(x) \
	[x] = { [false] = OVL_XATTR_TRUSTED_PREFIX x ## _POSTFIX, \
		[true] = OVL_XATTR_USER_PREFIX x ## _POSTFIX }

const char *const ovl_xattr_table[][2] = {
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_OPAQUE),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_REDIRECT),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_ORIGIN),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_IMPURE),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_NLINK),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_UPPER),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_METACOPY),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_PROTATTR),
};

int ovl_check_setxattr(struct ovl_fs *ofs, struct dentry *upperdentry,
		       enum ovl_xattr ox, const void *value, size_t size,
		       int xerr)
{
	int err;

	if (ofs->noxattr)
		return xerr;

	err = ovl_setxattr(ofs, upperdentry, ox, value, size);

	if (err == -EOPNOTSUPP) {
		pr_warn("cannot set %s xattr on upper\n", ovl_xattr(ofs, ox));
		ofs->noxattr = true;
		return xerr;
	}

	return err;
}

int ovl_set_impure(struct dentry *dentry, struct dentry *upperdentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	int err;

	if (ovl_test_flag(OVL_IMPURE, d_inode(dentry)))
		return 0;

	/*
	 * Do not fail when upper doesn't support xattrs.
	 * Upper inodes won't have origin nor redirect xattr anyway.
	 */
	err = ovl_check_setxattr(ofs, upperdentry, OVL_XATTR_IMPURE, "y", 1, 0);
	if (!err)
		ovl_set_flag(OVL_IMPURE, d_inode(dentry));

	return err;
}


#define OVL_PROTATTR_MAX 32 /* Reserved for future flags */

void ovl_check_protattr(struct inode *inode, struct dentry *upper)
{
	struct ovl_fs *ofs = OVL_FS(inode->i_sb);
	u32 iflags = inode->i_flags & OVL_PROT_I_FLAGS_MASK;
	char buf[OVL_PROTATTR_MAX+1];
	int res, n;

	res = ovl_getxattr_upper(ofs, upper, OVL_XATTR_PROTATTR, buf,
				 OVL_PROTATTR_MAX);
	if (res < 0)
		return;

	/*
	 * Initialize inode flags from overlay.protattr xattr and upper inode
	 * flags.  If upper inode has those fileattr flags set (i.e. from old
	 * kernel), we do not clear them on ovl_get_inode(), but we will clear
	 * them on next fileattr_set().
	 */
	for (n = 0; n < res; n++) {
		if (buf[n] == 'a')
			iflags |= S_APPEND;
		else if (buf[n] == 'i')
			iflags |= S_IMMUTABLE;
		else
			break;
	}

	if (!res || n < res) {
		pr_warn_ratelimited("incompatible overlay.protattr format (%pd2, len=%d)\n",
				    upper, res);
	} else {
		inode_set_flags(inode, iflags, OVL_PROT_I_FLAGS_MASK);
	}
}

int ovl_set_protattr(struct inode *inode, struct dentry *upper,
		      struct fileattr *fa)
{
	struct ovl_fs *ofs = OVL_FS(inode->i_sb);
	char buf[OVL_PROTATTR_MAX];
	int len = 0, err = 0;
	u32 iflags = 0;

	BUILD_BUG_ON(HWEIGHT32(OVL_PROT_FS_FLAGS_MASK) > OVL_PROTATTR_MAX);

	if (fa->flags & FS_APPEND_FL) {
		buf[len++] = 'a';
		iflags |= S_APPEND;
	}
	if (fa->flags & FS_IMMUTABLE_FL) {
		buf[len++] = 'i';
		iflags |= S_IMMUTABLE;
	}

	/*
	 * Do not allow to set protection flags when upper doesn't support
	 * xattrs, because we do not set those fileattr flags on upper inode.
	 * Remove xattr if it exist and all protection flags are cleared.
	 */
	if (len) {
		err = ovl_check_setxattr(ofs, upper, OVL_XATTR_PROTATTR,
					 buf, len, -EPERM);
	} else if (inode->i_flags & OVL_PROT_I_FLAGS_MASK) {
		err = ovl_removexattr(ofs, upper, OVL_XATTR_PROTATTR);
		if (err == -EOPNOTSUPP || err == -ENODATA)
			err = 0;
	}
	if (err)
		return err;

	inode_set_flags(inode, iflags, OVL_PROT_I_FLAGS_MASK);

	/* Mask out the fileattr flags that should not be set in upper inode */
	fa->flags &= ~OVL_PROT_FS_FLAGS_MASK;
	fa->fsx_xflags &= ~OVL_PROT_FSX_FLAGS_MASK;

	return 0;
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

bool ovl_is_inuse(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	bool inuse;

	spin_lock(&inode->i_lock);
	inuse = (inode->i_state & I_OVL_INUSE);
	spin_unlock(&inode->i_lock);

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
	if (!d_is_dir(lower) && d_inode(lower)->i_nlink > 1)
		return true;

	return false;
}

/* Caller must hold OVL_I(inode)->lock */
static void ovl_cleanup_index(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	struct dentry *indexdir = ovl_indexdir(dentry->d_sb);
	struct inode *dir = indexdir->d_inode;
	struct dentry *lowerdentry = ovl_dentry_lower(dentry);
	struct dentry *upperdentry = ovl_dentry_upper(dentry);
	struct dentry *index = NULL;
	struct inode *inode;
	struct qstr name = { };
	int err;

	err = ovl_get_index_name(ofs, lowerdentry, &name);
	if (err)
		goto fail;

	inode = d_inode(upperdentry);
	if (!S_ISDIR(inode->i_mode) && inode->i_nlink != 1) {
		pr_warn_ratelimited("cleanup linked index (%pd2, ino=%lu, nlink=%u)\n",
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
	index = ovl_lookup_upper(ofs, name.name, indexdir, name.len);
	err = PTR_ERR(index);
	if (IS_ERR(index)) {
		index = NULL;
	} else if (ovl_index_all(dentry->d_sb)) {
		/* Whiteout orphan index to block future open by handle */
		err = ovl_cleanup_and_whiteout(OVL_FS(dentry->d_sb),
					       dir, index);
	} else {
		/* Cleanup orphan index entries */
		err = ovl_cleanup(ofs, dir, index);
	}

	inode_unlock(dir);
	if (err)
		goto fail;

out:
	kfree(name.name);
	dput(index);
	return;

fail:
	pr_err("cleanup index of '%pd2' failed (%i)\n", dentry, err);
	goto out;
}

/*
 * Operations that change overlay inode and upper inode nlink need to be
 * synchronized with copy up for persistent nlink accounting.
 */
int ovl_nlink_start(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	const struct cred *old_cred;
	int err;

	if (WARN_ON(!inode))
		return -ENOENT;

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

	err = ovl_inode_lock_interruptible(inode);
	if (err)
		return err;

	if (d_is_dir(dentry) || !ovl_test_flag(OVL_INDEX, inode))
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
		ovl_inode_unlock(inode);

	return err;
}

void ovl_nlink_end(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (ovl_test_flag(OVL_INDEX, inode) && inode->i_nlink == 0) {
		const struct cred *old_cred;

		old_cred = ovl_override_creds(dentry->d_sb);
		ovl_cleanup_index(dentry);
		revert_creds(old_cred);
	}

	ovl_inode_unlock(inode);
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
	pr_err("failed to lock workdir+upperdir\n");
	return -EIO;
}

/* err < 0, 0 if no metacopy xattr, 1 if metacopy xattr found */
int ovl_check_metacopy_xattr(struct ovl_fs *ofs, const struct path *path)
{
	int res;

	/* Only regular files can have metacopy xattr */
	if (!S_ISREG(d_inode(path->dentry)->i_mode))
		return 0;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_METACOPY, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return 0;
		/*
		 * getxattr on user.* may fail with EACCES in case there's no
		 * read permission on the inode.  Not much we can do, other than
		 * tell the caller that this is not a metacopy inode.
		 */
		if (ofs->config.userxattr && res == -EACCES)
			return 0;
		goto out;
	}

	return 1;
out:
	pr_warn_ratelimited("failed to get metacopy (%i)\n", res);
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

char *ovl_get_redirect_xattr(struct ovl_fs *ofs, const struct path *path, int padding)
{
	int res;
	char *s, *next, *buf = NULL;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_REDIRECT, NULL, 0);
	if (res == -ENODATA || res == -EOPNOTSUPP)
		return NULL;
	if (res < 0)
		goto fail;
	if (res == 0)
		goto invalid;

	buf = kzalloc(res + padding + 1, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_REDIRECT, buf, res);
	if (res < 0)
		goto fail;
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
	pr_warn_ratelimited("invalid redirect (%s)\n", buf);
	res = -EINVAL;
	goto err_free;
fail:
	pr_warn_ratelimited("failed to get redirect (%i)\n", res);
err_free:
	kfree(buf);
	return ERR_PTR(res);
}

/*
 * ovl_sync_status() - Check fs sync status for volatile mounts
 *
 * Returns 1 if this is not a volatile mount and a real sync is required.
 *
 * Returns 0 if syncing can be skipped because mount is volatile, and no errors
 * have occurred on the upperdir since the mount.
 *
 * Returns -errno if it is a volatile mount, and the error that occurred since
 * the last mount. If the error code changes, it'll return the latest error
 * code.
 */

int ovl_sync_status(struct ovl_fs *ofs)
{
	struct vfsmount *mnt;

	if (ovl_should_sync(ofs))
		return 1;

	mnt = ovl_upper_mnt(ofs);
	if (!mnt)
		return 0;

	return errseq_check(&mnt->mnt_sb->s_wb_err, ofs->errseq);
}

/*
 * ovl_copyattr() - copy inode attributes from layer to ovl inode
 *
 * When overlay copies inode information from an upper or lower layer to the
 * relevant overlay inode it will apply the idmapping of the upper or lower
 * layer when doing so ensuring that the ovl inode ownership will correctly
 * reflect the ownership of the idmapped upper or lower layer. For example, an
 * idmapped upper or lower layer mapping id 1001 to id 1000 will take care to
 * map any lower or upper inode owned by id 1001 to id 1000. These mapping
 * helpers are nops when the relevant layer isn't idmapped.
 */
void ovl_copyattr(struct inode *inode)
{
	struct path realpath;
	struct inode *realinode;
	struct user_namespace *real_mnt_userns;
	vfsuid_t vfsuid;
	vfsgid_t vfsgid;

	ovl_i_path_real(inode, &realpath);
	realinode = d_inode(realpath.dentry);
	real_mnt_userns = mnt_user_ns(realpath.mnt);

	vfsuid = i_uid_into_vfsuid(real_mnt_userns, realinode);
	vfsgid = i_gid_into_vfsgid(real_mnt_userns, realinode);

	inode->i_uid = vfsuid_into_kuid(vfsuid);
	inode->i_gid = vfsgid_into_kgid(vfsgid);
	inode->i_mode = realinode->i_mode;
	inode->i_atime = realinode->i_atime;
	inode->i_mtime = realinode->i_mtime;
	inode->i_ctime = realinode->i_ctime;
	i_size_write(inode, i_size_read(realinode));
}
