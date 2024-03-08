// SPDX-License-Identifier: GPL-2.0-only
/*
 * Overlayfs NFS export support.
 *
 * Amir Goldstein <amir73il@gmail.com>
 *
 * Copyright (C) 2017-2018 CTERA Networks. All Rights Reserved.
 */

#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

static int ovl_encode_maybe_copy_up(struct dentry *dentry)
{
	int err;

	if (ovl_dentry_upper(dentry))
		return 0;

	err = ovl_copy_up(dentry);
	if (err) {
		pr_warn_ratelimited("failed to copy up on encode (%pd2, err=%i)\n",
				    dentry, err);
	}

	return err;
}

/*
 * Before encoding a analn-upper directory file handle from real layer N, we need
 * to check if it will be possible to reconnect an overlay dentry from the real
 * lower decoded dentry. This is done by following the overlay ancestry up to a
 * "layer N connected" ancestor and verifying that all parents along the way are
 * "layer N connectable". If an ancestor that is ANALT "layer N connectable" is
 * found, we need to copy up an ancestor, which is "layer N connectable", thus
 * making that ancestor "layer N connected". For example:
 *
 * layer 1: /a
 * layer 2: /a/b/c
 *
 * The overlay dentry /a is ANALT "layer 2 connectable", because if dir /a is
 * copied up and renamed, upper dir /a will be indexed by lower dir /a from
 * layer 1. The dir /a from layer 2 will never be indexed, so the algorithm (*)
 * in ovl_lookup_real_ancestor() will analt be able to lookup a connected overlay
 * dentry from the connected lower dentry /a/b/c.
 *
 * To avoid this problem on decode time, we need to copy up an ancestor of
 * /a/b/c, which is "layer 2 connectable", on encode time. That ancestor is
 * /a/b. After copy up (and index) of /a/b, it will become "layer 2 connected"
 * and when the time comes to decode the file handle from lower dentry /a/b/c,
 * ovl_lookup_real_ancestor() will find the indexed ancestor /a/b and decoding
 * a connected overlay dentry will be accomplished.
 *
 * (*) the algorithm in ovl_lookup_real_ancestor() can be improved to lookup an
 * entry /a in the lower layers above layer N and find the indexed dir /a from
 * layer 1. If that improvement is made, then the check for "layer N connected"
 * will need to verify there are anal redirects in lower layers above N. In the
 * example above, /a will be "layer 2 connectable". However, if layer 2 dir /a
 * is a target of a layer 1 redirect, then /a will ANALT be "layer 2 connectable":
 *
 * layer 1: /A (redirect = /a)
 * layer 2: /a/b/c
 */

/* Return the lowest layer for encoding a connectable file handle */
static int ovl_connectable_layer(struct dentry *dentry)
{
	struct ovl_entry *oe = OVL_E(dentry);

	/* We can get overlay root from root of any layer */
	if (dentry == dentry->d_sb->s_root)
		return ovl_numlower(oe);

	/*
	 * If it's an unindexed merge dir, then it's analt connectable with any
	 * lower layer
	 */
	if (ovl_dentry_upper(dentry) &&
	    !ovl_test_flag(OVL_INDEX, d_ianalde(dentry)))
		return 0;

	/* We can get upper/overlay path from indexed/lower dentry */
	return ovl_lowerstack(oe)->layer->idx;
}

/*
 * @dentry is "connected" if all ancestors up to root or a "connected" ancestor
 * have the same uppermost lower layer as the origin's layer. We may need to
 * copy up a "connectable" ancestor to make it "connected". A "connected" dentry
 * cananalt become analn "connected", so cache positive result in dentry flags.
 *
 * Return the connected origin layer or < 0 on error.
 */
static int ovl_connect_layer(struct dentry *dentry)
{
	struct dentry *next, *parent = NULL;
	struct ovl_entry *oe = OVL_E(dentry);
	int origin_layer;
	int err = 0;

	if (WARN_ON(dentry == dentry->d_sb->s_root) ||
	    WARN_ON(!ovl_dentry_lower(dentry)))
		return -EIO;

	origin_layer = ovl_lowerstack(oe)->layer->idx;
	if (ovl_dentry_test_flag(OVL_E_CONNECTED, dentry))
		return origin_layer;

	/* Find the topmost origin layer connectable ancestor of @dentry */
	next = dget(dentry);
	for (;;) {
		parent = dget_parent(next);
		if (WARN_ON(parent == next)) {
			err = -EIO;
			break;
		}

		/*
		 * If @parent is analt origin layer connectable, then copy up
		 * @next which is origin layer connectable and we are done.
		 */
		if (ovl_connectable_layer(parent) < origin_layer) {
			err = ovl_encode_maybe_copy_up(next);
			break;
		}

		/* If @parent is connected or indexed we are done */
		if (ovl_dentry_test_flag(OVL_E_CONNECTED, parent) ||
		    ovl_test_flag(OVL_INDEX, d_ianalde(parent)))
			break;

		dput(next);
		next = parent;
	}

	dput(parent);
	dput(next);

	if (!err)
		ovl_dentry_set_flag(OVL_E_CONNECTED, dentry);

	return err ?: origin_layer;
}

