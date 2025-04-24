// SPDX-License-Identifier: GPL-2.0-only

#include <linux/phy.h>
#include <linux/ethtool_netlink.h>
#include "netlink.h"
#include "common.h"

/* 802.3 standard allows 100 meters for BaseT cables. However longer
 * cables might work, depending on the quality of the cables and the
 * PHY. So allow testing for up to 150 meters.
 */
#define MAX_CABLE_LENGTH_CM (150 * 100)

const struct nla_policy ethnl_cable_test_act_policy[] = {
	[ETHTOOL_A_CABLE_TEST_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy_phy),
};

static int ethnl_cable_test_started(struct phy_device *phydev, u8 cmd)
{
	struct sk_buff *skb;
	int err = -ENOMEM;
	void *ehdr;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		goto out;

	ehdr = ethnl_bcastmsg_put(skb, cmd);
	if (!ehdr) {
		err = -EMSGSIZE;
		goto out;
	}

	err = ethnl_fill_reply_header(skb, phydev->attached_dev,
				      ETHTOOL_A_CABLE_TEST_NTF_HEADER);
	if (err)
		goto out;

	err = nla_put_u8(skb, ETHTOOL_A_CABLE_TEST_NTF_STATUS,
			 ETHTOOL_A_CABLE_TEST_NTF_STATUS_STARTED);
	if (err)
		goto out;

	genlmsg_end(skb, ehdr);

	return ethnl_multicast(skb, phydev->attached_dev);

out:
	nlmsg_free(skb);
	phydev_err(phydev, "%s: Error %pe\n", __func__, ERR_PTR(err));

	return err;
}

int ethnl_act_cable_test(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_req_info req_info = {};
	const struct ethtool_phy_ops *ops;
	struct nlattr **tb = info->attrs;
	struct phy_device *phydev;
	struct net_device *dev;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_CABLE_TEST_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	dev = req_info.dev;

	rtnl_lock();
	phydev = ethnl_req_get_phydev(&req_info, tb,
				      ETHTOOL_A_CABLE_TEST_HEADER,
				      info->extack);
	if (IS_ERR_OR_NULL(phydev)) {
		ret = -EOPNOTSUPP;
		goto out_rtnl;
	}

	ops = ethtool_phy_ops;
	if (!ops || !ops->start_cable_test) {
		ret = -EOPNOTSUPP;
		goto out_rtnl;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = ops->start_cable_test(phydev, info->extack);

	ethnl_ops_complete(dev);

	if (!ret)
		ethnl_cable_test_started(phydev, ETHTOOL_MSG_CABLE_TEST_NTF);

out_rtnl:
	rtnl_unlock();
	ethnl_parse_header_dev_put(&req_info);
	return ret;
}

int ethnl_cable_test_alloc(struct phy_device *phydev, u8 cmd)
{
	int err = -ENOMEM;

	/* One TDR sample occupies 20 bytes. For a 150 meter cable,
	 * with four pairs, around 12K is needed.
	 */
	phydev->skb = genlmsg_new(SZ_16K, GFP_KERNEL);
	if (!phydev->skb)
		goto out;

	phydev->ehdr = ethnl_bcastmsg_put(phydev->skb, cmd);
	if (!phydev->ehdr) {
		err = -EMSGSIZE;
		goto out;
	}

	err = ethnl_fill_reply_header(phydev->skb, phydev->attached_dev,
				      ETHTOOL_A_CABLE_TEST_NTF_HEADER);
	if (err)
		goto out;

	err = nla_put_u8(phydev->skb, ETHTOOL_A_CABLE_TEST_NTF_STATUS,
			 ETHTOOL_A_CABLE_TEST_NTF_STATUS_COMPLETED);
	if (err)
		goto out;

	phydev->nest = nla_nest_start(phydev->skb,
				      ETHTOOL_A_CABLE_TEST_NTF_NEST);
	if (!phydev->nest) {
		err = -EMSGSIZE;
		goto out;
	}

	return 0;

out:
	nlmsg_free(phydev->skb);
	phydev->skb = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_alloc);

void ethnl_cable_test_free(struct phy_device *phydev)
{
	nlmsg_free(phydev->skb);
	phydev->skb = NULL;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_free);

void ethnl_cable_test_finished(struct phy_device *phydev)
{
	nla_nest_end(phydev->skb, phydev->nest);

	genlmsg_end(phydev->skb, phydev->ehdr);

	ethnl_multicast(phydev->skb, phydev->attached_dev);
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_finished);

int ethnl_cable_test_result_with_src(struct phy_device *phydev, u8 pair,
				     u8 result, u32 src)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = nla_nest_start(phydev->skb, ETHTOOL_A_CABLE_NEST_RESULT);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u8(phydev->skb, ETHTOOL_A_CABLE_RESULT_PAIR, pair))
		goto err;
	if (nla_put_u8(phydev->skb, ETHTOOL_A_CABLE_RESULT_CODE, result))
		goto err;
	if (src != ETHTOOL_A_CABLE_INF_SRC_UNSPEC) {
		if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_RESULT_SRC, src))
			goto err;
	}

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_result_with_src);

int ethnl_cable_test_fault_length_with_src(struct phy_device *phydev, u8 pair,
					   u32 cm, u32 src)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = nla_nest_start(phydev->skb,
			      ETHTOOL_A_CABLE_NEST_FAULT_LENGTH);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u8(phydev->skb, ETHTOOL_A_CABLE_FAULT_LENGTH_PAIR, pair))
		goto err;
	if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_FAULT_LENGTH_CM, cm))
		goto err;
	if (src != ETHTOOL_A_CABLE_INF_SRC_UNSPEC) {
		if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_FAULT_LENGTH_SRC,
				src))
			goto err;
	}

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_fault_length_with_src);

