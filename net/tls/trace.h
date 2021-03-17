/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tls

#if !defined(_TLS_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TLS_TRACE_H_

#include <asm/unaligned.h>
#include <linux/tracepoint.h>

struct sock;

TRACE_EVENT(tls_device_offload_set,

	TP_PROTO(struct sock *sk, int dir, u32 tcp_seq, u8 *rec_no, int ret),

	TP_ARGS(sk, dir, tcp_seq, rec_no, ret),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u64,		rec_no		)
		__field(	int,		dir		)
		__field(	u32,		tcp_seq		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->rec_no = get_unaligned_be64(rec_no);
		__entry->dir = dir;
		__entry->tcp_seq = tcp_seq;
		__entry->ret = ret;
	),

	TP_printk(
		"sk=%p direction=%d tcp_seq=%u rec_no=%llu ret=%d",
		__entry->sk, __entry->dir, __entry->tcp_seq, __entry->rec_no,
		__entry->ret
	)
);

TRACE_EVENT(tls_device_decrypted,

	TP_PROTO(struct sock *sk, u32 tcp_seq, u8 *rec_no, u32 rec_len,
		 bool encrypted, bool decrypted),

	TP_ARGS(sk, tcp_seq, rec_no, rec_len, encrypted, decrypted),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u64,		rec_no		)
		__field(	u32,		tcp_seq		)
		__field(	u32,		rec_len		)
		__field(	bool,		encrypted	)
		__field(	bool,		decrypted	)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->rec_no = get_unaligned_be64(rec_no);
		__entry->tcp_seq = tcp_seq;
		__entry->rec_len = rec_len;
		__entry->encrypted = encrypted;
		__entry->decrypted = decrypted;
	),

	TP_printk(
		"sk=%p tcp_seq=%u rec_no=%llu len=%u encrypted=%d decrypted=%d",
		__entry->sk, __entry->tcp_seq,
		__entry->rec_no, __entry->rec_len,
		__entry->encrypted, __entry->decrypted
	)
);

TRACE_EVENT(tls_device_rx_resync_send,

	TP_PROTO(struct sock *sk, u32 tcp_seq, u8 *rec_no, int sync_type),

	TP_ARGS(sk, tcp_seq, rec_no, sync_type),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u64,		rec_no		)
		__field(	u32,		tcp_seq		)
		__field(	int,		sync_type	)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->rec_no = get_unaligned_be64(rec_no);
		__entry->tcp_seq = tcp_seq;
		__entry->sync_type = sync_type;
	),

	TP_printk(
		"sk=%p tcp_seq=%u rec_no=%llu sync_type=%d",
		__entry->sk, __entry->tcp_seq, __entry->rec_no,
		__entry->sync_type
	)
);

TRACE_EVENT(tls_device_rx_resync_nh_schedule,

	TP_PROTO(struct sock *sk),

	TP_ARGS(sk),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
	),

	TP_fast_assign(
		__entry->sk = sk;
	),

	TP_printk(
		"sk=%p", __entry->sk
	)
);

TRACE_EVENT(tls_device_rx_resync_nh_delay,

	TP_PROTO(struct sock *sk, u32 sock_data, u32 rec_len),

	TP_ARGS(sk, sock_data, rec_len),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u32,		sock_data	)
		__field(	u32,		rec_len		)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->sock_data = sock_data;
		__entry->rec_len = rec_len;
	),

	TP_printk(
		"sk=%p sock_data=%u rec_len=%u",
		__entry->sk, __entry->sock_data, __entry->rec_len
	)
);

TRACE_EVENT(tls_device_tx_resync_req,

	TP_PROTO(struct sock *sk, u32 tcp_seq, u32 exp_tcp_seq),

	TP_ARGS(sk, tcp_seq, exp_tcp_seq),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u32,		tcp_seq		)
		__field(	u32,		exp_tcp_seq	)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->tcp_seq = tcp_seq;
		__entry->exp_tcp_seq = exp_tcp_seq;
	),

	TP_printk(
		"sk=%p tcp_seq=%u exp_tcp_seq=%u",
		__entry->sk, __entry->tcp_seq, __entry->exp_tcp_seq
	)
);

TRACE_EVENT(tls_device_tx_resync_send,

	TP_PROTO(struct sock *sk, u32 tcp_seq, u8 *rec_no),

	TP_ARGS(sk, tcp_seq, rec_no),

	TP_STRUCT__entry(
		__field(	struct sock *,	sk		)
		__field(	u64,		rec_no		)
		__field(	u32,		tcp_seq		)
	),

	TP_fast_assign(
		__entry->sk = sk;
		__entry->rec_no = get_unaligned_be64(rec_no);
		__entry->tcp_seq = tcp_seq;
	),

	TP_printk(
		"sk=%p tcp_seq=%u rec_no=%llu",
		__entry->sk, __entry->tcp_seq, __entry->rec_no
	)
);

#endif /* _TLS_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
