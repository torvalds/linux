/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 HE definitions
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

#ifndef LINUX_IEEE80211_HE_H
#define LINUX_IEEE80211_HE_H

#include <linux/types.h>
#include <linux/if_ether.h>

#define IEEE80211_TWT_CONTROL_NDP			BIT(0)
#define IEEE80211_TWT_CONTROL_RESP_MODE			BIT(1)
#define IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST	BIT(3)
#define IEEE80211_TWT_CONTROL_RX_DISABLED		BIT(4)
#define IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT		BIT(5)

#define IEEE80211_TWT_REQTYPE_REQUEST			BIT(0)
#define IEEE80211_TWT_REQTYPE_SETUP_CMD			GENMASK(3, 1)
#define IEEE80211_TWT_REQTYPE_TRIGGER			BIT(4)
#define IEEE80211_TWT_REQTYPE_IMPLICIT			BIT(5)
#define IEEE80211_TWT_REQTYPE_FLOWTYPE			BIT(6)
#define IEEE80211_TWT_REQTYPE_FLOWID			GENMASK(9, 7)
#define IEEE80211_TWT_REQTYPE_WAKE_INT_EXP		GENMASK(14, 10)
#define IEEE80211_TWT_REQTYPE_PROTECTION		BIT(15)

enum ieee80211_twt_setup_cmd {
	TWT_SETUP_CMD_REQUEST,
	TWT_SETUP_CMD_SUGGEST,
	TWT_SETUP_CMD_DEMAND,
	TWT_SETUP_CMD_GROUPING,
	TWT_SETUP_CMD_ACCEPT,
	TWT_SETUP_CMD_ALTERNATE,
	TWT_SETUP_CMD_DICTATE,
	TWT_SETUP_CMD_REJECT,
};

struct ieee80211_twt_params {
	__le16 req_type;
	__le64 twt;
	u8 min_twt_dur;
	__le16 mantissa;
	u8 channel;
} __packed;

struct ieee80211_twt_setup {
	u8 dialog_token;
	u8 element_id;
	u8 length;
	u8 control;
	u8 params[];
} __packed;

/**
 * struct ieee80211_he_cap_elem - HE capabilities element
 * @mac_cap_info: HE MAC Capabilities Information
 * @phy_cap_info: HE PHY Capabilities Information
 *
 * This structure represents the fixed fields of the payload of the
 * "HE capabilities element" as described in IEEE Std 802.11ax-2021
 * sections 9.4.2.248.2 and 9.4.2.248.3.
 */
struct ieee80211_he_cap_elem {
	u8 mac_cap_info[6];
	u8 phy_cap_info[11];
} __packed;

#define IEEE80211_TX_RX_MCS_NSS_DESC_MAX_LEN	5

/**
 * enum ieee80211_he_mcs_support - HE MCS support definitions
 * @IEEE80211_HE_MCS_SUPPORT_0_7: MCSes 0-7 are supported for the
 *	number of streams
 * @IEEE80211_HE_MCS_SUPPORT_0_9: MCSes 0-9 are supported
 * @IEEE80211_HE_MCS_SUPPORT_0_11: MCSes 0-11 are supported
 * @IEEE80211_HE_MCS_NOT_SUPPORTED: This number of streams isn't supported
 *
 * These definitions are used in each 2-bit subfield of the rx_mcs_*
 * and tx_mcs_* fields of &struct ieee80211_he_mcs_nss_supp, which are
 * both split into 8 subfields by number of streams. These values indicate
 * which MCSes are supported for the number of streams the value appears
 * for.
 */
enum ieee80211_he_mcs_support {
	IEEE80211_HE_MCS_SUPPORT_0_7	= 0,
	IEEE80211_HE_MCS_SUPPORT_0_9	= 1,
	IEEE80211_HE_MCS_SUPPORT_0_11	= 2,
	IEEE80211_HE_MCS_NOT_SUPPORTED	= 3,
};

