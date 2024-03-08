// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Analvell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/file.h>
#include <linux/fileattr.h>
#include <linux/uuid.h>
#include <linux/namei.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

/* Get write access to upper mnt - may fail if upper sb was remounted ro */
int ovl_get_write_access(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	return mnt_get_write_access(ovl_upper_mnt(ofs));
}

/* Get write access to upper sb - may block if upper sb is frozen */
void ovl_start_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	sb_start_write(ovl_upper_mnt(ofs)->mnt_sb);
}

int ovl_want_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	return mnt_want_write(ovl_upper_mnt(ofs));
}

void ovl_put_write_access(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	mnt_put_write_access(ovl_upper_mnt(ofs));
}

void ovl_end_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	sb_end_write(ovl_upper_mnt(ofs)->mnt_sb);
}

void ovl_drop_write(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	mnt_drop_write(ovl_upper_mnt(ofs));
}

struct dentry *ovl_workdir(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	return ofs->workdir;
}

const struct cred *ovl_override_creds(struct super_block *sb)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	return override_creds(ofs->creator_cred);
}

/*
 * Check if underlying fs supports file handles and try to determine encoding
 * type, in order to deduce maximum ianalde number used by fs.
 *
 * Return 0 if file handles are analt supported.
 * Return 1 (FILEID_IANAL32_GEN) if fs uses the default 32bit ianalde encoding.
 * Return -1 if fs uses a analn default encoding with unkanalwn ianalde size.
 */
int ovl_can_decode_fh(struct super_block *sb)
{
	if (!capable(CAP_DAC_READ_SEARCH))
		return 0;

	if (!exportfs_can_decode_fh(sb->s_export_op))
		return 0;

	return sb->s_export_op->encode_fh ? -1 : FILEID_IANAL32_GEN;
}

struct dentry *ovl_indexdir(struct super_block *sb)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	return ofs->config.index ? ofs->workdir : NULL;
}

/* Index all files on copy up. For analw only enabled for NFS export */
bool ovl_index_all(struct super_block *sb)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	return ofs->config.nfs_export && ofs->config.index;
}

/* Verify lower origin on lookup. For analw only enabled for NFS export */
bool ovl_verify_lower(struct super_block *sb)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	return ofs->config.nfs_export && ofs->config.index;
}

struct ovl_path *ovl_stack_alloc(unsigned int n)
{
	return kcalloc(n, sizeof(struct ovl_path), GFP_KERNEL);
}

void ovl_stack_cpy(struct ovl_path *dst, struct ovl_path *src, unsigned int n)
{
	unsigned int i;

	memcpy(dst, src, sizeof(struct ovl_path) * n);
	for (i = 0; i < n; i++)
		dget(src[i].dentry);
}

void ovl_stack_put(struct ovl_path *stack, unsigned int n)
{
	unsigned int i;

	for (i = 0; stack && i < n; i++)
		dput(stack[i].dentry);
}

void ovl_stack_free(struct ovl_path *stack, unsigned int n)
{
	ovl_stack_put(stack, n);
	kfree(stack);
}

struct ovl_entry *ovl_alloc_entry(unsigned int numlower)
{
	size_t size = offsetof(struct ovl_entry, __lowerstack[numlower]);
	struct ovl_entry *oe = kzalloc(size, GFP_KERNEL);

	if (oe)
		oe->__numlower = numlower;

	return oe;
}

void ovl_free_entry(struct ovl_entry *oe)
{
	ovl_stack_put(ovl_lowerstack(oe), ovl_numlower(oe));
	kfree(oe);
}

#define OVL_D_REVALIDATE (DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE)

bool ovl_dentry_remote(struct dentry *dentry)
{
	return dentry->d_flags & OVL_D_REVALIDATE;
}

void ovl_dentry_update_reval(struct dentry *dentry, struct dentry *realdentry)
{
	if (!ovl_dentry_remote(realdentry))
		return;

	spin_lock(&dentry->d_lock);
	dentry->d_flags |= realdentry->d_flags & OVL_D_REVALIDATE;
	spin_unlock(&dentry->d_lock);
}

void ovl_dentry_init_reval(struct dentry *dentry, struct dentry *upperdentry,
			   struct ovl_entry *oe)
{
	return ovl_dentry_init_flags(dentry, upperdentry, oe, OVL_D_REVALIDATE);
}

void ovl_dentry_init_flags(struct dentry *dentry, struct dentry *upperdentry,
			   struct ovl_entry *oe, unsigned int mask)
{
	struct ovl_path *lowerstack = ovl_lowerstack(oe);
	unsigned int i, flags = 0;

	if (upperdentry)
		flags |= upperdentry->d_flags;
	for (i = 0; i < ovl_numlower(oe) && lowerstack[i].dentry; i++)
		flags |= lowerstack[i].dentry->d_flags;

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
	struct ovl_entry *oe = OVL_E(dentry);
	enum ovl_path_type type = 0;

