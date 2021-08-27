/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NFS filesystem cache interface definitions
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _NFS_FSCACHE_H
#define _NFS_FSCACHE_H

#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/fscache.h>
#include <linux/iversion.h>

#ifdef CONFIG_NFS_FSCACHE

/*
 * Definition of the auxiliary data attached to NFS inode storage objects
 * within the cache.
 *
 * The contents of this struct are recorded in the on-disk local cache in the
 * auxiliary data attached to the data storage object backing an inode.  This
 * permits coherency to be managed when a new inode binds to an already extant
 * cache object.
 */
struct nfs_fscache_inode_auxdata {
	s64	mtime_sec;
	s64	mtime_nsec;
	s64	ctime_sec;
	s64	ctime_nsec;
	u64	change_attr;
};

/*
 * fscache.c
 */
extern int nfs_fscache_get_super_cookie(struct super_block *, const char *, int);
extern void nfs_fscache_release_super_cookie(struct super_block *);

extern void nfs_fscache_init_inode(struct inode *);
extern void nfs_fscache_clear_inode(struct inode *);
extern void nfs_fscache_open_file(struct inode *, struct file *);
extern void nfs_fscache_release_file(struct inode *, struct file *);

extern int __nfs_readpage_from_fscache(struct inode *, struct page *);
extern void __nfs_read_completion_to_fscache(struct nfs_pgio_header *hdr,
					     unsigned long bytes);
extern void __nfs_readpage_to_fscache(struct inode *, struct page *);

static inline int nfs_fscache_release_page(struct page *page, gfp_t gfp)
{
	if (PageFsCache(page)) {
		if (!gfpflags_allow_blocking(gfp) || !(gfp & __GFP_FS))
			return false;
		wait_on_page_fscache(page);
		fscache_note_page_release(nfs_i_fscache(page->mapping->host));
		nfs_inc_fscache_stats(page->mapping->host,
				      NFSIOS_FSCACHE_PAGES_UNCACHED);
	}
	return true;
}

/*
 * Retrieve a page from an inode data storage object.
 */
static inline int nfs_readpage_from_fscache(struct inode *inode,
					    struct page *page)
{
	if (NFS_I(inode)->fscache)
		return __nfs_readpage_from_fscache(inode, page);
	return -ENOBUFS;
}

/*
 * Store a page newly fetched from the server in an inode data storage object
 * in the cache.
 */
static inline void nfs_readpage_to_fscache(struct inode *inode,
					   struct page *page)
{
	if (NFS_I(inode)->fscache)
		__nfs_readpage_to_fscache(inode, page);
}

static inline void nfs_fscache_update_auxdata(struct nfs_fscache_inode_auxdata *auxdata,
					      struct nfs_inode *nfsi)
{
	memset(auxdata, 0, sizeof(*auxdata));
	auxdata->mtime_sec  = nfsi->vfs_inode.i_mtime.tv_sec;
	auxdata->mtime_nsec = nfsi->vfs_inode.i_mtime.tv_nsec;
	auxdata->ctime_sec  = nfsi->vfs_inode.i_ctime.tv_sec;
	auxdata->ctime_nsec = nfsi->vfs_inode.i_ctime.tv_nsec;

	if (NFS_SERVER(&nfsi->vfs_inode)->nfs_client->rpc_ops->version == 4)
		auxdata->change_attr = inode_peek_iversion_raw(&nfsi->vfs_inode);
}

/*
 * Invalidate the contents of fscache for this inode.  This will not sleep.
 */
static inline void nfs_fscache_invalidate(struct inode *inode, int flags)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfsi->fscache) {
		nfs_fscache_update_auxdata(&auxdata, nfsi);
		fscache_invalidate(nfsi->fscache, &auxdata,
				   i_size_read(&nfsi->vfs_inode), flags);
	}
}

/*
 * indicate the client caching state as readable text
 */
static inline const char *nfs_server_fscache_state(struct nfs_server *server)
{
	if (server->fscache)
		return "yes";
	return "no ";
}

#else /* CONFIG_NFS_FSCACHE */
static inline void nfs_fscache_release_super_cookie(struct super_block *sb) {}

static inline void nfs_fscache_init_inode(struct inode *inode) {}
static inline void nfs_fscache_clear_inode(struct inode *inode) {}
static inline void nfs_fscache_open_file(struct inode *inode,
					 struct file *filp) {}
static inline void nfs_fscache_release_file(struct inode *inode, struct file *file) {}

static inline int nfs_fscache_release_page(struct page *page, gfp_t gfp)
{
	return 1; /* True: may release page */
}
static inline int nfs_readpage_from_fscache(struct inode *inode,
					    struct page *page)
{
	return -ENOBUFS;
}
static inline void nfs_readpage_to_fscache(struct inode *inode,
					   struct page *page) {}


static inline void nfs_fscache_invalidate(struct inode *inode, int flags) {}

static inline const char *nfs_server_fscache_state(struct nfs_server *server)
{
	return "no ";
}

#endif /* CONFIG_NFS_FSCACHE */
#endif /* _NFS_FSCACHE_H */
