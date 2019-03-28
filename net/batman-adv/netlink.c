// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2016-2019  B.A.T.M.A.N. contributors:
 *
 * Matthias Schiffer
 */

#include "netlink.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/byteorder/generic.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/genetlink.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/printk.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <uapi/linux/batadv_packet.h>
#include <uapi/linux/batman_adv.h>

#include "bat_algo.h"
#include "bridge_loop_avoidance.h"
#include "distributed-arp-table.h"
#include "gateway_client.h"
#include "gateway_common.h"
#include "hard-interface.h"
#include "log.h"
#include "multicast.h"
#include "network-coding.h"
#include "originator.h"
#include "soft-interface.h"
#include "tp_meter.h"
#include "translation-table.h"

struct net;

struct genl_family batadv_netlink_family;

/* multicast groups */
enum batadv_netlink_multicast_groups {
	BATADV_NL_MCGRP_CONFIG,
	BATADV_NL_MCGRP_TPMETER,
};

/**
 * enum batadv_genl_ops_flags - flags for genl_ops's internal_flags
 */
enum batadv_genl_ops_flags {
	/**
	 * @BATADV_FLAG_NEED_MESH: request requires valid soft interface in
	 *  attribute BATADV_ATTR_MESH_IFINDEX and expects a pointer to it to be
	 *  saved in info->user_ptr[0]
	 */
	BATADV_FLAG_NEED_MESH = BIT(0),

	/**
	 * @BATADV_FLAG_NEED_HARDIF: request requires valid hard interface in
	 *  attribute BATADV_ATTR_HARD_IFINDEX and expects a pointer to it to be
	 *  saved in info->user_ptr[1]
	 */
	BATADV_FLAG_NEED_HARDIF = BIT(1),

	/**
	 * @BATADV_FLAG_NEED_VLAN: request requires valid vlan in
	 *  attribute BATADV_ATTR_VLANID and expects a pointer to it to be
	 *  saved in info->user_ptr[1]
	 */
	BATADV_FLAG_NEED_VLAN = BIT(2),
};

static const struct genl_multicast_group batadv_netlink_mcgrps[] = {
	[BATADV_NL_MCGRP_CONFIG] = { .name = BATADV_NL_MCAST_GROUP_CONFIG },
	[BATADV_NL_MCGRP_TPMETER] = { .name = BATADV_NL_MCAST_GROUP_TPMETER },
};

static const struct nla_policy batadv_netlink_policy[NUM_BATADV_ATTR] = {
	[BATADV_ATTR_VERSION]			= { .type = NLA_STRING },
	[BATADV_ATTR_ALGO_NAME]			= { .type = NLA_STRING },
	[BATADV_ATTR_MESH_IFINDEX]		= { .type = NLA_U32 },
	[BATADV_ATTR_MESH_IFNAME]		= { .type = NLA_STRING },
	[BATADV_ATTR_MESH_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_HARD_IFINDEX]		= { .type = NLA_U32 },
	[BATADV_ATTR_HARD_IFNAME]		= { .type = NLA_STRING },
	[BATADV_ATTR_HARD_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_ORIG_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_TPMETER_RESULT]		= { .type = NLA_U8 },
	[BATADV_ATTR_TPMETER_TEST_TIME]		= { .type = NLA_U32 },
	[BATADV_ATTR_TPMETER_BYTES]		= { .type = NLA_U64 },
	[BATADV_ATTR_TPMETER_COOKIE]		= { .type = NLA_U32 },
	[BATADV_ATTR_ACTIVE]			= { .type = NLA_FLAG },
	[BATADV_ATTR_TT_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_TT_TTVN]			= { .type = NLA_U8 },
	[BATADV_ATTR_TT_LAST_TTVN]		= { .type = NLA_U8 },
	[BATADV_ATTR_TT_CRC32]			= { .type = NLA_U32 },
	[BATADV_ATTR_TT_VID]			= { .type = NLA_U16 },
	[BATADV_ATTR_TT_FLAGS]			= { .type = NLA_U32 },
	[BATADV_ATTR_FLAG_BEST]			= { .type = NLA_FLAG },
	[BATADV_ATTR_LAST_SEEN_MSECS]		= { .type = NLA_U32 },
	[BATADV_ATTR_NEIGH_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_TQ]			= { .type = NLA_U8 },
	[BATADV_ATTR_THROUGHPUT]		= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_UP]		= { .type = NLA_U32 },
	[BATADV_ATTR_BANDWIDTH_DOWN]		= { .type = NLA_U32 },
	[BATADV_ATTR_ROUTER]			= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_OWN]			= { .type = NLA_FLAG },
	[BATADV_ATTR_BLA_ADDRESS]		= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_VID]			= { .type = NLA_U16 },
	[BATADV_ATTR_BLA_BACKBONE]		= { .len = ETH_ALEN },
	[BATADV_ATTR_BLA_CRC]			= { .type = NLA_U16 },
	[BATADV_ATTR_DAT_CACHE_IP4ADDRESS]	= { .type = NLA_U32 },
	[BATADV_ATTR_DAT_CACHE_HWADDRESS]	= { .len = ETH_ALEN },
	[BATADV_ATTR_DAT_CACHE_VID]		= { .type = NLA_U16 },
	[BATADV_ATTR_MCAST_FLAGS]		= { .type = NLA_U32 },
	[BATADV_ATTR_MCAST_FLAGS_PRIV]		= { .type = NLA_U32 },
	[BATADV_ATTR_VLANID]			= { .type = NLA_U16 },
	[BATADV_ATTR_AGGREGATED_OGMS_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_AP_ISOLATION_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_ISOLATION_MARK]		= { .type = NLA_U32 },
	[BATADV_ATTR_ISOLATION_MASK]		= { .type = NLA_U32 },
	[BATADV_ATTR_BONDING_ENABLED]		= { .type = NLA_U8 },
	[BATADV_ATTR_BRIDGE_LOOP_AVOIDANCE_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_DISTRIBUTED_ARP_TABLE_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_FRAGMENTATION_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_GW_BANDWIDTH_DOWN]		= { .type = NLA_U32 },
	[BATADV_ATTR_GW_BANDWIDTH_UP]		= { .type = NLA_U32 },
	[BATADV_ATTR_GW_MODE]			= { .type = NLA_U8 },
	[BATADV_ATTR_GW_SEL_CLASS]		= { .type = NLA_U32 },
	[BATADV_ATTR_HOP_PENALTY]		= { .type = NLA_U8 },
	[BATADV_ATTR_LOG_LEVEL]			= { .type = NLA_U32 },
	[BATADV_ATTR_MULTICAST_FORCEFLOOD_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_MULTICAST_FANOUT]		= { .type = NLA_U32 },
	[BATADV_ATTR_NETWORK_CODING_ENABLED]	= { .type = NLA_U8 },
	[BATADV_ATTR_ORIG_INTERVAL]		= { .type = NLA_U32 },
	[BATADV_ATTR_ELP_INTERVAL]		= { .type = NLA_U32 },
	[BATADV_ATTR_THROUGHPUT_OVERRIDE]	= { .type = NLA_U32 },
};

