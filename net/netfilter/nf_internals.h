/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_INTERNALS_H
#define _NF_INTERNALS_H

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

/* nf_conntrack_netlink.c: applied on tuple filters */
#define CTA_FILTER_F_CTA_IP_SRC			(1 << 0)
#define CTA_FILTER_F_CTA_IP_DST			(1 << 1)
#define CTA_FILTER_F_CTA_TUPLE_ZONE		(1 << 2)
#define CTA_FILTER_F_CTA_PROTO_NUM		(1 << 3)
#define CTA_FILTER_F_CTA_PROTO_SRC_PORT		(1 << 4)
#define CTA_FILTER_F_CTA_PROTO_DST_PORT		(1 << 5)
#define CTA_FILTER_F_CTA_PROTO_ICMP_TYPE	(1 << 6)
#define CTA_FILTER_F_CTA_PROTO_ICMP_CODE	(1 << 7)
#define CTA_FILTER_F_CTA_PROTO_ICMP_ID		(1 << 8)
#define CTA_FILTER_F_CTA_PROTO_ICMPV6_TYPE	(1 << 9)
#define CTA_FILTER_F_CTA_PROTO_ICMPV6_CODE	(1 << 10)
#define CTA_FILTER_F_CTA_PROTO_ICMPV6_ID	(1 << 11)
#define CTA_FILTER_F_MAX			(1 << 12)
#define CTA_FILTER_F_ALL			(CTA_FILTER_F_MAX-1)
#define CTA_FILTER_FLAG(ctattr) CTA_FILTER_F_ ## ctattr

/* nf_queue.c */
void nf_queue_nf_hook_drop(struct net *net);

/* nf_log.c */
int __init netfilter_log_init(void);

#ifdef CONFIG_LWTUNNEL
/* nf_hooks_lwtunnel.c */
int __init netfilter_lwtunnel_init(void);
void netfilter_lwtunnel_fini(void);
#endif

/* core.c */
void nf_hook_entries_delete_raw(struct nf_hook_entries __rcu **pp,
				const struct nf_hook_ops *reg);
int nf_hook_entries_insert_raw(struct nf_hook_entries __rcu **pp,
				const struct nf_hook_ops *reg);
#endif
