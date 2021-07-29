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
#include <net/netlink.h>
#include <net/sock.h>

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

static int mctp_route_output(struct mctp_route *route, struct sk_buff *skb)
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
static int mctp_route_add(struct mctp_dev *mdev, mctp_eid_t daddr_start,
			  unsigned int daddr_extent, unsigned int mtu,
			  bool is_local)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *ert;

	if (!mctp_address_ok(daddr_start))
		return -EINVAL;

	if (daddr_extent > 0xff || daddr_start + daddr_extent >= 255)
		return -EINVAL;

	rt = mctp_route_alloc();
	if (!rt)
		return -ENOMEM;

	rt->min = daddr_start;
	rt->max = daddr_start + daddr_extent;
	rt->mtu = mtu;
	rt->dev = mdev;
	dev_hold(rt->dev->dev);
	rt->output = is_local ? mctp_route_input : mctp_route_output;

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

static int mctp_route_remove(struct mctp_dev *mdev, mctp_eid_t daddr_start,
			     unsigned int daddr_extent)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *tmp;
	mctp_eid_t daddr_end;
	bool dropped;

	if (daddr_extent > 0xff || daddr_start + daddr_extent >= 255)
		return -EINVAL;

	daddr_end = daddr_start + daddr_extent;
	dropped = false;

	ASSERT_RTNL();

	list_for_each_entry_safe(rt, tmp, &net->mctp.routes, list) {
		if (rt->dev == mdev &&
		    rt->min == daddr_start && rt->max == daddr_end) {
			list_del_rcu(&rt->list);
			/* TODO: immediate RTM_DELROUTE */
			mctp_route_release(rt);
			dropped = true;
		}
	}

	return dropped ? 0 : -ENOENT;
}

int mctp_route_add_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	return mctp_route_add(mdev, addr, 0, 0, true);
}

int mctp_route_remove_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	return mctp_route_remove(mdev, addr, 0);
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

/* netlink interface */

static const struct nla_policy rta_mctp_policy[RTA_MAX + 1] = {
	[RTA_DST]		= { .type = NLA_U8 },
	[RTA_METRICS]		= { .type = NLA_NESTED },
	[RTA_OIF]		= { .type = NLA_U32 },
};

/* Common part for RTM_NEWROUTE and RTM_DELROUTE parsing.
 * tb must hold RTA_MAX+1 elements.
 */
static int mctp_route_nlparse(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack,
			      struct nlattr **tb, struct rtmsg **rtm,
			      struct mctp_dev **mdev, mctp_eid_t *daddr_start)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	unsigned int ifindex;
	int rc;

	rc = nlmsg_parse(nlh, sizeof(struct rtmsg), tb, RTA_MAX,
			 rta_mctp_policy, extack);
	if (rc < 0) {
		NL_SET_ERR_MSG(extack, "incorrect format");
		return rc;
	}

	if (!tb[RTA_DST]) {
		NL_SET_ERR_MSG(extack, "dst EID missing");
		return -EINVAL;
	}
	*daddr_start = nla_get_u8(tb[RTA_DST]);

	if (!tb[RTA_OIF]) {
		NL_SET_ERR_MSG(extack, "ifindex missing");
		return -EINVAL;
	}
	ifindex = nla_get_u32(tb[RTA_OIF]);

	*rtm = nlmsg_data(nlh);
	if ((*rtm)->rtm_family != AF_MCTP) {
		NL_SET_ERR_MSG(extack, "route family must be AF_MCTP");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ifindex);
	if (!dev) {
		NL_SET_ERR_MSG(extack, "bad ifindex");
		return -ENODEV;
	}
	*mdev = mctp_dev_get_rtnl(dev);
	if (!*mdev)
		return -ENODEV;

	if (dev->flags & IFF_LOOPBACK) {
		NL_SET_ERR_MSG(extack, "no routes to loopback");
		return -EINVAL;
	}

	return 0;
}

