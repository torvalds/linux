/*
 * Copyright (C) 2016 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Partial implementation of virtio 0.9. event index is used for signalling,
 * unconditionally. Design roughly follows linux kernel implementation in order
 * to be able to judge its performance.
 */
#define _GNU_SOURCE
#include "main.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <linux/virtio_ring.h>

struct data {
	void *data;
} *data;

struct vring ring;

/* enabling the below activates experimental ring polling code
 * (which skips index reads on consumer in favor of looking at
 * high bits of ring id ^ 0x8000).
 */
/* #ifdef RING_POLL */
/* enabling the below activates experimental in-order code
 * (which skips ring updates and reads and writes len in descriptor).
 */
/* #ifdef INORDER */

#if defined(RING_POLL) && defined(INORDER)
#error "RING_POLL and INORDER are mutually exclusive"
#endif

/* how much padding is needed to avoid false cache sharing */
#define HOST_GUEST_PADDING 0x80

struct guest {
	unsigned short avail_idx;
	unsigned short last_used_idx;
	unsigned short num_free;
	unsigned short kicked_avail_idx;
#ifndef INORDER
	unsigned short free_head;
#else
	unsigned short reserved_free_head;
#endif
	unsigned char reserved[HOST_GUEST_PADDING - 10];
} guest;

struct host {
	/* we do not need to track last avail index
	 * unless we have more than one in flight.
	 */
	unsigned short used_idx;
	unsigned short called_used_idx;
	unsigned char reserved[HOST_GUEST_PADDING - 4];
} host;

/* implemented by ring */
void alloc_ring(void)
{
	int ret;
	int i;
	void *p;

	ret = posix_memalign(&p, 0x1000, vring_size(ring_size, 0x1000));
	if (ret) {
		perror("Unable to allocate ring buffer.\n");
		exit(3);
	}
	memset(p, 0, vring_size(ring_size, 0x1000));
	vring_init(&ring, ring_size, p, 0x1000);

	guest.avail_idx = 0;
	guest.kicked_avail_idx = -1;
	guest.last_used_idx = 0;
#ifndef INORDER
	/* Put everything in free lists. */
	guest.free_head = 0;
#endif
	for (i = 0; i < ring_size - 1; i++)
		ring.desc[i].next = i + 1;
	host.used_idx = 0;
	host.called_used_idx = -1;
	guest.num_free = ring_size;
	data = malloc(ring_size * sizeof *data);
	if (!data) {
		perror("Unable to allocate data buffer.\n");
		exit(3);
	}
	memset(data, 0, ring_size * sizeof *data);
}

/* guest side */
int add_inbuf(unsigned len, void *buf, void *datap)
{
	unsigned head;
#ifndef INORDER
	unsigned avail;
#endif
	struct vring_desc *desc;

	if (!guest.num_free)
		return -1;

#ifdef INORDER
	head = (ring_size - 1) & (guest.avail_idx++);
#else
	head = guest.free_head;
#endif
	guest.num_free--;

	desc = ring.desc;
	desc[head].flags = VRING_DESC_F_NEXT;
	desc[head].addr = (unsigned long)(void *)buf;
	desc[head].len = len;
	/* We do it like this to simulate the way
	 * we'd have to flip it if we had multiple
	 * descriptors.
	 */
	desc[head].flags &= ~VRING_DESC_F_NEXT;
#ifndef INORDER
	guest.free_head = desc[head].next;
#endif

	data[head].data = datap;

#ifdef RING_POLL
	/* Barrier A (for pairing) */
	smp_release();
	avail = guest.avail_idx++;
	ring.avail->ring[avail & (ring_size - 1)] =
		(head | (avail & ~(ring_size - 1))) ^ 0x8000;
#else
#ifndef INORDER
	/* Barrier A (for pairing) */
	smp_release();
	avail = (ring_size - 1) & (guest.avail_idx++);
	ring.avail->ring[avail] = head;
#endif
	/* Barrier A (for pairing) */
	smp_release();
#endif
	ring.avail->idx = guest.avail_idx;
	return 0;
}

