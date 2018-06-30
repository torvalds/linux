/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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
const void *const mac80211_wiphy_privid = &mac80211_wiphy_privid;

struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct ieee80211_local *local;
	BUG_ON(!wiphy);

	local = wiphy_priv(wiphy);
	return &local->hw;
}
EXPORT_SYMBOL(wiphy_to_ieee80211_hw);

void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	skb_queue_walk(&tx->skbs, skb) {
		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}
}

int ieee80211_frame_duration(enum nl80211_band band, size_t len,
			     int rate, int erp, int short_preamble,
			     int shift)
{
	int dur;

	/* calculate duration (in microseconds, rounded up to next higher
	 * integer if it includes a fractional microsecond) to send frame of
	 * len bytes (does not include FCS) at the given rate. Duration will
	 * also include SIFS.
	 *
	 * rate is in 100 kbps, so divident is multiplied by 10 in the
	 * DIV_ROUND_UP() operations.
	 *
	 * shift may be 2 for 5 MHz channels or 1 for 10 MHz channels, and
	 * is assumed to be 0 otherwise.
	 */

	if (band == NL80211_BAND_5GHZ || erp) {
		/*
		 * OFDM:
		 *
		 * N_DBPS = DATARATE x 4
		 * N_SYM = Ceiling((16+8xLENGTH+6) / N_DBPS)
		 *	(16 = SIGNAL time, 6 = tail bits)
		 * TXTIME = T_PREAMBLE + T_SIGNAL + T_SYM x N_SYM + Signal Ext
		 *
		 * T_SYM = 4 usec
		 * 802.11a - 18.5.2: aSIFSTime = 16 usec
		 * 802.11g - 19.8.4: aSIFSTime = 10 usec +
		 *	signal ext = 6 usec
		 */
		dur = 16; /* SIFS + signal ext */
		dur += 16; /* IEEE 802.11-2012 18.3.2.4: T_PREAMBLE = 16 usec */
		dur += 4; /* IEEE 802.11-2012 18.3.2.4: T_SIGNAL = 4 usec */

		/* IEEE 802.11-2012 18.3.2.4: all values above are:
		 *  * times 4 for 5 MHz
		 *  * times 2 for 10 MHz
		 */
		dur *= 1 << shift;

		/* rates should already consider the channel bandwidth,
		 * don't apply divisor again.
		 */
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
					enum nl80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	struct ieee80211_sub_if_data *sdata;
	u16 dur;
	int erp, shift = 0;
	bool short_preamble = false;

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
		shift = ieee80211_vif_get_shift(vif);
	}

	dur = ieee80211_frame_duration(band, frame_len, rate->bitrate, erp,
				       short_preamble, shift);

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
	int erp, shift = 0, bitrate;
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
		shift = ieee80211_vif_get_shift(vif);
	}

	bitrate = DIV_ROUND_UP(rate->bitrate, 1 << shift);

	/* CTS duration */
	dur = ieee80211_frame_duration(sband->band, 10, bitrate,
				       erp, short_preamble, shift);
	/* Data frame duration */
	dur += ieee80211_frame_duration(sband->band, frame_len, bitrate,
					erp, short_preamble, shift);
	/* ACK duration */
	dur += ieee80211_frame_duration(sband->band, 10, bitrate,
					erp, short_preamble, shift);

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
	int erp, shift = 0, bitrate;
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
		shift = ieee80211_vif_get_shift(vif);
	}

	bitrate = DIV_ROUND_UP(rate->bitrate, 1 << shift);

	/* Data frame duration */
	dur = ieee80211_frame_duration(sband->band, frame_len, bitrate,
				       erp, short_preamble, shift);
	if (!(frame_txctl->flags & IEEE80211_TX_CTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(sband->band, 10, bitrate,
						erp, short_preamble, shift);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

void ieee80211_propagate_queue_wake(struct ieee80211_local *local, int queue)
{
	struct ieee80211_sub_if_data *sdata;
	int n_acs = IEEE80211_NUM_ACS;

	if (local->ops->wake_tx_queue)
		return;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		n_acs = 1;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		if (!sdata->dev)
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
				   enum queue_stop_reason reason,
				   bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_wake_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	if (!refcounted) {
		local->q_stop_reasons[queue][reason] = 0;
	} else {
		local->q_stop_reasons[queue][reason]--;
		if (WARN_ON(local->q_stop_reasons[queue][reason] < 0))
			local->q_stop_reasons[queue][reason] = 0;
	}

	if (local->q_stop_reasons[queue][reason] == 0)
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
				    enum queue_stop_reason reason,
				    bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_wake_queue(hw, queue, reason, refcounted);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_wake_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER,
				       false);
}
EXPORT_SYMBOL(ieee80211_wake_queue);

static void __ieee80211_stop_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason,
				   bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;
	int n_acs = IEEE80211_NUM_ACS;

	trace_stop_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!refcounted)
		local->q_stop_reasons[queue][reason] = 1;
	else
		local->q_stop_reasons[queue][reason]++;

	if (__test_and_set_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	if (local->ops->wake_tx_queue)
		return;

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
				    enum queue_stop_reason reason,
				    bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_stop_queue(hw, queue, reason, refcounted);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_stop_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER,
				       false);
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
	__ieee80211_stop_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
			       false);
	__skb_queue_tail(&local->pending[queue], skb);
	__ieee80211_wake_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
			       false);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs)
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
				IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
				false);

		__skb_queue_tail(&local->pending[queue], skb);
	}

	for (i = 0; i < hw->queues; i++)
		__ieee80211_wake_queue(hw, i,
			IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
			false);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for_each_set_bit(i, &queues, hw->queues)
		__ieee80211_stop_queue(hw, i, reason, refcounted);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	ieee80211_stop_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_DRIVER,
					false);
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
	ret = test_bit(IEEE80211_QUEUE_STOP_REASON_DRIVER,
		       &local->queue_stop_reasons[queue]);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ieee80211_queue_stopped);

void ieee80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for_each_set_bit(i, &queues, hw->queues)
		__ieee80211_wake_queue(hw, i, reason, refcounted);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	ieee80211_wake_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_DRIVER,
					false);
}
EXPORT_SYMBOL(ieee80211_wake_queues);

static unsigned int
ieee80211_get_vif_queues(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata)
{
	unsigned int queues;

	if (sdata && ieee80211_hw_check(&local->hw, QUEUE_CONTROL)) {
		int ac;

		queues = 0;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			queues |= BIT(sdata->vif.hw_queue[ac]);
		if (sdata->vif.cab_queue != IEEE80211_INVAL_HW_QUEUE)
			queues |= BIT(sdata->vif.cab_queue);
	} else {
		/* all queues */
		queues = BIT(local->hw.queues) - 1;
	}

	return queues;
}

void __ieee80211_flush_queues(struct ieee80211_local *local,
			      struct ieee80211_sub_if_data *sdata,
			      unsigned int queues, bool drop)
{
	if (!local->ops->flush)
		return;

	/*
	 * If no queue was set, or if the HW doesn't support
	 * IEEE80211_HW_QUEUE_CONTROL - flush all queues
	 */
	if (!queues || !ieee80211_hw_check(&local->hw, QUEUE_CONTROL))
		queues = ieee80211_get_vif_queues(local, sdata);

	ieee80211_stop_queues_by_reason(&local->hw, queues,
					IEEE80211_QUEUE_STOP_REASON_FLUSH,
					false);

	drv_flush(local, sdata, queues, drop);

	ieee80211_wake_queues_by_reason(&local->hw, queues,
					IEEE80211_QUEUE_STOP_REASON_FLUSH,
					false);
}

void ieee80211_flush_queues(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata, bool drop)
{
	__ieee80211_flush_queues(local, sdata, 0, drop);
}

void ieee80211_stop_vif_queues(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       enum queue_stop_reason reason)
{
	ieee80211_stop_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, true);
}

void ieee80211_wake_vif_queues(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       enum queue_stop_reason reason)
{
	ieee80211_wake_queues_by_reason(&local->hw,
					ieee80211_get_vif_queues(local, sdata),
					reason, true);
}

static void __iterate_interfaces(struct ieee80211_local *local,
				 u32 iter_flags,
				 void (*iterator)(void *data, u8 *mac,
						  struct ieee80211_vif *vif),
				 void *data)
{
	struct ieee80211_sub_if_data *sdata;
	bool active_only = iter_flags & IEEE80211_IFACE_ITER_ACTIVE;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
			if (!(sdata->u.mntr.flags & MONITOR_FLAG_ACTIVE))
				continue;
			break;
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (!(iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL) &&
		    active_only && !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
			continue;
		if (ieee80211_sdata_running(sdata) || !active_only)
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	sdata = rcu_dereference_check(local->monitor_sdata,
				      lockdep_is_held(&local->iflist_mtx) ||
				      lockdep_rtnl_is_held());
	if (sdata &&
	    (iter_flags & IEEE80211_IFACE_ITER_RESUME_ALL || !active_only ||
	     sdata->flags & IEEE80211_SDATA_IN_DRIVER))
		iterator(data, sdata->vif.addr, &sdata->vif);
}

void ieee80211_iterate_interfaces(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);

	mutex_lock(&local->iflist_mtx);
	__iterate_interfaces(local, iter_flags, iterator, data);
	mutex_unlock(&local->iflist_mtx);
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_interfaces);

void ieee80211_iterate_active_interfaces_atomic(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);

	rcu_read_lock();
	__iterate_interfaces(local, iter_flags | IEEE80211_IFACE_ITER_ACTIVE,
			     iterator, data);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_atomic);

void ieee80211_iterate_active_interfaces_rtnl(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);

	ASSERT_RTNL();

	__iterate_interfaces(local, iter_flags | IEEE80211_IFACE_ITER_ACTIVE,
			     iterator, data);
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_rtnl);

static void __iterate_stations(struct ieee80211_local *local,
			       void (*iterator)(void *data,
						struct ieee80211_sta *sta),
			       void *data)
{
	struct sta_info *sta;

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (!sta->uploaded)
			continue;

		iterator(data, &sta->sta);
	}
}

void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
			void (*iterator)(void *data,
					 struct ieee80211_sta *sta),
			void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);

	rcu_read_lock();
	__iterate_stations(local, iterator, data);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_stations_atomic);

