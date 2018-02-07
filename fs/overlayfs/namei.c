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
	return 1;
}

static struct ovl_fh *ovl_get_origin_fh(struct dentry *dentry)
{
	int res;
	struct ovl_fh *fh = NULL;

	res = vfs_getxattr(dentry, OVL_XATTR_ORIGIN, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return NULL;
		goto fail;
	}
	/* Zero size value means "copied up but origin unknown" */
	if (res == 0)
		return NULL;

	fh  = kzalloc(res, GFP_KERNEL);
	if (!fh)
		return ERR_PTR(-ENOMEM);

	res = vfs_getxattr(dentry, OVL_XATTR_ORIGIN, fh, res);
	if (res < 0)
		goto fail;

	if (res < sizeof(struct ovl_fh) || res < fh->len)
		goto invalid;

	if (fh->magic != OVL_FH_MAGIC)
		goto invalid;

	/* Treat larger version and unknown flags as "origin unknown" */
	if (fh->version > OVL_FH_VERSION || fh->flags & ~OVL_FH_FLAG_ALL)
		goto out;

	/* Treat endianness mismatch as "origin unknown" */
	if (!(fh->flags & OVL_FH_FLAG_ANY_ENDIAN) &&
	    (fh->flags & OVL_FH_FLAG_BIG_ENDIAN) != OVL_FH_FLAG_CPU_ENDIAN)
		goto out;

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

static struct dentry *ovl_get_origin(struct dentry *dentry,
				     struct vfsmount *mnt)
{
	struct dentry *origin = NULL;
	struct ovl_fh *fh = ovl_get_origin_fh(dentry);
	int bytes;

	if (IS_ERR_OR_NULL(fh))
		return (struct dentry *)fh;

	/*
	 * Make sure that the stored uuid matches the uuid of the lower
	 * layer where file handle will be decoded.
	 */
	if (!uuid_equal(&fh->uuid, &mnt->mnt_sb->s_uuid))
		goto out;

	bytes = (fh->len - offsetof(struct ovl_fh, fid));
	origin = exportfs_decode_fh(mnt, (struct fid *)fh->fid,
				    bytes >> 2, (int)fh->type,
				    ovl_acceptable, NULL);
	if (IS_ERR(origin)) {
		/* Treat stale file handle as "origin unknown" */
		if (origin == ERR_PTR(-ESTALE))
			origin = NULL;
		goto out;
	}

