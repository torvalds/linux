// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Bootlin
 *
 */
#include "common.h"
#include "netlink.h"

#include <linux/phy.h>
#include <linux/phy_link_topology.h>
#include <linux/sfp.h>
#include <net/netdev_lock.h>

struct phy_req_info {
	struct ethnl_req_info base;
};

struct phy_reply_data {
	struct ethnl_reply_data	base;
	u32 phyindex;
	char *drvname;
	char *name;
	unsigned int upstream_type;
	char *upstream_sfp_name;
	unsigned int upstream_index;
	char *downstream_sfp_name;
};

#define PHY_REPDATA(__reply_base) \
	container_of(__reply_base, struct phy_reply_data, base)

const struct nla_policy ethnl_phy_get_policy[ETHTOOL_A_PHY_HEADER + 1] = {
	[ETHTOOL_A_PHY_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static int phy_reply_size(const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data)
{
	struct phy_reply_data *rep_data = PHY_REPDATA(reply_data);
	size_t size = 0;

	/* ETHTOOL_A_PHY_INDEX */
	size += nla_total_size(sizeof(u32));

	/* ETHTOOL_A_DRVNAME */
	if (rep_data->drvname)
		size += nla_total_size(strlen(rep_data->drvname) + 1);

	/* ETHTOOL_A_NAME */
	size += nla_total_size(strlen(rep_data->name) + 1);

	/* ETHTOOL_A_PHY_UPSTREAM_TYPE */
	size += nla_total_size(sizeof(u32));

	/* ETHTOOL_A_PHY_UPSTREAM_SFP_NAME */
	if (rep_data->upstream_sfp_name)
		size += nla_total_size(strlen(rep_data->upstream_sfp_name) + 1);

	/* ETHTOOL_A_PHY_UPSTREAM_INDEX */
	if (rep_data->upstream_index)
		size += nla_total_size(sizeof(u32));

	/* ETHTOOL_A_PHY_DOWNSTREAM_SFP_NAME */
	if (rep_data->downstream_sfp_name)
		size += nla_total_size(strlen(rep_data->downstream_sfp_name) + 1);

	return size;
}

static int phy_prepare_data(const struct ethnl_req_info *req_info,
			    struct ethnl_reply_data *reply_data,
			    const struct genl_info *info)
{
	struct phy_link_topology *topo = reply_data->dev->link_topo;
	struct phy_reply_data *rep_data = PHY_REPDATA(reply_data);
	struct nlattr **tb = info->attrs;
	struct phy_device_node *pdn;
	struct phy_device *phydev;

	/* RTNL is held by the caller */
	phydev = ethnl_req_get_phydev(req_info, tb, ETHTOOL_A_PHY_HEADER,
				      info->extack);
	if (IS_ERR_OR_NULL(phydev))
		return -EOPNOTSUPP;

	pdn = xa_load(&topo->phys, phydev->phyindex);
	if (!pdn)
		return -EOPNOTSUPP;

	rep_data->phyindex = phydev->phyindex;
	rep_data->name = kstrdup(dev_name(&phydev->mdio.dev), GFP_KERNEL);
	rep_data->drvname = kstrdup(phydev->drv->name, GFP_KERNEL);
	rep_data->upstream_type = pdn->upstream_type;

	if (pdn->upstream_type == PHY_UPSTREAM_PHY) {
		struct phy_device *upstream = pdn->upstream.phydev;
		rep_data->upstream_index = upstream->phyindex;
	}

	if (pdn->parent_sfp_bus)
		rep_data->upstream_sfp_name = kstrdup(sfp_get_name(pdn->parent_sfp_bus),
						      GFP_KERNEL);

	if (phydev->sfp_bus)
		rep_data->downstream_sfp_name = kstrdup(sfp_get_name(phydev->sfp_bus),
							GFP_KERNEL);

	return 0;
}

static int phy_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data)
{
	struct phy_reply_data *rep_data = PHY_REPDATA(reply_data);

	if (nla_put_u32(skb, ETHTOOL_A_PHY_INDEX, rep_data->phyindex) ||
	    nla_put_string(skb, ETHTOOL_A_PHY_NAME, rep_data->name) ||
	    nla_put_u32(skb, ETHTOOL_A_PHY_UPSTREAM_TYPE, rep_data->upstream_type))
		return -EMSGSIZE;

	if (rep_data->drvname &&
	    nla_put_string(skb, ETHTOOL_A_PHY_DRVNAME, rep_data->drvname))
		return -EMSGSIZE;

	if (rep_data->upstream_index &&
	    nla_put_u32(skb, ETHTOOL_A_PHY_UPSTREAM_INDEX,
			rep_data->upstream_index))
		return -EMSGSIZE;

	if (rep_data->upstream_sfp_name &&
	    nla_put_string(skb, ETHTOOL_A_PHY_UPSTREAM_SFP_NAME,
			   rep_data->upstream_sfp_name))
		return -EMSGSIZE;

	if (rep_data->downstream_sfp_name &&
	    nla_put_string(skb, ETHTOOL_A_PHY_DOWNSTREAM_SFP_NAME,
			   rep_data->downstream_sfp_name))
		return -EMSGSIZE;

	return 0;
}

static void phy_cleanup_data(struct ethnl_reply_data *reply_data)
{
	struct phy_reply_data *rep_data = PHY_REPDATA(reply_data);

	kfree(rep_data->drvname);
	kfree(rep_data->name);
	kfree(rep_data->upstream_sfp_name);
	kfree(rep_data->downstream_sfp_name);
}

const struct ethnl_request_ops ethnl_phy_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PHY_GET,
	.reply_cmd		= ETHTOOL_MSG_PHY_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PHY_HEADER,
	.req_info_size		= sizeof(struct phy_req_info),
	.reply_data_size	= sizeof(struct phy_reply_data),

	.prepare_data		= phy_prepare_data,
	.reply_size		= phy_reply_size,
	.fill_reply		= phy_fill_reply,
	.cleanup_data		= phy_cleanup_data,
};