struct ieee80211_vif *wdev_to_ieee80211_vif(struct wireless_dev *wdev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

	if (!ieee80211_sdata_running(sdata) ||
	    !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
		return NULL;
	return &sdata->vif;
}
EXPORT_SYMBOL_GPL(wdev_to_ieee80211_vif);

struct wireless_dev *ieee80211_vif_to_wdev(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata;

	if (!vif)
		return NULL;

	sdata = vif_to_sdata(vif);

	if (!ieee80211_sdata_running(sdata) ||
	    !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
		return NULL;

	return &sdata->wdev;
}
EXPORT_SYMBOL_GPL(ieee80211_vif_to_wdev);

/*
 * Nothing should have been stuffed into the workqueue during
 * the suspend->resume cycle. Since we can't check each caller
 * of this function if we are already quiescing / suspended,
 * check here and don't WARN since this can actually happen when
 * the rx path (for example) is racing against __ieee80211_suspend
 * and suspending / quiescing was set after the rx path checked
 * them.
 */
static bool ieee80211_can_queue_work(struct ieee80211_local *local)
{
	if (local->quiescing || (local->suspended && !local->resuming)) {
		pr_warn("queueing ieee80211 work while going to suspend\n");
		return false;
	}

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

u32 ieee802_11_parse_elems_crc(const u8 *start, size_t len, bool action,
			       struct ieee802_11_elems *elems,
			       u64 filter, u32 crc)
{
	size_t left = len;
	const u8 *pos = start;
	bool calc_crc = filter != 0;
	DECLARE_BITMAP(seen_elems, 256);
	const u8 *ie;

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
		case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
		case WLAN_EID_WIDE_BW_CHANNEL_SWITCH:
		case WLAN_EID_CHAN_SWITCH_PARAM:
		case WLAN_EID_EXT_CAPABILITY:
		case WLAN_EID_CHAN_SWITCH_TIMING:
		case WLAN_EID_LINK_ID:
		case WLAN_EID_BSS_MAX_IDLE_PERIOD:
		/*
		 * not listing WLAN_EID_CHANNEL_SWITCH_WRAPPER -- it seems possible
		 * that if the content gets bigger it might be needed more than once
		 */
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
		case WLAN_EID_LINK_ID:
			if (elen + 2 != sizeof(struct ieee80211_tdls_lnkie)) {
				elem_parse_failed = true;
				break;
			}
			elems->lnk_id = (void *)(pos - 2);
			break;
		case WLAN_EID_CHAN_SWITCH_TIMING:
			if (elen != sizeof(struct ieee80211_ch_switch_timing)) {
				elem_parse_failed = true;
				break;
			}
			elems->ch_sw_timing = (void *)pos;
			break;
		case WLAN_EID_EXT_CAPABILITY:
			elems->ext_capab = pos;
			elems->ext_capab_len = elen;
			break;
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			if (elen >= 1)
				elems->ds_params = pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_TIM:
			if (elen >= sizeof(struct ieee80211_tim_ie)) {
				elems->tim = (void *)pos;
				elems->tim_len = elen;
			} else
				elem_parse_failed = true;
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

				if (elen >= 5 && pos[3] == 2) {
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
			if (elen >= 1)
				elems->erp_info = pos;
			else
				elem_parse_failed = true;
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
		case WLAN_EID_OPMODE_NOTIF:
			if (elen > 0)
				elems->opmode_notif = pos;
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
		case WLAN_EID_MESH_AWAKE_WINDOW:
			if (elen >= 2)
				elems->awake_window = (void *)pos;
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
		case WLAN_EID_EXT_CHANSWITCH_ANN:
			if (elen != sizeof(struct ieee80211_ext_chansw_ie)) {
				elem_parse_failed = true;
				break;
			}
			elems->ext_chansw_ie = (void *)pos;
			break;
		case WLAN_EID_SECONDARY_CHANNEL_OFFSET:
			if (elen != sizeof(struct ieee80211_sec_chan_offs_ie)) {
				elem_parse_failed = true;
				break;
			}
			elems->sec_chan_offs = (void *)pos;
			break;
		case WLAN_EID_CHAN_SWITCH_PARAM:
			if (elen !=
			    sizeof(*elems->mesh_chansw_params_ie)) {
				elem_parse_failed = true;
				break;
			}
			elems->mesh_chansw_params_ie = (void *)pos;
			break;
		case WLAN_EID_WIDE_BW_CHANNEL_SWITCH:
			if (!action ||
			    elen != sizeof(*elems->wide_bw_chansw_ie)) {
				elem_parse_failed = true;
				break;
			}
			elems->wide_bw_chansw_ie = (void *)pos;
			break;
		case WLAN_EID_CHANNEL_SWITCH_WRAPPER:
			if (action) {
				elem_parse_failed = true;
				break;
			}
			/*
			 * This is a bit tricky, but as we only care about
			 * the wide bandwidth channel switch element, so
			 * just parse it out manually.
			 */
			ie = cfg80211_find_ie(WLAN_EID_WIDE_BW_CHANNEL_SWITCH,
					      pos, elen);
			if (ie) {
				if (ie[1] == sizeof(*elems->wide_bw_chansw_ie))
					elems->wide_bw_chansw_ie =
						(void *)(ie + 2);
				else
					elem_parse_failed = true;
			}
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
		case WLAN_EID_CISCO_VENDOR_SPECIFIC:
			/* Lots of different options exist, but we only care
			 * about the Dynamic Transmit Power Control element.
			 * First check for the Cisco OUI, then for the DTPC
			 * tag (0x00).
			 */
			if (elen < 4) {
				elem_parse_failed = true;
				break;
			}

			if (pos[0] != 0x00 || pos[1] != 0x40 ||
			    pos[2] != 0x96 || pos[3] != 0x00)
				break;

			if (elen != 6) {
				elem_parse_failed = true;
				break;
			}

			if (calc_crc)
				crc = crc32_be(crc, pos - 2, elen + 2);

			elems->cisco_dtpc_elem = pos;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			if (elen >= sizeof(struct ieee80211_timeout_interval_ie))
				elems->timeout_int = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_BSS_MAX_IDLE_PERIOD:
			if (elen >= sizeof(*elems->max_idle_period_ie))
				elems->max_idle_period_ie = (void *)pos;
			break;
		case WLAN_EID_EXTENSION:
			if (pos[0] == WLAN_EID_EXT_HE_MU_EDCA &&
			    elen >= (sizeof(*elems->mu_edca_param_set) + 1)) {
				elems->mu_edca_param_set = (void *)&pos[1];
			} else if (pos[0] == WLAN_EID_EXT_HE_CAPABILITY) {
				elems->he_cap = (void *)&pos[1];
				elems->he_cap_len = elen - 1;
			} else if (pos[0] == WLAN_EID_EXT_HE_OPERATION &&
				   elen >= sizeof(*elems->he_operation) &&
				   elen >= ieee80211_he_oper_size(&pos[1])) {
				elems->he_operation = (void *)&pos[1];
			} else if (pos[0] == WLAN_EID_EXT_UORA && elen >= 1) {
				elems->uora_element = (void *)&pos[1];
			}
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

void ieee80211_regulatory_limit_wmm_params(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_tx_queue_params
					   *qparam, int ac)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	const struct ieee80211_reg_rule *rrule;
	struct ieee80211_wmm_ac *wmm_ac;
	u16 center_freq = 0;

	if (sdata->vif.type != NL80211_IFTYPE_AP &&
	    sdata->vif.type != NL80211_IFTYPE_STATION)
		return;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (chanctx_conf)
		center_freq = chanctx_conf->def.chan->center_freq;

	if (!center_freq) {
		rcu_read_unlock();
		return;
	}

	rrule = freq_reg_info(sdata->wdev.wiphy, MHZ_TO_KHZ(center_freq));

	if (IS_ERR_OR_NULL(rrule) || !rrule->wmm_rule) {
		rcu_read_unlock();
		return;
	}

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		wmm_ac = &rrule->wmm_rule->ap[ac];
	else
		wmm_ac = &rrule->wmm_rule->client[ac];
	qparam->cw_min = max_t(u16, qparam->cw_min, wmm_ac->cw_min);
	qparam->cw_max = max_t(u16, qparam->cw_max, wmm_ac->cw_max);
	qparam->aifs = max_t(u8, qparam->aifs, wmm_ac->aifsn);
	qparam->txop = !qparam->txop ? wmm_ac->cot / 32 :
		min_t(u16, qparam->txop, wmm_ac->cot / 32);
	rcu_read_unlock();
}

void ieee80211_set_wmm_default(struct ieee80211_sub_if_data *sdata,
			       bool bss_notify, bool enable_qos)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_queue_params qparam;
	struct ieee80211_chanctx_conf *chanctx_conf;
	int ac;
	bool use_11b;
	bool is_ocb; /* Use another EDCA parameters if dot11OCBActivated=true */
	int aCWmin, aCWmax;

	if (!local->ops->conf_tx)
		return;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return;

	memset(&qparam, 0, sizeof(qparam));

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	use_11b = (chanctx_conf &&
		   chanctx_conf->def.chan->band == NL80211_BAND_2GHZ) &&
		 !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE);
	rcu_read_unlock();

	is_ocb = (sdata->vif.type == NL80211_IFTYPE_OCB);

	/* Set defaults according to 802.11-2007 Table 7-37 */
	aCWmax = 1023;
	if (use_11b)
		aCWmin = 31;
	else
		aCWmin = 15;

	/* Confiure old 802.11b/g medium access rules. */
	qparam.cw_max = aCWmax;
	qparam.cw_min = aCWmin;
	qparam.txop = 0;
	qparam.aifs = 2;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		/* Update if QoS is enabled. */
		if (enable_qos) {
			switch (ac) {
			case IEEE80211_AC_BK:
				qparam.cw_max = aCWmax;
				qparam.cw_min = aCWmin;
				qparam.txop = 0;
				if (is_ocb)
					qparam.aifs = 9;
				else
					qparam.aifs = 7;
				break;
			/* never happens but let's not leave undefined */
			default:
			case IEEE80211_AC_BE:
				qparam.cw_max = aCWmax;
				qparam.cw_min = aCWmin;
				qparam.txop = 0;
				if (is_ocb)
					qparam.aifs = 6;
				else
					qparam.aifs = 3;
				break;
			case IEEE80211_AC_VI:
				qparam.cw_max = aCWmin;
				qparam.cw_min = (aCWmin + 1) / 2 - 1;
				if (is_ocb)
					qparam.txop = 0;
				else if (use_11b)
					qparam.txop = 6016/32;
				else
					qparam.txop = 3008/32;

				if (is_ocb)
					qparam.aifs = 3;
				else
					qparam.aifs = 2;
				break;
			case IEEE80211_AC_VO:
				qparam.cw_max = (aCWmin + 1) / 2 - 1;
				qparam.cw_min = (aCWmin + 1) / 4 - 1;
				if (is_ocb)
					qparam.txop = 0;
				else if (use_11b)
					qparam.txop = 3264/32;
				else
					qparam.txop = 1504/32;
				qparam.aifs = 2;
				break;
			}
		}
		ieee80211_regulatory_limit_wmm_params(sdata, &qparam, ac);

		qparam.uapsd = false;

		sdata->tx_conf[ac] = qparam;
		drv_conf_tx(local, sdata, ac, &qparam);
	}

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_NAN) {
		sdata->vif.bss_conf.qos = enable_qos;
		if (bss_notify)
			ieee80211_bss_info_change_notify(sdata,
							 BSS_CHANGED_QOS);
	}
}

