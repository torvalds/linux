/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 */

#ifndef _CEPH_CACHE_H
#define _CEPH_CACHE_H

#include <linux/netfs.h>

#ifdef CONFIG_CEPH_FSCACHE
#include <linux/fscache.h>

int ceph_fscache_register_fs(struct ceph_fs_client* fsc, struct fs_context *fc);
void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc);

void ceph_fscache_register_inode_cookie(struct inode *inode);
void ceph_fscache_unregister_inode_cookie(struct ceph_inode_info* ci);

void ceph_fscache_use_cookie(struct inode *inode, bool will_modify);
void ceph_fscache_unuse_cookie(struct inode *inode, bool update);

void ceph_fscache_update(struct inode *inode);
void ceph_fscache_invalidate(struct inode *inode, bool dio_write);

static inline void ceph_fscache_inode_init(struct ceph_inode_info *ci)
{
	ci->fscache = NULL;
}

static inline struct fscache_cookie *ceph_fscache_cookie(struct ceph_inode_info *ci)
{
	return ci->fscache;
}

static inline void ceph_fscache_resize(struct inode *inode, loff_t to)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct fscache_cookie *cookie = ceph_fscache_cookie(ci);

	if (cookie) {
		ceph_fscache_use_cookie(inode, true);
		fscache_resize_cookie(cookie, to);
		ceph_fscache_unuse_cookie(inode, true);
	}
}

static inline void ceph_fscache_unpin_writeback(struct inode *inode,
						struct writeback_control *wbc)
{
	fscache_unpin_writeback(wbc, ceph_fscache_cookie(ceph_inode(inode)));
}

static inline int ceph_fscache_set_page_dirty(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct ceph_inode_info *ci = ceph_inode(inode);

	return fscache_set_page_dirty(page, ceph_fscache_cookie(ci));
}

static inline int ceph_begin_cache_operation(struct netfs_io_request *rreq)
{
	struct fscache_cookie *cookie = ceph_fscache_cookie(ceph_inode(rreq->inode));

	return fscache_begin_read_operation(&rreq->cache_resources, cookie);
}

static inline bool ceph_is_cache_enabled(struct inode *inode)
{
	return fscache_cookie_enabled(ceph_fscache_cookie(ceph_inode(inode)));
}

static inline void ceph_fscache_note_page_release(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	fscache_note_page_release(ceph_fscache_cookie(ci));
}
#else /* CONFIG_CEPH_FSCACHE */
static inline int ceph_fscache_register_fs(struct ceph_fs_client* fsc,
					   struct fs_context *fc)
{
	return 0;
}

static inline void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc)
{
}

static inline void ceph_fscache_inode_init(struct ceph_inode_info *ci)
{
}

static inline void ceph_fscache_register_inode_cookie(struct inode *inode)
{
}

static inline void ceph_fscache_unregister_inode_cookie(struct ceph_inode_info* ci)
{
}

static inline void ceph_fscache_use_cookie(struct inode *inode, bool will_modify)
{
}

static inline void ceph_fscache_unuse_cookie(struct inode *inode, bool update)
{
}

static inline void ceph_fscache_update(struct inode *inode)
{
}

static inline void ceph_fscache_invalidate(struct inode *inode, bool dio_write)
{
}

static inline struct fscache_cookie *ceph_fscache_cookie(struct ceph_inode_info *ci)
{
	return NULL;
}

static inline void ceph_fscache_resize(struct inode *inode, loff_t to)
{
}

static inline void ceph_fscache_unpin_writeback(struct inode *inode,
						struct writeback_control *wbc)
{
}

static inline int ceph_fscache_set_page_dirty(struct page *page)
{
	return __set_page_dirty_nobuffers(page);
}

static inline bool ceph_is_cache_enabled(struct inode *inode)
{
	return false;
}

static inline int ceph_begin_cache_operation(struct netfs_io_request *rreq)
{
	return -ENOBUFS;
}

static inline void ceph_fscache_note_page_release(struct inode *inode)
{
}
#endif /* CONFIG_CEPH_FSCACHE */

#endif
