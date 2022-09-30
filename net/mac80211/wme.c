// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2022 Intel Corporation
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
const int ieee802_1d_to_ac[8] = {
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO
};

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

/**
 * ieee80211_fix_reserved_tid - return the TID to use if this one is reserved
 * @tid: the assumed-reserved TID
 *
 * Returns: the alternative TID to use, or 0 on error
 */
static inline u8 ieee80211_fix_reserved_tid(u8 tid)
{
	switch (tid) {
	case 0:
		return 3;
	case 1:
		return 2;
	case 2:
		return 1;
	case 3:
		return 0;
	case 4:
		return 5;
	case 5:
		return 4;
	case 6:
		return 7;
	case 7:
		return 6;
	}

	return 0;
}

static u16 ieee80211_downgrade_queue(struct ieee80211_sub_if_data *sdata,
				     struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/* in case we are a client verify acm is not set for this ac */
	while (sdata->wmm_acm & BIT(skb->priority)) {
		int ac = ieee802_1d_to_ac[skb->priority];

		if (ifmgd->tx_tspec[ac].admitted_time &&
		    skb->priority == ifmgd->tx_tspec[ac].up)
			return ac;

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

	/* Check to see if this is a reserved TID */
	if (sta && sta->reserved_tid == skb->priority)
		skb->priority = ieee80211_fix_reserved_tid(skb->priority);

	/* look up which queue to use for frames with this 1d tag */
	return ieee802_1d_to_ac[skb->priority];
}

/* Indicate which queue to use for this fully formed 802.11 frame */
u16 ieee80211_select_queue_80211(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb,
				 struct ieee80211_hdr *hdr)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 *p;

	if ((info->control.flags & IEEE80211_TX_CTRL_DONT_REORDER) ||
	    local->hw.queues < IEEE80211_NUM_ACS)
		return 0;

	if (!ieee80211_is_data(hdr->frame_control)) {
		skb->priority = 7;
		return ieee802_1d_to_ac[skb->priority];
	}
	if (!ieee80211_is_data_qos(hdr->frame_control)) {
		skb->priority = 0;
		return ieee802_1d_to_ac[skb->priority];
	}

	p = ieee80211_get_qos_ctl(hdr);
	skb->priority = *p & IEEE80211_QOS_CTL_TAG1D_MASK;

	return ieee80211_downgrade_queue(sdata, NULL, skb);
}

u16 __ieee80211_select_queue(struct ieee80211_sub_if_data *sdata,
			     struct sta_info *sta, struct sk_buff *skb)
{
	struct mac80211_qos_map *qos_map;
	bool qos;

	/* all mesh/ocb stations are required to support WME */
	if (sta && (sdata->vif.type == NL80211_IFTYPE_MESH_POINT ||
		    sdata->vif.type == NL80211_IFTYPE_OCB))
		qos = true;
	else if (sta)
		qos = sta->sta.wme;
	else
		qos = false;

	if (!qos) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return IEEE80211_AC_BE;
	}

	if (skb->protocol == sdata->control_port_protocol) {
		skb->priority = 7;
		goto downgrade;
	}

	/* use the data classifier to determine what 802.1d tag the
	 * data frame has */
	qos_map = rcu_dereference(sdata->qos_map);
	skb->priority = cfg80211_classify8021d(skb, qos_map ?
					       &qos_map->qos_map : NULL);

 downgrade:
	return ieee80211_downgrade_queue(sdata, sta, skb);
}


/* Indicate which queue to use. */
u16 ieee80211_select_queue(struct ieee80211_sub_if_data *sdata,
			   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta = NULL;
	const u8 *ra = NULL;
	u16 ret;

	/* when using iTXQ, we can do this later */
	if (local->ops->wake_tx_queue)
		return 0;

	if (local->hw.queues < IEEE80211_NUM_ACS || skb->len < 6) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return 0;
	}

	rcu_read_lock();
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		sta = rcu_dereference(sdata->u.vlan.sta);
		if (sta)
			break;
		fallthrough;
	case NL80211_IFTYPE_AP:
		ra = skb->data;
		break;
	case NL80211_IFTYPE_STATION:
		/* might be a TDLS station */
		sta = sta_info_get(sdata, skb->data);
		if (sta)
			break;

		ra = sdata->deflink.u.mgd.bssid;
		break;
	case NL80211_IFTYPE_ADHOC:
		ra = skb->data;
		break;
	default:
		break;
	}

	if (!sta && ra && !is_multicast_ether_addr(ra))
		sta = sta_info_get(sdata, ra);

	ret = __ieee80211_select_queue(sdata, sta, skb);

	rcu_read_unlock();
	return ret;
}

/**
 * ieee80211_set_qos_hdr - Fill in the QoS header if there is one.
 *
 * @sdata: local subif
 * @skb: packet to be updated
 */
void ieee80211_set_qos_hdr(struct ieee80211_sub_if_data *sdata,
			   struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	u8 flags;
	u8 *p;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	p = ieee80211_get_qos_ctl(hdr);

	/* don't overwrite the QoS field of injected frames */
	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		/* do take into account Ack policy of injected frames */
		if (*p & IEEE80211_QOS_CTL_ACK_POLICY_NOACK)
			info->flags |= IEEE80211_TX_CTL_NO_ACK;
		return;
	}

	/* set up the first byte */

	/*
	 * preserve everything but the TID and ACK policy
	 * (which we both write here)
	 */
	flags = *p & ~(IEEE80211_QOS_CTL_TID_MASK |
		       IEEE80211_QOS_CTL_ACK_POLICY_MASK);

	if (is_multicast_ether_addr(hdr->addr1) ||
	    sdata->noack_map & BIT(tid)) {
		flags |= IEEE80211_QOS_CTL_ACK_POLICY_NOACK;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	}

	*p = flags | tid;

	/* set up the second byte */
	p++;

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		/* preserve RSPI and Mesh PS Level bit */
		*p &= ((IEEE80211_QOS_CTL_RSPI |
			IEEE80211_QOS_CTL_MESH_PS_LEVEL) >> 8);

		/* Nulls don't have a mesh header (frame body) */
		if (!ieee80211_is_qos_nullfunc(hdr->frame_control))
			*p |= (IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT >> 8);
	} else {
		*p = 0;
	}
}
