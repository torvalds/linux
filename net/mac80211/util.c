// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
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
#include <kunit/visibility.h>

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

	local = wiphy_priv(wiphy);
	return &local->hw;
}
EXPORT_SYMBOL(wiphy_to_ieee80211_hw);

const struct ieee80211_conn_settings ieee80211_conn_settings_unlimited = {
	.mode = IEEE80211_CONN_MODE_EHT,
	.bw_limit = IEEE80211_CONN_BW_LIMIT_320,
};

u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
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

	if (ieee80211_is_s1g_beacon(fc)) {
		struct ieee80211_ext *ext = (void *) hdr;

		return ext->u.s1g_beacon.sa;
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
EXPORT_SYMBOL(ieee80211_get_bssid);

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
	int erp;
	bool short_preamble = false;

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->deflink.operating_11g_mode)
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
	int erp, bitrate;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->deflink.operating_11g_mode)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	bitrate = rate->bitrate;

	/* CTS duration */
	dur = ieee80211_frame_duration(sband->band, 10, bitrate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += ieee80211_frame_duration(sband->band, frame_len, bitrate,
					erp, short_preamble);
	/* ACK duration */
	dur += ieee80211_frame_duration(sband->band, 10, bitrate,
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
	int erp, bitrate;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];
	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->deflink.operating_11g_mode)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	bitrate = rate->bitrate;

	/* Data frame duration */
	dur = ieee80211_frame_duration(sband->band, frame_len, bitrate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TX_CTL_NO_ACK)) {
		/* ACK duration */
		dur += ieee80211_frame_duration(sband->band, 10, bitrate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}
EXPORT_SYMBOL(ieee80211_ctstoself_duration);

static void wake_tx_push_queue(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_txq *queue)
{
	struct ieee80211_tx_control control = {
		.sta = queue->sta,
	};
	struct sk_buff *skb;

	while (1) {
		skb = ieee80211_tx_dequeue(&local->hw, queue);
		if (!skb)
			break;

		drv_tx(local, &control, skb);
	}
}

/* wake_tx_queue handler for driver not implementing a custom one*/
void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
				    struct ieee80211_txq *txq)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(txq->vif);
	struct ieee80211_txq *queue;

	spin_lock(&local->handle_wake_tx_queue_lock);

	/* Use ieee80211_next_txq() for airtime fairness accounting */
	ieee80211_txq_schedule_start(hw, txq->ac);
	while ((queue = ieee80211_next_txq(hw, txq->ac))) {
		wake_tx_push_queue(local, sdata, queue);
		ieee80211_return_txq(hw, queue, false);
	}
	ieee80211_txq_schedule_end(hw, txq->ac);
	spin_unlock(&local->handle_wake_tx_queue_lock);
}
EXPORT_SYMBOL(ieee80211_handle_wake_tx_queue);

static void __ieee80211_wake_txqs(struct ieee80211_sub_if_data *sdata, int ac)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_vif *vif = &sdata->vif;
	struct fq *fq = &local->fq;
	struct ps_data *ps = NULL;
	struct txq_info *txqi;
	struct sta_info *sta;
	int i;

	local_bh_disable();
	spin_lock(&fq->lock);

	if (!test_bit(SDATA_STATE_RUNNING, &sdata->state))
		goto out;

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		ps = &sdata->bss->ps;

	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		if (sdata != sta->sdata)
			continue;

		for (i = 0; i < ARRAY_SIZE(sta->sta.txq); i++) {
			struct ieee80211_txq *txq = sta->sta.txq[i];

			if (!txq)
				continue;

			txqi = to_txq_info(txq);

			if (ac != txq->ac)
				continue;

			if (!test_and_clear_bit(IEEE80211_TXQ_DIRTY,
						&txqi->flags))
				continue;

			spin_unlock(&fq->lock);
			drv_wake_tx_queue(local, txqi);
			spin_lock(&fq->lock);
		}
	}

	if (!vif->txq)
		goto out;

	txqi = to_txq_info(vif->txq);

	if (!test_and_clear_bit(IEEE80211_TXQ_DIRTY, &txqi->flags) ||
	    (ps && atomic_read(&ps->num_sta_ps)) || ac != vif->txq->ac)
		goto out;

	spin_unlock(&fq->lock);

	drv_wake_tx_queue(local, txqi);
	local_bh_enable();
	return;
out:
	spin_unlock(&fq->lock);
	local_bh_enable();
}

static void
__releases(&local->queue_stop_reason_lock)
__acquires(&local->queue_stop_reason_lock)
_ieee80211_wake_txqs(struct ieee80211_local *local, unsigned long *flags)
{
	struct ieee80211_sub_if_data *sdata;
	int n_acs = IEEE80211_NUM_ACS;
	int i;

	rcu_read_lock();

	if (local->hw.queues < IEEE80211_NUM_ACS)
		n_acs = 1;

	for (i = 0; i < local->hw.queues; i++) {
		if (local->queue_stop_reasons[i])
			continue;

		spin_unlock_irqrestore(&local->queue_stop_reason_lock, *flags);
		list_for_each_entry_rcu(sdata, &local->interfaces, list) {
			int ac;

			for (ac = 0; ac < n_acs; ac++) {
				int ac_queue = sdata->vif.hw_queue[ac];

				if (ac_queue == i ||
				    sdata->vif.cab_queue == i)
					__ieee80211_wake_txqs(sdata, ac);
			}
		}
		spin_lock_irqsave(&local->queue_stop_reason_lock, *flags);
	}

	rcu_read_unlock();
}

void ieee80211_wake_txqs(struct tasklet_struct *t)
{
	struct ieee80211_local *local = from_tasklet(local, t,
						     wake_txqs_tasklet);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	_ieee80211_wake_txqs(local, &flags);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

static void __ieee80211_wake_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason,
				   bool refcounted,
				   unsigned long *flags)
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

	if (!skb_queue_empty(&local->pending[queue]))
		tasklet_schedule(&local->tx_pending_tasklet);

	/*
	 * Calling _ieee80211_wake_txqs here can be a problem because it may
	 * release queue_stop_reason_lock which has been taken by
	 * __ieee80211_wake_queue's caller. It is certainly not very nice to
	 * release someone's lock, but it is fine because all the callers of
	 * __ieee80211_wake_queue call it right before releasing the lock.
	 */
	if (reason == IEEE80211_QUEUE_STOP_REASON_DRIVER)
		tasklet_schedule(&local->wake_txqs_tasklet);
	else
		_ieee80211_wake_txqs(local, flags);
}

void ieee80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason,
				    bool refcounted)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_wake_queue(hw, queue, reason, refcounted, &flags);
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

	trace_stop_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!refcounted)
		local->q_stop_reasons[queue][reason] = 1;
	else
		local->q_stop_reasons[queue][reason]++;

	set_bit(reason, &local->queue_stop_reasons[queue]);
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
			       false, &flags);
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
			false, &flags);
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
		__ieee80211_wake_queue(hw, i, reason, refcounted, &flags);

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

	if (drop) {
		struct sta_info *sta;

		/* Purge the queues, so the frames on them won't be
		 * sent during __ieee80211_wake_queue()
		 */
		list_for_each_entry(sta, &local->sta_list, list) {
			if (sdata != sta->sdata)
				continue;
			ieee80211_purge_sta_txqs(sta);
		}
	}

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

	list_for_each_entry_rcu(sdata, &local->interfaces, list,
				lockdep_is_held(&local->iflist_mtx) ||
				lockdep_is_held(&local->hw.wiphy->mtx)) {
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
		if ((iter_flags & IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER) &&
		    !(sdata->flags & IEEE80211_SDATA_IN_DRIVER))
			continue;
		if (ieee80211_sdata_running(sdata) || !active_only)
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	sdata = rcu_dereference_check(local->monitor_sdata,
				      lockdep_is_held(&local->iflist_mtx) ||
				      lockdep_is_held(&local->hw.wiphy->mtx));
	if (sdata && ieee80211_hw_check(&local->hw, WANT_MONITOR_VIF) &&
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

void ieee80211_iterate_active_interfaces_mtx(
	struct ieee80211_hw *hw, u32 iter_flags,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);

	lockdep_assert_wiphy(hw->wiphy);

	__iterate_interfaces(local, iter_flags | IEEE80211_IFACE_ITER_ACTIVE,
			     iterator, data);
}
EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_mtx);

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
	if (!vif)
		return NULL;

	return &vif_to_sdata(vif)->wdev;
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

void ieee80211_regulatory_limit_wmm_params(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_tx_queue_params
					   *qparam, int ac)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	const struct ieee80211_reg_rule *rrule;
	const struct ieee80211_wmm_ac *wmm_ac;
	u16 center_freq = 0;

	if (sdata->vif.type != NL80211_IFTYPE_AP &&
	    sdata->vif.type != NL80211_IFTYPE_STATION)
		return;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.bss_conf.chanctx_conf);
	if (chanctx_conf)
		center_freq = chanctx_conf->def.chan->center_freq;

	if (!center_freq) {
		rcu_read_unlock();
		return;
	}

	rrule = freq_reg_info(sdata->wdev.wiphy, MHZ_TO_KHZ(center_freq));

	if (IS_ERR_OR_NULL(rrule) || !rrule->has_wmm) {
		rcu_read_unlock();
		return;
	}

	if (sdata->vif.type == NL80211_IFTYPE_AP)
		wmm_ac = &rrule->wmm_rule.ap[ac];
	else
		wmm_ac = &rrule->wmm_rule.client[ac];
	qparam->cw_min = max_t(u16, qparam->cw_min, wmm_ac->cw_min);
	qparam->cw_max = max_t(u16, qparam->cw_max, wmm_ac->cw_max);
	qparam->aifs = max_t(u8, qparam->aifs, wmm_ac->aifsn);
	qparam->txop = min_t(u16, qparam->txop, wmm_ac->cot / 32);
	rcu_read_unlock();
}

