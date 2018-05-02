/* SPDX-License-Identifier: GPL-2.0
 * XDP user-space ring structure
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

#ifndef _LINUX_XSK_QUEUE_H
#define _LINUX_XSK_QUEUE_H

#include <linux/types.h>
#include <linux/if_xdp.h>

#include "xdp_umem_props.h"

struct xsk_queue {
	struct xdp_umem_props umem_props;
	u32 ring_mask;
	u32 nentries;
	u32 prod_head;
	u32 prod_tail;
	u32 cons_head;
	u32 cons_tail;
	struct xdp_ring *ring;
	u64 invalid_descs;
};

struct xsk_queue *xskq_create(u32 nentries, bool umem_queue);
void xskq_destroy(struct xsk_queue *q);

#endif /* _LINUX_XSK_QUEUE_H */
