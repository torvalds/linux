// SPDX-License-Identifier: LGPL-2.1
/*
 *   CIFS filesystem cache interface
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Author(s): Suresh Jayaraman <sjayaraman@suse.de>
 *
 */
#include "fscache.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifsproto.h"

static void cifs_fscache_fill_volume_coherency(
	struct cifs_tcon *tcon,
	struct cifs_fscache_volume_coherency_data *cd)
{
	memset(cd, 0, sizeof(*cd));
	cd->resource_id		= cpu_to_le64(tcon->resource_id);
	cd->vol_create_time	= tcon->vol_create_time;
	cd->vol_serial_number	= cpu_to_le32(tcon->vol_serial_number);
}

int cifs_fscache_get_super_cookie(struct cifs_tcon *tcon)
{
	struct cifs_fscache_volume_coherency_data cd;
	struct TCP_Server_Info *server = tcon->ses->server;
	struct fscache_volume *vcookie;
	const struct sockaddr *sa = (struct sockaddr *)&server->dstaddr;
	size_t slen, i;
	char *sharename;
	char *key;
	int ret = -ENOMEM;

	tcon->fscache = NULL;
	switch (sa->sa_family) {
	case AF_INET:
	case AF_INET6:
		break;
	default:
		cifs_dbg(VFS, "Unknown network family '%d'\n", sa->sa_family);
		return -EINVAL;
	}

	memset(&key, 0, sizeof(key));

	sharename = extract_sharename(tcon->treeName);
	if (IS_ERR(sharename)) {
		cifs_dbg(FYI, "%s: couldn't extract sharename\n", __func__);
		return -EINVAL;
	}

	slen = strlen(sharename);
	for (i = 0; i < slen; i++)
		if (sharename[i] == '/')
			sharename[i] = ';';

	key = kasprintf(GFP_KERNEL, "cifs,%pISpc,%s", sa, sharename);
	if (!key)
		goto out;

	cifs_fscache_fill_volume_coherency(tcon, &cd);
	vcookie = fscache_acquire_volume(key,
					 NULL, /* preferred_cache */
					 &cd, sizeof(cd));
	cifs_dbg(FYI, "%s: (%s/0x%p)\n", __func__, key, vcookie);
	if (IS_ERR(vcookie)) {
		if (vcookie != ERR_PTR(-EBUSY)) {
			ret = PTR_ERR(vcookie);
			goto out_2;
		}
		pr_err("Cache volume key already in use (%s)\n", key);
		vcookie = NULL;
	}

	tcon->fscache = vcookie;
	ret = 0;
out_2:
	kfree(key);
out:
	kfree(sharename);
	return ret;
}

void cifs_fscache_release_super_cookie(struct cifs_tcon *tcon)
{
	struct cifs_fscache_volume_coherency_data cd;

	cifs_dbg(FYI, "%s: (0x%p)\n", __func__, tcon->fscache);

	cifs_fscache_fill_volume_coherency(tcon, &cd);
	fscache_relinquish_volume(tcon->fscache, &cd, false);
	tcon->fscache = NULL;
}

void cifs_fscache_get_inode_cookie(struct inode *inode)
{
	struct cifs_fscache_inode_coherency_data cd;
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	cifs_fscache_fill_coherency(&cifsi->vfs_inode, &cd);

	cifsi->fscache =
		fscache_acquire_cookie(tcon->fscache, 0,
				       &cifsi->uniqueid, sizeof(cifsi->uniqueid),
				       &cd, sizeof(cd),
				       i_size_read(&cifsi->vfs_inode));
}

void cifs_fscache_unuse_inode_cookie(struct inode *inode, bool update)
{
	if (update) {
		struct cifs_fscache_inode_coherency_data cd;
		loff_t i_size = i_size_read(inode);

		cifs_fscache_fill_coherency(inode, &cd);
		fscache_unuse_cookie(cifs_inode_cookie(inode), &cd, &i_size);
	} else {
		fscache_unuse_cookie(cifs_inode_cookie(inode), NULL, NULL);
	}
}

