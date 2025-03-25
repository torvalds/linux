// SPDX-License-Identifier: GPL-2.0-only

#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phy_link_topology.h>
#include <linux/ptp_clock_kernel.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"
#include "ts.h"

struct tsinfo_req_info {
	struct ethnl_req_info		base;
	struct hwtstamp_provider_desc	hwprov_desc;
};

struct tsinfo_reply_data {
	struct ethnl_reply_data		base;
	struct kernel_ethtool_ts_info	ts_info;
	struct ethtool_ts_stats		stats;
};

#define TSINFO_REQINFO(__req_base) \
	container_of(__req_base, struct tsinfo_req_info, base)

#define TSINFO_REPDATA(__reply_base) \
	container_of(__reply_base, struct tsinfo_reply_data, base)

#define ETHTOOL_TS_STAT_CNT \
	(__ETHTOOL_A_TS_STAT_CNT - (ETHTOOL_A_TS_STAT_UNSPEC + 1))

const struct nla_policy ethnl_tsinfo_get_policy[ETHTOOL_A_TSINFO_MAX + 1] = {
	[ETHTOOL_A_TSINFO_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy_stats),
	[ETHTOOL_A_TSINFO_HWTSTAMP_PROVIDER] =
		NLA_POLICY_NESTED(ethnl_ts_hwtst_prov_policy),
};

int ts_parse_hwtst_provider(const struct nlattr *nest,
			    struct hwtstamp_provider_desc *hwprov_desc,
			    struct netlink_ext_ack *extack,
			    bool *mod)
{
	struct nlattr *tb[ARRAY_SIZE(ethnl_ts_hwtst_prov_policy)];
	int ret;

	ret = nla_parse_nested(tb,
			       ARRAY_SIZE(ethnl_ts_hwtst_prov_policy) - 1,
			       nest,
			       ethnl_ts_hwtst_prov_policy, extack);
	if (ret < 0)
		return ret;

	if (NL_REQ_ATTR_CHECK(extack, nest, tb,
			      ETHTOOL_A_TS_HWTSTAMP_PROVIDER_INDEX) ||
	    NL_REQ_ATTR_CHECK(extack, nest, tb,
			      ETHTOOL_A_TS_HWTSTAMP_PROVIDER_QUALIFIER))
		return -EINVAL;

	ethnl_update_u32(&hwprov_desc->index,
			 tb[ETHTOOL_A_TS_HWTSTAMP_PROVIDER_INDEX],
			 mod);
	ethnl_update_u32(&hwprov_desc->qualifier,
			 tb[ETHTOOL_A_TS_HWTSTAMP_PROVIDER_QUALIFIER],
			 mod);

	return 0;
}

static int
tsinfo_parse_request(struct ethnl_req_info *req_base, struct nlattr **tb,
		     struct netlink_ext_ack *extack)
{
	struct tsinfo_req_info *req = TSINFO_REQINFO(req_base);
	bool mod = false;

	req->hwprov_desc.index = -1;

	if (!tb[ETHTOOL_A_TSINFO_HWTSTAMP_PROVIDER])
		return 0;

	return ts_parse_hwtst_provider(tb[ETHTOOL_A_TSINFO_HWTSTAMP_PROVIDER],
				       &req->hwprov_desc, extack, &mod);
}

