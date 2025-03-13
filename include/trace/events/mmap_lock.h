/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmap_lock

#if !defined(_TRACE_MMAP_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMAP_LOCK_H

#include <linux/memcontrol.h>
#include <linux/tracepoint.h>
#include <linux/types.h>

struct mm_struct;

DECLARE_EVENT_CLASS(mmap_lock,

	TP_PROTO(struct mm_struct *mm, bool write),

	TP_ARGS(mm, write),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(u64, memcg_id)
		__field(bool, write)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->memcg_id = cgroup_id_from_mm(mm);
		__entry->write = write;
	),

	TP_printk(
		"mm=%p memcg_id=%llu write=%s",
		__entry->mm, __entry->memcg_id,
		__entry->write ? "true" : "false"
	)
);

#define DEFINE_MMAP_LOCK_EVENT(name)                                    \
	DEFINE_EVENT(mmap_lock, name,                                   \
		TP_PROTO(struct mm_struct *mm, bool write),		\
		TP_ARGS(mm, write))

DEFINE_MMAP_LOCK_EVENT(mmap_lock_start_locking);
DEFINE_MMAP_LOCK_EVENT(mmap_lock_released);

TRACE_EVENT(mmap_lock_acquire_returned,

	TP_PROTO(struct mm_struct *mm, bool write, bool success),

	TP_ARGS(mm, write, success),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(u64, memcg_id)
		__field(bool, write)
		__field(bool, success)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->memcg_id = cgroup_id_from_mm(mm);
		__entry->write = write;
		__entry->success = success;
	),

	TP_printk(
		"mm=%p memcg_id=%llu write=%s success=%s",
		__entry->mm,
		__entry->memcg_id,
		__entry->write ? "true" : "false",
		__entry->success ? "true" : "false"
	)
);

#endif /* _TRACE_MMAP_LOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
