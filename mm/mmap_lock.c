// SPDX-License-Identifier: GPL-2.0
#define CREATE_TRACE_POINTS
#include <trace/events/mmap_lock.h>

#include <linux/mm.h>
#include <linux/cgroup.h>
#include <linux/memcontrol.h>
#include <linux/mmap_lock.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/trace_events.h>
#include <linux/local_lock.h>

EXPORT_TRACEPOINT_SYMBOL(mmap_lock_start_locking);
EXPORT_TRACEPOINT_SYMBOL(mmap_lock_acquire_returned);
EXPORT_TRACEPOINT_SYMBOL(mmap_lock_released);

#ifdef CONFIG_TRACING
/*
 * Trace calls must be in a separate file, as otherwise there's a circular
 * dependency between linux/mmap_lock.h and trace/events/mmap_lock.h.
 */

void __mmap_lock_do_trace_start_locking(struct mm_struct *mm, bool write)
{
	trace_mmap_lock_start_locking(mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_start_locking);

void __mmap_lock_do_trace_acquire_returned(struct mm_struct *mm, bool write,
					   bool success)
{
	trace_mmap_lock_acquire_returned(mm, write, success);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_acquire_returned);

void __mmap_lock_do_trace_released(struct mm_struct *mm, bool write)
{
	trace_mmap_lock_released(mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_released);
#endif /* CONFIG_TRACING */
