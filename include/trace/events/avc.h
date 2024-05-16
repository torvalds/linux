/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Authors:	Thiébaud Weksteen <tweek@google.com>
 *		Peter Enderborg <Peter.Enderborg@sony.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM avc

#if !defined(_TRACE_SELINUX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SELINUX_H

#include <linux/tracepoint.h>

TRACE_EVENT(selinux_audited,

	TP_PROTO(struct selinux_audit_data *sad,
		char *scontext,
		char *tcontext,
		const char *tclass
	),

	TP_ARGS(sad, scontext, tcontext, tclass),

	TP_STRUCT__entry(
		__field(u32, requested)
		__field(u32, denied)
		__field(u32, audited)
		__field(int, result)
		__string(scontext, scontext)
		__string(tcontext, tcontext)
		__string(tclass, tclass)
	),

	TP_fast_assign(
		__entry->requested	= sad->requested;
		__entry->denied		= sad->denied;
		__entry->audited	= sad->audited;
		__entry->result		= sad->result;
		__assign_str(tcontext);
		__assign_str(scontext);
		__assign_str(tclass);
	),

	TP_printk("requested=0x%x denied=0x%x audited=0x%x result=%d scontext=%s tcontext=%s tclass=%s",
		__entry->requested, __entry->denied, __entry->audited, __entry->result,
		__get_str(scontext), __get_str(tcontext), __get_str(tclass)
	)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
