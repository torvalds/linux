// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Samuel Mendoza-Jonas, IBM Corporation 2018.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <net/genetlink.h>
#include <net/ncsi.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <uapi/linux/ncsi.h>

#include "internal.h"
#include "ncsi-pkt.h"
#include "ncsi-netlink.h"

static struct genl_family ncsi_genl_family;

static const struct nla_policy ncsi_genl_policy[NCSI_ATTR_MAX + 1] = {
	[NCSI_ATTR_IFINDEX] =		{ .type = NLA_U32 },
	[NCSI_ATTR_PACKAGE_LIST] =	{ .type = NLA_NESTED },
	[NCSI_ATTR_PACKAGE_ID] =	{ .type = NLA_U32 },
	[NCSI_ATTR_CHANNEL_ID] =	{ .type = NLA_U32 },
	[NCSI_ATTR_DATA] =		{ .type = NLA_BINARY, .len = 2048 },
	[NCSI_ATTR_MULTI_FLAG] =	{ .type = NLA_FLAG },
	[NCSI_ATTR_PACKAGE_MASK] =	{ .type = NLA_U32 },
	[NCSI_ATTR_CHANNEL_MASK] =	{ .type = NLA_U32 },
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
	if (nc == nc->package->preferred_channel)
		nla_put_flag(skb, NCSI_CHANNEL_ATTR_FORCED);

	nla_put_u32(skb, NCSI_CHANNEL_ATTR_VERSION_MAJOR, nc->version.major);
	nla_put_u32(skb, NCSI_CHANNEL_ATTR_VERSION_MINOR, nc->version.minor);
	nla_put(skb, NCSI_CHANNEL_ATTR_VERSION_STR,
		strnlen(nc->version.fw_name, 12), nc->version.fw_name);

	vid_nest = nla_nest_start_noflag(skb, NCSI_CHANNEL_ATTR_VLAN_LIST);
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

	if (id > ndp->package_num - 1) {
		netdev_info(ndp->ndev.dev, "NCSI: No package with id %u\n", id);
		return -ENODEV;
	}

	found = false;
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (np->id != id)
			continue;
		pnest = nla_nest_start_noflag(skb, NCSI_PKG_ATTR);
		if (!pnest)
			return -ENOMEM;
		rc = nla_put_u32(skb, NCSI_PKG_ATTR_ID, np->id);
		if (rc) {
			nla_nest_cancel(skb, pnest);
			return rc;
		}
		if ((0x1 << np->id) == ndp->package_whitelist)
			nla_put_flag(skb, NCSI_PKG_ATTR_FORCED);
		cnest = nla_nest_start_noflag(skb, NCSI_PKG_ATTR_CHANNEL_LIST);
		if (!cnest) {
			nla_nest_cancel(skb, pnest);
			return -ENOMEM;
		}
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			nest = nla_nest_start_noflag(skb, NCSI_CHANNEL_ATTR);
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

	attr = nla_nest_start_noflag(skb, NCSI_ATTR_PACKAGE_LIST);
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
	kfree_skb(skb);
	return rc;
}

