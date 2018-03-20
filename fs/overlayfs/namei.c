/*
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/ratelimit.h>
#include <linux/mount.h>
#include <linux/exportfs.h>
#include "overlayfs.h"

struct ovl_lookup_data {
	struct qstr name;
	bool is_dir;
	bool opaque;
	bool stop;
	bool last;
	char *redirect;
};

static int ovl_check_redirect(struct dentry *dentry, struct ovl_lookup_data *d,
			      size_t prelen, const char *post)
{
	int res;
	char *s, *next, *buf = NULL;

	res = vfs_getxattr(dentry, OVL_XATTR_REDIRECT, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return 0;
		goto fail;
	}
	buf = kzalloc(prelen + res + strlen(post) + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (res == 0)
		goto invalid;

	res = vfs_getxattr(dentry, OVL_XATTR_REDIRECT, buf, res);
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
		/*
		 * One of the ancestor path elements in an absolute path
		 * lookup in ovl_lookup_layer() could have been opaque and
		 * that will stop further lookup in lower layers (d->stop=true)
		 * But we have found an absolute redirect in decendant path
		 * element and that should force continue lookup in lower
		 * layers (reset d->stop).
		 */
		d->stop = false;
	} else {
		if (strchr(buf, '/') != NULL)
			goto invalid;

		memmove(buf + prelen, buf, res);
		memcpy(buf, d->name.name, prelen);
	}

	strcat(buf, post);
	kfree(d->redirect);
	d->redirect = buf;
	d->name.name = d->redirect;
	d->name.len = strlen(d->redirect);

	return 0;

err_free:
	kfree(buf);
	return 0;
fail:
	pr_warn_ratelimited("overlayfs: failed to get redirect (%i)\n", res);
	goto err_free;
invalid:
	pr_warn_ratelimited("overlayfs: invalid redirect (%s)\n", buf);
	goto err_free;
}

static int ovl_acceptable(void *ctx, struct dentry *dentry)
{
	/*
	 * A non-dir origin may be disconnected, which is fine, because
	 * we only need it for its unique inode number.
	 */
	if (!d_is_dir(dentry))
		return 1;

	/* Don't decode a deleted empty directory */
	if (d_unhashed(dentry))
		return 0;

	/* Check if directory belongs to the layer we are decoding from */
	return is_subdir(dentry, ((struct vfsmount *)ctx)->mnt_root);
}

/*
 * Check validity of an overlay file handle buffer.
 *
 * Return 0 for a valid file handle.
 * Return -ENODATA for "origin unknown".
 * Return <0 for an invalid file handle.
 */
int ovl_check_fh_len(struct ovl_fh *fh, int fh_len)
{
	if (fh_len < sizeof(struct ovl_fh) || fh_len < fh->len)
		return -EINVAL;

	if (fh->magic != OVL_FH_MAGIC)
		return -EINVAL;

	/* Treat larger version and unknown flags as "origin unknown" */
	if (fh->version > OVL_FH_VERSION || fh->flags & ~OVL_FH_FLAG_ALL)
		return -ENODATA;

	/* Treat endianness mismatch as "origin unknown" */
	if (!(fh->flags & OVL_FH_FLAG_ANY_ENDIAN) &&
	    (fh->flags & OVL_FH_FLAG_BIG_ENDIAN) != OVL_FH_FLAG_CPU_ENDIAN)
		return -ENODATA;

	return 0;
}

static struct ovl_fh *ovl_get_fh(struct dentry *dentry, const char *name)
{
	int res, err;
	struct ovl_fh *fh = NULL;

	res = vfs_getxattr(dentry, name, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return NULL;
		goto fail;
	}
	/* Zero size value means "copied up but origin unknown" */
	if (res == 0)
		return NULL;

	fh = kzalloc(res, GFP_KERNEL);
	if (!fh)
		return ERR_PTR(-ENOMEM);

	res = vfs_getxattr(dentry, name, fh, res);
	if (res < 0)
		goto fail;

