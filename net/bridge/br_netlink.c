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
#include <linux/rtnetlink.h>
#include <net/netlink.h>
#include "br_private.h"

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

	pr_debug("br_fill_info event %d port %s master %s\n",
		 event, dev->name, br->dev->name);

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*hdr), flags);
	if (nlh == NULL)
		return -ENOBUFS;

	hdr = nlmsg_data(nlh);
	hdr->ifi_family = AF_BRIDGE;
	hdr->__ifi_pad = 0;
	hdr->ifi_type = dev->type;
	hdr->ifi_index = dev->ifindex;
	hdr->ifi_flags = dev_get_flags(dev);
	hdr->ifi_change = 0;

	NLA_PUT_STRING(skb, IFLA_IFNAME, dev->name);
	NLA_PUT_U32(skb, IFLA_MASTER, br->dev->ifindex);
	NLA_PUT_U32(skb, IFLA_MTU, dev->mtu);
	NLA_PUT_U8(skb, IFLA_OPERSTATE, operstate);

	if (dev->addr_len)
		NLA_PUT(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr);

	if (dev->ifindex != dev->iflink)
		NLA_PUT_U32(skb, IFLA_LINK, dev->iflink);

	if (event == RTM_NEWLINK)
		NLA_PUT_U8(skb, IFLA_PROTINFO, port->state);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	return nlmsg_cancel(skb, nlh);
}

/*
 * Notify listeners of a change in port information
 */
void br_ifinfo_notify(int event, struct net_bridge_port *port)
{
	struct sk_buff *skb;
	int err = -ENOBUFS;

	pr_debug("bridge notify event=%d\n", event);
	skb = nlmsg_new(br_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = br_fill_ifinfo(skb, port, 0, 0, event, 0);
	/* failure implies BUG in br_nlmsg_size() */
	BUG_ON(err < 0);

	err = rtnl_notify(skb, 0, RTNLGRP_LINK, NULL, GFP_ATOMIC);
errout:
	if (err < 0)
		rtnl_set_sk_err(RTNLGRP_LINK, err);
}

/*
 * Dump information about all ports, in response to GETLINK
 */
static int br_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net_device *dev;
	int idx;

	read_lock(&dev_base_lock);
	for (dev = dev_base, idx = 0; dev; dev = dev->next) {
		/* not a bridge port */
		if (dev->br_port == NULL || idx < cb->args[0])
			goto skip;

		if (br_fill_ifinfo(skb, dev->br_port, NETLINK_CB(cb->skb).pid,
				   cb->nlh->nlmsg_seq, RTM_NEWLINK,
				   NLM_F_MULTI) < 0)
			break;
skip:
		++idx;
	}
	read_unlock(&dev_base_lock);

	cb->args[0] = idx;

	return skb->len;
}

/*
 * Change state of port (ie from forwarding to blocking etc)
 * Used by spanning tree in user space.
 */
static int br_rtm_setlink(struct sk_buff *skb,  struct nlmsghdr *nlh, void *arg)
{
	struct ifinfomsg *ifm;
	struct nlattr *protinfo;
	struct net_device *dev;
	struct net_bridge_port *p;
	u8 new_state;

	if (nlmsg_len(nlh) < sizeof(*ifm))
		return -EINVAL;

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_family != AF_BRIDGE)
		return -EPFNOSUPPORT;

	protinfo = nlmsg_find_attr(nlh, sizeof(*ifm), IFLA_PROTINFO);
	if (!protinfo || nla_len(protinfo) < sizeof(u8))
		return -EINVAL;

	new_state = nla_get_u8(protinfo);
	if (new_state > BR_STATE_BLOCKING)
		return -EINVAL;

	dev = __dev_get_by_index(ifm->ifi_index);
	if (!dev)
		return -ENODEV;

	p = dev->br_port;
	if (!p)
		return -EINVAL;

	/* if kernel STP is running, don't allow changes */
	if (p->br->stp_enabled)
		return -EBUSY;

	if (!netif_running(dev) ||
	    (!netif_carrier_ok(dev) && new_state != BR_STATE_DISABLED))
		return -ENETDOWN;

	p->state = new_state;
	br_log_state(p);
	return 0;
}


static struct rtnetlink_link bridge_rtnetlink_table[RTM_NR_MSGTYPES] = {
	[RTM_GETLINK - RTM_BASE] = { .dumpit	= br_dump_ifinfo, },
	[RTM_SETLINK - RTM_BASE] = { .doit      = br_rtm_setlink, },
};

void __init br_netlink_init(void)
{
	rtnetlink_links[PF_BRIDGE] = bridge_rtnetlink_table;
}

void __exit br_netlink_fini(void)
{
	rtnetlink_links[PF_BRIDGE] = NULL;
}

