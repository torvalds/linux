// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dev-energymodel.yaml */
/* YNL-GEN kernel source */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "em_netlink_autogen.h"

#include <uapi/linux/dev_energymodel.h>

/* DEV_ENERGYMODEL_CMD_GET_PERF_DOMAINS - do */
static const struct nla_policy dev_energymodel_get_perf_domains_nl_policy[DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID + 1] = {
	[DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID] = { .type = NLA_U32, },
};

/* DEV_ENERGYMODEL_CMD_GET_PERF_TABLE - do */
static const struct nla_policy dev_energymodel_get_perf_table_nl_policy[DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID + 1] = {
	[DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID] = { .type = NLA_U32, },
};

/* Ops table for dev_energymodel */
static const struct genl_split_ops dev_energymodel_nl_ops[] = {
	{
		.cmd		= DEV_ENERGYMODEL_CMD_GET_PERF_DOMAINS,
		.doit		= dev_energymodel_nl_get_perf_domains_doit,
		.policy		= dev_energymodel_get_perf_domains_nl_policy,
		.maxattr	= DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= DEV_ENERGYMODEL_CMD_GET_PERF_DOMAINS,
		.dumpit	= dev_energymodel_nl_get_perf_domains_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= DEV_ENERGYMODEL_CMD_GET_PERF_TABLE,
		.doit		= dev_energymodel_nl_get_perf_table_doit,
		.policy		= dev_energymodel_get_perf_table_nl_policy,
		.maxattr	= DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group dev_energymodel_nl_mcgrps[] = {
	[DEV_ENERGYMODEL_NLGRP_EVENT] = { "event", },
};

struct genl_family dev_energymodel_nl_family __ro_after_init = {
	.name		= DEV_ENERGYMODEL_FAMILY_NAME,
	.version	= DEV_ENERGYMODEL_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= dev_energymodel_nl_ops,
	.n_split_ops	= ARRAY_SIZE(dev_energymodel_nl_ops),
	.mcgrps		= dev_energymodel_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(dev_energymodel_nl_mcgrps),
};
