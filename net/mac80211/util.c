/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * utilities for mac80211
 */

#include <net/mac80211.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/bitmap.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"

/* privid for wiphys to determine whether they belong to us or not */
void *mac80211_wiphy_privid = &mac80211_wiphy_privid;

/* See IEEE 802.1H for LLC/SNAP encapsulation/decapsulation */
/* Ethernet-II snap header (RFC1042 for most EtherTypes) */
const unsigned char rfc1042_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };


u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum ieee80211_if_types type)
{
	u16 fc;

	 /* drop ACK/CTS frames and incorrect hdr len (ctrl) */
	if (len < 16)
		return NULL;

	fc = le16_to_cpu(hdr->frame_control);

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		if (len < 24) /* drop incorrect hdr len (data) */
			return NULL;
		switch (fc & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
		case IEEE80211_FCTL_TODS:
			return hdr->addr1;
		case (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
			return NULL;
		case IEEE80211_FCTL_FROMDS:
			return hdr->addr2;
		case 0:
			return hdr->addr3;
		}
		break;
	case IEEE80211_FTYPE_MGMT:
		if (len < 24) /* drop incorrect hdr len (mgmt) */
			return NULL;
		return hdr->addr3;
	case IEEE80211_FTYPE_CTL:
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)
			return hdr->addr1;
		else if ((fc & IEEE80211_FCTL_STYPE) ==
						IEEE80211_STYPE_BACK_REQ) {
			switch (type) {
			case IEEE80211_IF_TYPE_STA:
				return hdr->addr2;
			case IEEE80211_IF_TYPE_AP:
			case IEEE80211_IF_TYPE_VLAN:
				return hdr->addr1;
			default:
				return NULL;
			}
		}
		else
			return NULL;
	}

	return NULL;
}

int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = 30; /* Addr4 */
		/*
		 * The QoS Control field is two bytes and its presence is
		 * indicated by the IEEE80211_STYPE_QOS_DATA bit. Add 2 to
		 * hdrlen if that bit is set.
		 * This works by masking out the bit and shifting it to
		 * bit position 1 so the result has the value 0 or 2.
		 */
		hdrlen += (fc & IEEE80211_STYPE_QOS_DATA)
				>> (ilog2(IEEE80211_STYPE_QOS_DATA)-1);
		break;
	case IEEE80211_FTYPE_CTL:
		/*
		 * ACK and CTS are 10 bytes, all others 16. To see how
		 * to get this condition consider
		 *   subtype mask:   0b0000000011110000 (0x00F0)
		 *   ACK subtype:    0b0000000011010000 (0x00D0)
		 *   CTS subtype:    0b0000000011000000 (0x00C0)
		 *   bits that matter:         ^^^      (0x00E0)
		 *   value of those: 0b0000000011000000 (0x00C0)
		 */
		if ((fc & 0xE0) == 0xC0)
			hdrlen = 10;
		else
			hdrlen = 16;
		break;
	}

	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen);

int ieee80211_get_hdrlen_from_skb(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *) skb->data;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;
	hdrlen = ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_control));
	if (unlikely(hdrlen > skb->len))
		return 0;
	return hdrlen;
}
EXPORT_SYMBOL(ieee80211_get_hdrlen_from_skb);

int ieee80211_get_mesh_hdrlen(struct ieee80211s_hdr *meshhdr)
{
	int ae = meshhdr->flags & IEEE80211S_FLAGS_AE;
	/* 7.1.3.5a.2 */
	switch (ae) {
	case 0:
		return 5;
	case 1:
		return 11;
	case 2:
		return 17;
	case 3:
		return 23;
	default:
		return 5;
	}
}

void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	if (tx->extra_frag) {
		struct ieee80211_hdr *fhdr;
		int i;
		for (i = 0; i < tx->num_extra_frag; i++) {
			fhdr = (struct ieee80211_hdr *)
				tx->extra_frag[i]->data;
			fhdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		}
	}
}