void ieee80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg, u16 status,
			 const u8 *extra, size_t extra_len, const u8 *da,
			 const u8 *bssid, const u8 *key, u8 key_len, u8 key_idx,
			 u32 tx_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	int err;

	/* 24 + 6 = header + auth_algo + auth_transaction + status_code */
	skb = dev_alloc_skb(local->hw.extra_tx_headroom + IEEE80211_WEP_IV_LEN +
			    24 + 6 + extra_len + IEEE80211_WEP_ICV_LEN);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom + IEEE80211_WEP_IV_LEN);

	mgmt = skb_put_zero(skb, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_AUTH);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	mgmt->u.auth.status_code = cpu_to_le16(status);
	if (extra)
		skb_put_data(skb, extra, extra_len);

	if (auth_alg == WLAN_AUTH_SHARED_KEY && transaction == 3) {
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		err = ieee80211_wep_encrypt(local, skb, key, key_len, key_idx);
		WARN_ON(err);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					tx_flags;
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
		skb_put_data(skb, mgmt, IEEE80211_DEAUTH_FRAME_LEN);

		if (sdata->vif.type != NL80211_IFTYPE_STATION ||
		    !(sdata->u.mgd.flags & IEEE80211_STA_MFP_ENABLED))
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_INTFL_DONT_ENCRYPT;

		ieee80211_tx_skb(sdata, skb);
	}
}

static int ieee80211_build_preq_ies_band(struct ieee80211_local *local,
					 u8 *buffer, size_t buffer_len,
					 const u8 *ie, size_t ie_len,
					 enum nl80211_band band,
					 u32 rate_mask,
					 struct cfg80211_chan_def *chandef,
					 size_t *offset, u32 flags)
{
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *he_cap;
	u8 *pos = buffer, *end = buffer + buffer_len;
	size_t noffset;
	int supp_rates_len, i;
	u8 rates[32];
	int num_rates;
	int ext_rates_len;
	int shift;
	u32 rate_flags;
	bool have_80mhz = false;

	*offset = 0;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON_ONCE(!sband))
		return 0;

	rate_flags = ieee80211_chandef_rate_flags(chandef);
	shift = ieee80211_chandef_get_shift(chandef);

	num_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((BIT(i) & rate_mask) == 0)
			continue; /* skip rate */
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;

		rates[num_rates++] =
			(u8) DIV_ROUND_UP(sband->bitrates[i].bitrate,
					  (1 << shift) * 5);
	}

	supp_rates_len = min_t(int, num_rates, 8);

	if (end - pos < 2 + supp_rates_len)
		goto out_err;
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
					     *offset);
		if (end - pos < noffset - *offset)
			goto out_err;
		memcpy(pos, ie + *offset, noffset - *offset);
		pos += noffset - *offset;
		*offset = noffset;
	}

	ext_rates_len = num_rates - supp_rates_len;
	if (ext_rates_len > 0) {
		if (end - pos < 2 + ext_rates_len)
			goto out_err;
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = ext_rates_len;
		memcpy(pos, rates + supp_rates_len, ext_rates_len);
		pos += ext_rates_len;
	}

	if (chandef->chan && sband->band == NL80211_BAND_2GHZ) {
		if (end - pos < 3)
			goto out_err;
		*pos++ = WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = ieee80211_frequency_to_channel(
				chandef->chan->center_freq);
	}

	if (flags & IEEE80211_PROBE_FLAG_MIN_CONTENT)
		goto done;

	/* insert custom IEs that go before HT */
	if (ie && ie_len) {
		static const u8 before_ht[] = {
			/*
			 * no need to list the ones split off already
			 * (or generated here)
			 */
			WLAN_EID_DS_PARAMS,
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		noffset = ieee80211_ie_split(ie, ie_len,
					     before_ht, ARRAY_SIZE(before_ht),
					     *offset);
		if (end - pos < noffset - *offset)
			goto out_err;
		memcpy(pos, ie + *offset, noffset - *offset);
		pos += noffset - *offset;
		*offset = noffset;
	}

	if (sband->ht_cap.ht_supported) {
		if (end - pos < 2 + sizeof(struct ieee80211_ht_cap))
			goto out_err;
		pos = ieee80211_ie_build_ht_cap(pos, &sband->ht_cap,
						sband->ht_cap.cap);
	}

	/* insert custom IEs that go before VHT */
	if (ie && ie_len) {
		static const u8 before_vht[] = {
			/*
			 * no need to list the ones split off already
			 * (or generated here)
			 */
			WLAN_EID_BSS_COEX_2040,
			WLAN_EID_EXT_CAPABILITY,
			WLAN_EID_SSID_LIST,
			WLAN_EID_CHANNEL_USAGE,
			WLAN_EID_INTERWORKING,
			WLAN_EID_MESH_ID,
			/* 60 GHz (Multi-band, DMG, MMS) can't happen */
		};
		noffset = ieee80211_ie_split(ie, ie_len,
					     before_vht, ARRAY_SIZE(before_vht),
					     *offset);
		if (end - pos < noffset - *offset)
			goto out_err;
		memcpy(pos, ie + *offset, noffset - *offset);
		pos += noffset - *offset;
		*offset = noffset;
	}

	/* Check if any channel in this sband supports at least 80 MHz */
	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED |
						IEEE80211_CHAN_NO_80MHZ))
			continue;

		have_80mhz = true;
		break;
	}

	if (sband->vht_cap.vht_supported && have_80mhz) {
		if (end - pos < 2 + sizeof(struct ieee80211_vht_cap))
			goto out_err;
		pos = ieee80211_ie_build_vht_cap(pos, &sband->vht_cap,
						 sband->vht_cap.cap);
	}

	/* insert custom IEs that go before HE */
	if (ie && ie_len) {
		static const u8 before_he[] = {
			/*
			 * no need to list the ones split off before VHT
			 * or generated here
			 */
			WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_REQ_PARAMS,
			WLAN_EID_AP_CSN,
			/* TODO: add 11ah/11aj/11ak elements */
		};
		noffset = ieee80211_ie_split(ie, ie_len,
					     before_he, ARRAY_SIZE(before_he),
					     *offset);
		if (end - pos < noffset - *offset)
			goto out_err;
		memcpy(pos, ie + *offset, noffset - *offset);
		pos += noffset - *offset;
		*offset = noffset;
	}

	he_cap = ieee80211_get_he_sta_cap(sband);
	if (he_cap) {
		pos = ieee80211_ie_build_he_cap(pos, he_cap, end);
		if (!pos)
			goto out_err;
	}

	/*
	 * If adding more here, adjust code in main.c
	 * that calculates local->scan_ies_len.
	 */

	return pos - buffer;
 out_err:
	WARN_ONCE(1, "not enough space for preq IEs\n");
 done:
	return pos - buffer;
}

int ieee80211_build_preq_ies(struct ieee80211_local *local, u8 *buffer,
			     size_t buffer_len,
			     struct ieee80211_scan_ies *ie_desc,
			     const u8 *ie, size_t ie_len,
			     u8 bands_used, u32 *rate_masks,
			     struct cfg80211_chan_def *chandef,
			     u32 flags)
{
	size_t pos = 0, old_pos = 0, custom_ie_offset = 0;
	int i;

	memset(ie_desc, 0, sizeof(*ie_desc));

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		if (bands_used & BIT(i)) {
			pos += ieee80211_build_preq_ies_band(local,
							     buffer + pos,
							     buffer_len - pos,
							     ie, ie_len, i,
							     rate_masks[i],
							     chandef,
							     &custom_ie_offset,
							     flags);
			ie_desc->ies[i] = buffer + old_pos;
			ie_desc->len[i] = pos - old_pos;
			old_pos = pos;
		}
	}

	/* add any remaining custom IEs */
	if (ie && ie_len) {
		if (WARN_ONCE(buffer_len - pos < ie_len - custom_ie_offset,
			      "not enough space for preq custom IEs\n"))
			return pos;
		memcpy(buffer + pos, ie + custom_ie_offset,
		       ie_len - custom_ie_offset);
		ie_desc->common_ies = buffer + pos;
		ie_desc->common_ie_len = ie_len - custom_ie_offset;
		pos += ie_len - custom_ie_offset;
	}

	return pos;
};

struct sk_buff *ieee80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  const u8 *src, const u8 *dst,
					  u32 ratemask,
					  struct ieee80211_channel *chan,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  u32 flags)
{
	struct ieee80211_local *local = sdata->local;
	struct cfg80211_chan_def chandef;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	int ies_len;
	u32 rate_masks[NUM_NL80211_BANDS] = {};
	struct ieee80211_scan_ies dummy_ie_desc;

