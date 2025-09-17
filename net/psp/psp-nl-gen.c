// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/psp.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "psp-nl-gen.h"

#include <uapi/linux/psp.h>

/* Common nested types */
const struct nla_policy psp_keys_nl_policy[PSP_A_KEYS_SPI + 1] = {
	[PSP_A_KEYS_KEY] = { .type = NLA_BINARY, },
	[PSP_A_KEYS_SPI] = { .type = NLA_U32, },
};

/* PSP_CMD_DEV_GET - do */
static const struct nla_policy psp_dev_get_nl_policy[PSP_A_DEV_ID + 1] = {
	[PSP_A_DEV_ID] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* PSP_CMD_DEV_SET - do */
static const struct nla_policy psp_dev_set_nl_policy[PSP_A_DEV_PSP_VERSIONS_ENA + 1] = {
	[PSP_A_DEV_ID] = NLA_POLICY_MIN(NLA_U32, 1),
	[PSP_A_DEV_PSP_VERSIONS_ENA] = NLA_POLICY_MASK(NLA_U32, 0xf),
};

/* PSP_CMD_KEY_ROTATE - do */
static const struct nla_policy psp_key_rotate_nl_policy[PSP_A_DEV_ID + 1] = {
	[PSP_A_DEV_ID] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* PSP_CMD_RX_ASSOC - do */
static const struct nla_policy psp_rx_assoc_nl_policy[PSP_A_ASSOC_SOCK_FD + 1] = {
	[PSP_A_ASSOC_DEV_ID] = NLA_POLICY_MIN(NLA_U32, 1),
	[PSP_A_ASSOC_VERSION] = NLA_POLICY_MAX(NLA_U32, 3),
	[PSP_A_ASSOC_SOCK_FD] = { .type = NLA_U32, },
};

/* PSP_CMD_TX_ASSOC - do */
static const struct nla_policy psp_tx_assoc_nl_policy[PSP_A_ASSOC_SOCK_FD + 1] = {
	[PSP_A_ASSOC_DEV_ID] = NLA_POLICY_MIN(NLA_U32, 1),
	[PSP_A_ASSOC_VERSION] = NLA_POLICY_MAX(NLA_U32, 3),
	[PSP_A_ASSOC_TX_KEY] = NLA_POLICY_NESTED(psp_keys_nl_policy),
	[PSP_A_ASSOC_SOCK_FD] = { .type = NLA_U32, },
};

/* Ops table for psp */
static const struct genl_split_ops psp_nl_ops[] = {
	{
		.cmd		= PSP_CMD_DEV_GET,
		.pre_doit	= psp_device_get_locked,
		.doit		= psp_nl_dev_get_doit,
		.post_doit	= psp_device_unlock,
		.policy		= psp_dev_get_nl_policy,
		.maxattr	= PSP_A_DEV_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= PSP_CMD_DEV_GET,
		.dumpit	= psp_nl_dev_get_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= PSP_CMD_DEV_SET,
		.pre_doit	= psp_device_get_locked,
		.doit		= psp_nl_dev_set_doit,
		.post_doit	= psp_device_unlock,
		.policy		= psp_dev_set_nl_policy,
		.maxattr	= PSP_A_DEV_PSP_VERSIONS_ENA,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= PSP_CMD_KEY_ROTATE,
		.pre_doit	= psp_device_get_locked,
		.doit		= psp_nl_key_rotate_doit,
		.post_doit	= psp_device_unlock,
		.policy		= psp_key_rotate_nl_policy,
		.maxattr	= PSP_A_DEV_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= PSP_CMD_RX_ASSOC,
		.pre_doit	= psp_assoc_device_get_locked,
		.doit		= psp_nl_rx_assoc_doit,
		.post_doit	= psp_device_unlock,
		.policy		= psp_rx_assoc_nl_policy,
		.maxattr	= PSP_A_ASSOC_SOCK_FD,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= PSP_CMD_TX_ASSOC,
		.pre_doit	= psp_assoc_device_get_locked,
		.doit		= psp_nl_tx_assoc_doit,
		.post_doit	= psp_device_unlock,
		.policy		= psp_tx_assoc_nl_policy,
		.maxattr	= PSP_A_ASSOC_SOCK_FD,
		.flags		= GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group psp_nl_mcgrps[] = {
	[PSP_NLGRP_MGMT] = { "mgmt", },
	[PSP_NLGRP_USE] = { "use", },
};

struct genl_family psp_nl_family __ro_after_init = {
	.name		= PSP_FAMILY_NAME,
	.version	= PSP_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= psp_nl_ops,
	.n_split_ops	= ARRAY_SIZE(psp_nl_ops),
	.mcgrps		= psp_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(psp_nl_mcgrps),
};
