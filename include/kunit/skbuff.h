/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit resource management helpers for SKBs (skbuff).
 *
 * Copyright (C) 2023 Intel Corporation
 */

#ifndef _KUNIT_SKBUFF_H
#define _KUNIT_SKBUFF_H

#include <kunit/resource.h>
#include <linux/skbuff.h>

static void kunit_action_kfree_skb(void *p)
{
	kfree_skb((struct sk_buff *)p);
}

/**
 * kunit_zalloc_skb() - Allocate and initialize a resource managed skb.
 * @test: The test case to which the skb belongs
 * @len: size to allocate
 * @gfp: allocation flags
 *
 * Allocate a new struct sk_buff with gfp flags, zero fill the given length
 * and add it as a resource to the kunit test for automatic cleanup.
 *
 * Returns: newly allocated SKB, or %NULL on error
 */
static inline struct sk_buff *kunit_zalloc_skb(struct kunit *test, int len,
					       gfp_t gfp)
{
	struct sk_buff *res = alloc_skb(len, gfp);

	if (!res || skb_pad(res, len))
		return NULL;

	if (kunit_add_action_or_reset(test, kunit_action_kfree_skb, res))
		return NULL;

	return res;
}

/**
 * kunit_kfree_skb() - Like kfree_skb except for allocations managed by KUnit.
 * @test: The test case to which the resource belongs.
 * @skb: The SKB to free.
 */
static inline void kunit_kfree_skb(struct kunit *test, struct sk_buff *skb)
{
	if (!skb)
		return;

	kunit_release_action(test, kunit_action_kfree_skb, (void *)skb);
}

#endif /* _KUNIT_SKBUFF_H */