int ieee80211_frame_duration(struct ieee80211_local *local, size_t len,
			     int rate, int erp, int short_preamble)
{
	int dur;

	/* calculate duration (in microseconds, rounded up to next higher
	 * integer if it includes a fractional microsecond) to send frame of
	 * len bytes (does not include FCS) at the given rate. Duration will
	 * also include SIFS.
	 *
	 * rate is in 100 kbps, so divident is multiplied by 10 in the
	 * DIV_ROUND_UP() operations.
	 */

	if (local->hw.conf.channel->band == IEEE80211_BAND_5GHZ || erp) {
		/*
		 * OFDM:
		 *
		 * N_DBPS = DATARATE x 4
		 * N_SYM = Ceiling((16+8xLENGTH+6) / N_DBPS)
		 *	(16 = SIGNAL time, 6 = tail bits)
		 * TXTIME = T_PREAMBLE + T_SIGNAL + T_SYM x N_SYM + Signal Ext
		 *
		 * T_SYM = 4 usec
		 * 802.11a - 17.5.2: aSIFSTime = 16 usec
		 * 802.11g - 19.8.4: aSIFSTime = 10 usec +
		 *	signal ext = 6 usec
		 */
		dur = 16; /* SIFS + signal ext */
		dur += 16; /* 17.3.2.3: T_PREAMBLE = 16 usec */
		dur += 4; /* 17.3.2.3: T_SIGNAL = 4 usec */
		dur += 4 * DIV_ROUND_UP((16 + 8 * (len + 4) + 6) * 10,
					4 * rate); /* T_SYM x N_SYM */
	} else {
		/*
		 * 802.11b or 802.11g with 802.11b compatibility:
		 * 18.3.4: TXTIME = PreambleLength + PLCPHeaderTime +
		 * Ceiling(((LENGTH+PBCC)x8)/DATARATE). PBCC=0.
		 *
		 * 802.11 (DS): 15.3.3, 802.11b: 18.3.4
		 * aSIFSTime = 10 usec
		 * aPreambleLength = 144 usec or 72 usec with short preamble
		 * aPLCPHeaderLength = 48 usec or 24 usec with short preamble
		 */
		dur = 10; /* aSIFSTime = 10 usec */
		dur += short_preamble ? (72 + 24) : (144 + 48);

		dur += DIV_ROUND_UP(8 * (len + 4) * 10, rate);
	}

	return dur;
}

/* Exported duration function for driver use */
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	u16 dur;
	int erp;

	erp = 0;
	if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
		erp = rate->flags & IEEE80211_RATE_ERP_G;

	dur = ieee80211_frame_duration(local, frame_len, rate->bitrate, erp,
				       sdata->bss_conf.use_short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_generic_frame_duration);

__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, size_t frame_len,
			      const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	bool short_preamble;
	int erp;
	u16 dur;

	short_preamble = sdata->bss_conf.use_short_preamble;

	rate = frame_txctl->rts_cts_rate;

	erp = 0;
	if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
		erp = rate->flags & IEEE80211_RATE_ERP_G;

	/* CTS duration */
	dur = ieee80211_frame_duration(local, 10, rate->bitrate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(local, frame_len, rate->bitrate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(local, 10, rate->bitrate,
					erp, short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_rts_duration);

__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    size_t frame_len,
				    const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	bool short_preamble;
	int erp;
	u16 dur;

	short_preamble = sdata->bss_conf.use_short_preamble;

	rate = frame_txctl->rts_cts_rate;
	erp = 0;
	if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
		erp = rate->flags & IEEE80211_RATE_ERP_G;

	/* Data frame duration */
	dur = ieee80211_frame_duration(local, frame_len, rate->bitrate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TXCTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(local, 10, rate->bitrate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (test_and_clear_bit(IEEE80211_LINK_STATE_XOFF,
			       &local->state[queue])) {
		if (test_bit(IEEE80211_LINK_STATE_PENDING,
			     &local->state[queue]))
			tasklet_schedule(&local->tx_pending_tasklet);
		else
			if (!ieee80211_qdisc_installed(local->mdev)) {
				if (queue == 0)
					netif_wake_queue(local->mdev);
			} else
				__netif_schedule(local->mdev);
	}
}
EXPORT_SYMBOL(ieee80211_wake_queue);

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_qdisc_installed(local->mdev) && queue == 0)
		netif_stop_queue(local->mdev);
	set_bit(IEEE80211_LINK_STATE_XOFF, &local->state[queue]);
}
EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_start_queues(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	int i;

	for (i = 0; i < local->hw.queues; i++)
		clear_bit(IEEE80211_LINK_STATE_XOFF, &local->state[i]);
	if (!ieee80211_qdisc_installed(local->mdev))
		netif_start_queue(local->mdev);
}
EXPORT_SYMBOL(ieee80211_start_queues);

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_stop_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_stop_queues);

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues; i++)
		ieee80211_wake_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_wake_queues);

void ieee80211_iterate_active_interfaces(
	struct ieee80211_hw *hw,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case IEEE80211_IF_TYPE_INVALID:
		case IEEE80211_IF_TYPE_MNTR:
		case IEEE80211_IF_TYPE_VLAN:
			continue;
		case IEEE80211_IF_TYPE_AP:
		case IEEE80211_IF_TYPE_STA:
		case IEEE80211_IF_TYPE_IBSS:
		case IEEE80211_IF_TYPE_WDS:
		case IEEE80211_IF_TYPE_MESH_POINT:
			break;
		}
		if (sdata->dev == local->mdev)
			continue;
		if (netif_running(sdata->dev))
			iterator(data, sdata->dev->dev_addr,
				 &sdata->vif);
	}

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces);
