// SPDX-License-Identifier: GPL-2.0-only
/*
 * VHT handling
 *
 * Portions of this file
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
 * Copyright (C) 2018 - 2021 Intel Corporation
 */

#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "rate.h"


static void __check_vhtcap_disable(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_sta_vht_cap *vht_cap,
				   u32 flag)
{
	__le32 le_flag = cpu_to_le32(flag);

	if (sdata->u.mgd.vht_capa_mask.vht_cap_info & le_flag &&
	    !(sdata->u.mgd.vht_capa.vht_cap_info & le_flag))
		vht_cap->cap &= ~flag;
}

void ieee80211_apply_vhtcap_overrides(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_sta_vht_cap *vht_cap)
{
	int i;
	u16 rxmcs_mask, rxmcs_cap, rxmcs_n, txmcs_mask, txmcs_cap, txmcs_n;

	if (!vht_cap->vht_supported)
		return;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return;

	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_RXLDPC);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_SHORT_GI_80);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_SHORT_GI_160);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_TXSTBC);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN);
	__check_vhtcap_disable(sdata, vht_cap,
			       IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN);

	/* Allow user to decrease AMPDU length exponent */
	if (sdata->u.mgd.vht_capa_mask.vht_cap_info &
	    cpu_to_le32(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK)) {
		u32 cap, n;

		n = le32_to_cpu(sdata->u.mgd.vht_capa.vht_cap_info) &
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
		n >>= IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;
		cap = vht_cap->cap & IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
		cap >>= IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

		if (n < cap) {
			vht_cap->cap &=
				~IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
			vht_cap->cap |=
				n << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;
		}
	}

	/* Allow the user to decrease MCSes */
	rxmcs_mask =
		le16_to_cpu(sdata->u.mgd.vht_capa_mask.supp_mcs.rx_mcs_map);
	rxmcs_n = le16_to_cpu(sdata->u.mgd.vht_capa.supp_mcs.rx_mcs_map);
	rxmcs_n &= rxmcs_mask;
	rxmcs_cap = le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map);

	txmcs_mask =
		le16_to_cpu(sdata->u.mgd.vht_capa_mask.supp_mcs.tx_mcs_map);
	txmcs_n = le16_to_cpu(sdata->u.mgd.vht_capa.supp_mcs.tx_mcs_map);
	txmcs_n &= txmcs_mask;
	txmcs_cap = le16_to_cpu(vht_cap->vht_mcs.tx_mcs_map);
	for (i = 0; i < 8; i++) {
		u8 m, n, c;

		m = (rxmcs_mask >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;
		n = (rxmcs_n >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;
		c = (rxmcs_cap >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		if (m && ((c != IEEE80211_VHT_MCS_NOT_SUPPORTED && n < c) ||
			  n == IEEE80211_VHT_MCS_NOT_SUPPORTED)) {
			rxmcs_cap &= ~(3 << 2*i);
			rxmcs_cap |= (rxmcs_n & (3 << 2*i));
		}

		m = (txmcs_mask >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;
		n = (txmcs_n >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;
		c = (txmcs_cap >> 2*i) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		if (m && ((c != IEEE80211_VHT_MCS_NOT_SUPPORTED && n < c) ||
			  n == IEEE80211_VHT_MCS_NOT_SUPPORTED)) {
			txmcs_cap &= ~(3 << 2*i);
			txmcs_cap |= (txmcs_n & (3 << 2*i));
		}
	}
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(rxmcs_cap);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(txmcs_cap);
}

void
ieee80211_vht_cap_ie_to_sta_vht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_vht_cap *vht_cap_ie,
				    struct sta_info *sta)
{
	struct ieee80211_sta_vht_cap *vht_cap = &sta->sta.vht_cap;
	struct ieee80211_sta_vht_cap own_cap;
	u32 cap_info, i;
	bool have_80mhz;

	memset(vht_cap, 0, sizeof(*vht_cap));

	if (!sta->sta.ht_cap.ht_supported)
		return;

	if (!vht_cap_ie || !sband->vht_cap.vht_supported)
		return;

	/* Allow VHT if at least one channel on the sband supports 80 MHz */
	have_80mhz = false;
	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED |
						IEEE80211_CHAN_NO_80MHZ))
			continue;

		have_80mhz = true;
		break;
	}

	if (!have_80mhz)
		return;

	/*
	 * A VHT STA must support 40 MHz, but if we verify that here
	 * then we break a few things - some APs (e.g. Netgear R6300v2
	 * and others based on the BCM4360 chipset) will unset this
	 * capability bit when operating in 20 MHz.
	 */

	vht_cap->vht_supported = true;

	own_cap = sband->vht_cap;
	/*
	 * If user has specified capability overrides, take care
	 * of that if the station we're setting up is the AP that
	 * we advertised a restricted capability set to. Override
	 * our own capabilities and then use those below.
	 */
	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !test_sta_flag(sta, WLAN_STA_TDLS_PEER))
		ieee80211_apply_vhtcap_overrides(sdata, &own_cap);

	/* take some capabilities as-is */
	cap_info = le32_to_cpu(vht_cap_ie->vht_cap_info);
	vht_cap->cap = cap_info;
	vht_cap->cap &= IEEE80211_VHT_CAP_RXLDPC |
			IEEE80211_VHT_CAP_VHT_TXOP_PS |
			IEEE80211_VHT_CAP_HTC_VHT |
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
			IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB |
			IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB |
			IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN |
			IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;

	vht_cap->cap |= min_t(u32, cap_info & IEEE80211_VHT_CAP_MAX_MPDU_MASK,
			      own_cap.cap & IEEE80211_VHT_CAP_MAX_MPDU_MASK);

	/* and some based on our own capabilities */
	switch (own_cap.cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
		vht_cap->cap |= cap_info &
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		break;
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
		vht_cap->cap |= cap_info &
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
		break;
	default:
		/* nothing */
		break;
	}

	/* symmetric capabilities */
	vht_cap->cap |= cap_info & own_cap.cap &
			(IEEE80211_VHT_CAP_SHORT_GI_80 |
			 IEEE80211_VHT_CAP_SHORT_GI_160);

	/* remaining ones */
	if (own_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)
		vht_cap->cap |= cap_info &
				(IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
				 IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK);

	if (own_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)
		vht_cap->cap |= cap_info &
				(IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
				 IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK);

	if (own_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)
		vht_cap->cap |= cap_info &
				IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	if (own_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)
		vht_cap->cap |= cap_info &
				IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

	if (own_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
		vht_cap->cap |= cap_info & IEEE80211_VHT_CAP_RXSTBC_MASK;

	if (own_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK)
		vht_cap->cap |= cap_info & IEEE80211_VHT_CAP_TXSTBC;

	/* Copy peer MCS info, the driver might need them. */
	memcpy(&vht_cap->vht_mcs, &vht_cap_ie->supp_mcs,
	       sizeof(struct ieee80211_vht_mcs_info));

	/* copy EXT_NSS_BW Support value or remove the capability */
	if (ieee80211_hw_check(&sdata->local->hw, SUPPORTS_VHT_EXT_NSS_BW))
		vht_cap->cap |= (cap_info & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK);
	else
		vht_cap->vht_mcs.tx_highest &=
			~cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);

	/* but also restrict MCSes */
	for (i = 0; i < 8; i++) {
		u16 own_rx, own_tx, peer_rx, peer_tx;

		own_rx = le16_to_cpu(own_cap.vht_mcs.rx_mcs_map);
		own_rx = (own_rx >> i * 2) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		own_tx = le16_to_cpu(own_cap.vht_mcs.tx_mcs_map);
		own_tx = (own_tx >> i * 2) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		peer_rx = le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map);
		peer_rx = (peer_rx >> i * 2) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		peer_tx = le16_to_cpu(vht_cap->vht_mcs.tx_mcs_map);
		peer_tx = (peer_tx >> i * 2) & IEEE80211_VHT_MCS_NOT_SUPPORTED;

		if (peer_tx != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
			if (own_rx == IEEE80211_VHT_MCS_NOT_SUPPORTED)
				peer_tx = IEEE80211_VHT_MCS_NOT_SUPPORTED;
			else if (own_rx < peer_tx)
				peer_tx = own_rx;
		}

		if (peer_rx != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
			if (own_tx == IEEE80211_VHT_MCS_NOT_SUPPORTED)
				peer_rx = IEEE80211_VHT_MCS_NOT_SUPPORTED;
			else if (own_tx < peer_rx)
				peer_rx = own_tx;
		}

		vht_cap->vht_mcs.rx_mcs_map &=
			~cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << i * 2);
		vht_cap->vht_mcs.rx_mcs_map |= cpu_to_le16(peer_rx << i * 2);

		vht_cap->vht_mcs.tx_mcs_map &=
			~cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << i * 2);
		vht_cap->vht_mcs.tx_mcs_map |= cpu_to_le16(peer_tx << i * 2);
	}

	/*
	 * This is a workaround for VHT-enabled STAs which break the spec
	 * and have the VHT-MCS Rx map filled in with value 3 for all eight
	 * spacial streams, an example is AR9462.
	 *
	 * As per spec, in section 22.1.1 Introduction to the VHT PHY
	 * A VHT STA shall support at least single spactial stream VHT-MCSs
	 * 0 to 7 (transmit and receive) in all supported channel widths.
	 */
	if (vht_cap->vht_mcs.rx_mcs_map == cpu_to_le16(0xFFFF)) {
		vht_cap->vht_supported = false;
		sdata_info(sdata, "Ignoring VHT IE from %pM due to invalid rx_mcs_map\n",
			   sta->addr);
		return;
	}

	/* finally set up the bandwidth */
	switch (vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
	case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
		break;
	default:
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_80;

		if (!(vht_cap->vht_mcs.tx_highest &
				cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE)))
			break;

		/*
		 * If this is non-zero, then it does support 160 MHz after all,
		 * in one form or the other. We don't distinguish here (or even
		 * above) between 160 and 80+80 yet.
		 */
		if (cap_info & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK)
			sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
	}

	sta->sta.bandwidth = ieee80211_sta_cur_vht_bw(sta);

	switch (vht_cap->cap & IEEE80211_VHT_CAP_MAX_MPDU_MASK) {
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454:
		sta->sta.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_11454;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
		sta->sta.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_7991;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895:
	default:
		sta->sta.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_3895;
		break;
	}
}