	/*
	 * Do not send DS Channel parameter for directed probe requests
	 * in order to maximize the chance that we get a response.  Some
	 * badly-behaved APs don't respond when this parameter is included.
	 */
	chandef.width = sdata->vif.bss_conf.chandef.width;
	if (flags & IEEE80211_PROBE_FLAG_DIRECTED)
		chandef.chan = NULL;
	else
		chandef.chan = chan;

	skb = ieee80211_probereq_get(&local->hw, src, ssid, ssid_len,
				     100 + ie_len);
	if (!skb)
		return NULL;

	rate_masks[chan->band] = ratemask;
	ies_len = ieee80211_build_preq_ies(local, skb_tail_pointer(skb),
					   skb_tailroom(skb), &dummy_ie_desc,
					   ie, ie_len, BIT(chan->band),
					   rate_masks, &chandef, flags);
	skb_put(skb, ies_len);

	if (dst) {
		mgmt = (struct ieee80211_mgmt *) skb->data;
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	return skb;
}

u32 ieee80211_sta_get_rates(struct ieee80211_sub_if_data *sdata,
			    struct ieee802_11_elems *elems,
			    enum nl80211_band band, u32 *basic_rates)
{
	struct ieee80211_supported_band *sband;
	size_t num_rates;
	u32 supp_rates, rate_flags;
	int i, j, shift;

	sband = sdata->local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return 1;

	rate_flags = ieee80211_chandef_rate_flags(&sdata->vif.bss_conf.chandef);
	shift = ieee80211_vif_get_shift(&sdata->vif);

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
			int brate;
			if ((rate_flags & sband->bitrates[j].flags)
			    != rate_flags)
				continue;

			brate = DIV_ROUND_UP(sband->bitrates[j].bitrate,
					     1 << shift);

			if (brate == own_rate) {
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

static void ieee80211_flush_completed_scan(struct ieee80211_local *local,
					   bool aborted)
{
	/* It's possible that we don't handle the scan completion in
	 * time during suspend, so if it's still marked as completed
	 * here, queue the work and flush it to clean things up.
	 * Instead of calling the worker function directly here, we
	 * really queue it to avoid potential races with other flows
	 * scheduling the same work.
	 */
	if (test_bit(SCAN_COMPLETED, &local->scanning)) {
		/* If coming from reconfiguration failure, abort the scan so
		 * we don't attempt to continue a partial HW scan - which is
		 * possible otherwise if (e.g.) the 2.4 GHz portion was the
		 * completed scan, and a 5 GHz portion is still pending.
		 */
		if (aborted)
			set_bit(SCAN_ABORTED, &local->scanning);
		ieee80211_queue_delayed_work(&local->hw, &local->scan_work, 0);
		flush_delayed_work(&local->scan_work);
	}
}

static void ieee80211_handle_reconfig_failure(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx;

	/*
	 * We get here if during resume the device can't be restarted properly.
	 * We might also get here if this happens during HW reset, which is a
	 * slightly different situation and we need to drop all connections in
	 * the latter case.
	 *
	 * Ask cfg80211 to turn off all interfaces, this will result in more
	 * warnings but at least we'll then get into a clean stopped state.
	 */

	local->resuming = false;
	local->suspended = false;
	local->in_reconfig = false;

	ieee80211_flush_completed_scan(local, true);

	/* scheduled scan clearly can't be running any more, but tell
	 * cfg80211 and clear local state
	 */
	ieee80211_sched_scan_end(local);

	list_for_each_entry(sdata, &local->interfaces, list)
		sdata->flags &= ~IEEE80211_SDATA_IN_DRIVER;

	/* Mark channel contexts as not being in the driver any more to avoid
	 * removing them from the driver during the shutdown process...
	 */
	mutex_lock(&local->chanctx_mtx);
	list_for_each_entry(ctx, &local->chanctx_list, list)
		ctx->driver_present = false;
	mutex_unlock(&local->chanctx_mtx);

	cfg80211_shutdown_all_interfaces(local->hw.wiphy);
}

static void ieee80211_assign_chanctx(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;

	if (!local->use_chanctx)
		return;

	mutex_lock(&local->chanctx_mtx);
	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (conf) {
		ctx = container_of(conf, struct ieee80211_chanctx, conf);
		drv_assign_vif_chanctx(local, sdata, ctx);
	}
	mutex_unlock(&local->chanctx_mtx);
}

static void ieee80211_reconfig_stations(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	/* add STAs back */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		enum ieee80211_sta_state state;

		if (!sta->uploaded || sta->sdata != sdata)
			continue;

		for (state = IEEE80211_STA_NOTEXIST;
		     state < sta->sta_state; state++)
			WARN_ON(drv_sta_state(local, sta->sdata, sta, state,
					      state + 1));
	}
	mutex_unlock(&local->sta_mtx);
}

static int ieee80211_reconfig_nan(struct ieee80211_sub_if_data *sdata)
{
	struct cfg80211_nan_func *func, **funcs;
	int res, id, i = 0;

	res = drv_start_nan(sdata->local, sdata,
			    &sdata->u.nan.conf);
	if (WARN_ON(res))
		return res;

	funcs = kcalloc(sdata->local->hw.max_nan_de_entries + 1,
			sizeof(*funcs),
			GFP_KERNEL);
	if (!funcs)
		return -ENOMEM;

	/* Add all the functions:
	 * This is a little bit ugly. We need to call a potentially sleeping
	 * callback for each NAN function, so we can't hold the spinlock.
	 */
	spin_lock_bh(&sdata->u.nan.func_lock);

	idr_for_each_entry(&sdata->u.nan.function_inst_ids, func, id)
		funcs[i++] = func;

	spin_unlock_bh(&sdata->u.nan.func_lock);

	for (i = 0; funcs[i]; i++) {
		res = drv_add_nan_func(sdata->local, sdata, funcs[i]);
		if (WARN_ON(res))
			ieee80211_nan_func_terminated(&sdata->vif,
						      funcs[i]->instance_id,
						      NL80211_NAN_FUNC_TERM_REASON_ERROR,
						      GFP_KERNEL);
	}

	kfree(funcs);

	return 0;
}

int ieee80211_reconfig(struct ieee80211_local *local)
{
	struct ieee80211_hw *hw = &local->hw;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx;
	struct sta_info *sta;
	int res, i;
	bool reconfig_due_to_wowlan = false;
	struct ieee80211_sub_if_data *sched_scan_sdata;
	struct cfg80211_sched_scan_request *sched_scan_req;
	bool sched_scan_stopped = false;
	bool suspended = local->suspended;

	/* nothing to do if HW shouldn't run */
	if (!local->open_count)
		goto wake_up;

#ifdef CONFIG_PM
	if (suspended)
		local->resuming = true;

	if (local->wowlan) {
		/*
		 * In the wowlan case, both mac80211 and the device
		 * are functional when the resume op is called, so
		 * clear local->suspended so the device could operate
		 * normally (e.g. pass rx frames).
		 */
		local->suspended = false;
		res = drv_resume(local);
		local->wowlan = false;
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
		 * restore local->suspended in this case.
		 */
		reconfig_due_to_wowlan = true;
		local->suspended = true;
	}
#endif

	/*
	 * In case of hw_restart during suspend (without wowlan),
	 * cancel restart work, as we are reconfiguring the device
	 * anyway.
	 * Note that restart_work is scheduled on a frozen workqueue,
	 * so we can't deadlock in this case.
	 */
	if (suspended && local->in_reconfig && !reconfig_due_to_wowlan)
		cancel_work_sync(&local->restart_work);

	local->started = false;

	/*
	 * Upon resume hardware can sometimes be goofy due to
	 * various platform / driver / bus issues, so restarting
	 * the device may at times not work immediately. Propagate
	 * the error.
	 */
	res = drv_start(local);
	if (res) {
		if (suspended)
			WARN(1, "Hardware became unavailable upon resume. This could be a software issue prior to suspend or a hardware issue.\n");
		else
			WARN(1, "Hardware became unavailable during restart.\n");
		ieee80211_handle_reconfig_failure(local);
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
		/* in HW restart it exists already */
		WARN_ON(local->resuming);
		res = drv_add_interface(local, sdata);
		if (WARN_ON(res)) {
			RCU_INIT_POINTER(local->monitor_sdata, NULL);
			synchronize_net();
			kfree(sdata);
		}
	}

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    ieee80211_sdata_running(sdata)) {
			res = drv_add_interface(local, sdata);
			if (WARN_ON(res))
				break;
		}
	}

