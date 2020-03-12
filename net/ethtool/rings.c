// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct rings_req_info {
	struct ethnl_req_info		base;
};

struct rings_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_ringparam	ringparam;
};

#define RINGS_REPDATA(__reply_base) \
	container_of(__reply_base, struct rings_reply_data, base)

static const struct nla_policy
rings_get_policy[ETHTOOL_A_RINGS_MAX + 1] = {
	[ETHTOOL_A_RINGS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_RINGS_RX_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_RX_MINI_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_RX_JUMBO_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_TX_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_RX]			= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_RX_MINI]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_RX_JUMBO]		= { .type = NLA_REJECT },
	[ETHTOOL_A_RINGS_TX]			= { .type = NLA_REJECT },
};

static int rings_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	struct rings_reply_data *data = RINGS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_ringparam(dev, &data->ringparam);
	ethnl_ops_complete(dev);

	return 0;
}

static int rings_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MINI_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_JUMBO_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_TX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_MINI */
	       nla_total_size(sizeof(u32)) +	/* _RINGS_RX_JUMBO */
	       nla_total_size(sizeof(u32));	/* _RINGS_TX */
}

static int rings_fill_reply(struct sk_buff *skb,
			    const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct rings_reply_data *data = RINGS_REPDATA(reply_base);
	const struct ethtool_ringparam *ringparam = &data->ringparam;

	if ((ringparam->rx_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MAX,
			  ringparam->rx_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX,
			  ringparam->rx_pending))) ||
	    (ringparam->rx_mini_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MINI_MAX,
			  ringparam->rx_mini_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX_MINI,
			  ringparam->rx_mini_pending))) ||
	    (ringparam->rx_jumbo_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_RX_JUMBO_MAX,
			  ringparam->rx_jumbo_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_RX_JUMBO,
			  ringparam->rx_jumbo_pending))) ||
	    (ringparam->tx_max_pending &&
	     (nla_put_u32(skb, ETHTOOL_A_RINGS_TX_MAX,
			  ringparam->tx_max_pending) ||
	      nla_put_u32(skb, ETHTOOL_A_RINGS_TX,
			  ringparam->tx_pending))))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_rings_request_ops = {
	.request_cmd		= ETHTOOL_MSG_RINGS_GET,
	.reply_cmd		= ETHTOOL_MSG_RINGS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_RINGS_HEADER,
	.max_attr		= ETHTOOL_A_RINGS_MAX,
	.req_info_size		= sizeof(struct rings_req_info),
	.reply_data_size	= sizeof(struct rings_reply_data),
	.request_policy		= rings_get_policy,

	.prepare_data		= rings_prepare_data,
	.reply_size		= rings_reply_size,
	.fill_reply		= rings_fill_reply,
};
