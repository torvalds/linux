/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Definitions for the 'struct skb_array' datastructure.
 *
 *	Author:
 *		Michael S. Tsirkin <mst@redhat.com>
 *
 *	Copyright (C) 2016 Red Hat, Inc.
 *
 *	Limited-size FIFO of skbs. Can be used more or less whenever
 *	sk_buff_head can be used, except you need to know the queue size in
 *	advance.
 *	Implemented as a type-safe wrapper around ptr_ring.
 */

#ifndef _LINUX_SKB_ARRAY_H
#define _LINUX_SKB_ARRAY_H 1

#ifdef __KERNEL__
#include <linux/ptr_ring.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#endif

struct skb_array {
	struct ptr_ring ring;
};

/* Might be slightly faster than skb_array_full below, but callers invoking
 * this in a loop must use a compiler barrier, for example cpu_relax().
 */
static inline bool __skb_array_full(struct skb_array *a)
{
	return __ptr_ring_full(&a->ring);
}

static inline bool skb_array_full(struct skb_array *a)
{
	return ptr_ring_full(&a->ring);
}

static inline int skb_array_produce(struct skb_array *a, struct sk_buff *skb)
{
	return ptr_ring_produce(&a->ring, skb);
}

static inline int skb_array_produce_irq(struct skb_array *a, struct sk_buff *skb)
{
	return ptr_ring_produce_irq(&a->ring, skb);
}

static inline int skb_array_produce_bh(struct skb_array *a, struct sk_buff *skb)
{
	return ptr_ring_produce_bh(&a->ring, skb);
}

static inline int skb_array_produce_any(struct skb_array *a, struct sk_buff *skb)
{
	return ptr_ring_produce_any(&a->ring, skb);
}

/* Might be slightly faster than skb_array_empty below, but only safe if the
 * array is never resized. Also, callers invoking this in a loop must take care
 * to use a compiler barrier, for example cpu_relax().
 */
static inline bool __skb_array_empty(struct skb_array *a)
{
	return __ptr_ring_empty(&a->ring);
}

static inline struct sk_buff *__skb_array_peek(struct skb_array *a)
{
	return __ptr_ring_peek(&a->ring);
}

static inline bool skb_array_empty(struct skb_array *a)
{
	return ptr_ring_empty(&a->ring);
}

static inline bool skb_array_empty_bh(struct skb_array *a)
{
	return ptr_ring_empty_bh(&a->ring);
}

static inline bool skb_array_empty_irq(struct skb_array *a)
{
	return ptr_ring_empty_irq(&a->ring);
}

static inline bool skb_array_empty_any(struct skb_array *a)
{
	return ptr_ring_empty_any(&a->ring);
}

static inline struct sk_buff *__skb_array_consume(struct skb_array *a)
{
	return __ptr_ring_consume(&a->ring);
}

static inline struct sk_buff *skb_array_consume(struct skb_array *a)
{
	return ptr_ring_consume(&a->ring);
}

static inline int skb_array_consume_batched(struct skb_array *a,
					    struct sk_buff **array, int n)
{
	return ptr_ring_consume_batched(&a->ring, (void **)array, n);
}

static inline struct sk_buff *skb_array_consume_irq(struct skb_array *a)
{
	return ptr_ring_consume_irq(&a->ring);
}

static inline int skb_array_consume_batched_irq(struct skb_array *a,
						struct sk_buff **array, int n)
{
	return ptr_ring_consume_batched_irq(&a->ring, (void **)array, n);
}

static inline struct sk_buff *skb_array_consume_any(struct skb_array *a)
{
	return ptr_ring_consume_any(&a->ring);
}

static inline int skb_array_consume_batched_any(struct skb_array *a,
						struct sk_buff **array, int n)
{
	return ptr_ring_consume_batched_any(&a->ring, (void **)array, n);
}


static inline struct sk_buff *skb_array_consume_bh(struct skb_array *a)
{
	return ptr_ring_consume_bh(&a->ring);
}

static inline int skb_array_consume_batched_bh(struct skb_array *a,
					       struct sk_buff **array, int n)
{
	return ptr_ring_consume_batched_bh(&a->ring, (void **)array, n);
}

static inline int __skb_array_len_with_tag(struct sk_buff *skb)
{
	if (likely(skb)) {
		int len = skb->len;

		if (skb_vlan_tag_present(skb))
			len += VLAN_HLEN;

		return len;
	} else {
		return 0;
	}
}

static inline int skb_array_peek_len(struct skb_array *a)
{
	return PTR_RING_PEEK_CALL(&a->ring, __skb_array_len_with_tag);
}

static inline int skb_array_peek_len_irq(struct skb_array *a)
{
	return PTR_RING_PEEK_CALL_IRQ(&a->ring, __skb_array_len_with_tag);
}

static inline int skb_array_peek_len_bh(struct skb_array *a)
{
	return PTR_RING_PEEK_CALL_BH(&a->ring, __skb_array_len_with_tag);
}

static inline int skb_array_peek_len_any(struct skb_array *a)
{
	return PTR_RING_PEEK_CALL_ANY(&a->ring, __skb_array_len_with_tag);
}

static inline int skb_array_init(struct skb_array *a, int size, gfp_t gfp)
{
	return ptr_ring_init(&a->ring, size, gfp);
}

static void __skb_array_destroy_skb(void *ptr)
{
	kfree_skb(ptr);
}

static inline void skb_array_unconsume(struct skb_array *a,
				       struct sk_buff **skbs, int n)
{
	ptr_ring_unconsume(&a->ring, (void **)skbs, n, __skb_array_destroy_skb);
}

static inline int skb_array_resize(struct skb_array *a, int size, gfp_t gfp)
{
	return ptr_ring_resize(&a->ring, size, gfp, __skb_array_destroy_skb);
}

static inline int skb_array_resize_multiple(struct skb_array **rings,
					    int nrings, unsigned int size,
					    gfp_t gfp)
{
	BUILD_BUG_ON(offsetof(struct skb_array, ring));
	return ptr_ring_resize_multiple((struct ptr_ring **)rings,
					nrings, size, gfp,
					__skb_array_destroy_skb);
}

static inline void skb_array_cleanup(struct skb_array *a)
{
	ptr_ring_cleanup(&a->ring, __skb_array_destroy_skb);
}

#endif /* _LINUX_SKB_ARRAY_H  */
