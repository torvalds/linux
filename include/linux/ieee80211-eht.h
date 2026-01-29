/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 EHT definitions
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005, Devicescape Software, Inc.
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (c) 2018 - 2025 Intel Corporation
 */

#ifndef LINUX_IEEE80211_EHT_H
#define LINUX_IEEE80211_EHT_H

#include <linux/types.h>
#include <linux/if_ether.h>
/* need HE definitions for the inlines here */
#include <linux/ieee80211-he.h>

#define IEEE80211_TTLM_MAX_CNT				2
#define IEEE80211_TTLM_CONTROL_DIRECTION		0x03
#define IEEE80211_TTLM_CONTROL_DEF_LINK_MAP		0x04
#define IEEE80211_TTLM_CONTROL_SWITCH_TIME_PRESENT	0x08
#define IEEE80211_TTLM_CONTROL_EXPECTED_DUR_PRESENT	0x10
#define IEEE80211_TTLM_CONTROL_LINK_MAP_SIZE		0x20

#define IEEE80211_TTLM_DIRECTION_DOWN		0
#define IEEE80211_TTLM_DIRECTION_UP		1
#define IEEE80211_TTLM_DIRECTION_BOTH		2

/**
 * struct ieee80211_ttlm_elem - TID-To-Link Mapping element
 *
 * Defined in section 9.4.2.314 in P802.11be_D4
 *
 * @control: the first part of control field
 * @optional: the second part of control field
 */
struct ieee80211_ttlm_elem {
	u8 control;
	u8 optional[];
} __packed;

#define IEEE80211_EHT_MCS_NSS_RX 0x0f
#define IEEE80211_EHT_MCS_NSS_TX 0xf0

/**
 * struct ieee80211_eht_mcs_nss_supp_20mhz_only - EHT 20MHz only station max
 * supported NSS for per MCS.
 *
 * For each field below, bits 0 - 3 indicate the maximal number of spatial
 * streams for Rx, and bits 4 - 7 indicate the maximal number of spatial streams
 * for Tx.
 *
 * @rx_tx_mcs7_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 0 - 7.
 * @rx_tx_mcs9_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 8 - 9.
 * @rx_tx_mcs11_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 10 - 11.
 * @rx_tx_mcs13_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 12 - 13.
 * @rx_tx_max_nss: array of the previous fields for easier loop access
 */
struct ieee80211_eht_mcs_nss_supp_20mhz_only {
	union {
		struct {
			u8 rx_tx_mcs7_max_nss;
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[4];
	};
};

/**
 * struct ieee80211_eht_mcs_nss_supp_bw - EHT max supported NSS per MCS (except
 * 20MHz only stations).
 *
 * For each field below, bits 0 - 3 indicate the maximal number of spatial
 * streams for Rx, and bits 4 - 7 indicate the maximal number of spatial streams
 * for Tx.
 *
 * @rx_tx_mcs9_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 0 - 9.
 * @rx_tx_mcs11_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 10 - 11.
 * @rx_tx_mcs13_max_nss: indicates the maximum number of spatial streams
 *     supported for reception and the maximum number of spatial streams
 *     supported for transmission for MCS 12 - 13.
 * @rx_tx_max_nss: array of the previous fields for easier loop access
 */
struct ieee80211_eht_mcs_nss_supp_bw {
	union {
		struct {
			u8 rx_tx_mcs9_max_nss;
			u8 rx_tx_mcs11_max_nss;
			u8 rx_tx_mcs13_max_nss;
		};
		u8 rx_tx_max_nss[3];
	};
};

/**
 * struct ieee80211_eht_cap_elem_fixed - EHT capabilities fixed data
 *
 * This structure is the "EHT Capabilities element" fixed fields as
 * described in P802.11be_D2.0 section 9.4.2.313.
 *
 * @mac_cap_info: MAC capabilities, see IEEE80211_EHT_MAC_CAP*
 * @phy_cap_info: PHY capabilities, see IEEE80211_EHT_PHY_CAP*
 */
struct ieee80211_eht_cap_elem_fixed {
	u8 mac_cap_info[2];
	u8 phy_cap_info[9];
} __packed;

/**
 * struct ieee80211_eht_cap_elem - EHT capabilities element
 * @fixed: fixed parts, see &ieee80211_eht_cap_elem_fixed
 * @optional: optional parts
 */
struct ieee80211_eht_cap_elem {
	struct ieee80211_eht_cap_elem_fixed fixed;

