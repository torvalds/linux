/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/bitops.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "led.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "tkip.h"
#include "wme.h"
#include "rate.h"

static inline void ieee80211_rx_stats(struct net_device *dev, u32 len)
{
	struct pcpu_sw_netstats *tstats = this_cpu_ptr(dev->tstats);

	u64_stats_update_begin(&tstats->syncp);
	tstats->rx_packets++;
	tstats->rx_bytes += len;
	u64_stats_update_end(&tstats->syncp);
}

static u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			       enum nl80211_iftype type)
{
	__le16 fc = hdr->frame_control;

	if (ieee80211_is_data(fc)) {
		if (len < 24) /* drop incorrect hdr len (data) */
			return NULL;

		if (ieee80211_has_a4(fc))
			return NULL;
		if (ieee80211_has_tods(fc))
			return hdr->addr1;
		if (ieee80211_has_fromds(fc))
			return hdr->addr2;

		return hdr->addr3;
	}

	if (ieee80211_is_mgmt(fc)) {
		if (len < 24) /* drop incorrect hdr len (mgmt) */
			return NULL;
		return hdr->addr3;
	}

	if (ieee80211_is_ctl(fc)) {
		if (ieee80211_is_pspoll(fc))
			return hdr->addr1;

		if (ieee80211_is_back_req(fc)) {
			switch (type) {
			case NL80211_IFTYPE_STATION:
				return hdr->addr2;
			case NL80211_IFTYPE_AP:
			case NL80211_IFTYPE_AP_VLAN:
				return hdr->addr1;
			default:
				break; /* fall through to the return */
			}
		}
	}

	return NULL;
}

/*
 * monitor mode reception
 *
 * This function cleans up the SKB, i.e. it removes all the stuff
 * only useful for monitoring.
 */
static struct sk_buff *remove_monitor_info(struct ieee80211_local *local,
					   struct sk_buff *skb,
					   unsigned int rtap_vendor_space)
{
	if (ieee80211_hw_check(&local->hw, RX_INCLUDES_FCS)) {
		if (likely(skb->len > FCS_LEN))
			__pskb_trim(skb, skb->len - FCS_LEN);
		else {
			/* driver bug */
			WARN_ON(1);
			dev_kfree_skb(skb);
			return NULL;
		}
	}

	__pskb_pull(skb, rtap_vendor_space);

	return skb;
}

static inline bool should_drop_frame(struct sk_buff *skb, int present_fcs_len,
				     unsigned int rtap_vendor_space)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr;

	hdr = (void *)(skb->data + rtap_vendor_space);

	if (status->flag & (RX_FLAG_FAILED_FCS_CRC |
			    RX_FLAG_FAILED_PLCP_CRC |
			    RX_FLAG_ONLY_MONITOR))
		return true;

	if (unlikely(skb->len < 16 + present_fcs_len + rtap_vendor_space))
		return true;

	if (ieee80211_is_ctl(hdr->frame_control) &&
	    !ieee80211_is_pspoll(hdr->frame_control) &&
	    !ieee80211_is_back_req(hdr->frame_control))
		return true;

	return false;
}

static int
ieee80211_rx_radiotap_hdrlen(struct ieee80211_local *local,
			     struct ieee80211_rx_status *status,
			     struct sk_buff *skb)
{
	int len;

	/* always present fields */
	len = sizeof(struct ieee80211_radiotap_header) + 8;

	/* allocate extra bitmaps */
	if (status->chains)
		len += 4 * hweight8(status->chains);

	if (ieee80211_have_rx_timestamp(status)) {
		len = ALIGN(len, 8);
		len += 8;
	}
	if (ieee80211_hw_check(&local->hw, SIGNAL_DBM))
		len += 1;

	/* antenna field, if we don't have per-chain info */
	if (!status->chains)
		len += 1;

	/* padding for RX_FLAGS if necessary */
	len = ALIGN(len, 2);

	if (status->flag & RX_FLAG_HT) /* HT info */
		len += 3;

	if (status->flag & RX_FLAG_AMPDU_DETAILS) {
		len = ALIGN(len, 4);
		len += 8;
	}

	if (status->flag & RX_FLAG_VHT) {
		len = ALIGN(len, 2);
		len += 12;
	}

	if (local->hw.radiotap_timestamp.units_pos >= 0) {
		len = ALIGN(len, 8);
		len += 12;
	}

	if (status->chains) {
		/* antenna and antenna signal fields */
		len += 2 * hweight8(status->chains);
	}

	if (status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA) {
		struct ieee80211_vendor_radiotap *rtap = (void *)skb->data;

		/* vendor presence bitmap */
		len += 4;
		/* alignment for fixed 6-byte vendor data header */
		len = ALIGN(len, 2);
		/* vendor data header */
		len += 6;
		if (WARN_ON(rtap->align == 0))
			rtap->align = 1;
		len = ALIGN(len, rtap->align);
		len += rtap->len + rtap->pad;
	}

	return len;
}

/*
 * ieee80211_add_rx_radiotap_header - add radiotap header
 *
 * add a radiotap header containing all the fields which the hardware provided.
 */
static void
ieee80211_add_rx_radiotap_header(struct ieee80211_local *local,
				 struct sk_buff *skb,
				 struct ieee80211_rate *rate,
				 int rtap_len, bool has_fcs)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_header *rthdr;
	unsigned char *pos;
	__le32 *it_present;
	u32 it_present_val;
	u16 rx_flags = 0;
	u16 channel_flags = 0;
	int mpdulen, chain;
	unsigned long chains = status->chains;
	struct ieee80211_vendor_radiotap rtap = {};

	if (status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA) {
		rtap = *(struct ieee80211_vendor_radiotap *)skb->data;
		/* rtap.len and rtap.pad are undone immediately */
		skb_pull(skb, sizeof(rtap) + rtap.len + rtap.pad);
	}

	mpdulen = skb->len;
	if (!(has_fcs && ieee80211_hw_check(&local->hw, RX_INCLUDES_FCS)))
		mpdulen += FCS_LEN;

	rthdr = (struct ieee80211_radiotap_header *)skb_push(skb, rtap_len);
	memset(rthdr, 0, rtap_len - rtap.len - rtap.pad);
	it_present = &rthdr->it_present;

	/* radiotap header, set always present flags */
	rthdr->it_len = cpu_to_le16(rtap_len);
	it_present_val = BIT(IEEE80211_RADIOTAP_FLAGS) |
			 BIT(IEEE80211_RADIOTAP_CHANNEL) |
			 BIT(IEEE80211_RADIOTAP_RX_FLAGS);

	if (!status->chains)
		it_present_val |= BIT(IEEE80211_RADIOTAP_ANTENNA);

	for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
		it_present_val |=
			BIT(IEEE80211_RADIOTAP_EXT) |
			BIT(IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE);
		put_unaligned_le32(it_present_val, it_present);
		it_present++;
		it_present_val = BIT(IEEE80211_RADIOTAP_ANTENNA) |
				 BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
	}

	if (status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA) {
		it_present_val |= BIT(IEEE80211_RADIOTAP_VENDOR_NAMESPACE) |
				  BIT(IEEE80211_RADIOTAP_EXT);
		put_unaligned_le32(it_present_val, it_present);
		it_present++;
		it_present_val = rtap.present;
	}

	put_unaligned_le32(it_present_val, it_present);

	pos = (void *)(it_present + 1);

	/* the order of the following fields is important */

	/* IEEE80211_RADIOTAP_TSFT */
	if (ieee80211_have_rx_timestamp(status)) {
		/* padding */
		while ((pos - (u8 *)rthdr) & 7)
			*pos++ = 0;
		put_unaligned_le64(
			ieee80211_calculate_rx_timestamp(local, status,
							 mpdulen, 0),
			pos);
		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_TSFT);
		pos += 8;
	}

	/* IEEE80211_RADIOTAP_FLAGS */
	if (has_fcs && ieee80211_hw_check(&local->hw, RX_INCLUDES_FCS))
		*pos |= IEEE80211_RADIOTAP_F_FCS;
	if (status->flag & (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC))
		*pos |= IEEE80211_RADIOTAP_F_BADFCS;
	if (status->flag & RX_FLAG_SHORTPRE)
		*pos |= IEEE80211_RADIOTAP_F_SHORTPRE;
	pos++;

	/* IEEE80211_RADIOTAP_RATE */
	if (!rate || status->flag & (RX_FLAG_HT | RX_FLAG_VHT)) {
		/*
		 * Without rate information don't add it. If we have,
		 * MCS information is a separate field in radiotap,
		 * added below. The byte here is needed as padding
		 * for the channel though, so initialise it to 0.
		 */
		*pos = 0;
	} else {
		int shift = 0;
		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_RATE);
		if (status->flag & RX_FLAG_10MHZ)
			shift = 1;
		else if (status->flag & RX_FLAG_5MHZ)
			shift = 2;
		*pos = DIV_ROUND_UP(rate->bitrate, 5 * (1 << shift));
	}
	pos++;

	/* IEEE80211_RADIOTAP_CHANNEL */
	put_unaligned_le16(status->freq, pos);
	pos += 2;
	if (status->flag & RX_FLAG_10MHZ)
		channel_flags |= IEEE80211_CHAN_HALF;
	else if (status->flag & RX_FLAG_5MHZ)
		channel_flags |= IEEE80211_CHAN_QUARTER;

	if (status->band == NL80211_BAND_5GHZ)
		channel_flags |= IEEE80211_CHAN_OFDM | IEEE80211_CHAN_5GHZ;
	else if (status->flag & (RX_FLAG_HT | RX_FLAG_VHT))
		channel_flags |= IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	else if (rate && rate->flags & IEEE80211_RATE_ERP_G)
		channel_flags |= IEEE80211_CHAN_OFDM | IEEE80211_CHAN_2GHZ;
	else if (rate)
		channel_flags |= IEEE80211_CHAN_CCK | IEEE80211_CHAN_2GHZ;
	else
		channel_flags |= IEEE80211_CHAN_2GHZ;
	put_unaligned_le16(channel_flags, pos);
	pos += 2;

	/* IEEE80211_RADIOTAP_DBM_ANTSIGNAL */
	if (ieee80211_hw_check(&local->hw, SIGNAL_DBM) &&
	    !(status->flag & RX_FLAG_NO_SIGNAL_VAL)) {
		*pos = status->signal;
		rthdr->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
		pos++;
	}

	/* IEEE80211_RADIOTAP_LOCK_QUALITY is missing */

	if (!status->chains) {
		/* IEEE80211_RADIOTAP_ANTENNA */
		*pos = status->antenna;
		pos++;
	}

	/* IEEE80211_RADIOTAP_DB_ANTNOISE is not used */

	/* IEEE80211_RADIOTAP_RX_FLAGS */
	/* ensure 2 byte alignment for the 2 byte field as required */
	if ((pos - (u8 *)rthdr) & 1)
		*pos++ = 0;
	if (status->flag & RX_FLAG_FAILED_PLCP_CRC)
		rx_flags |= IEEE80211_RADIOTAP_F_RX_BADPLCP;
	put_unaligned_le16(rx_flags, pos);
	pos += 2;

	if (status->flag & RX_FLAG_HT) {
		unsigned int stbc;

		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_MCS);
		*pos++ = local->hw.radiotap_mcs_details;
		*pos = 0;
		if (status->flag & RX_FLAG_SHORT_GI)
			*pos |= IEEE80211_RADIOTAP_MCS_SGI;
		if (status->flag & RX_FLAG_40MHZ)
			*pos |= IEEE80211_RADIOTAP_MCS_BW_40;
		if (status->flag & RX_FLAG_HT_GF)
			*pos |= IEEE80211_RADIOTAP_MCS_FMT_GF;
		if (status->flag & RX_FLAG_LDPC)
			*pos |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
		stbc = (status->flag & RX_FLAG_STBC_MASK) >> RX_FLAG_STBC_SHIFT;
		*pos |= stbc << IEEE80211_RADIOTAP_MCS_STBC_SHIFT;
		pos++;
		*pos++ = status->rate_idx;
	}

	if (status->flag & RX_FLAG_AMPDU_DETAILS) {
		u16 flags = 0;

		/* ensure 4 byte alignment */
		while ((pos - (u8 *)rthdr) & 3)
			pos++;
		rthdr->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_AMPDU_STATUS);
		put_unaligned_le32(status->ampdu_reference, pos);
		pos += 4;
		if (status->flag & RX_FLAG_AMPDU_LAST_KNOWN)
			flags |= IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN;
		if (status->flag & RX_FLAG_AMPDU_IS_LAST)
			flags |= IEEE80211_RADIOTAP_AMPDU_IS_LAST;
		if (status->flag & RX_FLAG_AMPDU_DELIM_CRC_ERROR)
			flags |= IEEE80211_RADIOTAP_AMPDU_DELIM_CRC_ERR;
		if (status->flag & RX_FLAG_AMPDU_DELIM_CRC_KNOWN)
			flags |= IEEE80211_RADIOTAP_AMPDU_DELIM_CRC_KNOWN;
		put_unaligned_le16(flags, pos);
		pos += 2;
		if (status->flag & RX_FLAG_AMPDU_DELIM_CRC_KNOWN)
			*pos++ = status->ampdu_delimiter_crc;
		else
			*pos++ = 0;
		*pos++ = 0;
	}

	if (status->flag & RX_FLAG_VHT) {
		u16 known = local->hw.radiotap_vht_details;

		rthdr->it_present |= cpu_to_le32(1 << IEEE80211_RADIOTAP_VHT);
		put_unaligned_le16(known, pos);
		pos += 2;
		/* flags */
		if (status->flag & RX_FLAG_SHORT_GI)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
		/* in VHT, STBC is binary */
		if (status->flag & RX_FLAG_STBC_MASK)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
		if (status->vht_flag & RX_VHT_FLAG_BF)
			*pos |= IEEE80211_RADIOTAP_VHT_FLAG_BEAMFORMED;
		pos++;
		/* bandwidth */
		if (status->vht_flag & RX_VHT_FLAG_80MHZ)
			*pos++ = 4;
		else if (status->vht_flag & RX_VHT_FLAG_160MHZ)
			*pos++ = 11;
		else if (status->flag & RX_FLAG_40MHZ)
			*pos++ = 1;
		else /* 20 MHz */
			*pos++ = 0;
		/* MCS/NSS */
		*pos = (status->rate_idx << 4) | status->vht_nss;
		pos += 4;
		/* coding field */
		if (status->flag & RX_FLAG_LDPC)
			*pos |= IEEE80211_RADIOTAP_CODING_LDPC_USER0;
		pos++;
		/* group ID */
		pos++;
		/* partial_aid */
		pos += 2;
	}

	if (local->hw.radiotap_timestamp.units_pos >= 0) {
		u16 accuracy = 0;
		u8 flags = IEEE80211_RADIOTAP_TIMESTAMP_FLAG_32BIT;

		rthdr->it_present |=
			cpu_to_le32(1 << IEEE80211_RADIOTAP_TIMESTAMP);

		/* ensure 8 byte alignment */
		while ((pos - (u8 *)rthdr) & 7)
			pos++;

		put_unaligned_le64(status->device_timestamp, pos);
		pos += sizeof(u64);

		if (local->hw.radiotap_timestamp.accuracy >= 0) {
			accuracy = local->hw.radiotap_timestamp.accuracy;
			flags |= IEEE80211_RADIOTAP_TIMESTAMP_FLAG_ACCURACY;
		}
		put_unaligned_le16(accuracy, pos);
		pos += sizeof(u16);

		*pos++ = local->hw.radiotap_timestamp.units_pos;
		*pos++ = flags;
	}

	for_each_set_bit(chain, &chains, IEEE80211_MAX_CHAINS) {
		*pos++ = status->chain_signal[chain];
		*pos++ = chain;
	}

	if (status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA) {
		/* ensure 2 byte alignment for the vendor field as required */
		if ((pos - (u8 *)rthdr) & 1)
			*pos++ = 0;
		*pos++ = rtap.oui[0];
		*pos++ = rtap.oui[1];
		*pos++ = rtap.oui[2];
		*pos++ = rtap.subns;
		put_unaligned_le16(rtap.len, pos);
		pos += 2;
		/* align the actual payload as requested */
		while ((pos - (u8 *)rthdr) & (rtap.align - 1))
			*pos++ = 0;
		/* data (and possible padding) already follows */
	}
}

/*
 * This function copies a received frame to all monitor interfaces and
 * returns a cleaned-up SKB that no longer includes the FCS nor the
 * radiotap header the driver might have added.
 */
