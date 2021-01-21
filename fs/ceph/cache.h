/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 */

#ifndef _CEPH_CACHE_H
#define _CEPH_CACHE_H

#ifdef CONFIG_CEPH_FSCACHE

extern struct fscache_netfs ceph_cache_netfs;

int ceph_fscache_register(void);
void ceph_fscache_unregister(void);

int ceph_fscache_register_fs(struct ceph_fs_client* fsc, struct fs_context *fc);
void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc);

void ceph_fscache_register_inode_cookie(struct inode *inode);
void ceph_fscache_unregister_inode_cookie(struct ceph_inode_info* ci);
void ceph_fscache_file_set_cookie(struct inode *inode, struct file *filp);
void ceph_fscache_revalidate_cookie(struct ceph_inode_info *ci);

int ceph_readpage_from_fscache(struct inode *inode, struct page *page);
int ceph_readpages_from_fscache(struct inode *inode,
				struct address_space *mapping,
				struct list_head *pages,
				unsigned *nr_pages);

static inline void ceph_fscache_inode_init(struct ceph_inode_info *ci)
{
	ci->fscache = NULL;
}

static inline void ceph_fscache_invalidate(struct inode *inode)
{
	fscache_invalidate(ceph_inode(inode)->fscache);
}

#else

static inline int ceph_fscache_register(void)
{
	return 0;
}

static inline void ceph_fscache_unregister(void)
{
}

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

static inline void ceph_fscache_file_set_cookie(struct inode *inode,
						struct file *filp)
{
}

static inline void ceph_fscache_invalidate(struct inode *inode)
{
}

#endif

#endif /* _CEPH_CACHE_H */
