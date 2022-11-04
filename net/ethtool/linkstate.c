// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include <linux/phy.h>

struct linkstate_req_info {
	struct ethnl_req_info		base;
};

struct linkstate_reply_data {
	struct ethnl_reply_data			base;
	int					link;
	int					sqi;
	int					sqi_max;
	struct ethtool_link_ext_stats		link_stats;
	bool					link_ext_state_provided;
	struct ethtool_link_ext_state_info	ethtool_link_ext_state_info;
};

#define LINKSTATE_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkstate_reply_data, base)

const struct nla_policy ethnl_linkstate_get_policy[] = {
	[ETHTOOL_A_LINKSTATE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy_stats),
};

static int linkstate_get_sqi(struct net_device *dev)
{
	struct phy_device *phydev = dev->phydev;
	int ret;

	if (!phydev)
		return -EOPNOTSUPP;

	mutex_lock(&phydev->lock);
	if (!phydev->drv || !phydev->drv->get_sqi)
		ret = -EOPNOTSUPP;
	else
		ret = phydev->drv->get_sqi(phydev);
	mutex_unlock(&phydev->lock);

	return ret;
}

static int linkstate_get_sqi_max(struct net_device *dev)
{
	struct phy_device *phydev = dev->phydev;
	int ret;

	if (!phydev)
		return -EOPNOTSUPP;

	mutex_lock(&phydev->lock);
	if (!phydev->drv || !phydev->drv->get_sqi_max)
		ret = -EOPNOTSUPP;
	else
		ret = phydev->drv->get_sqi_max(phydev);
	mutex_unlock(&phydev->lock);

	return ret;
};

static int linkstate_get_link_ext_state(struct net_device *dev,
					struct linkstate_reply_data *data)
{
	int err;

	if (!dev->ethtool_ops->get_link_ext_state)
		return -EOPNOTSUPP;

	err = dev->ethtool_ops->get_link_ext_state(dev, &data->ethtool_link_ext_state_info);
	if (err)
		return err;

	data->link_ext_state_provided = true;

	return 0;
}

static int linkstate_prepare_data(const struct ethnl_req_info *req_base,
				  struct ethnl_reply_data *reply_base,
				  struct genl_info *info)
{
	struct linkstate_reply_data *data = LINKSTATE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	data->link = __ethtool_get_link(dev);

	ret = linkstate_get_sqi(dev);
	if (ret < 0 && ret != -EOPNOTSUPP)
		goto out;
	data->sqi = ret;

	ret = linkstate_get_sqi_max(dev);
	if (ret < 0 && ret != -EOPNOTSUPP)
		goto out;
	data->sqi_max = ret;

	if (dev->flags & IFF_UP) {
		ret = linkstate_get_link_ext_state(dev, data);
		if (ret < 0 && ret != -EOPNOTSUPP && ret != -ENODATA)
			goto out;
	}

	ethtool_stats_init((u64 *)&data->link_stats,
			   sizeof(data->link_stats) / 8);

	if (req_base->flags & ETHTOOL_FLAG_STATS) {
		if (dev->phydev)
			data->link_stats.link_down_events =
				READ_ONCE(dev->phydev->link_down_events);

		if (dev->ethtool_ops->get_link_ext_stats)
			dev->ethtool_ops->get_link_ext_stats(dev,
							     &data->link_stats);
	}

	ret = 0;
out:
	ethnl_ops_complete(dev);
	return ret;
}

static int linkstate_reply_size(const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	struct linkstate_reply_data *data = LINKSTATE_REPDATA(reply_base);
	int len;

	len = nla_total_size(sizeof(u8)) /* LINKSTATE_LINK */
		+ 0;

	if (data->sqi != -EOPNOTSUPP)
		len += nla_total_size(sizeof(u32));

	if (data->sqi_max != -EOPNOTSUPP)
		len += nla_total_size(sizeof(u32));

	if (data->link_ext_state_provided)
		len += nla_total_size(sizeof(u8)); /* LINKSTATE_EXT_STATE */

	if (data->ethtool_link_ext_state_info.__link_ext_substate)
		len += nla_total_size(sizeof(u8)); /* LINKSTATE_EXT_SUBSTATE */

	if (data->link_stats.link_down_events != ETHTOOL_STAT_NOT_SET)
		len += nla_total_size(sizeof(u32));

	return len;
}

static int linkstate_fill_reply(struct sk_buff *skb,
				const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	struct linkstate_reply_data *data = LINKSTATE_REPDATA(reply_base);

	if (data->link >= 0 &&
	    nla_put_u8(skb, ETHTOOL_A_LINKSTATE_LINK, !!data->link))
		return -EMSGSIZE;

	if (data->sqi != -EOPNOTSUPP &&
	    nla_put_u32(skb, ETHTOOL_A_LINKSTATE_SQI, data->sqi))
		return -EMSGSIZE;

	if (data->sqi_max != -EOPNOTSUPP &&
	    nla_put_u32(skb, ETHTOOL_A_LINKSTATE_SQI_MAX, data->sqi_max))
		return -EMSGSIZE;

	if (data->link_ext_state_provided) {
		if (nla_put_u8(skb, ETHTOOL_A_LINKSTATE_EXT_STATE,
			       data->ethtool_link_ext_state_info.link_ext_state))
			return -EMSGSIZE;

		if (data->ethtool_link_ext_state_info.__link_ext_substate &&
		    nla_put_u8(skb, ETHTOOL_A_LINKSTATE_EXT_SUBSTATE,
			       data->ethtool_link_ext_state_info.__link_ext_substate))
			return -EMSGSIZE;
	}

	if (data->link_stats.link_down_events != ETHTOOL_STAT_NOT_SET)
		if (nla_put_u32(skb, ETHTOOL_A_LINKSTATE_EXT_DOWN_CNT,
				data->link_stats.link_down_events))
			return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_linkstate_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKSTATE_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKSTATE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKSTATE_HEADER,
	.req_info_size		= sizeof(struct linkstate_req_info),
	.reply_data_size	= sizeof(struct linkstate_reply_data),

	.prepare_data		= linkstate_prepare_data,
	.reply_size		= linkstate_reply_size,
	.fill_reply		= linkstate_fill_reply,
};
