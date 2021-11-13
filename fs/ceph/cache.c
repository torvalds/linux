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

struct fscache_netfs ceph_cache_netfs = {
	.name		= "ceph",
	.version	= 0,
};

static DEFINE_MUTEX(ceph_fscache_lock);
static LIST_HEAD(ceph_fscache_list);

struct ceph_fscache_entry {
	struct list_head list;
	struct fscache_cookie *fscache;
	size_t uniq_len;
	/* The following members must be last */
	struct ceph_fsid fsid;
	char uniquifier[];
};

static const struct fscache_cookie_def ceph_fscache_fsid_object_def = {
	.name		= "CEPH.fsid",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

int __init ceph_fscache_register(void)
{
	return fscache_register_netfs(&ceph_cache_netfs);
}

void ceph_fscache_unregister(void)
{
	fscache_unregister_netfs(&ceph_cache_netfs);
}

int ceph_fscache_register_fs(struct ceph_fs_client* fsc, struct fs_context *fc)
{
	const struct ceph_fsid *fsid = &fsc->client->fsid;
	const char *fscache_uniq = fsc->mount_options->fscache_uniq;
	size_t uniq_len = fscache_uniq ? strlen(fscache_uniq) : 0;
	struct ceph_fscache_entry *ent;
	int err = 0;

	mutex_lock(&ceph_fscache_lock);
	list_for_each_entry(ent, &ceph_fscache_list, list) {
		if (memcmp(&ent->fsid, fsid, sizeof(*fsid)))
			continue;
		if (ent->uniq_len != uniq_len)
			continue;
		if (uniq_len && memcmp(ent->uniquifier, fscache_uniq, uniq_len))
			continue;

		errorfc(fc, "fscache cookie already registered for fsid %pU, use fsc=<uniquifier> option",
		       fsid);
		err = -EBUSY;
		goto out_unlock;
	}

	ent = kzalloc(sizeof(*ent) + uniq_len, GFP_KERNEL);
	if (!ent) {
		err = -ENOMEM;
		goto out_unlock;
	}

	memcpy(&ent->fsid, fsid, sizeof(*fsid));
	if (uniq_len > 0) {
		memcpy(&ent->uniquifier, fscache_uniq, uniq_len);
		ent->uniq_len = uniq_len;
	}

	fsc->fscache = fscache_acquire_cookie(ceph_cache_netfs.primary_index,
					      &ceph_fscache_fsid_object_def,
					      &ent->fsid, sizeof(ent->fsid) + uniq_len,
					      NULL, 0,
					      fsc, 0, true);

	if (fsc->fscache) {
		ent->fscache = fsc->fscache;
		list_add_tail(&ent->list, &ceph_fscache_list);
	} else {
		kfree(ent);
		errorfc(fc, "unable to register fscache cookie for fsid %pU",
		       fsid);
		/* all other fs ignore this error */
	}
out_unlock:
	mutex_unlock(&ceph_fscache_lock);
	return err;
}

static enum fscache_checkaux ceph_fscache_inode_check_aux(
	void *cookie_netfs_data, const void *data, uint16_t dlen,
	loff_t object_size)
{
	struct ceph_inode_info* ci = cookie_netfs_data;
	struct inode* inode = &ci->vfs_inode;

	if (dlen != sizeof(ci->i_version) ||
	    i_size_read(inode) != object_size)
		return FSCACHE_CHECKAUX_OBSOLETE;

	if (*(u64 *)data != ci->i_version)
		return FSCACHE_CHECKAUX_OBSOLETE;

	dout("ceph inode 0x%p cached okay\n", ci);
	return FSCACHE_CHECKAUX_OKAY;
}

static const struct fscache_cookie_def ceph_fscache_inode_object_def = {
	.name		= "CEPH.inode",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= ceph_fscache_inode_check_aux,
};

void ceph_fscache_register_inode_cookie(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);

	/* No caching for filesystem */
	if (!fsc->fscache)
		return;

	/* Only cache for regular files that are read only */
	if (!S_ISREG(inode->i_mode))
		return;

	inode_lock_nested(inode, I_MUTEX_CHILD);
	if (!ci->fscache) {
		ci->fscache = fscache_acquire_cookie(fsc->fscache,
						     &ceph_fscache_inode_object_def,
						     &ci->i_vino, sizeof(ci->i_vino),
						     &ci->i_version, sizeof(ci->i_version),
						     ci, i_size_read(inode), false);
	}
	inode_unlock(inode);
}

void ceph_fscache_unregister_inode_cookie(struct ceph_inode_info* ci)
{
	struct fscache_cookie* cookie;

	if ((cookie = ci->fscache) == NULL)
		return;

	ci->fscache = NULL;

	fscache_relinquish_cookie(cookie, &ci->i_vino, false);
}

static bool ceph_fscache_can_enable(void *data)
{
	struct inode *inode = data;
	return !inode_is_open_for_write(inode);
}

void ceph_fscache_file_set_cookie(struct inode *inode, struct file *filp)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (!fscache_cookie_valid(ci->fscache))
		return;

	if (inode_is_open_for_write(inode)) {
		dout("fscache_file_set_cookie %p %p disabling cache\n",
		     inode, filp);
		fscache_disable_cookie(ci->fscache, &ci->i_vino, false);
	} else {
		fscache_enable_cookie(ci->fscache, &ci->i_vino, i_size_read(inode),
				      ceph_fscache_can_enable, inode);
		if (fscache_cookie_enabled(ci->fscache)) {
			dout("fscache_file_set_cookie %p %p enabling cache\n",
			     inode, filp);
		}
	}
}

void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc)
{
	if (fscache_cookie_valid(fsc->fscache)) {
		struct ceph_fscache_entry *ent;
		bool found = false;

		mutex_lock(&ceph_fscache_lock);
		list_for_each_entry(ent, &ceph_fscache_list, list) {
			if (ent->fscache == fsc->fscache) {
				list_del(&ent->list);
				kfree(ent);
				found = true;
				break;
			}
		}
		WARN_ON_ONCE(!found);
		mutex_unlock(&ceph_fscache_lock);

		__fscache_relinquish_cookie(fsc->fscache, NULL, false);
	}
	fsc->fscache = NULL;
}
