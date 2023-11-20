/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/mptcp.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_MPTCP_PM_GEN_H
#define _LINUX_MPTCP_PM_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/mptcp_pm.h>

/* Common nested types */
extern const struct nla_policy mptcp_pm_address_nl_policy[MPTCP_PM_ADDR_ATTR_IF_IDX + 1];

extern const struct nla_policy mptcp_pm_add_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1];

extern const struct nla_policy mptcp_pm_del_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1];

extern const struct nla_policy mptcp_pm_get_addr_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1];

extern const struct nla_policy mptcp_pm_flush_addrs_nl_policy[MPTCP_PM_ENDPOINT_ADDR + 1];

extern const struct nla_policy mptcp_pm_set_limits_nl_policy[MPTCP_PM_ATTR_SUBFLOWS + 1];

extern const struct nla_policy mptcp_pm_get_limits_nl_policy[MPTCP_PM_ATTR_SUBFLOWS + 1];

extern const struct nla_policy mptcp_pm_set_flags_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1];

extern const struct nla_policy mptcp_pm_announce_nl_policy[MPTCP_PM_ATTR_TOKEN + 1];

extern const struct nla_policy mptcp_pm_remove_nl_policy[MPTCP_PM_ATTR_LOC_ID + 1];

extern const struct nla_policy mptcp_pm_subflow_create_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1];

extern const struct nla_policy mptcp_pm_subflow_destroy_nl_policy[MPTCP_PM_ATTR_ADDR_REMOTE + 1];

/* Ops table for mptcp_pm */
extern const struct genl_ops mptcp_pm_nl_ops[11];

int mptcp_pm_nl_add_addr_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_del_addr_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_get_addr_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_get_addr_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb);
int mptcp_pm_nl_flush_addrs_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_set_limits_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_get_limits_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_set_flags_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_announce_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_remove_doit(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_subflow_create_doit(struct sk_buff *skb,
				    struct genl_info *info);
int mptcp_pm_nl_subflow_destroy_doit(struct sk_buff *skb,
				     struct genl_info *info);

#endif /* _LINUX_MPTCP_PM_GEN_H */
