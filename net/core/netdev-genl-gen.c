// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "netdev-genl-gen.h"

#include <uapi/linux/netdev.h>
#include <linux/list.h>

/* Integer value ranges */
static const struct netlink_range_validation netdev_a_page_pool_id_range = {
	.min	= 1ULL,
	.max	= U32_MAX,
};

static const struct netlink_range_validation netdev_a_page_pool_ifindex_range = {
	.min	= 1ULL,
	.max	= S32_MAX,
};

static const struct netlink_range_validation netdev_a_napi_defer_hard_irqs_range = {
	.max	= S32_MAX,
};

/* Common nested types */
const struct nla_policy netdev_page_pool_info_nl_policy[NETDEV_A_PAGE_POOL_IFINDEX + 1] = {
	[NETDEV_A_PAGE_POOL_ID] = NLA_POLICY_FULL_RANGE(NLA_UINT, &netdev_a_page_pool_id_range),
	[NETDEV_A_PAGE_POOL_IFINDEX] = NLA_POLICY_FULL_RANGE(NLA_U32, &netdev_a_page_pool_ifindex_range),
};

const struct nla_policy netdev_queue_id_nl_policy[NETDEV_A_QUEUE_TYPE + 1] = {
	[NETDEV_A_QUEUE_ID] = { .type = NLA_U32, },
	[NETDEV_A_QUEUE_TYPE] = NLA_POLICY_MAX(NLA_U32, 1),
};

