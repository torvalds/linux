// SPDX-License-Identifier: BSD-3-Clause
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/fou.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "fou_nl.h"

#include <linux/fou.h>

/* Global operation policy for fou */
const struct nla_policy fou_nl_policy[FOU_ATTR_IFINDEX + 1] = {
	[FOU_ATTR_PORT] = { .type = NLA_U16, },
	[FOU_ATTR_AF] = { .type = NLA_U8, },
	[FOU_ATTR_IPPROTO] = { .type = NLA_U8, },
	[FOU_ATTR_TYPE] = { .type = NLA_U8, },
	[FOU_ATTR_REMCSUM_NOPARTIAL] = { .type = NLA_FLAG, },
	[FOU_ATTR_LOCAL_V4] = { .type = NLA_U32, },
	[FOU_ATTR_LOCAL_V6] = { .len = 16, },
	[FOU_ATTR_PEER_V4] = { .type = NLA_U32, },
	[FOU_ATTR_PEER_V6] = { .len = 16, },
	[FOU_ATTR_PEER_PORT] = { .type = NLA_U16, },
	[FOU_ATTR_IFINDEX] = { .type = NLA_S32, },
};

/* Ops table for fou */
const struct genl_small_ops fou_nl_ops[3] = {
	{
		.cmd		= FOU_CMD_ADD,
		.validate	= GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= fou_nl_add_doit,
		.flags		= GENL_ADMIN_PERM,
	},
	{
		.cmd		= FOU_CMD_DEL,
		.validate	= GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= fou_nl_del_doit,
		.flags		= GENL_ADMIN_PERM,
	},
	{
		.cmd		= FOU_CMD_GET,
		.validate	= GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= fou_nl_get_doit,
		.dumpit		= fou_nl_get_dumpit,
	},
};
