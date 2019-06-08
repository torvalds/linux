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
#define ETH_P_SJA1105_META			0x0008

/* IEEE 802.3 Annex 57A: Slow Protocols PDUs (01:80:C2:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_A		0x0180C2000000ull
#define SJA1105_LINKLOCAL_FILTER_A_MASK		0xFFFFFF000000ull
/* IEEE 1588 Annex F: Transport of PTP over Ethernet (01:1B:19:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_B		0x011B19000000ull
#define SJA1105_LINKLOCAL_FILTER_B_MASK		0xFFFFFF000000ull

/* Source and Destination MAC of follow-up meta frames.
 * Whereas the choice of SMAC only affects the unique identification of the
 * switch as sender of meta frames, the DMAC must be an address that is present
 * in the DSA master port's multicast MAC filter.
 * 01-80-C2-00-00-0E is a good choice for this, as all profiles of IEEE 1588
 * over L2 use this address for some purpose already.
 */
#define SJA1105_META_SMAC			0x222222222222ull
#define SJA1105_META_DMAC			0x0180C200000Eull

/* Global tagger data: each struct sja1105_port has a reference to
 * the structure defined in struct sja1105_private.
 */
struct sja1105_tagger_data {
	struct sk_buff_head skb_rxtstamp_queue;
	struct work_struct rxtstamp_work;
	struct sk_buff *stampable_skb;
	/* Protects concurrent access to the meta state machine
	 * from taggers running on multiple ports on SMP systems
	 */
	spinlock_t meta_lock;
	bool hwts_rx_en;
};

struct sja1105_skb_cb {
	u32 meta_tstamp;
};

#define SJA1105_SKB_CB(skb) \
	((struct sja1105_skb_cb *)DSA_SKB_CB_PRIV(skb))

struct sja1105_port {
	struct sja1105_tagger_data *data;
	struct dsa_port *dp;
	bool hwts_tx_en;
	int mgmt_slot;
};

#endif /* _NET_DSA_SJA1105_H */
