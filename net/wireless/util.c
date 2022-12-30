// SPDX-License-Identifier: GPL-2.0
/*
 * Wireless utility functions
 *
 * Copyright 2007-2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2022 Intel Corporation
 */
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <net/dsfield.h>
#include <linux/if_vlan.h>
#include <linux/mpls.h>
#include <linux/gcd.h>
#include <linux/bitfield.h>
#include <linux/nospec.h>
#include "core.h"
#include "rdev-ops.h"


const struct ieee80211_rate *
ieee80211_get_response_rate(struct ieee80211_supported_band *sband,
			    u32 basic_rates, int bitrate)
{
	struct ieee80211_rate *result = &sband->bitrates[0];
	int i;

	for (i = 0; i < sband->n_bitrates; i++) {
		if (!(basic_rates & BIT(i)))
			continue;
		if (sband->bitrates[i].bitrate > bitrate)
			continue;
		result = &sband->bitrates[i];
	}

	return result;
}
EXPORT_SYMBOL(ieee80211_get_response_rate);

u32 ieee80211_mandatory_rates(struct ieee80211_supported_band *sband,
			      enum nl80211_bss_scan_width scan_width)
{
	struct ieee80211_rate *bitrates;
	u32 mandatory_rates = 0;
	enum ieee80211_rate_flags mandatory_flag;
	int i;

	if (WARN_ON(!sband))
		return 1;

	if (sband->band == NL80211_BAND_2GHZ) {
		if (scan_width == NL80211_BSS_CHAN_WIDTH_5 ||
		    scan_width == NL80211_BSS_CHAN_WIDTH_10)
			mandatory_flag = IEEE80211_RATE_MANDATORY_G;
		else
			mandatory_flag = IEEE80211_RATE_MANDATORY_B;
	} else {
		mandatory_flag = IEEE80211_RATE_MANDATORY_A;
	}

	bitrates = sband->bitrates;
	for (i = 0; i < sband->n_bitrates; i++)
		if (bitrates[i].flags & mandatory_flag)
			mandatory_rates |= BIT(i);
	return mandatory_rates;
}
EXPORT_SYMBOL(ieee80211_mandatory_rates);

u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	 * there are overlapping channel numbers in 5GHz and 2GHz bands */
	if (chan <= 0)
		return 0; /* not supported */
	switch (band) {
	case NL80211_BAND_2GHZ:
	case NL80211_BAND_LC:
		if (chan == 14)
			return MHZ_TO_KHZ(2484);
		else if (chan < 14)
			return MHZ_TO_KHZ(2407 + chan * 5);
		break;
	case NL80211_BAND_5GHZ:
		if (chan >= 182 && chan <= 196)
			return MHZ_TO_KHZ(4000 + chan * 5);
		else
			return MHZ_TO_KHZ(5000 + chan * 5);
		break;
	case NL80211_BAND_6GHZ:
		/* see 802.11ax D6.1 27.3.23.2 */
		if (chan == 2)
			return MHZ_TO_KHZ(5935);
		if (chan <= 233)
			return MHZ_TO_KHZ(5950 + chan * 5);
		break;
	case NL80211_BAND_60GHZ:
		if (chan < 7)
			return MHZ_TO_KHZ(56160 + chan * 2160);
		break;
	case NL80211_BAND_S1GHZ:
		return 902000 + chan * 500;
	default:
		;
	}
	return 0; /* not supported */
}
EXPORT_SYMBOL(ieee80211_channel_to_freq_khz);

enum nl80211_chan_width
ieee80211_s1g_channel_width(const struct ieee80211_channel *chan)
{
	if (WARN_ON(!chan || chan->band != NL80211_BAND_S1GHZ))
		return NL80211_CHAN_WIDTH_20_NOHT;

	/*S1G defines a single allowed channel width per channel.
	 * Extract that width here.
	 */
	if (chan->flags & IEEE80211_CHAN_1MHZ)
		return NL80211_CHAN_WIDTH_1;
	else if (chan->flags & IEEE80211_CHAN_2MHZ)
		return NL80211_CHAN_WIDTH_2;
	else if (chan->flags & IEEE80211_CHAN_4MHZ)
		return NL80211_CHAN_WIDTH_4;
	else if (chan->flags & IEEE80211_CHAN_8MHZ)
		return NL80211_CHAN_WIDTH_8;
	else if (chan->flags & IEEE80211_CHAN_16MHZ)
		return NL80211_CHAN_WIDTH_16;

	pr_err("unknown channel width for channel at %dKHz?\n",
	       ieee80211_channel_to_khz(chan));

	return NL80211_CHAN_WIDTH_1;
}
EXPORT_SYMBOL(ieee80211_s1g_channel_width);

int ieee80211_freq_khz_to_channel(u32 freq)
{
	/* TODO: just handle MHz for now */
	freq = KHZ_TO_MHZ(freq);

	/* see 802.11 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq < 5925)
		return (freq - 5000) / 5;
	else if (freq == 5935)
		return 2;
	else if (freq <= 45000) /* DMG band lower limit */
		/* see 802.11ax D6.1 27.3.22.2 */
		return (freq - 5950) / 5;
	else if (freq >= 58320 && freq <= 70200)
		return (freq - 56160) / 2160;
	else
		return 0;
}
EXPORT_SYMBOL(ieee80211_freq_khz_to_channel);

struct ieee80211_channel *ieee80211_get_channel_khz(struct wiphy *wiphy,
						    u32 freq)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	int i;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		sband = wiphy->bands[band];

		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels; i++) {
			struct ieee80211_channel *chan = &sband->channels[i];

			if (ieee80211_channel_to_khz(chan) == freq)
				return chan;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(ieee80211_get_channel_khz);

static void set_mandatory_flags_band(struct ieee80211_supported_band *sband)
{
	int i, want;

	switch (sband->band) {
	case NL80211_BAND_5GHZ:
	case NL80211_BAND_6GHZ:
		want = 3;
		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == 60 ||
			    sband->bitrates[i].bitrate == 120 ||
			    sband->bitrates[i].bitrate == 240) {
				sband->bitrates[i].flags |=
					IEEE80211_RATE_MANDATORY_A;
				want--;
			}
		}
		WARN_ON(want);
		break;
	case NL80211_BAND_2GHZ:
	case NL80211_BAND_LC:
		want = 7;
		for (i = 0; i < sband->n_bitrates; i++) {
			switch (sband->bitrates[i].bitrate) {
			case 10:
			case 20:
			case 55:
			case 110:
				sband->bitrates[i].flags |=
					IEEE80211_RATE_MANDATORY_B |
					IEEE80211_RATE_MANDATORY_G;
				want--;
				break;
			case 60:
			case 120:
			case 240:
				sband->bitrates[i].flags |=
					IEEE80211_RATE_MANDATORY_G;
				want--;
				fallthrough;
			default:
				sband->bitrates[i].flags |=
					IEEE80211_RATE_ERP_G;
				break;
			}
		}
		WARN_ON(want != 0 && want != 3);
		break;
	case NL80211_BAND_60GHZ:
		/* check for mandatory HT MCS 1..4 */
		WARN_ON(!sband->ht_cap.ht_supported);
		WARN_ON((sband->ht_cap.mcs.rx_mask[0] & 0x1e) != 0x1e);
		break;
	case NL80211_BAND_S1GHZ:
		/* Figure 9-589bd: 3 means unsupported, so != 3 means at least
		 * mandatory is ok.
		 */
		WARN_ON((sband->s1g_cap.nss_mcs[0] & 0x3) == 0x3);
		break;
	case NUM_NL80211_BANDS:
	default:
		WARN_ON(1);
		break;
	}
}

void ieee80211_set_bitrate_flags(struct wiphy *wiphy)
{
	enum nl80211_band band;

	for (band = 0; band < NUM_NL80211_BANDS; band++)
		if (wiphy->bands[band])
			set_mandatory_flags_band(wiphy->bands[band]);
}

bool cfg80211_supported_cipher_suite(struct wiphy *wiphy, u32 cipher)
{
	int i;
	for (i = 0; i < wiphy->n_cipher_suites; i++)
		if (cipher == wiphy->cipher_suites[i])
			return true;
	return false;
}

static bool
cfg80211_igtk_cipher_supported(struct cfg80211_registered_device *rdev)
{
	struct wiphy *wiphy = &rdev->wiphy;
	int i;

	for (i = 0; i < wiphy->n_cipher_suites; i++) {
		switch (wiphy->cipher_suites[i]) {
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			return true;
		}
	}

	return false;
}

bool cfg80211_valid_key_idx(struct cfg80211_registered_device *rdev,
			    int key_idx, bool pairwise)
{
	int max_key_idx;

	if (pairwise)
		max_key_idx = 3;
	else if (wiphy_ext_feature_isset(&rdev->wiphy,
					 NL80211_EXT_FEATURE_BEACON_PROTECTION) ||
		 wiphy_ext_feature_isset(&rdev->wiphy,
					 NL80211_EXT_FEATURE_BEACON_PROTECTION_CLIENT))
		max_key_idx = 7;
	else if (cfg80211_igtk_cipher_supported(rdev))
		max_key_idx = 5;
	else
		max_key_idx = 3;

	if (key_idx < 0 || key_idx > max_key_idx)
		return false;

	return true;
}

