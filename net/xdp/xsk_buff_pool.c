// SPDX-License-Identifier: GPL-2.0

#include <net/xsk_buff_pool.h>
#include <net/xdp_sock.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/swiotlb.h>

#include "xsk_queue.h"

/* Masks for xdp_umem_page flags.
 * The low 12-bits of the addr will be 0 since this is the page address, so we
 * can use them for flags.
 */
#define XSK_NEXT_PG_CONTIG_SHIFT 0
#define XSK_NEXT_PG_CONTIG_MASK BIT_ULL(XSK_NEXT_PG_CONTIG_SHIFT)

struct xsk_buff_pool {
	struct xsk_queue *fq;
	struct list_head free_list;
	dma_addr_t *dma_pages;
	struct xdp_buff_xsk *heads;
	u64 chunk_mask;
	u64 addrs_cnt;
	u32 free_list_cnt;
	u32 dma_pages_cnt;
	u32 heads_cnt;
	u32 free_heads_cnt;
	u32 headroom;
	u32 chunk_size;
	u32 frame_len;
	bool cheap_dma;
	bool unaligned;
	void *addrs;
	struct device *dev;
	struct xdp_buff_xsk *free_heads[];
};

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
	pool->cheap_dma = true;
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

static bool __maybe_unused xp_check_swiotlb_dma(struct xsk_buff_pool *pool)
{
#if defined(CONFIG_SWIOTLB)
	phys_addr_t paddr;
	u32 i;

	for (i = 0; i < pool->dma_pages_cnt; i++) {
		paddr = dma_to_phys(pool->dev, pool->dma_pages[i]);
		if (is_swiotlb_buffer(paddr))
			return false;
	}
#endif
	return true;
}

static bool xp_check_cheap_dma(struct xsk_buff_pool *pool)
{
#if defined(CONFIG_HAS_DMA)
	const struct dma_map_ops *ops = get_dma_ops(pool->dev);

	if (ops) {
		return !ops->sync_single_for_cpu &&
			!ops->sync_single_for_device;
	}

	if (!dma_is_direct(ops))
		return false;

	if (!xp_check_swiotlb_dma(pool))
		return false;

	if (!dev_is_dma_coherent(pool->dev)) {
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) ||		\
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) ||	\
	defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE)
		return false;
#endif
	}
#endif
	return true;
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

	for (i = 0; i < pool->dma_pages_cnt; i++) {
		dma = dma_map_page_attrs(dev, pages[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, attrs);
		if (dma_mapping_error(dev, dma)) {
			xp_dma_unmap(pool, attrs);
			return -ENOMEM;
		}
		pool->dma_pages[i] = dma;
	}

	if (pool->unaligned)
		xp_check_dma_contiguity(pool);

	pool->dev = dev;
	pool->cheap_dma = xp_check_cheap_dma(pool);
	return 0;
}
EXPORT_SYMBOL(xp_dma_map);

static bool xp_desc_crosses_non_contig_pg(struct xsk_buff_pool *pool,
					  u64 addr, u32 len)
{
	bool cross_pg = (addr & (PAGE_SIZE - 1)) + len > PAGE_SIZE;

	if (pool->dma_pages_cnt && cross_pg) {
		return !(pool->dma_pages[addr >> PAGE_SHIFT] &
			 XSK_NEXT_PG_CONTIG_MASK);
	}
	return false;
}

static bool xp_addr_crosses_non_contig_pg(struct xsk_buff_pool *pool,
					  u64 addr)
{
	return xp_desc_crosses_non_contig_pg(pool, addr, pool->chunk_size);
}

void xp_release(struct xdp_buff_xsk *xskb)
{
	xskb->pool->free_heads[xskb->pool->free_heads_cnt++] = xskb;
}

static u64 xp_aligned_extract_addr(struct xsk_buff_pool *pool, u64 addr)
{
	return addr & pool->chunk_mask;
}

static u64 xp_unaligned_extract_addr(u64 addr)
{
	return addr & XSK_UNALIGNED_BUF_ADDR_MASK;
}

static u64 xp_unaligned_extract_offset(u64 addr)
{
	return addr >> XSK_UNALIGNED_BUF_OFFSET_SHIFT;
}

static u64 xp_unaligned_add_offset_to_addr(u64 addr)
{
	return xp_unaligned_extract_addr(addr) +
		xp_unaligned_extract_offset(addr);
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

	if (!pool->cheap_dma) {
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

static bool xp_aligned_validate_desc(struct xsk_buff_pool *pool,
				     struct xdp_desc *desc)
{
	u64 chunk, chunk_end;

	chunk = xp_aligned_extract_addr(pool, desc->addr);
	chunk_end = xp_aligned_extract_addr(pool, desc->addr + desc->len);
	if (chunk != chunk_end)
		return false;

	if (chunk >= pool->addrs_cnt)
		return false;

	if (desc->options)
		return false;
	return true;
}

static bool xp_unaligned_validate_desc(struct xsk_buff_pool *pool,
				       struct xdp_desc *desc)
{
	u64 addr, base_addr;

	base_addr = xp_unaligned_extract_addr(desc->addr);
	addr = xp_unaligned_add_offset_to_addr(desc->addr);

	if (desc->len > pool->chunk_size)
		return false;

	if (base_addr >= pool->addrs_cnt || addr >= pool->addrs_cnt ||
	    xp_desc_crosses_non_contig_pg(pool, addr, desc->len))
		return false;

	if (desc->options)
		return false;
	return true;
}

bool xp_validate_desc(struct xsk_buff_pool *pool, struct xdp_desc *desc)
{
	return pool->unaligned ? xp_unaligned_validate_desc(pool, desc) :
		xp_aligned_validate_desc(pool, desc);
}

u64 xp_get_handle(struct xdp_buff_xsk *xskb)
{
	u64 offset = xskb->xdp.data - xskb->xdp.data_hard_start;

	offset += xskb->pool->headroom;
	if (!xskb->pool->unaligned)
		return xskb->orig_addr + offset;
	return xskb->orig_addr + (offset << XSK_UNALIGNED_BUF_OFFSET_SHIFT);
}

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

dma_addr_t xp_get_dma(struct xdp_buff_xsk *xskb)
{
	return xskb->dma;
}
EXPORT_SYMBOL(xp_get_dma);

dma_addr_t xp_get_frame_dma(struct xdp_buff_xsk *xskb)
{
	return xskb->frame_dma;
}
EXPORT_SYMBOL(xp_get_frame_dma);

void xp_dma_sync_for_cpu(struct xdp_buff_xsk *xskb)
{
	if (xskb->pool->cheap_dma)
		return;

	dma_sync_single_range_for_cpu(xskb->pool->dev, xskb->dma, 0,
				      xskb->pool->frame_len, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_cpu);

void xp_dma_sync_for_device(struct xsk_buff_pool *pool, dma_addr_t dma,
			    size_t size)
{
	if (pool->cheap_dma)
		return;

	dma_sync_single_range_for_device(pool->dev, dma, 0,
					 size, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_device);
