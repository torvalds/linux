// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for channel mode functions
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <net/cfg80211.h>
#include <kunit/test.h>

#include "util.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct determine_chan_mode_case {
	const char *desc;
	u8 extra_supp_rate;
	enum ieee80211_conn_mode conn_mode;
	enum ieee80211_conn_mode expected_mode;
	bool strict;
	u8 userspace_selector;
	struct ieee80211_ht_cap ht_capa_mask;
	struct ieee80211_vht_cap vht_capa;
	struct ieee80211_vht_cap vht_capa_mask;
	u8 vht_basic_mcs_1_4_set:1,
	   vht_basic_mcs_5_8_set:1,
	   he_basic_mcs_1_4_set:1,
	   he_basic_mcs_5_8_set:1;
	u8 vht_basic_mcs_1_4, vht_basic_mcs_5_8;
	u8 he_basic_mcs_1_4, he_basic_mcs_5_8;
	u8 eht_mcs7_min_nss;
	int error;
} determine_chan_mode_cases[] = {
	{
		.desc = "Normal case, EHT is working",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_EHT,
	}, {
		.desc = "Requiring EHT support is fine",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_EHT,
		.extra_supp_rate = 0x80 | BSS_MEMBERSHIP_SELECTOR_EHT_PHY,
	}, {
		.desc = "Lowering the mode limits us",
		.conn_mode = IEEE80211_CONN_MODE_VHT,
		.expected_mode = IEEE80211_CONN_MODE_VHT,
	}, {
		.desc = "Requesting a basic rate/selector that we do not support",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.extra_supp_rate = 0x80 | (BSS_MEMBERSHIP_SELECTOR_MIN - 1),
		.error = EINVAL,
	}, {
		.desc = "As before, but userspace says it is taking care of it",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.userspace_selector = BSS_MEMBERSHIP_SELECTOR_MIN - 1,
		.extra_supp_rate = 0x80 | (BSS_MEMBERSHIP_SELECTOR_MIN - 1),
		.expected_mode = IEEE80211_CONN_MODE_EHT,
	}, {
		.desc = "Masking out a supported rate in HT capabilities",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_LEGACY,
		.ht_capa_mask = {
			.mcs.rx_mask[0] = 0xf7,
		},
	}, {
		.desc = "Masking out a RX rate in VHT capabilities",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_HT,
		/* Only one RX stream at MCS 0-7 */
		.vht_capa = {
			.supp_mcs.rx_mcs_map =
				cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_7),
		},
		.vht_capa_mask = {
			.supp_mcs.rx_mcs_map = cpu_to_le16(0xffff),
		},
		.strict = true,
	}, {
		.desc = "Masking out a TX rate in VHT capabilities",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_HT,
		/* Only one TX stream at MCS 0-7 */
		.vht_capa = {
			.supp_mcs.tx_mcs_map =
				cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_7),
		},
		.vht_capa_mask = {
			.supp_mcs.tx_mcs_map = cpu_to_le16(0xffff),
		},
		.strict = true,
	}, {
		.desc = "AP has higher VHT requirement than client",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_HT,
		.vht_basic_mcs_5_8_set = 1,
		.vht_basic_mcs_5_8 = 0xFE, /* require 5th stream */
		.strict = true,
	}, {
		.desc = "all zero VHT basic rates are ignored (many APs broken)",
		.conn_mode = IEEE80211_CONN_MODE_VHT,
		.expected_mode = IEEE80211_CONN_MODE_VHT,
		.vht_basic_mcs_1_4_set = 1,
		.vht_basic_mcs_5_8_set = 1,
	}, {
		.desc = "AP requires 3 HE streams but client only has two",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_VHT,
		.he_basic_mcs_1_4 = 0b11001010,
		.he_basic_mcs_1_4_set = 1,
	}, {
		.desc = "all zero HE basic rates are ignored (iPhone workaround)",
		.conn_mode = IEEE80211_CONN_MODE_HE,
		.expected_mode = IEEE80211_CONN_MODE_HE,
		.he_basic_mcs_1_4_set = 1,
		.he_basic_mcs_5_8_set = 1,
	}, {
		.desc = "AP requires too many RX streams with EHT MCS 7",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_HE,
		.eht_mcs7_min_nss = 0x15,
	}, {
		.desc = "AP requires too many TX streams with EHT MCS 7",
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.expected_mode = IEEE80211_CONN_MODE_HE,
		.eht_mcs7_min_nss = 0x51,
	}, {
		.desc = "AP requires too many RX streams with EHT MCS 7 and EHT is required",
		.extra_supp_rate = 0x80 | BSS_MEMBERSHIP_SELECTOR_EHT_PHY,
		.conn_mode = IEEE80211_CONN_MODE_EHT,
		.eht_mcs7_min_nss = 0x15,
		.error = EINVAL,
	}
};
KUNIT_ARRAY_PARAM_DESC(determine_chan_mode, determine_chan_mode_cases, desc)

