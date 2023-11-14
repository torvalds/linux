// SPDX-License-Identifier: GPL-2.0-only

#include <linux/net_tstamp.h>
#include <linux/phy.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct ts_req_info {
	struct ethnl_req_info		base;
};

struct ts_reply_data {
	struct ethnl_reply_data		base;
	enum timestamping_layer		ts_layer;
};

#define TS_REPDATA(__reply_base) \
	container_of(__reply_base, struct ts_reply_data, base)

/* TS_GET */
const struct nla_policy ethnl_ts_get_policy[] = {
	[ETHTOOL_A_TS_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int ts_prepare_data(const struct ethnl_req_info *req_base,
			   struct ethnl_reply_data *reply_base,
			   const struct genl_info *info)
{
	struct ts_reply_data *data = TS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	data->ts_layer = dev->ts_layer;

	ethnl_ops_complete(dev);

	return ret;
}

static int ts_reply_size(const struct ethnl_req_info *req_base,
			 const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u32));
}

static int ts_fill_reply(struct sk_buff *skb,
			 const struct ethnl_req_info *req_base,
			 const struct ethnl_reply_data *reply_base)
{
	struct ts_reply_data *data = TS_REPDATA(reply_base);

	return nla_put_u32(skb, ETHTOOL_A_TS_LAYER, data->ts_layer);
}

/* TS_SET */
const struct nla_policy ethnl_ts_set_policy[] = {
	[ETHTOOL_A_TS_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_TS_LAYER]	= NLA_POLICY_RANGE(NLA_U32, 0,
						   __TIMESTAMPING_COUNT - 1)
};

static int ethnl_set_ts_validate(struct ethnl_req_info *req_info,
				 struct genl_info *info)
{
	struct nlattr **tb = info->attrs;
	const struct net_device_ops *ops = req_info->dev->netdev_ops;

	if (!ops->ndo_hwtstamp_set)
		return -EOPNOTSUPP;

	if (!tb[ETHTOOL_A_TS_LAYER])
		return 0;

	return 1;
}

static int ethnl_set_ts(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct net_device *dev = req_info->dev;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct kernel_hwtstamp_config config = {0};
	struct nlattr **tb = info->attrs;
	enum timestamping_layer ts_layer;
	bool mod = false;
	int ret;

	ts_layer = dev->ts_layer;
	ethnl_update_u32(&ts_layer, tb[ETHTOOL_A_TS_LAYER], &mod);

	if (!mod)
		return 0;

	if (ts_layer == SOFTWARE_TIMESTAMPING) {
		struct ethtool_ts_info ts_info = {0};

		if (!ops->get_ts_info) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_TS_LAYER],
					    "this net device cannot support timestamping");
			return -EINVAL;
		}

		ops->get_ts_info(dev, &ts_info);
		if ((ts_info.so_timestamping &
		    SOF_TIMESTAMPING_SOFTWARE_MASK) !=
		    SOF_TIMESTAMPING_SOFTWARE_MASK) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_TS_LAYER],
					    "this net device cannot support software timestamping");
			return -EINVAL;
		}
	} else if (ts_layer == MAC_TIMESTAMPING) {
		struct ethtool_ts_info ts_info = {0};

		if (!ops->get_ts_info) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_TS_LAYER],
					    "this net device cannot support timestamping");
			return -EINVAL;
		}

		ops->get_ts_info(dev, &ts_info);
		if ((ts_info.so_timestamping &
		    SOF_TIMESTAMPING_HARDWARE_MASK) !=
		    SOF_TIMESTAMPING_HARDWARE_MASK) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_TS_LAYER],
					    "this net device cannot support hardware timestamping");
			return -EINVAL;
		}
	} else if (ts_layer == PHY_TIMESTAMPING && !phy_has_tsinfo(dev->phydev)) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_TS_LAYER],
				    "this phy device cannot support timestamping");
		return -EINVAL;
	}

	/* Disable time stamping in the current layer. */
	if (netif_device_present(dev) &&
	    (dev->ts_layer == PHY_TIMESTAMPING ||
	    dev->ts_layer == MAC_TIMESTAMPING)) {
		ret = dev_set_hwtstamp_phylib(dev, &config, info->extack);
		if (ret < 0)
			return ret;
	}

	dev->ts_layer = ts_layer;

	return 1;
}

const struct ethnl_request_ops ethnl_ts_request_ops = {
	.request_cmd		= ETHTOOL_MSG_TS_GET,
	.reply_cmd		= ETHTOOL_MSG_TS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_TS_HEADER,
	.req_info_size		= sizeof(struct ts_req_info),
	.reply_data_size	= sizeof(struct ts_reply_data),

	.prepare_data		= ts_prepare_data,
	.reply_size		= ts_reply_size,
	.fill_reply		= ts_fill_reply,

	.set_validate		= ethnl_set_ts_validate,
	.set			= ethnl_set_ts,
};

/* TS_LIST_GET */
struct ts_list_reply_data {
	struct ethnl_reply_data		base;
	enum timestamping_layer		ts_layer[__TIMESTAMPING_COUNT];
	u8				num_ts;
};

#define TS_LIST_REPDATA(__reply_base) \
	container_of(__reply_base, struct ts_list_reply_data, base)

static int ts_list_prepare_data(const struct ethnl_req_info *req_base,
				struct ethnl_reply_data *reply_base,
				const struct genl_info *info)
{
	struct ts_list_reply_data *data = TS_LIST_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int ret, i = 0;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	if (phy_has_tsinfo(dev->phydev))
		data->ts_layer[i++] = PHY_TIMESTAMPING;
	if (ops->get_ts_info) {
		struct ethtool_ts_info ts_info = {0};

		ops->get_ts_info(dev, &ts_info);
		if (ts_info.so_timestamping &
		    SOF_TIMESTAMPING_HARDWARE_MASK)
			data->ts_layer[i++] = MAC_TIMESTAMPING;

		if (ts_info.so_timestamping &
		    SOF_TIMESTAMPING_SOFTWARE_MASK)
			data->ts_layer[i++] = SOFTWARE_TIMESTAMPING;
	}

	data->num_ts = i;
	ethnl_ops_complete(dev);

	return ret;
}

static int ts_list_reply_size(const struct ethnl_req_info *req_base,
			      const struct ethnl_reply_data *reply_base)
{
	struct ts_list_reply_data *data = TS_LIST_REPDATA(reply_base);

	return nla_total_size(sizeof(u32)) * data->num_ts;
}

static int ts_list_fill_reply(struct sk_buff *skb,
			      const struct ethnl_req_info *req_base,
			 const struct ethnl_reply_data *reply_base)
{
	struct ts_list_reply_data *data = TS_LIST_REPDATA(reply_base);

	return nla_put(skb, ETHTOOL_A_TS_LIST_LAYER, sizeof(u32) * data->num_ts, data->ts_layer);
}

const struct ethnl_request_ops ethnl_ts_list_request_ops = {
	.request_cmd		= ETHTOOL_MSG_TS_LIST_GET,
	.reply_cmd		= ETHTOOL_MSG_TS_LIST_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_TS_HEADER,
	.req_info_size		= sizeof(struct ts_req_info),
	.reply_data_size	= sizeof(struct ts_list_reply_data),

	.prepare_data		= ts_list_prepare_data,
	.reply_size		= ts_list_reply_size,
	.fill_reply		= ts_list_fill_reply,
};
