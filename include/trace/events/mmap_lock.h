/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmap_lock

#if !defined(_TRACE_MMAP_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMAP_LOCK_H

#include <linux/tracepoint.h>
#include <linux/types.h>

struct mm_struct;

extern int trace_mmap_lock_reg(void);
extern void trace_mmap_lock_unreg(void);

TRACE_EVENT_FN(mmap_lock_start_locking,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, bool write),

	TP_ARGS(mm, memcg_path, write),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__string(memcg_path, memcg_path)
		__field(bool, write)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__assign_str(memcg_path, memcg_path);
		__entry->write = write;
	),

	TP_printk(
		"mm=%p memcg_path=%s write=%s",
		__entry->mm,
		__get_str(memcg_path),
		__entry->write ? "true" : "false"
	),

	trace_mmap_lock_reg, trace_mmap_lock_unreg
);

TRACE_EVENT_FN(mmap_lock_acquire_returned,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, bool write,
		bool success),

	TP_ARGS(mm, memcg_path, write, success),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__string(memcg_path, memcg_path)
		__field(bool, write)
		__field(bool, success)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__assign_str(memcg_path, memcg_path);
		__entry->write = write;
		__entry->success = success;
	),

	TP_printk(
		"mm=%p memcg_path=%s write=%s success=%s",
		__entry->mm,
		__get_str(memcg_path),
		__entry->write ? "true" : "false",
		__entry->success ? "true" : "false"
	),

	trace_mmap_lock_reg, trace_mmap_lock_unreg
);

TRACE_EVENT_FN(mmap_lock_released,

	TP_PROTO(struct mm_struct *mm, const char *memcg_path, bool write),

	TP_ARGS(mm, memcg_path, write),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__string(memcg_path, memcg_path)
		__field(bool, write)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__assign_str(memcg_path, memcg_path);
		__entry->write = write;
	),

	TP_printk(
		"mm=%p memcg_path=%s write=%s",
		__entry->mm,
		__get_str(memcg_path),
		__entry->write ? "true" : "false"
	),

	trace_mmap_lock_reg, trace_mmap_lock_unreg
);

#endif /* _TRACE_MMAP_LOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
