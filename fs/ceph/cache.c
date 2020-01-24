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

struct ceph_aux_inode {
	u64 	version;
	u64	mtime_sec;
	u64	mtime_nsec;
};

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
	char uniquifier[0];
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

		errorf(fc, "ceph: fscache cookie already registered for fsid %pU, use fsc=<uniquifier> option",
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
		errorf(fc, "ceph: unable to register fscache cookie for fsid %pU",
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
	struct ceph_aux_inode aux;
	struct ceph_inode_info* ci = cookie_netfs_data;
	struct inode* inode = &ci->vfs_inode;

	if (dlen != sizeof(aux) ||
	    i_size_read(inode) != object_size)
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&aux, 0, sizeof(aux));
	aux.version = ci->i_version;
	aux.mtime_sec = inode->i_mtime.tv_sec;
	aux.mtime_nsec = inode->i_mtime.tv_nsec;

	if (memcmp(data, &aux, sizeof(aux)) != 0)
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
	struct ceph_aux_inode aux;

	/* No caching for filesystem */
	if (!fsc->fscache)
		return;

	/* Only cache for regular files that are read only */
	if (!S_ISREG(inode->i_mode))
		return;

	inode_lock_nested(inode, I_MUTEX_CHILD);
	if (!ci->fscache) {
		memset(&aux, 0, sizeof(aux));
		aux.version = ci->i_version;
		aux.mtime_sec = inode->i_mtime.tv_sec;
		aux.mtime_nsec = inode->i_mtime.tv_nsec;
		ci->fscache = fscache_acquire_cookie(fsc->fscache,
						     &ceph_fscache_inode_object_def,
						     &ci->i_vino, sizeof(ci->i_vino),
						     &aux, sizeof(aux),
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

	fscache_uncache_all_inode_pages(cookie, &ci->vfs_inode);
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
		fscache_uncache_all_inode_pages(ci->fscache, inode);
	} else {
		fscache_enable_cookie(ci->fscache, &ci->i_vino, i_size_read(inode),
				      ceph_fscache_can_enable, inode);
		if (fscache_cookie_enabled(ci->fscache)) {
			dout("fscache_file_set_cookie %p %p enabling cache\n",
			     inode, filp);
		}
	}
}

static void ceph_readpage_from_fscache_complete(struct page *page, void *data, int error)
{
	if (!error)
		SetPageUptodate(page);

	unlock_page(page);
}

static inline bool cache_valid(struct ceph_inode_info *ci)
{
	return ci->i_fscache_gen == ci->i_rdcache_gen;
}


/* Atempt to read from the fscache,
 *
 * This function is called from the readpage_nounlock context. DO NOT attempt to
 * unlock the page here (or in the callback).
 */
int ceph_readpage_from_fscache(struct inode *inode, struct page *page)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	if (!cache_valid(ci))
		return -ENOBUFS;

	ret = fscache_read_or_alloc_page(ci->fscache, page,
					 ceph_readpage_from_fscache_complete, NULL,
					 GFP_KERNEL);

	switch (ret) {
		case 0: /* Page found */
			dout("page read submitted\n");
			return 0;
		case -ENOBUFS: /* Pages were not found, and can't be */
		case -ENODATA: /* Pages were not found */
			dout("page/inode not in cache\n");
			return ret;
		default:
			dout("%s: unknown error ret = %i\n", __func__, ret);
			return ret;
	}
}

int ceph_readpages_from_fscache(struct inode *inode,
				  struct address_space *mapping,
				  struct list_head *pages,
				  unsigned *nr_pages)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	if (!cache_valid(ci))
		return -ENOBUFS;

	ret = fscache_read_or_alloc_pages(ci->fscache, mapping, pages, nr_pages,
					  ceph_readpage_from_fscache_complete,
					  NULL, mapping_gfp_mask(mapping));

	switch (ret) {
		case 0: /* All pages found */
			dout("all-page read submitted\n");
			return 0;
		case -ENOBUFS: /* Some pages were not found, and can't be */
		case -ENODATA: /* some pages were not found */
			dout("page/inode not in cache\n");
			return ret;
		default:
			dout("%s: unknown error ret = %i\n", __func__, ret);
			return ret;
	}
}

void ceph_readpage_to_fscache(struct inode *inode, struct page *page)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	if (!PageFsCache(page))
		return;

	if (!cache_valid(ci))
		return;

	ret = fscache_write_page(ci->fscache, page, i_size_read(inode),
				 GFP_KERNEL);
	if (ret)
		 fscache_uncache_page(ci->fscache, page);
}

void ceph_invalidate_fscache_page(struct inode* inode, struct page *page)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (!PageFsCache(page))
		return;

	fscache_wait_on_page_write(ci->fscache, page);
	fscache_uncache_page(ci->fscache, page);
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

/*
 * caller should hold CEPH_CAP_FILE_{RD,CACHE}
 */
void ceph_fscache_revalidate_cookie(struct ceph_inode_info *ci)
{
	if (cache_valid(ci))
		return;

	/* resue i_truncate_mutex. There should be no pending
	 * truncate while the caller holds CEPH_CAP_FILE_RD */
	mutex_lock(&ci->i_truncate_mutex);
	if (!cache_valid(ci)) {
		if (fscache_check_consistency(ci->fscache, &ci->i_vino))
			fscache_invalidate(ci->fscache);
		spin_lock(&ci->i_ceph_lock);
		ci->i_fscache_gen = ci->i_rdcache_gen;
		spin_unlock(&ci->i_ceph_lock);
	}
	mutex_unlock(&ci->i_truncate_mutex);
}