/**
 * struct ieee80211_he_mcs_nss_supp - HE Tx/Rx HE MCS NSS Support Field
 *
 * This structure holds the data required for the Tx/Rx HE MCS NSS Support Field
 * described in P802.11ax_D2.0 section 9.4.2.237.4
 *
 * @rx_mcs_80: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @tx_mcs_80: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     widths less than 80MHz.
 * @rx_mcs_160: Rx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @tx_mcs_160: Tx MCS map 2 bits for each stream, total 8 streams, for channel
 *     width 160MHz.
 * @rx_mcs_80p80: Rx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 * @tx_mcs_80p80: Tx MCS map 2 bits for each stream, total 8 streams, for
 *     channel width 80p80MHz.
 */
struct ieee80211_he_mcs_nss_supp {
	__le16 rx_mcs_80;
	__le16 tx_mcs_80;
	__le16 rx_mcs_160;
	__le16 tx_mcs_160;
	__le16 rx_mcs_80p80;
	__le16 tx_mcs_80p80;
} __packed;

/**
 * struct ieee80211_he_operation - HE Operation element
 * @he_oper_params: HE Operation Parameters + BSS Color Information
 * @he_mcs_nss_set: Basic HE-MCS And NSS Set
 * @optional: Optional fields VHT Operation Information, Max Co-Hosted
 *            BSSID Indicator, and 6 GHz Operation Information
 *
 * This structure represents the payload of the "HE Operation
 * element" as described in IEEE Std 802.11ax-2021 section 9.4.2.249.
 */
