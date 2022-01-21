// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct pause_req_info {
	struct ethnl_req_info		base;
};

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
};

static void ethtool_stats_init(u64 *stats, unsigned int n)
{
	while (n--)
		stats[n] = ETHTOOL_STAT_NOT_SET;
}

static int pause_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	struct pause_reply_data *data = PAUSE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	ethtool_stats_init((u64 *)&data->pausestat,
			   sizeof(data->pausestat) / 8);

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
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

const struct ethnl_request_ops ethnl_pause_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PAUSE_GET,
	.reply_cmd		= ETHTOOL_MSG_PAUSE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PAUSE_HEADER,
	.req_info_size		= sizeof(struct pause_req_info),
	.reply_data_size	= sizeof(struct pause_reply_data),

	.prepare_data		= pause_prepare_data,
	.reply_size		= pause_reply_size,
	.fill_reply		= pause_fill_reply,
};

/* PAUSE_SET */

const struct nla_policy ethnl_pause_set_policy[] = {
	[ETHTOOL_A_PAUSE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_PAUSE_AUTONEG]		= { .type = NLA_U8 },
	[ETHTOOL_A_PAUSE_RX]			= { .type = NLA_U8 },
	[ETHTOOL_A_PAUSE_TX]			= { .type = NLA_U8 },
};

int ethnl_set_pause(struct sk_buff *skb, struct genl_info *info)
{
	struct ethtool_pauseparam params = {};
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	const struct ethtool_ops *ops;
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_PAUSE_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ops = dev->ethtool_ops;
	ret = -EOPNOTSUPP;
	if (!ops->get_pauseparam || !ops->set_pauseparam)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;
	ops->get_pauseparam(dev, &params);

	ethnl_update_bool32(&params.autoneg, tb[ETHTOOL_A_PAUSE_AUTONEG], &mod);
	ethnl_update_bool32(&params.rx_pause, tb[ETHTOOL_A_PAUSE_RX], &mod);
	ethnl_update_bool32(&params.tx_pause, tb[ETHTOOL_A_PAUSE_TX], &mod);
	ret = 0;
	if (!mod)
		goto out_ops;

	ret = dev->ethtool_ops->set_pauseparam(dev, &params);
	if (ret < 0)
		goto out_ops;
	ethtool_notify(dev, ETHTOOL_MSG_PAUSE_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