	/*
	 * Followed by:
	 * Supported EHT-MCS And NSS Set field: 4, 3, 6 or 9 octets.
	 * EHT PPE Thresholds field: variable length.
	 */
	u8 optional[];
} __packed;

#define IEEE80211_EHT_OPER_INFO_PRESENT	                        0x01
#define IEEE80211_EHT_OPER_DISABLED_SUBCHANNEL_BITMAP_PRESENT	0x02
#define IEEE80211_EHT_OPER_EHT_DEF_PE_DURATION	                0x04
#define IEEE80211_EHT_OPER_GROUP_ADDRESSED_BU_IND_LIMIT         0x08
#define IEEE80211_EHT_OPER_GROUP_ADDRESSED_BU_IND_EXP_MASK      0x30
#define IEEE80211_EHT_OPER_MCS15_DISABLE                        0x40

/**
 * struct ieee80211_eht_operation - eht operation element
 *
 * This structure is the "EHT Operation Element" fields as
 * described in P802.11be_D2.0 section 9.4.2.311
 *
 * @params: EHT operation element parameters. See &IEEE80211_EHT_OPER_*
 * @basic_mcs_nss: indicates the EHT-MCSs for each number of spatial streams in
 *     EHT PPDUs that are supported by all EHT STAs in the BSS in transmit and
 *     receive.
 * @optional: optional parts
 */
struct ieee80211_eht_operation {
	u8 params;
	struct ieee80211_eht_mcs_nss_supp_20mhz_only basic_mcs_nss;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_eht_operation_info - eht operation information
 *
 * @control: EHT operation information control.
 * @ccfs0: defines a channel center frequency for a 20, 40, 80, 160, or 320 MHz
 *     EHT BSS.
 * @ccfs1: defines a channel center frequency for a 160 or 320 MHz EHT BSS.
 * @optional: optional parts
 */
struct ieee80211_eht_operation_info {
	u8 control;
	u8 ccfs0;
	u8 ccfs1;
	u8 optional[];
} __packed;

/* EHT MAC capabilities as defined in P802.11be_D2.0 section 9.4.2.313.2 */
#define IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS			0x01
#define IEEE80211_EHT_MAC_CAP0_OM_CONTROL			0x02
#define IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1		0x04
#define IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE2		0x08
#define IEEE80211_EHT_MAC_CAP0_RESTRICTED_TWT			0x10
#define IEEE80211_EHT_MAC_CAP0_SCS_TRAFFIC_DESC			0x20
#define IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK		0xc0
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_3895	        0
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_7991	        1
#define	IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_11454	        2

#define IEEE80211_EHT_MAC_CAP1_MAX_AMPDU_LEN_MASK		0x01
#define IEEE80211_EHT_MAC_CAP1_EHT_TRS				0x02
#define IEEE80211_EHT_MAC_CAP1_TXOP_RET				0x04
#define IEEE80211_EHT_MAC_CAP1_TWO_BQRS				0x08
#define IEEE80211_EHT_MAC_CAP1_EHT_LINK_ADAPT_MASK		0x30
#define IEEE80211_EHT_MAC_CAP1_UNSOL_EPCS_PRIO_ACCESS		0x40

/* EHT PHY capabilities as defined in P802.11be_D2.0 section 9.4.2.313.3 */
#define IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ			0x02
#define IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ		0x04
#define IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI		0x08
#define IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO		0x10
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER			0x20
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE			0x40

/* EHT beamformee number of spatial streams <= 80MHz is split */
#define IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK		0x80
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK		0x03

#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK	0x1c
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK	0xe0

#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK		0x07
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK		0x38

/* EHT number of sounding dimensions for 320MHz is split */
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK		0xc0
#define IEEE80211_EHT_PHY_CAP3_SOUNDING_DIM_320MHZ_MASK		0x01
#define IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK		0x02
#define IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK		0x04
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK		0x08
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK		0x10
#define IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK			0x20
#define IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK		0x40
#define IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK			0x80

#define IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO		0x01
#define IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP			0x02
#define IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP		0x04
#define IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI	0x08
#define IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK			0xf0

#define IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK		0x01
#define IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP		0x02
#define IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP		0x04
#define IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT		0x08
#define IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK	0x30
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_0US	0
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_8US	1
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US	2
#define   IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_20US	3

/* Maximum number of supported EHT LTF is split */
#define IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK	0xc0
#define IEEE80211_EHT_PHY_CAP5_SUPP_EXTRA_EHT_LTF		0x40
#define IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK	0x07

#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_80MHZ			0x08
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_160MHZ		0x30
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_320MHZ		0x40
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK			0x78
#define IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP		0x80

#define IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW	0x01
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ	0x02
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ	0x04
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ	0x08
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ		0x10
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ		0x20
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ		0x40
#define IEEE80211_EHT_PHY_CAP7_TB_SOUNDING_FDBK_RATE_LIMIT	0x80

#define IEEE80211_EHT_PHY_CAP8_RX_1024QAM_WIDER_BW_DL_OFDMA	0x01
#define IEEE80211_EHT_PHY_CAP8_RX_4096QAM_WIDER_BW_DL_OFDMA	0x02

/*
 * EHT operation channel width as defined in P802.11be_D2.0 section 9.4.2.311
 */
#define IEEE80211_EHT_OPER_CHAN_WIDTH		0x7
#define IEEE80211_EHT_OPER_CHAN_WIDTH_20MHZ	0
#define IEEE80211_EHT_OPER_CHAN_WIDTH_40MHZ	1
#define IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ	2
#define IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ	3
#define IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ	4

/* Calculate 802.11be EHT capabilities IE Tx/Rx EHT MCS NSS Support Field size */
static inline u8
ieee80211_eht_mcs_nss_size(const struct ieee80211_he_cap_elem *he_cap,
			   const struct ieee80211_eht_cap_elem_fixed *eht_cap,
			   bool from_ap)
{
	u8 count = 0;

	/* on 2.4 GHz, if it supports 40 MHz, the result is 3 */
	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G)
		return 3;