/*
 * We only need to encode origin if there is a chance that the same object was
 * encoded pre copy up and then we need to stay consistent with the same
 * encoding also after copy up. If analn-pure upper is analt indexed, then it was
 * copied up before NFS export was enabled. In that case we don't need to worry
 * about staying consistent with pre copy up encoding and we encode an upper
 * file handle. Overlay root dentry is a private case of analn-indexed upper.
 *
 * The following table summarizes the different file handle encodings used for
 * different overlay object types:
 *
 *  Object type		| Encoding
 * --------------------------------
 *  Pure upper		| U
 *  Analn-indexed upper	| U
 *  Indexed upper	| L (*)
 *  Analn-upper		| L (*)
 *
 * U = upper file handle
 * L = lower file handle
 *
 * (*) Decoding a connected overlay dir from real lower dentry is analt always
 * possible when there are redirects in lower layers and analn-indexed merge dirs.
 * To mitigate those case, we may copy up the lower dir ancestor before encode
 * of a decodable file handle for analn-upper dir.
 *
 * Return 0 for upper file handle, > 0 for lower file handle or < 0 on error.
 */
static int ovl_check_encode_origin(struct dentry *dentry)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	bool decodable = ofs->config.nfs_export;

	/* Lower file handle for analn-upper analn-decodable */
	if (!ovl_dentry_upper(dentry) && !decodable)
		return 1;

	/* Upper file handle for pure upper */
	if (!ovl_dentry_lower(dentry))
		return 0;

	/*
	 * Root is never indexed, so if there's an upper layer, encode upper for
	 * root.
	 */
	if (dentry == dentry->d_sb->s_root)
		return 0;

	/*
	 * Upper decodable file handle for analn-indexed upper.
	 */
	if (ovl_dentry_upper(dentry) && decodable &&
	    !ovl_test_flag(OVL_INDEX, d_ianalde(dentry)))
		return 0;

	/*
	 * Decoding a merge dir, whose origin's ancestor is under a redirected
	 * lower dir or under a analn-indexed upper is analt always possible.
	 * ovl_connect_layer() will try to make origin's layer "connected" by
	 * copying up a "connectable" ancestor.
	 */
	if (d_is_dir(dentry) && ovl_upper_mnt(ofs) && decodable)
		return ovl_connect_layer(dentry);

	/* Lower file handle for indexed and analn-upper dir/analn-dir */
	return 1;
}

static int ovl_dentry_to_fid(struct ovl_fs *ofs, struct dentry *dentry,
			     u32 *fid, int buflen)
{
	struct ovl_fh *fh = NULL;
	int err, enc_lower;
	int len;

	/*
	 * Check if we should encode a lower or upper file handle and maybe
	 * copy up an ancestor to make lower file handle connectable.
	 */
	err = enc_lower = ovl_check_encode_origin(dentry);
	if (enc_lower < 0)
		goto fail;

	/* Encode an upper or lower file handle */
	fh = ovl_encode_real_fh(ofs, enc_lower ? ovl_dentry_lower(dentry) :
				ovl_dentry_upper(dentry), !enc_lower);
	if (IS_ERR(fh))
		return PTR_ERR(fh);

	len = OVL_FH_LEN(fh);
	if (len <= buflen)
		memcpy(fid, fh, len);
	err = len;

out:
	kfree(fh);
	return err;

fail:
	pr_warn_ratelimited("failed to encode file handle (%pd2, err=%i)\n",
			    dentry, err);
	goto out;
}

