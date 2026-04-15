/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 UHR definitions
 *
 * Copyright (c) 2025-2026 Intel Corporation
 */
#ifndef LINUX_IEEE80211_UHR_H
#define LINUX_IEEE80211_UHR_H

#include <linux/types.h>
#include <linux/if_ether.h>

#define IEEE80211_UHR_OPER_PARAMS_DPS_ENA		0x0001
#define IEEE80211_UHR_OPER_PARAMS_NPCA_ENA		0x0002
#define IEEE80211_UHR_OPER_PARAMS_PEDCA_ENA		0x0004
#define IEEE80211_UHR_OPER_PARAMS_DBE_ENA		0x0008

struct ieee80211_uhr_operation {
	__le16 params;
	u8 basic_mcs_nss_set[4];
	u8 variable[];
} __packed;

#define IEEE80211_UHR_NPCA_PARAMS_PRIMARY_CHAN_OFFS	0x0000000F
#define IEEE80211_UHR_NPCA_PARAMS_MIN_DUR_THRESH	0x000000F0
#define IEEE80211_UHR_NPCA_PARAMS_SWITCH_DELAY		0x00003F00
#define IEEE80211_UHR_NPCA_PARAMS_SWITCH_BACK_DELAY	0x000FC000
#define IEEE80211_UHR_NPCA_PARAMS_INIT_QSRC		0x00300000
#define IEEE80211_UHR_NPCA_PARAMS_MOPLEN		0x00400000
#define IEEE80211_UHR_NPCA_PARAMS_DIS_SUBCH_BMAP_PRES	0x00800000

/**
 * struct ieee80211_uhr_npca_info - npca operation information
 *
 * This structure is the "NPCA Operation Parameters field format" of "UHR
 * Operation Element" fields as described in P802.11bn_D1.3
 * subclause 9.4.2.353. See Figure 9-aa4.
 *
 * Refer to IEEE80211_UHR_NPCA*
 * @params:
 *	NPCA Primary Channel - NPCA primary channel
 *	NPCA_Min Duration Threshold - Minimum duration of inter-BSS activity
 *	NPCA Switching Delay -
 *		Time needed by an NPCA AP to switch from the
 *		BSS primary channel to the NPCA primary channel
 *		in the unit of 4 µs.
 *	NPCA Switching Back Delay -
 *		Time to switch from the NPCA primary channel
 *		to the BSS primary channel in the unit of 4 µs.
 *	NPCA Initial QSRC -
 *		Initialize the EDCAF QSRC[AC] variables
 *		when an NPCA STA in the BSS
 *		switches to NPCA operation.
 *	NPCA MOPLEN -
 *		Indicates which conditions can be used to
 *		initiate an NPCA operation,
 *		1 -> both PHYLEN NPCA operation and MOPLEN
 *		NPCA operation are
 *		permitted in the BSS
 *		0 -> only PHYLEN NPCA operation is allowed in the BSS.
 *	NPCA Disabled Subchannel Bitmap Present -
 *		Indicates whether the NPCA Disabled Subchannel
 *		Bitmap field is present. A 1 in this field indicates that
 *		the NPCA Disabled Subchannel Bitmap field is present
 * @dis_subch_bmap:
 *		A bit in the bitmap that lies within the BSS bandwidth is set
 *		to 1 to indicate that the corresponding 20 MHz subchannel is
 *		punctured and is set to 0 to indicate that the corresponding
 *		20 MHz subchannel is not punctured. A bit in the bitmap that
 *		falls outside of the BSS bandwidth is reserved. This field is
 *		present when the value of the NPCA Disabled Subchannel Bitmap
 *		Field Present field is equal to 1, and not present, otherwise
 */
struct ieee80211_uhr_npca_info {
	__le32 params;
	__le16 dis_subch_bmap[];
} __packed;

#define IEEE80211_UHR_DPS_PADDING_DELAY			0x0000003F
#define IEEE80211_UHR_DPS_TRANSITION_DELAY		0x00003F00
#define IEEE80211_UHR_DPS_ICF_REQUIRED			0x00010000
#define IEEE80211_UHR_DPS_PARAMETERIZED_FLAG		0x00020000
#define IEEE80211_UHR_DPS_LC_MODE_BW			0x001C0000
#define IEEE80211_UHR_DPS_LC_MODE_NSS			0x01E00000
#define IEEE80211_UHR_DPS_LC_MODE_MCS			0x1E000000
#define IEEE80211_UHR_DPS_MOBILE_AP_DPS_STATIC_HCM	0x20000000