	/* on 2.4 GHz, these three bits are reserved, so should be 0 */
	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G)
		count += 3;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		count += 3;

	if (eht_cap->phy_cap_info[0] & IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ)
		count += 3;

	if (count)
		return count;

	return from_ap ? 3 : 4;
}

/* 802.11be EHT PPE Thresholds */
#define IEEE80211_EHT_PPE_THRES_NSS_POS			0
#define IEEE80211_EHT_PPE_THRES_NSS_MASK		0xf
#define IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK	0x1f0
#define IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE		3
#define IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE	9

/*
 * Calculate 802.11be EHT capabilities IE EHT field size
 */
static inline u8
ieee80211_eht_ppe_size(u16 ppe_thres_hdr, const u8 *phy_cap_info)
{
	u32 n;

	if (!(phy_cap_info[5] &
	      IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT))
		return 0;

	n = hweight16(ppe_thres_hdr &
		      IEEE80211_EHT_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= 1 + u16_get_bits(ppe_thres_hdr, IEEE80211_EHT_PPE_THRES_NSS_MASK);

	/*
	 * Each pair is 6 bits, and we need to add the 9 "header" bits to the
	 * total size.
	 */
	n = n * IEEE80211_EHT_PPE_THRES_INFO_PPET_SIZE * 2 +
	    IEEE80211_EHT_PPE_THRES_INFO_HEADER_SIZE;
	return DIV_ROUND_UP(n, 8);
}

static inline bool
ieee80211_eht_capa_size_ok(const u8 *he_capa, const u8 *data, u8 len,
			   bool from_ap)
{
	const struct ieee80211_eht_cap_elem_fixed *elem = (const void *)data;
	u8 needed = sizeof(struct ieee80211_eht_cap_elem_fixed);

	if (len < needed || !he_capa)
		return false;

	needed += ieee80211_eht_mcs_nss_size((const void *)he_capa,
					     (const void *)data,
					     from_ap);
	if (len < needed)
		return false;

	if (elem->phy_cap_info[5] &
			IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT) {
		u16 ppe_thres_hdr;

		if (len < needed + sizeof(ppe_thres_hdr))
			return false;

		ppe_thres_hdr = get_unaligned_le16(data + needed);
		needed += ieee80211_eht_ppe_size(ppe_thres_hdr,
						 elem->phy_cap_info);
	}

	return len >= needed;
}

static inline bool
ieee80211_eht_oper_size_ok(const u8 *data, u8 len)
{
	const struct ieee80211_eht_operation *elem = (const void *)data;
	u8 needed = sizeof(*elem);

	if (len < needed)
		return false;

	if (elem->params & IEEE80211_EHT_OPER_INFO_PRESENT) {
		needed += 3;

		if (elem->params &
		    IEEE80211_EHT_OPER_DISABLED_SUBCHANNEL_BITMAP_PRESENT)
			needed += 2;
	}

	return len >= needed;
}

/* must validate ieee80211_eht_oper_size_ok() first */
static inline u16
ieee80211_eht_oper_dis_subchan_bitmap(const struct ieee80211_eht_operation *eht_oper)
{
	const struct ieee80211_eht_operation_info *info =
		(const void *)eht_oper->optional;

	if (!(eht_oper->params & IEEE80211_EHT_OPER_INFO_PRESENT))
		return 0;

	if (!(eht_oper->params & IEEE80211_EHT_OPER_DISABLED_SUBCHANNEL_BITMAP_PRESENT))
		return 0;

	return get_unaligned_le16(info->optional);
}

#define IEEE80211_BW_IND_DIS_SUBCH_PRESENT	BIT(1)

struct ieee80211_bandwidth_indication {
	u8 params;
	struct ieee80211_eht_operation_info info;
} __packed;

static inline bool
ieee80211_bandwidth_indication_size_ok(const u8 *data, u8 len)
{
	const struct ieee80211_bandwidth_indication *bwi = (const void *)data;

	if (len < sizeof(*bwi))
		return false;

	if (bwi->params & IEEE80211_BW_IND_DIS_SUBCH_PRESENT &&
	    len < sizeof(*bwi) + 2)
		return false;

	return true;
}

/* Protected EHT action codes */
enum ieee80211_protected_eht_actioncode {
	WLAN_PROTECTED_EHT_ACTION_TTLM_REQ = 0,
	WLAN_PROTECTED_EHT_ACTION_TTLM_RES = 1,
	WLAN_PROTECTED_EHT_ACTION_TTLM_TEARDOWN = 2,
	WLAN_PROTECTED_EHT_ACTION_EPCS_ENABLE_REQ = 3,
	WLAN_PROTECTED_EHT_ACTION_EPCS_ENABLE_RESP = 4,
	WLAN_PROTECTED_EHT_ACTION_EPCS_ENABLE_TEARDOWN = 5,
	WLAN_PROTECTED_EHT_ACTION_EML_OP_MODE_NOTIF = 6,
	WLAN_PROTECTED_EHT_ACTION_LINK_RECOMMEND = 7,
	WLAN_PROTECTED_EHT_ACTION_ML_OP_UPDATE_REQ = 8,
	WLAN_PROTECTED_EHT_ACTION_ML_OP_UPDATE_RESP = 9,
	WLAN_PROTECTED_EHT_ACTION_LINK_RECONFIG_NOTIF = 10,
	WLAN_PROTECTED_EHT_ACTION_LINK_RECONFIG_REQ = 11,
	WLAN_PROTECTED_EHT_ACTION_LINK_RECONFIG_RESP = 12,
};

/* multi-link device */
#define IEEE80211_MLD_MAX_NUM_LINKS	15

#define IEEE80211_ML_CONTROL_TYPE			0x0007
#define IEEE80211_ML_CONTROL_TYPE_BASIC			0
#define IEEE80211_ML_CONTROL_TYPE_PREQ			1
#define IEEE80211_ML_CONTROL_TYPE_RECONF		2
#define IEEE80211_ML_CONTROL_TYPE_TDLS			3
#define IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS		4
#define IEEE80211_ML_CONTROL_PRESENCE_MASK		0xfff0

struct ieee80211_multi_link_elem {
	__le16 control;
	u8 variable[];
} __packed;

#define IEEE80211_MLC_BASIC_PRES_LINK_ID		0x0010
#define IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT	0x0020
#define IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY		0x0040
#define IEEE80211_MLC_BASIC_PRES_EML_CAPA		0x0080
#define IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP		0x0100
#define IEEE80211_MLC_BASIC_PRES_MLD_ID			0x0200
#define IEEE80211_MLC_BASIC_PRES_EXT_MLD_CAPA_OP	0x0400

#define IEEE80211_MED_SYNC_DELAY_DURATION		0x00ff
#define IEEE80211_MED_SYNC_DELAY_SYNC_OFDM_ED_THRESH	0x0f00
#define IEEE80211_MED_SYNC_DELAY_SYNC_MAX_NUM_TXOPS	0xf000

/*
 * Described in P802.11be_D3.0
 * dot11MSDTimerDuration should default to 5484 (i.e. 171.375)
 * dot11MSDOFDMEDthreshold defaults to -72 (i.e. 0)
 * dot11MSDTXOPMAX defaults to 1
 */
#define IEEE80211_MED_SYNC_DELAY_DEFAULT		0x10ac

#define IEEE80211_EML_CAP_EMLSR_SUPP			0x0001
#define IEEE80211_EML_CAP_EMLSR_PADDING_DELAY		0x000e
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_0US		0
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_32US		1
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_64US		2
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_128US		3
#define  IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_256US		4
#define IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY	0x0070
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_0US		0
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_16US		1
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_32US		2
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_64US		3
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_128US		4
#define  IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US		5
#define IEEE80211_EML_CAP_EMLMR_SUPPORT			0x0080
#define IEEE80211_EML_CAP_EMLMR_DELAY			0x0700
#define  IEEE80211_EML_CAP_EMLMR_DELAY_0US			0
#define  IEEE80211_EML_CAP_EMLMR_DELAY_32US			1
#define  IEEE80211_EML_CAP_EMLMR_DELAY_64US			2
#define  IEEE80211_EML_CAP_EMLMR_DELAY_128US			3
#define  IEEE80211_EML_CAP_EMLMR_DELAY_256US			4
#define IEEE80211_EML_CAP_TRANSITION_TIMEOUT		0x7800
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_0			0
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128US		1
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_256US		2
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_512US		3
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_1TU		4
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_2TU		5
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_4TU		6
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_8TU		7
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_16TU		8
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_32TU		9
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_64TU		10
#define  IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU		11

#define IEEE80211_MLD_CAP_OP_MAX_SIMUL_LINKS		0x000f
#define IEEE80211_MLD_CAP_OP_SRS_SUPPORT		0x0010
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP	0x0060
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_NO_SUPP	0
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP_SAME	1
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_RESERVED	2
#define IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP_DIFF	3
#define IEEE80211_MLD_CAP_OP_FREQ_SEP_TYPE_IND		0x0f80
#define IEEE80211_MLD_CAP_OP_AAR_SUPPORT		0x1000
#define IEEE80211_MLD_CAP_OP_LINK_RECONF_SUPPORT	0x2000
#define IEEE80211_MLD_CAP_OP_ALIGNED_TWT_SUPPORT	0x4000

struct ieee80211_mle_basic_common_info {
	u8 len;
	u8 mld_mac_addr[ETH_ALEN];
	u8 variable[];
} __packed;

#define IEEE80211_MLC_PREQ_PRES_MLD_ID			0x0010

struct ieee80211_mle_preq_common_info {
	u8 len;
	u8 variable[];
} __packed;

#define IEEE80211_MLC_RECONF_PRES_MLD_MAC_ADDR		0x0010
#define IEEE80211_MLC_RECONF_PRES_EML_CAPA		0x0020
#define IEEE80211_MLC_RECONF_PRES_MLD_CAPA_OP		0x0040
#define IEEE80211_MLC_RECONF_PRES_EXT_MLD_CAPA_OP	0x0080

/* no fixed fields in RECONF */

struct ieee80211_mle_tdls_common_info {
	u8 len;
	u8 ap_mld_mac_addr[ETH_ALEN];
} __packed;

#define IEEE80211_MLC_PRIO_ACCESS_PRES_AP_MLD_MAC_ADDR	0x0010

#define IEEE80211_EML_CTRL_EMLSR_MODE		BIT(0)
#define IEEE80211_EML_CTRL_EMLMR_MODE		BIT(1)
#define IEEE80211_EML_CTRL_EMLSR_PARAM_UPDATE	BIT(2)
#define IEEE80211_EML_CTRL_INDEV_COEX_ACT	BIT(3)

#define IEEE80211_EML_EMLSR_PAD_DELAY		0x07
#define IEEE80211_EML_EMLSR_TRANS_DELAY		0x38

#define IEEE80211_EML_EMLMR_RX_MCS_MAP		0xf0
#define IEEE80211_EML_EMLMR_TX_MCS_MAP		0x0f

/* no fixed fields in PRIO_ACCESS */

/**
 * ieee80211_mle_common_size - check multi-link element common size
 * @data: multi-link element, must already be checked for size using
 *	ieee80211_mle_size_ok()
 * Return: the size of the multi-link element's "common" subfield 
 */
static inline u8 ieee80211_mle_common_size(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);

	switch (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE)) {
	case IEEE80211_ML_CONTROL_TYPE_BASIC:
	case IEEE80211_ML_CONTROL_TYPE_PREQ:
	case IEEE80211_ML_CONTROL_TYPE_TDLS:
	case IEEE80211_ML_CONTROL_TYPE_RECONF:
	case IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS:
		/*
		 * The length is the first octet pointed by mle->variable so no
		 * need to add anything
		 */
		break;
	default:
		WARN_ON(1);
		return 0;
	}

	return sizeof(*mle) + mle->variable[0];
}

