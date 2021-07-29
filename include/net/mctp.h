/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Management Component Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#ifndef __NET_MCTP_H
#define __NET_MCTP_H

#include <linux/bits.h>
#include <linux/mctp.h>
#include <net/net_namespace.h>

/* MCTP packet definitions */
struct mctp_hdr {
	u8	ver;
	u8	dest;
	u8	src;
	u8	flags_seq_tag;
};

#define MCTP_VER_MIN	1
#define MCTP_VER_MAX	1

/* Definitions for flags_seq_tag field */
#define MCTP_HDR_FLAG_SOM	BIT(7)
#define MCTP_HDR_FLAG_EOM	BIT(6)
#define MCTP_HDR_FLAG_TO	BIT(3)
#define MCTP_HDR_FLAGS		GENMASK(5, 3)
#define MCTP_HDR_SEQ_SHIFT	4
#define MCTP_HDR_SEQ_MASK	GENMASK(1, 0)
#define MCTP_HDR_TAG_SHIFT	0
#define MCTP_HDR_TAG_MASK	GENMASK(2, 0)

#define MCTP_HEADER_MAXLEN	4

static inline bool mctp_address_ok(mctp_eid_t eid)
{
	return eid >= 8 && eid < 255;
}

static inline struct mctp_hdr *mctp_hdr(struct sk_buff *skb)
{
	return (struct mctp_hdr *)skb_network_header(skb);
}

struct mctp_skb_cb {
	unsigned int	magic;
	unsigned int	net;
	mctp_eid_t	src;
};

/* skb control-block accessors with a little extra debugging for initial
 * development.
 *
 * TODO: remove checks & mctp_skb_cb->magic; replace callers of __mctp_cb
 * with mctp_cb().
 *
 * __mctp_cb() is only for the initial ingress code; we should see ->magic set
 * at all times after this.
 */
static inline struct mctp_skb_cb *__mctp_cb(struct sk_buff *skb)
{
	struct mctp_skb_cb *cb = (void *)skb->cb;

	cb->magic = 0x4d435450;
	return cb;
}

static inline struct mctp_skb_cb *mctp_cb(struct sk_buff *skb)
{
	struct mctp_skb_cb *cb = (void *)skb->cb;

	WARN_ON(cb->magic != 0x4d435450);
	return (void *)(skb->cb);
}

/* Route definition.
 *
 * These are held in the pernet->mctp.routes list, with RCU protection for
 * removed routes. We hold a reference to the netdev; routes need to be
 * dropped on NETDEV_UNREGISTER events.
 *
 * Updates to the route table are performed under rtnl; all reads under RCU,
 * so routes cannot be referenced over a RCU grace period. Specifically: A
 * caller cannot block between mctp_route_lookup and passing the route to
 * mctp_do_route.
 */
struct mctp_route {
	mctp_eid_t		min, max;

	struct mctp_dev		*dev;
	unsigned int		mtu;
	int			(*output)(struct mctp_route *route,
					  struct sk_buff *skb);

	struct list_head	list;
	refcount_t		refs;
	struct rcu_head		rcu;
};

/* route interfaces */
struct mctp_route *mctp_route_lookup(struct net *net, unsigned int dnet,
				     mctp_eid_t daddr);

int mctp_do_route(struct mctp_route *rt, struct sk_buff *skb);

int mctp_local_output(struct sock *sk, struct mctp_route *rt,
		      struct sk_buff *skb, mctp_eid_t daddr, u8 req_tag);

/* routing <--> device interface */
int mctp_route_add_local(struct mctp_dev *mdev, mctp_eid_t addr);
int mctp_route_remove_local(struct mctp_dev *mdev, mctp_eid_t addr);
void mctp_route_remove_dev(struct mctp_dev *mdev);

int mctp_routes_init(void);
void mctp_routes_exit(void);

void mctp_device_init(void);
void mctp_device_exit(void);

#endif /* __NET_MCTP_H */