int cfg80211_validate_key_settings(struct cfg80211_registered_device *rdev,
				   struct key_params *params, int key_idx,
				   bool pairwise, const u8 *mac_addr)
{
	if (!cfg80211_valid_key_idx(rdev, key_idx, pairwise))
		return -EINVAL;

	if (!pairwise && mac_addr && !(rdev->wiphy.flags & WIPHY_FLAG_IBSS_RSN))
		return -EINVAL;

	if (pairwise && !mac_addr)
		return -EINVAL;

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		/* Extended Key ID can only be used with CCMP/GCMP ciphers */
		if ((pairwise && key_idx) ||
		    params->mode != NL80211_KEY_RX_TX)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		/* IEEE802.11-2016 allows only 0 and - when supporting
		 * Extended Key ID - 1 as index for pairwise keys.
		 * @NL80211_KEY_NO_TX is only allowed for pairwise keys when
		 * the driver supports Extended Key ID.
		 * @NL80211_KEY_SET_TX can't be set when installing and
		 * validating a key.
		 */
		if ((params->mode == NL80211_KEY_NO_TX && !pairwise) ||
		    params->mode == NL80211_KEY_SET_TX)
			return -EINVAL;
		if (wiphy_ext_feature_isset(&rdev->wiphy,
					    NL80211_EXT_FEATURE_EXT_KEY_ID)) {
			if (pairwise && (key_idx < 0 || key_idx > 1))
				return -EINVAL;
		} else if (pairwise && key_idx) {
			return -EINVAL;
		}
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		/* Disallow BIP (group-only) cipher as pairwise cipher */
		if (pairwise)
			return -EINVAL;
		if (key_idx < 4)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (key_idx > 3)
			return -EINVAL;
		break;
	default:
		break;
	}

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		if (params->key_len != WLAN_KEY_LEN_WEP40)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (params->key_len != WLAN_KEY_LEN_TKIP)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (params->key_len != WLAN_KEY_LEN_CCMP)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		if (params->key_len != WLAN_KEY_LEN_CCMP_256)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		if (params->key_len != WLAN_KEY_LEN_GCMP)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (params->key_len != WLAN_KEY_LEN_GCMP_256)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		if (params->key_len != WLAN_KEY_LEN_WEP104)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (params->key_len != WLAN_KEY_LEN_AES_CMAC)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		if (params->key_len != WLAN_KEY_LEN_BIP_CMAC_256)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		if (params->key_len != WLAN_KEY_LEN_BIP_GMAC_128)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (params->key_len != WLAN_KEY_LEN_BIP_GMAC_256)
			return -EINVAL;
		break;
	default:
		/*
		 * We don't know anything about this algorithm,
		 * allow using it -- but the driver must check
		 * all parameters! We still check below whether
		 * or not the driver supports this algorithm,
		 * of course.
		 */
		break;
	}

	if (params->seq) {
		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			/* These ciphers do not use key sequence */
			return -EINVAL;
		case WLAN_CIPHER_SUITE_TKIP:
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			if (params->seq_len != 6)
				return -EINVAL;
			break;
		}
	}

	if (!cfg80211_supported_cipher_suite(&rdev->wiphy, params->cipher))
		return -EINVAL;

	return 0;
}

unsigned int __attribute_const__ ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;

	if (ieee80211_is_ext(fc)) {
		hdrlen = 4;
		goto out;
	}

	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc))
			hdrlen = 30;
		if (ieee80211_is_data_qos(fc)) {
			hdrlen += IEEE80211_QOS_CTL_LEN;
			if (ieee80211_has_order(fc))
				hdrlen += IEEE80211_HT_CTL_LEN;
		}
		goto out;
	}

	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_has_order(fc))
			hdrlen += IEEE80211_HT_CTL_LEN;
		goto out;
	}

	if (ieee80211_is_ctl(fc)) {
		/*
		 * ACK and CTS are 10 bytes, all others 16. To see how
		 * to get this condition consider
		 *   subtype mask:   0b0000000011110000 (0x00F0)
		 *   ACK subtype:    0b0000000011010000 (0x00D0)
		 *   CTS subtype:    0b0000000011000000 (0x00C0)
		 *   bits that matter:         ^^^      (0x00E0)
		 *   value of those: 0b0000000011000000 (0x00C0)
		 */
		if ((fc & cpu_to_le16(0x00E0)) == cpu_to_le16(0x00C0))
			hdrlen = 10;
		else
			hdrlen = 16;
	}
out:
	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_hdrlen);

unsigned int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr =
			(const struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (unlikely(hdrlen > skb->len))
		return 0;
	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen_from_skb);

static unsigned int __ieee80211_get_mesh_hdrlen(u8 flags)
{
	int ae = flags & MESH_FLAGS_AE;
	/* 802.11-2012, 8.2.4.7.3 */
	switch (ae) {
	default:
	case 0:
		return 6;
	case MESH_FLAGS_AE_A4:
		return 12;
	case MESH_FLAGS_AE_A5_A6:
		return 18;
	}
}

unsigned int ieee80211_get_mesh_hdrlen(struct ieee80211s_hdr *meshhdr)
{
	return __ieee80211_get_mesh_hdrlen(meshhdr->flags);
}
EXPORT_SYMBOL(ieee80211_get_mesh_hdrlen);

int ieee80211_data_to_8023_exthdr(struct sk_buff *skb, struct ethhdr *ehdr,
				  const u8 *addr, enum nl80211_iftype iftype,
				  u8 data_offset, bool is_amsdu)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct {
		u8 hdr[ETH_ALEN] __aligned(2);
		__be16 proto;
	} payload;
	struct ethhdr tmp;
	u16 hdrlen;
	u8 mesh_flags = 0;

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return -1;

	hdrlen = ieee80211_hdrlen(hdr->frame_control) + data_offset;
	if (skb->len < hdrlen)
		return -1;

	/* convert IEEE 802.11 header + possible LLC headers into Ethernet
	 * header
	 * IEEE 802.11 address fields:
	 * ToDS FromDS Addr1 Addr2 Addr3 Addr4
	 *   0     0   DA    SA    BSSID n/a
	 *   0     1   DA    BSSID SA    n/a
	 *   1     0   BSSID SA    DA    n/a
	 *   1     1   RA    TA    DA    SA
	 */
	memcpy(tmp.h_dest, ieee80211_get_DA(hdr), ETH_ALEN);
	memcpy(tmp.h_source, ieee80211_get_SA(hdr), ETH_ALEN);

	if (iftype == NL80211_IFTYPE_MESH_POINT &&
	    skb_copy_bits(skb, hdrlen, &mesh_flags, 1) < 0)
		return -1;

	mesh_flags &= MESH_FLAGS_AE;

	switch (hdr->frame_control &
		cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
	case cpu_to_le16(IEEE80211_FCTL_TODS):
		if (unlikely(iftype != NL80211_IFTYPE_AP &&
			     iftype != NL80211_IFTYPE_AP_VLAN &&
			     iftype != NL80211_IFTYPE_P2P_GO))
			return -1;
		break;
	case cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		if (unlikely(iftype != NL80211_IFTYPE_MESH_POINT &&
			     iftype != NL80211_IFTYPE_AP_VLAN &&
			     iftype != NL80211_IFTYPE_STATION))
			return -1;
		if (iftype == NL80211_IFTYPE_MESH_POINT) {
			if (mesh_flags == MESH_FLAGS_AE_A4)
				return -1;
			if (mesh_flags == MESH_FLAGS_AE_A5_A6 &&
			    skb_copy_bits(skb, hdrlen +
					  offsetof(struct ieee80211s_hdr, eaddr1),
					  tmp.h_dest, 2 * ETH_ALEN) < 0)
				return -1;

			hdrlen += __ieee80211_get_mesh_hdrlen(mesh_flags);
		}
		break;
	case cpu_to_le16(IEEE80211_FCTL_FROMDS):
		if ((iftype != NL80211_IFTYPE_STATION &&
		     iftype != NL80211_IFTYPE_P2P_CLIENT &&
		     iftype != NL80211_IFTYPE_MESH_POINT) ||
		    (is_multicast_ether_addr(tmp.h_dest) &&
		     ether_addr_equal(tmp.h_source, addr)))
			return -1;
		if (iftype == NL80211_IFTYPE_MESH_POINT) {
			if (mesh_flags == MESH_FLAGS_AE_A5_A6)
				return -1;
			if (mesh_flags == MESH_FLAGS_AE_A4 &&
			    skb_copy_bits(skb, hdrlen +
					  offsetof(struct ieee80211s_hdr, eaddr1),
					  tmp.h_source, ETH_ALEN) < 0)
				return -1;
			hdrlen += __ieee80211_get_mesh_hdrlen(mesh_flags);
		}
		break;
	case cpu_to_le16(0):
		if (iftype != NL80211_IFTYPE_ADHOC &&
		    iftype != NL80211_IFTYPE_STATION &&
		    iftype != NL80211_IFTYPE_OCB)
				return -1;
		break;
	}

	if (likely(skb_copy_bits(skb, hdrlen, &payload, sizeof(payload)) == 0 &&
	           ((!is_amsdu && ether_addr_equal(payload.hdr, rfc1042_header) &&
		     payload.proto != htons(ETH_P_AARP) &&
		     payload.proto != htons(ETH_P_IPX)) ||
		    ether_addr_equal(payload.hdr, bridge_tunnel_header)))) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		hdrlen += ETH_ALEN + 2;
		tmp.h_proto = payload.proto;
		skb_postpull_rcsum(skb, &payload, ETH_ALEN + 2);
	} else {
		tmp.h_proto = htons(skb->len - hdrlen);
	}

	pskb_pull(skb, hdrlen);

	if (!ehdr)
		ehdr = skb_push(skb, sizeof(struct ethhdr));
	memcpy(ehdr, &tmp, sizeof(tmp));

	return 0;
}
EXPORT_SYMBOL(ieee80211_data_to_8023_exthdr);

