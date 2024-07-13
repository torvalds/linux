/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_NFSD_GEN_H
#define _LINUX_NFSD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/nfsd_netlink.h>

/* Common nested types */
extern const struct nla_policy nfsd_sock_nl_policy[NFSD_A_SOCK_TRANSPORT_NAME + 1];
extern const struct nla_policy nfsd_version_nl_policy[NFSD_A_VERSION_ENABLED + 1];

int nfsd_nl_rpc_status_get_dumpit(struct sk_buff *skb,
				  struct netlink_callback *cb);
int nfsd_nl_threads_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_threads_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_version_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_version_get_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_listener_set_doit(struct sk_buff *skb, struct genl_info *info);
int nfsd_nl_listener_get_doit(struct sk_buff *skb, struct genl_info *info);

extern struct genl_family nfsd_nl_family;

#endif /* _LINUX_NFSD_GEN_H */
