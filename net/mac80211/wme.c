/*
 * Copyright 2004, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/pkt_sched.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "wme.h"

/* Default mapping in classifier to work with default
 * queue setup.
 */
const int ieee802_1d_to_ac[8] = { 2, 3, 3, 2, 1, 1, 0, 0 };

static const char llc_ip_hdr[8] = {0xAA, 0xAA, 0x3, 0, 0, 0, 0x08, 0};

/* Given a data frame determine the 802.1p/1d tag to use.  */
static unsigned int classify_1d(struct sk_buff *skb)
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
	case __constant_htons(ETH_P_IP):
		dscp = ip_hdr(skb)->tos & 0xfc;
		break;

	default:
		return 0;
	}

	if (dscp & 0x1c)
		return 0;
	return dscp >> 5;
}


static int wme_downgrade_ac(struct sk_buff *skb)
{
	switch (skb->priority) {
	case 6:
	case 7:
		skb->priority = 5; /* VO -> VI */
		return 0;
	case 4:
	case 5:
		skb->priority = 3; /* VI -> BE */
		return 0;
	case 0:
	case 3:
		skb->priority = 2; /* BE -> BK */
		return 0;
	default:
		return -1;
	}
}


/* Indicate which queue to use.  */
static u16 classify80211(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (!ieee80211_is_data(hdr->frame_control)) {
		/* management frames go on AC_VO queue, but are sent
		* without QoS control fields */
		return 0;
	}

	if (0 /* injected */) {
		/* use AC from radiotap */
	}

	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return ieee802_1d_to_ac[skb->priority];
	}

	/* use the data classifier to determine what 802.1d tag the
	 * data frame has */
	skb->priority = classify_1d(skb);

	/* in case we are a client verify acm is not set for this ac */
	while (unlikely(local->wmm_acm & BIT(skb->priority))) {
		if (wme_downgrade_ac(skb)) {
			/* The old code would drop the packet in this
			 * case.
			 */
			return 0;
		}
	}

	/* look up which queue to use for frames with this 1d tag */
	return ieee802_1d_to_ac[skb->priority];
}

u16 ieee80211_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct sta_info *sta;
	u16 queue;
	u8 tid;

	queue = classify80211(skb, dev);
	if (unlikely(queue >= local->hw.queues))
		queue = local->hw.queues - 1;

	if (info->flags & IEEE80211_TX_CTL_REQUEUE) {
		rcu_read_lock();
		sta = sta_info_get(local, hdr->addr1);
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		if (sta) {
			struct ieee80211_hw *hw = &local->hw;
			int ampdu_queue = sta->tid_to_tx_q[tid];

			if ((ampdu_queue < ieee80211_num_queues(hw)) &&
			    test_bit(ampdu_queue, local->queue_pool)) {
				queue = ampdu_queue;
				info->flags |= IEEE80211_TX_CTL_AMPDU;
			} else {
				info->flags &= ~IEEE80211_TX_CTL_AMPDU;
			}
		}
		rcu_read_unlock();

		return queue;
	}

	/* Now we know the 1d priority, fill in the QoS header if
	 * there is one.
	 */
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *p = ieee80211_get_qos_ctl(hdr);
		u8 ack_policy = 0;
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		if (local->wifi_wme_noack_test)
			ack_policy |= QOS_CONTROL_ACK_POLICY_NOACK <<
					QOS_CONTROL_ACK_POLICY_SHIFT;
		/* qos header is 2 bytes, second reserved */
		*p++ = ack_policy | tid;
		*p = 0;

		rcu_read_lock();

		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			int ampdu_queue = sta->tid_to_tx_q[tid];
			struct ieee80211_hw *hw = &local->hw;

			if ((ampdu_queue < ieee80211_num_queues(hw)) &&
			    test_bit(ampdu_queue, local->queue_pool)) {
				queue = ampdu_queue;
				info->flags |= IEEE80211_TX_CTL_AMPDU;
			} else {
				info->flags &= ~IEEE80211_TX_CTL_AMPDU;
			}
		}

		rcu_read_unlock();
	}

	return queue;
}

