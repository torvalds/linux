// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for element parsing
 *
 * Copyright (C) 2023-2025 Intel Corporation
 */
#include <kunit/test.h>
#include "../ieee80211_i.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct mesh_preq_parse_test_case {
	const char *desc;
	u8 len;
	bool ae_enabled;
	u8 target_count;
	bool result;
} mesh_preq_parse_cases[] = {
	{
		.desc = "shorter than header",
		.len = 16,
		.ae_enabled = false,
		.target_count = 1,
		.result = false,
	},
	{
		.desc = "too short non AE, target count is not included",
		.len = 29,
		.ae_enabled = false,
		.target_count = 1,
		.result = false,
	},
	{
		.desc = "too short non AE, target count is 1",
		.len = 36,
		.ae_enabled = false,
		.target_count = 1,
		.result = false,
	},
	{
		.desc = "too short AE, target count is not included",
		.len = 35,
		.ae_enabled = true,
		.target_count = 1,
		.result = false,
	},
	{
		.desc = "too short AE, target count is 1",
		.len = 42,
		.ae_enabled = true,
		.target_count = 1,
		.result = false,
	},
	{
		.desc = "target count is zero",
		.len = 26,
		.ae_enabled = false,
		.target_count = 0,
		.result = false,
	},
	{
		.desc = "target count is 21",
		.len = 255,
		.ae_enabled = false,
		.target_count = 21,
		.result = false,
	},
	{
		.desc = "non AE, target count is 1",
		.len = 37,
		.ae_enabled = false,
		.target_count = 1,
		.result = true,
	},
	{
		.desc = "non AE, target count is 20",
		.len = 246,
		.ae_enabled = false,
		.target_count = 20,
		.result = true,
	},
	{
		.desc = "AE, target count is 1",
		.len = 43,
		.ae_enabled = true,
		.target_count = 1,
		.result = true,
	},
	{
		.desc = "AE, target count is 20",
		.len = 252,
		.ae_enabled = true,
		.target_count = 20,
		.result = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(mesh_preq_parse, mesh_preq_parse_cases, desc);

static const struct mesh_prep_parse_test_case {
	const char *desc;
	u8 len;
	bool ae_enabled;
	bool result;
} mesh_prep_parse_cases[] = {
	{
		.desc = "shorter than header",
		.len = 12,
		.ae_enabled = false,
		.result = false,
	},
	{
		.desc = "non AE short",
		.len = 30,
		.ae_enabled = false,
		.result = false,
	},
	{
		.desc = "non AE",
		.len = 31,
		.ae_enabled = false,
		.result = true,
	},
	{
		.desc = "AE short",
		.len = 36,
		.ae_enabled = true,
		.result = false,
	},
	{
		.desc = "AE",
		.len = 37,
		.ae_enabled = true,
		.result = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(mesh_prep_parse, mesh_prep_parse_cases, desc);

static const struct mesh_perr_parse_test_case {
	const char *desc;
	u8 len;
	u8 number_of_dst;
	int ae_enabled_idx;
	bool result;
} mesh_perr_parse_cases[] = {
	{
		.desc = "shorter than header",
		.len = 1,
		.number_of_dst = 1,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 0",
		.len = 2,
		.number_of_dst = 0,
		.ae_enabled_idx = -1,
		.result = true,
	},
	{
		.desc = "number_of_dst is 20",
		.len = 255,
		.number_of_dst = 20,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 1, non AE, short",
		.len = 14,
		.number_of_dst = 1,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 1, non AE",
		.len = 15,
		.number_of_dst = 1,
		.ae_enabled_idx = -1,
		.result = true,
	},
	{
		.desc = "number_of_dst is 1, non AE, extra short dst header",
		.len = 25,
		.number_of_dst = 1,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 1, non AE, extra dst header",
		.len = 26,
		.number_of_dst = 1,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 1, AE, short",
		.len = 20,
		.number_of_dst = 1,
		.ae_enabled_idx = 0,
		.result = false,
	},
	{
		.desc = "number_of_dst is 1, AE",
		.len = 21,
		.number_of_dst = 1,
		.ae_enabled_idx = 0,
		.result = true,
	},
	{
		.desc = "number_of_dst is 19, non AE, short",
		.len = 2 + 13 * 19 - 1,
		.number_of_dst = 19,
		.ae_enabled_idx = -1,
		.result = false,
	},
	{
		.desc = "number_of_dst is 19, non AE",
		.len = 2 + 13 * 19,
		.number_of_dst = 19,
		.ae_enabled_idx = -1,
		.result = true,
	},
	{
		.desc = "number_of_dst is 19, AE, short",
		.len = 2 + 13 * 19 + 6 - 1,
		.number_of_dst = 19,
		.ae_enabled_idx = 18,
		.result = false,
	},
	{
		.desc = "number_of_dst is 19, AE",
		.len = 2 + 13 * 19 + 6,
		.number_of_dst = 19,
		.ae_enabled_idx = 18,
		.result = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(mesh_perr_parse, mesh_perr_parse_cases, desc);


static void mle_defrag(struct kunit *test)
{
	struct ieee80211_elems_parse_params parse_params = {
		.link_id = 12,
		.from_ap = true,
		.mode = IEEE80211_CONN_MODE_EHT,
		/* type is not really relevant here */
		.type = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON,
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

	KUNIT_EXPECT_NOT_NULL(test, parsed->ml_basic);
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

static void mesh_preq_parse(struct kunit *test)
{
	const struct mesh_preq_parse_test_case *params = test->param_value;
	u8 data[64] = {};
	struct ieee80211_mesh_hwmp_preq_top *top = (void *)data;
	struct ieee80211_mesh_hwmp_preq_bottom *bottom;

	top->flags = params->ae_enabled ? AE_F : 0;
	bottom = ieee80211_mesh_hwmp_preq_get_bottom(data);
	bottom->target_count = params->target_count;

	KUNIT_EXPECT_EQ(test,
			ieee80211_mesh_preq_size_ok(data, params->len),
			params->result);
}

static void mesh_prep_parse(struct kunit *test)
{
	const struct mesh_prep_parse_test_case *params = test->param_value;
	u8 data[64] = {};
	struct ieee80211_mesh_hwmp_prep_top *top = (void *)data;
	top->flags = params->ae_enabled ? AE_F : 0;

	KUNIT_EXPECT_EQ(test,
			ieee80211_mesh_prep_size_ok(data, params->len),
			params->result);
}

static void mesh_perr_parse(struct kunit *test)
{
	const struct mesh_perr_parse_test_case *params = test->param_value;
	u8 data[256] = {};
	struct ieee80211_mesh_hwmp_perr *perr = (void *)data;

	perr->number_of_dst = params->number_of_dst;
	if (params->ae_enabled_idx > -1) {
		struct ieee80211_mesh_hwmp_perr_dst *dst =
			ieee80211_mesh_hwmp_perr_get_dst(
				data, params->ae_enabled_idx);

		dst->flags = AE_F;
	}

	KUNIT_EXPECT_EQ(test,
			ieee80211_mesh_perr_size_ok(data, params->len),
			params->result);
}

static struct kunit_case element_parsing_test_cases[] = {
	KUNIT_CASE(mle_defrag),
	KUNIT_CASE_PARAM(mesh_preq_parse, mesh_preq_parse_gen_params),
	KUNIT_CASE_PARAM(mesh_prep_parse, mesh_prep_parse_gen_params),
	KUNIT_CASE_PARAM(mesh_perr_parse, mesh_perr_parse_gen_params),
	{}
};

static struct kunit_suite element_parsing = {
	.name = "mac80211-element-parsing",
	.test_cases = element_parsing_test_cases,
};

kunit_test_suite(element_parsing);