	/* If adding any of the interfaces failed above, roll back and
	 * report failure.
	 */
	if (res) {
		list_for_each_entry_continue_reverse(sdata, &local->interfaces,
						     list)
			if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
			    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
			    ieee80211_sdata_running(sdata))
				drv_remove_interface(local, sdata);
		ieee80211_handle_reconfig_failure(local);
		return res;
	}

	/* add channel contexts */
	if (local->use_chanctx) {
		mutex_lock(&local->chanctx_mtx);
		list_for_each_entry(ctx, &local->chanctx_list, list)
			if (ctx->replace_state !=
			    IEEE80211_CHANCTX_REPLACES_OTHER)
				WARN_ON(drv_add_chanctx(local, ctx));
		mutex_unlock(&local->chanctx_mtx);

		sdata = rtnl_dereference(local->monitor_sdata);
		if (sdata && ieee80211_sdata_running(sdata))
			ieee80211_assign_chanctx(local, sdata);
	}

	/* reconfigure hardware */
	ieee80211_hw_config(local, ~0);

	ieee80211_configure_filter(local);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		u32 changed;

		if (!ieee80211_sdata_running(sdata))
			continue;

		ieee80211_assign_chanctx(local, sdata);

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			break;
		default:
			ieee80211_reconfig_stations(sdata);
			/* fall through */
		case NL80211_IFTYPE_AP: /* AP stations are handled later */
			for (i = 0; i < IEEE80211_NUM_ACS; i++)
				drv_conf_tx(local, sdata, i,
					    &sdata->tx_conf[i]);
			break;
		}

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
			  BSS_CHANGED_TXPOWER |
			  BSS_CHANGED_MCAST_RATE;

		if (sdata->vif.mu_mimo_owner)
			changed |= BSS_CHANGED_MU_GROUPS;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			changed |= BSS_CHANGED_ASSOC |
				   BSS_CHANGED_ARP_FILTER |
				   BSS_CHANGED_PS;

			/* Re-send beacon info report to the driver */
			if (sdata->u.mgd.have_beacon)
				changed |= BSS_CHANGED_BEACON_INFO;

			if (sdata->vif.bss_conf.max_idle_period ||
			    sdata->vif.bss_conf.protected_keep_alive)
				changed |= BSS_CHANGED_KEEP_ALIVE;

			sdata_lock(sdata);
			ieee80211_bss_info_change_notify(sdata, changed);
			sdata_unlock(sdata);
			break;
		case NL80211_IFTYPE_OCB:
			changed |= BSS_CHANGED_OCB;
			ieee80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_ADHOC:
			changed |= BSS_CHANGED_IBSS;
			/* fall through */
		case NL80211_IFTYPE_AP:
			changed |= BSS_CHANGED_SSID | BSS_CHANGED_P2P_PS;

			if (sdata->vif.type == NL80211_IFTYPE_AP) {
				changed |= BSS_CHANGED_AP_PROBE_RESP;

				if (rcu_access_pointer(sdata->u.ap.beacon))
					drv_start_ap(local, sdata);
			}

			/* fall through */
		case NL80211_IFTYPE_MESH_POINT:
			if (sdata->vif.bss_conf.enable_beacon) {
				changed |= BSS_CHANGED_BEACON |
					   BSS_CHANGED_BEACON_ENABLED;
				ieee80211_bss_info_change_notify(sdata, changed);
			}
			break;
		case NL80211_IFTYPE_NAN:
			res = ieee80211_reconfig_nan(sdata);
			if (res < 0) {
				ieee80211_handle_reconfig_failure(local);
				return res;
			}
			break;
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_P2P_DEVICE:
			/* nothing to do */
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
			WARN_ON(1);
			break;
		}
	}

	ieee80211_recalc_ps(local);

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
			if (!sdata->u.mgd.associated)
				continue;

			ieee80211_send_nullfunc(local, sdata, false);
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
		ieee80211_reset_crypto_tx_tailroom(sdata);

	list_for_each_entry(sdata, &local->interfaces, list)
		if (ieee80211_sdata_running(sdata))
			ieee80211_enable_keys(sdata);

	/* Reconfigure sched scan if it was interrupted by FW restart */
	mutex_lock(&local->mtx);
	sched_scan_sdata = rcu_dereference_protected(local->sched_scan_sdata,
						lockdep_is_held(&local->mtx));
	sched_scan_req = rcu_dereference_protected(local->sched_scan_req,
						lockdep_is_held(&local->mtx));
	if (sched_scan_sdata && sched_scan_req)
		/*
		 * Sched scan stopped, but we don't want to report it. Instead,
		 * we're trying to reschedule. However, if more than one scan
		 * plan was set, we cannot reschedule since we don't know which
		 * scan plan was currently running (and some scan plans may have
		 * already finished).
		 */
		if (sched_scan_req->n_scan_plans > 1 ||
		    __ieee80211_request_sched_scan_start(sched_scan_sdata,
							 sched_scan_req)) {
			RCU_INIT_POINTER(local->sched_scan_sdata, NULL);
			RCU_INIT_POINTER(local->sched_scan_req, NULL);
			sched_scan_stopped = true;
		}
	mutex_unlock(&local->mtx);

	if (sched_scan_stopped)
		cfg80211_sched_scan_stopped_rtnl(local->hw.wiphy, 0);

 wake_up:

	if (local->monitors == local->open_count && local->monitors > 0)
		ieee80211_add_virtual_monitor(local);

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
	if (ieee80211_hw_check(hw, AMPDU_AGGREGATION)) {
		mutex_lock(&local->sta_mtx);

		list_for_each_entry(sta, &local->sta_list, list) {
			if (!local->resuming)
				ieee80211_sta_tear_down_BA_sessions(
						sta, AGG_STOP_LOCAL_REQUEST);
			clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
		}

		mutex_unlock(&local->sta_mtx);
	}

	if (local->in_reconfig) {
		local->in_reconfig = false;
		barrier();

		/* Restart deferred ROCs */
		mutex_lock(&local->mtx);
		ieee80211_start_next_roc(local);
		mutex_unlock(&local->mtx);
	}

	ieee80211_wake_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_SUSPEND,
					false);

	/*
	 * If this is for hw restart things are still running.
	 * We may want to change that later, however.
	 */
	if (local->open_count && (!suspended || reconfig_due_to_wowlan))
		drv_reconfig_complete(local, IEEE80211_RECONFIG_TYPE_RESTART);

	if (!suspended)
		return 0;

#ifdef CONFIG_PM
	/* first set suspended false, then resuming */
	local->suspended = false;
	mb();
	local->resuming = false;

	ieee80211_flush_completed_scan(local, false);

	if (local->open_count && !reconfig_due_to_wowlan)
		drv_reconfig_complete(local, IEEE80211_RECONFIG_TYPE_SUSPEND);

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_STATION)
			ieee80211_sta_restart(sdata);
	}

	mod_timer(&local->sta_cleanup, jiffies + 1);
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

	/*
	 * This function can be called from a work, thus it may be possible
	 * that the chanctx_conf is removed (due to a disconnection, for
	 * example).
	 * So nothing should be done in such case.
	 */
	if (!chanctx_conf)
		goto unlock;

	chanctx = container_of(chanctx_conf, struct ieee80211_chanctx, conf);
	ieee80211_recalc_smps_chanctx(local, chanctx);
 unlock:
	mutex_unlock(&local->chanctx_mtx);
}

void ieee80211_recalc_min_chandef(struct ieee80211_sub_if_data *sdata)
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
	ieee80211_recalc_chanctx_min_def(local, chanctx);
 unlock:
	mutex_unlock(&local->chanctx_mtx);
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

u8 *ieee80211_ie_build_he_cap(u8 *pos,
			      const struct ieee80211_sta_he_cap *he_cap,
			      u8 *end)
{
	u8 n;
	u8 ie_len;
	u8 *orig_pos = pos;

	/* Make sure we have place for the IE */
	/*
	 * TODO: the 1 added is because this temporarily is under the EXTENSION
	 * IE. Get rid of it when it moves.
	 */
	if (!he_cap)
		return orig_pos;

	n = ieee80211_he_mcs_nss_size(&he_cap->he_cap_elem);
	ie_len = 2 + 1 +
		 sizeof(he_cap->he_cap_elem) + n +
		 ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				       he_cap->he_cap_elem.phy_cap_info);

	if ((end - pos) < ie_len)
		return orig_pos;

	*pos++ = WLAN_EID_EXTENSION;
	pos++; /* We'll set the size later below */
	*pos++ = WLAN_EID_EXT_HE_CAPABILITY;

	/* Fixed data */
	memcpy(pos, &he_cap->he_cap_elem, sizeof(he_cap->he_cap_elem));
	pos += sizeof(he_cap->he_cap_elem);

	memcpy(pos, &he_cap->he_mcs_nss_supp, n);
	pos += n;

	/* Check if PPE Threshold should be present */
	if ((he_cap->he_cap_elem.phy_cap_info[6] &
	     IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) == 0)
		goto end;

	/*
	 * Calculate how many PPET16/PPET8 pairs are to come. Algorithm:
	 * (NSS_M1 + 1) x (num of 1 bits in RU_INDEX_BITMASK)
	 */
	n = hweight8(he_cap->ppe_thres[0] &
		     IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= (1 + ((he_cap->ppe_thres[0] & IEEE80211_PPE_THRES_NSS_MASK) >>
		   IEEE80211_PPE_THRES_NSS_POS));

	/*
	 * Each pair is 6 bits, and we need to add the 7 "header" bits to the
	 * total size.
	 */
	n = (n * IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2) + 7;
	n = DIV_ROUND_UP(n, 8);

	/* Copy PPE Thresholds */
	memcpy(pos, &he_cap->ppe_thres, n);
	pos += n;

end:
	orig_pos[1] = (pos - orig_pos) - 2;
	return pos;
}

u8 *ieee80211_ie_build_ht_oper(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			       const struct cfg80211_chan_def *chandef,
			       u16 prot_mode, bool rifs_mode)
{
	struct ieee80211_ht_operation *ht_oper;
	/* Build HT Information */
	*pos++ = WLAN_EID_HT_OPERATION;
	*pos++ = sizeof(struct ieee80211_ht_operation);
	ht_oper = (struct ieee80211_ht_operation *)pos;
	ht_oper->primary_chan = ieee80211_frequency_to_channel(
					chandef->chan->center_freq);
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_160:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_40:
		if (chandef->center_freq1 > chandef->chan->center_freq)
			ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
		else
			ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
		break;
	default:
		ht_oper->ht_param = IEEE80211_HT_PARAM_CHA_SEC_NONE;
		break;
	}
	if (ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40 &&
	    chandef->width != NL80211_CHAN_WIDTH_20_NOHT &&
	    chandef->width != NL80211_CHAN_WIDTH_20)
		ht_oper->ht_param |= IEEE80211_HT_PARAM_CHAN_WIDTH_ANY;

	if (rifs_mode)
		ht_oper->ht_param |= IEEE80211_HT_PARAM_RIFS_MODE;

	ht_oper->operation_mode = cpu_to_le16(prot_mode);
	ht_oper->stbc_param = 0x0000;

	/* It seems that Basic MCS set and Supported MCS set
	   are identical for the first 10 bytes */
	memset(&ht_oper->basic_set, 0, 16);
	memcpy(&ht_oper->basic_set, &ht_cap->mcs, 10);

	return pos + sizeof(struct ieee80211_ht_operation);
}

void ieee80211_ie_build_wide_bw_cs(u8 *pos,
				   const struct cfg80211_chan_def *chandef)
{
	*pos++ = WLAN_EID_WIDE_BW_CHANNEL_SWITCH;	/* EID */
	*pos++ = 3;					/* IE length */
	/* New channel width */
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_80:
		*pos++ = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		*pos++ = IEEE80211_VHT_CHANWIDTH_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		*pos++ = IEEE80211_VHT_CHANWIDTH_80P80MHZ;
		break;
	default:
		*pos++ = IEEE80211_VHT_CHANWIDTH_USE_HT;
	}

	/* new center frequency segment 0 */
	*pos++ = ieee80211_frequency_to_channel(chandef->center_freq1);
	/* new center frequency segment 1 */
	if (chandef->center_freq2)
		*pos++ = ieee80211_frequency_to_channel(chandef->center_freq2);
	else
		*pos++ = 0;
}

