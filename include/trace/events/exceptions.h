/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM exceptions

#if !defined(_TRACE_PAGE_FAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGE_FAULT_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(exceptions,

	TP_PROTO(unsigned long address, struct pt_regs *regs,
		 unsigned long error_code),

	TP_ARGS(address, regs, error_code),

	TP_STRUCT__entry(
		__field(		unsigned long, address	)
		__field(		unsigned long, ip	)
		__field(		unsigned long, error_code )
	),

	TP_fast_assign(
		__entry->address = address;
		__entry->ip = instruction_pointer(regs);
		__entry->error_code = error_code;
	),

	TP_printk("address=%ps ip=%ps error_code=0x%lx",
		  (void *)__entry->address, (void *)__entry->ip,
		  __entry->error_code) );

DEFINE_EVENT(exceptions, page_fault_user,
	TP_PROTO(unsigned long address,	struct pt_regs *regs, unsigned long error_code),
	TP_ARGS(address, regs, error_code));
DEFINE_EVENT(exceptions, page_fault_kernel,
	TP_PROTO(unsigned long address,	struct pt_regs *regs, unsigned long error_code),
	TP_ARGS(address, regs, error_code));

#endif /*  _TRACE_PAGE_FAULT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