/**
 * ieee80211_mle_get_link_id - returns the link ID
 * @data: the basic multi link element
 * Return: the link ID, or -1 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline int ieee80211_mle_get_link_id(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_LINK_ID))
		return -1;

	return *common;
}

/**
 * ieee80211_mle_get_bss_param_ch_cnt - returns the BSS parameter change count
 * @data: pointer to the basic multi link element
 * Return: the BSS Parameter Change Count field value, or -1 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline int
ieee80211_mle_get_bss_param_ch_cnt(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT))
		return -1;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;

	return *common;
}

/**
 * ieee80211_mle_get_eml_med_sync_delay - returns the medium sync delay
 * @data: pointer to the multi-link element
 * Return: the medium synchronization delay field value from the multi-link
 *	element, or the default value (%IEEE80211_MED_SYNC_DELAY_DEFAULT)
 *	if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline u16 ieee80211_mle_get_eml_med_sync_delay(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY))
		return IEEE80211_MED_SYNC_DELAY_DEFAULT;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;

	return get_unaligned_le16(common);
}

/**
 * ieee80211_mle_get_eml_cap - returns the EML capability
 * @data: pointer to the multi-link element
 * Return: the EML capability field value from the multi-link element,
 *	or 0 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline u16 ieee80211_mle_get_eml_cap(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/* common points now at the beginning of ieee80211_mle_basic_common_info */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_EML_CAPA))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
		common += 2;

	return get_unaligned_le16(common);
}