u8 *ieee80211_ie_build_vht_oper(u8 *pos, struct ieee80211_sta_vht_cap *vht_cap,
				const struct cfg80211_chan_def *chandef)
{
	struct ieee80211_vht_operation *vht_oper;

	*pos++ = WLAN_EID_VHT_OPERATION;
	*pos++ = sizeof(struct ieee80211_vht_operation);
	vht_oper = (struct ieee80211_vht_operation *)pos;
	vht_oper->center_freq_seg0_idx = ieee80211_frequency_to_channel(
							chandef->center_freq1);
	if (chandef->center_freq2)
		vht_oper->center_freq_seg1_idx =
			ieee80211_frequency_to_channel(chandef->center_freq2);
	else
		vht_oper->center_freq_seg1_idx = 0x00;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_160:
		/*
		 * Convert 160 MHz channel width to new style as interop
		 * workaround.
		 */
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		vht_oper->center_freq_seg1_idx = vht_oper->center_freq_seg0_idx;
		if (chandef->chan->center_freq < chandef->center_freq1)
			vht_oper->center_freq_seg0_idx -= 8;
		else
			vht_oper->center_freq_seg0_idx += 8;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		/*
		 * Convert 80+80 MHz channel width to new style as interop
		 * workaround.
		 */
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	default:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_USE_HT;
		break;
	}

	/* don't require special VHT peer rates */
	vht_oper->basic_mcs_set = cpu_to_le16(0xffff);

	return pos + sizeof(struct ieee80211_vht_operation);
}

bool ieee80211_chandef_ht_oper(const struct ieee80211_ht_operation *ht_oper,
			       struct cfg80211_chan_def *chandef)
{
	enum nl80211_channel_type channel_type;

	if (!ht_oper)
		return false;

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
		return false;
	}

	cfg80211_chandef_create(chandef, chandef->chan, channel_type);
	return true;
}

bool ieee80211_chandef_vht_oper(const struct ieee80211_vht_operation *oper,
				struct cfg80211_chan_def *chandef)
{
	struct cfg80211_chan_def new = *chandef;
	int cf1, cf2;

	if (!oper)
		return false;

	cf1 = ieee80211_channel_to_frequency(oper->center_freq_seg0_idx,
					     chandef->chan->band);
	cf2 = ieee80211_channel_to_frequency(oper->center_freq_seg1_idx,
					     chandef->chan->band);

	switch (oper->chan_width) {
	case IEEE80211_VHT_CHANWIDTH_USE_HT:
		break;
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		new.width = NL80211_CHAN_WIDTH_80;
		new.center_freq1 = cf1;
		/* If needed, adjust based on the newer interop workaround. */
		if (oper->center_freq_seg1_idx) {
			unsigned int diff;

			diff = abs(oper->center_freq_seg1_idx -
				   oper->center_freq_seg0_idx);
			if (diff == 8) {
				new.width = NL80211_CHAN_WIDTH_160;
				new.center_freq1 = cf2;
			} else if (diff > 8) {
				new.width = NL80211_CHAN_WIDTH_80P80;
				new.center_freq2 = cf2;
			}
		}
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		new.width = NL80211_CHAN_WIDTH_160;
		new.center_freq1 = cf1;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		new.width = NL80211_CHAN_WIDTH_80P80;
		new.center_freq1 = cf1;
		new.center_freq2 = cf2;
		break;
	default:
		return false;
	}

	if (!cfg80211_chandef_valid(&new))
		return false;

	*chandef = new;
	return true;
}

int ieee80211_parse_bitrates(struct cfg80211_chan_def *chandef,
			     const struct ieee80211_supported_band *sband,
			     const u8 *srates, int srates_len, u32 *rates)
{
	u32 rate_flags = ieee80211_chandef_rate_flags(chandef);
	int shift = ieee80211_chandef_get_shift(chandef);
	struct ieee80211_rate *br;
	int brate, rate, i, j, count = 0;

	*rates = 0;

	for (i = 0; i < srates_len; i++) {
		rate = srates[i] & 0x7f;

		for (j = 0; j < sband->n_bitrates; j++) {
			br = &sband->bitrates[j];
			if ((rate_flags & br->flags) != rate_flags)
				continue;

			brate = DIV_ROUND_UP(br->bitrate, (1 << shift) * 5);
			if (brate == rate) {
				*rates |= BIT(j);
				count++;
				break;
			}
		}
	}
	return count;
}

int ieee80211_add_srates_ie(struct ieee80211_sub_if_data *sdata,
			    struct sk_buff *skb, bool need_basic,
			    enum nl80211_band band)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	int rate, shift;
	u8 i, rates, *pos;
	u32 basic_rates = sdata->vif.bss_conf.basic_rates;
	u32 rate_flags;

	shift = ieee80211_vif_get_shift(&sdata->vif);
	rate_flags = ieee80211_chandef_rate_flags(&sdata->vif.bss_conf.chandef);
	sband = local->hw.wiphy->bands[band];
	rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;
		rates++;
	}
	if (rates > 8)
		rates = 8;

	if (skb_tailroom(skb) < rates + 2)
		return -ENOMEM;

	pos = skb_put(skb, rates + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = rates;
	for (i = 0; i < rates; i++) {
		u8 basic = 0;
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;

		if (need_basic && basic_rates & BIT(i))
			basic = 0x80;
		rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
				    5 * (1 << shift));
		*pos++ = basic | (u8) rate;
	}

	return 0;
}

int ieee80211_add_ext_srates_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, bool need_basic,
				enum nl80211_band band)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	int rate, shift;
	u8 i, exrates, *pos;
	u32 basic_rates = sdata->vif.bss_conf.basic_rates;
	u32 rate_flags;

	rate_flags = ieee80211_chandef_rate_flags(&sdata->vif.bss_conf.chandef);
	shift = ieee80211_vif_get_shift(&sdata->vif);

	sband = local->hw.wiphy->bands[band];
	exrates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;
		exrates++;
	}

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
			if ((rate_flags & sband->bitrates[i].flags)
			    != rate_flags)
				continue;
			if (need_basic && basic_rates & BIT(i))
				basic = 0x80;
			rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
					    5 * (1 << shift));
			*pos++ = basic | (u8) rate;
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
	return -ewma_beacon_signal_read(&ifmgd->ave_beacon_signal);
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

/**
 * ieee80211_calculate_rx_timestamp - calculate timestamp in frame
 * @local: mac80211 hw info struct
 * @status: RX status
 * @mpdu_len: total MPDU length (including FCS)
 * @mpdu_offset: offset into MPDU to calculate timestamp at
 *
 * This function calculates the RX timestamp at the given MPDU offset, taking
 * into account what the RX timestamp was. An offset of 0 will just normalize
 * the timestamp to TSF at beginning of MPDU reception.
 */
u64 ieee80211_calculate_rx_timestamp(struct ieee80211_local *local,
				     struct ieee80211_rx_status *status,
				     unsigned int mpdu_len,
				     unsigned int mpdu_offset)
{
	u64 ts = status->mactime;
	struct rate_info ri;
	u16 rate;

	if (WARN_ON(!ieee80211_have_rx_timestamp(status)))
		return 0;

	memset(&ri, 0, sizeof(ri));

	ri.bw = status->bw;

	/* Fill cfg80211 rate info */
	switch (status->encoding) {
	case RX_ENC_HT:
		ri.mcs = status->rate_idx;
		ri.flags |= RATE_INFO_FLAGS_MCS;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case RX_ENC_VHT:
		ri.flags |= RATE_INFO_FLAGS_VHT_MCS;
		ri.mcs = status->rate_idx;
		ri.nss = status->nss;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	default:
		WARN_ON(1);
		/* fall through */
	case RX_ENC_LEGACY: {
		struct ieee80211_supported_band *sband;
		int shift = 0;
		int bitrate;

		switch (status->bw) {
		case RATE_INFO_BW_10:
			shift = 1;
			break;
		case RATE_INFO_BW_5:
			shift = 2;
			break;
		}

		sband = local->hw.wiphy->bands[status->band];
		bitrate = sband->bitrates[status->rate_idx].bitrate;
		ri.legacy = DIV_ROUND_UP(bitrate, (1 << shift));

		if (status->flag & RX_FLAG_MACTIME_PLCP_START) {
			/* TODO: handle HT/VHT preambles */
			if (status->band == NL80211_BAND_5GHZ) {
				ts += 20 << shift;
				mpdu_offset += 2;
			} else if (status->enc_flags & RX_ENC_FLAG_SHORTPRE) {
				ts += 96;
			} else {
				ts += 192;
			}
		}
		break;
		}
	}

	rate = cfg80211_calculate_bitrate(&ri);
	if (WARN_ONCE(!rate,
		      "Invalid bitrate: flags=0x%llx, idx=%d, vht_nss=%d\n",
		      (unsigned long long)status->flag, status->rate_idx,
		      status->nss))
		return 0;

	/* rewind from end of MPDU */
	if (status->flag & RX_FLAG_MACTIME_END)
		ts -= mpdu_len * 8 * 10 / rate;

	ts += mpdu_offset * 8 * 10 / rate;

	return ts;
}

void ieee80211_dfs_cac_cancel(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_chan_def chandef;

	/* for interface list, to avoid linking iflist_mtx and chanctx_mtx */
	ASSERT_RTNL();

	mutex_lock(&local->mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		/* it might be waiting for the local->mtx, but then
		 * by the time it gets it, sdata->wdev.cac_started
		 * will no longer be true
		 */
		cancel_delayed_work(&sdata->dfs_cac_timer_work);

		if (sdata->wdev.cac_started) {
			chandef = sdata->vif.bss_conf.chandef;
			ieee80211_vif_release_channel(sdata);
			cfg80211_cac_event(sdata->dev,
					   &chandef,
					   NL80211_RADAR_CAC_ABORTED,
					   GFP_KERNEL);
		}
	}
	mutex_unlock(&local->mtx);
}

