// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for inform_bss functions
 *
 * Copyright (C) 2023-2024 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <kunit/test.h>
#include <kunit/skbuff.h>
#include "../core.h"
#include "util.h"

/* mac80211 helpers for element building */
#include "../../mac80211/ieee80211_i.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

struct test_elem {
	u8 id;
	u8 len;
	union {
		u8 data[255];
		struct {
			u8 eid;
			u8 edata[254];
		};
	};
};

static struct gen_new_ie_case {
	const char *desc;
	struct test_elem parent_ies[16];
	struct test_elem child_ies[16];
	struct test_elem result_ies[16];
} gen_new_ie_cases[] = {
	{
		.desc = "ML not inherited",
		.parent_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 255,
			  .eid = WLAN_EID_EXT_EHT_MULTI_LINK },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "fragments are ignored if previous len not 255",
		.parent_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 254, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
		},
		.result_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 254, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "fragments inherited",
		.parent_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "fragments copied",
		.parent_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "multiple elements inherit",
		.parent_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 123, },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 123, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "one child element overrides",
		.parent_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 255, },
			{ .id = WLAN_EID_FRAGMENT, .len = 125, },
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 123, },
		},
		.child_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 127, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_REDUCED_NEIGHBOR_REPORT, .len = 127, },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
	{
		.desc = "empty elements from parent",
		.parent_ies = {
			{ .id = 0x1, .len = 0, },
			{ .id = WLAN_EID_EXTENSION, .len = 1, .eid = 0x10 },
		},
		.child_ies = {
		},
		.result_ies = {
			{ .id = 0x1, .len = 0, },
			{ .id = WLAN_EID_EXTENSION, .len = 1, .eid = 0x10 },
		},
	},
	{
		.desc = "empty elements from child",
		.parent_ies = {
		},
		.child_ies = {
			{ .id = 0x1, .len = 0, },
			{ .id = WLAN_EID_EXTENSION, .len = 1, .eid = 0x10 },
		},
		.result_ies = {
			{ .id = 0x1, .len = 0, },
			{ .id = WLAN_EID_EXTENSION, .len = 1, .eid = 0x10 },
		},
	},
	{
		.desc = "invalid extended elements ignored",
		.parent_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 0 },
		},
		.child_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 0 },
		},
		.result_ies = {
		},
	},
	{
		.desc = "multiple extended elements",
		.parent_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 3,
			  .eid = WLAN_EID_EXT_HE_CAPABILITY },
			{ .id = WLAN_EID_EXTENSION, .len = 5,
			  .eid = WLAN_EID_EXT_ASSOC_DELAY_INFO },
			{ .id = WLAN_EID_EXTENSION, .len = 7,
			  .eid = WLAN_EID_EXT_HE_OPERATION },
			{ .id = WLAN_EID_EXTENSION, .len = 11,
			  .eid = WLAN_EID_EXT_FILS_REQ_PARAMS },
		},
		.child_ies = {
			{ .id = WLAN_EID_SSID, .len = 13 },
			{ .id = WLAN_EID_EXTENSION, .len = 17,
			  .eid = WLAN_EID_EXT_HE_CAPABILITY },
			{ .id = WLAN_EID_EXTENSION, .len = 11,
			  .eid = WLAN_EID_EXT_FILS_KEY_CONFIRM },
			{ .id = WLAN_EID_EXTENSION, .len = 19,
			  .eid = WLAN_EID_EXT_HE_OPERATION },
		},
		.result_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 17,
			  .eid = WLAN_EID_EXT_HE_CAPABILITY },
			{ .id = WLAN_EID_EXTENSION, .len = 5,
			  .eid = WLAN_EID_EXT_ASSOC_DELAY_INFO },
			{ .id = WLAN_EID_EXTENSION, .len = 19,
			  .eid = WLAN_EID_EXT_HE_OPERATION },
			{ .id = WLAN_EID_EXTENSION, .len = 11,
			  .eid = WLAN_EID_EXT_FILS_REQ_PARAMS },
			{ .id = WLAN_EID_SSID, .len = 13 },
			{ .id = WLAN_EID_EXTENSION, .len = 11,
			  .eid = WLAN_EID_EXT_FILS_KEY_CONFIRM },
		},
	},
	{
		.desc = "non-inherit element",
		.parent_ies = {
			{ .id = 0x1, .len = 7, },
			{ .id = 0x2, .len = 11, },
			{ .id = 0x3, .len = 13, },
			{ .id = WLAN_EID_EXTENSION, .len = 17, .eid = 0x10 },
			{ .id = WLAN_EID_EXTENSION, .len = 19, .eid = 0x11 },
			{ .id = WLAN_EID_EXTENSION, .len = 23, .eid = 0x12 },
			{ .id = WLAN_EID_EXTENSION, .len = 29, .eid = 0x14 },
		},
		.child_ies = {
			{ .id = WLAN_EID_EXTENSION,
			  .eid = WLAN_EID_EXT_NON_INHERITANCE,
			  .len = 10,
			  .edata = { 0x3, 0x1, 0x2, 0x3,
				     0x4, 0x10, 0x11, 0x13, 0x14 } },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
		.result_ies = {
			{ .id = WLAN_EID_EXTENSION, .len = 23, .eid = 0x12 },
			{ .id = WLAN_EID_SSID, .len = 2 },
		},
	},
};
KUNIT_ARRAY_PARAM_DESC(gen_new_ie, gen_new_ie_cases, desc)

