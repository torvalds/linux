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
#include <linux/export.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"
#include "led.h"
#include "wep.h"

/* privid for wiphys to determine whether they belong to us or not */
void *mac80211_wiphy_privid = &mac80211_wiphy_privid;

struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct ieee80211_local *local;
	BUG_ON(!wiphy);

	local = wiphy_priv(wiphy);
	return &local->hw;
}
EXPORT_SYMBOL(wiphy_to_ieee80211_hw);

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

void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	skb_queue_walk(&tx->skbs, skb) {
		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}
}

int ieee80211_frame_duration(enum ieee80211_band band, size_t len,
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

	if (band == IEEE80211_BAND_5GHZ || erp) {
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
					enum ieee80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	struct ieee80211_sub_if_data *sdata;
	u16 dur;
	int erp;
	bool short_preamble = false;

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	dur = ieee80211_frame_duration(band, frame_len, rate->bitrate, erp,
				       short_preamble);

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_generic_frame_duration);

__le16 ieee80211_rts_duration(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, size_t frame_len,
			      const struct ieee80211_tx_info *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata;
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	/* CTS duration */
	dur = ieee80211_frame_duration(sband->band, 10, rate->bitrate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(sband->band, frame_len, rate->bitrate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(sband->band, 10, rate->bitrate,
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
	struct ieee80211_sub_if_data *sdata;
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];
	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	/* Data frame duration */
	dur = ieee80211_frame_duration(sband->band, frame_len, rate->bitrate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TX_CTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(sband->band, 10, rate->bitrate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

void ieee80211_propagate_queue_wake(struct ieee80211_local *local, int queue)
{
	struct ieee80211_sub_if_data *sdata;
	int n_acs = IEEE80211_NUM_ACS;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		n_acs = 1;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		if (!sdata->dev)
			continue;

		if (test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state))
			continue;

		if (sdata->vif.cab_queue != IEEE80211_INVAL_HW_QUEUE &&
		    local->queue_stop_reasons[sdata->vif.cab_queue] != 0)
			continue;

		for (ac = 0; ac < n_acs; ac++) {
			int ac_queue = sdata->vif.hw_queue[ac];

			if (ac_queue == queue ||
			    (sdata->vif.cab_queue == queue &&
			     local->queue_stop_reasons[ac_queue] == 0 &&
			     skb_queue_empty(&local->pending[ac_queue])))
				netif_wake_subqueue(sdata->dev, ac);
		}
	}
}

static void __ieee80211_wake_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_wake_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__clear_bit(reason, &local->queue_stop_reasons[queue]);

	if (local->queue_stop_reasons[queue] != 0)
		/* someone still has this queue stopped */
		return;

	if (skb_queue_empty(&local->pending[queue])) {
		rcu_read_lock();
		ieee80211_propagate_queue_wake(local, queue);
		rcu_read_unlock();
	} else
		tasklet_schedule(&local->tx_pending_tasklet);
}

void ieee80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_wake_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_wake_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
EXPORT_SYMBOL(ieee80211_wake_queue);

static void __ieee80211_stop_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	int n_acs = IEEE80211_NUM_ACS;

	trace_stop_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__set_bit(reason, &local->queue_stop_reasons[queue]);

	if (local->hw.queues < IEEE80211_NUM_ACS)
		n_acs = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		if (!sdata->dev)
			continue;

		for (ac = 0; ac < n_acs; ac++) {
			if (sdata->vif.hw_queue[ac] == queue ||
			    sdata->vif.cab_queue == queue)
				netif_stop_subqueue(sdata->dev, ac);
		}
	}
	rcu_read_unlock();
}

void ieee80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_stop_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_stop_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb)
{
	struct ieee80211_hw *hw = &local->hw;
	unsigned long flags;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int queue = info->hw_queue;

	if (WARN_ON(!info->control.vif)) {
		ieee80211_free_txskb(&local->hw, skb);
		return;
	}

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_stop_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	__skb_queue_tail(&local->pending[queue], skb);
	__ieee80211_wake_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_add_pending_skbs_fn(struct ieee80211_local *local,
				   struct sk_buff_head *skbs,
				   void (*fn)(void *data), void *data)
{
	struct ieee80211_hw *hw = &local->hw;
	struct sk_buff *skb;
	unsigned long flags;
	int queue, i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	while ((skb = skb_dequeue(skbs))) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (WARN_ON(!info->control.vif)) {
			ieee80211_free_txskb(&local->hw, skb);
			continue;
		}

		queue = info->hw_queue;

		__ieee80211_stop_queue(hw, queue,
				IEEE80211_QUEUE_STOP_REASON_SKB_ADD);

		__skb_queue_tail(&local->pending[queue], skb);
	}

	if (fn)
		fn(data);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_wake_queue(hw, i,
			IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_stop_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	ieee80211_stop_queues_by_reason(hw,
					IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
EXPORT_SYMBOL(ieee80211_stop_queues);

int ieee80211_queue_stopped(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int ret;

	if (WARN_ON(queue >= hw->queues))
		return true;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	ret = !!local->queue_stop_reasons[queue];
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ieee80211_queue_stopped);

void ieee80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_wake_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	ieee80211_wake_queues_by_reason(hw, IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
EXPORT_SYMBOL(ieee80211_wake_queues);

void ieee80211_iterate_active_interfaces(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (!(iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL) &&
		    !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
			continue;
		if (ieee80211_sdata_running(sdata))
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	sdata = rcu_dereference_protected(local->monitor_sdata,
					  lockdep_is_held(&local->iflist_mtx));
	if (sdata &&
	    (iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL ||
	     sdata->flags & IEEE80211_SDATA_IN_DRIVER))
		iterator(data, sdata->vif.addr, &sdata->vif);

	mutex_unlock(&local->iflist_mtx);
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces);

void ieee80211_iterate_active_interfaces_atomic(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (!(iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL) &&
		    !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
			continue;
		if (ieee80211_sdata_running(sdata))
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	sdata = rcu_dereference(local->monitor_sdata);
	if (sdata &&
	    (iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL ||
	     sdata->flags & IEEE80211_SDATA_IN_DRIVER))
		iterator(data, sdata->vif.addr, &sdata->vif);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_atomic);

/*
 * Nothing should have been stuffed into the workqueue during
 * the suspend->resume cycle. If this WARN is seen then there
 * is a bug with either the driver suspend or something in
 * mac80211 stuffing into the workqueue which we haven't yet
 * cleared during mac80211's suspend cycle.
 */
static bool ieee80211_can_queue_work(struct ieee80211_local *local)
{
	if (WARN(local->suspended && !local->resuming,
		 "queueing ieee80211 work while going to suspend\n"))
		return false;

	return true;
}

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	queue_work(local->workqueue, work);
}
EXPORT_SYMBOL(ieee80211_queue_work);

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct delayed_work *dwork,
				  unsigned long delay)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	queue_delayed_work(local->workqueue, dwork, delay);
}
EXPORT_SYMBOL(ieee80211_queue_delayed_work);

u32 ieee802_11_parse_elems_crc(u8 *start, size_t len,
			       struct ieee802_11_elems *elems,
			       u64 filter, u32 crc)
{
	size_t left = len;
	u8 *pos = start;
	bool calc_crc = filter != 0;
	DECLARE_BITMAP(seen_elems, 256);

	bitmap_zero(seen_elems, 256);
	memset(elems, 0, sizeof(*elems));
	elems->ie_start = start;
	elems->total_len = len;

	while (left >= 2) {
		u8 id, elen;
		bool elem_parse_failed;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			elems->parse_error = true;
			break;
		}

		switch (id) {
		case WLAN_EID_SSID:
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_FH_PARAMS:
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_CF_PARAMS:
		case WLAN_EID_TIM:
		case WLAN_EID_IBSS_PARAMS:
		case WLAN_EID_CHALLENGE:
		case WLAN_EID_RSN:
		case WLAN_EID_ERP_INFO:
		case WLAN_EID_EXT_SUPP_RATES:
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_HT_OPERATION:
		case WLAN_EID_VHT_CAPABILITY:
		case WLAN_EID_VHT_OPERATION:
		case WLAN_EID_MESH_ID:
		case WLAN_EID_MESH_CONFIG:
		case WLAN_EID_PEER_MGMT:
		case WLAN_EID_PREQ:
		case WLAN_EID_PREP:
		case WLAN_EID_PERR:
		case WLAN_EID_RANN:
		case WLAN_EID_CHANNEL_SWITCH:
		case WLAN_EID_EXT_CHANSWITCH_ANN:
		case WLAN_EID_COUNTRY:
		case WLAN_EID_PWR_CONSTRAINT:
		case WLAN_EID_TIMEOUT_INTERVAL:
			if (test_bit(id, seen_elems)) {
				elems->parse_error = true;
				left -= elen;
				pos += elen;
				continue;
			}
			break;
		}

		if (calc_crc && id < 64 && (filter & (1ULL << id)))
			crc = crc32_be(crc, pos - 2, elen + 2);

		elem_parse_failed = false;

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
			if (elen >= sizeof(struct ieee80211_tim_ie)) {
				elems->tim = (void *)pos;
				elems->tim_len = elen;
			} else
				elem_parse_failed = true;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */

				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);

				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					/* OUI Type 2 - WMM IE */
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
			if (elen >= sizeof(struct ieee80211_ht_cap))
				elems->ht_cap_elem = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_HT_OPERATION:
			if (elen >= sizeof(struct ieee80211_ht_operation))
				elems->ht_operation = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_VHT_CAPABILITY:
			if (elen >= sizeof(struct ieee80211_vht_cap))
				elems->vht_cap_elem = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_VHT_OPERATION:
			if (elen >= sizeof(struct ieee80211_vht_operation))
				elems->vht_operation = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_MESH_CONFIG:
			if (elen >= sizeof(struct ieee80211_meshconf_ie))
				elems->mesh_config = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_PEER_MGMT:
			elems->peering = pos;
			elems->peering_len = elen;
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
		case WLAN_EID_RANN:
			if (elen >= sizeof(struct ieee80211_rann_ie))
				elems->rann = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_CHANNEL_SWITCH:
			if (elen != sizeof(struct ieee80211_channel_sw_ie)) {
				elem_parse_failed = true;
				break;
			}
			elems->ch_switch_ie = (void *)pos;
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
			if (elen != 1) {
				elem_parse_failed = true;
				break;
			}
			elems->pwr_constr_elem = pos;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		default:
			break;
		}

		if (elem_parse_failed)
			elems->parse_error = true;
		else
			__set_bit(id, seen_elems);

		left -= elen;
		pos += elen;
	}

	if (left != 0)
		elems->parse_error = true;

	return crc;
}