struct ieee80211_he_operation {
	__le32 he_oper_params;
	__le16 he_mcs_nss_set;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_he_spr - Spatial Reuse Parameter Set element
 * @he_sr_control: SR Control
 * @optional: Optional fields Non-SRG OBSS PD Max Offset, SRG OBSS PD
 *            Min Offset, SRG OBSS PD Max Offset, SRG BSS Color
 *            Bitmap, and SRG Partial BSSID Bitmap
 *
 * This structure represents the payload of the "Spatial Reuse
 * Parameter Set element" as described in IEEE Std 802.11ax-2021
 * section 9.4.2.252.
 */
struct ieee80211_he_spr {
	u8 he_sr_control;
	u8 optional[];
} __packed;

/**
 * struct ieee80211_he_mu_edca_param_ac_rec - MU AC Parameter Record field
 * @aifsn: ACI/AIFSN
 * @ecw_min_max: ECWmin/ECWmax
 * @mu_edca_timer: MU EDCA Timer
 *
 * This structure represents the "MU AC Parameter Record" as described
 * in IEEE Std 802.11ax-2021 section 9.4.2.251, Figure 9-788p.
 */
struct ieee80211_he_mu_edca_param_ac_rec {
	u8 aifsn;
	u8 ecw_min_max;
	u8 mu_edca_timer;
} __packed;

/**
 * struct ieee80211_mu_edca_param_set - MU EDCA Parameter Set element
 * @mu_qos_info: QoS Info
 * @ac_be: MU AC_BE Parameter Record
 * @ac_bk: MU AC_BK Parameter Record
 * @ac_vi: MU AC_VI Parameter Record
 * @ac_vo: MU AC_VO Parameter Record
 *
 * This structure represents the payload of the "MU EDCA Parameter Set
 * element" as described in IEEE Std 802.11ax-2021 section 9.4.2.251.
 */
struct ieee80211_mu_edca_param_set {
	u8 mu_qos_info;
	struct ieee80211_he_mu_edca_param_ac_rec ac_be;
	struct ieee80211_he_mu_edca_param_ac_rec ac_bk;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vi;
	struct ieee80211_he_mu_edca_param_ac_rec ac_vo;
} __packed;

/* 802.11ax HE MAC capabilities */
#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01
#define IEEE80211_HE_MAC_CAP0_TWT_REQ				0x02
#define IEEE80211_HE_MAC_CAP0_TWT_RES				0x04
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_NOT_SUPP		0x00
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_1		0x08
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_2		0x10
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_LEVEL_3		0x18
#define IEEE80211_HE_MAC_CAP0_DYNAMIC_FRAG_MASK			0x18
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_1		0x00
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_2		0x20
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_4		0x40
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_8		0x60
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_16		0x80
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_32		0xa0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_64		0xc0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_UNLIMITED	0xe0
#define IEEE80211_HE_MAC_CAP0_MAX_NUM_FRAG_MSDU_MASK		0xe0

#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_UNLIMITED		0x00
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_128			0x01
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_256			0x02
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_512			0x03
#define IEEE80211_HE_MAC_CAP1_MIN_FRAG_SIZE_MASK		0x03
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_0US		0x00
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_8US		0x04
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK		0x0c
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1		0x00
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_2		0x10
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_3		0x20
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_4		0x30
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_5		0x40
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_6		0x50
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_7		0x60
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8		0x70
#define IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_MASK		0x70

/* Link adaptation is split between byte HE_MAC_CAP1 and
 * HE_MAC_CAP2. It should be set only if IEEE80211_HE_MAC_CAP0_HTC_HE
 * in which case the following values apply:
 * 0 = No feedback.
 * 1 = reserved.
 * 2 = Unsolicited feedback.
 * 3 = both
 */
#define IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION			0x80

#define IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION			0x01
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02
#define IEEE80211_HE_MAC_CAP2_TRS				0x04
#define IEEE80211_HE_MAC_CAP2_BSR				0x08
#define IEEE80211_HE_MAC_CAP2_BCAST_TWT				0x10
#define IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP			0x20
#define IEEE80211_HE_MAC_CAP2_MU_CASCADING			0x40
#define IEEE80211_HE_MAC_CAP2_ACK_EN				0x80

#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02
#define IEEE80211_HE_MAC_CAP3_OFDMA_RA				0x04

/* The maximum length of an A-MDPU is defined by the combination of the Maximum
 * A-MDPU Length Exponent field in the HT capabilities, VHT capabilities and the
 * same field in the HE capabilities.
 */
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0		0x00
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1		0x08
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3		0x18
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK		0x18
#define IEEE80211_HE_MAC_CAP3_AMSDU_FRAG			0x20
#define IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED			0x40
#define IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS		0x80

#define IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG		0x01
#define IEEE80211_HE_MAC_CAP4_QTP				0x02
#define IEEE80211_HE_MAC_CAP4_BQR				0x04
#define IEEE80211_HE_MAC_CAP4_PSR_RESP				0x08
#define IEEE80211_HE_MAC_CAP4_NDP_FB_REP			0x10
#define IEEE80211_HE_MAC_CAP4_OPS				0x20
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU			0x40
/* Multi TID agg TX is split between byte #4 and #5
 * The value is a combination of B39,B40,B41
 */
#define IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39		0x80

#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40		0x01
#define IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41		0x02
#define IEEE80211_HE_MAC_CAP5_SUBCHAN_SELECTIVE_TRANSMISSION	0x04
#define IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU			0x08
#define IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX		0x10
#define IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS			0x20
#define IEEE80211_HE_MAC_CAP5_PUNCTURED_SOUNDING		0x40
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x80

#define IEEE80211_HE_VHT_MAX_AMPDU_FACTOR	20
#define IEEE80211_HE_HT_MAX_AMPDU_FACTOR	16
#define IEEE80211_HE_6GHZ_MAX_AMPDU_FACTOR	13

/* 802.11ax HE PHY capabilities */
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G	0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G		0x08
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G	0x10
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL		0x1e

#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G	0x20
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G	0x40
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK			0xfe

#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ	0x01
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ	0x02
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_20MHZ	0x04
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_160MHZ_ONLY_SECOND_40MHZ	0x08
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK			0x0f
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A				0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD			0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US		0x40
/* Midamble RX/TX Max NSTS is split between byte #2 and byte #3 */
#define IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS			0x80

#define IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS			0x01
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US			0x02
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ			0x04
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ			0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX				0x10
#define IEEE80211_HE_PHY_CAP2_DOPPLER_RX				0x20

/* Note that the meaning of UL MU below is different between an AP and a non-AP
 * sta, where in the AP case it indicates support for Rx and in the non-AP sta
 * case it indicates support for Tx.
 */
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO			0x40
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO			0x80

#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK			0x01
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK			0x02
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK			0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2				0x04
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_NO_DCM			0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK			0x08
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK			0x10
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK			0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1				0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2				0x20
#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU		0x40
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER				0x80

#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE				0x01
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER				0x02

/* Minimal allowed value of Max STS under 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4		0x0c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5		0x10
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_6		0x14
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_7		0x18
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8		0x1c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK	0x1c

/* Minimal allowed value of Max STS above 80MHz is 3 */
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4		0x60
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5		0x80
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_6		0xa0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_7		0xc0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8		0xe0
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK	0xe0

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2	0x01
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_3	0x02
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_4	0x03
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_5	0x04
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_6	0x05
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_7	0x06
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_8	0x07
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK	0x07

#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_1	0x00
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2	0x08
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_3	0x10
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_4	0x18
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_5	0x20
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_6	0x28
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_7	0x30
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_8	0x38
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK	0x38

#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK				0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK				0x80

#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU			0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU			0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB			0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB		0x08
#define IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB				0x10
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE			0x20
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO		0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT			0x80

#define IEEE80211_HE_PHY_CAP7_PSR_BASED_SR				0x01
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP			0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI		0x04
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1					0x08
#define IEEE80211_HE_PHY_CAP7_MAX_NC_2					0x10
#define IEEE80211_HE_PHY_CAP7_MAX_NC_3					0x18
#define IEEE80211_HE_PHY_CAP7_MAX_NC_4					0x20
#define IEEE80211_HE_PHY_CAP7_MAX_NC_5					0x28
#define IEEE80211_HE_PHY_CAP7_MAX_NC_6					0x30
#define IEEE80211_HE_PHY_CAP7_MAX_NC_7					0x38
#define IEEE80211_HE_PHY_CAP7_MAX_NC_MASK				0x38
#define IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ			0x40
#define IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ			0x80

#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI		0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G		0x02
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU			0x04
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU			0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI		0x10
#define IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF		0x20
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242				0x00
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484				0x40
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996				0x80
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996				0xc0
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK				0xc0

#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM		0x01
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK		0x02
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU		0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU		0x08
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB	0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB	0x20
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_0US			0x0
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_8US			0x1
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US			0x2
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED		0x3
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS			6
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK			0xc0

#define IEEE80211_HE_PHY_CAP10_HE_MU_M1RU_MAX_LTF			0x01

/* 802.11ax HE TX/RX MCS NSS Support  */
#define IEEE80211_TX_RX_MCS_NSS_SUPP_HIGHEST_MCS_POS			(3)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_POS			(6)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_POS			(11)
#define IEEE80211_TX_RX_MCS_NSS_SUPP_TX_BITMAP_MASK			0x07c0
#define IEEE80211_TX_RX_MCS_NSS_SUPP_RX_BITMAP_MASK			0xf800

/* TX/RX HE MCS Support field Highest MCS subfield encoding */
enum ieee80211_he_highest_mcs_supported_subfield_enc {
	HIGHEST_MCS_SUPPORTED_MCS7 = 0,
	HIGHEST_MCS_SUPPORTED_MCS8,
	HIGHEST_MCS_SUPPORTED_MCS9,
	HIGHEST_MCS_SUPPORTED_MCS10,
	HIGHEST_MCS_SUPPORTED_MCS11,
};

/* Calculate 802.11ax HE capabilities IE Tx/Rx HE MCS NSS Support Field size */
static inline u8
ieee80211_he_mcs_nss_size(const struct ieee80211_he_cap_elem *he_cap)
{
	u8 count = 4;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G)
		count += 4;