static void test_determine_chan_mode(struct kunit *test)
{
	const struct determine_chan_mode_case *params = test->param_value;
	struct t_sdata *t_sdata = T_SDATA(test);
	struct ieee80211_conn_settings conn = {
		.mode = params->conn_mode,
		.bw_limit = IEEE80211_CONN_BW_LIMIT_20,
	};
	struct cfg80211_bss cbss = {
		.channel = &t_sdata->band_5ghz.channels[0],
	};
	unsigned long userspace_selectors[BITS_TO_LONGS(128)] = {};
	u8 bss_ies[] = {
		/* Supported Rates */
		WLAN_EID_SUPP_RATES, 0x08,
		0x82, 0x84, 0x8b, 0x96, 0xc, 0x12, 0x18, 0x24,
		/* Extended Supported Rates */
		WLAN_EID_EXT_SUPP_RATES, 0x05,
		0x30, 0x48, 0x60, 0x6c, params->extra_supp_rate,
		/* HT Capabilities */
		WLAN_EID_HT_CAPABILITY, 0x1a,
		0x0c, 0x00, 0x1b, 0xff, 0xff, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00,
		/* HT Information (0xff for 1 stream) */
		WLAN_EID_HT_OPERATION, 0x16,
		0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* VHT Capabilities */
		WLAN_EID_VHT_CAPABILITY, 0xc,
		0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
		0xff, 0xff, 0x00, 0x00,
		/* VHT Operation */
		WLAN_EID_VHT_OPERATION, 0x05,
		0x00, 0x00, 0x00,
		params->vht_basic_mcs_1_4_set ?
			params->vht_basic_mcs_1_4 :
			le16_get_bits(t_sdata->band_5ghz.vht_cap.vht_mcs.rx_mcs_map, 0xff),
		params->vht_basic_mcs_5_8_set ?
			params->vht_basic_mcs_5_8 :
			le16_get_bits(t_sdata->band_5ghz.vht_cap.vht_mcs.rx_mcs_map, 0xff00),
		/* HE Capabilities */
		WLAN_EID_EXTENSION, 0x16, WLAN_EID_EXT_HE_CAPABILITY,
		0x01, 0x78, 0xc8, 0x1a, 0x40, 0x00, 0x00, 0xbf,
		0xce, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0xfa, 0xff, 0xfa, 0xff,
		/* HE Operation (permit overriding values) */
		WLAN_EID_EXTENSION, 0x07, WLAN_EID_EXT_HE_OPERATION,
		0xf0, 0x3f, 0x00, 0xb0,
		params->he_basic_mcs_1_4_set ? params->he_basic_mcs_1_4 : 0xfc,
		params->he_basic_mcs_5_8_set ? params->he_basic_mcs_5_8 : 0xff,
		/* EHT Capabilities */
		WLAN_EID_EXTENSION, 0x12, WLAN_EID_EXT_EHT_CAPABILITY,
		0x07, 0x00, 0x1c, 0x00, 0x00, 0xfe, 0xff, 0xff,
		0x7f, 0x01, 0x00, 0x88, 0x88, 0x88, 0x00, 0x00,
		0x00,
		/* EHT Operation */
		WLAN_EID_EXTENSION, 0x09, WLAN_EID_EXT_EHT_OPERATION,
		0x01, params->eht_mcs7_min_nss ? params->eht_mcs7_min_nss : 0x11,
		0x00, 0x00, 0x00, 0x00, 0x24, 0x00,
	};
	struct ieee80211_chan_req chanreq = {};
	struct cfg80211_chan_def ap_chandef = {};
	struct ieee802_11_elems *elems;

	if (params->strict)
		set_bit(IEEE80211_HW_STRICT, t_sdata->local.hw.flags);
	else
		clear_bit(IEEE80211_HW_STRICT, t_sdata->local.hw.flags);

	t_sdata->sdata->u.mgd.ht_capa_mask = params->ht_capa_mask;
	t_sdata->sdata->u.mgd.vht_capa = params->vht_capa;
	t_sdata->sdata->u.mgd.vht_capa_mask = params->vht_capa_mask;

	if (params->userspace_selector)
		set_bit(params->userspace_selector, userspace_selectors);

	rcu_assign_pointer(cbss.ies,
			   kunit_kzalloc(test,
					 sizeof(cbss) + sizeof(bss_ies),
					 GFP_KERNEL));
	KUNIT_ASSERT_NOT_NULL(test, rcu_access_pointer(cbss.ies));
	((struct cfg80211_bss_ies *)rcu_access_pointer(cbss.ies))->len = sizeof(bss_ies);

	memcpy((void *)rcu_access_pointer(cbss.ies)->data, bss_ies,
	       sizeof(bss_ies));

	rcu_read_lock();
	elems = ieee80211_determine_chan_mode(t_sdata->sdata, &conn, &cbss,
					      0, &chanreq, &ap_chandef,
					      userspace_selectors);
	rcu_read_unlock();

	/* We do not need elems, free them if they are valid. */
	if (!IS_ERR_OR_NULL(elems))
		kfree(elems);

	if (params->error) {
		KUNIT_ASSERT_TRUE(test, IS_ERR(elems));
		KUNIT_ASSERT_EQ(test, PTR_ERR(elems), -params->error);
	} else {
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, elems);
		KUNIT_ASSERT_EQ(test, conn.mode, params->expected_mode);
	}
}

static struct kunit_case chan_mode_cases[] = {
	KUNIT_CASE_PARAM(test_determine_chan_mode,
			 determine_chan_mode_gen_params),
	{}
};

static struct kunit_suite chan_mode = {
	.name = "mac80211-mlme-chan-mode",
	.test_cases = chan_mode_cases,
};

kunit_test_suite(chan_mode);
