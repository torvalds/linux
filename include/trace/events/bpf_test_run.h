/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf_test_run

#if !defined(_TRACE_BPF_TEST_RUN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BPF_TEST_RUN_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(bpf_test_finish,

	TP_PROTO(int *err),

	TP_ARGS(err),

	TP_STRUCT__entry(
		__field(int, err)
	),

	TP_fast_assign(
		__entry->err = *err;
	),

	TP_printk("bpf_test_finish with err=%d", __entry->err)
);

#ifdef DEFINE_EVENT_WRITABLE
#undef BPF_TEST_RUN_DEFINE_EVENT
#define BPF_TEST_RUN_DEFINE_EVENT(template, call, proto, args, size)	\
	DEFINE_EVENT_WRITABLE(template, call, PARAMS(proto),		\
			      PARAMS(args), size)
#else
#undef BPF_TEST_RUN_DEFINE_EVENT
#define BPF_TEST_RUN_DEFINE_EVENT(template, call, proto, args, size)	\
	DEFINE_EVENT(template, call, PARAMS(proto), PARAMS(args))
#endif

BPF_TEST_RUN_DEFINE_EVENT(bpf_test_finish, bpf_test_finish,

	TP_PROTO(int *err),

	TP_ARGS(err),

	sizeof(int)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
