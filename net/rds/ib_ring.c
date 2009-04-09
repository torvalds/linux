/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>

#include "rds.h"
#include "ib.h"

/*
 * Locking for IB rings.
 * We assume that allocation is always protected by a mutex
 * in the caller (this is a valid assumption for the current
 * implementation).
 *
 * Freeing always happens in an interrupt, and hence only
 * races with allocations, but not with other free()s.
 *
 * The interaction between allocation and freeing is that
 * the alloc code has to determine the number of free entries.
 * To this end, we maintain two counters; an allocation counter
 * and a free counter. Both are allowed to run freely, and wrap
 * around.
 * The number of used entries is always (alloc_ctr - free_ctr) % NR.
 *
 * The current implementation makes free_ctr atomic. When the
 * caller finds an allocation fails, it should set an "alloc fail"
 * bit and retry the allocation. The "alloc fail" bit essentially tells
 * the CQ completion handlers to wake it up after freeing some
 * more entries.
 */

/*
 * This only happens on shutdown.
 */
DECLARE_WAIT_QUEUE_HEAD(rds_ib_ring_empty_wait);

void rds_ib_ring_init(struct rds_ib_work_ring *ring, u32 nr)
{
	memset(ring, 0, sizeof(*ring));
	ring->w_nr = nr;
	rdsdebug("ring %p nr %u\n", ring, ring->w_nr);
}

static inline u32 __rds_ib_ring_used(struct rds_ib_work_ring *ring)
{
	u32 diff;

	/* This assumes that atomic_t has at least as many bits as u32 */
	diff = ring->w_alloc_ctr - (u32) atomic_read(&ring->w_free_ctr);
	BUG_ON(diff > ring->w_nr);

	return diff;
}

void rds_ib_ring_resize(struct rds_ib_work_ring *ring, u32 nr)
{
	/* We only ever get called from the connection setup code,
	 * prior to creating the QP. */
	BUG_ON(__rds_ib_ring_used(ring));
	ring->w_nr = nr;
}

static int __rds_ib_ring_empty(struct rds_ib_work_ring *ring)
{
	return __rds_ib_ring_used(ring) == 0;
}

u32 rds_ib_ring_alloc(struct rds_ib_work_ring *ring, u32 val, u32 *pos)
{
	u32 ret = 0, avail;

	avail = ring->w_nr - __rds_ib_ring_used(ring);

	rdsdebug("ring %p val %u next %u free %u\n", ring, val,
		 ring->w_alloc_ptr, avail);

	if (val && avail) {
		ret = min(val, avail);
		*pos = ring->w_alloc_ptr;

		ring->w_alloc_ptr = (ring->w_alloc_ptr + ret) % ring->w_nr;
		ring->w_alloc_ctr += ret;
	}

	return ret;
}

void rds_ib_ring_free(struct rds_ib_work_ring *ring, u32 val)
{
	ring->w_free_ptr = (ring->w_free_ptr + val) % ring->w_nr;
	atomic_add(val, &ring->w_free_ctr);

	if (__rds_ib_ring_empty(ring) &&
	    waitqueue_active(&rds_ib_ring_empty_wait))
		wake_up(&rds_ib_ring_empty_wait);
}

void rds_ib_ring_unalloc(struct rds_ib_work_ring *ring, u32 val)
{
	ring->w_alloc_ptr = (ring->w_alloc_ptr - val) % ring->w_nr;
	ring->w_alloc_ctr -= val;
}

int rds_ib_ring_empty(struct rds_ib_work_ring *ring)
{
	return __rds_ib_ring_empty(ring);
}

int rds_ib_ring_low(struct rds_ib_work_ring *ring)
{
	return __rds_ib_ring_used(ring) <= (ring->w_nr >> 1);
}

/*
 * returns the oldest alloced ring entry.  This will be the next one
 * freed.  This can't be called if there are none allocated.
 */
u32 rds_ib_ring_oldest(struct rds_ib_work_ring *ring)
{
	return ring->w_free_ptr;
}

/*
 * returns the number of completed work requests.
 */

u32 rds_ib_ring_completed(struct rds_ib_work_ring *ring, u32 wr_id, u32 oldest)
{
	u32 ret;

	if (oldest <= (unsigned long long)wr_id)
		ret = (unsigned long long)wr_id - oldest + 1;
	else
		ret = ring->w_nr - oldest + (unsigned long long)wr_id + 1;

	rdsdebug("ring %p ret %u wr_id %u oldest %u\n", ring, ret,
		 wr_id, oldest);
	return ret;
}