static int tsinfo_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       const struct genl_info *info)
{
	struct tsinfo_reply_data *data = TSINFO_REPDATA(reply_base);
	struct tsinfo_req_info *req = TSINFO_REQINFO(req_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	if (req->hwprov_desc.index != -1) {
		ret = ethtool_get_ts_info_by_phc(dev, &data->ts_info,
						 &req->hwprov_desc);
		ethnl_ops_complete(dev);
		return ret;
	}

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
	const struct kernel_ethtool_ts_info *ts_info = &data->ts_info;
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
	if (ts_info->phc_index >= 0) {
		len += nla_total_size(sizeof(u32));	/* _TSINFO_PHC_INDEX */
		/* _TSINFO_HWTSTAMP_PROVIDER */
		len += nla_total_size(0) + 2 * nla_total_size(sizeof(u32));
	}
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
	    tsinfo_put_stat(skb, stats->tx_stats.onestep_pkts_unconfirmed,
			    ETHTOOL_A_TS_STAT_TX_ONESTEP_PKTS_UNCONFIRMED) ||
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
	const struct kernel_ethtool_ts_info *ts_info = &data->ts_info;
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
	if (ts_info->phc_index >= 0) {
		struct nlattr *nest;

		ret = nla_put_u32(skb, ETHTOOL_A_TSINFO_PHC_INDEX,
				  ts_info->phc_index);
		if (ret)
			return -EMSGSIZE;

		nest = nla_nest_start(skb, ETHTOOL_A_TSINFO_HWTSTAMP_PROVIDER);
		if (!nest)
			return -EMSGSIZE;

		if (nla_put_u32(skb, ETHTOOL_A_TS_HWTSTAMP_PROVIDER_INDEX,
				ts_info->phc_index) ||
		    nla_put_u32(skb,
				ETHTOOL_A_TS_HWTSTAMP_PROVIDER_QUALIFIER,
				ts_info->phc_qualifier)) {
			nla_nest_cancel(skb, nest);
			return -EMSGSIZE;
		}

		nla_nest_end(skb, nest);
	}
	if (req_base->flags & ETHTOOL_FLAG_STATS &&
	    tsinfo_put_stats(skb, &data->stats))
		return -EMSGSIZE;

	return 0;
}

struct ethnl_tsinfo_dump_ctx {
	struct tsinfo_req_info		*req_info;
	struct tsinfo_reply_data	*reply_data;
	unsigned long			pos_ifindex;
	bool				netdev_dump_done;
	unsigned long			pos_phyindex;
	enum hwtstamp_provider_qualifier pos_phcqualifier;
};

static void *ethnl_tsinfo_prepare_dump(struct sk_buff *skb,
				       struct net_device *dev,
				       struct tsinfo_reply_data *reply_data,
				       struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	void *ehdr = NULL;

	ehdr = ethnl_dump_put(skb, cb,
			      ETHTOOL_MSG_TSINFO_GET_REPLY);
	if (!ehdr)
		return ERR_PTR(-EMSGSIZE);

	reply_data = ctx->reply_data;
	memset(reply_data, 0, sizeof(*reply_data));
	reply_data->base.dev = dev;
	reply_data->ts_info.cmd = ETHTOOL_GET_TS_INFO;
	reply_data->ts_info.phc_index = -1;

	return ehdr;
}

static int ethnl_tsinfo_end_dump(struct sk_buff *skb,
				 struct net_device *dev,
				 struct tsinfo_req_info *req_info,
				 struct tsinfo_reply_data *reply_data,
				 void *ehdr)
{
	int ret;

	reply_data->ts_info.so_timestamping |= SOF_TIMESTAMPING_RX_SOFTWARE |
					       SOF_TIMESTAMPING_SOFTWARE;

	ret = ethnl_fill_reply_header(skb, dev, ETHTOOL_A_TSINFO_HEADER);
	if (ret < 0)
		return ret;

	ret = tsinfo_fill_reply(skb, &req_info->base, &reply_data->base);
	if (ret < 0)
		return ret;

	reply_data->base.dev = NULL;
	genlmsg_end(skb, ehdr);

	return ret;
}

static int ethnl_tsinfo_dump_one_phydev(struct sk_buff *skb,
					struct net_device *dev,
					struct phy_device *phydev,
					struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	struct tsinfo_reply_data *reply_data;
	struct tsinfo_req_info *req_info;
	void *ehdr = NULL;
	int ret = 0;

	if (!phy_has_tsinfo(phydev))
		return -EOPNOTSUPP;

	reply_data = ctx->reply_data;
	req_info = ctx->req_info;
	ehdr = ethnl_tsinfo_prepare_dump(skb, dev, reply_data, cb);
	if (IS_ERR(ehdr))
		return PTR_ERR(ehdr);

	ret = phy_ts_info(phydev, &reply_data->ts_info);
	if (ret < 0)
		goto err;

	ret = ethnl_tsinfo_end_dump(skb, dev, req_info, reply_data, ehdr);
	if (ret < 0)
		goto err;

	return ret;
err:
	genlmsg_cancel(skb, ehdr);
	return ret;
}

static int ethnl_tsinfo_dump_one_netdev(struct sk_buff *skb,
					struct net_device *dev,
					struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct tsinfo_reply_data *reply_data;
	struct tsinfo_req_info *req_info;
	void *ehdr = NULL;
	int ret = 0;

	if (!ops->get_ts_info)
		return -EOPNOTSUPP;

	reply_data = ctx->reply_data;
	req_info = ctx->req_info;
	for (; ctx->pos_phcqualifier < HWTSTAMP_PROVIDER_QUALIFIER_CNT;
	     ctx->pos_phcqualifier++) {
		if (!net_support_hwtstamp_qualifier(dev,
						    ctx->pos_phcqualifier))
			continue;

		ehdr = ethnl_tsinfo_prepare_dump(skb, dev, reply_data, cb);
		if (IS_ERR(ehdr)) {
			ret = PTR_ERR(ehdr);
			goto err;
		}

		reply_data->ts_info.phc_qualifier = ctx->pos_phcqualifier;
		ret = ops->get_ts_info(dev, &reply_data->ts_info);
		if (ret < 0)
			goto err;

		ret = ethnl_tsinfo_end_dump(skb, dev, req_info, reply_data,
					    ehdr);
		if (ret < 0)
			goto err;
	}

	return ret;

err:
	genlmsg_cancel(skb, ehdr);
	return ret;
}

static int ethnl_tsinfo_dump_one_net_topo(struct sk_buff *skb,
					  struct net_device *dev,
					  struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	struct phy_device_node *pdn;
	int ret = 0;

	if (!ctx->netdev_dump_done) {
		ret = ethnl_tsinfo_dump_one_netdev(skb, dev, cb);
		if (ret < 0 && ret != -EOPNOTSUPP)
			return ret;
		ctx->netdev_dump_done = true;
	}

	if (!dev->link_topo) {
		if (phy_has_tsinfo(dev->phydev)) {
			ret = ethnl_tsinfo_dump_one_phydev(skb, dev,
							   dev->phydev, cb);
			if (ret < 0 && ret != -EOPNOTSUPP)
				return ret;
		}

		return 0;
	}

	xa_for_each_start(&dev->link_topo->phys, ctx->pos_phyindex, pdn,
			  ctx->pos_phyindex) {
		if (phy_has_tsinfo(pdn->phy)) {
			ret = ethnl_tsinfo_dump_one_phydev(skb, dev,
							   pdn->phy, cb);
			if (ret < 0 && ret != -EOPNOTSUPP)
				return ret;
		}
	}

	return ret;
}

int ethnl_tsinfo_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	if (ctx->req_info->base.dev) {
		ret = ethnl_tsinfo_dump_one_net_topo(skb,
						     ctx->req_info->base.dev,
						     cb);
	} else {
		for_each_netdev_dump(net, dev, ctx->pos_ifindex) {
			ret = ethnl_tsinfo_dump_one_net_topo(skb, dev, cb);
			if (ret < 0 && ret != -EOPNOTSUPP)
				break;
			ctx->pos_phyindex = 0;
			ctx->netdev_dump_done = false;
			ctx->pos_phcqualifier = HWTSTAMP_PROVIDER_QUALIFIER_PRECISE;
		}
	}
	rtnl_unlock();

	return ret;
}

