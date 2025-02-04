/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_PAGE_POOL_MEMORY_PROVIDER_H
#define _NET_PAGE_POOL_MEMORY_PROVIDER_H

#include <net/netmem.h>
#include <net/page_pool/types.h>

struct netdev_rx_queue;
struct sk_buff;

struct memory_provider_ops {
	netmem_ref (*alloc_netmems)(struct page_pool *pool, gfp_t gfp);
	bool (*release_netmem)(struct page_pool *pool, netmem_ref netmem);
	int (*init)(struct page_pool *pool);
	void (*destroy)(struct page_pool *pool);
	int (*nl_fill)(void *mp_priv, struct sk_buff *rsp,
		       struct netdev_rx_queue *rxq);
	void (*uninstall)(void *mp_priv, struct netdev_rx_queue *rxq);
};

#endif
