/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ZSWAP_H
#define _LINUX_ZSWAP_H

#include <linux/types.h>
#include <linux/mm_types.h>

struct lruvec;

extern atomic_t zswap_stored_pages;

#ifdef CONFIG_ZSWAP

struct zswap_lruvec_state {
	/*
	 * Number of pages in zswap that should be protected from the shrinker.
	 * This number is an estimate of the following counts:
	 *
	 * a) Recent page faults.
	 * b) Recent insertion to the zswap LRU. This includes new zswap stores,
	 *    as well as recent zswap LRU rotations.
	 *
	 * These pages are likely to be warm, and might incur IO if the are written
	 * to swap.
	 */
	atomic_long_t nr_zswap_protected;
};

unsigned long zswap_total_pages(void);
bool zswap_store(struct folio *folio);
bool zswap_load(struct folio *folio);
void zswap_invalidate(swp_entry_t swp);
int zswap_swapon(int type, unsigned long nr_pages);
void zswap_swapoff(int type);
void zswap_memcg_offline_cleanup(struct mem_cgroup *memcg);
void zswap_lruvec_state_init(struct lruvec *lruvec);
void zswap_folio_swapin(struct folio *folio);
bool zswap_is_enabled(void);
#else

struct zswap_lruvec_state {};

static inline bool zswap_store(struct folio *folio)
{
	return false;
}

static inline bool zswap_load(struct folio *folio)
{
	return false;
}

static inline void zswap_invalidate(swp_entry_t swp) {}
static inline int zswap_swapon(int type, unsigned long nr_pages)
{
	return 0;
}
static inline void zswap_swapoff(int type) {}
static inline void zswap_memcg_offline_cleanup(struct mem_cgroup *memcg) {}
static inline void zswap_lruvec_state_init(struct lruvec *lruvec) {}
static inline void zswap_folio_swapin(struct folio *folio) {}

static inline bool zswap_is_enabled(void)
{
	return false;
}

#endif

#endif /* _LINUX_ZSWAP_H */
