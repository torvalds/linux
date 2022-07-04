/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NFS filesystem cache interface definitions
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _NFS_FSCACHE_H
#define _NFS_FSCACHE_H

#include <linux/swap.h>
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

extern int __nfs_fscache_read_page(struct inode *, struct page *);
extern void __nfs_fscache_write_page(struct inode *, struct page *);

static inline bool nfs_fscache_release_folio(struct folio *folio, gfp_t gfp)
{
	if (folio_test_fscache(folio)) {
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return false;
		folio_wait_fscache(folio);
		fscache_note_page_release(nfs_i_fscache(folio->mapping->host));
		nfs_inc_fscache_stats(folio->mapping->host,
				      NFSIOS_FSCACHE_PAGES_UNCACHED);
	}
	return true;
}

/*
 * Retrieve a page from an inode data storage object.
 */
static inline int nfs_fscache_read_page(struct inode *inode, struct page *page)
{
	if (nfs_i_fscache(inode))
		return __nfs_fscache_read_page(inode, page);
	return -ENOBUFS;
}

/*
 * Store a page newly fetched from the server in an inode data storage object
 * in the cache.
 */
static inline void nfs_fscache_write_page(struct inode *inode,
					   struct page *page)
{
	if (nfs_i_fscache(inode))
		__nfs_fscache_write_page(inode, page);
}

static inline void nfs_fscache_update_auxdata(struct nfs_fscache_inode_auxdata *auxdata,
					      struct inode *inode)
{
	memset(auxdata, 0, sizeof(*auxdata));
	auxdata->mtime_sec  = inode->i_mtime.tv_sec;
	auxdata->mtime_nsec = inode->i_mtime.tv_nsec;
	auxdata->ctime_sec  = inode->i_ctime.tv_sec;
	auxdata->ctime_nsec = inode->i_ctime.tv_nsec;

	if (NFS_SERVER(inode)->nfs_client->rpc_ops->version == 4)
		auxdata->change_attr = inode_peek_iversion_raw(inode);
}

/*
 * Invalidate the contents of fscache for this inode.  This will not sleep.
 */
static inline void nfs_fscache_invalidate(struct inode *inode, int flags)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfsi->fscache) {
		nfs_fscache_update_auxdata(&auxdata, inode);
		fscache_invalidate(nfsi->fscache, &auxdata,
				   i_size_read(inode), flags);
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

static inline bool nfs_fscache_release_folio(struct folio *folio, gfp_t gfp)
{
	return true; /* may release folio */
}
static inline int nfs_fscache_read_page(struct inode *inode, struct page *page)
{
	return -ENOBUFS;
}
static inline void nfs_fscache_write_page(struct inode *inode, struct page *page) {}
static inline void nfs_fscache_invalidate(struct inode *inode, int flags) {}

static inline const char *nfs_server_fscache_state(struct nfs_server *server)
{
	return "no ";
}

#endif /* CONFIG_NFS_FSCACHE */
#endif /* _NFS_FSCACHE_H */
