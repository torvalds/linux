// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/em.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "em_netlink_autogen.h"

#include <uapi/linux/energy_model.h>

/* EM_CMD_GET_PD_TABLE - do */
static const struct nla_policy em_get_pd_table_nl_policy[EM_A_PD_TABLE_PD_ID + 1] = {
	[EM_A_PD_TABLE_PD_ID] = { .type = NLA_U32, },
};

/* Ops table for em */
static const struct genl_split_ops em_nl_ops[] = {
	{
		.cmd	= EM_CMD_GET_PDS,
		.doit	= em_nl_get_pds_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= EM_CMD_GET_PD_TABLE,
		.doit		= em_nl_get_pd_table_doit,
		.policy		= em_get_pd_table_nl_policy,
		.maxattr	= EM_A_PD_TABLE_PD_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group em_nl_mcgrps[] = {
	[EM_NLGRP_EVENT] = { "event", },
};

struct genl_family em_nl_family __ro_after_init = {
	.name		= EM_FAMILY_NAME,
	.version	= EM_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= em_nl_ops,
	.n_split_ops	= ARRAY_SIZE(em_nl_ops),
	.mcgrps		= em_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(em_nl_mcgrps),
};
