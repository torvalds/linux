// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/lockd.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink.h"

#include <uapi/linux/lockd_netlink.h>

/* LOCKD_CMD_SERVER_SET - do */
static const struct nla_policy lockd_server_set_nl_policy[LOCKD_A_SERVER_UDP_PORT + 1] = {
	[LOCKD_A_SERVER_GRACETIME] = { .type = NLA_U32, },
	[LOCKD_A_SERVER_TCP_PORT] = { .type = NLA_U16, },
	[LOCKD_A_SERVER_UDP_PORT] = { .type = NLA_U16, },
};

/* Ops table for lockd */
static const struct genl_split_ops lockd_nl_ops[] = {
	{
		.cmd		= LOCKD_CMD_SERVER_SET,
		.doit		= lockd_nl_server_set_doit,
		.policy		= lockd_server_set_nl_policy,
		.maxattr	= LOCKD_A_SERVER_UDP_PORT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= LOCKD_CMD_SERVER_GET,
		.doit	= lockd_nl_server_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
};

struct genl_family lockd_nl_family __ro_after_init = {
	.name		= LOCKD_FAMILY_NAME,
	.version	= LOCKD_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= lockd_nl_ops,
	.n_split_ops	= ARRAY_SIZE(lockd_nl_ops),
};