void ieee80211_set_wmm_default(struct ieee80211_link_data *link,
			       bool bss_notify, bool enable_qos)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
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
	chanctx_conf = rcu_dereference(link->conf->chanctx_conf);
	use_11b = (chanctx_conf &&
		   chanctx_conf->def.chan->band == NL80211_BAND_2GHZ) &&
		 !link->operating_11g_mode;
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

		link->tx_conf[ac] = qparam;
		drv_conf_tx(local, link, ac, &qparam);
	}

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE &&
	    sdata->vif.type != NL80211_IFTYPE_NAN) {
		link->conf->qos = enable_qos;
		if (bss_notify)
			ieee80211_link_info_change_notify(sdata, link,
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
	bool multi_link = ieee80211_vif_is_mld(&sdata->vif);
	struct {
		u8 id;
		u8 len;
		u8 ext_id;
		struct ieee80211_multi_link_elem ml;
		struct ieee80211_mle_basic_common_info basic;
	} __packed mle = {
		.id = WLAN_EID_EXTENSION,
		.len = sizeof(mle) - 2,
		.ext_id = WLAN_EID_EXT_EHT_MULTI_LINK,
		.ml.control = cpu_to_le16(IEEE80211_ML_CONTROL_TYPE_BASIC),
		.basic.len = sizeof(mle.basic),
	};
	int err;

	memcpy(mle.basic.mld_mac_addr, sdata->vif.addr, ETH_ALEN);

	/* 24 + 6 = header + auth_algo + auth_transaction + status_code */
	skb = dev_alloc_skb(local->hw.extra_tx_headroom + IEEE80211_WEP_IV_LEN +
			    24 + 6 + extra_len + IEEE80211_WEP_ICV_LEN +
			    multi_link * sizeof(mle));
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
	if (multi_link)
		skb_put_data(skb, &mle, sizeof(mle));

	if (auth_alg == WLAN_AUTH_SHARED_KEY && transaction == 3) {
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		err = ieee80211_wep_encrypt(local, skb, key, key_len, key_idx);
		if (WARN_ON(err)) {
			kfree_skb(skb);
			return;
		}
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					tx_flags;
	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
				    const u8 *da, const u8 *bssid,
				    u16 stype, u16 reason,
				    bool send_frame, u8 *frame_buf)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt = (void *)frame_buf;

	/* build frame */
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	mgmt->duration = 0; /* initialize only */
	mgmt->seq_ctrl = 0; /* initialize only */
	memcpy(mgmt->da, da, ETH_ALEN);
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

static int ieee80211_put_s1g_cap(struct sk_buff *skb,
				 struct ieee80211_sta_s1g_cap *s1g_cap)
{
	if (skb_tailroom(skb) < 2 + sizeof(struct ieee80211_s1g_cap))
		return -ENOBUFS;

	skb_put_u8(skb, WLAN_EID_S1G_CAPABILITIES);
	skb_put_u8(skb, sizeof(struct ieee80211_s1g_cap));

	skb_put_data(skb, &s1g_cap->cap, sizeof(s1g_cap->cap));
	skb_put_data(skb, &s1g_cap->nss_mcs, sizeof(s1g_cap->nss_mcs));

	return 0;
}

static int ieee80211_put_preq_ies_band(struct sk_buff *skb,
				       struct ieee80211_sub_if_data *sdata,
				       const u8 *ie, size_t ie_len,
				       size_t *offset,
				       enum nl80211_band band,
				       u32 rate_mask,
				       struct cfg80211_chan_def *chandef,
				       u32 flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	int i, err;
	size_t noffset;
	u32 rate_flags;
	bool have_80mhz = false;

	*offset = 0;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON_ONCE(!sband))
		return 0;

	rate_flags = ieee80211_chandef_rate_flags(chandef);

	/* For direct scan add S1G IE and consider its override bits */
	if (band == NL80211_BAND_S1GHZ)
		return ieee80211_put_s1g_cap(skb, &sband->s1g_cap);

	err = ieee80211_put_srates_elem(skb, sband, 0, rate_flags,
					~rate_mask, WLAN_EID_SUPP_RATES);
	if (err)
		return err;

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
		if (skb_tailroom(skb) < noffset - *offset)
			return -ENOBUFS;
		skb_put_data(skb, ie + *offset, noffset - *offset);
		*offset = noffset;
	}

	err = ieee80211_put_srates_elem(skb, sband, 0, rate_flags,
					~rate_mask, WLAN_EID_EXT_SUPP_RATES);
	if (err)
		return err;

	if (chandef->chan && sband->band == NL80211_BAND_2GHZ) {
		if (skb_tailroom(skb) < 3)
			return -ENOBUFS;
		skb_put_u8(skb, WLAN_EID_DS_PARAMS);
		skb_put_u8(skb, 1);
		skb_put_u8(skb,
			   ieee80211_frequency_to_channel(chandef->chan->center_freq));
	}

	if (flags & IEEE80211_PROBE_FLAG_MIN_CONTENT)
		return 0;

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
		if (skb_tailroom(skb) < noffset - *offset)
			return -ENOBUFS;
		skb_put_data(skb, ie + *offset, noffset - *offset);
		*offset = noffset;
	}

	if (sband->ht_cap.ht_supported) {
		u8 *pos;

		if (skb_tailroom(skb) < 2 + sizeof(struct ieee80211_ht_cap))
			return -ENOBUFS;

		pos = skb_put(skb, 2 + sizeof(struct ieee80211_ht_cap));
		ieee80211_ie_build_ht_cap(pos, &sband->ht_cap,
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
		if (skb_tailroom(skb) < noffset - *offset)
			return -ENOBUFS;
		skb_put_data(skb, ie + *offset, noffset - *offset);
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
		u8 *pos;

		if (skb_tailroom(skb) < 2 + sizeof(struct ieee80211_vht_cap))
			return -ENOBUFS;

		pos = skb_put(skb, 2 + sizeof(struct ieee80211_vht_cap));
		ieee80211_ie_build_vht_cap(pos, &sband->vht_cap,
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
		if (skb_tailroom(skb) < noffset - *offset)
			return -ENOBUFS;
		skb_put_data(skb, ie + *offset, noffset - *offset);
		*offset = noffset;
	}

	if (cfg80211_any_usable_channels(local->hw.wiphy, BIT(sband->band),
					 IEEE80211_CHAN_NO_HE)) {
		err = ieee80211_put_he_cap(skb, sdata, sband, NULL);
		if (err)
			return err;
	}

	if (cfg80211_any_usable_channels(local->hw.wiphy, BIT(sband->band),
					 IEEE80211_CHAN_NO_HE |
					 IEEE80211_CHAN_NO_EHT)) {
		err = ieee80211_put_eht_cap(skb, sdata, sband, NULL);
		if (err)
			return err;
	}

	err = ieee80211_put_he_6ghz_cap(skb, sdata, IEEE80211_SMPS_OFF);
	if (err)
		return err;

	/*
	 * If adding more here, adjust code in main.c
	 * that calculates local->scan_ies_len.
	 */

	return 0;
}

static int ieee80211_put_preq_ies(struct sk_buff *skb,
				  struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_scan_ies *ie_desc,
				  const u8 *ie, size_t ie_len,
				  u8 bands_used, u32 *rate_masks,
				  struct cfg80211_chan_def *chandef,
				  u32 flags)
{
	size_t custom_ie_offset = 0;
	int i, err;

	memset(ie_desc, 0, sizeof(*ie_desc));

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		if (bands_used & BIT(i)) {
			ie_desc->ies[i] = skb_tail_pointer(skb);
			err = ieee80211_put_preq_ies_band(skb, sdata,
							  ie, ie_len,
							  &custom_ie_offset,
							  i, rate_masks[i],
							  chandef, flags);
			if (err)
				return err;
			ie_desc->len[i] = skb_tail_pointer(skb) -
					  ie_desc->ies[i];
		}
	}

	/* add any remaining custom IEs */
	if (ie && ie_len) {
		if (WARN_ONCE(skb_tailroom(skb) < ie_len - custom_ie_offset,
			      "not enough space for preq custom IEs\n"))
			return -ENOBUFS;
		ie_desc->common_ies = skb_tail_pointer(skb);
		skb_put_data(skb, ie + custom_ie_offset,
			     ie_len - custom_ie_offset);
		ie_desc->common_ie_len = skb_tail_pointer(skb) -
					 ie_desc->common_ies;
	}

	return 0;
};

int ieee80211_build_preq_ies(struct ieee80211_sub_if_data *sdata, u8 *buffer,
			     size_t buffer_len,
			     struct ieee80211_scan_ies *ie_desc,
			     const u8 *ie, size_t ie_len,
			     u8 bands_used, u32 *rate_masks,
			     struct cfg80211_chan_def *chandef,
			     u32 flags)
{
	struct sk_buff *skb = alloc_skb(buffer_len, GFP_KERNEL);
	uintptr_t offs;
	int ret, i;
	u8 *start;

	if (!skb)
		return -ENOMEM;

	start = skb_tail_pointer(skb);
	memset(start, 0, skb_tailroom(skb));
	ret = ieee80211_put_preq_ies(skb, sdata, ie_desc, ie, ie_len,
				     bands_used, rate_masks, chandef,
				     flags);
	if (ret < 0) {
		goto out;
	}

	if (skb->len > buffer_len) {
		ret = -ENOBUFS;
		goto out;
	}

	memcpy(buffer, start, skb->len);

	/* adjust ie_desc for copy */
	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		offs = ie_desc->ies[i] - start;
		ie_desc->ies[i] = buffer + offs;
	}
	offs = ie_desc->common_ies - start;
	ie_desc->common_ies = buffer + offs;

	ret = skb->len;
out:
	consume_skb(skb);
	return ret;
}

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
	u32 rate_masks[NUM_NL80211_BANDS] = {};
	struct ieee80211_scan_ies dummy_ie_desc;

	/*
	 * Do not send DS Channel parameter for directed probe requests
	 * in order to maximize the chance that we get a response.  Some
	 * badly-behaved APs don't respond when this parameter is included.
	 */
	chandef.width = sdata->vif.bss_conf.chanreq.oper.width;
	if (flags & IEEE80211_PROBE_FLAG_DIRECTED)
		chandef.chan = NULL;
	else
		chandef.chan = chan;

	skb = ieee80211_probereq_get(&local->hw, src, ssid, ssid_len,
				     local->scan_ies_len + ie_len);
	if (!skb)
		return NULL;

	rate_masks[chan->band] = ratemask;
	ieee80211_put_preq_ies(skb, sdata, &dummy_ie_desc,
			       ie, ie_len, BIT(chan->band),
			       rate_masks, &chandef, flags);

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
	int i, j;

	sband = sdata->local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return 1;

	rate_flags =
		ieee80211_chandef_rate_flags(&sdata->vif.bss_conf.chanreq.oper);

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

			brate = sband->bitrates[j].bitrate;

			if (brate == own_rate) {
				supp_rates |= BIT(j);
				if (basic_rates && is_basic)
					*basic_rates |= BIT(j);
			}
		}
	}
	return supp_rates;
}

