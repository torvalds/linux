/* SPDX-License-Identifier: GPL-2.0+ */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rseq

#if !defined(_TRACE_RSEQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RSEQ_H

#include <linux/tracepoint.h>
#include <linux/types.h>

TRACE_EVENT(rseq_update,

	TP_PROTO(struct task_struct *t),

	TP_ARGS(t),

	TP_STRUCT__entry(
		__field(s32, cpu_id)
	),

	TP_fast_assign(
		__entry->cpu_id = raw_smp_processor_id();
	),

	TP_printk("cpu_id=%d", __entry->cpu_id)
);

TRACE_EVENT(rseq_ip_fixup,

	TP_PROTO(unsigned long regs_ip, unsigned long start_ip,
		unsigned long post_commit_offset, unsigned long abort_ip),

	TP_ARGS(regs_ip, start_ip, post_commit_offset, abort_ip),

	TP_STRUCT__entry(
		__field(unsigned long, regs_ip)
		__field(unsigned long, start_ip)
		__field(unsigned long, post_commit_offset)
		__field(unsigned long, abort_ip)
	),

	TP_fast_assign(
		__entry->regs_ip = regs_ip;
		__entry->start_ip = start_ip;
		__entry->post_commit_offset = post_commit_offset;
		__entry->abort_ip = abort_ip;
	),

	TP_printk("regs_ip=0x%lx start_ip=0x%lx post_commit_offset=%lu abort_ip=0x%lx",
		__entry->regs_ip, __entry->start_ip,
		__entry->post_commit_offset, __entry->abort_ip)
);

#endif /* _TRACE_SOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
