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

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/netlink.h>
#include <net/sock.h>

static int mctp_neigh_add(struct mctp_dev *mdev, mctp_eid_t eid,
			  enum mctp_neigh_source source,
			  size_t lladdr_len, const void *lladdr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh;
	int rc;

	mutex_lock(&net->mctp.neigh_lock);
	if (mctp_neigh_lookup(mdev, eid, NULL) == 0) {
		rc = -EEXIST;
		goto out;
	}

	if (lladdr_len > sizeof(neigh->ha)) {
		rc = -EINVAL;
		goto out;
	}

	neigh = kzalloc(sizeof(*neigh), GFP_KERNEL);
	if (!neigh) {
		rc = -ENOMEM;
		goto out;
	}
	INIT_LIST_HEAD(&neigh->list);
	neigh->dev = mdev;
	mctp_dev_hold(neigh->dev);
	neigh->eid = eid;
	neigh->source = source;
	memcpy(neigh->ha, lladdr, lladdr_len);

	list_add_rcu(&neigh->list, &net->mctp.neighbours);
	rc = 0;
out:
	mutex_unlock(&net->mctp.neigh_lock);
	return rc;
}

static void __mctp_neigh_free(struct rcu_head *rcu)
{
	struct mctp_neigh *neigh = container_of(rcu, struct mctp_neigh, rcu);

	mctp_dev_put(neigh->dev);
	kfree(neigh);
}

/* Removes all neighbour entries referring to a device */
void mctp_neigh_remove_dev(struct mctp_dev *mdev)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh, *tmp;

	mutex_lock(&net->mctp.neigh_lock);
	list_for_each_entry_safe(neigh, tmp, &net->mctp.neighbours, list) {
		if (neigh->dev == mdev) {
			list_del_rcu(&neigh->list);
			/* TODO: immediate RTM_DELNEIGH */
			call_rcu(&neigh->rcu, __mctp_neigh_free);
		}
	}

	mutex_unlock(&net->mctp.neigh_lock);
}

static int mctp_neigh_remove(struct mctp_dev *mdev, mctp_eid_t eid,
			     enum mctp_neigh_source source)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh, *tmp;
	bool dropped = false;

	mutex_lock(&net->mctp.neigh_lock);
	list_for_each_entry_safe(neigh, tmp, &net->mctp.neighbours, list) {
		if (neigh->dev == mdev && neigh->eid == eid &&
		    neigh->source == source) {
			list_del_rcu(&neigh->list);
			/* TODO: immediate RTM_DELNEIGH */
			call_rcu(&neigh->rcu, __mctp_neigh_free);
			dropped = true;
		}
	}

	mutex_unlock(&net->mctp.neigh_lock);
	return dropped ? 0 : -ENOENT;
}

static const struct nla_policy nd_mctp_policy[NDA_MAX + 1] = {
	[NDA_DST]		= { .type = NLA_U8 },
	[NDA_LLADDR]		= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
};

static int mctp_rtm_newneigh(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	struct mctp_dev *mdev;
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX + 1];
	int rc;
	mctp_eid_t eid;
	void *lladdr;
	int lladdr_len;

	rc = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, nd_mctp_policy,
			 extack);
	if (rc < 0) {
		NL_SET_ERR_MSG(extack, "lladdr too large?");
		return rc;
	}

	if (!tb[NDA_DST]) {
		NL_SET_ERR_MSG(extack, "Neighbour EID must be specified");
		return -EINVAL;
	}

	if (!tb[NDA_LLADDR]) {
		NL_SET_ERR_MSG(extack, "Neighbour lladdr must be specified");
		return -EINVAL;
	}

	eid = nla_get_u8(tb[NDA_DST]);
	if (!mctp_address_ok(eid)) {
		NL_SET_ERR_MSG(extack, "Invalid neighbour EID");
		return -EINVAL;
	}

	lladdr = nla_data(tb[NDA_LLADDR]);
	lladdr_len = nla_len(tb[NDA_LLADDR]);

	ndm = nlmsg_data(nlh);

	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (!dev)
		return -ENODEV;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODEV;

	if (lladdr_len != dev->addr_len) {
		NL_SET_ERR_MSG(extack, "Wrong lladdr length");
		return -EINVAL;
	}

	return mctp_neigh_add(mdev, eid, MCTP_NEIGH_STATIC,
			lladdr_len, lladdr);
}

