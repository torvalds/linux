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

/*
 * We only need to encode origin if there is a chance that the same object was
 * encoded pre copy up and then we need to stay consistent with the same
 * encoding also after copy up. If non-pure upper is not indexed, then it was
 * copied up before NFS export was enabled. In that case we don't need to worry
 * about staying consistent with pre copy up encoding and we encode an upper
 * file handle. Overlay root dentry is a private case of non-indexed upper.
 *
 * The following table summarizes the different file handle encodings used for
 * different overlay object types:
 *
 *  Object type		| Encoding
 * --------------------------------
 *  Pure upper		| U
 *  Non-indexed upper	| U
 *  Indexed upper	| L (*)
 *  Non-upper		| L (*)
 *
 * U = upper file handle
 * L = lower file handle
 *
 * (*) Connecting an overlay dir from real lower dentry is not always
 * possible when there are redirects in lower layers. To mitigate this case,
 * we copy up the lower dir first and then encode an upper dir file handle.
 */
static bool ovl_should_encode_origin(struct dentry *dentry)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;

	if (!ovl_dentry_lower(dentry))
		return false;

	/*
	 * Decoding a merge dir, whose origin's parent is under a redirected
	 * lower dir is not always possible. As a simple aproximation, we do
	 * not encode lower dir file handles when overlay has multiple lower
	 * layers and origin is below the topmost lower layer.
	 *
	 * TODO: copy up only the parent that is under redirected lower.
	 */
	if (d_is_dir(dentry) && ofs->upper_mnt &&
	    OVL_E(dentry)->lowerstack[0].layer->idx > 1)
		return false;

	/* Decoding a non-indexed upper from origin is not implemented */
	if (ovl_dentry_upper(dentry) &&
	    !ovl_test_flag(OVL_INDEX, d_inode(dentry)))
		return false;

	return true;
}

static int ovl_encode_maybe_copy_up(struct dentry *dentry)
{
	int err;

	if (ovl_dentry_upper(dentry))
		return 0;

	err = ovl_want_write(dentry);
	if (err)
		return err;

	err = ovl_copy_up(dentry);

	ovl_drop_write(dentry);
	return err;
}