void *get_buf(unsigned *lenp, void **bufp)
{
	unsigned head;
	unsigned index;
	void *datap;

#ifdef RING_POLL
	head = (ring_size - 1) & guest.last_used_idx;
	index = ring.used->ring[head].id;
	if ((index ^ guest.last_used_idx ^ 0x8000) & ~(ring_size - 1))
		return NULL;
	/* Barrier B (for pairing) */
	smp_acquire();
	index &= ring_size - 1;
#else
	if (ring.used->idx == guest.last_used_idx)
		return NULL;
	/* Barrier B (for pairing) */
	smp_acquire();
#ifdef INORDER
	head = (ring_size - 1) & guest.last_used_idx;
	index = head;
#else
	head = (ring_size - 1) & guest.last_used_idx;
	index = ring.used->ring[head].id;
#endif

#endif
#ifdef INORDER
	*lenp = ring.desc[index].len;
#else
	*lenp = ring.used->ring[head].len;
#endif
	datap = data[index].data;
	*bufp = (void*)(unsigned long)ring.desc[index].addr;
	data[index].data = NULL;
#ifndef INORDER
	ring.desc[index].next = guest.free_head;
	guest.free_head = index;
#endif
	guest.num_free++;
	guest.last_used_idx++;
	return datap;
}

bool used_empty()
{
	unsigned short last_used_idx = guest.last_used_idx;
#ifdef RING_POLL
	unsigned short head = last_used_idx & (ring_size - 1);
	unsigned index = ring.used->ring[head].id;

	return (index ^ last_used_idx ^ 0x8000) & ~(ring_size - 1);
#else
	return ring.used->idx == last_used_idx;
#endif
}

void disable_call()
{
	/* Doing nothing to disable calls might cause
	 * extra interrupts, but reduces the number of cache misses.
	 */
}

bool enable_call()
{
	vring_used_event(&ring) = guest.last_used_idx;
	/* Flush call index write */
	/* Barrier D (for pairing) */
	smp_mb();
	return used_empty();
}

void kick_available(void)
{
	/* Flush in previous flags write */
	/* Barrier C (for pairing) */
	smp_mb();
	if (!vring_need_event(vring_avail_event(&ring),
			      guest.avail_idx,
			      guest.kicked_avail_idx))
		return;

	guest.kicked_avail_idx = guest.avail_idx;
	kick();
}

/* host side */
void disable_kick()
{
	/* Doing nothing to disable kicks might cause
	 * extra interrupts, but reduces the number of cache misses.
	 */
}

bool enable_kick()
{
	vring_avail_event(&ring) = host.used_idx;
	/* Barrier C (for pairing) */
	smp_mb();
	return avail_empty();
}

bool avail_empty()
{
	unsigned head = host.used_idx;
#ifdef RING_POLL
	unsigned index = ring.avail->ring[head & (ring_size - 1)];

	return ((index ^ head ^ 0x8000) & ~(ring_size - 1));
#else
	return head == ring.avail->idx;
#endif
}

bool use_buf(unsigned *lenp, void **bufp)
{
	unsigned used_idx = host.used_idx;
	struct vring_desc *desc;
	unsigned head;

#ifdef RING_POLL
	head = ring.avail->ring[used_idx & (ring_size - 1)];
	if ((used_idx ^ head ^ 0x8000) & ~(ring_size - 1))
		return false;
	/* Barrier A (for pairing) */
	smp_acquire();

	used_idx &= ring_size - 1;
	desc = &ring.desc[head & (ring_size - 1)];
#else
	if (used_idx == ring.avail->idx)
		return false;

	/* Barrier A (for pairing) */
	smp_acquire();

	used_idx &= ring_size - 1;
#ifdef INORDER
	head = used_idx;
#else
	head = ring.avail->ring[used_idx];
#endif
	desc = &ring.desc[head];
#endif

	*lenp = desc->len;
	*bufp = (void *)(unsigned long)desc->addr;

#ifdef INORDER
	desc->len = desc->len - 1;
#else
	/* now update used ring */
	ring.used->ring[used_idx].id = head;
	ring.used->ring[used_idx].len = desc->len - 1;
#endif
	/* Barrier B (for pairing) */
	smp_release();
	host.used_idx++;
	ring.used->idx = host.used_idx;
	
	return true;
}

void call_used(void)
{
	/* Flush in previous flags write */
	/* Barrier D (for pairing) */
	smp_mb();
	if (!vring_need_event(vring_used_event(&ring),
			      host.used_idx,
			      host.called_used_idx))
		return;

	host.called_used_idx = host.used_idx;
	call();
}