/**
 * struct ieee80211_uhr_dps_info - DPS operation information
 *
 * This structure is the "DPS Operation Parameter field" of "UHR
 * Operation Element" fields as described in P802.11bn_D1.3
 * subclause 9.4.1.87. See Figure 9-207u.
 *
 * Refer to IEEE80211_UHR_DPS*
 * @params:
 *	DPS Padding Delay -
 *		Indicates the minimum MAC padding
 *		duration that is required by a DPS STA
 *		in an ICF to cause the STA to transition
 *		from the lower capability mode to the
 *		higher capability mode. The DPS Padding
 *		Delay field is in units of 4 µs.
 *	DPS Transition Delay -
 *		Indicates the amount of time required by a
 *		DPS STA to transition from the higher
 *		capability mode to the lower capability
 *		mode. The DPS Transition Delay field is in
 *		units of 4 µs.
 *	ICF Required -
 *		Indicates when the DPS assisting STA needs
 *		to transmit an ICF frame to the peer DPS STA
 *		before performing the frame exchanges with
 *		the peer DPS STA in a TXOP.
 *			1 -> indicates that the transmission of the
 *			ICF frame to the peer DPS STA prior to
 *			any frame exchange is needed.
 *			0 -> ICF transmission before the frame
 *			exchanges with the peer DPS STA is only
 *			needed if the frame exchange is performed
 *			in the HC mode.
 *	Parameterized Flag -
 *		0 -> indicates that only 20 MHz, 1 SS,
 *		non-HT PPDU format with the data
 *		rate of 6, 12, and 24 Mb/s as the
 *		default mode are supported by the
 *		DPS STA in the LC mode
 *		1 -> indicates that a bandwidth up to the
 *		bandwidth indicated in the LC Mode
 *		Bandwidth field, a number of spatial
 *		streams up to the NSS indicated in
 *		the LC Mode Nss field, and an MCS up
 *		to the MCS indicated in the LC Mode
 *		MCS fields are supported by the DPS
 *		STA in the LC mode as the
 *		parameterized mode.
 *	LC Mode Bandwidth -
 *		Indicates the maximum bandwidth supported
 *		by the STA in the LC mode.
 *	LC Mode NSS -
 *		Indicates the maximum number of the spatial
 *		streams supported by the STA in the LC mode.
 *	LC Mode MCS -
 *		Indicates the highest MCS supported by the STA
 *		in the LC mode.
 *	Mobile AP DPS Static HCM -
 *		1 -> indicates that it will remain in the DPS high
 *		capability mode until the next TBTT on that
 *		link.
 *		0 -> otherwise.
 */
struct ieee80211_uhr_dps_info {
	__le32 params;
} __packed;

#define IEEE80211_UHR_DBE_OPER_BANDWIDTH			0x07
#define IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES	0x08

/**
 * enum ieee80211_uhr_dbe_oper_bw - DBE Operational Bandwidth
 *
 * Encoding for the DBE Operational Bandwidth field in the UHR Operation
 * element (DBE Operation Parameters).
 *
 * @IEEE80211_UHR_DBE_OPER_BW_40: 40 MHz operational DBE bandwidth
 * @IEEE80211_UHR_DBE_OPER_BW_80: 80 MHz operational DBE bandwidth
 * @IEEE80211_UHR_DBE_OPER_BW_160: 160 MHz operational DBE bandwidth
 * @IEEE80211_UHR_DBE_OPER_BW_320_1: 320-1 MHz operational DBE bandwidth
 * @IEEE80211_UHR_DBE_OPER_BW_320_2: 320-2 MHz operational DBE bandwidth
 */
enum ieee80211_uhr_dbe_oper_bw {
	IEEE80211_UHR_DBE_OPER_BW_40 = 1,
	IEEE80211_UHR_DBE_OPER_BW_80 = 2,
	IEEE80211_UHR_DBE_OPER_BW_160 = 3,
	IEEE80211_UHR_DBE_OPER_BW_320_1 = 4,
	IEEE80211_UHR_DBE_OPER_BW_320_2 = 5,
};