/**
 * batadv_netlink_get_ifindex() - Extract an interface index from a message
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
 * batadv_netlink_mesh_fill_ap_isolation() - Add ap_isolation softif attribute
 * @msg: Netlink message to dump into
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_mesh_fill_ap_isolation(struct sk_buff *msg,
						 struct batadv_priv *bat_priv)
{
	struct batadv_softif_vlan *vlan;
	u8 ap_isolation;

	vlan = batadv_softif_vlan_get(bat_priv, BATADV_NO_FLAGS);
	if (!vlan)
		return 0;

	ap_isolation = atomic_read(&vlan->ap_isolation);
	batadv_softif_vlan_put(vlan);

	return nla_put_u8(msg, BATADV_ATTR_AP_ISOLATION_ENABLED,
			  !!ap_isolation);
}

/**
 * batadv_option_set_ap_isolation() - Set ap_isolation from genl msg
 * @attr: parsed BATADV_ATTR_AP_ISOLATION_ENABLED attribute
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_set_mesh_ap_isolation(struct nlattr *attr,
						struct batadv_priv *bat_priv)
{
	struct batadv_softif_vlan *vlan;

	vlan = batadv_softif_vlan_get(bat_priv, BATADV_NO_FLAGS);
	if (!vlan)
		return -ENOENT;

	atomic_set(&vlan->ap_isolation, !!nla_get_u8(attr));
	batadv_softif_vlan_put(vlan);

	return 0;
}

/**
 * batadv_netlink_mesh_fill() - Fill message with mesh attributes
 * @msg: Netlink message to dump into
 * @bat_priv: the bat priv with all the soft interface information
 * @cmd: type of message to generate
 * @portid: Port making netlink request
 * @seq: sequence number for message
 * @flags: Additional flags for message
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_mesh_fill(struct sk_buff *msg,
				    struct batadv_priv *bat_priv,
				    enum batadv_nl_commands cmd,
				    u32 portid, u32 seq, int flags)
{
	struct net_device *soft_iface = bat_priv->soft_iface;
	struct batadv_hard_iface *primary_if = NULL;
	struct net_device *hard_iface;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &batadv_netlink_family, flags, cmd);
	if (!hdr)
		return -ENOBUFS;

	if (nla_put_string(msg, BATADV_ATTR_VERSION, BATADV_SOURCE_VERSION) ||
	    nla_put_string(msg, BATADV_ATTR_ALGO_NAME,
			   bat_priv->algo_ops->name) ||
	    nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX, soft_iface->ifindex) ||
	    nla_put_string(msg, BATADV_ATTR_MESH_IFNAME, soft_iface->name) ||
	    nla_put(msg, BATADV_ATTR_MESH_ADDRESS, ETH_ALEN,
		    soft_iface->dev_addr) ||
	    nla_put_u8(msg, BATADV_ATTR_TT_TTVN,
		       (u8)atomic_read(&bat_priv->tt.vn)))
		goto nla_put_failure;

#ifdef CONFIG_BATMAN_ADV_BLA
	if (nla_put_u16(msg, BATADV_ATTR_BLA_CRC,
			ntohs(bat_priv->bla.claim_dest.group)))
		goto nla_put_failure;
#endif

	if (batadv_mcast_mesh_info_put(msg, bat_priv))
		goto nla_put_failure;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (primary_if && primary_if->if_status == BATADV_IF_ACTIVE) {
		hard_iface = primary_if->net_dev;

		if (nla_put_u32(msg, BATADV_ATTR_HARD_IFINDEX,
				hard_iface->ifindex) ||
		    nla_put_string(msg, BATADV_ATTR_HARD_IFNAME,
				   hard_iface->name) ||
		    nla_put(msg, BATADV_ATTR_HARD_ADDRESS, ETH_ALEN,
			    hard_iface->dev_addr))
			goto nla_put_failure;
	}

	if (nla_put_u8(msg, BATADV_ATTR_AGGREGATED_OGMS_ENABLED,
		       !!atomic_read(&bat_priv->aggregated_ogms)))
		goto nla_put_failure;

	if (batadv_netlink_mesh_fill_ap_isolation(msg, bat_priv))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_ISOLATION_MARK,
			bat_priv->isolation_mark))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_ISOLATION_MASK,
			bat_priv->isolation_mark_mask))
		goto nla_put_failure;

	if (nla_put_u8(msg, BATADV_ATTR_BONDING_ENABLED,
		       !!atomic_read(&bat_priv->bonding)))
		goto nla_put_failure;

#ifdef CONFIG_BATMAN_ADV_BLA
	if (nla_put_u8(msg, BATADV_ATTR_BRIDGE_LOOP_AVOIDANCE_ENABLED,
		       !!atomic_read(&bat_priv->bridge_loop_avoidance)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_BLA */

