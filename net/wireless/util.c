/*
 * Wireless utility functions
 *
 * Copyright 2007-2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 */
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <net/dsfield.h>
#include <linux/if_vlan.h>
#include <linux/mpls.h>
#include "core.h"
#include "rdev-ops.h"


struct ieee80211_rate *
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

	if (sband->band == IEEE80211_BAND_2GHZ) {
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

int ieee80211_channel_to_frequency(int chan, enum ieee80211_band band)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	 * there are overlapping channel numbers in 5GHz and 2GHz bands */
	if (chan <= 0)
		return 0; /* not supported */
	switch (band) {
	case IEEE80211_BAND_2GHZ:
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
		break;
	case IEEE80211_BAND_5GHZ:
		if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;
		else
			return 5000 + chan * 5;
		break;
	case IEEE80211_BAND_60GHZ:
		if (chan < 5)
			return 56160 + chan * 2160;
		break;
	default:
		;
	}
	return 0; /* not supported */
}
EXPORT_SYMBOL(ieee80211_channel_to_frequency);

int ieee80211_frequency_to_channel(int freq)
{
	/* see 802.11 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}
EXPORT_SYMBOL(ieee80211_frequency_to_channel);

struct ieee80211_channel *__ieee80211_get_channel(struct wiphy *wiphy,
						  int freq)
{
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	int i;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];

		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels; i++) {
			if (sband->channels[i].center_freq == freq)
				return &sband->channels[i];
		}
	}

	return NULL;
}
EXPORT_SYMBOL(__ieee80211_get_channel);

static void set_mandatory_flags_band(struct ieee80211_supported_band *sband,
				     enum ieee80211_band band)
{
	int i, want;

	switch (band) {
	case IEEE80211_BAND_5GHZ:
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
	case IEEE80211_BAND_2GHZ:
		want = 7;
		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == 10) {
				sband->bitrates[i].flags |=
					IEEE80211_RATE_MANDATORY_B |
					IEEE80211_RATE_MANDATORY_G;
				want--;
			}

			if (sband->bitrates[i].bitrate == 20 ||
			    sband->bitrates[i].bitrate == 55 ||
			    sband->bitrates[i].bitrate == 110 ||
			    sband->bitrates[i].bitrate == 60 ||
			    sband->bitrates[i].bitrate == 120 ||
			    sband->bitrates[i].bitrate == 240) {
				sband->bitrates[i].flags |=
					IEEE80211_RATE_MANDATORY_G;
				want--;
			}

			if (sband->bitrates[i].bitrate != 10 &&
			    sband->bitrates[i].bitrate != 20 &&
			    sband->bitrates[i].bitrate != 55 &&
			    sband->bitrates[i].bitrate != 110)
				sband->bitrates[i].flags |=
					IEEE80211_RATE_ERP_G;
		}
		WARN_ON(want != 0 && want != 3 && want != 6);
		break;
	case IEEE80211_BAND_60GHZ:
		/* check for mandatory HT MCS 1..4 */
		WARN_ON(!sband->ht_cap.ht_supported);
		WARN_ON((sband->ht_cap.mcs.rx_mask[0] & 0x1e) != 0x1e);
		break;
	case IEEE80211_NUM_BANDS:
		WARN_ON(1);
		break;
	}
}

void ieee80211_set_bitrate_flags(struct wiphy *wiphy)
{
	enum ieee80211_band band;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++)
		if (wiphy->bands[band])
			set_mandatory_flags_band(wiphy->bands[band], band);
}

bool cfg80211_supported_cipher_suite(struct wiphy *wiphy, u32 cipher)
{
	int i;
	for (i = 0; i < wiphy->n_cipher_suites; i++)
		if (cipher == wiphy->cipher_suites[i])
			return true;
	return false;
}