	if (he_cap->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
		count += 4;

	return count;
}

/* 802.11ax HE PPE Thresholds */
#define IEEE80211_PPE_THRES_NSS_SUPPORT_2NSS			(1)
#define IEEE80211_PPE_THRES_NSS_POS				(0)
#define IEEE80211_PPE_THRES_NSS_MASK				(7)
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_2x966_AND_966_RU	\
	(BIT(5) | BIT(6))
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK		0x78
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_POS		(3)
#define IEEE80211_PPE_THRES_INFO_PPET_SIZE			(3)
#define IEEE80211_HE_PPE_THRES_INFO_HEADER_SIZE			(7)

/*
 * Calculate 802.11ax HE capabilities IE PPE field size
 * Input: Header byte of ppe_thres (first byte), and HE capa IE's PHY cap u8*
 */
static inline u8
ieee80211_he_ppe_size(u8 ppe_thres_hdr, const u8 *phy_cap_info)
{
	u8 n;

	if ((phy_cap_info[6] &
	     IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) == 0)
		return 0;

	n = hweight8(ppe_thres_hdr &
		     IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= (1 + ((ppe_thres_hdr & IEEE80211_PPE_THRES_NSS_MASK) >>
		   IEEE80211_PPE_THRES_NSS_POS));

	/*
	 * Each pair is 6 bits, and we need to add the 7 "header" bits to the
	 * total size.
	 */
	n = (n * IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2) + 7;
	n = DIV_ROUND_UP(n, 8);

	return n;
}

static inline bool ieee80211_he_capa_size_ok(const u8 *data, u8 len)
{
	const struct ieee80211_he_cap_elem *he_cap_ie_elem = (const void *)data;
	u8 needed = sizeof(*he_cap_ie_elem);

	if (len < needed)
		return false;

	needed += ieee80211_he_mcs_nss_size(he_cap_ie_elem);
	if (len < needed)
		return false;

	if (he_cap_ie_elem->phy_cap_info[6] &
			IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		if (len < needed + 1)
			return false;
		needed += ieee80211_he_ppe_size(data[needed],
						he_cap_ie_elem->phy_cap_info);
	}

	return len >= needed;
}

/* HE Operation defines */
#define IEEE80211_HE_OPERATION_DFLT_PE_DURATION_MASK		0x00000007
#define IEEE80211_HE_OPERATION_TWT_REQUIRED			0x00000008
#define IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK		0x00003ff0
#define IEEE80211_HE_OPERATION_RTS_THRESHOLD_OFFSET		4
#define IEEE80211_HE_OPERATION_VHT_OPER_INFO			0x00004000
#define IEEE80211_HE_OPERATION_CO_HOSTED_BSS			0x00008000
#define IEEE80211_HE_OPERATION_ER_SU_DISABLE			0x00010000
#define IEEE80211_HE_OPERATION_6GHZ_OP_INFO			0x00020000
#define IEEE80211_HE_OPERATION_BSS_COLOR_MASK			0x3f000000
#define IEEE80211_HE_OPERATION_BSS_COLOR_OFFSET			24
#define IEEE80211_HE_OPERATION_PARTIAL_BSS_COLOR		0x40000000
#define IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED		0x80000000

#define IEEE80211_6GHZ_CTRL_REG_LPI_AP			0
#define IEEE80211_6GHZ_CTRL_REG_SP_AP			1
#define IEEE80211_6GHZ_CTRL_REG_VLP_AP			2
#define IEEE80211_6GHZ_CTRL_REG_INDOOR_LPI_AP		3
#define IEEE80211_6GHZ_CTRL_REG_INDOOR_SP_AP_OLD	4
#define IEEE80211_6GHZ_CTRL_REG_AP_ROLE_NOT_RELEVANT	7
#define IEEE80211_6GHZ_CTRL_REG_INDOOR_SP_AP		8

/**
 * struct ieee80211_he_6ghz_oper - HE 6 GHz operation Information field
 * @primary: primary channel
 * @control: control flags
 * @ccfs0: channel center frequency segment 0
 * @ccfs1: channel center frequency segment 1
 * @minrate: minimum rate (in 1 Mbps units)
 */
struct ieee80211_he_6ghz_oper {
	u8 primary;
#define IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH	0x3
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ	0
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ	1
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ	2
#define		IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ	3
#define IEEE80211_HE_6GHZ_OPER_CTRL_DUP_BEACON	0x4
#define IEEE80211_HE_6GHZ_OPER_CTRL_REG_INFO	0x78
	u8 control;
	u8 ccfs0;
	u8 ccfs1;
	u8 minrate;
} __packed;

/**
 * enum ieee80211_reg_conn_bits - represents Regulatory connectivity field bits.
 *
 * This enumeration defines bit flags used to represent regulatory connectivity
 * field bits.
 *
 * @IEEE80211_REG_CONN_LPI_VALID: Indicates whether the LPI bit is valid.
 * @IEEE80211_REG_CONN_LPI_VALUE: Represents the value of the LPI bit.
 * @IEEE80211_REG_CONN_SP_VALID: Indicates whether the SP bit is valid.
 * @IEEE80211_REG_CONN_SP_VALUE: Represents the value of the SP bit.
 */
enum ieee80211_reg_conn_bits {
	IEEE80211_REG_CONN_LPI_VALID = BIT(0),
	IEEE80211_REG_CONN_LPI_VALUE = BIT(1),
	IEEE80211_REG_CONN_SP_VALID = BIT(2),
	IEEE80211_REG_CONN_SP_VALUE = BIT(3),
};

/* transmit power interpretation type of transmit power envelope element */
enum ieee80211_tx_power_intrpt_type {
	IEEE80211_TPE_LOCAL_EIRP,
	IEEE80211_TPE_LOCAL_EIRP_PSD,
	IEEE80211_TPE_REG_CLIENT_EIRP,
	IEEE80211_TPE_REG_CLIENT_EIRP_PSD,
};

/* category type of transmit power envelope element */
enum ieee80211_tx_power_category_6ghz {
	IEEE80211_TPE_CAT_6GHZ_DEFAULT = 0,
	IEEE80211_TPE_CAT_6GHZ_SUBORDINATE = 1,
};

/*
 * For IEEE80211_TPE_LOCAL_EIRP / IEEE80211_TPE_REG_CLIENT_EIRP,
 * setting to 63.5 dBm means no constraint.
 */
#define IEEE80211_TPE_MAX_TX_PWR_NO_CONSTRAINT	127

/*
 * For IEEE80211_TPE_LOCAL_EIRP_PSD / IEEE80211_TPE_REG_CLIENT_EIRP_PSD,
 * setting to 127 indicates no PSD limit for the 20 MHz channel.
 */
#define IEEE80211_TPE_PSD_NO_LIMIT		127

/**
 * struct ieee80211_tx_pwr_env - Transmit Power Envelope
 * @info: Transmit Power Information field
 * @variable: Maximum Transmit Power field
 *
 * This structure represents the payload of the "Transmit Power
 * Envelope element" as described in IEEE Std 802.11ax-2021 section
 * 9.4.2.161
 */
struct ieee80211_tx_pwr_env {
	u8 info;
	u8 variable[];
} __packed;

#define IEEE80211_TX_PWR_ENV_INFO_COUNT 0x7
#define IEEE80211_TX_PWR_ENV_INFO_INTERPRET 0x38
#define IEEE80211_TX_PWR_ENV_INFO_CATEGORY 0xC0

#define IEEE80211_TX_PWR_ENV_EXT_COUNT	0xF

static inline bool ieee80211_valid_tpe_element(const u8 *data, u8 len)
{
	const struct ieee80211_tx_pwr_env *env = (const void *)data;
	u8 count, interpret, category;
	u8 needed = sizeof(*env);
	u8 N; /* also called N in the spec */

	if (len < needed)
		return false;

	count = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_COUNT);
	interpret = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_INTERPRET);
	category = u8_get_bits(env->info, IEEE80211_TX_PWR_ENV_INFO_CATEGORY);

