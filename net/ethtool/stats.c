// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct stats_req_info {
	struct ethnl_req_info		base;
	DECLARE_BITMAP(stat_mask, __ETHTOOL_STATS_CNT);
};

#define STATS_REQINFO(__req_base) \
	container_of(__req_base, struct stats_req_info, base)

struct stats_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_eth_phy_stats	phy_stats;
	struct ethtool_eth_mac_stats	mac_stats;
	struct ethtool_eth_ctrl_stats	ctrl_stats;
	struct ethtool_rmon_stats	rmon_stats;
	const struct ethtool_rmon_hist_range	*rmon_ranges;
};

#define STATS_REPDATA(__reply_base) \
	container_of(__reply_base, struct stats_reply_data, base)

const char stats_std_names[__ETHTOOL_STATS_CNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_STATS_ETH_PHY]			= "eth-phy",
	[ETHTOOL_STATS_ETH_MAC]			= "eth-mac",
	[ETHTOOL_STATS_ETH_CTRL]		= "eth-ctrl",
	[ETHTOOL_STATS_RMON]			= "rmon",
};

const char stats_eth_phy_names[__ETHTOOL_A_STATS_ETH_PHY_CNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_A_STATS_ETH_PHY_5_SYM_ERR]	= "SymbolErrorDuringCarrier",
};

const char stats_eth_mac_names[__ETHTOOL_A_STATS_ETH_MAC_CNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_A_STATS_ETH_MAC_2_TX_PKT]	= "FramesTransmittedOK",
	[ETHTOOL_A_STATS_ETH_MAC_3_SINGLE_COL]	= "SingleCollisionFrames",
	[ETHTOOL_A_STATS_ETH_MAC_4_MULTI_COL]	= "MultipleCollisionFrames",
	[ETHTOOL_A_STATS_ETH_MAC_5_RX_PKT]	= "FramesReceivedOK",
	[ETHTOOL_A_STATS_ETH_MAC_6_FCS_ERR]	= "FrameCheckSequenceErrors",
	[ETHTOOL_A_STATS_ETH_MAC_7_ALIGN_ERR]	= "AlignmentErrors",
	[ETHTOOL_A_STATS_ETH_MAC_8_TX_BYTES]	= "OctetsTransmittedOK",
	[ETHTOOL_A_STATS_ETH_MAC_9_TX_DEFER]	= "FramesWithDeferredXmissions",
	[ETHTOOL_A_STATS_ETH_MAC_10_LATE_COL]	= "LateCollisions",
	[ETHTOOL_A_STATS_ETH_MAC_11_XS_COL]	= "FramesAbortedDueToXSColls",
	[ETHTOOL_A_STATS_ETH_MAC_12_TX_INT_ERR]	= "FramesLostDueToIntMACXmitError",
	[ETHTOOL_A_STATS_ETH_MAC_13_CS_ERR]	= "CarrierSenseErrors",
	[ETHTOOL_A_STATS_ETH_MAC_14_RX_BYTES]	= "OctetsReceivedOK",
	[ETHTOOL_A_STATS_ETH_MAC_15_RX_INT_ERR]	= "FramesLostDueToIntMACRcvError",
	[ETHTOOL_A_STATS_ETH_MAC_18_TX_MCAST]	= "MulticastFramesXmittedOK",
	[ETHTOOL_A_STATS_ETH_MAC_19_TX_BCAST]	= "BroadcastFramesXmittedOK",
	[ETHTOOL_A_STATS_ETH_MAC_20_XS_DEFER]	= "FramesWithExcessiveDeferral",
	[ETHTOOL_A_STATS_ETH_MAC_21_RX_MCAST]	= "MulticastFramesReceivedOK",
	[ETHTOOL_A_STATS_ETH_MAC_22_RX_BCAST]	= "BroadcastFramesReceivedOK",
	[ETHTOOL_A_STATS_ETH_MAC_23_IR_LEN_ERR]	= "InRangeLengthErrors",
	[ETHTOOL_A_STATS_ETH_MAC_24_OOR_LEN]	= "OutOfRangeLengthField",
	[ETHTOOL_A_STATS_ETH_MAC_25_TOO_LONG_ERR]	= "FrameTooLongErrors",
};

const char stats_eth_ctrl_names[__ETHTOOL_A_STATS_ETH_CTRL_CNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_A_STATS_ETH_CTRL_3_TX]		= "MACControlFramesTransmitted",
	[ETHTOOL_A_STATS_ETH_CTRL_4_RX]		= "MACControlFramesReceived",
	[ETHTOOL_A_STATS_ETH_CTRL_5_RX_UNSUP]	= "UnsupportedOpcodesReceived",
};

