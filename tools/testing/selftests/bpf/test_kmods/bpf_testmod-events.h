/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf_testmod

#if !defined(_BPF_TESTMOD_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _BPF_TESTMOD_EVENTS_H

#include <linux/tracepoint.h>
#include "bpf_testmod.h"

TRACE_EVENT(bpf_testmod_test_read,
	TP_PROTO(struct task_struct *task, struct bpf_testmod_test_read_ctx *ctx),
	TP_ARGS(task, ctx),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__field(loff_t, off)
		__field(size_t, len)
	),
	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->off = ctx->off;
		__entry->len = ctx->len;
	),
	TP_printk("pid=%d comm=%s off=%llu len=%zu",
		  __entry->pid, __entry->comm, __entry->off, __entry->len)
);

/* A bare tracepoint with no event associated with it */
DECLARE_TRACE(bpf_testmod_test_write_bare,
	TP_PROTO(struct task_struct *task, struct bpf_testmod_test_write_ctx *ctx),
	TP_ARGS(task, ctx)
);

/* Used in bpf_testmod_test_read() to test __nullable suffix */
DECLARE_TRACE(bpf_testmod_test_nullable_bare,
	TP_PROTO(struct bpf_testmod_test_read_ctx *ctx__nullable),
	TP_ARGS(ctx__nullable)
);

struct sk_buff;

DECLARE_TRACE(bpf_testmod_test_raw_tp_null,
	TP_PROTO(struct sk_buff *skb),
	TP_ARGS(skb)
);


#undef BPF_TESTMOD_DECLARE_TRACE
#ifdef DECLARE_TRACE_WRITABLE
#define BPF_TESTMOD_DECLARE_TRACE(call, proto, args, size) \
	DECLARE_TRACE_WRITABLE(call, PARAMS(proto), PARAMS(args), size)
#else
#define BPF_TESTMOD_DECLARE_TRACE(call, proto, args, size) \
	DECLARE_TRACE(call, PARAMS(proto), PARAMS(args))
#endif

BPF_TESTMOD_DECLARE_TRACE(bpf_testmod_test_writable_bare,
	TP_PROTO(struct bpf_testmod_test_writable_ctx *ctx),
	TP_ARGS(ctx),
	sizeof(struct bpf_testmod_test_writable_ctx)
);

#endif /* _BPF_TESTMOD_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE bpf_testmod-events
#include <trace/define_trace.h>