static int ncsi_pkg_info_all_nl(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct nlattr *attrs[NCSI_ATTR_MAX + 1];
	struct ncsi_package *np, *package;
	struct ncsi_dev_priv *ndp;
	unsigned int package_id;
	struct nlattr *attr;
	void *hdr;
	int rc;

	rc = genlmsg_parse_deprecated(cb->nlh, &ncsi_genl_family, attrs, NCSI_ATTR_MAX,
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
			  &ncsi_genl_family, NLM_F_MULTI,  NCSI_CMD_PKG_INFO);
	if (!hdr) {
		rc = -EMSGSIZE;
		goto err;
	}

	attr = nla_nest_start_noflag(skb, NCSI_ATTR_PACKAGE_LIST);
	if (!attr) {
		rc = -EMSGSIZE;
		goto err;
	}
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

	NCSI_FOR_EACH_PACKAGE(ndp, np)
		if (np->id == package_id)
			package = np;
	if (!package) {
		/* The user has set a package that does not exist */
		return -ERANGE;
	}

	channel = NULL;
	if (info->attrs[NCSI_ATTR_CHANNEL_ID]) {
		channel_id = nla_get_u32(info->attrs[NCSI_ATTR_CHANNEL_ID]);
		NCSI_FOR_EACH_CHANNEL(package, nc)
			if (nc->id == channel_id) {
				channel = nc;
				break;
			}
		if (!channel) {
			netdev_info(ndp->ndev.dev,
				    "NCSI: Channel %u does not exist!\n",
				    channel_id);
			return -ERANGE;
		}
	}

	spin_lock_irqsave(&ndp->lock, flags);
	ndp->package_whitelist = 0x1 << package->id;
	ndp->multi_package = false;
	spin_unlock_irqrestore(&ndp->lock, flags);

	spin_lock_irqsave(&package->lock, flags);
	package->multi_channel = false;
	if (channel) {
		package->channel_whitelist = 0x1 << channel->id;
		package->preferred_channel = channel;
	} else {
		/* Allow any channel */
		package->channel_whitelist = UINT_MAX;
		package->preferred_channel = NULL;
	}
	spin_unlock_irqrestore(&package->lock, flags);

	if (channel)
		netdev_info(ndp->ndev.dev,
			    "Set package 0x%x, channel 0x%x as preferred\n",
			    package_id, channel_id);
	else
		netdev_info(ndp->ndev.dev, "Set package 0x%x as preferred\n",
			    package_id);

	/* Update channel configuration */
	if (!(ndp->flags & NCSI_DEV_RESET))
		ncsi_reset_dev(&ndp->ndev);

	return 0;
}

static int ncsi_clear_interface_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct ncsi_dev_priv *ndp;
	struct ncsi_package *np;
	unsigned long flags;

	if (!info || !info->attrs)
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	/* Reset any whitelists and disable multi mode */
	spin_lock_irqsave(&ndp->lock, flags);
	ndp->package_whitelist = UINT_MAX;
	ndp->multi_package = false;
	spin_unlock_irqrestore(&ndp->lock, flags);

	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		spin_lock_irqsave(&np->lock, flags);
		np->multi_channel = false;
		np->channel_whitelist = UINT_MAX;
		np->preferred_channel = NULL;
		spin_unlock_irqrestore(&np->lock, flags);
	}
	netdev_info(ndp->ndev.dev, "NCSI: Cleared preferred package/channel\n");

	/* Update channel configuration */
	if (!(ndp->flags & NCSI_DEV_RESET))
		ncsi_reset_dev(&ndp->ndev);

	return 0;
}

static int ncsi_send_cmd_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct ncsi_dev_priv *ndp;
	struct ncsi_pkt_hdr *hdr;
	struct ncsi_cmd_arg nca;
	unsigned char *data;
	u32 package_id;
	u32 channel_id;
	int len, ret;

	if (!info || !info->attrs) {
		ret = -EINVAL;
		goto out;
	}

	if (!info->attrs[NCSI_ATTR_IFINDEX]) {
		ret = -EINVAL;
		goto out;
	}

	if (!info->attrs[NCSI_ATTR_PACKAGE_ID]) {
		ret = -EINVAL;
		goto out;
	}

	if (!info->attrs[NCSI_ATTR_CHANNEL_ID]) {
		ret = -EINVAL;
		goto out;
	}

	if (!info->attrs[NCSI_ATTR_DATA]) {
		ret = -EINVAL;
		goto out;
	}

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp) {
		ret = -ENODEV;
		goto out;
	}

	package_id = nla_get_u32(info->attrs[NCSI_ATTR_PACKAGE_ID]);
	channel_id = nla_get_u32(info->attrs[NCSI_ATTR_CHANNEL_ID]);

	if (package_id >= NCSI_MAX_PACKAGE || channel_id >= NCSI_MAX_CHANNEL) {
		ret = -ERANGE;
		goto out_netlink;
	}

	len = nla_len(info->attrs[NCSI_ATTR_DATA]);
	if (len < sizeof(struct ncsi_pkt_hdr)) {
		netdev_info(ndp->ndev.dev, "NCSI: no command to send %u\n",
			    package_id);
		ret = -EINVAL;
		goto out_netlink;
	} else {
		data = (unsigned char *)nla_data(info->attrs[NCSI_ATTR_DATA]);
	}

	hdr = (struct ncsi_pkt_hdr *)data;

	nca.ndp = ndp;
	nca.package = (unsigned char)package_id;
	nca.channel = (unsigned char)channel_id;
	nca.type = hdr->type;
	nca.req_flags = NCSI_REQ_FLAG_NETLINK_DRIVEN;
	nca.info = info;
	nca.payload = ntohs(hdr->length);
	nca.data = data + sizeof(*hdr);

	ret = ncsi_xmit_cmd(&nca);
