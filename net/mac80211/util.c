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
const unsigned char rfc1042_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
const unsigned char bridge_tunnel_header[] __aligned(2) =
	{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };


u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type)
{
	__le16 fc = hdr->frame_control;

	 /* drop ACK/CTS frames and incorrect hdr len (ctrl) */
	if (len < 16)
		return NULL;

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
		if(ieee80211_is_pspoll(fc))
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

unsigned int ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;

	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc))
			hdrlen = 30;
		if (ieee80211_is_data_qos(fc))
			hdrlen += IEEE80211_QOS_CTL_LEN;
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
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;

	if (unlikely(skb->len < 10))
		return 0;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
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
		return 6;
	case 1:
		return 12;
	case 2:
		return 18;
	case 3:
		return 24;
	default:
		return 6;
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
			      const struct ieee80211_tx_info *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	short_preamble = sdata->bss_conf.use_short_preamble;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];

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
				    const struct ieee80211_tx_info *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	short_preamble = sdata->bss_conf.use_short_preamble;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];
	erp = 0;
	if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
		erp = rate->flags & IEEE80211_RATE_ERP_G;

	/* Data frame duration */
	dur = ieee80211_frame_duration(local, frame_len, rate->bitrate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TX_CTL_NO_ACK)) {
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

	if (test_bit(queue, local->queues_pending)) {
		set_bit(queue, local->queues_pending_run);
		tasklet_schedule(&local->tx_pending_tasklet);
	} else {
		netif_wake_subqueue(local->mdev, queue);
	}
}
EXPORT_SYMBOL(ieee80211_wake_queue);

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);

	netif_stop_subqueue(local->mdev, queue);
}
EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < ieee80211_num_queues(hw); i++)
		ieee80211_stop_queue(hw, i);
}
EXPORT_SYMBOL(ieee80211_stop_queues);

int ieee80211_queue_stopped(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);
	return __netif_subqueue_stopped(local->mdev, queue);
}
EXPORT_SYMBOL(ieee80211_queue_stopped);

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	int i;

	for (i = 0; i < hw->queues + hw->ampdu_queues; i++)
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

	rtnl_lock();

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case __NL80211_IFTYPE_AFTER_LAST:
		case NL80211_IFTYPE_UNSPECIFIED:
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MESH_POINT:
			break;
		}
		if (netif_running(sdata->dev))
			iterator(data, sdata->dev->dev_addr,
				 &sdata->vif);
	}

	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces);

void ieee80211_iterate_active_interfaces_atomic(
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
		case __NL80211_IFTYPE_AFTER_LAST:
		case NL80211_IFTYPE_UNSPECIFIED:
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MESH_POINT:
			break;
		}
		if (netif_running(sdata->dev))
			iterator(data, sdata->dev->dev_addr,
				 &sdata->vif);
	}

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_atomic);

void ieee802_11_parse_elems(u8 *start, size_t len,
			    struct ieee802_11_elems *elems)
{
	size_t left = len;
	u8 *pos = start;

	memset(elems, 0, sizeof(*elems));
	elems->ie_start = start;
	elems->total_len = len;

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			return;

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_WPA:
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */
				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			break;
		case WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_HT_CAPABILITY:
			elems->ht_cap_elem = pos;
			elems->ht_cap_elem_len = elen;
			break;
		case WLAN_EID_HT_EXTRA_INFO:
			elems->ht_info_elem = pos;
			elems->ht_info_elem_len = elen;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_MESH_CONFIG:
			elems->mesh_config = pos;
			elems->mesh_config_len = elen;
			break;
		case WLAN_EID_PEER_LINK:
			elems->peer_link = pos;
			elems->peer_link_len = elen;
			break;
		case WLAN_EID_PREQ:
			elems->preq = pos;
			elems->preq_len = elen;
			break;
		case WLAN_EID_PREP:
			elems->prep = pos;
			elems->prep_len = elen;
			break;
		case WLAN_EID_PERR:
			elems->perr = pos;
			elems->perr_len = elen;
			break;
		case WLAN_EID_CHANNEL_SWITCH:
			elems->ch_switch_elem = pos;
			elems->ch_switch_elem_len = elen;
			break;
		case WLAN_EID_QUIET:
			if (!elems->quiet_elem) {
				elems->quiet_elem = pos;
				elems->quiet_elem_len = elen;
			}
			elems->num_of_quiet_elem++;
			break;
		case WLAN_EID_COUNTRY:
			elems->country_elem = pos;
			elems->country_elem_len = elen;
			break;
		case WLAN_EID_PWR_CONSTRAINT:
			elems->pwr_constr_elem = pos;
			elems->pwr_constr_elem_len = elen;
			break;
		default:
			break;
		}

		left -= elen;
		pos += elen;
	}
}

void ieee80211_set_wmm_default(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_queue_params qparam;
	int i;

	if (!local->ops->conf_tx)
		return;

	memset(&qparam, 0, sizeof(qparam));

	qparam.aifs = 2;

	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ &&
	    !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE))
		qparam.cw_min = 31;
	else
		qparam.cw_min = 15;

	qparam.cw_max = 1023;
	qparam.txop = 0;

	for (i = 0; i < local_to_hw(local)->queues; i++)
		local->ops->conf_tx(local_to_hw(local), i, &qparam);
}

void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb,
		      int encrypt)
{
	skb->dev = sdata->local->mdev;
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, 0);
	skb_set_transport_header(skb, 0);

	skb->iif = sdata->dev->ifindex;
	skb->do_not_encrypt = !encrypt;

	dev_queue_xmit(skb);
}

int ieee80211_set_freq(struct ieee80211_sub_if_data *sdata, int freqMHz)
{
	int ret = -EINVAL;
	struct ieee80211_channel *chan;
	struct ieee80211_local *local = sdata->local;

	chan = ieee80211_get_channel(local->hw.wiphy, freqMHz);

	if (chan && !(chan->flags & IEEE80211_CHAN_DISABLED)) {
		if (sdata->vif.type == NL80211_IFTYPE_ADHOC &&
		    chan->flags & IEEE80211_CHAN_NO_IBSS) {
			printk(KERN_DEBUG "%s: IBSS not allowed on frequency "
				"%d MHz\n", sdata->dev->name, chan->center_freq);
			return ret;
		}
		local->oper_channel = chan;

		if (local->sw_scanning || local->hw_scanning)
			ret = 0;
		else
			ret = ieee80211_hw_config(local);

		rate_control_clear(local);
	}

	return ret;
}

u64 ieee80211_mandatory_rates(struct ieee80211_local *local,
			      enum ieee80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	u64 mandatory_rates;
	enum ieee80211_rate_flags mandatory_flag;
	int i;

	sband = local->hw.wiphy->bands[band];
	if (!sband) {
		WARN_ON(1);
		sband = local->hw.wiphy->bands[local->hw.conf.channel->band];
	}

	if (band == IEEE80211_BAND_2GHZ)
		mandatory_flag = IEEE80211_RATE_MANDATORY_B;
	else
		mandatory_flag = IEEE80211_RATE_MANDATORY_A;

	bitrates = sband->bitrates;
	mandatory_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++)
		if (bitrates[i].flags & mandatory_flag)
			mandatory_rates |= BIT(i);
	return mandatory_rates;
}