/* FIXME: move this to some better location - parses HE/EHT now */
enum ieee80211_sta_rx_bandwidth ieee80211_sta_cap_rx_bw(struct sta_info *sta)
{
	struct ieee80211_sta_vht_cap *vht_cap = &sta->sta.vht_cap;
	struct ieee80211_sta_he_cap *he_cap = &sta->sta.he_cap;
	struct ieee80211_sta_eht_cap *eht_cap = &sta->sta.eht_cap;
	u32 cap_width;

	if (he_cap->has_he) {
		u8 info;

		if (eht_cap->has_eht &&
		    sta->sdata->vif.bss_conf.chandef.chan->band ==
		    NL80211_BAND_6GHZ) {
			info = eht_cap->eht_cap_elem.phy_cap_info[0];

			if (info & IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ)
				return IEEE80211_STA_RX_BW_320;
		}

		info = he_cap->he_cap_elem.phy_cap_info[0];

		if (sta->sdata->vif.bss_conf.chandef.chan->band ==
				NL80211_BAND_2GHZ) {
			if (info & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G)
				return IEEE80211_STA_RX_BW_40;
			else
				return IEEE80211_STA_RX_BW_20;
		}

		if (info & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G ||
		    info & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			return IEEE80211_STA_RX_BW_160;
		else if (info & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G)
			return IEEE80211_STA_RX_BW_80;

		return IEEE80211_STA_RX_BW_20;
	}

	if (!vht_cap->vht_supported)
		return sta->sta.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 ?
				IEEE80211_STA_RX_BW_40 :
				IEEE80211_STA_RX_BW_20;

	cap_width = vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;

	if (cap_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ ||
	    cap_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ)
		return IEEE80211_STA_RX_BW_160;

	/*
	 * If this is non-zero, then it does support 160 MHz after all,
	 * in one form or the other. We don't distinguish here (or even
	 * above) between 160 and 80+80 yet.
	 */
	if (vht_cap->cap & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK)
		return IEEE80211_STA_RX_BW_160;

	return IEEE80211_STA_RX_BW_80;
}

enum nl80211_chan_width ieee80211_sta_cap_chan_bw(struct sta_info *sta)
{
	struct ieee80211_sta_vht_cap *vht_cap = &sta->sta.vht_cap;
	u32 cap_width;