int cfg80211_validate_key_settings(struct cfg80211_registered_device *rdev,
				   struct key_params *params, int key_idx,
				   bool pairwise, const u8 *mac_addr)
{
	if (key_idx > 5)
		return -EINVAL;

	if (!pairwise && mac_addr && !(rdev->wiphy.flags & WIPHY_FLAG_IBSS_RSN))
		return -EINVAL;

	if (pairwise && !mac_addr)
		return -EINVAL;

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		/* Disallow pairwise keys with non-zero index unless it's WEP
		 * or a vendor specific cipher (because current deployments use
		 * pairwise WEP keys with non-zero indices and for vendor
		 * specific ciphers this should be validated in the driver or
		 * hardware level - but 802.11i clearly specifies to use zero)
		 */
		if (pairwise && key_idx)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		/* Disallow BIP (group-only) cipher as pairwise cipher */
		if (pairwise)
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

unsigned int ieee80211_get_mesh_hdrlen(struct ieee80211s_hdr *meshhdr)
{
	int ae = meshhdr->flags & MESH_FLAGS_AE;
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
EXPORT_SYMBOL(ieee80211_get_mesh_hdrlen);

int ieee80211_data_to_8023(struct sk_buff *skb, const u8 *addr,
			   enum nl80211_iftype iftype)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 hdrlen, ethertype;
	u8 *payload;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN] __aligned(2);

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return -1;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	/* convert IEEE 802.11 header + possible LLC headers into Ethernet
	 * header
	 * IEEE 802.11 address fields:
	 * ToDS FromDS Addr1 Addr2 Addr3 Addr4
	 *   0     0   DA    SA    BSSID n/a
	 *   0     1   DA    BSSID SA    n/a
	 *   1     0   BSSID SA    DA    n/a
	 *   1     1   RA    TA    DA    SA
	 */
	memcpy(dst, ieee80211_get_DA(hdr), ETH_ALEN);
	memcpy(src, ieee80211_get_SA(hdr), ETH_ALEN);

	switch (hdr->frame_control &
		cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
	case cpu_to_le16(IEEE80211_FCTL_TODS):
		if (unlikely(iftype != NL80211_IFTYPE_AP &&
			     iftype != NL80211_IFTYPE_AP_VLAN &&
			     iftype != NL80211_IFTYPE_P2P_GO))
			return -1;
		break;
	case cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		if (unlikely(iftype != NL80211_IFTYPE_WDS &&
			     iftype != NL80211_IFTYPE_MESH_POINT &&
			     iftype != NL80211_IFTYPE_AP_VLAN &&
			     iftype != NL80211_IFTYPE_STATION))
			return -1;
		if (iftype == NL80211_IFTYPE_MESH_POINT) {
			struct ieee80211s_hdr *meshdr =
				(struct ieee80211s_hdr *) (skb->data + hdrlen);
			/* make sure meshdr->flags is on the linear part */
			if (!pskb_may_pull(skb, hdrlen + 1))
				return -1;
			if (meshdr->flags & MESH_FLAGS_AE_A4)
				return -1;
			if (meshdr->flags & MESH_FLAGS_AE_A5_A6) {
				skb_copy_bits(skb, hdrlen +
					offsetof(struct ieee80211s_hdr, eaddr1),
				       	dst, ETH_ALEN);
				skb_copy_bits(skb, hdrlen +
					offsetof(struct ieee80211s_hdr, eaddr2),
				        src, ETH_ALEN);
			}
			hdrlen += ieee80211_get_mesh_hdrlen(meshdr);
		}
		break;
	case cpu_to_le16(IEEE80211_FCTL_FROMDS):
		if ((iftype != NL80211_IFTYPE_STATION &&
		     iftype != NL80211_IFTYPE_P2P_CLIENT &&
		     iftype != NL80211_IFTYPE_MESH_POINT) ||
		    (is_multicast_ether_addr(dst) &&
		     ether_addr_equal(src, addr)))
			return -1;
		if (iftype == NL80211_IFTYPE_MESH_POINT) {
			struct ieee80211s_hdr *meshdr =
				(struct ieee80211s_hdr *) (skb->data + hdrlen);
			/* make sure meshdr->flags is on the linear part */
			if (!pskb_may_pull(skb, hdrlen + 1))
				return -1;
			if (meshdr->flags & MESH_FLAGS_AE_A5_A6)
				return -1;
			if (meshdr->flags & MESH_FLAGS_AE_A4)
				skb_copy_bits(skb, hdrlen +
					offsetof(struct ieee80211s_hdr, eaddr1),
					src, ETH_ALEN);
			hdrlen += ieee80211_get_mesh_hdrlen(meshdr);
		}
		break;
	case cpu_to_le16(0):
		if (iftype != NL80211_IFTYPE_ADHOC &&
		    iftype != NL80211_IFTYPE_STATION &&
		    iftype != NL80211_IFTYPE_OCB)
				return -1;
		break;
	}

	if (!pskb_may_pull(skb, hdrlen + 8))
		return -1;

	payload = skb->data + hdrlen;
	ethertype = (payload[6] << 8) | payload[7];

	if (likely((ether_addr_equal(payload, rfc1042_header) &&
		    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
		   ether_addr_equal(payload, bridge_tunnel_header))) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and
		 * replace EtherType */
		skb_pull(skb, hdrlen + 6);
		memcpy(skb_push(skb, ETH_ALEN), src, ETH_ALEN);
		memcpy(skb_push(skb, ETH_ALEN), dst, ETH_ALEN);
	} else {
		struct ethhdr *ehdr;
		__be16 len;

		skb_pull(skb, hdrlen);
		len = htons(skb->len);
		ehdr = (struct ethhdr *) skb_push(skb, sizeof(struct ethhdr));
		memcpy(ehdr->h_dest, dst, ETH_ALEN);
		memcpy(ehdr->h_source, src, ETH_ALEN);
		ehdr->h_proto = len;
	}
	return 0;
}
EXPORT_SYMBOL(ieee80211_data_to_8023);

