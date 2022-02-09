// SPDX-License-Identifier: GPL-2.0-or-later
/* NFS filesystem cache interface
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/iversion.h>

#include "internal.h"
#include "iostat.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_FSCACHE

#define NFS_MAX_KEY_LEN 1000

static bool nfs_append_int(char *key, int *_len, unsigned long long x)
{
	if (*_len > NFS_MAX_KEY_LEN)
		return false;
	if (x == 0)
		key[(*_len)++] = ',';
	else
		*_len += sprintf(key + *_len, ",%llx", x);
	return true;
}

/*
 * Get the per-client index cookie for an NFS client if the appropriate mount
 * flag was set
 * - We always try and get an index cookie for the client, but get filehandle
 *   cookies on a per-superblock basis, depending on the mount flags
 */
static bool nfs_fscache_get_client_key(struct nfs_client *clp,
				       char *key, int *_len)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &clp->cl_addr;
	const struct sockaddr_in *sin = (struct sockaddr_in *) &clp->cl_addr;

	*_len += snprintf(key + *_len, NFS_MAX_KEY_LEN - *_len,
			  ",%u.%u,%x",
			  clp->rpc_ops->version,
			  clp->cl_minorversion,
			  clp->cl_addr.ss_family);

	switch (clp->cl_addr.ss_family) {
	case AF_INET:
		if (!nfs_append_int(key, _len, sin->sin_port) ||
		    !nfs_append_int(key, _len, sin->sin_addr.s_addr))
			return false;
		return true;

	case AF_INET6:
		if (!nfs_append_int(key, _len, sin6->sin6_port) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[0]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[1]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[2]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[3]))
			return false;
		return true;

	default:
		printk(KERN_WARNING "NFS: Unknown network family '%d'\n",
		       clp->cl_addr.ss_family);
		return false;
	}
}

/*
 * Get the cache cookie for an NFS superblock.
 *
 * The default uniquifier is just an empty string, but it may be overridden
 * either by the 'fsc=xxx' option to mount, or by inheriting it from the parent
 * superblock across an automount point of some nature.
 */
int nfs_fscache_get_super_cookie(struct super_block *sb, const char *uniq, int ulen)
{
	struct fscache_volume *vcookie;
	struct nfs_server *nfss = NFS_SB(sb);
	unsigned int len = 3;
	char *key;

	if (uniq) {
		nfss->fscache_uniq = kmemdup_nul(uniq, ulen, GFP_KERNEL);
		if (!nfss->fscache_uniq)
			return -ENOMEM;
	}

	key = kmalloc(NFS_MAX_KEY_LEN + 24, GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	memcpy(key, "nfs", 3);
	if (!nfs_fscache_get_client_key(nfss->nfs_client, key, &len) ||
	    !nfs_append_int(key, &len, nfss->fsid.major) ||
	    !nfs_append_int(key, &len, nfss->fsid.minor) ||
	    !nfs_append_int(key, &len, sb->s_flags & NFS_SB_MASK) ||
	    !nfs_append_int(key, &len, nfss->flags) ||
	    !nfs_append_int(key, &len, nfss->rsize) ||
	    !nfs_append_int(key, &len, nfss->wsize) ||
	    !nfs_append_int(key, &len, nfss->acregmin) ||
	    !nfs_append_int(key, &len, nfss->acregmax) ||
	    !nfs_append_int(key, &len, nfss->acdirmin) ||
	    !nfs_append_int(key, &len, nfss->acdirmax) ||
	    !nfs_append_int(key, &len, nfss->client->cl_auth->au_flavor))
		goto out;

	if (ulen > 0) {
		if (ulen > NFS_MAX_KEY_LEN - len)
			goto out;
		key[len++] = ',';
		memcpy(key + len, uniq, ulen);
		len += ulen;
	}
	key[len] = 0;

	/* create a cache index for looking up filehandles */
	vcookie = fscache_acquire_volume(key,
					 NULL, /* preferred_cache */
					 NULL, 0 /* coherency_data */);
	dfprintk(FSCACHE, "NFS: get superblock cookie (0x%p/0x%p)\n",
		 nfss, vcookie);
	if (IS_ERR(vcookie)) {
		if (vcookie != ERR_PTR(-EBUSY)) {
			kfree(key);
			return PTR_ERR(vcookie);
		}
		pr_err("NFS: Cache volume key already in use (%s)\n", key);
		vcookie = NULL;
	}
	nfss->fscache = vcookie;

out:
	kfree(key);
	return 0;
}

/*
 * release a per-superblock cookie
 */
void nfs_fscache_release_super_cookie(struct super_block *sb)
{
	struct nfs_server *nfss = NFS_SB(sb);

	dfprintk(FSCACHE, "NFS: releasing superblock cookie (0x%p/0x%p)\n",
		 nfss, nfss->fscache);

	fscache_relinquish_volume(nfss->fscache, NULL, false);
	nfss->fscache = NULL;
	kfree(nfss->fscache_uniq);
}

/*
 * Initialise the per-inode cache cookie pointer for an NFS inode.
 */
void nfs_fscache_init_inode(struct inode *inode)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_server *nfss = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);

	nfsi->fscache = NULL;
	if (!(nfss->fscache && S_ISREG(inode->i_mode)))
		return;

	nfs_fscache_update_auxdata(&auxdata, nfsi);

	nfsi->fscache = fscache_acquire_cookie(NFS_SB(inode->i_sb)->fscache,
					       0,
					       nfsi->fh.data, /* index_key */
					       nfsi->fh.size,
					       &auxdata,      /* aux_data */
					       sizeof(auxdata),
					       i_size_read(&nfsi->vfs_inode));
}

