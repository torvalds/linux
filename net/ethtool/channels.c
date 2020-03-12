// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct channels_req_info {
	struct ethnl_req_info		base;
};

struct channels_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_channels		channels;
};

#define CHANNELS_REPDATA(__reply_base) \
	container_of(__reply_base, struct channels_reply_data, base)

static const struct nla_policy
channels_get_policy[ETHTOOL_A_CHANNELS_MAX + 1] = {
	[ETHTOOL_A_CHANNELS_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_CHANNELS_RX_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_TX_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_OTHER_MAX]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_COMBINED_MAX]	= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_RX_COUNT]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_TX_COUNT]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_OTHER_COUNT]	= { .type = NLA_REJECT },
	[ETHTOOL_A_CHANNELS_COMBINED_COUNT]	= { .type = NLA_REJECT },
};

static int channels_prepare_data(const struct ethnl_req_info *req_base,
				 struct ethnl_reply_data *reply_base,
				 struct genl_info *info)
{
	struct channels_reply_data *data = CHANNELS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_channels)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_channels(dev, &data->channels);
	ethnl_ops_complete(dev);

	return 0;
}

static int channels_reply_size(const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u32)) +	/* _CHANNELS_RX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_TX_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_OTHER_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_COMBINED_MAX */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_RX_COUNT */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_TX_COUNT */
	       nla_total_size(sizeof(u32)) +	/* _CHANNELS_OTHER_COUNT */
	       nla_total_size(sizeof(u32));	/* _CHANNELS_COMBINED_COUNT */
}

static int channels_fill_reply(struct sk_buff *skb,
			       const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct channels_reply_data *data = CHANNELS_REPDATA(reply_base);
	const struct ethtool_channels *channels = &data->channels;

	if ((channels->max_rx &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_RX_MAX,
			  channels->max_rx) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_RX_COUNT,
			  channels->rx_count))) ||
	    (channels->max_tx &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_TX_MAX,
			  channels->max_tx) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_TX_COUNT,
			  channels->tx_count))) ||
	    (channels->max_other &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_OTHER_MAX,
			  channels->max_other) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_OTHER_COUNT,
			  channels->other_count))) ||
	    (channels->max_combined &&
	     (nla_put_u32(skb, ETHTOOL_A_CHANNELS_COMBINED_MAX,
			  channels->max_combined) ||
	      nla_put_u32(skb, ETHTOOL_A_CHANNELS_COMBINED_COUNT,
			  channels->combined_count))))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_channels_request_ops = {
	.request_cmd		= ETHTOOL_MSG_CHANNELS_GET,
	.reply_cmd		= ETHTOOL_MSG_CHANNELS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_CHANNELS_HEADER,
	.max_attr		= ETHTOOL_A_CHANNELS_MAX,
	.req_info_size		= sizeof(struct channels_req_info),
	.reply_data_size	= sizeof(struct channels_reply_data),
	.request_policy		= channels_get_policy,

	.prepare_data		= channels_prepare_data,
	.reply_size		= channels_reply_size,
	.fill_reply		= channels_fill_reply,
};
