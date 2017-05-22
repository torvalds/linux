/*
 * fs/sdcardfs/dentry.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include "linux/ctype.h"

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int sdcardfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int err = 1;
	struct path parent_lower_path, lower_path;
	struct dentry *parent_dentry = NULL;
	struct dentry *parent_lower_dentry = NULL;
	struct dentry *lower_cur_parent_dentry = NULL;
	struct dentry *lower_dentry = NULL;
	struct inode *inode;
	struct sdcardfs_inode_data *data;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	spin_lock(&dentry->d_lock);
	if (IS_ROOT(dentry)) {
		spin_unlock(&dentry->d_lock);
		return 1;
	}
	spin_unlock(&dentry->d_lock);

	/* check uninitialized obb_dentry and
	 * whether the base obbpath has been changed or not
	 */
	if (is_obbpath_invalid(dentry)) {
		d_drop(dentry);
		return 0;
	}

	parent_dentry = dget_parent(dentry);
	sdcardfs_get_lower_path(parent_dentry, &parent_lower_path);
	sdcardfs_get_real_lower(dentry, &lower_path);
	parent_lower_dentry = parent_lower_path.dentry;
	lower_dentry = lower_path.dentry;
	lower_cur_parent_dentry = dget_parent(lower_dentry);

	if ((lower_dentry->d_flags & DCACHE_OP_REVALIDATE)) {
		err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
		if (err == 0) {
			d_drop(dentry);
			goto out;
		}
	}

	spin_lock(&lower_dentry->d_lock);
	if (d_unhashed(lower_dentry)) {
		spin_unlock(&lower_dentry->d_lock);
		d_drop(dentry);
		err = 0;
		goto out;
	}
	spin_unlock(&lower_dentry->d_lock);

	if (parent_lower_dentry != lower_cur_parent_dentry) {
		d_drop(dentry);
		err = 0;
		goto out;
	}

	if (dentry < lower_dentry) {
		spin_lock(&dentry->d_lock);
		spin_lock_nested(&lower_dentry->d_lock, DENTRY_D_LOCK_NESTED);
	} else {
		spin_lock(&lower_dentry->d_lock);
		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	}

	if (!qstr_case_eq(&dentry->d_name, &lower_dentry->d_name)) {
		__d_drop(dentry);
		err = 0;
	}

	if (dentry < lower_dentry) {
		spin_unlock(&lower_dentry->d_lock);
		spin_unlock(&dentry->d_lock);
	} else {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&lower_dentry->d_lock);
	}
	if (!err)
		goto out;

	/* If our top's inode is gone, we may be out of date */
	inode = igrab(d_inode(dentry));
	if (inode) {
		data = top_data_get(SDCARDFS_I(inode));
		if (!data || data->abandoned) {
			d_drop(dentry);
			err = 0;
		}
		if (data)
			data_put(data);
		iput(inode);
	}

out:
	dput(parent_dentry);
	dput(lower_cur_parent_dentry);
	sdcardfs_put_lower_path(parent_dentry, &parent_lower_path);
	sdcardfs_put_real_lower(dentry, &lower_path);
	return err;
}

static void sdcardfs_d_release(struct dentry *dentry)
{
	/* release and reset the lower paths */
	if (has_graft_path(dentry))
		sdcardfs_put_reset_orig_path(dentry);
	sdcardfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
}

static int sdcardfs_hash_ci(const struct dentry *dentry,
				struct qstr *qstr)
{
	/*
	 * This function is copy of vfat_hashi.
	 * FIXME Should we support national language?
	 *       Refer to vfat_hashi()
	 * struct nls_table *t = MSDOS_SB(dentry->d_sb)->nls_io;
	 */
	const unsigned char *name;
	unsigned int len;
	unsigned long hash;

	name = qstr->name;
	len = qstr->len;

	hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(tolower(*name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two vfat names.
 */
static int sdcardfs_cmp_ci(const struct dentry *parent,
		const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	/* FIXME Should we support national language? */

	if (name->len == len) {
		if (str_n_case_eq(name->name, str, len))
			return 0;
	}
	return 1;
}

static void sdcardfs_canonical_path(const struct path *path,
				struct path *actual_path)
{
	sdcardfs_get_real_lower(path->dentry, actual_path);
}

const struct dentry_operations sdcardfs_ci_dops = {
	.d_revalidate	= sdcardfs_d_revalidate,
	.d_release	= sdcardfs_d_release,
	.d_hash	= sdcardfs_hash_ci,
	.d_compare	= sdcardfs_cmp_ci,
	.d_canonical_path = sdcardfs_canonical_path,
};

