/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

/* Included by drivers/net/dsa/sja1105/sja1105.h and net/dsa/tag_sja1105.c */

#ifndef _NET_DSA_SJA1105_H
#define _NET_DSA_SJA1105_H

#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>

#define ETH_P_SJA1105				ETH_P_DSA_8021Q
#define ETH_P_SJA1105_META			0x0008
#define ETH_P_SJA1110				0xdadc

#define SJA1105_DEFAULT_VLAN			(VLAN_N_VID - 1)

/* IEEE 802.3 Annex 57A: Slow Protocols PDUs (01:80:C2:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_A		0x0180C2000000ull
#define SJA1105_LINKLOCAL_FILTER_A_MASK		0xFFFFFF000000ull
/* IEEE 1588 Annex F: Transport of PTP over Ethernet (01:1B:19:xx:xx:xx) */
#define SJA1105_LINKLOCAL_FILTER_B		0x011B19000000ull
#define SJA1105_LINKLOCAL_FILTER_B_MASK		0xFFFFFF000000ull

/* Source and Destination MAC of follow-up meta frames.
 * Whereas the choice of SMAC only affects the unique identification of the
 * switch as sender of meta frames, the DMAC must be an address that is present
 * in the DSA conduit port's multicast MAC filter.
 * 01-80-C2-00-00-0E is a good choice for this, as all profiles of IEEE 1588
 * over L2 use this address for some purpose already.
 */
#define SJA1105_META_SMAC			0x222222222222ull
#define SJA1105_META_DMAC			0x0180C200000Eull

enum sja1110_meta_tstamp {
	SJA1110_META_TSTAMP_TX = 0,
	SJA1110_META_TSTAMP_RX = 1,
};

struct sja1105_deferred_xmit_work {
	struct dsa_port *dp;
	struct sk_buff *skb;
	struct kthread_work work;
};

/* Global tagger data */
struct sja1105_tagger_data {
	void (*xmit_work_fn)(struct kthread_work *work);
	void (*meta_tstamp_handler)(struct dsa_switch *ds, int port, u8 ts_id,
				    enum sja1110_meta_tstamp dir, u64 tstamp);
};

struct sja1105_skb_cb {
	struct sk_buff *clone;
	u64 tstamp;
	/* Only valid for packets cloned for 2-step TX timestamping */
	u8 ts_id;
};

#define SJA1105_SKB_CB(skb) \
	((struct sja1105_skb_cb *)((skb)->cb))

static inline struct sja1105_tagger_data *
sja1105_tagger_data(struct dsa_switch *ds)
{
	BUG_ON(ds->dst->tag_ops->proto != DSA_TAG_PROTO_SJA1105 &&
	       ds->dst->tag_ops->proto != DSA_TAG_PROTO_SJA1110);

	return ds->tagger_data;
}

#endif /* _NET_DSA_SJA1105_H */