out_netlink:
	if (ret != 0) {
		netdev_err(ndp->ndev.dev,
			   "NCSI: Error %d sending command\n",
			   ret);
		ncsi_send_netlink_err(ndp->ndev.dev,
				      info->snd_seq,
				      info->snd_portid,
				      info->nlhdr,
				      ret);
	}
out:
	return ret;
}

int ncsi_send_netlink_rsp(struct ncsi_request *nr,
			  struct ncsi_package *np,
			  struct ncsi_channel *nc)
{
	struct sk_buff *skb;
	struct net *net;
	void *hdr;
	int rc;

	net = dev_net(nr->rsp->dev);

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, nr->snd_portid, nr->snd_seq,
			  &ncsi_genl_family, 0, NCSI_CMD_SEND_CMD);
	if (!hdr) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	nla_put_u32(skb, NCSI_ATTR_IFINDEX, nr->rsp->dev->ifindex);
	if (np)
		nla_put_u32(skb, NCSI_ATTR_PACKAGE_ID, np->id);
	if (nc)
		nla_put_u32(skb, NCSI_ATTR_CHANNEL_ID, nc->id);
	else
		nla_put_u32(skb, NCSI_ATTR_CHANNEL_ID, NCSI_RESERVED_CHANNEL);

	rc = nla_put(skb, NCSI_ATTR_DATA, nr->rsp->len, (void *)nr->rsp->data);
	if (rc)
		goto err;

	genlmsg_end(skb, hdr);
	return genlmsg_unicast(net, skb, nr->snd_portid);

err:
	kfree_skb(skb);
	return rc;
}

int ncsi_send_netlink_timeout(struct ncsi_request *nr,
			      struct ncsi_package *np,
			      struct ncsi_channel *nc)
{
	struct sk_buff *skb;
	struct net *net;
	void *hdr;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, nr->snd_portid, nr->snd_seq,
			  &ncsi_genl_family, 0, NCSI_CMD_SEND_CMD);
	if (!hdr) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	net = dev_net(nr->cmd->dev);

	nla_put_u32(skb, NCSI_ATTR_IFINDEX, nr->cmd->dev->ifindex);

	if (np)
		nla_put_u32(skb, NCSI_ATTR_PACKAGE_ID, np->id);
	else
		nla_put_u32(skb, NCSI_ATTR_PACKAGE_ID,
			    NCSI_PACKAGE_INDEX((((struct ncsi_pkt_hdr *)
						 nr->cmd->data)->channel)));

	if (nc)
		nla_put_u32(skb, NCSI_ATTR_CHANNEL_ID, nc->id);
	else
		nla_put_u32(skb, NCSI_ATTR_CHANNEL_ID, NCSI_RESERVED_CHANNEL);

	genlmsg_end(skb, hdr);
	return genlmsg_unicast(net, skb, nr->snd_portid);
}

int ncsi_send_netlink_err(struct net_device *dev,
			  u32 snd_seq,
			  u32 snd_portid,
			  const struct nlmsghdr *nlhdr,
			  int err)
{
	struct nlmsghdr *nlh;
	struct nlmsgerr *nle;
	struct sk_buff *skb;
	struct net *net;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	net = dev_net(dev);

	nlh = nlmsg_put(skb, snd_portid, snd_seq,
			NLMSG_ERROR, sizeof(*nle), 0);
	nle = (struct nlmsgerr *)nlmsg_data(nlh);
	nle->error = err;
	memcpy(&nle->msg, nlhdr, sizeof(*nlh));

	nlmsg_end(skb, nlh);

	return nlmsg_unicast(net->genl_sock, skb, snd_portid);
}

static int ncsi_set_package_mask_nl(struct sk_buff *msg,
				    struct genl_info *info)
{
	struct ncsi_dev_priv *ndp;
	unsigned long flags;
	int rc;

