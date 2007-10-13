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

#include "ieee80211_i.h"
#include "ieee80211_rate.h"
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

/* No encapsulation header if EtherType < 0x600 (=length) */
static const unsigned char eapol_header[] =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };


static int rate_list_match(const int *rate_list, int rate)
{
	int i;

	if (!rate_list)
		return 0;

	for (i = 0; rate_list[i] >= 0; i++)
		if (rate_list[i] == rate)
			return 1;

	return 0;
}

void ieee80211_prepare_rates(struct ieee80211_local *local,
			     struct ieee80211_hw_mode *mode)
{
	int i;

	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];

		rate->flags &= ~(IEEE80211_RATE_SUPPORTED |
				 IEEE80211_RATE_BASIC);

		if (local->supp_rates[mode->mode]) {
			if (!rate_list_match(local->supp_rates[mode->mode],
					     rate->rate))
				continue;
		}

		rate->flags |= IEEE80211_RATE_SUPPORTED;

		/* Use configured basic rate set if it is available. If not,
		 * use defaults that are sane for most cases. */
		if (local->basic_rates[mode->mode]) {
			if (rate_list_match(local->basic_rates[mode->mode],
					    rate->rate))
				rate->flags |= IEEE80211_RATE_BASIC;
		} else switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10 || rate->rate == 20)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110)
				rate->flags |= IEEE80211_RATE_BASIC;
			break;
		case NUM_IEEE80211_MODES:
			/* not useful */
			break;
		}

		/* Set ERP and MANDATORY flags based on phymode */
		switch (mode->mode) {
		case MODE_IEEE80211A:
			if (rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_IEEE80211B:
			if (rate->rate == 10)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case MODE_IEEE80211G:
			if (rate->rate == 10 || rate->rate == 20 ||
			    rate->rate == 55 || rate->rate == 110 ||
			    rate->rate == 60 || rate->rate == 120 ||
			    rate->rate == 240)
				rate->flags |= IEEE80211_RATE_MANDATORY;
			break;
		case NUM_IEEE80211_MODES:
			/* not useful */
			break;
		}
		if (ieee80211_is_erp_rate(mode->mode, rate->rate))
			rate->flags |= IEEE80211_RATE_ERP;
	}
}

u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len)
{
	u16 fc;

	if (len < 24)
		return NULL;

	fc = le16_to_cpu(hdr->frame_control);

	switch (fc & IEEE80211_FCTL_FTYPE) {
	case IEEE80211_FTYPE_DATA:
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
		return hdr->addr3;
	case IEEE80211_FTYPE_CTL:
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)
			return hdr->addr1;
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

int ieee80211_is_eapol(const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr;
	u16 fc;
	int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;

	hdr = (const struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	if (unlikely(!WLAN_FC_DATA_PRESENT(fc)))
		return 0;

	hdrlen = ieee80211_get_hdrlen(fc);

	if (unlikely(skb->len >= hdrlen + sizeof(eapol_header) &&
		     memcmp(skb->data + hdrlen, eapol_header,
			    sizeof(eapol_header)) == 0))
		return 1;

	return 0;
}

void ieee80211_tx_set_iswep(struct ieee80211_txrx_data *tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx->skb->data;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	if (tx->u.tx.extra_frag) {
		struct ieee80211_hdr *fhdr;
		int i;
		for (i = 0; i < tx->u.tx.num_extra_frag; i++) {
			fhdr = (struct ieee80211_hdr *)
				tx->u.tx.extra_frag[i]->data;
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

	if (local->hw.conf.phymode == MODE_IEEE80211A || erp) {
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
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw, int if_id,
					size_t frame_len, int rate)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct net_device *bdev = dev_get_by_index(&init_net, if_id);
	struct ieee80211_sub_if_data *sdata;
	u16 dur;
	int erp;

	if (unlikely(!bdev))
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
	erp = ieee80211_is_erp_rate(hw->conf.phymode, rate);
	dur = ieee80211_frame_duration(local, frame_len, rate,
		       erp, sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE);

	dev_put(bdev);
	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_generic_frame_duration);

__le16 ieee80211_rts_duration(struct ieee80211_hw *hw, int if_id,
			      size_t frame_len,
			      const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct net_device *bdev = dev_get_by_index(&init_net, if_id);
	struct ieee80211_sub_if_data *sdata;
	int short_preamble;
	int erp;
	u16 dur;

	if (unlikely(!bdev))
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
	short_preamble = sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* CTS duration */
	dur = ieee80211_frame_duration(local, 10, rate->rate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(local, frame_len, rate->rate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(local, 10, rate->rate,
					erp, short_preamble);

	dev_put(bdev);
	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_rts_duration);

__le16 ieee80211_ctstoself_duration(struct ieee80211_hw *hw, int if_id,
				    size_t frame_len,
				    const struct ieee80211_tx_control *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct net_device *bdev = dev_get_by_index(&init_net, if_id);
	struct ieee80211_sub_if_data *sdata;
	int short_preamble;
	int erp;
	u16 dur;

	if (unlikely(!bdev))
		return 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(bdev);
	short_preamble = sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE;

	rate = frame_txctl->rts_rate;
	erp = !!(rate->flags & IEEE80211_RATE_ERP);

	/* Data frame duration */
	dur = ieee80211_frame_duration(local, frame_len, rate->rate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TXCTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(local, 10, rate->rate,
						erp, short_preamble);
	}

	dev_put(bdev);
	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

struct ieee80211_rate *
ieee80211_get_rate(struct ieee80211_local *local, int phymode, int hw_rate)
{
	struct ieee80211_hw_mode *mode;
	int r;

	list_for_each_entry(mode, &local->modes_list, list) {
		if (mode->mode != phymode)
			continue;
		for (r = 0; r < mode->num_rates; r++) {
			struct ieee80211_rate *rate = &mode->rates[r];
			if (rate->val == hw_rate ||
			    (rate->flags & IEEE80211_RATE_PREAMBLE2 &&
			     rate->val2 == hw_rate))
				return rate;
		}
	}

	return NULL;
}

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
