/*
 *	Bridge netlink control interface
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "br_private.h"
#include "br_private_stp.h"

static inline size_t br_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(4) /* IFLA_MASTER */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(4) /* IFLA_LINK */
	       + nla_total_size(1) /* IFLA_OPERSTATE */
	       + nla_total_size(1); /* IFLA_PROTINFO */
}

/*
 * Create one netlink message for one interface
 * Contains port and master info as well as carrier and bridge state.
 */
static int br_fill_ifinfo(struct sk_buff *skb, const struct net_bridge_port *port,
			  u32 pid, u32 seq, int event, unsigned int flags)
{
	const struct net_bridge *br = port->br;
	const struct net_device *dev = port->dev;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;
	u8 operstate = netif_running(dev) ? dev->operstate : IF_OPER_DOWN;

	br_debug(br, "br_fill_info event %d port %s master %s\n",
		     event, dev->name, br->dev->name);

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*hdr), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifi_family = AF_BRIDGE;
	hdr->__ifi_pad = 0;
	hdr->ifi_type = dev->type;
	hdr->ifi_index = dev->ifindex;
	hdr->ifi_flags = dev_get_flags(dev);
	hdr->ifi_change = 0;

	if (nla_put_string(skb, IFLA_IFNAME, dev->name) ||
	    nla_put_u32(skb, IFLA_MASTER, br->dev->ifindex) ||
	    nla_put_u32(skb, IFLA_MTU, dev->mtu) ||
	    nla_put_u8(skb, IFLA_OPERSTATE, operstate) ||
	    (dev->addr_len &&
	     nla_put(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr)) ||
	    (dev->ifindex != dev->iflink &&
	     nla_put_u32(skb, IFLA_LINK, dev->iflink)) ||
	    (event == RTM_NEWLINK &&
	     nla_put_u8(skb, IFLA_PROTINFO, port->state)))
		goto nla_put_failure;
	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/*
 * Notify listeners of a change in port information
 */
void br_ifinfo_notify(int event, struct net_bridge_port *port)
{
	struct net *net = dev_net(port->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	br_debug(port->br, "port %u(%s) event %d\n",
		 (unsigned int)port->port_no, port->dev->name, event);

	skb = nlmsg_new(br_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = br_fill_ifinfo(skb, port, 0, 0, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in br_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_LINK, err);
}

/*
 * Dump information about all ports, in response to GETLINK
 */
int br_getlink(struct sk_buff *skb, u32 pid, u32 seq,
	       struct net_device *dev)
{
	int err = 0;
	struct net_bridge_port *port = br_port_get_rcu(dev);

	/* not a bridge port */
	if (!port)
		goto out;

	err = br_fill_ifinfo(skb, port, pid, seq, RTM_NEWLINK, NLM_F_MULTI);
out:
	return err;
}

/*
 * Change state of port (ie from forwarding to blocking etc)
 * Used by spanning tree in user space.
 */
int br_setlink(struct net_device *dev, struct nlmsghdr *nlh)
{
	struct ifinfomsg *ifm;
	struct nlattr *protinfo;
	struct net_bridge_port *p;
	u8 new_state;

	ifm = nlmsg_data(nlh);

	protinfo = nlmsg_find_attr(nlh, sizeof(*ifm), IFLA_PROTINFO);
	if (!protinfo || nla_len(protinfo) < sizeof(u8))
		return -EINVAL;

	new_state = nla_get_u8(protinfo);
	if (new_state > BR_STATE_BLOCKING)
		return -EINVAL;

	p = br_port_get_rtnl(dev);
	if (!p)
		return -EINVAL;

	/* if kernel STP is running, don't allow changes */
	if (p->br->stp_enabled == BR_KERNEL_STP)
		return -EBUSY;

	if (!netif_running(dev) ||
	    (!netif_carrier_ok(dev) && new_state != BR_STATE_DISABLED))
		return -ENETDOWN;

	p->state = new_state;
	br_log_state(p);

	spin_lock_bh(&p->br->lock);
	br_port_state_selection(p->br);
	spin_unlock_bh(&p->br->lock);

	return 0;
}

static int br_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	return 0;
}

struct rtnl_link_ops br_link_ops __read_mostly = {
	.kind		= "bridge",
	.priv_size	= sizeof(struct net_bridge),
	.setup		= br_dev_setup,
	.validate	= br_validate,
	.dellink	= br_dev_delete,
};

int __init br_netlink_init(void)
{
	return rtnl_link_register(&br_link_ops);
}

void __exit br_netlink_fini(void)
{
	rtnl_link_unregister(&br_link_ops);
	rtnl_unregister_all(PF_BRIDGE);
}