/**
 * struct ieee80211_uhr_dbe_info - DBE operation information
 *
 * This structure is the "DBE Operation Parameters field" of
 * "UHR Operation Element" fields as described in P802.11bn_D1.3
 * subclause 9.4.2.353. See Figure 9-aa6.
 *
 * Refer to IEEE80211_UHR_DBE_OPER*
 * @params:
 *	B0-B2 - DBE Operational Bandwidth field, see
 *	"enum ieee80211_uhr_dbe_oper_bw" for values.
 *	Value 0 is reserved.
 *	Value 1 indicates 40 MHz operational DBE bandwidth.
 *	Value 2 indicates 80 MHz operational DBE bandwidth.
 *	Value 3 indicates 160 MHz operational DBE bandwidth.
 *	Value 4 indicates 320-1 MHz operational DBE bandwidth.
 *	Value 5 indicates 320-2 MHz operational DBE bandwidth.
 *	Values 6 to 7 are reserved.
 *	B3 - DBE Disabled Subchannel Bitmap Present.
 * @dis_subch_bmap: DBE Disabled Subchannel Bitmap field is set to indicate
 *	disabled 20 MHz subchannels within the DBE Bandwidth.
 */
struct ieee80211_uhr_dbe_info {
	u8 params;
	__le16 dis_subch_bmap[];
} __packed;

#define IEEE80211_UHR_P_EDCA_ECWMIN		0x0F
#define IEEE80211_UHR_P_EDCA_ECWMAX		0xF0
#define IEEE80211_UHR_P_EDCA_AIFSN		0x000F
#define IEEE80211_UHR_P_EDCA_CW_DS		0x0030
#define IEEE80211_UHR_P_EDCA_PSRC_THRESHOLD	0x01C0
#define IEEE80211_UHR_P_EDCA_QSRC_THRESHOLD	0x0600

/**
 * struct ieee80211_uhr_p_edca_info - P-EDCA operation information
 *
 * This structure is the "P-EDCA Operation Parameters field" of
 * "UHR Operation Element" fields as described in P802.11bn_D1.3
 * subclause 9.4.2.353. See Figure 9-aa5.
 *
 * Refer to IEEE80211_UHR_P_EDCA*
 * @p_edca_ec: P-EDCA ECWmin and ECWmax.
 *	These fields indicate the CWmin and CWmax values used by a
 *	P-EDCA STA during P-EDCA contention.
 * @params: AIFSN, CW DS, PSRC threshold, and QSRC threshold.
 *	- The AIFSN field indicates the AIFSN value used by a P-EDCA STA
 *	  during P-EDCA contention.
 *	- The CW DS field indicates the value used for randomization of the
 *	  transmission slot of the DS-CTS frame. The value 3 is reserved.
 *	  The value 0 indicates that randomization is not enabled.
 *	- The P-EDCA PSRC threshold field indicates the maximum number of
 *	  allowed consecutive DS-CTS transmissions. The value 0 and values
 *	  greater than 4 are reserved.
 *	- The P-EDCA QSRC threshold field indicates the value of the
 *	  QSRC[AC_VO] counter required to start P-EDCA contention. The
 *	  value 0 is reserved.
 */
struct ieee80211_uhr_p_edca_info {
	u8 p_edca_ec;
	__le16 params;
} __packed;

static inline bool ieee80211_uhr_oper_size_ok(const u8 *data, u8 len,
					      bool beacon)
{
	const struct ieee80211_uhr_operation *oper = (const void *)data;
	u8 needed = sizeof(*oper);

	if (len < needed)
		return false;

	/* nothing else present in beacons */
	if (beacon)
		return true;

	/* DPS Operation Parameters (fixed 4 bytes) */
	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_DPS_ENA)) {
		needed += sizeof(struct ieee80211_uhr_dps_info);
		if (len < needed)
			return false;
	}

	/* NPCA Operation Parameters (fixed 4 bytes + optional 2 bytes) */
	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_NPCA_ENA)) {
		const struct ieee80211_uhr_npca_info *npca =
			(const void *)(data + needed);

		needed += sizeof(*npca);
		if (len < needed)
			return false;

		if (npca->params &
		    cpu_to_le32(IEEE80211_UHR_NPCA_PARAMS_DIS_SUBCH_BMAP_PRES)) {
			needed += sizeof(npca->dis_subch_bmap[0]);
			if (len < needed)
				return false;
		}
	}

	/* P-EDCA Operation Parameters (fixed 3 bytes) */
	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_PEDCA_ENA)) {
		needed += sizeof(struct ieee80211_uhr_p_edca_info);
		if (len < needed)
			return false;
	}

	/* DBE Operation Parameters (fixed 1 byte + optional 2 bytes) */
	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_DBE_ENA)) {
		const struct ieee80211_uhr_dbe_info *dbe =
			(const void *)(data + needed);

		needed += sizeof(*dbe);
		if (len < needed)
			return false;

		if (dbe->params &
		    IEEE80211_UHR_DBE_OPER_DIS_SUBCHANNEL_BITMAP_PRES) {
			needed += sizeof(dbe->dis_subch_bmap[0]);
			if (len < needed)
				return false;
		}
	}

	return len >= needed;
}