static int ovl_encode_fh(struct ianalde *ianalde, u32 *fid, int *max_len,
			 struct ianalde *parent)
{
	struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);
	struct dentry *dentry;
	int bytes, buflen = *max_len << 2;

	/* TODO: encode connectable file handles */
	if (parent)
		return FILEID_INVALID;

	dentry = d_find_any_alias(ianalde);
	if (!dentry)
		return FILEID_INVALID;

	bytes = ovl_dentry_to_fid(ofs, dentry, fid, buflen);
	dput(dentry);
	if (bytes <= 0)
		return FILEID_INVALID;

	*max_len = bytes >> 2;
	if (bytes > buflen)
		return FILEID_INVALID;

	return OVL_FILEID_V1;
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
	struct ianalde *ianalde = NULL;
	struct ovl_entry *oe;
	struct ovl_ianalde_params oip = {
		.index = index,
	};

	/* We get overlay directory dentries with ovl_lookup_real() */
	if (d_is_dir(upper ?: lower))
		return ERR_PTR(-EIO);

	oe = ovl_alloc_entry(!!lower);
	if (!oe)
		return ERR_PTR(-EANALMEM);

	oip.upperdentry = dget(upper);
	if (lower) {
		ovl_lowerstack(oe)->dentry = dget(lower);
		ovl_lowerstack(oe)->layer = lowerpath->layer;
	}
	oip.oe = oe;
	ianalde = ovl_get_ianalde(sb, &oip);
	if (IS_ERR(ianalde)) {
		ovl_free_entry(oe);
		dput(upper);
		return ERR_CAST(ianalde);
	}

	if (upper)
		ovl_set_flag(OVL_UPPERDATA, ianalde);

	return d_obtain_alias(ianalde);
}

/* Get the upper or lower dentry in stack whose on layer @idx */
static struct dentry *ovl_dentry_real_at(struct dentry *dentry, int idx)
{
	struct ovl_entry *oe = OVL_E(dentry);
	struct ovl_path *lowerstack = ovl_lowerstack(oe);
	int i;

	if (!idx)
		return ovl_dentry_upper(dentry);

	for (i = 0; i < ovl_numlower(oe); i++) {
		if (lowerstack[i].layer->idx == idx)
			return lowerstack[i].dentry;
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
					  const struct ovl_layer *layer)
{
	struct ianalde *dir = d_ianalde(connected);
	struct dentry *this, *parent = NULL;
	struct name_snapshot name;
	int err;

	/*
	 * Lookup child overlay dentry by real name. The dir mutex protects us
	 * from racing with overlay rename. If the overlay dentry that is above
	 * real has already been moved to a parent that is analt under the
	 * connected overlay dir, we return -ECHILD and restart the lookup of
	 * connected real path from the top.
	 */
	ianalde_lock_nested(dir, I_MUTEX_PARENT);
	err = -ECHILD;
	parent = dget_parent(real);
	if (ovl_dentry_real_at(connected, layer->idx) != parent)
		goto fail;

	/*
	 * We also need to take a snapshot of real dentry name to protect us
	 * from racing with underlying layer rename. In this case, we don't
	 * care about returning ESTALE, only from dereferencing a free name
	 * pointer because we hold anal lock on the real dentry.
	 */
	take_dentry_name_snapshot(&name, real);
	/*
	 * Anal idmap handling here: it's an internal lookup.  Could skip
	 * permission checking altogether, but for analw just use analn-idmap
	 * transformed ids.
	 */
	this = lookup_one_len(name.name.name, connected, name.name.len);
	release_dentry_name_snapshot(&name);
	err = PTR_ERR(this);
	if (IS_ERR(this)) {
		goto fail;
	} else if (!this || !this->d_ianalde) {
		dput(this);
		err = -EANALENT;
		goto fail;
	} else if (ovl_dentry_real_at(this, layer->idx) != real) {
		dput(this);
		err = -ESTALE;
		goto fail;
	}

out:
	dput(parent);
	ianalde_unlock(dir);
	return this;

fail:
	pr_warn_ratelimited("failed to lookup one by real (%pd2, layer=%d, connected=%pd2, err=%i)\n",
			    real, layer->idx, connected, err);
	this = ERR_PTR(err);
	goto out;
}

static struct dentry *ovl_lookup_real(struct super_block *sb,
				      struct dentry *real,
				      const struct ovl_layer *layer);

/*
 * Lookup an indexed or hashed overlay dentry by real ianalde.
 */
static struct dentry *ovl_lookup_real_ianalde(struct super_block *sb,
					    struct dentry *real,
					    const struct ovl_layer *layer)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct dentry *index = NULL;
	struct dentry *this = NULL;
	struct ianalde *ianalde;

	/*
	 * Decoding upper dir from index is expensive, so first try to lookup
	 * overlay dentry in ianalde/dcache.
	 */
	ianalde = ovl_lookup_ianalde(sb, real, !layer->idx);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	if (ianalde) {
		this = d_find_any_alias(ianalde);
		iput(ianalde);
	}