static int ovl_d_to_fh(struct dentry *dentry, char *buf, int buflen)
{
	struct dentry *origin = ovl_dentry_lower(dentry);
	struct ovl_fh *fh = NULL;
	int err;

	/*
	 * If we should not encode a lower dir file handle, copy up and encode
	 * an upper dir file handle.
	 */
	if (!ovl_should_encode_origin(dentry)) {
		err = ovl_encode_maybe_copy_up(dentry);
		if (err)
			goto fail;

		origin = NULL;
	}

	/* Encode an upper or origin file handle */
	fh = ovl_encode_fh(origin ?: ovl_dentry_upper(dentry), !origin);
	err = PTR_ERR(fh);
	if (IS_ERR(fh))
		goto fail;

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
 * Find or instantiate an overlay dentry from real dentries and index.
 */
static struct dentry *ovl_obtain_alias(struct super_block *sb,
				       struct dentry *upper_alias,
				       struct ovl_path *lowerpath,
				       struct dentry *index)
{
	struct dentry *lower = lowerpath ? lowerpath->dentry : NULL;
	struct dentry *upper = upper_alias ?: index;
	struct dentry *dentry;
	struct inode *inode;
	struct ovl_entry *oe;

	/* We get overlay directory dentries with ovl_lookup_real() */
	if (d_is_dir(upper ?: lower))
		return ERR_PTR(-EIO);

	inode = ovl_get_inode(sb, dget(upper), lower, index, !!lower);
	if (IS_ERR(inode)) {
		dput(upper);
		return ERR_CAST(inode);
	}

	if (index)
		ovl_set_flag(OVL_INDEX, inode);

	dentry = d_find_any_alias(inode);
	if (!dentry) {
		dentry = d_alloc_anon(inode->i_sb);
		if (!dentry)
			goto nomem;
		oe = ovl_alloc_entry(lower ? 1 : 0);
		if (!oe)
			goto nomem;

		if (lower) {
			oe->lowerstack->dentry = dget(lower);
			oe->lowerstack->layer = lowerpath->layer;
		}
		dentry->d_fsdata = oe;
		if (upper_alias)
			ovl_dentry_set_upper_alias(dentry);
	}

	return d_instantiate_anon(dentry, inode);

nomem:
	iput(inode);
	dput(dentry);
	return ERR_PTR(-ENOMEM);
}

/* Get the upper or lower dentry in stach whose on layer @idx */
static struct dentry *ovl_dentry_real_at(struct dentry *dentry, int idx)
{
	struct ovl_entry *oe = dentry->d_fsdata;
	int i;

	if (!idx)
		return ovl_dentry_upper(dentry);

	for (i = 0; i < oe->numlower; i++) {
		if (oe->lowerstack[i].layer->idx == idx)
			return oe->lowerstack[i].dentry;
	}

	return NULL;
}

/*
 * Lookup a child overlay dentry to get a connected overlay dentry whose real
 * dentry is @real. If @real is on upper layer, we lookup a child overlay
 * dentry with the same name as the real dentry. Otherwise, we need to consult
 * index for lookup.
 */
static struct dentry *ovl_lookup_real_one(struct dentry *connected,
					  struct dentry *real,
					  struct ovl_layer *layer)
{
	struct inode *dir = d_inode(connected);
	struct dentry *this, *parent = NULL;
	struct name_snapshot name;
	int err;

	/*
	 * Lookup child overlay dentry by real name. The dir mutex protects us
	 * from racing with overlay rename. If the overlay dentry that is above
	 * real has already been moved to a parent that is not under the
	 * connected overlay dir, we return -ECHILD and restart the lookup of
	 * connected real path from the top.
	 */
	inode_lock_nested(dir, I_MUTEX_PARENT);
	err = -ECHILD;
	parent = dget_parent(real);
	if (ovl_dentry_real_at(connected, layer->idx) != parent)
		goto fail;

	/*
	 * We also need to take a snapshot of real dentry name to protect us
	 * from racing with underlying layer rename. In this case, we don't
	 * care about returning ESTALE, only from dereferencing a free name
	 * pointer because we hold no lock on the real dentry.
	 */
	take_dentry_name_snapshot(&name, real);
	this = lookup_one_len(name.name, connected, strlen(name.name));
	err = PTR_ERR(this);
	if (IS_ERR(this)) {
		goto fail;
	} else if (!this || !this->d_inode) {
		dput(this);
		err = -ENOENT;
		goto fail;
	} else if (ovl_dentry_real_at(this, layer->idx) != real) {
		dput(this);
		err = -ESTALE;
		goto fail;
	}

out:
	release_dentry_name_snapshot(&name);
	dput(parent);
	inode_unlock(dir);
	return this;

fail:
	pr_warn_ratelimited("overlayfs: failed to lookup one by real (%pd2, layer=%d, connected=%pd2, err=%i)\n",
			    real, layer->idx, connected, err);
	this = ERR_PTR(err);
	goto out;
}

static struct dentry *ovl_lookup_real(struct super_block *sb,
				      struct dentry *real,
				      struct ovl_layer *layer);

/*
 * Lookup an indexed or hashed overlay dentry by real inode.
 */
static struct dentry *ovl_lookup_real_inode(struct super_block *sb,
					    struct dentry *real,
					    struct ovl_layer *layer)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct ovl_layer upper_layer = { .mnt = ofs->upper_mnt };
	struct dentry *index = NULL;
	struct dentry *this = NULL;
	struct inode *inode;

	/*
	 * Decoding upper dir from index is expensive, so first try to lookup
	 * overlay dentry in inode/dcache.
	 */
	inode = ovl_lookup_inode(sb, real, !layer->idx);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (inode) {
		this = d_find_any_alias(inode);
		iput(inode);
	}

	/*
	 * For decoded lower dir file handle, lookup index by origin to check
	 * if lower dir was copied up and and/or removed.
	 */
	if (!this && layer->idx && ofs->indexdir && !WARN_ON(!d_is_dir(real))) {
		index = ovl_lookup_index(ofs, NULL, real, false);
		if (IS_ERR(index))
			return index;
	}

	/* Get connected upper overlay dir from index */
	if (index) {
		struct dentry *upper = ovl_index_upper(ofs, index);

		dput(index);
		if (IS_ERR_OR_NULL(upper))
			return upper;

		/*
		 * ovl_lookup_real() in lower layer may call recursively once to
		 * ovl_lookup_real() in upper layer. The first level call walks
		 * back lower parents to the topmost indexed parent. The second
		 * recursive call walks back from indexed upper to the topmost
		 * connected/hashed upper parent (or up to root).
		 */
		this = ovl_lookup_real(sb, upper, &upper_layer);
		dput(upper);
	}

	if (!this)
		return NULL;

	if (WARN_ON(ovl_dentry_real_at(this, layer->idx) != real)) {
		dput(this);
		this = ERR_PTR(-EIO);
	}

	return this;
}