	if (!vht_cap->vht_supported) {
		if (!sta->sta.ht_cap.ht_supported)
			return NL80211_CHAN_WIDTH_20_NOHT;

		return sta->sta.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 ?
				NL80211_CHAN_WIDTH_40 : NL80211_CHAN_WIDTH_20;
	}

	cap_width = vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;

	if (cap_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ)
		return NL80211_CHAN_WIDTH_160;
	else if (cap_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ)
		return NL80211_CHAN_WIDTH_80P80;

	return NL80211_CHAN_WIDTH_80;
}

enum nl80211_chan_width
ieee80211_sta_rx_bw_to_chan_width(struct sta_info *sta)
{
	enum ieee80211_sta_rx_bandwidth cur_bw = sta->sta.bandwidth;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->sta.vht_cap;
	u32 cap_width;

	switch (cur_bw) {
	case IEEE80211_STA_RX_BW_20:
		if (!sta->sta.ht_cap.ht_supported)
			return NL80211_CHAN_WIDTH_20_NOHT;
		else
			return NL80211_CHAN_WIDTH_20;
	case IEEE80211_STA_RX_BW_40:
		return NL80211_CHAN_WIDTH_40;
	case IEEE80211_STA_RX_BW_80:
		return NL80211_CHAN_WIDTH_80;
	case IEEE80211_STA_RX_BW_160:
		cap_width =
			vht_cap->cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;

		if (cap_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ)
			return NL80211_CHAN_WIDTH_160;

		return NL80211_CHAN_WIDTH_80P80;
	default:
		return NL80211_CHAN_WIDTH_20;
	}
}

enum ieee80211_sta_rx_bandwidth
ieee80211_chan_width_to_rx_bw(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return IEEE80211_STA_RX_BW_20;
	case NL80211_CHAN_WIDTH_40:
		return IEEE80211_STA_RX_BW_40;
	case NL80211_CHAN_WIDTH_80:
		return IEEE80211_STA_RX_BW_80;
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_80P80:
		return IEEE80211_STA_RX_BW_160;
	case NL80211_CHAN_WIDTH_320:
		return IEEE80211_STA_RX_BW_320;
	default:
		WARN_ON_ONCE(1);
		return IEEE80211_STA_RX_BW_20;
	}
}

