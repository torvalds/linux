/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Thiébaud Weksteen <tweek@google.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM avc

#if !defined(_TRACE_SELINUX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SELINUX_H

#include <linux/tracepoint.h>

TRACE_EVENT(selinux_audited,

	TP_PROTO(struct selinux_audit_data *sad),

	TP_ARGS(sad),

	TP_STRUCT__entry(
		__field(unsigned int, tclass)
		__field(unsigned int, audited)
	),

	TP_fast_assign(
		__entry->tclass = sad->tclass;
		__entry->audited = sad->audited;
	),

	TP_printk("tclass=%u audited=%x",
		__entry->tclass,
		__entry->audited)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