static struct sk_buff *
ieee80211_rx_monitor(struct ieee80211_local *local, struct sk_buff *origskb,
		     struct ieee80211_rate *rate)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(origskb);
	struct ieee80211_sub_if_data *sdata;
	int rt_hdrlen, needed_headroom;
	struct sk_buff *skb, *skb2;
	struct net_device *prev_dev = NULL;
	int present_fcs_len = 0;
	unsigned int rtap_vendor_space = 0;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_sub_if_data *monitor_sdata =
		rcu_dereference(local->monitor_sdata);

	if (unlikely(status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA)) {
		struct ieee80211_vendor_radiotap *rtap = (void *)origskb->data;

		rtap_vendor_space = sizeof(*rtap) + rtap->len + rtap->pad;
	}

	/*
	 * First, we may need to make a copy of the skb because
	 *  (1) we need to modify it for radiotap (if not present), and
	 *  (2) the other RX handlers will modify the skb we got.
	 *
	 * We don't need to, of course, if we aren't going to return
	 * the SKB because it has a bad FCS/PLCP checksum.
	 */

	if (ieee80211_hw_check(&local->hw, RX_INCLUDES_FCS))
		present_fcs_len = FCS_LEN;

	/* ensure hdr->frame_control and vendor radiotap data are in skb head */
	if (!pskb_may_pull(origskb, 2 + rtap_vendor_space)) {
		dev_kfree_skb(origskb);
		return NULL;
	}

	if (!local->monitors || (status->flag & RX_FLAG_SKIP_MONITOR)) {
		if (should_drop_frame(origskb, present_fcs_len,
				      rtap_vendor_space)) {
			dev_kfree_skb(origskb);
			return NULL;
		}

		return remove_monitor_info(local, origskb, rtap_vendor_space);
	}

	/* room for the radiotap header based on driver features */
	rt_hdrlen = ieee80211_rx_radiotap_hdrlen(local, status, origskb);
	needed_headroom = rt_hdrlen - rtap_vendor_space;

	if (should_drop_frame(origskb, present_fcs_len, rtap_vendor_space)) {
		/* only need to expand headroom if necessary */
		skb = origskb;
		origskb = NULL;

		/*
		 * This shouldn't trigger often because most devices have an
		 * RX header they pull before we get here, and that should
		 * be big enough for our radiotap information. We should
		 * probably export the length to drivers so that we can have
		 * them allocate enough headroom to start with.
		 */
		if (skb_headroom(skb) < needed_headroom &&
		    pskb_expand_head(skb, needed_headroom, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return NULL;
		}
	} else {
		/*
		 * Need to make a copy and possibly remove radiotap header
		 * and FCS from the original.
		 */
		skb = skb_copy_expand(origskb, needed_headroom, 0, GFP_ATOMIC);

		origskb = remove_monitor_info(local, origskb,
					      rtap_vendor_space);

		if (!skb)
			return origskb;
	}

	/* prepend radiotap information */
	ieee80211_add_rx_radiotap_header(local, skb, rate, rt_hdrlen, true);

	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
			continue;

		if (sdata->u.mntr.flags & MONITOR_FLAG_COOK_FRAMES)
			continue;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (prev_dev) {
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2) {
				skb2->dev = prev_dev;
				netif_receive_skb(skb2);
			}
		}

		prev_dev = sdata->dev;
		ieee80211_rx_stats(sdata->dev, skb->len);
	}

	mgmt = (void *)skb->data;
	if (monitor_sdata &&
	    skb->len >= IEEE80211_MIN_ACTION_SIZE + 1 + VHT_MUMIMO_GROUPS_DATA_LEN &&
	    ieee80211_is_action(mgmt->frame_control) &&
	    mgmt->u.action.category == WLAN_CATEGORY_VHT &&
	    mgmt->u.action.u.vht_group_notif.action_code == WLAN_VHT_ACTION_GROUPID_MGMT &&
	    is_valid_ether_addr(monitor_sdata->u.mntr.mu_follow_addr) &&
	    ether_addr_equal(mgmt->da, monitor_sdata->u.mntr.mu_follow_addr)) {
		struct sk_buff *mu_skb = skb_copy(skb, GFP_ATOMIC);

		if (mu_skb) {
			mu_skb->pkt_type = IEEE80211_SDATA_QUEUE_TYPE_FRAME;
			skb_queue_tail(&monitor_sdata->skb_queue, mu_skb);
			ieee80211_queue_work(&local->hw, &monitor_sdata->work);
		}
	}

	if (prev_dev) {
		skb->dev = prev_dev;
		netif_receive_skb(skb);
	} else
		dev_kfree_skb(skb);

	return origskb;
}

static void ieee80211_parse_qos(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	int tid, seqno_idx, security_idx;

	/* does the frame have a qos control field? */
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		/* frame has qos control */
		tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
		if (*qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT)
			status->rx_flags |= IEEE80211_RX_AMSDU;

		seqno_idx = tid;
		security_idx = tid;
	} else {
		/*
		 * IEEE 802.11-2007, 7.1.3.4.1 ("Sequence Number field"):
		 *
		 *	Sequence numbers for management frames, QoS data
		 *	frames with a broadcast/multicast address in the
		 *	Address 1 field, and all non-QoS data frames sent
		 *	by QoS STAs are assigned using an additional single
		 *	modulo-4096 counter, [...]
		 *
		 * We also use that counter for non-QoS STAs.
		 */
		seqno_idx = IEEE80211_NUM_TIDS;
		security_idx = 0;
		if (ieee80211_is_mgmt(hdr->frame_control))
			security_idx = IEEE80211_NUM_TIDS;
		tid = 0;
	}

	rx->seqno_idx = seqno_idx;
	rx->security_idx = security_idx;
	/* Set skb->priority to 1d tag if highest order bit of TID is not set.
	 * For now, set skb->priority to 0 for other cases. */
	rx->skb->priority = (tid > 7) ? 0 : tid;
}

/**
 * DOC: Packet alignment
 *
 * Drivers always need to pass packets that are aligned to two-byte boundaries
 * to the stack.
 *
 * Additionally, should, if possible, align the payload data in a way that
 * guarantees that the contained IP header is aligned to a four-byte
 * boundary. In the case of regular frames, this simply means aligning the
 * payload to a four-byte boundary (because either the IP header is directly
 * contained, or IV/RFC1042 headers that have a length divisible by four are
 * in front of it).  If the payload data is not properly aligned and the
 * architecture doesn't support efficient unaligned operations, mac80211
 * will align the data.
 *
 * With A-MSDU frames, however, the payload data address must yield two modulo
 * four because there are 14-byte 802.3 headers within the A-MSDU frames that
 * push the IP header further back to a multiple of four again. Thankfully, the
 * specs were sane enough this time around to require padding each A-MSDU
 * subframe to a length that is a multiple of four.
 *
 * Padding like Atheros hardware adds which is between the 802.11 header and
 * the payload is not supported, the driver is required to move the 802.11
 * header to be directly in front of the payload in that case.
 */
static void ieee80211_verify_alignment(struct ieee80211_rx_data *rx)
{
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	WARN_ON_ONCE((unsigned long)rx->skb->data & 1);
#endif
}


/* rx handlers */

static int ieee80211_is_unicast_robust_mgmt_frame(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (is_multicast_ether_addr(hdr->addr1))
		return 0;

	return ieee80211_is_robust_mgmt_frame(skb);
}


static int ieee80211_is_multicast_robust_mgmt_frame(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (!is_multicast_ether_addr(hdr->addr1))
		return 0;

	return ieee80211_is_robust_mgmt_frame(skb);
}


/* Get the BIP key index from MMIE; return -1 if this is not a BIP frame */
static int ieee80211_get_mmie_keyidx(struct sk_buff *skb)
{
	struct ieee80211_mgmt *hdr = (struct ieee80211_mgmt *) skb->data;
	struct ieee80211_mmie *mmie;
	struct ieee80211_mmie_16 *mmie16;

	if (skb->len < 24 + sizeof(*mmie) || !is_multicast_ether_addr(hdr->da))
		return -1;

	if (!ieee80211_is_robust_mgmt_frame(skb))
		return -1; /* not a robust management frame */

	mmie = (struct ieee80211_mmie *)
		(skb->data + skb->len - sizeof(*mmie));
	if (mmie->element_id == WLAN_EID_MMIE &&
	    mmie->length == sizeof(*mmie) - 2)
		return le16_to_cpu(mmie->key_id);

	mmie16 = (struct ieee80211_mmie_16 *)
		(skb->data + skb->len - sizeof(*mmie16));
	if (skb->len >= 24 + sizeof(*mmie16) &&
	    mmie16->element_id == WLAN_EID_MMIE &&
	    mmie16->length == sizeof(*mmie16) - 2)
		return le16_to_cpu(mmie16->key_id);

	return -1;
}

static int ieee80211_get_cs_keyid(const struct ieee80211_cipher_scheme *cs,
				  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc;
	int hdrlen;
	u8 keyid;

	fc = hdr->frame_control;
	hdrlen = ieee80211_hdrlen(fc);

	if (skb->len < hdrlen + cs->hdr_len)
		return -EINVAL;

	skb_copy_bits(skb, hdrlen + cs->key_idx_off, &keyid, 1);
	keyid &= cs->key_idx_mask;
	keyid >>= cs->key_idx_shift;

	return keyid;
}

static ieee80211_rx_result ieee80211_rx_mesh_check(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	char *dev_addr = rx->sdata->vif.addr;

	if (ieee80211_is_data(hdr->frame_control)) {
		if (is_multicast_ether_addr(hdr->addr1)) {
			if (ieee80211_has_tods(hdr->frame_control) ||
			    !ieee80211_has_fromds(hdr->frame_control))
				return RX_DROP_MONITOR;
			if (ether_addr_equal(hdr->addr3, dev_addr))
				return RX_DROP_MONITOR;
		} else {
			if (!ieee80211_has_a4(hdr->frame_control))
				return RX_DROP_MONITOR;
			if (ether_addr_equal(hdr->addr4, dev_addr))
				return RX_DROP_MONITOR;
		}
	}

	/* If there is not an established peer link and this is not a peer link
	 * establisment frame, beacon or probe, drop the frame.
	 */

	if (!rx->sta || sta_plink_state(rx->sta) != NL80211_PLINK_ESTAB) {
		struct ieee80211_mgmt *mgmt;

		if (!ieee80211_is_mgmt(hdr->frame_control))
			return RX_DROP_MONITOR;

		if (ieee80211_is_action(hdr->frame_control)) {
			u8 category;

			/* make sure category field is present */
			if (rx->skb->len < IEEE80211_MIN_ACTION_SIZE)
				return RX_DROP_MONITOR;

			mgmt = (struct ieee80211_mgmt *)hdr;
			category = mgmt->u.action.category;
			if (category != WLAN_CATEGORY_MESH_ACTION &&
			    category != WLAN_CATEGORY_SELF_PROTECTED)
				return RX_DROP_MONITOR;
			return RX_CONTINUE;
		}

		if (ieee80211_is_probe_req(hdr->frame_control) ||
		    ieee80211_is_probe_resp(hdr->frame_control) ||
		    ieee80211_is_beacon(hdr->frame_control) ||
		    ieee80211_is_auth(hdr->frame_control))
			return RX_CONTINUE;

		return RX_DROP_MONITOR;
	}

	return RX_CONTINUE;
}

static inline bool ieee80211_rx_reorder_ready(struct tid_ampdu_rx *tid_agg_rx,
					      int index)
{
	struct sk_buff_head *frames = &tid_agg_rx->reorder_buf[index];
	struct sk_buff *tail = skb_peek_tail(frames);
	struct ieee80211_rx_status *status;

	if (tid_agg_rx->reorder_buf_filtered & BIT_ULL(index))
		return true;

	if (!tail)
		return false;

	status = IEEE80211_SKB_RXCB(tail);
	if (status->flag & RX_FLAG_AMSDU_MORE)
		return false;

	return true;
}

static void ieee80211_release_reorder_frame(struct ieee80211_sub_if_data *sdata,
					    struct tid_ampdu_rx *tid_agg_rx,
					    int index,
					    struct sk_buff_head *frames)
{
	struct sk_buff_head *skb_list = &tid_agg_rx->reorder_buf[index];
	struct sk_buff *skb;
	struct ieee80211_rx_status *status;

	lockdep_assert_held(&tid_agg_rx->reorder_lock);

	if (skb_queue_empty(skb_list))
		goto no_frame;

	if (!ieee80211_rx_reorder_ready(tid_agg_rx, index)) {
		__skb_queue_purge(skb_list);
		goto no_frame;
	}

	/* release frames from the reorder ring buffer */
	tid_agg_rx->stored_mpdu_num--;
	while ((skb = __skb_dequeue(skb_list))) {
		status = IEEE80211_SKB_RXCB(skb);
		status->rx_flags |= IEEE80211_RX_DEFERRED_RELEASE;
		__skb_queue_tail(frames, skb);
	}

no_frame:
	tid_agg_rx->reorder_buf_filtered &= ~BIT_ULL(index);
	tid_agg_rx->head_seq_num = ieee80211_sn_inc(tid_agg_rx->head_seq_num);
}

static void ieee80211_release_reorder_frames(struct ieee80211_sub_if_data *sdata,
					     struct tid_ampdu_rx *tid_agg_rx,
					     u16 head_seq_num,
					     struct sk_buff_head *frames)
{
	int index;

	lockdep_assert_held(&tid_agg_rx->reorder_lock);

	while (ieee80211_sn_less(tid_agg_rx->head_seq_num, head_seq_num)) {
		index = tid_agg_rx->head_seq_num % tid_agg_rx->buf_size;
		ieee80211_release_reorder_frame(sdata, tid_agg_rx, index,
						frames);
	}
}

/*
 * Timeout (in jiffies) for skb's that are waiting in the RX reorder buffer. If
 * the skb was added to the buffer longer than this time ago, the earlier
 * frames that have not yet been received are assumed to be lost and the skb
 * can be released for processing. This may also release other skb's from the
 * reorder buffer if there are no additional gaps between the frames.
 *
 * Callers must hold tid_agg_rx->reorder_lock.
 */
#define HT_RX_REORDER_BUF_TIMEOUT (HZ / 10)

static void ieee80211_sta_reorder_release(struct ieee80211_sub_if_data *sdata,
					  struct tid_ampdu_rx *tid_agg_rx,
					  struct sk_buff_head *frames)
{
	int index, i, j;

	lockdep_assert_held(&tid_agg_rx->reorder_lock);

	/* release the buffer until next missing frame */
	index = tid_agg_rx->head_seq_num % tid_agg_rx->buf_size;
	if (!ieee80211_rx_reorder_ready(tid_agg_rx, index) &&
	    tid_agg_rx->stored_mpdu_num) {
		/*
		 * No buffers ready to be released, but check whether any
		 * frames in the reorder buffer have timed out.
		 */
		int skipped = 1;
		for (j = (index + 1) % tid_agg_rx->buf_size; j != index;
		     j = (j + 1) % tid_agg_rx->buf_size) {
			if (!ieee80211_rx_reorder_ready(tid_agg_rx, j)) {
				skipped++;
				continue;
			}
			if (skipped &&
			    !time_after(jiffies, tid_agg_rx->reorder_time[j] +
					HT_RX_REORDER_BUF_TIMEOUT))
				goto set_release_timer;

			/* don't leave incomplete A-MSDUs around */
			for (i = (index + 1) % tid_agg_rx->buf_size; i != j;
			     i = (i + 1) % tid_agg_rx->buf_size)
				__skb_queue_purge(&tid_agg_rx->reorder_buf[i]);

			ht_dbg_ratelimited(sdata,
					   "release an RX reorder frame due to timeout on earlier frames\n");
			ieee80211_release_reorder_frame(sdata, tid_agg_rx, j,
							frames);

			/*
			 * Increment the head seq# also for the skipped slots.
			 */
			tid_agg_rx->head_seq_num =
				(tid_agg_rx->head_seq_num +
				 skipped) & IEEE80211_SN_MASK;
			skipped = 0;
		}
	} else while (ieee80211_rx_reorder_ready(tid_agg_rx, index)) {
		ieee80211_release_reorder_frame(sdata, tid_agg_rx, index,
						frames);
		index =	tid_agg_rx->head_seq_num % tid_agg_rx->buf_size;
	}

	if (tid_agg_rx->stored_mpdu_num) {
		j = index = tid_agg_rx->head_seq_num % tid_agg_rx->buf_size;

		for (; j != (index - 1) % tid_agg_rx->buf_size;
		     j = (j + 1) % tid_agg_rx->buf_size) {
			if (ieee80211_rx_reorder_ready(tid_agg_rx, j))
				break;
		}

 set_release_timer:

		if (!tid_agg_rx->removed)
			mod_timer(&tid_agg_rx->reorder_timer,
				  tid_agg_rx->reorder_time[j] + 1 +
				  HT_RX_REORDER_BUF_TIMEOUT);
	} else {
		del_timer(&tid_agg_rx->reorder_timer);
	}
}

/*
 * As this function belongs to the RX path it must be under
 * rcu_read_lock protection. It returns false if the frame
 * can be processed immediately, true if it was consumed.
 */
static bool ieee80211_sta_manage_reorder_buf(struct ieee80211_sub_if_data *sdata,
					     struct tid_ampdu_rx *tid_agg_rx,
					     struct sk_buff *skb,
					     struct sk_buff_head *frames)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	u16 sc = le16_to_cpu(hdr->seq_ctrl);
	u16 mpdu_seq_num = (sc & IEEE80211_SCTL_SEQ) >> 4;
	u16 head_seq_num, buf_size;
	int index;
	bool ret = true;

	spin_lock(&tid_agg_rx->reorder_lock);

	/*
	 * Offloaded BA sessions have no known starting sequence number so pick
	 * one from first Rxed frame for this tid after BA was started.
	 */
	if (unlikely(tid_agg_rx->auto_seq)) {
		tid_agg_rx->auto_seq = false;
		tid_agg_rx->ssn = mpdu_seq_num;
		tid_agg_rx->head_seq_num = mpdu_seq_num;
	}

	buf_size = tid_agg_rx->buf_size;
	head_seq_num = tid_agg_rx->head_seq_num;

	/* frame with out of date sequence number */
	if (ieee80211_sn_less(mpdu_seq_num, head_seq_num)) {
		dev_kfree_skb(skb);
		goto out;
	}

	/*
	 * If frame the sequence number exceeds our buffering window
	 * size release some previous frames to make room for this one.
	 */
	if (!ieee80211_sn_less(mpdu_seq_num, head_seq_num + buf_size)) {
		head_seq_num = ieee80211_sn_inc(
				ieee80211_sn_sub(mpdu_seq_num, buf_size));
		/* release stored frames up to new head to stack */
		ieee80211_release_reorder_frames(sdata, tid_agg_rx,
						 head_seq_num, frames);
	}

	/* Now the new frame is always in the range of the reordering buffer */

	index = mpdu_seq_num % tid_agg_rx->buf_size;

	/* check if we already stored this frame */
	if (ieee80211_rx_reorder_ready(tid_agg_rx, index)) {
		dev_kfree_skb(skb);
		goto out;
	}

	/*
	 * If the current MPDU is in the right order and nothing else
	 * is stored we can process it directly, no need to buffer it.
	 * If it is first but there's something stored, we may be able
	 * to release frames after this one.
	 */
	if (mpdu_seq_num == tid_agg_rx->head_seq_num &&
	    tid_agg_rx->stored_mpdu_num == 0) {
		if (!(status->flag & RX_FLAG_AMSDU_MORE))
			tid_agg_rx->head_seq_num =
				ieee80211_sn_inc(tid_agg_rx->head_seq_num);
		ret = false;
		goto out;
	}

	/* put the frame in the reordering buffer */
	__skb_queue_tail(&tid_agg_rx->reorder_buf[index], skb);
	if (!(status->flag & RX_FLAG_AMSDU_MORE)) {
		tid_agg_rx->reorder_time[index] = jiffies;
		tid_agg_rx->stored_mpdu_num++;
		ieee80211_sta_reorder_release(sdata, tid_agg_rx, frames);
	}

 out:
	spin_unlock(&tid_agg_rx->reorder_lock);
	return ret;
}

