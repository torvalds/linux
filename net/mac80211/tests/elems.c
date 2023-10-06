// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for element parsing
 *
 * Copyright (C) 2023 Intel Corporation
 */
#include <kunit/test.h>
#include "../ieee80211_i.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

static void mle_defrag(struct kunit *test)
{
	struct ieee80211_elems_parse_params parse_params = {
		.link_id = 12,
		.from_ap = true,
	};
	struct ieee802_11_elems *parsed;
	struct sk_buff *skb;
	u8 *len_mle, *len_prof;
	int i;

	skb = alloc_skb(1024, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);

	if (skb_pad(skb, skb_tailroom(skb))) {
		KUNIT_FAIL(test, "failed to pad skb");
		return;
	}

	/* build a multi-link element */
	skb_put_u8(skb, WLAN_EID_EXTENSION);
	len_mle = skb_put(skb, 1);
	skb_put_u8(skb, WLAN_EID_EXT_EHT_MULTI_LINK);

	put_unaligned_le16(IEEE80211_ML_CONTROL_TYPE_BASIC,
			   skb_put(skb, 2));
	/* struct ieee80211_mle_basic_common_info */
	skb_put_u8(skb, 7); /* includes len field */
	skb_put_data(skb, "\x00\x00\x00\x00\x00\x00", ETH_ALEN); /* MLD addr */

	/* with a STA profile inside */
	skb_put_u8(skb, IEEE80211_MLE_SUBELEM_PER_STA_PROFILE);
	len_prof = skb_put(skb, 1);
	put_unaligned_le16(IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE |
			   parse_params.link_id,
			   skb_put(skb, 2));
	skb_put_u8(skb, 1); /* fake sta_info_len - includes itself */
	/* put a bunch of useless elements into it */
	for (i = 0; i < 20; i++) {
		skb_put_u8(skb, WLAN_EID_SSID);
		skb_put_u8(skb, 20);
		skb_put(skb, 20);
	}

	/* fragment STA profile */
	ieee80211_fragment_element(skb, len_prof,
				   IEEE80211_MLE_SUBELEM_FRAGMENT);
	/* fragment MLE */
	ieee80211_fragment_element(skb, len_mle, WLAN_EID_FRAGMENT);

	parse_params.start = skb->data;
	parse_params.len = skb->len;
	parsed = ieee802_11_parse_elems_full(&parse_params);
	/* should return ERR_PTR or valid, not NULL */
	KUNIT_EXPECT_NOT_NULL(test, parsed);

	if (IS_ERR_OR_NULL(parsed))
		goto free_skb;

	KUNIT_EXPECT_NOT_NULL(test, parsed->ml_basic_elem);
	KUNIT_EXPECT_EQ(test,
			parsed->ml_basic_len,
			2 /* control */ +
			7 /* common info */ +
			2 /* sta profile element header */ +
			3 /* sta profile header */ +
			20 * 22 /* sta profile data */ +
			2 /* sta profile fragment element */);
	KUNIT_EXPECT_NOT_NULL(test, parsed->prof);
	KUNIT_EXPECT_EQ(test,
			parsed->sta_prof_len,
			3 /* sta profile header */ +
			20 * 22 /* sta profile data */);

	kfree(parsed);
free_skb:
	kfree_skb(skb);
}

static struct kunit_case element_parsing_test_cases[] = {
	KUNIT_CASE(mle_defrag),
	{}
};

static struct kunit_suite element_parsing = {
	.name = "mac80211-element-parsing",
	.test_cases = element_parsing_test_cases,
};

kunit_test_suite(element_parsing);