void ieee802_11_parse_elems(u8 *start, size_t len,
			    struct ieee802_11_elems *elems)
{
	ieee802_11_parse_elems_crc(start, len, elems, 0, 0);
}

void ieee80211_set_wmm_default(struct ieee80211_sub_if_data *sdata,
			       bool bss_notify)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_queue_params qparam;
	struct ieee80211_chanctx_conf *chanctx_conf;
	int ac;
	bool use_11b, enable_qos;
	int aCWmin, aCWmax;

	if (!local->ops->conf_tx)
		return;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return;

	memset(&qparam, 0, sizeof(qparam));

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	use_11b = (chanctx_conf &&
		   chanctx_conf->channel->band == IEEE80211_BAND_2GHZ) &&
		 !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE);
	rcu_read_unlock();

	/*
	 * By default disable QoS in STA mode for old access points, which do
	 * not support 802.11e. New APs will provide proper queue parameters,
	 * that we will configure later.
	 */
	enable_qos = (sdata->vif.type != NL80211_IFTYPE_STATION);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		/* Set defaults according to 802.11-2007 Table 7-37 */
		aCWmax = 1023;
		if (use_11b)
			aCWmin = 31;
		else
			aCWmin = 15;

		if (enable_qos) {
			switch (ac) {
			case IEEE80211_AC_BK:
				qparam.cw_max = aCWmax;
				qparam.cw_min = aCWmin;
				qparam.txop = 0;
				qparam.aifs = 7;
				break;
			/* never happens but let's not leave undefined */
			default:
			case IEEE80211_AC_BE:
				qparam.cw_max = aCWmax;
				qparam.cw_min = aCWmin;
				qparam.txop = 0;
				qparam.aifs = 3;
				break;
			case IEEE80211_AC_VI:
				qparam.cw_max = aCWmin;
				qparam.cw_min = (aCWmin + 1) / 2 - 1;
				if (use_11b)
					qparam.txop = 6016/32;
				else
					qparam.txop = 3008/32;
				qparam.aifs = 2;
				break;
			case IEEE80211_AC_VO:
				qparam.cw_max = (aCWmin + 1) / 2 - 1;
				qparam.cw_min = (aCWmin + 1) / 4 - 1;
				if (use_11b)
					qparam.txop = 3264/32;
				else
					qparam.txop = 1504/32;
				qparam.aifs = 2;
				break;
			}
		} else {
			/* Confiure old 802.11b/g medium access rules. */
			qparam.cw_max = aCWmax;
			qparam.cw_min = aCWmin;
			qparam.txop = 0;
			qparam.aifs = 2;
		}

		qparam.uapsd = false;

		sdata->tx_conf[ac] = qparam;
		drv_conf_tx(local, sdata, ac, &qparam);
	}

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE) {
		sdata->vif.bss_conf.qos = enable_qos;
		if (bss_notify)
			ieee80211_bss_info_change_notify(sdata,
							 BSS_CHANGED_QOS);
	}
}

