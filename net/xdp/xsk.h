/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. */

#ifndef XSK_H_
#define XSK_H_

/* Masks for xdp_umem_page flags.
 * The low 12-bits of the addr will be 0 since this is the page address, so we
 * can use them for flags.
 */
#define XSK_NEXT_PG_CONTIG_SHIFT 0
#define XSK_NEXT_PG_CONTIG_MASK BIT_ULL(XSK_NEXT_PG_CONTIG_SHIFT)

struct xdp_ring_offset_v1 {
	__u64 producer;
	__u64 consumer;
	__u64 desc;
};

struct xdp_mmap_offsets_v1 {
	struct xdp_ring_offset_v1 rx;
	struct xdp_ring_offset_v1 tx;
	struct xdp_ring_offset_v1 fr;
	struct xdp_ring_offset_v1 cr;
};

/* Nodes are linked in the struct xdp_sock map_list field, and used to
 * track which maps a certain socket reside in.
 */

struct xsk_map_node {
	struct list_head node;
	struct xsk_map *map;
	struct xdp_sock **map_entry;
};

static inline struct xdp_sock *xdp_sk(struct sock *sk)
{
	return (struct xdp_sock *)sk;
}

void xsk_map_try_sock_delete(struct xsk_map *map, struct xdp_sock *xs,
			     struct xdp_sock **map_entry);
int xsk_map_inc(struct xsk_map *map);
void xsk_map_put(struct xsk_map *map);
void xsk_clear_pool_at_qid(struct net_device *dev, u16 queue_id);
int xsk_reg_pool_at_qid(struct net_device *dev, struct xsk_buff_pool *pool,
			u16 queue_id);

#endif /* XSK_H_ */
