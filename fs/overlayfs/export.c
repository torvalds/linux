/*
 * Overlayfs NFS export support.
 *
 * Amir Goldstein <amir73il@gmail.com>
 *
 * Copyright (C) 2017-2018 CTERA Networks. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

static int ovl_d_to_fh(struct dentry *dentry, char *buf, int buflen)
{
	struct dentry *upper = ovl_dentry_upper(dentry);
	struct dentry *origin = ovl_dentry_lower(dentry);
	struct ovl_fh *fh = NULL;
	int err;

	/*
	 * On overlay with an upper layer, overlay root inode is encoded as
	 * an upper file handle, because upper root dir is not indexed.
	 */
	if (dentry == dentry->d_sb->s_root && upper)
		origin = NULL;

	err = -EACCES;
	if (!upper || origin)
		goto fail;

	/* TODO: encode non pure-upper by origin */
	fh = ovl_encode_fh(upper, true);

	err = -EOVERFLOW;
	if (fh->len > buflen)
		goto fail;

	memcpy(buf, (char *)fh, fh->len);
	err = fh->len;

out:
	kfree(fh);
	return err;

fail:
	pr_warn_ratelimited("overlayfs: failed to encode file handle (%pd2, err=%i, buflen=%d, len=%d, type=%d)\n",
			    dentry, err, buflen, fh ? (int)fh->len : 0,
			    fh ? fh->type : 0);
	goto out;
}

static int ovl_dentry_to_fh(struct dentry *dentry, u32 *fid, int *max_len)
{
	int res, len = *max_len << 2;

	res = ovl_d_to_fh(dentry, (char *)fid, len);
	if (res <= 0)
		return FILEID_INVALID;

	len = res;

	/* Round up to dwords */
	*max_len = (len + 3) >> 2;
	return OVL_FILEID;
}

static int ovl_encode_inode_fh(struct inode *inode, u32 *fid, int *max_len,
			       struct inode *parent)
{
	struct dentry *dentry;
	int type;

	/* TODO: encode connectable file handles */
	if (parent)
		return FILEID_INVALID;

	dentry = d_find_any_alias(inode);
	if (WARN_ON(!dentry))
		return FILEID_INVALID;

	type = ovl_dentry_to_fh(dentry, fid, max_len);

	dput(dentry);
	return type;
}

/*
 * Find or instantiate an overlay dentry from real dentries.
 */
static struct dentry *ovl_obtain_alias(struct super_block *sb,
				       struct dentry *upper,
				       struct ovl_path *lowerpath)
{
	struct inode *inode;
	struct dentry *dentry;
	struct ovl_entry *oe;
	void *fsdata = &oe;

	/* TODO: obtain non pure-upper */
	if (lowerpath)
		return ERR_PTR(-EIO);

	inode = ovl_get_inode(sb, dget(upper), NULL, NULL, 0);
	if (IS_ERR(inode)) {
		dput(upper);
		return ERR_CAST(inode);
	}

	dentry = d_find_any_alias(inode);
	if (!dentry) {
		dentry = d_alloc_anon(inode->i_sb);
		if (!dentry)
			goto nomem;
		oe = ovl_alloc_entry(0);
		if (!oe)
			goto nomem;

		dentry->d_fsdata = oe;
		ovl_dentry_set_upper_alias(dentry);
	}

	return d_instantiate_anon(dentry, inode);

nomem:
	iput(inode);
	dput(dentry);
	return ERR_PTR(-ENOMEM);
}

static struct dentry *ovl_upper_fh_to_d(struct super_block *sb,
					struct ovl_fh *fh)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct dentry *dentry;
	struct dentry *upper;

	if (!ofs->upper_mnt)
		return ERR_PTR(-EACCES);

	upper = ovl_decode_fh(fh, ofs->upper_mnt);
	if (IS_ERR_OR_NULL(upper))
		return upper;

	dentry = ovl_obtain_alias(sb, upper, NULL);
	dput(upper);

	return dentry;
}

static struct dentry *ovl_fh_to_dentry(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	struct dentry *dentry = NULL;
	struct ovl_fh *fh = (struct ovl_fh *) fid;
	int len = fh_len << 2;
	unsigned int flags = 0;
	int err;

	err = -EINVAL;
	if (fh_type != OVL_FILEID)
		goto out_err;

	err = ovl_check_fh_len(fh, len);
	if (err)
		goto out_err;

	/* TODO: decode non-upper */
	flags = fh->flags;
	if (flags & OVL_FH_FLAG_PATH_UPPER)
		dentry = ovl_upper_fh_to_d(sb, fh);
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry) && err != -ESTALE)
		goto out_err;

	return dentry;

out_err:
	pr_warn_ratelimited("overlayfs: failed to decode file handle (len=%d, type=%d, flags=%x, err=%i)\n",
			    len, fh_type, flags, err);
	return ERR_PTR(err);
}

const struct export_operations ovl_export_operations = {
	.encode_fh	= ovl_encode_inode_fh,
	.fh_to_dentry	= ovl_fh_to_dentry,
};
