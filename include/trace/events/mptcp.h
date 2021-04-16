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

DECLARE_EVENT_CLASS(mptcp_dump_mpext,

	TP_PROTO(struct mptcp_ext *mpext),

	TP_ARGS(mpext),

	TP_STRUCT__entry(
		__field(u64, data_ack)
		__field(u64, data_seq)
		__field(u32, subflow_seq)
		__field(u16, data_len)
		__field(u8, use_map)
		__field(u8, dsn64)
		__field(u8, data_fin)
		__field(u8, use_ack)
		__field(u8, ack64)
		__field(u8, mpc_map)
		__field(u8, frozen)
		__field(u8, reset_transient)
		__field(u8, reset_reason)
	),

	TP_fast_assign(
		__entry->data_ack = mpext->ack64 ? mpext->data_ack : mpext->data_ack32;
		__entry->data_seq = mpext->data_seq;
		__entry->subflow_seq = mpext->subflow_seq;
		__entry->data_len = mpext->data_len;
		__entry->use_map = mpext->use_map;
		__entry->dsn64 = mpext->dsn64;
		__entry->data_fin = mpext->data_fin;
		__entry->use_ack = mpext->use_ack;
		__entry->ack64 = mpext->ack64;
		__entry->mpc_map = mpext->mpc_map;
		__entry->frozen = mpext->frozen;
		__entry->reset_transient = mpext->reset_transient;
		__entry->reset_reason = mpext->reset_reason;
	),

	TP_printk("data_ack=%llu data_seq=%llu subflow_seq=%u data_len=%u use_map=%u dsn64=%u data_fin=%u use_ack=%u ack64=%u mpc_map=%u frozen=%u reset_transient=%u reset_reason=%u",
		  __entry->data_ack, __entry->data_seq,
		  __entry->subflow_seq, __entry->data_len,
		  __entry->use_map, __entry->dsn64,
		  __entry->data_fin, __entry->use_ack,
		  __entry->ack64, __entry->mpc_map,
		  __entry->frozen, __entry->reset_transient,
		  __entry->reset_reason)
);

DEFINE_EVENT(mptcp_dump_mpext, get_mapping_status,
	TP_PROTO(struct mptcp_ext *mpext),
	TP_ARGS(mpext));

#endif /* _TRACE_MPTCP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
