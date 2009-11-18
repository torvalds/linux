/*
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

#define QOS_CONTROL_ACK_POLICY_NORMAL 0
#define QOS_CONTROL_ACK_POLICY_NOACK 1

#define QOS_CONTROL_ACK_POLICY_SHIFT 5

extern const int ieee802_1d_to_ac[8];

void ieee80211_select_queue(struct ieee80211_local *local,
			    struct sk_buff *skb);

#endif /* _WME_H */