void ieee80211_dfs_radar_detected_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, radar_detected_work);
	struct cfg80211_chan_def chandef = local->hw.conf.chandef;
	struct ieee80211_chanctx *ctx;
	int num_chanctx = 0;

	mutex_lock(&local->chanctx_mtx);
	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		num_chanctx++;
		chandef = ctx->conf.def;
	}
	mutex_unlock(&local->chanctx_mtx);

	rtnl_lock();
	ieee80211_dfs_cac_cancel(local);
	rtnl_unlock();

	if (num_chanctx > 1)
		/* XXX: multi-channel is not supported yet */
		WARN_ON(1);
	else
		cfg80211_radar_event(local->hw.wiphy, &chandef, GFP_KERNEL);
}

void ieee80211_radar_detected(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_radar_detected(local);

	schedule_work(&local->radar_detected_work);
}
EXPORT_SYMBOL(ieee80211_radar_detected);

u32 ieee80211_chandef_downgrade(struct cfg80211_chan_def *c)
{
	u32 ret;
	int tmp;

	switch (c->width) {
	case NL80211_CHAN_WIDTH_20:
		c->width = NL80211_CHAN_WIDTH_20_NOHT;
		ret = IEEE80211_STA_DISABLE_HT | IEEE80211_STA_DISABLE_VHT;
		break;
	case NL80211_CHAN_WIDTH_40:
		c->width = NL80211_CHAN_WIDTH_20;
		c->center_freq1 = c->chan->center_freq;
		ret = IEEE80211_STA_DISABLE_40MHZ |
		      IEEE80211_STA_DISABLE_VHT;
		break;
	case NL80211_CHAN_WIDTH_80:
		tmp = (30 + c->chan->center_freq - c->center_freq1)/20;
		/* n_P40 */
		tmp /= 2;
		/* freq_P40 */
		c->center_freq1 = c->center_freq1 - 20 + 40 * tmp;
		c->width = NL80211_CHAN_WIDTH_40;
		ret = IEEE80211_STA_DISABLE_VHT;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		c->center_freq2 = 0;
		c->width = NL80211_CHAN_WIDTH_80;
		ret = IEEE80211_STA_DISABLE_80P80MHZ |
		      IEEE80211_STA_DISABLE_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_160:
		/* n_P20 */
		tmp = (70 + c->chan->center_freq - c->center_freq1)/20;
		/* n_P80 */
		tmp /= 4;
		c->center_freq1 = c->center_freq1 - 40 + 80 * tmp;
		c->width = NL80211_CHAN_WIDTH_80;
		ret = IEEE80211_STA_DISABLE_80P80MHZ |
		      IEEE80211_STA_DISABLE_160MHZ;
		break;
	default:
	case NL80211_CHAN_WIDTH_20_NOHT:
		WARN_ON_ONCE(1);
		c->width = NL80211_CHAN_WIDTH_20_NOHT;
		ret = IEEE80211_STA_DISABLE_HT | IEEE80211_STA_DISABLE_VHT;
		break;
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
		WARN_ON_ONCE(1);
		/* keep c->width */
		ret = IEEE80211_STA_DISABLE_HT | IEEE80211_STA_DISABLE_VHT;
		break;
	}

	WARN_ON_ONCE(!cfg80211_chandef_valid(c));

	return ret;
}

/*
 * Returns true if smps_mode_new is strictly more restrictive than
 * smps_mode_old.
 */
bool ieee80211_smps_is_restrictive(enum ieee80211_smps_mode smps_mode_old,
				   enum ieee80211_smps_mode smps_mode_new)
{
	if (WARN_ON_ONCE(smps_mode_old == IEEE80211_SMPS_AUTOMATIC ||
			 smps_mode_new == IEEE80211_SMPS_AUTOMATIC))
		return false;

	switch (smps_mode_old) {
	case IEEE80211_SMPS_STATIC:
		return false;
	case IEEE80211_SMPS_DYNAMIC:
		return smps_mode_new == IEEE80211_SMPS_STATIC;
	case IEEE80211_SMPS_OFF:
		return smps_mode_new != IEEE80211_SMPS_OFF;
	default:
		WARN_ON(1);
	}

	return false;
}

int ieee80211_send_action_csa(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings)
{
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_local *local = sdata->local;
	int freq;
	int hdr_len = offsetofend(struct ieee80211_mgmt,
				  u.action.u.chan_switch);
	u8 *pos;

	if (sdata->vif.type != NL80211_IFTYPE_ADHOC &&
	    sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	skb = dev_alloc_skb(local->tx_headroom + hdr_len +
			    5 + /* channel switch announcement element */
			    3 + /* secondary channel offset element */
			    5 + /* wide bandwidth channel switch announcement */
			    8); /* mesh channel switch parameters element */
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, local->tx_headroom);
	mgmt = skb_put_zero(skb, hdr_len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	eth_broadcast_addr(mgmt->da);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	} else {
		struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
		memcpy(mgmt->bssid, ifibss->bssid, ETH_ALEN);
	}
	mgmt->u.action.category = WLAN_CATEGORY_SPECTRUM_MGMT;
	mgmt->u.action.u.chan_switch.action_code = WLAN_ACTION_SPCT_CHL_SWITCH;
	pos = skb_put(skb, 5);
	*pos++ = WLAN_EID_CHANNEL_SWITCH;			/* EID */
	*pos++ = 3;						/* IE length */
	*pos++ = csa_settings->block_tx ? 1 : 0;		/* CSA mode */
	freq = csa_settings->chandef.chan->center_freq;
	*pos++ = ieee80211_frequency_to_channel(freq);		/* channel */
	*pos++ = csa_settings->count;				/* count */

	if (csa_settings->chandef.width == NL80211_CHAN_WIDTH_40) {
		enum nl80211_channel_type ch_type;

		skb_put(skb, 3);
		*pos++ = WLAN_EID_SECONDARY_CHANNEL_OFFSET;	/* EID */
		*pos++ = 1;					/* IE length */
		ch_type = cfg80211_get_chandef_type(&csa_settings->chandef);
		if (ch_type == NL80211_CHAN_HT40PLUS)
			*pos++ = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
		else
			*pos++ = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
	}

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

		skb_put(skb, 8);
		*pos++ = WLAN_EID_CHAN_SWITCH_PARAM;		/* EID */
		*pos++ = 6;					/* IE length */
		*pos++ = sdata->u.mesh.mshcfg.dot11MeshTTL;	/* Mesh TTL */
		*pos = 0x00;	/* Mesh Flag: Tx Restrict, Initiator, Reason */
		*pos |= WLAN_EID_CHAN_SWITCH_PARAM_INITIATOR;
		*pos++ |= csa_settings->block_tx ?
			  WLAN_EID_CHAN_SWITCH_PARAM_TX_RESTRICT : 0x00;
		put_unaligned_le16(WLAN_REASON_MESH_CHAN, pos); /* Reason Cd */
		pos += 2;
		put_unaligned_le16(ifmsh->pre_value, pos);/* Precedence Value */
		pos += 2;
	}

	if (csa_settings->chandef.width == NL80211_CHAN_WIDTH_80 ||
	    csa_settings->chandef.width == NL80211_CHAN_WIDTH_80P80 ||
	    csa_settings->chandef.width == NL80211_CHAN_WIDTH_160) {
		skb_put(skb, 5);
		ieee80211_ie_build_wide_bw_cs(pos, &csa_settings->chandef);
	}

	ieee80211_tx_skb(sdata, skb);
	return 0;
}

bool ieee80211_cs_valid(const struct ieee80211_cipher_scheme *cs)
{
	return !(cs == NULL || cs->cipher == 0 ||
		 cs->hdr_len < cs->pn_len + cs->pn_off ||
		 cs->hdr_len <= cs->key_idx_off ||
		 cs->key_idx_shift > 7 ||
		 cs->key_idx_mask == 0);
}

bool ieee80211_cs_list_valid(const struct ieee80211_cipher_scheme *cs, int n)
{
	int i;

	/* Ensure we have enough iftype bitmap space for all iftype values */
	WARN_ON((NUM_NL80211_IFTYPES / 8 + 1) > sizeof(cs[0].iftype));

	for (i = 0; i < n; i++)
		if (!ieee80211_cs_valid(&cs[i]))
			return false;

	return true;
}

const struct ieee80211_cipher_scheme *
ieee80211_cs_get(struct ieee80211_local *local, u32 cipher,
		 enum nl80211_iftype iftype)
{
	const struct ieee80211_cipher_scheme *l = local->hw.cipher_schemes;
	int n = local->hw.n_cipher_schemes;
	int i;
	const struct ieee80211_cipher_scheme *cs = NULL;

	for (i = 0; i < n; i++) {
		if (l[i].cipher == cipher) {
			cs = &l[i];
			break;
		}
	}

	if (!cs || !(cs->iftype & BIT(iftype)))
		return NULL;

	return cs;
}

int ieee80211_cs_headroom(struct ieee80211_local *local,
			  struct cfg80211_crypto_settings *crypto,
			  enum nl80211_iftype iftype)
{
	const struct ieee80211_cipher_scheme *cs;
	int headroom = IEEE80211_ENCRYPT_HEADROOM;
	int i;

	for (i = 0; i < crypto->n_ciphers_pairwise; i++) {
		cs = ieee80211_cs_get(local, crypto->ciphers_pairwise[i],
				      iftype);

		if (cs && headroom < cs->hdr_len)
			headroom = cs->hdr_len;
	}

	cs = ieee80211_cs_get(local, crypto->cipher_group, iftype);
	if (cs && headroom < cs->hdr_len)
		headroom = cs->hdr_len;

	return headroom;
}

static bool
ieee80211_extend_noa_desc(struct ieee80211_noa_data *data, u32 tsf, int i)
{
	s32 end = data->desc[i].start + data->desc[i].duration - (tsf + 1);
	int skip;

	if (end > 0)
		return false;

	/* One shot NOA  */
	if (data->count[i] == 1)
		return false;

	if (data->desc[i].interval == 0)
		return false;

	/* End time is in the past, check for repetitions */
	skip = DIV_ROUND_UP(-end, data->desc[i].interval);
	if (data->count[i] < 255) {
		if (data->count[i] <= skip) {
			data->count[i] = 0;
			return false;
		}

		data->count[i] -= skip;
	}

	data->desc[i].start += skip * data->desc[i].interval;

	return true;
}