static void test_gen_new_ie(struct kunit *test)
{
	const struct gen_new_ie_case *params = test->param_value;
	struct sk_buff *parent = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	struct sk_buff *child = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	struct sk_buff *reference = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	u8 *out = kunit_kzalloc(test, IEEE80211_MAX_DATA_LEN, GFP_KERNEL);
	size_t len;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, parent);
	KUNIT_ASSERT_NOT_NULL(test, child);
	KUNIT_ASSERT_NOT_NULL(test, reference);
	KUNIT_ASSERT_NOT_NULL(test, out);

	for (i = 0; i < ARRAY_SIZE(params->parent_ies); i++) {
		if (params->parent_ies[i].len != 0) {
			skb_put_u8(parent, params->parent_ies[i].id);
			skb_put_u8(parent, params->parent_ies[i].len);
			skb_put_data(parent, params->parent_ies[i].data,
				     params->parent_ies[i].len);
		}

		if (params->child_ies[i].len != 0) {
			skb_put_u8(child, params->child_ies[i].id);
			skb_put_u8(child, params->child_ies[i].len);
			skb_put_data(child, params->child_ies[i].data,
				     params->child_ies[i].len);
		}

		if (params->result_ies[i].len != 0) {
			skb_put_u8(reference, params->result_ies[i].id);
			skb_put_u8(reference, params->result_ies[i].len);
			skb_put_data(reference, params->result_ies[i].data,
				     params->result_ies[i].len);
		}
	}

	len = cfg80211_gen_new_ie(parent->data, parent->len,
				  child->data, child->len,
				  out, IEEE80211_MAX_DATA_LEN);
	KUNIT_EXPECT_EQ(test, len, reference->len);
	KUNIT_EXPECT_MEMEQ(test, out, reference->data, reference->len);
	memset(out, 0, IEEE80211_MAX_DATA_LEN);

	/* Exactly enough space */
	len = cfg80211_gen_new_ie(parent->data, parent->len,
				  child->data, child->len,
				  out, reference->len);
	KUNIT_EXPECT_EQ(test, len, reference->len);
	KUNIT_EXPECT_MEMEQ(test, out, reference->data, reference->len);
	memset(out, 0, IEEE80211_MAX_DATA_LEN);

	/* Not enough space (or expected zero length) */
	len = cfg80211_gen_new_ie(parent->data, parent->len,
				  child->data, child->len,
				  out, reference->len - 1);
	KUNIT_EXPECT_EQ(test, len, 0);
}

