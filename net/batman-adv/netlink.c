/* Copyright (C) 2016 B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "netlink.h"
#include "main.h"

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/printk.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <uapi/linux/batman_adv.h>

#include "hard-interface.h"
#include "soft-interface.h"
#include "tp_meter.h"

struct sk_buff;

static struct genl_family batadv_netlink_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = BATADV_NL_NAME,
	.version = 1,
	.maxattr = BATADV_ATTR_MAX,
};

/* multicast groups */
enum batadv_netlink_multicast_groups {
	BATADV_NL_MCGRP_TPMETER,
};

static struct genl_multicast_group batadv_netlink_mcgrps[] = {
	[BATADV_NL_MCGRP_TPMETER] = { .name = BATADV_NL_MCAST_GROUP_TPMETER },
};

static struct nla_policy batadv_netlink_policy[NUM_BATADV_ATTR] = {
	[BATADV_ATTR_VERSION]		= { .type = NLA_STRING },
	[BATADV_ATTR_ALGO_NAME]		= { .type = NLA_STRING },
	[BATADV_ATTR_MESH_IFINDEX]	= { .type = NLA_U32 },
	[BATADV_ATTR_MESH_IFNAME]	= { .type = NLA_STRING },
	[BATADV_ATTR_MESH_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_HARD_IFINDEX]	= { .type = NLA_U32 },
	[BATADV_ATTR_HARD_IFNAME]	= { .type = NLA_STRING },
	[BATADV_ATTR_HARD_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_ORIG_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_TPMETER_RESULT]	= { .type = NLA_U8 },
	[BATADV_ATTR_TPMETER_TEST_TIME]	= { .type = NLA_U32 },
	[BATADV_ATTR_TPMETER_BYTES]	= { .type = NLA_U64 },
	[BATADV_ATTR_TPMETER_COOKIE]	= { .type = NLA_U32 },
};

/**
 * batadv_netlink_mesh_info_put - fill in generic information about mesh
 *  interface
 * @msg: netlink message to be sent back
 * @soft_iface: interface for which the data should be taken
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_mesh_info_put(struct sk_buff *msg, struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	struct batadv_hard_iface *primary_if = NULL;
	struct net_device *hard_iface;
	int ret = -ENOBUFS;

	if (nla_put_string(msg, BATADV_ATTR_VERSION, BATADV_SOURCE_VERSION) ||
	    nla_put_string(msg, BATADV_ATTR_ALGO_NAME,
			   bat_priv->algo_ops->name) ||
	    nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, soft_iface->ifindex) ||
	    nla_put_string(msg, BATADV_ATTR_MESH_IFNAME, soft_iface->name) ||
	    nla_put(msg, BATADV_ATTR_MESH_ADDRESS, ETH_ALEN,
		    soft_iface->dev_addr))
		goto out;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (primary_if && primary_if->if_status == BATADV_IF_ACTIVE) {
		hard_iface = primary_if->net_dev;

		if (nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX,
				hard_iface->ifindex) ||
		    nla_put_string(msg, BATADV_ATTR_HARD_IFNAME,
				   hard_iface->name) ||
		    nla_put(msg, BATADV_ATTR_HARD_ADDRESS, ETH_ALEN,
			    hard_iface->dev_addr))
			goto out;
	}

	ret = 0;

 out:
	if (primary_if)
		batadv_hardif_put(primary_if);

	return ret;
}

/**
 * batadv_netlink_get_mesh_info - handle incoming BATADV_CMD_GET_MESH_INFO
 *  netlink request
 * @skb: received netlink message
 * @info: receiver information
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_get_mesh_info(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct net_device *soft_iface;
	struct sk_buff *msg = NULL;
	void *msg_head;
	int ifindex;
	int ret;

	if (!info->attrs[BATADV_ATTR_MESH_IFINDEX])
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[BATADV_ATTR_MESH_IFINDEX]);
	if (!ifindex)
		return -EINVAL;

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface || !batadv_softif_is_valid(soft_iface)) {
		ret = -ENODEV;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	msg_head = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			       &batadv_netlink_family, 0,
			       BATADV_CMD_GET_MESH_INFO);
	if (!msg_head) {
		ret = -ENOBUFS;
		goto out;
	}

	ret = batadv_netlink_mesh_info_put(msg, soft_iface);

 out:
	if (soft_iface)
		dev_put(soft_iface);

	if (ret) {
		if (msg)
			nlmsg_free(msg);
		return ret;
	}

	genlmsg_end(msg, msg_head);
	return genlmsg_reply(msg, info);
}

/**
 * batadv_netlink_tp_meter_put - Fill information of started tp_meter session
 * @msg: netlink message to be sent back
 * @cookie: tp meter session cookie
 *
 *  Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_tp_meter_put(struct sk_buff *msg, u32 cookie)
{
	if (nla_put_u32(msg, BATADV_ATTR_TPMETER_COOKIE, cookie))
		return -ENOBUFS;

	return 0;
}

/**
 * batadv_netlink_tpmeter_notify - send tp_meter result via netlink to client
 * @bat_priv: the bat priv with all the soft interface information
 * @dst: destination of tp_meter session
 * @result: reason for tp meter session stop
 * @test_time: total time ot the tp_meter session
 * @total_bytes: bytes acked to the receiver
 * @cookie: cookie of tp_meter session
 *
 * Return: 0 on success, < 0 on error
 */
