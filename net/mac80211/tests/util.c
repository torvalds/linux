// SPDX-License-Identifier: GPL-2.0-only
/*
 * Utilities for mac80211 unit testing
 *
 * Copyright (C) 2024 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include "util.h"

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

#define CHAN5G(_freq) { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
}

static const struct ieee80211_channel channels_5ghz[] = {
	CHAN5G(5180), /* Channel 36 */
	CHAN5G(5200), /* Channel 40 */
	CHAN5G(5220), /* Channel 44 */
	CHAN5G(5240), /* Channel 48 */
};

static const struct ieee80211_rate bitrates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 }
};

/* Copied from hwsim except that it only supports 4 EHT streams and STA/P2P mode */
static const struct ieee80211_sband_iftype_data sband_capa_5ghz[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION) |
			      BIT(NL80211_IFTYPE_P2P_CLIENT),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK,
				.phy_cap_info[2] =
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ,
			},

			/* For all MCS and bandwidth, set 4 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field include all
				 * the following MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x44,
					.rx_tx_mcs11_max_nss = 0x44,
					.rx_tx_mcs13_max_nss = 0x44,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x44,
					.rx_tx_mcs11_max_nss = 0x44,
					.rx_tx_mcs13_max_nss = 0x44,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
};

int t_sdata_init(struct kunit_resource *resource, void *ctx)
{
	struct kunit *test = kunit_get_current_test();
	struct t_sdata *t_sdata;

	t_sdata = kzalloc(sizeof(*t_sdata), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, t_sdata);

	resource->data = t_sdata;
	resource->name = "sdata";

	t_sdata->sdata = kzalloc(sizeof(*t_sdata->sdata), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, t_sdata->sdata);

	t_sdata->wiphy = kzalloc(sizeof(*t_sdata->wiphy), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, t_sdata->wiphy);

	strscpy(t_sdata->sdata->name, "kunit");

	t_sdata->sdata->local = &t_sdata->local;
	t_sdata->sdata->local->hw.wiphy = t_sdata->wiphy;
	t_sdata->sdata->wdev.wiphy = t_sdata->wiphy;
	t_sdata->sdata->vif.type = NL80211_IFTYPE_STATION;

	t_sdata->sdata->deflink.sdata = t_sdata->sdata;
	t_sdata->sdata->deflink.link_id = 0;

	t_sdata->wiphy->bands[NL80211_BAND_2GHZ] = &t_sdata->band_2ghz;
	t_sdata->wiphy->bands[NL80211_BAND_5GHZ] = &t_sdata->band_5ghz;

	for (int band = NL80211_BAND_2GHZ; band <= NL80211_BAND_5GHZ; band++) {
		struct ieee80211_supported_band *sband;

		sband = t_sdata->wiphy->bands[band];
		sband->band = band;

		sband->bitrates =
			kmemdup(bitrates, sizeof(bitrates), GFP_KERNEL);
		sband->n_bitrates = ARRAY_SIZE(bitrates);

		/* Initialize channels, feel free to add more channels/bands */
		switch (band) {
		case NL80211_BAND_2GHZ:
			sband->channels = kmemdup(channels_2ghz,
						  sizeof(channels_2ghz),
						  GFP_KERNEL);
			sband->n_channels = ARRAY_SIZE(channels_2ghz);
			sband->bitrates = kmemdup(bitrates,
						  sizeof(bitrates),
						  GFP_KERNEL);
			sband->n_bitrates = ARRAY_SIZE(bitrates);
			break;
		case NL80211_BAND_5GHZ:
			sband->channels = kmemdup(channels_5ghz,
						  sizeof(channels_5ghz),
						  GFP_KERNEL);
			sband->n_channels = ARRAY_SIZE(channels_5ghz);
			sband->bitrates = kmemdup(bitrates,
						  sizeof(bitrates),
						  GFP_KERNEL);
			sband->n_bitrates = ARRAY_SIZE(bitrates);

			sband->vht_cap.vht_supported = true;
			sband->vht_cap.cap =
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ |
				IEEE80211_VHT_CAP_RXLDPC |
				IEEE80211_VHT_CAP_SHORT_GI_80 |
				IEEE80211_VHT_CAP_SHORT_GI_160 |
				IEEE80211_VHT_CAP_TXSTBC |
				IEEE80211_VHT_CAP_RXSTBC_4 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
			sband->vht_cap.vht_mcs.rx_mcs_map =
				cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 4 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 6);
			sband->vht_cap.vht_mcs.tx_mcs_map =
				sband->vht_cap.vht_mcs.rx_mcs_map;
			break;
		default:
			continue;
		}

		sband->ht_cap.ht_supported = band != NL80211_BAND_6GHZ;
		sband->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
					IEEE80211_HT_CAP_GRN_FLD |
					IEEE80211_HT_CAP_SGI_20 |
					IEEE80211_HT_CAP_SGI_40 |
					IEEE80211_HT_CAP_DSSSCCK40;
		sband->ht_cap.ampdu_factor = 0x3;
		sband->ht_cap.ampdu_density = 0x6;
		memset(&sband->ht_cap.mcs, 0, sizeof(sband->ht_cap.mcs));
		sband->ht_cap.mcs.rx_mask[0] = 0xff;
		sband->ht_cap.mcs.rx_mask[1] = 0xff;
		sband->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	}

	ieee80211_set_sband_iftype_data(&t_sdata->band_5ghz, sband_capa_5ghz);

	return 0;
}

void t_sdata_exit(struct kunit_resource *resource)
{
	struct t_sdata *t_sdata = resource->data;

	kfree(t_sdata->band_2ghz.channels);
	kfree(t_sdata->band_2ghz.bitrates);
	kfree(t_sdata->band_5ghz.channels);
	kfree(t_sdata->band_5ghz.bitrates);

	kfree(t_sdata->sdata);
	kfree(t_sdata->wiphy);

	kfree(t_sdata);
}
