/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PAGE_POOL_PRIV_H
#define __PAGE_POOL_PRIV_H

#include <net/page_pool/helpers.h>

#include "netmem_priv.h"

extern struct mutex page_pools_lock;

s32 page_pool_inflight(const struct page_pool *pool, bool strict);

int page_pool_list(struct page_pool *pool);
void page_pool_detached(struct page_pool *pool);
void page_pool_unlist(struct page_pool *pool);

static inline bool
page_pool_set_dma_addr_netmem(netmem_ref netmem, dma_addr_t addr)
{
	if (PAGE_POOL_32BIT_ARCH_WITH_64BIT_DMA) {
		netmem_set_dma_addr(netmem, addr >> PAGE_SHIFT);

		/* We assume page alignment to shave off bottom bits,
		 * if this "compression" doesn't work we need to drop.
		 */
		return addr != (dma_addr_t)netmem_get_dma_addr(netmem)
				       << PAGE_SHIFT;
	}

	netmem_set_dma_addr(netmem, addr);
	return false;
}

static inline bool page_pool_set_dma_addr(struct page *page, dma_addr_t addr)
{
	return page_pool_set_dma_addr_netmem(page_to_netmem(page), addr);
}

#if defined(CONFIG_PAGE_POOL)
void page_pool_set_pp_info(struct page_pool *pool, netmem_ref netmem);
void page_pool_clear_pp_info(netmem_ref netmem);
int page_pool_check_memory_provider(struct net_device *dev,
				    struct netdev_rx_queue *rxq);
#else
static inline void page_pool_set_pp_info(struct page_pool *pool,
					 netmem_ref netmem)
{
}
static inline void page_pool_clear_pp_info(netmem_ref netmem)
{
}
static inline int page_pool_check_memory_provider(struct net_device *dev,
						  struct netdev_rx_queue *rxq)
{
	return 0;
}
#endif

#endif