/* FIXME: rename/move - this deals with everything not just VHT */
enum ieee80211_sta_rx_bandwidth ieee80211_sta_cur_vht_bw(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	enum ieee80211_sta_rx_bandwidth bw;
	enum nl80211_chan_width bss_width = sdata->vif.bss_conf.chandef.width;

	bw = ieee80211_sta_cap_rx_bw(sta);
	bw = min(bw, sta->cur_max_bandwidth);

	/* Don't consider AP's bandwidth for TDLS peers, section 11.23.1 of
	 * IEEE80211-2016 specification makes higher bandwidth operation
	 * possible on the TDLS link if the peers have wider bandwidth
	 * capability.
	 *
	 * However, in this case, and only if the TDLS peer is authorized,
	 * limit to the tdls_chandef so that the configuration here isn't
	 * wider than what's actually requested on the channel context.
	 */
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    test_sta_flag(sta, WLAN_STA_TDLS_WIDER_BW) &&
	    test_sta_flag(sta, WLAN_STA_AUTHORIZED) &&
	    sta->tdls_chandef.chan)
		bw = min(bw, ieee80211_chan_width_to_rx_bw(sta->tdls_chandef.width));
	else
		bw = min(bw, ieee80211_chan_width_to_rx_bw(bss_width));

	return bw;
}

