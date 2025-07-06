/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/lockd.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_LOCKD_GEN_H
#define _LINUX_LOCKD_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/lockd_netlink.h>

int lockd_nl_server_set_doit(struct sk_buff *skb, struct genl_info *info);
int lockd_nl_server_get_doit(struct sk_buff *skb, struct genl_info *info);

extern struct genl_family lockd_nl_family;

#endif /* _LINUX_LOCKD_GEN_H */
