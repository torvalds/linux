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

static struct mctp_sock *mctp_lookup_bind(struct net *net, struct sk_buff *skb)
{
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_hdr *mh;
	struct sock *sk;
	u8 type;

	WARN_ON(!rcu_read_lock_held());

	/* TODO: look up in skb->cb? */
	mh = mctp_hdr(skb);

	if (!skb_headlen(skb))
		return NULL;

	type = (*(u8 *)skb->data) & 0x7f;

	sk_for_each_rcu(sk, &net->mctp.binds) {
		struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);

		if (msk->bind_net != MCTP_NET_ANY && msk->bind_net != cb->net)
			continue;

		if (msk->bind_type != type)
			continue;

		if (msk->bind_addr != MCTP_ADDR_ANY &&
		    msk->bind_addr != mh->dest)
			continue;

		return msk;
	}

	return NULL;
}

static bool mctp_key_match(struct mctp_sk_key *key, mctp_eid_t local,
			   mctp_eid_t peer, u8 tag)
{
	if (key->local_addr != local)
		return false;

	if (key->peer_addr != peer)
		return false;

	if (key->tag != tag)
		return false;

	return true;
}

static struct mctp_sk_key *mctp_lookup_key(struct net *net, struct sk_buff *skb,
					   mctp_eid_t peer)
{
	struct mctp_sk_key *key, *ret;
	struct mctp_hdr *mh;
	u8 tag;

	WARN_ON(!rcu_read_lock_held());

	mh = mctp_hdr(skb);
	tag = mh->flags_seq_tag & (MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO);

	ret = NULL;

	hlist_for_each_entry_rcu(key, &net->mctp.keys, hlist) {
		if (mctp_key_match(key, mh->dest, peer, tag)) {
			ret = key;
			break;
		}
	}

	return ret;
}

static int mctp_route_input(struct mctp_route *route, struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	struct mctp_sk_key *key;
	struct mctp_sock *msk;
	struct mctp_hdr *mh;

	msk = NULL;

	/* we may be receiving a locally-routed packet; drop source sk
	 * accounting
	 */
	skb_orphan(skb);

	/* ensure we have enough data for a header and a type */
	if (skb->len < sizeof(struct mctp_hdr) + 1)
		goto drop;

	/* grab header, advance data ptr */
	mh = mctp_hdr(skb);
	skb_pull(skb, sizeof(struct mctp_hdr));

	if (mh->ver != 1)
		goto drop;

	/* TODO: reassembly */
	if ((mh->flags_seq_tag & (MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM))
				!= (MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM))
		goto drop;

	rcu_read_lock();
	/* 1. lookup socket matching (src,dest,tag) */
	key = mctp_lookup_key(net, skb, mh->src);

	/* 2. lookup socket macthing (BCAST,dest,tag) */
	if (!key)
		key = mctp_lookup_key(net, skb, MCTP_ADDR_ANY);

	/* 3. SOM? -> lookup bound socket, conditionally (!EOM) create
	 * mapping for future (1)/(2).
	 */
	if (key)
		msk = container_of(key->sk, struct mctp_sock, sk);
	else if (!msk && (mh->flags_seq_tag & MCTP_HDR_FLAG_SOM))
		msk = mctp_lookup_bind(net, skb);

	if (!msk)
		goto unlock_drop;

	sock_queue_rcv_skb(&msk->sk, skb);

	rcu_read_unlock();

	return 0;

unlock_drop:
	rcu_read_unlock();
drop:
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

/* tag management */
static void mctp_reserve_tag(struct net *net, struct mctp_sk_key *key,
			     struct mctp_sock *msk)
{
	struct netns_mctp *mns = &net->mctp;

	lockdep_assert_held(&mns->keys_lock);

	key->sk = &msk->sk;

	/* we hold the net->key_lock here, allowing updates to both
	 * then net and sk
	 */
	hlist_add_head_rcu(&key->hlist, &mns->keys);
	hlist_add_head_rcu(&key->sklist, &msk->keys);
}

/* Allocate a locally-owned tag value for (saddr, daddr), and reserve
 * it for the socket msk
 */
static int mctp_alloc_local_tag(struct mctp_sock *msk,
				mctp_eid_t saddr, mctp_eid_t daddr, u8 *tagp)
{
	struct net *net = sock_net(&msk->sk);
	struct netns_mctp *mns = &net->mctp;
	struct mctp_sk_key *key, *tmp;
	unsigned long flags;
	int rc = -EAGAIN;
	u8 tagbits;

	/* be optimistic, alloc now */
	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;
	key->local_addr = saddr;
	key->peer_addr = daddr;

	/* 8 possible tag values */
	tagbits = 0xff;

	spin_lock_irqsave(&mns->keys_lock, flags);

	/* Walk through the existing keys, looking for potential conflicting
	 * tags. If we find a conflict, clear that bit from tagbits
	 */
	hlist_for_each_entry(tmp, &mns->keys, hlist) {
		/* if we don't own the tag, it can't conflict */
		if (tmp->tag & MCTP_HDR_FLAG_TO)
			continue;

		if ((tmp->peer_addr == daddr ||
		     tmp->peer_addr == MCTP_ADDR_ANY) &&
		    tmp->local_addr == saddr)
			tagbits &= ~(1 << tmp->tag);

		if (!tagbits)
			break;
	}

	if (tagbits) {
		key->tag = __ffs(tagbits);
		mctp_reserve_tag(net, key, msk);
		*tagp = key->tag;
		rc = 0;
	}

	spin_unlock_irqrestore(&mns->keys_lock, flags);

	if (!tagbits)
		kfree(key);

	return rc;
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
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_hdr *hdr;
	unsigned long flags;
	mctp_eid_t saddr;
	int rc;
	u8 tag;

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

	if (req_tag & MCTP_HDR_FLAG_TO) {
		rc = mctp_alloc_local_tag(msk, saddr, daddr, &tag);
		if (rc)
			return rc;
		tag |= MCTP_HDR_FLAG_TO;
	} else {
		tag = req_tag;
	}

	/* TODO: we have the route MTU here; packetise */

	skb_reset_transport_header(skb);
	skb_push(skb, sizeof(struct mctp_hdr));
	skb_reset_network_header(skb);
	hdr = mctp_hdr(skb);
	hdr->ver = 1;
	hdr->dest = daddr;
	hdr->src = saddr;
	hdr->flags_seq_tag = MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM | /* TODO */
		tag;

	skb->dev = rt->dev->dev;
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
	INIT_HLIST_HEAD(&ns->binds);
	mutex_init(&ns->bind_lock);
	INIT_HLIST_HEAD(&ns->keys);
	spin_lock_init(&ns->keys_lock);
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