static void test_gen_new_ie_malformed(struct kunit *test)
{
	struct sk_buff *malformed = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	u8 *out = kunit_kzalloc(test, IEEE80211_MAX_DATA_LEN, GFP_KERNEL);
	size_t len;

	KUNIT_ASSERT_NOT_NULL(test, malformed);
	KUNIT_ASSERT_NOT_NULL(test, out);

	skb_put_u8(malformed, WLAN_EID_SSID);
	skb_put_u8(malformed, 3);
	skb_put(malformed, 3);
	skb_put_u8(malformed, WLAN_EID_REDUCED_NEIGHBOR_REPORT);
	skb_put_u8(malformed, 10);
	skb_put(malformed, 9);

	len = cfg80211_gen_new_ie(malformed->data, malformed->len,
				  out, 0,
				  out, IEEE80211_MAX_DATA_LEN);
	KUNIT_EXPECT_EQ(test, len, 5);

	len = cfg80211_gen_new_ie(out, 0,
				  malformed->data, malformed->len,
				  out, IEEE80211_MAX_DATA_LEN);
	KUNIT_EXPECT_EQ(test, len, 5);
}

struct inform_bss {
	struct kunit *test;

	int inform_bss_count;
};

static void inform_bss_inc_counter(struct wiphy *wiphy,
				   struct cfg80211_bss *bss,
				   const struct cfg80211_bss_ies *ies,
				   void *drv_data)
{
	struct inform_bss *ctx = t_wiphy_ctx(wiphy);

	ctx->inform_bss_count++;

	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(ctx->test, drv_data, ctx);
	KUNIT_EXPECT_PTR_EQ(ctx->test, ies, rcu_dereference(bss->ies));
	rcu_read_unlock();
}

static void test_inform_bss_ssid_only(struct kunit *test)
{
	struct inform_bss ctx = {
		.test = test,
	};
	struct wiphy *wiphy = T_WIPHY(test, ctx);
	struct t_wiphy_priv *w_priv = wiphy_priv(wiphy);
	struct cfg80211_inform_bss inform_bss = {
		.signal = 50,
		.drv_data = &ctx,
	};
	const u8 bssid[ETH_ALEN] = { 0x10, 0x22, 0x33, 0x44, 0x55, 0x66 };
	u64 tsf = 0x1000000000000000ULL;
	int beacon_int = 100;
	u16 capability = 0x1234;
	static const u8 input[] = {
		[0] = WLAN_EID_SSID,
		[1] = 4,
		[2] = 'T', 'E', 'S', 'T'
	};
	struct cfg80211_bss *bss, *other;
	const struct cfg80211_bss_ies *ies;

	w_priv->ops->inform_bss = inform_bss_inc_counter;

	inform_bss.chan = ieee80211_get_channel_khz(wiphy, MHZ_TO_KHZ(2412));
	KUNIT_ASSERT_NOT_NULL(test, inform_bss.chan);

	bss = cfg80211_inform_bss_data(wiphy, &inform_bss,
				       CFG80211_BSS_FTYPE_PRESP, bssid, tsf,
				       capability, beacon_int,
				       input, sizeof(input),
				       GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, bss);
	KUNIT_EXPECT_EQ(test, ctx.inform_bss_count, 1);

	/* Check values in returned bss are correct */
	KUNIT_EXPECT_EQ(test, bss->signal, inform_bss.signal);
	KUNIT_EXPECT_EQ(test, bss->beacon_interval, beacon_int);
	KUNIT_EXPECT_EQ(test, bss->capability, capability);
	KUNIT_EXPECT_EQ(test, bss->bssid_index, 0);
	KUNIT_EXPECT_PTR_EQ(test, bss->channel, inform_bss.chan);
	KUNIT_EXPECT_MEMEQ(test, bssid, bss->bssid, sizeof(bssid));

	/* Check the IEs have the expected value */
	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	KUNIT_EXPECT_NOT_NULL(test, ies);
	KUNIT_EXPECT_EQ(test, ies->tsf, tsf);
	KUNIT_EXPECT_EQ(test, ies->len, sizeof(input));
	KUNIT_EXPECT_MEMEQ(test, ies->data, input, sizeof(input));
	rcu_read_unlock();

	/* Check we can look up the BSS - by SSID */
	other = cfg80211_get_bss(wiphy, NULL, NULL, "TEST", 4,
				 IEEE80211_BSS_TYPE_ANY,
				 IEEE80211_PRIVACY_ANY);
	KUNIT_EXPECT_PTR_EQ(test, bss, other);
	cfg80211_put_bss(wiphy, other);

	/* Check we can look up the BSS - by BSSID */
	other = cfg80211_get_bss(wiphy, NULL, bssid, NULL, 0,
				 IEEE80211_BSS_TYPE_ANY,
				 IEEE80211_PRIVACY_ANY);
	KUNIT_EXPECT_PTR_EQ(test, bss, other);
	cfg80211_put_bss(wiphy, other);

	cfg80211_put_bss(wiphy, bss);
}

