/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#ifndef _CEPH_CACHE_H
#define _CEPH_CACHE_H

#ifdef CONFIG_CEPH_FSCACHE

extern struct fscache_netfs ceph_cache_netfs;

int ceph_fscache_register(void);
void ceph_fscache_unregister(void);

int ceph_fscache_register_fs(struct ceph_fs_client* fsc);
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
void ceph_readpage_to_fscache(struct inode *inode, struct page *page);
void ceph_invalidate_fscache_page(struct inode* inode, struct page *page);

static inline void ceph_fscache_inode_init(struct ceph_inode_info *ci)
{
	ci->fscache = NULL;
	ci->i_fscache_gen = 0;
}

static inline void ceph_fscache_invalidate(struct inode *inode)
{
	fscache_invalidate(ceph_inode(inode)->fscache);
}

static inline void ceph_fscache_uncache_page(struct inode *inode,
					     struct page *page)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	return fscache_uncache_page(ci->fscache, page);
}

static inline int ceph_release_fscache_page(struct page *page, gfp_t gfp)
{
	struct inode* inode = page->mapping->host;
	struct ceph_inode_info *ci = ceph_inode(inode);
	return fscache_maybe_release_page(ci->fscache, page, gfp);
}

static inline void ceph_fscache_readpage_cancel(struct inode *inode,
						struct page *page)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	if (fscache_cookie_valid(ci->fscache) && PageFsCache(page))
		__fscache_uncache_page(ci->fscache, page);
}

static inline void ceph_fscache_readpages_cancel(struct inode *inode,
						 struct list_head *pages)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	return fscache_readpages_cancel(ci->fscache, pages);
}

static inline void ceph_disable_fscache_readpage(struct ceph_inode_info *ci)
{
	ci->i_fscache_gen = ci->i_rdcache_gen - 1;
}

#else

static inline int ceph_fscache_register(void)
{
	return 0;
}

static inline void ceph_fscache_unregister(void)
{
}

static inline int ceph_fscache_register_fs(struct ceph_fs_client* fsc)
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

static inline void ceph_fscache_revalidate_cookie(struct ceph_inode_info *ci)
{
}

static inline void ceph_fscache_uncache_page(struct inode *inode,
					     struct page *pages)
{
}

static inline int ceph_readpage_from_fscache(struct inode* inode,
					     struct page *page)
{
	return -ENOBUFS;
}

static inline int ceph_readpages_from_fscache(struct inode *inode,
					      struct address_space *mapping,
					      struct list_head *pages,
					      unsigned *nr_pages)
{
	return -ENOBUFS;
}

static inline void ceph_readpage_to_fscache(struct inode *inode,
					    struct page *page)
{
}

static inline void ceph_fscache_invalidate(struct inode *inode)
{
}

static inline void ceph_invalidate_fscache_page(struct inode *inode,
						struct page *page)
{
}

static inline int ceph_release_fscache_page(struct page *page, gfp_t gfp)
{
	return 1;
}

static inline void ceph_fscache_readpage_cancel(struct inode *inode,
						struct page *page)
{
}

static inline void ceph_fscache_readpages_cancel(struct inode *inode,
						 struct list_head *pages)
{
}

static inline void ceph_disable_fscache_readpage(struct ceph_inode_info *ci)
{
}

#endif

#endif
