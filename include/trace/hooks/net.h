/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM net
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_NET_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_NET_VH_H
#include <trace/hooks/vendor_hooks.h>

struct packet_type;
struct list_head;
DECLARE_HOOK(android_vh_ptype_head,
	TP_PROTO(const struct packet_type *pt, struct list_head *vendor_pt),
	TP_ARGS(pt, vendor_pt));

struct nf_conn;
struct sock;
struct net_device;
DECLARE_RESTRICTED_HOOK(android_rvh_nf_conn_alloc,
	TP_PROTO(struct nf_conn *nf_conn), TP_ARGS(nf_conn), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_nf_conn_free,
	TP_PROTO(struct nf_conn *nf_conn), TP_ARGS(nf_conn), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_sk_alloc,
	TP_PROTO(struct sock *sock), TP_ARGS(sock), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_sk_free,
	TP_PROTO(struct sock *sock), TP_ARGS(sock), 1);

struct poll_table_struct;
typedef struct poll_table_struct poll_table;
DECLARE_HOOK(android_vh_netlink_poll,
	TP_PROTO(struct file *file, struct socket *sock, poll_table *wait,
		__poll_t *mask),
	TP_ARGS(file, sock, wait, mask));
DECLARE_HOOK(android_vh_dc_send_copy,
	TP_PROTO(struct sk_buff *skb, struct net_device *dev), TP_ARGS(skb, dev));
DECLARE_HOOK(android_vh_dc_receive,
	TP_PROTO(struct sk_buff *skb, int *flag), TP_ARGS(skb, flag));
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_NET_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