static int mctp_newroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RTA_MAX + 1];
	mctp_eid_t daddr_start;
	struct mctp_dev *mdev;
	struct rtmsg *rtm;
	unsigned int mtu;
	int rc;

	rc = mctp_route_nlparse(skb, nlh, extack, tb,
				&rtm, &mdev, &daddr_start);
	if (rc < 0)
		return rc;

	if (rtm->rtm_type != RTN_UNICAST) {
		NL_SET_ERR_MSG(extack, "rtm_type must be RTN_UNICAST");
		return -EINVAL;
	}

	/* TODO: parse mtu from nlparse */
	mtu = 0;

	rc = mctp_route_add(mdev, daddr_start, rtm->rtm_dst_len, mtu, false);
	return rc;
}

static int mctp_delroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RTA_MAX + 1];
	mctp_eid_t daddr_start;
	struct mctp_dev *mdev;
	struct rtmsg *rtm;
	int rc;

	rc = mctp_route_nlparse(skb, nlh, extack, tb,
				&rtm, &mdev, &daddr_start);
	if (rc < 0)
		return rc;

	/* we only have unicast routes */
	if (rtm->rtm_type != RTN_UNICAST)
		return -EINVAL;

	rc = mctp_route_remove(mdev, daddr_start, rtm->rtm_dst_len);
	return rc;
}

static int mctp_fill_rtinfo(struct sk_buff *skb, struct mctp_route *rt,
			    u32 portid, u32 seq, int event, unsigned int flags)
{
	struct nlmsghdr *nlh;
	struct rtmsg *hdr;
	void *metrics;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*hdr), flags);
	if (!nlh)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->rtm_family = AF_MCTP;

	/* we use the _len fields as a number of EIDs, rather than
	 * a number of bits in the address
	 */
	hdr->rtm_dst_len = rt->max - rt->min;
	hdr->rtm_src_len = 0;
	hdr->rtm_tos = 0;
	hdr->rtm_table = RT_TABLE_DEFAULT;
	hdr->rtm_protocol = RTPROT_STATIC; /* everything is user-defined */
	hdr->rtm_scope = RT_SCOPE_LINK; /* TODO: scope in mctp_route? */
	hdr->rtm_type = RTN_ANYCAST; /* TODO: type from route */

	if (nla_put_u8(skb, RTA_DST, rt->min))
		goto cancel;

	metrics = nla_nest_start_noflag(skb, RTA_METRICS);
	if (!metrics)
		goto cancel;

	if (rt->mtu) {
		if (nla_put_u32(skb, RTAX_MTU, rt->mtu))
			goto cancel;
	}

	nla_nest_end(skb, metrics);

	if (rt->dev) {
		if (nla_put_u32(skb, RTA_OIF, rt->dev->dev->ifindex))
			goto cancel;
	}

	/* TODO: conditional neighbour physaddr? */

	nlmsg_end(skb, nlh);

	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mctp_dump_rtinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct mctp_route *rt;
	int s_idx, idx;

	/* TODO: allow filtering on route data, possibly under
	 * cb->strict_check
	 */

	/* TODO: change to struct overlay */
	s_idx = cb->args[0];
	idx = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(rt, &net->mctp.routes, list) {
		if (idx++ < s_idx)
			continue;
		if (mctp_fill_rtinfo(skb, rt,
				     NETLINK_CB(cb->skb).portid,
				     cb->nlh->nlmsg_seq,
				     RTM_NEWROUTE, NLM_F_MULTI) < 0)
			break;
	}

	rcu_read_unlock();
	cb->args[0] = idx;

	return skb->len;
}

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

	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_GETROUTE,
			     NULL, mctp_dump_rtinfo, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_NEWROUTE,
			     mctp_newroute, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_DELROUTE,
			     mctp_delroute, NULL, 0);

	return register_pernet_subsys(&mctp_net_ops);
}

void __exit mctp_routes_exit(void)
{
	unregister_pernet_subsys(&mctp_net_ops);
	rtnl_unregister(PF_MCTP, RTM_DELROUTE);
	rtnl_unregister(PF_MCTP, RTM_NEWROUTE);
	rtnl_unregister(PF_MCTP, RTM_GETROUTE);
	dev_remove_pack(&mctp_packet_type);
}