#ifdef CONFIG_BATMAN_ADV_DAT
	if (nla_put_u8(msg, BATADV_ATTR_DISTRIBUTED_ARP_TABLE_ENABLED,
		       !!atomic_read(&bat_priv->distributed_arp_table)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_DAT */

	if (nla_put_u8(msg, BATADV_ATTR_FRAGMENTATION_ENABLED,
		       !!atomic_read(&bat_priv->fragmentation)))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_GW_BANDWIDTH_DOWN,
			atomic_read(&bat_priv->gw.bandwidth_down)))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_GW_BANDWIDTH_UP,
			atomic_read(&bat_priv->gw.bandwidth_up)))
		goto nla_put_failure;

	if (nla_put_u8(msg, BATADV_ATTR_GW_MODE,
		       atomic_read(&bat_priv->gw.mode)))
		goto nla_put_failure;

	if (bat_priv->algo_ops->gw.get_best_gw_node &&
	    bat_priv->algo_ops->gw.is_eligible) {
		/* GW selection class is not available if the routing algorithm
		 * in use does not implement the GW API
		 */
		if (nla_put_u32(msg, BATADV_ATTR_GW_SEL_CLASS,
				atomic_read(&bat_priv->gw.sel_class)))
			goto nla_put_failure;
	}

	if (nla_put_u8(msg, BATADV_ATTR_HOP_PENALTY,
		       atomic_read(&bat_priv->hop_penalty)))
		goto nla_put_failure;

