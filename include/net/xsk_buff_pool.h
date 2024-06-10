/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef XSK_BUFF_POOL_H_
#define XSK_BUFF_POOL_H_

#include <linux/if_xdp.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/bpf.h>
#include <net/xdp.h>

struct xsk_buff_pool;
struct xdp_rxq_info;
struct xsk_cb_desc;
struct xsk_queue;
struct xdp_desc;
struct xdp_umem;
struct xdp_sock;
struct device;
struct page;

#define XSK_PRIV_MAX 24

struct xdp_buff_xsk {
	struct xdp_buff xdp;
	u8 cb[XSK_PRIV_MAX];
	dma_addr_t dma;
	dma_addr_t frame_dma;
	struct xsk_buff_pool *pool;
	u64 orig_addr;
	struct list_head free_list_node;
	struct list_head xskb_list_node;
};

#define XSK_CHECK_PRIV_TYPE(t) BUILD_BUG_ON(sizeof(t) > offsetofend(struct xdp_buff_xsk, cb))
#define XSK_TX_COMPL_FITS(t) BUILD_BUG_ON(sizeof(struct xsk_tx_metadata_compl) > sizeof(t))

struct xsk_dma_map {
	dma_addr_t *dma_pages;
	struct device *dev;
	struct net_device *netdev;
	refcount_t users;
	struct list_head list; /* Protected by the RTNL_LOCK */
	u32 dma_pages_cnt;
};

struct xsk_buff_pool {
	/* Members only used in the control path first. */
	struct device *dev;
	struct net_device *netdev;
	struct list_head xsk_tx_list;
	/* Protects modifications to the xsk_tx_list */
	spinlock_t xsk_tx_list_lock;
	refcount_t users;
	struct xdp_umem *umem;
	struct work_struct work;
	struct list_head free_list;
	struct list_head xskb_list;
	u32 heads_cnt;
	u16 queue_id;

	/* Data path members as close to free_heads at the end as possible. */
	struct xsk_queue *fq ____cacheline_aligned_in_smp;
	struct xsk_queue *cq;
	/* For performance reasons, each buff pool has its own array of dma_pages
	 * even when they are identical.
	 */
	dma_addr_t *dma_pages;
	struct xdp_buff_xsk *heads;
	struct xdp_desc *tx_descs;
	u64 chunk_mask;
	u64 addrs_cnt;
	u32 free_list_cnt;
	u32 dma_pages_cnt;
	u32 free_heads_cnt;
	u32 headroom;
	u32 chunk_size;
	u32 chunk_shift;
	u32 frame_len;
	u8 tx_metadata_len; /* inherited from umem */
	u8 cached_need_wakeup;
	bool uses_need_wakeup;
	bool unaligned;
	bool tx_sw_csum;
	void *addrs;
	/* Mutual exclusion of the completion ring in the SKB mode. Two cases to protect:
	 * NAPI TX thread and sendmsg error paths in the SKB destructor callback and when
	 * sockets share a single cq when the same netdev and queue id is shared.
	 */
	spinlock_t cq_lock;
	struct xdp_buff_xsk *free_heads[];
};

/* Masks for xdp_umem_page flags.
 * The low 12-bits of the addr will be 0 since this is the page address, so we
 * can use them for flags.
 */
#define XSK_NEXT_PG_CONTIG_SHIFT 0
#define XSK_NEXT_PG_CONTIG_MASK BIT_ULL(XSK_NEXT_PG_CONTIG_SHIFT)

/* AF_XDP core. */
struct xsk_buff_pool *xp_create_and_assign_umem(struct xdp_sock *xs,
						struct xdp_umem *umem);
int xp_assign_dev(struct xsk_buff_pool *pool, struct net_device *dev,
		  u16 queue_id, u16 flags);
int xp_assign_dev_shared(struct xsk_buff_pool *pool, struct xdp_sock *umem_xs,
			 struct net_device *dev, u16 queue_id);