static struct inform_bss_ml_sta_case {
	const char *desc;
	int mld_id;
	bool sta_prof_vendor_elems;
	bool include_oper_class;
	bool nstr;
} inform_bss_ml_sta_cases[] = {
	{
		.desc = "zero_mld_id",
		.mld_id = 0,
		.sta_prof_vendor_elems = false,
	}, {
		.desc = "zero_mld_id_with_oper_class",
		.mld_id = 0,
		.sta_prof_vendor_elems = false,
		.include_oper_class = true,
	}, {
		.desc = "mld_id_eq_1",
		.mld_id = 1,
		.sta_prof_vendor_elems = true,
	}, {
		.desc = "mld_id_eq_1_with_oper_class",
		.mld_id = 1,
		.sta_prof_vendor_elems = true,
		.include_oper_class = true,
	}, {
		.desc = "nstr",
		.mld_id = 0,
		.nstr = true,
	},
};
KUNIT_ARRAY_PARAM_DESC(inform_bss_ml_sta, inform_bss_ml_sta_cases, desc)

static void test_inform_bss_ml_sta(struct kunit *test)
{
	const struct inform_bss_ml_sta_case *params = test->param_value;
	struct inform_bss ctx = {
		.test = test,
	};
	struct wiphy *wiphy = T_WIPHY(test, ctx);
	struct t_wiphy_priv *w_priv = wiphy_priv(wiphy);
	struct cfg80211_inform_bss inform_bss = {
		.signal = 50,
		.drv_data = &ctx,
	};
	struct cfg80211_bss *bss, *link_bss;
	const struct cfg80211_bss_ies *ies;

	/* sending station */
	const u8 bssid[ETH_ALEN] = { 0x10, 0x22, 0x33, 0x44, 0x55, 0x66 };
	u64 tsf = 0x1000000000000000ULL;
	int beacon_int = 100;
	u16 capability = 0x1234;

	/* Building the frame *************************************************/
	struct sk_buff *input = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	u8 *len_mle, *len_prof;
	u8 link_id = 2;
	struct {
		struct ieee80211_neighbor_ap_info info;
		struct ieee80211_tbtt_info_ge_11 ap;
	} __packed rnr_normal = {
		.info = {
			.tbtt_info_hdr = u8_encode_bits(0, IEEE80211_AP_INFO_TBTT_HDR_COUNT),
			.tbtt_info_len = sizeof(struct ieee80211_tbtt_info_ge_11),
			.op_class = 81,
			.channel = 11,
		},
		.ap = {
			.tbtt_offset = 0xff,
			.bssid = { 0x10, 0x22, 0x33, 0x44, 0x55, 0x67 },
			.short_ssid = 0, /* unused */
			.bss_params = 0,
			.psd_20 = 0,
			.mld_params.mld_id = params->mld_id,
			.mld_params.params =
				le16_encode_bits(link_id,
						 IEEE80211_RNR_MLD_PARAMS_LINK_ID),
		}
	};
	struct {
		struct ieee80211_neighbor_ap_info info;
		struct ieee80211_rnr_mld_params mld_params;
	} __packed rnr_nstr = {
		.info = {
			.tbtt_info_hdr =
				u8_encode_bits(0, IEEE80211_AP_INFO_TBTT_HDR_COUNT) |
				u8_encode_bits(IEEE80211_TBTT_INFO_TYPE_MLD,
					       IEEE80211_AP_INFO_TBTT_HDR_TYPE),
			.tbtt_info_len = sizeof(struct ieee80211_rnr_mld_params),
			.op_class = 81,
			.channel = 11,
		},
		.mld_params = {
			.mld_id = params->mld_id,
			.params =
				le16_encode_bits(link_id,
						 IEEE80211_RNR_MLD_PARAMS_LINK_ID),
		}
	};
	size_t rnr_len = params->nstr ? sizeof(rnr_nstr) : sizeof(rnr_normal);
	void *rnr = params->nstr ? (void *)&rnr_nstr : (void *)&rnr_normal;
	struct {
		__le16 control;
		u8 var_len;
		u8 mld_mac_addr[ETH_ALEN];
		u8 link_id_info;
		u8 params_change_count;
		__le16 mld_caps_and_ops;
		u8 mld_id;
		__le16 ext_mld_caps_and_ops;
	} __packed mle_basic_common_info = {
		.control =
			cpu_to_le16(IEEE80211_ML_CONTROL_TYPE_BASIC |
				    IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT |
				    IEEE80211_MLC_BASIC_PRES_LINK_ID |
				    (params->mld_id ? IEEE80211_MLC_BASIC_PRES_MLD_ID : 0) |
				    IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP),
		.mld_id = params->mld_id,
		.mld_caps_and_ops = cpu_to_le16(0x0102),
		.ext_mld_caps_and_ops = cpu_to_le16(0x0304),
		.var_len = sizeof(mle_basic_common_info) - 2 -
			   (params->mld_id ? 0 : 1),
		.mld_mac_addr = { 0x10, 0x22, 0x33, 0x44, 0x55, 0x60 },
	};
	struct {
		__le16 control;
		u8 var_len;
		u8 bssid[ETH_ALEN];
		__le16 beacon_int;
		__le64 tsf_offset;
		__le16 capabilities; /* already part of payload */
	} __packed sta_prof = {
		.control =
			cpu_to_le16(IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE |
				    IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT |
				    IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT |
				    IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT |
				    u16_encode_bits(link_id,
						    IEEE80211_MLE_STA_CONTROL_LINK_ID)),
		.var_len = sizeof(sta_prof) - 2 - 2,
		.bssid = { *rnr_normal.ap.bssid },
		.beacon_int = cpu_to_le16(101),
		.tsf_offset = cpu_to_le64(-123ll),
		.capabilities = cpu_to_le16(0xdead),
	};

	KUNIT_ASSERT_NOT_NULL(test, input);

	w_priv->ops->inform_bss = inform_bss_inc_counter;

	inform_bss.chan = ieee80211_get_channel_khz(wiphy, MHZ_TO_KHZ(2412));
	KUNIT_ASSERT_NOT_NULL(test, inform_bss.chan);

	skb_put_u8(input, WLAN_EID_SSID);
	skb_put_u8(input, 4);
	skb_put_data(input, "TEST", 4);

	if (params->include_oper_class) {
		skb_put_u8(input, WLAN_EID_SUPPORTED_REGULATORY_CLASSES);
		skb_put_u8(input, 1);
		skb_put_u8(input, 81);
	}

	skb_put_u8(input, WLAN_EID_REDUCED_NEIGHBOR_REPORT);
	skb_put_u8(input, rnr_len);
	skb_put_data(input, rnr, rnr_len);

	/* build a multi-link element */
	skb_put_u8(input, WLAN_EID_EXTENSION);
	len_mle = skb_put(input, 1);
	skb_put_u8(input, WLAN_EID_EXT_EHT_MULTI_LINK);
	skb_put_data(input, &mle_basic_common_info, sizeof(mle_basic_common_info));
	if (!params->mld_id)
		t_skb_remove_member(input, typeof(mle_basic_common_info), mld_id);
	/* with a STA profile inside */
	skb_put_u8(input, IEEE80211_MLE_SUBELEM_PER_STA_PROFILE);
	len_prof = skb_put(input, 1);
	skb_put_data(input, &sta_prof, sizeof(sta_prof));

	if (params->sta_prof_vendor_elems) {
		/* Put two (vendor) element into sta_prof */
		skb_put_u8(input, WLAN_EID_VENDOR_SPECIFIC);
		skb_put_u8(input, 160);
		skb_put(input, 160);

		skb_put_u8(input, WLAN_EID_VENDOR_SPECIFIC);
		skb_put_u8(input, 165);
		skb_put(input, 165);
	}

	/* fragment STA profile */
	ieee80211_fragment_element(input, len_prof,
				   IEEE80211_MLE_SUBELEM_FRAGMENT);
	/* fragment MLE */
	ieee80211_fragment_element(input, len_mle, WLAN_EID_FRAGMENT);

	/* Put a (vendor) element after the ML element */
	skb_put_u8(input, WLAN_EID_VENDOR_SPECIFIC);
	skb_put_u8(input, 155);
	skb_put(input, 155);

	/* Submit *************************************************************/
	bss = cfg80211_inform_bss_data(wiphy, &inform_bss,
				       CFG80211_BSS_FTYPE_PRESP, bssid, tsf,
				       capability, beacon_int,
				       input->data, input->len,
				       GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, bss);
	KUNIT_EXPECT_EQ(test, ctx.inform_bss_count, 2);

	/* Check link_bss *****************************************************/
	link_bss = __cfg80211_get_bss(wiphy, NULL, sta_prof.bssid, NULL, 0,
				      IEEE80211_BSS_TYPE_ANY,
				      IEEE80211_PRIVACY_ANY,
				      0);
	KUNIT_ASSERT_NOT_NULL(test, link_bss);
	KUNIT_EXPECT_EQ(test, link_bss->signal, 0);
	KUNIT_EXPECT_EQ(test, link_bss->beacon_interval,
			      le16_to_cpu(sta_prof.beacon_int));
	KUNIT_EXPECT_EQ(test, link_bss->capability,
			      le16_to_cpu(sta_prof.capabilities));
	KUNIT_EXPECT_EQ(test, link_bss->bssid_index, 0);
	KUNIT_EXPECT_PTR_EQ(test, link_bss->channel,
			    ieee80211_get_channel_khz(wiphy, MHZ_TO_KHZ(2462)));

	/* Test wiphy does not set WIPHY_FLAG_SUPPORTS_NSTR_NONPRIMARY */
	if (params->nstr) {
		KUNIT_EXPECT_EQ(test, link_bss->use_for, 0);
		KUNIT_EXPECT_EQ(test, link_bss->cannot_use_reasons,
				NL80211_BSS_CANNOT_USE_NSTR_NONPRIMARY);
		KUNIT_EXPECT_NULL(test,
				  cfg80211_get_bss(wiphy, NULL, sta_prof.bssid,
						   NULL, 0,
						   IEEE80211_BSS_TYPE_ANY,
						   IEEE80211_PRIVACY_ANY));
	} else {
		KUNIT_EXPECT_EQ(test, link_bss->use_for,
				NL80211_BSS_USE_FOR_ALL);
		KUNIT_EXPECT_EQ(test, link_bss->cannot_use_reasons, 0);
	}

	rcu_read_lock();
	ies = rcu_dereference(link_bss->ies);
	KUNIT_EXPECT_NOT_NULL(test, ies);
	KUNIT_EXPECT_EQ(test, ies->tsf, tsf + le64_to_cpu(sta_prof.tsf_offset));
	/* Resulting length should be:
	 * SSID (inherited) + RNR (inherited) + vendor element(s) +
	 * operating class (if requested) +
	 * generated RNR (if MLD ID == 0 and not NSTR) +
	 * MLE common info + MLE header and control
	 */
	if (params->sta_prof_vendor_elems)
		KUNIT_EXPECT_EQ(test, ies->len,
				6 + 2 + rnr_len + 2 + 160 + 2 + 165 +
				(params->include_oper_class ? 3 : 0) +
				(!params->mld_id && !params->nstr ? 22 : 0) +
				mle_basic_common_info.var_len + 5);
	else
		KUNIT_EXPECT_EQ(test, ies->len,
				6 + 2 + rnr_len + 2 + 155 +
				(params->include_oper_class ? 3 : 0) +
				(!params->mld_id && !params->nstr ? 22 : 0) +
				mle_basic_common_info.var_len + 5);
	rcu_read_unlock();

	cfg80211_put_bss(wiphy, bss);
	cfg80211_put_bss(wiphy, link_bss);
}

