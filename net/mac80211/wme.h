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

#define QOS_CONTROL_ACK_POLICY_SHIFT 5

extern const int ieee802_1d_to_ac[8];

u16 ieee80211_select_queue(struct net_device *dev, struct sk_buff *skb);
int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
			       struct sta_info *sta, u16 tid);
void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
				   struct sta_info *sta, u16 tid,
				   u8 requeue);
void ieee80211_requeue(struct ieee80211_local *local, int queue);

#endif /* _WME_H */