int ieee80211_data_from_8023(struct sk_buff *skb, const u8 *addr,
			     enum nl80211_iftype iftype,
			     const u8 *bssid, bool qos)
{
	struct ieee80211_hdr hdr;
	u16 hdrlen, ethertype;
	__le16 fc;
	const u8 *encaps_data;
	int encaps_len, skip_header_bytes;
	int nh_pos, h_pos;
	int head_need;

	if (unlikely(skb->len < ETH_HLEN))
		return -EINVAL;

	nh_pos = skb_network_header(skb) - skb->data;
	h_pos = skb_transport_header(skb) - skb->data;

	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	switch (iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		fc |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, addr, ETH_ALEN);
		memcpy(hdr.addr3, skb->data + ETH_ALEN, ETH_ALEN);
		hdrlen = 24;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		fc |= cpu_to_le16(IEEE80211_FCTL_TODS);
		/* BSSID SA DA */
		memcpy(hdr.addr1, bssid, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, skb->data, ETH_ALEN);
		hdrlen = 24;
		break;
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		memcpy(hdr.addr1, skb->data, ETH_ALEN);
		memcpy(hdr.addr2, skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr.addr3, bssid, ETH_ALEN);
		hdrlen = 24;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (qos) {
		fc |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		hdrlen += 2;
	}

	hdr.frame_control = fc;
	hdr.duration_id = 0;
	hdr.seq_ctrl = 0;

	skip_header_bytes = ETH_HLEN;
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		skip_header_bytes -= 2;
	} else if (ethertype >= ETH_P_802_3_MIN) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		skip_header_bytes -= 2;
	} else {
		encaps_data = NULL;
		encaps_len = 0;
	}

	skb_pull(skb, skip_header_bytes);
	nh_pos -= skip_header_bytes;
	h_pos -= skip_header_bytes;

	head_need = hdrlen + encaps_len - skb_headroom(skb);

	if (head_need > 0 || skb_cloned(skb)) {
		head_need = max(head_need, 0);
		if (head_need)
			skb_orphan(skb);

		if (pskb_expand_head(skb, head_need, 0, GFP_ATOMIC))
			return -ENOMEM;

		skb->truesize += head_need;
	}

	if (encaps_data) {
		memcpy(skb_push(skb, encaps_len), encaps_data, encaps_len);
		nh_pos += encaps_len;
		h_pos += encaps_len;
	}

	memcpy(skb_push(skb, hdrlen), &hdr, hdrlen);

	nh_pos += hdrlen;
	h_pos += hdrlen;

	/* Update skb pointers to various headers since this modified frame
	 * is going to go through Linux networking code that may potentially
	 * need things like pointer to IP header. */
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, nh_pos);
	skb_set_transport_header(skb, h_pos);

	return 0;
}
EXPORT_SYMBOL(ieee80211_data_from_8023);