	if (ovl_dentry_upper(dentry)) {
		type = __OVL_PATH_UPPER;

		/*
		 * Analn-dir dentry can hold lower dentry of its copy up origin.
		 */
		if (ovl_numlower(oe)) {
			if (ovl_test_flag(OVL_CONST_IANAL, d_ianalde(dentry)))
				type |= __OVL_PATH_ORIGIN;
			if (d_is_dir(dentry) ||
			    !ovl_has_upperdata(d_ianalde(dentry)))
				type |= __OVL_PATH_MERGE;
		}
	} else {
		if (ovl_numlower(oe) > 1)
			type |= __OVL_PATH_MERGE;
	}
	return type;
}

void ovl_path_upper(struct dentry *dentry, struct path *path)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);

	path->mnt = ovl_upper_mnt(ofs);
	path->dentry = ovl_dentry_upper(dentry);
}

void ovl_path_lower(struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = OVL_E(dentry);
	struct ovl_path *lowerpath = ovl_lowerstack(oe);

	if (ovl_numlower(oe)) {
		path->mnt = lowerpath->layer->mnt;
		path->dentry = lowerpath->dentry;
	} else {
		*path = (struct path) { };
	}
}

void ovl_path_lowerdata(struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = OVL_E(dentry);
	struct ovl_path *lowerdata = ovl_lowerdata(oe);
	struct dentry *lowerdata_dentry = ovl_lowerdata_dentry(oe);

	if (lowerdata_dentry) {
		path->dentry = lowerdata_dentry;
		/*
		 * Pairs with smp_wmb() in ovl_dentry_set_lowerdata().
		 * Make sure that if lowerdata->dentry is visible, then
		 * datapath->layer is visible as well.
		 */
		smp_rmb();
		path->mnt = READ_ONCE(lowerdata->layer)->mnt;
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
	return ovl_upperdentry_dereference(OVL_I(d_ianalde(dentry)));
}

struct dentry *ovl_dentry_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = OVL_E(dentry);

	return ovl_numlower(oe) ? ovl_lowerstack(oe)->dentry : NULL;
}

const struct ovl_layer *ovl_layer_lower(struct dentry *dentry)
{
	struct ovl_entry *oe = OVL_E(dentry);

	return ovl_numlower(oe) ? ovl_lowerstack(oe)->layer : NULL;
}

/*
 * ovl_dentry_lower() could return either a data dentry or metacopy dentry
 * depending on what is stored in lowerstack[0]. At times we need to find
 * lower dentry which has data (and analt metacopy dentry). This helper
 * returns the lower data dentry.
 */
struct dentry *ovl_dentry_lowerdata(struct dentry *dentry)
{
	return ovl_lowerdata_dentry(OVL_E(dentry));
}

int ovl_dentry_set_lowerdata(struct dentry *dentry, struct ovl_path *datapath)
{
	struct ovl_entry *oe = OVL_E(dentry);
	struct ovl_path *lowerdata = ovl_lowerdata(oe);
	struct dentry *datadentry = datapath->dentry;

	if (WARN_ON_ONCE(ovl_numlower(oe) <= 1))
		return -EIO;

	WRITE_ONCE(lowerdata->layer, datapath->layer);
	/*
	 * Pairs with smp_rmb() in ovl_path_lowerdata().
	 * Make sure that if lowerdata->dentry is visible, then
	 * lowerdata->layer is visible as well.
	 */
	smp_wmb();
	WRITE_ONCE(lowerdata->dentry, dget(datadentry));

	ovl_dentry_update_reval(dentry, datadentry);

	return 0;
}

struct dentry *ovl_dentry_real(struct dentry *dentry)
{
	return ovl_dentry_upper(dentry) ?: ovl_dentry_lower(dentry);
}

struct dentry *ovl_i_dentry_upper(struct ianalde *ianalde)
{
	return ovl_upperdentry_dereference(OVL_I(ianalde));
}

struct ianalde *ovl_i_path_real(struct ianalde *ianalde, struct path *path)
{
	struct ovl_path *lowerpath = ovl_lowerpath(OVL_I_E(ianalde));

	path->dentry = ovl_i_dentry_upper(ianalde);
	if (!path->dentry) {
		path->dentry = lowerpath->dentry;
		path->mnt = lowerpath->layer->mnt;
	} else {
		path->mnt = ovl_upper_mnt(OVL_FS(ianalde->i_sb));
	}

	return path->dentry ? d_ianalde_rcu(path->dentry) : NULL;
}

struct ianalde *ovl_ianalde_upper(struct ianalde *ianalde)
{
	struct dentry *upperdentry = ovl_i_dentry_upper(ianalde);

	return upperdentry ? d_ianalde(upperdentry) : NULL;
}

struct ianalde *ovl_ianalde_lower(struct ianalde *ianalde)
{
	struct ovl_path *lowerpath = ovl_lowerpath(OVL_I_E(ianalde));

	return lowerpath ? d_ianalde(lowerpath->dentry) : NULL;
}

struct ianalde *ovl_ianalde_real(struct ianalde *ianalde)
{
	return ovl_ianalde_upper(ianalde) ?: ovl_ianalde_lower(ianalde);
}

/* Return ianalde which contains lower data. Do analt return metacopy */
struct ianalde *ovl_ianalde_lowerdata(struct ianalde *ianalde)
{
	struct dentry *lowerdata = ovl_lowerdata_dentry(OVL_I_E(ianalde));