	switch (category) {
	case IEEE80211_TPE_CAT_6GHZ_DEFAULT:
	case IEEE80211_TPE_CAT_6GHZ_SUBORDINATE:
		break;
	default:
		return false;
	}

	switch (interpret) {
	case IEEE80211_TPE_LOCAL_EIRP:
	case IEEE80211_TPE_REG_CLIENT_EIRP:
		if (count > 3)
			return false;

		/* count == 0 encodes 1 value for 20 MHz, etc. */
		needed += count + 1;

		if (len < needed)
			return false;

		/* there can be extension fields not accounted for in 'count' */

		return true;
	case IEEE80211_TPE_LOCAL_EIRP_PSD:
	case IEEE80211_TPE_REG_CLIENT_EIRP_PSD:
		if (count > 4)
			return false;

		N = count ? 1 << (count - 1) : 1;
		needed += N;

		if (len < needed)
			return false;

		if (len > needed) {
			u8 K = u8_get_bits(env->variable[N],
					   IEEE80211_TX_PWR_ENV_EXT_COUNT);

			needed += 1 + K;
			if (len < needed)
				return false;
		}

		return true;
	}

	return false;
}

/*
 * ieee80211_he_oper_size - calculate 802.11ax HE Operations IE size
 * @he_oper_ie: byte data of the He Operations IE, stating from the byte
 *	after the ext ID byte. It is assumed that he_oper_ie has at least
 *	sizeof(struct ieee80211_he_operation) bytes, the caller must have
 *	validated this.
 * @return the actual size of the IE data (not including header), or 0 on error
 */