static void
__frame_add_frag(struct sk_buff *skb, struct page *page,
		 void *ptr, int len, int size)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	int page_offset;

	get_page(page);
	page_offset = ptr - page_address(page);
	skb_add_rx_frag(skb, sh->nr_frags, page, page_offset, len, size);
}

static void
__ieee80211_amsdu_copy_frag(struct sk_buff *skb, struct sk_buff *frame,
			    int offset, int len)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	const skb_frag_t *frag = &sh->frags[0];
	struct page *frag_page;
	void *frag_ptr;
	int frag_len, frag_size;
	int head_size = skb->len - skb->data_len;
	int cur_len;

	frag_page = virt_to_head_page(skb->head);
	frag_ptr = skb->data;
	frag_size = head_size;

	while (offset >= frag_size) {
		offset -= frag_size;
		frag_page = skb_frag_page(frag);
		frag_ptr = skb_frag_address(frag);
		frag_size = skb_frag_size(frag);
		frag++;
	}

	frag_ptr += offset;
	frag_len = frag_size - offset;

	cur_len = min(len, frag_len);

	__frame_add_frag(frame, frag_page, frag_ptr, cur_len, frag_size);
	len -= cur_len;

	while (len > 0) {
		frag_len = skb_frag_size(frag);
		cur_len = min(len, frag_len);
		__frame_add_frag(frame, skb_frag_page(frag),
				 skb_frag_address(frag), cur_len, frag_len);
		len -= cur_len;
		frag++;
	}
}

static struct sk_buff *
__ieee80211_amsdu_copy(struct sk_buff *skb, unsigned int hlen,
		       int offset, int len, bool reuse_frag)
{
	struct sk_buff *frame;
	int cur_len = len;

	if (skb->len - offset < len)
		return NULL;

	/*
	 * When reusing framents, copy some data to the head to simplify
	 * ethernet header handling and speed up protocol header processing
	 * in the stack later.
	 */
	if (reuse_frag)
		cur_len = min_t(int, len, 32);

	/*
	 * Allocate and reserve two bytes more for payload
	 * alignment since sizeof(struct ethhdr) is 14.
	 */
	frame = dev_alloc_skb(hlen + sizeof(struct ethhdr) + 2 + cur_len);
	if (!frame)
		return NULL;

	skb_reserve(frame, hlen + sizeof(struct ethhdr) + 2);
	skb_copy_bits(skb, offset, skb_put(frame, cur_len), cur_len);

	len -= cur_len;
	if (!len)
		return frame;

	offset += cur_len;
	__ieee80211_amsdu_copy_frag(skb, frame, offset, len);

	return frame;
}

void ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			      const u8 *addr, enum nl80211_iftype iftype,
			      const unsigned int extra_headroom,
			      const u8 *check_da, const u8 *check_sa)
{
	unsigned int hlen = ALIGN(extra_headroom, 4);
	struct sk_buff *frame = NULL;
	u16 ethertype;
	u8 *payload;
	int offset = 0, remaining;
	struct ethhdr eth;
	bool reuse_frag = skb->head_frag && !skb_has_frag_list(skb);
	bool reuse_skb = false;
	bool last = false;

	while (!last) {
		unsigned int subframe_len;
		int len;
		u8 padding;

		skb_copy_bits(skb, offset, &eth, sizeof(eth));
		len = ntohs(eth.h_proto);
		subframe_len = sizeof(struct ethhdr) + len;
		padding = (4 - subframe_len) & 0x3;

		/* the last MSDU has no padding */
		remaining = skb->len - offset;
		if (subframe_len > remaining)
			goto purge;
		/* mitigate A-MSDU aggregation injection attacks */
		if (ether_addr_equal(eth.h_dest, rfc1042_header))
			goto purge;

		offset += sizeof(struct ethhdr);
		last = remaining <= subframe_len + padding;

		/* FIXME: should we really accept multicast DA? */
		if ((check_da && !is_multicast_ether_addr(eth.h_dest) &&
		     !ether_addr_equal(check_da, eth.h_dest)) ||
		    (check_sa && !ether_addr_equal(check_sa, eth.h_source))) {
			offset += len + padding;
			continue;
		}

		/* reuse skb for the last subframe */
		if (!skb_is_nonlinear(skb) && !reuse_frag && last) {
			skb_pull(skb, offset);
			frame = skb;
			reuse_skb = true;
		} else {
			frame = __ieee80211_amsdu_copy(skb, hlen, offset, len,
						       reuse_frag);
			if (!frame)
				goto purge;

			offset += len + padding;
		}

		skb_reset_network_header(frame);
		frame->dev = skb->dev;
		frame->priority = skb->priority;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];
		if (likely((ether_addr_equal(payload, rfc1042_header) &&
			    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
			   ether_addr_equal(payload, bridge_tunnel_header))) {
			eth.h_proto = htons(ethertype);
			skb_pull(frame, ETH_ALEN + 2);
		}

		memcpy(skb_push(frame, sizeof(eth)), &eth, sizeof(eth));
		__skb_queue_tail(list, frame);
	}

	if (!reuse_skb)
		dev_kfree_skb(skb);

	return;

 purge:
	__skb_queue_purge(list);
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_amsdu_to_8023s);

/* Given a data frame determine the 802.1p/1d tag to use. */
unsigned int cfg80211_classify8021d(struct sk_buff *skb,
				    struct cfg80211_qos_map *qos_map)
{
	unsigned int dscp;
	unsigned char vlan_priority;
	unsigned int ret;

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263) {
		ret = skb->priority - 256;
		goto out;
	}

	if (skb_vlan_tag_present(skb)) {
		vlan_priority = (skb_vlan_tag_get(skb) & VLAN_PRIO_MASK)
			>> VLAN_PRIO_SHIFT;
		if (vlan_priority > 0) {
			ret = vlan_priority;
			goto out;
		}
	}

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		dscp = ipv4_get_dsfield(ip_hdr(skb)) & 0xfc;
		break;
	case htons(ETH_P_IPV6):
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) & 0xfc;
		break;
	case htons(ETH_P_MPLS_UC):
	case htons(ETH_P_MPLS_MC): {
		struct mpls_label mpls_tmp, *mpls;

		mpls = skb_header_pointer(skb, sizeof(struct ethhdr),
					  sizeof(*mpls), &mpls_tmp);
		if (!mpls)
			return 0;

		ret = (ntohl(mpls->entry) & MPLS_LS_TC_MASK)
			>> MPLS_LS_TC_SHIFT;
		goto out;
	}
	case htons(ETH_P_80221):
		/* 802.21 is always network control traffic */
		return 7;
	default:
		return 0;
	}

	if (qos_map) {
		unsigned int i, tmp_dscp = dscp >> 2;

		for (i = 0; i < qos_map->num_des; i++) {
			if (tmp_dscp == qos_map->dscp_exception[i].dscp) {
				ret = qos_map->dscp_exception[i].up;
				goto out;
			}
		}

		for (i = 0; i < 8; i++) {
			if (tmp_dscp >= qos_map->up[i].low &&
			    tmp_dscp <= qos_map->up[i].high) {
				ret = i;
				goto out;
			}
		}
	}

	ret = dscp >> 5;
out:
	return array_index_nospec(ret, IEEE80211_NUM_TIDS);
}
EXPORT_SYMBOL(cfg80211_classify8021d);

const struct element *ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id)
{
	const struct cfg80211_bss_ies *ies;

	ies = rcu_dereference(bss->ies);
	if (!ies)
		return NULL;

	return cfg80211_find_elem(id, ies->data, ies->len);
}
EXPORT_SYMBOL(ieee80211_bss_get_elem);

void cfg80211_upload_connect_keys(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct net_device *dev = wdev->netdev;
	int i;

	if (!wdev->connect_keys)
		return;

	for (i = 0; i < CFG80211_MAX_WEP_KEYS; i++) {
		if (!wdev->connect_keys->params[i].cipher)
			continue;
		if (rdev_add_key(rdev, dev, -1, i, false, NULL,
				 &wdev->connect_keys->params[i])) {
			netdev_err(dev, "failed to set key %d\n", i);
			continue;
		}
		if (wdev->connect_keys->def == i &&
		    rdev_set_default_key(rdev, dev, -1, i, true, true)) {
			netdev_err(dev, "failed to set defkey %d\n", i);
			continue;
		}
	}

	kfree_sensitive(wdev->connect_keys);
	wdev->connect_keys = NULL;
}