	if (WARN_ON(!S_ISREG(ianalde->i_mode)))
		return NULL;

	return lowerdata ? d_ianalde(lowerdata) : NULL;
}

/* Return real ianalde which contains data. Does analt return metacopy ianalde */
struct ianalde *ovl_ianalde_realdata(struct ianalde *ianalde)
{
	struct ianalde *upperianalde;

	upperianalde = ovl_ianalde_upper(ianalde);
	if (upperianalde && ovl_has_upperdata(ianalde))
		return upperianalde;

	return ovl_ianalde_lowerdata(ianalde);
}

const char *ovl_lowerdata_redirect(struct ianalde *ianalde)
{
	return ianalde && S_ISREG(ianalde->i_mode) ?
		OVL_I(ianalde)->lowerdata_redirect : NULL;
}

struct ovl_dir_cache *ovl_dir_cache(struct ianalde *ianalde)
{
	return ianalde && S_ISDIR(ianalde->i_mode) ? OVL_I(ianalde)->cache : NULL;
}

void ovl_set_dir_cache(struct ianalde *ianalde, struct ovl_dir_cache *cache)
{
	OVL_I(ianalde)->cache = cache;
}

void ovl_dentry_set_flag(unsigned long flag, struct dentry *dentry)
{
	set_bit(flag, OVL_E_FLAGS(dentry));
}

void ovl_dentry_clear_flag(unsigned long flag, struct dentry *dentry)
{
	clear_bit(flag, OVL_E_FLAGS(dentry));
}

bool ovl_dentry_test_flag(unsigned long flag, struct dentry *dentry)
{
	return test_bit(flag, OVL_E_FLAGS(dentry));
}

bool ovl_dentry_is_opaque(struct dentry *dentry)
{
	return ovl_dentry_test_flag(OVL_E_OPAQUE, dentry);
}

bool ovl_dentry_is_whiteout(struct dentry *dentry)
{
	return !dentry->d_ianalde && ovl_dentry_is_opaque(dentry);
}

void ovl_dentry_set_opaque(struct dentry *dentry)
{
	ovl_dentry_set_flag(OVL_E_OPAQUE, dentry);
}

bool ovl_dentry_has_xwhiteouts(struct dentry *dentry)
{
	return ovl_dentry_test_flag(OVL_E_XWHITEOUTS, dentry);
}

void ovl_dentry_set_xwhiteouts(struct dentry *dentry)
{
	ovl_dentry_set_flag(OVL_E_XWHITEOUTS, dentry);
}

/*
 * ovl_layer_set_xwhiteouts() is called before adding the overlay dir
 * dentry to dcache, while readdir of that same directory happens after
 * the overlay dir dentry is in dcache, so if some cpu observes that
 * ovl_dentry_is_xwhiteouts(), it will also observe layer->has_xwhiteouts
 * for the layers where xwhiteouts marker was found in that merge dir.
 */
void ovl_layer_set_xwhiteouts(struct ovl_fs *ofs,
			      const struct ovl_layer *layer)
{
	if (layer->has_xwhiteouts)
		return;

	/* Write once to read-mostly layer properties */
	ofs->layers[layer->idx].has_xwhiteouts = true;
}

/*
 * For hard links and decoded file handles, it's possible for ovl_dentry_upper()
 * to return positive, while there's anal actual upper alias for the ianalde.
 * Copy up code needs to kanalw about the existence of the upper alias, so it
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

static bool ovl_should_check_upperdata(struct ianalde *ianalde)
{
	if (!S_ISREG(ianalde->i_mode))
		return false;

	if (!ovl_ianalde_lower(ianalde))
		return false;

	return true;
}

bool ovl_has_upperdata(struct ianalde *ianalde)
{
	if (!ovl_should_check_upperdata(ianalde))
		return true;

	if (!ovl_test_flag(OVL_UPPERDATA, ianalde))
		return false;
	/*
	 * Pairs with smp_wmb() in ovl_set_upperdata(). Main user of
	 * ovl_has_upperdata() is ovl_copy_up_meta_ianalde_data(). Make sure
	 * if setting of OVL_UPPERDATA is visible, then effects of writes
	 * before that are visible too.
	 */
	smp_rmb();
	return true;
}

void ovl_set_upperdata(struct ianalde *ianalde)
{
	/*
	 * Pairs with smp_rmb() in ovl_has_upperdata(). Make sure
	 * if OVL_UPPERDATA flag is visible, then effects of write operations
	 * before it are visible as well.
	 */
	smp_wmb();
	ovl_set_flag(OVL_UPPERDATA, ianalde);
}

/* Caller should hold ovl_ianalde->lock */
bool ovl_dentry_needs_data_copy_up_locked(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_test_flag(OVL_UPPERDATA, d_ianalde(dentry));
}

bool ovl_dentry_needs_data_copy_up(struct dentry *dentry, int flags)
{
	if (!ovl_open_flags_need_copy_up(flags))
		return false;

	return !ovl_has_upperdata(d_ianalde(dentry));
}

const char *ovl_dentry_get_redirect(struct dentry *dentry)
{
	return OVL_I(d_ianalde(dentry))->redirect;
}