void ieee80211_sta_def_wmm_params(struct ieee80211_sub_if_data *sdata,
				  const size_t supp_rates_len,
				  const u8 *supp_rates)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	int i, have_higher_than_11mbit = 0;

	/* cf. IEEE 802.11 9.2.12 */
	for (i = 0; i < supp_rates_len; i++)
		if ((supp_rates[i] & 0x7f) * 5 > 110)
			have_higher_than_11mbit = 1;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);

	if (chanctx_conf &&
	    chanctx_conf->channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;
	rcu_read_unlock();

	ieee80211_set_wmm_default(sdata, true);
}

u32 ieee80211_mandatory_rates(struct ieee80211_local *local,
			      enum ieee80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	u32 mandatory_rates;
	enum ieee80211_rate_flags mandatory_flag;
	int i;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return 1;

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

void ieee80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg, u16 status,
			 u8 *extra, size_t extra_len, const u8 *da,
			 const u8 *bssid, const u8 *key, u8 key_len, u8 key_idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	int err;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 6 + extra_len);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24 + 6);
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_AUTH);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	mgmt->u.auth.status_code = cpu_to_le16(status);
	if (extra)
		memcpy(skb_put(skb, extra_len), extra, extra_len);

	if (auth_alg == WLAN_AUTH_SHARED_KEY && transaction == 3) {
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		err = ieee80211_wep_encrypt(local, skb, key, key_len, key_idx);
		WARN_ON(err);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
				    const u8 *bssid, u16 stype, u16 reason,
				    bool send_frame, u8 *frame_buf)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt = (void *)frame_buf;

	/* build frame */
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	mgmt->duration = 0; /* initialize only */
	mgmt->seq_ctrl = 0; /* initialize only */
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	/* u.deauth.reason_code == u.disassoc.reason_code */
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	if (send_frame) {
		skb = dev_alloc_skb(local->hw.extra_tx_headroom +
				    IEEE80211_DEAUTH_FRAME_LEN);
		if (!skb)
			return;

		skb_reserve(skb, local->hw.extra_tx_headroom);

		/* copy in frame */
		memcpy(skb_put(skb, IEEE80211_DEAUTH_FRAME_LEN),
		       mgmt, IEEE80211_DEAUTH_FRAME_LEN);

		if (sdata->vif.type != NL80211_IFTYPE_STATION ||
		    !(sdata->u.mgd.flags & IEEE80211_STA_MFP_ENABLED))
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_INTFL_DONT_ENCRYPT;

		ieee80211_tx_skb(sdata, skb);
	}
}