#ifdef CONFIG_BATMAN_ADV_DEBUG
	if (nla_put_u32(msg, BATADV_ATTR_LOG_LEVEL,
			atomic_read(&bat_priv->log_level)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_DEBUG */

#ifdef CONFIG_BATMAN_ADV_MCAST
	if (nla_put_u8(msg, BATADV_ATTR_MULTICAST_FORCEFLOOD_ENABLED,
		       !atomic_read(&bat_priv->multicast_mode)))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_MULTICAST_FANOUT,
			atomic_read(&bat_priv->multicast_fanout)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_MCAST */

#ifdef CONFIG_BATMAN_ADV_NC
	if (nla_put_u8(msg, BATADV_ATTR_NETWORK_CODING_ENABLED,
		       !!atomic_read(&bat_priv->network_coding)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_NC */

	if (nla_put_u32(msg, BATADV_ATTR_ORIG_INTERVAL,
			atomic_read(&bat_priv->orig_interval)))
		goto nla_put_failure;

	if (primary_if)
		batadv_hardif_put(primary_if);

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	if (primary_if)
		batadv_hardif_put(primary_if);

	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/**
 * batadv_netlink_notify_mesh() - send softif attributes to listener
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success, < 0 on error
 */
int batadv_netlink_notify_mesh(struct batadv_priv *bat_priv)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_mesh_fill(msg, bat_priv, BATADV_CMD_SET_MESH,
				       0, 0, 0);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	genlmsg_multicast_netns(&batadv_netlink_family,
				dev_net(bat_priv->soft_iface), msg, 0,
				BATADV_NL_MCGRP_CONFIG, GFP_KERNEL);

	return 0;
}

/**
 * batadv_netlink_get_mesh() - Get softif attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_get_mesh(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_mesh_fill(msg, bat_priv, BATADV_CMD_GET_MESH,
				       info->snd_portid, info->snd_seq, 0);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	ret = genlmsg_reply(msg, info);

	return ret;
}

/**
 * batadv_netlink_set_mesh() - Set softif attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_set_mesh(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct nlattr *attr;

	if (info->attrs[BATADV_ATTR_AGGREGATED_OGMS_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_AGGREGATED_OGMS_ENABLED];

		atomic_set(&bat_priv->aggregated_ogms, !!nla_get_u8(attr));
	}

	if (info->attrs[BATADV_ATTR_AP_ISOLATION_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_AP_ISOLATION_ENABLED];

		batadv_netlink_set_mesh_ap_isolation(attr, bat_priv);
	}

	if (info->attrs[BATADV_ATTR_ISOLATION_MARK]) {
		attr = info->attrs[BATADV_ATTR_ISOLATION_MARK];

		bat_priv->isolation_mark = nla_get_u32(attr);
	}

	if (info->attrs[BATADV_ATTR_ISOLATION_MASK]) {
		attr = info->attrs[BATADV_ATTR_ISOLATION_MASK];

		bat_priv->isolation_mark_mask = nla_get_u32(attr);
	}

	if (info->attrs[BATADV_ATTR_BONDING_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_BONDING_ENABLED];

		atomic_set(&bat_priv->bonding, !!nla_get_u8(attr));
	}

#ifdef CONFIG_BATMAN_ADV_BLA
	if (info->attrs[BATADV_ATTR_BRIDGE_LOOP_AVOIDANCE_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_BRIDGE_LOOP_AVOIDANCE_ENABLED];

		atomic_set(&bat_priv->bridge_loop_avoidance,
			   !!nla_get_u8(attr));
		batadv_bla_status_update(bat_priv->soft_iface);
	}
#endif /* CONFIG_BATMAN_ADV_BLA */

#ifdef CONFIG_BATMAN_ADV_DAT
	if (info->attrs[BATADV_ATTR_DISTRIBUTED_ARP_TABLE_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_DISTRIBUTED_ARP_TABLE_ENABLED];

		atomic_set(&bat_priv->distributed_arp_table,
			   !!nla_get_u8(attr));
		batadv_dat_status_update(bat_priv->soft_iface);
	}
#endif /* CONFIG_BATMAN_ADV_DAT */

	if (info->attrs[BATADV_ATTR_FRAGMENTATION_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_FRAGMENTATION_ENABLED];

		atomic_set(&bat_priv->fragmentation, !!nla_get_u8(attr));
		batadv_update_min_mtu(bat_priv->soft_iface);
	}

	if (info->attrs[BATADV_ATTR_GW_BANDWIDTH_DOWN]) {
		attr = info->attrs[BATADV_ATTR_GW_BANDWIDTH_DOWN];

		atomic_set(&bat_priv->gw.bandwidth_down, nla_get_u32(attr));
		batadv_gw_tvlv_container_update(bat_priv);
	}

	if (info->attrs[BATADV_ATTR_GW_BANDWIDTH_UP]) {
		attr = info->attrs[BATADV_ATTR_GW_BANDWIDTH_UP];

		atomic_set(&bat_priv->gw.bandwidth_up, nla_get_u32(attr));
		batadv_gw_tvlv_container_update(bat_priv);
	}

	if (info->attrs[BATADV_ATTR_GW_MODE]) {
		u8 gw_mode;

		attr = info->attrs[BATADV_ATTR_GW_MODE];
		gw_mode = nla_get_u8(attr);

		if (gw_mode <= BATADV_GW_MODE_SERVER) {
			/* Invoking batadv_gw_reselect() is not enough to really
			 * de-select the current GW. It will only instruct the
			 * gateway client code to perform a re-election the next
			 * time that this is needed.
			 *
			 * When gw client mode is being switched off the current
			 * GW must be de-selected explicitly otherwise no GW_ADD
			 * uevent is thrown on client mode re-activation. This
			 * is operation is performed in
			 * batadv_gw_check_client_stop().
			 */
			batadv_gw_reselect(bat_priv);

			/* always call batadv_gw_check_client_stop() before
			 * changing the gateway state
			 */
			batadv_gw_check_client_stop(bat_priv);
			atomic_set(&bat_priv->gw.mode, gw_mode);
			batadv_gw_tvlv_container_update(bat_priv);
		}
	}

	if (info->attrs[BATADV_ATTR_GW_SEL_CLASS] &&
	    bat_priv->algo_ops->gw.get_best_gw_node &&
	    bat_priv->algo_ops->gw.is_eligible) {
		/* setting the GW selection class is allowed only if the routing
		 * algorithm in use implements the GW API
		 */

		u32 sel_class_max = 0xffffffffu;
		u32 sel_class;

		attr = info->attrs[BATADV_ATTR_GW_SEL_CLASS];
		sel_class = nla_get_u32(attr);

		if (!bat_priv->algo_ops->gw.store_sel_class)
			sel_class_max = BATADV_TQ_MAX_VALUE;

		if (sel_class >= 1 && sel_class <= sel_class_max) {
			atomic_set(&bat_priv->gw.sel_class, sel_class);
			batadv_gw_reselect(bat_priv);
		}
	}

	if (info->attrs[BATADV_ATTR_HOP_PENALTY]) {
		attr = info->attrs[BATADV_ATTR_HOP_PENALTY];

		atomic_set(&bat_priv->hop_penalty, nla_get_u8(attr));
	}

#ifdef CONFIG_BATMAN_ADV_DEBUG
	if (info->attrs[BATADV_ATTR_LOG_LEVEL]) {
		attr = info->attrs[BATADV_ATTR_LOG_LEVEL];

		atomic_set(&bat_priv->log_level,
			   nla_get_u32(attr) & BATADV_DBG_ALL);
	}
#endif /* CONFIG_BATMAN_ADV_DEBUG */

#ifdef CONFIG_BATMAN_ADV_MCAST
	if (info->attrs[BATADV_ATTR_MULTICAST_FORCEFLOOD_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_MULTICAST_FORCEFLOOD_ENABLED];

		atomic_set(&bat_priv->multicast_mode, !nla_get_u8(attr));
	}

	if (info->attrs[BATADV_ATTR_MULTICAST_FANOUT]) {
		attr = info->attrs[BATADV_ATTR_MULTICAST_FANOUT];

		atomic_set(&bat_priv->multicast_fanout, nla_get_u32(attr));
	}
#endif /* CONFIG_BATMAN_ADV_MCAST */

#ifdef CONFIG_BATMAN_ADV_NC
	if (info->attrs[BATADV_ATTR_NETWORK_CODING_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_NETWORK_CODING_ENABLED];

		atomic_set(&bat_priv->network_coding, !!nla_get_u8(attr));
		batadv_nc_status_update(bat_priv->soft_iface);
	}
#endif /* CONFIG_BATMAN_ADV_NC */

	if (info->attrs[BATADV_ATTR_ORIG_INTERVAL]) {
		u32 orig_interval;

		attr = info->attrs[BATADV_ATTR_ORIG_INTERVAL];
		orig_interval = nla_get_u32(attr);

		orig_interval = min_t(u32, orig_interval, INT_MAX);
		orig_interval = max_t(u32, orig_interval, 2 * BATADV_JITTER);

		atomic_set(&bat_priv->orig_interval, orig_interval);
	}

	batadv_netlink_notify_mesh(bat_priv);

	return 0;
}

/**
 * batadv_netlink_tp_meter_put() - Fill information of started tp_meter session
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
 * batadv_netlink_tpmeter_notify() - send tp_meter result via netlink to client
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
 * batadv_netlink_tp_meter_start() - Start a new tp_meter session
 * @skb: received netlink message
 * @info: receiver information
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_tp_meter_start(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct sk_buff *msg = NULL;
	u32 test_length;
	void *msg_head;
	u32 cookie;
	u8 *dst;
	int ret;

	if (!info->attrs[BATADV_ATTR_ORIG_ADDRESS])
		return -EINVAL;

	if (!info->attrs[BATADV_ATTR_TPMETER_TEST_TIME])
		return -EINVAL;

	dst = nla_data(info->attrs[BATADV_ATTR_ORIG_ADDRESS]);

	test_length = nla_get_u32(info->attrs[BATADV_ATTR_TPMETER_TEST_TIME]);

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

	batadv_tp_start(bat_priv, dst, test_length, &cookie);

	ret = batadv_netlink_tp_meter_put(msg, cookie);

 out:
	if (ret) {
		if (msg)
			nlmsg_free(msg);
		return ret;
	}

	genlmsg_end(msg, msg_head);
	return genlmsg_reply(msg, info);
}

/**
 * batadv_netlink_tp_meter_start() - Cancel a running tp_meter session
 * @skb: received netlink message
 * @info: receiver information
 *
 * Return: 0 on success, < 0 on error
 */
static int
batadv_netlink_tp_meter_cancel(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_priv *bat_priv = info->user_ptr[0];
	u8 *dst;
	int ret = 0;

	if (!info->attrs[BATADV_ATTR_ORIG_ADDRESS])
		return -EINVAL;

	dst = nla_data(info->attrs[BATADV_ATTR_ORIG_ADDRESS]);

	batadv_tp_stop(bat_priv, dst, BATADV_TP_REASON_CANCEL);

	return ret;
}

/**
 * batadv_netlink_hardif_fill() - Fill message with hardif attributes
 * @msg: Netlink message to dump into
 * @bat_priv: the bat priv with all the soft interface information
 * @hard_iface: hard interface which was modified
 * @cmd: type of message to generate
 * @portid: Port making netlink request
 * @seq: sequence number for message
 * @flags: Additional flags for message
 * @cb: Control block containing additional options
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_hardif_fill(struct sk_buff *msg,
				      struct batadv_priv *bat_priv,
				      struct batadv_hard_iface *hard_iface,
				      enum batadv_nl_commands cmd,
				      u32 portid, u32 seq, int flags,
				      struct netlink_callback *cb)
{
	struct net_device *net_dev = hard_iface->net_dev;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &batadv_netlink_family, flags, cmd);
	if (!hdr)
		return -ENOBUFS;

	if (cb)
		genl_dump_check_consistent(cb, hdr);

	if (nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX,
			bat_priv->soft_iface->ifindex))
		goto nla_put_failure;

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

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	if (nla_put_u32(msg, BATADV_ATTR_ELP_INTERVAL,
			atomic_read(&hard_iface->bat_v.elp_interval)))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_THROUGHPUT_OVERRIDE,
			atomic_read(&hard_iface->bat_v.throughput_override)))
		goto nla_put_failure;
#endif /* CONFIG_BATMAN_ADV_BATMAN_V */

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/**
 * batadv_netlink_notify_hardif() - send hardif attributes to listener
 * @bat_priv: the bat priv with all the soft interface information
 * @hard_iface: hard interface which was modified
 *
 * Return: 0 on success, < 0 on error
 */
int batadv_netlink_notify_hardif(struct batadv_priv *bat_priv,
				 struct batadv_hard_iface *hard_iface)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_hardif_fill(msg, bat_priv, hard_iface,
					 BATADV_CMD_SET_HARDIF, 0, 0, 0, NULL);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	genlmsg_multicast_netns(&batadv_netlink_family,
				dev_net(bat_priv->soft_iface), msg, 0,
				BATADV_NL_MCGRP_CONFIG, GFP_KERNEL);

	return 0;
}

/**
 * batadv_netlink_get_hardif() - Get hardif attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_get_hardif(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct batadv_hard_iface *hard_iface = info->user_ptr[1];
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_hardif_fill(msg, bat_priv, hard_iface,
					 BATADV_CMD_GET_HARDIF,
					 info->snd_portid, info->snd_seq, 0,
					 NULL);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	ret = genlmsg_reply(msg, info);

	return ret;
}

/**
 * batadv_netlink_set_hardif() - Set hardif attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_set_hardif(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct batadv_hard_iface *hard_iface = info->user_ptr[1];
	struct batadv_priv *bat_priv = info->user_ptr[0];

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	struct nlattr *attr;

	if (info->attrs[BATADV_ATTR_ELP_INTERVAL]) {
		attr = info->attrs[BATADV_ATTR_ELP_INTERVAL];

		atomic_set(&hard_iface->bat_v.elp_interval, nla_get_u32(attr));
	}

	if (info->attrs[BATADV_ATTR_THROUGHPUT_OVERRIDE]) {
		attr = info->attrs[BATADV_ATTR_THROUGHPUT_OVERRIDE];

		atomic_set(&hard_iface->bat_v.throughput_override,
			   nla_get_u32(attr));
	}
#endif /* CONFIG_BATMAN_ADV_BATMAN_V */

	batadv_netlink_notify_hardif(bat_priv, hard_iface);

	return 0;
}

