// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for management frame acceptance
 *
 * Copyright (C) 2023 Intel Corporation
 */
#include <kunit/test.h>
#include <kunit/skbuff.h>
#include "../ieee80211_i.h"
#include "../sta_info.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct mfp_test_case {
	const char *desc;
	bool sta, mfp, decrypted, unicast, assoc;
	u8 category;
	u8 stype;
	u8 action;
	ieee80211_rx_result result;
} accept_mfp_cases[] = {
	/* regular public action */
	{
		.desc = "public action: accept unicast from unknown peer",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept multicast from unknown peer",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept unicast without MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.sta = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept multicast without MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.sta = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: drop unicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_UNICAST_PUB_ACTION,
	},
	{
		.desc = "public action: accept multicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PUBLIC,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
	/* protected dual of public action */
	{
		.desc = "protected dual: drop unicast from unknown peer",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop multicast from unknown peer",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop unicast without MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.sta = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop multicast without MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.sta = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop undecrypted unicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop undecrypted multicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: accept unicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.decrypted = true,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "protected dual: accept multicast with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION,
		.action = WLAN_PUB_ACTION_DSE_ENABLEMENT,
		.decrypted = true,
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
	/* deauth/disassoc before keys are set */
	{
		.desc = "deauth: accept unicast with MFP but w/o key",
		.stype = IEEE80211_STYPE_DEAUTH,
		.sta = true,
		.mfp = true,
		.unicast = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "disassoc: accept unicast with MFP but w/o key",
		.stype = IEEE80211_STYPE_DEAUTH,
		.sta = true,
		.mfp = true,
		.unicast = true,
		.result = RX_CONTINUE,
	},
	/* non-public robust action frame ... */
	{
		.desc = "BA action: drop unicast before assoc",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_BACK,
		.unicast = true,
		.sta = true,
		.result = RX_DROP_U_UNPROT_ROBUST_ACTION,
	},
	{
		.desc = "BA action: drop unprotected after assoc",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_BACK,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_UCAST_MGMT,
	},
	{
		.desc = "BA action: accept unprotected without MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_BACK,
		.unicast = true,
		.sta = true,
		.assoc = true,
		.mfp = false,
		.result = RX_CONTINUE,
	},
	{
		.desc = "BA action: drop unprotected with MFP",
		.stype = IEEE80211_STYPE_ACTION,
		.category = WLAN_CATEGORY_BACK,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_UCAST_MGMT,
	},
};

KUNIT_ARRAY_PARAM_DESC(accept_mfp, accept_mfp_cases, desc);

static void accept_mfp(struct kunit *test)
{
	static struct sta_info sta;
	const struct mfp_test_case *params = test->param_value;
	struct ieee80211_rx_data rx = {
		.sta = params->sta ? &sta : NULL,
	};
	struct ieee80211_rx_status *status;
	struct ieee80211_hdr_3addr hdr = {
		.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					     params->stype),
		.addr1 = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		.addr2 = { 0x12, 0x22, 0x33, 0x44, 0x55, 0x66 },
		/* A3/BSSID doesn't matter here */
	};

	memset(&sta, 0, sizeof(sta));

	if (!params->sta) {
		KUNIT_ASSERT_FALSE(test, params->mfp);
		KUNIT_ASSERT_FALSE(test, params->decrypted);
	}

	if (params->mfp)
		set_sta_flag(&sta, WLAN_STA_MFP);

	if (params->assoc)
		set_bit(WLAN_STA_ASSOC, &sta._flags);

	rx.skb = kunit_zalloc_skb(test, 128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, rx.skb);
	status = IEEE80211_SKB_RXCB(rx.skb);

	if (params->decrypted) {
		status->flag |= RX_FLAG_DECRYPTED;
		if (params->unicast)
			hdr.frame_control |=
				cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}

	if (params->unicast)
		hdr.addr1[0] = 0x02;

	skb_put_data(rx.skb, &hdr, sizeof(hdr));

	switch (params->stype) {
	case IEEE80211_STYPE_ACTION:
		skb_put_u8(rx.skb, params->category);
		skb_put_u8(rx.skb, params->action);
		break;
	case IEEE80211_STYPE_DEAUTH:
	case IEEE80211_STYPE_DISASSOC: {
		__le16 reason = cpu_to_le16(WLAN_REASON_UNSPECIFIED);

		skb_put_data(rx.skb, &reason, sizeof(reason));
		}
		break;
	}

	KUNIT_EXPECT_EQ(test,
			(__force u32)ieee80211_drop_unencrypted_mgmt(&rx),
			(__force u32)params->result);
}

static struct kunit_case mfp_test_cases[] = {
	KUNIT_CASE_PARAM(accept_mfp, accept_mfp_gen_params),
	{}
};

static struct kunit_suite mfp = {
	.name = "mac80211-mfp",
	.test_cases = mfp_test_cases,
};

kunit_test_suite(mfp);
