/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_MRU_CACHE_H__
#define __XFS_MRU_CACHE_H__

struct xfs_mru_cache;

struct xfs_mru_cache_elem {
	struct list_head list_node;
	unsigned long	key;
};

/* Function pointer type for callback to free a client's data pointer. */
typedef void (*xfs_mru_cache_free_func_t)(void *, struct xfs_mru_cache_elem *);

int xfs_mru_cache_init(void);
void xfs_mru_cache_uninit(void);
int xfs_mru_cache_create(struct xfs_mru_cache **mrup, void *data,
		unsigned int lifetime_ms, unsigned int grp_count,
		xfs_mru_cache_free_func_t free_func);
void xfs_mru_cache_destroy(struct xfs_mru_cache *mru);
int xfs_mru_cache_insert(struct xfs_mru_cache *mru, unsigned long key,
		struct xfs_mru_cache_elem *elem);
struct xfs_mru_cache_elem *
xfs_mru_cache_remove(struct xfs_mru_cache *mru, unsigned long key);
void xfs_mru_cache_delete(struct xfs_mru_cache *mru, unsigned long key);
struct xfs_mru_cache_elem *
xfs_mru_cache_lookup(struct xfs_mru_cache *mru, unsigned long key);
void xfs_mru_cache_done(struct xfs_mru_cache *mru);

#endif /* __XFS_MRU_CACHE_H__ */