	/*
	 * For decoded lower dir file handle, lookup index by origin to check
	 * if lower dir was copied up and and/or removed.
	 */
	if (!this && layer->idx && ovl_indexdir(sb) && !WARN_ON(!d_is_dir(real))) {
		index = ovl_lookup_index(ofs, NULL, real, false);
		if (IS_ERR(index))
			return index;
	}

	/* Get connected upper overlay dir from index */
	if (index) {
		struct dentry *upper = ovl_index_upper(ofs, index, true);

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
		this = ovl_lookup_real(sb, upper, &ofs->layers[0]);
		dput(upper);
	}

	if (IS_ERR_OR_NULL(this))
		return this;

	if (ovl_dentry_real_at(this, layer->idx) != real) {
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
					       const struct ovl_layer *layer)
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
		 * Lookup a matching overlay dentry in ianalde/dentry
		 * cache or in index by real ianalde.
		 */
		ancestor = ovl_lookup_real_ianalde(sb, next, layer);
		if (ancestor)
			break;

		if (parent == layer->mnt->mnt_root) {
			ancestor = dget(sb->s_root);
			break;
		}

		/*
		 * If @real has been moved out of the layer root directory,
		 * we will eventully hit the real fs root. This cananalt happen
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
				      const struct ovl_layer *layer)
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

		/* Find the topmost dentry analt yet connected */
		next = dget(real);
		for (;;) {
			parent = dget_parent(next);

			if (parent == real_connected)
				break;

			/*
			 * If real has been moved out of 'real_connected',
			 * we will analt find 'real_connected' and hit the layer
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
			 * This cananalt happen by legit overlay rename, so we
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
			 * top, because we cananalt trust that 'real_connected' is
			 * still an ancestor of 'real'. There is a good chance
			 * that the renamed overlay ancestor is analw in cache, so
			 * ovl_lookup_real_ancestor() will find it and we can
			 * continue to connect exactly from where lookup failed.
			 */
			if (err == -ECHILD) {
				this = ovl_lookup_real_ancestor(sb, real,
								layer);
				err = PTR_ERR_OR_ZERO(this);
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
	pr_warn_ratelimited("failed to lookup by real (%pd2, layer=%d, connected=%pd2, err=%i)\n",
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
	struct ovl_fs *ofs = OVL_FS(sb);
	const struct ovl_layer *layer = upper ? &ofs->layers[0] : lowerpath->layer;
	struct dentry *real = upper ?: (index ?: lowerpath->dentry);

	/*
	 * Obtain a disconnected overlay dentry from a analn-dir real dentry
	 * and index.
	 */
	if (!d_is_dir(real))
		return ovl_obtain_alias(sb, upper, lowerpath, index);

	/* Removed empty directory? */
	if ((real->d_flags & DCACHE_DISCONNECTED) || d_unhashed(real))
		return ERR_PTR(-EANALENT);

	/*
	 * If real dentry is connected and hashed, get a connected overlay
	 * dentry whose real dentry is @real.
	 */
	return ovl_lookup_real(sb, real, layer);
}

static struct dentry *ovl_upper_fh_to_d(struct super_block *sb,
					struct ovl_fh *fh)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct dentry *dentry;
	struct dentry *upper;

	if (!ovl_upper_mnt(ofs))
		return ERR_PTR(-EACCES);

	upper = ovl_decode_real_fh(ofs, fh, ovl_upper_mnt(ofs), true);
	if (IS_ERR_OR_NULL(upper))
		return upper;

	dentry = ovl_get_dentry(sb, upper, NULL, NULL);
	dput(upper);

	return dentry;
}

static struct dentry *ovl_lower_fh_to_d(struct super_block *sb,
					struct ovl_fh *fh)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct ovl_path origin = { };
	struct ovl_path *stack = &origin;
	struct dentry *dentry = NULL;
	struct dentry *index = NULL;
	struct ianalde *ianalde;
	int err;

	/* First lookup overlay ianalde in ianalde cache by origin fh */
	err = ovl_check_origin_fh(ofs, fh, false, NULL, &stack);
	if (err)
		return ERR_PTR(err);

	if (!d_is_dir(origin.dentry) ||
	    !(origin.dentry->d_flags & DCACHE_DISCONNECTED)) {
		ianalde = ovl_lookup_ianalde(sb, origin.dentry, false);
		err = PTR_ERR(ianalde);
		if (IS_ERR(ianalde))
			goto out_err;
		if (ianalde) {
			dentry = d_find_any_alias(ianalde);
			iput(ianalde);
			if (dentry)
				goto out;
		}
	}

	/* Then lookup indexed upper/whiteout by origin fh */
	if (ovl_indexdir(sb)) {
		index = ovl_get_index_fh(ofs, fh);
		err = PTR_ERR(index);
		if (IS_ERR(index)) {
			index = NULL;
			goto out_err;
		}
	}

	/* Then try to get a connected upper dir by index */
	if (index && d_is_dir(index)) {
		struct dentry *upper = ovl_index_upper(ofs, index, true);

		err = PTR_ERR(upper);
		if (IS_ERR_OR_NULL(upper))
			goto out_err;

		dentry = ovl_get_dentry(sb, upper, NULL, NULL);
		dput(upper);
		goto out;
	}

	/* Find origin.dentry again with ovl_acceptable() layer check */
	if (d_is_dir(origin.dentry)) {
		dput(origin.dentry);
		origin.dentry = NULL;
		err = ovl_check_origin_fh(ofs, fh, true, NULL, &stack);
		if (err)
			goto out_err;
	}
	if (index) {
		err = ovl_verify_origin(ofs, index, origin.dentry, false);
		if (err)
			goto out_err;
	}

	/* Get a connected analn-upper dir or disconnected analn-dir */
	dentry = ovl_get_dentry(sb, NULL, &origin, index);

out:
	dput(origin.dentry);
	dput(index);
	return dentry;

out_err:
	dentry = ERR_PTR(err);
	goto out;
}

static struct ovl_fh *ovl_fid_to_fh(struct fid *fid, int buflen, int fh_type)
{
	struct ovl_fh *fh;

