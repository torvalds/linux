/*
 *   fs/cifs/fscache.h - CIFS filesystem cache interface definitions
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Authors(s): Suresh Jayaraman (sjayaraman@suse.de>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _CIFS_FSCACHE_H
#define _CIFS_FSCACHE_H

#include <linux/fscache.h>

#include "cifsglob.h"

#ifdef CONFIG_CIFS_FSCACHE

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
extern void cifs_fscache_get_super_cookie(struct cifsTconInfo *);
extern void cifs_fscache_release_super_cookie(struct cifsTconInfo *);

extern void cifs_fscache_release_inode_cookie(struct inode *);
extern void cifs_fscache_set_inode_cookie(struct inode *, struct file *);
extern void cifs_fscache_reset_inode_cookie(struct inode *);

extern void __cifs_fscache_invalidate_page(struct page *, struct inode *);
extern int cifs_fscache_release_page(struct page *page, gfp_t gfp);

static inline void cifs_fscache_invalidate_page(struct page *page,
					       struct inode *inode)
{
	if (PageFsCache(page))
		__cifs_fscache_invalidate_page(page, inode);
}

#else /* CONFIG_CIFS_FSCACHE */
static inline int cifs_fscache_register(void) { return 0; }
static inline void cifs_fscache_unregister(void) {}

static inline void
cifs_fscache_get_client_cookie(struct TCP_Server_Info *server) {}
static inline void
cifs_fscache_get_client_cookie(struct TCP_Server_Info *server); {}
static inline void cifs_fscache_get_super_cookie(struct cifsTconInfo *tcon) {}
static inline void
cifs_fscache_release_super_cookie(struct cifsTconInfo *tcon) {}

static inline void cifs_fscache_release_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_set_inode_cookie(struct inode *inode,
						 struct file *filp) {}
static inline void cifs_fscache_reset_inode_cookie(struct inode *inode) {}
static inline void cifs_fscache_release_page(struct page *page, gfp_t gfp)
{
	return 1; /* May release page */
}

static inline int cifs_fscache_invalidate_page(struct page *page,
			struct inode *) {}

#endif /* CONFIG_CIFS_FSCACHE */

#endif /* _CIFS_FSCACHE_H */