void ieee80211_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			      const u8 *addr, enum nl80211_iftype iftype,
			      const unsigned int extra_headroom,
			      bool has_80211_header)
{
	struct sk_buff *frame = NULL;
	u16 ethertype;
	u8 *payload;
	const struct ethhdr *eth;
	int remaining, err;
	u8 dst[ETH_ALEN], src[ETH_ALEN];

	if (has_80211_header) {
		err = ieee80211_data_to_8023(skb, addr, iftype);
		if (err)
			goto out;

		/* skip the wrapping header */
		eth = (struct ethhdr *) skb_pull(skb, sizeof(struct ethhdr));
		if (!eth)
			goto out;
	} else {
		eth = (struct ethhdr *) skb->data;
	}

	while (skb != frame) {
		u8 padding;
		__be16 len = eth->h_proto;
		unsigned int subframe_len = sizeof(struct ethhdr) + ntohs(len);

		remaining = skb->len;
		memcpy(dst, eth->h_dest, ETH_ALEN);
		memcpy(src, eth->h_source, ETH_ALEN);

		padding = (4 - subframe_len) & 0x3;
		/* the last MSDU has no padding */
		if (subframe_len > remaining)
			goto purge;

		skb_pull(skb, sizeof(struct ethhdr));
		/* reuse skb for the last subframe */
		if (remaining <= subframe_len + padding)
			frame = skb;
		else {
			unsigned int hlen = ALIGN(extra_headroom, 4);
			/*
			 * Allocate and reserve two bytes more for payload
			 * alignment since sizeof(struct ethhdr) is 14.
			 */
			frame = dev_alloc_skb(hlen + subframe_len + 2);
			if (!frame)
				goto purge;

			skb_reserve(frame, hlen + sizeof(struct ethhdr) + 2);
			memcpy(skb_put(frame, ntohs(len)), skb->data,
				ntohs(len));

			eth = (struct ethhdr *)skb_pull(skb, ntohs(len) +
							padding);
			if (!eth) {
				dev_kfree_skb(frame);
				goto purge;
			}
		}

		skb_reset_network_header(frame);
		frame->dev = skb->dev;
		frame->priority = skb->priority;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];

		if (likely((ether_addr_equal(payload, rfc1042_header) &&
			    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
			   ether_addr_equal(payload, bridge_tunnel_header))) {
			/* remove RFC1042 or Bridge-Tunnel
			 * encapsulation and replace EtherType */
			skb_pull(frame, 6);
			memcpy(skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		} else {
			memcpy(skb_push(frame, sizeof(__be16)), &len,
				sizeof(__be16));
			memcpy(skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		}
		__skb_queue_tail(list, frame);
	}

	return;

 purge:
	__skb_queue_purge(list);
 out:
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_amsdu_to_8023s);

/* Given a data frame determine the 802.1p/1d tag to use. */
unsigned int cfg80211_classify8021d(struct sk_buff *skb,
				    struct cfg80211_qos_map *qos_map)
{
	unsigned int dscp;
	unsigned char vlan_priority;

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263)
		return skb->priority - 256;

	if (skb_vlan_tag_present(skb)) {
		vlan_priority = (skb_vlan_tag_get(skb) & VLAN_PRIO_MASK)
			>> VLAN_PRIO_SHIFT;
		if (vlan_priority > 0)
			return vlan_priority;
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

		return (ntohl(mpls->entry) & MPLS_LS_TC_MASK)
			>> MPLS_LS_TC_SHIFT;
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
			if (tmp_dscp == qos_map->dscp_exception[i].dscp)
				return qos_map->dscp_exception[i].up;
		}

		for (i = 0; i < 8; i++) {
			if (tmp_dscp >= qos_map->up[i].low &&
			    tmp_dscp <= qos_map->up[i].high)
				return i;
		}
	}

	return dscp >> 5;
}
EXPORT_SYMBOL(cfg80211_classify8021d);

