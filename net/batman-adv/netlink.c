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

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <uapi/linux/batman_adv.h>

#include "bat_algo.h"
#include "bridge_loop_avoidance.h"
#include "gateway_client.h"
#include "hard-interface.h"
#include "originator.h"
#include "packet.h"
#include "soft-interface.h"
#include "tp_meter.h"
#include "translation-table.h"

struct genl_family batadv_netlink_family;

/* multicast groups */
enum batadv_netlink_multicast_groups {
	BATADV_NL_MCGRP_TPMETER,
};

static const struct genl_multicast_group batadv_netlink_mcgrps[] = {
	[BATADV_NL_MCGRP_TPMETER] = { .name = BATADV_NL_MCAST_GROUP_TPMETER },
};

static const struct nla_policy batadv_netlink_policy[NUM_BATADV_ATTR] = {
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
	[BATADV_ATTR_ACTIVE]		= { .type = NLA_FLAG },
	[BATADV_ATTR_TT_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_TT_TTVN]		= { .type = NLA_U8 },
	[BATADV_ATTR_TT_LAST_TTVN]	= { .type = NLA_U8 },
	[BATADV_ATTR_TT_CRC32]		= { .type = NLA_U32 },
	[BATADV_ATTR_TT_VID]		= { .type = NLA_U16 },
	[BATADV_ATTR_TT_FLAGS]		= { .type = NLA_U32 },
	[BATADV_ATTR_FLAG_BEST]		= { .type = NLA_FLAG },
	[BATADV_ATTR_LAST_SEEN_MSECS]	= { .type = NLA_U32 },
	[BATADV_ATTR_NEIGH_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_TQ]		= { .type = NLA_U8 },
	[BATADV_ATTR_THROUGHPUT]	= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_UP]	= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_DOWN]	= { .type = NLA_U32 },
	[BATADV_ATTR_ROUTER]		= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_OWN]		= { .type = NLA_FLAG },
	[BATADV_ATTR_BLA_ADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_VID]		= { .type = NLA_U16 },
	[BATADV_ATTR_BLA_BACKBONE]	= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_CRC]		= { .type = NLA_U16 },
};

/**
 * batadv_netlink_get_ifindex - Extract an interface index from a message
 * @nlh: Message header
 * @attrtype: Attribute which holds an interface index
 *
 * Return: interface index, or 0.
 */
int
batadv_netlink_get_ifindex(const struct nlmsghdr *nlh, int attrtype)
{
	struct nlattr *attr = nlmsg_find_attr(nlh, GENL_HDRLEN, attrtype);

	return attr ? nla_get_u32(attr) : 0;
}

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
		    soft_iface->dev_addr) ||
	    nla_put_u8(msg, BATADV_ATTR_TT_TTVN,
		       (u8)atomic_read(&bat_priv->tt.vn)))
		goto out;

#ifdef CONFIG_BATMAN_ADV_BLA
	if (nla_put_u16(msg, BATADV_ATTR_BLA_CRC,
			ntohs(bat_priv->bla.claim_dest.group)))
		goto out;
#endif

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

/**
 * batadv_netlink_dump_hardif_entry - Dump one hard interface into a message
 * @msg: Netlink message to dump into
 * @portid: Port making netlink request
 * @seq: Sequence number of netlink message
 * @hard_iface: Hard interface to dump
 *
 * Return: error code, or 0 on success
 */
static int
batadv_netlink_dump_hardif_entry(struct sk_buff *msg, u32 portid, u32 seq,
				 struct batadv_hard_iface *hard_iface)
{
	struct net_device *net_dev = hard_iface->net_dev;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &batadv_netlink_family, NLM_F_MULTI,
			  BATADV_CMD_GET_HARDIFS);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX,
			net_dev->ifindex) ||
	    nla_put_string(msg, BATADV_ATTR_HARD_IFNAME,
			   net_dev->name) ||
	    nla_put(msg, BATADV_ATTR_HARD_ADDRESS, ETH_ALEN,
		    net_dev->dev_addr))
		goto nla_put_failure;

	if (hard_iface->if_status == BATADV_IF_ACTIVE) {
		if (nla_put_flag(msg, BATADV_ATTR_ACTIVE))
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/**
 * batadv_netlink_dump_hardifs - Dump all hard interface into a messages
 * @msg: Netlink message to dump into
 * @cb: Parameters from query
 *
 * Return: error code, or length of reply message on success
 */
static int
batadv_netlink_dump_hardifs(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct net *net = sock_net(cb->skb->sk);
	struct net_device *soft_iface;
	struct batadv_hard_iface *hard_iface;
	int ifindex;
	int portid = NETLINK_CB(cb->skb).portid;
	int seq = cb->nlh->nlmsg_seq;
	int skip = cb->args[0];
	int i = 0;

	ifindex = batadv_netlink_get_ifindex(cb->nlh,
					     BATADV_ATTR_MESH_IFINDEX);
	if (!ifindex)
		return -EINVAL;

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface)
		return -ENODEV;

	if (!batadv_softif_is_valid(soft_iface)) {
		dev_put(soft_iface);
		return -ENODEV;
	}

	rcu_read_lock();

	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		if (i++ < skip)
			continue;

		if (batadv_netlink_dump_hardif_entry(msg, portid, seq,
						     hard_iface)) {
			i--;
			break;
		}
	}

	rcu_read_unlock();

	dev_put(soft_iface);

	cb->args[0] = i;

	return msg->len;
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
	{
		.cmd = BATADV_CMD_GET_ROUTING_ALGOS,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_algo_dump,
	},
	{
		.cmd = BATADV_CMD_GET_HARDIFS,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_netlink_dump_hardifs,
	},
	{
		.cmd = BATADV_CMD_GET_TRANSTABLE_LOCAL,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_tt_local_dump,
	},
	{
		.cmd = BATADV_CMD_GET_TRANSTABLE_GLOBAL,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_tt_global_dump,
	},
	{
		.cmd = BATADV_CMD_GET_ORIGINATORS,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_orig_dump,
	},
	{
		.cmd = BATADV_CMD_GET_NEIGHBORS,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_hardif_neigh_dump,
	},
	{
		.cmd = BATADV_CMD_GET_GATEWAYS,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_gw_dump,
	},
	{
		.cmd = BATADV_CMD_GET_BLA_CLAIM,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_bla_claim_dump,
	},
	{
		.cmd = BATADV_CMD_GET_BLA_BACKBONE,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.dumpit = batadv_bla_backbone_dump,
	},

};

struct genl_family batadv_netlink_family = {
	.hdrsize = 0,
	.name = BATADV_NL_NAME,
	.version = 1,
	.maxattr = BATADV_ATTR_MAX,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = batadv_netlink_ops,
	.n_ops = ARRAY_SIZE(batadv_netlink_ops),
	.mcgrps = batadv_netlink_mcgrps,
	.n_mcgrps = ARRAY_SIZE(batadv_netlink_mcgrps),
};

/**
 * batadv_netlink_register - register batadv genl netlink family
 */
void __init batadv_netlink_register(void)
{
	int ret;

	ret = genl_register_family(&batadv_netlink_family);
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