int ieee80211_build_preq_ies(struct ieee80211_local *local, u8 *buffer,
			     const u8 *ie, size_t ie_len,
			     enum ieee80211_band band, u32 rate_mask,
			     u8 channel)
{
	struct ieee80211_supported_band *sband;
	u8 *pos;
	size_t offset = 0, noffset;
	int supp_rates_len, i;
	u8 rates[32];
	int num_rates;
	int ext_rates_len;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON_ONCE(!sband))
		return 0;

	pos = buffer;

	num_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((BIT(i) & rate_mask) == 0)
			continue; /* skip rate */
		rates[num_rates++] = (u8) (sband->bitrates[i].bitrate / 5);
	}

	supp_rates_len = min_t(int, num_rates, 8);

	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;
	memcpy(pos, rates, supp_rates_len);
	pos += supp_rates_len;

	/* insert "request information" if in custom IEs */
	if (ie && ie_len) {
		static const u8 before_extrates[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
		};
		noffset = ieee80211_ie_split(ie, ie_len,
					     before_extrates,
					     ARRAY_SIZE(before_extrates),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	ext_rates_len = num_rates - supp_rates_len;
	if (ext_rates_len > 0) {
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = ext_rates_len;
		memcpy(pos, rates + supp_rates_len, ext_rates_len);
		pos += ext_rates_len;
	}

	if (channel && sband->band == IEEE80211_BAND_2GHZ) {
		*pos++ = WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = channel;
	}

	/* insert custom IEs that go before HT */
	if (ie && ie_len) {
		static const u8 before_ht[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
			WLAN_EID_EXT_SUPP_RATES,
			WLAN_EID_DS_PARAMS,
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		noffset = ieee80211_ie_split(ie, ie_len,
					     before_ht, ARRAY_SIZE(before_ht),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	if (sband->ht_cap.ht_supported)
		pos = ieee80211_ie_build_ht_cap(pos, &sband->ht_cap,
						sband->ht_cap.cap);

	/*
	 * If adding more here, adjust code in main.c
	 * that calculates local->scan_ies_len.
	 */

	/* add any remaining custom IEs */
	if (ie && ie_len) {
		noffset = ie_len;
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
	}

	if (sband->vht_cap.vht_supported)
		pos = ieee80211_ie_build_vht_cap(pos, &sband->vht_cap,
						 sband->vht_cap.cap);

	return pos - buffer;
}

struct sk_buff *ieee80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  u8 *dst, u32 ratemask,
					  struct ieee80211_channel *chan,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  bool directed)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	size_t buf_len;
	u8 *buf;
	u8 chan_no;

	/* FIXME: come up with a proper value */
	buf = kmalloc(200 + ie_len, GFP_KERNEL);
	if (!buf)
		return NULL;

	/*
	 * Do not send DS Channel parameter for directed probe requests
	 * in order to maximize the chance that we get a response.  Some
	 * badly-behaved APs don't respond when this parameter is included.
	 */
	if (directed)
		chan_no = 0;
	else
		chan_no = ieee80211_frequency_to_channel(chan->center_freq);

	buf_len = ieee80211_build_preq_ies(local, buf, ie, ie_len, chan->band,
					   ratemask, chan_no);

	skb = ieee80211_probereq_get(&local->hw, &sdata->vif,
				     ssid, ssid_len,
				     buf, buf_len);
	if (!skb)
		goto out;

	if (dst) {
		mgmt = (struct ieee80211_mgmt *) skb->data;
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

 out:
	kfree(buf);

	return skb;
}

void ieee80211_send_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *ie, size_t ie_len,
			      u32 ratemask, bool directed, bool no_cck,
			      struct ieee80211_channel *channel, bool scan)
{
	struct sk_buff *skb;

	skb = ieee80211_build_probe_req(sdata, dst, ratemask, channel,
					ssid, ssid_len,
					ie, ie_len, directed);
	if (skb) {
		if (no_cck)
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_CTL_NO_CCK_RATE;
		if (scan)
			ieee80211_tx_skb_tid_band(sdata, skb, 7, channel->band);
		else
			ieee80211_tx_skb(sdata, skb);
	}
}

u32 ieee80211_sta_get_rates(struct ieee80211_local *local,
			    struct ieee802_11_elems *elems,
			    enum ieee80211_band band, u32 *basic_rates)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	size_t num_rates;
	u32 supp_rates;
	int i, j;
	sband = local->hw.wiphy->bands[band];

	if (WARN_ON(!sband))
		return 1;

	bitrates = sband->bitrates;
	num_rates = sband->n_bitrates;
	supp_rates = 0;
	for (i = 0; i < elems->supp_rates_len +
		     elems->ext_supp_rates_len; i++) {
		u8 rate = 0;
		int own_rate;
		bool is_basic;
		if (i < elems->supp_rates_len)
			rate = elems->supp_rates[i];
		else if (elems->ext_supp_rates)
			rate = elems->ext_supp_rates
				[i - elems->supp_rates_len];
		own_rate = 5 * (rate & 0x7f);
		is_basic = !!(rate & 0x80);

		if (is_basic && (rate & 0x7f) == BSS_MEMBERSHIP_SELECTOR_HT_PHY)
			continue;

		for (j = 0; j < num_rates; j++) {
			if (bitrates[j].bitrate == own_rate) {
				supp_rates |= BIT(j);
				if (basic_rates && is_basic)
					*basic_rates |= BIT(j);
			}
		}
	}
	return supp_rates;
}

void ieee80211_stop_device(struct ieee80211_local *local)
{
	ieee80211_led_radio(local, false);
	ieee80211_mod_tpt_led_trig(local, 0, IEEE80211_TPT_LEDTRIG_FL_RADIO);

	cancel_work_sync(&local->reconfig_filter);

	flush_workqueue(local->workqueue);
	drv_stop(local);
}

int ieee80211_reconfig(struct ieee80211_local *local)
{
	struct ieee80211_hw *hw = &local->hw;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx;
	struct sta_info *sta;
	int res, i;

#ifdef CONFIG_PM
	if (local->suspended)
		local->resuming = true;

	if (local->wowlan) {
		local->wowlan = false;
		res = drv_resume(local);
		if (res < 0) {
			local->resuming = false;
			return res;
		}
		if (res == 0)
			goto wake_up;
		WARN_ON(res > 1);
		/*
		 * res is 1, which means the driver requested
		 * to go through a regular reset on wakeup.
		 */
	}
#endif
	/* everything else happens only if HW was up & running */
	if (!local->open_count)
		goto wake_up;

	/*
	 * Upon resume hardware can sometimes be goofy due to
	 * various platform / driver / bus issues, so restarting
	 * the device may at times not work immediately. Propagate
	 * the error.
	 */
	res = drv_start(local);
	if (res) {
		WARN(local->suspended, "Hardware became unavailable "
		     "upon resume. This could be a software issue "
		     "prior to suspend or a hardware issue.\n");
		return res;
	}

	/* setup fragmentation threshold */
	drv_set_frag_threshold(local, hw->wiphy->frag_threshold);

	/* setup RTS threshold */
	drv_set_rts_threshold(local, hw->wiphy->rts_threshold);

	/* reset coverage class */
	drv_set_coverage_class(local, hw->wiphy->coverage_class);

	ieee80211_led_radio(local, true);
	ieee80211_mod_tpt_led_trig(local,
				   IEEE80211_TPT_LEDTRIG_FL_RADIO, 0);

	/* add interfaces */
	sdata = rtnl_dereference(local->monitor_sdata);
	if (sdata) {
		res = drv_add_interface(local, sdata);
		if (WARN_ON(res)) {
			rcu_assign_pointer(local->monitor_sdata, NULL);
			synchronize_net();
			kfree(sdata);
		}
	}

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    ieee80211_sdata_running(sdata))
			res = drv_add_interface(local, sdata);
	}

	/* add channel contexts */
	mutex_lock(&local->chanctx_mtx);
	list_for_each_entry(ctx, &local->chanctx_list, list)
		WARN_ON(drv_add_chanctx(local, ctx));
	mutex_unlock(&local->chanctx_mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		struct ieee80211_chanctx_conf *ctx_conf;

		if (!ieee80211_sdata_running(sdata))
			continue;

		mutex_lock(&local->chanctx_mtx);
		ctx_conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
				lockdep_is_held(&local->chanctx_mtx));
		if (ctx_conf) {
			ctx = container_of(ctx_conf, struct ieee80211_chanctx,
					   conf);
			drv_assign_vif_chanctx(local, sdata, ctx);
		}
		mutex_unlock(&local->chanctx_mtx);
	}

	/* add STAs back */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		enum ieee80211_sta_state state;

		if (!sta->uploaded)
			continue;

		/* AP-mode stations will be added later */
		if (sta->sdata->vif.type == NL80211_IFTYPE_AP)
			continue;

		for (state = IEEE80211_STA_NOTEXIST;
		     state < sta->sta_state; state++)
			WARN_ON(drv_sta_state(local, sta->sdata, sta, state,
					      state + 1));
	}
	mutex_unlock(&local->sta_mtx);

	/* reconfigure tx conf */
	if (hw->queues >= IEEE80211_NUM_ACS) {
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
			    sdata->vif.type == NL80211_IFTYPE_MONITOR ||
			    !ieee80211_sdata_running(sdata))
				continue;

			for (i = 0; i < IEEE80211_NUM_ACS; i++)
				drv_conf_tx(local, sdata, i,
					    &sdata->tx_conf[i]);
		}
	}

	/* reconfigure hardware */
	ieee80211_hw_config(local, ~0);

	ieee80211_configure_filter(local);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		u32 changed;

		if (!ieee80211_sdata_running(sdata))
			continue;

		/* common change flags for all interface types */
		changed = BSS_CHANGED_ERP_CTS_PROT |
			  BSS_CHANGED_ERP_PREAMBLE |
			  BSS_CHANGED_ERP_SLOT |
			  BSS_CHANGED_HT |
			  BSS_CHANGED_BASIC_RATES |
			  BSS_CHANGED_BEACON_INT |
			  BSS_CHANGED_BSSID |
			  BSS_CHANGED_CQM |
			  BSS_CHANGED_QOS |
			  BSS_CHANGED_IDLE |
			  BSS_CHANGED_TXPOWER;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			changed |= BSS_CHANGED_ASSOC |
				   BSS_CHANGED_ARP_FILTER |
				   BSS_CHANGED_PS;
			mutex_lock(&sdata->u.mgd.mtx);
			ieee80211_bss_info_change_notify(sdata, changed);
			mutex_unlock(&sdata->u.mgd.mtx);
			break;
		case NL80211_IFTYPE_ADHOC:
			changed |= BSS_CHANGED_IBSS;
			/* fall through */
		case NL80211_IFTYPE_AP:
			changed |= BSS_CHANGED_SSID;

			if (sdata->vif.type == NL80211_IFTYPE_AP) {
				changed |= BSS_CHANGED_AP_PROBE_RESP;

				if (rcu_access_pointer(sdata->u.ap.beacon))
					drv_start_ap(local, sdata);
			}

			/* fall through */
		case NL80211_IFTYPE_MESH_POINT:
			changed |= BSS_CHANGED_BEACON |
				   BSS_CHANGED_BEACON_ENABLED;
			ieee80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_WDS:
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* ignore virtual */
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
			changed = BSS_CHANGED_IDLE;
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
			WARN_ON(1);
			break;
		}
	}

	ieee80211_recalc_ps(local, -1);

	/*
	 * The sta might be in psm against the ap (e.g. because
	 * this was the state before a hw restart), so we
	 * explicitly send a null packet in order to make sure
	 * it'll sync against the ap (and get out of psm).
	 */
	if (!(local->hw.conf.flags & IEEE80211_CONF_PS)) {
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type != NL80211_IFTYPE_STATION)
				continue;

			ieee80211_send_nullfunc(local, sdata, 0);
		}
	}

	/* APs are now beaconing, add back stations */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		enum ieee80211_sta_state state;

		if (!sta->uploaded)
			continue;

		if (sta->sdata->vif.type != NL80211_IFTYPE_AP)
			continue;

		for (state = IEEE80211_STA_NOTEXIST;
		     state < sta->sta_state; state++)
			WARN_ON(drv_sta_state(local, sta->sdata, sta, state,
					      state + 1));
	}
	mutex_unlock(&local->sta_mtx);

	/* add back keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (ieee80211_sdata_running(sdata))
			ieee80211_enable_keys(sdata);

 wake_up:
	local->in_reconfig = false;
	barrier();

	/*
	 * Clear the WLAN_STA_BLOCK_BA flag so new aggregation
	 * sessions can be established after a resume.
	 *
	 * Also tear down aggregation sessions since reconfiguring
	 * them in a hardware restart scenario is not easily done
	 * right now, and the hardware will have lost information
	 * about the sessions, but we and the AP still think they
	 * are active. This is really a workaround though.
	 */
	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		mutex_lock(&local->sta_mtx);

		list_for_each_entry(sta, &local->sta_list, list) {
			ieee80211_sta_tear_down_BA_sessions(sta, true);
			clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
		}

		mutex_unlock(&local->sta_mtx);
	}

	ieee80211_wake_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/*
	 * If this is for hw restart things are still running.
	 * We may want to change that later, however.
	 */
	if (!local->suspended) {
		drv_restart_complete(local);
		return 0;
	}