void ieee80211_stop_device(struct ieee80211_local *local, bool suspend)
{
	local_bh_disable();
	ieee80211_handle_queued_frames(local);
	local_bh_enable();

	ieee80211_led_radio(local, false);
	ieee80211_mod_tpt_led_trig(local, 0, IEEE80211_TPT_LEDTRIG_FL_RADIO);

	wiphy_work_cancel(local->hw.wiphy, &local->reconfig_filter);

	flush_workqueue(local->workqueue);
	wiphy_work_flush(local->hw.wiphy, NULL);
	drv_stop(local, suspend);
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
		wiphy_delayed_work_queue(local->hw.wiphy, &local->scan_work, 0);
		wiphy_delayed_work_flush(local->hw.wiphy, &local->scan_work);
	}
}

static void ieee80211_handle_reconfig_failure(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(local->hw.wiphy);

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
	local->reconfig_failure = true;

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
	list_for_each_entry(ctx, &local->chanctx_list, list)
		ctx->driver_present = false;
}

static void ieee80211_assign_chanctx(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_link_data *link)
{
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;

	lockdep_assert_wiphy(local->hw.wiphy);

	conf = rcu_dereference_protected(link->conf->chanctx_conf,
					 lockdep_is_held(&local->hw.wiphy->mtx));
	if (conf) {
		ctx = container_of(conf, struct ieee80211_chanctx, conf);
		drv_assign_vif_chanctx(local, sdata, link->conf, ctx);
	}
}

static void ieee80211_reconfig_stations(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* add STAs back */
	list_for_each_entry(sta, &local->sta_list, list) {
		enum ieee80211_sta_state state;

		if (!sta->uploaded || sta->sdata != sdata)
			continue;

		for (state = IEEE80211_STA_NOTEXIST;
		     state < sta->sta_state; state++)
			WARN_ON(drv_sta_state(local, sta->sdata, sta, state,
					      state + 1));
	}
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

static void ieee80211_reconfig_ap_links(struct ieee80211_local *local,
					struct ieee80211_sub_if_data *sdata,
					u64 changed)
{
	int link_id;

	for (link_id = 0; link_id < ARRAY_SIZE(sdata->link); link_id++) {
		struct ieee80211_link_data *link;

		if (!(sdata->vif.active_links & BIT(link_id)))
			continue;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (!link)
			continue;

		if (rcu_access_pointer(link->u.ap.beacon))
			drv_start_ap(local, sdata, link->conf);

		if (!link->conf->enable_beacon)
			continue;

		changed |= BSS_CHANGED_BEACON |
			   BSS_CHANGED_BEACON_ENABLED;

		ieee80211_link_info_change_notify(sdata, link, changed);
	}
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
	bool in_reconfig = false;

	lockdep_assert_wiphy(local->hw.wiphy);

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
	sdata = wiphy_dereference(local->hw.wiphy, local->monitor_sdata);
	if (sdata && ieee80211_hw_check(&local->hw, WANT_MONITOR_VIF)) {
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
	list_for_each_entry(ctx, &local->chanctx_list, list)
		if (ctx->replace_state != IEEE80211_CHANCTX_REPLACES_OTHER)
			WARN_ON(drv_add_chanctx(local, ctx));

	sdata = wiphy_dereference(local->hw.wiphy, local->monitor_sdata);
	if (sdata && ieee80211_sdata_running(sdata))
		ieee80211_assign_chanctx(local, sdata, &sdata->deflink);

	/* reconfigure hardware */
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_LISTEN_INTERVAL |
				   IEEE80211_CONF_CHANGE_MONITOR |
				   IEEE80211_CONF_CHANGE_PS |
				   IEEE80211_CONF_CHANGE_RETRY_LIMITS |
				   IEEE80211_CONF_CHANGE_IDLE);

	ieee80211_configure_filter(local);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		/* common change flags for all interface types - link only */
		u64 changed = BSS_CHANGED_ERP_CTS_PROT |
			      BSS_CHANGED_ERP_PREAMBLE |
			      BSS_CHANGED_ERP_SLOT |
			      BSS_CHANGED_HT |
			      BSS_CHANGED_BASIC_RATES |
			      BSS_CHANGED_BEACON_INT |
			      BSS_CHANGED_BSSID |
			      BSS_CHANGED_CQM |
			      BSS_CHANGED_QOS |
			      BSS_CHANGED_TXPOWER |
			      BSS_CHANGED_MCAST_RATE;
		struct ieee80211_link_data *link = NULL;
		unsigned int link_id;
		u32 active_links = 0;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (ieee80211_vif_is_mld(&sdata->vif)) {
			struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS] = {
				[0] = &sdata->vif.bss_conf,
			};

			if (sdata->vif.type == NL80211_IFTYPE_STATION) {
				/* start with a single active link */
				active_links = sdata->vif.active_links;
				link_id = ffs(active_links) - 1;
				sdata->vif.active_links = BIT(link_id);
			}

			drv_change_vif_links(local, sdata, 0,
					     sdata->vif.active_links,
					     old);
		}

		sdata->restart_active_links = active_links;

		for (link_id = 0;
		     link_id < ARRAY_SIZE(sdata->vif.link_conf);
		     link_id++) {
			if (!ieee80211_vif_link_active(&sdata->vif, link_id))
				continue;

			link = sdata_dereference(sdata->link[link_id], sdata);
			if (!link)
				continue;

			ieee80211_assign_chanctx(local, sdata, link);
		}

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			break;
		case NL80211_IFTYPE_ADHOC:
			if (sdata->vif.cfg.ibss_joined)
				WARN_ON(drv_join_ibss(local, sdata));
			fallthrough;
		default:
			ieee80211_reconfig_stations(sdata);
			fallthrough;
		case NL80211_IFTYPE_AP: /* AP stations are handled later */
			for (i = 0; i < IEEE80211_NUM_ACS; i++)
				drv_conf_tx(local, &sdata->deflink, i,
					    &sdata->deflink.tx_conf[i]);
			break;
		}

		if (sdata->vif.bss_conf.mu_mimo_owner)
			changed |= BSS_CHANGED_MU_GROUPS;

		if (!ieee80211_vif_is_mld(&sdata->vif))
			changed |= BSS_CHANGED_IDLE;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			if (!ieee80211_vif_is_mld(&sdata->vif)) {
				changed |= BSS_CHANGED_ASSOC |
					   BSS_CHANGED_ARP_FILTER |
					   BSS_CHANGED_PS;

				/* Re-send beacon info report to the driver */
				if (sdata->deflink.u.mgd.have_beacon)
					changed |= BSS_CHANGED_BEACON_INFO;

				if (sdata->vif.bss_conf.max_idle_period ||
				    sdata->vif.bss_conf.protected_keep_alive)
					changed |= BSS_CHANGED_KEEP_ALIVE;

				ieee80211_bss_info_change_notify(sdata,
								 changed);
			} else if (!WARN_ON(!link)) {
				ieee80211_link_info_change_notify(sdata, link,
								  changed);
				changed = BSS_CHANGED_ASSOC |
					  BSS_CHANGED_IDLE |
					  BSS_CHANGED_PS |
					  BSS_CHANGED_ARP_FILTER;
				ieee80211_vif_cfg_change_notify(sdata, changed);
			}
			break;
		case NL80211_IFTYPE_OCB:
			changed |= BSS_CHANGED_OCB;
			ieee80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_ADHOC:
			changed |= BSS_CHANGED_IBSS;
			fallthrough;
		case NL80211_IFTYPE_AP:
			changed |= BSS_CHANGED_P2P_PS;

			if (ieee80211_vif_is_mld(&sdata->vif))
				ieee80211_vif_cfg_change_notify(sdata,
								BSS_CHANGED_SSID);
			else
				changed |= BSS_CHANGED_SSID;

			if (sdata->vif.bss_conf.ftm_responder == 1 &&
			    wiphy_ext_feature_isset(sdata->local->hw.wiphy,
					NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER))
				changed |= BSS_CHANGED_FTM_RESPONDER;

			if (sdata->vif.type == NL80211_IFTYPE_AP) {
				changed |= BSS_CHANGED_AP_PROBE_RESP;

				if (ieee80211_vif_is_mld(&sdata->vif)) {
					ieee80211_reconfig_ap_links(local,
								    sdata,
								    changed);
					break;
				}

				if (rcu_access_pointer(sdata->deflink.u.ap.beacon))
					drv_start_ap(local, sdata,
						     sdata->deflink.conf);
			}
			fallthrough;
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
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_P2P_DEVICE:
			/* nothing to do */
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_WDS:
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
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_AP:
			ieee80211_reconfig_stations(sdata);
			break;
		default:
			break;
		}
	}

	/* add back keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		ieee80211_reenable_keys(sdata);

	/* re-enable multi-link for client interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->restart_active_links)
			ieee80211_set_active_links(&sdata->vif,
						   sdata->restart_active_links);
		/*
		 * If a link switch was scheduled before the restart, and ran
		 * before reconfig, it will do nothing, so re-schedule.
		 */
		if (sdata->desired_active_links)
			wiphy_work_queue(sdata->local->hw.wiphy,
					 &sdata->activate_links_work);
	}

	/* Reconfigure sched scan if it was interrupted by FW restart */
	sched_scan_sdata = rcu_dereference_protected(local->sched_scan_sdata,
						lockdep_is_held(&local->hw.wiphy->mtx));
	sched_scan_req = rcu_dereference_protected(local->sched_scan_req,
						lockdep_is_held(&local->hw.wiphy->mtx));
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

	if (sched_scan_stopped)
		cfg80211_sched_scan_stopped_locked(local->hw.wiphy, 0);

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
		list_for_each_entry(sta, &local->sta_list, list) {
			if (!local->resuming)
				ieee80211_sta_tear_down_BA_sessions(
						sta, AGG_STOP_LOCAL_REQUEST);
			clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
		}
	}

	/*
	 * If this is for hw restart things are still running.
	 * We may want to change that later, however.
	 */
	if (local->open_count && (!suspended || reconfig_due_to_wowlan))
		drv_reconfig_complete(local, IEEE80211_RECONFIG_TYPE_RESTART);

	if (local->in_reconfig) {
		in_reconfig = local->in_reconfig;
		local->in_reconfig = false;
		barrier();

		ieee80211_reconfig_roc(local);

		/* Requeue all works */
		list_for_each_entry(sdata, &local->interfaces, list)
			wiphy_work_queue(local->hw.wiphy, &sdata->work);
	}

	ieee80211_wake_queues_by_reason(hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_SUSPEND,
					false);

	if (in_reconfig) {
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (!ieee80211_sdata_running(sdata))
				continue;
			if (sdata->vif.type == NL80211_IFTYPE_STATION)
				ieee80211_sta_restart(sdata);
		}
	}

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