void ieee80211_sta_set_rx_nss(struct sta_info *sta)
{
	u8 ht_rx_nss = 0, vht_rx_nss = 0, he_rx_nss = 0, eht_rx_nss = 0, rx_nss;
	bool support_160;

	/* if we received a notification already don't overwrite it */
	if (sta->sta.rx_nss)
		return;

	if (sta->sta.eht_cap.has_eht) {
		int i;
		const u8 *rx_nss_mcs = (void *)&sta->sta.eht_cap.eht_mcs_nss_supp;

		/* get the max nss for EHT over all possible bandwidths and mcs */
		for (i = 0; i < sizeof(struct ieee80211_eht_mcs_nss_supp); i++)
			eht_rx_nss = max_t(u8, eht_rx_nss,
					   u8_get_bits(rx_nss_mcs[i],
						       IEEE80211_EHT_MCS_NSS_RX));
	}

	if (sta->sta.he_cap.has_he) {
		int i;
		u8 rx_mcs_80 = 0, rx_mcs_160 = 0;
		const struct ieee80211_sta_he_cap *he_cap = &sta->sta.he_cap;
		u16 mcs_160_map =
			le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
		u16 mcs_80_map = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);

		for (i = 7; i >= 0; i--) {
			u8 mcs_160 = (mcs_160_map >> (2 * i)) & 3;

			if (mcs_160 != IEEE80211_HE_MCS_NOT_SUPPORTED) {
				rx_mcs_160 = i + 1;
				break;
			}
		}
		for (i = 7; i >= 0; i--) {
			u8 mcs_80 = (mcs_80_map >> (2 * i)) & 3;

			if (mcs_80 != IEEE80211_HE_MCS_NOT_SUPPORTED) {
				rx_mcs_80 = i + 1;
				break;
			}
		}

		support_160 = he_cap->he_cap_elem.phy_cap_info[0] &
			      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

		if (support_160)
			he_rx_nss = min(rx_mcs_80, rx_mcs_160);
		else
			he_rx_nss = rx_mcs_80;
	}

	if (sta->sta.ht_cap.ht_supported) {
		if (sta->sta.ht_cap.mcs.rx_mask[0])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[1])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[2])
			ht_rx_nss++;
		if (sta->sta.ht_cap.mcs.rx_mask[3])
			ht_rx_nss++;
		/* FIXME: consider rx_highest? */
	}

	if (sta->sta.vht_cap.vht_supported) {
		int i;
		u16 rx_mcs_map;

		rx_mcs_map = le16_to_cpu(sta->sta.vht_cap.vht_mcs.rx_mcs_map);

		for (i = 7; i >= 0; i--) {
			u8 mcs = (rx_mcs_map >> (2 * i)) & 3;

			if (mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
				vht_rx_nss = i + 1;
				break;
			}
		}
		/* FIXME: consider rx_highest? */
	}

	rx_nss = max(vht_rx_nss, ht_rx_nss);
	rx_nss = max(he_rx_nss, rx_nss);
	rx_nss = max(eht_rx_nss, rx_nss);
	sta->sta.rx_nss = max_t(u8, 1, rx_nss);
}

u32 __ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				  struct sta_info *sta, u8 opmode,
				  enum nl80211_band band)
{
	enum ieee80211_sta_rx_bandwidth new_bw;
	struct sta_opmode_info sta_opmode = {};
	u32 changed = 0;
	u8 nss;

	/* ignore - no support for BF yet */
	if (opmode & IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF)
		return 0;

	nss = opmode & IEEE80211_OPMODE_NOTIF_RX_NSS_MASK;
	nss >>= IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
	nss += 1;