static const struct nla_policy cable_test_tdr_act_cfg_policy[] = {
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_FIRST]	= { .type = NLA_U32 },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_LAST]	= { .type = NLA_U32 },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP]	= { .type = NLA_U32 },
	[ETHTOOL_A_CABLE_TEST_TDR_CFG_PAIR]	= { .type = NLA_U8 },
};

const struct nla_policy ethnl_cable_test_tdr_act_policy[] = {
	[ETHTOOL_A_CABLE_TEST_TDR_HEADER]	=
		NLA_POLICY_NESTED(ethnl_header_policy_phy),
	[ETHTOOL_A_CABLE_TEST_TDR_CFG]		= { .type = NLA_NESTED },
};

/* CABLE_TEST_TDR_ACT */
static int ethnl_act_cable_test_tdr_cfg(const struct nlattr *nest,
					struct genl_info *info,
					struct phy_tdr_config *cfg)
{
	struct nlattr *tb[ARRAY_SIZE(cable_test_tdr_act_cfg_policy)];
	int ret;

	cfg->first = 100;
	cfg->step = 100;
	cfg->last = MAX_CABLE_LENGTH_CM;
	cfg->pair = PHY_PAIR_ALL;

	if (!nest)
		return 0;

	ret = nla_parse_nested(tb,
			       ARRAY_SIZE(cable_test_tdr_act_cfg_policy) - 1,
			       nest, cable_test_tdr_act_cfg_policy,
			       info->extack);
	if (ret < 0)
		return ret;

	if (tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_FIRST])
		cfg->first = nla_get_u32(
			tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_FIRST]);

	if (tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_LAST])
		cfg->last = nla_get_u32(tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_LAST]);

	if (tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP])
		cfg->step = nla_get_u32(tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP]);

	if (tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_PAIR]) {
		cfg->pair = nla_get_u8(tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_PAIR]);
		if (cfg->pair > ETHTOOL_A_CABLE_PAIR_D) {
			NL_SET_ERR_MSG_ATTR(
				info->extack,
				tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_PAIR],
				"invalid pair parameter");
			return -EINVAL;
		}
	}

	if (cfg->first > MAX_CABLE_LENGTH_CM) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_FIRST],
				    "invalid first parameter");
		return -EINVAL;
	}

	if (cfg->last > MAX_CABLE_LENGTH_CM) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_LAST],
				    "invalid last parameter");
		return -EINVAL;
	}

	if (cfg->first > cfg->last) {
		NL_SET_ERR_MSG(info->extack, "invalid first/last parameter");
		return -EINVAL;
	}

	if (!cfg->step) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP],
				    "invalid step parameter");
		return -EINVAL;
	}

	if (cfg->step > (cfg->last - cfg->first)) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_CABLE_TEST_TDR_CFG_STEP],
				    "step parameter too big");
		return -EINVAL;
	}

	return 0;
}

int ethnl_act_cable_test_tdr(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_req_info req_info = {};
	const struct ethtool_phy_ops *ops;
	struct nlattr **tb = info->attrs;
	struct phy_device *phydev;
	struct phy_tdr_config cfg;
	struct net_device *dev;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_CABLE_TEST_TDR_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	dev = req_info.dev;

	ret = ethnl_act_cable_test_tdr_cfg(tb[ETHTOOL_A_CABLE_TEST_TDR_CFG],
					   info, &cfg);
	if (ret)
		goto out_dev_put;

	rtnl_lock();
	phydev = ethnl_req_get_phydev(&req_info, tb,
				      ETHTOOL_A_CABLE_TEST_TDR_HEADER,
				      info->extack);
	if (IS_ERR_OR_NULL(phydev)) {
		ret = -EOPNOTSUPP;
		goto out_rtnl;
	}

	ops = ethtool_phy_ops;
	if (!ops || !ops->start_cable_test_tdr) {
		ret = -EOPNOTSUPP;
		goto out_rtnl;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = ops->start_cable_test_tdr(phydev, info->extack, &cfg);

	ethnl_ops_complete(dev);

	if (!ret)
		ethnl_cable_test_started(phydev,
					 ETHTOOL_MSG_CABLE_TEST_TDR_NTF);

out_rtnl:
	rtnl_unlock();
out_dev_put:
	ethnl_parse_header_dev_put(&req_info);
	return ret;
}

int ethnl_cable_test_amplitude(struct phy_device *phydev,
			       u8 pair, s16 mV)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = nla_nest_start(phydev->skb,
			      ETHTOOL_A_CABLE_TDR_NEST_AMPLITUDE);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u8(phydev->skb, ETHTOOL_A_CABLE_AMPLITUDE_PAIR, pair))
		goto err;
	if (nla_put_u16(phydev->skb, ETHTOOL_A_CABLE_AMPLITUDE_mV, mV))
		goto err;

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_amplitude);

int ethnl_cable_test_pulse(struct phy_device *phydev, u16 mV)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = nla_nest_start(phydev->skb, ETHTOOL_A_CABLE_TDR_NEST_PULSE);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u16(phydev->skb, ETHTOOL_A_CABLE_PULSE_mV, mV))
		goto err;

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_pulse);

int ethnl_cable_test_step(struct phy_device *phydev, u32 first, u32 last,
			  u32 step)
{
	struct nlattr *nest;
	int ret = -EMSGSIZE;

	nest = nla_nest_start(phydev->skb, ETHTOOL_A_CABLE_TDR_NEST_STEP);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_STEP_FIRST_DISTANCE,
			first))
		goto err;

	if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_STEP_LAST_DISTANCE, last))
		goto err;

	if (nla_put_u32(phydev->skb, ETHTOOL_A_CABLE_STEP_STEP_DISTANCE, step))
		goto err;

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_step);