	if (ovl_dentry_weird(origin) ||
	    ((d_inode(origin)->i_mode ^ d_inode(dentry)->i_mode) & S_IFMT))
		goto invalid;

out:
	kfree(fh);
	return origin;

invalid:
	pr_warn_ratelimited("overlayfs: invalid origin (%pd2)\n", origin);
	dput(origin);
	origin = NULL;
	goto out;
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
	d->is_dir = true;
	if (!d->last && ovl_is_opaquedir(this)) {
		d->stop = d->opaque = true;
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


static int ovl_check_origin(struct dentry *upperdentry,
			    struct ovl_path *lower, unsigned int numlower,
			    struct ovl_path **stackp, unsigned int *ctrp)
{
	struct vfsmount *mnt;
	struct dentry *origin = NULL;
	int i;

	for (i = 0; i < numlower; i++) {
		mnt = lower[i].layer->mnt;
		origin = ovl_get_origin(upperdentry, mnt);
		if (IS_ERR(origin))
			return PTR_ERR(origin);

		if (origin)
			break;
	}

	if (!origin)
		return 0;

	BUG_ON(*ctrp);
	if (!*stackp)
		*stackp = kmalloc(sizeof(struct ovl_path), GFP_KERNEL);
	if (!*stackp) {
		dput(origin);
		return -ENOMEM;
	}
	**stackp = (struct ovl_path){.dentry = origin, .layer = lower[i].layer};
	*ctrp = 1;

	return 0;
}

/*
 * Verify that @fh matches the origin file handle stored in OVL_XATTR_ORIGIN.
 * Return 0 on match, -ESTALE on mismatch, < 0 on error.
 */
static int ovl_verify_origin_fh(struct dentry *dentry, const struct ovl_fh *fh)
{
	struct ovl_fh *ofh = ovl_get_origin_fh(dentry);
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
 * Verify that an inode matches the origin file handle stored in upper inode.
 *
 * If @set is true and there is no stored file handle, encode and store origin
 * file handle in OVL_XATTR_ORIGIN.
 *
 * Return 0 on match, -ESTALE on mismatch, < 0 on error.
 */
int ovl_verify_origin(struct dentry *dentry, struct dentry *origin,
		      bool is_upper, bool set)
{
	struct inode *inode;
	struct ovl_fh *fh;
	int err;

	fh = ovl_encode_fh(origin, is_upper);
	err = PTR_ERR(fh);
	if (IS_ERR(fh))
		goto fail;

	err = ovl_verify_origin_fh(dentry, fh);
	if (set && err == -ENODATA)
		err = ovl_do_setxattr(dentry, OVL_XATTR_ORIGIN, fh, fh->len, 0);
	if (err)
		goto fail;

out:
	kfree(fh);
	return err;

fail:
	inode = d_inode(origin);
	pr_warn_ratelimited("overlayfs: failed to verify origin (%pd2, ino=%lu, err=%i)\n",
			    origin, inode ? inode->i_ino : 0, err);
	goto out;
}

/*
 * Verify that an index entry name matches the origin file handle stored in
 * OVL_XATTR_ORIGIN and that origin file handle can be decoded to lower path.
 * Return 0 on match, -ESTALE on mismatch or stale origin, < 0 on error.
 */
int ovl_verify_index(struct dentry *index, struct ovl_path *lower,
		     unsigned int numlower)
{
	struct ovl_fh *fh = NULL;
	size_t len;
	struct ovl_path origin = { };
	struct ovl_path *stack = &origin;
	unsigned int ctr = 0;
	int err;

	if (!d_inode(index))
		return 0;

	/*
	 * Directory index entries are going to be used for looking up
	 * redirected upper dirs by lower dir fh when decoding an overlay
	 * file handle of a merge dir. Whiteout index entries are going to be
	 * used as an indication that an exported overlay file handle should
	 * be treated as stale (i.e. after unlink of the overlay inode).
	 * We don't know the verification rules for directory and whiteout
	 * index entries, because they have not been implemented yet, so return
	 * EINVAL if those entries are found to abort the mount to avoid
	 * corrupting an index that was created by a newer kernel.
	 */
	err = -EINVAL;
	if (d_is_dir(index) || ovl_is_whiteout(index))
		goto fail;

	if (index->d_name.len < sizeof(struct ovl_fh)*2)
		goto fail;

	err = -ENOMEM;
	len = index->d_name.len / 2;
	fh = kzalloc(len, GFP_KERNEL);
	if (!fh)
		goto fail;

	err = -EINVAL;
	if (hex2bin((u8 *)fh, index->d_name.name, len) || len != fh->len)
		goto fail;

	err = ovl_verify_origin_fh(index, fh);
	if (err)
		goto fail;

	err = ovl_check_origin(index, lower, numlower, &stack, &ctr);
	if (!err && !ctr)
		err = -ESTALE;
	if (err)
		goto fail;

	/* Check if index is orphan and don't warn before cleaning it */
	if (d_inode(index)->i_nlink == 1 &&
	    ovl_get_nlink(origin.dentry, index, 0) == 0)
		err = -ENOENT;

	dput(origin.dentry);
out:
	kfree(fh);
	return err;

fail:
	pr_warn_ratelimited("overlayfs: failed to verify index (%pd2, ftype=%x, err=%i)\n",
			    index, d_inode(index)->i_mode & S_IFMT, err);
	goto out;
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
	int err;
	struct ovl_fh *fh;
	char *n, *s;

	fh = ovl_encode_fh(origin, false);
	if (IS_ERR(fh))
		return PTR_ERR(fh);

	err = -ENOMEM;
	n = kzalloc(fh->len * 2, GFP_KERNEL);
	if (n) {
		s  = bin2hex(n, fh, fh->len);
		*name = (struct qstr) QSTR_INIT(n, s - n);
		err = 0;
	}
	kfree(fh);

	return err;

}

static struct dentry *ovl_lookup_index(struct dentry *dentry,
				       struct dentry *upper,
				       struct dentry *origin)
{
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct dentry *index;
	struct inode *inode;
	struct qstr name;
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
	} else if (upper && d_inode(upper) != inode) {
		goto out_dput;
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

static int ovl_find_layer(struct ovl_fs *ofs, struct ovl_path *path)
{
	int i;

	for (i = 0; i < ofs->numlower; i++) {
		if (ofs->lower_layers[i].mnt == path->layer->mnt)
			break;
	}

	return i;
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
		.last = !poe->numlower,
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
			err = ovl_check_origin(upperdentry, roe->lowerstack,
					       roe->numlower, &stack, &ctr);
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

		d.last = i == poe->numlower - 1;
		err = ovl_lookup_layer(lower.dentry, &d, &this);
		if (err)
			goto out_put;

		if (!this)
			continue;

		stack[ctr].dentry = this;
		stack[ctr].layer = lower.layer;
		ctr++;

		if (d.stop)
			break;

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
			pr_warn_ratelimited("overlay: refusing to follow redirect for (%pd2)\n", dentry);
			goto out_put;
		}

		if (d.redirect && d.redirect[0] == '/' && poe != roe) {
			poe = roe;

			/* Find the current layer on the root dentry */
			i = ovl_find_layer(ofs, &lower);
			if (WARN_ON(i == ofs->numlower))
				break;
		}
	}

	/* Lookup index by lower inode and verify it matches upper inode */
	if (ctr && !d.is_dir && ovl_indexdir(dentry->d_sb)) {
		struct dentry *origin = stack[0].dentry;

		index = ovl_lookup_index(dentry, upperdentry, origin);
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

	oe->opaque = upperopaque;
	memcpy(oe->lowerstack, stack, sizeof(struct ovl_path) * ctr);
	dentry->d_fsdata = oe;

	if (upperdentry)
		ovl_dentry_set_upper_alias(dentry);
	else if (index)
		upperdentry = dget(index);

	if (upperdentry || ctr) {
		inode = ovl_get_inode(dentry, upperdentry, index);
		err = PTR_ERR(inode);
		if (IS_ERR(inode))
			goto out_free_oe;

		OVL_I(inode)->redirect = upperredirect;
		if (index)
			ovl_set_flag(OVL_INDEX, inode);
	}

	revert_creds(old_cred);
	dput(index);
	kfree(stack);
	kfree(d.redirect);
	d_add(dentry, inode);

	return NULL;

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
	struct ovl_entry *oe = dentry->d_fsdata;
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	const struct qstr *name = &dentry->d_name;
	unsigned int i;
	bool positive = false;
	bool done = false;

	/*
	 * If dentry is negative, then lower is positive iff this is a
	 * whiteout.
	 */
	if (!dentry->d_inode)
		return oe->opaque;

	/* Negative upper -> positive lower */
	if (!ovl_dentry_upper(dentry))
		return true;

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

	return positive;
}