#ifdef CONFIG_PM
	/* first set suspended false, then resuming */
	local->suspended = false;
	mb();
	local->resuming = false;

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch(sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			ieee80211_sta_restart(sdata);
			break;
		case NL80211_IFTYPE_ADHOC:
			ieee80211_ibss_restart(sdata);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			ieee80211_mesh_restart(sdata);
			break;
		default:
			break;
		}
	}

	mod_timer(&local->sta_cleanup, jiffies + 1);

	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list)
		mesh_plink_restart(sta);
	mutex_unlock(&local->sta_mtx);
#else
	WARN_ON(1);
#endif
	return 0;
}

void ieee80211_resume_disconnect(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_key *key;

	if (WARN_ON(!vif))
		return;

	sdata = vif_to_sdata(vif);
	local = sdata->local;

	if (WARN_ON(!local->resuming))
		return;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return;

	sdata->flags |= IEEE80211_SDATA_DISCONNECT_RESUME;

	mutex_lock(&local->key_mtx);
	list_for_each_entry(key, &sdata->key_list, list)
		key->flags |= KEY_FLAG_TAINTED;
	mutex_unlock(&local->key_mtx);
}
EXPORT_SYMBOL_GPL(ieee80211_resume_disconnect);

void ieee80211_recalc_smps(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_chanctx *chanctx;

	mutex_lock(&local->chanctx_mtx);

	chanctx_conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					lockdep_is_held(&local->chanctx_mtx));

	if (WARN_ON_ONCE(!chanctx_conf))
		goto unlock;

	chanctx = container_of(chanctx_conf, struct ieee80211_chanctx, conf);
	ieee80211_recalc_smps_chanctx(local, chanctx);
 unlock:
	mutex_unlock(&local->chanctx_mtx);
}