static int mctp_rtm_delneigh(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[NDA_MAX + 1];
	struct net_device *dev;
	struct mctp_dev *mdev;
	struct ndmsg *ndm;
	int rc;
	mctp_eid_t eid;

	rc = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, nd_mctp_policy,
			 extack);
	if (rc < 0) {
		NL_SET_ERR_MSG(extack, "incorrect format");
		return rc;
	}

	if (!tb[NDA_DST]) {
		NL_SET_ERR_MSG(extack, "Neighbour EID must be specified");
		return -EINVAL;
	}
	eid = nla_get_u8(tb[NDA_DST]);

	ndm = nlmsg_data(nlh);
	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (!dev)
		return -ENODEV;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODEV;

	return mctp_neigh_remove(mdev, eid, MCTP_NEIGH_STATIC);
}

static int mctp_fill_neigh(struct sk_buff *skb, u32 portid, u32 seq, int event,
			   unsigned int flags, struct mctp_neigh *neigh)
{
	struct net_device *dev = neigh->dev->dev;
	struct nlmsghdr *nlh;
	struct ndmsg *hdr;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*hdr), flags);
	if (!nlh)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ndm_family = AF_MCTP;
	hdr->ndm_ifindex = dev->ifindex;
	hdr->ndm_state = 0; // TODO other state bits?
	if (neigh->source == MCTP_NEIGH_STATIC)
		hdr->ndm_state |= NUD_PERMANENT;
	hdr->ndm_flags = 0;
	hdr->ndm_type = RTN_UNICAST; // TODO: is loopback RTN_LOCAL?

	if (nla_put_u8(skb, NDA_DST, neigh->eid))
		goto cancel;

	if (nla_put(skb, NDA_LLADDR, dev->addr_len, neigh->ha))
		goto cancel;

	nlmsg_end(skb, nlh);

	return 0;
cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mctp_rtm_getneigh(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int rc, idx, req_ifindex;
	struct mctp_neigh *neigh;
	struct ndmsg *ndmsg;
	struct {
		int idx;
	} *cbctx = (void *)cb->ctx;

	ndmsg = nlmsg_data(cb->nlh);
	req_ifindex = ndmsg->ndm_ifindex;

	idx = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(neigh, &net->mctp.neighbours, list) {
		if (idx < cbctx->idx)
			goto cont;

		rc = 0;
		if (req_ifindex == 0 || req_ifindex == neigh->dev->dev->ifindex)
			rc = mctp_fill_neigh(skb, NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     RTM_NEWNEIGH, NLM_F_MULTI, neigh);

		if (rc)
			break;
cont:
		idx++;
	}
	rcu_read_unlock();

	cbctx->idx = idx;
	return skb->len;
}

int mctp_neigh_lookup(struct mctp_dev *mdev, mctp_eid_t eid, void *ret_hwaddr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh;
	int rc = -EHOSTUNREACH; // TODO: or ENOENT?

	rcu_read_lock();
	list_for_each_entry_rcu(neigh, &net->mctp.neighbours, list) {
		if (mdev == neigh->dev && eid == neigh->eid) {
			if (ret_hwaddr)
				memcpy(ret_hwaddr, neigh->ha,
				       sizeof(neigh->ha));
			rc = 0;
			break;
		}
	}
	rcu_read_unlock();
	return rc;
}

/* namespace registration */
static int __net_init mctp_neigh_net_init(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;

	INIT_LIST_HEAD(&ns->neighbours);
	mutex_init(&ns->neigh_lock);
	return 0;
}

static void __net_exit mctp_neigh_net_exit(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;
	struct mctp_neigh *neigh;

	list_for_each_entry(neigh, &ns->neighbours, list)
		call_rcu(&neigh->rcu, __mctp_neigh_free);
}

/* net namespace implementation */

static struct pernet_operations mctp_net_ops = {
	.init = mctp_neigh_net_init,
	.exit = mctp_neigh_net_exit,
};

int __init mctp_neigh_init(void)
{
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_NEWNEIGH,
			     mctp_rtm_newneigh, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_DELNEIGH,
			     mctp_rtm_delneigh, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_GETNEIGH,
			     NULL, mctp_rtm_getneigh, 0);

	return register_pernet_subsys(&mctp_net_ops);
}

void __exit mctp_neigh_exit(void)
{
	unregister_pernet_subsys(&mctp_net_ops);
	rtnl_unregister(PF_MCTP, RTM_GETNEIGH);
	rtnl_unregister(PF_MCTP, RTM_DELNEIGH);
	rtnl_unregister(PF_MCTP, RTM_NEWNEIGH);
}
