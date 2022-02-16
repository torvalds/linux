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
 * Retrieve a page from FS-Cache
 */
int __cifs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	cifs_dbg(FYI, "%s: (fsc:%p, p:%p, i:0x%p\n",
		 __func__, CIFS_I(inode)->fscache, page, inode);
	return -ENOBUFS; // Needs conversion to using netfslib
}

/*
 * Retrieve a set of pages from FS-Cache
 */
int __cifs_readpages_from_fscache(struct inode *inode,
				struct address_space *mapping,
				struct list_head *pages,
				unsigned *nr_pages)
{
	cifs_dbg(FYI, "%s: (0x%p/%u/0x%p)\n",
		 __func__, CIFS_I(inode)->fscache, *nr_pages, inode);
	return -ENOBUFS; // Needs conversion to using netfslib
}

void __cifs_readpage_to_fscache(struct inode *inode, struct page *page)
{
	struct cifsInodeInfo *cifsi = CIFS_I(inode);

	WARN_ON(!cifsi->fscache);

	cifs_dbg(FYI, "%s: (fsc: %p, p: %p, i: %p)\n",
		 __func__, cifsi->fscache, page, inode);

	// Needs conversion to using netfslib
}