static void ieee80211_reconfig_disconnect(struct ieee80211_vif *vif, u8 flag)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_key *key;

	if (WARN_ON(!vif))
		return;

	sdata = vif_to_sdata(vif);
	local = sdata->local;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(flag & IEEE80211_SDATA_DISCONNECT_RESUME &&
		    !local->resuming))
		return;

	if (WARN_ON(flag & IEEE80211_SDATA_DISCONNECT_HW_RESTART &&
		    !local->in_reconfig))
		return;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return;

	sdata->flags |= flag;

	list_for_each_entry(key, &sdata->key_list, list)
		key->flags |= KEY_FLAG_TAINTED;
}

void ieee80211_hw_restart_disconnect(struct ieee80211_vif *vif)
{
	ieee80211_reconfig_disconnect(vif, IEEE80211_SDATA_DISCONNECT_HW_RESTART);
}
EXPORT_SYMBOL_GPL(ieee80211_hw_restart_disconnect);

void ieee80211_resume_disconnect(struct ieee80211_vif *vif)
{
	ieee80211_reconfig_disconnect(vif, IEEE80211_SDATA_DISCONNECT_RESUME);
}
EXPORT_SYMBOL_GPL(ieee80211_resume_disconnect);

void ieee80211_recalc_smps(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_link_data *link)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_chanctx *chanctx;

	lockdep_assert_wiphy(local->hw.wiphy);

	chanctx_conf = rcu_dereference_protected(link->conf->chanctx_conf,
						 lockdep_is_held(&local->hw.wiphy->mtx));

	/*
	 * This function can be called from a work, thus it may be possible
	 * that the chanctx_conf is removed (due to a disconnection, for
	 * example).
	 * So nothing should be done in such case.
	 */
	if (!chanctx_conf)
		return;

	chanctx = container_of(chanctx_conf, struct ieee80211_chanctx, conf);
	ieee80211_recalc_smps_chanctx(local, chanctx);
}

void ieee80211_recalc_min_chandef(struct ieee80211_sub_if_data *sdata,
				  int link_id)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_chanctx *chanctx;
	int i;

	lockdep_assert_wiphy(local->hw.wiphy);

	for (i = 0; i < ARRAY_SIZE(sdata->vif.link_conf); i++) {
		struct ieee80211_bss_conf *bss_conf;

		if (link_id >= 0 && link_id != i)
			continue;

		rcu_read_lock();
		bss_conf = rcu_dereference(sdata->vif.link_conf[i]);
		if (!bss_conf) {
			rcu_read_unlock();
			continue;
		}

		chanctx_conf = rcu_dereference_protected(bss_conf->chanctx_conf,
							 lockdep_is_held(&local->hw.wiphy->mtx));
		/*
		 * Since we hold the wiphy mutex (checked above)
		 * we can take the chanctx_conf pointer out of the
		 * RCU critical section, it cannot go away without
		 * the mutex. Just the way we reached it could - in
		 * theory - go away, but we don't really care and
		 * it really shouldn't happen anyway.
		 */
		rcu_read_unlock();

		if (!chanctx_conf)
			return;

		chanctx = container_of(chanctx_conf, struct ieee80211_chanctx,
				       conf);
		ieee80211_recalc_chanctx_min_def(local, chanctx, NULL, false);
	}
}

size_t ieee80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ies[pos] != WLAN_EID_VENDOR_SPECIFIC)
		pos += 2 + ies[pos + 1];

	return pos;
}

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

/* this may return more than ieee80211_put_he_6ghz_cap() will need */
u8 ieee80211_ie_len_he_cap(struct ieee80211_sub_if_data *sdata)
{
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_supported_band *sband;
	u8 n;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return 0;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, &sdata->vif);
	if (!he_cap)
		return 0;

	n = ieee80211_he_mcs_nss_size(&he_cap->he_cap_elem);
	return 2 + 1 +
	       sizeof(he_cap->he_cap_elem) + n +
	       ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				     he_cap->he_cap_elem.phy_cap_info);
}

static void
ieee80211_get_adjusted_he_cap(const struct ieee80211_conn_settings *conn,
			      const struct ieee80211_sta_he_cap *he_cap,
			      struct ieee80211_he_cap_elem *elem)
{
	u8 ru_limit, max_ru;

	*elem = he_cap->he_cap_elem;

	switch (conn->bw_limit) {
	case IEEE80211_CONN_BW_LIMIT_20:
		ru_limit = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242;
		break;
	case IEEE80211_CONN_BW_LIMIT_40:
		ru_limit = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
		break;
	case IEEE80211_CONN_BW_LIMIT_80:
		ru_limit = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
		break;
	default:
		ru_limit = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996;
		break;
	}

	max_ru = elem->phy_cap_info[8] & IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK;
	max_ru = min(max_ru, ru_limit);
	elem->phy_cap_info[8] &= ~IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK;
	elem->phy_cap_info[8] |= max_ru;

	if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_40) {
		elem->phy_cap_info[0] &=
			~(IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
			  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G);
		elem->phy_cap_info[9] &=
			~IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM;
	}

	if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_160) {
		elem->phy_cap_info[0] &=
			~(IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
			  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G);
		elem->phy_cap_info[5] &=
			~IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK;
		elem->phy_cap_info[7] &=
			~(IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ |
			  IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ);
	}
}

int ieee80211_put_he_cap(struct sk_buff *skb,
			 struct ieee80211_sub_if_data *sdata,
			 const struct ieee80211_supported_band *sband,
			 const struct ieee80211_conn_settings *conn)
{
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_he_cap_elem elem;
	u8 *len;
	u8 n;
	u8 ie_len;

	if (!conn)
		conn = &ieee80211_conn_settings_unlimited;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, &sdata->vif);
	if (!he_cap)
		return 0;

	/* modify on stack first to calculate 'n' and 'ie_len' correctly */
	ieee80211_get_adjusted_he_cap(conn, he_cap, &elem);

	n = ieee80211_he_mcs_nss_size(&elem);
	ie_len = 2 + 1 +
		 sizeof(he_cap->he_cap_elem) + n +
		 ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				       he_cap->he_cap_elem.phy_cap_info);

	if (skb_tailroom(skb) < ie_len)
		return -ENOBUFS;

	skb_put_u8(skb, WLAN_EID_EXTENSION);
	len = skb_put(skb, 1); /* We'll set the size later below */
	skb_put_u8(skb, WLAN_EID_EXT_HE_CAPABILITY);

	/* Fixed data */
	skb_put_data(skb, &elem, sizeof(elem));

	skb_put_data(skb, &he_cap->he_mcs_nss_supp, n);

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
	skb_put_data(skb, &he_cap->ppe_thres, n);

end:
	*len = skb_tail_pointer(skb) - len - 1;
	return 0;
}

int ieee80211_put_he_6ghz_cap(struct sk_buff *skb,
			      struct ieee80211_sub_if_data *sdata,
			      enum ieee80211_smps_mode smps_mode)
{
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sband_iftype_data *iftd;
	enum nl80211_iftype iftype = ieee80211_vif_type_p2p(&sdata->vif);
	__le16 cap;

	if (!cfg80211_any_usable_channels(sdata->local->hw.wiphy,
					  BIT(NL80211_BAND_6GHZ),
					  IEEE80211_CHAN_NO_HE))
		return 0;

	sband = sdata->local->hw.wiphy->bands[NL80211_BAND_6GHZ];

	iftd = ieee80211_get_sband_iftype_data(sband, iftype);
	if (!iftd)
		return 0;

	/* Check for device HE 6 GHz capability before adding element */
	if (!iftd->he_6ghz_capa.capa)
		return 0;

	cap = iftd->he_6ghz_capa.capa;
	cap &= cpu_to_le16(~IEEE80211_HE_6GHZ_CAP_SM_PS);

	switch (smps_mode) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
		fallthrough;
	case IEEE80211_SMPS_OFF:
		cap |= le16_encode_bits(WLAN_HT_CAP_SM_PS_DISABLED,
					IEEE80211_HE_6GHZ_CAP_SM_PS);
		break;
	case IEEE80211_SMPS_STATIC:
		cap |= le16_encode_bits(WLAN_HT_CAP_SM_PS_STATIC,
					IEEE80211_HE_6GHZ_CAP_SM_PS);
		break;
	case IEEE80211_SMPS_DYNAMIC:
		cap |= le16_encode_bits(WLAN_HT_CAP_SM_PS_DYNAMIC,
					IEEE80211_HE_6GHZ_CAP_SM_PS);
		break;
	}

	if (skb_tailroom(skb) < 2 + 1 + sizeof(cap))
		return -ENOBUFS;

	skb_put_u8(skb, WLAN_EID_EXTENSION);
	skb_put_u8(skb, 1 + sizeof(cap));
	skb_put_u8(skb, WLAN_EID_EXT_HE_6GHZ_CAPA);
	skb_put_data(skb, &cap, sizeof(cap));
	return 0;
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
	case NL80211_CHAN_WIDTH_320:
		/* HT information element should not be included on 6GHz */
		WARN_ON(1);
		return pos;
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
	case NL80211_CHAN_WIDTH_320:
		/* The behavior is not defined for 320 MHz channels */
		WARN_ON(1);
		fallthrough;
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
	case NL80211_CHAN_WIDTH_320:
		/* VHT information element should not be included on 6GHz */
		WARN_ON(1);
		return pos;
	default:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_USE_HT;
		break;
	}

	/* don't require special VHT peer rates */
	vht_oper->basic_mcs_set = cpu_to_le16(0xffff);

	return pos + sizeof(struct ieee80211_vht_operation);
}

