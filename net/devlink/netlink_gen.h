/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_DEVLINK_GEN_H
#define _LINUX_DEVLINK_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/devlink.h>

/* Common nested types */
extern const struct nla_policy devlink_dl_port_function_nl_policy[DEVLINK_PORT_FN_ATTR_CAPS + 1];
extern const struct nla_policy devlink_dl_selftest_id_nl_policy[DEVLINK_ATTR_SELFTEST_ID_FLASH + 1];

/* Ops table for devlink */
extern const struct genl_split_ops devlink_nl_ops[73];

int devlink_nl_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
			struct genl_info *info);
int devlink_nl_pre_doit_port(const struct genl_split_ops *ops,
			     struct sk_buff *skb, struct genl_info *info);
int devlink_nl_pre_doit_port_optional(const struct genl_split_ops *ops,
				      struct sk_buff *skb,
				      struct genl_info *info);
void
devlink_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		     struct genl_info *info);

int devlink_nl_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int devlink_nl_port_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int devlink_nl_port_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_new_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_del_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_split_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_unsplit_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_sb_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_sb_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int devlink_nl_sb_pool_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_sb_pool_get_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb);
int devlink_nl_sb_pool_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_sb_port_pool_get_doit(struct sk_buff *skb,
				     struct genl_info *info);
int devlink_nl_sb_port_pool_get_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb);
int devlink_nl_sb_port_pool_set_doit(struct sk_buff *skb,
				     struct genl_info *info);
int devlink_nl_sb_tc_pool_bind_get_doit(struct sk_buff *skb,
					struct genl_info *info);
int devlink_nl_sb_tc_pool_bind_get_dumpit(struct sk_buff *skb,
					  struct netlink_callback *cb);
int devlink_nl_sb_tc_pool_bind_set_doit(struct sk_buff *skb,
					struct genl_info *info);
int devlink_nl_sb_occ_snapshot_doit(struct sk_buff *skb,
				    struct genl_info *info);
int devlink_nl_sb_occ_max_clear_doit(struct sk_buff *skb,
				     struct genl_info *info);
int devlink_nl_eswitch_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_eswitch_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_dpipe_table_get_doit(struct sk_buff *skb,
				    struct genl_info *info);
int devlink_nl_dpipe_entries_get_doit(struct sk_buff *skb,
				      struct genl_info *info);
int devlink_nl_dpipe_headers_get_doit(struct sk_buff *skb,
				      struct genl_info *info);
int devlink_nl_dpipe_table_counters_set_doit(struct sk_buff *skb,
					     struct genl_info *info);
int devlink_nl_resource_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_resource_dump_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_reload_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_param_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_param_get_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb);
int devlink_nl_param_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_region_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_region_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb);
int devlink_nl_region_new_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_region_del_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_region_read_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb);
int devlink_nl_port_param_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_port_param_get_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb);
int devlink_nl_port_param_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_info_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_info_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int devlink_nl_health_reporter_get_doit(struct sk_buff *skb,
					struct genl_info *info);
int devlink_nl_health_reporter_get_dumpit(struct sk_buff *skb,
					  struct netlink_callback *cb);
int devlink_nl_health_reporter_set_doit(struct sk_buff *skb,
					struct genl_info *info);
int devlink_nl_health_reporter_recover_doit(struct sk_buff *skb,
					    struct genl_info *info);
int devlink_nl_health_reporter_diagnose_doit(struct sk_buff *skb,
					     struct genl_info *info);
int devlink_nl_health_reporter_dump_get_dumpit(struct sk_buff *skb,
					       struct netlink_callback *cb);
int devlink_nl_health_reporter_dump_clear_doit(struct sk_buff *skb,
					       struct genl_info *info);
int devlink_nl_flash_update_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_trap_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_trap_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int devlink_nl_trap_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_trap_group_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_trap_group_get_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb);
int devlink_nl_trap_group_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_trap_policer_get_doit(struct sk_buff *skb,
				     struct genl_info *info);
int devlink_nl_trap_policer_get_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb);
int devlink_nl_trap_policer_set_doit(struct sk_buff *skb,
				     struct genl_info *info);
int devlink_nl_health_reporter_test_doit(struct sk_buff *skb,
					 struct genl_info *info);
int devlink_nl_rate_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_rate_get_dumpit(struct sk_buff *skb,
			       struct netlink_callback *cb);
int devlink_nl_rate_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_rate_new_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_rate_del_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_linecard_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_linecard_get_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb);
int devlink_nl_linecard_set_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_selftests_get_doit(struct sk_buff *skb, struct genl_info *info);
int devlink_nl_selftests_get_dumpit(struct sk_buff *skb,
				    struct netlink_callback *cb);
int devlink_nl_selftests_run_doit(struct sk_buff *skb, struct genl_info *info);

#endif /* _LINUX_DEVLINK_GEN_H */