static bool ieee80211_id_in_list(const u8 *ids, int n_ids, u8 id)
{
	int i;

	for (i = 0; i < n_ids; i++)
		if (ids[i] == id)
			return true;
	return false;
}

/**
 * ieee80211_ie_split - split an IE buffer according to ordering
 *
 * @ies: the IE buffer
 * @ielen: the length of the IE buffer
 * @ids: an array with element IDs that are allowed before
 *	the split
 * @n_ids: the size of the element ID array
 * @offset: offset where to start splitting in the buffer
 *
 * This function splits an IE buffer by updating the @offset
 * variable to point to the location where the buffer should be
 * split.
 *
 * It assumes that the given IE buffer is well-formed, this
 * has to be guaranteed by the caller!
 *
 * It also assumes that the IEs in the buffer are ordered
 * correctly, if not the result of using this function will not
 * be ordered correctly either, i.e. it does no reordering.
 *
 * The function returns the offset where the next part of the
 * buffer starts, which may be @ielen if the entire (remainder)
 * of the buffer should be used.
 */
size_t ieee80211_ie_split(const u8 *ies, size_t ielen,
			  const u8 *ids, int n_ids, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ieee80211_id_in_list(ids, n_ids, ies[pos]))
		pos += 2 + ies[pos + 1];

	return pos;
}