/*
 * Note: cannot call this on the element coming from a beacon,
 * must ensure ieee80211_uhr_oper_size_ok(..., false) first
 */
static inline const struct ieee80211_uhr_npca_info *
ieee80211_uhr_npca_info(const struct ieee80211_uhr_operation *oper)
{
	const u8 *pos = oper->variable;

	if (!(oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_NPCA_ENA)))
		return NULL;

	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_DPS_ENA))
		pos += sizeof(struct ieee80211_uhr_dps_info);

	return (const void *)pos;
}

static inline const __le16 *
ieee80211_uhr_npca_dis_subch_bitmap(const struct ieee80211_uhr_operation *oper)
{
	const struct ieee80211_uhr_npca_info *npca;

	npca = ieee80211_uhr_npca_info(oper);
	if (!npca)
		return NULL;
	if (!(npca->params & cpu_to_le32(IEEE80211_UHR_NPCA_PARAMS_DIS_SUBCH_BMAP_PRES)))
		return NULL;
	return npca->dis_subch_bmap;
}

#define IEEE80211_UHR_MAC_CAP0_DPS_SUPP			0x01
#define IEEE80211_UHR_MAC_CAP0_DPS_ASSIST_SUPP		0x02
#define IEEE80211_UHR_MAC_CAP0_DPS_AP_STATIC_HCM_SUPP	0x04
#define IEEE80211_UHR_MAC_CAP0_NPCA_SUPP		0x10
#define IEEE80211_UHR_MAC_CAP0_ENH_BSR_SUPP		0x20
#define IEEE80211_UHR_MAC_CAP0_ADD_MAP_TID_SUPP		0x40
#define IEEE80211_UHR_MAC_CAP0_EOTSP_SUPP		0x80

#define IEEE80211_UHR_MAC_CAP1_DSO_SUPP			0x01
#define IEEE80211_UHR_MAC_CAP1_PEDCA_SUPP		0x02
#define IEEE80211_UHR_MAC_CAP1_DBE_SUPP			0x04
#define IEEE80211_UHR_MAC_CAP1_UL_LLI_SUPP		0x08
#define IEEE80211_UHR_MAC_CAP1_P2P_LLI_SUPP		0x10
#define IEEE80211_UHR_MAC_CAP1_PUO_SUPP			0x20
#define IEEE80211_UHR_MAC_CAP1_AP_PUO_SUPP		0x40
#define IEEE80211_UHR_MAC_CAP1_DUO_SUPP			0x80

#define IEEE80211_UHR_MAC_CAP2_OMC_UL_MU_DIS_RX_SUPP	0x01
#define IEEE80211_UHR_MAC_CAP2_AOM_SUPP			0x02
#define IEEE80211_UHR_MAC_CAP2_IFCS_LOC_SUPP		0x04
#define IEEE80211_UHR_MAC_CAP2_UHR_TRS_SUPP		0x08
#define IEEE80211_UHR_MAC_CAP2_TXSPG_SUPP		0x10
#define IEEE80211_UHR_MAC_CAP2_TXOP_RET_IN_TXSPG	0x20
#define IEEE80211_UHR_MAC_CAP2_UHR_OM_PU_TO_LOW		0xC0

#define IEEE80211_UHR_MAC_CAP3_UHR_OM_PU_TO_HIGH	0x03
#define IEEE80211_UHR_MAC_CAP3_PARAM_UPD_ADV_NOTIF_INTV	0x1C
#define IEEE80211_UHR_MAC_CAP3_UPD_IND_TIM_INTV_LOW	0xE0

#define IEEE80211_UHR_MAC_CAP4_UPD_IND_TIM_INTV_HIGH	0x03
#define IEEE80211_UHR_MAC_CAP4_BOUNDED_ESS		0x04
#define IEEE80211_UHR_MAC_CAP4_BTM_ASSURANCE		0x08
#define IEEE80211_UHR_MAC_CAP4_CO_BF_SUPP		0x10