const u8 *ieee80211_bss_get_ie(struct cfg80211_bss *bss, u8 ie)
{
	const struct cfg80211_bss_ies *ies;

	ies = rcu_dereference(bss->ies);
	if (!ies)
		return NULL;

	return cfg80211_find_ie(ie, ies->data, ies->len);
}
EXPORT_SYMBOL(ieee80211_bss_get_ie);

void cfg80211_upload_connect_keys(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct net_device *dev = wdev->netdev;
	int i;

	if (!wdev->connect_keys)
		return;

	for (i = 0; i < 6; i++) {
		if (!wdev->connect_keys->params[i].cipher)
			continue;
		if (rdev_add_key(rdev, dev, i, false, NULL,
				 &wdev->connect_keys->params[i])) {
			netdev_err(dev, "failed to set key %d\n", i);
			continue;
		}
		if (wdev->connect_keys->def == i)
			if (rdev_set_default_key(rdev, dev, i, true, true)) {
				netdev_err(dev, "failed to set defkey %d\n", i);
				continue;
			}
		if (wdev->connect_keys->defmgmt == i)
			if (rdev_set_default_mgmt_key(rdev, dev, i))
				netdev_err(dev, "failed to set mgtdef %d\n", i);
	}

	kzfree(wdev->connect_keys);
	wdev->connect_keys = NULL;
}

