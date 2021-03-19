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
#include <net/sock.h>

void can_flush_echo_skb(struct net_device *dev);
int can_put_echo_skb(struct sk_buff *skb, struct net_device *dev,
		     unsigned int idx, unsigned int frame_len);
struct sk_buff *__can_get_echo_skb(struct net_device *dev, unsigned int idx,
				   u8 *len_ptr, unsigned int *frame_len_ptr);
unsigned int can_get_echo_skb(struct net_device *dev, unsigned int idx,
			      unsigned int *frame_len_ptr);
void can_free_echo_skb(struct net_device *dev, unsigned int idx);
struct sk_buff *alloc_can_skb(struct net_device *dev, struct can_frame **cf);
struct sk_buff *alloc_canfd_skb(struct net_device *dev,
				struct canfd_frame **cfd);
struct sk_buff *alloc_can_err_skb(struct net_device *dev,
				  struct can_frame **cf);

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
 * @skbcnt:	atomic counter to have an unique id together with skb pointer
 * @frame_len:	length of CAN frame in data link layer
 * @cf:		align to the following CAN frame at skb->data
 */
struct can_skb_priv {
	int ifindex;
	int skbcnt;
	unsigned int frame_len;
	struct can_frame cf[];
};

static inline struct can_skb_priv *can_skb_prv(struct sk_buff *skb)
{
	return (struct can_skb_priv *)(skb->head);
}

static inline void can_skb_reserve(struct sk_buff *skb)
{
	skb_reserve(skb, sizeof(struct can_skb_priv));
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

/* Check for outgoing skbs that have not been created by the CAN subsystem */
static inline bool can_skb_headroom_valid(struct net_device *dev,
					  struct sk_buff *skb)
{
	/* af_packet creates a headroom of HH_DATA_MOD bytes which is fine */
	if (WARN_ON_ONCE(skb_headroom(skb) < sizeof(struct can_skb_priv)))
		return false;

	/* af_packet does not apply CAN skb specific settings */
	if (skb->ip_summed == CHECKSUM_NONE) {
		/* init headroom */
		can_skb_prv(skb)->ifindex = dev->ifindex;
		can_skb_prv(skb)->skbcnt = 0;

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* perform proper loopback on capable devices */
		if (dev->flags & IFF_ECHO)
			skb->pkt_type = PACKET_LOOPBACK;
		else
			skb->pkt_type = PACKET_HOST;

		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
	}

	return true;
}

/* Drop a given socketbuffer if it does not contain a valid CAN frame. */
static inline bool can_dropped_invalid_skb(struct net_device *dev,
					  struct sk_buff *skb)
{
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

	if (skb->protocol == htons(ETH_P_CAN)) {
		if (unlikely(skb->len != CAN_MTU ||
			     cfd->len > CAN_MAX_DLEN))
			goto inval_skb;
	} else if (skb->protocol == htons(ETH_P_CANFD)) {
		if (unlikely(skb->len != CANFD_MTU ||
			     cfd->len > CANFD_MAX_DLEN))
			goto inval_skb;
	} else
		goto inval_skb;

	if (!can_skb_headroom_valid(dev, skb))
		goto inval_skb;

	return false;

inval_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return true;
}

static inline bool can_is_canfd_skb(const struct sk_buff *skb)
{
	/* the CAN specific type of skb is identified by its data length */
	return skb->len == CANFD_MTU;
}

#endif /* !_CAN_SKB_H */
