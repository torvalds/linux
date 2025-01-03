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

#ifdef CONFIG_MEMCG

/*
 * Size of the buffer for memcg path names. Ignoring stack trace support,
 * trace_events_hist.c uses MAX_FILTER_STR_VAL for this, so we also use it.
 */
#define MEMCG_PATH_BUF_SIZE MAX_FILTER_STR_VAL

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)				\
	do {								\
		if (trace_mmap_lock_##type##_enabled()) {		\
			char buf[MEMCG_PATH_BUF_SIZE];                  \
			get_mm_memcg_path(mm, buf, sizeof(buf));        \
			trace_mmap_lock_##type(mm, buf, ##__VA_ARGS__); \
		}							\
	} while (0)

#else /* !CONFIG_MEMCG */

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)                                   \
	trace_mmap_lock_##type(mm, "", ##__VA_ARGS__)

#endif /* CONFIG_MEMCG */

#ifdef CONFIG_TRACING
#ifdef CONFIG_MEMCG
/*
 * Write the given mm_struct's memcg path to a buffer. If the path cannot be
 * determined, empty string is written.
 */
static void get_mm_memcg_path(struct mm_struct *mm, char *buf, size_t buflen)
{
	struct mem_cgroup *memcg;

	buf[0] = '\0';
	memcg = get_mem_cgroup_from_mm(mm);
	if (memcg == NULL)
		return;
	if (memcg->css.cgroup)
		cgroup_path(memcg->css.cgroup, buf, buflen);
	css_put(&memcg->css);
}

#endif /* CONFIG_MEMCG */

/*
 * Trace calls must be in a separate file, as otherwise there's a circular
 * dependency between linux/mmap_lock.h and trace/events/mmap_lock.h.
 */

void __mmap_lock_do_trace_start_locking(struct mm_struct *mm, bool write)
{
	TRACE_MMAP_LOCK_EVENT(start_locking, mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_start_locking);

void __mmap_lock_do_trace_acquire_returned(struct mm_struct *mm, bool write,
					   bool success)
{
	TRACE_MMAP_LOCK_EVENT(acquire_returned, mm, write, success);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_acquire_returned);

void __mmap_lock_do_trace_released(struct mm_struct *mm, bool write)
{
	TRACE_MMAP_LOCK_EVENT(released, mm, write);
}
EXPORT_SYMBOL(__mmap_lock_do_trace_released);
#endif /* CONFIG_TRACING */