/**
 * batadv_netlink_dump_hardif() - Dump all hard interface into a messages
 * @msg: Netlink message to dump into
 * @cb: Parameters from query
 *
 * Return: error code, or length of reply message on success
 */
static int
batadv_netlink_dump_hardif(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct net *net = sock_net(cb->skb->sk);
	struct net_device *soft_iface;
	struct batadv_hard_iface *hard_iface;
	struct batadv_priv *bat_priv;
	int ifindex;
	int portid = NETLINK_CB(cb->skb).portid;
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

	bat_priv = netdev_priv(soft_iface);

	rtnl_lock();
	cb->seq = batadv_hardif_generation << 1 | 1;

	list_for_each_entry(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != soft_iface)
			continue;

		if (i++ < skip)
			continue;

		if (batadv_netlink_hardif_fill(msg, bat_priv, hard_iface,
					       BATADV_CMD_GET_HARDIF,
					       portid, cb->nlh->nlmsg_seq,
					       NLM_F_MULTI, cb)) {
			i--;
			break;
		}
	}

	rtnl_unlock();

	dev_put(soft_iface);

	cb->args[0] = i;

	return msg->len;
}

/**
 * batadv_netlink_vlan_fill() - Fill message with vlan attributes
 * @msg: Netlink message to dump into
 * @bat_priv: the bat priv with all the soft interface information
 * @vlan: vlan which was modified
 * @cmd: type of message to generate
 * @portid: Port making netlink request
 * @seq: sequence number for message
 * @flags: Additional flags for message
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_vlan_fill(struct sk_buff *msg,
				    struct batadv_priv *bat_priv,
				    struct batadv_softif_vlan *vlan,
				    enum batadv_nl_commands cmd,
				    u32 portid, u32 seq, int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &batadv_netlink_family, flags, cmd);
	if (!hdr)
		return -ENOBUFS;

	if (nla_put_u32(msg, BATADV_ATTR_MESH_IFINDEX,
			bat_priv->soft_iface->ifindex))
		goto nla_put_failure;

	if (nla_put_u32(msg, BATADV_ATTR_VLANID, vlan->vid & VLAN_VID_MASK))
		goto nla_put_failure;

	if (nla_put_u8(msg, BATADV_ATTR_AP_ISOLATION_ENABLED,
		       !!atomic_read(&vlan->ap_isolation)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/**
 * batadv_netlink_notify_vlan() - send vlan attributes to listener
 * @bat_priv: the bat priv with all the soft interface information
 * @vlan: vlan which was modified
 *
 * Return: 0 on success, < 0 on error
 */
