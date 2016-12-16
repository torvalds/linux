/*
 * Copyright (C) 2011 Novell Inc.
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include "overlayfs.h"
#include "ovl_entry.h"

static struct dentry *ovl_lookup_real(struct dentry *dir,
				      const struct qstr *name)
{
	struct dentry *dentry;

	dentry = lookup_one_len_unlocked(name->name, dir, name->len);
	if (IS_ERR(dentry)) {
		if (PTR_ERR(dentry) == -ENOENT)
			dentry = NULL;
	} else if (!dentry->d_inode) {
		dput(dentry);
		dentry = NULL;
	} else if (ovl_dentry_weird(dentry)) {
		dput(dentry);
		/* Don't support traversing automounts and other weirdness */
		dentry = ERR_PTR(-EREMOTE);
	}
	return dentry;
}

static bool ovl_is_opaquedir(struct dentry *dentry)
{
	int res;
	char val;

	if (!d_is_dir(dentry))
		return false;

	res = vfs_getxattr(dentry, OVL_XATTR_OPAQUE, &val, 1);
	if (res == 1 && val == 'y')
		return true;

	return false;
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
	struct ovl_entry *poe = dentry->d_parent->d_fsdata;
	struct path *stack = NULL;
	struct dentry *upperdir, *upperdentry = NULL;
	unsigned int ctr = 0;
	struct inode *inode = NULL;
	bool upperopaque = false;
	bool stop = false;
	bool isdir = false;
	struct dentry *this;
	unsigned int i;
	int err;

	old_cred = ovl_override_creds(dentry->d_sb);
	upperdir = ovl_upperdentry_dereference(poe);
	if (upperdir) {
		this = ovl_lookup_real(upperdir, &dentry->d_name);
		err = PTR_ERR(this);
		if (IS_ERR(this))
			goto out;

		if (this) {
			if (unlikely(ovl_dentry_remote(this))) {
				dput(this);
				err = -EREMOTE;
				goto out;
			}
			if (ovl_is_whiteout(this)) {
				dput(this);
				this = NULL;
				stop = upperopaque = true;
			} else if (!d_is_dir(this)) {
				stop = true;
			} else {
				isdir = true;
				if (poe->numlower && ovl_is_opaquedir(this))
					stop = upperopaque = true;
			}
		}
		upperdentry = this;
	}

	if (!stop && poe->numlower) {
		err = -ENOMEM;
		stack = kcalloc(poe->numlower, sizeof(struct path), GFP_KERNEL);
		if (!stack)
			goto out_put_upper;
	}

	for (i = 0; !stop && i < poe->numlower; i++) {
		struct path lowerpath = poe->lowerstack[i];

		this = ovl_lookup_real(lowerpath.dentry, &dentry->d_name);
		err = PTR_ERR(this);
		if (IS_ERR(this)) {
			/*
			 * If it's positive, then treat ENAMETOOLONG as ENOENT.
			 */
			if (err == -ENAMETOOLONG && (upperdentry || ctr))
				continue;
			goto out_put;
		}
		if (!this)
			continue;
		if (ovl_is_whiteout(this)) {
			dput(this);
			break;
		}
		/*
		 * If this is a non-directory then stop here.
		 */
		if (!d_is_dir(this)) {
			if (isdir) {
				dput(this);
				break;
			}
			stop = true;
		} else {
			/*
			 * Only makes sense to check opaque dir if this is not
			 * the lowermost layer.
			 */
			if (i < poe->numlower - 1 && ovl_is_opaquedir(this))
				stop = true;
		}

		stack[ctr].dentry = this;
		stack[ctr].mnt = lowerpath.mnt;
		ctr++;
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
	oe->__upperdentry = upperdentry;
	memcpy(oe->lowerstack, stack, sizeof(struct path) * ctr);
	kfree(stack);
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
out:
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