/*
 * Release a per-inode cookie.
 */
void nfs_fscache_clear_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct fscache_cookie *cookie = nfs_i_fscache(inode);

	dfprintk(FSCACHE, "NFS: clear cookie (0x%p/0x%p)\n", nfsi, cookie);

	fscache_relinquish_cookie(cookie, false);
	nfsi->fscache = NULL;
}

/*
 * Enable or disable caching for a file that is being opened as appropriate.
 * The cookie is allocated when the inode is initialised, but is not enabled at
 * that time.  Enablement is deferred to file-open time to avoid stat() and
 * access() thrashing the cache.
 *
 * For now, with NFS, only regular files that are open read-only will be able
 * to use the cache.
 *
 * We enable the cache for an inode if we open it read-only and it isn't
 * currently open for writing.  We disable the cache if the inode is open
 * write-only.
 *
 * The caller uses the file struct to pin i_writecount on the inode before
 * calling us when a file is opened for writing, so we can make use of that.
 *
 * Note that this may be invoked multiple times in parallel by parallel
 * nfs_open() functions.
 */
void nfs_fscache_open_file(struct inode *inode, struct file *filp)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct fscache_cookie *cookie = nfs_i_fscache(inode);
	bool open_for_write = inode_is_open_for_write(inode);

	if (!fscache_cookie_valid(cookie))
		return;

	fscache_use_cookie(cookie, open_for_write);
	if (open_for_write) {
		dfprintk(FSCACHE, "NFS: nfsi 0x%p disabling cache\n", nfsi);
		nfs_fscache_update_auxdata(&auxdata, nfsi);
		fscache_invalidate(cookie, &auxdata, i_size_read(inode),
				   FSCACHE_INVAL_DIO_WRITE);
	}
}
EXPORT_SYMBOL_GPL(nfs_fscache_open_file);

void nfs_fscache_release_file(struct inode *inode, struct file *filp)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);
	struct fscache_cookie *cookie = nfs_i_fscache(inode);

	if (fscache_cookie_valid(cookie)) {
		nfs_fscache_update_auxdata(&auxdata, nfsi);
		fscache_unuse_cookie(cookie, &auxdata, NULL);
	}
}

/*
 * Fallback page reading interface.
 */
static int fscache_fallback_read_page(struct inode *inode, struct page *page)
{
	struct netfs_cache_resources cres;
	struct fscache_cookie *cookie = nfs_i_fscache(inode);
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
	struct fscache_cookie *cookie = nfs_i_fscache(inode);
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
 * Retrieve a page from fscache
 */
int __nfs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	int ret;

	dfprintk(FSCACHE,
		 "NFS: readpage_from_fscache(fsc:%p/p:%p(i:%lx f:%lx)/0x%p)\n",
		 nfs_i_fscache(inode), page, page->index, page->flags, inode);

	if (PageChecked(page)) {
		dfprintk(FSCACHE, "NFS:    readpage_from_fscache: PageChecked\n");
		ClearPageChecked(page);
		return 1;
	}

	ret = fscache_fallback_read_page(inode, page);
	if (ret < 0) {
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_FAIL);
		dfprintk(FSCACHE,
			 "NFS:    readpage_from_fscache failed %d\n", ret);
		SetPageChecked(page);
		return ret;
	}

	/* Read completed synchronously */
	dfprintk(FSCACHE, "NFS:    readpage_from_fscache: read successful\n");
	nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_READ_OK);
	SetPageUptodate(page);
	return 0;
}

/*
 * Store a newly fetched page in fscache.  We can be certain there's no page
 * stored in the cache as yet otherwise we would've read it from there.
 */
void __nfs_readpage_to_fscache(struct inode *inode, struct page *page)
{
	int ret;

	dfprintk(FSCACHE,
		 "NFS: readpage_to_fscache(fsc:%p/p:%p(i:%lx f:%lx))\n",
		 nfs_i_fscache(inode), page, page->index, page->flags);

	ret = fscache_fallback_write_page(inode, page, true);

	dfprintk(FSCACHE,
		 "NFS:     readpage_to_fscache: p:%p(i:%lu f:%lx) ret %d\n",
		 page, page->index, page->flags, ret);

	if (ret != 0) {
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_WRITTEN_FAIL);
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_UNCACHED);
	} else {
		nfs_inc_fscache_stats(inode, NFSIOS_FSCACHE_PAGES_WRITTEN_OK);
	}
}