size_t ieee80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ies[pos] != WLAN_EID_VENDOR_SPECIFIC)
		pos += 2 + ies[pos + 1];

	return pos;
}

static void _ieee80211_enable_rssi_reports(struct ieee80211_sub_if_data *sdata,
					    int rssi_min_thold,
					    int rssi_max_thold)
{
	trace_api_enable_rssi_reports(sdata, rssi_min_thold, rssi_max_thold);

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	/*
	 * Scale up threshold values before storing it, as the RSSI averaging
	 * algorithm uses a scaled up value as well. Change this scaling
	 * factor if the RSSI averaging algorithm changes.
	 */
	sdata->u.mgd.rssi_min_thold = rssi_min_thold*16;
	sdata->u.mgd.rssi_max_thold = rssi_max_thold*16;
}

void ieee80211_enable_rssi_reports(struct ieee80211_vif *vif,
				    int rssi_min_thold,
				    int rssi_max_thold)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	WARN_ON(rssi_min_thold == rssi_max_thold ||
		rssi_min_thold > rssi_max_thold);

	_ieee80211_enable_rssi_reports(sdata, rssi_min_thold,
				       rssi_max_thold);
}
EXPORT_SYMBOL(ieee80211_enable_rssi_reports);

void ieee80211_disable_rssi_reports(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	_ieee80211_enable_rssi_reports(sdata, 0, 0);
}
EXPORT_SYMBOL(ieee80211_disable_rssi_reports);

u8 *ieee80211_ie_build_ht_cap(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			      u16 cap)
{
	__le16 tmp;

	*pos++ = WLAN_EID_HT_CAPABILITY;
	*pos++ = sizeof(struct ieee80211_ht_cap);
	memset(pos, 0, sizeof(struct ieee80211_ht_cap));

	/* capability flags */
	tmp = cpu_to_le16(cap);
	memcpy(pos, &tmp, sizeof(u16));
	pos += sizeof(u16);

	/* AMPDU parameters */
	*pos++ = ht_cap->ampdu_factor |
		 (ht_cap->ampdu_density <<
			IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);

	/* MCS set */
	memcpy(pos, &ht_cap->mcs, sizeof(ht_cap->mcs));
	pos += sizeof(ht_cap->mcs);

	/* extended capabilities */
	pos += sizeof(__le16);

	/* BF capabilities */
	pos += sizeof(__le32);

	/* antenna selection */
	pos += sizeof(u8);

	return pos;
}