void cfg80211_process_wdev_events(struct wireless_dev *wdev)
{
	struct cfg80211_event *ev;
	unsigned long flags;

	spin_lock_irqsave(&wdev->event_lock, flags);
	while (!list_empty(&wdev->event_list)) {
		ev = list_first_entry(&wdev->event_list,
				      struct cfg80211_event, list);
		list_del(&ev->list);
		spin_unlock_irqrestore(&wdev->event_lock, flags);

		wdev_lock(wdev);
		switch (ev->type) {
		case EVENT_CONNECT_RESULT:
			__cfg80211_connect_result(
				wdev->netdev,
				&ev->cr,
				ev->cr.status == WLAN_STATUS_SUCCESS);
			break;
		case EVENT_ROAMED:
			__cfg80211_roamed(wdev, &ev->rm);
			break;
		case EVENT_DISCONNECTED:
			__cfg80211_disconnected(wdev->netdev,
						ev->dc.ie, ev->dc.ie_len,
						ev->dc.reason,
						!ev->dc.locally_generated);
			break;
		case EVENT_IBSS_JOINED:
			__cfg80211_ibss_joined(wdev->netdev, ev->ij.bssid,
					       ev->ij.channel);
			break;
		case EVENT_STOPPED:
			__cfg80211_leave(wiphy_to_rdev(wdev->wiphy), wdev);
			break;
		case EVENT_PORT_AUTHORIZED:
			__cfg80211_port_authorized(wdev, ev->pa.bssid,
						   ev->pa.td_bitmap,
						   ev->pa.td_bitmap_len);
			break;
		}
		wdev_unlock(wdev);

		kfree(ev);

		spin_lock_irqsave(&wdev->event_lock, flags);
	}
	spin_unlock_irqrestore(&wdev->event_lock, flags);
}

void cfg80211_process_rdev_events(struct cfg80211_registered_device *rdev)
{
	struct wireless_dev *wdev;

	lockdep_assert_held(&rdev->wiphy.mtx);

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list)
		cfg80211_process_wdev_events(wdev);
}

int cfg80211_change_iface(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, enum nl80211_iftype ntype,
			  struct vif_params *params)
{
	int err;
	enum nl80211_iftype otype = dev->ieee80211_ptr->iftype;

	lockdep_assert_held(&rdev->wiphy.mtx);

	/* don't support changing VLANs, you just re-create them */
	if (otype == NL80211_IFTYPE_AP_VLAN)
		return -EOPNOTSUPP;

	/* cannot change into P2P device or NAN */
	if (ntype == NL80211_IFTYPE_P2P_DEVICE ||
	    ntype == NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!rdev->ops->change_virtual_intf ||
	    !(rdev->wiphy.interface_modes & (1 << ntype)))
		return -EOPNOTSUPP;

	if (ntype != otype) {
		/* if it's part of a bridge, reject changing type to station/ibss */
		if (netif_is_bridge_port(dev) &&
		    (ntype == NL80211_IFTYPE_ADHOC ||
		     ntype == NL80211_IFTYPE_STATION ||
		     ntype == NL80211_IFTYPE_P2P_CLIENT))
			return -EBUSY;

		dev->ieee80211_ptr->use_4addr = false;
		wdev_lock(dev->ieee80211_ptr);
		rdev_set_qos_map(rdev, dev, NULL);
		wdev_unlock(dev->ieee80211_ptr);

		switch (otype) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
			cfg80211_stop_ap(rdev, dev, -1, true);
			break;
		case NL80211_IFTYPE_ADHOC:
			cfg80211_leave_ibss(rdev, dev, false);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			wdev_lock(dev->ieee80211_ptr);
			cfg80211_disconnect(rdev, dev,
					    WLAN_REASON_DEAUTH_LEAVING, true);
			wdev_unlock(dev->ieee80211_ptr);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			/* mesh should be handled? */
			break;
		case NL80211_IFTYPE_OCB:
			cfg80211_leave_ocb(rdev, dev);
			break;
		default:
			break;
		}

		cfg80211_process_rdev_events(rdev);
		cfg80211_mlme_purge_registrations(dev->ieee80211_ptr);

		memset(&dev->ieee80211_ptr->u, 0,
		       sizeof(dev->ieee80211_ptr->u));
		memset(&dev->ieee80211_ptr->links, 0,
		       sizeof(dev->ieee80211_ptr->links));
	}

	err = rdev_change_virtual_intf(rdev, dev, ntype, params);

	WARN_ON(!err && dev->ieee80211_ptr->iftype != ntype);

	if (!err && params && params->use_4addr != -1)
		dev->ieee80211_ptr->use_4addr = params->use_4addr;

	if (!err) {
		dev->priv_flags &= ~IFF_DONT_BRIDGE;
		switch (ntype) {
		case NL80211_IFTYPE_STATION:
			if (dev->ieee80211_ptr->use_4addr)
				break;
			fallthrough;
		case NL80211_IFTYPE_OCB:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_ADHOC:
			dev->priv_flags |= IFF_DONT_BRIDGE;
			break;
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MESH_POINT:
			/* bridging OK */
			break;
		case NL80211_IFTYPE_MONITOR:
			/* monitor can't bridge anyway */
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
			/* not happening */
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_NAN:
			WARN_ON(1);
			break;
		}
	}

	if (!err && ntype != otype && netif_running(dev)) {
		cfg80211_update_iface_num(rdev, ntype, 1);
		cfg80211_update_iface_num(rdev, otype, -1);
	}

	return err;
}

static u32 cfg80211_calculate_bitrate_ht(struct rate_info *rate)
{
	int modulation, streams, bitrate;

	/* the formula below does only work for MCS values smaller than 32 */
	if (WARN_ON_ONCE(rate->mcs >= 32))
		return 0;

	modulation = rate->mcs & 7;
	streams = (rate->mcs >> 3) + 1;

	bitrate = (rate->bw == RATE_INFO_BW_40) ? 13500000 : 6500000;

	if (modulation < 4)
		bitrate *= (modulation + 1);
	else if (modulation == 4)
		bitrate *= (modulation + 2);
	else
		bitrate *= (modulation + 3);

	bitrate *= streams;

	if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
}

static u32 cfg80211_calculate_bitrate_dmg(struct rate_info *rate)
{
	static const u32 __mcs2bitrate[] = {
		/* control PHY */
		[0] =   275,
		/* SC PHY */
		[1] =  3850,
		[2] =  7700,
		[3] =  9625,
		[4] = 11550,
		[5] = 12512, /* 1251.25 mbps */
		[6] = 15400,
		[7] = 19250,
		[8] = 23100,
		[9] = 25025,
		[10] = 30800,
		[11] = 38500,
		[12] = 46200,
		/* OFDM PHY */
		[13] =  6930,
		[14] =  8662, /* 866.25 mbps */
		[15] = 13860,
		[16] = 17325,
		[17] = 20790,
		[18] = 27720,
		[19] = 34650,
		[20] = 41580,
		[21] = 45045,
		[22] = 51975,
		[23] = 62370,
		[24] = 67568, /* 6756.75 mbps */
		/* LP-SC PHY */
		[25] =  6260,
		[26] =  8340,
		[27] = 11120,
		[28] = 12510,
		[29] = 16680,
		[30] = 22240,
		[31] = 25030,
	};

	if (WARN_ON_ONCE(rate->mcs >= ARRAY_SIZE(__mcs2bitrate)))
		return 0;

	return __mcs2bitrate[rate->mcs];
}

static u32 cfg80211_calculate_bitrate_extended_sc_dmg(struct rate_info *rate)
{
	static const u32 __mcs2bitrate[] = {
		[6 - 6] = 26950, /* MCS 9.1 : 2695.0 mbps */
		[7 - 6] = 50050, /* MCS 12.1 */
		[8 - 6] = 53900,
		[9 - 6] = 57750,
		[10 - 6] = 63900,
		[11 - 6] = 75075,
		[12 - 6] = 80850,
	};

	/* Extended SC MCS not defined for base MCS below 6 or above 12 */
	if (WARN_ON_ONCE(rate->mcs < 6 || rate->mcs > 12))
		return 0;

	return __mcs2bitrate[rate->mcs - 6];
}

static u32 cfg80211_calculate_bitrate_edmg(struct rate_info *rate)
{
	static const u32 __mcs2bitrate[] = {
		/* control PHY */
		[0] =   275,
		/* SC PHY */
		[1] =  3850,
		[2] =  7700,
		[3] =  9625,
		[4] = 11550,
		[5] = 12512, /* 1251.25 mbps */
		[6] = 13475,
		[7] = 15400,
		[8] = 19250,
		[9] = 23100,
		[10] = 25025,
		[11] = 26950,
		[12] = 30800,
		[13] = 38500,
		[14] = 46200,
		[15] = 50050,
		[16] = 53900,
		[17] = 57750,
		[18] = 69300,
		[19] = 75075,
		[20] = 80850,
	};

	if (WARN_ON_ONCE(rate->mcs >= ARRAY_SIZE(__mcs2bitrate)))
		return 0;

	return __mcs2bitrate[rate->mcs] * rate->n_bonded_ch;
}

