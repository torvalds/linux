// SPDX-License-Identifier: GPL-2.0-only

#include <linux/phy.h>
#include <linux/ethtool_netlink.h>

#include "netlink.h"
#include "common.h"

struct plca_req_info {
	struct ethnl_req_info		base;
};

struct plca_reply_data {
	struct ethnl_reply_data		base;
	struct phy_plca_cfg		plca_cfg;
	struct phy_plca_status		plca_st;
};

// Helpers ------------------------------------------------------------------ //

#define PLCA_REPDATA(__reply_base) \
	container_of(__reply_base, struct plca_reply_data, base)

// PLCA get configuration message ------------------------------------------- //

const struct nla_policy ethnl_plca_get_cfg_policy[] = {
	[ETHTOOL_A_PLCA_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static void plca_update_sint(int *dst, struct nlattr **tb, u32 attrid,
			     bool *mod)
{
	const struct nlattr *attr = tb[attrid];

	if (!attr ||
	    WARN_ON_ONCE(attrid >= ARRAY_SIZE(ethnl_plca_set_cfg_policy)))
		return;

	switch (ethnl_plca_set_cfg_policy[attrid].type) {
	case NLA_U8:
		*dst = nla_get_u8(attr);
		break;
	case NLA_U32:
		*dst = nla_get_u32(attr);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	*mod = true;
}

static int plca_get_cfg_prepare_data(const struct ethnl_req_info *req_base,
				     struct ethnl_reply_data *reply_base,
				     const struct genl_info *info)
{
	struct plca_reply_data *data = PLCA_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_phy_ops *ops;
	int ret;

	// check that the PHY device is available and connected
	if (!req_base->phydev) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	// note: rtnl_lock is held already by ethnl_default_doit
	ops = ethtool_phy_ops;
	if (!ops || !ops->get_plca_cfg) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out;

	memset(&data->plca_cfg, 0xff,
	       sizeof_field(struct plca_reply_data, plca_cfg));

	ret = ops->get_plca_cfg(req_base->phydev, &data->plca_cfg);
	ethnl_ops_complete(dev);

out:
	return ret;
}

static int plca_get_cfg_reply_size(const struct ethnl_req_info *req_base,
				   const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u16)) +	/* _VERSION */
	       nla_total_size(sizeof(u8)) +	/* _ENABLED */
	       nla_total_size(sizeof(u32)) +	/* _NODE_CNT */
	       nla_total_size(sizeof(u32)) +	/* _NODE_ID */
	       nla_total_size(sizeof(u32)) +	/* _TO_TIMER */
	       nla_total_size(sizeof(u32)) +	/* _BURST_COUNT */
	       nla_total_size(sizeof(u32));	/* _BURST_TIMER */
}

static int plca_get_cfg_fill_reply(struct sk_buff *skb,
				   const struct ethnl_req_info *req_base,
				   const struct ethnl_reply_data *reply_base)
{
	const struct plca_reply_data *data = PLCA_REPDATA(reply_base);
	const struct phy_plca_cfg *plca = &data->plca_cfg;

	if ((plca->version >= 0 &&
	     nla_put_u16(skb, ETHTOOL_A_PLCA_VERSION, plca->version)) ||
	    (plca->enabled >= 0 &&
	     nla_put_u8(skb, ETHTOOL_A_PLCA_ENABLED, !!plca->enabled)) ||
	    (plca->node_id >= 0 &&
	     nla_put_u32(skb, ETHTOOL_A_PLCA_NODE_ID, plca->node_id)) ||
	    (plca->node_cnt >= 0 &&
	     nla_put_u32(skb, ETHTOOL_A_PLCA_NODE_CNT, plca->node_cnt)) ||
	    (plca->to_tmr >= 0 &&
	     nla_put_u32(skb, ETHTOOL_A_PLCA_TO_TMR, plca->to_tmr)) ||
	    (plca->burst_cnt >= 0 &&
	     nla_put_u32(skb, ETHTOOL_A_PLCA_BURST_CNT, plca->burst_cnt)) ||
	    (plca->burst_tmr >= 0 &&
	     nla_put_u32(skb, ETHTOOL_A_PLCA_BURST_TMR, plca->burst_tmr)))
		return -EMSGSIZE;

	return 0;
};

// PLCA set configuration message ------------------------------------------- //

const struct nla_policy ethnl_plca_set_cfg_policy[] = {
	[ETHTOOL_A_PLCA_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_PLCA_ENABLED]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_PLCA_NODE_ID]	= NLA_POLICY_MAX(NLA_U32, 255),
	[ETHTOOL_A_PLCA_NODE_CNT]	= NLA_POLICY_RANGE(NLA_U32, 1, 255),
	[ETHTOOL_A_PLCA_TO_TMR]		= NLA_POLICY_MAX(NLA_U32, 255),
	[ETHTOOL_A_PLCA_BURST_CNT]	= NLA_POLICY_MAX(NLA_U32, 255),
	[ETHTOOL_A_PLCA_BURST_TMR]	= NLA_POLICY_MAX(NLA_U32, 255),
};

