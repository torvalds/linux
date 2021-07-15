/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM net
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_NET_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_NET_VH_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct packet_type;
struct list_head;
struct sk_buff;
DECLARE_HOOK(android_vh_ptype_head,
	TP_PROTO(const struct packet_type *pt, struct list_head *vendor_pt),
	TP_ARGS(pt, vendor_pt));
DECLARE_HOOK(android_vh_kfree_skb,
	TP_PROTO(struct sk_buff *skb), TP_ARGS(skb));

struct nf_conn;
struct sock;
DECLARE_RESTRICTED_HOOK(android_rvh_nf_conn_alloc,
	TP_PROTO(struct nf_conn *nf_conn), TP_ARGS(nf_conn), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_nf_conn_free,
	TP_PROTO(struct nf_conn *nf_conn), TP_ARGS(nf_conn), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_sk_alloc,
	TP_PROTO(struct sock *sock), TP_ARGS(sock), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_sk_free,
	TP_PROTO(struct sock *sock), TP_ARGS(sock), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_NET_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
