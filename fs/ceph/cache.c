// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 */

#include <linux/ceph/ceph_debug.h>

#include <linux/fs_context.h>
#include "super.h"
#include "cache.h"

void ceph_fscache_register_inode_cookie(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);

	/* No caching for filesystem? */
	if (!fsc->fscache)
		return;

	/* Regular files only */
	if (!S_ISREG(inode->i_mode))
		return;

	/* Only new inodes! */
	if (!(inode->i_state & I_NEW))
		return;

	WARN_ON_ONCE(ci->fscache);

	ci->fscache = fscache_acquire_cookie(fsc->fscache, 0,
					     &ci->i_vino, sizeof(ci->i_vino),
					     &ci->i_version, sizeof(ci->i_version),
					     i_size_read(inode));
}

void ceph_fscache_unregister_inode_cookie(struct ceph_inode_info* ci)
{
	struct fscache_cookie *cookie = ci->fscache;

	fscache_relinquish_cookie(cookie, false);
}

void ceph_fscache_use_cookie(struct inode *inode, bool will_modify)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	fscache_use_cookie(ci->fscache, will_modify);
}

void ceph_fscache_unuse_cookie(struct inode *inode, bool update)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (update) {
		loff_t i_size = i_size_read(inode);

		fscache_unuse_cookie(ci->fscache, &ci->i_version, &i_size);
	} else {
		fscache_unuse_cookie(ci->fscache, NULL, NULL);
	}
}

void ceph_fscache_update(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	loff_t i_size = i_size_read(inode);

	fscache_update_cookie(ci->fscache, &ci->i_version, &i_size);
}

void ceph_fscache_invalidate(struct inode *inode, bool dio_write)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	fscache_invalidate(ceph_inode(inode)->fscache,
			   &ci->i_version, i_size_read(inode),
			   dio_write ? FSCACHE_INVAL_DIO_WRITE : 0);
}

int ceph_fscache_register_fs(struct ceph_fs_client* fsc, struct fs_context *fc)
{
	const struct ceph_fsid *fsid = &fsc->client->fsid;
	const char *fscache_uniq = fsc->mount_options->fscache_uniq;
	size_t uniq_len = fscache_uniq ? strlen(fscache_uniq) : 0;
	char *name;
	int err = 0;

	name = kasprintf(GFP_KERNEL, "ceph,%pU%s%s", fsid, uniq_len ? "," : "",
			 uniq_len ? fscache_uniq : "");
	if (!name)
		return -ENOMEM;

	fsc->fscache = fscache_acquire_volume(name, NULL, NULL, 0);
	if (IS_ERR_OR_NULL(fsc->fscache)) {
		errorfc(fc, "Unable to register fscache cookie for %s", name);
		err = fsc->fscache ? PTR_ERR(fsc->fscache) : -EOPNOTSUPP;
		fsc->fscache = NULL;
	}
	kfree(name);
	return err;
}

void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc)
{
	fscache_relinquish_volume(fsc->fscache, NULL, false);
}
