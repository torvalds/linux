/*
 * Wireless utility functions
 *
 * Copyright 2007-2009	Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/export.h>
#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <net/dsfield.h>
#include "core.h"

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

	/*
	 * Disallow pairwise keys with non-zero index unless it's WEP
	 * or a vendor specific cipher (because current deployments use
	 * pairwise WEP keys with non-zero indices and for vendor specific
	 * ciphers this should be validated in the driver or hardware level
	 * - but 802.11i clearly specifies to use zero)
	 */
	if (pairwise && key_idx &&
	    ((params->cipher == WLAN_CIPHER_SUITE_TKIP) ||
	     (params->cipher == WLAN_CIPHER_SUITE_CCMP) ||
	     (params->cipher == WLAN_CIPHER_SUITE_AES_CMAC)))
		return -EINVAL;

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
	case WLAN_CIPHER_SUITE_WEP104:
		if (params->key_len != WLAN_KEY_LEN_WEP104)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (params->key_len != WLAN_KEY_LEN_AES_CMAC)
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
		case WLAN_CIPHER_SUITE_AES_CMAC:
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
		    iftype != NL80211_IFTYPE_STATION)
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
			     enum nl80211_iftype iftype, u8 *bssid, bool qos)
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
	} else if (ethertype > 0x600) {
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
unsigned int cfg80211_classify8021d(struct sk_buff *skb)
{
	unsigned int dscp;

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (skb->priority >= 256 && skb->priority <= 263)
		return skb->priority - 256;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		dscp = ipv4_get_dsfield(ip_hdr(skb)) & 0xfc;
		break;
	case htons(ETH_P_IPV6):
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) & 0xfc;
		break;
	default:
		return 0;
	}

	return dscp >> 5;
}
EXPORT_SYMBOL(cfg80211_classify8021d);

const u8 *ieee80211_bss_get_ie(struct cfg80211_bss *bss, u8 ie)
{
	if (bss->information_elements == NULL)
		return NULL;
	return cfg80211_find_ie(ie, bss->information_elements,
				 bss->len_information_elements);
}
EXPORT_SYMBOL(ieee80211_bss_get_ie);

void cfg80211_upload_connect_keys(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct net_device *dev = wdev->netdev;
	int i;

	if (!wdev->connect_keys)
		return;

	for (i = 0; i < 6; i++) {
		if (!wdev->connect_keys->params[i].cipher)
			continue;
		if (rdev->ops->add_key(wdev->wiphy, dev, i, false, NULL,
					&wdev->connect_keys->params[i])) {
			netdev_err(dev, "failed to set key %d\n", i);
			continue;
		}
		if (wdev->connect_keys->def == i)
			if (rdev->ops->set_default_key(wdev->wiphy, dev,
						       i, true, true)) {
				netdev_err(dev, "failed to set defkey %d\n", i);
				continue;
			}
		if (wdev->connect_keys->defmgmt == i)
			if (rdev->ops->set_default_mgmt_key(wdev->wiphy, dev, i))
				netdev_err(dev, "failed to set mgtdef %d\n", i);
	}

	kfree(wdev->connect_keys);
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
						ev->dc.reason, true);
			break;
		case EVENT_IBSS_JOINED:
			__cfg80211_ibss_joined(wdev->netdev, ev->ij.bssid);
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
	ASSERT_RDEV_LOCK(rdev);

	mutex_lock(&rdev->devlist_mtx);

	list_for_each_entry(wdev, &rdev->wdev_list, list)
		cfg80211_process_wdev_events(wdev);

	mutex_unlock(&rdev->devlist_mtx);
}