int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
			       struct sta_info *sta, u16 tid)
{
	int i;

	/* XXX: currently broken due to cb/requeue use */
	return -EPERM;

	/* prepare the filter and save it for the SW queue
	 * matching the received HW queue */

	if (!local->hw.ampdu_queues)
		return -EPERM;

	/* try to get a Qdisc from the pool */
	for (i = local->hw.queues; i < ieee80211_num_queues(&local->hw); i++)
		if (!test_and_set_bit(i, local->queue_pool)) {
			ieee80211_stop_queue(local_to_hw(local), i);
			sta->tid_to_tx_q[tid] = i;

			/* IF there are already pending packets
			 * on this tid first we need to drain them
			 * on the previous queue
			 * since HT is strict in order */
#ifdef CONFIG_MAC80211_HT_DEBUG
			if (net_ratelimit()) {
				DECLARE_MAC_BUF(mac);
				printk(KERN_DEBUG "allocated aggregation queue"
					" %d tid %d addr %s pool=0x%lX\n",
					i, tid, print_mac(mac, sta->addr),
					local->queue_pool[0]);
			}
#endif /* CONFIG_MAC80211_HT_DEBUG */
			return 0;
		}

	return -EAGAIN;
}

/**
 * the caller needs to hold netdev_get_tx_queue(local->mdev, X)->lock
 */
void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
				   struct sta_info *sta, u16 tid,
				   u8 requeue)
{
	int agg_queue = sta->tid_to_tx_q[tid];
	struct ieee80211_hw *hw = &local->hw;

	/* return the qdisc to the pool */
	clear_bit(agg_queue, local->queue_pool);
	sta->tid_to_tx_q[tid] = ieee80211_num_queues(hw);

	if (requeue) {
		ieee80211_requeue(local, agg_queue);
	} else {
		struct netdev_queue *txq;
		spinlock_t *root_lock;
		struct Qdisc *q;

		txq = netdev_get_tx_queue(local->mdev, agg_queue);
		q = rcu_dereference(txq->qdisc);
		root_lock = qdisc_lock(q);

		spin_lock_bh(root_lock);
		qdisc_reset(q);
		spin_unlock_bh(root_lock);
	}
}

void ieee80211_requeue(struct ieee80211_local *local, int queue)
{
	struct netdev_queue *txq = netdev_get_tx_queue(local->mdev, queue);
	struct sk_buff_head list;
	spinlock_t *root_lock;
	struct Qdisc *qdisc;
	u32 len;

	rcu_read_lock_bh();

	qdisc = rcu_dereference(txq->qdisc);
	if (!qdisc || !qdisc->dequeue)
		goto out_unlock;

	skb_queue_head_init(&list);

	root_lock = qdisc_root_lock(qdisc);
	spin_lock(root_lock);
	for (len = qdisc->q.qlen; len > 0; len--) {
		struct sk_buff *skb = qdisc->dequeue(qdisc);

		if (skb)
			__skb_queue_tail(&list, skb);
	}
	spin_unlock(root_lock);

	for (len = list.qlen; len > 0; len--) {
		struct sk_buff *skb = __skb_dequeue(&list);
		u16 new_queue;

		BUG_ON(!skb);
		new_queue = ieee80211_select_queue(local->mdev, skb);
		skb_set_queue_mapping(skb, new_queue);

		txq = netdev_get_tx_queue(local->mdev, new_queue);


		qdisc = rcu_dereference(txq->qdisc);
		root_lock = qdisc_root_lock(qdisc);

		spin_lock(root_lock);
		qdisc_enqueue_root(skb, qdisc);
		spin_unlock(root_lock);
	}

out_unlock:
	rcu_read_unlock_bh();
}
