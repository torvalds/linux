// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/handshake.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "genl.h"

#include <linux/handshake.h>

/* HANDSHAKE_CMD_ACCEPT - do */
static const struct nla_policy handshake_accept_nl_policy[HANDSHAKE_A_ACCEPT_HANDLER_CLASS + 1] = {
	[HANDSHAKE_A_ACCEPT_HANDLER_CLASS] = NLA_POLICY_MAX(NLA_U32, 2),
};

/* HANDSHAKE_CMD_DONE - do */
static const struct nla_policy handshake_done_nl_policy[HANDSHAKE_A_DONE_REMOTE_AUTH + 1] = {
	[HANDSHAKE_A_DONE_STATUS] = { .type = NLA_U32, },
	[HANDSHAKE_A_DONE_SOCKFD] = { .type = NLA_U32, },
	[HANDSHAKE_A_DONE_REMOTE_AUTH] = { .type = NLA_U32, },
};

/* Ops table for handshake */
static const struct genl_split_ops handshake_nl_ops[] = {
	{
		.cmd		= HANDSHAKE_CMD_ACCEPT,
		.doit		= handshake_nl_accept_doit,
		.policy		= handshake_accept_nl_policy,
		.maxattr	= HANDSHAKE_A_ACCEPT_HANDLER_CLASS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= HANDSHAKE_CMD_DONE,
		.doit		= handshake_nl_done_doit,
		.policy		= handshake_done_nl_policy,
		.maxattr	= HANDSHAKE_A_DONE_REMOTE_AUTH,
		.flags		= GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group handshake_nl_mcgrps[] = {
	[HANDSHAKE_NLGRP_NONE] = { "none", },
	[HANDSHAKE_NLGRP_TLSHD] = { "tlshd", },
};

struct genl_family handshake_nl_family __ro_after_init = {
	.name		= HANDSHAKE_FAMILY_NAME,
	.version	= HANDSHAKE_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= handshake_nl_ops,
	.n_split_ops	= ARRAY_SIZE(handshake_nl_ops),
	.mcgrps		= handshake_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(handshake_nl_mcgrps),
};