const char stats_rmon_names[__ETHTOOL_A_STATS_RMON_CNT][ETH_GSTRING_LEN] = {
	[ETHTOOL_A_STATS_RMON_UNDERSIZE]	= "etherStatsUndersizePkts",
	[ETHTOOL_A_STATS_RMON_OVERSIZE]		= "etherStatsOversizePkts",
	[ETHTOOL_A_STATS_RMON_FRAG]		= "etherStatsFragments",
	[ETHTOOL_A_STATS_RMON_JABBER]		= "etherStatsJabbers",
};

const struct nla_policy ethnl_stats_get_policy[ETHTOOL_A_STATS_GROUPS + 1] = {
	[ETHTOOL_A_STATS_HEADER]	=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_STATS_GROUPS]	= { .type = NLA_NESTED },
};

static int stats_parse_request(struct ethnl_req_info *req_base,
			       struct nlattr **tb,
			       struct netlink_ext_ack *extack)
{
	struct stats_req_info *req_info = STATS_REQINFO(req_base);
	bool mod = false;
	int err;

	err = ethnl_update_bitset(req_info->stat_mask, __ETHTOOL_STATS_CNT,
				  tb[ETHTOOL_A_STATS_GROUPS], stats_std_names,
				  extack, &mod);
	if (err)
		return err;

	if (!mod) {
		NL_SET_ERR_MSG(extack, "no stats requested");
		return -EINVAL;
	}

	return 0;
}

static int stats_prepare_data(const struct ethnl_req_info *req_base,
			      struct ethnl_reply_data *reply_base,
			      struct genl_info *info)
{
	const struct stats_req_info *req_info = STATS_REQINFO(req_base);
	struct stats_reply_data *data = STATS_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	/* Mark all stats as unset (see ETHTOOL_STAT_NOT_SET) to prevent them
	 * from being reported to user space in case driver did not set them.
	 */
	memset(&data->phy_stats, 0xff, sizeof(data->phy_stats));
	memset(&data->mac_stats, 0xff, sizeof(data->mac_stats));
	memset(&data->ctrl_stats, 0xff, sizeof(data->ctrl_stats));
	memset(&data->rmon_stats, 0xff, sizeof(data->rmon_stats));

	if (test_bit(ETHTOOL_STATS_ETH_PHY, req_info->stat_mask) &&
	    dev->ethtool_ops->get_eth_phy_stats)
		dev->ethtool_ops->get_eth_phy_stats(dev, &data->phy_stats);
	if (test_bit(ETHTOOL_STATS_ETH_MAC, req_info->stat_mask) &&
	    dev->ethtool_ops->get_eth_mac_stats)
		dev->ethtool_ops->get_eth_mac_stats(dev, &data->mac_stats);
	if (test_bit(ETHTOOL_STATS_ETH_CTRL, req_info->stat_mask) &&
	    dev->ethtool_ops->get_eth_ctrl_stats)
		dev->ethtool_ops->get_eth_ctrl_stats(dev, &data->ctrl_stats);
	if (test_bit(ETHTOOL_STATS_RMON, req_info->stat_mask) &&
	    dev->ethtool_ops->get_rmon_stats)
		dev->ethtool_ops->get_rmon_stats(dev, &data->rmon_stats,
						 &data->rmon_ranges);

	ethnl_ops_complete(dev);
	return 0;
}

static int stats_reply_size(const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct stats_req_info *req_info = STATS_REQINFO(req_base);
	unsigned int n_grps = 0, n_stats = 0;
	int len = 0;

	if (test_bit(ETHTOOL_STATS_ETH_PHY, req_info->stat_mask)) {
		n_stats += sizeof(struct ethtool_eth_phy_stats) / sizeof(u64);
		n_grps++;
	}
	if (test_bit(ETHTOOL_STATS_ETH_MAC, req_info->stat_mask)) {
		n_stats += sizeof(struct ethtool_eth_mac_stats) / sizeof(u64);
		n_grps++;
	}
	if (test_bit(ETHTOOL_STATS_ETH_CTRL, req_info->stat_mask)) {
		n_stats += sizeof(struct ethtool_eth_ctrl_stats) / sizeof(u64);
		n_grps++;
	}
	if (test_bit(ETHTOOL_STATS_RMON, req_info->stat_mask)) {
		n_stats += sizeof(struct ethtool_rmon_stats) / sizeof(u64);
		n_grps++;
		/* Above includes the space for _A_STATS_GRP_HIST_VALs */

		len += (nla_total_size(0) +	/* _A_STATS_GRP_HIST */
			nla_total_size(4) +	/* _A_STATS_GRP_HIST_BKT_LOW */
			nla_total_size(4)) *	/* _A_STATS_GRP_HIST_BKT_HI */
			ETHTOOL_RMON_HIST_MAX * 2;
	}

	len += n_grps * (nla_total_size(0) + /* _A_STATS_GRP */
			 nla_total_size(4) + /* _A_STATS_GRP_ID */
			 nla_total_size(4)); /* _A_STATS_GRP_SS_ID */
	len += n_stats * (nla_total_size(0) + /* _A_STATS_GRP_STAT */
			  nla_total_size_64bit(sizeof(u64)));

	return len;
}