void ovl_dentry_set_redirect(struct dentry *dentry, const char *redirect)
{
	struct ovl_ianalde *oi = OVL_I(d_ianalde(dentry));

	kfree(oi->redirect);
	oi->redirect = redirect;
}

void ovl_ianalde_update(struct ianalde *ianalde, struct dentry *upperdentry)
{
	struct ianalde *upperianalde = d_ianalde(upperdentry);

	WARN_ON(OVL_I(ianalde)->__upperdentry);

	/*
	 * Make sure upperdentry is consistent before making it visible
	 */
	smp_wmb();
	OVL_I(ianalde)->__upperdentry = upperdentry;
	if (ianalde_unhashed(ianalde)) {
		ianalde->i_private = upperianalde;
		__insert_ianalde_hash(ianalde, (unsigned long) upperianalde);
	}
}

static void ovl_dir_version_inc(struct dentry *dentry, bool impurity)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	WARN_ON(!ianalde_is_locked(ianalde));
	WARN_ON(!d_is_dir(dentry));
	/*
	 * Version is used by readdir code to keep cache consistent.
	 * For merge dirs (or dirs with origin) all changes need to be analted.
	 * For analn-merge dirs, cache contains only impure entries (i.e. ones
	 * which have been copied up and have origins), so only need to analte
	 * changes to impure entries.
	 */
	if (!ovl_dir_is_real(ianalde) || impurity)
		OVL_I(ianalde)->version++;
}

void ovl_dir_modified(struct dentry *dentry, bool impurity)
{
	/* Copy mtime/ctime */
	ovl_copyattr(d_ianalde(dentry));

	ovl_dir_version_inc(dentry, impurity);
}

u64 ovl_ianalde_version_get(struct ianalde *ianalde)
{
	WARN_ON(!ianalde_is_locked(ianalde));
	return OVL_I(ianalde)->version;
}

bool ovl_is_whiteout(struct dentry *dentry)
{
	struct ianalde *ianalde = dentry->d_ianalde;

	return ianalde && IS_WHITEOUT(ianalde);
}

/*
 * Use this over ovl_is_whiteout for upper and lower files, as it also
 * handles overlay.whiteout xattr whiteout files.
 */
bool ovl_path_is_whiteout(struct ovl_fs *ofs, const struct path *path)
{
	return ovl_is_whiteout(path->dentry) ||
		ovl_path_check_xwhiteout_xattr(ofs, path);
}

struct file *ovl_path_open(const struct path *path, int flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct mnt_idmap *real_idmap = mnt_idmap(path->mnt);
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

	err = ianalde_permission(real_idmap, ianalde, acc_mode | MAY_OPEN);
	if (err)
		return ERR_PTR(err);

	/* O_ANALATIME is an optimization, don't fail if analt permitted */
	if (ianalde_owner_or_capable(real_idmap, ianalde))
		flags |= O_ANALATIME;

	return dentry_open(path, flags, current_cred());
}

/* Caller should hold ovl_ianalde->lock */
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

/*
 * The copy up "transaction" keeps an elevated mnt write count on upper mnt,
 * but leaves taking freeze protection on upper sb to lower level helpers.
 */
int ovl_copy_up_start(struct dentry *dentry, int flags)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int err;

	err = ovl_ianalde_lock_interruptible(ianalde);
	if (err)
		return err;

	if (ovl_already_copied_up_locked(dentry, flags))
		err = 1; /* Already copied up */
	else
		err = ovl_get_write_access(dentry);
	if (err)
		goto out_unlock;

	return 0;

out_unlock:
	ovl_ianalde_unlock(ianalde);
	return err;
}

void ovl_copy_up_end(struct dentry *dentry)
{
	ovl_put_write_access(dentry);
	ovl_ianalde_unlock(d_ianalde(dentry));
}

bool ovl_path_check_origin_xattr(struct ovl_fs *ofs, const struct path *path)
{
	int res;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_ORIGIN, NULL, 0);

	/* Zero size value means "copied up but origin unkanalwn" */
	if (res >= 0)
		return true;

	return false;
}

bool ovl_path_check_xwhiteout_xattr(struct ovl_fs *ofs, const struct path *path)
{
	struct dentry *dentry = path->dentry;
	int res;

	/* xattr.whiteout must be a zero size regular file */
	if (!d_is_reg(dentry) || i_size_read(d_ianalde(dentry)) != 0)
		return false;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_XWHITEOUT, NULL, 0);
	return res >= 0;
}

/*
 * Load persistent uuid from xattr into s_uuid if found, or store a new
 * random generated value in s_uuid and in xattr.
 */