/*
 * Reorder MPDUs from A-MPDUs, keeping them on a buffer. Returns
 * true if the MPDU was buffered, false if it should be processed.
 */
static void ieee80211_rx_reorder_ampdu(struct ieee80211_rx_data *rx,
				       struct sk_buff_head *frames)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_local *local = rx->local;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct sta_info *sta = rx->sta;
	struct tid_ampdu_rx *tid_agg_rx;
	u16 sc;
	u8 tid, ack_policy;

	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		goto dont_reorder;

	/*
	 * filter the QoS data rx stream according to
	 * STA/TID and check if this STA/TID is on aggregation
	 */

	if (!sta)
		goto dont_reorder;

	ack_policy = *ieee80211_get_qos_ctl(hdr) &
		     IEEE80211_QOS_CTL_ACK_POLICY_MASK;
	tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;

	tid_agg_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[tid]);
	if (!tid_agg_rx) {
		if (ack_policy == IEEE80211_QOS_CTL_ACK_POLICY_BLOCKACK &&
		    !test_bit(tid, rx->sta->ampdu_mlme.agg_session_valid) &&
		    !test_and_set_bit(tid, rx->sta->ampdu_mlme.unexpected_agg))
			ieee80211_send_delba(rx->sdata, rx->sta->sta.addr, tid,
					     WLAN_BACK_RECIPIENT,
					     WLAN_REASON_QSTA_REQUIRE_SETUP);
		goto dont_reorder;
	}

	/* qos null data frames are excluded */
	if (unlikely(hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_NULLFUNC)))
		goto dont_reorder;

	/* not part of a BA session */
	if (ack_policy != IEEE80211_QOS_CTL_ACK_POLICY_BLOCKACK &&
	    ack_policy != IEEE80211_QOS_CTL_ACK_POLICY_NORMAL)
		goto dont_reorder;

	/* new, potentially un-ordered, ampdu frame - process it */

	/* reset session timer */
	if (tid_agg_rx->timeout)
		tid_agg_rx->last_rx = jiffies;

	/* if this mpdu is fragmented - terminate rx aggregation session */
	sc = le16_to_cpu(hdr->seq_ctrl);
	if (sc & IEEE80211_SCTL_FRAG) {
		skb->pkt_type = IEEE80211_SDATA_QUEUE_TYPE_FRAME;
		skb_queue_tail(&rx->sdata->skb_queue, skb);
		ieee80211_queue_work(&local->hw, &rx->sdata->work);
		return;
	}

	/*
	 * No locking needed -- we will only ever process one
	 * RX packet at a time, and thus own tid_agg_rx. All
	 * other code manipulating it needs to (and does) make
	 * sure that we cannot get to it any more before doing
	 * anything with it.
	 */
	if (ieee80211_sta_manage_reorder_buf(rx->sdata, tid_agg_rx, skb,
					     frames))
		return;

 dont_reorder:
	__skb_queue_tail(frames, skb);
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_check_dup(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);

	if (status->flag & RX_FLAG_DUP_VALIDATED)
		return RX_CONTINUE;

	/*
	 * Drop duplicate 802.11 retransmissions
	 * (IEEE 802.11-2012: 9.3.2.10 "Duplicate detection and recovery")
	 */

	if (rx->skb->len < 24)
		return RX_CONTINUE;

	if (ieee80211_is_ctl(hdr->frame_control) ||
	    ieee80211_is_qos_nullfunc(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return RX_CONTINUE;

	if (!rx->sta)
		return RX_CONTINUE;

	if (unlikely(ieee80211_has_retry(hdr->frame_control) &&
		     rx->sta->last_seq_ctrl[rx->seqno_idx] == hdr->seq_ctrl)) {
		I802_DEBUG_INC(rx->local->dot11FrameDuplicateCount);
		rx->sta->rx_stats.num_duplicates++;
		return RX_DROP_UNUSABLE;
	} else if (!(status->flag & RX_FLAG_AMSDU_MORE)) {
		rx->sta->last_seq_ctrl[rx->seqno_idx] = hdr->seq_ctrl;
	}

	return RX_CONTINUE;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_check(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;

	/* Drop disallowed frame classes based on STA auth/assoc state;
	 * IEEE 802.11, Chap 5.5.
	 *
	 * mac80211 filters only based on association state, i.e. it drops
	 * Class 3 frames from not associated stations. hostapd sends
	 * deauth/disassoc frames when needed. In addition, hostapd is
	 * responsible for filtering on both auth and assoc states.
	 */

	if (ieee80211_vif_is_mesh(&rx->sdata->vif))
		return ieee80211_rx_mesh_check(rx);

	if (unlikely((ieee80211_is_data(hdr->frame_control) ||
		      ieee80211_is_pspoll(hdr->frame_control)) &&
		     rx->sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		     rx->sdata->vif.type != NL80211_IFTYPE_WDS &&
		     rx->sdata->vif.type != NL80211_IFTYPE_OCB &&
		     (!rx->sta || !test_sta_flag(rx->sta, WLAN_STA_ASSOC)))) {
		/*
		 * accept port control frames from the AP even when it's not
		 * yet marked ASSOC to prevent a race where we don't set the
		 * assoc bit quickly enough before it sends the first frame
		 */
		if (rx->sta && rx->sdata->vif.type == NL80211_IFTYPE_STATION &&
		    ieee80211_is_data_present(hdr->frame_control)) {
			unsigned int hdrlen;
			__be16 ethertype;

			hdrlen = ieee80211_hdrlen(hdr->frame_control);

			if (rx->skb->len < hdrlen + 8)
				return RX_DROP_MONITOR;

			skb_copy_bits(rx->skb, hdrlen + 6, &ethertype, 2);
			if (ethertype == rx->sdata->control_port_protocol)
				return RX_CONTINUE;
		}

		if (rx->sdata->vif.type == NL80211_IFTYPE_AP &&
		    cfg80211_rx_spurious_frame(rx->sdata->dev,
					       hdr->addr2,
					       GFP_ATOMIC))
			return RX_DROP_UNUSABLE;

		return RX_DROP_MONITOR;
	}

	return RX_CONTINUE;
}


static ieee80211_rx_result debug_noinline
ieee80211_rx_h_check_more_data(struct ieee80211_rx_data *rx)
{
	struct ieee80211_local *local;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;

	local = rx->local;
	skb = rx->skb;
	hdr = (struct ieee80211_hdr *) skb->data;

	if (!local->pspolling)
		return RX_CONTINUE;

	if (!ieee80211_has_fromds(hdr->frame_control))
		/* this is not from AP */
		return RX_CONTINUE;

	if (!ieee80211_is_data(hdr->frame_control))
		return RX_CONTINUE;

	if (!ieee80211_has_moredata(hdr->frame_control)) {
		/* AP has no more frames buffered for us */
		local->pspolling = false;
		return RX_CONTINUE;
	}

	/* more data bit is set, let's request a new frame from the AP */
	ieee80211_send_pspoll(local, rx->sdata);

	return RX_CONTINUE;
}

static void sta_ps_start(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ps_data *ps;
	int tid;

	if (sta->sdata->vif.type == NL80211_IFTYPE_AP ||
	    sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		ps = &sdata->bss->ps;
	else
		return;

	atomic_inc(&ps->num_sta_ps);
	set_sta_flag(sta, WLAN_STA_PS_STA);
	if (!ieee80211_hw_check(&local->hw, AP_LINK_PS))
		drv_sta_notify(local, sdata, STA_NOTIFY_SLEEP, &sta->sta);
	ps_dbg(sdata, "STA %pM aid %d enters power save mode\n",
	       sta->sta.addr, sta->sta.aid);

	ieee80211_clear_fast_xmit(sta);

	if (!sta->sta.txq[0])
		return;

	for (tid = 0; tid < ARRAY_SIZE(sta->sta.txq); tid++) {
		if (txq_has_queue(sta->sta.txq[tid]))
			set_bit(tid, &sta->txq_buffered_tids);
		else
			clear_bit(tid, &sta->txq_buffered_tids);
	}
}

static void sta_ps_end(struct sta_info *sta)
{
	ps_dbg(sta->sdata, "STA %pM aid %d exits power save mode\n",
	       sta->sta.addr, sta->sta.aid);

	if (test_sta_flag(sta, WLAN_STA_PS_DRIVER)) {
		/*
		 * Clear the flag only if the other one is still set
		 * so that the TX path won't start TX'ing new frames
		 * directly ... In the case that the driver flag isn't
		 * set ieee80211_sta_ps_deliver_wakeup() will clear it.
		 */
		clear_sta_flag(sta, WLAN_STA_PS_STA);
		ps_dbg(sta->sdata, "STA %pM aid %d driver-ps-blocked\n",
		       sta->sta.addr, sta->sta.aid);
		return;
	}

	set_sta_flag(sta, WLAN_STA_PS_DELIVER);
	clear_sta_flag(sta, WLAN_STA_PS_STA);
	ieee80211_sta_ps_deliver_wakeup(sta);
}

int ieee80211_sta_ps_transition(struct ieee80211_sta *pubsta, bool start)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	bool in_ps;

	WARN_ON(!ieee80211_hw_check(&sta->local->hw, AP_LINK_PS));

	/* Don't let the same PS state be set twice */
	in_ps = test_sta_flag(sta, WLAN_STA_PS_STA);
	if ((start && in_ps) || (!start && !in_ps))
		return -EINVAL;

	if (start)
		sta_ps_start(sta);
	else
		sta_ps_end(sta);

	return 0;
}
EXPORT_SYMBOL(ieee80211_sta_ps_transition);

void ieee80211_sta_pspoll(struct ieee80211_sta *pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	if (test_sta_flag(sta, WLAN_STA_SP))
		return;

	if (!test_sta_flag(sta, WLAN_STA_PS_DRIVER))
		ieee80211_sta_ps_deliver_poll_response(sta);
	else
		set_sta_flag(sta, WLAN_STA_PSPOLL);
}
EXPORT_SYMBOL(ieee80211_sta_pspoll);

void ieee80211_sta_uapsd_trigger(struct ieee80211_sta *pubsta, u8 tid)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	u8 ac = ieee802_1d_to_ac[tid & 7];

	/*
	 * If this AC is not trigger-enabled do nothing unless the
	 * driver is calling us after it already checked.
	 *
	 * NB: This could/should check a separate bitmap of trigger-
	 * enabled queues, but for now we only implement uAPSD w/o
	 * TSPEC changes to the ACs, so they're always the same.
	 */
	if (!(sta->sta.uapsd_queues & ieee80211_ac_to_qos_mask[ac]) &&
	    tid != IEEE80211_NUM_TIDS)
		return;

	/* if we are in a service period, do nothing */
	if (test_sta_flag(sta, WLAN_STA_SP))
		return;

	if (!test_sta_flag(sta, WLAN_STA_PS_DRIVER))
		ieee80211_sta_ps_deliver_uapsd(sta);
	else
		set_sta_flag(sta, WLAN_STA_UAPSD);
}
EXPORT_SYMBOL(ieee80211_sta_uapsd_trigger);

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_uapsd_and_pspoll(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_hdr *hdr = (void *)rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);

	if (!rx->sta)
		return RX_CONTINUE;

	if (sdata->vif.type != NL80211_IFTYPE_AP &&
	    sdata->vif.type != NL80211_IFTYPE_AP_VLAN)
		return RX_CONTINUE;

	/*
	 * The device handles station powersave, so don't do anything about
	 * uAPSD and PS-Poll frames (the latter shouldn't even come up from
	 * it to mac80211 since they're handled.)
	 */
	if (ieee80211_hw_check(&sdata->local->hw, AP_LINK_PS))
		return RX_CONTINUE;

	/*
	 * Don't do anything if the station isn't already asleep. In
	 * the uAPSD case, the station will probably be marked asleep,
	 * in the PS-Poll case the station must be confused ...
	 */
	if (!test_sta_flag(rx->sta, WLAN_STA_PS_STA))
		return RX_CONTINUE;

	if (unlikely(ieee80211_is_pspoll(hdr->frame_control))) {
		ieee80211_sta_pspoll(&rx->sta->sta);

		/* Free PS Poll skb here instead of returning RX_DROP that would
		 * count as an dropped frame. */
		dev_kfree_skb(rx->skb);

		return RX_QUEUED;
	} else if (!ieee80211_has_morefrags(hdr->frame_control) &&
		   !(status->rx_flags & IEEE80211_RX_DEFERRED_RELEASE) &&
		   ieee80211_has_pm(hdr->frame_control) &&
		   (ieee80211_is_data_qos(hdr->frame_control) ||
		    ieee80211_is_qos_nullfunc(hdr->frame_control))) {
		u8 tid;

		tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;

		ieee80211_sta_uapsd_trigger(&rx->sta->sta, tid);
	}

	return RX_CONTINUE;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_sta_process(struct ieee80211_rx_data *rx)
{
	struct sta_info *sta = rx->sta;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int i;

	if (!sta)
		return RX_CONTINUE;

	/*
	 * Update last_rx only for IBSS packets which are for the current
	 * BSSID and for station already AUTHORIZED to avoid keeping the
	 * current IBSS network alive in cases where other STAs start
	 * using different BSSID. This will also give the station another
	 * chance to restart the authentication/authorization in case
	 * something went wrong the first time.
	 */
	if (rx->sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		u8 *bssid = ieee80211_get_bssid(hdr, rx->skb->len,
						NL80211_IFTYPE_ADHOC);
		if (ether_addr_equal(bssid, rx->sdata->u.ibss.bssid) &&
		    test_sta_flag(sta, WLAN_STA_AUTHORIZED)) {
			sta->rx_stats.last_rx = jiffies;
			if (ieee80211_is_data(hdr->frame_control) &&
			    !is_multicast_ether_addr(hdr->addr1))
				sta->rx_stats.last_rate =
					sta_stats_encode_rate(status);
		}
	} else if (rx->sdata->vif.type == NL80211_IFTYPE_OCB) {
		sta->rx_stats.last_rx = jiffies;
	} else if (!is_multicast_ether_addr(hdr->addr1)) {
		/*
		 * Mesh beacons will update last_rx when if they are found to
		 * match the current local configuration when processed.
		 */
		sta->rx_stats.last_rx = jiffies;
		if (ieee80211_is_data(hdr->frame_control))
			sta->rx_stats.last_rate = sta_stats_encode_rate(status);
	}

	if (rx->sdata->vif.type == NL80211_IFTYPE_STATION)
		ieee80211_sta_rx_notify(rx->sdata, hdr);

	sta->rx_stats.fragments++;

	u64_stats_update_begin(&rx->sta->rx_stats.syncp);
	sta->rx_stats.bytes += rx->skb->len;
	u64_stats_update_end(&rx->sta->rx_stats.syncp);

	if (!(status->flag & RX_FLAG_NO_SIGNAL_VAL)) {
		sta->rx_stats.last_signal = status->signal;
		ewma_signal_add(&sta->rx_stats_avg.signal, -status->signal);
	}

	if (status->chains) {
		sta->rx_stats.chains = status->chains;
		for (i = 0; i < ARRAY_SIZE(status->chain_signal); i++) {
			int signal = status->chain_signal[i];

			if (!(status->chains & BIT(i)))
				continue;

			sta->rx_stats.chain_signal_last[i] = signal;
			ewma_signal_add(&sta->rx_stats_avg.chain_signal[i],
					-signal);
		}
	}

	/*
	 * Change STA power saving mode only at the end of a frame
	 * exchange sequence.
	 */
	if (!ieee80211_hw_check(&sta->local->hw, AP_LINK_PS) &&
	    !ieee80211_has_morefrags(hdr->frame_control) &&
	    !(status->rx_flags & IEEE80211_RX_DEFERRED_RELEASE) &&
	    (rx->sdata->vif.type == NL80211_IFTYPE_AP ||
	     rx->sdata->vif.type == NL80211_IFTYPE_AP_VLAN) &&
	    /* PM bit is only checked in frames where it isn't reserved,
	     * in AP mode it's reserved in non-bufferable management frames
	     * (cf. IEEE 802.11-2012 8.2.4.1.7 Power Management field)
	     */
	    (!ieee80211_is_mgmt(hdr->frame_control) ||
	     ieee80211_is_bufferable_mmpdu(hdr->frame_control))) {
		if (test_sta_flag(sta, WLAN_STA_PS_STA)) {
			if (!ieee80211_has_pm(hdr->frame_control))
				sta_ps_end(sta);
		} else {
			if (ieee80211_has_pm(hdr->frame_control))
				sta_ps_start(sta);
		}
	}

	/* mesh power save support */
	if (ieee80211_vif_is_mesh(&rx->sdata->vif))
		ieee80211_mps_rx_h_sta_process(sta, hdr);

	/*
	 * Drop (qos-)data::nullfunc frames silently, since they
	 * are used only to control station power saving mode.
	 */
	if (ieee80211_is_nullfunc(hdr->frame_control) ||
	    ieee80211_is_qos_nullfunc(hdr->frame_control)) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_nullfunc);

		/*
		 * If we receive a 4-addr nullfunc frame from a STA
		 * that was not moved to a 4-addr STA vlan yet send
		 * the event to userspace and for older hostapd drop
		 * the frame to the monitor interface.
		 */
		if (ieee80211_has_a4(hdr->frame_control) &&
		    (rx->sdata->vif.type == NL80211_IFTYPE_AP ||
		     (rx->sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
		      !rx->sdata->u.vlan.sta))) {
			if (!test_and_set_sta_flag(sta, WLAN_STA_4ADDR_EVENT))
				cfg80211_rx_unexpected_4addr_frame(
					rx->sdata->dev, sta->sta.addr,
					GFP_ATOMIC);
			return RX_DROP_MONITOR;
		}
		/*
		 * Update counter and free packet here to avoid
		 * counting this as a dropped packed.
		 */
		sta->rx_stats.packets++;
		dev_kfree_skb(rx->skb);
		return RX_QUEUED;
	}

	return RX_CONTINUE;
} /* ieee80211_rx_h_sta_process */

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_decrypt(struct ieee80211_rx_data *rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int keyidx;
	int hdrlen;
	ieee80211_rx_result result = RX_DROP_UNUSABLE;
	struct ieee80211_key *sta_ptk = NULL;
	int mmie_keyidx = -1;
	__le16 fc;
	const struct ieee80211_cipher_scheme *cs = NULL;

	/*
	 * Key selection 101
	 *
	 * There are four types of keys:
	 *  - GTK (group keys)
	 *  - IGTK (group keys for management frames)
	 *  - PTK (pairwise keys)
	 *  - STK (station-to-station pairwise keys)
	 *
	 * When selecting a key, we have to distinguish between multicast
	 * (including broadcast) and unicast frames, the latter can only
	 * use PTKs and STKs while the former always use GTKs and IGTKs.
	 * Unless, of course, actual WEP keys ("pre-RSNA") are used, then
	 * unicast frames can also use key indices like GTKs. Hence, if we
	 * don't have a PTK/STK we check the key index for a WEP key.
	 *
	 * Note that in a regular BSS, multicast frames are sent by the
	 * AP only, associated stations unicast the frame to the AP first
	 * which then multicasts it on their behalf.
	 *
	 * There is also a slight problem in IBSS mode: GTKs are negotiated
	 * with each station, that is something we don't currently handle.
	 * The spec seems to expect that one negotiates the same key with
	 * every station but there's no such requirement; VLANs could be
	 * possible.
	 */

	/* start without a key */
	rx->key = NULL;
	fc = hdr->frame_control;

	if (rx->sta) {
		int keyid = rx->sta->ptk_idx;

		if (ieee80211_has_protected(fc) && rx->sta->cipher_scheme) {
			cs = rx->sta->cipher_scheme;
			keyid = ieee80211_get_cs_keyid(cs, rx->skb);
			if (unlikely(keyid < 0))
				return RX_DROP_UNUSABLE;
		}
		sta_ptk = rcu_dereference(rx->sta->ptk[keyid]);
	}

	if (!ieee80211_has_protected(fc))
		mmie_keyidx = ieee80211_get_mmie_keyidx(rx->skb);

	if (!is_multicast_ether_addr(hdr->addr1) && sta_ptk) {
		rx->key = sta_ptk;
		if ((status->flag & RX_FLAG_DECRYPTED) &&
		    (status->flag & RX_FLAG_IV_STRIPPED))
			return RX_CONTINUE;
		/* Skip decryption if the frame is not protected. */
		if (!ieee80211_has_protected(fc))
			return RX_CONTINUE;
	} else if (mmie_keyidx >= 0) {
		/* Broadcast/multicast robust management frame / BIP */
		if ((status->flag & RX_FLAG_DECRYPTED) &&
		    (status->flag & RX_FLAG_IV_STRIPPED))
			return RX_CONTINUE;

		if (mmie_keyidx < NUM_DEFAULT_KEYS ||
		    mmie_keyidx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS)
			return RX_DROP_MONITOR; /* unexpected BIP keyidx */
		if (rx->sta) {
			if (ieee80211_is_group_privacy_action(skb) &&
			    test_sta_flag(rx->sta, WLAN_STA_MFP))
				return RX_DROP_MONITOR;

			rx->key = rcu_dereference(rx->sta->gtk[mmie_keyidx]);
		}
		if (!rx->key)
			rx->key = rcu_dereference(rx->sdata->keys[mmie_keyidx]);
	} else if (!ieee80211_has_protected(fc)) {
		/*
		 * The frame was not protected, so skip decryption. However, we
		 * need to set rx->key if there is a key that could have been
		 * used so that the frame may be dropped if encryption would
		 * have been expected.
		 */
		struct ieee80211_key *key = NULL;
		struct ieee80211_sub_if_data *sdata = rx->sdata;
		int i;

		if (ieee80211_is_mgmt(fc) &&
		    is_multicast_ether_addr(hdr->addr1) &&
		    (key = rcu_dereference(rx->sdata->default_mgmt_key)))
			rx->key = key;
		else {
			if (rx->sta) {
				for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
					key = rcu_dereference(rx->sta->gtk[i]);
					if (key)
						break;
				}
			}
			if (!key) {
				for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
					key = rcu_dereference(sdata->keys[i]);
					if (key)
						break;
				}
			}
			if (key)
				rx->key = key;
		}
		return RX_CONTINUE;
	} else {
		u8 keyid;

		/*
		 * The device doesn't give us the IV so we won't be
		 * able to look up the key. That's ok though, we
		 * don't need to decrypt the frame, we just won't
		 * be able to keep statistics accurate.
		 * Except for key threshold notifications, should
		 * we somehow allow the driver to tell us which key
		 * the hardware used if this flag is set?
		 */
		if ((status->flag & RX_FLAG_DECRYPTED) &&
		    (status->flag & RX_FLAG_IV_STRIPPED))
			return RX_CONTINUE;

		hdrlen = ieee80211_hdrlen(fc);

		if (cs) {
			keyidx = ieee80211_get_cs_keyid(cs, rx->skb);

			if (unlikely(keyidx < 0))
				return RX_DROP_UNUSABLE;
		} else {
			if (rx->skb->len < 8 + hdrlen)
				return RX_DROP_UNUSABLE; /* TODO: count this? */
			/*
			 * no need to call ieee80211_wep_get_keyidx,
			 * it verifies a bunch of things we've done already
			 */
			skb_copy_bits(rx->skb, hdrlen + 3, &keyid, 1);
			keyidx = keyid >> 6;
		}

		/* check per-station GTK first, if multicast packet */
		if (is_multicast_ether_addr(hdr->addr1) && rx->sta)
			rx->key = rcu_dereference(rx->sta->gtk[keyidx]);

		/* if not found, try default key */
		if (!rx->key) {
			rx->key = rcu_dereference(rx->sdata->keys[keyidx]);

			/*
			 * RSNA-protected unicast frames should always be
			 * sent with pairwise or station-to-station keys,
			 * but for WEP we allow using a key index as well.
			 */
			if (rx->key &&
			    rx->key->conf.cipher != WLAN_CIPHER_SUITE_WEP40 &&
			    rx->key->conf.cipher != WLAN_CIPHER_SUITE_WEP104 &&
			    !is_multicast_ether_addr(hdr->addr1))
				rx->key = NULL;
		}
	}

	if (rx->key) {
		if (unlikely(rx->key->flags & KEY_FLAG_TAINTED))
			return RX_DROP_MONITOR;

		/* TODO: add threshold stuff again */
	} else {
		return RX_DROP_MONITOR;
	}

	switch (rx->key->conf.cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		result = ieee80211_crypto_wep_decrypt(rx);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		result = ieee80211_crypto_tkip_decrypt(rx);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		result = ieee80211_crypto_ccmp_decrypt(
			rx, IEEE80211_CCMP_MIC_LEN);
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		result = ieee80211_crypto_ccmp_decrypt(
			rx, IEEE80211_CCMP_256_MIC_LEN);
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		result = ieee80211_crypto_aes_cmac_decrypt(rx);
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		result = ieee80211_crypto_aes_cmac_256_decrypt(rx);
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		result = ieee80211_crypto_aes_gmac_decrypt(rx);
		break;
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		result = ieee80211_crypto_gcmp_decrypt(rx);
		break;
	default:
		result = ieee80211_crypto_hw_decrypt(rx);
	}

	/* the hdr variable is invalid after the decrypt handlers */

	/* either the frame has been decrypted or will be dropped */
	status->flag |= RX_FLAG_DECRYPTED;

	return result;
}

