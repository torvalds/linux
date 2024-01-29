/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Utilities for cfg80211 unit testing
 *
 * Copyright (C) 2023 Intel Corporation
 */
#ifndef __CFG80211_UTILS_H
#define __CFG80211_UTILS_H

#define CHAN2G(_freq)  { \
	.band = NL80211_BAND_2GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
}

static const struct ieee80211_channel channels_2ghz[] = {
	CHAN2G(2412), /* Channel 1 */
	CHAN2G(2417), /* Channel 2 */
	CHAN2G(2422), /* Channel 3 */
	CHAN2G(2427), /* Channel 4 */
	CHAN2G(2432), /* Channel 5 */
	CHAN2G(2437), /* Channel 6 */
	CHAN2G(2442), /* Channel 7 */
	CHAN2G(2447), /* Channel 8 */
	CHAN2G(2452), /* Channel 9 */
	CHAN2G(2457), /* Channel 10 */
	CHAN2G(2462), /* Channel 11 */
	CHAN2G(2467), /* Channel 12 */
	CHAN2G(2472), /* Channel 13 */
	CHAN2G(2484), /* Channel 14 */
};

struct t_wiphy_priv {
	struct kunit *test;
	struct cfg80211_ops *ops;

	void *ctx;

	struct ieee80211_supported_band band_2ghz;
	struct ieee80211_channel channels_2ghz[ARRAY_SIZE(channels_2ghz)];
};

#define T_WIPHY(test, ctx) ({						\
		struct wiphy *__wiphy =					\
			kunit_alloc_resource(test, t_wiphy_init,	\
					     t_wiphy_exit,		\
					     GFP_KERNEL, &(ctx));	\
									\
		KUNIT_ASSERT_NOT_NULL(test, __wiphy);			\
		__wiphy;						\
	})
#define t_wiphy_ctx(wiphy) (((struct t_wiphy_priv *)wiphy_priv(wiphy))->ctx)

int t_wiphy_init(struct kunit_resource *resource, void *data);
void t_wiphy_exit(struct kunit_resource *resource);

#define t_skb_remove_member(skb, type, member)	do {				\
		memmove((skb)->data + (skb)->len - sizeof(type) +		\
			offsetof(type, member),					\
			(skb)->data + (skb)->len - sizeof(type) +		\
			offsetofend(type, member),				\
			offsetofend(type, member));				\
		skb_trim(skb, (skb)->len - sizeof_field(type, member));		\
	} while (0)

#endif /* __CFG80211_UTILS_H */
