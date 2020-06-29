// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include <linux/phy.h>

struct linkstate_req_info {
	struct ethnl_req_info		base;
};

struct linkstate_reply_data {
	struct ethnl_reply_data		base;
	int				link;
	int				sqi;
	int				sqi_max;
};

#define LINKSTATE_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkstate_reply_data, base)

static const struct nla_policy
linkstate_get_policy[ETHTOOL_A_LINKSTATE_MAX + 1] = {
	[ETHTOOL_A_LINKSTATE_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKSTATE_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKSTATE_LINK]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKSTATE_SQI]		= { .type = NLA_REJECT },
	[ETHTOOL_A_LINKSTATE_SQI_MAX]		= { .type = NLA_REJECT },
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
		return ret;

	data->sqi = ret;

	ret = linkstate_get_sqi_max(dev);
	if (ret < 0 && ret != -EOPNOTSUPP)
		return ret;

	data->sqi_max = ret;

	ethnl_ops_complete(dev);

	return 0;
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

	return 0;
}

const struct ethnl_request_ops ethnl_linkstate_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKSTATE_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKSTATE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKSTATE_HEADER,
	.max_attr		= ETHTOOL_A_LINKSTATE_MAX,
	.req_info_size		= sizeof(struct linkstate_req_info),
	.reply_data_size	= sizeof(struct linkstate_reply_data),
	.request_policy		= linkstate_get_policy,

	.prepare_data		= linkstate_prepare_data,
	.reply_size		= linkstate_reply_size,
	.fill_reply		= linkstate_fill_reply,
};