u8 *ieee80211_ie_build_he_oper(u8 *pos, struct cfg80211_chan_def *chandef)
{
	struct ieee80211_he_operation *he_oper;
	struct ieee80211_he_6ghz_oper *he_6ghz_op;
	u32 he_oper_params;
	u8 ie_len = 1 + sizeof(struct ieee80211_he_operation);

	if (chandef->chan->band == NL80211_BAND_6GHZ)
		ie_len += sizeof(struct ieee80211_he_6ghz_oper);

	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = ie_len;
	*pos++ = WLAN_EID_EXT_HE_OPERATION;

	he_oper_params = 0;
	he_oper_params |= u32_encode_bits(1023, /* disabled */
				IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK);
	he_oper_params |= u32_encode_bits(1,
				IEEE80211_HE_OPERATION_ER_SU_DISABLE);
	he_oper_params |= u32_encode_bits(1,
				IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED);
	if (chandef->chan->band == NL80211_BAND_6GHZ)
		he_oper_params |= u32_encode_bits(1,
				IEEE80211_HE_OPERATION_6GHZ_OP_INFO);

	he_oper = (struct ieee80211_he_operation *)pos;
	he_oper->he_oper_params = cpu_to_le32(he_oper_params);

	/* don't require special HE peer rates */
	he_oper->he_mcs_nss_set = cpu_to_le16(0xffff);
	pos += sizeof(struct ieee80211_he_operation);

	if (chandef->chan->band != NL80211_BAND_6GHZ)
		goto out;

	/* TODO add VHT operational */
	he_6ghz_op = (struct ieee80211_he_6ghz_oper *)pos;
	he_6ghz_op->minrate = 6; /* 6 Mbps */
	he_6ghz_op->primary =
		ieee80211_frequency_to_channel(chandef->chan->center_freq);
	he_6ghz_op->ccfs0 =
		ieee80211_frequency_to_channel(chandef->center_freq1);
	if (chandef->center_freq2)
		he_6ghz_op->ccfs1 =
			ieee80211_frequency_to_channel(chandef->center_freq2);
	else
		he_6ghz_op->ccfs1 = 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_320:
		/*
		 * TODO: mesh operation is not defined over 6GHz 320 MHz
		 * channels.
		 */
		WARN_ON(1);
		break;
	case NL80211_CHAN_WIDTH_160:
		/* Convert 160 MHz channel width to new style as interop
		 * workaround.
		 */
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ;
		he_6ghz_op->ccfs1 = he_6ghz_op->ccfs0;
		if (chandef->chan->center_freq < chandef->center_freq1)
			he_6ghz_op->ccfs0 -= 8;
		else
			he_6ghz_op->ccfs0 += 8;
		fallthrough;
	case NL80211_CHAN_WIDTH_80P80:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ;
		break;
	default:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ;
		break;
	}

	pos += sizeof(struct ieee80211_he_6ghz_oper);

out:
	return pos;
}

u8 *ieee80211_ie_build_eht_oper(u8 *pos, struct cfg80211_chan_def *chandef,
				const struct ieee80211_sta_eht_cap *eht_cap)

{
	const struct ieee80211_eht_mcs_nss_supp_20mhz_only *eht_mcs_nss =
					&eht_cap->eht_mcs_nss_supp.only_20mhz;
	struct ieee80211_eht_operation *eht_oper;
	struct ieee80211_eht_operation_info *eht_oper_info;
	u8 eht_oper_len = offsetof(struct ieee80211_eht_operation, optional);
	u8 eht_oper_info_len =
		offsetof(struct ieee80211_eht_operation_info, optional);
	u8 chan_width = 0;

	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = 1 + eht_oper_len + eht_oper_info_len;
	*pos++ = WLAN_EID_EXT_EHT_OPERATION;

	eht_oper = (struct ieee80211_eht_operation *)pos;

	memcpy(&eht_oper->basic_mcs_nss, eht_mcs_nss, sizeof(*eht_mcs_nss));
	eht_oper->params |= IEEE80211_EHT_OPER_INFO_PRESENT;
	pos += eht_oper_len;

	eht_oper_info =
		(struct ieee80211_eht_operation_info *)eht_oper->optional;

	eht_oper_info->ccfs0 =
		ieee80211_frequency_to_channel(chandef->center_freq1);
	if (chandef->center_freq2)
		eht_oper_info->ccfs1 =
			ieee80211_frequency_to_channel(chandef->center_freq2);
	else
		eht_oper_info->ccfs1 = 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_320:
		chan_width = IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ;
		eht_oper_info->ccfs1 = eht_oper_info->ccfs0;
		if (chandef->chan->center_freq < chandef->center_freq1)
			eht_oper_info->ccfs0 -= 16;
		else
			eht_oper_info->ccfs0 += 16;
		break;
	case NL80211_CHAN_WIDTH_160:
		eht_oper_info->ccfs1 = eht_oper_info->ccfs0;
		if (chandef->chan->center_freq < chandef->center_freq1)
			eht_oper_info->ccfs0 -= 8;
		else
			eht_oper_info->ccfs0 += 8;
		fallthrough;
	case NL80211_CHAN_WIDTH_80P80:
		chan_width = IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		chan_width = IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		chan_width = IEEE80211_EHT_OPER_CHAN_WIDTH_40MHZ;
		break;
	default:
		chan_width = IEEE80211_EHT_OPER_CHAN_WIDTH_20MHZ;
		break;
	}
	eht_oper_info->control = chan_width;
	pos += eht_oper_info_len;

	/* TODO: eht_oper_info->optional */

	return pos;
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
		return false;
	}

	cfg80211_chandef_create(chandef, chandef->chan, channel_type);
	return true;
}

bool ieee80211_chandef_vht_oper(struct ieee80211_hw *hw, u32 vht_cap_info,
				const struct ieee80211_vht_operation *oper,
				const struct ieee80211_ht_operation *htop,
				struct cfg80211_chan_def *chandef)
{
	struct cfg80211_chan_def new = *chandef;
	int cf0, cf1;
	int ccfs0, ccfs1, ccfs2;
	int ccf0, ccf1;
	u32 vht_cap;
	bool support_80_80 = false;
	bool support_160 = false;
	u8 ext_nss_bw_supp = u32_get_bits(vht_cap_info,
					  IEEE80211_VHT_CAP_EXT_NSS_BW_MASK);
	u8 supp_chwidth = u32_get_bits(vht_cap_info,
				       IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK);

	if (!oper || !htop)
		return false;

	vht_cap = hw->wiphy->bands[chandef->chan->band]->vht_cap.cap;
	support_160 = (vht_cap & (IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK |
				  IEEE80211_VHT_CAP_EXT_NSS_BW_MASK));
	support_80_80 = ((vht_cap &
			 IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ) ||
			(vht_cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ &&
			 vht_cap & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK) ||
			((vht_cap & IEEE80211_VHT_CAP_EXT_NSS_BW_MASK) >>
				    IEEE80211_VHT_CAP_EXT_NSS_BW_SHIFT > 1));
	ccfs0 = oper->center_freq_seg0_idx;
	ccfs1 = oper->center_freq_seg1_idx;
	ccfs2 = (le16_to_cpu(htop->operation_mode) &
				IEEE80211_HT_OP_MODE_CCFS2_MASK)
			>> IEEE80211_HT_OP_MODE_CCFS2_SHIFT;

	ccf0 = ccfs0;

	/* if not supported, parse as though we didn't understand it */
	if (!ieee80211_hw_check(hw, SUPPORTS_VHT_EXT_NSS_BW))
		ext_nss_bw_supp = 0;

	/*
	 * Cf. IEEE 802.11 Table 9-250
	 *
	 * We really just consider that because it's inefficient to connect
	 * at a higher bandwidth than we'll actually be able to use.
	 */
	switch ((supp_chwidth << 4) | ext_nss_bw_supp) {
	default:
	case 0x00:
		ccf1 = 0;
		support_160 = false;
		support_80_80 = false;
		break;
	case 0x01:
		support_80_80 = false;
		fallthrough;
	case 0x02:
	case 0x03:
		ccf1 = ccfs2;
		break;
	case 0x10:
		ccf1 = ccfs1;
		break;
	case 0x11:
	case 0x12:
		if (!ccfs1)
			ccf1 = ccfs2;
		else
			ccf1 = ccfs1;
		break;
	case 0x13:
	case 0x20:
	case 0x23:
		ccf1 = ccfs1;
		break;
	}

	cf0 = ieee80211_channel_to_frequency(ccf0, chandef->chan->band);
	cf1 = ieee80211_channel_to_frequency(ccf1, chandef->chan->band);

	switch (oper->chan_width) {
	case IEEE80211_VHT_CHANWIDTH_USE_HT:
		/* just use HT information directly */
		break;
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		new.width = NL80211_CHAN_WIDTH_80;
		new.center_freq1 = cf0;
		/* If needed, adjust based on the newer interop workaround. */
		if (ccf1) {
			unsigned int diff;

			diff = abs(ccf1 - ccf0);
			if ((diff == 8) && support_160) {
				new.width = NL80211_CHAN_WIDTH_160;
				new.center_freq1 = cf1;
			} else if ((diff > 8) && support_80_80) {
				new.width = NL80211_CHAN_WIDTH_80P80;
				new.center_freq2 = cf1;
			}
		}
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		/* deprecated encoding */
		new.width = NL80211_CHAN_WIDTH_160;
		new.center_freq1 = cf0;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		/* deprecated encoding */
		new.width = NL80211_CHAN_WIDTH_80P80;
		new.center_freq1 = cf0;
		new.center_freq2 = cf1;
		break;
	default:
		return false;
	}

