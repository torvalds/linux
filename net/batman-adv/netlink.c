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
#include <net/genetlink.h>
#include <net/netlink.h>
#include <uapi/linux/batman_adv.h>

#include "hard-interface.h"
#include "soft-interface.h"

struct sk_buff;

static struct genl_family batadv_netlink_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = BATADV_NL_NAME,
	.version = 1,
	.maxattr = BATADV_ATTR_MAX,
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
			   bat_priv->bat_algo_ops->name) ||
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

static struct genl_ops batadv_netlink_ops[] = {
	{
		.cmd = BATADV_CMD_GET_MESH_INFO,
		.flags = GENL_ADMIN_PERM,
		.policy = batadv_netlink_policy,
		.doit = batadv_netlink_get_mesh_info,
	},
};

/**
 * batadv_netlink_register - register batadv genl netlink family
 */
void __init batadv_netlink_register(void)
{
	int ret;

	ret = genl_register_family_with_ops(&batadv_netlink_family,
					    batadv_netlink_ops);
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