int batadv_netlink_tpmeter_notify(struct batadv_priv *bat_priv, const u8 *dst,
				  u8 result, u32 test_time, u64 total_bytes,
				  u32 cookie)
{
	struct sk_buff *msg;
	void *hdr;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &batadv_netlink_family, 0,
			  BATADV_CMD_TP_METER);
	if (!hdr) {
		ret = -ENOBUFS;
		goto err_genlmsg;
	}

	if (nla_put_u32(msg, BATADV_ATTR_TPMETER_COOKIE, cookie))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_TPMETER_TEST_TIME, test_time))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, BATADV_ATTR_TPMETER_BYTES, total_bytes,
			      BATADV_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u8(msg, BATADV_ATTR_TPMETER_RESULT, result))
		goto nla_put_failure;

	if (nla_put(msg, BATADV_ATTR_ORIG_ADDRESS, ETH_ALEN, dst))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&batadv_netlink_family,
				dev_net(bat_priv->soft_iface), msg, 0,
				BATADV_NL_MCGRP_TPMETER, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	ret = -EMSGSIZE;

err_genlmsg:
	nlmsg_free(msg);
	return ret;
}

/**
 * batadv_netlink_tp_meter_start - Start a new tp_meter session
 * @skb: received netlink message
 * @info: receiver information
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_tp_meter_start(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	struct sk_buff *msg = NULL;
	u32 test_length;
	void *msg_head;
	int ifindex;
	u32 cookie;
	u8 *dst;
	int ret;

	if (!info->attrs[BATADV_ATTR_MESH_IFINDEX])
		return -EINVAL;

	if (!info->attrs[BATADV_ATTR_ORIG_ADDRESS])
		return -EINVAL;

	if (!info->attrs[BATADV_ATTR_TPMETER_TEST_TIME])
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[BATADV_ATTR_MESH_IFINDEX]);
	if (!ifindex)
		return -EINVAL;

	dst = nla_data(info->attrs[BATADV_ATTR_ORIG_ADDRESS]);

	test_length = nla_get_u32(info->attrs[BATADV_ATTR_TPMETER_TEST_TIME]);

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface || !batadv_softif_is_valid(soft_iface)) {
		ret = -ENODEV;
		goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	msg_head = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			       &batadv_netlink_family, 0,
			       BATADV_CMD_TP_METER);
	if (!msg_head) {
		ret = -ENOBUFS;
		goto out;
	}

	bat_priv = netdev_priv(soft_iface);
	batadv_tp_start(bat_priv, dst, test_length, &cookie);

	ret = batadv_netlink_tp_meter_put(msg, cookie);

 out:
	if (soft_iface)
		dev_put(soft_iface);

	if (ret) {
		if (msg)
			nlmsg_free(msg);
		return ret;
	}

	genlmsg_end(msg, msg_head);
	return genlmsg_reply(msg, info);
}

/**
 * batadv_netlink_tp_meter_start - Cancel a running tp_meter session
 * @skb: received netlink message
 * @info: receiver information
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_tp_meter_cancel(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	int ifindex;
	u8 *dst;
	int ret = 0;

	if (!info->attrs[BATADV_ATTR_MESH_IFINDEX])
		return -EINVAL;

	if (!info->attrs[BATADV_ATTR_ORIG_ADDRESS])
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[BATADV_ATTR_MESH_IFINDEX]);
	if (!ifindex)
		return -EINVAL;

	dst = nla_data(info->attrs[BATADV_ATTR_ORIG_ADDRESS]);

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface || !batadv_softif_is_valid(soft_iface)) {
		ret = -ENODEV;
		goto out;
	}

	bat_priv = netdev_priv(soft_iface);
	batadv_tp_stop(bat_priv, dst, BATADV_TP_REASON_CANCEL);

out:
	if (soft_iface)
		dev_put(soft_iface);

	return ret;
}

static struct genl_ops batadv_netlink_ops[] = {
	{
		.cmd = BATADV_CMD_GET_MESH_INFO,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.doit = batadv_netlink_get_mesh_info,
	},
	{
		.cmd = BATADV_CMD_TP_METER,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.doit = batadv_netlink_tp_meter_start,
	},
	{
		.cmd = BATADV_CMD_TP_METER_CANCEL,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.doit = batadv_netlink_tp_meter_cancel,
	},
};

/**
 * batadv_netlink_register - register batadv genl netlink family
 */
void __init batadv_netlink_register(void)
{
	int ret;

	ret = genl_register_family_with_ops_groups(&batadv_netlink_family,
						   batadv_netlink_ops,
						   batadv_netlink_mcgrps);
	if (ret)
		pr_warn("unable to register netlink family");
}

/**
 * batadv_netlink_unregister - unregister batadv genl netlink family
 */
void batadv_netlink_unregister(void)
{
	genl_unregister_family(&batadv_netlink_family);
}
