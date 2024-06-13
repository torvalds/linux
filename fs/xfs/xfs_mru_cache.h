// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * All Rights Reserved.
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