	if (sta->sta.rx_nss != nss) {
		sta->sta.rx_nss = nss;
		sta_opmode.rx_nss = nss;
		changed |= IEEE80211_RC_NSS_CHANGED;
		sta_opmode.changed |= STA_OPMODE_N_SS_CHANGED;
	}

	switch (opmode & IEEE80211_OPMODE_NOTIF_CHANWIDTH_MASK) {
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ:
		/* ignore IEEE80211_OPMODE_NOTIF_BW_160_80P80 must not be set */
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_20;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ:
		/* ignore IEEE80211_OPMODE_NOTIF_BW_160_80P80 must not be set */
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_40;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ:
		if (opmode & IEEE80211_OPMODE_NOTIF_BW_160_80P80)
			sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
		else
			sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_80;
		break;
	case IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ:
		/* legacy only, no longer used by newer spec */
		sta->cur_max_bandwidth = IEEE80211_STA_RX_BW_160;
		break;
	}

	new_bw = ieee80211_sta_cur_vht_bw(sta);
	if (new_bw != sta->sta.bandwidth) {
		sta->sta.bandwidth = new_bw;
		sta_opmode.bw = ieee80211_sta_rx_bw_to_chan_width(sta);
		changed |= IEEE80211_RC_BW_CHANGED;
		sta_opmode.changed |= STA_OPMODE_MAX_BW_CHANGED;
	}

	if (sta_opmode.changed)
		cfg80211_sta_opmode_change_notify(sdata->dev, sta->addr,
						  &sta_opmode, GFP_KERNEL);

	return changed;
}

void ieee80211_process_mu_groups(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_mgmt *mgmt)
{
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;

	if (!sdata->vif.mu_mimo_owner)
		return;

	if (!memcmp(mgmt->u.action.u.vht_group_notif.position,
		    bss_conf->mu_group.position, WLAN_USER_POSITION_LEN) &&
	    !memcmp(mgmt->u.action.u.vht_group_notif.membership,
		    bss_conf->mu_group.membership, WLAN_MEMBERSHIP_LEN))
		return;

	memcpy(bss_conf->mu_group.membership,
	       mgmt->u.action.u.vht_group_notif.membership,
	       WLAN_MEMBERSHIP_LEN);
	memcpy(bss_conf->mu_group.position,
	       mgmt->u.action.u.vht_group_notif.position,
	       WLAN_USER_POSITION_LEN);

	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_MU_GROUPS);
}

void ieee80211_update_mu_groups(struct ieee80211_vif *vif,
				const u8 *membership, const u8 *position)
{
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	if (WARN_ON_ONCE(!vif->mu_mimo_owner))
		return;

	memcpy(bss_conf->mu_group.membership, membership, WLAN_MEMBERSHIP_LEN);
	memcpy(bss_conf->mu_group.position, position, WLAN_USER_POSITION_LEN);
}
EXPORT_SYMBOL_GPL(ieee80211_update_mu_groups);

void ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta, u8 opmode,
				 enum nl80211_band band)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband = local->hw.wiphy->bands[band];

	u32 changed = __ieee80211_vht_handle_opmode(sdata, sta, opmode, band);

	if (changed > 0) {
		ieee80211_recalc_min_chandef(sdata);
		rate_control_rate_update(local, sband, sta, changed);
	}
}

void ieee80211_get_vht_mask_from_cap(__le16 vht_cap,
				     u16 vht_mask[NL80211_VHT_NSS_MAX])
{
	int i;
	u16 mask, cap = le16_to_cpu(vht_cap);

	for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
		mask = (cap >> i * 2) & IEEE80211_VHT_MCS_NOT_SUPPORTED;
		switch (mask) {
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			vht_mask[i] = 0x00FF;
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			vht_mask[i] = 0x01FF;
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			vht_mask[i] = 0x03FF;
			break;
		case IEEE80211_VHT_MCS_NOT_SUPPORTED:
		default:
			vht_mask[i] = 0;
			break;
		}
	}
}