static inline u8
ieee80211_he_oper_size(const u8 *he_oper_ie)
{
	const struct ieee80211_he_operation *he_oper = (const void *)he_oper_ie;
	u8 oper_len = sizeof(struct ieee80211_he_operation);
	u32 he_oper_params;

	/* Make sure the input is not NULL */
	if (!he_oper_ie)
		return 0;

	/* Calc required length */
	he_oper_params = le32_to_cpu(he_oper->he_oper_params);
	if (he_oper_params & IEEE80211_HE_OPERATION_VHT_OPER_INFO)
		oper_len += 3;
	if (he_oper_params & IEEE80211_HE_OPERATION_CO_HOSTED_BSS)
		oper_len++;
	if (he_oper_params & IEEE80211_HE_OPERATION_6GHZ_OP_INFO)
		oper_len += sizeof(struct ieee80211_he_6ghz_oper);

	/* Add the first byte (extension ID) to the total length */
	oper_len++;

	return oper_len;
}

/**
 * ieee80211_he_6ghz_oper - obtain 6 GHz operation field
 * @he_oper: HE operation element (must be pre-validated for size)
 *	but may be %NULL
 *
 * Return: a pointer to the 6 GHz operation field, or %NULL
 */
static inline const struct ieee80211_he_6ghz_oper *
ieee80211_he_6ghz_oper(const struct ieee80211_he_operation *he_oper)
{
	const u8 *ret;
	u32 he_oper_params;

	if (!he_oper)
		return NULL;

	ret = (const void *)&he_oper->optional;

	he_oper_params = le32_to_cpu(he_oper->he_oper_params);

	if (!(he_oper_params & IEEE80211_HE_OPERATION_6GHZ_OP_INFO))
		return NULL;
	if (he_oper_params & IEEE80211_HE_OPERATION_VHT_OPER_INFO)
		ret += 3;
	if (he_oper_params & IEEE80211_HE_OPERATION_CO_HOSTED_BSS)
		ret++;

	return (const void *)ret;
}