	err = ovl_check_fh_len(fh, res);
	if (err < 0) {
		if (err == -ENODATA)
			goto out;
		goto invalid;
	}

	return fh;

out:
	kfree(fh);
	return NULL;

fail:
	pr_warn_ratelimited("overlayfs: failed to get origin (%i)\n", res);
	goto out;
invalid:
	pr_warn_ratelimited("overlayfs: invalid origin (%*phN)\n", res, fh);
	goto out;
}

struct dentry *ovl_decode_real_fh(struct ovl_fh *fh, struct vfsmount *mnt,
				  bool connected)
{
	struct dentry *real;
	int bytes;

	/*
	 * Make sure that the stored uuid matches the uuid of the lower
	 * layer where file handle will be decoded.
	 */
	if (!uuid_equal(&fh->uuid, &mnt->mnt_sb->s_uuid))
		return NULL;

	bytes = (fh->len - offsetof(struct ovl_fh, fid));
	real = exportfs_decode_fh(mnt, (struct fid *)fh->fid,
				  bytes >> 2, (int)fh->type,
				  connected ? ovl_acceptable : NULL, mnt);
	if (IS_ERR(real)) {
		/*
		 * Treat stale file handle to lower file as "origin unknown".
		 * upper file handle could become stale when upper file is
		 * unlinked and this information is needed to handle stale
		 * index entries correctly.
		 */
		if (real == ERR_PTR(-ESTALE) &&
		    !(fh->flags & OVL_FH_FLAG_PATH_UPPER))
			real = NULL;
		return real;
	}

	if (ovl_dentry_weird(real)) {
		dput(real);
		return NULL;
	}

	return real;
}

static bool ovl_is_opaquedir(struct dentry *dentry)
{
	return ovl_check_dir_xattr(dentry, OVL_XATTR_OPAQUE);
}

static int ovl_lookup_single(struct dentry *base, struct ovl_lookup_data *d,
			     const char *name, unsigned int namelen,
			     size_t prelen, const char *post,
			     struct dentry **ret)
{
	struct dentry *this;
	int err;
	bool last_element = !post[0];

	this = lookup_one_len_unlocked(name, base, namelen);
	if (IS_ERR(this)) {
		err = PTR_ERR(this);
		this = NULL;
		if (err == -ENOENT || err == -ENAMETOOLONG)
			goto out;
		goto out_err;
	}
	if (!this->d_inode)
		goto put_and_out;

	if (ovl_dentry_weird(this)) {
		/* Don't support traversing automounts and other weirdness */
		err = -EREMOTE;
		goto out_err;
	}
	if (ovl_is_whiteout(this)) {
		d->stop = d->opaque = true;
		goto put_and_out;
	}
	if (!d_can_lookup(this)) {
		d->stop = true;
		if (d->is_dir)
			goto put_and_out;
		goto out;
	}
	if (last_element)
		d->is_dir = true;
	if (d->last)
		goto out;

	if (ovl_is_opaquedir(this)) {
		d->stop = true;
		if (last_element)
			d->opaque = true;
		goto out;
	}
	err = ovl_check_redirect(this, d, prelen, post);
	if (err)
		goto out_err;
out:
	*ret = this;
	return 0;

put_and_out:
	dput(this);
	this = NULL;
	goto out;

out_err:
	dput(this);
	return err;
}

static int ovl_lookup_layer(struct dentry *base, struct ovl_lookup_data *d,
			    struct dentry **ret)
{
	/* Counting down from the end, since the prefix can change */
	size_t rem = d->name.len - 1;
	struct dentry *dentry = NULL;
	int err;

	if (d->name.name[0] != '/')
		return ovl_lookup_single(base, d, d->name.name, d->name.len,
					 0, "", ret);