int batadv_netlink_notify_vlan(struct batadv_priv *bat_priv,
			       struct batadv_softif_vlan *vlan)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_vlan_fill(msg, bat_priv, vlan,
				       BATADV_CMD_SET_VLAN, 0, 0, 0);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	genlmsg_multicast_netns(&batadv_netlink_family,
				dev_net(bat_priv->soft_iface), msg, 0,
				BATADV_NL_MCGRP_CONFIG, GFP_KERNEL);

	return 0;
}

/**
 * batadv_netlink_get_vlan() - Get vlan attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_get_vlan(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_softif_vlan *vlan = info->user_ptr[1];
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = batadv_netlink_vlan_fill(msg, bat_priv, vlan, BATADV_CMD_GET_VLAN,
				       info->snd_portid, info->snd_seq, 0);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	ret = genlmsg_reply(msg, info);

	return ret;
}

/**
 * batadv_netlink_set_vlan() - Get vlan attributes
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_netlink_set_vlan(struct sk_buff *skb, struct genl_info *info)
{
	struct batadv_softif_vlan *vlan = info->user_ptr[1];
	struct batadv_priv *bat_priv = info->user_ptr[0];
	struct nlattr *attr;

	if (info->attrs[BATADV_ATTR_AP_ISOLATION_ENABLED]) {
		attr = info->attrs[BATADV_ATTR_AP_ISOLATION_ENABLED];

		atomic_set(&vlan->ap_isolation, !!nla_get_u8(attr));
	}

	batadv_netlink_notify_vlan(bat_priv, vlan);

	return 0;
}

/**
 * batadv_get_softif_from_info() - Retrieve soft interface from genl attributes
 * @net: the applicable net namespace
 * @info: receiver information
 *
 * Return: Pointer to soft interface (with increased refcnt) on success, error
 *  pointer on error
 */