static inline struct ieee80211_fragment_entry *
ieee80211_reassemble_add(struct ieee80211_sub_if_data *sdata,
			 unsigned int frag, unsigned int seq, int rx_queue,
			 struct sk_buff **skb)
{
	struct ieee80211_fragment_entry *entry;

	entry = &sdata->fragments[sdata->fragment_next++];
	if (sdata->fragment_next >= IEEE80211_FRAGMENT_MAX)
		sdata->fragment_next = 0;

	if (!skb_queue_empty(&entry->skb_list))
		__skb_queue_purge(&entry->skb_list);

	__skb_queue_tail(&entry->skb_list, *skb); /* no need for locking */
	*skb = NULL;
	entry->first_frag_time = jiffies;
	entry->seq = seq;
	entry->rx_queue = rx_queue;
	entry->last_frag = frag;
	entry->check_sequential_pn = false;
	entry->extra_len = 0;

	return entry;
}

static inline struct ieee80211_fragment_entry *
ieee80211_reassemble_find(struct ieee80211_sub_if_data *sdata,
			  unsigned int frag, unsigned int seq,
			  int rx_queue, struct ieee80211_hdr *hdr)
{
	struct ieee80211_fragment_entry *entry;
	int i, idx;

	idx = sdata->fragment_next;
	for (i = 0; i < IEEE80211_FRAGMENT_MAX; i++) {
		struct ieee80211_hdr *f_hdr;

		idx--;
		if (idx < 0)
			idx = IEEE80211_FRAGMENT_MAX - 1;

		entry = &sdata->fragments[idx];
		if (skb_queue_empty(&entry->skb_list) || entry->seq != seq ||
		    entry->rx_queue != rx_queue ||
		    entry->last_frag + 1 != frag)
			continue;

		f_hdr = (struct ieee80211_hdr *)entry->skb_list.next->data;

		/*
		 * Check ftype and addresses are equal, else check next fragment
		 */
		if (((hdr->frame_control ^ f_hdr->frame_control) &
		     cpu_to_le16(IEEE80211_FCTL_FTYPE)) ||
		    !ether_addr_equal(hdr->addr1, f_hdr->addr1) ||
		    !ether_addr_equal(hdr->addr2, f_hdr->addr2))
			continue;

		if (time_after(jiffies, entry->first_frag_time + 2 * HZ)) {
			__skb_queue_purge(&entry->skb_list);
			continue;
		}
		return entry;
	}

	return NULL;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_defragment(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr;
	u16 sc;
	__le16 fc;
	unsigned int frag, seq;
	struct ieee80211_fragment_entry *entry;
	struct sk_buff *skb;
	struct ieee80211_rx_status *status;

	hdr = (struct ieee80211_hdr *)rx->skb->data;
	fc = hdr->frame_control;

	if (ieee80211_is_ctl(fc))
		return RX_CONTINUE;

	sc = le16_to_cpu(hdr->seq_ctrl);
	frag = sc & IEEE80211_SCTL_FRAG;

	if (is_multicast_ether_addr(hdr->addr1)) {
		I802_DEBUG_INC(rx->local->dot11MulticastReceivedFrameCount);
		goto out_no_led;
	}

	if (likely(!ieee80211_has_morefrags(fc) && frag == 0))
		goto out;

	I802_DEBUG_INC(rx->local->rx_handlers_fragments);

	if (skb_linearize(rx->skb))
		return RX_DROP_UNUSABLE;

	/*
	 *  skb_linearize() might change the skb->data and
	 *  previously cached variables (in this case, hdr) need to
	 *  be refreshed with the new data.
	 */
	hdr = (struct ieee80211_hdr *)rx->skb->data;
	seq = (sc & IEEE80211_SCTL_SEQ) >> 4;

	if (frag == 0) {
		/* This is the first fragment of a new frame. */
		entry = ieee80211_reassemble_add(rx->sdata, frag, seq,
						 rx->seqno_idx, &(rx->skb));
		if (rx->key &&
		    (rx->key->conf.cipher == WLAN_CIPHER_SUITE_CCMP ||
		     rx->key->conf.cipher == WLAN_CIPHER_SUITE_CCMP_256 ||
		     rx->key->conf.cipher == WLAN_CIPHER_SUITE_GCMP ||
		     rx->key->conf.cipher == WLAN_CIPHER_SUITE_GCMP_256) &&
		    ieee80211_has_protected(fc)) {
			int queue = rx->security_idx;

			/* Store CCMP/GCMP PN so that we can verify that the
			 * next fragment has a sequential PN value.
			 */
			entry->check_sequential_pn = true;
			memcpy(entry->last_pn,
			       rx->key->u.ccmp.rx_pn[queue],
			       IEEE80211_CCMP_PN_LEN);
			BUILD_BUG_ON(offsetof(struct ieee80211_key,
					      u.ccmp.rx_pn) !=
				     offsetof(struct ieee80211_key,
					      u.gcmp.rx_pn));
			BUILD_BUG_ON(sizeof(rx->key->u.ccmp.rx_pn[queue]) !=
				     sizeof(rx->key->u.gcmp.rx_pn[queue]));
			BUILD_BUG_ON(IEEE80211_CCMP_PN_LEN !=
				     IEEE80211_GCMP_PN_LEN);
		}
		return RX_QUEUED;
	}

	/* This is a fragment for a frame that should already be pending in
	 * fragment cache. Add this fragment to the end of the pending entry.
	 */
	entry = ieee80211_reassemble_find(rx->sdata, frag, seq,
					  rx->seqno_idx, hdr);
	if (!entry) {
		I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
		return RX_DROP_MONITOR;
	}

	/* "The receiver shall discard MSDUs and MMPDUs whose constituent
	 *  MPDU PN values are not incrementing in steps of 1."
	 * see IEEE P802.11-REVmc/D5.0, 12.5.3.4.4, item d (for CCMP)
	 * and IEEE P802.11-REVmc/D5.0, 12.5.5.4.4, item d (for GCMP)
	 */
	if (entry->check_sequential_pn) {
		int i;
		u8 pn[IEEE80211_CCMP_PN_LEN], *rpn;
		int queue;

		if (!rx->key ||
		    (rx->key->conf.cipher != WLAN_CIPHER_SUITE_CCMP &&
		     rx->key->conf.cipher != WLAN_CIPHER_SUITE_CCMP_256 &&
		     rx->key->conf.cipher != WLAN_CIPHER_SUITE_GCMP &&
		     rx->key->conf.cipher != WLAN_CIPHER_SUITE_GCMP_256))
			return RX_DROP_UNUSABLE;
		memcpy(pn, entry->last_pn, IEEE80211_CCMP_PN_LEN);
		for (i = IEEE80211_CCMP_PN_LEN - 1; i >= 0; i--) {
			pn[i]++;
			if (pn[i])
				break;
		}
		queue = rx->security_idx;
		rpn = rx->key->u.ccmp.rx_pn[queue];
		if (memcmp(pn, rpn, IEEE80211_CCMP_PN_LEN))
			return RX_DROP_UNUSABLE;
		memcpy(entry->last_pn, pn, IEEE80211_CCMP_PN_LEN);
	}

	skb_pull(rx->skb, ieee80211_hdrlen(fc));
	__skb_queue_tail(&entry->skb_list, rx->skb);
	entry->last_frag = frag;
	entry->extra_len += rx->skb->len;
	if (ieee80211_has_morefrags(fc)) {
		rx->skb = NULL;
		return RX_QUEUED;
	}

	rx->skb = __skb_dequeue(&entry->skb_list);
	if (skb_tailroom(rx->skb) < entry->extra_len) {
		I802_DEBUG_INC(rx->local->rx_expand_skb_head_defrag);
		if (unlikely(pskb_expand_head(rx->skb, 0, entry->extra_len,
					      GFP_ATOMIC))) {
			I802_DEBUG_INC(rx->local->rx_handlers_drop_defrag);
			__skb_queue_purge(&entry->skb_list);
			return RX_DROP_UNUSABLE;
		}
	}
	while ((skb = __skb_dequeue(&entry->skb_list))) {
		memcpy(skb_put(rx->skb, skb->len), skb->data, skb->len);
		dev_kfree_skb(skb);
	}

	/* Complete frame has been reassembled - process it now */
	status = IEEE80211_SKB_RXCB(rx->skb);

 out:
	ieee80211_led_rx(rx->local);
 out_no_led:
	if (rx->sta)
		rx->sta->rx_stats.packets++;
	return RX_CONTINUE;
}

static int ieee80211_802_1x_port_control(struct ieee80211_rx_data *rx)
{
	if (unlikely(!rx->sta || !test_sta_flag(rx->sta, WLAN_STA_AUTHORIZED)))
		return -EACCES;

	return 0;
}

static int ieee80211_drop_unencrypted(struct ieee80211_rx_data *rx, __le16 fc)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);

	/*
	 * Pass through unencrypted frames if the hardware has
	 * decrypted them already.
	 */
	if (status->flag & RX_FLAG_DECRYPTED)
		return 0;

	/* Drop unencrypted frames if key is set. */
	if (unlikely(!ieee80211_has_protected(fc) &&
		     !ieee80211_is_nullfunc(fc) &&
		     ieee80211_is_data(fc) && rx->key))
		return -EACCES;

	return 0;
}