/* HE Spatial Reuse defines */
#define IEEE80211_HE_SPR_PSR_DISALLOWED				BIT(0)
#define IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED		BIT(1)
#define IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT			BIT(2)
#define IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT		BIT(3)
#define IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED		BIT(4)

/*
 * ieee80211_he_spr_size - calculate 802.11ax HE Spatial Reuse IE size
 * @he_spr_ie: byte data of the He Spatial Reuse IE, stating from the byte
 *	after the ext ID byte. It is assumed that he_spr_ie has at least
 *	sizeof(struct ieee80211_he_spr) bytes, the caller must have validated
 *	this
 * @return the actual size of the IE data (not including header), or 0 on error
 */
static inline u8
ieee80211_he_spr_size(const u8 *he_spr_ie)
{
	const struct ieee80211_he_spr *he_spr = (const void *)he_spr_ie;
	u8 spr_len = sizeof(struct ieee80211_he_spr);
	u8 he_spr_params;

	/* Make sure the input is not NULL */
	if (!he_spr_ie)
		return 0;

	/* Calc required length */
	he_spr_params = he_spr->he_sr_control;
	if (he_spr_params & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		spr_len++;
	if (he_spr_params & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		spr_len += 18;

	/* Add the first byte (extension ID) to the total length */
	spr_len++;

	return spr_len;
}

struct ieee80211_he_6ghz_capa {
	/* uses IEEE80211_HE_6GHZ_CAP_* below */
	__le16 capa;
} __packed;

/* HE 6 GHz band capabilities */
/* uses enum ieee80211_min_mpdu_spacing values */
#define IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START	0x0007
/* uses enum ieee80211_vht_max_ampdu_length_exp values */
#define IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP	0x0038
/* uses IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_* values */
#define IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN	0x00c0
/* WLAN_HT_CAP_SM_PS_* values */
#define IEEE80211_HE_6GHZ_CAP_SM_PS		0x0600
#define IEEE80211_HE_6GHZ_CAP_RD_RESPONDER	0x0800
#define IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS	0x1000
#define IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS	0x2000

#endif /* LINUX_IEEE80211_HE_H */