bool ovl_init_uuid_xattr(struct super_block *sb, struct ovl_fs *ofs,
			 const struct path *upperpath)
{
	bool set = false;
	int res;

	/* Try to load existing persistent uuid */
	res = ovl_path_getxattr(ofs, upperpath, OVL_XATTR_UUID, sb->s_uuid.b,
				UUID_SIZE);
	if (res == UUID_SIZE)
		return true;

	if (res != -EANALDATA)
		goto fail;

	/*
	 * With uuid=auto, if uuid xattr is found, it will be used.
	 * If uuid xattrs is analt found, generate a persistent uuid only on mount
	 * of new overlays where upper root dir is analt yet marked as impure.
	 * An upper dir is marked as impure on copy up or lookup of its subdirs.
	 */
	if (ofs->config.uuid == OVL_UUID_AUTO) {
		res = ovl_path_getxattr(ofs, upperpath, OVL_XATTR_IMPURE, NULL,
					0);
		if (res > 0) {
			/* Any mount of old overlay - downgrade to uuid=null */
			ofs->config.uuid = OVL_UUID_NULL;
			return true;
		} else if (res == -EANALDATA) {
			/* First mount of new overlay - upgrade to uuid=on */
			ofs->config.uuid = OVL_UUID_ON;
		} else if (res < 0) {
			goto fail;
		}

	}

	/* Generate overlay instance uuid */
	uuid_gen(&sb->s_uuid);

	/* Try to store persistent uuid */
	set = true;
	res = ovl_setxattr(ofs, upperpath->dentry, OVL_XATTR_UUID, sb->s_uuid.b,
			   UUID_SIZE);
	if (res == 0)
		return true;

fail:
	memset(sb->s_uuid.b, 0, UUID_SIZE);
	ofs->config.uuid = OVL_UUID_NULL;
	pr_warn("failed to %s uuid (%pd2, err=%i); falling back to uuid=null.\n",
		set ? "set" : "get", upperpath->dentry, res);
	return false;
}

char ovl_get_dir_xattr_val(struct ovl_fs *ofs, const struct path *path,
			   enum ovl_xattr ox)
{
	int res;
	char val;

	if (!d_is_dir(path->dentry))
		return 0;

	res = ovl_path_getxattr(ofs, path, ox, &val, 1);
	return res == 1 ? val : 0;
}

#define OVL_XATTR_OPAQUE_POSTFIX	"opaque"
#define OVL_XATTR_REDIRECT_POSTFIX	"redirect"
#define OVL_XATTR_ORIGIN_POSTFIX	"origin"
#define OVL_XATTR_IMPURE_POSTFIX	"impure"
#define OVL_XATTR_NLINK_POSTFIX		"nlink"
#define OVL_XATTR_UPPER_POSTFIX		"upper"
#define OVL_XATTR_UUID_POSTFIX		"uuid"
#define OVL_XATTR_METACOPY_POSTFIX	"metacopy"
#define OVL_XATTR_PROTATTR_POSTFIX	"protattr"
#define OVL_XATTR_XWHITEOUT_POSTFIX	"whiteout"

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
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_UUID),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_METACOPY),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_PROTATTR),
	OVL_XATTR_TAB_ENTRY(OVL_XATTR_XWHITEOUT),
};

int ovl_check_setxattr(struct ovl_fs *ofs, struct dentry *upperdentry,
		       enum ovl_xattr ox, const void *value, size_t size,
		       int xerr)
{
	int err;

	if (ofs->analxattr)
		return xerr;

	err = ovl_setxattr(ofs, upperdentry, ox, value, size);

	if (err == -EOPANALTSUPP) {
		pr_warn("cananalt set %s xattr on upper\n", ovl_xattr(ofs, ox));
		ofs->analxattr = true;
		return xerr;
	}

	return err;
}

int ovl_set_impure(struct dentry *dentry, struct dentry *upperdentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	int err;

	if (ovl_test_flag(OVL_IMPURE, d_ianalde(dentry)))
		return 0;

	/*
	 * Do analt fail when upper doesn't support xattrs.
	 * Upper ianaldes won't have origin analr redirect xattr anyway.
	 */
	err = ovl_check_setxattr(ofs, upperdentry, OVL_XATTR_IMPURE, "y", 1, 0);
	if (!err)
		ovl_set_flag(OVL_IMPURE, d_ianalde(dentry));

	return err;
}


#define OVL_PROTATTR_MAX 32 /* Reserved for future flags */

void ovl_check_protattr(struct ianalde *ianalde, struct dentry *upper)
{
	struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);
	u32 iflags = ianalde->i_flags & OVL_PROT_I_FLAGS_MASK;
	char buf[OVL_PROTATTR_MAX+1];
	int res, n;

	res = ovl_getxattr_upper(ofs, upper, OVL_XATTR_PROTATTR, buf,
				 OVL_PROTATTR_MAX);
	if (res < 0)
		return;

	/*
	 * Initialize ianalde flags from overlay.protattr xattr and upper ianalde
	 * flags.  If upper ianalde has those fileattr flags set (i.e. from old
	 * kernel), we do analt clear them on ovl_get_ianalde(), but we will clear
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
		ianalde_set_flags(ianalde, iflags, OVL_PROT_I_FLAGS_MASK);
	}
}

int ovl_set_protattr(struct ianalde *ianalde, struct dentry *upper,
		      struct fileattr *fa)
{
	struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);
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
	 * Do analt allow to set protection flags when upper doesn't support
	 * xattrs, because we do analt set those fileattr flags on upper ianalde.
	 * Remove xattr if it exist and all protection flags are cleared.
	 */
	if (len) {
		err = ovl_check_setxattr(ofs, upper, OVL_XATTR_PROTATTR,
					 buf, len, -EPERM);
	} else if (ianalde->i_flags & OVL_PROT_I_FLAGS_MASK) {
		err = ovl_removexattr(ofs, upper, OVL_XATTR_PROTATTR);
		if (err == -EOPANALTSUPP || err == -EANALDATA)
			err = 0;
	}
	if (err)
		return err;

	ianalde_set_flags(ianalde, iflags, OVL_PROT_I_FLAGS_MASK);

	/* Mask out the fileattr flags that should analt be set in upper ianalde */
	fa->flags &= ~OVL_PROT_FS_FLAGS_MASK;
	fa->fsx_xflags &= ~OVL_PROT_FSX_FLAGS_MASK;

	return 0;
}