/**
 * ieee80211_mle_get_mld_capa_op - returns the MLD capabilities and operations.
 * @data: pointer to the multi-link element
 * Return: the MLD capabilities and operations field value from the multi-link
 *	element, or 0 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline u16 ieee80211_mle_get_mld_capa_op(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/*
	 * common points now at the beginning of
	 * ieee80211_mle_basic_common_info
	 */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_EML_CAPA)
		common += 2;

	return get_unaligned_le16(common);
}

/* Defined in Figure 9-1074t in P802.11be_D7.0 */
#define IEEE80211_EHT_ML_EXT_MLD_CAPA_OP_PARAM_UPDATE           0x0001
#define IEEE80211_EHT_ML_EXT_MLD_CAPA_OP_RECO_MAX_LINKS_MASK    0x001e
#define IEEE80211_EHT_ML_EXT_MLD_CAPA_NSTR_UPDATE               0x0020
#define IEEE80211_EHT_ML_EXT_MLD_CAPA_EMLSR_ENA_ON_ONE_LINK     0x0040
#define IEEE80211_EHT_ML_EXT_MLD_CAPA_BTM_MLD_RECO_MULTI_AP     0x0080

/**
 * ieee80211_mle_get_ext_mld_capa_op - returns the extended MLD capabilities
 *	and operations.
 * @data: pointer to the multi-link element
 * Return: the extended MLD capabilities and operations field value from
 *	the multi-link element, or 0 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline u16 ieee80211_mle_get_ext_mld_capa_op(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/*
	 * common points now at the beginning of
	 * ieee80211_mle_basic_common_info
	 */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_EXT_MLD_CAPA_OP))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_EML_CAPA)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_MLD_ID)
		common += 1;

	return get_unaligned_le16(common);
}

