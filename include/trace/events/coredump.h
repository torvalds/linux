/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM coredump

#if !defined(_TRACE_COREDUMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COREDUMP_H

#include <linux/sched.h>
#include <linux/tracepoint.h>

/**
 * coredump - called when a coredump starts
 * @sig: signal number that triggered the coredump
 *
 * This tracepoint fires at the beginning of a coredump attempt,
 * providing a stable interface for monitoring coredump events.
 */
TRACE_EVENT(coredump,

	TP_PROTO(int sig),

	TP_ARGS(sig),

	TP_STRUCT__entry(
		__field(int, sig)
		__array(char, comm, TASK_COMM_LEN)
	),

	TP_fast_assign(
		__entry->sig = sig;
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
	),

	TP_printk("sig=%d comm=%s",
		  __entry->sig, __entry->comm)
);

#endif /* _TRACE_COREDUMP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