static struct cfg80211_parse_colocated_ap_case {
	const char *desc;
	u8 op_class;
	u8 channel;
	struct ieee80211_neighbor_ap_info info;
	union {
		struct ieee80211_tbtt_info_ge_11 tbtt_long;
		struct ieee80211_tbtt_info_7_8_9 tbtt_short;
	};
	bool add_junk;
	bool same_ssid;
	bool valid;
} cfg80211_parse_colocated_ap_cases[] = {
	{
		.desc = "wrong_band",
		.info.op_class = 81,
		.info.channel = 11,
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
		},
		.valid = false,
	},
	{
		.desc = "wrong_type",
		/* IEEE80211_AP_INFO_TBTT_HDR_TYPE is in the least significant bits */
		.info.tbtt_info_hdr = IEEE80211_TBTT_INFO_TYPE_MLD,
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
		},
		.valid = false,
	},
	{
		.desc = "colocated_invalid_len_short",
		.info.tbtt_info_len = 6,
		.tbtt_short = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP |
				      IEEE80211_RNR_TBTT_PARAMS_SAME_SSID,
		},
		.valid = false,
	},
	{
		.desc = "colocated_invalid_len_short_mld",
		.info.tbtt_info_len = 10,
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
		},
		.valid = false,
	},
	{
		.desc = "colocated_non_mld",
		.info.tbtt_info_len = sizeof(struct ieee80211_tbtt_info_7_8_9),
		.tbtt_short = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP |
				      IEEE80211_RNR_TBTT_PARAMS_SAME_SSID,
		},
		.same_ssid = true,
		.valid = true,
	},
	{
		.desc = "colocated_non_mld_invalid_bssid",
		.info.tbtt_info_len = sizeof(struct ieee80211_tbtt_info_7_8_9),
		.tbtt_short = {
			.bssid = { 0xff, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP |
				      IEEE80211_RNR_TBTT_PARAMS_SAME_SSID,
		},
		.same_ssid = true,
		.valid = false,
	},
	{
		.desc = "colocated_mld",
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
		},
		.valid = true,
	},
	{
		.desc = "colocated_mld",
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
		},
		.add_junk = true,
		.valid = false,
	},
	{
		.desc = "colocated_disabled_mld",
		.tbtt_long = {
			.bssid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
			.bss_params = IEEE80211_RNR_TBTT_PARAMS_COLOC_AP,
			.mld_params.params = cpu_to_le16(IEEE80211_RNR_MLD_PARAMS_DISABLED_LINK),
		},
		.valid = false,
	},
};
KUNIT_ARRAY_PARAM_DESC(cfg80211_parse_colocated_ap, cfg80211_parse_colocated_ap_cases, desc)