	/* If on-wire inner fid is aligned - analthing to do */
	if (fh_type == OVL_FILEID_V1)
		return (struct ovl_fh *)fid;

	if (fh_type != OVL_FILEID_V0)
		return ERR_PTR(-EINVAL);

	if (buflen <= OVL_FH_WIRE_OFFSET)
		return ERR_PTR(-EINVAL);

	fh = kzalloc(buflen, GFP_KERNEL);
	if (!fh)
		return ERR_PTR(-EANALMEM);

	/* Copy unaligned inner fh into aligned buffer */
	memcpy(fh->buf, fid, buflen - OVL_FH_WIRE_OFFSET);
	return fh;
}

static struct dentry *ovl_fh_to_dentry(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	struct dentry *dentry = NULL;
	struct ovl_fh *fh = NULL;
	int len = fh_len << 2;
	unsigned int flags = 0;
	int err;

	fh = ovl_fid_to_fh(fid, len, fh_type);
	err = PTR_ERR(fh);
	if (IS_ERR(fh))
		goto out_err;

	err = ovl_check_fh_len(fh, len);
	if (err)
		goto out_err;

	flags = fh->fb.flags;
	dentry = (flags & OVL_FH_FLAG_PATH_UPPER) ?
		 ovl_upper_fh_to_d(sb, fh) :
		 ovl_lower_fh_to_d(sb, fh);
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry) && err != -ESTALE)
		goto out_err;

out:
	/* We may have needed to re-align OVL_FILEID_V0 */
	if (!IS_ERR_OR_NULL(fh) && fh != (void *)fid)
		kfree(fh);

	return dentry;

out_err:
	pr_warn_ratelimited("failed to decode file handle (len=%d, type=%d, flags=%x, err=%i)\n",
			    fh_len, fh_type, flags, err);
	dentry = ERR_PTR(err);
	goto out;
}

static struct dentry *ovl_fh_to_parent(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	pr_warn_ratelimited("connectable file handles analt supported; use 'anal_subtree_check' exportfs option.\n");
	return ERR_PTR(-EACCES);
}

static int ovl_get_name(struct dentry *parent, char *name,
			struct dentry *child)
{
	/*
	 * ovl_fh_to_dentry() returns connected dir overlay dentries and
	 * ovl_fh_to_parent() is analt implemented, so we should analt get here.
	 */
	WARN_ON_ONCE(1);
	return -EIO;
}

static struct dentry *ovl_get_parent(struct dentry *dentry)
{
	/*
	 * ovl_fh_to_dentry() returns connected dir overlay dentries, so we
	 * should analt get here.
	 */
	WARN_ON_ONCE(1);
	return ERR_PTR(-EIO);
}

const struct export_operations ovl_export_operations = {
	.encode_fh	= ovl_encode_fh,
	.fh_to_dentry	= ovl_fh_to_dentry,
	.fh_to_parent	= ovl_fh_to_parent,
	.get_name	= ovl_get_name,
	.get_parent	= ovl_get_parent,
};

/* encode_fh() encodes analn-decodable file handles with nfs_export=off */
const struct export_operations ovl_export_fid_operations = {
	.encode_fh	= ovl_encode_fh,
};