static u32 cfg80211_calculate_bitrate_vht(struct rate_info *rate)
{
	static const u32 base[4][12] = {
		{   6500000,
		   13000000,
		   19500000,
		   26000000,
		   39000000,
		   52000000,
		   58500000,
		   65000000,
		   78000000,
		/* not in the spec, but some devices use this: */
		   86700000,
		   97500000,
		  108300000,
		},
		{  13500000,
		   27000000,
		   40500000,
		   54000000,
		   81000000,
		  108000000,
		  121500000,
		  135000000,
		  162000000,
		  180000000,
		  202500000,
		  225000000,
		},
		{  29300000,
		   58500000,
		   87800000,
		  117000000,
		  175500000,
		  234000000,
		  263300000,
		  292500000,
		  351000000,
		  390000000,
		  438800000,
		  487500000,
		},
		{  58500000,
		  117000000,
		  175500000,
		  234000000,
		  351000000,
		  468000000,
		  526500000,
		  585000000,
		  702000000,
		  780000000,
		  877500000,
		  975000000,
		},
	};
	u32 bitrate;
	int idx;

	if (rate->mcs > 11)
		goto warn;

	switch (rate->bw) {
	case RATE_INFO_BW_160:
		idx = 3;
		break;
	case RATE_INFO_BW_80:
		idx = 2;
		break;
	case RATE_INFO_BW_40:
		idx = 1;
		break;
	case RATE_INFO_BW_5:
	case RATE_INFO_BW_10:
	default:
		goto warn;
	case RATE_INFO_BW_20:
		idx = 0;
	}

	bitrate = base[idx][rate->mcs];
	bitrate *= rate->nss;

	if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
 warn:
	WARN_ONCE(1, "invalid rate bw=%d, mcs=%d, nss=%d\n",
		  rate->bw, rate->mcs, rate->nss);
	return 0;
}

static u32 cfg80211_calculate_bitrate_he(struct rate_info *rate)
{
#define SCALE 6144
	u32 mcs_divisors[14] = {
		102399, /* 16.666666... */
		 51201, /*  8.333333... */
		 34134, /*  5.555555... */
		 25599, /*  4.166666... */
		 17067, /*  2.777777... */
		 12801, /*  2.083333... */
		 11377, /*  1.851725... */
		 10239, /*  1.666666... */
		  8532, /*  1.388888... */
		  7680, /*  1.250000... */
		  6828, /*  1.111111... */
		  6144, /*  1.000000... */
		  5690, /*  0.926106... */
		  5120, /*  0.833333... */
	};
	u32 rates_160M[3] = { 960777777, 907400000, 816666666 };
	u32 rates_969[3] =  { 480388888, 453700000, 408333333 };
	u32 rates_484[3] =  { 229411111, 216666666, 195000000 };
	u32 rates_242[3] =  { 114711111, 108333333,  97500000 };
	u32 rates_106[3] =  {  40000000,  37777777,  34000000 };
	u32 rates_52[3]  =  {  18820000,  17777777,  16000000 };
	u32 rates_26[3]  =  {   9411111,   8888888,   8000000 };
	u64 tmp;
	u32 result;

	if (WARN_ON_ONCE(rate->mcs > 13))
		return 0;

	if (WARN_ON_ONCE(rate->he_gi > NL80211_RATE_INFO_HE_GI_3_2))
		return 0;
	if (WARN_ON_ONCE(rate->he_ru_alloc >
			 NL80211_RATE_INFO_HE_RU_ALLOC_2x996))
		return 0;
	if (WARN_ON_ONCE(rate->nss < 1 || rate->nss > 8))
		return 0;

	if (rate->bw == RATE_INFO_BW_160)
		result = rates_160M[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_80 ||
		 (rate->bw == RATE_INFO_BW_HE_RU &&
		  rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_996))
		result = rates_969[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_40 ||
		 (rate->bw == RATE_INFO_BW_HE_RU &&
		  rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_484))
		result = rates_484[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_20 ||
		 (rate->bw == RATE_INFO_BW_HE_RU &&
		  rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_242))
		result = rates_242[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_HE_RU &&
		 rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_106)
		result = rates_106[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_HE_RU &&
		 rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_52)
		result = rates_52[rate->he_gi];
	else if (rate->bw == RATE_INFO_BW_HE_RU &&
		 rate->he_ru_alloc == NL80211_RATE_INFO_HE_RU_ALLOC_26)
		result = rates_26[rate->he_gi];
	else {
		WARN(1, "invalid HE MCS: bw:%d, ru:%d\n",
		     rate->bw, rate->he_ru_alloc);
		return 0;
	}

	/* now scale to the appropriate MCS */
	tmp = result;
	tmp *= SCALE;
	do_div(tmp, mcs_divisors[rate->mcs]);
	result = tmp;

	/* and take NSS, DCM into account */
	result = (result * rate->nss) / 8;
	if (rate->he_dcm)
		result /= 2;

	return result / 10000;
}

static u32 cfg80211_calculate_bitrate_eht(struct rate_info *rate)
{
#define SCALE 6144
	static const u32 mcs_divisors[16] = {
		102399, /* 16.666666... */
		 51201, /*  8.333333... */
		 34134, /*  5.555555... */
		 25599, /*  4.166666... */
		 17067, /*  2.777777... */
		 12801, /*  2.083333... */
		 11377, /*  1.851725... */
		 10239, /*  1.666666... */
		  8532, /*  1.388888... */
		  7680, /*  1.250000... */
		  6828, /*  1.111111... */
		  6144, /*  1.000000... */
		  5690, /*  0.926106... */
		  5120, /*  0.833333... */
		409600, /* 66.666666... */
		204800, /* 33.333333... */
	};
	static const u32 rates_996[3] =  { 480388888, 453700000, 408333333 };
	static const u32 rates_484[3] =  { 229411111, 216666666, 195000000 };
	static const u32 rates_242[3] =  { 114711111, 108333333,  97500000 };
	static const u32 rates_106[3] =  {  40000000,  37777777,  34000000 };
	static const u32 rates_52[3]  =  {  18820000,  17777777,  16000000 };
	static const u32 rates_26[3]  =  {   9411111,   8888888,   8000000 };
	u64 tmp;
	u32 result;

	if (WARN_ON_ONCE(rate->mcs > 15))
		return 0;
	if (WARN_ON_ONCE(rate->eht_gi > NL80211_RATE_INFO_EHT_GI_3_2))
		return 0;
	if (WARN_ON_ONCE(rate->eht_ru_alloc >
			 NL80211_RATE_INFO_EHT_RU_ALLOC_4x996))
		return 0;
	if (WARN_ON_ONCE(rate->nss < 1 || rate->nss > 8))
		return 0;

	/* Bandwidth checks for MCS 14 */
	if (rate->mcs == 14) {
		if ((rate->bw != RATE_INFO_BW_EHT_RU &&
		     rate->bw != RATE_INFO_BW_80 &&
		     rate->bw != RATE_INFO_BW_160 &&
		     rate->bw != RATE_INFO_BW_320) ||
		    (rate->bw == RATE_INFO_BW_EHT_RU &&
		     rate->eht_ru_alloc != NL80211_RATE_INFO_EHT_RU_ALLOC_996 &&
		     rate->eht_ru_alloc != NL80211_RATE_INFO_EHT_RU_ALLOC_2x996 &&
		     rate->eht_ru_alloc != NL80211_RATE_INFO_EHT_RU_ALLOC_4x996)) {
			WARN(1, "invalid EHT BW for MCS 14: bw:%d, ru:%d\n",
			     rate->bw, rate->eht_ru_alloc);
			return 0;
		}
	}

	if (rate->bw == RATE_INFO_BW_320 ||
	    (rate->bw == RATE_INFO_BW_EHT_RU &&
	     rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_4x996))
		result = 4 * rates_996[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_3x996P484)
		result = 3 * rates_996[rate->eht_gi] + rates_484[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_3x996)
		result = 3 * rates_996[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_2x996P484)
		result = 2 * rates_996[rate->eht_gi] + rates_484[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_160 ||
		 (rate->bw == RATE_INFO_BW_EHT_RU &&
		  rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_2x996))
		result = 2 * rates_996[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc ==
		 NL80211_RATE_INFO_EHT_RU_ALLOC_996P484P242)
		result = rates_996[rate->eht_gi] + rates_484[rate->eht_gi]
			 + rates_242[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_996P484)
		result = rates_996[rate->eht_gi] + rates_484[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_80 ||
		 (rate->bw == RATE_INFO_BW_EHT_RU &&
		  rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_996))
		result = rates_996[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_484P242)
		result = rates_484[rate->eht_gi] + rates_242[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_40 ||
		 (rate->bw == RATE_INFO_BW_EHT_RU &&
		  rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_484))
		result = rates_484[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_20 ||
		 (rate->bw == RATE_INFO_BW_EHT_RU &&
		  rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_242))
		result = rates_242[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_106P26)
		result = rates_106[rate->eht_gi] + rates_26[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_106)
		result = rates_106[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_52P26)
		result = rates_52[rate->eht_gi] + rates_26[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_52)
		result = rates_52[rate->eht_gi];
	else if (rate->bw == RATE_INFO_BW_EHT_RU &&
		 rate->eht_ru_alloc == NL80211_RATE_INFO_EHT_RU_ALLOC_26)
		result = rates_26[rate->eht_gi];
	else {
		WARN(1, "invalid EHT MCS: bw:%d, ru:%d\n",
		     rate->bw, rate->eht_ru_alloc);
		return 0;
	}

	/* now scale to the appropriate MCS */
	tmp = result;
	tmp *= SCALE;
	do_div(tmp, mcs_divisors[rate->mcs]);

	/* and take NSS */
	tmp *= rate->nss;
	do_div(tmp, 8);

	result = tmp;

	return result / 10000;
}

