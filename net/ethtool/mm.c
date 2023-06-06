// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2023 NXP
 */
#include "common.h"
#include "netlink.h"

struct mm_req_info {
	struct ethnl_req_info		base;
};

struct mm_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_mm_state		state;
	struct ethtool_mm_stats		stats;
};

#define MM_REPDATA(__reply_base) \
	container_of(__reply_base, struct mm_reply_data, base)

#define ETHTOOL_MM_STAT_CNT \
	(__ETHTOOL_A_MM_STAT_CNT - (ETHTOOL_A_MM_STAT_PAD + 1))

const struct nla_policy ethnl_mm_get_policy[ETHTOOL_A_MM_HEADER + 1] = {
	[ETHTOOL_A_MM_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy_stats),
};

static int mm_prepare_data(const struct ethnl_req_info *req_base,
			   struct ethnl_reply_data *reply_base,
			   struct genl_info *info)
{
	struct mm_reply_data *data = MM_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_ops *ops;
	int ret;

	ops = dev->ethtool_ops;

	if (!ops->get_mm)
		return -EOPNOTSUPP;

	ethtool_stats_init((u64 *)&data->stats,
			   sizeof(data->stats) / sizeof(u64));

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = ops->get_mm(dev, &data->state);
	if (ret)
		goto out_complete;

	if (ops->get_mm_stats && (req_base->flags & ETHTOOL_FLAG_STATS))
		ops->get_mm_stats(dev, &data->stats);

out_complete:
	ethnl_ops_complete(dev);

	return ret;
}

static int mm_reply_size(const struct ethnl_req_info *req_base,
			 const struct ethnl_reply_data *reply_base)
{
	int len = 0;

	len += nla_total_size(sizeof(u8)); /* _MM_PMAC_ENABLED */
	len += nla_total_size(sizeof(u8)); /* _MM_TX_ENABLED */
	len += nla_total_size(sizeof(u8)); /* _MM_TX_ACTIVE */
	len += nla_total_size(sizeof(u8)); /* _MM_VERIFY_ENABLED */
	len += nla_total_size(sizeof(u8)); /* _MM_VERIFY_STATUS */
	len += nla_total_size(sizeof(u32)); /* _MM_VERIFY_TIME */
	len += nla_total_size(sizeof(u32)); /* _MM_MAX_VERIFY_TIME */
	len += nla_total_size(sizeof(u32)); /* _MM_TX_MIN_FRAG_SIZE */
	len += nla_total_size(sizeof(u32)); /* _MM_RX_MIN_FRAG_SIZE */

	if (req_base->flags & ETHTOOL_FLAG_STATS)
		len += nla_total_size(0) + /* _MM_STATS */
		       nla_total_size_64bit(sizeof(u64)) * ETHTOOL_MM_STAT_CNT;

	return len;
}

static int mm_put_stat(struct sk_buff *skb, u64 val, u16 attrtype)
{
	if (val == ETHTOOL_STAT_NOT_SET)
		return 0;
	if (nla_put_u64_64bit(skb, attrtype, val, ETHTOOL_A_MM_STAT_PAD))
		return -EMSGSIZE;
	return 0;
}

static int mm_put_stats(struct sk_buff *skb,
			const struct ethtool_mm_stats *stats)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, ETHTOOL_A_MM_STATS);
	if (!nest)
		return -EMSGSIZE;

	if (mm_put_stat(skb, stats->MACMergeFrameAssErrorCount,
			ETHTOOL_A_MM_STAT_REASSEMBLY_ERRORS) ||
	    mm_put_stat(skb, stats->MACMergeFrameSmdErrorCount,
			ETHTOOL_A_MM_STAT_SMD_ERRORS) ||
	    mm_put_stat(skb, stats->MACMergeFrameAssOkCount,
			ETHTOOL_A_MM_STAT_REASSEMBLY_OK) ||
	    mm_put_stat(skb, stats->MACMergeFragCountRx,
			ETHTOOL_A_MM_STAT_RX_FRAG_COUNT) ||
	    mm_put_stat(skb, stats->MACMergeFragCountTx,
			ETHTOOL_A_MM_STAT_TX_FRAG_COUNT) ||
	    mm_put_stat(skb, stats->MACMergeHoldCount,
			ETHTOOL_A_MM_STAT_HOLD_COUNT))
		goto err_cancel;

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int mm_fill_reply(struct sk_buff *skb,
			 const struct ethnl_req_info *req_base,
			 const struct ethnl_reply_data *reply_base)
{
	const struct mm_reply_data *data = MM_REPDATA(reply_base);
	const struct ethtool_mm_state *state = &data->state;

	if (nla_put_u8(skb, ETHTOOL_A_MM_TX_ENABLED, state->tx_enabled) ||
	    nla_put_u8(skb, ETHTOOL_A_MM_TX_ACTIVE, state->tx_active) ||
	    nla_put_u8(skb, ETHTOOL_A_MM_PMAC_ENABLED, state->pmac_enabled) ||
	    nla_put_u8(skb, ETHTOOL_A_MM_VERIFY_ENABLED, state->verify_enabled) ||
	    nla_put_u8(skb, ETHTOOL_A_MM_VERIFY_STATUS, state->verify_status) ||
	    nla_put_u32(skb, ETHTOOL_A_MM_VERIFY_TIME, state->verify_time) ||
	    nla_put_u32(skb, ETHTOOL_A_MM_MAX_VERIFY_TIME, state->max_verify_time) ||
	    nla_put_u32(skb, ETHTOOL_A_MM_TX_MIN_FRAG_SIZE, state->tx_min_frag_size) ||
	    nla_put_u32(skb, ETHTOOL_A_MM_RX_MIN_FRAG_SIZE, state->rx_min_frag_size))
		return -EMSGSIZE;

	if (req_base->flags & ETHTOOL_FLAG_STATS &&
	    mm_put_stats(skb, &data->stats))
		return -EMSGSIZE;

	return 0;
}

