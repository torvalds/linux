// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool_netlink.h>
#include "netlink.h"

/* genetlink setup */

static const struct genl_ops ethtool_genl_ops[] = {
};

static struct genl_family ethtool_genl_family = {
	.name		= ETHTOOL_GENL_NAME,
	.version	= ETHTOOL_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= ethtool_genl_ops,
	.n_ops		= ARRAY_SIZE(ethtool_genl_ops),
};

/* module setup */

static int __init ethnl_init(void)
{
	int ret;

	ret = genl_register_family(&ethtool_genl_family);
	if (WARN(ret < 0, "ethtool: genetlink family registration failed"))
		return ret;

	return 0;
}

subsys_initcall(ethnl_init);