	while (!IS_ERR_OR_NULL(base) && d_can_lookup(base)) {
		const char *s = d->name.name + d->name.len - rem;
		const char *next = strchrnul(s, '/');
		size_t thislen = next - s;
		bool end = !next[0];

		/* Verify we did not go off the rails */
		if (WARN_ON(s[-1] != '/'))
			return -EIO;

		err = ovl_lookup_single(base, d, s, thislen,
					d->name.len - rem, next, &base);
		dput(dentry);
		if (err)
			return err;
		dentry = base;
		if (end)
			break;

		rem -= thislen + 1;

		if (WARN_ON(rem >= d->name.len))
			return -EIO;
	}
	*ret = dentry;
	return 0;
}


int ovl_check_origin_fh(struct ovl_fs *ofs, struct ovl_fh *fh, bool connected,
			struct dentry *upperdentry, struct ovl_path **stackp)
{
	struct dentry *origin = NULL;
	int i;

	for (i = 0; i < ofs->numlower; i++) {
		origin = ovl_decode_real_fh(fh, ofs->lower_layers[i].mnt,
					    connected);
		if (origin)
			break;
	}

	if (!origin)
		return -ESTALE;
	else if (IS_ERR(origin))
		return PTR_ERR(origin);

	if (upperdentry && !ovl_is_whiteout(upperdentry) &&
	    ((d_inode(origin)->i_mode ^ d_inode(upperdentry)->i_mode) & S_IFMT))
		goto invalid;

	if (!*stackp)
		*stackp = kmalloc(sizeof(struct ovl_path), GFP_KERNEL);
	if (!*stackp) {
		dput(origin);
		return -ENOMEM;
	}
	**stackp = (struct ovl_path){
		.dentry = origin,
		.layer = &ofs->lower_layers[i]
	};

	return 0;

invalid:
	pr_warn_ratelimited("overlayfs: invalid origin (%pd2, ftype=%x, origin ftype=%x).\n",
			    upperdentry, d_inode(upperdentry)->i_mode & S_IFMT,
			    d_inode(origin)->i_mode & S_IFMT);
	dput(origin);
	return -EIO;
}

static int ovl_check_origin(struct ovl_fs *ofs, struct dentry *upperdentry,
			    struct ovl_path **stackp, unsigned int *ctrp)
{
	struct ovl_fh *fh = ovl_get_fh(upperdentry, OVL_XATTR_ORIGIN);
	int err;

	if (IS_ERR_OR_NULL(fh))
		return PTR_ERR(fh);

	err = ovl_check_origin_fh(ofs, fh, false, upperdentry, stackp);
	kfree(fh);

	if (err) {
		if (err == -ESTALE)
			return 0;
		return err;
	}

	if (WARN_ON(*ctrp))
		return -EIO;

	*ctrp = 1;
	return 0;
}

/*
 * Verify that @fh matches the file handle stored in xattr @name.
 * Return 0 on match, -ESTALE on mismatch, < 0 on error.
 */
static int ovl_verify_fh(struct dentry *dentry, const char *name,
			 const struct ovl_fh *fh)
{
	struct ovl_fh *ofh = ovl_get_fh(dentry, name);
	int err = 0;

	if (!ofh)
		return -ENODATA;

	if (IS_ERR(ofh))
		return PTR_ERR(ofh);

	if (fh->len != ofh->len || memcmp(fh, ofh, fh->len))
		err = -ESTALE;

	kfree(ofh);
	return err;
}

/*
 * Verify that @real dentry matches the file handle stored in xattr @name.
 *
 * If @set is true and there is no stored file handle, encode @real and store
 * file handle in xattr @name.
 *
 * Return 0 on match, -ESTALE on mismatch, -ENODATA on no xattr, < 0 on error.
 */
int ovl_verify_set_fh(struct dentry *dentry, const char *name,
		      struct dentry *real, bool is_upper, bool set)
{
	struct inode *inode;
	struct ovl_fh *fh;
	int err;

	fh = ovl_encode_real_fh(real, is_upper);
	err = PTR_ERR(fh);
	if (IS_ERR(fh))
		goto fail;

