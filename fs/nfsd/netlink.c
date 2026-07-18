// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN kernel source */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netlink.h"

#include <uapi/linux/nfsd_netlink.h>

/* Common nested types */
const struct nla_policy nfsd_auth_flavor_nl_policy[NFSD_A_AUTH_FLAVOR_FLAGS + 1] = {
	[NFSD_A_AUTH_FLAVOR_PSEUDOFLAVOR] = { .type = NLA_U32, },
	[NFSD_A_AUTH_FLAVOR_FLAGS] = NLA_POLICY_MASK(NLA_U32, 0x3ffff),
};

const struct nla_policy nfsd_expkey_nl_policy[NFSD_A_EXPKEY_PATH + 1] = {
	[NFSD_A_EXPKEY_SEQNO] = { .type = NLA_U64, },
	[NFSD_A_EXPKEY_CLIENT] = { .type = NLA_NUL_STRING, },
	[NFSD_A_EXPKEY_FSIDTYPE] = { .type = NLA_U8, },
	[NFSD_A_EXPKEY_FSID] = { .type = NLA_BINARY, },
	[NFSD_A_EXPKEY_NEGATIVE] = { .type = NLA_FLAG, },
	[NFSD_A_EXPKEY_EXPIRY] = { .type = NLA_U64, },
	[NFSD_A_EXPKEY_PATH] = { .type = NLA_NUL_STRING, },
};

const struct nla_policy nfsd_fslocation_nl_policy[NFSD_A_FSLOCATION_PATH + 1] = {
	[NFSD_A_FSLOCATION_HOST] = { .type = NLA_NUL_STRING, },
	[NFSD_A_FSLOCATION_PATH] = { .type = NLA_NUL_STRING, },
};

const struct nla_policy nfsd_fslocations_nl_policy[NFSD_A_FSLOCATIONS_LOCATION + 1] = {
	[NFSD_A_FSLOCATIONS_LOCATION] = NLA_POLICY_NESTED(nfsd_fslocation_nl_policy),
};

const struct nla_policy nfsd_sock_nl_policy[NFSD_A_SOCK_TRANSPORT_NAME + 1] = {
	[NFSD_A_SOCK_ADDR] = { .type = NLA_BINARY, },
	[NFSD_A_SOCK_TRANSPORT_NAME] = { .type = NLA_NUL_STRING, },
};

const struct nla_policy nfsd_svc_export_nl_policy[NFSD_A_SVC_EXPORT_FSID + 1] = {
	[NFSD_A_SVC_EXPORT_SEQNO] = { .type = NLA_U64, },
	[NFSD_A_SVC_EXPORT_CLIENT] = { .type = NLA_NUL_STRING, },
	[NFSD_A_SVC_EXPORT_PATH] = { .type = NLA_NUL_STRING, },
	[NFSD_A_SVC_EXPORT_NEGATIVE] = { .type = NLA_FLAG, },
	[NFSD_A_SVC_EXPORT_EXPIRY] = { .type = NLA_U64, },
	[NFSD_A_SVC_EXPORT_ANON_UID] = { .type = NLA_U32, },
	[NFSD_A_SVC_EXPORT_ANON_GID] = { .type = NLA_U32, },
	[NFSD_A_SVC_EXPORT_FSLOCATIONS] = NLA_POLICY_NESTED(nfsd_fslocations_nl_policy),
	[NFSD_A_SVC_EXPORT_UUID] = { .type = NLA_BINARY, },
	[NFSD_A_SVC_EXPORT_SECINFO] = NLA_POLICY_NESTED(nfsd_auth_flavor_nl_policy),
	[NFSD_A_SVC_EXPORT_XPRTSEC] = NLA_POLICY_MASK(NLA_U32, 0x7),
	[NFSD_A_SVC_EXPORT_FLAGS] = NLA_POLICY_MASK(NLA_U32, 0x3ffff),
	[NFSD_A_SVC_EXPORT_FSID] = { .type = NLA_S32, },
};

