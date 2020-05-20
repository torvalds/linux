/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef XSK_BUFF_POOL_H_
#define XSK_BUFF_POOL_H_

#include <linux/if_xdp.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <net/xdp.h>

struct xsk_buff_pool;
struct xdp_rxq_info;
struct xsk_queue;
struct xdp_desc;
struct device;
struct page;

struct xdp_buff_xsk {
	struct xdp_buff xdp;
	dma_addr_t dma;
	dma_addr_t frame_dma;
	struct xsk_buff_pool *pool;
	bool unaligned;
	u64 orig_addr;
	struct list_head free_list_node;
};

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

/* AF_XDP core. */
struct xsk_buff_pool *xp_create(struct page **pages, u32 nr_pages, u32 chunks,
				u32 chunk_size, u32 headroom, u64 size,
				bool unaligned);
void xp_set_fq(struct xsk_buff_pool *pool, struct xsk_queue *fq);
void xp_destroy(struct xsk_buff_pool *pool);
void xp_release(struct xdp_buff_xsk *xskb);

/* AF_XDP, and XDP core. */
void xp_free(struct xdp_buff_xsk *xskb);

/* AF_XDP ZC drivers, via xdp_sock_buff.h */
void xp_set_rxq_info(struct xsk_buff_pool *pool, struct xdp_rxq_info *rxq);
int xp_dma_map(struct xsk_buff_pool *pool, struct device *dev,
	       unsigned long attrs, struct page **pages, u32 nr_pages);
void xp_dma_unmap(struct xsk_buff_pool *pool, unsigned long attrs);
struct xdp_buff *xp_alloc(struct xsk_buff_pool *pool);
bool xp_can_alloc(struct xsk_buff_pool *pool, u32 count);
void *xp_raw_get_data(struct xsk_buff_pool *pool, u64 addr);
dma_addr_t xp_raw_get_dma(struct xsk_buff_pool *pool, u64 addr);
static inline dma_addr_t xp_get_dma(struct xdp_buff_xsk *xskb)
{
	return xskb->dma;
}

static inline dma_addr_t xp_get_frame_dma(struct xdp_buff_xsk *xskb)
{
	return xskb->frame_dma;
}

void xp_dma_sync_for_cpu_slow(struct xdp_buff_xsk *xskb);
static inline void xp_dma_sync_for_cpu(struct xdp_buff_xsk *xskb)
{
	if (xskb->pool->cheap_dma)
		return;

	xp_dma_sync_for_cpu_slow(xskb);
}

void xp_dma_sync_for_device_slow(struct xsk_buff_pool *pool, dma_addr_t dma,
				 size_t size);
static inline void xp_dma_sync_for_device(struct xsk_buff_pool *pool,
					  dma_addr_t dma, size_t size)
{
	if (pool->cheap_dma)
		return;

	xp_dma_sync_for_device_slow(pool, dma, size);
}

/* Masks for xdp_umem_page flags.
 * The low 12-bits of the addr will be 0 since this is the page address, so we
 * can use them for flags.
 */
#define XSK_NEXT_PG_CONTIG_SHIFT 0
#define XSK_NEXT_PG_CONTIG_MASK BIT_ULL(XSK_NEXT_PG_CONTIG_SHIFT)

static inline bool xp_desc_crosses_non_contig_pg(struct xsk_buff_pool *pool,
						 u64 addr, u32 len)
{
	bool cross_pg = (addr & (PAGE_SIZE - 1)) + len > PAGE_SIZE;

	if (pool->dma_pages_cnt && cross_pg) {
		return !(pool->dma_pages[addr >> PAGE_SHIFT] &
			 XSK_NEXT_PG_CONTIG_MASK);
	}
	return false;
}

static inline u64 xp_aligned_extract_addr(struct xsk_buff_pool *pool, u64 addr)
{
	return addr & pool->chunk_mask;
}

static inline u64 xp_unaligned_extract_addr(u64 addr)
{
	return addr & XSK_UNALIGNED_BUF_ADDR_MASK;
}

static inline u64 xp_unaligned_extract_offset(u64 addr)
{
	return addr >> XSK_UNALIGNED_BUF_OFFSET_SHIFT;
}

static inline u64 xp_unaligned_add_offset_to_addr(u64 addr)
{
	return xp_unaligned_extract_addr(addr) +
		xp_unaligned_extract_offset(addr);
}

#endif /* XSK_BUFF_POOL_H_ */