static int ieee80211_drop_unencrypted_mgmt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	__le16 fc = hdr->frame_control;

	/*
	 * Pass through unencrypted frames if the hardware has
	 * decrypted them already.
	 */
	if (status->flag & RX_FLAG_DECRYPTED)
		return 0;

	if (rx->sta && test_sta_flag(rx->sta, WLAN_STA_MFP)) {
		if (unlikely(!ieee80211_has_protected(fc) &&
			     ieee80211_is_unicast_robust_mgmt_frame(rx->skb) &&
			     rx->key)) {
			if (ieee80211_is_deauth(fc) ||
			    ieee80211_is_disassoc(fc))
				cfg80211_rx_unprot_mlme_mgmt(rx->sdata->dev,
							     rx->skb->data,
							     rx->skb->len);
			return -EACCES;
		}
		/* BIP does not use Protected field, so need to check MMIE */
		if (unlikely(ieee80211_is_multicast_robust_mgmt_frame(rx->skb) &&
			     ieee80211_get_mmie_keyidx(rx->skb) < 0)) {
			if (ieee80211_is_deauth(fc) ||
			    ieee80211_is_disassoc(fc))
				cfg80211_rx_unprot_mlme_mgmt(rx->sdata->dev,
							     rx->skb->data,
							     rx->skb->len);
			return -EACCES;
		}
		/*
		 * When using MFP, Action frames are not allowed prior to
		 * having configured keys.
		 */
		if (unlikely(ieee80211_is_action(fc) && !rx->key &&
			     ieee80211_is_robust_mgmt_frame(rx->skb)))
			return -EACCES;
	}

	return 0;
}

static int
__ieee80211_data_to_8023(struct ieee80211_rx_data *rx, bool *port_control)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	bool check_port_control = false;
	struct ethhdr *ehdr;
	int ret;

	*port_control = false;
	if (ieee80211_has_a4(hdr->frame_control) &&
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN && !sdata->u.vlan.sta)
		return -1;

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !!sdata->u.mgd.use_4addr != !!ieee80211_has_a4(hdr->frame_control)) {

		if (!sdata->u.mgd.use_4addr)
			return -1;
		else
			check_port_control = true;
	}

	if (is_multicast_ether_addr(hdr->addr1) &&
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN && sdata->u.vlan.sta)
		return -1;

	ret = ieee80211_data_to_8023(rx->skb, sdata->vif.addr, sdata->vif.type);
	if (ret < 0)
		return ret;

	ehdr = (struct ethhdr *) rx->skb->data;
	if (ehdr->h_proto == rx->sdata->control_port_protocol)
		*port_control = true;
	else if (check_port_control)
		return -1;

	return 0;
}

/*
 * requires that rx->skb is a frame with ethernet header
 */
static bool ieee80211_frame_allowed(struct ieee80211_rx_data *rx, __le16 fc)
{
	static const u8 pae_group_addr[ETH_ALEN] __aligned(2)
		= { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x03 };
	struct ethhdr *ehdr = (struct ethhdr *) rx->skb->data;

	/*
	 * Allow EAPOL frames to us/the PAE group address regardless
	 * of whether the frame was encrypted or not.
	 */
	if (ehdr->h_proto == rx->sdata->control_port_protocol &&
	    (ether_addr_equal(ehdr->h_dest, rx->sdata->vif.addr) ||
	     ether_addr_equal(ehdr->h_dest, pae_group_addr)))
		return true;

	if (ieee80211_802_1x_port_control(rx) ||
	    ieee80211_drop_unencrypted(rx, fc))
		return false;

	return true;
}

/*
 * requires that rx->skb is a frame with ethernet header
 */
static void
ieee80211_deliver_skb(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct net_device *dev = sdata->dev;
	struct sk_buff *skb, *xmit_skb;
	struct ethhdr *ehdr = (struct ethhdr *) rx->skb->data;
	struct sta_info *dsta;

	skb = rx->skb;
	xmit_skb = NULL;

	ieee80211_rx_stats(dev, skb->len);

	if (rx->sta) {
		/* The seqno index has the same property as needed
		 * for the rx_msdu field, i.e. it is IEEE80211_NUM_TIDS
		 * for non-QoS-data frames. Here we know it's a data
		 * frame, so count MSDUs.
		 */
		u64_stats_update_begin(&rx->sta->rx_stats.syncp);
		rx->sta->rx_stats.msdu[rx->seqno_idx]++;
		u64_stats_update_end(&rx->sta->rx_stats.syncp);
	}

	if ((sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_AP_VLAN) &&
	    !(sdata->flags & IEEE80211_SDATA_DONT_BRIDGE_PACKETS) &&
	    (sdata->vif.type != NL80211_IFTYPE_AP_VLAN || !sdata->u.vlan.sta)) {
		if (is_multicast_ether_addr(ehdr->h_dest) &&
		    ieee80211_vif_get_num_mcast_if(sdata) != 0) {
			/*
			 * send multicast frames both to higher layers in
			 * local net stack and back to the wireless medium
			 */
			xmit_skb = skb_copy(skb, GFP_ATOMIC);
			if (!xmit_skb)
				net_info_ratelimited("%s: failed to clone multicast frame\n",
						    dev->name);
		} else if (!is_multicast_ether_addr(ehdr->h_dest)) {
			dsta = sta_info_get(sdata, skb->data);
			if (dsta) {
				/*
				 * The destination station is associated to
				 * this AP (in this VLAN), so send the frame
				 * directly to it and do not pass it to local
				 * net stack.
				 */
				xmit_skb = skb;
				skb = NULL;
			}
		}
	}

#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (skb) {
		/* 'align' will only take the values 0 or 2 here since all
		 * frames are required to be aligned to 2-byte boundaries
		 * when being passed to mac80211; the code here works just
		 * as well if that isn't true, but mac80211 assumes it can
		 * access fields as 2-byte aligned (e.g. for ether_addr_equal)
		 */
		int align;

		align = (unsigned long)(skb->data + sizeof(struct ethhdr)) & 3;
		if (align) {
			if (WARN_ON(skb_headroom(skb) < 3)) {
				dev_kfree_skb(skb);
				skb = NULL;
			} else {
				u8 *data = skb->data;
				size_t len = skb_headlen(skb);
				skb->data -= align;
				memmove(skb->data, data, len);
				skb_set_tail_pointer(skb, len);
			}
		}
	}
#endif

	if (skb) {
		/* deliver to local stack */
		skb->protocol = eth_type_trans(skb, dev);
		memset(skb->cb, 0, sizeof(skb->cb));
		if (rx->napi)
			napi_gro_receive(rx->napi, skb);
		else
			netif_receive_skb(skb);
	}

	if (xmit_skb) {
		/*
		 * Send to wireless media and increase priority by 256 to
		 * keep the received priority instead of reclassifying
		 * the frame (see cfg80211_classify8021d).
		 */
		xmit_skb->priority += 256;
		xmit_skb->protocol = htons(ETH_P_802_3);
		skb_reset_network_header(xmit_skb);
		skb_reset_mac_header(xmit_skb);
		dev_queue_xmit(xmit_skb);
	}
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_amsdu(struct ieee80211_rx_data *rx)
{
	struct net_device *dev = rx->sdata->dev;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	struct sk_buff_head frame_list;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	struct ethhdr ethhdr;
	const u8 *check_da = ethhdr.h_dest, *check_sa = ethhdr.h_source;

	if (unlikely(!ieee80211_is_data(fc)))
		return RX_CONTINUE;

	if (unlikely(!ieee80211_is_data_present(fc)))
		return RX_DROP_MONITOR;

	if (!(status->rx_flags & IEEE80211_RX_AMSDU))
		return RX_CONTINUE;

	if (unlikely(ieee80211_has_a4(hdr->frame_control))) {
		switch (rx->sdata->vif.type) {
		case NL80211_IFTYPE_AP_VLAN:
			if (!rx->sdata->u.vlan.sta)
				return RX_DROP_UNUSABLE;
			break;
		case NL80211_IFTYPE_STATION:
			if (!rx->sdata->u.mgd.use_4addr)
				return RX_DROP_UNUSABLE;
			break;
		default:
			return RX_DROP_UNUSABLE;
		}
		check_da = NULL;
		check_sa = NULL;
	} else switch (rx->sdata->vif.type) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_AP_VLAN:
			check_da = NULL;
			break;
		case NL80211_IFTYPE_STATION:
			if (!rx->sta ||
			    !test_sta_flag(rx->sta, WLAN_STA_TDLS_PEER))
				check_sa = NULL;
			break;
		case NL80211_IFTYPE_MESH_POINT:
			check_sa = NULL;
			break;
		default:
			break;
	}

	if (is_multicast_ether_addr(hdr->addr1))
		return RX_DROP_UNUSABLE;

	skb->dev = dev;
	__skb_queue_head_init(&frame_list);

	if (ieee80211_data_to_8023_exthdr(skb, &ethhdr,
					  rx->sdata->vif.addr,
					  rx->sdata->vif.type))
		return RX_DROP_UNUSABLE;

	ieee80211_amsdu_to_8023s(skb, &frame_list, dev->dev_addr,
				 rx->sdata->vif.type,
				 rx->local->hw.extra_tx_headroom,
				 check_da, check_sa);

	while (!skb_queue_empty(&frame_list)) {
		rx->skb = __skb_dequeue(&frame_list);

		if (!ieee80211_frame_allowed(rx, fc)) {
			dev_kfree_skb(rx->skb);
			continue;
		}

		ieee80211_deliver_skb(rx);
	}

	return RX_QUEUED;
}

#ifdef CONFIG_MAC80211_MESH
static ieee80211_rx_result
ieee80211_rx_h_mesh_fwding(struct ieee80211_rx_data *rx)
{
	struct ieee80211_hdr *fwd_hdr, *hdr;
	struct ieee80211_tx_info *info;
	struct ieee80211s_hdr *mesh_hdr;
	struct sk_buff *skb = rx->skb, *fwd_skb;
	struct ieee80211_local *local = rx->local;
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	u16 ac, q, hdrlen;

	hdr = (struct ieee80211_hdr *) skb->data;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	/* make sure fixed part of mesh header is there, also checks skb len */
	if (!pskb_may_pull(rx->skb, hdrlen + 6))
		return RX_DROP_MONITOR;

	mesh_hdr = (struct ieee80211s_hdr *) (skb->data + hdrlen);

	/* make sure full mesh header is there, also checks skb len */
	if (!pskb_may_pull(rx->skb,
			   hdrlen + ieee80211_get_mesh_hdrlen(mesh_hdr)))
		return RX_DROP_MONITOR;

	/* reload pointers */
	hdr = (struct ieee80211_hdr *) skb->data;
	mesh_hdr = (struct ieee80211s_hdr *) (skb->data + hdrlen);

	if (ieee80211_drop_unencrypted(rx, hdr->frame_control))
		return RX_DROP_MONITOR;

	/* frame is in RMC, don't forward */
	if (ieee80211_is_data(hdr->frame_control) &&
	    is_multicast_ether_addr(hdr->addr1) &&
	    mesh_rmc_check(rx->sdata, hdr->addr3, mesh_hdr))
		return RX_DROP_MONITOR;

	if (!ieee80211_is_data(hdr->frame_control))
		return RX_CONTINUE;

	if (!mesh_hdr->ttl)
		return RX_DROP_MONITOR;

	if (mesh_hdr->flags & MESH_FLAGS_AE) {
		struct mesh_path *mppath;
		char *proxied_addr;
		char *mpp_addr;

		if (is_multicast_ether_addr(hdr->addr1)) {
			mpp_addr = hdr->addr3;
			proxied_addr = mesh_hdr->eaddr1;
		} else if (mesh_hdr->flags & MESH_FLAGS_AE_A5_A6) {
			/* has_a4 already checked in ieee80211_rx_mesh_check */
			mpp_addr = hdr->addr4;
			proxied_addr = mesh_hdr->eaddr2;
		} else {
			return RX_DROP_MONITOR;
		}

		rcu_read_lock();
		mppath = mpp_path_lookup(sdata, proxied_addr);
		if (!mppath) {
			mpp_path_add(sdata, proxied_addr, mpp_addr);
		} else {
			spin_lock_bh(&mppath->state_lock);
			if (!ether_addr_equal(mppath->mpp, mpp_addr))
				memcpy(mppath->mpp, mpp_addr, ETH_ALEN);
			mppath->exp_time = jiffies;
			spin_unlock_bh(&mppath->state_lock);
		}
		rcu_read_unlock();
	}

	/* Frame has reached destination.  Don't forward */
	if (!is_multicast_ether_addr(hdr->addr1) &&
	    ether_addr_equal(sdata->vif.addr, hdr->addr3))
		return RX_CONTINUE;

	ac = ieee80211_select_queue_80211(sdata, skb, hdr);
	q = sdata->vif.hw_queue[ac];
	if (ieee80211_queue_stopped(&local->hw, q)) {
		IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, dropped_frames_congestion);
		return RX_DROP_MONITOR;
	}
	skb_set_queue_mapping(skb, q);

	if (!--mesh_hdr->ttl) {
		IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, dropped_frames_ttl);
		goto out;
	}

	if (!ifmsh->mshcfg.dot11MeshForwarding)
		goto out;

	fwd_skb = skb_copy(skb, GFP_ATOMIC);
	if (!fwd_skb) {
		net_info_ratelimited("%s: failed to clone mesh frame\n",
				    sdata->name);
		goto out;
	}

	fwd_hdr =  (struct ieee80211_hdr *) fwd_skb->data;
	fwd_hdr->frame_control &= ~cpu_to_le16(IEEE80211_FCTL_RETRY);
	info = IEEE80211_SKB_CB(fwd_skb);
	memset(info, 0, sizeof(*info));
	info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
	info->control.vif = &rx->sdata->vif;
	info->control.jiffies = jiffies;
	if (is_multicast_ether_addr(fwd_hdr->addr1)) {
		IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, fwded_mcast);
		memcpy(fwd_hdr->addr2, sdata->vif.addr, ETH_ALEN);
		/* update power mode indication when forwarding */
		ieee80211_mps_set_frame_flags(sdata, NULL, fwd_hdr);
	} else if (!mesh_nexthop_lookup(sdata, fwd_skb)) {
		/* mesh power mode flags updated in mesh_nexthop_lookup */
		IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, fwded_unicast);
	} else {
		/* unable to resolve next hop */
		mesh_path_error_tx(sdata, ifmsh->mshcfg.element_ttl,
				   fwd_hdr->addr3, 0,
				   WLAN_REASON_MESH_PATH_NOFORWARD,
				   fwd_hdr->addr2);
		IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, dropped_frames_no_route);
		kfree_skb(fwd_skb);
		return RX_DROP_MONITOR;
	}

	IEEE80211_IFSTA_MESH_CTR_INC(ifmsh, fwded_frames);
	ieee80211_add_pending_skb(local, fwd_skb);
 out:
	if (is_multicast_ether_addr(hdr->addr1))
		return RX_CONTINUE;
	return RX_DROP_MONITOR;
}
#endif

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_data(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_local *local = rx->local;
	struct net_device *dev = sdata->dev;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)rx->skb->data;
	__le16 fc = hdr->frame_control;
	bool port_control;
	int err;

	if (unlikely(!ieee80211_is_data(hdr->frame_control)))
		return RX_CONTINUE;

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return RX_DROP_MONITOR;

	/*
	 * Send unexpected-4addr-frame event to hostapd. For older versions,
	 * also drop the frame to cooked monitor interfaces.
	 */
	if (ieee80211_has_a4(hdr->frame_control) &&
	    sdata->vif.type == NL80211_IFTYPE_AP) {
		if (rx->sta &&
		    !test_and_set_sta_flag(rx->sta, WLAN_STA_4ADDR_EVENT))
			cfg80211_rx_unexpected_4addr_frame(
				rx->sdata->dev, rx->sta->sta.addr, GFP_ATOMIC);
		return RX_DROP_MONITOR;
	}

	err = __ieee80211_data_to_8023(rx, &port_control);
	if (unlikely(err))
		return RX_DROP_UNUSABLE;

	if (!ieee80211_frame_allowed(rx, fc))
		return RX_DROP_MONITOR;

	/* directly handle TDLS channel switch requests/responses */
	if (unlikely(((struct ethhdr *)rx->skb->data)->h_proto ==
						cpu_to_be16(ETH_P_TDLS))) {
		struct ieee80211_tdls_data *tf = (void *)rx->skb->data;

		if (pskb_may_pull(rx->skb,
				  offsetof(struct ieee80211_tdls_data, u)) &&
		    tf->payload_type == WLAN_TDLS_SNAP_RFTYPE &&
		    tf->category == WLAN_CATEGORY_TDLS &&
		    (tf->action_code == WLAN_TDLS_CHANNEL_SWITCH_REQUEST ||
		     tf->action_code == WLAN_TDLS_CHANNEL_SWITCH_RESPONSE)) {
			skb_queue_tail(&local->skb_queue_tdls_chsw, rx->skb);
			schedule_work(&local->tdls_chsw_work);
			if (rx->sta)
				rx->sta->rx_stats.packets++;

			return RX_QUEUED;
		}
	}

	if (rx->sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
	    unlikely(port_control) && sdata->bss) {
		sdata = container_of(sdata->bss, struct ieee80211_sub_if_data,
				     u.ap);
		dev = sdata->dev;
		rx->sdata = sdata;
	}

	rx->skb->dev = dev;

	if (!ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS) &&
	    local->ps_sdata && local->hw.conf.dynamic_ps_timeout > 0 &&
	    !is_multicast_ether_addr(
		    ((struct ethhdr *)rx->skb->data)->h_dest) &&
	    (!local->scanning &&
	     !test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state)))
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(local->hw.conf.dynamic_ps_timeout));

	ieee80211_deliver_skb(rx);

	return RX_QUEUED;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_ctrl(struct ieee80211_rx_data *rx, struct sk_buff_head *frames)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_bar *bar = (struct ieee80211_bar *)skb->data;
	struct tid_ampdu_rx *tid_agg_rx;
	u16 start_seq_num;
	u16 tid;

	if (likely(!ieee80211_is_ctl(bar->frame_control)))
		return RX_CONTINUE;

	if (ieee80211_is_back_req(bar->frame_control)) {
		struct {
			__le16 control, start_seq_num;
		} __packed bar_data;
		struct ieee80211_event event = {
			.type = BAR_RX_EVENT,
		};

		if (!rx->sta)
			return RX_DROP_MONITOR;

		if (skb_copy_bits(skb, offsetof(struct ieee80211_bar, control),
				  &bar_data, sizeof(bar_data)))
			return RX_DROP_MONITOR;

		tid = le16_to_cpu(bar_data.control) >> 12;

		if (!test_bit(tid, rx->sta->ampdu_mlme.agg_session_valid) &&
		    !test_and_set_bit(tid, rx->sta->ampdu_mlme.unexpected_agg))
			ieee80211_send_delba(rx->sdata, rx->sta->sta.addr, tid,
					     WLAN_BACK_RECIPIENT,
					     WLAN_REASON_QSTA_REQUIRE_SETUP);

		tid_agg_rx = rcu_dereference(rx->sta->ampdu_mlme.tid_rx[tid]);
		if (!tid_agg_rx)
			return RX_DROP_MONITOR;

		start_seq_num = le16_to_cpu(bar_data.start_seq_num) >> 4;
		event.u.ba.tid = tid;
		event.u.ba.ssn = start_seq_num;
		event.u.ba.sta = &rx->sta->sta;

		/* reset session timer */
		if (tid_agg_rx->timeout)
			mod_timer(&tid_agg_rx->session_timer,
				  TU_TO_EXP_TIME(tid_agg_rx->timeout));

		spin_lock(&tid_agg_rx->reorder_lock);
		/* release stored frames up to start of BAR */
		ieee80211_release_reorder_frames(rx->sdata, tid_agg_rx,
						 start_seq_num, frames);
		spin_unlock(&tid_agg_rx->reorder_lock);

		drv_event_callback(rx->local, rx->sdata, &event);

		kfree_skb(skb);
		return RX_QUEUED;
	}

	/*
	 * After this point, we only want management frames,
	 * so we can drop all remaining control frames to
	 * cooked monitor interfaces.
	 */
	return RX_DROP_MONITOR;
}

