/*
 * IEEE 802.11 driver (80211.o) - QoS datatypes
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WME_H
#define _WME_H

#include <linux/netdevice.h>
#include "ieee80211_i.h"

#define QOS_CONTROL_LEN 2

#define QOS_CONTROL_ACK_POLICY_NORMAL 0
#define QOS_CONTROL_ACK_POLICY_NOACK 1

#define QOS_CONTROL_TID_MASK 0x0f
#define QOS_CONTROL_ACK_POLICY_SHIFT 5

#define QOS_CONTROL_TAG1D_MASK 0x07

extern const int ieee802_1d_to_ac[8];

static inline int WLAN_FC_IS_QOS_DATA(u16 fc)
{
	return (fc & 0x8C) == 0x88;
}

#ifdef CONFIG_NET_SCHED
void ieee80211_install_qdisc(struct net_device *dev);
int ieee80211_qdisc_installed(struct net_device *dev);
int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
			       struct sta_info *sta, u16 tid);
void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
				   struct sta_info *sta, u16 tid,
				   u8 requeue);
void ieee80211_requeue(struct ieee80211_local *local, int queue);
int ieee80211_wme_register(void);
void ieee80211_wme_unregister(void);
#else
static inline void ieee80211_install_qdisc(struct net_device *dev)
{
}
static inline int ieee80211_qdisc_installed(struct net_device *dev)
{
	return 0;
}
static inline int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
					     struct sta_info *sta, u16 tid)
{
	return -EAGAIN;
}
static inline void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
						 struct sta_info *sta, u16 tid,
						 u8 requeue)
{
}
static inline void ieee80211_requeue(struct ieee80211_local *local, int queue)
{
}
static inline int ieee80211_wme_register(void)
{
	return 0;
}
static inline void ieee80211_wme_unregister(void)
{
}
#endif /* CONFIG_NET_SCHED */

#endif /* _WME_H */
