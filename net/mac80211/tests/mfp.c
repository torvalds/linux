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

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

static const struct mfp_test_case {
	const char *desc;
	bool sta, mfp, decrypted, unicast, protected_dual;
	ieee80211_rx_result result;
} accept_public_action_cases[] = {
	/* regular public action */
	{
		.desc = "public action: accept unicast from unknown peer",
		.unicast = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept multicast from unknown peer",
		.unicast = false,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept unicast without MFP",
		.unicast = true,
		.sta = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: accept multicast without MFP",
		.unicast = false,
		.sta = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "public action: drop unicast with MFP",
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_UNICAST_PUB_ACTION,
	},
	{
		.desc = "public action: accept multicast with MFP",
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
	/* protected dual of public action */
	{
		.desc = "protected dual: drop unicast from unknown peer",
		.protected_dual = true,
		.unicast = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop multicast from unknown peer",
		.protected_dual = true,
		.unicast = false,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop unicast without MFP",
		.protected_dual = true,
		.unicast = true,
		.sta = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop multicast without MFP",
		.protected_dual = true,
		.unicast = false,
		.sta = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop undecrypted unicast with MFP",
		.protected_dual = true,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: drop undecrypted multicast with MFP",
		.protected_dual = true,
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_DROP_U_UNPROT_DUAL,
	},
	{
		.desc = "protected dual: accept unicast with MFP",
		.protected_dual = true,
		.decrypted = true,
		.unicast = true,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
	{
		.desc = "protected dual: accept multicast with MFP",
		.protected_dual = true,
		.decrypted = true,
		.unicast = false,
		.sta = true,
		.mfp = true,
		.result = RX_CONTINUE,
	},
};

KUNIT_ARRAY_PARAM_DESC(accept_public_action,
		       accept_public_action_cases,
		       desc);

static void accept_public_action(struct kunit *test)
{
	static struct sta_info sta = {};
	const struct mfp_test_case *params = test->param_value;
	struct ieee80211_rx_data rx = {
		.sta = params->sta ? &sta : NULL,
	};
	struct ieee80211_rx_status *status;
	struct ieee80211_hdr_3addr hdr = {
		.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					     IEEE80211_STYPE_ACTION),
		.addr1 = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		.addr2 = { 0x12, 0x22, 0x33, 0x44, 0x55, 0x66 },
		/* A3/BSSID doesn't matter here */
	};

	if (!params->sta) {
		KUNIT_ASSERT_FALSE(test, params->mfp);
		KUNIT_ASSERT_FALSE(test, params->decrypted);
	}

	if (params->mfp)
		set_sta_flag(&sta, WLAN_STA_MFP);

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

	if (params->protected_dual)
		skb_put_u8(rx.skb, WLAN_CATEGORY_PROTECTED_DUAL_OF_ACTION);
	else
		skb_put_u8(rx.skb, WLAN_CATEGORY_PUBLIC);
	skb_put_u8(rx.skb, WLAN_PUB_ACTION_DSE_ENABLEMENT);

	KUNIT_EXPECT_EQ(test,
			ieee80211_drop_unencrypted_mgmt(&rx),
			params->result);
}

static struct kunit_case mfp_test_cases[] = {
	KUNIT_CASE_PARAM(accept_public_action, accept_public_action_gen_params),
	{}
};

static struct kunit_suite mfp = {
	.name = "mac80211-mfp",
	.test_cases = mfp_test_cases,
};

kunit_test_suite(mfp);