	if (!cfg80211_chandef_valid(&new))
		return false;

	*chandef = new;
	return true;
}

void ieee80211_chandef_eht_oper(const struct ieee80211_eht_operation_info *info,
				struct cfg80211_chan_def *chandef)
{
	chandef->center_freq1 =
		ieee80211_channel_to_frequency(info->ccfs0,
					       chandef->chan->band);

	switch (u8_get_bits(info->control,
			    IEEE80211_EHT_OPER_CHAN_WIDTH)) {
	case IEEE80211_EHT_OPER_CHAN_WIDTH_20MHZ:
		chandef->width = NL80211_CHAN_WIDTH_20;
		break;
	case IEEE80211_EHT_OPER_CHAN_WIDTH_40MHZ:
		chandef->width = NL80211_CHAN_WIDTH_40;
		break;
	case IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ:
		chandef->width = NL80211_CHAN_WIDTH_80;
		break;
	case IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ:
		chandef->width = NL80211_CHAN_WIDTH_160;
		chandef->center_freq1 =
			ieee80211_channel_to_frequency(info->ccfs1,
						       chandef->chan->band);
		break;
	case IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ:
		chandef->width = NL80211_CHAN_WIDTH_320;
		chandef->center_freq1 =
			ieee80211_channel_to_frequency(info->ccfs1,
						       chandef->chan->band);
		break;
	}
}

bool ieee80211_chandef_he_6ghz_oper(struct ieee80211_local *local,
				    const struct ieee80211_he_operation *he_oper,
				    const struct ieee80211_eht_operation *eht_oper,
				    struct cfg80211_chan_def *chandef)
{
	struct cfg80211_chan_def he_chandef = *chandef;
	const struct ieee80211_he_6ghz_oper *he_6ghz_oper;
	u32 freq;

	if (chandef->chan->band != NL80211_BAND_6GHZ)
		return true;

	if (!he_oper)
		return false;

	he_6ghz_oper = ieee80211_he_6ghz_oper(he_oper);
	if (!he_6ghz_oper)
		return false;

	/*
	 * The EHT operation IE does not contain the primary channel so the
	 * primary channel frequency should be taken from the 6 GHz operation
	 * information.
	 */
	freq = ieee80211_channel_to_frequency(he_6ghz_oper->primary,
					      NL80211_BAND_6GHZ);
	he_chandef.chan = ieee80211_get_channel(local->hw.wiphy, freq);

	if (!he_chandef.chan)
		return false;

	if (!eht_oper ||
	    !(eht_oper->params & IEEE80211_EHT_OPER_INFO_PRESENT)) {
		switch (u8_get_bits(he_6ghz_oper->control,
				    IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH)) {
		case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ:
			he_chandef.width = NL80211_CHAN_WIDTH_20;
			break;
		case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ:
			he_chandef.width = NL80211_CHAN_WIDTH_40;
			break;
		case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ:
			he_chandef.width = NL80211_CHAN_WIDTH_80;
			break;
		case IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ:
			he_chandef.width = NL80211_CHAN_WIDTH_80;
			if (!he_6ghz_oper->ccfs1)
				break;
			if (abs(he_6ghz_oper->ccfs1 - he_6ghz_oper->ccfs0) == 8)
				he_chandef.width = NL80211_CHAN_WIDTH_160;
			else
				he_chandef.width = NL80211_CHAN_WIDTH_80P80;
			break;
		}

		if (he_chandef.width == NL80211_CHAN_WIDTH_160) {
			he_chandef.center_freq1 =
				ieee80211_channel_to_frequency(he_6ghz_oper->ccfs1,
							       NL80211_BAND_6GHZ);
		} else {
			he_chandef.center_freq1 =
				ieee80211_channel_to_frequency(he_6ghz_oper->ccfs0,
							       NL80211_BAND_6GHZ);
			he_chandef.center_freq2 =
				ieee80211_channel_to_frequency(he_6ghz_oper->ccfs1,
							       NL80211_BAND_6GHZ);
		}
	} else {
		ieee80211_chandef_eht_oper((const void *)eht_oper->optional,
					   &he_chandef);
		he_chandef.punctured =
			ieee80211_eht_oper_dis_subchan_bitmap(eht_oper);
	}

	if (!cfg80211_chandef_valid(&he_chandef))
		return false;

	*chandef = he_chandef;

	return true;
}

bool ieee80211_chandef_s1g_oper(const struct ieee80211_s1g_oper_ie *oper,
				struct cfg80211_chan_def *chandef)
{
	u32 oper_freq;

	if (!oper)
		return false;

	switch (FIELD_GET(S1G_OPER_CH_WIDTH_OPER, oper->ch_width)) {
	case IEEE80211_S1G_CHANWIDTH_1MHZ:
		chandef->width = NL80211_CHAN_WIDTH_1;
		break;
	case IEEE80211_S1G_CHANWIDTH_2MHZ:
		chandef->width = NL80211_CHAN_WIDTH_2;
		break;
	case IEEE80211_S1G_CHANWIDTH_4MHZ:
		chandef->width = NL80211_CHAN_WIDTH_4;
		break;
	case IEEE80211_S1G_CHANWIDTH_8MHZ:
		chandef->width = NL80211_CHAN_WIDTH_8;
		break;
	case IEEE80211_S1G_CHANWIDTH_16MHZ:
		chandef->width = NL80211_CHAN_WIDTH_16;
		break;
	default:
		return false;
	}

	oper_freq = ieee80211_channel_to_freq_khz(oper->oper_ch,
						  NL80211_BAND_S1GHZ);
	chandef->center_freq1 = KHZ_TO_MHZ(oper_freq);
	chandef->freq1_offset = oper_freq % 1000;

	return true;
}

int ieee80211_put_srates_elem(struct sk_buff *skb,
			      const struct ieee80211_supported_band *sband,
			      u32 basic_rates, u32 rate_flags, u32 masked_rates,
			      u8 element_id)
{
	u8 i, rates, skip;

	rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;
		if (masked_rates & BIT(i))
			continue;
		rates++;
	}

	if (element_id == WLAN_EID_SUPP_RATES) {
		rates = min_t(u8, rates, 8);
		skip = 0;
	} else {
		skip = 8;
		if (rates <= skip)
			return 0;
		rates -= skip;
	}

	if (skb_tailroom(skb) < rates + 2)
		return -ENOBUFS;

	skb_put_u8(skb, element_id);
	skb_put_u8(skb, rates);

	for (i = 0; i < sband->n_bitrates && rates; i++) {
		int rate;
		u8 basic;

		if ((rate_flags & sband->bitrates[i].flags) != rate_flags)
			continue;
		if (masked_rates & BIT(i))
			continue;

		if (skip > 0) {
			skip--;
			continue;
		}

		basic = basic_rates & BIT(i) ? 0x80 : 0;

		rate = DIV_ROUND_UP(sband->bitrates[i].bitrate, 5);
		skb_put_u8(skb, basic | (u8)rate);
		rates--;
	}

	WARN(rates > 0, "rates confused: rates:%d, element:%d\n",
	     rates, element_id);

	return 0;
}

int ieee80211_ave_rssi(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	if (WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION))
		return 0;

	return -ewma_beacon_signal_read(&sdata->deflink.u.mgd.ave_beacon_signal);
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
 *
 * Returns: the calculated timestamp
 */