u8 *ieee80211_ie_build_vht_cap(u8 *pos, struct ieee80211_sta_vht_cap *vht_cap,
							   u32 cap)
{
	__le32 tmp;

	*pos++ = WLAN_EID_VHT_CAPABILITY;
	*pos++ = sizeof(struct ieee80211_vht_cap);
	memset(pos, 0, sizeof(struct ieee80211_vht_cap));

	/* capability flags */
	tmp = cpu_to_le32(cap);
	memcpy(pos, &tmp, sizeof(u32));
	pos += sizeof(u32);

	/* VHT MCS set */
	memcpy(pos, &vht_cap->vht_mcs, sizeof(vht_cap->vht_mcs));
	pos += sizeof(vht_cap->vht_mcs);

	return pos;
}

u8 *ieee80211_ie_build_ht_oper(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			       struct ieee80211_channel *channel,
			       enum nl80211_channel_type channel_type,
			       u16 prot_mode)
{
	struct ieee80211_ht_operation *ht_oper;
	/* Build HT Information */
	*pos++ = WLAN_EID_HT_OPERATION;
	*pos++ = sizeof(struct ieee80211_ht_operation);
	ht_oper = (struct ieee80211_ht_operation *)pos;
	ht_oper->primary_chan =
			ieee80211_frequency_to_channel(channel->center_freq);
	switch (channel_type) {
	case NL80211_CHAN_HT40MINUS:
		ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
		break;
	case NL80211_CHAN_HT40PLUS:
		ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
		break;
	case NL80211_CHAN_HT20:
	default:
		ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_NONE;
		break;
	}
	if (ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
	    channel_type != NL80211_CHAN_NO_HT &&
	    channel_type != NL80211_CHAN_HT20)
		ht_oper->ht_param |= IEEE80211_HT_PARAM_CHAN_WIDTH_ANY;

	ht_oper->operation_mode = cpu_to_le16(prot_mode);
	ht_oper->stbc_param = 0x0000;

	/* It seems that Basic MCS set and Supported MCS set
	   are identical for the first 10 bytes */
	memset(&ht_oper->basic_set, 0, 16);
	memcpy(&ht_oper->basic_set, &ht_cap->mcs, 10);

	return pos + sizeof(struct ieee80211_ht_operation);
}

enum nl80211_channel_type
ieee80211_ht_oper_to_channel_type(struct ieee80211_ht_operation *ht_oper)
{
	enum nl80211_channel_type channel_type;

	if (!ht_oper)
		return NL80211_CHAN_NO_HT;

	switch (ht_oper->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
	case IEEE80211_HT_PARAM_CHA_SEC_NONE:
		channel_type = NL80211_CHAN_HT20;
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		channel_type = NL80211_CHAN_HT40PLUS;
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		channel_type = NL80211_CHAN_HT40MINUS;
		break;
	default:
		channel_type = NL80211_CHAN_NO_HT;
	}

	return channel_type;
}

int ieee80211_add_srates_ie(struct ieee80211_sub_if_data *sdata,
			    struct sk_buff *skb, bool need_basic,
			    enum ieee80211_band band)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, rates, *pos;
	u32 basic_rates = sdata->vif.bss_conf.basic_rates;

	sband = local->hw.wiphy->bands[band];
	rates = sband->n_bitrates;
	if (rates > 8)
		rates = 8;

	if (skb_tailroom(skb) < rates + 2)
		return -ENOMEM;

	pos = skb_put(skb, rates + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = rates;
	for (i = 0; i < rates; i++) {
		u8 basic = 0;
		if (need_basic && basic_rates & BIT(i))
			basic = 0x80;
		rate = sband->bitrates[i].bitrate;
		*pos++ = basic | (u8) (rate / 5);
	}

	return 0;
}

int ieee80211_add_ext_srates_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, bool need_basic,
				enum ieee80211_band band)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, exrates, *pos;
	u32 basic_rates = sdata->vif.bss_conf.basic_rates;

	sband = local->hw.wiphy->bands[band];
	exrates = sband->n_bitrates;
	if (exrates > 8)
		exrates -= 8;
	else
		exrates = 0;

	if (skb_tailroom(skb) < exrates + 2)
		return -ENOMEM;

	if (exrates) {
		pos = skb_put(skb, exrates + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = exrates;
		for (i = 8; i < sband->n_bitrates; i++) {
			u8 basic = 0;
			if (need_basic && basic_rates & BIT(i))
				basic = 0x80;
			rate = sband->bitrates[i].bitrate;
			*pos++ = basic | (u8) (rate / 5);
		}
	}
	return 0;
}

int ieee80211_ave_rssi(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION)) {
		/* non-managed type inferfaces */
		return 0;
	}
	return ifmgd->ave_beacon_signal;
}
EXPORT_SYMBOL_GPL(ieee80211_ave_rssi);

u8 ieee80211_mcs_to_chains(const struct ieee80211_mcs_info *mcs)
{
	if (!mcs)
		return 1;

	/* TODO: consider rx_highest */

	if (mcs->rx_mask[3])
		return 4;
	if (mcs->rx_mask[2])
		return 3;
	if (mcs->rx_mask[1])
		return 2;
	return 1;
}