	err = ovl_verify_fh(dentry, name, fh);
	if (set && err == -ENODATA)
		err = ovl_do_setxattr(dentry, name, fh, fh->len, 0);
	if (err)
		goto fail;

out:
	kfree(fh);
	return err;

fail:
	inode = d_inode(real);
	pr_warn_ratelimited("overlayfs: failed to verify %s (%pd2, ino=%lu, err=%i)\n",
			    is_upper ? "upper" : "origin", real,
			    inode ? inode->i_ino : 0, err);
	goto out;
}

/* Get upper dentry from index */
struct dentry *ovl_index_upper(struct ovl_fs *ofs, struct dentry *index)
{
	struct ovl_fh *fh;
	struct dentry *upper;

	if (!d_is_dir(index))
		return dget(index);

	fh = ovl_get_fh(index, OVL_XATTR_UPPER);
	if (IS_ERR_OR_NULL(fh))
		return ERR_CAST(fh);

	upper = ovl_decode_real_fh(fh, ofs->upper_mnt, true);
	kfree(fh);

	if (IS_ERR_OR_NULL(upper))
		return upper ?: ERR_PTR(-ESTALE);

	if (!d_is_dir(upper)) {
		pr_warn_ratelimited("overlayfs: invalid index upper (%pd2, upper=%pd2).\n",
				    index, upper);
		dput(upper);
		return ERR_PTR(-EIO);
	}

	return upper;
}

/* Is this a leftover from create/whiteout of directory index entry? */
static bool ovl_is_temp_index(struct dentry *index)
{
	return index->d_name.name[0] == '#';
}

/*
 * Verify that an index entry name matches the origin file handle stored in
 * OVL_XATTR_ORIGIN and that origin file handle can be decoded to lower path.
 * Return 0 on match, -ESTALE on mismatch or stale origin, < 0 on error.
 */
int ovl_verify_index(struct ovl_fs *ofs, struct dentry *index)
{
	struct ovl_fh *fh = NULL;
	size_t len;
	struct ovl_path origin = { };
	struct ovl_path *stack = &origin;
	struct dentry *upper = NULL;
	int err;

	if (!d_inode(index))
		return 0;

	/* Cleanup leftover from index create/cleanup attempt */
	err = -ESTALE;
	if (ovl_is_temp_index(index))
		goto fail;

	err = -EINVAL;
	if (index->d_name.len < sizeof(struct ovl_fh)*2)
		goto fail;

	err = -ENOMEM;
	len = index->d_name.len / 2;
	fh = kzalloc(len, GFP_KERNEL);
	if (!fh)
		goto fail;

	err = -EINVAL;
	if (hex2bin((u8 *)fh, index->d_name.name, len))
		goto fail;

	err = ovl_check_fh_len(fh, len);
	if (err)
		goto fail;

	/*
	 * Whiteout index entries are used as an indication that an exported
	 * overlay file handle should be treated as stale (i.e. after unlink
	 * of the overlay inode). These entries contain no origin xattr.
	 */
	if (ovl_is_whiteout(index))
		goto out;

	/*
	 * Verifying directory index entries are not stale is expensive, so
	 * only verify stale dir index if NFS export is enabled.
	 */
	if (d_is_dir(index) && !ofs->config.nfs_export)
		goto out;

	/*
	 * Directory index entries should have 'upper' xattr pointing to the
	 * real upper dir. Non-dir index entries are hardlinks to the upper
	 * real inode. For non-dir index, we can read the copy up origin xattr
	 * directly from the index dentry, but for dir index we first need to
	 * decode the upper directory.
	 */
	upper = ovl_index_upper(ofs, index);
	if (IS_ERR_OR_NULL(upper)) {
		err = PTR_ERR(upper);
		/*
		 * Directory index entries with no 'upper' xattr need to be
		 * removed. When dir index entry has a stale 'upper' xattr,
		 * we assume that upper dir was removed and we treat the dir
		 * index as orphan entry that needs to be whited out.
		 */
		if (err == -ESTALE)
			goto orphan;
		else if (!err)
			err = -ESTALE;
		goto fail;
	}

	err = ovl_verify_fh(upper, OVL_XATTR_ORIGIN, fh);
	dput(upper);
	if (err)
		goto fail;

	/* Check if non-dir index is orphan and don't warn before cleaning it */
	if (!d_is_dir(index) && d_inode(index)->i_nlink == 1) {
		err = ovl_check_origin_fh(ofs, fh, false, index, &stack);
		if (err)
			goto fail;

		if (ovl_get_nlink(origin.dentry, index, 0) == 0)
			goto orphan;
	}

out:
	dput(origin.dentry);
	kfree(fh);
	return err;

fail:
	pr_warn_ratelimited("overlayfs: failed to verify index (%pd2, ftype=%x, err=%i)\n",
			    index, d_inode(index)->i_mode & S_IFMT, err);
	goto out;

orphan:
	pr_warn_ratelimited("overlayfs: orphan index entry (%pd2, ftype=%x, nlink=%u)\n",
			    index, d_inode(index)->i_mode & S_IFMT,
			    d_inode(index)->i_nlink);
	err = -ENOENT;
	goto out;
}

