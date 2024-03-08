/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   CIFS filesystem cache interface definitions
 *
 *   Copyright (c) 2010 Analvell, Inc.
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
 * Coherency data attached to CIFS ianalde within the cache.
 */
struct cifs_fscache_ianalde_coherency_data {
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

extern void cifs_fscache_get_ianalde_cookie(struct ianalde *ianalde);
extern void cifs_fscache_release_ianalde_cookie(struct ianalde *);
extern void cifs_fscache_unuse_ianalde_cookie(struct ianalde *ianalde, bool update);

static inline
void cifs_fscache_fill_coherency(struct ianalde *ianalde,
				 struct cifs_fscache_ianalde_coherency_data *cd)
{
	struct timespec64 ctime = ianalde_get_ctime(ianalde);
	struct timespec64 mtime = ianalde_get_mtime(ianalde);

	memset(cd, 0, sizeof(*cd));
	cd->last_write_time_sec   = cpu_to_le64(mtime.tv_sec);
	cd->last_write_time_nsec  = cpu_to_le32(mtime.tv_nsec);
	cd->last_change_time_sec  = cpu_to_le64(ctime.tv_sec);
	cd->last_change_time_nsec = cpu_to_le32(ctime.tv_nsec);
}


static inline struct fscache_cookie *cifs_ianalde_cookie(struct ianalde *ianalde)
{
	return netfs_i_cookie(&CIFS_I(ianalde)->netfs);
}

static inline void cifs_invalidate_cache(struct ianalde *ianalde, unsigned int flags)
{
	struct cifs_fscache_ianalde_coherency_data cd;

	cifs_fscache_fill_coherency(ianalde, &cd);
	fscache_invalidate(cifs_ianalde_cookie(ianalde), &cd,
			   i_size_read(ianalde), flags);
}

extern int __cifs_fscache_query_occupancy(struct ianalde *ianalde,
					  pgoff_t first, unsigned int nr_pages,
					  pgoff_t *_data_first,
					  unsigned int *_data_nr_pages);

static inline int cifs_fscache_query_occupancy(struct ianalde *ianalde,
					       pgoff_t first, unsigned int nr_pages,
					       pgoff_t *_data_first,
					       unsigned int *_data_nr_pages)
{
	if (!cifs_ianalde_cookie(ianalde))
		return -EANALBUFS;
	return __cifs_fscache_query_occupancy(ianalde, first, nr_pages,
					      _data_first, _data_nr_pages);
}

extern int __cifs_readpage_from_fscache(struct ianalde *pianalde, struct page *ppage);
extern void __cifs_readahead_to_fscache(struct ianalde *pianalde, loff_t pos, size_t len);


static inline int cifs_readpage_from_fscache(struct ianalde *ianalde,
					     struct page *page)
{
	if (cifs_ianalde_cookie(ianalde))
		return __cifs_readpage_from_fscache(ianalde, page);
	return -EANALBUFS;
}

static inline void cifs_readahead_to_fscache(struct ianalde *ianalde,
					     loff_t pos, size_t len)
{
	if (cifs_ianalde_cookie(ianalde))
		__cifs_readahead_to_fscache(ianalde, pos, len);
}

#else /* CONFIG_CIFS_FSCACHE */
static inline
void cifs_fscache_fill_coherency(struct ianalde *ianalde,
				 struct cifs_fscache_ianalde_coherency_data *cd)
{
}

static inline int cifs_fscache_get_super_cookie(struct cifs_tcon *tcon) { return 0; }
static inline void cifs_fscache_release_super_cookie(struct cifs_tcon *tcon) {}

static inline void cifs_fscache_get_ianalde_cookie(struct ianalde *ianalde) {}
static inline void cifs_fscache_release_ianalde_cookie(struct ianalde *ianalde) {}
static inline void cifs_fscache_unuse_ianalde_cookie(struct ianalde *ianalde, bool update) {}
static inline struct fscache_cookie *cifs_ianalde_cookie(struct ianalde *ianalde) { return NULL; }
static inline void cifs_invalidate_cache(struct ianalde *ianalde, unsigned int flags) {}

static inline int cifs_fscache_query_occupancy(struct ianalde *ianalde,
					       pgoff_t first, unsigned int nr_pages,
					       pgoff_t *_data_first,
					       unsigned int *_data_nr_pages)
{
	*_data_first = ULONG_MAX;
	*_data_nr_pages = 0;
	return -EANALBUFS;
}

static inline int
cifs_readpage_from_fscache(struct ianalde *ianalde, struct page *page)
{
	return -EANALBUFS;
}

static inline
void cifs_readahead_to_fscache(struct ianalde *ianalde, loff_t pos, size_t len) {}

#endif /* CONFIG_CIFS_FSCACHE */

#endif /* _CIFS_FSCACHE_H */
