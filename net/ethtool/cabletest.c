// SPDX-License-Identifier: GPL-2.0-only

#include <linux/phy.h>
#include "netlink.h"
#include "common.h"

/* CABLE_TEST_ACT */

static const struct nla_policy
cable_test_act_policy[ETHTOOL_A_CABLE_TEST_MAX + 1] = {
	[ETHTOOL_A_CABLE_TEST_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_CABLE_TEST_HEADER]		= { .type = NLA_NESTED },
};

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
out_rtnl:
	rtnl_unlock();
out_dev_put:
	dev_put(dev);
	return ret;
}