u32 cfg80211_calculate_bitrate(struct rate_info *rate)
{
	if (rate->flags & RATE_INFO_FLAGS_MCS)
		return cfg80211_calculate_bitrate_ht(rate);
	if (rate->flags & RATE_INFO_FLAGS_DMG)
		return cfg80211_calculate_bitrate_dmg(rate);
	if (rate->flags & RATE_INFO_FLAGS_EXTENDED_SC_DMG)
		return cfg80211_calculate_bitrate_extended_sc_dmg(rate);
	if (rate->flags & RATE_INFO_FLAGS_EDMG)
		return cfg80211_calculate_bitrate_edmg(rate);
	if (rate->flags & RATE_INFO_FLAGS_VHT_MCS)
		return cfg80211_calculate_bitrate_vht(rate);
	if (rate->flags & RATE_INFO_FLAGS_HE_MCS)
		return cfg80211_calculate_bitrate_he(rate);
	if (rate->flags & RATE_INFO_FLAGS_EHT_MCS)
		return cfg80211_calculate_bitrate_eht(rate);

	return rate->legacy;
}
EXPORT_SYMBOL(cfg80211_calculate_bitrate);

int cfg80211_get_p2p_attr(const u8 *ies, unsigned int len,
			  enum ieee80211_p2p_attr_id attr,
			  u8 *buf, unsigned int bufsize)
{
	u8 *out = buf;
	u16 attr_remaining = 0;
	bool desired_attr = false;
	u16 desired_len = 0;

	while (len > 0) {
		unsigned int iedatalen;
		unsigned int copy;
		const u8 *iedata;

		if (len < 2)
			return -EILSEQ;
		iedatalen = ies[1];
		if (iedatalen + 2 > len)
			return -EILSEQ;

		if (ies[0] != WLAN_EID_VENDOR_SPECIFIC)
			goto cont;

		if (iedatalen < 4)
			goto cont;

		iedata = ies + 2;

		/* check WFA OUI, P2P subtype */
		if (iedata[0] != 0x50 || iedata[1] != 0x6f ||
		    iedata[2] != 0x9a || iedata[3] != 0x09)
			goto cont;

		iedatalen -= 4;
		iedata += 4;

		/* check attribute continuation into this IE */
		copy = min_t(unsigned int, attr_remaining, iedatalen);
		if (copy && desired_attr) {
			desired_len += copy;
			if (out) {
				memcpy(out, iedata, min(bufsize, copy));
				out += min(bufsize, copy);
				bufsize -= min(bufsize, copy);
			}


			if (copy == attr_remaining)
				return desired_len;
		}

		attr_remaining -= copy;
		if (attr_remaining)
			goto cont;

		iedatalen -= copy;
		iedata += copy;

		while (iedatalen > 0) {
			u16 attr_len;

			/* P2P attribute ID & size must fit */
			if (iedatalen < 3)
				return -EILSEQ;
			desired_attr = iedata[0] == attr;
			attr_len = get_unaligned_le16(iedata + 1);
			iedatalen -= 3;
			iedata += 3;

			copy = min_t(unsigned int, attr_len, iedatalen);

			if (desired_attr) {
				desired_len += copy;
				if (out) {
					memcpy(out, iedata, min(bufsize, copy));
					out += min(bufsize, copy);
					bufsize -= min(bufsize, copy);
				}

				if (copy == attr_len)
					return desired_len;
			}

			iedata += copy;
			iedatalen -= copy;
			attr_remaining = attr_len - copy;
		}

 cont:
		len -= ies[1] + 2;
		ies += ies[1] + 2;
	}

	if (attr_remaining && desired_attr)
		return -EILSEQ;

	return -ENOENT;
}
EXPORT_SYMBOL(cfg80211_get_p2p_attr);

static bool ieee80211_id_in_list(const u8 *ids, int n_ids, u8 id, bool id_ext)
{
	int i;

	/* Make sure array values are legal */
	if (WARN_ON(ids[n_ids - 1] == WLAN_EID_EXTENSION))
		return false;

	i = 0;
	while (i < n_ids) {
		if (ids[i] == WLAN_EID_EXTENSION) {
			if (id_ext && (ids[i + 1] == id))
				return true;

			i += 2;
			continue;
		}

		if (ids[i] == id && !id_ext)
			return true;

		i++;
	}
	return false;
}

static size_t skip_ie(const u8 *ies, size_t ielen, size_t pos)
{
	/* we assume a validly formed IEs buffer */
	u8 len = ies[pos + 1];

	pos += 2 + len;

	/* the IE itself must have 255 bytes for fragments to follow */
	if (len < 255)
		return pos;

	while (pos < ielen && ies[pos] == WLAN_EID_FRAGMENT) {
		len = ies[pos + 1];
		pos += 2 + len;
	}

	return pos;
}

size_t ieee80211_ie_split_ric(const u8 *ies, size_t ielen,
			      const u8 *ids, int n_ids,
			      const u8 *after_ric, int n_after_ric,
			      size_t offset)
{
	size_t pos = offset;

	while (pos < ielen) {
		u8 ext = 0;

		if (ies[pos] == WLAN_EID_EXTENSION)
			ext = 2;
		if ((pos + ext) >= ielen)
			break;

		if (!ieee80211_id_in_list(ids, n_ids, ies[pos + ext],
					  ies[pos] == WLAN_EID_EXTENSION))
			break;

		if (ies[pos] == WLAN_EID_RIC_DATA && n_after_ric) {
			pos = skip_ie(ies, ielen, pos);

			while (pos < ielen) {
				if (ies[pos] == WLAN_EID_EXTENSION)
					ext = 2;
				else
					ext = 0;

				if ((pos + ext) >= ielen)
					break;

				if (!ieee80211_id_in_list(after_ric,
							  n_after_ric,
							  ies[pos + ext],
							  ext == 2))
					pos = skip_ie(ies, ielen, pos);
				else
					break;
			}
		} else {
			pos = skip_ie(ies, ielen, pos);
		}
	}

	return pos;
}
EXPORT_SYMBOL(ieee80211_ie_split_ric);

bool ieee80211_operating_class_to_band(u8 operating_class,
				       enum nl80211_band *band)
{
	switch (operating_class) {
	case 112:
	case 115 ... 127:
	case 128 ... 130:
		*band = NL80211_BAND_5GHZ;
		return true;
	case 131 ... 135:
		*band = NL80211_BAND_6GHZ;
		return true;
	case 81:
	case 82:
	case 83:
	case 84:
		*band = NL80211_BAND_2GHZ;
		return true;
	case 180:
		*band = NL80211_BAND_60GHZ;
		return true;
	}

	return false;
}
EXPORT_SYMBOL(ieee80211_operating_class_to_band);

bool ieee80211_chandef_to_operating_class(struct cfg80211_chan_def *chandef,
					  u8 *op_class)
{
	u8 vht_opclass;
	u32 freq = chandef->center_freq1;

	if (freq >= 2412 && freq <= 2472) {
		if (chandef->width > NL80211_CHAN_WIDTH_40)
			return false;

		/* 2.407 GHz, channels 1..13 */
		if (chandef->width == NL80211_CHAN_WIDTH_40) {
			if (freq > chandef->chan->center_freq)
				*op_class = 83; /* HT40+ */
			else
				*op_class = 84; /* HT40- */
		} else {
			*op_class = 81;
		}

		return true;
	}

	if (freq == 2484) {
		/* channel 14 is only for IEEE 802.11b */
		if (chandef->width != NL80211_CHAN_WIDTH_20_NOHT)
			return false;

		*op_class = 82; /* channel 14 */
		return true;
	}

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_80:
		vht_opclass = 128;
		break;
	case NL80211_CHAN_WIDTH_160:
		vht_opclass = 129;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		vht_opclass = 130;
		break;
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_5:
		return false; /* unsupported for now */
	default:
		vht_opclass = 0;
		break;
	}

	/* 5 GHz, channels 36..48 */
	if (freq >= 5180 && freq <= 5240) {
		if (vht_opclass) {
			*op_class = vht_opclass;
		} else if (chandef->width == NL80211_CHAN_WIDTH_40) {
			if (freq > chandef->chan->center_freq)
				*op_class = 116;
			else
				*op_class = 117;
		} else {
			*op_class = 115;
		}

		return true;
	}

	/* 5 GHz, channels 52..64 */
	if (freq >= 5260 && freq <= 5320) {
		if (vht_opclass) {
			*op_class = vht_opclass;
		} else if (chandef->width == NL80211_CHAN_WIDTH_40) {
			if (freq > chandef->chan->center_freq)
				*op_class = 119;
			else
				*op_class = 120;
		} else {
			*op_class = 118;
		}

		return true;
	}

	/* 5 GHz, channels 100..144 */
	if (freq >= 5500 && freq <= 5720) {
		if (vht_opclass) {
			*op_class = vht_opclass;
		} else if (chandef->width == NL80211_CHAN_WIDTH_40) {
			if (freq > chandef->chan->center_freq)
				*op_class = 122;
			else
				*op_class = 123;
		} else {
			*op_class = 121;
		}

		return true;
	}

	/* 5 GHz, channels 149..169 */
	if (freq >= 5745 && freq <= 5845) {
		if (vht_opclass) {
			*op_class = vht_opclass;
		} else if (chandef->width == NL80211_CHAN_WIDTH_40) {
			if (freq > chandef->chan->center_freq)
				*op_class = 126;
			else
				*op_class = 127;
		} else if (freq <= 5805) {
			*op_class = 124;
		} else {
			*op_class = 125;
		}

		return true;
	}

	/* 56.16 GHz, channel 1..4 */
	if (freq >= 56160 + 2160 * 1 && freq <= 56160 + 2160 * 6) {
		if (chandef->width >= NL80211_CHAN_WIDTH_40)
			return false;

		*op_class = 180;
		return true;
	}

	/* not supported yet */
	return false;
}
EXPORT_SYMBOL(ieee80211_chandef_to_operating_class);

