// SPDX-License-Identifier: GPL-2.0
/* XDP user-space ring structure
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/overflow.h>
#include <linux/vmalloc.h>
#include <net/xdp_sock_drv.h>

#include "xsk_queue.h"

static size_t xskq_get_ring_size(struct xsk_queue *q, bool umem_queue)
{
	struct xdp_umem_ring *umem_ring;
	struct xdp_rxtx_ring *rxtx_ring;

	if (umem_queue)
		return struct_size(umem_ring, desc, q->nentries);
	return struct_size(rxtx_ring, desc, q->nentries);
}

struct xsk_queue *xskq_create(u32 nentries, bool umem_queue)
{
	struct xsk_queue *q;
	size_t size;

	q = kzalloc(sizeof(*q), GFP_KERNEL);
	if (!q)
		return NULL;

	q->nentries = nentries;
	q->ring_mask = nentries - 1;

	size = xskq_get_ring_size(q, umem_queue);

	/* size which is overflowing or close to SIZE_MAX will become 0 in
	 * PAGE_ALIGN(), checking SIZE_MAX is enough due to the previous
	 * is_power_of_2(), the rest will be handled by vmalloc_user()
	 */
	if (unlikely(size == SIZE_MAX)) {
		kfree(q);
		return NULL;
	}

	size = PAGE_ALIGN(size);

	q->ring = vmalloc_user(size);
	if (!q->ring) {
		kfree(q);
		return NULL;
	}

	q->ring_vmalloc_size = size;
	return q;
}

void xskq_destroy(struct xsk_queue *q)
{
	if (!q)
		return;

	vfree(q->ring);
	kfree(q);
}