u64 ieee80211_calculate_rx_timestamp(struct ieee80211_local *local,
				     struct ieee80211_rx_status *status,
				     unsigned int mpdu_len,
				     unsigned int mpdu_offset)
{
	u64 ts = status->mactime;
	bool mactime_plcp_start;
	struct rate_info ri;
	u16 rate;
	u8 n_ltf;

	if (WARN_ON(!ieee80211_have_rx_timestamp(status)))
		return 0;

	mactime_plcp_start = (status->flag & RX_FLAG_MACTIME) ==
				RX_FLAG_MACTIME_PLCP_START;

	memset(&ri, 0, sizeof(ri));

	ri.bw = status->bw;

	/* Fill cfg80211 rate info */
	switch (status->encoding) {
	case RX_ENC_EHT:
		ri.flags |= RATE_INFO_FLAGS_EHT_MCS;
		ri.mcs = status->rate_idx;
		ri.nss = status->nss;
		ri.eht_ru_alloc = status->eht.ru;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;
		/* TODO/FIXME: is this right? handle other PPDUs */
		if (mactime_plcp_start) {
			mpdu_offset += 2;
			ts += 36;
		}
		break;
	case RX_ENC_HE:
		ri.flags |= RATE_INFO_FLAGS_HE_MCS;
		ri.mcs = status->rate_idx;
		ri.nss = status->nss;
		ri.he_ru_alloc = status->he_ru;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;

		/*
		 * See P802.11ax_D6.0, section 27.3.4 for
		 * VHT PPDU format.
		 */
		if (mactime_plcp_start) {
			mpdu_offset += 2;
			ts += 36;

			/*
			 * TODO:
			 * For HE MU PPDU, add the HE-SIG-B.
			 * For HE ER PPDU, add 8us for the HE-SIG-A.
			 * For HE TB PPDU, add 4us for the HE-STF.
			 * Add the HE-LTF durations - variable.
			 */
		}

		break;
	case RX_ENC_HT:
		ri.mcs = status->rate_idx;
		ri.flags |= RATE_INFO_FLAGS_MCS;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;

		/*
		 * See P802.11REVmd_D3.0, section 19.3.2 for
		 * HT PPDU format.
		 */
		if (mactime_plcp_start) {
			mpdu_offset += 2;
			if (status->enc_flags & RX_ENC_FLAG_HT_GF)
				ts += 24;
			else
				ts += 32;

			/*
			 * Add Data HT-LTFs per streams
			 * TODO: add Extension HT-LTFs, 4us per LTF
			 */
			n_ltf = ((ri.mcs >> 3) & 3) + 1;
			n_ltf = n_ltf == 3 ? 4 : n_ltf;
			ts += n_ltf * 4;
		}

		break;
	case RX_ENC_VHT:
		ri.flags |= RATE_INFO_FLAGS_VHT_MCS;
		ri.mcs = status->rate_idx;
		ri.nss = status->nss;
		if (status->enc_flags & RX_ENC_FLAG_SHORT_GI)
			ri.flags |= RATE_INFO_FLAGS_SHORT_GI;

		/*
		 * See P802.11REVmd_D3.0, section 21.3.2 for
		 * VHT PPDU format.
		 */
		if (mactime_plcp_start) {
			mpdu_offset += 2;
			ts += 36;

			/*
			 * Add VHT-LTFs per streams
			 */
			n_ltf = (ri.nss != 1) && (ri.nss % 2) ?
				ri.nss + 1 : ri.nss;
			ts += 4 * n_ltf;
		}

		break;
	default:
		WARN_ON(1);
		fallthrough;
	case RX_ENC_LEGACY: {
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[status->band];
		ri.legacy = sband->bitrates[status->rate_idx].bitrate;

		if (mactime_plcp_start) {
			if (status->band == NL80211_BAND_5GHZ) {
				ts += 20;
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
	if ((status->flag & RX_FLAG_MACTIME) == RX_FLAG_MACTIME_END)
		ts -= mpdu_len * 8 * 10 / rate;

	ts += mpdu_offset * 8 * 10 / rate;

	return ts;
}

void ieee80211_dfs_cac_cancel(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	struct cfg80211_chan_def chandef;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(sdata, &local->interfaces, list) {
		wiphy_delayed_work_cancel(local->hw.wiphy,
					  &sdata->dfs_cac_timer_work);

		if (sdata->wdev.cac_started) {
			chandef = sdata->vif.bss_conf.chanreq.oper;
			ieee80211_link_release_channel(&sdata->deflink);
			cfg80211_cac_event(sdata->dev,
					   &chandef,
					   NL80211_RADAR_CAC_ABORTED,
					   GFP_KERNEL);
		}
	}
}

void ieee80211_dfs_radar_detected_work(struct wiphy *wiphy,
				       struct wiphy_work *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, radar_detected_work);
	struct cfg80211_chan_def chandef = local->hw.conf.chandef;
	struct ieee80211_chanctx *ctx;
	int num_chanctx = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER)
			continue;

		num_chanctx++;
		chandef = ctx->conf.def;
	}

	ieee80211_dfs_cac_cancel(local);

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

	wiphy_work_queue(hw->wiphy, &local->radar_detected_work);
}
EXPORT_SYMBOL(ieee80211_radar_detected);

void ieee80211_chandef_downgrade(struct cfg80211_chan_def *c,
				 struct ieee80211_conn_settings *conn)
{
	enum nl80211_chan_width new_primary_width;
	struct ieee80211_conn_settings _ignored = {};

	/* allow passing NULL if caller doesn't care */
	if (!conn)
		conn = &_ignored;

again:
	/* no-HT indicates nothing to do */
	new_primary_width = NL80211_CHAN_WIDTH_20_NOHT;

	switch (c->width) {
	default:
	case NL80211_CHAN_WIDTH_20_NOHT:
		WARN_ON_ONCE(1);
		fallthrough;
	case NL80211_CHAN_WIDTH_20:
		c->width = NL80211_CHAN_WIDTH_20_NOHT;
		conn->mode = IEEE80211_CONN_MODE_LEGACY;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_20;
		c->punctured = 0;
		break;
	case NL80211_CHAN_WIDTH_40:
		c->width = NL80211_CHAN_WIDTH_20;
		c->center_freq1 = c->chan->center_freq;
		if (conn->mode == IEEE80211_CONN_MODE_VHT)
			conn->mode = IEEE80211_CONN_MODE_HT;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_20;
		c->punctured = 0;
		break;
	case NL80211_CHAN_WIDTH_80:
		new_primary_width = NL80211_CHAN_WIDTH_40;
		if (conn->mode == IEEE80211_CONN_MODE_VHT)
			conn->mode = IEEE80211_CONN_MODE_HT;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_40;
		break;
	case NL80211_CHAN_WIDTH_80P80:
		c->center_freq2 = 0;
		c->width = NL80211_CHAN_WIDTH_80;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_80;
		break;
	case NL80211_CHAN_WIDTH_160:
		new_primary_width = NL80211_CHAN_WIDTH_80;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_80;
		break;
	case NL80211_CHAN_WIDTH_320:
		new_primary_width = NL80211_CHAN_WIDTH_160;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_160;
		break;
	case NL80211_CHAN_WIDTH_1:
	case NL80211_CHAN_WIDTH_2:
	case NL80211_CHAN_WIDTH_4:
	case NL80211_CHAN_WIDTH_8:
	case NL80211_CHAN_WIDTH_16:
		WARN_ON_ONCE(1);
		/* keep c->width */
		conn->mode = IEEE80211_CONN_MODE_S1G;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_20;
		break;
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
		WARN_ON_ONCE(1);
		/* keep c->width */
		conn->mode = IEEE80211_CONN_MODE_LEGACY;
		conn->bw_limit = IEEE80211_CONN_BW_LIMIT_20;
		break;
	}

	if (new_primary_width != NL80211_CHAN_WIDTH_20_NOHT) {
		c->center_freq1 = cfg80211_chandef_primary(c, new_primary_width,
							   &c->punctured);
		c->width = new_primary_width;
	}

	/*
	 * With an 80 MHz channel, we might have the puncturing in the primary
	 * 40 Mhz channel, but that's not valid when downgraded to 40 MHz width.
	 * In that case, downgrade again.
	 */
	if (!cfg80211_chandef_valid(c) && c->punctured)
		goto again;

	WARN_ON_ONCE(!cfg80211_chandef_valid(c));
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
	struct ieee80211_link_data *link;
	u8 radar_detect = 0;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED))
		return 0;

	list_for_each_entry(link, &ctx->reserved_links, reserved_chanctx_list)
		if (link->reserved_radar_required)
			radar_detect |= BIT(link->reserved.oper.width);

	/*
	 * An in-place reservation context should not have any assigned vifs
	 * until it replaces the other context.
	 */
	WARN_ON(ctx->replace_state == IEEE80211_CHANCTX_REPLACES_OTHER &&
		!list_empty(&ctx->assigned_links));

	list_for_each_entry(link, &ctx->assigned_links, assigned_chanctx_list) {
		if (!link->radar_required)
			continue;

		radar_detect |=
			BIT(link->conf->chanreq.oper.width);
	}

	return radar_detect;
}

static u32
__ieee80211_get_radio_mask(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_chanctx_conf *conf;
	unsigned int link_id;
	u32 mask = 0;

	for_each_vif_active_link(&sdata->vif, link_conf, link_id) {
		conf = sdata_dereference(link_conf->chanctx_conf, sdata);
		if (!conf || conf->radio_idx < 0)
			continue;

		mask |= BIT(conf->radio_idx);
	}

	return mask;
}

u32 ieee80211_get_radio_mask(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	return __ieee80211_get_radio_mask(sdata);
}

static bool
ieee80211_sdata_uses_radio(struct ieee80211_sub_if_data *sdata, int radio_idx)
{
	if (radio_idx < 0)
		return true;

	return __ieee80211_get_radio_mask(sdata) & BIT(radio_idx);
}

static int
ieee80211_fill_ifcomb_params(struct ieee80211_local *local,
			     struct iface_combination_params *params,
			     const struct cfg80211_chan_def *chandef,
			     struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_sub_if_data *sdata_iter;
	struct ieee80211_chanctx *ctx;
	int total = !!sdata;

	list_for_each_entry(ctx, &local->chanctx_list, list) {
		if (ctx->replace_state == IEEE80211_CHANCTX_WILL_BE_REPLACED)
			continue;

		if (params->radio_idx >= 0 &&
		    ctx->conf.radio_idx != params->radio_idx)
			continue;

		params->radar_detect |=
			ieee80211_chanctx_radar_detect(local, ctx);

		if (chandef && ctx->mode != IEEE80211_CHANCTX_EXCLUSIVE &&
		    cfg80211_chandef_compatible(chandef, &ctx->conf.def))
			continue;

		params->num_different_channels++;
	}

	list_for_each_entry(sdata_iter, &local->interfaces, list) {
		struct wireless_dev *wdev_iter;

		wdev_iter = &sdata_iter->wdev;

		if (sdata_iter == sdata ||
		    !ieee80211_sdata_running(sdata_iter) ||
		    cfg80211_iftype_allowed(local->hw.wiphy,
					    wdev_iter->iftype, 0, 1))
			continue;

		if (!ieee80211_sdata_uses_radio(sdata_iter, params->radio_idx))
			continue;

		params->iftype_num[wdev_iter->iftype]++;
		total++;
	}

	return total;
}

int ieee80211_check_combinations(struct ieee80211_sub_if_data *sdata,
				 const struct cfg80211_chan_def *chandef,
				 enum ieee80211_chanctx_mode chanmode,
				 u8 radar_detect, int radio_idx)
{
	bool shared = chanmode == IEEE80211_CHANCTX_SHARED;
	struct ieee80211_local *local = sdata->local;
	enum nl80211_iftype iftype = sdata->wdev.iftype;
	struct iface_combination_params params = {
		.radar_detect = radar_detect,
		.radio_idx = radio_idx,
	};
	int total;

	lockdep_assert_wiphy(local->hw.wiphy);

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
	if (cfg80211_iftype_allowed(local->hw.wiphy, iftype, 0, 1)) {
		if (radar_detect)
			return -EINVAL;
		return 0;
	}

	if (chandef)
		params.num_different_channels = 1;

	if (iftype != NL80211_IFTYPE_UNSPECIFIED)
		params.iftype_num[iftype] = 1;

	total = ieee80211_fill_ifcomb_params(local, &params,
					     shared ? chandef : NULL,
					     sdata);
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

int ieee80211_max_num_channels(struct ieee80211_local *local, int radio_idx)
{
	u32 max_num_different_channels = 1;
	int err;
	struct iface_combination_params params = {
		.radio_idx = radio_idx,
	};

	lockdep_assert_wiphy(local->hw.wiphy);

	ieee80211_fill_ifcomb_params(local, &params, NULL, NULL);

	err = cfg80211_iter_combinations(local->hw.wiphy, &params,
					 ieee80211_iter_max_chans,
					 &max_num_different_channels);
	if (err < 0)
		return err;

	return max_num_different_channels;
}

void ieee80211_add_s1g_capab_ie(struct ieee80211_sub_if_data *sdata,
				struct ieee80211_sta_s1g_cap *caps,
				struct sk_buff *skb)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_s1g_cap s1g_capab;
	u8 *pos;
	int i;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	if (!caps->s1g)
		return;

	memcpy(s1g_capab.capab_info, caps->cap, sizeof(caps->cap));
	memcpy(s1g_capab.supp_mcs_nss, caps->nss_mcs, sizeof(caps->nss_mcs));

	/* override the capability info */
	for (i = 0; i < sizeof(ifmgd->s1g_capa.capab_info); i++) {
		u8 mask = ifmgd->s1g_capa_mask.capab_info[i];

		s1g_capab.capab_info[i] &= ~mask;
		s1g_capab.capab_info[i] |= ifmgd->s1g_capa.capab_info[i] & mask;
	}

	/* then MCS and NSS set */
	for (i = 0; i < sizeof(ifmgd->s1g_capa.supp_mcs_nss); i++) {
		u8 mask = ifmgd->s1g_capa_mask.supp_mcs_nss[i];

		s1g_capab.supp_mcs_nss[i] &= ~mask;
		s1g_capab.supp_mcs_nss[i] |=
			ifmgd->s1g_capa.supp_mcs_nss[i] & mask;
	}

	pos = skb_put(skb, 2 + sizeof(s1g_capab));
	*pos++ = WLAN_EID_S1G_CAPABILITIES;
	*pos++ = sizeof(s1g_capab);

	memcpy(pos, &s1g_capab, sizeof(s1g_capab));
}

