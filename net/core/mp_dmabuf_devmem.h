/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Dmabuf device memory provider.
 *
 * Authors:	Mina Almasry <almasrymina@google.com>
 *
 */
#ifndef _NET_MP_DMABUF_DEVMEM_H
#define _NET_MP_DMABUF_DEVMEM_H

#include <net/netmem.h>

#if defined(CONFIG_NET_DEVMEM)
int mp_dmabuf_devmem_init(struct page_pool *pool);

netmem_ref mp_dmabuf_devmem_alloc_netmems(struct page_pool *pool, gfp_t gfp);

void mp_dmabuf_devmem_destroy(struct page_pool *pool);

bool mp_dmabuf_devmem_release_page(struct page_pool *pool, netmem_ref netmem);
#else
static inline int mp_dmabuf_devmem_init(struct page_pool *pool)
{
	return -EOPNOTSUPP;
}

static inline netmem_ref
mp_dmabuf_devmem_alloc_netmems(struct page_pool *pool, gfp_t gfp)
{
	return 0;
}

static inline void mp_dmabuf_devmem_destroy(struct page_pool *pool)
{
}

static inline bool
mp_dmabuf_devmem_release_page(struct page_pool *pool, netmem_ref netmem)
{
	return false;
}
#endif

#endif /* _NET_MP_DMABUF_DEVMEM_H */
