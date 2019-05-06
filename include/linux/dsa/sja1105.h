/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

/* Included by drivers/net/dsa/sja1105/sja1105.h and net/dsa/tag_sja1105.c */

#ifndef _NET_DSA_SJA1105_H
#define _NET_DSA_SJA1105_H

#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/dsa.h>

#define ETH_P_SJA1105				ETH_P_DSA_8021Q

/* IEEE 802.3 Annex 57A: Slow Protocols PDUs (01:80:C2:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_A		0x0180C2000000ull
#define SJA1105_LINKLOCAL_FILTER_A_MASK		0xFFFFFF000000ull
/* IEEE 1588 Annex F: Transport of PTP over Ethernet (01:1B:19:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_B		0x011B19000000ull
#define SJA1105_LINKLOCAL_FILTER_B_MASK		0xFFFFFF000000ull

enum sja1105_frame_type {
	SJA1105_FRAME_TYPE_NORMAL = 0,
	SJA1105_FRAME_TYPE_LINK_LOCAL,
};

struct sja1105_skb_cb {
	enum sja1105_frame_type type;
};

#define SJA1105_SKB_CB(skb) \
	((struct sja1105_skb_cb *)DSA_SKB_CB_PRIV(skb))

struct sja1105_port {
	struct dsa_port *dp;
	int mgmt_slot;
};

#endif /* _NET_DSA_SJA1105_H */
