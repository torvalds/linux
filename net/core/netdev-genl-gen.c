// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netdev-genl-gen.h"

#include <uapi/linux/netdev.h>

/* NETDEV_CMD_DEV_GET - do */
static const struct nla_policy netdev_dev_get_nl_policy[NETDEV_A_DEV_IFINDEX + 1] = {
	[NETDEV_A_DEV_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* Ops table for netdev */
static const struct genl_split_ops netdev_nl_ops[] = {
	{
		.cmd		= NETDEV_CMD_DEV_GET,
		.doit		= netdev_nl_dev_get_doit,
		.policy		= netdev_dev_get_nl_policy,
		.maxattr	= NETDEV_A_DEV_IFINDEX,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NETDEV_CMD_DEV_GET,
		.dumpit	= netdev_nl_dev_get_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
};

static const struct genl_multicast_group netdev_nl_mcgrps[] = {
	[NETDEV_NLGRP_MGMT] = { "mgmt", },
};

struct genl_family netdev_nl_family __ro_after_init = {
	.name		= NETDEV_FAMILY_NAME,
	.version	= NETDEV_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= netdev_nl_ops,
	.n_split_ops	= ARRAY_SIZE(netdev_nl_ops),
	.mcgrps		= netdev_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(netdev_nl_mcgrps),
};