static int ovl_get_index_name_fh(struct ovl_fh *fh, struct qstr *name)
{
	char *n, *s;

	n = kzalloc(fh->len * 2, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	s  = bin2hex(n, fh, fh->len);
	*name = (struct qstr) QSTR_INIT(n, s - n);

	return 0;

}

/*
 * Lookup in indexdir for the index entry of a lower real inode or a copy up
 * origin inode. The index entry name is the hex representation of the lower
 * inode file handle.
 *
 * If the index dentry in negative, then either no lower aliases have been
 * copied up yet, or aliases have been copied up in older kernels and are
 * not indexed.
 *
 * If the index dentry for a copy up origin inode is positive, but points
 * to an inode different than the upper inode, then either the upper inode
 * has been copied up and not indexed or it was indexed, but since then
 * index dir was cleared. Either way, that index cannot be used to indentify
 * the overlay inode.
 */
int ovl_get_index_name(struct dentry *origin, struct qstr *name)
{
	struct ovl_fh *fh;
	int err;

	fh = ovl_encode_real_fh(origin, false);
	if (IS_ERR(fh))
		return PTR_ERR(fh);

	err = ovl_get_index_name_fh(fh, name);

	kfree(fh);
	return err;
}

/* Lookup index by file handle for NFS export */
struct dentry *ovl_get_index_fh(struct ovl_fs *ofs, struct ovl_fh *fh)
{
	struct dentry *index;
	struct qstr name;
	int err;

	err = ovl_get_index_name_fh(fh, &name);
	if (err)
		return ERR_PTR(err);

	index = lookup_one_len_unlocked(name.name, ofs->indexdir, name.len);
	kfree(name.name);
	if (IS_ERR(index)) {
		if (PTR_ERR(index) == -ENOENT)
			index = NULL;
		return index;
	}

	if (d_is_negative(index))
		err = 0;
	else if (ovl_is_whiteout(index))
		err = -ESTALE;
	else if (ovl_dentry_weird(index))
		err = -EIO;
	else
		return index;

	dput(index);
	return ERR_PTR(err);
}

struct dentry *ovl_lookup_index(struct ovl_fs *ofs, struct dentry *upper,
				struct dentry *origin, bool verify)
{
	struct dentry *index;
	struct inode *inode;
	struct qstr name;
	bool is_dir = d_is_dir(origin);
	int err;

	err = ovl_get_index_name(origin, &name);
	if (err)
		return ERR_PTR(err);

	index = lookup_one_len_unlocked(name.name, ofs->indexdir, name.len);
	if (IS_ERR(index)) {
		err = PTR_ERR(index);
		if (err == -ENOENT) {
			index = NULL;
			goto out;
		}
		pr_warn_ratelimited("overlayfs: failed inode index lookup (ino=%lu, key=%*s, err=%i);\n"
				    "overlayfs: mount with '-o index=off' to disable inodes index.\n",
				    d_inode(origin)->i_ino, name.len, name.name,
				    err);
		goto out;
	}

	inode = d_inode(index);
	if (d_is_negative(index)) {
		goto out_dput;
	} else if (ovl_is_whiteout(index) && !verify) {
		/*
		 * When index lookup is called with !verify for decoding an
		 * overlay file handle, a whiteout index implies that decode
		 * should treat file handle as stale and no need to print a
		 * warning about it.
		 */
		dput(index);
		index = ERR_PTR(-ESTALE);
		goto out;
	} else if (ovl_dentry_weird(index) || ovl_is_whiteout(index) ||
		   ((inode->i_mode ^ d_inode(origin)->i_mode) & S_IFMT)) {
		/*
		 * Index should always be of the same file type as origin
		 * except for the case of a whiteout index. A whiteout
		 * index should only exist if all lower aliases have been
		 * unlinked, which means that finding a lower origin on lookup
		 * whose index is a whiteout should be treated as an error.
		 */
		pr_warn_ratelimited("overlayfs: bad index found (index=%pd2, ftype=%x, origin ftype=%x).\n",
				    index, d_inode(index)->i_mode & S_IFMT,
				    d_inode(origin)->i_mode & S_IFMT);
		goto fail;
	} else if (is_dir && verify) {
		if (!upper) {
			pr_warn_ratelimited("overlayfs: suspected uncovered redirected dir found (origin=%pd2, index=%pd2).\n",
					    origin, index);
			goto fail;
		}

		/* Verify that dir index 'upper' xattr points to upper dir */
		err = ovl_verify_upper(index, upper, false);
		if (err) {
			if (err == -ESTALE) {
				pr_warn_ratelimited("overlayfs: suspected multiply redirected dir found (upper=%pd2, origin=%pd2, index=%pd2).\n",
						    upper, origin, index);
			}
			goto fail;
		}
	} else if (upper && d_inode(upper) != inode) {
		goto out_dput;
	}
out:
	kfree(name.name);
	return index;

out_dput:
	dput(index);
	index = NULL;
	goto out;

fail:
	dput(index);
	index = ERR_PTR(-EIO);
	goto out;
}

/*
 * Returns next layer in stack starting from top.
 * Returns -1 if this is the last layer.
 */
int ovl_path_next(int idx, struct dentry *dentry, struct path *path)
{
	struct ovl_entry *oe = dentry->d_fsdata;

	BUG_ON(idx < 0);
	if (idx == 0) {
		ovl_path_upper(dentry, path);
		if (path->dentry)
			return oe->numlower ? 1 : -1;
		idx++;
	}
	BUG_ON(idx > oe->numlower);
	path->dentry = oe->lowerstack[idx - 1].dentry;
	path->mnt = oe->lowerstack[idx - 1].layer->mnt;

	return (idx < oe->numlower) ? idx + 1 : -1;
}

/* Fix missing 'origin' xattr */
static int ovl_fix_origin(struct dentry *dentry, struct dentry *lower,
			  struct dentry *upper)
{
	int err;

	if (ovl_check_origin_xattr(upper))
		return 0;

	err = ovl_want_write(dentry);
	if (err)
		return err;

	err = ovl_set_origin(dentry, lower, upper);
	if (!err)
		err = ovl_set_impure(dentry->d_parent, upper->d_parent);

	ovl_drop_write(dentry);
	return err;
}

struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags)
{
	struct ovl_entry *oe;
	const struct cred *old_cred;
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	struct ovl_entry *roe = dentry->d_sb->s_root->d_fsdata;
	struct ovl_path *stack = NULL;
	struct dentry *upperdir, *upperdentry = NULL;
	struct dentry *origin = NULL;
	struct dentry *index = NULL;
	unsigned int ctr = 0;
	struct inode *inode = NULL;
	bool upperopaque = false;
	char *upperredirect = NULL;
	struct dentry *this;
	unsigned int i;
	int err;
	struct ovl_lookup_data d = {
		.name = dentry->d_name,
		.is_dir = false,
		.opaque = false,
		.stop = false,
		.last = ofs->config.redirect_follow ? false : !poe->numlower,
		.redirect = NULL,
	};

	if (dentry->d_name.len > ofs->namelen)
		return ERR_PTR(-ENAMETOOLONG);

	old_cred = ovl_override_creds(dentry->d_sb);
	upperdir = ovl_dentry_upper(dentry->d_parent);
	if (upperdir) {
		err = ovl_lookup_layer(upperdir, &d, &upperdentry);
		if (err)
			goto out;

		if (upperdentry && unlikely(ovl_dentry_remote(upperdentry))) {
			dput(upperdentry);
			err = -EREMOTE;
			goto out;
		}
		if (upperdentry && !d.is_dir) {
			BUG_ON(!d.stop || d.redirect);
			/*
			 * Lookup copy up origin by decoding origin file handle.
			 * We may get a disconnected dentry, which is fine,
			 * because we only need to hold the origin inode in
			 * cache and use its inode number.  We may even get a
			 * connected dentry, that is not under any of the lower
			 * layers root.  That is also fine for using it's inode
			 * number - it's the same as if we held a reference
			 * to a dentry in lower layer that was moved under us.
			 */
			err = ovl_check_origin(ofs, upperdentry, &stack, &ctr);
			if (err)
				goto out_put_upper;
		}

		if (d.redirect) {
			err = -ENOMEM;
			upperredirect = kstrdup(d.redirect, GFP_KERNEL);
			if (!upperredirect)
				goto out_put_upper;
			if (d.redirect[0] == '/')
				poe = roe;
		}
		upperopaque = d.opaque;
	}

	if (!d.stop && poe->numlower) {
		err = -ENOMEM;
		stack = kcalloc(ofs->numlower, sizeof(struct ovl_path),
				GFP_KERNEL);
		if (!stack)
			goto out_put_upper;
	}

	for (i = 0; !d.stop && i < poe->numlower; i++) {
		struct ovl_path lower = poe->lowerstack[i];

		if (!ofs->config.redirect_follow)
			d.last = i == poe->numlower - 1;
		else
			d.last = lower.layer->idx == roe->numlower;

		err = ovl_lookup_layer(lower.dentry, &d, &this);
		if (err)
			goto out_put;

		if (!this)
			continue;

		/*
		 * If no origin fh is stored in upper of a merge dir, store fh
		 * of lower dir and set upper parent "impure".
		 */
		if (upperdentry && !ctr && !ofs->noxattr) {
			err = ovl_fix_origin(dentry, this, upperdentry);
			if (err) {
				dput(this);
				goto out_put;
			}
		}

		/*
		 * When "verify_lower" feature is enabled, do not merge with a
		 * lower dir that does not match a stored origin xattr. In any
		 * case, only verified origin is used for index lookup.
		 */
		if (upperdentry && !ctr && ovl_verify_lower(dentry->d_sb)) {
			err = ovl_verify_origin(upperdentry, this, false);
			if (err) {
				dput(this);
				break;
			}

			/* Bless lower dir as verified origin */
			origin = this;
		}

		stack[ctr].dentry = this;
		stack[ctr].layer = lower.layer;
		ctr++;

		/*
		 * Following redirects can have security consequences: it's like
		 * a symlink into the lower layer without the permission checks.
		 * This is only a problem if the upper layer is untrusted (e.g
		 * comes from an USB drive).  This can allow a non-readable file
		 * or directory to become readable.
		 *
		 * Only following redirects when redirects are enabled disables
		 * this attack vector when not necessary.
		 */
		err = -EPERM;
		if (d.redirect && !ofs->config.redirect_follow) {
			pr_warn_ratelimited("overlayfs: refusing to follow redirect for (%pd2)\n",
					    dentry);
			goto out_put;
		}

		if (d.stop)
			break;

		if (d.redirect && d.redirect[0] == '/' && poe != roe) {
			poe = roe;
			/* Find the current layer on the root dentry */
			i = lower.layer->idx - 1;
		}
	}

	/*
	 * Lookup index by lower inode and verify it matches upper inode.
	 * We only trust dir index if we verified that lower dir matches
	 * origin, otherwise dir index entries may be inconsistent and we
	 * ignore them. Always lookup index of non-dir and non-upper.
	 */
	if (ctr && (!upperdentry || !d.is_dir))
		origin = stack[0].dentry;

	if (origin && ovl_indexdir(dentry->d_sb) &&
	    (!d.is_dir || ovl_index_all(dentry->d_sb))) {
		index = ovl_lookup_index(ofs, upperdentry, origin, true);
		if (IS_ERR(index)) {
			err = PTR_ERR(index);
			index = NULL;
			goto out_put;
		}
	}

	oe = ovl_alloc_entry(ctr);
	err = -ENOMEM;
	if (!oe)
		goto out_put;

	memcpy(oe->lowerstack, stack, sizeof(struct ovl_path) * ctr);
	dentry->d_fsdata = oe;

	if (upperopaque)
		ovl_dentry_set_opaque(dentry);

	if (upperdentry)
		ovl_dentry_set_upper_alias(dentry);
	else if (index)
		upperdentry = dget(index);

	if (upperdentry || ctr) {
		if (ctr)
			origin = stack[0].dentry;
		inode = ovl_get_inode(dentry->d_sb, upperdentry, origin, index,
				      ctr);
		err = PTR_ERR(inode);
		if (IS_ERR(inode))
			goto out_free_oe;

		OVL_I(inode)->redirect = upperredirect;
	}

	revert_creds(old_cred);
	dput(index);
	kfree(stack);
	kfree(d.redirect);
	return d_splice_alias(inode, dentry);

out_free_oe:
	dentry->d_fsdata = NULL;
	kfree(oe);
out_put:
	dput(index);
	for (i = 0; i < ctr; i++)
		dput(stack[i].dentry);
	kfree(stack);
out_put_upper:
	dput(upperdentry);
	kfree(upperredirect);
out:
	kfree(d.redirect);
	revert_creds(old_cred);
	return ERR_PTR(err);
}

