/* SPDX-License-Identifier: GPL-2.0
 * XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef XDP_UMEM_H_
#define XDP_UMEM_H_

#include <linux/mm.h>
#include <linux/if_xdp.h>
#include <linux/workqueue.h>

#include "xsk_queue.h"
#include "xdp_umem_props.h"

struct xdp_umem {
	struct xsk_queue *fq;
	struct page **pgs;
	struct xdp_umem_props props;
	u32 npgs;
	u32 frame_headroom;
	u32 nfpp_mask;
	u32 nfpplog2;
	u32 frame_size_log2;
	struct user_struct *user;
	struct pid *pid;
	unsigned long address;
	size_t size;
	atomic_t users;
	struct work_struct work;
};

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u32 idx)
{
	u64 pg, off;
	char *data;

	pg = idx >> umem->nfpplog2;
	off = (idx & umem->nfpp_mask) << umem->frame_size_log2;

	data = page_address(umem->pgs[pg]);
	return data + off;
}

static inline char *xdp_umem_get_data_with_headroom(struct xdp_umem *umem,
						    u32 idx)
{
	return xdp_umem_get_data(umem, idx) + umem->frame_headroom;
}

bool xdp_umem_validate_queues(struct xdp_umem *umem);
int xdp_umem_reg(struct xdp_umem *umem, struct xdp_umem_reg *mr);
void xdp_get_umem(struct xdp_umem *umem);
void xdp_put_umem(struct xdp_umem *umem);
int xdp_umem_create(struct xdp_umem **umem);

#endif /* XDP_UMEM_H_ */