const struct nla_policy ethnl_mm_set_policy[ETHTOOL_A_MM_MAX + 1] = {
	[ETHTOOL_A_MM_HEADER]		= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_MM_VERIFY_ENABLED]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_MM_VERIFY_TIME]	= NLA_POLICY_RANGE(NLA_U32, 1, 128),
	[ETHTOOL_A_MM_TX_ENABLED]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_MM_PMAC_ENABLED]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_MM_TX_MIN_FRAG_SIZE]	= NLA_POLICY_RANGE(NLA_U32, 60, 252),
};

static void mm_state_to_cfg(const struct ethtool_mm_state *state,
			    struct ethtool_mm_cfg *cfg)
{
	/* We could also compare state->verify_status against
	 * ETHTOOL_MM_VERIFY_STATUS_DISABLED, but state->verify_enabled
	 * is more like an administrative state which should be seen in
	 * ETHTOOL_MSG_MM_GET replies. For example, a port with verification
	 * disabled might be in the ETHTOOL_MM_VERIFY_STATUS_INITIAL
	 * if it's down.
	 */
	cfg->verify_enabled = state->verify_enabled;
	cfg->verify_time = state->verify_time;
	cfg->tx_enabled = state->tx_enabled;
	cfg->pmac_enabled = state->pmac_enabled;
	cfg->tx_min_frag_size = state->tx_min_frag_size;
}

static int
ethnl_set_mm_validate(struct ethnl_req_info *req_info, struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;

	return ops->get_mm && ops->set_mm ? 1 : -EOPNOTSUPP;
}

static int ethnl_set_mm(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct net_device *dev = req_info->dev;
	struct ethtool_mm_state state = {};
	struct nlattr **tb = info->attrs;
	struct ethtool_mm_cfg cfg = {};
	bool mod = false;
	int ret;

	ret = dev->ethtool_ops->get_mm(dev, &state);
	if (ret)
		return ret;

	mm_state_to_cfg(&state, &cfg);

	ethnl_update_bool(&cfg.verify_enabled, tb[ETHTOOL_A_MM_VERIFY_ENABLED],
			  &mod);
	ethnl_update_u32(&cfg.verify_time, tb[ETHTOOL_A_MM_VERIFY_TIME], &mod);
	ethnl_update_bool(&cfg.tx_enabled, tb[ETHTOOL_A_MM_TX_ENABLED], &mod);
	ethnl_update_bool(&cfg.pmac_enabled, tb[ETHTOOL_A_MM_PMAC_ENABLED],
			  &mod);
	ethnl_update_u32(&cfg.tx_min_frag_size,
			 tb[ETHTOOL_A_MM_TX_MIN_FRAG_SIZE], &mod);

	if (!mod)
		return 0;

	if (cfg.verify_time > state.max_verify_time) {
		NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_MM_VERIFY_TIME],
				    "verifyTime exceeds device maximum");
		return -ERANGE;
	}

	if (cfg.verify_enabled && !cfg.tx_enabled) {
		NL_SET_ERR_MSG(extack, "Verification requires TX enabled");
		return -EINVAL;
	}

	if (cfg.tx_enabled && !cfg.pmac_enabled) {
		NL_SET_ERR_MSG(extack, "TX enabled requires pMAC enabled");
		return -EINVAL;
	}

	ret = dev->ethtool_ops->set_mm(dev, &cfg, extack);
	return ret < 0 ? ret : 1;
}

const struct ethnl_request_ops ethnl_mm_request_ops = {
	.request_cmd		= ETHTOOL_MSG_MM_GET,
	.reply_cmd		= ETHTOOL_MSG_MM_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_MM_HEADER,
	.req_info_size		= sizeof(struct mm_req_info),
	.reply_data_size	= sizeof(struct mm_reply_data),

	.prepare_data		= mm_prepare_data,
	.reply_size		= mm_reply_size,
	.fill_reply		= mm_fill_reply,

	.set_validate		= ethnl_set_mm_validate,
	.set			= ethnl_set_mm,
	.set_ntf_cmd		= ETHTOOL_MSG_MM_NTF,
};

/* Returns whether a given device supports the MAC merge layer
 * (has an eMAC and a pMAC). Must be called under rtnl_lock() and
 * ethnl_ops_begin().
 */
bool __ethtool_dev_mm_supported(struct net_device *dev)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_mm_state state = {};
	int ret = -EOPNOTSUPP;

	if (ops && ops->get_mm)
		ret = ops->get_mm(dev, &state);

	return !ret;
}

bool ethtool_dev_mm_supported(struct net_device *dev)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	bool supported;
	int ret;

	ASSERT_RTNL();

	if (!ops)
		return false;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return false;

	supported = __ethtool_dev_mm_supported(dev);

	ethnl_ops_complete(dev);

	return supported;
}
EXPORT_SYMBOL_GPL(ethtool_dev_mm_supported);
