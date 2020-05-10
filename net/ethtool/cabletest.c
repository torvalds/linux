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
	if (!phydev->nest)
		goto out;

	return 0;

out:
	nlmsg_free(phydev->skb);
	return err;
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_alloc);

void ethnl_cable_test_free(struct phy_device *phydev)
{
	nlmsg_free(phydev->skb);
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_free);

void ethnl_cable_test_finished(struct phy_device *phydev)
{
	nla_nest_end(phydev->skb, phydev->nest);

	genlmsg_end(phydev->skb, phydev->ehdr);

	ethnl_multicast(phydev->skb, phydev->attached_dev);
}
EXPORT_SYMBOL_GPL(ethnl_cable_test_finished);