void cifs_fscache_release_inode_cookie(struct inode *inode)
{
	struct cifsInodeInfo *cifsi = CIFS_I(inode);

	if (cifsi->fscache) {
		cifs_dbg(FYI, "%s: (0x%p)\n", __func__, cifsi->fscache);
		fscache_relinquish_cookie(cifsi->fscache, false);
		cifsi->fscache = NULL;
	}
}

/*
 * Fallback page reading interface.
 */
static int fscache_fallback_read_page(struct inode *inode, struct page *page)
{
	struct netfs_cache_resources cres;
	struct fscache_cookie *cookie = cifs_inode_cookie(inode);
	struct iov_iter iter;
	struct bio_vec bvec[1];
	int ret;

	memset(&cres, 0, sizeof(cres));
	bvec[0].bv_page		= page;
	bvec[0].bv_offset	= 0;
	bvec[0].bv_len		= PAGE_SIZE;
	iov_iter_bvec(&iter, READ, bvec, ARRAY_SIZE(bvec), PAGE_SIZE);

	ret = fscache_begin_read_operation(&cres, cookie);
	if (ret < 0)
		return ret;

	ret = fscache_read(&cres, page_offset(page), &iter, NETFS_READ_HOLE_FAIL,
			   NULL, NULL);
	fscache_end_operation(&cres);
	return ret;
}

/*
 * Fallback page writing interface.
 */
static int fscache_fallback_write_page(struct inode *inode, struct page *page,
				       bool no_space_allocated_yet)
{
	struct netfs_cache_resources cres;
	struct fscache_cookie *cookie = cifs_inode_cookie(inode);
	struct iov_iter iter;
	struct bio_vec bvec[1];
	loff_t start = page_offset(page);
	size_t len = PAGE_SIZE;
	int ret;

	memset(&cres, 0, sizeof(cres));
	bvec[0].bv_page		= page;
	bvec[0].bv_offset	= 0;
	bvec[0].bv_len		= PAGE_SIZE;
	iov_iter_bvec(&iter, WRITE, bvec, ARRAY_SIZE(bvec), PAGE_SIZE);

	ret = fscache_begin_write_operation(&cres, cookie);
	if (ret < 0)
		return ret;

	ret = cres.ops->prepare_write(&cres, &start, &len, i_size_read(inode),
				      no_space_allocated_yet);
	if (ret == 0)
		ret = fscache_write(&cres, page_offset(page), &iter, NULL, NULL);
	fscache_end_operation(&cres);
	return ret;
}

/*
 * Retrieve a page from FS-Cache
 */
int __cifs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	int ret;

	cifs_dbg(FYI, "%s: (fsc:%p, p:%p, i:0x%p\n",
		 __func__, cifs_inode_cookie(inode), page, inode);

	ret = fscache_fallback_read_page(inode, page);
	if (ret < 0)
		return ret;

	/* Read completed synchronously */
	SetPageUptodate(page);
	return 0;
}

void __cifs_readpage_to_fscache(struct inode *inode, struct page *page)
{
	cifs_dbg(FYI, "%s: (fsc: %p, p: %p, i: %p)\n",
		 __func__, cifs_inode_cookie(inode), page, inode);

	fscache_fallback_write_page(inode, page, true);
}

/*
 * Query the cache occupancy.
 */
int __cifs_fscache_query_occupancy(struct inode *inode,
				   pgoff_t first, unsigned int nr_pages,
				   pgoff_t *_data_first,
				   unsigned int *_data_nr_pages)
{
	struct netfs_cache_resources cres;
	struct fscache_cookie *cookie = cifs_inode_cookie(inode);
	loff_t start, data_start;
	size_t len, data_len;
	int ret;

	ret = fscache_begin_read_operation(&cres, cookie);
	if (ret < 0)
		return ret;

	start = first * PAGE_SIZE;
	len = nr_pages * PAGE_SIZE;
	ret = cres.ops->query_occupancy(&cres, start, len, PAGE_SIZE,
					&data_start, &data_len);
	if (ret == 0) {
		*_data_first = data_start / PAGE_SIZE;
		*_data_nr_pages = len / PAGE_SIZE;
	}

	fscache_end_operation(&cres);
	return ret;
}
