// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct pause_req_info {
	struct ethnl_req_info		base;
	enum ethtool_mac_stats_src	src;
};

#define PAUSE_REQINFO(__req_base) \
	container_of(__req_base, struct pause_req_info, base)

struct pause_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_pauseparam	pauseparam;
	struct ethtool_pause_stats	pausestat;
};

#define PAUSE_REPDATA(__reply_base) \
	container_of(__reply_base, struct pause_reply_data, base)

const struct nla_policy ethnl_pause_get_policy[] = {
	[ETHTOOL_A_PAUSE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy_stats),
	[ETHTOOL_A_PAUSE_STATS_SRC]		=
		NLA_POLICY_MAX(NLA_U32, ETHTOOL_MAC_STATS_SRC_PMAC),
};

static int pause_parse_request(struct ethnl_req_info *req_base,
			       struct nlattr **tb,
			       struct netlink_ext_ack *extack)
{
	enum ethtool_mac_stats_src src = ETHTOOL_MAC_STATS_SRC_AGGREGATE;
	struct pause_req_info *req_info = PAUSE_REQINFO(req_base);

	if (tb[ETHTOOL_A_PAUSE_STATS_SRC]) {
		if (!(req_base->flags & ETHTOOL_FLAG_STATS)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "ETHTOOL_FLAG_STATS must be set when requesting a source of stats");
			return -EINVAL;
		}

		src = nla_get_u32(tb[ETHTOOL_A_PAUSE_STATS_SRC]);
	}

	req_info->src = src;

	return 0;
}

static int pause_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	const struct pause_req_info *req_info = PAUSE_REQINFO(req_base);
	struct netlink_ext_ack *extack = info ? info->extack : NULL;
	struct pause_reply_data *data = PAUSE_REPDATA(reply_base);
	enum ethtool_mac_stats_src src = req_info->src;
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	ethtool_stats_init((u64 *)&data->pausestat,
			   sizeof(data->pausestat) / 8);
	data->pausestat.src = src;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	if ((src == ETHTOOL_MAC_STATS_SRC_EMAC ||
	     src == ETHTOOL_MAC_STATS_SRC_PMAC) &&
	    !__ethtool_dev_mm_supported(dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Device does not support MAC merge layer");
		ethnl_ops_complete(dev);
		return -EOPNOTSUPP;
	}

	dev->ethtool_ops->get_pauseparam(dev, &data->pauseparam);
	if (req_base->flags & ETHTOOL_FLAG_STATS &&
	    dev->ethtool_ops->get_pause_stats)
		dev->ethtool_ops->get_pause_stats(dev, &data->pausestat);

	ethnl_ops_complete(dev);

	return 0;
}

static int pause_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	int n = nla_total_size(sizeof(u8)) +	/* _PAUSE_AUTONEG */
		nla_total_size(sizeof(u8)) +	/* _PAUSE_RX */
		nla_total_size(sizeof(u8));	/* _PAUSE_TX */

	if (req_base->flags & ETHTOOL_FLAG_STATS)
		n += nla_total_size(0) +	/* _PAUSE_STATS */
		     nla_total_size(sizeof(u32)) + /* _PAUSE_STATS_SRC */
		     nla_total_size_64bit(sizeof(u64)) * ETHTOOL_PAUSE_STAT_CNT;
	return n;
}

static int ethtool_put_stat(struct sk_buff *skb, u64 val, u16 attrtype,
			    u16 padtype)
{
	if (val == ETHTOOL_STAT_NOT_SET)
		return 0;
	if (nla_put_u64_64bit(skb, attrtype, val, padtype))
		return -EMSGSIZE;

	return 0;
}

static int pause_put_stats(struct sk_buff *skb,
			   const struct ethtool_pause_stats *pause_stats)
{
	const u16 pad = ETHTOOL_A_PAUSE_STAT_PAD;
	struct nlattr *nest;

	if (nla_put_u32(skb, ETHTOOL_A_PAUSE_STATS_SRC, pause_stats->src))
		return -EMSGSIZE;

	nest = nla_nest_start(skb, ETHTOOL_A_PAUSE_STATS);
	if (!nest)
		return -EMSGSIZE;

	if (ethtool_put_stat(skb, pause_stats->tx_pause_frames,
			     ETHTOOL_A_PAUSE_STAT_TX_FRAMES, pad) ||
	    ethtool_put_stat(skb, pause_stats->rx_pause_frames,
			     ETHTOOL_A_PAUSE_STAT_RX_FRAMES, pad))
		goto err_cancel;

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
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

	if (req_base->flags & ETHTOOL_FLAG_STATS &&
	    pause_put_stats(skb, &data->pausestat))
		return -EMSGSIZE;

	return 0;
}

/* PAUSE_SET */

const struct nla_policy ethnl_pause_set_policy[] = {
	[ETHTOOL_A_PAUSE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_PAUSE_AUTONEG]		= { .type = NLA_U8 },
	[ETHTOOL_A_PAUSE_RX]			= { .type = NLA_U8 },
	[ETHTOOL_A_PAUSE_TX]			= { .type = NLA_U8 },
};

static int
ethnl_set_pause_validate(struct ethnl_req_info *req_info,
			 struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;

	return ops->get_pauseparam && ops->set_pauseparam ? 1 : -EOPNOTSUPP;
}

static int
ethnl_set_pause(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct net_device *dev = req_info->dev;
	struct ethtool_pauseparam params = {};
	struct nlattr **tb = info->attrs;
	bool mod = false;
	int ret;

	dev->ethtool_ops->get_pauseparam(dev, &params);

	ethnl_update_bool32(&params.autoneg, tb[ETHTOOL_A_PAUSE_AUTONEG], &mod);
	ethnl_update_bool32(&params.rx_pause, tb[ETHTOOL_A_PAUSE_RX], &mod);
	ethnl_update_bool32(&params.tx_pause, tb[ETHTOOL_A_PAUSE_TX], &mod);
	if (!mod)
		return 0;

	ret = dev->ethtool_ops->set_pauseparam(dev, &params);
	return ret < 0 ? ret : 1;
}

const struct ethnl_request_ops ethnl_pause_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PAUSE_GET,
	.reply_cmd		= ETHTOOL_MSG_PAUSE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PAUSE_HEADER,
	.req_info_size		= sizeof(struct pause_req_info),
	.reply_data_size	= sizeof(struct pause_reply_data),

	.parse_request		= pause_parse_request,
	.prepare_data		= pause_prepare_data,
	.reply_size		= pause_reply_size,
	.fill_reply		= pause_fill_reply,

	.set_validate		= ethnl_set_pause_validate,
	.set			= ethnl_set_pause,
	.set_ntf_cmd		= ETHTOOL_MSG_PAUSE_NTF,
};
