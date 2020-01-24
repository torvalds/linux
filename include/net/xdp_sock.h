/* SPDX-License-Identifier: GPL-2.0 */
/* AF_XDP internal functions
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_H
#define _LINUX_XDP_SOCK_H

#include <linux/workqueue.h>
#include <linux/if_xdp.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <net/sock.h>

struct net_device;
struct xsk_queue;

/* Masks for xdp_umem_page flags.
 * The low 12-bits of the addr will be 0 since this is the page address, so we
 * can use them for flags.
 */
#define XSK_NEXT_PG_CONTIG_SHIFT 0
#define XSK_NEXT_PG_CONTIG_MASK (1ULL << XSK_NEXT_PG_CONTIG_SHIFT)

struct xdp_umem_page {
	void *addr;
	dma_addr_t dma;
};

struct xdp_umem_fq_reuse {
	u32 nentries;
	u32 length;
	u64 handles[];
};

/* Flags for the umem flags field.
 *
 * The NEED_WAKEUP flag is 1 due to the reuse of the flags field for public
 * flags. See inlude/uapi/include/linux/if_xdp.h.
 */
#define XDP_UMEM_USES_NEED_WAKEUP (1 << 1)

struct xdp_umem {
	struct xsk_queue *fq;
	struct xsk_queue *cq;
	struct xdp_umem_page *pages;
	u64 chunk_mask;
	u64 size;
	u32 headroom;
	u32 chunk_size_nohr;
	struct user_struct *user;
	unsigned long address;
	refcount_t users;
	struct work_struct work;
	struct page **pgs;
	u32 npgs;
	u16 queue_id;
	u8 need_wakeup;
	u8 flags;
	int id;
	struct net_device *dev;
	struct xdp_umem_fq_reuse *fq_reuse;
	bool zc;
	spinlock_t xsk_list_lock;
	struct list_head xsk_list;
};

/* Nodes are linked in the struct xdp_sock map_list field, and used to
 * track which maps a certain socket reside in.
 */

struct xsk_map {
	struct bpf_map map;
	struct list_head __percpu *flush_list;
	spinlock_t lock; /* Synchronize map updates */
	struct xdp_sock *xsk_map[];
};

struct xsk_map_node {
	struct list_head node;
	struct xsk_map *map;
	struct xdp_sock **map_entry;
};

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xsk_queue *rx;
	struct net_device *dev;
	struct xdp_umem *umem;
	struct list_head flush_node;
	u16 queue_id;
	bool zc;
	enum {
		XSK_READY = 0,
		XSK_BOUND,
		XSK_UNBOUND,
	} state;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	struct xsk_queue *tx ____cacheline_aligned_in_smp;
	struct list_head list;
	/* Mutual exclusion of NAPI TX thread and sendmsg error paths
	 * in the SKB destructor callback.
	 */
	spinlock_t tx_completion_lock;
	/* Protects generic receive. */
	spinlock_t rx_lock;
	u64 rx_dropped;
	struct list_head map_list;
	/* Protects map_list */
	spinlock_t map_list_lock;
};

struct xdp_buff;
#ifdef CONFIG_XDP_SOCKETS
int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
bool xsk_is_setup_for_bpf_map(struct xdp_sock *xs);
/* Used from netdev driver */
bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt);
u64 *xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr);
void xsk_umem_discard_addr(struct xdp_umem *umem);
void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries);
bool xsk_umem_consume_tx(struct xdp_umem *umem, struct xdp_desc *desc);
void xsk_umem_consume_tx_done(struct xdp_umem *umem);
struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries);
struct xdp_umem_fq_reuse *xsk_reuseq_swap(struct xdp_umem *umem,
					  struct xdp_umem_fq_reuse *newq);
void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq);
struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev, u16 queue_id);
void xsk_set_rx_need_wakeup(struct xdp_umem *umem);
void xsk_set_tx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_rx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_tx_need_wakeup(struct xdp_umem *umem);
bool xsk_umem_uses_need_wakeup(struct xdp_umem *umem);

void xsk_map_try_sock_delete(struct xsk_map *map, struct xdp_sock *xs,
			     struct xdp_sock **map_entry);
int xsk_map_inc(struct xsk_map *map);
void xsk_map_put(struct xsk_map *map);
int __xsk_map_redirect(struct bpf_map *map, struct xdp_buff *xdp,
		       struct xdp_sock *xs);
void __xsk_map_flush(struct bpf_map *map);

static inline struct xdp_sock *__xsk_map_lookup_elem(struct bpf_map *map,
						     u32 key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *xs;

	if (key >= map->max_entries)
		return NULL;

	xs = READ_ONCE(m->xsk_map[key]);
	return xs;
}

