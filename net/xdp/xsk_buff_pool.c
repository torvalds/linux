// SPDX-License-Identifier: GPL-2.0

#include <net/xsk_buff_pool.h>
#include <net/xdp_sock.h>

#include "xsk_queue.h"

static void xp_addr_unmap(struct xsk_buff_pool *pool)
{
	vunmap(pool->addrs);
}

static int xp_addr_map(struct xsk_buff_pool *pool,
		       struct page **pages, u32 nr_pages)
{
	pool->addrs = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!pool->addrs)
		return -ENOMEM;
	return 0;
}

void xp_destroy(struct xsk_buff_pool *pool)
{
	if (!pool)
		return;

	xp_addr_unmap(pool);
	kvfree(pool->heads);
	kvfree(pool);
}

struct xsk_buff_pool *xp_create(struct page **pages, u32 nr_pages, u32 chunks,
				u32 chunk_size, u32 headroom, u64 size,
				bool unaligned)
{
	struct xsk_buff_pool *pool;
	struct xdp_buff_xsk *xskb;
	int err;
	u32 i;

	pool = kvzalloc(struct_size(pool, free_heads, chunks), GFP_KERNEL);
	if (!pool)
		goto out;

	pool->heads = kvcalloc(chunks, sizeof(*pool->heads), GFP_KERNEL);
	if (!pool->heads)
		goto out;

	pool->chunk_mask = ~((u64)chunk_size - 1);
	pool->addrs_cnt = size;
	pool->heads_cnt = chunks;
	pool->free_heads_cnt = chunks;
	pool->headroom = headroom;
	pool->chunk_size = chunk_size;
	pool->unaligned = unaligned;
	pool->frame_len = chunk_size - headroom - XDP_PACKET_HEADROOM;
	INIT_LIST_HEAD(&pool->free_list);

	for (i = 0; i < pool->free_heads_cnt; i++) {
		xskb = &pool->heads[i];
		xskb->pool = pool;
		xskb->xdp.frame_sz = chunk_size - headroom;
		pool->free_heads[i] = xskb;
	}

	err = xp_addr_map(pool, pages, nr_pages);
	if (!err)
		return pool;

out:
	xp_destroy(pool);
	return NULL;
}

void xp_set_fq(struct xsk_buff_pool *pool, struct xsk_queue *fq)
{
	pool->fq = fq;
}

void xp_set_rxq_info(struct xsk_buff_pool *pool, struct xdp_rxq_info *rxq)
{
	u32 i;

	for (i = 0; i < pool->heads_cnt; i++)
		pool->heads[i].xdp.rxq = rxq;
}
EXPORT_SYMBOL(xp_set_rxq_info);

void xp_dma_unmap(struct xsk_buff_pool *pool, unsigned long attrs)
{
	dma_addr_t *dma;
	u32 i;

	if (pool->dma_pages_cnt == 0)
		return;

	for (i = 0; i < pool->dma_pages_cnt; i++) {
		dma = &pool->dma_pages[i];
		if (*dma) {
			dma_unmap_page_attrs(pool->dev, *dma, PAGE_SIZE,
					     DMA_BIDIRECTIONAL, attrs);
			*dma = 0;
		}
	}

	kvfree(pool->dma_pages);
	pool->dma_pages_cnt = 0;
	pool->dev = NULL;
}
EXPORT_SYMBOL(xp_dma_unmap);

static void xp_check_dma_contiguity(struct xsk_buff_pool *pool)
{
	u32 i;

	for (i = 0; i < pool->dma_pages_cnt - 1; i++) {
		if (pool->dma_pages[i] + PAGE_SIZE == pool->dma_pages[i + 1])
			pool->dma_pages[i] |= XSK_NEXT_PG_CONTIG_MASK;
		else
			pool->dma_pages[i] &= ~XSK_NEXT_PG_CONTIG_MASK;
	}
}

