// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct wol_req_info {
	struct ethnl_req_info		base;
};

struct wol_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_wolinfo		wol;
	bool				show_sopass;
};

#define WOL_REPDATA(__reply_base) \
	container_of(__reply_base, struct wol_reply_data, base)

static const struct nla_policy
wol_get_policy[ETHTOOL_A_WOL_MAX + 1] = {
	[ETHTOOL_A_WOL_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_WOL_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_WOL_MODES]		= { .type = NLA_REJECT },
	[ETHTOOL_A_WOL_SOPASS]		= { .type = NLA_REJECT },
};

static int wol_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    struct genl_info *info)
{
	struct wol_reply_data *data = WOL_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_wol(dev, &data->wol);
	ethnl_ops_complete(dev);
	data->show_sopass = data->wol.supported & WAKE_MAGICSECURE;

	return 0;
}

static int wol_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct wol_reply_data *data = WOL_REPDATA(reply_base);
	int len;

	len = ethnl_bitset32_size(&data->wol.wolopts, &data->wol.supported,
				  WOL_MODE_COUNT, wol_mode_names, compact);
	if (len < 0)
		return len;
	if (data->show_sopass)
		len += nla_total_size(sizeof(data->wol.sopass));

	return len;
}

static int wol_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct wol_reply_data *data = WOL_REPDATA(reply_base);
	int ret;

	ret = ethnl_put_bitset32(skb, ETHTOOL_A_WOL_MODES, &data->wol.wolopts,
				 &data->wol.supported, WOL_MODE_COUNT,
				 wol_mode_names, compact);
	if (ret < 0)
		return ret;
	if (data->show_sopass &&
	    nla_put(skb, ETHTOOL_A_WOL_SOPASS, sizeof(data->wol.sopass),
		    data->wol.sopass))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_wol_request_ops = {
	.request_cmd		= ETHTOOL_MSG_WOL_GET,
	.reply_cmd		= ETHTOOL_MSG_WOL_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_WOL_HEADER,
	.max_attr		= ETHTOOL_A_WOL_MAX,
	.req_info_size		= sizeof(struct wol_req_info),
	.reply_data_size	= sizeof(struct wol_reply_data),
	.request_policy		= wol_get_policy,

	.prepare_data		= wol_prepare_data,
	.reply_size		= wol_reply_size,
	.fill_reply		= wol_fill_reply,
};
