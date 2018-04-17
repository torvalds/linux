/*
 * Copyright Samuel Mendoza-Jonas, IBM Corporation 2018.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <net/genetlink.h>
#include <net/ncsi.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <uapi/linux/ncsi.h>

#include "internal.h"
#include "ncsi-netlink.h"

static struct genl_family ncsi_genl_family;

static const struct nla_policy ncsi_genl_policy[NCSI_ATTR_MAX + 1] = {
	[NCSI_ATTR_IFINDEX] =		{ .type = NLA_U32 },
	[NCSI_ATTR_PACKAGE_LIST] =	{ .type = NLA_NESTED },
	[NCSI_ATTR_PACKAGE_ID] =	{ .type = NLA_U32 },
	[NCSI_ATTR_CHANNEL_ID] =	{ .type = NLA_U32 },
};

static struct ncsi_dev_priv *ndp_from_ifindex(struct net *net, u32 ifindex)
{
	struct ncsi_dev_priv *ndp;
	struct net_device *dev;
	struct ncsi_dev *nd;
	struct ncsi_dev;

	if (!net)
		return NULL;

	dev = dev_get_by_index(net, ifindex);
	if (!dev) {
		pr_err("NCSI netlink: No device for ifindex %u\n", ifindex);
		return NULL;
	}

	nd = ncsi_find_dev(dev);
	ndp = nd ? TO_NCSI_DEV_PRIV(nd) : NULL;

	dev_put(dev);
	return ndp;
}

static int ncsi_write_channel_info(struct sk_buff *skb,
				   struct ncsi_dev_priv *ndp,
				   struct ncsi_channel *nc)
{
	struct ncsi_channel_vlan_filter *ncf;
	struct ncsi_channel_mode *m;
	struct nlattr *vid_nest;
	int i;

	nla_put_u32(skb, NCSI_CHANNEL_ATTR_ID, nc->id);
	m = &nc->modes[NCSI_MODE_LINK];
	nla_put_u32(skb, NCSI_CHANNEL_ATTR_LINK_STATE, m->data[2]);
	if (nc->state == NCSI_CHANNEL_ACTIVE)
		nla_put_flag(skb, NCSI_CHANNEL_ATTR_ACTIVE);
	if (ndp->force_channel == nc)
		nla_put_flag(skb, NCSI_CHANNEL_ATTR_FORCED);

	nla_put_u32(skb, NCSI_CHANNEL_ATTR_VERSION_MAJOR, nc->version.version);
	nla_put_u32(skb, NCSI_CHANNEL_ATTR_VERSION_MINOR, nc->version.alpha2);
	nla_put_string(skb, NCSI_CHANNEL_ATTR_VERSION_STR, nc->version.fw_name);

	vid_nest = nla_nest_start(skb, NCSI_CHANNEL_ATTR_VLAN_LIST);
	if (!vid_nest)
		return -ENOMEM;
	ncf = &nc->vlan_filter;
	i = -1;
	while ((i = find_next_bit((void *)&ncf->bitmap, ncf->n_vids,
				  i + 1)) < ncf->n_vids) {
		if (ncf->vids[i])
			nla_put_u16(skb, NCSI_CHANNEL_ATTR_VLAN_ID,
				    ncf->vids[i]);
	}
	nla_nest_end(skb, vid_nest);

	return 0;
}

static int ncsi_write_package_info(struct sk_buff *skb,
				   struct ncsi_dev_priv *ndp, unsigned int id)
{
	struct nlattr *pnest, *cnest, *nest;
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	bool found;
	int rc;

	if (id > ndp->package_num) {
		netdev_info(ndp->ndev.dev, "NCSI: No package with id %u\n", id);
		return -ENODEV;
	}

	found = false;
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (np->id != id)
			continue;
		pnest = nla_nest_start(skb, NCSI_PKG_ATTR);
		if (!pnest)
			return -ENOMEM;
		nla_put_u32(skb, NCSI_PKG_ATTR_ID, np->id);
		if (ndp->force_package == np)
			nla_put_flag(skb, NCSI_PKG_ATTR_FORCED);
		cnest = nla_nest_start(skb, NCSI_PKG_ATTR_CHANNEL_LIST);
		if (!cnest) {
			nla_nest_cancel(skb, pnest);
			return -ENOMEM;
		}
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			nest = nla_nest_start(skb, NCSI_CHANNEL_ATTR);
			if (!nest) {
				nla_nest_cancel(skb, cnest);
				nla_nest_cancel(skb, pnest);
				return -ENOMEM;
			}
			rc = ncsi_write_channel_info(skb, ndp, nc);
			if (rc) {
				nla_nest_cancel(skb, nest);
				nla_nest_cancel(skb, cnest);
				nla_nest_cancel(skb, pnest);
				return rc;
			}
			nla_nest_end(skb, nest);
		}
		nla_nest_end(skb, cnest);
		nla_nest_end(skb, pnest);
		found = true;
	}

	if (!found)
		return -ENODEV;

	return 0;
}

static int ncsi_pkg_info_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct ncsi_dev_priv *ndp;
	unsigned int package_id;
	struct sk_buff *skb;
	struct nlattr *attr;
	void *hdr;
	int rc;

	if (!info || !info->attrs)
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_PACKAGE_ID])
		return -EINVAL;

	ndp = ndp_from_ifindex(genl_info_net(info),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &ncsi_genl_family, 0, NCSI_CMD_PKG_INFO);
	if (!hdr) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	package_id = nla_get_u32(info->attrs[NCSI_ATTR_PACKAGE_ID]);

	attr = nla_nest_start(skb, NCSI_ATTR_PACKAGE_LIST);
	if (!attr) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	rc = ncsi_write_package_info(skb, ndp, package_id);

	if (rc) {
		nla_nest_cancel(skb, attr);
		goto err;
	}

	nla_nest_end(skb, attr);

	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, info);

err:
	genlmsg_cancel(skb, hdr);
	kfree_skb(skb);
	return rc;
}

static int ncsi_pkg_info_all_nl(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct nlattr *attrs[NCSI_ATTR_MAX];
	struct ncsi_package *np, *package;
	struct ncsi_dev_priv *ndp;
	unsigned int package_id;
	struct nlattr *attr;
	void *hdr;
	int rc;

	rc = genlmsg_parse(cb->nlh, &ncsi_genl_family, attrs, NCSI_ATTR_MAX,
			   ncsi_genl_policy, NULL);
	if (rc)
		return rc;

	if (!attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(skb->sk)),
			       nla_get_u32(attrs[NCSI_ATTR_IFINDEX]));

	if (!ndp)
		return -ENODEV;

	package_id = cb->args[0];
	package = NULL;
	NCSI_FOR_EACH_PACKAGE(ndp, np)
		if (np->id == package_id)
			package = np;

	if (!package)
		return 0; /* done */

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &ncsi_genl_family, 0,  NCSI_CMD_PKG_INFO);
	if (!hdr) {
		rc = -EMSGSIZE;
		goto err;
	}

	attr = nla_nest_start(skb, NCSI_ATTR_PACKAGE_LIST);
	rc = ncsi_write_package_info(skb, ndp, package->id);
	if (rc) {
		nla_nest_cancel(skb, attr);
		goto err;
	}

	nla_nest_end(skb, attr);
	genlmsg_end(skb, hdr);

	cb->args[0] = package_id + 1;

	return skb->len;
