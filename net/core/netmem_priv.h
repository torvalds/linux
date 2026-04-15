/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NETMEM_PRIV_H
#define __NETMEM_PRIV_H

static inline unsigned long netmem_get_pp_magic(netmem_ref netmem)
{
	return netmem_to_nmdesc(netmem)->pp_magic & ~PP_DMA_INDEX_MASK;
}

static inline bool netmem_is_pp(netmem_ref netmem)
{
	struct page *page;

	/* XXX: Now that the offset of page_type is shared between
	 * struct page and net_iov, just cast the netmem to struct page
	 * unconditionally by clearing NET_IOV if any, no matter whether
	 * it comes from struct net_iov or struct page.  This should be
	 * adjusted once the offset is no longer shared.
	 */
	page = (struct page *)((__force unsigned long)netmem & ~NET_IOV);
	return PageNetpp(page);
}

static inline void netmem_set_pp(netmem_ref netmem, struct page_pool *pool)
{
	netmem_to_nmdesc(netmem)->pp = pool;
}

static inline void netmem_set_dma_addr(netmem_ref netmem,
				       unsigned long dma_addr)
{
	netmem_to_nmdesc(netmem)->dma_addr = dma_addr;
}

static inline unsigned long netmem_get_dma_index(netmem_ref netmem)
{
	unsigned long magic;

	if (WARN_ON_ONCE(netmem_is_net_iov(netmem)))
		return 0;

	magic = netmem_to_nmdesc(netmem)->pp_magic;

	return (magic & PP_DMA_INDEX_MASK) >> PP_DMA_INDEX_SHIFT;
}

static inline void netmem_set_dma_index(netmem_ref netmem,
					unsigned long id)
{
	unsigned long magic;

	if (WARN_ON_ONCE(netmem_is_net_iov(netmem)))
		return;

	magic = netmem_get_pp_magic(netmem) | (id << PP_DMA_INDEX_SHIFT);
	netmem_to_nmdesc(netmem)->pp_magic = magic;
}
#endif