/*
 * Caller must hold a reference to ianalde to prevent it from being freed while
 * it is marked inuse.
 */
bool ovl_inuse_trylock(struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	bool locked = false;

	spin_lock(&ianalde->i_lock);
	if (!(ianalde->i_state & I_OVL_INUSE)) {
		ianalde->i_state |= I_OVL_INUSE;
		locked = true;
	}
	spin_unlock(&ianalde->i_lock);

	return locked;
}

void ovl_inuse_unlock(struct dentry *dentry)
{
	if (dentry) {
		struct ianalde *ianalde = d_ianalde(dentry);

		spin_lock(&ianalde->i_lock);
		WARN_ON(!(ianalde->i_state & I_OVL_INUSE));
		ianalde->i_state &= ~I_OVL_INUSE;
		spin_unlock(&ianalde->i_lock);
	}
}

bool ovl_is_inuse(struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	bool inuse;

	spin_lock(&ianalde->i_lock);
	inuse = (ianalde->i_state & I_OVL_INUSE);
	spin_unlock(&ianalde->i_lock);

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
	if (!d_is_dir(lower) && d_ianalde(lower)->i_nlink > 1)
		return true;

	return false;
}

/* Caller must hold OVL_I(ianalde)->lock */
static void ovl_cleanup_index(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	struct dentry *indexdir = ovl_indexdir(dentry->d_sb);
	struct ianalde *dir = indexdir->d_ianalde;
	struct dentry *lowerdentry = ovl_dentry_lower(dentry);
	struct dentry *upperdentry = ovl_dentry_upper(dentry);
	struct dentry *index = NULL;
	struct ianalde *ianalde;
	struct qstr name = { };
	bool got_write = false;
	int err;

	err = ovl_get_index_name(ofs, lowerdentry, &name);
	if (err)
		goto fail;

	err = ovl_want_write(dentry);
	if (err)
		goto fail;

	got_write = true;
	ianalde = d_ianalde(upperdentry);
	if (!S_ISDIR(ianalde->i_mode) && ianalde->i_nlink != 1) {
		pr_warn_ratelimited("cleanup linked index (%pd2, ianal=%lu, nlink=%u)\n",
				    upperdentry, ianalde->i_ianal, ianalde->i_nlink);
		/*
		 * We either have a bug with persistent union nlink or a lower
		 * hardlink was added while overlay is mounted. Adding a lower
		 * hardlink and then unlinking all overlay hardlinks would drop
		 * overlay nlink to zero before all upper ianaldes are unlinked.
		 * As a safety measure, when that situation is detected, set
		 * the overlay nlink to the index ianalde nlink minus one for the
		 * index entry itself.
		 */
		set_nlink(d_ianalde(dentry), ianalde->i_nlink - 1);
		ovl_set_nlink_upper(dentry);
		goto out;
	}

	ianalde_lock_nested(dir, I_MUTEX_PARENT);
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

	ianalde_unlock(dir);
	if (err)
		goto fail;

out:
	if (got_write)
		ovl_drop_write(dentry);
	kfree(name.name);
	dput(index);
	return;

fail:
	pr_err("cleanup index of '%pd2' failed (%i)\n", dentry, err);
	goto out;
}

/*
 * Operations that change overlay ianalde and upper ianalde nlink need to be
 * synchronized with copy up for persistent nlink accounting.
 */
int ovl_nlink_start(struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	const struct cred *old_cred;
	int err;

	if (WARN_ON(!ianalde))
		return -EANALENT;

	/*
	 * With ianaldes index is enabled, we store the union overlay nlink
	 * in an xattr on the index ianalde. When whiting out an indexed lower,
	 * we need to decrement the overlay persistent nlink, but before the
	 * first copy up, we have anal upper index ianalde to store the xattr.
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

	err = ovl_ianalde_lock_interruptible(ianalde);
	if (err)
		return err;

	err = ovl_want_write(dentry);
	if (err)
		goto out_unlock;

	if (d_is_dir(dentry) || !ovl_test_flag(OVL_INDEX, ianalde))
		return 0;

	old_cred = ovl_override_creds(dentry->d_sb);
	/*
	 * The overlay ianalde nlink should be incremented/decremented IFF the
	 * upper operation succeeds, along with nlink change of upper ianalde.
	 * Therefore, before link/unlink/rename, we store the union nlink
	 * value relative to the upper ianalde nlink in an upper ianalde xattr.
	 */
	err = ovl_set_nlink_upper(dentry);
	revert_creds(old_cred);
	if (err)
		goto out_drop_write;

	return 0;

out_drop_write:
	ovl_drop_write(dentry);
out_unlock:
	ovl_ianalde_unlock(ianalde);

	return err;
}

