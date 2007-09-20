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


/* Function pointer type for callback to free a client's data pointer. */
typedef void (*xfs_mru_cache_free_func_t)(unsigned long, void*);

typedef struct xfs_mru_cache
{
	struct radix_tree_root	store;     /* Core storage data structure.  */
	struct list_head	*lists;    /* Array of lists, one per grp.  */
	struct list_head	reap_list; /* Elements overdue for reaping. */
	spinlock_t		lock;      /* Lock to protect this struct.  */
	unsigned int		grp_count; /* Number of discrete groups.    */
	unsigned int		grp_time;  /* Time period spanned by grps.  */
	unsigned int		lru_grp;   /* Group containing time zero.   */
	unsigned long		time_zero; /* Time first element was added. */
	xfs_mru_cache_free_func_t free_func; /* Function pointer for freeing. */
	struct delayed_work	work;      /* Workqueue data for reaping.   */
	unsigned int		queued;	   /* work has been queued */
} xfs_mru_cache_t;

int xfs_mru_cache_init(void);
void xfs_mru_cache_uninit(void);
int xfs_mru_cache_create(struct xfs_mru_cache **mrup, unsigned int lifetime_ms,
			     unsigned int grp_count,
			     xfs_mru_cache_free_func_t free_func);
void xfs_mru_cache_flush(xfs_mru_cache_t *mru);
void xfs_mru_cache_destroy(struct xfs_mru_cache *mru);
int xfs_mru_cache_insert(struct xfs_mru_cache *mru, unsigned long key,
				void *value);
void * xfs_mru_cache_remove(struct xfs_mru_cache *mru, unsigned long key);
void xfs_mru_cache_delete(struct xfs_mru_cache *mru, unsigned long key);
void *xfs_mru_cache_lookup(struct xfs_mru_cache *mru, unsigned long key);
void *xfs_mru_cache_peek(struct xfs_mru_cache *mru, unsigned long key);
void xfs_mru_cache_done(struct xfs_mru_cache *mru);

#endif /* __XFS_MRU_CACHE_H__ */