/*
 * Lookup an indexed or hashed overlay dentry, whose real dentry is an
 * ancestor of @real.
 */
static struct dentry *ovl_lookup_real_ancestor(struct super_block *sb,
					       struct dentry *real,
					       struct ovl_layer *layer)
{
	struct dentry *next, *parent = NULL;
	struct dentry *ancestor = ERR_PTR(-EIO);

	if (real == layer->mnt->mnt_root)
		return dget(sb->s_root);

	/* Find the topmost indexed or hashed ancestor */
	next = dget(real);
	for (;;) {
		parent = dget_parent(next);

		/*
		 * Lookup a matching overlay dentry in inode/dentry
		 * cache or in index by real inode.
		 */
		ancestor = ovl_lookup_real_inode(sb, next, layer);
		if (ancestor)
			break;

		if (parent == layer->mnt->mnt_root) {
			ancestor = dget(sb->s_root);
			break;
		}

		/*
		 * If @real has been moved out of the layer root directory,
		 * we will eventully hit the real fs root. This cannot happen
		 * by legit overlay rename, so we return error in that case.
		 */
		if (parent == next) {
			ancestor = ERR_PTR(-EXDEV);
			break;
		}

		dput(next);
		next = parent;
	}

	dput(parent);
	dput(next);

	return ancestor;
}

/*
 * Lookup a connected overlay dentry whose real dentry is @real.
 * If @real is on upper layer, we lookup a child overlay dentry with the same
 * path the real dentry. Otherwise, we need to consult index for lookup.
 */
static struct dentry *ovl_lookup_real(struct super_block *sb,
				      struct dentry *real,
				      struct ovl_layer *layer)
{
	struct dentry *connected;
	int err = 0;

	connected = ovl_lookup_real_ancestor(sb, real, layer);
	if (IS_ERR(connected))
		return connected;

	while (!err) {
		struct dentry *next, *this;
		struct dentry *parent = NULL;
		struct dentry *real_connected = ovl_dentry_real_at(connected,
								   layer->idx);

		if (real_connected == real)
			break;

		/* Find the topmost dentry not yet connected */
		next = dget(real);
		for (;;) {
			parent = dget_parent(next);

			if (parent == real_connected)
				break;

			/*
			 * If real has been moved out of 'real_connected',
			 * we will not find 'real_connected' and hit the layer
			 * root. In that case, we need to restart connecting.
			 * This game can go on forever in the worst case. We
			 * may want to consider taking s_vfs_rename_mutex if
			 * this happens more than once.
			 */
			if (parent == layer->mnt->mnt_root) {
				dput(connected);
				connected = dget(sb->s_root);
				break;
			}

			/*
			 * If real file has been moved out of the layer root
			 * directory, we will eventully hit the real fs root.
			 * This cannot happen by legit overlay rename, so we
			 * return error in that case.
			 */
			if (parent == next) {
				err = -EXDEV;
				break;
			}

			dput(next);
			next = parent;
		}

		if (!err) {
			this = ovl_lookup_real_one(connected, next, layer);
			if (IS_ERR(this))
				err = PTR_ERR(this);

			/*
			 * Lookup of child in overlay can fail when racing with
			 * overlay rename of child away from 'connected' parent.
			 * In this case, we need to restart the lookup from the
			 * top, because we cannot trust that 'real_connected' is
			 * still an ancestor of 'real'. There is a good chance
			 * that the renamed overlay ancestor is now in cache, so
			 * ovl_lookup_real_ancestor() will find it and we can
			 * continue to connect exactly from where lookup failed.
			 */
			if (err == -ECHILD) {
				this = ovl_lookup_real_ancestor(sb, real,
								layer);
				err = IS_ERR(this) ? PTR_ERR(this) : 0;
			}
			if (!err) {
				dput(connected);
				connected = this;
			}
		}

		dput(parent);
		dput(next);
	}

	if (err)
		goto fail;

	return connected;

fail:
	pr_warn_ratelimited("overlayfs: failed to lookup by real (%pd2, layer=%d, connected=%pd2, err=%i)\n",
			    real, layer->idx, connected, err);
	dput(connected);
	return ERR_PTR(err);
}

/*
 * Get an overlay dentry from upper/lower real dentries and index.
 */