err:
	genlmsg_cancel(skb, hdr);
	return rc;
}

static int ncsi_set_interface_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct ncsi_package *np, *package;
	struct ncsi_channel *nc, *channel;
	u32 package_id, channel_id;
	struct ncsi_dev_priv *ndp;
	unsigned long flags;

	if (!info || !info->attrs)
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_PACKAGE_ID])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	package_id = nla_get_u32(info->attrs[NCSI_ATTR_PACKAGE_ID]);
	package = NULL;

	spin_lock_irqsave(&ndp->lock, flags);

	NCSI_FOR_EACH_PACKAGE(ndp, np)
		if (np->id == package_id)
			package = np;
	if (!package) {
		/* The user has set a package that does not exist */
		spin_unlock_irqrestore(&ndp->lock, flags);
		return -ERANGE;
	}

	channel = NULL;
	if (!info->attrs[NCSI_ATTR_CHANNEL_ID]) {
		/* Allow any channel */
		channel_id = NCSI_RESERVED_CHANNEL;
	} else {
		channel_id = nla_get_u32(info->attrs[NCSI_ATTR_CHANNEL_ID]);
		NCSI_FOR_EACH_CHANNEL(package, nc)
			if (nc->id == channel_id)
				channel = nc;
	}

	if (channel_id != NCSI_RESERVED_CHANNEL && !channel) {
		/* The user has set a channel that does not exist on this
		 * package
		 */
		spin_unlock_irqrestore(&ndp->lock, flags);
		netdev_info(ndp->ndev.dev, "NCSI: Channel %u does not exist!\n",
			    channel_id);
		return -ERANGE;
	}

	ndp->force_package = package;
	ndp->force_channel = channel;
	spin_unlock_irqrestore(&ndp->lock, flags);

	netdev_info(ndp->ndev.dev, "Set package 0x%x, channel 0x%x%s as preferred\n",
		    package_id, channel_id,
		    channel_id == NCSI_RESERVED_CHANNEL ? " (any)" : "");

	/* Bounce the NCSI channel to set changes */
	ncsi_stop_dev(&ndp->ndev);
	ncsi_start_dev(&ndp->ndev);

	return 0;
}

