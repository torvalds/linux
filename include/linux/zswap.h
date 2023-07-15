/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ZSWAP_H
#define _LINUX_ZSWAP_H

#include <linux/types.h>
#include <linux/mm_types.h>

extern u64 zswap_pool_total_size;
extern atomic_t zswap_stored_pages;

#ifdef CONFIG_ZSWAP

bool zswap_store(struct folio *folio);
bool zswap_load(struct folio *folio);
void zswap_invalidate(int type, pgoff_t offset);
void zswap_swapon(int type);
void zswap_swapoff(int type);

#else

static inline bool zswap_store(struct folio *folio)
{
	return false;
}

static inline bool zswap_load(struct folio *folio)
{
	return false;
}

static inline void zswap_invalidate(int type, pgoff_t offset) {}
static inline void zswap_swapon(int type) {}
static inline void zswap_swapoff(int type) {}

#endif

#endif /* _LINUX_ZSWAP_H */