static void test_cfg80211_parse_colocated_ap(struct kunit *test)
{
	const struct cfg80211_parse_colocated_ap_case *params = test->param_value;
	struct sk_buff *input = kunit_zalloc_skb(test, 1024, GFP_KERNEL);
	struct cfg80211_bss_ies *ies;
	struct ieee80211_neighbor_ap_info info;
	LIST_HEAD(coloc_ap_list);
	int count;

	KUNIT_ASSERT_NOT_NULL(test, input);

	info = params->info;

	/* Reasonable values for a colocated AP */
	if (!info.tbtt_info_len)
		info.tbtt_info_len = sizeof(params->tbtt_long);
	if (!info.op_class)
		info.op_class = 131;
	if (!info.channel)
		info.channel = 33;
	/* Zero is the correct default for .btt_info_hdr (one entry, TBTT type) */

	skb_put_u8(input, WLAN_EID_SSID);
	skb_put_u8(input, 4);
	skb_put_data(input, "TEST", 4);

	skb_put_u8(input, WLAN_EID_REDUCED_NEIGHBOR_REPORT);
	skb_put_u8(input, sizeof(info) + info.tbtt_info_len + (params->add_junk ? 3 : 0));
	skb_put_data(input, &info, sizeof(info));
	skb_put_data(input, &params->tbtt_long, info.tbtt_info_len);

	if (params->add_junk)
		skb_put_data(input, "123", 3);

	ies = kunit_kzalloc(test, struct_size(ies, data, input->len), GFP_KERNEL);
	ies->len = input->len;
	memcpy(ies->data, input->data, input->len);

	count = cfg80211_parse_colocated_ap(ies, &coloc_ap_list);

	KUNIT_EXPECT_EQ(test, count, params->valid);
	KUNIT_EXPECT_EQ(test, list_count_nodes(&coloc_ap_list), params->valid);

	if (params->valid && !list_empty(&coloc_ap_list)) {
		struct cfg80211_colocated_ap *ap;

		ap = list_first_entry(&coloc_ap_list, typeof(*ap), list);
		if (info.tbtt_info_len <= sizeof(params->tbtt_short))
			KUNIT_EXPECT_MEMEQ(test, ap->bssid, params->tbtt_short.bssid, ETH_ALEN);
		else
			KUNIT_EXPECT_MEMEQ(test, ap->bssid, params->tbtt_long.bssid, ETH_ALEN);

		if (params->same_ssid) {
			KUNIT_EXPECT_EQ(test, ap->ssid_len, 4);
			KUNIT_EXPECT_MEMEQ(test, ap->ssid, "TEST", 4);
		} else {
			KUNIT_EXPECT_EQ(test, ap->ssid_len, 0);
		}
	}

	cfg80211_free_coloc_ap_list(&coloc_ap_list);
}