/**
 * ieee80211_mle_get_mld_id - returns the MLD ID
 * @data: pointer to the multi-link element
 * Return: The MLD ID in the given multi-link element, or 0 if not present
 *
 * The element is assumed to be of the correct type (BASIC) and big enough,
 * this must be checked using ieee80211_mle_type_ok().
 */
static inline u8 ieee80211_mle_get_mld_id(const u8 *data)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control = le16_to_cpu(mle->control);
	const u8 *common = mle->variable;

	/*
	 * common points now at the beginning of
	 * ieee80211_mle_basic_common_info
	 */
	common += sizeof(struct ieee80211_mle_basic_common_info);

	if (!(control & IEEE80211_MLC_BASIC_PRES_MLD_ID))
		return 0;

	if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
		common += 1;
	if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_EML_CAPA)
		common += 2;
	if (control & IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP)
		common += 2;

	return *common;
}

/**
 * ieee80211_mle_size_ok - validate multi-link element size
 * @data: pointer to the element data
 * @len: length of the containing element
 * Return: whether or not the multi-link element size is OK
 */
static inline bool ieee80211_mle_size_ok(const u8 *data, size_t len)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u8 fixed = sizeof(*mle);
	u8 common = 0;
	bool check_common_len = false;
	u16 control;

	if (!data || len < fixed)
		return false;

	control = le16_to_cpu(mle->control);

	switch (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE)) {
	case IEEE80211_ML_CONTROL_TYPE_BASIC:
		common += sizeof(struct ieee80211_mle_basic_common_info);
		check_common_len = true;
		if (control & IEEE80211_MLC_BASIC_PRES_LINK_ID)
			common += 1;
		if (control & IEEE80211_MLC_BASIC_PRES_BSS_PARAM_CH_CNT)
			common += 1;
		if (control & IEEE80211_MLC_BASIC_PRES_MED_SYNC_DELAY)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_EML_CAPA)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP)
			common += 2;
		if (control & IEEE80211_MLC_BASIC_PRES_MLD_ID)
			common += 1;
		if (control & IEEE80211_MLC_BASIC_PRES_EXT_MLD_CAPA_OP)
			common += 2;
		break;
	case IEEE80211_ML_CONTROL_TYPE_PREQ:
		common += sizeof(struct ieee80211_mle_preq_common_info);
		if (control & IEEE80211_MLC_PREQ_PRES_MLD_ID)
			common += 1;
		check_common_len = true;
		break;
	case IEEE80211_ML_CONTROL_TYPE_RECONF:
		if (control & IEEE80211_MLC_RECONF_PRES_MLD_MAC_ADDR)
			common += ETH_ALEN;
		if (control & IEEE80211_MLC_RECONF_PRES_EML_CAPA)
			common += 2;
		if (control & IEEE80211_MLC_RECONF_PRES_MLD_CAPA_OP)
			common += 2;
		if (control & IEEE80211_MLC_RECONF_PRES_EXT_MLD_CAPA_OP)
			common += 2;
		break;
	case IEEE80211_ML_CONTROL_TYPE_TDLS:
		common += sizeof(struct ieee80211_mle_tdls_common_info);
		check_common_len = true;
		break;
	case IEEE80211_ML_CONTROL_TYPE_PRIO_ACCESS:
		common = ETH_ALEN + 1;
		break;
	default:
		/* we don't know this type */
		return true;
	}

	if (len < fixed + common)
		return false;

	if (!check_common_len)
		return true;

	/* if present, common length is the first octet there */
	return mle->variable[0] >= common;
}