	if (!info || !info->attrs)
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_IFINDEX])
		return -EINVAL;

	if (!info->attrs[NCSI_ATTR_PACKAGE_MASK])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	spin_lock_irqsave(&ndp->lock, flags);
	if (nla_get_flag(info->attrs[NCSI_ATTR_MULTI_FLAG])) {
		if (ndp->flags & NCSI_DEV_HWA) {
			ndp->multi_package = true;
			rc = 0;
		} else {
			netdev_err(ndp->ndev.dev,
				   "NCSI: Can't use multiple packages without HWA\n");
			rc = -EPERM;
		}
	} else {
		ndp->multi_package = false;
		rc = 0;
	}

	if (!rc)
		ndp->package_whitelist =
			nla_get_u32(info->attrs[NCSI_ATTR_PACKAGE_MASK]);
	spin_unlock_irqrestore(&ndp->lock, flags);

	if (!rc) {
		/* Update channel configuration */
		if (!(ndp->flags & NCSI_DEV_RESET))
			ncsi_reset_dev(&ndp->ndev);
	}

	return rc;
}

static int ncsi_set_channel_mask_nl(struct sk_buff *msg,
				    struct genl_info *info)
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

	if (!info->attrs[NCSI_ATTR_CHANNEL_MASK])
		return -EINVAL;

	ndp = ndp_from_ifindex(get_net(sock_net(msg->sk)),
			       nla_get_u32(info->attrs[NCSI_ATTR_IFINDEX]));
	if (!ndp)
		return -ENODEV;

	package_id = nla_get_u32(info->attrs[NCSI_ATTR_PACKAGE_ID]);
	package = NULL;
	NCSI_FOR_EACH_PACKAGE(ndp, np)
		if (np->id == package_id) {
			package = np;
			break;
		}
	if (!package)
		return -ERANGE;

	spin_lock_irqsave(&package->lock, flags);

	channel = NULL;
	if (info->attrs[NCSI_ATTR_CHANNEL_ID]) {
		channel_id = nla_get_u32(info->attrs[NCSI_ATTR_CHANNEL_ID]);
		NCSI_FOR_EACH_CHANNEL(np, nc)
			if (nc->id == channel_id) {
				channel = nc;
				break;
			}
		if (!channel) {
			spin_unlock_irqrestore(&package->lock, flags);
			return -ERANGE;
		}
		netdev_dbg(ndp->ndev.dev,
			   "NCSI: Channel %u set as preferred channel\n",
			   channel->id);
	}

	package->channel_whitelist =
		nla_get_u32(info->attrs[NCSI_ATTR_CHANNEL_MASK]);
	if (package->channel_whitelist == 0)
		netdev_dbg(ndp->ndev.dev,
			   "NCSI: Package %u set to all channels disabled\n",
			   package->id);

	package->preferred_channel = channel;

	if (nla_get_flag(info->attrs[NCSI_ATTR_MULTI_FLAG])) {
		package->multi_channel = true;
		netdev_info(ndp->ndev.dev,
			    "NCSI: Multi-channel enabled on package %u\n",
			    package_id);
	} else {
		package->multi_channel = false;
	}

	spin_unlock_irqrestore(&package->lock, flags);

	/* Update channel configuration */
	if (!(ndp->flags & NCSI_DEV_RESET))
		ncsi_reset_dev(&ndp->ndev);

	return 0;
}

static const struct genl_small_ops ncsi_ops[] = {
	{
		.cmd = NCSI_CMD_PKG_INFO,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_pkg_info_nl,
		.dumpit = ncsi_pkg_info_all_nl,
		.flags = 0,
	},
	{
		.cmd = NCSI_CMD_SET_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_set_interface_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NCSI_CMD_CLEAR_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_clear_interface_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NCSI_CMD_SEND_CMD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_send_cmd_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NCSI_CMD_SET_PACKAGE_MASK,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_set_package_mask_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NCSI_CMD_SET_CHANNEL_MASK,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = ncsi_set_channel_mask_nl,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family ncsi_genl_family __ro_after_init = {
	.name = "NCSI",
	.version = 0,
	.maxattr = NCSI_ATTR_MAX,
	.policy = ncsi_genl_policy,
	.module = THIS_MODULE,
	.small_ops = ncsi_ops,
	.n_small_ops = ARRAY_SIZE(ncsi_ops),
	.resv_start_op = NCSI_CMD_SET_CHANNEL_MASK + 1,
};

static int __init ncsi_init_netlink(void)
{
	return genl_register_family(&ncsi_genl_family);
}
subsys_initcall(ncsi_init_netlink);