int ethnl_tsinfo_start(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	struct nlattr **tb = info->info.attrs;
	struct tsinfo_reply_data *reply_data;
	struct tsinfo_req_info *req_info;
	int ret;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));

	req_info = kzalloc(sizeof(*req_info), GFP_KERNEL);
	if (!req_info)
		return -ENOMEM;
	reply_data = kzalloc(sizeof(*reply_data), GFP_KERNEL);
	if (!reply_data) {
		ret = -ENOMEM;
		goto free_req_info;
	}

	ret = ethnl_parse_header_dev_get(&req_info->base,
					 tb[ETHTOOL_A_TSINFO_HEADER],
					 sock_net(cb->skb->sk), cb->extack,
					 false);
	if (ret < 0)
		goto free_reply_data;

	ctx->req_info = req_info;
	ctx->reply_data = reply_data;
	ctx->pos_ifindex = 0;
	ctx->pos_phyindex = 0;
	ctx->netdev_dump_done = false;
	ctx->pos_phcqualifier = HWTSTAMP_PROVIDER_QUALIFIER_PRECISE;

	return 0;

free_reply_data:
	kfree(reply_data);
free_req_info:
	kfree(req_info);

	return ret;
}

int ethnl_tsinfo_done(struct netlink_callback *cb)
{
	struct ethnl_tsinfo_dump_ctx *ctx = (void *)cb->ctx;
	struct tsinfo_req_info *req_info = ctx->req_info;

	ethnl_parse_header_dev_put(&req_info->base);
	kfree(ctx->reply_data);
	kfree(ctx->req_info);

	return 0;
}

const struct ethnl_request_ops ethnl_tsinfo_request_ops = {
	.request_cmd		= ETHTOOL_MSG_TSINFO_GET,
	.reply_cmd		= ETHTOOL_MSG_TSINFO_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_TSINFO_HEADER,
	.req_info_size		= sizeof(struct tsinfo_req_info),
	.reply_data_size	= sizeof(struct tsinfo_reply_data),

	.parse_request		= tsinfo_parse_request,
	.prepare_data		= tsinfo_prepare_data,
	.reply_size		= tsinfo_reply_size,
	.fill_reply		= tsinfo_fill_reply,
};
