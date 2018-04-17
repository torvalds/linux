/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#undef TRACE_INCLUDE_PATH
#define TRACE_SYSTEM pagefault

#if !defined(_TRACE_PAGEFAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGEFAULT_H

#include <linux/tracepoint.h>
#include <linux/mm.h>

DECLARE_EVENT_CLASS(spf,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address),

	TP_STRUCT__entry(
		__field(unsigned long, caller)
		__field(unsigned long, vm_start)
		__field(unsigned long, vm_end)
		__field(unsigned long, address)
	),

	TP_fast_assign(
		__entry->caller		= caller;
		__entry->vm_start	= vma->vm_start;
		__entry->vm_end		= vma->vm_end;
		__entry->address	= address;
	),

	TP_printk("ip:%lx vma:%lx-%lx address:%lx",
		  __entry->caller, __entry->vm_start, __entry->vm_end,
		  __entry->address)
);

DEFINE_EVENT(spf, spf_pte_lock,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

DEFINE_EVENT(spf, spf_vma_changed,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

DEFINE_EVENT(spf, spf_vma_noanon,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

DEFINE_EVENT(spf, spf_vma_notsup,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

DEFINE_EVENT(spf, spf_vma_access,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

DEFINE_EVENT(spf, spf_pmd_changed,

	TP_PROTO(unsigned long caller,
		 struct vm_area_struct *vma, unsigned long address),

	TP_ARGS(caller, vma, address)
);

#endif /* _TRACE_PAGEFAULT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