static void ieee80211_process_sa_query_req(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_mgmt *mgmt,
					   size_t len)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *resp;

	if (!ether_addr_equal(mgmt->da, sdata->vif.addr)) {
		/* Not to own unicast address */
		return;
	}

	if (!ether_addr_equal(mgmt->sa, sdata->u.mgd.bssid) ||
	    !ether_addr_equal(mgmt->bssid, sdata->u.mgd.bssid)) {
		/* Not from the current AP or not associated yet. */
		return;
	}

	if (len < 24 + 1 + sizeof(resp->u.action.u.sa_query)) {
		/* Too short SA Query request frame */
		return;
	}

	skb = dev_alloc_skb(sizeof(*resp) + local->hw.extra_tx_headroom);
	if (skb == NULL)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	resp = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(resp, 0, 24);
	memcpy(resp->da, mgmt->sa, ETH_ALEN);
	memcpy(resp->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(resp->bssid, sdata->u.mgd.bssid, ETH_ALEN);
	resp->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	skb_put(skb, 1 + sizeof(resp->u.action.u.sa_query));
	resp->u.action.category = WLAN_CATEGORY_SA_QUERY;
	resp->u.action.u.sa_query.action = WLAN_ACTION_SA_QUERY_RESPONSE;
	memcpy(resp->u.action.u.sa_query.trans_id,
	       mgmt->u.action.u.sa_query.trans_id,
	       WLAN_SA_QUERY_TR_ID_LEN);

	ieee80211_tx_skb(sdata, skb);
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_mgmt_check(struct ieee80211_rx_data *rx)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *) rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);

	/*
	 * From here on, look only at management frames.
	 * Data and control frames are already handled,
	 * and unknown (reserved) frames are useless.
	 */
	if (rx->skb->len < 24)
		return RX_DROP_MONITOR;

	if (!ieee80211_is_mgmt(mgmt->frame_control))
		return RX_DROP_MONITOR;

	if (rx->sdata->vif.type == NL80211_IFTYPE_AP &&
	    ieee80211_is_beacon(mgmt->frame_control) &&
	    !(rx->flags & IEEE80211_RX_BEACON_REPORTED)) {
		int sig = 0;

		if (ieee80211_hw_check(&rx->local->hw, SIGNAL_DBM))
			sig = status->signal;

		cfg80211_report_obss_beacon(rx->local->hw.wiphy,
					    rx->skb->data, rx->skb->len,
					    status->freq, sig);
		rx->flags |= IEEE80211_RX_BEACON_REPORTED;
	}

	if (ieee80211_drop_unencrypted_mgmt(rx))
		return RX_DROP_UNUSABLE;

	return RX_CONTINUE;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_action(struct ieee80211_rx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *) rx->skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	int len = rx->skb->len;

	if (!ieee80211_is_action(mgmt->frame_control))
		return RX_CONTINUE;

	/* drop too small frames */
	if (len < IEEE80211_MIN_ACTION_SIZE)
		return RX_DROP_UNUSABLE;

	if (!rx->sta && mgmt->u.action.category != WLAN_CATEGORY_PUBLIC &&
	    mgmt->u.action.category != WLAN_CATEGORY_SELF_PROTECTED &&
	    mgmt->u.action.category != WLAN_CATEGORY_SPECTRUM_MGMT)
		return RX_DROP_UNUSABLE;

	switch (mgmt->u.action.category) {
	case WLAN_CATEGORY_HT:
		/* reject HT action frames from stations not supporting HT */
		if (!rx->sta->sta.ht_cap.ht_supported)
			goto invalid;

		if (sdata->vif.type != NL80211_IFTYPE_STATION &&
		    sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
		    sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP &&
		    sdata->vif.type != NL80211_IFTYPE_ADHOC)
			break;

		/* verify action & smps_control/chanwidth are present */
		if (len < IEEE80211_MIN_ACTION_SIZE + 2)
			goto invalid;

		switch (mgmt->u.action.u.ht_smps.action) {
		case WLAN_HT_ACTION_SMPS: {
			struct ieee80211_supported_band *sband;
			enum ieee80211_smps_mode smps_mode;

			/* convert to HT capability */
			switch (mgmt->u.action.u.ht_smps.smps_control) {
			case WLAN_HT_SMPS_CONTROL_DISABLED:
				smps_mode = IEEE80211_SMPS_OFF;
				break;
			case WLAN_HT_SMPS_CONTROL_STATIC:
				smps_mode = IEEE80211_SMPS_STATIC;
				break;
			case WLAN_HT_SMPS_CONTROL_DYNAMIC:
				smps_mode = IEEE80211_SMPS_DYNAMIC;
				break;
			default:
				goto invalid;
			}

			/* if no change do nothing */
			if (rx->sta->sta.smps_mode == smps_mode)
				goto handled;
			rx->sta->sta.smps_mode = smps_mode;

			sband = rx->local->hw.wiphy->bands[status->band];

			rate_control_rate_update(local, sband, rx->sta,
						 IEEE80211_RC_SMPS_CHANGED);
			goto handled;
		}
		case WLAN_HT_ACTION_NOTIFY_CHANWIDTH: {
			struct ieee80211_supported_band *sband;
			u8 chanwidth = mgmt->u.action.u.ht_notify_cw.chanwidth;
			enum ieee80211_sta_rx_bandwidth max_bw, new_bw;

			/* If it doesn't support 40 MHz it can't change ... */
			if (!(rx->sta->sta.ht_cap.cap &
					IEEE80211_HT_CAP_SUP_WIDTH_20_40))
				goto handled;

			if (chanwidth == IEEE80211_HT_CHANWIDTH_20MHZ)
				max_bw = IEEE80211_STA_RX_BW_20;
			else
				max_bw = ieee80211_sta_cap_rx_bw(rx->sta);

			/* set cur_max_bandwidth and recalc sta bw */
			rx->sta->cur_max_bandwidth = max_bw;
			new_bw = ieee80211_sta_cur_vht_bw(rx->sta);

			if (rx->sta->sta.bandwidth == new_bw)
				goto handled;

			rx->sta->sta.bandwidth = new_bw;
			sband = rx->local->hw.wiphy->bands[status->band];

			rate_control_rate_update(local, sband, rx->sta,
						 IEEE80211_RC_BW_CHANGED);
			goto handled;
		}
		default:
			goto invalid;
		}

		break;
	case WLAN_CATEGORY_PUBLIC:
		if (len < IEEE80211_MIN_ACTION_SIZE + 1)
			goto invalid;
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			break;
		if (!rx->sta)
			break;
		if (!ether_addr_equal(mgmt->bssid, sdata->u.mgd.bssid))
			break;
		if (mgmt->u.action.u.ext_chan_switch.action_code !=
				WLAN_PUB_ACTION_EXT_CHANSW_ANN)
			break;
		if (len < offsetof(struct ieee80211_mgmt,
				   u.action.u.ext_chan_switch.variable))
			goto invalid;
		goto queue;
	case WLAN_CATEGORY_VHT:
		if (sdata->vif.type != NL80211_IFTYPE_STATION &&
		    sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
		    sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP &&
		    sdata->vif.type != NL80211_IFTYPE_ADHOC)
			break;

		/* verify action code is present */
		if (len < IEEE80211_MIN_ACTION_SIZE + 1)
			goto invalid;

		switch (mgmt->u.action.u.vht_opmode_notif.action_code) {
		case WLAN_VHT_ACTION_OPMODE_NOTIF: {
			u8 opmode;

			/* verify opmode is present */
			if (len < IEEE80211_MIN_ACTION_SIZE + 2)
				goto invalid;

			opmode = mgmt->u.action.u.vht_opmode_notif.operating_mode;

			ieee80211_vht_handle_opmode(rx->sdata, rx->sta,
						    opmode, status->band);
			goto handled;
		}
		case WLAN_VHT_ACTION_GROUPID_MGMT: {
			if (len < IEEE80211_MIN_ACTION_SIZE + 25)
				goto invalid;
			goto queue;
		}
		default:
			break;
		}
		break;
	case WLAN_CATEGORY_BACK:
		if (sdata->vif.type != NL80211_IFTYPE_STATION &&
		    sdata->vif.type != NL80211_IFTYPE_MESH_POINT &&
		    sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP &&
		    sdata->vif.type != NL80211_IFTYPE_ADHOC)
			break;

		/* verify action_code is present */
		if (len < IEEE80211_MIN_ACTION_SIZE + 1)
			break;

		switch (mgmt->u.action.u.addba_req.action_code) {
		case WLAN_ACTION_ADDBA_REQ:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.addba_req)))
				goto invalid;
			break;
		case WLAN_ACTION_ADDBA_RESP:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.addba_resp)))
				goto invalid;
			break;
		case WLAN_ACTION_DELBA:
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.delba)))
				goto invalid;
			break;
		default:
			goto invalid;
		}

		goto queue;
	case WLAN_CATEGORY_SPECTRUM_MGMT:
		/* verify action_code is present */
		if (len < IEEE80211_MIN_ACTION_SIZE + 1)
			break;

		switch (mgmt->u.action.u.measurement.action_code) {
		case WLAN_ACTION_SPCT_MSR_REQ:
			if (status->band != NL80211_BAND_5GHZ)
				break;

			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.measurement)))
				break;

			if (sdata->vif.type != NL80211_IFTYPE_STATION)
				break;

			ieee80211_process_measurement_req(sdata, mgmt, len);
			goto handled;
		case WLAN_ACTION_SPCT_CHL_SWITCH: {
			u8 *bssid;
			if (len < (IEEE80211_MIN_ACTION_SIZE +
				   sizeof(mgmt->u.action.u.chan_switch)))
				break;

			if (sdata->vif.type != NL80211_IFTYPE_STATION &&
			    sdata->vif.type != NL80211_IFTYPE_ADHOC &&
			    sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
				break;

			if (sdata->vif.type == NL80211_IFTYPE_STATION)
				bssid = sdata->u.mgd.bssid;
			else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
				bssid = sdata->u.ibss.bssid;
			else if (sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
				bssid = mgmt->sa;
			else
				break;

			if (!ether_addr_equal(mgmt->bssid, bssid))
				break;

			goto queue;
			}
		}
		break;
	case WLAN_CATEGORY_SA_QUERY:
		if (len < (IEEE80211_MIN_ACTION_SIZE +
			   sizeof(mgmt->u.action.u.sa_query)))
			break;

		switch (mgmt->u.action.u.sa_query.action) {
		case WLAN_ACTION_SA_QUERY_REQUEST:
			if (sdata->vif.type != NL80211_IFTYPE_STATION)
				break;
			ieee80211_process_sa_query_req(sdata, mgmt, len);
			goto handled;
		}
		break;
	case WLAN_CATEGORY_SELF_PROTECTED:
		if (len < (IEEE80211_MIN_ACTION_SIZE +
			   sizeof(mgmt->u.action.u.self_prot.action_code)))
			break;

		switch (mgmt->u.action.u.self_prot.action_code) {
		case WLAN_SP_MESH_PEERING_OPEN:
		case WLAN_SP_MESH_PEERING_CLOSE:
		case WLAN_SP_MESH_PEERING_CONFIRM:
			if (!ieee80211_vif_is_mesh(&sdata->vif))
				goto invalid;
			if (sdata->u.mesh.user_mpm)
				/* userspace handles this frame */
				break;
			goto queue;
		case WLAN_SP_MGK_INFORM:
		case WLAN_SP_MGK_ACK:
			if (!ieee80211_vif_is_mesh(&sdata->vif))
				goto invalid;
			break;
		}
		break;
	case WLAN_CATEGORY_MESH_ACTION:
		if (len < (IEEE80211_MIN_ACTION_SIZE +
			   sizeof(mgmt->u.action.u.mesh_action.action_code)))
			break;

		if (!ieee80211_vif_is_mesh(&sdata->vif))
			break;
		if (mesh_action_is_path_sel(mgmt) &&
		    !mesh_path_sel_is_hwmp(sdata))
			break;
		goto queue;
	}

	return RX_CONTINUE;

 invalid:
	status->rx_flags |= IEEE80211_RX_MALFORMED_ACTION_FRM;
	/* will return in the next handlers */
	return RX_CONTINUE;

 handled:
	if (rx->sta)
		rx->sta->rx_stats.packets++;
	dev_kfree_skb(rx->skb);
	return RX_QUEUED;

 queue:
	rx->skb->pkt_type = IEEE80211_SDATA_QUEUE_TYPE_FRAME;
	skb_queue_tail(&sdata->skb_queue, rx->skb);
	ieee80211_queue_work(&local->hw, &sdata->work);
	if (rx->sta)
		rx->sta->rx_stats.packets++;
	return RX_QUEUED;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_userspace_mgmt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);
	int sig = 0;

	/* skip known-bad action frames and return them in the next handler */
	if (status->rx_flags & IEEE80211_RX_MALFORMED_ACTION_FRM)
		return RX_CONTINUE;

	/*
	 * Getting here means the kernel doesn't know how to handle
	 * it, but maybe userspace does ... include returned frames
	 * so userspace can register for those to know whether ones
	 * it transmitted were processed or returned.
	 */

	if (ieee80211_hw_check(&rx->local->hw, SIGNAL_DBM))
		sig = status->signal;

	if (cfg80211_rx_mgmt(&rx->sdata->wdev, status->freq, sig,
			     rx->skb->data, rx->skb->len, 0)) {
		if (rx->sta)
			rx->sta->rx_stats.packets++;
		dev_kfree_skb(rx->skb);
		return RX_QUEUED;
	}

	return RX_CONTINUE;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_action_return(struct ieee80211_rx_data *rx)
{
	struct ieee80211_local *local = rx->local;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *) rx->skb->data;
	struct sk_buff *nskb;
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(rx->skb);

	if (!ieee80211_is_action(mgmt->frame_control))
		return RX_CONTINUE;

	/*
	 * For AP mode, hostapd is responsible for handling any action
	 * frames that we didn't handle, including returning unknown
	 * ones. For all other modes we will return them to the sender,
	 * setting the 0x80 bit in the action category, as required by
	 * 802.11-2012 9.24.4.
	 * Newer versions of hostapd shall also use the management frame
	 * registration mechanisms, but older ones still use cooked
	 * monitor interfaces so push all frames there.
	 */
	if (!(status->rx_flags & IEEE80211_RX_MALFORMED_ACTION_FRM) &&
	    (sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_AP_VLAN))
		return RX_DROP_MONITOR;

	if (is_multicast_ether_addr(mgmt->da))
		return RX_DROP_MONITOR;

	/* do not return rejected action frames */
	if (mgmt->u.action.category & 0x80)
		return RX_DROP_UNUSABLE;

	nskb = skb_copy_expand(rx->skb, local->hw.extra_tx_headroom, 0,
			       GFP_ATOMIC);
	if (nskb) {
		struct ieee80211_mgmt *nmgmt = (void *)nskb->data;

		nmgmt->u.action.category |= 0x80;
		memcpy(nmgmt->da, nmgmt->sa, ETH_ALEN);
		memcpy(nmgmt->sa, rx->sdata->vif.addr, ETH_ALEN);

		memset(nskb->cb, 0, sizeof(nskb->cb));

		if (rx->sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(nskb);

			info->flags = IEEE80211_TX_CTL_TX_OFFCHAN |
				      IEEE80211_TX_INTFL_OFFCHAN_TX_OK |
				      IEEE80211_TX_CTL_NO_CCK_RATE;
			if (ieee80211_hw_check(&local->hw, QUEUE_CONTROL))
				info->hw_queue =
					local->hw.offchannel_tx_hw_queue;
		}

		__ieee80211_tx_skb_tid_band(rx->sdata, nskb, 7,
					    status->band);
	}
	dev_kfree_skb(rx->skb);
	return RX_QUEUED;
}