static struct net_device *
batadv_get_softif_from_info(struct net *net, struct genl_info *info)
{
	struct net_device *soft_iface;
	int ifindex;

	if (!info->attrs[BATADV_ATTR_MESH_IFINDEX])
		return ERR_PTR(-EINVAL);

	ifindex = nla_get_u32(info->attrs[BATADV_ATTR_MESH_IFINDEX]);

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface)
		return ERR_PTR(-ENODEV);

	if (!batadv_softif_is_valid(soft_iface))
		goto err_put_softif;

	return soft_iface;

err_put_softif:
	dev_put(soft_iface);

	return ERR_PTR(-EINVAL);
}

/**
 * batadv_get_hardif_from_info() - Retrieve hardif from genl attributes
 * @bat_priv: the bat priv with all the soft interface information
 * @net: the applicable net namespace
 * @info: receiver information
 *
 * Return: Pointer to hard interface (with increased refcnt) on success, error
 *  pointer on error
 */
static struct batadv_hard_iface *
batadv_get_hardif_from_info(struct batadv_priv *bat_priv, struct net *net,
			    struct genl_info *info)
{
	struct batadv_hard_iface *hard_iface;
	struct net_device *hard_dev;
	unsigned int hardif_index;

	if (!info->attrs[BATADV_ATTR_HARD_IFINDEX])
		return ERR_PTR(-EINVAL);

	hardif_index = nla_get_u32(info->attrs[BATADV_ATTR_HARD_IFINDEX]);

	hard_dev = dev_get_by_index(net, hardif_index);
	if (!hard_dev)
		return ERR_PTR(-ENODEV);

	hard_iface = batadv_hardif_get_by_netdev(hard_dev);
	if (!hard_iface)
		goto err_put_harddev;

	if (hard_iface->soft_iface != bat_priv->soft_iface)
		goto err_put_hardif;

	/* hard_dev is referenced by hard_iface and not needed here */
	dev_put(hard_dev);

	return hard_iface;

err_put_hardif:
	batadv_hardif_put(hard_iface);
err_put_harddev:
	dev_put(hard_dev);

	return ERR_PTR(-EINVAL);
}

/**
 * batadv_get_vlan_from_info() - Retrieve vlan from genl attributes
 * @bat_priv: the bat priv with all the soft interface information
 * @net: the applicable net namespace
 * @info: receiver information
 *
 * Return: Pointer to vlan on success (with increased refcnt), error pointer
 *  on error
 */
static struct batadv_softif_vlan *
batadv_get_vlan_from_info(struct batadv_priv *bat_priv, struct net *net,
			  struct genl_info *info)
{
	struct batadv_softif_vlan *vlan;
	u16 vid;

	if (!info->attrs[BATADV_ATTR_VLANID])
		return ERR_PTR(-EINVAL);

	vid = nla_get_u16(info->attrs[BATADV_ATTR_VLANID]);

	vlan = batadv_softif_vlan_get(bat_priv, vid | BATADV_VLAN_HAS_TAG);
	if (!vlan)
		return ERR_PTR(-ENOENT);

	return vlan;
}

/**
 * batadv_pre_doit() - Prepare batman-adv genl doit request
 * @ops: requested netlink operation
 * @skb: Netlink message with request data
 * @info: receiver information
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			   struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct batadv_hard_iface *hard_iface;
	struct batadv_priv *bat_priv = NULL;
	struct batadv_softif_vlan *vlan;
	struct net_device *soft_iface;
	u8 user_ptr1_flags;
	u8 mesh_dep_flags;
	int ret;

	user_ptr1_flags = BATADV_FLAG_NEED_HARDIF | BATADV_FLAG_NEED_VLAN;
	if (WARN_ON(hweight8(ops->internal_flags & user_ptr1_flags) > 1))
		return -EINVAL;

	mesh_dep_flags = BATADV_FLAG_NEED_HARDIF | BATADV_FLAG_NEED_VLAN;
	if (WARN_ON((ops->internal_flags & mesh_dep_flags) &&
		    (~ops->internal_flags & BATADV_FLAG_NEED_MESH)))
		return -EINVAL;

	if (ops->internal_flags & BATADV_FLAG_NEED_MESH) {
		soft_iface = batadv_get_softif_from_info(net, info);
		if (IS_ERR(soft_iface))
			return PTR_ERR(soft_iface);

		bat_priv = netdev_priv(soft_iface);
		info->user_ptr[0] = bat_priv;
	}

	if (ops->internal_flags & BATADV_FLAG_NEED_HARDIF) {
		hard_iface = batadv_get_hardif_from_info(bat_priv, net, info);
		if (IS_ERR(hard_iface)) {
			ret = PTR_ERR(hard_iface);
			goto err_put_softif;
		}

		info->user_ptr[1] = hard_iface;
	}

	if (ops->internal_flags & BATADV_FLAG_NEED_VLAN) {
		vlan = batadv_get_vlan_from_info(bat_priv, net, info);
		if (IS_ERR(vlan)) {
			ret = PTR_ERR(vlan);
			goto err_put_softif;
		}

		info->user_ptr[1] = vlan;
	}

	return 0;

err_put_softif:
	if (bat_priv)
		dev_put(bat_priv->soft_iface);

	return ret;
}

/**
 * batadv_post_doit() - End batman-adv genl doit request
 * @ops: requested netlink operation
 * @skb: Netlink message with request data
 * @info: receiver information
 */
