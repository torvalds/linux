// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 NXP
 */
#include <linux/netdevice.h>
#include <net/rtnetlink.h>

#include "netlink.h"
#include "user.h"

static const struct nla_policy dsa_policy[IFLA_DSA_MAX + 1] = {
	[IFLA_DSA_CONDUIT]	= { .type = NLA_U32 },
};

static int dsa_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	int err;

	if (!data)
		return 0;

	if (data[IFLA_DSA_CONDUIT]) {
		u32 ifindex = nla_get_u32(data[IFLA_DSA_CONDUIT]);
		struct net_device *conduit;

		conduit = __dev_get_by_index(dev_net(dev), ifindex);
		if (!conduit)
			return -EINVAL;

		err = dsa_user_change_conduit(dev, conduit, extack);
		if (err)
			return err;
	}

	return 0;
}

static size_t dsa_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(u32)) +	/* IFLA_DSA_CONDUIT  */
	       0;
}

static int dsa_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct net_device *conduit = dsa_user_to_conduit(dev);

	if (nla_put_u32(skb, IFLA_DSA_CONDUIT, conduit->ifindex))
		return -EMSGSIZE;

	return 0;
}

struct rtnl_link_ops dsa_link_ops __read_mostly = {
	.kind			= "dsa",
	.priv_size		= sizeof(struct dsa_port),
	.maxtype		= IFLA_DSA_MAX,
	.policy			= dsa_policy,
	.changelink		= dsa_changelink,
	.get_size		= dsa_get_size,
	.fill_info		= dsa_fill_info,
	.netns_refund		= true,
};
