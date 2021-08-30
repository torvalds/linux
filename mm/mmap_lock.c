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
 * Our various events all share the same buffer (because we don't want or need
 * to allocate a set of buffers *per event type*), so we need to protect against
 * concurrent _reg() and _unreg() calls, and count how many _reg() calls have
 * been made.
 */
static DEFINE_MUTEX(reg_lock);
static int reg_refcount; /* Protected by reg_lock. */

/*
 * Size of the buffer for memcg path names. Ignoring stack trace support,
 * trace_events_hist.c uses MAX_FILTER_STR_VAL for this, so we also use it.
 */
#define MEMCG_PATH_BUF_SIZE MAX_FILTER_STR_VAL

/*
 * How many contexts our trace events might be called in: normal, softirq, irq,
 * and NMI.
 */
#define CONTEXT_COUNT 4

struct memcg_path {
	local_lock_t lock;
	char __rcu *buf;
	local_t buf_idx;
};
static DEFINE_PER_CPU(struct memcg_path, memcg_paths) = {
	.lock = INIT_LOCAL_LOCK(lock),
	.buf_idx = LOCAL_INIT(0),
};

static char **tmp_bufs;

/* Called with reg_lock held. */
static void free_memcg_path_bufs(void)
{
	struct memcg_path *memcg_path;
	int cpu;
	char **old = tmp_bufs;

	for_each_possible_cpu(cpu) {
		memcg_path = per_cpu_ptr(&memcg_paths, cpu);
		*(old++) = rcu_dereference_protected(memcg_path->buf,
			lockdep_is_held(&reg_lock));
		rcu_assign_pointer(memcg_path->buf, NULL);
	}

	/* Wait for inflight memcg_path_buf users to finish. */
	synchronize_rcu();

	old = tmp_bufs;
	for_each_possible_cpu(cpu) {
		kfree(*(old++));
	}

	kfree(tmp_bufs);
	tmp_bufs = NULL;
}

int trace_mmap_lock_reg(void)
{
	int cpu;
	char *new;

	mutex_lock(&reg_lock);

	/* If the refcount is going 0->1, proceed with allocating buffers. */
	if (reg_refcount++)
		goto out;

	tmp_bufs = kmalloc_array(num_possible_cpus(), sizeof(*tmp_bufs),
				 GFP_KERNEL);
	if (tmp_bufs == NULL)
		goto out_fail;

	for_each_possible_cpu(cpu) {
		new = kmalloc(MEMCG_PATH_BUF_SIZE * CONTEXT_COUNT, GFP_KERNEL);
		if (new == NULL)
			goto out_fail_free;
		rcu_assign_pointer(per_cpu_ptr(&memcg_paths, cpu)->buf, new);
		/* Don't need to wait for inflights, they'd have gotten NULL. */
	}

out:
	mutex_unlock(&reg_lock);
	return 0;

out_fail_free:
	free_memcg_path_bufs();
out_fail:
	/* Since we failed, undo the earlier ref increment. */
	--reg_refcount;

	mutex_unlock(&reg_lock);
	return -ENOMEM;
}

void trace_mmap_lock_unreg(void)
{
	mutex_lock(&reg_lock);

	/* If the refcount is going 1->0, proceed with freeing buffers. */
	if (--reg_refcount)
		goto out;

	free_memcg_path_bufs();

out:
	mutex_unlock(&reg_lock);
}

static inline char *get_memcg_path_buf(void)
{
	struct memcg_path *memcg_path = this_cpu_ptr(&memcg_paths);
	char *buf;
	int idx;

	rcu_read_lock();
	buf = rcu_dereference(memcg_path->buf);
	if (buf == NULL) {
		rcu_read_unlock();
		return NULL;
	}
	idx = local_add_return(MEMCG_PATH_BUF_SIZE, &memcg_path->buf_idx) -
	      MEMCG_PATH_BUF_SIZE;
	return &buf[idx];
}

static inline void put_memcg_path_buf(void)
{
	local_sub(MEMCG_PATH_BUF_SIZE, &this_cpu_ptr(&memcg_paths)->buf_idx);
	rcu_read_unlock();
}

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)                                   \
	do {                                                                   \
		const char *memcg_path;                                        \
		preempt_disable();                                             \
		memcg_path = get_mm_memcg_path(mm);                            \
		trace_mmap_lock_##type(mm,                                     \
				       memcg_path != NULL ? memcg_path : "",   \
				       ##__VA_ARGS__);                         \
		if (likely(memcg_path != NULL))                                \
			put_memcg_path_buf();                                  \
		preempt_enable();                                              \
	} while (0)

#else /* !CONFIG_MEMCG */

int trace_mmap_lock_reg(void)
{
	return 0;
}

void trace_mmap_lock_unreg(void)
{
}

#define TRACE_MMAP_LOCK_EVENT(type, mm, ...)                                   \
	trace_mmap_lock_##type(mm, "", ##__VA_ARGS__)

#endif /* CONFIG_MEMCG */

#ifdef CONFIG_TRACING
#ifdef CONFIG_MEMCG
/*
 * Write the given mm_struct's memcg path to a percpu buffer, and return a
 * pointer to it. If the path cannot be determined, or no buffer was available
 * (because the trace event is being unregistered), NULL is returned.
 *
 * Note: buffers are allocated per-cpu to avoid locking, so preemption must be
 * disabled by the caller before calling us, and re-enabled only after the
 * caller is done with the pointer.
 *
 * The caller must call put_memcg_path_buf() once the buffer is no longer
 * needed. This must be done while preemption is still disabled.
 */
static const char *get_mm_memcg_path(struct mm_struct *mm)
{
	char *buf = NULL;
	struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);

	if (memcg == NULL)
		goto out;
	if (unlikely(memcg->css.cgroup == NULL))
		goto out_put;

	buf = get_memcg_path_buf();
	if (buf == NULL)
		goto out_put;

	cgroup_path(memcg->css.cgroup, buf, MEMCG_PATH_BUF_SIZE);

out_put:
	css_put(&memcg->css);
out:
	return buf;
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