static ieee80211_rx_result debug_noinline
ieee80211_rx_h_mgmt(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct ieee80211_mgmt *mgmt = (void *)rx->skb->data;
	__le16 stype;

	stype = mgmt->frame_control & cpu_to_le16(IEEE80211_FCTL_STYPE);

	if (!ieee80211_vif_is_mesh(&sdata->vif) &&
	    sdata->vif.type != NL80211_IFTYPE_ADHOC &&
	    sdata->vif.type != NL80211_IFTYPE_OCB &&
	    sdata->vif.type != NL80211_IFTYPE_STATION)
		return RX_DROP_MONITOR;

	switch (stype) {
	case cpu_to_le16(IEEE80211_STYPE_AUTH):
	case cpu_to_le16(IEEE80211_STYPE_BEACON):
	case cpu_to_le16(IEEE80211_STYPE_PROBE_RESP):
		/* process for all: mesh, mlme, ibss */
		break;
	case cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP):
	case cpu_to_le16(IEEE80211_STYPE_REASSOC_RESP):
	case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
	case cpu_to_le16(IEEE80211_STYPE_DISASSOC):
		if (is_multicast_ether_addr(mgmt->da) &&
		    !is_broadcast_ether_addr(mgmt->da))
			return RX_DROP_MONITOR;

		/* process only for station */
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			return RX_DROP_MONITOR;
		break;
	case cpu_to_le16(IEEE80211_STYPE_PROBE_REQ):
		/* process only for ibss and mesh */
		if (sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		    sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
			return RX_DROP_MONITOR;
		break;
	default:
		return RX_DROP_MONITOR;
	}

	/* queue up frame and kick off work to process it */
	rx->skb->pkt_type = IEEE80211_SDATA_QUEUE_TYPE_FRAME;
	skb_queue_tail(&sdata->skb_queue, rx->skb);
	ieee80211_queue_work(&rx->local->hw, &sdata->work);
	if (rx->sta)
		rx->sta->rx_stats.packets++;

	return RX_QUEUED;
}

static void ieee80211_rx_cooked_monitor(struct ieee80211_rx_data *rx,
					struct ieee80211_rate *rate)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = rx->local;
	struct sk_buff *skb = rx->skb, *skb2;
	struct net_device *prev_dev = NULL;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	int needed_headroom;

	/*
	 * If cooked monitor has been processed already, then
	 * don't do it again. If not, set the flag.
	 */
	if (rx->flags & IEEE80211_RX_CMNTR)
		goto out_free_skb;
	rx->flags |= IEEE80211_RX_CMNTR;

	/* If there are no cooked monitor interfaces, just free the SKB */
	if (!local->cooked_mntrs)
		goto out_free_skb;

	/* vendor data is long removed here */
	status->flag &= ~RX_FLAG_RADIOTAP_VENDOR_DATA;
	/* room for the radiotap header based on driver features */
	needed_headroom = ieee80211_rx_radiotap_hdrlen(local, status, skb);

	if (skb_headroom(skb) < needed_headroom &&
	    pskb_expand_head(skb, needed_headroom, 0, GFP_ATOMIC))
		goto out_free_skb;

	/* prepend radiotap information */
	ieee80211_add_rx_radiotap_header(local, skb, rate, needed_headroom,
					 false);

	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR ||
		    !(sdata->u.mntr.flags & MONITOR_FLAG_COOK_FRAMES))
			continue;

		if (prev_dev) {
			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (skb2) {
				skb2->dev = prev_dev;
				netif_receive_skb(skb2);
			}
		}

		prev_dev = sdata->dev;
		ieee80211_rx_stats(sdata->dev, skb->len);
	}

	if (prev_dev) {
		skb->dev = prev_dev;
		netif_receive_skb(skb);
		return;
	}

 out_free_skb:
	dev_kfree_skb(skb);
}

static void ieee80211_rx_handlers_result(struct ieee80211_rx_data *rx,
					 ieee80211_rx_result res)
{
	switch (res) {
	case RX_DROP_MONITOR:
		I802_DEBUG_INC(rx->sdata->local->rx_handlers_drop);
		if (rx->sta)
			rx->sta->rx_stats.dropped++;
		/* fall through */
	case RX_CONTINUE: {
		struct ieee80211_rate *rate = NULL;
		struct ieee80211_supported_band *sband;
		struct ieee80211_rx_status *status;

		status = IEEE80211_SKB_RXCB((rx->skb));

		sband = rx->local->hw.wiphy->bands[status->band];
		if (!(status->flag & RX_FLAG_HT) &&
		    !(status->flag & RX_FLAG_VHT))
			rate = &sband->bitrates[status->rate_idx];

		ieee80211_rx_cooked_monitor(rx, rate);
		break;
		}
	case RX_DROP_UNUSABLE:
		I802_DEBUG_INC(rx->sdata->local->rx_handlers_drop);
		if (rx->sta)
			rx->sta->rx_stats.dropped++;
		dev_kfree_skb(rx->skb);
		break;
	case RX_QUEUED:
		I802_DEBUG_INC(rx->sdata->local->rx_handlers_queued);
		break;
	}
}

static void ieee80211_rx_handlers(struct ieee80211_rx_data *rx,
				  struct sk_buff_head *frames)
{
	ieee80211_rx_result res = RX_DROP_MONITOR;
	struct sk_buff *skb;

#define CALL_RXH(rxh)			\
	do {				\
		res = rxh(rx);		\
		if (res != RX_CONTINUE)	\
			goto rxh_next;  \
	} while (0)

	/* Lock here to avoid hitting all of the data used in the RX
	 * path (e.g. key data, station data, ...) concurrently when
	 * a frame is released from the reorder buffer due to timeout
	 * from the timer, potentially concurrently with RX from the
	 * driver.
	 */
	spin_lock_bh(&rx->local->rx_path_lock);

	while ((skb = __skb_dequeue(frames))) {
		/*
		 * all the other fields are valid across frames
		 * that belong to an aMPDU since they are on the
		 * same TID from the same station
		 */
		rx->skb = skb;

		CALL_RXH(ieee80211_rx_h_check_more_data);
		CALL_RXH(ieee80211_rx_h_uapsd_and_pspoll);
		CALL_RXH(ieee80211_rx_h_sta_process);
		CALL_RXH(ieee80211_rx_h_decrypt);
		CALL_RXH(ieee80211_rx_h_defragment);
		CALL_RXH(ieee80211_rx_h_michael_mic_verify);
		/* must be after MMIC verify so header is counted in MPDU mic */
#ifdef CONFIG_MAC80211_MESH
		if (ieee80211_vif_is_mesh(&rx->sdata->vif))
			CALL_RXH(ieee80211_rx_h_mesh_fwding);
#endif
		CALL_RXH(ieee80211_rx_h_amsdu);
		CALL_RXH(ieee80211_rx_h_data);

		/* special treatment -- needs the queue */
		res = ieee80211_rx_h_ctrl(rx, frames);
		if (res != RX_CONTINUE)
			goto rxh_next;

		CALL_RXH(ieee80211_rx_h_mgmt_check);
		CALL_RXH(ieee80211_rx_h_action);
		CALL_RXH(ieee80211_rx_h_userspace_mgmt);
		CALL_RXH(ieee80211_rx_h_action_return);
		CALL_RXH(ieee80211_rx_h_mgmt);

 rxh_next:
		ieee80211_rx_handlers_result(rx, res);

#undef CALL_RXH
	}

	spin_unlock_bh(&rx->local->rx_path_lock);
}

static void ieee80211_invoke_rx_handlers(struct ieee80211_rx_data *rx)
{
	struct sk_buff_head reorder_release;
	ieee80211_rx_result res = RX_DROP_MONITOR;

	__skb_queue_head_init(&reorder_release);

#define CALL_RXH(rxh)			\
	do {				\
		res = rxh(rx);		\
		if (res != RX_CONTINUE)	\
			goto rxh_next;  \
	} while (0)

	CALL_RXH(ieee80211_rx_h_check_dup);
	CALL_RXH(ieee80211_rx_h_check);

	ieee80211_rx_reorder_ampdu(rx, &reorder_release);

	ieee80211_rx_handlers(rx, &reorder_release);
	return;

 rxh_next:
	ieee80211_rx_handlers_result(rx, res);

#undef CALL_RXH
}

/*
 * This function makes calls into the RX path, therefore
 * it has to be invoked under RCU read lock.
 */
void ieee80211_release_reorder_timeout(struct sta_info *sta, int tid)
{
	struct sk_buff_head frames;
	struct ieee80211_rx_data rx = {
		.sta = sta,
		.sdata = sta->sdata,
		.local = sta->local,
		/* This is OK -- must be QoS data frame */
		.security_idx = tid,
		.seqno_idx = tid,
		.napi = NULL, /* must be NULL to not have races */
	};
	struct tid_ampdu_rx *tid_agg_rx;

	tid_agg_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[tid]);
	if (!tid_agg_rx)
		return;

	__skb_queue_head_init(&frames);

	spin_lock(&tid_agg_rx->reorder_lock);
	ieee80211_sta_reorder_release(sta->sdata, tid_agg_rx, &frames);
	spin_unlock(&tid_agg_rx->reorder_lock);

	if (!skb_queue_empty(&frames)) {
		struct ieee80211_event event = {
			.type = BA_FRAME_TIMEOUT,
			.u.ba.tid = tid,
			.u.ba.sta = &sta->sta,
		};
		drv_event_callback(rx.local, rx.sdata, &event);
	}

	ieee80211_rx_handlers(&rx, &frames);
}

void ieee80211_mark_rx_ba_filtered_frames(struct ieee80211_sta *pubsta, u8 tid,
					  u16 ssn, u64 filtered,
					  u16 received_mpdus)
{
	struct sta_info *sta;
	struct tid_ampdu_rx *tid_agg_rx;
	struct sk_buff_head frames;
	struct ieee80211_rx_data rx = {
		/* This is OK -- must be QoS data frame */
		.security_idx = tid,
		.seqno_idx = tid,
	};
	int i, diff;

	if (WARN_ON(!pubsta || tid >= IEEE80211_NUM_TIDS))
		return;

	__skb_queue_head_init(&frames);

	sta = container_of(pubsta, struct sta_info, sta);

	rx.sta = sta;
	rx.sdata = sta->sdata;
	rx.local = sta->local;

	rcu_read_lock();
	tid_agg_rx = rcu_dereference(sta->ampdu_mlme.tid_rx[tid]);
	if (!tid_agg_rx)
		goto out;

	spin_lock_bh(&tid_agg_rx->reorder_lock);

	if (received_mpdus >= IEEE80211_SN_MODULO >> 1) {
		int release;

		/* release all frames in the reorder buffer */
		release = (tid_agg_rx->head_seq_num + tid_agg_rx->buf_size) %
			   IEEE80211_SN_MODULO;
		ieee80211_release_reorder_frames(sta->sdata, tid_agg_rx,
						 release, &frames);
		/* update ssn to match received ssn */
		tid_agg_rx->head_seq_num = ssn;
	} else {
		ieee80211_release_reorder_frames(sta->sdata, tid_agg_rx, ssn,
						 &frames);
	}

	/* handle the case that received ssn is behind the mac ssn.
	 * it can be tid_agg_rx->buf_size behind and still be valid */
	diff = (tid_agg_rx->head_seq_num - ssn) & IEEE80211_SN_MASK;
	if (diff >= tid_agg_rx->buf_size) {
		tid_agg_rx->reorder_buf_filtered = 0;
		goto release;
	}
	filtered = filtered >> diff;
	ssn += diff;

	/* update bitmap */
	for (i = 0; i < tid_agg_rx->buf_size; i++) {
		int index = (ssn + i) % tid_agg_rx->buf_size;

		tid_agg_rx->reorder_buf_filtered &= ~BIT_ULL(index);
		if (filtered & BIT_ULL(i))
			tid_agg_rx->reorder_buf_filtered |= BIT_ULL(index);
	}

	/* now process also frames that the filter marking released */
	ieee80211_sta_reorder_release(sta->sdata, tid_agg_rx, &frames);

release:
	spin_unlock_bh(&tid_agg_rx->reorder_lock);

	ieee80211_rx_handlers(&rx, &frames);

 out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee80211_mark_rx_ba_filtered_frames);

/* main receive path */

static bool ieee80211_accept_frame(struct ieee80211_rx_data *rx)
{
	struct ieee80211_sub_if_data *sdata = rx->sdata;
	struct sk_buff *skb = rx->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	u8 *bssid = ieee80211_get_bssid(hdr, skb->len, sdata->vif.type);
	int multicast = is_multicast_ether_addr(hdr->addr1);

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		if (!bssid && !sdata->u.mgd.use_4addr)
			return false;
		if (multicast)
			return true;
		return ether_addr_equal(sdata->vif.addr, hdr->addr1);
	case NL80211_IFTYPE_ADHOC:
		if (!bssid)
			return false;
		if (ether_addr_equal(sdata->vif.addr, hdr->addr2) ||
		    ether_addr_equal(sdata->u.ibss.bssid, hdr->addr2))
			return false;
		if (ieee80211_is_beacon(hdr->frame_control))
			return true;
		if (!ieee80211_bssid_match(bssid, sdata->u.ibss.bssid))
			return false;
		if (!multicast &&
		    !ether_addr_equal(sdata->vif.addr, hdr->addr1))
			return false;
		if (!rx->sta) {
			int rate_idx;
			if (status->flag & (RX_FLAG_HT | RX_FLAG_VHT))
				rate_idx = 0; /* TODO: HT/VHT rates */
			else
				rate_idx = status->rate_idx;
			ieee80211_ibss_rx_no_sta(sdata, bssid, hdr->addr2,
						 BIT(rate_idx));
		}
		return true;
	case NL80211_IFTYPE_OCB:
		if (!bssid)
			return false;
		if (!ieee80211_is_data_present(hdr->frame_control))
			return false;
		if (!is_broadcast_ether_addr(bssid))
			return false;
		if (!multicast &&
		    !ether_addr_equal(sdata->dev->dev_addr, hdr->addr1))
			return false;
		if (!rx->sta) {
			int rate_idx;
			if (status->flag & RX_FLAG_HT)
				rate_idx = 0; /* TODO: HT rates */
			else
				rate_idx = status->rate_idx;
			ieee80211_ocb_rx_no_sta(sdata, bssid, hdr->addr2,
						BIT(rate_idx));
		}
		return true;
	case NL80211_IFTYPE_MESH_POINT:
		if (multicast)
			return true;
		return ether_addr_equal(sdata->vif.addr, hdr->addr1);
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_AP:
		if (!bssid)
			return ether_addr_equal(sdata->vif.addr, hdr->addr1);

		if (!ieee80211_bssid_match(bssid, sdata->vif.addr)) {
			/*
			 * Accept public action frames even when the
			 * BSSID doesn't match, this is used for P2P
			 * and location updates. Note that mac80211
			 * itself never looks at these frames.
			 */
			if (!multicast &&
			    !ether_addr_equal(sdata->vif.addr, hdr->addr1))
				return false;
			if (ieee80211_is_public_action(hdr, skb->len))
				return true;
			return ieee80211_is_beacon(hdr->frame_control);
		}

		if (!ieee80211_has_tods(hdr->frame_control)) {
			/* ignore data frames to TDLS-peers */
			if (ieee80211_is_data(hdr->frame_control))
				return false;
			/* ignore action frames to TDLS-peers */
			if (ieee80211_is_action(hdr->frame_control) &&
			    !is_broadcast_ether_addr(bssid) &&
			    !ether_addr_equal(bssid, hdr->addr1))
				return false;
		}
		return true;
	case NL80211_IFTYPE_WDS:
		if (bssid || !ieee80211_is_data(hdr->frame_control))
			return false;
		return ether_addr_equal(sdata->u.wds.remote_addr, hdr->addr2);
	case NL80211_IFTYPE_P2P_DEVICE:
		return ieee80211_is_public_action(hdr, skb->len) ||
		       ieee80211_is_probe_req(hdr->frame_control) ||
		       ieee80211_is_probe_resp(hdr->frame_control) ||
		       ieee80211_is_beacon(hdr->frame_control);
	case NL80211_IFTYPE_NAN:
		/* Currently no frames on NAN interface are allowed */
		return false;
	default:
		break;
	}

	WARN_ON_ONCE(1);
	return false;
}