/**
 * ieee80211_mle_type_ok - validate multi-link element type and size
 * @data: pointer to the element data
 * @type: expected type of the element
 * @len: length of the containing element
 * Return: whether or not the multi-link element type matches and size is OK
 */
static inline bool ieee80211_mle_type_ok(const u8 *data, u8 type, size_t len)
{
	const struct ieee80211_multi_link_elem *mle = (const void *)data;
	u16 control;

	if (!ieee80211_mle_size_ok(data, len))
		return false;

	control = le16_to_cpu(mle->control);

	if (u16_get_bits(control, IEEE80211_ML_CONTROL_TYPE) == type)
		return true;

	return false;
}

enum ieee80211_mle_subelems {
	IEEE80211_MLE_SUBELEM_PER_STA_PROFILE		= 0,
	IEEE80211_MLE_SUBELEM_FRAGMENT		        = 254,
};

#define IEEE80211_MLE_STA_CONTROL_LINK_ID			0x000f
#define IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE		0x0010
#define IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT		0x0020
#define IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT		0x0040
#define IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT		0x0080
#define IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT		0x0100
#define IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT	0x0200
#define IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE		0x0400
#define IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT	0x0800

struct ieee80211_mle_per_sta_profile {
	__le16 control;
	u8 sta_info_len;
	u8 variable[];
} __packed;

/**
 * ieee80211_mle_basic_sta_prof_size_ok - validate basic multi-link element sta
 *	profile size
 * @data: pointer to the sub element data
 * @len: length of the containing sub element
 * Return: %true if the STA profile is large enough, %false otherwise
 */
static inline bool ieee80211_mle_basic_sta_prof_size_ok(const u8 *data,
							size_t len)
{
	const struct ieee80211_mle_per_sta_profile *prof = (const void *)data;
	u16 control;
	u8 fixed = sizeof(*prof);
	u8 info_len = 1;

	if (len < fixed)
		return false;

	control = le16_to_cpu(prof->control);

	if (control & IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT)
		info_len += 6;
	if (control & IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT)
		info_len += 8;
	if (control & IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE &&
	    control & IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT) {
		if (control & IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE)
			info_len += 2;
		else
			info_len += 1;
	}
	if (control & IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT)
		info_len += 1;

	return prof->sta_info_len >= info_len &&
	       fixed + prof->sta_info_len - 1 <= len;
}

/**
 * ieee80211_mle_basic_sta_prof_bss_param_ch_cnt - get per-STA profile BSS
 *	parameter change count
 * @prof: the per-STA profile, having been checked with
 *	ieee80211_mle_basic_sta_prof_size_ok() for the correct length
 *
 * Return: The BSS parameter change count value if present, 0 otherwise.
 */
static inline u8
ieee80211_mle_basic_sta_prof_bss_param_ch_cnt(const struct ieee80211_mle_per_sta_profile *prof)
{
	u16 control = le16_to_cpu(prof->control);
	const u8 *pos = prof->variable;

	if (!(control & IEEE80211_MLE_STA_CONTROL_BSS_PARAM_CHANGE_CNT_PRESENT))
		return 0;

	if (control & IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT)
		pos += 6;
	if (control & IEEE80211_MLE_STA_CONTROL_BEACON_INT_PRESENT)
		pos += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_TSF_OFFS_PRESENT)
		pos += 8;
	if (control & IEEE80211_MLE_STA_CONTROL_DTIM_INFO_PRESENT)
		pos += 2;
	if (control & IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE &&
	    control & IEEE80211_MLE_STA_CONTROL_NSTR_LINK_PAIR_PRESENT) {
		if (control & IEEE80211_MLE_STA_CONTROL_NSTR_BITMAP_SIZE)
			pos += 2;
		else
			pos += 1;
	}

	return *pos;
}

#define IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID			0x000f
#define IEEE80211_MLE_STA_RECONF_CONTROL_COMPLETE_PROFILE		0x0010
#define IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT		0x0020
#define IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT		0x0040
#define	IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE                 0x0780
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_AP_REM          0
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_OP_PARAM_UPDATE 1
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_ADD_LINK        2
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_DEL_LINK        3
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_TYPE_NSTR_STATUS     4
#define IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_PARAMS_PRESENT       0x0800

/**
 * ieee80211_mle_reconf_sta_prof_size_ok - validate reconfiguration multi-link
 *	element sta profile size.
 * @data: pointer to the sub element data
 * @len: length of the containing sub element
 * Return: %true if the STA profile is large enough, %false otherwise
 */
