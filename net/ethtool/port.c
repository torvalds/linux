// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Bootlin
 */
#include <linux/phy.h>
#include <linux/phy_link_topology.h>
#include <linux/phy_port.h>
#include <net/netdev_lock.h>

#include "bitset.h"
#include "common.h"
#include "netlink.h"

struct port_req_info {
	struct ethnl_req_info base;
	u32 port_id;
};

struct port_reply_data {
	struct ethnl_reply_data	base;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	u32 port_id;
	bool mii;
	bool sfp;
	u32 upstream_port;
};

#define PORT_REQINFO(__req_base) \
	container_of(__req_base, struct port_req_info, base)

#define PORT_REPDATA(__reply_base) \
	container_of(__reply_base, struct port_reply_data, base)

const struct nla_policy ethnl_port_get_policy[ETHTOOL_A_PORT_ID + 1] = {
	[ETHTOOL_A_PORT_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_PORT_ID] = NLA_POLICY_MIN(NLA_U32, 1),
};

static int port_parse_request(struct ethnl_req_info *req_info,
			      const struct genl_info *info,
			      struct nlattr **tb,
			      struct netlink_ext_ack *extack)
{
	struct port_req_info *request = PORT_REQINFO(req_info);

	if (GENL_REQ_ATTR_CHECK(info, ETHTOOL_A_PORT_ID))
		return -EINVAL;

	request->port_id = nla_get_u32(tb[ETHTOOL_A_PORT_ID]);

	return 0;
}

static int port_prepare_data(const struct ethnl_req_info *req_info,
			     struct ethnl_reply_data *reply_data,
			     const struct genl_info *info)
{
	struct port_reply_data *reply = PORT_REPDATA(reply_data);
	struct port_req_info *request = PORT_REQINFO(req_info);
	struct phy_port *port;

	/* RTNL must be held while holding a ref to the phy_port. Here, caller
	 * holds RTNL.
	 */
	port = phy_link_topo_get_port(req_info->dev, request->port_id);
	if (!port)
		return -ENODEV;

	linkmode_copy(reply->supported, port->supported);
	phy_interface_copy(reply->interfaces, port->interfaces);
	reply->port_id = port->id;
	reply->mii = port->is_mii;
	reply->sfp = port->is_sfp;
	reply->upstream_port = port->upstream_port ? port->upstream_port->id : 0;

	return 0;
}

static int port_reply_size(const struct ethnl_req_info *req_info,
			   const struct ethnl_reply_data *reply_data)
{
	bool compact = req_info->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	struct port_reply_data *reply = PORT_REPDATA(reply_data);
	size_t size = 0;
	int ret;

	/* ETHTOOL_A_PORT_ID */
	size += nla_total_size(sizeof(u32));

	if (!reply->mii) {
		/* ETHTOOL_A_PORT_SUPPORTED_MODES */
		ret = ethnl_bitset_size(reply->supported, NULL,
					__ETHTOOL_LINK_MODE_MASK_NBITS,
					link_mode_names, compact);
		if (ret < 0)
			return ret;

		size += ret;
	} else {
		/* ETHTOOL_A_PORT_SUPPORTED_INTERFACES */
		ret = ethnl_bitset_size(reply->interfaces, NULL,
					PHY_INTERFACE_MODE_MAX,
					phy_interface_names, compact);
		if (ret < 0)
			return ret;

		size += ret;
	}

	/* ETHTOOL_A_PORT_TYPE */
	size += nla_total_size(sizeof(u32));

	/* ETHTOOL_A_PORT_UPSTREAM_PORT */
	if (reply->upstream_port)
		size += nla_total_size(sizeof(u32));

	return size;
}

static int port_fill_reply(struct sk_buff *skb,
			   const struct ethnl_req_info *req_info,
			   const struct ethnl_reply_data *reply_data)
{
	bool compact = req_info->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	struct port_reply_data *reply = PORT_REPDATA(reply_data);
	int ret, port_type;

	if (nla_put_u32(skb, ETHTOOL_A_PORT_ID, reply->port_id))
		return -EMSGSIZE;

	if (!reply->mii) {
		ret = ethnl_put_bitset(skb, ETHTOOL_A_PORT_SUPPORTED_MODES,
				       reply->supported, NULL,
				       __ETHTOOL_LINK_MODE_MASK_NBITS,
				       link_mode_names, compact);
		if (ret < 0)
			return ret;
	} else {
		ret = ethnl_put_bitset(skb, ETHTOOL_A_PORT_SUPPORTED_INTERFACES,
				       reply->interfaces, NULL,
				       PHY_INTERFACE_MODE_MAX,
				       phy_interface_names, compact);
		if (ret < 0)
			return ret;
	}

	port_type = ETHTOOL_PORT_TYPE_MDI;
	if (reply->mii || reply->sfp)
		port_type = ETHTOOL_PORT_TYPE_SFP;

	if (nla_put_u32(skb, ETHTOOL_A_PORT_TYPE, port_type))
		return -EMSGSIZE;

	if (reply->upstream_port &&
	    nla_put_u32(skb, ETHTOOL_A_PORT_UPSTREAM_PORT,
			reply->upstream_port))
		return -EMSGSIZE;

	return 0;
}

struct port_dump_ctx {
	struct port_req_info	*req_info;
	struct port_reply_data	*reply_data;
	unsigned long		ifindex;
	unsigned long		pos_ifindex;
	unsigned long		pos_portid;
};