static int stat_put(struct sk_buff *skb, u16 attrtype, u64 val)
{
	struct nlattr *nest;
	int ret;

	if (val == ETHTOOL_STAT_NOT_SET)
		return 0;

	/* We want to start stats attr types from 0, so we don't have a type
	 * for pad inside ETHTOOL_A_STATS_GRP_STAT. Pad things on the outside
	 * of ETHTOOL_A_STATS_GRP_STAT. Since we're one nest away from the
	 * actual attr we're 4B off - nla_need_padding_for_64bit() & co.
	 * can't be used.
	 */
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (!IS_ALIGNED((unsigned long)skb_tail_pointer(skb), 8))
		if (!nla_reserve(skb, ETHTOOL_A_STATS_GRP_PAD, 0))
			return -EMSGSIZE;
#endif

	nest = nla_nest_start(skb, ETHTOOL_A_STATS_GRP_STAT);
	if (!nest)
		return -EMSGSIZE;

	ret = nla_put_u64_64bit(skb, attrtype, val, -1 /* not used */);
	if (ret) {
		nla_nest_cancel(skb, nest);
		return ret;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int stats_put_phy_stats(struct sk_buff *skb,
			       const struct stats_reply_data *data)
{
	if (stat_put(skb, ETHTOOL_A_STATS_ETH_PHY_5_SYM_ERR,
		     data->phy_stats.SymbolErrorDuringCarrier))
		return -EMSGSIZE;
	return 0;
}

static int stats_put_mac_stats(struct sk_buff *skb,
			       const struct stats_reply_data *data)
{
	if (stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_2_TX_PKT,
		     data->mac_stats.FramesTransmittedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_3_SINGLE_COL,
		     data->mac_stats.SingleCollisionFrames) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_4_MULTI_COL,
		     data->mac_stats.MultipleCollisionFrames) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_5_RX_PKT,
		     data->mac_stats.FramesReceivedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_6_FCS_ERR,
		     data->mac_stats.FrameCheckSequenceErrors) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_7_ALIGN_ERR,
		     data->mac_stats.AlignmentErrors) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_8_TX_BYTES,
		     data->mac_stats.OctetsTransmittedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_9_TX_DEFER,
		     data->mac_stats.FramesWithDeferredXmissions) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_10_LATE_COL,
		     data->mac_stats.LateCollisions) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_11_XS_COL,
		     data->mac_stats.FramesAbortedDueToXSColls) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_12_TX_INT_ERR,
		     data->mac_stats.FramesLostDueToIntMACXmitError) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_13_CS_ERR,
		     data->mac_stats.CarrierSenseErrors) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_14_RX_BYTES,
		     data->mac_stats.OctetsReceivedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_15_RX_INT_ERR,
		     data->mac_stats.FramesLostDueToIntMACRcvError) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_18_TX_MCAST,
		     data->mac_stats.MulticastFramesXmittedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_19_TX_BCAST,
		     data->mac_stats.BroadcastFramesXmittedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_20_XS_DEFER,
		     data->mac_stats.FramesWithExcessiveDeferral) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_21_RX_MCAST,
		     data->mac_stats.MulticastFramesReceivedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_22_RX_BCAST,
		     data->mac_stats.BroadcastFramesReceivedOK) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_23_IR_LEN_ERR,
		     data->mac_stats.InRangeLengthErrors) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_24_OOR_LEN,
		     data->mac_stats.OutOfRangeLengthField) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_MAC_25_TOO_LONG_ERR,
		     data->mac_stats.FrameTooLongErrors))
		return -EMSGSIZE;
	return 0;
}

static int stats_put_ctrl_stats(struct sk_buff *skb,
				const struct stats_reply_data *data)
{
	if (stat_put(skb, ETHTOOL_A_STATS_ETH_CTRL_3_TX,
		     data->ctrl_stats.MACControlFramesTransmitted) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_CTRL_4_RX,
		     data->ctrl_stats.MACControlFramesReceived) ||
	    stat_put(skb, ETHTOOL_A_STATS_ETH_CTRL_5_RX_UNSUP,
		     data->ctrl_stats.UnsupportedOpcodesReceived))
		return -EMSGSIZE;
	return 0;
}