static void batadv_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			     struct genl_info *info)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_softif_vlan *vlan;
	struct batadv_priv *bat_priv;

	if (ops->internal_flags & BATADV_FLAG_NEED_HARDIF &&
	    info->user_ptr[1]) {
		hard_iface = info->user_ptr[1];

		batadv_hardif_put(hard_iface);
	}

	if (ops->internal_flags & BATADV_FLAG_NEED_VLAN && info->user_ptr[1]) {
		vlan = info->user_ptr[1];
		batadv_softif_vlan_put(vlan);
	}

	if (ops->internal_flags & BATADV_FLAG_NEED_MESH && info->user_ptr[0]) {
		bat_priv = info->user_ptr[0];
		dev_put(bat_priv->soft_iface);
	}
}

static const struct genl_ops batadv_netlink_ops[] = {
	{
		.cmd = BATADV_CMD_GET_MESH,
		/* can be retrieved by unprivileged users */
		.doit = batadv_netlink_get_mesh,
		.internal_flags = BATADV_FLAG_NEED_MESH,
	},
	{
		.cmd = BATADV_CMD_TP_METER,
		.flags = GENL_ADMIN_PERM,
		.doit = batadv_netlink_tp_meter_start,
		.internal_flags = BATADV_FLAG_NEED_MESH,
	},
	{
		.cmd = BATADV_CMD_TP_METER_CANCEL,
		.flags = GENL_ADMIN_PERM,
		.doit = batadv_netlink_tp_meter_cancel,
		.internal_flags = BATADV_FLAG_NEED_MESH,
	},
	{
		.cmd = BATADV_CMD_GET_ROUTING_ALGOS,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_algo_dump,
	},
	{
		.cmd = BATADV_CMD_GET_HARDIF,
		/* can be retrieved by unprivileged users */
		.dumpit = batadv_netlink_dump_hardif,
		.doit = batadv_netlink_get_hardif,
		.internal_flags = BATADV_FLAG_NEED_MESH |
				  BATADV_FLAG_NEED_HARDIF,
	},
	{
		.cmd = BATADV_CMD_GET_TRANSTABLE_LOCAL,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_tt_local_dump,
	},
	{
		.cmd = BATADV_CMD_GET_TRANSTABLE_GLOBAL,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_tt_global_dump,
	},
	{
		.cmd = BATADV_CMD_GET_ORIGINATORS,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_orig_dump,
	},
	{
		.cmd = BATADV_CMD_GET_NEIGHBORS,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_hardif_neigh_dump,
	},
	{
		.cmd = BATADV_CMD_GET_GATEWAYS,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_gw_dump,
	},
	{
		.cmd = BATADV_CMD_GET_BLA_CLAIM,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_bla_claim_dump,
	},
	{
		.cmd = BATADV_CMD_GET_BLA_BACKBONE,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_bla_backbone_dump,
	},
	{
		.cmd = BATADV_CMD_GET_DAT_CACHE,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_dat_cache_dump,
	},
	{
		.cmd = BATADV_CMD_GET_MCAST_FLAGS,
		.flags = GENL_ADMIN_PERM,
		.dumpit = batadv_mcast_flags_dump,
	},
	{
		.cmd = BATADV_CMD_SET_MESH,
		.flags = GENL_ADMIN_PERM,
		.doit = batadv_netlink_set_mesh,
		.internal_flags = BATADV_FLAG_NEED_MESH,
	},
	{
		.cmd = BATADV_CMD_SET_HARDIF,
		.flags = GENL_ADMIN_PERM,
		.doit = batadv_netlink_set_hardif,
		.internal_flags = BATADV_FLAG_NEED_MESH |
				  BATADV_FLAG_NEED_HARDIF,
	},
	{
		.cmd = BATADV_CMD_GET_VLAN,
		/* can be retrieved by unprivileged users */
		.doit = batadv_netlink_get_vlan,
		.internal_flags = BATADV_FLAG_NEED_MESH |
				  BATADV_FLAG_NEED_VLAN,
	},
	{
		.cmd = BATADV_CMD_SET_VLAN,
		.flags = GENL_ADMIN_PERM,
		.doit = batadv_netlink_set_vlan,
		.internal_flags = BATADV_FLAG_NEED_MESH |
				  BATADV_FLAG_NEED_VLAN,
	},
};

struct genl_family batadv_netlink_family __ro_after_init = {
	.hdrsize = 0,
	.name = BATADV_NL_NAME,
	.version = 1,
	.maxattr = BATADV_ATTR_MAX,
	.policy = batadv_netlink_policy,
	.netnsok = true,
	.pre_doit = batadv_pre_doit,
	.post_doit = batadv_post_doit,
	.module = THIS_MODULE,
	.ops = batadv_netlink_ops,
	.n_ops = ARRAY_SIZE(batadv_netlink_ops),
	.mcgrps = batadv_netlink_mcgrps,
	.n_mcgrps = ARRAY_SIZE(batadv_netlink_mcgrps),
};

/**
 * batadv_netlink_register() - register batadv genl netlink family
 */
void __init batadv_netlink_register(void)
{
	int ret;

	ret = genl_register_family(&batadv_netlink_family);
	if (ret)
		pr_warn("unable to register netlink family");
}

/**
 * batadv_netlink_unregister() - unregister batadv genl netlink family
 */
void batadv_netlink_unregister(void)
{
	genl_unregister_family(&batadv_netlink_family);
}
