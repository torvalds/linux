// SPDX-License-Identifier: GPL-2.0-only

#include <linux/net_tstamp.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct tsinfo_req_info {
	struct ethnl_req_info		base;
};

struct tsinfo_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_ts_info		ts_info;
};

#define TSINFO_REPDATA(__reply_base) \
	container_of(__reply_base, struct tsinfo_reply_data, base)

static const struct nla_policy
tsinfo_get_policy[ETHTOOL_A_TSINFO_MAX + 1] = {
	[ETHTOOL_A_TSINFO_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_TSINFO_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_TSINFO_TIMESTAMPING]		= { .type = NLA_REJECT },
	[ETHTOOL_A_TSINFO_TX_TYPES]		= { .type = NLA_REJECT },
	[ETHTOOL_A_TSINFO_RX_FILTERS]		= { .type = NLA_REJECT },
	[ETHTOOL_A_TSINFO_PHC_INDEX]		= { .type = NLA_REJECT },
};

static int tsinfo_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       struct genl_info *info)
{
	struct tsinfo_reply_data *data = TSINFO_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	ret = __ethtool_get_ts_info(dev, &data->ts_info);
	ethnl_ops_complete(dev);

	return ret;
}

static int tsinfo_reply_size(const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct tsinfo_reply_data *data = TSINFO_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct ethtool_ts_info *ts_info = &data->ts_info;
	int len = 0;
	int ret;

	BUILD_BUG_ON(__SOF_TIMESTAMPING_CNT > 32);
	BUILD_BUG_ON(__HWTSTAMP_TX_CNT > 32);
	BUILD_BUG_ON(__HWTSTAMP_FILTER_CNT > 32);

	if (ts_info->so_timestamping) {
		ret = ethnl_bitset32_size(&ts_info->so_timestamping, NULL,
					  __SOF_TIMESTAMPING_CNT,
					  sof_timestamping_names, compact);
		if (ret < 0)
			return ret;
		len += ret;	/* _TSINFO_TIMESTAMPING */
	}
	if (ts_info->tx_types) {
		ret = ethnl_bitset32_size(&ts_info->tx_types, NULL,
					  __HWTSTAMP_TX_CNT,
					  ts_tx_type_names, compact);
		if (ret < 0)
			return ret;
		len += ret;	/* _TSINFO_TX_TYPES */
	}
	if (ts_info->rx_filters) {
		ret = ethnl_bitset32_size(&ts_info->rx_filters, NULL,
					  __HWTSTAMP_FILTER_CNT,
					  ts_rx_filter_names, compact);
		if (ret < 0)
			return ret;
		len += ret;	/* _TSINFO_RX_FILTERS */
	}
	if (ts_info->phc_index >= 0)
		len += nla_total_size(sizeof(u32));	/* _TSINFO_PHC_INDEX */

	return len;
}

static int tsinfo_fill_reply(struct sk_buff *skb,
			     const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct tsinfo_reply_data *data = TSINFO_REPDATA(reply_base);
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct ethtool_ts_info *ts_info = &data->ts_info;
	int ret;

	if (ts_info->so_timestamping) {
		ret = ethnl_put_bitset32(skb, ETHTOOL_A_TSINFO_TIMESTAMPING,
					 &ts_info->so_timestamping, NULL,
					 __SOF_TIMESTAMPING_CNT,
					 sof_timestamping_names, compact);
		if (ret < 0)
			return ret;
	}
	if (ts_info->tx_types) {
		ret = ethnl_put_bitset32(skb, ETHTOOL_A_TSINFO_TX_TYPES,
					 &ts_info->tx_types, NULL,
					 __HWTSTAMP_TX_CNT,
					 ts_tx_type_names, compact);
		if (ret < 0)
			return ret;
	}
	if (ts_info->rx_filters) {
		ret = ethnl_put_bitset32(skb, ETHTOOL_A_TSINFO_RX_FILTERS,
					 &ts_info->rx_filters, NULL,
					 __HWTSTAMP_FILTER_CNT,
					 ts_rx_filter_names, compact);
		if (ret < 0)
			return ret;
	}
	if (ts_info->phc_index >= 0 &&
	    nla_put_u32(skb, ETHTOOL_A_TSINFO_PHC_INDEX, ts_info->phc_index))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_tsinfo_request_ops = {
	.request_cmd		= ETHTOOL_MSG_TSINFO_GET,
	.reply_cmd		= ETHTOOL_MSG_TSINFO_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_TSINFO_HEADER,
	.max_attr		= ETHTOOL_A_TSINFO_MAX,
	.req_info_size		= sizeof(struct tsinfo_req_info),
	.reply_data_size	= sizeof(struct tsinfo_reply_data),
	.request_policy		= tsinfo_get_policy,

	.prepare_data		= tsinfo_prepare_data,
	.reply_size		= tsinfo_reply_size,
	.fill_reply		= tsinfo_fill_reply,
};
