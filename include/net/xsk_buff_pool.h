/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef XSK_BUFF_POOL_H_
#define XSK_BUFF_POOL_H_

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

/* AF_XDP core. */
struct xsk_buff_pool *xp_create(struct page **pages, u32 nr_pages, u32 chunks,
				u32 chunk_size, u32 headroom, u64 size,
				bool unaligned);
void xp_set_fq(struct xsk_buff_pool *pool, struct xsk_queue *fq);
void xp_destroy(struct xsk_buff_pool *pool);
void xp_release(struct xdp_buff_xsk *xskb);
u64 xp_get_handle(struct xdp_buff_xsk *xskb);
bool xp_validate_desc(struct xsk_buff_pool *pool, struct xdp_desc *desc);

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
dma_addr_t xp_get_dma(struct xdp_buff_xsk *xskb);
dma_addr_t xp_get_frame_dma(struct xdp_buff_xsk *xskb);
void xp_dma_sync_for_cpu(struct xdp_buff_xsk *xskb);
void xp_dma_sync_for_device(struct xsk_buff_pool *pool, dma_addr_t dma,
			    size_t size);

#endif /* XSK_BUFF_POOL_H_ */
