// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct pause_req_info {
	struct ethnl_req_info		base;
};

struct pause_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_pauseparam	pauseparam;
};

#define PAUSE_REPDATA(__reply_base) \
	container_of(__reply_base, struct pause_reply_data, base)

static const struct nla_policy
pause_get_policy[ETHTOOL_A_PAUSE_MAX + 1] = {
	[ETHTOOL_A_PAUSE_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_PAUSE_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_PAUSE_AUTONEG]		= { .type = NLA_REJECT },
	[ETHTOOL_A_PAUSE_RX]			= { .type = NLA_REJECT },
	[ETHTOOL_A_PAUSE_TX]			= { .type = NLA_REJECT },
};

static int pause_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	struct pause_reply_data *data = PAUSE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_pauseparam(dev, &data->pauseparam);
	ethnl_ops_complete(dev);

	return 0;
}

static int pause_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u8)) +	/* _PAUSE_AUTONEG */
		nla_total_size(sizeof(u8)) +	/* _PAUSE_RX */
		nla_total_size(sizeof(u8));	/* _PAUSE_TX */
}

static int pause_fill_reply(struct sk_buff *skb,
			    const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct pause_reply_data *data = PAUSE_REPDATA(reply_base);
	const struct ethtool_pauseparam *pauseparam = &data->pauseparam;

	if (nla_put_u8(skb, ETHTOOL_A_PAUSE_AUTONEG, !!pauseparam->autoneg) ||
	    nla_put_u8(skb, ETHTOOL_A_PAUSE_RX, !!pauseparam->rx_pause) ||
	    nla_put_u8(skb, ETHTOOL_A_PAUSE_TX, !!pauseparam->tx_pause))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_pause_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PAUSE_GET,
	.reply_cmd		= ETHTOOL_MSG_PAUSE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PAUSE_HEADER,
	.max_attr		= ETHTOOL_A_PAUSE_MAX,
	.req_info_size		= sizeof(struct pause_req_info),
	.reply_data_size	= sizeof(struct pause_reply_data),
	.request_policy		= pause_get_policy,

	.prepare_data		= pause_prepare_data,
	.reply_size		= pause_reply_size,
	.fill_reply		= pause_fill_reply,
};
