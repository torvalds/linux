/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/em.yaml */
/* YNL-GEN kernel header */

#ifndef _LINUX_EM_GEN_H
#define _LINUX_EM_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/energy_model.h>

int em_nl_get_pds_doit(struct sk_buff *skb, struct genl_info *info);
int em_nl_get_pd_table_doit(struct sk_buff *skb, struct genl_info *info);

enum {
	EM_NLGRP_EVENT,
};

extern struct genl_family em_nl_family;

#endif /* _LINUX_EM_GEN_H */