static inline bool ieee80211_mle_reconf_sta_prof_size_ok(const u8 *data,
							 size_t len)
{
	const struct ieee80211_mle_per_sta_profile *prof = (const void *)data;
	u16 control;
	u8 fixed = sizeof(*prof);
	u8 info_len = 1;

	if (len < fixed)
		return false;

	control = le16_to_cpu(prof->control);

	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT)
		info_len += ETH_ALEN;
	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT)
		info_len += 2;
	if (control & IEEE80211_MLE_STA_RECONF_CONTROL_OPERATION_PARAMS_PRESENT)
		info_len += 2;

	return prof->sta_info_len >= info_len &&
	       fixed + prof->sta_info_len - 1 <= len;
}

#define IEEE80211_MLE_STA_EPCS_CONTROL_LINK_ID			0x000f
#define IEEE80211_EPCS_ENA_RESP_BODY_LEN                        3

static inline bool ieee80211_tid_to_link_map_size_ok(const u8 *data, size_t len)
{
	const struct ieee80211_ttlm_elem *t2l = (const void *)data;
	u8 control, fixed = sizeof(*t2l), elem_len = 0;

	if (len < fixed)
		return false;

	control = t2l->control;

	if (control & IEEE80211_TTLM_CONTROL_SWITCH_TIME_PRESENT)
		elem_len += 2;
	if (control & IEEE80211_TTLM_CONTROL_EXPECTED_DUR_PRESENT)
		elem_len += 3;

	if (!(control & IEEE80211_TTLM_CONTROL_DEF_LINK_MAP)) {
		u8 bm_size;

		elem_len += 1;
		if (len < fixed + elem_len)
			return false;

		if (control & IEEE80211_TTLM_CONTROL_LINK_MAP_SIZE)
			bm_size = 1;
		else
			bm_size = 2;

		elem_len += hweight8(t2l->optional[0]) * bm_size;
	}

	return len >= fixed + elem_len;
}

/**
 * ieee80211_emlsr_pad_delay_in_us - Fetch the EMLSR Padding delay
 *	in microseconds
 * @eml_cap: EML capabilities field value from common info field of
 *	the Multi-link element
 * Return: the EMLSR Padding delay (in microseconds) encoded in the
 *	EML Capabilities field
 */

static inline u32 ieee80211_emlsr_pad_delay_in_us(u16 eml_cap)
{
	/* IEEE Std 802.11be-2024 Table 9-417i—Encoding of the EMLSR
	 * Padding Delay subfield.
	 */
	u32 pad_delay = u16_get_bits(eml_cap,
				     IEEE80211_EML_CAP_EMLSR_PADDING_DELAY);

	if (!pad_delay ||
	    pad_delay > IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_256US)
		return 0;

	return 32 * (1 << (pad_delay - 1));
}

/**
 * ieee80211_emlsr_trans_delay_in_us - Fetch the EMLSR Transition
 *	delay in microseconds
 * @eml_cap: EML capabilities field value from common info field of
 *	the Multi-link element
 * Return: the EMLSR Transition delay (in microseconds) encoded in the
 *	EML Capabilities field
 */

static inline u32 ieee80211_emlsr_trans_delay_in_us(u16 eml_cap)
{
	/* IEEE Std 802.11be-2024 Table 9-417j—Encoding of the EMLSR
	 * Transition Delay subfield.
	 */
	u32 trans_delay =
		u16_get_bits(eml_cap,
			     IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY);

	/* invalid values also just use 0 */
	if (!trans_delay ||
	    trans_delay > IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_256US)
		return 0;

	return 16 * (1 << (trans_delay - 1));
}

/**
 * ieee80211_eml_trans_timeout_in_us - Fetch the EMLSR Transition
 *	timeout value in microseconds
 * @eml_cap: EML capabilities field value from common info field of
 *	the Multi-link element
 * Return: the EMLSR Transition timeout (in microseconds) encoded in
 *	the EML Capabilities field
 */

static inline u32 ieee80211_eml_trans_timeout_in_us(u16 eml_cap)
{
	/* IEEE Std 802.11be-2024 Table 9-417m—Encoding of the
	 * Transition Timeout subfield.
	 */
	u8 timeout = u16_get_bits(eml_cap,
				  IEEE80211_EML_CAP_TRANSITION_TIMEOUT);

	/* invalid values also just use 0 */
	if (!timeout || timeout > IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU)
		return 0;

	return 128 * (1 << (timeout - 1));
}

#define for_each_mle_subelement(_elem, _data, _len)			\
	if (ieee80211_mle_size_ok(_data, _len))				\
		for_each_element(_elem,					\
				 _data + ieee80211_mle_common_size(_data),\
				 _len - ieee80211_mle_common_size(_data))

#endif /* LINUX_IEEE80211_EHT_H */