#define IEEE80211_UHR_MAC_CAP_DBE_MAX_BW		0x07
#define IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_160_PRES	0x08
#define IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_320_PRES	0x10

/**
 * enum ieee80211_uhr_dbe_max_supported_bw - DBE Maximum Supported Bandwidth
 *
 * As per spec P802.11bn_D1.3 "Table 9-bb5—Encoding of the DBE Maximum
 * Supported Bandwidth field".
 *
 * @IEEE80211_UHR_DBE_MAX_BW_40: Indicates 40 MHz DBE max supported bw
 * @IEEE80211_UHR_DBE_MAX_BW_80: Indicates 80 MHz DBE max supported bw
 * @IEEE80211_UHR_DBE_MAX_BW_160: Indicates 160 MHz DBE max supported bw
 * @IEEE80211_UHR_DBE_MAX_BW_320: Indicates 320 MHz DBE max supported bw
 */
enum ieee80211_uhr_dbe_max_supported_bw {
	IEEE80211_UHR_DBE_MAX_BW_40 = 1,
	IEEE80211_UHR_DBE_MAX_BW_80 = 2,
	IEEE80211_UHR_DBE_MAX_BW_160 = 3,
	IEEE80211_UHR_DBE_MAX_BW_320 = 4,
};

struct ieee80211_uhr_cap_mac {
	u8 mac_cap[5];
} __packed;

struct ieee80211_uhr_cap {
	struct ieee80211_uhr_cap_mac mac;
	/* DBE, PHY capabilities */
	u8 variable[];
} __packed;

#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_SND_NDP_LE80	0x01
#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_DL_MU_LE80	0x02
#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_SND_NDP_160	0x04
#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_DL_MU_160	0x08
#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_SND_NDP_320	0x10
#define IEEE80211_UHR_PHY_CAP_MAX_NSS_RX_DL_MU_320	0x20
#define IEEE80211_UHR_PHY_CAP_ELR_RX			0x40
#define IEEE80211_UHR_PHY_CAP_ELR_TX			0x80

struct ieee80211_uhr_cap_phy {
	u8 cap;
} __packed;

static inline bool ieee80211_uhr_capa_size_ok(const u8 *data, u8 len,
					      bool from_ap)
{
	const struct ieee80211_uhr_cap *cap = (const void *)data;
	size_t needed = sizeof(*cap) + sizeof(struct ieee80211_uhr_cap_phy);

	if (len < needed)
		return false;

	/*
	 * A non-AP STA does not include the DBE Capability Parameters field
	 * in the UHR MAC Capabilities Information field.
	 */
	if (from_ap && cap->mac.mac_cap[1] & IEEE80211_UHR_MAC_CAP1_DBE_SUPP) {
		u8 dbe;

		needed += 1;
		if (len < needed)
			return false;

		dbe = cap->variable[0];

		if (dbe & IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_160_PRES)
			needed += 3;

		if (dbe & IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_320_PRES)
			needed += 3;
	}

	return len >= needed;
}

static inline const struct ieee80211_uhr_cap_phy *
ieee80211_uhr_phy_cap(const struct ieee80211_uhr_cap *cap, bool from_ap)
{
	u8 offs = 0;

	if (from_ap && cap->mac.mac_cap[1] & IEEE80211_UHR_MAC_CAP1_DBE_SUPP) {
		u8 dbe = cap->variable[0];

		offs += 1;

		if (dbe & IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_160_PRES)
			offs += 3;

		if (dbe & IEEE80211_UHR_MAC_CAP_DBE_EHT_MCS_MAP_320_PRES)
			offs += 3;
	}

	return (const void *)&cap->variable[offs];
}

#define IEEE80211_SMD_INFO_CAPA_DL_DATA_FWD		0x01
#define IEEE80211_SMD_INFO_CAPA_MAX_NUM_PREP		0x0E
#define IEEE80211_SMD_INFO_CAPA_TYPE			0x10
#define IEEE80211_SMD_INFO_CAPA_PTK_PER_AP_MLD		0x20

struct ieee80211_smd_info {
	u8 id[ETH_ALEN];
	u8 capa;
	__le16 timeout;
} __packed;

#endif /* LINUX_IEEE80211_UHR_H */