static int ncsi_clear_interface_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct ncsi_dev_priv *ndp;
	unsigned long flags;

	if (!info || !info->attrs)
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	/* Clear any override */
	spin_lock_irqsave(&ndp->lock, flags);
	ndp->force_package = NULL;
	ndp->force_channel = NULL;
	spin_unlock_irqrestore(&ndp->lock, flags);
	netdev_info(ndp->ndev.dev, "NCSI: Cleared preferred package/channel\n");

	/* Bounce the NCSI channel to set changes */
	ncsi_stop_dev(&ndp->ndev);
	ncsi_start_dev(&ndp->ndev);

	return 0;
}

static const struct genl_ops ncsi_ops[] = {
	{
		.cmd = NCSI_CMD_PKG_INFO,
		.policy = ncsi_genl_policy,
		.doit = ncsi_pkg_info_nl,
		.dumpit = ncsi_pkg_info_all_nl,
		.flags = 0,
	},
	{
		.cmd = NCSI_CMD_SET_INTERFACE,
		.policy = ncsi_genl_policy,
		.doit = ncsi_set_interface_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NCSI_CMD_CLEAR_INTERFACE,
		.policy = ncsi_genl_policy,
		.doit = ncsi_clear_interface_nl,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family ncsi_genl_family __ro_after_init = {
	.name = "NCSI",
	.version = 0,
	.maxattr = NCSI_ATTR_MAX,
	.module = THIS_MODULE,
	.ops = ncsi_ops,
	.n_ops = ARRAY_SIZE(ncsi_ops),
};

int ncsi_init_netlink(struct net_device *dev)
{
	int rc;

	rc = genl_register_family(&ncsi_genl_family);
	if (rc)
		netdev_err(dev, "ncsi: failed to register netlink family\n");

	return rc;
}

int ncsi_unregister_netlink(struct net_device *dev)
{
	int rc;

	rc = genl_unregister_family(&ncsi_genl_family);
	if (rc)
		netdev_err(dev, "ncsi: failed to unregister netlink family\n");

	return rc;
}
