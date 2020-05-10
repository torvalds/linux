// SPDX-License-Identifier: GPL-2.0-only

#include <linux/phy.h>
#include <linux/ethtool_netlink.h>
#include "netlink.h"
#include "common.h"

/* CABLE_TEST_ACT */

static const struct nla_policy
cable_test_act_policy[ETHTOOL_A_CABLE_TEST_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CABLE_TEST_HEADER]		= { .type = NLA_NESTED },
};

static int ethnl_cable_test_started(struct phy_device *phydev)
{
	struct sk_buff *skb;
	int err = -ENOMEM;
	void *ehdr;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		goto out;

	ehdr = ethnl_bcastmsg_put(skb, ETHTOOL_MSG_CABLE_TEST_NTF);
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
	struct nlattr *tb[ETHTOOL_A_CABLE_TEST_MAX + 1];
	struct ethnl_req_info req_info = {};
	struct net_device *dev;
	int ret;

	ret = nlmsg_parse(info->nlhdr, GENL_HDRLEN, tb,
			  ETHTOOL_A_CABLE_TEST_MAX,
			  cable_test_act_policy, info->extack);
	if (ret < 0)
		return ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_CABLE_TEST_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	dev = req_info.dev;
	if (!dev->phydev) {
		ret = -EOPNOTSUPP;
		goto out_dev_put;
	}

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = phy_start_cable_test(dev->phydev, info->extack);

	ethnl_ops_complete(dev);

	if (!ret)
		ethnl_cable_test_started(dev->phydev);

out_rtnl:
	rtnl_unlock();
out_dev_put:
	dev_put(dev);
	return ret;
}

int ethnl_cable_test_alloc(struct phy_device *phydev)
{
	int err = -ENOMEM;

	phydev->skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!phydev->skb)
		goto out;

	phydev->ehdr = ethnl_bcastmsg_put(phydev->skb,
					  ETHTOOL_MSG_CABLE_TEST_NTF);
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

int ethnl_cable_test_result(struct phy_device *phydev, u8 pair, u8 result)
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

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_result);

int ethnl_cable_test_fault_length(struct phy_device *phydev, u8 pair, u32 cm)
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

	nla_nest_end(phydev->skb, nest);
	return 0;

err:
	nla_nest_cancel(phydev->skb, nest);
	return ret;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_fault_length);