const struct nla_policy nfsd_version_nl_policy[NFSD_A_VERSION_ENABLED + 1] = {
	[NFSD_A_VERSION_MAJOR] = { .type = NLA_U32, },
	[NFSD_A_VERSION_MINOR] = { .type = NLA_U32, },
	[NFSD_A_VERSION_ENABLED] = { .type = NLA_FLAG, },
};

/* NFSD_CMD_THREADS_SET - do */
static const struct nla_policy nfsd_threads_set_nl_policy[NFSD_A_SERVER_FH_KEY + 1] = {
	[NFSD_A_SERVER_THREADS] = { .type = NLA_U32, },
	[NFSD_A_SERVER_GRACETIME] = { .type = NLA_U32, },
	[NFSD_A_SERVER_LEASETIME] = { .type = NLA_U32, },
	[NFSD_A_SERVER_SCOPE] = { .type = NLA_NUL_STRING, },
	[NFSD_A_SERVER_MIN_THREADS] = { .type = NLA_U32, },
	[NFSD_A_SERVER_FH_KEY] = NLA_POLICY_EXACT_LEN(16),
};

/* NFSD_CMD_VERSION_SET - do */
static const struct nla_policy nfsd_version_set_nl_policy[NFSD_A_SERVER_PROTO_VERSION + 1] = {
	[NFSD_A_SERVER_PROTO_VERSION] = NLA_POLICY_NESTED(nfsd_version_nl_policy),
};

/* NFSD_CMD_LISTENER_SET - do */
static const struct nla_policy nfsd_listener_set_nl_policy[NFSD_A_SERVER_SOCK_ADDR + 1] = {
	[NFSD_A_SERVER_SOCK_ADDR] = NLA_POLICY_NESTED(nfsd_sock_nl_policy),
};

/* NFSD_CMD_POOL_MODE_SET - do */
static const struct nla_policy nfsd_pool_mode_set_nl_policy[NFSD_A_POOL_MODE_MODE + 1] = {
	[NFSD_A_POOL_MODE_MODE] = { .type = NLA_NUL_STRING, },
};

/* NFSD_CMD_SVC_EXPORT_SET_REQS - do */
static const struct nla_policy nfsd_svc_export_set_reqs_nl_policy[NFSD_A_SVC_EXPORT_REQS_REQUESTS + 1] = {
	[NFSD_A_SVC_EXPORT_REQS_REQUESTS] = NLA_POLICY_NESTED(nfsd_svc_export_nl_policy),
};

/* NFSD_CMD_EXPKEY_SET_REQS - do */
static const struct nla_policy nfsd_expkey_set_reqs_nl_policy[NFSD_A_EXPKEY_REQS_REQUESTS + 1] = {
	[NFSD_A_EXPKEY_REQS_REQUESTS] = NLA_POLICY_NESTED(nfsd_expkey_nl_policy),
};

/* NFSD_CMD_CACHE_FLUSH - do */
static const struct nla_policy nfsd_cache_flush_nl_policy[NFSD_A_CACHE_FLUSH_MASK + 1] = {
	[NFSD_A_CACHE_FLUSH_MASK] = NLA_POLICY_MASK(NLA_U32, 0x3),
};

/* NFSD_CMD_UNLOCK_IP - do */
static const struct nla_policy nfsd_unlock_ip_nl_policy[NFSD_A_UNLOCK_IP_ADDRESS + 1] = {
	[NFSD_A_UNLOCK_IP_ADDRESS] = NLA_POLICY_MIN_LEN(16),
};

/* NFSD_CMD_UNLOCK_FILESYSTEM - do */
static const struct nla_policy nfsd_unlock_filesystem_nl_policy[NFSD_A_UNLOCK_FILESYSTEM_PATH + 1] = {
	[NFSD_A_UNLOCK_FILESYSTEM_PATH] = { .type = NLA_NUL_STRING, },
};

/* NFSD_CMD_UNLOCK_EXPORT - do */
static const struct nla_policy nfsd_unlock_export_nl_policy[NFSD_A_UNLOCK_EXPORT_PATH + 1] = {
	[NFSD_A_UNLOCK_EXPORT_PATH] = { .type = NLA_NUL_STRING, },
};

