// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - routing
 * implementation.
 *
 * This is currently based on a simple routing table, with no dst cache. The
 * number of routes should stay fairly small, so the lookup cost is small.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/idr.h>
#include <linux/mctp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>

#include <uapi/linux/if_arp.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>

/* route output callbacks */
static int mctp_route_discard(struct mctp_route *route, struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

static int mctp_route_input(struct mctp_route *route, struct sk_buff *skb)
{
	/* -> to local stack */
	/* TODO: socket lookup, reassemble */
	kfree_skb(skb);
	return 0;
}

static int __always_unused mctp_route_output(struct mctp_route *route,
					     struct sk_buff *skb)
{
	unsigned int mtu;
	int rc;

	skb->protocol = htons(ETH_P_MCTP);

	mtu = READ_ONCE(skb->dev->mtu);
	if (skb->len > mtu) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	/* TODO: daddr (from rt->neigh), saddr (from device?)  */
	rc = dev_hard_header(skb, skb->dev, ntohs(skb->protocol),
			     NULL, NULL, skb->len);
	if (rc) {
		kfree_skb(skb);
		return -EHOSTUNREACH;
	}

	rc = dev_queue_xmit(skb);
	if (rc)
		rc = net_xmit_errno(rc);

	return rc;
}

/* route alloc/release */
static void mctp_route_release(struct mctp_route *rt)
{
	if (refcount_dec_and_test(&rt->refs)) {
		dev_put(rt->dev->dev);
		kfree_rcu(rt, rcu);
	}
}

/* returns a route with the refcount at 1 */
static struct mctp_route *mctp_route_alloc(void)
{
	struct mctp_route *rt;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return NULL;

	INIT_LIST_HEAD(&rt->list);
	refcount_set(&rt->refs, 1);
	rt->output = mctp_route_discard;

	return rt;
}

/* routing lookups */
static bool mctp_rt_match_eid(struct mctp_route *rt,
			      unsigned int net, mctp_eid_t eid)
{
	return READ_ONCE(rt->dev->net) == net &&
		rt->min <= eid && rt->max >= eid;
}

/* compares match, used for duplicate prevention */
static bool mctp_rt_compare_exact(struct mctp_route *rt1,
				  struct mctp_route *rt2)
{
	ASSERT_RTNL();
	return rt1->dev->net == rt2->dev->net &&
		rt1->min == rt2->min &&
		rt1->max == rt2->max;
}

struct mctp_route *mctp_route_lookup(struct net *net, unsigned int dnet,
				     mctp_eid_t daddr)
{
	struct mctp_route *tmp, *rt = NULL;

	list_for_each_entry_rcu(tmp, &net->mctp.routes, list) {
		/* TODO: add metrics */
		if (mctp_rt_match_eid(tmp, dnet, daddr)) {
			if (refcount_inc_not_zero(&tmp->refs)) {
				rt = tmp;
				break;
			}
		}
	}

	return rt;
}

/* sends a skb to rt and releases the route. */
int mctp_do_route(struct mctp_route *rt, struct sk_buff *skb)
{
	int rc;

	rc = rt->output(rt, skb);
	mctp_route_release(rt);
	return rc;
}

int mctp_local_output(struct sock *sk, struct mctp_route *rt,
		      struct sk_buff *skb, mctp_eid_t daddr, u8 req_tag)
{
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_hdr *hdr;
	unsigned long flags;
	mctp_eid_t saddr;
	int rc;

	if (WARN_ON(!rt->dev))
		return -EINVAL;

	spin_lock_irqsave(&rt->dev->addrs_lock, flags);
	if (rt->dev->num_addrs == 0) {
		rc = -EHOSTUNREACH;
	} else {
		/* use the outbound interface's first address as our source */
		saddr = rt->dev->addrs[0];
		rc = 0;
	}
	spin_unlock_irqrestore(&rt->dev->addrs_lock, flags);

	if (rc)
		return rc;

	/* TODO: we have the route MTU here; packetise */

	skb_reset_transport_header(skb);
	skb_push(skb, sizeof(struct mctp_hdr));
	skb_reset_network_header(skb);
	hdr = mctp_hdr(skb);
	hdr->ver = 1;
	hdr->dest = daddr;
	hdr->src = saddr;
	hdr->flags_seq_tag = MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM; /* TODO */

	skb->protocol = htons(ETH_P_MCTP);
	skb->priority = 0;

	/* cb->net will have been set on initial ingress */
	cb->src = saddr;

	return mctp_do_route(rt, skb);
}

/* route management */
int mctp_route_add_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *ert;

	rt = mctp_route_alloc();
	if (!rt)
		return -ENOMEM;

	rt->min = addr;
	rt->max = addr;
	rt->dev = mdev;
	dev_hold(rt->dev->dev);
	rt->output = mctp_route_input;

	ASSERT_RTNL();
	/* Prevent duplicate identical routes. */
	list_for_each_entry(ert, &net->mctp.routes, list) {
		if (mctp_rt_compare_exact(rt, ert)) {
			mctp_route_release(rt);
			return -EEXIST;
		}
	}

	list_add_rcu(&rt->list, &net->mctp.routes);

	return 0;
}