static inline u64 xsk_umem_extract_addr(u64 addr)
{
	return addr & XSK_UNALIGNED_BUF_ADDR_MASK;
}

static inline u64 xsk_umem_extract_offset(u64 addr)
{
	return addr >> XSK_UNALIGNED_BUF_OFFSET_SHIFT;
}

static inline u64 xsk_umem_add_offset_to_addr(u64 addr)
{
	return xsk_umem_extract_addr(addr) + xsk_umem_extract_offset(addr);
}

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	unsigned long page_addr;

	addr = xsk_umem_add_offset_to_addr(addr);
	page_addr = (unsigned long)umem->pages[addr >> PAGE_SHIFT].addr;

	return (char *)(page_addr & PAGE_MASK) + (addr & ~PAGE_MASK);
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	addr = xsk_umem_add_offset_to_addr(addr);

	return umem->pages[addr >> PAGE_SHIFT].dma + (addr & ~PAGE_MASK);
}

/* Reuse-queue aware version of FILL queue helpers */
static inline bool xsk_umem_has_addrs_rq(struct xdp_umem *umem, u32 cnt)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (rq->length >= cnt)
		return true;

	return xsk_umem_has_addrs(umem, cnt - rq->length);
}

static inline u64 *xsk_umem_peek_addr_rq(struct xdp_umem *umem, u64 *addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		return xsk_umem_peek_addr(umem, addr);

	*addr = rq->handles[rq->length - 1];
	return addr;
}

static inline void xsk_umem_discard_addr_rq(struct xdp_umem *umem)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		xsk_umem_discard_addr(umem);
	else
		rq->length--;
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	rq->handles[rq->length++] = addr;
}

/* Handle the offset appropriately depending on aligned or unaligned mode.
 * For unaligned mode, we store the offset in the upper 16-bits of the address.
 * For aligned mode, we simply add the offset to the address.
 */
static inline u64 xsk_umem_adjust_offset(struct xdp_umem *umem, u64 address,
					 u64 offset)
{
	if (umem->flags & XDP_UMEM_UNALIGNED_CHUNK_FLAG)
		return address + (offset << XSK_UNALIGNED_BUF_OFFSET_SHIFT);
	else
		return address + offset;
}
#else
static inline int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	return -ENOTSUPP;
}

static inline bool xsk_is_setup_for_bpf_map(struct xdp_sock *xs)
{
	return false;
}

static inline bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt)
{
	return false;
}

static inline u64 *xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr)
{
	return NULL;
}

static inline void xsk_umem_discard_addr(struct xdp_umem *umem)
{
}

static inline void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries)
{
}

static inline bool xsk_umem_consume_tx(struct xdp_umem *umem,
				       struct xdp_desc *desc)
{
	return false;
}

static inline void xsk_umem_consume_tx_done(struct xdp_umem *umem)
{
}

static inline struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries)
{
	return NULL;
}

static inline struct xdp_umem_fq_reuse *xsk_reuseq_swap(
	struct xdp_umem *umem,
	struct xdp_umem_fq_reuse *newq)
{
	return NULL;
}
static inline void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq)
{
}

static inline struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev,
						     u16 queue_id)
{
	return NULL;
}

static inline u64 xsk_umem_extract_addr(u64 addr)
{
	return 0;
}

static inline u64 xsk_umem_extract_offset(u64 addr)
{
	return 0;
}

static inline u64 xsk_umem_add_offset_to_addr(u64 addr)
{
	return 0;
}

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	return NULL;
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	return 0;
}

static inline bool xsk_umem_has_addrs_rq(struct xdp_umem *umem, u32 cnt)
{
	return false;
}

static inline u64 *xsk_umem_peek_addr_rq(struct xdp_umem *umem, u64 *addr)
{
	return NULL;
}

static inline void xsk_umem_discard_addr_rq(struct xdp_umem *umem)
{
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
}

static inline void xsk_set_rx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_set_tx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_clear_rx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_clear_tx_need_wakeup(struct xdp_umem *umem)
{
}

static inline bool xsk_umem_uses_need_wakeup(struct xdp_umem *umem)
{
	return false;
}

static inline u64 xsk_umem_adjust_offset(struct xdp_umem *umem, u64 handle,
					 u64 offset)
{
	return 0;
}

static inline int __xsk_map_redirect(struct bpf_map *map, struct xdp_buff *xdp,
				     struct xdp_sock *xs)
{
	return -EOPNOTSUPP;
}

static inline void __xsk_map_flush(struct bpf_map *map)
{
}

static inline struct xdp_sock *__xsk_map_lookup_elem(struct bpf_map *map,
						     u32 key)
{
	return NULL;
}
#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_H */