/* Ops table for nfsd */
static const struct genl_split_ops nfsd_nl_ops[] = {
	{
		.cmd	= NFSD_CMD_RPC_STATUS_GET,
		.dumpit	= nfsd_nl_rpc_status_get_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NFSD_CMD_THREADS_SET,
		.doit		= nfsd_nl_threads_set_doit,
		.policy		= nfsd_threads_set_nl_policy,
		.maxattr	= NFSD_A_SERVER_FH_KEY,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_THREADS_GET,
		.doit	= nfsd_nl_threads_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_VERSION_SET,
		.doit		= nfsd_nl_version_set_doit,
		.policy		= nfsd_version_set_nl_policy,
		.maxattr	= NFSD_A_SERVER_PROTO_VERSION,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_VERSION_GET,
		.doit	= nfsd_nl_version_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_LISTENER_SET,
		.doit		= nfsd_nl_listener_set_doit,
		.policy		= nfsd_listener_set_nl_policy,
		.maxattr	= NFSD_A_SERVER_SOCK_ADDR,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_LISTENER_GET,
		.doit	= nfsd_nl_listener_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_POOL_MODE_SET,
		.doit		= nfsd_nl_pool_mode_set_doit,
		.policy		= nfsd_pool_mode_set_nl_policy,
		.maxattr	= NFSD_A_POOL_MODE_MODE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_POOL_MODE_GET,
		.doit	= nfsd_nl_pool_mode_get_doit,
		.flags	= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_SVC_EXPORT_GET_REQS,
		.dumpit	= nfsd_nl_svc_export_get_reqs_dumpit,
		.flags	= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NFSD_CMD_SVC_EXPORT_SET_REQS,
		.doit		= nfsd_nl_svc_export_set_reqs_doit,
		.policy		= nfsd_svc_export_set_reqs_nl_policy,
		.maxattr	= NFSD_A_SVC_EXPORT_REQS_REQUESTS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NFSD_CMD_EXPKEY_GET_REQS,
		.dumpit	= nfsd_nl_expkey_get_reqs_dumpit,
		.flags	= GENL_ADMIN_PERM | GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NFSD_CMD_EXPKEY_SET_REQS,
		.doit		= nfsd_nl_expkey_set_reqs_doit,
		.policy		= nfsd_expkey_set_reqs_nl_policy,
		.maxattr	= NFSD_A_EXPKEY_REQS_REQUESTS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_CACHE_FLUSH,
		.doit		= nfsd_nl_cache_flush_doit,
		.policy		= nfsd_cache_flush_nl_policy,
		.maxattr	= NFSD_A_CACHE_FLUSH_MASK,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_UNLOCK_IP,
		.doit		= nfsd_nl_unlock_ip_doit,
		.policy		= nfsd_unlock_ip_nl_policy,
		.maxattr	= NFSD_A_UNLOCK_IP_ADDRESS,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_UNLOCK_FILESYSTEM,
		.doit		= nfsd_nl_unlock_filesystem_doit,
		.policy		= nfsd_unlock_filesystem_nl_policy,
		.maxattr	= NFSD_A_UNLOCK_FILESYSTEM_PATH,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NFSD_CMD_UNLOCK_EXPORT,
		.doit		= nfsd_nl_unlock_export_doit,
		.policy		= nfsd_unlock_export_nl_policy,
		.maxattr	= NFSD_A_UNLOCK_EXPORT_PATH,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group nfsd_nl_mcgrps[] = {
	[NFSD_NLGRP_NONE] = { "none", },
	[NFSD_NLGRP_EXPORTD] = { "exportd", },
};

struct genl_family nfsd_nl_family __ro_after_init = {
	.name		= NFSD_FAMILY_NAME,
	.version	= NFSD_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= nfsd_nl_ops,
	.n_split_ops	= ARRAY_SIZE(nfsd_nl_ops),
	.mcgrps		= nfsd_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(nfsd_nl_mcgrps),
};
