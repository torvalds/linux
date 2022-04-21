/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   CIFS filesystem cache interface definitions
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Authors(s): Suresh Jayaraman (sjayaraman@suse.de>
 *
 */
#ifndef _CIFS_FSCACHE_H
#define _CIFS_FSCACHE_H

#include <linux/swap.h>
#include <linux/fscache.h>

#include "cifsglob.h"

/*
 * Coherency data attached to CIFS volume within the cache
 */
struct cifs_fscache_volume_coherency_data {
	__le64	resource_id;		/* unique server resource id */
	__le64	vol_create_time;
	__le32	vol_serial_number;
} __packed;

/*
 * Coherency data attached to CIFS inode within the cache.
 */
struct cifs_fscache_inode_coherency_data {
	__le64 last_write_time_sec;
	__le64 last_change_time_sec;
	__le32 last_write_time_nsec;
	__le32 last_change_time_nsec;
};

#ifdef CONFIG_CIFS_FSCACHE

/*
 * fscache.c
 */
extern int cifs_fscache_get_super_cookie(struct cifs_tcon *);
extern void cifs_fscache_release_super_cookie(struct cifs_tcon *);

extern void cifs_fscache_get_inode_cookie(struct inode *inode);
extern void cifs_fscache_release_inode_cookie(struct inode *);
extern void cifs_fscache_unuse_inode_cookie(struct inode *inode, bool update);

static inline
void cifs_fscache_fill_coherency(struct inode *inode,
				 struct cifs_fscache_inode_coherency_data *cd)
{
	struct cifsInodeInfo *cifsi = CIFS_I(inode);

	memset(cd, 0, sizeof(*cd));
	cd->last_write_time_sec   = cpu_to_le64(cifsi->vfs_inode.i_mtime.tv_sec);
	cd->last_write_time_nsec  = cpu_to_le32(cifsi->vfs_inode.i_mtime.tv_nsec);
	cd->last_change_time_sec  = cpu_to_le64(cifsi->vfs_inode.i_ctime.tv_sec);
	cd->last_change_time_nsec = cpu_to_le32(cifsi->vfs_inode.i_ctime.tv_nsec);
}


static inline struct fscache_cookie *cifs_inode_cookie(struct inode *inode)
{
	return netfs_i_cookie(inode);
}

static inline void cifs_invalidate_cache(struct inode *inode, unsigned int flags)
{
	struct cifs_fscache_inode_coherency_data cd;

	cifs_fscache_fill_coherency(inode, &cd);
	fscache_invalidate(cifs_inode_cookie(inode), &cd,
			   i_size_read(inode), flags);
}

extern int __cifs_fscache_query_occupancy(struct inode *inode,
					  pgoff_t first, unsigned int nr_pages,
					  pgoff_t *_data_first,
					  unsigned int *_data_nr_pages);

static inline int cifs_fscache_query_occupancy(struct inode *inode,
					       pgoff_t first, unsigned int nr_pages,
					       pgoff_t *_data_first,
					       unsigned int *_data_nr_pages)
{
	if (!cifs_inode_cookie(inode))
		return -ENOBUFS;
	return __cifs_fscache_query_occupancy(inode, first, nr_pages,
					      _data_first, _data_nr_pages);
}

extern int __cifs_readpage_from_fscache(struct inode *pinode, struct page *ppage);
extern void __cifs_readpage_to_fscache(struct inode *pinode, struct page *ppage);


static inline int cifs_readpage_from_fscache(struct inode *inode,
					     struct page *page)
{
	if (cifs_inode_cookie(inode))
		return __cifs_readpage_from_fscache(inode, page);
	return -ENOBUFS;
}

static inline void cifs_readpage_to_fscache(struct inode *inode,
					    struct page *page)
{
	if (cifs_inode_cookie(inode))
		__cifs_readpage_to_fscache(inode, page);
}

static inline int cifs_fscache_release_page(struct page *page, gfp_t gfp)
{
	if (PageFsCache(page)) {
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return false;
		wait_on_page_fscache(page);
		fscache_note_page_release(cifs_inode_cookie(page->mapping->host));
	}
	return true;
}

#else /* CONFIG_CIFS_FSCACHE */
static inline
void cifs_fscache_fill_coherency(struct inode *inode,
				 struct cifs_fscache_inode_coherency_data *cd)
{
}

static inline int cifs_fscache_get_super_cookie(struct cifs_tcon *tcon) { return 0; }
static inline void cifs_fscache_release_super_cookie(struct cifs_tcon *tcon) {}

static inline void cifs_fscache_get_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_release_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_unuse_inode_cookie(struct inode *inode, bool update) {}
static inline struct fscache_cookie *cifs_inode_cookie(struct inode *inode) { return NULL; }
static inline void cifs_invalidate_cache(struct inode *inode, unsigned int flags) {}

static inline int cifs_fscache_query_occupancy(struct inode *inode,
					       pgoff_t first, unsigned int nr_pages,
					       pgoff_t *_data_first,
					       unsigned int *_data_nr_pages)
{
	*_data_first = ULONG_MAX;
	*_data_nr_pages = 0;
	return -ENOBUFS;
}

static inline int
cifs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	return -ENOBUFS;
}

static inline
void cifs_readpage_to_fscache(struct inode *inode, struct page *page) {}

static inline int nfs_fscache_release_page(struct page *page, gfp_t gfp)
{
	return true; /* May release page */
}

#endif /* CONFIG_CIFS_FSCACHE */

#endif /* _CIFS_FSCACHE_H */