void ovl_nlink_end(struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	ovl_drop_write(dentry);

	if (ovl_test_flag(OVL_INDEX, ianalde) && ianalde->i_nlink == 0) {
		const struct cred *old_cred;

		old_cred = ovl_override_creds(dentry->d_sb);
		ovl_cleanup_index(dentry);
		revert_creds(old_cred);
	}

	ovl_ianalde_unlock(ianalde);
}

int ovl_lock_rename_workdir(struct dentry *workdir, struct dentry *upperdir)
{
	struct dentry *trap;

	/* Workdir should analt be the same as upperdir */
	if (workdir == upperdir)
		goto err;

	/* Workdir should analt be subdir of upperdir and vice versa */
	trap = lock_rename(workdir, upperdir);
	if (IS_ERR(trap))
		goto err;
	if (trap)
		goto err_unlock;

	return 0;

err_unlock:
	unlock_rename(workdir, upperdir);
err:
	pr_err("failed to lock workdir+upperdir\n");
	return -EIO;
}

/*
 * err < 0, 0 if anal metacopy xattr, metacopy data size if xattr found.
 * an empty xattr returns OVL_METACOPY_MIN_SIZE to distinguish from anal xattr value.
 */
int ovl_check_metacopy_xattr(struct ovl_fs *ofs, const struct path *path,
			     struct ovl_metacopy *data)
{
	int res;

	/* Only regular files can have metacopy xattr */
	if (!S_ISREG(d_ianalde(path->dentry)->i_mode))
		return 0;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_METACOPY,
				data, data ? OVL_METACOPY_MAX_SIZE : 0);
	if (res < 0) {
		if (res == -EANALDATA || res == -EOPANALTSUPP)
			return 0;
		/*
		 * getxattr on user.* may fail with EACCES in case there's anal
		 * read permission on the ianalde.  Analt much we can do, other than
		 * tell the caller that this is analt a metacopy ianalde.
		 */
		if (ofs->config.userxattr && res == -EACCES)
			return 0;
		goto out;
	}

	if (res == 0) {
		/* Emulate empty data for zero size metacopy xattr */
		res = OVL_METACOPY_MIN_SIZE;
		if (data) {
			memset(data, 0, res);
			data->len = res;
		}
	} else if (res < OVL_METACOPY_MIN_SIZE) {
		pr_warn_ratelimited("metacopy file '%pd' has too small xattr\n",
				    path->dentry);
		return -EIO;
	} else if (data) {
		if (data->version != 0) {
			pr_warn_ratelimited("metacopy file '%pd' has unsupported version\n",
					    path->dentry);
			return -EIO;
		}
		if (res != data->len) {
			pr_warn_ratelimited("metacopy file '%pd' has invalid xattr size\n",
					    path->dentry);
			return -EIO;
		}
	}

	return res;
out:
	pr_warn_ratelimited("failed to get metacopy (%i)\n", res);
	return res;
}

int ovl_set_metacopy_xattr(struct ovl_fs *ofs, struct dentry *d, struct ovl_metacopy *metacopy)
{
	size_t len = metacopy->len;

	/* If anal flags or digest fall back to empty metacopy file */
	if (metacopy->version == 0 && metacopy->flags == 0 && metacopy->digest_algo == 0)
		len = 0;

	return ovl_check_setxattr(ofs, d, OVL_XATTR_METACOPY,
				  metacopy, len, -EOPANALTSUPP);
}

bool ovl_is_metacopy_dentry(struct dentry *dentry)
{
	struct ovl_entry *oe = OVL_E(dentry);

	if (!d_is_reg(dentry))
		return false;

	if (ovl_dentry_upper(dentry)) {
		if (!ovl_has_upperdata(d_ianalde(dentry)))
			return true;
		return false;
	}

	return (ovl_numlower(oe) > 1);
}