static int cfg80211_wdev_bi(struct wireless_dev *wdev)
{
	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		WARN_ON(wdev->valid_links);
		return wdev->links[0].ap.beacon_interval;
	case NL80211_IFTYPE_MESH_POINT:
		return wdev->u.mesh.beacon_interval;
	case NL80211_IFTYPE_ADHOC:
		return wdev->u.ibss.beacon_interval;
	default:
		break;
	}

	return 0;
}

static void cfg80211_calculate_bi_data(struct wiphy *wiphy, u32 new_beacon_int,
				       u32 *beacon_int_gcd,
				       bool *beacon_int_different)
{
	struct wireless_dev *wdev;

	*beacon_int_gcd = 0;
	*beacon_int_different = false;

	list_for_each_entry(wdev, &wiphy->wdev_list, list) {
		int wdev_bi;

		/* this feature isn't supported with MLO */
		if (wdev->valid_links)
			continue;

		wdev_bi = cfg80211_wdev_bi(wdev);

		if (!wdev_bi)
			continue;

		if (!*beacon_int_gcd) {
			*beacon_int_gcd = wdev_bi;
			continue;
		}

		if (wdev_bi == *beacon_int_gcd)
			continue;

		*beacon_int_different = true;
		*beacon_int_gcd = gcd(*beacon_int_gcd, wdev_bi);
	}

	if (new_beacon_int && *beacon_int_gcd != new_beacon_int) {
		if (*beacon_int_gcd)
			*beacon_int_different = true;
		*beacon_int_gcd = gcd(*beacon_int_gcd, new_beacon_int);
	}
}

int cfg80211_validate_beacon_int(struct cfg80211_registered_device *rdev,
				 enum nl80211_iftype iftype, u32 beacon_int)
{
	/*
	 * This is just a basic pre-condition check; if interface combinations
	 * are possible the driver must already be checking those with a call
	 * to cfg80211_check_combinations(), in which case we'll validate more
	 * through the cfg80211_calculate_bi_data() call and code in
	 * cfg80211_iter_combinations().
	 */

	if (beacon_int < 10 || beacon_int > 10000)
		return -EINVAL;

	return 0;
}

int cfg80211_iter_combinations(struct wiphy *wiphy,
			       struct iface_combination_params *params,
			       void (*iter)(const struct ieee80211_iface_combination *c,
					    void *data),
			       void *data)
{
	const struct ieee80211_regdomain *regdom;
	enum nl80211_dfs_regions region = 0;
	int i, j, iftype;
	int num_interfaces = 0;
	u32 used_iftypes = 0;
	u32 beacon_int_gcd;
	bool beacon_int_different;

	/*
	 * This is a bit strange, since the iteration used to rely only on
	 * the data given by the driver, but here it now relies on context,
	 * in form of the currently operating interfaces.
	 * This is OK for all current users, and saves us from having to
	 * push the GCD calculations into all the drivers.
	 * In the future, this should probably rely more on data that's in
	 * cfg80211 already - the only thing not would appear to be any new
	 * interfaces (while being brought up) and channel/radar data.
	 */
	cfg80211_calculate_bi_data(wiphy, params->new_beacon_int,
				   &beacon_int_gcd, &beacon_int_different);

	if (params->radar_detect) {
		rcu_read_lock();
		regdom = rcu_dereference(cfg80211_regdomain);
		if (regdom)
			region = regdom->dfs_region;
		rcu_read_unlock();
	}

	for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
		num_interfaces += params->iftype_num[iftype];
		if (params->iftype_num[iftype] > 0 &&
		    !cfg80211_iftype_allowed(wiphy, iftype, 0, 1))
			used_iftypes |= BIT(iftype);
	}

	for (i = 0; i < wiphy->n_iface_combinations; i++) {
		const struct ieee80211_iface_combination *c;
		struct ieee80211_iface_limit *limits;
		u32 all_iftypes = 0;

		c = &wiphy->iface_combinations[i];

		if (num_interfaces > c->max_interfaces)
			continue;
		if (params->num_different_channels > c->num_different_channels)
			continue;

		limits = kmemdup(c->limits, sizeof(limits[0]) * c->n_limits,
				 GFP_KERNEL);
		if (!limits)
			return -ENOMEM;

		for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
			if (cfg80211_iftype_allowed(wiphy, iftype, 0, 1))
				continue;
			for (j = 0; j < c->n_limits; j++) {
				all_iftypes |= limits[j].types;
				if (!(limits[j].types & BIT(iftype)))
					continue;
				if (limits[j].max < params->iftype_num[iftype])
					goto cont;
				limits[j].max -= params->iftype_num[iftype];
			}
		}

		if (params->radar_detect !=
			(c->radar_detect_widths & params->radar_detect))
			goto cont;

		if (params->radar_detect && c->radar_detect_regions &&
		    !(c->radar_detect_regions & BIT(region)))
			goto cont;

		/* Finally check that all iftypes that we're currently
		 * using are actually part of this combination. If they
		 * aren't then we can't use this combination and have
		 * to continue to the next.
		 */
		if ((all_iftypes & used_iftypes) != used_iftypes)
			goto cont;

		if (beacon_int_gcd) {
			if (c->beacon_int_min_gcd &&
			    beacon_int_gcd < c->beacon_int_min_gcd)
				goto cont;
			if (!c->beacon_int_min_gcd && beacon_int_different)
				goto cont;
		}

		/* This combination covered all interface types and
		 * supported the requested numbers, so we're good.
		 */

		(*iter)(c, data);
 cont:
		kfree(limits);
	}

	return 0;
}
EXPORT_SYMBOL(cfg80211_iter_combinations);

static void
cfg80211_iter_sum_ifcombs(const struct ieee80211_iface_combination *c,
			  void *data)
{
	int *num = data;
	(*num)++;
}

int cfg80211_check_combinations(struct wiphy *wiphy,
				struct iface_combination_params *params)
{
	int err, num = 0;

	err = cfg80211_iter_combinations(wiphy, params,
					 cfg80211_iter_sum_ifcombs, &num);
	if (err)
		return err;
	if (num == 0)
		return -EBUSY;

	return 0;
}
EXPORT_SYMBOL(cfg80211_check_combinations);

int ieee80211_get_ratemask(struct ieee80211_supported_band *sband,
			   const u8 *rates, unsigned int n_rates,
			   u32 *mask)
{
	int i, j;

	if (!sband)
		return -EINVAL;

	if (n_rates == 0 || n_rates > NL80211_MAX_SUPP_RATES)
		return -EINVAL;

	*mask = 0;

	for (i = 0; i < n_rates; i++) {
		int rate = (rates[i] & 0x7f) * 5;
		bool found = false;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate) {
				found = true;
				*mask |= BIT(j);
				break;
			}
		}
		if (!found)
			return -EINVAL;
	}

	/*
	 * mask must have at least one bit set here since we
	 * didn't accept a 0-length rates array nor allowed
	 * entries in the array that didn't exist
	 */

	return 0;
}

unsigned int ieee80211_get_num_supported_channels(struct wiphy *wiphy)
{
	enum nl80211_band band;
	unsigned int n_channels = 0;

	for (band = 0; band < NUM_NL80211_BANDS; band++)
		if (wiphy->bands[band])
			n_channels += wiphy->bands[band]->n_channels;

	return n_channels;
}
EXPORT_SYMBOL(ieee80211_get_num_supported_channels);

