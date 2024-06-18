/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmap

#if !defined(_TRACE_MMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMAP_H

#include <linux/tracepoint.h>

TRACE_EVENT(vm_unmapped_area,

	TP_PROTO(unsigned long addr, struct vm_unmapped_area_info *info),

	TP_ARGS(addr, info),

	TP_STRUCT__entry(
		__field(unsigned long,	addr)
		__field(unsigned long,	total_vm)
		__field(unsigned long,	flags)
		__field(unsigned long,	length)
		__field(unsigned long,	low_limit)
		__field(unsigned long,	high_limit)
		__field(unsigned long,	align_mask)
		__field(unsigned long,	align_offset)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->total_vm = current->mm->total_vm;
		__entry->flags = info->flags;
		__entry->length = info->length;
		__entry->low_limit = info->low_limit;
		__entry->high_limit = info->high_limit;
		__entry->align_mask = info->align_mask;
		__entry->align_offset = info->align_offset;
	),

	TP_printk("addr=0x%lx err=%ld total_vm=0x%lx flags=0x%lx len=0x%lx lo=0x%lx hi=0x%lx mask=0x%lx ofs=0x%lx",
		IS_ERR_VALUE(__entry->addr) ? 0 : __entry->addr,
		IS_ERR_VALUE(__entry->addr) ? __entry->addr : 0,
		__entry->total_vm, __entry->flags, __entry->length,
		__entry->low_limit, __entry->high_limit, __entry->align_mask,
		__entry->align_offset)
);

TRACE_EVENT(vma_mas_szero,
	TP_PROTO(struct maple_tree *mt, unsigned long start,
		 unsigned long end),

	TP_ARGS(mt, start, end),

	TP_STRUCT__entry(
			__field(struct maple_tree *, mt)
			__field(unsigned long, start)
			__field(unsigned long, end)
	),

	TP_fast_assign(
			__entry->mt		= mt;
			__entry->start		= start;
			__entry->end		= end;
	),

	TP_printk("mt_mod %p, (NULL), SNULL, %lu, %lu,",
		  __entry->mt,
		  (unsigned long) __entry->start,
		  (unsigned long) __entry->end
	)
);

TRACE_EVENT(vma_store,
	TP_PROTO(struct maple_tree *mt, struct vm_area_struct *vma),

	TP_ARGS(mt, vma),

	TP_STRUCT__entry(
			__field(struct maple_tree *, mt)
			__field(struct vm_area_struct *, vma)
			__field(unsigned long, vm_start)
			__field(unsigned long, vm_end)
	),

	TP_fast_assign(
			__entry->mt		= mt;
			__entry->vma		= vma;
			__entry->vm_start	= vma->vm_start;
			__entry->vm_end		= vma->vm_end - 1;
	),

	TP_printk("mt_mod %p, (%p), STORE, %lu, %lu,",
		  __entry->mt, __entry->vma,
		  (unsigned long) __entry->vm_start,
		  (unsigned long) __entry->vm_end
	)
);


TRACE_EVENT(exit_mmap,
	TP_PROTO(struct mm_struct *mm),

	TP_ARGS(mm),

	TP_STRUCT__entry(
			__field(struct mm_struct *, mm)
			__field(struct maple_tree *, mt)
	),

	TP_fast_assign(
		       __entry->mm		= mm;
		       __entry->mt		= &mm->mm_mt;
	),

	TP_printk("mt_mod %p, DESTROY",
		  __entry->mt
	)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