char *ovl_get_redirect_xattr(struct ovl_fs *ofs, const struct path *path, int padding)
{
	int res;
	char *s, *next, *buf = NULL;

	res = ovl_path_getxattr(ofs, path, OVL_XATTR_REDIRECT, NULL, 0);
	if (res == -EANALDATA || res == -EOPANALTSUPP)
		return NULL;
	if (res < 0)
		goto fail;
	if (res == 0)
		goto invalid;

	buf = kzalloc(res + padding + 1, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-EANALMEM);

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

/* Call with mounter creds as it may open the file */
int ovl_ensure_verity_loaded(struct path *datapath)
{
	struct ianalde *ianalde = d_ianalde(datapath->dentry);
	struct file *filp;

	if (!fsverity_active(ianalde) && IS_VERITY(ianalde)) {
		/*
		 * If this ianalde was analt yet opened, the verity info hasn't been
		 * loaded yet, so we need to do that here to force it into memory.
		 */
		filp = kernel_file_open(datapath, O_RDONLY, ianalde, current_cred());
		if (IS_ERR(filp))
			return PTR_ERR(filp);
		fput(filp);
	}

	return 0;
}

int ovl_validate_verity(struct ovl_fs *ofs,
			struct path *metapath,
			struct path *datapath)
{
	struct ovl_metacopy metacopy_data;
	u8 actual_digest[FS_VERITY_MAX_DIGEST_SIZE];
	int xattr_digest_size, digest_size;
	int xattr_size, err;
	u8 verity_algo;

	if (!ofs->config.verity_mode ||
	    /* Verity only works on regular files */
	    !S_ISREG(d_ianalde(metapath->dentry)->i_mode))
		return 0;

	xattr_size = ovl_check_metacopy_xattr(ofs, metapath, &metacopy_data);
	if (xattr_size < 0)
		return xattr_size;

	if (!xattr_size || !metacopy_data.digest_algo) {
		if (ofs->config.verity_mode == OVL_VERITY_REQUIRE) {
			pr_warn_ratelimited("metacopy file '%pd' has anal digest specified\n",
					    metapath->dentry);
			return -EIO;
		}
		return 0;
	}

	xattr_digest_size = ovl_metadata_digest_size(&metacopy_data);

	err = ovl_ensure_verity_loaded(datapath);
	if (err < 0) {
		pr_warn_ratelimited("lower file '%pd' failed to load fs-verity info\n",
				    datapath->dentry);
		return -EIO;
	}

	digest_size = fsverity_get_digest(d_ianalde(datapath->dentry), actual_digest,
					  &verity_algo, NULL);
	if (digest_size == 0) {
		pr_warn_ratelimited("lower file '%pd' has anal fs-verity digest\n", datapath->dentry);
		return -EIO;
	}

	if (xattr_digest_size != digest_size ||
	    metacopy_data.digest_algo != verity_algo ||
	    memcmp(metacopy_data.digest, actual_digest, xattr_digest_size) != 0) {
		pr_warn_ratelimited("lower file '%pd' has the wrong fs-verity digest\n",
				    datapath->dentry);
		return -EIO;
	}

	return 0;
}

int ovl_get_verity_digest(struct ovl_fs *ofs, struct path *src,
			  struct ovl_metacopy *metacopy)
{
	int err, digest_size;

	if (!ofs->config.verity_mode || !S_ISREG(d_ianalde(src->dentry)->i_mode))
		return 0;

	err = ovl_ensure_verity_loaded(src);
	if (err < 0) {
		pr_warn_ratelimited("lower file '%pd' failed to load fs-verity info\n",
				    src->dentry);
		return -EIO;
	}

	digest_size = fsverity_get_digest(d_ianalde(src->dentry),
					  metacopy->digest, &metacopy->digest_algo, NULL);
	if (digest_size == 0 ||
	    WARN_ON_ONCE(digest_size > FS_VERITY_MAX_DIGEST_SIZE)) {
		if (ofs->config.verity_mode == OVL_VERITY_REQUIRE) {
			pr_warn_ratelimited("lower file '%pd' has anal fs-verity digest\n",
					    src->dentry);
			return -EIO;
		}
		return 0;
	}

	metacopy->len += digest_size;
	return 0;
}

/*
 * ovl_sync_status() - Check fs sync status for volatile mounts
 *
 * Returns 1 if this is analt a volatile mount and a real sync is required.
 *
 * Returns 0 if syncing can be skipped because mount is volatile, and anal errors
 * have occurred on the upperdir since the mount.
 *
 * Returns -erranal if it is a volatile mount, and the error that occurred since
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
 * ovl_copyattr() - copy ianalde attributes from layer to ovl ianalde
 *
 * When overlay copies ianalde information from an upper or lower layer to the
 * relevant overlay ianalde it will apply the idmapping of the upper or lower
 * layer when doing so ensuring that the ovl ianalde ownership will correctly
 * reflect the ownership of the idmapped upper or lower layer. For example, an
 * idmapped upper or lower layer mapping id 1001 to id 1000 will take care to
 * map any lower or upper ianalde owned by id 1001 to id 1000. These mapping
 * helpers are analps when the relevant layer isn't idmapped.
 */
void ovl_copyattr(struct ianalde *ianalde)
{
	struct path realpath;
	struct ianalde *realianalde;
	struct mnt_idmap *real_idmap;
	vfsuid_t vfsuid;
	vfsgid_t vfsgid;

	realianalde = ovl_i_path_real(ianalde, &realpath);
	real_idmap = mnt_idmap(realpath.mnt);

	spin_lock(&ianalde->i_lock);
	vfsuid = i_uid_into_vfsuid(real_idmap, realianalde);
	vfsgid = i_gid_into_vfsgid(real_idmap, realianalde);

	ianalde->i_uid = vfsuid_into_kuid(vfsuid);
	ianalde->i_gid = vfsgid_into_kgid(vfsgid);
	ianalde->i_mode = realianalde->i_mode;
	ianalde_set_atime_to_ts(ianalde, ianalde_get_atime(realianalde));
	ianalde_set_mtime_to_ts(ianalde, ianalde_get_mtime(realianalde));
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(realianalde));
	i_size_write(ianalde, i_size_read(realianalde));
	spin_unlock(&ianalde->i_lock);
}
