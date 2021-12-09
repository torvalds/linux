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

#include <linux/fscache.h>

#include "cifsglob.h"

#ifdef CONFIG_CIFS_FSCACHE

/*
 * Auxiliary data attached to CIFS superblock within the cache
 */
struct cifs_fscache_super_auxdata {
	u64	resource_id;		/* unique server resource id */
	__le64	vol_create_time;
	u32	vol_serial_number;
} __packed;

/*
 * Auxiliary data attached to CIFS inode within the cache
 */
struct cifs_fscache_inode_auxdata {
	u64 last_write_time_sec;
	u64 last_change_time_sec;
	u32 last_write_time_nsec;
	u32 last_change_time_nsec;
	u64 eof;
};

/*
 * cache.c
 */
extern struct fscache_netfs cifs_fscache_netfs;
extern const struct fscache_cookie_def cifs_fscache_server_index_def;
extern const struct fscache_cookie_def cifs_fscache_super_index_def;
extern const struct fscache_cookie_def cifs_fscache_inode_object_def;

extern int cifs_fscache_register(void);
extern void cifs_fscache_unregister(void);

/*
 * fscache.c
 */
extern void cifs_fscache_get_client_cookie(struct TCP_Server_Info *);
extern void cifs_fscache_release_client_cookie(struct TCP_Server_Info *);
extern void cifs_fscache_get_super_cookie(struct cifs_tcon *);
extern void cifs_fscache_release_super_cookie(struct cifs_tcon *);

extern void cifs_fscache_release_inode_cookie(struct inode *);
extern void cifs_fscache_update_inode_cookie(struct inode *inode);
extern void cifs_fscache_set_inode_cookie(struct inode *, struct file *);
extern void cifs_fscache_reset_inode_cookie(struct inode *);

extern void __cifs_fscache_invalidate_page(struct page *, struct inode *);
extern void __cifs_fscache_wait_on_page_write(struct inode *inode, struct page *page);
extern void __cifs_fscache_uncache_page(struct inode *inode, struct page *page);
extern int cifs_fscache_release_page(struct page *page, gfp_t gfp);
extern int __cifs_readpage_from_fscache(struct inode *, struct page *);
extern int __cifs_readpages_from_fscache(struct inode *,
					 struct address_space *,
					 struct list_head *,
					 unsigned *);
extern void __cifs_fscache_readpages_cancel(struct inode *, struct list_head *);

extern void __cifs_readpage_to_fscache(struct inode *, struct page *);

static inline void cifs_fscache_invalidate_page(struct page *page,
					       struct inode *inode)
{
	if (PageFsCache(page))
		__cifs_fscache_invalidate_page(page, inode);
}

static inline void cifs_fscache_wait_on_page_write(struct inode *inode,
						   struct page *page)
{
	if (PageFsCache(page))
		__cifs_fscache_wait_on_page_write(inode, page);
}

static inline void cifs_fscache_uncache_page(struct inode *inode,
						   struct page *page)
{
	if (PageFsCache(page))
		__cifs_fscache_uncache_page(inode, page);
}

static inline int cifs_readpage_from_fscache(struct inode *inode,
					     struct page *page)
{
	if (CIFS_I(inode)->fscache)
		return __cifs_readpage_from_fscache(inode, page);

	return -ENOBUFS;
}

static inline int cifs_readpages_from_fscache(struct inode *inode,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned *nr_pages)
{
	if (CIFS_I(inode)->fscache)
		return __cifs_readpages_from_fscache(inode, mapping, pages,
						     nr_pages);
	return -ENOBUFS;
}

static inline void cifs_readpage_to_fscache(struct inode *inode,
					    struct page *page)
{
	if (PageFsCache(page))
		__cifs_readpage_to_fscache(inode, page);
}

static inline void cifs_fscache_readpages_cancel(struct inode *inode,
						 struct list_head *pages)
{
	if (CIFS_I(inode)->fscache)
		return __cifs_fscache_readpages_cancel(inode, pages);
}

#else /* CONFIG_CIFS_FSCACHE */
static inline int cifs_fscache_register(void) { return 0; }
static inline void cifs_fscache_unregister(void) {}

static inline void
cifs_fscache_get_client_cookie(struct TCP_Server_Info *server) {}
static inline void
cifs_fscache_release_client_cookie(struct TCP_Server_Info *server) {}
static inline void cifs_fscache_get_super_cookie(struct cifs_tcon *tcon) {}
static inline void
cifs_fscache_release_super_cookie(struct cifs_tcon *tcon) {}

static inline void cifs_fscache_release_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_update_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_set_inode_cookie(struct inode *inode,
						 struct file *filp) {}
static inline void cifs_fscache_reset_inode_cookie(struct inode *inode) {}
static inline int cifs_fscache_release_page(struct page *page, gfp_t gfp)
{
	return 1; /* May release page */
}

static inline void cifs_fscache_invalidate_page(struct page *page,
			struct inode *inode) {}
static inline void cifs_fscache_wait_on_page_write(struct inode *inode,
						   struct page *page) {}
static inline void cifs_fscache_uncache_page(struct inode *inode,
						   struct page *page) {}

static inline int
cifs_readpage_from_fscache(struct inode *inode, struct page *page)
{
	return -ENOBUFS;
}

static inline int cifs_readpages_from_fscache(struct inode *inode,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned *nr_pages)
{
	return -ENOBUFS;
}

static inline void cifs_readpage_to_fscache(struct inode *inode,
			struct page *page) {}

static inline void cifs_fscache_readpages_cancel(struct inode *inode,
						 struct list_head *pages)
{
}

#endif /* CONFIG_CIFS_FSCACHE */

#endif /* _CIFS_FSCACHE_H */
