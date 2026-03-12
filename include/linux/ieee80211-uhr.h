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
#define IEEE80211_UHR_OPER_PARAMS_DBE_ENA		0x0004
#define IEEE80211_UHR_OPER_PARAMS_PEDCA_ENA		0x0008

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

struct ieee80211_uhr_npca_info {
	__le32 params;
	__le16 dis_subch_bmap[];
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

	/* FIXME: DPS, DBE, P-EDCA (consider order, also relative to NPCA) */

	if (oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_NPCA_ENA)) {
		const struct ieee80211_uhr_npca_info *npca =
			(const void *)oper->variable;

		needed += sizeof(*npca);

		if (len < needed)
			return false;

		if (npca->params & cpu_to_le32(IEEE80211_UHR_NPCA_PARAMS_DIS_SUBCH_BMAP_PRES))
			needed += sizeof(npca->dis_subch_bmap[0]);
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
	if (!(oper->params & cpu_to_le16(IEEE80211_UHR_OPER_PARAMS_NPCA_ENA)))
		return NULL;

	/* FIXME: DPS */

	return (const void *)oper->variable;
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