bool ovl_lower_positive(struct dentry *dentry)
{
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	const struct qstr *name = &dentry->d_name;
	const struct cred *old_cred;
	unsigned int i;
	bool positive = false;
	bool done = false;

	/*
	 * If dentry is negative, then lower is positive iff this is a
	 * whiteout.
	 */
	if (!dentry->d_inode)
		return ovl_dentry_is_opaque(dentry);

	/* Negative upper -> positive lower */
	if (!ovl_dentry_upper(dentry))
		return true;

	old_cred = ovl_override_creds(dentry->d_sb);
	/* Positive upper -> have to look up lower to see whether it exists */
	for (i = 0; !done && !positive && i < poe->numlower; i++) {
		struct dentry *this;
		struct dentry *lowerdir = poe->lowerstack[i].dentry;

		this = lookup_one_len_unlocked(name->name, lowerdir,
					       name->len);
		if (IS_ERR(this)) {
			switch (PTR_ERR(this)) {
			case -ENOENT:
			case -ENAMETOOLONG:
				break;

			default:
				/*
				 * Assume something is there, we just couldn't
				 * access it.
				 */
				positive = true;
				break;
			}
		} else {
			if (this->d_inode) {
				positive = !ovl_is_whiteout(this);
				done = true;
			}
			dput(this);
		}
	}
	revert_creds(old_cred);

	return positive;
}
