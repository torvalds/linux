/*
 * linux/can/skb.h
 *
 * Definitions for the CAN network socket buffer
 *
 * Copyright (C) 2012 Oliver Hartkopp <socketcan@hartkopp.net>
 *
 */

#ifndef CAN_SKB_H
#define CAN_SKB_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <net/sock.h>

/*
 * The struct can_skb_priv is used to transport additional information along
 * with the stored struct can(fd)_frame that can not be contained in existing
 * struct sk_buff elements.
 * N.B. that this information must not be modified in cloned CAN sk_buffs.
 * To modify the CAN frame content or the struct can_skb_priv content
 * skb_copy() needs to be used instead of skb_clone().
 */

/**
 * struct can_skb_priv - private additional data inside CAN sk_buffs
 * @ifindex:	ifindex of the first interface the CAN frame appeared on
 * @cf:		align to the following CAN frame at skb->data
 */
struct can_skb_priv {
	int ifindex;
	struct can_frame cf[0];
};

static inline struct can_skb_priv *can_skb_prv(struct sk_buff *skb)
{
	return (struct can_skb_priv *)(skb->head);
}

static inline void can_skb_reserve(struct sk_buff *skb)
{
	skb_reserve(skb, sizeof(struct can_skb_priv));
}

static inline void can_skb_destructor(struct sk_buff *skb)
{
	sock_put(skb->sk);
}

static inline void can_skb_set_owner(struct sk_buff *skb, struct sock *sk)
{
	if (sk) {
		sock_hold(sk);
		skb->destructor = can_skb_destructor;
		skb->sk = sk;
	}
}

/*
 * returns an unshared skb owned by the original sock to be echo'ed back
 */
static inline struct sk_buff *can_create_echo_skb(struct sk_buff *skb)
{
	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);

		if (likely(nskb)) {
			can_skb_set_owner(nskb, skb->sk);
			consume_skb(skb);
			return nskb;
		} else {
			kfree_skb(skb);
			return NULL;
		}
	}

	/* we can assume to have an unshared skb with proper owner */
	return skb;
}

#endif /* CAN_SKB_H */
