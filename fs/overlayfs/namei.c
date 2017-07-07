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
#include "ovl_entry.h"

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
	buf = kzalloc(prelen + res + strlen(post) + 1, GFP_TEMPORARY);
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

static struct dentry *ovl_get_origin(struct dentry *dentry,
				     struct vfsmount *mnt)
{
	int res;
	struct ovl_fh *fh = NULL;
	struct dentry *origin = NULL;
	int bytes;

	res = vfs_getxattr(dentry, OVL_XATTR_ORIGIN, NULL, 0);
	if (res < 0) {
		if (res == -ENODATA || res == -EOPNOTSUPP)
			return NULL;
		goto fail;
	}
	/* Zero size value means "copied up but origin unknown" */
	if (res == 0)
		return NULL;

	fh  = kzalloc(res, GFP_TEMPORARY);
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

	bytes = (fh->len - offsetof(struct ovl_fh, fid));

	/*
	 * Make sure that the stored uuid matches the uuid of the lower
	 * layer where file handle will be decoded.
	 */
	if (!uuid_equal(&fh->uuid, &mnt->mnt_sb->s_uuid))
		goto out;

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
	    ((d_inode(origin)->i_mode ^ d_inode(dentry)->i_mode) & S_IFMT)) {
		dput(origin);
		origin = NULL;
		goto invalid;
	}

out:
	kfree(fh);
	return origin;

fail:
	pr_warn_ratelimited("overlayfs: failed to get origin (%i)\n", res);
	goto out;
invalid:
	pr_warn_ratelimited("overlayfs: invalid origin (%*phN)\n", res, fh);
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


static int ovl_check_origin(struct dentry *dentry, struct dentry *upperdentry,
			    struct path **stackp, unsigned int *ctrp)
{
	struct super_block *same_sb = ovl_same_sb(dentry->d_sb);
	struct ovl_entry *roe = dentry->d_sb->s_root->d_fsdata;
	struct vfsmount *mnt;
	struct dentry *origin;

	if (!same_sb || !roe->numlower)
		return 0;

       /*
	* Since all layers are on the same fs, we use the first layer for
	* decoding the file handle.  We may get a disconnected dentry,
	* which is fine, because we only need to hold the origin inode in
	* cache and use its inode number.  We may even get a connected dentry,
	* that is not under the first layer's root.  That is also fine for
	* using it's inode number - it's the same as if we held a reference
	* to a dentry in first layer that was moved under us.
	*/
	mnt = roe->lowerstack[0].mnt;

	origin = ovl_get_origin(upperdentry, mnt);
	if (IS_ERR_OR_NULL(origin))
		return PTR_ERR(origin);

	BUG_ON(*stackp || *ctrp);
	*stackp = kmalloc(sizeof(struct path), GFP_TEMPORARY);
	if (!*stackp) {
		dput(origin);
		return -ENOMEM;
	}
	**stackp = (struct path) { .dentry = origin, .mnt = mnt };
	*ctrp = 1;

	return 0;
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
	*path = oe->lowerstack[idx - 1];

	return (idx < oe->numlower) ? idx + 1 : -1;
}

struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags)
{
	struct ovl_entry *oe;
	const struct cred *old_cred;
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	struct ovl_entry *roe = dentry->d_sb->s_root->d_fsdata;
	struct path *stack = NULL;
	struct dentry *upperdir, *upperdentry = NULL;
	unsigned int ctr = 0;
	struct inode *inode = NULL;
	bool upperopaque = false;
	bool upperimpure = false;
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
	upperdir = ovl_upperdentry_dereference(poe);
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
			err = ovl_check_origin(dentry, upperdentry,
					       &stack, &ctr);
			if (err)
				goto out;
		}

		if (d.redirect) {
			upperredirect = kstrdup(d.redirect, GFP_KERNEL);
			if (!upperredirect)
				goto out_put_upper;
			if (d.redirect[0] == '/')
				poe = roe;
		}
		upperopaque = d.opaque;
		if (upperdentry && d.is_dir)
			upperimpure = ovl_is_impuredir(upperdentry);
	}

	if (!d.stop && poe->numlower) {
		err = -ENOMEM;
		stack = kcalloc(ofs->numlower, sizeof(struct path),
				GFP_TEMPORARY);
		if (!stack)
			goto out_put_upper;
	}

	for (i = 0; !d.stop && i < poe->numlower; i++) {
		struct path lowerpath = poe->lowerstack[i];

		d.last = i == poe->numlower - 1;
		err = ovl_lookup_layer(lowerpath.dentry, &d, &this);
		if (err)
			goto out_put;

		if (!this)
			continue;

		stack[ctr].dentry = this;
		stack[ctr].mnt = lowerpath.mnt;
		ctr++;

		if (d.stop)
			break;

		if (d.redirect && d.redirect[0] == '/' && poe != roe) {
			poe = roe;

			/* Find the current layer on the root dentry */
			for (i = 0; i < poe->numlower; i++)
				if (poe->lowerstack[i].mnt == lowerpath.mnt)
					break;
			if (WARN_ON(i == poe->numlower))
				break;
		}
	}

	oe = ovl_alloc_entry(ctr);
	err = -ENOMEM;
	if (!oe)
		goto out_put;

	if (upperdentry || ctr) {
		struct dentry *realdentry;
		struct inode *realinode;

		realdentry = upperdentry ? upperdentry : stack[0].dentry;
		realinode = d_inode(realdentry);

		err = -ENOMEM;
		if (upperdentry && !d_is_dir(upperdentry)) {
			inode = ovl_get_inode(dentry->d_sb, realinode);
		} else {
			inode = ovl_new_inode(dentry->d_sb, realinode->i_mode,
					      realinode->i_rdev);
			if (inode)
				ovl_inode_init(inode, realinode, !!upperdentry);
		}
		if (!inode)
			goto out_free_oe;
		ovl_copyattr(realdentry->d_inode, inode);
	}

	revert_creds(old_cred);
	oe->opaque = upperopaque;
	oe->impure = upperimpure;
	oe->redirect = upperredirect;
	oe->__upperdentry = upperdentry;
	memcpy(oe->lowerstack, stack, sizeof(struct path) * ctr);
	kfree(stack);
	kfree(d.redirect);
	dentry->d_fsdata = oe;
	d_add(dentry, inode);

	return NULL;

out_free_oe:
	kfree(oe);
out_put:
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
	if (!oe->__upperdentry)
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
