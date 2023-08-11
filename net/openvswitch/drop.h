/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenvSwitch drop reason list.
 */

#ifndef OPENVSWITCH_DROP_H
#define OPENVSWITCH_DROP_H
#include <linux/skbuff.h>
#include <net/dropreason.h>

#define OVS_DROP_REASONS(R)			\
	R(OVS_DROP_LAST_ACTION)		        \
	R(OVS_DROP_ACTION_ERROR)		\
	/* deliberate comment for trailing \ */

enum ovs_drop_reason {
	__OVS_DROP_REASON = SKB_DROP_REASON_SUBSYS_OPENVSWITCH <<
				SKB_DROP_REASON_SUBSYS_SHIFT,
#define ENUM(x) x,
	OVS_DROP_REASONS(ENUM)
#undef ENUM

	OVS_DROP_MAX,
};

static inline void
ovs_kfree_skb_reason(struct sk_buff *skb, enum ovs_drop_reason reason)
{
	kfree_skb_reason(skb, (u32)reason);
}

#endif /* OPENVSWITCH_DROP_H */