static struct port_dump_ctx *
port_dump_ctx_get(struct netlink_callback *cb)
{
	return (struct port_dump_ctx *)cb->ctx;
}

int ethnl_port_dump_start(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);
	struct nlattr **tb = info->info.attrs;
	struct port_reply_data *reply_data;
	struct port_req_info *req_info;
	int ret;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));

	if (tb[ETHTOOL_A_PORT_ID]) {
		NL_SET_BAD_ATTR(info->info.extack, tb[ETHTOOL_A_PORT_ID]);
		return -EINVAL;
	}

	req_info = kzalloc_obj(*req_info);
	if (!req_info)
		return -ENOMEM;

	reply_data = kmalloc_obj(*reply_data);
	if (!reply_data) {
		ret = -ENOMEM;
		goto free_req_info;
	}

	ret = ethnl_parse_header_dev_get(&req_info->base, tb[ETHTOOL_A_PORT_HEADER],
					 genl_info_net(&info->info),
					 info->info.extack, false);
	if (ret < 0)
		goto free_rep_data;

	ctx->ifindex = 0;
	ctx->pos_ifindex = 0;

	/* For filtered DUMP requests, let's just store the ifindex. We'll check
	 * again if the netdev is still there when looping over the netdev list
	 * in the DUMP loop.
	 */
	if (req_info->base.dev) {
		ctx->ifindex = req_info->base.dev->ifindex;
		netdev_put(req_info->base.dev, &req_info->base.dev_tracker);
		req_info->base.dev = NULL;
	}

	ctx->req_info = req_info;
	ctx->reply_data = reply_data;

	return 0;

free_rep_data:
	kfree(reply_data);
free_req_info:
	kfree(req_info);

	return ret;
}

static int port_dump_one(struct sk_buff *skb, struct net_device *dev,
			 struct netlink_callback *cb)
{
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);
	void *ehdr;
	int ret;

	ehdr = ethnl_dump_put(skb, cb, ETHTOOL_MSG_PORT_GET_REPLY);
	if (!ehdr)
		return -EMSGSIZE;

	memset(ctx->reply_data, 0, sizeof(struct port_reply_data));
	ctx->reply_data->base.dev = dev;

	ret = port_prepare_data(&ctx->req_info->base, &ctx->reply_data->base,
				genl_info_dump(cb));
	if (ret < 0)
		goto out;

	ret = ethnl_fill_reply_header(skb, dev, ETHTOOL_A_PORT_HEADER);
	if (ret < 0)
		goto out;

	ret = port_fill_reply(skb, &ctx->req_info->base, &ctx->reply_data->base);

out:
	ctx->reply_data->base.dev = NULL;
	if (ret < 0)
		genlmsg_cancel(skb, ehdr);
	else
		genlmsg_end(skb, ehdr);

	return ret;
}

static int port_dump_one_dev(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);
	struct net_device *dev;
	struct phy_port *port;
	int ret = 0;

	dev = ctx->req_info->base.dev;

	rtnl_lock();
	netdev_lock_ops(dev);

	if (!dev->link_topo)
		goto out;

	xa_for_each_start(&dev->link_topo->ports, ctx->pos_portid, port,
			  ctx->pos_portid) {
		ctx->req_info->port_id = ctx->pos_portid;

		ret = port_dump_one(skb, dev, cb);
		if (ret == -ENODEV)
			continue;
		if (ret)
			goto out;
	}

	ret = 0;

out:
	netdev_unlock_ops(dev);
	rtnl_unlock();

	return ret;
}

static int port_dump_all_dev(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);
	unsigned long start_ifindex = ctx->pos_ifindex;
	struct net *net = sock_net(skb->sk);
	netdevice_tracker dev_tracker;
	struct net_device *dev;
	int ret = 0;

	rcu_read_lock();
	for_each_netdev_dump(net, dev, ctx->pos_ifindex) {
		if (ctx->pos_ifindex != start_ifindex)
			ctx->pos_portid = 0;

		netdev_hold(dev, &dev_tracker, GFP_ATOMIC);
		rcu_read_unlock();

		ctx->req_info->base.dev = dev;
		ret = port_dump_one_dev(skb, cb);

		rcu_read_lock();
		netdev_put(dev, &dev_tracker);
		ctx->req_info->base.dev = NULL;

		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}

int ethnl_port_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);
	int ret = 0;

	if (ctx->ifindex) {
		netdevice_tracker dev_tracker;
		struct net_device *dev;

		dev = netdev_get_by_index(genl_info_net(&info->info),
					  ctx->ifindex, &dev_tracker,
					  GFP_KERNEL);
		if (!dev)
			return -ENODEV;

		ctx->req_info->base.dev = dev;
		ret = port_dump_one_dev(skb, cb);

		netdev_put(dev, &dev_tracker);
	} else {
		ret = port_dump_all_dev(skb, cb);
	}

	return ret;
}

int ethnl_port_dump_done(struct netlink_callback *cb)
{
	struct port_dump_ctx *ctx = port_dump_ctx_get(cb);

	kfree(ctx->req_info);
	kfree(ctx->reply_data);

	return 0;
}

const struct ethnl_request_ops ethnl_port_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PORT_GET,
	.reply_cmd		= ETHTOOL_MSG_PORT_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PORT_HEADER,
	.req_info_size		= sizeof(struct port_req_info),
	.reply_data_size	= sizeof(struct port_reply_data),

	.parse_request		= port_parse_request,
	.prepare_data		= port_prepare_data,
	.reply_size		= port_reply_size,
	.fill_reply		= port_fill_reply,
};
