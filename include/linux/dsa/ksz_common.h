/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip switch tag common header
 *
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#ifndef _NET_DSA_KSZ_COMMON_H_
#define _NET_DSA_KSZ_COMMON_H_

#include <net/dsa.h>

/* All time stamps from the KSZ consist of 2 bits for seconds and 30 bits for
 * nanoseconds. This is NOT the same as 32 bits for nanoseconds.
 */
#define KSZ_TSTAMP_SEC_MASK  GENMASK(31, 30)
#define KSZ_TSTAMP_NSEC_MASK GENMASK(29, 0)

static inline ktime_t ksz_decode_tstamp(u32 tstamp)
{
	u64 ns = FIELD_GET(KSZ_TSTAMP_SEC_MASK, tstamp) * NSEC_PER_SEC +
		 FIELD_GET(KSZ_TSTAMP_NSEC_MASK, tstamp);

	return ns_to_ktime(ns);
}

struct ksz_deferred_xmit_work {
	struct dsa_port *dp;
	struct sk_buff *skb;
	struct kthread_work work;
};

struct ksz_tagger_data {
	void (*xmit_work_fn)(struct kthread_work *work);
	void (*hwtstamp_set_state)(struct dsa_switch *ds, bool on);
};

struct ksz_skb_cb {
	struct sk_buff *clone;
	unsigned int ptp_type;
	bool update_correction;
	u32 tstamp;
};

#define KSZ_SKB_CB(skb) \
	((struct ksz_skb_cb *)((skb)->cb))

static inline struct ksz_tagger_data *
ksz_tagger_data(struct dsa_switch *ds)
{
	return ds->tagger_data;
}

#endif /* _NET_DSA_KSZ_COMMON_H_ */