static int
ethnl_set_plca(struct ethnl_req_info *req_info, struct genl_info *info)
{
	const struct ethtool_phy_ops *ops;
	struct nlattr **tb = info->attrs;
	struct phy_plca_cfg plca_cfg;
	bool mod = false;
	int ret;

	// check that the PHY device is available and connected
	if (!req_info->phydev)
		return -EOPNOTSUPP;

	ops = ethtool_phy_ops;
	if (!ops || !ops->set_plca_cfg)
		return -EOPNOTSUPP;

	memset(&plca_cfg, 0xff, sizeof(plca_cfg));
	plca_update_sint(&plca_cfg.enabled, tb, ETHTOOL_A_PLCA_ENABLED, &mod);
	plca_update_sint(&plca_cfg.node_id, tb, ETHTOOL_A_PLCA_NODE_ID, &mod);
	plca_update_sint(&plca_cfg.node_cnt, tb, ETHTOOL_A_PLCA_NODE_CNT, &mod);
	plca_update_sint(&plca_cfg.to_tmr, tb, ETHTOOL_A_PLCA_TO_TMR, &mod);
	plca_update_sint(&plca_cfg.burst_cnt, tb, ETHTOOL_A_PLCA_BURST_CNT,
			 &mod);
	plca_update_sint(&plca_cfg.burst_tmr, tb, ETHTOOL_A_PLCA_BURST_TMR,
			 &mod);
	if (!mod)
		return 0;

	ret = ops->set_plca_cfg(req_info->phydev, &plca_cfg, info->extack);
	return ret < 0 ? ret : 1;
}

const struct ethnl_request_ops ethnl_plca_cfg_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PLCA_GET_CFG,
	.reply_cmd		= ETHTOOL_MSG_PLCA_GET_CFG_REPLY,
	.hdr_attr		= ETHTOOL_A_PLCA_HEADER,
	.req_info_size		= sizeof(struct plca_req_info),
	.reply_data_size	= sizeof(struct plca_reply_data),

	.prepare_data		= plca_get_cfg_prepare_data,
	.reply_size		= plca_get_cfg_reply_size,
	.fill_reply		= plca_get_cfg_fill_reply,

	.set			= ethnl_set_plca,
	.set_ntf_cmd		= ETHTOOL_MSG_PLCA_NTF,
};

// PLCA get status message -------------------------------------------------- //

const struct nla_policy ethnl_plca_get_status_policy[] = {
	[ETHTOOL_A_PLCA_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int plca_get_status_prepare_data(const struct ethnl_req_info *req_base,
					struct ethnl_reply_data *reply_base,
					const struct genl_info *info)
{
	struct plca_reply_data *data = PLCA_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_phy_ops *ops;
	int ret;

	// check that the PHY device is available and connected
	if (!req_base->phydev) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	// note: rtnl_lock is held already by ethnl_default_doit
	ops = ethtool_phy_ops;
	if (!ops || !ops->get_plca_status) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out;

	memset(&data->plca_st, 0xff,
	       sizeof_field(struct plca_reply_data, plca_st));

	ret = ops->get_plca_status(req_base->phydev, &data->plca_st);
	ethnl_ops_complete(dev);
out:
	return ret;
}

static int plca_get_status_reply_size(const struct ethnl_req_info *req_base,
				      const struct ethnl_reply_data *reply_base)
{
	return nla_total_size(sizeof(u8));	/* _STATUS */
}

static int plca_get_status_fill_reply(struct sk_buff *skb,
				      const struct ethnl_req_info *req_base,
				      const struct ethnl_reply_data *reply_base)
{
	const struct plca_reply_data *data = PLCA_REPDATA(reply_base);
	const u8 status = data->plca_st.pst;

	if (nla_put_u8(skb, ETHTOOL_A_PLCA_STATUS, !!status))
		return -EMSGSIZE;

	return 0;
};

const struct ethnl_request_ops ethnl_plca_status_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PLCA_GET_STATUS,
	.reply_cmd		= ETHTOOL_MSG_PLCA_GET_STATUS_REPLY,
	.hdr_attr		= ETHTOOL_A_PLCA_HEADER,
	.req_info_size		= sizeof(struct plca_req_info),
	.reply_data_size	= sizeof(struct plca_reply_data),

	.prepare_data		= plca_get_status_prepare_data,
	.reply_size		= plca_get_status_reply_size,
	.fill_reply		= plca_get_status_fill_reply,
};