void ieee80211_add_aid_request_ie(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	u8 *pos = skb_put(skb, 3);

	*pos++ = WLAN_EID_AID_REQUEST;
	*pos++ = 1;
	*pos++ = 0;
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

u16 ieee80211_encode_usf(int listen_interval)
{
	static const int listen_int_usf[] = { 1, 10, 1000, 10000 };
	u16 ui, usf = 0;

	/* find greatest USF */
	while (usf < IEEE80211_MAX_USF) {
		if (listen_interval % listen_int_usf[usf + 1])
			break;
		usf += 1;
	}
	ui = listen_interval / listen_int_usf[usf];

	/* error if there is a remainder. Should've been checked by user */
	WARN_ON_ONCE(ui > IEEE80211_MAX_UI);
	listen_interval = FIELD_PREP(LISTEN_INT_USF, usf) |
			  FIELD_PREP(LISTEN_INT_UI, ui);

	return (u16) listen_interval;
}

/* this may return more than ieee80211_put_eht_cap() will need */
u8 ieee80211_ie_len_eht_cap(struct ieee80211_sub_if_data *sdata)
{
	const struct ieee80211_sta_he_cap *he_cap;
	const struct ieee80211_sta_eht_cap *eht_cap;
	struct ieee80211_supported_band *sband;
	bool is_ap;
	u8 n;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return 0;

	he_cap = ieee80211_get_he_iftype_cap_vif(sband, &sdata->vif);
	eht_cap = ieee80211_get_eht_iftype_cap_vif(sband, &sdata->vif);
	if (!he_cap || !eht_cap)
		return 0;

	is_ap = sdata->vif.type == NL80211_IFTYPE_AP;

	n = ieee80211_eht_mcs_nss_size(&he_cap->he_cap_elem,
				       &eht_cap->eht_cap_elem,
				       is_ap);
	return 2 + 1 +
	       sizeof(eht_cap->eht_cap_elem) + n +
	       ieee80211_eht_ppe_size(eht_cap->eht_ppe_thres[0],
				      eht_cap->eht_cap_elem.phy_cap_info);
	return 0;
}

int ieee80211_put_eht_cap(struct sk_buff *skb,
			  struct ieee80211_sub_if_data *sdata,
			  const struct ieee80211_supported_band *sband,
			  const struct ieee80211_conn_settings *conn)
{
	const struct ieee80211_sta_he_cap *he_cap =
		ieee80211_get_he_iftype_cap_vif(sband, &sdata->vif);
	const struct ieee80211_sta_eht_cap *eht_cap =
		ieee80211_get_eht_iftype_cap_vif(sband, &sdata->vif);
	bool for_ap = sdata->vif.type == NL80211_IFTYPE_AP;
	struct ieee80211_eht_cap_elem_fixed fixed;
	struct ieee80211_he_cap_elem he;
	u8 mcs_nss_len, ppet_len;
	u8 orig_mcs_nss_len;
	u8 ie_len;

	if (!conn)
		conn = &ieee80211_conn_settings_unlimited;

	/* Make sure we have place for the IE */
	if (!he_cap || !eht_cap)
		return 0;

	orig_mcs_nss_len = ieee80211_eht_mcs_nss_size(&he_cap->he_cap_elem,
						      &eht_cap->eht_cap_elem,
						      for_ap);

	ieee80211_get_adjusted_he_cap(conn, he_cap, &he);

	fixed = eht_cap->eht_cap_elem;

	if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_80)
		fixed.phy_cap_info[6] &=
			~IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_80MHZ;

	if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_160) {
		fixed.phy_cap_info[1] &=
			~IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK;
		fixed.phy_cap_info[2] &=
			~IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK;
		fixed.phy_cap_info[6] &=
			~IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_160MHZ;
	}

	if (conn->bw_limit < IEEE80211_CONN_BW_LIMIT_320) {
		fixed.phy_cap_info[0] &=
			~IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ;
		fixed.phy_cap_info[1] &=
			~IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK;
		fixed.phy_cap_info[2] &=
			~IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK;
		fixed.phy_cap_info[6] &=
			~IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_320MHZ;
	}

	if (conn->bw_limit == IEEE80211_CONN_BW_LIMIT_20)
		fixed.phy_cap_info[0] &=
			~IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ;

	mcs_nss_len = ieee80211_eht_mcs_nss_size(&he, &fixed, for_ap);
	ppet_len = ieee80211_eht_ppe_size(eht_cap->eht_ppe_thres[0],
					  fixed.phy_cap_info);

	ie_len = 2 + 1 + sizeof(eht_cap->eht_cap_elem) + mcs_nss_len + ppet_len;
	if (skb_tailroom(skb) < ie_len)
		return -ENOBUFS;

	skb_put_u8(skb, WLAN_EID_EXTENSION);
	skb_put_u8(skb, ie_len - 2);
	skb_put_u8(skb, WLAN_EID_EXT_EHT_CAPABILITY);
	skb_put_data(skb, &fixed, sizeof(fixed));

	if (mcs_nss_len == 4 && orig_mcs_nss_len != 4) {
		/*
		 * If the (non-AP) STA became 20 MHz only, then convert from
		 * <=80 to 20-MHz-only format, where MCSes are indicated in
		 * the groups 0-7, 8-9, 10-11, 12-13 rather than just 0-9,
		 * 10-11, 12-13. Thus, use 0-9 for 0-7 and 8-9.
		 */
		skb_put_u8(skb, eht_cap->eht_mcs_nss_supp.bw._80.rx_tx_mcs9_max_nss);
		skb_put_u8(skb, eht_cap->eht_mcs_nss_supp.bw._80.rx_tx_mcs9_max_nss);
		skb_put_u8(skb, eht_cap->eht_mcs_nss_supp.bw._80.rx_tx_mcs11_max_nss);
		skb_put_u8(skb, eht_cap->eht_mcs_nss_supp.bw._80.rx_tx_mcs13_max_nss);
	} else {
		skb_put_data(skb, &eht_cap->eht_mcs_nss_supp, mcs_nss_len);
	}

	if (ppet_len)
		skb_put_data(skb, &eht_cap->eht_ppe_thres, ppet_len);

	return 0;
}

const char *ieee80211_conn_mode_str(enum ieee80211_conn_mode mode)
{
	static const char * const modes[] = {
		[IEEE80211_CONN_MODE_S1G] = "S1G",
		[IEEE80211_CONN_MODE_LEGACY] = "legacy",
		[IEEE80211_CONN_MODE_HT] = "HT",
		[IEEE80211_CONN_MODE_VHT] = "VHT",
		[IEEE80211_CONN_MODE_HE] = "HE",
		[IEEE80211_CONN_MODE_EHT] = "EHT",
	};

	if (WARN_ON(mode >= ARRAY_SIZE(modes)))
		return "<out of range>";

	return modes[mode] ?: "<missing string>";
}

enum ieee80211_conn_bw_limit
ieee80211_min_bw_limit_from_chandef(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return IEEE80211_CONN_BW_LIMIT_20;
	case NL80211_CHAN_WIDTH_40:
		return IEEE80211_CONN_BW_LIMIT_40;
	case NL80211_CHAN_WIDTH_80:
		return IEEE80211_CONN_BW_LIMIT_80;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		return IEEE80211_CONN_BW_LIMIT_160;
	case NL80211_CHAN_WIDTH_320:
		return IEEE80211_CONN_BW_LIMIT_320;
	default:
		WARN(1, "unhandled chandef width %d\n", chandef->width);
		return IEEE80211_CONN_BW_LIMIT_20;
	}
}

void ieee80211_clear_tpe(struct ieee80211_parsed_tpe *tpe)
{
	for (int i = 0; i < 2; i++) {
		tpe->max_local[i].valid = false;
		memset(tpe->max_local[i].power,
		       IEEE80211_TPE_MAX_TX_PWR_NO_CONSTRAINT,
		       sizeof(tpe->max_local[i].power));

		tpe->max_reg_client[i].valid = false;
		memset(tpe->max_reg_client[i].power,
		       IEEE80211_TPE_MAX_TX_PWR_NO_CONSTRAINT,
		       sizeof(tpe->max_reg_client[i].power));

		tpe->psd_local[i].valid = false;
		memset(tpe->psd_local[i].power,
		       IEEE80211_TPE_PSD_NO_LIMIT,
		       sizeof(tpe->psd_local[i].power));

		tpe->psd_reg_client[i].valid = false;
		memset(tpe->psd_reg_client[i].power,
		       IEEE80211_TPE_PSD_NO_LIMIT,
		       sizeof(tpe->psd_reg_client[i].power));
	}
}
