/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_NFSD_GEN_H
#define _LINUX_NFSD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/nfsd_netlink.h>

/* Common nested types */
extern const struct nla_policy nfsd_auth_flavor_nl_policy[NFSD_A_AUTH_FLAVOR_FLAGS + 1];
extern const struct nla_policy nfsd_expkey_nl_policy[NFSD_A_EXPKEY_PATH + 1];
extern const struct nla_policy nfsd_fslocation_nl_policy[NFSD_A_FSLOCATION_PATH + 1];
extern const struct nla_policy nfsd_fslocations_nl_policy[NFSD_A_FSLOCATIONS_LOCATION + 1];
extern const struct nla_policy nfsd_sock_nl_policy[NFSD_A_SOCK_TRANSPORT_NAME + 1];
extern const struct nla_policy nfsd_svc_export_nl_policy[NFSD_A_SVC_EXPORT_FSID + 1];
extern const struct nla_policy nfsd_version_nl_policy[NFSD_A_VERSION_ENABLED + 1];

int nfsd_nl_rpc_status_get_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb);
int nfsd_nl_threads_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_threads_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_version_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_version_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_listener_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_listener_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_pool_mode_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_pool_mode_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_svc_export_get_reqs_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb);
int nfsd_nl_svc_export_set_reqs_doit(struct sk_buff *skb,
				     struct genl_info *info);
int nfsd_nl_expkey_get_reqs_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb);
int nfsd_nl_expkey_set_reqs_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_cache_flush_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_unlock_ip_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_unlock_filesystem_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_unlock_export_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	NFSD_NLGRP_NONE,
	NFSD_NLGRP_EXPORTD,
};

extern struct genl_family nfsd_nl_family;

#endif /* _LINUX_NFSD_GEN_H */
