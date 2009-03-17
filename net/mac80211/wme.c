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
	case htons(ETH_P_IP):
		dscp = ip_hdr(skb)->tos & 0xfc;
		break;

	default:
		return 0;
	}

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
static u16 classify80211(struct ieee80211_local *local, struct sk_buff *skb)
{
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
			/*
			 * This should not really happen. The AP has marked all
			 * lower ACs to require admission control which is not
			 * a reasonable configuration. Allow the frame to be
			 * transmitted using AC_BK as a workaround.
			 */
			break;
		}
	}

	/* look up which queue to use for frames with this 1d tag */
	return ieee802_1d_to_ac[skb->priority];
}

u16 ieee80211_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	struct ieee80211_master_priv *mpriv = netdev_priv(dev);
	struct ieee80211_local *local = mpriv->local;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 queue;
	u8 tid;

	queue = classify80211(local, skb);
	if (unlikely(queue >= local->hw.queues))
		queue = local->hw.queues - 1;

	/*
	 * Now we know the 1d priority, fill in the QoS header if
	 * there is one (and we haven't done this before).
	 */
	if (!skb->requeue && ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *p = ieee80211_get_qos_ctl(hdr);
		u8 ack_policy = 0;
		tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		if (local->wifi_wme_noack_test)
			ack_policy |= QOS_CONTROL_ACK_POLICY_NOACK <<
					QOS_CONTROL_ACK_POLICY_SHIFT;
		/* qos header is 2 bytes, second reserved */
		*p++ = ack_policy | tid;
		*p = 0;
	}

	return queue;
}
