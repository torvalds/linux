/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mptcp

#if !defined(_TRACE_MPTCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MPTCP_H

#include <linux/tracepoint.h>

TRACE_EVENT(mptcp_subflow_get_send,

	TP_PROTO(struct mptcp_subflow_context *subflow),

	TP_ARGS(subflow),

	TP_STRUCT__entry(
		__field(bool, active)
		__field(bool, free)
		__field(u32, snd_wnd)
		__field(u32, pace)
		__field(u8, backup)
		__field(u64, ratio)
	),

	TP_fast_assign(
		struct sock *ssk;

		__entry->active = mptcp_subflow_active(subflow);
		__entry->backup = subflow->backup;

		if (subflow->tcp_sock && sk_fullsock(subflow->tcp_sock))
			__entry->free = sk_stream_memory_free(subflow->tcp_sock);
		else
			__entry->free = 0;

		ssk = mptcp_subflow_tcp_sock(subflow);
		if (ssk && sk_fullsock(ssk)) {
			__entry->snd_wnd = tcp_sk(ssk)->snd_wnd;
			__entry->pace = ssk->sk_pacing_rate;
		} else {
			__entry->snd_wnd = 0;
			__entry->pace = 0;
		}

		if (ssk && sk_fullsock(ssk) && __entry->pace)
			__entry->ratio = div_u64((u64)ssk->sk_wmem_queued << 32, __entry->pace);
		else
			__entry->ratio = 0;
	),

	TP_printk("active=%d free=%d snd_wnd=%u pace=%u backup=%u ratio=%llu",
		  __entry->active, __entry->free,
		  __entry->snd_wnd, __entry->pace,
		  __entry->backup, __entry->ratio)
);

#endif /* _TRACE_MPTCP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