int cfg80211_change_iface(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, enum nl80211_iftype ntype,
			  u32 *flags, struct vif_params *params)
{
	int err;
	enum nl80211_iftype otype = dev->ieee80211_ptr->iftype;

	ASSERT_RDEV_LOCK(rdev);

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

	if (ntype != otype && netif_running(dev)) {
		mutex_lock(&rdev->devlist_mtx);
		err = cfg80211_can_change_interface(rdev, dev->ieee80211_ptr,
						    ntype);
		mutex_unlock(&rdev->devlist_mtx);
		if (err)
			return err;

		dev->ieee80211_ptr->use_4addr = false;
		dev->ieee80211_ptr->mesh_id_up_len = 0;

		switch (otype) {
		case NL80211_IFTYPE_AP:
			cfg80211_stop_ap(rdev, dev);
			break;
		case NL80211_IFTYPE_ADHOC:
			cfg80211_leave_ibss(rdev, dev, false);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			cfg80211_disconnect(rdev, dev,
					    WLAN_REASON_DEAUTH_LEAVING, true);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			/* mesh should be handled? */
			break;
		default:
			break;
		}

		cfg80211_process_rdev_events(rdev);
	}

	err = rdev->ops->change_virtual_intf(&rdev->wiphy, dev,
					     ntype, flags, params);

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

u32 cfg80211_calculate_bitrate(struct rate_info *rate)
{
	int modulation, streams, bitrate;

	if (!(rate->flags & RATE_INFO_FLAGS_MCS))
		return rate->legacy;
	if (rate->flags & RATE_INFO_FLAGS_60G)
		return cfg80211_calculate_bitrate_60g(rate);

	/* the formula below does only work for MCS values smaller than 32 */
	if (WARN_ON_ONCE(rate->mcs >= 32))
		return 0;

	modulation = rate->mcs & 7;
	streams = (rate->mcs >> 3) + 1;

	bitrate = (rate->flags & RATE_INFO_FLAGS_40_MHZ_WIDTH) ?
			13500000 : 6500000;

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

int cfg80211_validate_beacon_int(struct cfg80211_registered_device *rdev,
				 u32 beacon_int)
{
	struct wireless_dev *wdev;
	int res = 0;

	if (!beacon_int)
		return -EINVAL;

	mutex_lock(&rdev->devlist_mtx);

	list_for_each_entry(wdev, &rdev->wdev_list, list) {
		if (!wdev->beacon_interval)
			continue;
		if (wdev->beacon_interval != beacon_int) {
			res = -EINVAL;
			break;
		}
	}

	mutex_unlock(&rdev->devlist_mtx);

	return res;
}

int cfg80211_can_use_iftype_chan(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev,
				 enum nl80211_iftype iftype,
				 struct ieee80211_channel *chan,
				 enum cfg80211_chan_mode chanmode)
{
	struct wireless_dev *wdev_iter;
	u32 used_iftypes = BIT(iftype);
	int num[NUM_NL80211_IFTYPES];
	struct ieee80211_channel
			*used_channels[CFG80211_MAX_NUM_DIFFERENT_CHANNELS];
	struct ieee80211_channel *ch;
	enum cfg80211_chan_mode chmode;
	int num_different_channels = 0;
	int total = 1;
	int i, j;

	ASSERT_RTNL();
	lockdep_assert_held(&rdev->devlist_mtx);

	/* Always allow software iftypes */
	if (rdev->wiphy.software_iftypes & BIT(iftype))
		return 0;

	memset(num, 0, sizeof(num));
	memset(used_channels, 0, sizeof(used_channels));

	num[iftype] = 1;

	switch (chanmode) {
	case CHAN_MODE_UNDEFINED:
		break;
	case CHAN_MODE_SHARED:
		WARN_ON(!chan);
		used_channels[0] = chan;
		num_different_channels++;
		break;
	case CHAN_MODE_EXCLUSIVE:
		num_different_channels++;
		break;
	}

	list_for_each_entry(wdev_iter, &rdev->wdev_list, list) {
		if (wdev_iter == wdev)
			continue;
		if (wdev_iter->netdev) {
			if (!netif_running(wdev_iter->netdev))
				continue;
		} else if (wdev_iter->iftype == NL80211_IFTYPE_P2P_DEVICE) {
			if (!wdev_iter->p2p_started)
				continue;
		} else {
			WARN_ON(1);
		}

		if (rdev->wiphy.software_iftypes & BIT(wdev_iter->iftype))
			continue;

		/*
		 * We may be holding the "wdev" mutex, but now need to lock
		 * wdev_iter. This is OK because once we get here wdev_iter
		 * is not wdev (tested above), but we need to use the nested
		 * locking for lockdep.
		 */
		mutex_lock_nested(&wdev_iter->mtx, 1);
		__acquire(wdev_iter->mtx);
		cfg80211_get_chan_state(wdev_iter, &ch, &chmode);
		wdev_unlock(wdev_iter);

		switch (chmode) {
		case CHAN_MODE_UNDEFINED:
			break;
		case CHAN_MODE_SHARED:
			for (i = 0; i < CFG80211_MAX_NUM_DIFFERENT_CHANNELS; i++)
				if (!used_channels[i] || used_channels[i] == ch)
					break;

			if (i == CFG80211_MAX_NUM_DIFFERENT_CHANNELS)
				return -EBUSY;

			if (used_channels[i] == NULL) {
				used_channels[i] = ch;
				num_different_channels++;
			}
			break;
		case CHAN_MODE_EXCLUSIVE:
			num_different_channels++;
			break;
		}

		num[wdev_iter->iftype]++;
		total++;
		used_iftypes |= BIT(wdev_iter->iftype);
	}

	if (total == 1)
		return 0;

	for (i = 0; i < rdev->wiphy.n_iface_combinations; i++) {
		const struct ieee80211_iface_combination *c;
		struct ieee80211_iface_limit *limits;
		u32 all_iftypes = 0;

		c = &rdev->wiphy.iface_combinations[i];

		if (total > c->max_interfaces)
			continue;
		if (num_different_channels > c->num_different_channels)
			continue;

		limits = kmemdup(c->limits, sizeof(limits[0]) * c->n_limits,
				 GFP_KERNEL);
		if (!limits)
			return -ENOMEM;

		for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
			if (rdev->wiphy.software_iftypes & BIT(iftype))
				continue;
			for (j = 0; j < c->n_limits; j++) {
				all_iftypes |= limits[j].types;
				if (!(limits[j].types & BIT(iftype)))
					continue;
				if (limits[j].max < num[iftype])
					goto cont;
				limits[j].max -= num[iftype];
			}
		}

		/*
		 * Finally check that all iftypes that we're currently
		 * using are actually part of this combination. If they
		 * aren't then we can't use this combination and have
		 * to continue to the next.
		 */
		if ((all_iftypes & used_iftypes) != used_iftypes)
			goto cont;

		/*
		 * This combination covered all interface types and
		 * supported the requested numbers, so we're good.
		 */
		kfree(limits);
		return 0;
 cont:
		kfree(limits);
	}

	return -EBUSY;
}

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

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
const unsigned char rfc1042_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
EXPORT_SYMBOL(rfc1042_header);

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
EXPORT_SYMBOL(bridge_tunnel_header);