/* NETDEV_CMD_DEV_GET - do */
static const struct nla_policy netdev_dev_get_nl_policy[NETDEV_A_DEV_IFINDEX + 1] = {
	[NETDEV_A_DEV_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* NETDEV_CMD_PAGE_POOL_GET - do */
#ifdef CONFIG_PAGE_POOL
static const struct nla_policy netdev_page_pool_get_nl_policy[NETDEV_A_PAGE_POOL_ID + 1] = {
	[NETDEV_A_PAGE_POOL_ID] = NLA_POLICY_FULL_RANGE(NLA_UINT, &netdev_a_page_pool_id_range),
};
#endif /* CONFIG_PAGE_POOL */

/* NETDEV_CMD_PAGE_POOL_STATS_GET - do */
#ifdef CONFIG_PAGE_POOL_STATS
static const struct nla_policy netdev_page_pool_stats_get_nl_policy[NETDEV_A_PAGE_POOL_STATS_INFO + 1] = {
	[NETDEV_A_PAGE_POOL_STATS_INFO] = NLA_POLICY_NESTED(netdev_page_pool_info_nl_policy),
};
#endif /* CONFIG_PAGE_POOL_STATS */

/* NETDEV_CMD_QUEUE_GET - do */
static const struct nla_policy netdev_queue_get_do_nl_policy[NETDEV_A_QUEUE_TYPE + 1] = {
	[NETDEV_A_QUEUE_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[NETDEV_A_QUEUE_TYPE] = NLA_POLICY_MAX(NLA_U32, 1),
	[NETDEV_A_QUEUE_ID] = { .type = NLA_U32, },
};

/* NETDEV_CMD_QUEUE_GET - dump */
static const struct nla_policy netdev_queue_get_dump_nl_policy[NETDEV_A_QUEUE_IFINDEX + 1] = {
	[NETDEV_A_QUEUE_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* NETDEV_CMD_NAPI_GET - do */
static const struct nla_policy netdev_napi_get_do_nl_policy[NETDEV_A_NAPI_ID + 1] = {
	[NETDEV_A_NAPI_ID] = { .type = NLA_U32, },
};

/* NETDEV_CMD_NAPI_GET - dump */
static const struct nla_policy netdev_napi_get_dump_nl_policy[NETDEV_A_NAPI_IFINDEX + 1] = {
	[NETDEV_A_NAPI_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
};

/* NETDEV_CMD_QSTATS_GET - dump */
static const struct nla_policy netdev_qstats_get_nl_policy[NETDEV_A_QSTATS_SCOPE + 1] = {
	[NETDEV_A_QSTATS_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[NETDEV_A_QSTATS_SCOPE] = NLA_POLICY_MASK(NLA_UINT, 0x1),
};

/* NETDEV_CMD_BIND_RX - do */
static const struct nla_policy netdev_bind_rx_nl_policy[NETDEV_A_DMABUF_FD + 1] = {
	[NETDEV_A_DMABUF_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[NETDEV_A_DMABUF_FD] = { .type = NLA_U32, },
	[NETDEV_A_DMABUF_QUEUES] = NLA_POLICY_NESTED(netdev_queue_id_nl_policy),
};

/* NETDEV_CMD_NAPI_SET - do */
static const struct nla_policy netdev_napi_set_nl_policy[NETDEV_A_NAPI_IRQ_SUSPEND_TIMEOUT + 1] = {
	[NETDEV_A_NAPI_ID] = { .type = NLA_U32, },
	[NETDEV_A_NAPI_DEFER_HARD_IRQS] = NLA_POLICY_FULL_RANGE(NLA_U32, &netdev_a_napi_defer_hard_irqs_range),
	[NETDEV_A_NAPI_GRO_FLUSH_TIMEOUT] = { .type = NLA_UINT, },
	[NETDEV_A_NAPI_IRQ_SUSPEND_TIMEOUT] = { .type = NLA_UINT, },
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
#ifdef CONFIG_PAGE_POOL
	{
		.cmd		= NETDEV_CMD_PAGE_POOL_GET,
		.doit		= netdev_nl_page_pool_get_doit,
		.policy		= netdev_page_pool_get_nl_policy,
		.maxattr	= NETDEV_A_PAGE_POOL_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NETDEV_CMD_PAGE_POOL_GET,
		.dumpit	= netdev_nl_page_pool_get_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
#endif /* CONFIG_PAGE_POOL */
#ifdef CONFIG_PAGE_POOL_STATS
	{
		.cmd		= NETDEV_CMD_PAGE_POOL_STATS_GET,
		.doit		= netdev_nl_page_pool_stats_get_doit,
		.policy		= netdev_page_pool_stats_get_nl_policy,
		.maxattr	= NETDEV_A_PAGE_POOL_STATS_INFO,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd	= NETDEV_CMD_PAGE_POOL_STATS_GET,
		.dumpit	= netdev_nl_page_pool_stats_get_dumpit,
		.flags	= GENL_CMD_CAP_DUMP,
	},
#endif /* CONFIG_PAGE_POOL_STATS */
	{
		.cmd		= NETDEV_CMD_QUEUE_GET,
		.doit		= netdev_nl_queue_get_doit,
		.policy		= netdev_queue_get_do_nl_policy,
		.maxattr	= NETDEV_A_QUEUE_TYPE,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NETDEV_CMD_QUEUE_GET,
		.dumpit		= netdev_nl_queue_get_dumpit,
		.policy		= netdev_queue_get_dump_nl_policy,
		.maxattr	= NETDEV_A_QUEUE_IFINDEX,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NETDEV_CMD_NAPI_GET,
		.doit		= netdev_nl_napi_get_doit,
		.policy		= netdev_napi_get_do_nl_policy,
		.maxattr	= NETDEV_A_NAPI_ID,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NETDEV_CMD_NAPI_GET,
		.dumpit		= netdev_nl_napi_get_dumpit,
		.policy		= netdev_napi_get_dump_nl_policy,
		.maxattr	= NETDEV_A_NAPI_IFINDEX,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NETDEV_CMD_QSTATS_GET,
		.dumpit		= netdev_nl_qstats_get_dumpit,
		.policy		= netdev_qstats_get_nl_policy,
		.maxattr	= NETDEV_A_QSTATS_SCOPE,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NETDEV_CMD_BIND_RX,
		.doit		= netdev_nl_bind_rx_doit,
		.policy		= netdev_bind_rx_nl_policy,
		.maxattr	= NETDEV_A_DMABUF_FD,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NETDEV_CMD_NAPI_SET,
		.doit		= netdev_nl_napi_set_doit,
		.policy		= netdev_napi_set_nl_policy,
		.maxattr	= NETDEV_A_NAPI_IRQ_SUSPEND_TIMEOUT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
};

static const struct genl_multicast_group netdev_nl_mcgrps[] = {
	[NETDEV_NLGRP_MGMT] = { "mgmt", },
	[NETDEV_NLGRP_PAGE_POOL] = { "page-pool", },
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
	.sock_priv_size	= sizeof(struct list_head),
	.sock_priv_init	= (void *)netdev_nl_sock_priv_init,
	.sock_priv_destroy = (void *)netdev_nl_sock_priv_destroy,
};
