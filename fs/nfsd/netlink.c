// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink.h"

#include <uapi/linux/nfsd_netlink.h>

/* NFSD_CMD_THREADS_SET - do */
static const struct nla_policy nfsd_threads_set_nl_policy[NFSD_A_SERVER_SCOPE + 1] = {
	[NFSD_A_SERVER_THREADS] = { .type = NLA_U32, },
	[NFSD_A_SERVER_GRACETIME] = { .type = NLA_U32, },
	[NFSD_A_SERVER_LEASETIME] = { .type = NLA_U32, },
	[NFSD_A_SERVER_SCOPE] = { .type = NLA_NUL_STRING, },
};

/* Ops table for nfsd */
static const struct genl_split_ops nfsd_nl_ops[] = {
	{
		.cmd	= NFSD_CMD_RPC_STATUS_GET,
		.start	= nfsd_nl_rpc_status_get_start,
		.dumpit	= nfsd_nl_rpc_status_get_dumpit,
		.done	= nfsd_nl_rpc_status_get_done,
		.flags	= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NFSD_CMD_THREADS_SET,
		.doit		= nfsd_nl_threads_set_doit,
		.policy		= nfsd_threads_set_nl_policy,
		.maxattr	= NFSD_A_SERVER_SCOPE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_THREADS_GET,
		.doit	= nfsd_nl_threads_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
};

struct genl_family nfsd_nl_family __ro_after_init = {
	.name		= NFSD_FAMILY_NAME,
	.version	= NFSD_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= nfsd_nl_ops,
	.n_split_ops	= ARRAY_SIZE(nfsd_nl_ops),
};