int xp_alloc_tx_descs(struct xsk_buff_pool *pool, struct xdp_sock *xs);
void xp_destroy(struct xsk_buff_pool *pool);
void xp_get_pool(struct xsk_buff_pool *pool);
bool xp_put_pool(struct xsk_buff_pool *pool);
void xp_clear_dev(struct xsk_buff_pool *pool);
void xp_add_xsk(struct xsk_buff_pool *pool, struct xdp_sock *xs);
void xp_del_xsk(struct xsk_buff_pool *pool, struct xdp_sock *xs);

/* AF_XDP, and XDP core. */
void xp_free(struct xdp_buff_xsk *xskb);

static inline void xp_init_xskb_addr(struct xdp_buff_xsk *xskb, struct xsk_buff_pool *pool,
				     u64 addr)
{
	xskb->orig_addr = addr;
	xskb->xdp.data_hard_start = pool->addrs + addr + pool->headroom;
}

static inline void xp_init_xskb_dma(struct xdp_buff_xsk *xskb, struct xsk_buff_pool *pool,
				    dma_addr_t *dma_pages, u64 addr)
{
	xskb->frame_dma = (dma_pages[addr >> PAGE_SHIFT] & ~XSK_NEXT_PG_CONTIG_MASK) +
		(addr & ~PAGE_MASK);
	xskb->dma = xskb->frame_dma + pool->headroom + XDP_PACKET_HEADROOM;
}

/* AF_XDP ZC drivers, via xdp_sock_buff.h */
void xp_set_rxq_info(struct xsk_buff_pool *pool, struct xdp_rxq_info *rxq);
void xp_fill_cb(struct xsk_buff_pool *pool, struct xsk_cb_desc *desc);
int xp_dma_map(struct xsk_buff_pool *pool, struct device *dev,
	       unsigned long attrs, struct page **pages, u32 nr_pages);
void xp_dma_unmap(struct xsk_buff_pool *pool, unsigned long attrs);
struct xdp_buff *xp_alloc(struct xsk_buff_pool *pool);
u32 xp_alloc_batch(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max);
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

static inline void xp_dma_sync_for_cpu(struct xdp_buff_xsk *xskb)
{
	dma_sync_single_for_cpu(xskb->pool->dev, xskb->dma,
				xskb->pool->frame_len,
				DMA_BIDIRECTIONAL);
}

static inline void xp_dma_sync_for_device(struct xsk_buff_pool *pool,
					  dma_addr_t dma, size_t size)
{
	dma_sync_single_for_device(pool->dev, dma, size, DMA_BIDIRECTIONAL);
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

	if (likely(!cross_pg))
		return false;

	return pool->dma_pages &&
	       !(pool->dma_pages[addr >> PAGE_SHIFT] & XSK_NEXT_PG_CONTIG_MASK);
}

static inline bool xp_mb_desc(struct xdp_desc *desc)
{
	return desc->options & XDP_PKT_CONTD;
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

static inline u32 xp_aligned_extract_idx(struct xsk_buff_pool *pool, u64 addr)
{
	return xp_aligned_extract_addr(pool, addr) >> pool->chunk_shift;
}

static inline void xp_release(struct xdp_buff_xsk *xskb)
{
	if (xskb->pool->unaligned)
		xskb->pool->free_heads[xskb->pool->free_heads_cnt++] = xskb;
}

static inline u64 xp_get_handle(struct xdp_buff_xsk *xskb)
{
	u64 offset = xskb->xdp.data - xskb->xdp.data_hard_start;

	offset += xskb->pool->headroom;
	if (!xskb->pool->unaligned)
		return xskb->orig_addr + offset;
	return xskb->orig_addr + (offset << XSK_UNALIGNED_BUF_OFFSET_SHIFT);
}

static inline bool xp_tx_metadata_enabled(const struct xsk_buff_pool *pool)
{
	return pool->tx_metadata_len > 0;
}

#endif /* XSK_BUFF_POOL_H_ */