static int stats_put_rmon_hist(struct sk_buff *skb, u32 attr, const u64 *hist,
			       const struct ethtool_rmon_hist_range *ranges)
{
	struct nlattr *nest;
	int i;

	if (!ranges)
		return 0;

	for (i = 0; i <	ETHTOOL_RMON_HIST_MAX; i++) {
		if (!ranges[i].low && !ranges[i].high)
			break;
		if (hist[i] == ETHTOOL_STAT_NOT_SET)
			continue;

		nest = nla_nest_start(skb, attr);
		if (!nest)
			return -EMSGSIZE;

		if (nla_put_u32(skb, ETHTOOL_A_STATS_GRP_HIST_BKT_LOW,
				ranges[i].low) ||
		    nla_put_u32(skb, ETHTOOL_A_STATS_GRP_HIST_BKT_HI,
				ranges[i].high) ||
		    nla_put_u64_64bit(skb, ETHTOOL_A_STATS_GRP_HIST_VAL,
				      hist[i], ETHTOOL_A_STATS_GRP_PAD))
			goto err_cancel_hist;

		nla_nest_end(skb, nest);
	}

	return 0;

err_cancel_hist:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int stats_put_rmon_stats(struct sk_buff *skb,
				const struct stats_reply_data *data)
{
	if (stats_put_rmon_hist(skb, ETHTOOL_A_STATS_GRP_HIST_RX,
				data->rmon_stats.hist, data->rmon_ranges) ||
	    stats_put_rmon_hist(skb, ETHTOOL_A_STATS_GRP_HIST_TX,
				data->rmon_stats.hist_tx, data->rmon_ranges))
		return -EMSGSIZE;

	if (stat_put(skb, ETHTOOL_A_STATS_RMON_UNDERSIZE,
		     data->rmon_stats.undersize_pkts) ||
	    stat_put(skb, ETHTOOL_A_STATS_RMON_OVERSIZE,
		     data->rmon_stats.oversize_pkts) ||
	    stat_put(skb, ETHTOOL_A_STATS_RMON_FRAG,
		     data->rmon_stats.fragments) ||
	    stat_put(skb, ETHTOOL_A_STATS_RMON_JABBER,
		     data->rmon_stats.jabbers))
		return -EMSGSIZE;

	return 0;
}

static int stats_put_stats(struct sk_buff *skb,
			   const struct stats_reply_data *data,
			   u32 id, u32 ss_id,
			   int (*cb)(struct sk_buff *skb,
				     const struct stats_reply_data *data))
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, ETHTOOL_A_STATS_GRP);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, ETHTOOL_A_STATS_GRP_ID, id) ||
	    nla_put_u32(skb, ETHTOOL_A_STATS_GRP_SS_ID, ss_id))
		goto err_cancel;

	if (cb(skb, data))
		goto err_cancel;

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int stats_fill_reply(struct sk_buff *skb,
			    const struct ethnl_req_info *req_base,
			    const struct ethnl_reply_data *reply_base)
{
	const struct stats_req_info *req_info = STATS_REQINFO(req_base);
	const struct stats_reply_data *data = STATS_REPDATA(reply_base);
	int ret = 0;

	if (!ret && test_bit(ETHTOOL_STATS_ETH_PHY, req_info->stat_mask))
		ret = stats_put_stats(skb, data, ETHTOOL_STATS_ETH_PHY,
				      ETH_SS_STATS_ETH_PHY,
				      stats_put_phy_stats);
	if (!ret && test_bit(ETHTOOL_STATS_ETH_MAC, req_info->stat_mask))
		ret = stats_put_stats(skb, data, ETHTOOL_STATS_ETH_MAC,
				      ETH_SS_STATS_ETH_MAC,
				      stats_put_mac_stats);
	if (!ret && test_bit(ETHTOOL_STATS_ETH_CTRL, req_info->stat_mask))
		ret = stats_put_stats(skb, data, ETHTOOL_STATS_ETH_CTRL,
				      ETH_SS_STATS_ETH_CTRL,
				      stats_put_ctrl_stats);
	if (!ret && test_bit(ETHTOOL_STATS_RMON, req_info->stat_mask))
		ret = stats_put_stats(skb, data, ETHTOOL_STATS_RMON,
				      ETH_SS_STATS_RMON, stats_put_rmon_stats);

	return ret;
}

const struct ethnl_request_ops ethnl_stats_request_ops = {
	.request_cmd		= ETHTOOL_MSG_STATS_GET,
	.reply_cmd		= ETHTOOL_MSG_STATS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_STATS_HEADER,
	.req_info_size		= sizeof(struct stats_req_info),
	.reply_data_size	= sizeof(struct stats_reply_data),

	.parse_request		= stats_parse_request,
	.prepare_data		= stats_prepare_data,
	.reply_size		= stats_reply_size,
	.fill_reply		= stats_fill_reply,
};
