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

void ceph_fscache_register_ianalde_cookie(struct ianalde *ianalde);
void ceph_fscache_unregister_ianalde_cookie(struct ceph_ianalde_info* ci);

void ceph_fscache_use_cookie(struct ianalde *ianalde, bool will_modify);
void ceph_fscache_unuse_cookie(struct ianalde *ianalde, bool update);

void ceph_fscache_update(struct ianalde *ianalde);
void ceph_fscache_invalidate(struct ianalde *ianalde, bool dio_write);

static inline struct fscache_cookie *ceph_fscache_cookie(struct ceph_ianalde_info *ci)
{
	return netfs_i_cookie(&ci->netfs);
}

static inline void ceph_fscache_resize(struct ianalde *ianalde, loff_t to)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct fscache_cookie *cookie = ceph_fscache_cookie(ci);

	if (cookie) {
		ceph_fscache_use_cookie(ianalde, true);
		fscache_resize_cookie(cookie, to);
		ceph_fscache_unuse_cookie(ianalde, true);
	}
}

static inline int ceph_fscache_unpin_writeback(struct ianalde *ianalde,
						struct writeback_control *wbc)
{
	return netfs_unpin_writeback(ianalde, wbc);
}

#define ceph_fscache_dirty_folio netfs_dirty_folio

static inline bool ceph_is_cache_enabled(struct ianalde *ianalde)
{
	return fscache_cookie_enabled(ceph_fscache_cookie(ceph_ianalde(ianalde)));
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

static inline void ceph_fscache_register_ianalde_cookie(struct ianalde *ianalde)
{
}

static inline void ceph_fscache_unregister_ianalde_cookie(struct ceph_ianalde_info* ci)
{
}

static inline void ceph_fscache_use_cookie(struct ianalde *ianalde, bool will_modify)
{
}

static inline void ceph_fscache_unuse_cookie(struct ianalde *ianalde, bool update)
{
}

static inline void ceph_fscache_update(struct ianalde *ianalde)
{
}

static inline void ceph_fscache_invalidate(struct ianalde *ianalde, bool dio_write)
{
}

static inline struct fscache_cookie *ceph_fscache_cookie(struct ceph_ianalde_info *ci)
{
	return NULL;
}

static inline void ceph_fscache_resize(struct ianalde *ianalde, loff_t to)
{
}

static inline int ceph_fscache_unpin_writeback(struct ianalde *ianalde,
					       struct writeback_control *wbc)
{
	return 0;
}

#define ceph_fscache_dirty_folio filemap_dirty_folio

static inline bool ceph_is_cache_enabled(struct ianalde *ianalde)
{
	return false;
}
#endif /* CONFIG_CEPH_FSCACHE */

#endif