int mctp_route_remove_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *tmp;

	ASSERT_RTNL();

	list_for_each_entry_safe(rt, tmp, &net->mctp.routes, list) {
		if (rt->dev == mdev && rt->min == addr && rt->max == addr) {
			list_del_rcu(&rt->list);
			/* TODO: immediate RTM_DELROUTE */
			mctp_route_release(rt);
		}
	}

	return 0;
}

/* removes all entries for a given device */
void mctp_route_remove_dev(struct mctp_dev *mdev)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *tmp;

	ASSERT_RTNL();
	list_for_each_entry_safe(rt, tmp, &net->mctp.routes, list) {
		if (rt->dev == mdev) {
			list_del_rcu(&rt->list);
			/* TODO: immediate RTM_DELROUTE */
			mctp_route_release(rt);
		}
	}
}

/* Incoming packet-handling */

static int mctp_pkttype_receive(struct sk_buff *skb, struct net_device *dev,
				struct packet_type *pt,
				struct net_device *orig_dev)
{
	struct net *net = dev_net(dev);
	struct mctp_skb_cb *cb;
	struct mctp_route *rt;
	struct mctp_hdr *mh;

	/* basic non-data sanity checks */
	if (dev->type != ARPHRD_MCTP)
		goto err_drop;

	if (!pskb_may_pull(skb, sizeof(struct mctp_hdr)))
		goto err_drop;

	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);

	/* We have enough for a header; decode and route */
	mh = mctp_hdr(skb);
	if (mh->ver < MCTP_VER_MIN || mh->ver > MCTP_VER_MAX)
		goto err_drop;

	cb = __mctp_cb(skb);
	rcu_read_lock();
	cb->net = READ_ONCE(__mctp_dev_get(dev)->net);
	rcu_read_unlock();

	rt = mctp_route_lookup(net, cb->net, mh->dest);
	if (!rt)
		goto err_drop;

	mctp_do_route(rt, skb);

	return NET_RX_SUCCESS;

err_drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type mctp_packet_type = {
	.type = cpu_to_be16(ETH_P_MCTP),
	.func = mctp_pkttype_receive,
};

/* net namespace implementation */
static int __net_init mctp_routes_net_init(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;

	INIT_LIST_HEAD(&ns->routes);
	return 0;
}

static void __net_exit mctp_routes_net_exit(struct net *net)
{
	struct mctp_route *rt;

	list_for_each_entry_rcu(rt, &net->mctp.routes, list)
		mctp_route_release(rt);
}

static struct pernet_operations mctp_net_ops = {
	.init = mctp_routes_net_init,
	.exit = mctp_routes_net_exit,
};

int __init mctp_routes_init(void)
{
	dev_add_pack(&mctp_packet_type);
	return register_pernet_subsys(&mctp_net_ops);
}

void __exit mctp_routes_exit(void)
{
	unregister_pernet_subsys(&mctp_net_ops);
	dev_remove_pack(&mctp_packet_type);
}