int xp_dma_map(struct xsk_buff_pool *pool, struct device *dev,
	       unsigned long attrs, struct page **pages, u32 nr_pages)
{
	dma_addr_t dma;
	u32 i;

	pool->dma_pages = kvcalloc(nr_pages, sizeof(*pool->dma_pages),
				   GFP_KERNEL);
	if (!pool->dma_pages)
		return -ENOMEM;

	pool->dev = dev;
	pool->dma_pages_cnt = nr_pages;
	pool->dma_need_sync = false;

	for (i = 0; i < pool->dma_pages_cnt; i++) {
		dma = dma_map_page_attrs(dev, pages[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, attrs);
		if (dma_mapping_error(dev, dma)) {
			xp_dma_unmap(pool, attrs);
			return -ENOMEM;
		}
		if (dma_need_sync(dev, dma))
			pool->dma_need_sync = true;
		pool->dma_pages[i] = dma;
	}

	if (pool->unaligned)
		xp_check_dma_contiguity(pool);
	return 0;
}
EXPORT_SYMBOL(xp_dma_map);

static bool xp_addr_crosses_non_contig_pg(struct xsk_buff_pool *pool,
					  u64 addr)
{
	return xp_desc_crosses_non_contig_pg(pool, addr, pool->chunk_size);
}

static bool xp_check_unaligned(struct xsk_buff_pool *pool, u64 *addr)
{
	*addr = xp_unaligned_extract_addr(*addr);
	if (*addr >= pool->addrs_cnt ||
	    *addr + pool->chunk_size > pool->addrs_cnt ||
	    xp_addr_crosses_non_contig_pg(pool, *addr))
		return false;
	return true;
}

static bool xp_check_aligned(struct xsk_buff_pool *pool, u64 *addr)
{
	*addr = xp_aligned_extract_addr(pool, *addr);
	return *addr < pool->addrs_cnt;
}

static struct xdp_buff_xsk *__xp_alloc(struct xsk_buff_pool *pool)
{
	struct xdp_buff_xsk *xskb;
	u64 addr;
	bool ok;

	if (pool->free_heads_cnt == 0)
		return NULL;

	xskb = pool->free_heads[--pool->free_heads_cnt];

	for (;;) {
		if (!xskq_cons_peek_addr_unchecked(pool->fq, &addr)) {
			xp_release(xskb);
			return NULL;
		}

		ok = pool->unaligned ? xp_check_unaligned(pool, &addr) :
		     xp_check_aligned(pool, &addr);
		if (!ok) {
			pool->fq->invalid_descs++;
			xskq_cons_release(pool->fq);
			continue;
		}
		break;
	}
	xskq_cons_release(pool->fq);

	xskb->orig_addr = addr;
	xskb->xdp.data_hard_start = pool->addrs + addr + pool->headroom;
	if (pool->dma_pages_cnt) {
		xskb->frame_dma = (pool->dma_pages[addr >> PAGE_SHIFT] &
				   ~XSK_NEXT_PG_CONTIG_MASK) +
				  (addr & ~PAGE_MASK);
		xskb->dma = xskb->frame_dma + pool->headroom +
			    XDP_PACKET_HEADROOM;
	}
	return xskb;
}

struct xdp_buff *xp_alloc(struct xsk_buff_pool *pool)
{
	struct xdp_buff_xsk *xskb;

	if (!pool->free_list_cnt) {
		xskb = __xp_alloc(pool);
		if (!xskb)
			return NULL;
	} else {
		pool->free_list_cnt--;
		xskb = list_first_entry(&pool->free_list, struct xdp_buff_xsk,
					free_list_node);
		list_del(&xskb->free_list_node);
	}

	xskb->xdp.data = xskb->xdp.data_hard_start + XDP_PACKET_HEADROOM;
	xskb->xdp.data_meta = xskb->xdp.data;

	if (pool->dma_need_sync) {
		dma_sync_single_range_for_device(pool->dev, xskb->dma, 0,
						 pool->frame_len,
						 DMA_BIDIRECTIONAL);
	}
	return &xskb->xdp;
}
EXPORT_SYMBOL(xp_alloc);

bool xp_can_alloc(struct xsk_buff_pool *pool, u32 count)
{
	if (pool->free_list_cnt >= count)
		return true;
	return xskq_cons_has_entries(pool->fq, count - pool->free_list_cnt);
}
EXPORT_SYMBOL(xp_can_alloc);

void xp_free(struct xdp_buff_xsk *xskb)
{
	xskb->pool->free_list_cnt++;
	list_add(&xskb->free_list_node, &xskb->pool->free_list);
}
EXPORT_SYMBOL(xp_free);

void *xp_raw_get_data(struct xsk_buff_pool *pool, u64 addr)
{
	addr = pool->unaligned ? xp_unaligned_add_offset_to_addr(addr) : addr;
	return pool->addrs + addr;
}
EXPORT_SYMBOL(xp_raw_get_data);

dma_addr_t xp_raw_get_dma(struct xsk_buff_pool *pool, u64 addr)
{
	addr = pool->unaligned ? xp_unaligned_add_offset_to_addr(addr) : addr;
	return (pool->dma_pages[addr >> PAGE_SHIFT] &
		~XSK_NEXT_PG_CONTIG_MASK) +
		(addr & ~PAGE_MASK);
}
EXPORT_SYMBOL(xp_raw_get_dma);

void xp_dma_sync_for_cpu_slow(struct xdp_buff_xsk *xskb)
{
	dma_sync_single_range_for_cpu(xskb->pool->dev, xskb->dma, 0,
				      xskb->pool->frame_len, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_cpu_slow);

void xp_dma_sync_for_device_slow(struct xsk_buff_pool *pool, dma_addr_t dma,
				 size_t size)
{
	dma_sync_single_range_for_device(pool->dev, dma, 0,
					 size, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_device_slow);