static struct kunit_case gen_new_ie_test_cases[] = {
	KUNIT_CASE_PARAM(test_gen_new_ie, gen_new_ie_gen_params),
	KUNIT_CASE(test_gen_new_ie_malformed),
	{}
};

static struct kunit_suite gen_new_ie = {
	.name = "cfg80211-ie-generation",
	.test_cases = gen_new_ie_test_cases,
};

kunit_test_suite(gen_new_ie);

static struct kunit_case inform_bss_test_cases[] = {
	KUNIT_CASE(test_inform_bss_ssid_only),
	KUNIT_CASE_PARAM(test_inform_bss_ml_sta, inform_bss_ml_sta_gen_params),
	{}
};

static struct kunit_suite inform_bss = {
	.name = "cfg80211-inform-bss",
	.test_cases = inform_bss_test_cases,
};

kunit_test_suite(inform_bss);

static struct kunit_case scan_6ghz_cases[] = {
	KUNIT_CASE_PARAM(test_cfg80211_parse_colocated_ap,
			 cfg80211_parse_colocated_ap_gen_params),
	{}
};

static struct kunit_suite scan_6ghz = {
	.name = "cfg80211-scan-6ghz",
	.test_cases = scan_6ghz_cases,
};

kunit_test_suite(scan_6ghz);