void ieee80211_check_fast_rx(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_key *key;
	struct ieee80211_fast_rx fastrx = {
		.dev = sdata->dev,
		.vif_type = sdata->vif.type,
		.control_port_protocol = sdata->control_port_protocol,
	}, *old, *new = NULL;
	bool assign = false;

	/* use sparse to check that we don't return without updating */
	__acquire(check_fast_rx);

	BUILD_BUG_ON(sizeof(fastrx.rfc1042_hdr) != sizeof(rfc1042_header));
	BUILD_BUG_ON(sizeof(fastrx.rfc1042_hdr) != ETH_ALEN);
	ether_addr_copy(fastrx.rfc1042_hdr, rfc1042_header);
	ether_addr_copy(fastrx.vif_addr, sdata->vif.addr);

	fastrx.uses_rss = ieee80211_hw_check(&local->hw, USES_RSS);

	/* fast-rx doesn't do reordering */
	if (ieee80211_hw_check(&local->hw, AMPDU_AGGREGATION) &&
	    !ieee80211_hw_check(&local->hw, SUPPORTS_REORDERING_BUFFER))
		goto clear;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		/* 4-addr is harder to deal with, later maybe */
		if (sdata->u.mgd.use_4addr)
			goto clear;
		/* software powersave is a huge mess, avoid all of it */
		if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK))
			goto clear;
		if (ieee80211_hw_check(&local->hw, SUPPORTS_PS) &&
		    !ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS))
			goto clear;
		if (sta->sta.tdls) {
			fastrx.da_offs = offsetof(struct ieee80211_hdr, addr1);
			fastrx.sa_offs = offsetof(struct ieee80211_hdr, addr2);
			fastrx.expected_ds_bits = 0;
		} else {
			fastrx.sta_notify = sdata->u.mgd.probe_send_count > 0;
			fastrx.da_offs = offsetof(struct ieee80211_hdr, addr1);
			fastrx.sa_offs = offsetof(struct ieee80211_hdr, addr3);
			fastrx.expected_ds_bits =
				cpu_to_le16(IEEE80211_FCTL_FROMDS);
		}
		break;
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_AP:
		/* parallel-rx requires this, at least with calls to
		 * ieee80211_sta_ps_transition()
		 */
		if (!ieee80211_hw_check(&local->hw, AP_LINK_PS))
			goto clear;
		fastrx.da_offs = offsetof(struct ieee80211_hdr, addr3);
		fastrx.sa_offs = offsetof(struct ieee80211_hdr, addr2);
		fastrx.expected_ds_bits = cpu_to_le16(IEEE80211_FCTL_TODS);

		fastrx.internal_forward =
			!(sdata->flags & IEEE80211_SDATA_DONT_BRIDGE_PACKETS) &&
			(sdata->vif.type != NL80211_IFTYPE_AP_VLAN ||
			 !sdata->u.vlan.sta);
		break;
	default:
		goto clear;
	}

	if (!test_sta_flag(sta, WLAN_STA_AUTHORIZED))
		goto clear;

	rcu_read_lock();
	key = rcu_dereference(sta->ptk[sta->ptk_idx]);
	if (key) {
		switch (key->conf.cipher) {
		case WLAN_CIPHER_SUITE_TKIP:
			/* we don't want to deal with MMIC in fast-rx */
			goto clear_rcu;
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			break;
		default:
			/* we also don't want to deal with WEP or cipher scheme
			 * since those require looking up the key idx in the
			 * frame, rather than assuming the PTK is used
			 * (we need to revisit this once we implement the real
			 * PTK index, which is now valid in the spec, but we
			 * haven't implemented that part yet)
			 */
			goto clear_rcu;
		}

		fastrx.key = true;
		fastrx.icv_len = key->conf.icv_len;
	}

	assign = true;
 clear_rcu:
	rcu_read_unlock();
 clear:
	__release(check_fast_rx);

	if (assign)
		new = kmemdup(&fastrx, sizeof(fastrx), GFP_KERNEL);

	spin_lock_bh(&sta->lock);
	old = rcu_dereference_protected(sta->fast_rx, true);
	rcu_assign_pointer(sta->fast_rx, new);
	spin_unlock_bh(&sta->lock);

	if (old)
		kfree_rcu(old, rcu_head);
}

void ieee80211_clear_fast_rx(struct sta_info *sta)
{
	struct ieee80211_fast_rx *old;

	spin_lock_bh(&sta->lock);
	old = rcu_dereference_protected(sta->fast_rx, true);
	RCU_INIT_POINTER(sta->fast_rx, NULL);
	spin_unlock_bh(&sta->lock);

	if (old)
		kfree_rcu(old, rcu_head);
}

void __ieee80211_check_fast_rx_iface(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_held(&local->sta_mtx);

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata &&
		    (!sta->sdata->bss || sta->sdata->bss != sdata->bss))
			continue;
		ieee80211_check_fast_rx(sta);
	}
}

void ieee80211_check_fast_rx_iface(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	mutex_lock(&local->sta_mtx);
	__ieee80211_check_fast_rx_iface(sdata);
	mutex_unlock(&local->sta_mtx);
}

static bool ieee80211_invoke_fast_rx(struct ieee80211_rx_data *rx,
				     struct ieee80211_fast_rx *fast_rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct sta_info *sta = rx->sta;
	int orig_len = skb->len;
	int snap_offs = ieee80211_hdrlen(hdr->frame_control);
	struct {
		u8 snap[sizeof(rfc1042_header)];
		__be16 proto;
	} *payload __aligned(2);
	struct {
		u8 da[ETH_ALEN];
		u8 sa[ETH_ALEN];
	} addrs __aligned(2);
	struct ieee80211_sta_rx_stats *stats = &sta->rx_stats;

	if (fast_rx->uses_rss)
		stats = this_cpu_ptr(sta->pcpu_rx_stats);

	/* for parallel-rx, we need to have DUP_VALIDATED, otherwise we write
	 * to a common data structure; drivers can implement that per queue
	 * but we don't have that information in mac80211
	 */
	if (!(status->flag & RX_FLAG_DUP_VALIDATED))
		return false;

#define FAST_RX_CRYPT_FLAGS	(RX_FLAG_PN_VALIDATED | RX_FLAG_DECRYPTED)

	/* If using encryption, we also need to have:
	 *  - PN_VALIDATED: similar, but the implementation is tricky
	 *  - DECRYPTED: necessary for PN_VALIDATED
	 */
	if (fast_rx->key &&
	    (status->flag & FAST_RX_CRYPT_FLAGS) != FAST_RX_CRYPT_FLAGS)
		return false;

	/* we don't deal with A-MSDU deaggregation here */
	if (status->rx_flags & IEEE80211_RX_AMSDU)
		return false;

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return false;

	if (unlikely(ieee80211_is_frag(hdr)))
		return false;

	/* Since our interface address cannot be multicast, this
	 * implicitly also rejects multicast frames without the
	 * explicit check.
	 *
	 * We shouldn't get any *data* frames not addressed to us
	 * (AP mode will accept multicast *management* frames), but
	 * punting here will make it go through the full checks in
	 * ieee80211_accept_frame().
	 */
	if (!ether_addr_equal(fast_rx->vif_addr, hdr->addr1))
		return false;

	if ((hdr->frame_control & cpu_to_le16(IEEE80211_FCTL_FROMDS |
					      IEEE80211_FCTL_TODS)) !=
	    fast_rx->expected_ds_bits)
		goto drop;

	/* assign the key to drop unencrypted frames (later)
	 * and strip the IV/MIC if necessary
	 */
	if (fast_rx->key && !(status->flag & RX_FLAG_IV_STRIPPED)) {
		/* GCMP header length is the same */
		snap_offs += IEEE80211_CCMP_HDR_LEN;
	}

	if (!pskb_may_pull(skb, snap_offs + sizeof(*payload)))
		goto drop;
	payload = (void *)(skb->data + snap_offs);

	if (!ether_addr_equal(payload->snap, fast_rx->rfc1042_hdr))
		return false;

	/* Don't handle these here since they require special code.
	 * Accept AARP and IPX even though they should come with a
	 * bridge-tunnel header - but if we get them this way then
	 * there's little point in discarding them.
	 */
	if (unlikely(payload->proto == cpu_to_be16(ETH_P_TDLS) ||
		     payload->proto == fast_rx->control_port_protocol))
		return false;

	/* after this point, don't punt to the slowpath! */

	if (rx->key && !(status->flag & RX_FLAG_MIC_STRIPPED) &&
	    pskb_trim(skb, skb->len - fast_rx->icv_len))
		goto drop;

	if (unlikely(fast_rx->sta_notify)) {
		ieee80211_sta_rx_notify(rx->sdata, hdr);
		fast_rx->sta_notify = false;
	}

	/* statistics part of ieee80211_rx_h_sta_process() */
	stats->last_rx = jiffies;
	stats->last_rate = sta_stats_encode_rate(status);

	stats->fragments++;

	if (!(status->flag & RX_FLAG_NO_SIGNAL_VAL)) {
		stats->last_signal = status->signal;
		if (!fast_rx->uses_rss)
			ewma_signal_add(&sta->rx_stats_avg.signal,
					-status->signal);
	}

	if (status->chains) {
		int i;

		stats->chains = status->chains;
		for (i = 0; i < ARRAY_SIZE(status->chain_signal); i++) {
			int signal = status->chain_signal[i];

			if (!(status->chains & BIT(i)))
				continue;

			stats->chain_signal_last[i] = signal;
			if (!fast_rx->uses_rss)
				ewma_signal_add(&sta->rx_stats_avg.chain_signal[i],
						-signal);
		}
	}
	/* end of statistics */

	if (rx->key && !ieee80211_has_protected(hdr->frame_control))
		goto drop;

	/* do the header conversion - first grab the addresses */
	ether_addr_copy(addrs.da, skb->data + fast_rx->da_offs);
	ether_addr_copy(addrs.sa, skb->data + fast_rx->sa_offs);
	/* remove the SNAP but leave the ethertype */
	skb_pull(skb, snap_offs + sizeof(rfc1042_header));
	/* push the addresses in front */
	memcpy(skb_push(skb, sizeof(addrs)), &addrs, sizeof(addrs));

	skb->dev = fast_rx->dev;

	ieee80211_rx_stats(fast_rx->dev, skb->len);

	/* The seqno index has the same property as needed
	 * for the rx_msdu field, i.e. it is IEEE80211_NUM_TIDS
	 * for non-QoS-data frames. Here we know it's a data
	 * frame, so count MSDUs.
	 */
	u64_stats_update_begin(&stats->syncp);
	stats->msdu[rx->seqno_idx]++;
	stats->bytes += orig_len;
	u64_stats_update_end(&stats->syncp);

	if (fast_rx->internal_forward) {
		struct sta_info *dsta = sta_info_get(rx->sdata, skb->data);

		if (dsta) {
			/*
			 * Send to wireless media and increase priority by 256
			 * to keep the received priority instead of
			 * reclassifying the frame (see cfg80211_classify8021d).
			 */
			skb->priority += 256;
			skb->protocol = htons(ETH_P_802_3);
			skb_reset_network_header(skb);
			skb_reset_mac_header(skb);
			dev_queue_xmit(skb);
			return true;
		}
	}

	/* deliver to local stack */
	skb->protocol = eth_type_trans(skb, fast_rx->dev);
	memset(skb->cb, 0, sizeof(skb->cb));
	if (rx->napi)
		napi_gro_receive(rx->napi, skb);
	else
		netif_receive_skb(skb);

	return true;
 drop:
	dev_kfree_skb(skb);
	stats->dropped++;
	return true;
}

/*
 * This function returns whether or not the SKB
 * was destined for RX processing or not, which,
 * if consume is true, is equivalent to whether
 * or not the skb was consumed.
 */
static bool ieee80211_prepare_and_rx_handle(struct ieee80211_rx_data *rx,
					    struct sk_buff *skb, bool consume)
{
	struct ieee80211_local *local = rx->local;
	struct ieee80211_sub_if_data *sdata = rx->sdata;

	rx->skb = skb;

	/* See if we can do fast-rx; if we have to copy we already lost,
	 * so punt in that case. We should never have to deliver a data
	 * frame to multiple interfaces anyway.
	 *
	 * We skip the ieee80211_accept_frame() call and do the necessary
	 * checking inside ieee80211_invoke_fast_rx().
	 */
	if (consume && rx->sta) {
		struct ieee80211_fast_rx *fast_rx;

		fast_rx = rcu_dereference(rx->sta->fast_rx);
		if (fast_rx && ieee80211_invoke_fast_rx(rx, fast_rx))
			return true;
	}

	if (!ieee80211_accept_frame(rx))
		return false;

	if (!consume) {
		skb = skb_copy(skb, GFP_ATOMIC);
		if (!skb) {
			if (net_ratelimit())
				wiphy_debug(local->hw.wiphy,
					"failed to copy skb for %s\n",
					sdata->name);
			return true;
		}

		rx->skb = skb;
	}

	ieee80211_invoke_rx_handlers(rx);
	return true;
}

/*
 * This is the actual Rx frames handler. as it belongs to Rx path it must
 * be called with rcu_read_lock protection.
 */
static void __ieee80211_rx_handle_packet(struct ieee80211_hw *hw,
					 struct ieee80211_sta *pubsta,
					 struct sk_buff *skb,
					 struct napi_struct *napi)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_hdr *hdr;
	__le16 fc;
	struct ieee80211_rx_data rx;
	struct ieee80211_sub_if_data *prev;
	struct rhlist_head *tmp;
	int err = 0;

	fc = ((struct ieee80211_hdr *)skb->data)->frame_control;
	memset(&rx, 0, sizeof(rx));
	rx.skb = skb;
	rx.local = local;
	rx.napi = napi;

	if (ieee80211_is_data(fc) || ieee80211_is_mgmt(fc))
		I802_DEBUG_INC(local->dot11ReceivedFragmentCount);

	if (ieee80211_is_mgmt(fc)) {
		/* drop frame if too short for header */
		if (skb->len < ieee80211_hdrlen(fc))
			err = -ENOBUFS;
		else
			err = skb_linearize(skb);
	} else {
		err = !pskb_may_pull(skb, ieee80211_hdrlen(fc));
	}

	if (err) {
		dev_kfree_skb(skb);
		return;
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	ieee80211_parse_qos(&rx);
	ieee80211_verify_alignment(&rx);

	if (unlikely(ieee80211_is_probe_resp(hdr->frame_control) ||
		     ieee80211_is_beacon(hdr->frame_control)))
		ieee80211_scan_rx(local, skb);

	if (pubsta) {
		rx.sta = container_of(pubsta, struct sta_info, sta);
		rx.sdata = rx.sta->sdata;
		if (ieee80211_prepare_and_rx_handle(&rx, skb, true))
			return;
		goto out;
	} else if (ieee80211_is_data(fc)) {
		struct sta_info *sta, *prev_sta;

		prev_sta = NULL;

		for_each_sta_info(local, hdr->addr2, sta, tmp) {
			if (!prev_sta) {
				prev_sta = sta;
				continue;
			}

			rx.sta = prev_sta;
			rx.sdata = prev_sta->sdata;
			ieee80211_prepare_and_rx_handle(&rx, skb, false);

			prev_sta = sta;
		}

		if (prev_sta) {
			rx.sta = prev_sta;
			rx.sdata = prev_sta->sdata;

			if (ieee80211_prepare_and_rx_handle(&rx, skb, true))
				return;
			goto out;
		}
	}

	prev = NULL;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_MONITOR ||
		    sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			continue;

		/*
		 * frame is destined for this interface, but if it's
		 * not also for the previous one we handle that after
		 * the loop to avoid copying the SKB once too much
		 */

		if (!prev) {
			prev = sdata;
			continue;
		}

		rx.sta = sta_info_get_bss(prev, hdr->addr2);
		rx.sdata = prev;
		ieee80211_prepare_and_rx_handle(&rx, skb, false);

		prev = sdata;
	}

	if (prev) {
		rx.sta = sta_info_get_bss(prev, hdr->addr2);
		rx.sdata = prev;

		if (ieee80211_prepare_and_rx_handle(&rx, skb, true))
			return;
	}

 out:
	dev_kfree_skb(skb);
}

/*
 * This is the receive path handler. It is called by a low level driver when an
 * 802.11 MPDU is received from the hardware.
 */
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *pubsta,
		       struct sk_buff *skb, struct napi_struct *napi)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);

	WARN_ON_ONCE(softirq_count() == 0);

	if (WARN_ON(status->band >= NUM_NL80211_BANDS))
		goto drop;

	sband = local->hw.wiphy->bands[status->band];
	if (WARN_ON(!sband))
		goto drop;

	/*
	 * If we're suspending, it is possible although not too likely
	 * that we'd be receiving frames after having already partially
	 * quiesced the stack. We can't process such frames then since
	 * that might, for example, cause stations to be added or other
	 * driver callbacks be invoked.
	 */
	if (unlikely(local->quiescing || local->suspended))
		goto drop;

	/* We might be during a HW reconfig, prevent Rx for the same reason */
	if (unlikely(local->in_reconfig))
		goto drop;

	/*
	 * The same happens when we're not even started,
	 * but that's worth a warning.
	 */
	if (WARN_ON(!local->started))
		goto drop;

	if (likely(!(status->flag & RX_FLAG_FAILED_PLCP_CRC))) {
		/*
		 * Validate the rate, unless a PLCP error means that
		 * we probably can't have a valid rate here anyway.
		 */

		if (status->flag & RX_FLAG_HT) {
			/*
			 * rate_idx is MCS index, which can be [0-76]
			 * as documented on:
			 *
			 * http://wireless.kernel.org/en/developers/Documentation/ieee80211/802.11n
			 *
			 * Anything else would be some sort of driver or
			 * hardware error. The driver should catch hardware
			 * errors.
			 */
			if (WARN(status->rate_idx > 76,
				 "Rate marked as an HT rate but passed "
				 "status->rate_idx is not "
				 "an MCS index [0-76]: %d (0x%02x)\n",
				 status->rate_idx,
				 status->rate_idx))
				goto drop;
		} else if (status->flag & RX_FLAG_VHT) {
			if (WARN_ONCE(status->rate_idx > 9 ||
				      !status->vht_nss ||
				      status->vht_nss > 8,
				      "Rate marked as a VHT rate but data is invalid: MCS: %d, NSS: %d\n",
				      status->rate_idx, status->vht_nss))
				goto drop;
		} else {
			if (WARN_ON(status->rate_idx >= sband->n_bitrates))
				goto drop;
			rate = &sband->bitrates[status->rate_idx];
		}
	}

	status->rx_flags = 0;

	/*
	 * key references and virtual interfaces are protected using RCU
	 * and this requires that we are in a read-side RCU section during
	 * receive processing
	 */
	rcu_read_lock();

	/*
	 * Frames with failed FCS/PLCP checksum are not returned,
	 * all other frames are returned without radiotap header
	 * if it was previously present.
	 * Also, frames with less than 16 bytes are dropped.
	 */
	skb = ieee80211_rx_monitor(local, skb, rate);
	if (!skb) {
		rcu_read_unlock();
		return;
	}

	ieee80211_tpt_led_trig_rx(local,
			((struct ieee80211_hdr *)skb->data)->frame_control,
			skb->len);

	__ieee80211_rx_handle_packet(hw, pubsta, skb, napi);

	rcu_read_unlock();

	return;
 drop:
	kfree_skb(skb);
}
EXPORT_SYMBOL(ieee80211_rx_napi);

/* This is a version of the rx handler that can be called from hard irq
 * context. Post the skb on the queue and schedule the tasklet */
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);

	BUILD_BUG_ON(sizeof(struct ieee80211_rx_status) > sizeof(skb->cb));

	skb->pkt_type = IEEE80211_RX_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee80211_rx_irqsafe);