int cfg80211_get_station(struct net_device *dev, const u8 *mac_addr,
			 struct station_info *sinfo)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;

	wdev = dev->ieee80211_ptr;
	if (!wdev)
		return -EOPNOTSUPP;

	rdev = wiphy_to_rdev(wdev->wiphy);
	if (!rdev->ops->get_station)
		return -EOPNOTSUPP;

	memset(sinfo, 0, sizeof(*sinfo));

	return rdev_get_station(rdev, dev, mac_addr, sinfo);
}
EXPORT_SYMBOL(cfg80211_get_station);

void cfg80211_free_nan_func(struct cfg80211_nan_func *f)
{
	int i;

	if (!f)
		return;

	kfree(f->serv_spec_info);
	kfree(f->srf_bf);
	kfree(f->srf_macs);
	for (i = 0; i < f->num_rx_filters; i++)
		kfree(f->rx_filters[i].filter);

	for (i = 0; i < f->num_tx_filters; i++)
		kfree(f->tx_filters[i].filter);

	kfree(f->rx_filters);
	kfree(f->tx_filters);
	kfree(f);
}
EXPORT_SYMBOL(cfg80211_free_nan_func);

bool cfg80211_does_bw_fit_range(const struct ieee80211_freq_range *freq_range,
				u32 center_freq_khz, u32 bw_khz)
{
	u32 start_freq_khz, end_freq_khz;

	start_freq_khz = center_freq_khz - (bw_khz / 2);
	end_freq_khz = center_freq_khz + (bw_khz / 2);

	if (start_freq_khz >= freq_range->start_freq_khz &&
	    end_freq_khz <= freq_range->end_freq_khz)
		return true;

	return false;
}

int cfg80211_sinfo_alloc_tid_stats(struct station_info *sinfo, gfp_t gfp)
{
	sinfo->pertid = kcalloc(IEEE80211_NUM_TIDS + 1,
				sizeof(*(sinfo->pertid)),
				gfp);
	if (!sinfo->pertid)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(cfg80211_sinfo_alloc_tid_stats);

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
const unsigned char rfc1042_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
EXPORT_SYMBOL(rfc1042_header);

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
EXPORT_SYMBOL(bridge_tunnel_header);

/* Layer 2 Update frame (802.2 Type 1 LLC XID Update response) */
struct iapp_layer2_update {
	u8 da[ETH_ALEN];	/* broadcast */
	u8 sa[ETH_ALEN];	/* STA addr */
	__be16 len;		/* 6 */
	u8 dsap;		/* 0 */
	u8 ssap;		/* 0 */
	u8 control;
	u8 xid_info[3];
} __packed;

void cfg80211_send_layer2_update(struct net_device *dev, const u8 *addr)
{
	struct iapp_layer2_update *msg;
	struct sk_buff *skb;

	/* Send Level 2 Update Frame to update forwarding tables in layer 2
	 * bridge devices */

	skb = dev_alloc_skb(sizeof(*msg));
	if (!skb)
		return;
	msg = skb_put(skb, sizeof(*msg));

	/* 802.2 Type 1 Logical Link Control (LLC) Exchange Identifier (XID)
	 * Update response frame; IEEE Std 802.2-1998, 5.4.1.2.1 */

	eth_broadcast_addr(msg->da);
	ether_addr_copy(msg->sa, addr);
	msg->len = htons(6);
	msg->dsap = 0;
	msg->ssap = 0x01;	/* NULL LSAP, CR Bit: Response */
	msg->control = 0xaf;	/* XID response lsb.1111F101.
				 * F=0 (no poll command; unsolicited frame) */
	msg->xid_info[0] = 0x81;	/* XID format identifier */
	msg->xid_info[1] = 1;	/* LLC types/classes: Type 1 LLC */
	msg->xid_info[2] = 0;	/* XID sender's receive window size (RW) */

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}
EXPORT_SYMBOL(cfg80211_send_layer2_update);

int ieee80211_get_vht_max_nss(struct ieee80211_vht_cap *cap,
			      enum ieee80211_vht_chanwidth bw,
			      int mcs, bool ext_nss_bw_capable,
			      unsigned int max_vht_nss)
{
	u16 map = le16_to_cpu(cap->supp_mcs.rx_mcs_map);
	int ext_nss_bw;
	int supp_width;
	int i, mcs_encoding;

	if (map == 0xffff)
		return 0;

	if (WARN_ON(mcs > 9 || max_vht_nss > 8))
		return 0;
	if (mcs <= 7)
		mcs_encoding = 0;
	else if (mcs == 8)
		mcs_encoding = 1;
	else
		mcs_encoding = 2;

	if (!max_vht_nss) {
		/* find max_vht_nss for the given MCS */
		for (i = 7; i >= 0; i--) {
			int supp = (map >> (2 * i)) & 3;

			if (supp == 3)
				continue;

			if (supp >= mcs_encoding) {
				max_vht_nss = i + 1;
				break;
			}
		}
	}

	if (!(cap->supp_mcs.tx_mcs_map &
			cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE)))
		return max_vht_nss;

	ext_nss_bw = le32_get_bits(cap->vht_cap_info,
				   IEEE80211_VHT_CAP_EXT_NSS_BW_MASK);
	supp_width = le32_get_bits(cap->vht_cap_info,
				   IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK);

	/* if not capable, treat ext_nss_bw as 0 */
	if (!ext_nss_bw_capable)
		ext_nss_bw = 0;

	/* This is invalid */
	if (supp_width == 3)
		return 0;

	/* This is an invalid combination so pretend nothing is supported */
	if (supp_width == 2 && (ext_nss_bw == 1 || ext_nss_bw == 2))
		return 0;

	/*
	 * Cover all the special cases according to IEEE 802.11-2016
	 * Table 9-250. All other cases are either factor of 1 or not
	 * valid/supported.
	 */
	switch (bw) {
	case IEEE80211_VHT_CHANWIDTH_USE_HT:
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		if ((supp_width == 1 || supp_width == 2) &&
		    ext_nss_bw == 3)
			return 2 * max_vht_nss;
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		if (supp_width == 0 &&
		    (ext_nss_bw == 1 || ext_nss_bw == 2))
			return max_vht_nss / 2;
		if (supp_width == 0 &&
		    ext_nss_bw == 3)
			return (3 * max_vht_nss) / 4;
		if (supp_width == 1 &&
		    ext_nss_bw == 3)
			return 2 * max_vht_nss;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		if (supp_width == 0 && ext_nss_bw == 1)
			return 0; /* not possible */
		if (supp_width == 0 &&
		    ext_nss_bw == 2)
			return max_vht_nss / 2;
		if (supp_width == 0 &&
		    ext_nss_bw == 3)
			return (3 * max_vht_nss) / 4;
		if (supp_width == 1 &&
		    ext_nss_bw == 0)
			return 0; /* not possible */
		if (supp_width == 1 &&
		    ext_nss_bw == 1)
			return max_vht_nss / 2;
		if (supp_width == 1 &&
		    ext_nss_bw == 2)
			return (3 * max_vht_nss) / 4;
		break;
	}

	/* not covered or invalid combination received */
	return max_vht_nss;
}
EXPORT_SYMBOL(ieee80211_get_vht_max_nss);

bool cfg80211_iftype_allowed(struct wiphy *wiphy, enum nl80211_iftype iftype,
			     bool is_4addr, u8 check_swif)

{
	bool is_vlan = iftype == NL80211_IFTYPE_AP_VLAN;

	switch (check_swif) {
	case 0:
		if (is_vlan && is_4addr)
			return wiphy->flags & WIPHY_FLAG_4ADDR_AP;
		return wiphy->interface_modes & BIT(iftype);
	case 1:
		if (!(wiphy->software_iftypes & BIT(iftype)) && is_vlan)
			return wiphy->flags & WIPHY_FLAG_4ADDR_AP;
		return wiphy->software_iftypes & BIT(iftype);
	default:
		break;
	}

	return false;
}
EXPORT_SYMBOL(cfg80211_iftype_allowed);

void cfg80211_remove_link(struct wireless_dev *wdev, unsigned int link_id)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	ASSERT_WDEV_LOCK(wdev);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		__cfg80211_stop_ap(rdev, wdev->netdev, link_id, true);
		break;
	default:
		/* per-link not relevant */
		break;
	}

	wdev->valid_links &= ~BIT(link_id);

	rdev_del_intf_link(rdev, wdev, link_id);

	eth_zero_addr(wdev->links[link_id].addr);
}

void cfg80211_remove_links(struct wireless_dev *wdev)
{
	unsigned int link_id;

	wdev_lock(wdev);
	if (wdev->valid_links) {
		for_each_valid_link(wdev, link_id)
			cfg80211_remove_link(wdev, link_id);
	}
	wdev_unlock(wdev);
}

int cfg80211_remove_virtual_intf(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev)
{
	cfg80211_remove_links(wdev);

	return rdev_del_virtual_intf(rdev, wdev);
}

const struct wiphy_iftype_ext_capab *
cfg80211_get_iftype_ext_capa(struct wiphy *wiphy, enum nl80211_iftype type)
{
	int i;

	for (i = 0; i < wiphy->num_iftype_ext_capab; i++) {
		if (wiphy->iftype_ext_capab[i].iftype == type)
			return &wiphy->iftype_ext_capab[i];
	}

	return NULL;
}
EXPORT_SYMBOL(cfg80211_get_iftype_ext_capa);
