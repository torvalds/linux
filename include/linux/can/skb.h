/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * linux/can/skb.h
 *
 * Definitions for the CAN network socket buffer
 *
 * Copyright (C) 2012 Oliver Hartkopp <socketcan@hartkopp.net>
 *
 */

#ifndef _CAN_SKB_H
#define _CAN_SKB_H

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <net/can.h>
#include <net/sock.h>

void can_flush_echo_skb(struct net_device *dev);
int can_put_echo_skb(struct sk_buff *skb, struct net_device *dev,
		     unsigned int idx, unsigned int frame_len);
struct sk_buff *__can_get_echo_skb(struct net_device *dev, unsigned int idx,
				   unsigned int *len_ptr,
				   unsigned int *frame_len_ptr);
unsigned int __must_check can_get_echo_skb(struct net_device *dev,
					   unsigned int idx,
					   unsigned int *frame_len_ptr);
void can_free_echo_skb(struct net_device *dev, unsigned int idx,
		       unsigned int *frame_len_ptr);
struct sk_buff *alloc_can_skb(struct net_device *dev, struct can_frame **cf);
struct sk_buff *alloc_canfd_skb(struct net_device *dev,
				struct canfd_frame **cfd);
struct sk_buff *alloc_canxl_skb(struct net_device *dev,
				struct canxl_frame **cxl,
				unsigned int data_len);
struct sk_buff *alloc_can_err_skb(struct net_device *dev,
				  struct can_frame **cf);
bool can_dropped_invalid_skb(struct net_device *dev, struct sk_buff *skb);

static inline struct can_skb_ext *can_skb_ext_add(struct sk_buff *skb)
{
	struct can_skb_ext *csx = skb_ext_add(skb, SKB_EXT_CAN);

	/* skb_ext_add() returns uninitialized space */
	if (csx)
		csx->can_gw_hops = 0;

	return csx;
}

static inline struct can_skb_ext *can_skb_ext_find(struct sk_buff *skb)
{
	return skb_ext_find(skb, SKB_EXT_CAN);
}

static inline void can_skb_set_owner(struct sk_buff *skb, struct sock *sk)
{
	/* If the socket has already been closed by user space, the
	 * refcount may already be 0 (and the socket will be freed
	 * after the last TX skb has been freed). So only increase
	 * socket refcount if the refcount is > 0.
	 */
	if (sk && refcount_inc_not_zero(&sk->sk_refcnt)) {
		skb->destructor = sock_efree;
		skb->sk = sk;
	}
}

/*
 * returns an unshared skb owned by the original sock to be echo'ed back
 */
static inline struct sk_buff *can_create_echo_skb(struct sk_buff *skb)
{
	struct sk_buff *nskb;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (unlikely(!nskb)) {
		kfree_skb(skb);
		return NULL;
	}

	can_skb_set_owner(nskb, skb->sk);
	consume_skb(skb);
	return nskb;
}

static inline bool can_is_can_skb(const struct sk_buff *skb)
{
	struct can_frame *cf = (struct can_frame *)skb->data;

	/* the CAN specific type of skb is identified by its data length */
	return (skb->len == CAN_MTU && cf->len <= CAN_MAX_DLEN);
}

static inline bool can_is_canfd_skb(const struct sk_buff *skb)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

	/* the CAN specific type of skb is identified by its data length */
	return (skb->len == CANFD_MTU && cfd->len <= CANFD_MAX_DLEN);
}

static inline bool can_is_canxl_skb(const struct sk_buff *skb)
{
	const struct canxl_frame *cxl = (struct canxl_frame *)skb->data;

	if (skb->len < CANXL_HDR_SIZE + CANXL_MIN_DLEN || skb->len > CANXL_MTU)
		return false;

	/* this also checks valid CAN XL data length boundaries */
	if (skb->len != CANXL_HDR_SIZE + cxl->len)
		return false;

	return cxl->flags & CANXL_XLF;
}

/* get length element value from can[|fd|xl]_frame structure */
static inline unsigned int can_skb_get_len_val(struct sk_buff *skb)
{
	const struct canxl_frame *cxl = (struct canxl_frame *)skb->data;
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

	if (can_is_canxl_skb(skb))
		return cxl->len;

	return cfd->len;
}

/* get needed data length inside CAN frame for all frame types (RTR aware) */
static inline unsigned int can_skb_get_data_len(struct sk_buff *skb)
{
	unsigned int len = can_skb_get_len_val(skb);
	const struct can_frame *cf = (struct can_frame *)skb->data;

	/* RTR frames have an actual length of zero */
	if (can_is_can_skb(skb) && cf->can_id & CAN_RTR_FLAG)
		return 0;

	return len;
}

#endif /* !_CAN_SKB_H */
