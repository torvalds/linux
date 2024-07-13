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
	struct ethtool_ts_stats		stats;
};

#define TSINFO_REPDATA(__reply_base) \
	container_of(__reply_base, struct tsinfo_reply_data, base)

#define ETHTOOL_TS_STAT_CNT \
	(__ETHTOOL_A_TS_STAT_CNT - (ETHTOOL_A_TS_STAT_UNSPEC + 1))

const struct nla_policy ethnl_tsinfo_get_policy[] = {
	[ETHTOOL_A_TSINFO_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy_stats),
};

static int tsinfo_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       const struct genl_info *info)
{
	struct tsinfo_reply_data *data = TSINFO_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	if (req_base->flags & ETHTOOL_FLAG_STATS) {
		ethtool_stats_init((u64 *)&data->stats,
				   sizeof(data->stats) / sizeof(u64));
		if (dev->ethtool_ops->get_ts_stats)
			dev->ethtool_ops->get_ts_stats(dev, &data->stats);
	}
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
	if (req_base->flags & ETHTOOL_FLAG_STATS)
		len += nla_total_size(0) + /* _TSINFO_STATS */
		       nla_total_size_64bit(sizeof(u64)) * ETHTOOL_TS_STAT_CNT;

	return len;
}

static int tsinfo_put_stat(struct sk_buff *skb, u64 val, u16 attrtype)
{
	if (val == ETHTOOL_STAT_NOT_SET)
		return 0;
	if (nla_put_uint(skb, attrtype, val))
		return -EMSGSIZE;
	return 0;
}

static int tsinfo_put_stats(struct sk_buff *skb,
			    const struct ethtool_ts_stats *stats)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, ETHTOOL_A_TSINFO_STATS);
	if (!nest)
		return -EMSGSIZE;

	if (tsinfo_put_stat(skb, stats->tx_stats.pkts,
			    ETHTOOL_A_TS_STAT_TX_PKTS) ||
	    tsinfo_put_stat(skb, stats->tx_stats.lost,
			    ETHTOOL_A_TS_STAT_TX_LOST) ||
	    tsinfo_put_stat(skb, stats->tx_stats.err,
			    ETHTOOL_A_TS_STAT_TX_ERR))
		goto err_cancel;

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
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
	if (req_base->flags & ETHTOOL_FLAG_STATS &&
	    tsinfo_put_stats(skb, &data->stats))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_tsinfo_request_ops = {
	.request_cmd		= ETHTOOL_MSG_TSINFO_GET,
	.reply_cmd		= ETHTOOL_MSG_TSINFO_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_TSINFO_HEADER,
	.req_info_size		= sizeof(struct tsinfo_req_info),
	.reply_data_size	= sizeof(struct tsinfo_reply_data),

	.prepare_data		= tsinfo_prepare_data,
	.reply_size		= tsinfo_reply_size,
	.fill_reply		= tsinfo_fill_reply,
};
