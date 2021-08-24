/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ipv4
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IPV4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IPV4_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_tcp_sendmsg_locked,
	TP_PROTO(struct sock *sk, int size),
	TP_ARGS(sk, size), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_tcp_recvmsg,
	TP_PROTO(struct sock *sk),
	TP_ARGS(sk), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_udp_sendmsg,
	TP_PROTO(struct sock *sk),
	TP_ARGS(sk), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_udp_recvmsg,
	TP_PROTO(struct sock *sk),
	TP_ARGS(sk), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_tcp_recvmsg_stat,
	TP_PROTO(struct sock *sk, int size),
	TP_ARGS(sk, size), 1);

#endif /* _TRACE_HOOK_IPV4_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
