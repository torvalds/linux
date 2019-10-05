/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2019 Intel Corporation. */

#ifndef XSK_H_
#define XSK_H_

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

static inline struct xdp_sock *xdp_sk(struct sock *sk)
{
	return (struct xdp_sock *)sk;
}

#endif /* XSK_H_ */