void cfg80211_process_wdev_events(struct wireless_dev *wdev)
{
	struct cfg80211_event *ev;
	unsigned long flags;
	const u8 *bssid = NULL;

	spin_lock_irqsave(&wdev->event_lock, flags);
	while (!list_empty(&wdev->event_list)) {
		ev = list_first_entry(&wdev->event_list,
				      struct cfg80211_event, list);
		list_del(&ev->list);
		spin_unlock_irqrestore(&wdev->event_lock, flags);

		wdev_lock(wdev);
		switch (ev->type) {
		case EVENT_CONNECT_RESULT:
			if (!is_zero_ether_addr(ev->cr.bssid))
				bssid = ev->cr.bssid;
			__cfg80211_connect_result(
				wdev->netdev, bssid,
				ev->cr.req_ie, ev->cr.req_ie_len,
				ev->cr.resp_ie, ev->cr.resp_ie_len,
				ev->cr.status,
				ev->cr.status == WLAN_STATUS_SUCCESS,
				NULL);
			break;
		case EVENT_ROAMED:
			__cfg80211_roamed(wdev, ev->rm.bss, ev->rm.req_ie,
					  ev->rm.req_ie_len, ev->rm.resp_ie,
					  ev->rm.resp_ie_len);
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

	ASSERT_RTNL();

	list_for_each_entry(wdev, &rdev->wdev_list, list)
		cfg80211_process_wdev_events(wdev);
}

int cfg80211_change_iface(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, enum nl80211_iftype ntype,
			  u32 *flags, struct vif_params *params)
{
	int err;
	enum nl80211_iftype otype = dev->ieee80211_ptr->iftype;

	ASSERT_RTNL();

	/* don't support changing VLANs, you just re-create them */
	if (otype == NL80211_IFTYPE_AP_VLAN)
		return -EOPNOTSUPP;

	/* cannot change into P2P device type */
	if (ntype == NL80211_IFTYPE_P2P_DEVICE)
		return -EOPNOTSUPP;

	if (!rdev->ops->change_virtual_intf ||
	    !(rdev->wiphy.interface_modes & (1 << ntype)))
		return -EOPNOTSUPP;

	/* if it's part of a bridge, reject changing type to station/ibss */
	if ((dev->priv_flags & IFF_BRIDGE_PORT) &&
	    (ntype == NL80211_IFTYPE_ADHOC ||
	     ntype == NL80211_IFTYPE_STATION ||
	     ntype == NL80211_IFTYPE_P2P_CLIENT))
		return -EBUSY;

	if (ntype != otype) {
		dev->ieee80211_ptr->use_4addr = false;
		dev->ieee80211_ptr->mesh_id_up_len = 0;
		wdev_lock(dev->ieee80211_ptr);
		rdev_set_qos_map(rdev, dev, NULL);
		wdev_unlock(dev->ieee80211_ptr);

		switch (otype) {
		case NL80211_IFTYPE_AP:
			cfg80211_stop_ap(rdev, dev, true);
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
		default:
			break;
		}

		cfg80211_process_rdev_events(rdev);
	}

	err = rdev_change_virtual_intf(rdev, dev, ntype, flags, params);

	WARN_ON(!err && dev->ieee80211_ptr->iftype != ntype);

	if (!err && params && params->use_4addr != -1)
		dev->ieee80211_ptr->use_4addr = params->use_4addr;

	if (!err) {
		dev->priv_flags &= ~IFF_DONT_BRIDGE;
		switch (ntype) {
		case NL80211_IFTYPE_STATION:
			if (dev->ieee80211_ptr->use_4addr)
				break;
			/* fall through */
		case NL80211_IFTYPE_OCB:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_ADHOC:
			dev->priv_flags |= IFF_DONT_BRIDGE;
			break;
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_WDS:
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

static u32 cfg80211_calculate_bitrate_60g(struct rate_info *rate)
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

static u32 cfg80211_calculate_bitrate_vht(struct rate_info *rate)
{
	static const u32 base[4][10] = {
		{   6500000,
		   13000000,
		   19500000,
		   26000000,
		   39000000,
		   52000000,
		   58500000,
		   65000000,
		   78000000,
		   0,
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
		},
	};
	u32 bitrate;
	int idx;

	if (WARN_ON_ONCE(rate->mcs > 9))
		return 0;

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
		WARN_ON(1);
		/* fall through */
	case RATE_INFO_BW_20:
		idx = 0;
	}

	bitrate = base[idx][rate->mcs];
	bitrate *= rate->nss;

	if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
}

u32 cfg80211_calculate_bitrate(struct rate_info *rate)
{
	int modulation, streams, bitrate;

	if (!(rate->flags & RATE_INFO_FLAGS_MCS) &&
	    !(rate->flags & RATE_INFO_FLAGS_VHT_MCS))
		return rate->legacy;
	if (rate->flags & RATE_INFO_FLAGS_60G)
		return cfg80211_calculate_bitrate_60g(rate);
	if (rate->flags & RATE_INFO_FLAGS_VHT_MCS)
		return cfg80211_calculate_bitrate_vht(rate);

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

static bool ieee80211_id_in_list(const u8 *ids, int n_ids, u8 id)
{
	int i;

	for (i = 0; i < n_ids; i++)
		if (ids[i] == id)
			return true;
	return false;
}

size_t ieee80211_ie_split_ric(const u8 *ies, size_t ielen,
			      const u8 *ids, int n_ids,
			      const u8 *after_ric, int n_after_ric,
			      size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ieee80211_id_in_list(ids, n_ids, ies[pos])) {
		if (ies[pos] == WLAN_EID_RIC_DATA && n_after_ric) {
			pos += 2 + ies[pos + 1];

			while (pos < ielen &&
			       !ieee80211_id_in_list(after_ric, n_after_ric,
						     ies[pos]))
				pos += 2 + ies[pos + 1];
		} else {
			pos += 2 + ies[pos + 1];
		}
	}

	return pos;
}
EXPORT_SYMBOL(ieee80211_ie_split_ric);

bool ieee80211_operating_class_to_band(u8 operating_class,
				       enum ieee80211_band *band)
{
	switch (operating_class) {
	case 112:
	case 115 ... 127:
	case 128 ... 130:
		*band = IEEE80211_BAND_5GHZ;
		return true;
	case 81:
	case 82:
	case 83:
	case 84:
		*band = IEEE80211_BAND_2GHZ;
		return true;
	case 180:
		*band = IEEE80211_BAND_60GHZ;
		return true;
	}

	return false;
}
EXPORT_SYMBOL(ieee80211_operating_class_to_band);

bool ieee80211_chandef_to_operating_class(struct cfg80211_chan_def *chandef,
					  u8 *op_class)
{
	u8 vht_opclass;
	u16 freq = chandef->center_freq1;

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
		if (chandef->width > NL80211_CHAN_WIDTH_40)
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
	if (freq >= 56160 + 2160 * 1 && freq <= 56160 + 2160 * 4) {
		if (chandef->width >= NL80211_CHAN_WIDTH_40)
			return false;

		*op_class = 180;
		return true;
	}

	/* not supported yet */
	return false;
}
EXPORT_SYMBOL(ieee80211_chandef_to_operating_class);

int cfg80211_validate_beacon_int(struct cfg80211_registered_device *rdev,
				 u32 beacon_int)
{
	struct wireless_dev *wdev;
	int res = 0;

	if (!beacon_int)
		return -EINVAL;

	list_for_each_entry(wdev, &rdev->wdev_list, list) {
		if (!wdev->beacon_interval)
			continue;
		if (wdev->beacon_interval != beacon_int) {
			res = -EINVAL;
			break;
		}
	}

	return res;
}

int cfg80211_iter_combinations(struct wiphy *wiphy,
			       const int num_different_channels,
			       const u8 radar_detect,
			       const int iftype_num[NUM_NL80211_IFTYPES],
			       void (*iter)(const struct ieee80211_iface_combination *c,
					    void *data),
			       void *data)
{
	const struct ieee80211_regdomain *regdom;
	enum nl80211_dfs_regions region = 0;
	int i, j, iftype;
	int num_interfaces = 0;
	u32 used_iftypes = 0;

	if (radar_detect) {
		rcu_read_lock();
		regdom = rcu_dereference(cfg80211_regdomain);
		if (regdom)
			region = regdom->dfs_region;
		rcu_read_unlock();
	}

	for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
		num_interfaces += iftype_num[iftype];
		if (iftype_num[iftype] > 0 &&
		    !(wiphy->software_iftypes & BIT(iftype)))
			used_iftypes |= BIT(iftype);
	}

	for (i = 0; i < wiphy->n_iface_combinations; i++) {
		const struct ieee80211_iface_combination *c;
		struct ieee80211_iface_limit *limits;
		u32 all_iftypes = 0;

		c = &wiphy->iface_combinations[i];

		if (num_interfaces > c->max_interfaces)
			continue;
		if (num_different_channels > c->num_different_channels)
			continue;

		limits = kmemdup(c->limits, sizeof(limits[0]) * c->n_limits,
				 GFP_KERNEL);
		if (!limits)
			return -ENOMEM;

		for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
			if (wiphy->software_iftypes & BIT(iftype))
				continue;
			for (j = 0; j < c->n_limits; j++) {
				all_iftypes |= limits[j].types;
				if (!(limits[j].types & BIT(iftype)))
					continue;
				if (limits[j].max < iftype_num[iftype])
					goto cont;
				limits[j].max -= iftype_num[iftype];
			}
		}

		if (radar_detect != (c->radar_detect_widths & radar_detect))
			goto cont;

		if (radar_detect && c->radar_detect_regions &&
		    !(c->radar_detect_regions & BIT(region)))
			goto cont;

		/* Finally check that all iftypes that we're currently
		 * using are actually part of this combination. If they
		 * aren't then we can't use this combination and have
		 * to continue to the next.
		 */
		if ((all_iftypes & used_iftypes) != used_iftypes)
			goto cont;

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
				const int num_different_channels,
				const u8 radar_detect,
				const int iftype_num[NUM_NL80211_IFTYPES])
{
	int err, num = 0;

	err = cfg80211_iter_combinations(wiphy, num_different_channels,
					 radar_detect, iftype_num,
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
	enum ieee80211_band band;
	unsigned int n_channels = 0;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++)
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

	return rdev_get_station(rdev, dev, mac_addr, sinfo);
}
EXPORT_SYMBOL(cfg80211_get_station);

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
const unsigned char rfc1042_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
EXPORT_SYMBOL(rfc1042_header);

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
EXPORT_SYMBOL(bridge_tunnel_header);