static bool
ieee80211_extend_absent_time(struct ieee80211_noa_data *data, u32 tsf,
			     s32 *offset)
{
	bool ret = false;
	int i;

	for (i = 0; i < IEEE80211_P2P_NOA_DESC_MAX; i++) {
		s32 cur;

		if (!data->count[i])
			continue;

		if (ieee80211_extend_noa_desc(data, tsf + *offset, i))
			ret = true;

		cur = data->desc[i].start - tsf;
		if (cur > *offset)
			continue;

		cur = data->desc[i].start + data->desc[i].duration - tsf;
		if (cur > *offset)
			*offset = cur;
	}

	return ret;
}

static u32
ieee80211_get_noa_absent_time(struct ieee80211_noa_data *data, u32 tsf)
{
	s32 offset = 0;
	int tries = 0;
	/*
	 * arbitrary limit, used to avoid infinite loops when combined NoA
	 * descriptors cover the full time period.
	 */
	int max_tries = 5;

	ieee80211_extend_absent_time(data, tsf, &offset);
	do {
		if (!ieee80211_extend_absent_time(data, tsf, &offset))
			break;

		tries++;
	} while (tries < max_tries);

	return offset;
}

void ieee80211_update_p2p_noa(struct ieee80211_noa_data *data, u32 tsf)
{
	u32 next_offset = BIT(31) - 1;
	int i;

	data->absent = 0;
	data->has_next_tsf = false;
	for (i = 0; i < IEEE80211_P2P_NOA_DESC_MAX; i++) {
		s32 start;

		if (!data->count[i])
			continue;

		ieee80211_extend_noa_desc(data, tsf, i);
		start = data->desc[i].start - tsf;
		if (start <= 0)
			data->absent |= BIT(i);

		if (next_offset > start)
			next_offset = start;

		data->has_next_tsf = true;
	}

	if (data->absent)
		next_offset = ieee80211_get_noa_absent_time(data, tsf);

	data->next_tsf = tsf + next_offset;
}
EXPORT_SYMBOL(ieee80211_update_p2p_noa);

int ieee80211_parse_p2p_noa(const struct ieee80211_p2p_noa_attr *attr,
			    struct ieee80211_noa_data *data, u32 tsf)
{
	int ret = 0;
	int i;

	memset(data, 0, sizeof(*data));

	for (i = 0; i < IEEE80211_P2P_NOA_DESC_MAX; i++) {
		const struct ieee80211_p2p_noa_desc *desc = &attr->desc[i];

		if (!desc->count || !desc->duration)
			continue;

		data->count[i] = desc->count;
		data->desc[i].start = le32_to_cpu(desc->start_time);
		data->desc[i].duration = le32_to_cpu(desc->duration);
		data->desc[i].interval = le32_to_cpu(desc->interval);

		if (data->count[i] > 1 &&
		    data->desc[i].interval < data->desc[i].duration)
			continue;

		ieee80211_extend_noa_desc(data, tsf, i);
		ret++;
	}

	if (ret)
		ieee80211_update_p2p_noa(data, tsf);

	return ret;
}
EXPORT_SYMBOL(ieee80211_parse_p2p_noa);

void ieee80211_recalc_dtim(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata)
{
	u64 tsf = drv_get_tsf(local, sdata);
	u64 dtim_count = 0;
	u16 beacon_int = sdata->vif.bss_conf.beacon_int * 1024;
	u8 dtim_period = sdata->vif.bss_conf.dtim_period;
	struct ps_data *ps;
	u8 bcns_from_dtim;

	if (tsf == -1ULL || !beacon_int || !dtim_period)
		return;

	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		if (!sdata->bss)
			return;

		ps = &sdata->bss->ps;
	} else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		ps = &sdata->u.mesh.ps;
	} else {
		return;
	}

	/*
	 * actually finds last dtim_count, mac80211 will update in
	 * __beacon_add_tim().
	 * dtim_count = dtim_period - (tsf / bcn_int) % dtim_period
	 */
	do_div(tsf, beacon_int);
	bcns_from_dtim = do_div(tsf, dtim_period);
	/* just had a DTIM */
	if (!bcns_from_dtim)
		dtim_count = 0;
	else
		dtim_count = dtim_period - bcns_from_dtim;

	ps->dtim_count = dtim_count;
}

static u8 ieee80211_chanctx_radar_detect(struct ieee80211_local *local,
					 struct ieee80211_chanctx *ctx)
{
	struct ieee80211_sub_if_data *sdata;
	u8 radar_detect = 0;

	lockdep_assert_held(&local->chanctx_mtx);

	if (WARN_ON(ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED))
		return 0;

	list_for_each_entry(sdata, &ctx->reserved_vifs, reserved_chanctx_list)
		if (sdata->reserved_radar_required)
			radar_detect |= BIT(sdata->reserved_chandef.width);

	/*
	 * An in-place reservation context should not have any assigned vifs
	 * until it replaces the other context.
	 */
	WARN_ON(ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER &&
		!list_empty(&ctx->assigned_vifs));

	list_for_each_entry(sdata, &ctx->assigned_vifs, assigned_chanctx_list)
		if (sdata->radar_required)
			radar_detect |= BIT(sdata->vif.bss_conf.chandef.width);

	return radar_detect;
}

int ieee80211_check_combinations(struct ieee80211_sub_if_data *sdata,
				 const struct cfg80211_chan_def *chandef,
				 enum ieee80211_chanctx_mode chanmode,
				 u8 radar_detect)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *sdata_iter;
	enum nl80211_iftype iftype = sdata->wdev.iftype;
	struct ieee80211_chanctx *ctx;
	int total = 1;
	struct iface_combination_params params = {
		.radar_detect = radar_detect,
	};

	lockdep_assert_held(&local->chanctx_mtx);

	if (WARN_ON(hweight32(radar_detect) > 1))
		return -EINVAL;

	if (WARN_ON(chandef && chanmode == IEEE80211_CHANCTX_SHARED &&
		    !chandef->chan))
		return -EINVAL;

	if (WARN_ON(iftype >= NUM_NL80211_IFTYPES))
		return -EINVAL;

	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_MESH_POINT) {
		/*
		 * always passing this is harmless, since it'll be the
		 * same value that cfg80211 finds if it finds the same
		 * interface ... and that's always allowed
		 */
		params.new_beacon_int = sdata->vif.bss_conf.beacon_int;
	}

	/* Always allow software iftypes */
	if (local->hw.wiphy->software_iftypes & BIT(iftype)) {
		if (radar_detect)
			return -EINVAL;
		return 0;
	}

	if (chandef)
		params.num_different_channels = 1;

	if (iftype != NL80211_IFTYPE_UNSPECIFIED)
		params.iftype_num[iftype] = 1;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;
		params.radar_detect |=
			ieee80211_chanctx_radar_detect(local, ctx);
		if (ctx->mode == IEEE80211_CHANCTX_EXCLUSIVE) {
			params.num_different_channels++;
			continue;
		}
		if (chandef && chanmode == IEEE80211_CHANCTX_SHARED &&
		    cfg80211_chandef_compatible(chandef,
						&ctx->conf.def))
			continue;
		params.num_different_channels++;
	}

	list_for_each_entry_rcu(sdata_iter, &local->interfaces, list) {
		struct wireless_dev *wdev_iter;

		wdev_iter = &sdata_iter->wdev;

		if (sdata_iter == sdata ||
		    !ieee80211_sdata_running(sdata_iter) ||
		    local->hw.wiphy->software_iftypes & BIT(wdev_iter->iftype))
			continue;

		params.iftype_num[wdev_iter->iftype]++;
		total++;
	}

	if (total == 1 && !params.radar_detect)
		return 0;

	return cfg80211_check_combinations(local->hw.wiphy, &params);
}

static void
ieee80211_iter_max_chans(const struct ieee80211_iface_combination *c,
			 void *data)
{
	u32 *max_num_different_channels = data;

	*max_num_different_channels = max(*max_num_different_channels,
					  c->num_different_channels);
}

int ieee80211_max_num_channels(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx;
	u32 max_num_different_channels = 1;
	int err;
	struct iface_combination_params params = {0};

	lockdep_assert_held(&local->chanctx_mtx);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;

		params.num_different_channels++;

		params.radar_detect |=
			ieee80211_chanctx_radar_detect(local, ctx);
	}

	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		params.iftype_num[sdata->wdev.iftype]++;

	err = cfg80211_iter_combinations(local->hw.wiphy, &params,
					 ieee80211_iter_max_chans,
					 &max_num_different_channels);
	if (err < 0)
		return err;

	return max_num_different_channels;
}

u8 *ieee80211_add_wmm_info_ie(u8 *buf, u8 qosinfo)
{
	*buf++ = WLAN_EID_VENDOR_SPECIFIC;
	*buf++ = 7; /* len */
	*buf++ = 0x00; /* Microsoft OUI 00:50:F2 */
	*buf++ = 0x50;
	*buf++ = 0xf2;
	*buf++ = 2; /* WME */
	*buf++ = 0; /* WME info */
	*buf++ = 1; /* WME ver */
	*buf++ = qosinfo; /* U-APSD no in use */

	return buf;
}

void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
			     unsigned long *frame_cnt,
			     unsigned long *byte_cnt)
{
	struct txq_info *txqi = to_txq_info(txq);
	u32 frag_cnt = 0, frag_bytes = 0;
	struct sk_buff *skb;

	skb_queue_walk(&txqi->frags, skb) {
		frag_cnt++;
		frag_bytes += skb->len;
	}

	if (frame_cnt)
		*frame_cnt = txqi->tin.backlog_packets + frag_cnt;

	if (byte_cnt)
		*byte_cnt = txqi->tin.backlog_bytes + frag_bytes;
}
EXPORT_SYMBOL(ieee80211_txq_get_depth);

const u8 ieee80211_ac_to_qos_mask[IEEE80211_NUM_ACS] = {
	IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
	IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
	IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
	IEEE80211_WMM_IE_STA_QOSINFO_AC_BK
};
