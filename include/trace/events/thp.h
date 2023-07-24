/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thp

#if !defined(_TRACE_THP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THP_H

#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hugepage_set,

	    TP_PROTO(unsigned long addr, unsigned long pte),
	    TP_ARGS(addr, pte),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pte)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pte = pte;
		    ),

	    TP_printk("Set page table entry with 0x%lx with 0x%lx", __entry->addr, __entry->pte)
);

DEFINE_EVENT(hugepage_set, hugepage_set_pmd,
	    TP_PROTO(unsigned long addr, unsigned long pmd),
	    TP_ARGS(addr, pmd)
);

DECLARE_EVENT_CLASS(hugepage_update,

	    TP_PROTO(unsigned long addr, unsigned long pte, unsigned long clr, unsigned long set),
	    TP_ARGS(addr, pte, clr, set),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pte)
		    __field(unsigned long, clr)
		    __field(unsigned long, set)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pte = pte;
		    __entry->clr = clr;
		    __entry->set = set;

		    ),

	    TP_printk("hugepage update at addr 0x%lx and pte = 0x%lx clr = 0x%lx, set = 0x%lx", __entry->addr, __entry->pte, __entry->clr, __entry->set)
);

DEFINE_EVENT(hugepage_update, hugepage_update_pmd,
	    TP_PROTO(unsigned long addr, unsigned long pmd, unsigned long clr, unsigned long set),
	    TP_ARGS(addr, pmd, clr, set)
);

DECLARE_EVENT_CLASS(migration_pmd,

		TP_PROTO(unsigned long addr, unsigned long pmd),

		TP_ARGS(addr, pmd),

		TP_STRUCT__entry(
			__field(unsigned long, addr)
			__field(unsigned long, pmd)
		),

		TP_fast_assign(
			__entry->addr = addr;
			__entry->pmd = pmd;
		),
		TP_printk("addr=%lx, pmd=%lx", __entry->addr, __entry->pmd)
);

DEFINE_EVENT(migration_pmd, set_migration_pmd,
	TP_PROTO(unsigned long addr, unsigned long pmd),
	TP_ARGS(addr, pmd)
);

DEFINE_EVENT(migration_pmd, remove_migration_pmd,
	TP_PROTO(unsigned long addr, unsigned long pmd),
	TP_ARGS(addr, pmd)
);
#endif /* _TRACE_THP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