static struct dentry *ovl_get_dentry(struct super_block *sb,
				     struct dentry *upper,
				     struct ovl_path *lowerpath,
				     struct dentry *index)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct ovl_layer upper_layer = { .mnt = ofs->upper_mnt };
	struct ovl_layer *layer = upper ? &upper_layer : lowerpath->layer;
	struct dentry *real = upper ?: (index ?: lowerpath->dentry);

	/*
	 * Obtain a disconnected overlay dentry from a non-dir real dentry
	 * and index.
	 */
	if (!d_is_dir(real))
		return ovl_obtain_alias(sb, upper, lowerpath, index);

	/* Removed empty directory? */
	if ((real->d_flags & DCACHE_DISCONNECTED) || d_unhashed(real))
		return ERR_PTR(-ENOENT);

	/*
	 * If real dentry is connected and hashed, get a connected overlay
	 * dentry whose real dentry is @real.
	 */
	return ovl_lookup_real(sb, real, layer);
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

	dentry = ovl_get_dentry(sb, upper, NULL, NULL);
	dput(upper);

	return dentry;
}

static struct dentry *ovl_lower_fh_to_d(struct super_block *sb,
					struct ovl_fh *fh)
{
	struct ovl_fs *ofs = sb->s_fs_info;
	struct ovl_path origin = { };
	struct ovl_path *stack = &origin;
	struct dentry *dentry = NULL;
	struct dentry *index = NULL;
	struct inode *inode = NULL;
	bool is_deleted = false;
	int err;

	/* First lookup indexed upper by fh */
	if (ofs->indexdir) {
		index = ovl_get_index_fh(ofs, fh);
		err = PTR_ERR(index);
		if (IS_ERR(index)) {
			if (err != -ESTALE)
				return ERR_PTR(err);

			/* Found a whiteout index - treat as deleted inode */
			is_deleted = true;
			index = NULL;
		}
	}

	/* Then try to get upper dir by index */
	if (index && d_is_dir(index)) {
		struct dentry *upper = ovl_index_upper(ofs, index);

		err = PTR_ERR(upper);
		if (IS_ERR_OR_NULL(upper))
			goto out_err;

		dentry = ovl_get_dentry(sb, upper, NULL, NULL);
		dput(upper);
		goto out;
	}

	/* Then lookup origin by fh */
	err = ovl_check_origin_fh(ofs, fh, NULL, &stack);
	if (err) {
		goto out_err;
	} else if (index) {
		err = ovl_verify_origin(index, origin.dentry, false);
		if (err)
			goto out_err;
	} else if (is_deleted) {
		/* Lookup deleted non-dir by origin inode */
		if (!d_is_dir(origin.dentry))
			inode = ovl_lookup_inode(sb, origin.dentry, false);
		err = -ESTALE;
		if (!inode || atomic_read(&inode->i_count) == 1)
			goto out_err;

		/* Deleted but still open? */
		index = dget(ovl_i_dentry_upper(inode));
	}

	dentry = ovl_get_dentry(sb, NULL, &origin, index);

out:
	dput(origin.dentry);
	dput(index);
	iput(inode);
	return dentry;

out_err:
	dentry = ERR_PTR(err);
	goto out;
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

	flags = fh->flags;
	dentry = (flags & OVL_FH_FLAG_PATH_UPPER) ?
		 ovl_upper_fh_to_d(sb, fh) :
		 ovl_lower_fh_to_d(sb, fh);
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry) && err != -ESTALE)
		goto out_err;

	return dentry;

out_err:
	pr_warn_ratelimited("overlayfs: failed to decode file handle (len=%d, type=%d, flags=%x, err=%i)\n",
			    len, fh_type, flags, err);
	return ERR_PTR(err);
}

static struct dentry *ovl_fh_to_parent(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	pr_warn_ratelimited("overlayfs: connectable file handles not supported; use 'no_subtree_check' exportfs option.\n");
	return ERR_PTR(-EACCES);
}

static int ovl_get_name(struct dentry *parent, char *name,
			struct dentry *child)
{
	/*
	 * ovl_fh_to_dentry() returns connected dir overlay dentries and
	 * ovl_fh_to_parent() is not implemented, so we should not get here.
	 */
	WARN_ON_ONCE(1);
	return -EIO;
}

static struct dentry *ovl_get_parent(struct dentry *dentry)
{
	/*
	 * ovl_fh_to_dentry() returns connected dir overlay dentries, so we
	 * should not get here.
	 */
	WARN_ON_ONCE(1);
	return ERR_PTR(-EIO);
}

const struct export_operations ovl_export_operations = {
	.encode_fh	= ovl_encode_inode_fh,
	.fh_to_dentry	= ovl_fh_to_dentry,
	.fh_to_parent	= ovl_fh_to_parent,
	.get_name	= ovl_get_name,
	.get_parent	= ovl_get_parent,
};
