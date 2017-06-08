/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include "super.h"
#include "cache.h"

struct ceph_aux_inode {
	u64 		version;
	struct timespec	mtime;
	loff_t          size;
};

struct fscache_netfs ceph_cache_netfs = {
	.name		= "ceph",
	.version	= 0,
};

static uint16_t ceph_fscache_session_get_key(const void *cookie_netfs_data,
					     void *buffer, uint16_t maxbuf)
{
	const struct ceph_fs_client* fsc = cookie_netfs_data;
	uint16_t klen;

	klen = sizeof(fsc->client->fsid);
	if (klen > maxbuf)
		return 0;

	memcpy(buffer, &fsc->client->fsid, klen);
	return klen;
}

static const struct fscache_cookie_def ceph_fscache_fsid_object_def = {
	.name		= "CEPH.fsid",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= ceph_fscache_session_get_key,
};

int ceph_fscache_register(void)
{
	return fscache_register_netfs(&ceph_cache_netfs);
}

void ceph_fscache_unregister(void)
{
	fscache_unregister_netfs(&ceph_cache_netfs);
}

int ceph_fscache_register_fs(struct ceph_fs_client* fsc)
{
	fsc->fscache = fscache_acquire_cookie(ceph_cache_netfs.primary_index,
					      &ceph_fscache_fsid_object_def,
					      fsc, true);
	if (!fsc->fscache)
		pr_err("Unable to register fsid: %p fscache cookie\n", fsc);

	return 0;
}

static uint16_t ceph_fscache_inode_get_key(const void *cookie_netfs_data,
					   void *buffer, uint16_t maxbuf)
{
	const struct ceph_inode_info* ci = cookie_netfs_data;
	uint16_t klen;

	/* use ceph virtual inode (id + snapshot) */
	klen = sizeof(ci->i_vino);
	if (klen > maxbuf)
		return 0;

	memcpy(buffer, &ci->i_vino, klen);
	return klen;
}

static uint16_t ceph_fscache_inode_get_aux(const void *cookie_netfs_data,
					   void *buffer, uint16_t bufmax)
{
	struct ceph_aux_inode aux;
	const struct ceph_inode_info* ci = cookie_netfs_data;
	const struct inode* inode = &ci->vfs_inode;

	memset(&aux, 0, sizeof(aux));
	aux.version = ci->i_version;
	aux.mtime = inode->i_mtime;
	aux.size = i_size_read(inode);

	memcpy(buffer, &aux, sizeof(aux));

	return sizeof(aux);
}

static void ceph_fscache_inode_get_attr(const void *cookie_netfs_data,
					uint64_t *size)
{
	const struct ceph_inode_info* ci = cookie_netfs_data;
	*size = i_size_read(&ci->vfs_inode);
}

static enum fscache_checkaux ceph_fscache_inode_check_aux(
	void *cookie_netfs_data, const void *data, uint16_t dlen)
{
	struct ceph_aux_inode aux;
	struct ceph_inode_info* ci = cookie_netfs_data;
	struct inode* inode = &ci->vfs_inode;

	if (dlen != sizeof(aux))
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&aux, 0, sizeof(aux));
	aux.version = ci->i_version;
	aux.mtime = inode->i_mtime;
	aux.size = i_size_read(inode);

	if (memcmp(data, &aux, sizeof(aux)) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	dout("ceph inode 0x%p cached okay", ci);
	return FSCACHE_CHECKAUX_OKAY;
}

static void ceph_fscache_inode_now_uncached(void* cookie_netfs_data)
{
	struct ceph_inode_info* ci = cookie_netfs_data;
	struct pagevec pvec;
	pgoff_t first;
	int loop, nr_pages;

	pagevec_init(&pvec, 0);
	first = 0;

	dout("ceph inode 0x%p now uncached", ci);

	while (1) {
		nr_pages = pagevec_lookup(&pvec, ci->vfs_inode.i_mapping, first,
					  PAGEVEC_SIZE - pagevec_count(&pvec));

		if (!nr_pages)
			break;

		for (loop = 0; loop < nr_pages; loop++)
			ClearPageFsCache(pvec.pages[loop]);

		first = pvec.pages[nr_pages - 1]->index + 1;

		pvec.nr = nr_pages;
		pagevec_release(&pvec);
		cond_resched();
	}
}

static const struct fscache_cookie_def ceph_fscache_inode_object_def = {
	.name		= "CEPH.inode",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.get_key	= ceph_fscache_inode_get_key,
	.get_attr	= ceph_fscache_inode_get_attr,
	.get_aux	= ceph_fscache_inode_get_aux,
	.check_aux	= ceph_fscache_inode_check_aux,
	.now_uncached	= ceph_fscache_inode_now_uncached,
};

void ceph_fscache_register_inode_cookie(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);

	/* No caching for filesystem */
	if (fsc->fscache == NULL)
		return;

	/* Only cache for regular files that are read only */
	if (!S_ISREG(inode->i_mode))
		return;

	inode_lock_nested(inode, I_MUTEX_CHILD);
	if (!ci->fscache) {
		ci->fscache = fscache_acquire_cookie(fsc->fscache,
					&ceph_fscache_inode_object_def,
					ci, false);
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
	fscache_relinquish_cookie(cookie, 0);
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
		fscache_disable_cookie(ci->fscache, false);
		fscache_uncache_all_inode_pages(ci->fscache, inode);
	} else {
		fscache_enable_cookie(ci->fscache, ceph_fscache_can_enable,
				inode);
		if (fscache_cookie_enabled(ci->fscache)) {
			dout("fscache_file_set_cookie %p %p enabling cache\n",
			     inode, filp);
		}
	}
}

static void ceph_vfs_readpage_complete(struct page *page, void *data, int error)
{
	if (!error)
		SetPageUptodate(page);
}

static void ceph_vfs_readpage_complete_unlock(struct page *page, void *data, int error)
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
					 ceph_vfs_readpage_complete, NULL,
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
					  ceph_vfs_readpage_complete_unlock,
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

	ret = fscache_write_page(ci->fscache, page, GFP_KERNEL);
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
	fscache_relinquish_cookie(fsc->fscache, 0);
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
		if (fscache_check_consistency(ci->fscache))
			fscache_invalidate(ci->fscache);
		spin_lock(&ci->i_ceph_lock);
		ci->i_fscache_gen = ci->i_rdcache_gen;
		spin_unlock(&ci->i_ceph_lock);
	}
	mutex_unlock(&ci->i_truncate_mutex);
}
