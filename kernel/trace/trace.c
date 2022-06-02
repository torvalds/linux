// SPDX-License-Identifier: GPL-2.0
/*
 * ring buffer based function tracer
 *
 * Copyright (C) 2007-2012 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Originally taken from the RT patch by:
 *    Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 Nadia Yvette Chambers
 */
#include <linux/ring_buffer.h>
#include <generated/utsrelease.h>
#include <linux/stacktrace.h>
#include <linux/writeback.h>
#include <linux/kallsyms.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/irqflags.h>
#include <linux/debugfs.h>
#include <linux/tracefs.h>
#include <linux/pagemap.h>
#include <linux/hardirq.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/splice.h>
#include <linux/kdebug.h>
#include <linux/string.h>
#include <linux/mount.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/panic_notifier.h>
#include <linux/poll.h>
#include <linux/nmi.h>
#include <linux/fs.h>
#include <linux/trace.h>
#include <linux/sched/clock.h>
#include <linux/sched/rt.h>
#include <linux/fsnotify.h>
#include <linux/irq_work.h>
#include <linux/workqueue.h>

#include "trace.h"
#include "trace_output.h"

/*
 * On boot up, the ring buffer is set to the minimum size, so that
 * we do not waste memory on systems that are not using tracing.
 */
bool ring_buffer_expanded;

/*
 * We need to change this state when a selftest is running.
 * A selftest will lurk into the ring-buffer to count the
 * entries inserted during the selftest although some concurrent
 * insertions into the ring-buffer such as trace_printk could occurred
 * at the same time, giving false positive or negative results.
 */
static bool __read_mostly tracing_selftest_running;

/*
 * If boot-time tracing including tracers/events via kernel cmdline
 * is running, we do not want to run SELFTEST.
 */
bool __read_mostly tracing_selftest_disabled;

#ifdef CONFIG_FTRACE_STARTUP_TEST
void __init disable_tracing_selftest(const char *reason)
{
	if (!tracing_selftest_disabled) {
		tracing_selftest_disabled = true;
		pr_info("Ftrace startup test is disabled due to %s\n", reason);
	}
}
#endif

/* Pipe tracepoints to printk */
struct trace_iterator *tracepoint_print_iter;
int tracepoint_printk;
static bool tracepoint_printk_stop_on_boot __initdata;
static DEFINE_STATIC_KEY_FALSE(tracepoint_printk_key);

/* For tracers that don't implement custom flags */
static struct tracer_opt dummy_tracer_opt[] = {
	{ }
};

static int
dummy_set_flag(struct trace_array *tr, u32 old_flags, u32 bit, int set)
{
	return 0;
}

/*
 * To prevent the comm cache from being overwritten when no
 * tracing is active, only save the comm when a trace event
 * occurred.
 */
static DEFINE_PER_CPU(bool, trace_taskinfo_save);

/*
 * Kill all tracing for good (never come back).
 * It is initialized to 1 but will turn to zero if the initialization
 * of the tracer is successful. But that is the only place that sets
 * this back to zero.
 */
static int tracing_disabled = 1;

cpumask_var_t __read_mostly	tracing_buffer_mask;

/*
 * ftrace_dump_on_oops - variable to dump ftrace buffer on oops
 *
 * If there is an oops (or kernel panic) and the ftrace_dump_on_oops
 * is set, then ftrace_dump is called. This will output the contents
 * of the ftrace buffers to the console.  This is very useful for
 * capturing traces that lead to crashes and outputing it to a
 * serial console.
 *
 * It is default off, but you can enable it with either specifying
 * "ftrace_dump_on_oops" in the kernel command line, or setting
 * /proc/sys/kernel/ftrace_dump_on_oops
 * Set 1 if you want to dump buffers of all CPUs
 * Set 2 if you want to dump the buffer of the CPU that triggered oops
 */

enum ftrace_dump_mode ftrace_dump_on_oops;

/* When set, tracing will stop when a WARN*() is hit */
int __disable_trace_on_warning;

#ifdef CONFIG_TRACE_EVAL_MAP_FILE
/* Map of enums to their values, for "eval_map" file */
struct trace_eval_map_head {
	struct module			*mod;
	unsigned long			length;
};

union trace_eval_map_item;

struct trace_eval_map_tail {
	/*
	 * "end" is first and points to NULL as it must be different
	 * than "mod" or "eval_string"
	 */
	union trace_eval_map_item	*next;
	const char			*end;	/* points to NULL */
};

static DEFINE_MUTEX(trace_eval_mutex);

/*
 * The trace_eval_maps are saved in an array with two extra elements,
 * one at the beginning, and one at the end. The beginning item contains
 * the count of the saved maps (head.length), and the module they
 * belong to if not built in (head.mod). The ending item contains a
 * pointer to the next array of saved eval_map items.
 */
union trace_eval_map_item {
	struct trace_eval_map		map;
	struct trace_eval_map_head	head;
	struct trace_eval_map_tail	tail;
};

static union trace_eval_map_item *trace_eval_maps;
#endif /* CONFIG_TRACE_EVAL_MAP_FILE */

int tracing_set_tracer(struct trace_array *tr, const char *buf);
static void ftrace_trace_userstack(struct trace_array *tr,
				   struct trace_buffer *buffer,
				   unsigned int trace_ctx);

#define MAX_TRACER_SIZE		100
static char bootup_tracer_buf[MAX_TRACER_SIZE] __initdata;
static char *default_bootup_tracer;

static bool allocate_snapshot;

static int __init set_cmdline_ftrace(char *str)
{
	strlcpy(bootup_tracer_buf, str, MAX_TRACER_SIZE);
	default_bootup_tracer = bootup_tracer_buf;
	/* We are using ftrace early, expand it */
	ring_buffer_expanded = true;
	return 1;
}
__setup("ftrace=", set_cmdline_ftrace);

static int __init set_ftrace_dump_on_oops(char *str)
{
	if (*str++ != '=' || !*str || !strcmp("1", str)) {
		ftrace_dump_on_oops = DUMP_ALL;
		return 1;
	}

	if (!strcmp("orig_cpu", str) || !strcmp("2", str)) {
		ftrace_dump_on_oops = DUMP_ORIG;
                return 1;
        }

        return 0;
}
__setup("ftrace_dump_on_oops", set_ftrace_dump_on_oops);

static int __init stop_trace_on_warning(char *str)
{
	if ((strcmp(str, "=0") != 0 && strcmp(str, "=off") != 0))
		__disable_trace_on_warning = 1;
	return 1;
}
__setup("traceoff_on_warning", stop_trace_on_warning);

static int __init boot_alloc_snapshot(char *str)
{
	allocate_snapshot = true;
	/* We also need the main ring buffer expanded */
	ring_buffer_expanded = true;
	return 1;
}
__setup("alloc_snapshot", boot_alloc_snapshot);


static char trace_boot_options_buf[MAX_TRACER_SIZE] __initdata;

static int __init set_trace_boot_options(char *str)
{
	strlcpy(trace_boot_options_buf, str, MAX_TRACER_SIZE);
	return 1;
}
__setup("trace_options=", set_trace_boot_options);

static char trace_boot_clock_buf[MAX_TRACER_SIZE] __initdata;
static char *trace_boot_clock __initdata;

static int __init set_trace_boot_clock(char *str)
{
	strlcpy(trace_boot_clock_buf, str, MAX_TRACER_SIZE);
	trace_boot_clock = trace_boot_clock_buf;
	return 1;
}
__setup("trace_clock=", set_trace_boot_clock);

static int __init set_tracepoint_printk(char *str)
{
	/* Ignore the "tp_printk_stop_on_boot" param */
	if (*str == '_')
		return 0;

	if ((strcmp(str, "=0") != 0 && strcmp(str, "=off") != 0))
		tracepoint_printk = 1;
	return 1;
}
__setup("tp_printk", set_tracepoint_printk);

static int __init set_tracepoint_printk_stop(char *str)
{
	tracepoint_printk_stop_on_boot = true;
	return 1;
}
__setup("tp_printk_stop_on_boot", set_tracepoint_printk_stop);

unsigned long long ns2usecs(u64 nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

static void
trace_process_export(struct trace_export *export,
	       struct ring_buffer_event *event, int flag)
{
	struct trace_entry *entry;
	unsigned int size = 0;

	if (export->flags & flag) {
		entry = ring_buffer_event_data(event);
		size = ring_buffer_event_length(event);
		export->write(export, entry, size);
	}
}

static DEFINE_MUTEX(ftrace_export_lock);

static struct trace_export __rcu *ftrace_exports_list __read_mostly;

static DEFINE_STATIC_KEY_FALSE(trace_function_exports_enabled);
static DEFINE_STATIC_KEY_FALSE(trace_event_exports_enabled);
static DEFINE_STATIC_KEY_FALSE(trace_marker_exports_enabled);

static inline void ftrace_exports_enable(struct trace_export *export)
{
	if (export->flags & TRACE_EXPORT_FUNCTION)
		static_branch_inc(&trace_function_exports_enabled);

	if (export->flags & TRACE_EXPORT_EVENT)
		static_branch_inc(&trace_event_exports_enabled);

	if (export->flags & TRACE_EXPORT_MARKER)
		static_branch_inc(&trace_marker_exports_enabled);
}

static inline void ftrace_exports_disable(struct trace_export *export)
{
	if (export->flags & TRACE_EXPORT_FUNCTION)
		static_branch_dec(&trace_function_exports_enabled);

	if (export->flags & TRACE_EXPORT_EVENT)
		static_branch_dec(&trace_event_exports_enabled);

	if (export->flags & TRACE_EXPORT_MARKER)
		static_branch_dec(&trace_marker_exports_enabled);
}

static void ftrace_exports(struct ring_buffer_event *event, int flag)
{
	struct trace_export *export;

	preempt_disable_notrace();

	export = rcu_dereference_raw_check(ftrace_exports_list);
	while (export) {
		trace_process_export(export, event, flag);
		export = rcu_dereference_raw_check(export->next);
	}

	preempt_enable_notrace();
}

static inline void
add_trace_export(struct trace_export **list, struct trace_export *export)
{
	rcu_assign_pointer(export->next, *list);
	/*
	 * We are entering export into the list but another
	 * CPU might be walking that list. We need to make sure
	 * the export->next pointer is valid before another CPU sees
	 * the export pointer included into the list.
	 */
	rcu_assign_pointer(*list, export);
}

static inline int
rm_trace_export(struct trace_export **list, struct trace_export *export)
{
	struct trace_export **p;

	for (p = list; *p != NULL; p = &(*p)->next)
		if (*p == export)
			break;

	if (*p != export)
		return -1;

	rcu_assign_pointer(*p, (*p)->next);

	return 0;
}

static inline void
add_ftrace_export(struct trace_export **list, struct trace_export *export)
{
	ftrace_exports_enable(export);

	add_trace_export(list, export);
}

static inline int
rm_ftrace_export(struct trace_export **list, struct trace_export *export)
{
	int ret;

	ret = rm_trace_export(list, export);
	ftrace_exports_disable(export);

	return ret;
}

int register_ftrace_export(struct trace_export *export)
{
	if (WARN_ON_ONCE(!export->write))
		return -1;

	mutex_lock(&ftrace_export_lock);

	add_ftrace_export(&ftrace_exports_list, export);

	mutex_unlock(&ftrace_export_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(register_ftrace_export);

int unregister_ftrace_export(struct trace_export *export)
{
	int ret;

	mutex_lock(&ftrace_export_lock);

	ret = rm_ftrace_export(&ftrace_exports_list, export);

	mutex_unlock(&ftrace_export_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(unregister_ftrace_export);

/* trace_flags holds trace_options default values */
#define TRACE_DEFAULT_FLAGS						\
	(FUNCTION_DEFAULT_FLAGS |					\
	 TRACE_ITER_PRINT_PARENT | TRACE_ITER_PRINTK |			\
	 TRACE_ITER_ANNOTATE | TRACE_ITER_CONTEXT_INFO |		\
	 TRACE_ITER_RECORD_CMD | TRACE_ITER_OVERWRITE |			\
	 TRACE_ITER_IRQ_INFO | TRACE_ITER_MARKERS |			\
	 TRACE_ITER_HASH_PTR)

/* trace_options that are only supported by global_trace */
#define TOP_LEVEL_TRACE_FLAGS (TRACE_ITER_PRINTK |			\
	       TRACE_ITER_PRINTK_MSGONLY | TRACE_ITER_RECORD_CMD)

/* trace_flags that are default zero for instances */
#define ZEROED_TRACE_FLAGS \
	(TRACE_ITER_EVENT_FORK | TRACE_ITER_FUNC_FORK)

/*
 * The global_trace is the descriptor that holds the top-level tracing
 * buffers for the live tracing.
 */
static struct trace_array global_trace = {
	.trace_flags = TRACE_DEFAULT_FLAGS,
};

LIST_HEAD(ftrace_trace_arrays);

int trace_array_get(struct trace_array *this_tr)
{
	struct trace_array *tr;
	int ret = -ENODEV;

	mutex_lock(&trace_types_lock);
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (tr == this_tr) {
			tr->ref++;
			ret = 0;
			break;
		}
	}
	mutex_unlock(&trace_types_lock);

	return ret;
}

static void __trace_array_put(struct trace_array *this_tr)
{
	WARN_ON(!this_tr->ref);
	this_tr->ref--;
}

/**
 * trace_array_put - Decrement the reference counter for this trace array.
 * @this_tr : pointer to the trace array
 *
 * NOTE: Use this when we no longer need the trace array returned by
 * trace_array_get_by_name(). This ensures the trace array can be later
 * destroyed.
 *
 */
void trace_array_put(struct trace_array *this_tr)
{
	if (!this_tr)
		return;

	mutex_lock(&trace_types_lock);
	__trace_array_put(this_tr);
	mutex_unlock(&trace_types_lock);
}
EXPORT_SYMBOL_GPL(trace_array_put);

int tracing_check_open_get_tr(struct trace_array *tr)
{
	int ret;

	ret = security_locked_down(LOCKDOWN_TRACEFS);
	if (ret)
		return ret;

	if (tracing_disabled)
		return -ENODEV;

	if (tr && trace_array_get(tr) < 0)
		return -ENODEV;

	return 0;
}

int call_filter_check_discard(struct trace_event_call *call, void *rec,
			      struct trace_buffer *buffer,
			      struct ring_buffer_event *event)
{
	if (unlikely(call->flags & TRACE_EVENT_FL_FILTERED) &&
	    !filter_match_preds(call->filter, rec)) {
		__trace_event_discard_commit(buffer, event);
		return 1;
	}

	return 0;
}

/**
 * trace_find_filtered_pid - check if a pid exists in a filtered_pid list
 * @filtered_pids: The list of pids to check
 * @search_pid: The PID to find in @filtered_pids
 *
 * Returns true if @search_pid is found in @filtered_pids, and false otherwise.
 */
bool
trace_find_filtered_pid(struct trace_pid_list *filtered_pids, pid_t search_pid)
{
	return trace_pid_list_is_set(filtered_pids, search_pid);
}

/**
 * trace_ignore_this_task - should a task be ignored for tracing
 * @filtered_pids: The list of pids to check
 * @filtered_no_pids: The list of pids not to be traced
 * @task: The task that should be ignored if not filtered
 *
 * Checks if @task should be traced or not from @filtered_pids.
 * Returns true if @task should *NOT* be traced.
 * Returns false if @task should be traced.
 */
bool
trace_ignore_this_task(struct trace_pid_list *filtered_pids,
		       struct trace_pid_list *filtered_no_pids,
		       struct task_struct *task)
{
	/*
	 * If filtered_no_pids is not empty, and the task's pid is listed
	 * in filtered_no_pids, then return true.
	 * Otherwise, if filtered_pids is empty, that means we can
	 * trace all tasks. If it has content, then only trace pids
	 * within filtered_pids.
	 */

	return (filtered_pids &&
		!trace_find_filtered_pid(filtered_pids, task->pid)) ||
		(filtered_no_pids &&
		 trace_find_filtered_pid(filtered_no_pids, task->pid));
}

/**
 * trace_filter_add_remove_task - Add or remove a task from a pid_list
 * @pid_list: The list to modify
 * @self: The current task for fork or NULL for exit
 * @task: The task to add or remove
 *
 * If adding a task, if @self is defined, the task is only added if @self
 * is also included in @pid_list. This happens on fork and tasks should
 * only be added when the parent is listed. If @self is NULL, then the
 * @task pid will be removed from the list, which would happen on exit
 * of a task.
 */
void trace_filter_add_remove_task(struct trace_pid_list *pid_list,
				  struct task_struct *self,
				  struct task_struct *task)
{
	if (!pid_list)
		return;

	/* For forks, we only add if the forking task is listed */
	if (self) {
		if (!trace_find_filtered_pid(pid_list, self->pid))
			return;
	}

	/* "self" is set for forks, and NULL for exits */
	if (self)
		trace_pid_list_set(pid_list, task->pid);
	else
		trace_pid_list_clear(pid_list, task->pid);
}

/**
 * trace_pid_next - Used for seq_file to get to the next pid of a pid_list
 * @pid_list: The pid list to show
 * @v: The last pid that was shown (+1 the actual pid to let zero be displayed)
 * @pos: The position of the file
 *
 * This is used by the seq_file "next" operation to iterate the pids
 * listed in a trace_pid_list structure.
 *
 * Returns the pid+1 as we want to display pid of zero, but NULL would
 * stop the iteration.
 */
void *trace_pid_next(struct trace_pid_list *pid_list, void *v, loff_t *pos)
{
	long pid = (unsigned long)v;
	unsigned int next;

	(*pos)++;

	/* pid already is +1 of the actual previous bit */
	if (trace_pid_list_next(pid_list, pid, &next) < 0)
		return NULL;

	pid = next;

	/* Return pid + 1 to allow zero to be represented */
	return (void *)(pid + 1);
}

/**
 * trace_pid_start - Used for seq_file to start reading pid lists
 * @pid_list: The pid list to show
 * @pos: The position of the file
 *
 * This is used by seq_file "start" operation to start the iteration
 * of listing pids.
 *
 * Returns the pid+1 as we want to display pid of zero, but NULL would
 * stop the iteration.
 */
void *trace_pid_start(struct trace_pid_list *pid_list, loff_t *pos)
{
	unsigned long pid;
	unsigned int first;
	loff_t l = 0;

	if (trace_pid_list_first(pid_list, &first) < 0)
		return NULL;

	pid = first;

	/* Return pid + 1 so that zero can be the exit value */
	for (pid++; pid && l < *pos;
	     pid = (unsigned long)trace_pid_next(pid_list, (void *)pid, &l))
		;
	return (void *)pid;
}

/**
 * trace_pid_show - show the current pid in seq_file processing
 * @m: The seq_file structure to write into
 * @v: A void pointer of the pid (+1) value to display
 *
 * Can be directly used by seq_file operations to display the current
 * pid value.
 */
int trace_pid_show(struct seq_file *m, void *v)
{
	unsigned long pid = (unsigned long)v - 1;

	seq_printf(m, "%lu\n", pid);
	return 0;
}

/* 128 should be much more than enough */
#define PID_BUF_SIZE		127

int trace_pid_write(struct trace_pid_list *filtered_pids,
		    struct trace_pid_list **new_pid_list,
		    const char __user *ubuf, size_t cnt)
{
	struct trace_pid_list *pid_list;
	struct trace_parser parser;
	unsigned long val;
	int nr_pids = 0;
	ssize_t read = 0;
	ssize_t ret;
	loff_t pos;
	pid_t pid;

	if (trace_parser_get_init(&parser, PID_BUF_SIZE + 1))
		return -ENOMEM;

	/*
	 * Always recreate a new array. The write is an all or nothing
	 * operation. Always create a new array when adding new pids by
	 * the user. If the operation fails, then the current list is
	 * not modified.
	 */
	pid_list = trace_pid_list_alloc();
	if (!pid_list) {
		trace_parser_put(&parser);
		return -ENOMEM;
	}

	if (filtered_pids) {
		/* copy the current bits to the new max */
		ret = trace_pid_list_first(filtered_pids, &pid);
		while (!ret) {
			trace_pid_list_set(pid_list, pid);
			ret = trace_pid_list_next(filtered_pids, pid + 1, &pid);
			nr_pids++;
		}
	}

	ret = 0;
	while (cnt > 0) {

		pos = 0;

		ret = trace_get_user(&parser, ubuf, cnt, &pos);
		if (ret < 0)
			break;

		read += ret;
		ubuf += ret;
		cnt -= ret;

		if (!trace_parser_loaded(&parser))
			break;

		ret = -EINVAL;
		if (kstrtoul(parser.buffer, 0, &val))
			break;

		pid = (pid_t)val;

		if (trace_pid_list_set(pid_list, pid) < 0) {
			ret = -1;
			break;
		}
		nr_pids++;

		trace_parser_clear(&parser);
		ret = 0;
	}
	trace_parser_put(&parser);

	if (ret < 0) {
		trace_pid_list_free(pid_list);
		return ret;
	}

	if (!nr_pids) {
		/* Cleared the list of pids */
		trace_pid_list_free(pid_list);
		pid_list = NULL;
	}

	*new_pid_list = pid_list;

	return read;
}

static u64 buffer_ftrace_now(struct array_buffer *buf, int cpu)
{
	u64 ts;

	/* Early boot up does not have a buffer yet */
	if (!buf->buffer)
		return trace_clock_local();

	ts = ring_buffer_time_stamp(buf->buffer);
	ring_buffer_normalize_time_stamp(buf->buffer, cpu, &ts);

	return ts;
}

u64 ftrace_now(int cpu)
{
	return buffer_ftrace_now(&global_trace.array_buffer, cpu);
}

/**
 * tracing_is_enabled - Show if global_trace has been enabled
 *
 * Shows if the global trace has been enabled or not. It uses the
 * mirror flag "buffer_disabled" to be used in fast paths such as for
 * the irqsoff tracer. But it may be inaccurate due to races. If you
 * need to know the accurate state, use tracing_is_on() which is a little
 * slower, but accurate.
 */
int tracing_is_enabled(void)
{
	/*
	 * For quick access (irqsoff uses this in fast path), just
	 * return the mirror variable of the state of the ring buffer.
	 * It's a little racy, but we don't really care.
	 */
	smp_rmb();
	return !global_trace.buffer_disabled;
}

/*
 * trace_buf_size is the size in bytes that is allocated
 * for a buffer. Note, the number of bytes is always rounded
 * to page size.
 *
 * This number is purposely set to a low number of 16384.
 * If the dump on oops happens, it will be much appreciated
 * to not have to wait for all that output. Anyway this can be
 * boot time and run time configurable.
 */
#define TRACE_BUF_SIZE_DEFAULT	1441792UL /* 16384 * 88 (sizeof(entry)) */

static unsigned long		trace_buf_size = TRACE_BUF_SIZE_DEFAULT;

/* trace_types holds a link list of available tracers. */
static struct tracer		*trace_types __read_mostly;

/*
 * trace_types_lock is used to protect the trace_types list.
 */
DEFINE_MUTEX(trace_types_lock);

/*
 * serialize the access of the ring buffer
 *
 * ring buffer serializes readers, but it is low level protection.
 * The validity of the events (which returns by ring_buffer_peek() ..etc)
 * are not protected by ring buffer.
 *
 * The content of events may become garbage if we allow other process consumes
 * these events concurrently:
 *   A) the page of the consumed events may become a normal page
 *      (not reader page) in ring buffer, and this page will be rewritten
 *      by events producer.
 *   B) The page of the consumed events may become a page for splice_read,
 *      and this page will be returned to system.
 *
 * These primitives allow multi process access to different cpu ring buffer
 * concurrently.
 *
 * These primitives don't distinguish read-only and read-consume access.
 * Multi read-only access are also serialized.
 */

#ifdef CONFIG_SMP
static DECLARE_RWSEM(all_cpu_access_lock);
static DEFINE_PER_CPU(struct mutex, cpu_access_lock);

static inline void trace_access_lock(int cpu)
{
	if (cpu == RING_BUFFER_ALL_CPUS) {
		/* gain it for accessing the whole ring buffer. */
		down_write(&all_cpu_access_lock);
	} else {
		/* gain it for accessing a cpu ring buffer. */

		/* Firstly block other trace_access_lock(RING_BUFFER_ALL_CPUS). */
		down_read(&all_cpu_access_lock);

		/* Secondly block other access to this @cpu ring buffer. */
		mutex_lock(&per_cpu(cpu_access_lock, cpu));
	}
}

static inline void trace_access_unlock(int cpu)
{
	if (cpu == RING_BUFFER_ALL_CPUS) {
		up_write(&all_cpu_access_lock);
	} else {
		mutex_unlock(&per_cpu(cpu_access_lock, cpu));
		up_read(&all_cpu_access_lock);
	}
}

static inline void trace_access_lock_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		mutex_init(&per_cpu(cpu_access_lock, cpu));
}

#else

static DEFINE_MUTEX(access_lock);

static inline void trace_access_lock(int cpu)
{
	(void)cpu;
	mutex_lock(&access_lock);
}

static inline void trace_access_unlock(int cpu)
{
	(void)cpu;
	mutex_unlock(&access_lock);
}

static inline void trace_access_lock_init(void)
{
}

#endif

#ifdef CONFIG_STACKTRACE
static void __ftrace_trace_stack(struct trace_buffer *buffer,
				 unsigned int trace_ctx,
				 int skip, struct pt_regs *regs);
static inline void ftrace_trace_stack(struct trace_array *tr,
				      struct trace_buffer *buffer,
				      unsigned int trace_ctx,
				      int skip, struct pt_regs *regs);

#else
static inline void __ftrace_trace_stack(struct trace_buffer *buffer,
					unsigned int trace_ctx,
					int skip, struct pt_regs *regs)
{
}
static inline void ftrace_trace_stack(struct trace_array *tr,
				      struct trace_buffer *buffer,
				      unsigned long trace_ctx,
				      int skip, struct pt_regs *regs)
{
}

#endif

static __always_inline void
trace_event_setup(struct ring_buffer_event *event,
		  int type, unsigned int trace_ctx)
{
	struct trace_entry *ent = ring_buffer_event_data(event);

	tracing_generic_entry_update(ent, type, trace_ctx);
}

static __always_inline struct ring_buffer_event *
__trace_buffer_lock_reserve(struct trace_buffer *buffer,
			  int type,
			  unsigned long len,
			  unsigned int trace_ctx)
{
	struct ring_buffer_event *event;

	event = ring_buffer_lock_reserve(buffer, len);
	if (event != NULL)
		trace_event_setup(event, type, trace_ctx);

	return event;
}

void tracer_tracing_on(struct trace_array *tr)
{
	if (tr->array_buffer.buffer)
		ring_buffer_record_on(tr->array_buffer.buffer);
	/*
	 * This flag is looked at when buffers haven't been allocated
	 * yet, or by some tracers (like irqsoff), that just want to
	 * know if the ring buffer has been disabled, but it can handle
	 * races of where it gets disabled but we still do a record.
	 * As the check is in the fast path of the tracers, it is more
	 * important to be fast than accurate.
	 */
	tr->buffer_disabled = 0;
	/* Make the flag seen by readers */
	smp_wmb();
}

/**
 * tracing_on - enable tracing buffers
 *
 * This function enables tracing buffers that may have been
 * disabled with tracing_off.
 */
void tracing_on(void)
{
	tracer_tracing_on(&global_trace);
}
EXPORT_SYMBOL_GPL(tracing_on);


static __always_inline void
__buffer_unlock_commit(struct trace_buffer *buffer, struct ring_buffer_event *event)
{
	__this_cpu_write(trace_taskinfo_save, true);

	/* If this is the temp buffer, we need to commit fully */
	if (this_cpu_read(trace_buffered_event) == event) {
		/* Length is in event->array[0] */
		ring_buffer_write(buffer, event->array[0], &event->array[1]);
		/* Release the temp buffer */
		this_cpu_dec(trace_buffered_event_cnt);
	} else
		ring_buffer_unlock_commit(buffer, event);
}

/**
 * __trace_puts - write a constant string into the trace buffer.
 * @ip:	   The address of the caller
 * @str:   The constant string to write
 * @size:  The size of the string.
 */
int __trace_puts(unsigned long ip, const char *str, int size)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct print_entry *entry;
	unsigned int trace_ctx;
	int alloc;

	if (!(global_trace.trace_flags & TRACE_ITER_PRINTK))
		return 0;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	alloc = sizeof(*entry) + size + 2; /* possible \n added */

	trace_ctx = tracing_gen_ctx();
	buffer = global_trace.array_buffer.buffer;
	ring_buffer_nest_start(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_PRINT, alloc,
					    trace_ctx);
	if (!event) {
		size = 0;
		goto out;
	}

	entry = ring_buffer_event_data(event);
	entry->ip = ip;

	memcpy(&entry->buf, str, size);

	/* Add a newline if necessary */
	if (entry->buf[size - 1] != '\n') {
		entry->buf[size] = '\n';
		entry->buf[size + 1] = '\0';
	} else
		entry->buf[size] = '\0';

	__buffer_unlock_commit(buffer, event);
	ftrace_trace_stack(&global_trace, buffer, trace_ctx, 4, NULL);
 out:
	ring_buffer_nest_end(buffer);
	return size;
}
EXPORT_SYMBOL_GPL(__trace_puts);

/**
 * __trace_bputs - write the pointer to a constant string into trace buffer
 * @ip:	   The address of the caller
 * @str:   The constant string to write to the buffer to
 */
int __trace_bputs(unsigned long ip, const char *str)
{
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct bputs_entry *entry;
	unsigned int trace_ctx;
	int size = sizeof(struct bputs_entry);
	int ret = 0;

	if (!(global_trace.trace_flags & TRACE_ITER_PRINTK))
		return 0;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	trace_ctx = tracing_gen_ctx();
	buffer = global_trace.array_buffer.buffer;

	ring_buffer_nest_start(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_BPUTS, size,
					    trace_ctx);
	if (!event)
		goto out;

	entry = ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->str			= str;

	__buffer_unlock_commit(buffer, event);
	ftrace_trace_stack(&global_trace, buffer, trace_ctx, 4, NULL);

	ret = 1;
 out:
	ring_buffer_nest_end(buffer);
	return ret;
}
EXPORT_SYMBOL_GPL(__trace_bputs);

#ifdef CONFIG_TRACER_SNAPSHOT
static void tracing_snapshot_instance_cond(struct trace_array *tr,
					   void *cond_data)
{
	struct tracer *tracer = tr->current_trace;
	unsigned long flags;

	if (in_nmi()) {
		internal_trace_puts("*** SNAPSHOT CALLED FROM NMI CONTEXT ***\n");
		internal_trace_puts("*** snapshot is being ignored        ***\n");
		return;
	}

	if (!tr->allocated_snapshot) {
		internal_trace_puts("*** SNAPSHOT NOT ALLOCATED ***\n");
		internal_trace_puts("*** stopping trace here!   ***\n");
		tracing_off();
		return;
	}

	/* Note, snapshot can not be used when the tracer uses it */
	if (tracer->use_max_tr) {
		internal_trace_puts("*** LATENCY TRACER ACTIVE ***\n");
		internal_trace_puts("*** Can not use snapshot (sorry) ***\n");
		return;
	}

	local_irq_save(flags);
	update_max_tr(tr, current, smp_processor_id(), cond_data);
	local_irq_restore(flags);
}

void tracing_snapshot_instance(struct trace_array *tr)
{
	tracing_snapshot_instance_cond(tr, NULL);
}

/**
 * tracing_snapshot - take a snapshot of the current buffer.
 *
 * This causes a swap between the snapshot buffer and the current live
 * tracing buffer. You can use this to take snapshots of the live
 * trace when some condition is triggered, but continue to trace.
 *
 * Note, make sure to allocate the snapshot with either
 * a tracing_snapshot_alloc(), or by doing it manually
 * with: echo 1 > /sys/kernel/debug/tracing/snapshot
 *
 * If the snapshot buffer is not allocated, it will stop tracing.
 * Basically making a permanent snapshot.
 */
void tracing_snapshot(void)
{
	struct trace_array *tr = &global_trace;

	tracing_snapshot_instance(tr);
}
EXPORT_SYMBOL_GPL(tracing_snapshot);

/**
 * tracing_snapshot_cond - conditionally take a snapshot of the current buffer.
 * @tr:		The tracing instance to snapshot
 * @cond_data:	The data to be tested conditionally, and possibly saved
 *
 * This is the same as tracing_snapshot() except that the snapshot is
 * conditional - the snapshot will only happen if the
 * cond_snapshot.update() implementation receiving the cond_data
 * returns true, which means that the trace array's cond_snapshot
 * update() operation used the cond_data to determine whether the
 * snapshot should be taken, and if it was, presumably saved it along
 * with the snapshot.
 */
void tracing_snapshot_cond(struct trace_array *tr, void *cond_data)
{
	tracing_snapshot_instance_cond(tr, cond_data);
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond);

/**
 * tracing_snapshot_cond_data - get the user data associated with a snapshot
 * @tr:		The tracing instance
 *
 * When the user enables a conditional snapshot using
 * tracing_snapshot_cond_enable(), the user-defined cond_data is saved
 * with the snapshot.  This accessor is used to retrieve it.
 *
 * Should not be called from cond_snapshot.update(), since it takes
 * the tr->max_lock lock, which the code calling
 * cond_snapshot.update() has already done.
 *
 * Returns the cond_data associated with the trace array's snapshot.
 */
void *tracing_cond_snapshot_data(struct trace_array *tr)
{
	void *cond_data = NULL;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);

	if (tr->cond_snapshot)
		cond_data = tr->cond_snapshot->cond_data;

	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	return cond_data;
}
EXPORT_SYMBOL_GPL(tracing_cond_snapshot_data);

static int resize_buffer_duplicate_size(struct array_buffer *trace_buf,
					struct array_buffer *size_buf, int cpu_id);
static void set_buffer_entries(struct array_buffer *buf, unsigned long val);

int tracing_alloc_snapshot_instance(struct trace_array *tr)
{
	int ret;

	if (!tr->allocated_snapshot) {

		/* allocate spare buffer */
		ret = resize_buffer_duplicate_size(&tr->max_buffer,
				   &tr->array_buffer, RING_BUFFER_ALL_CPUS);
		if (ret < 0)
			return ret;

		tr->allocated_snapshot = true;
	}

	return 0;
}

static void free_snapshot(struct trace_array *tr)
{
	/*
	 * We don't free the ring buffer. instead, resize it because
	 * The max_tr ring buffer has some state (e.g. ring->clock) and
	 * we want preserve it.
	 */
	ring_buffer_resize(tr->max_buffer.buffer, 1, RING_BUFFER_ALL_CPUS);
	set_buffer_entries(&tr->max_buffer, 1);
	tracing_reset_online_cpus(&tr->max_buffer);
	tr->allocated_snapshot = false;
}

/**
 * tracing_alloc_snapshot - allocate snapshot buffer.
 *
 * This only allocates the snapshot buffer if it isn't already
 * allocated - it doesn't also take a snapshot.
 *
 * This is meant to be used in cases where the snapshot buffer needs
 * to be set up for events that can't sleep but need to be able to
 * trigger a snapshot.
 */
int tracing_alloc_snapshot(void)
{
	struct trace_array *tr = &global_trace;
	int ret;

	ret = tracing_alloc_snapshot_instance(tr);
	WARN_ON(ret < 0);

	return ret;
}
EXPORT_SYMBOL_GPL(tracing_alloc_snapshot);

/**
 * tracing_snapshot_alloc - allocate and take a snapshot of the current buffer.
 *
 * This is similar to tracing_snapshot(), but it will allocate the
 * snapshot buffer if it isn't already allocated. Use this only
 * where it is safe to sleep, as the allocation may sleep.
 *
 * This causes a swap between the snapshot buffer and the current live
 * tracing buffer. You can use this to take snapshots of the live
 * trace when some condition is triggered, but continue to trace.
 */
void tracing_snapshot_alloc(void)
{
	int ret;

	ret = tracing_alloc_snapshot();
	if (ret < 0)
		return;

	tracing_snapshot();
}
EXPORT_SYMBOL_GPL(tracing_snapshot_alloc);

/**
 * tracing_snapshot_cond_enable - enable conditional snapshot for an instance
 * @tr:		The tracing instance
 * @cond_data:	User data to associate with the snapshot
 * @update:	Implementation of the cond_snapshot update function
 *
 * Check whether the conditional snapshot for the given instance has
 * already been enabled, or if the current tracer is already using a
 * snapshot; if so, return -EBUSY, else create a cond_snapshot and
 * save the cond_data and update function inside.
 *
 * Returns 0 if successful, error otherwise.
 */
int tracing_snapshot_cond_enable(struct trace_array *tr, void *cond_data,
				 cond_update_fn_t update)
{
	struct cond_snapshot *cond_snapshot;
	int ret = 0;

	cond_snapshot = kzalloc(sizeof(*cond_snapshot), GFP_KERNEL);
	if (!cond_snapshot)
		return -ENOMEM;

	cond_snapshot->cond_data = cond_data;
	cond_snapshot->update = update;

	mutex_lock(&trace_types_lock);

	ret = tracing_alloc_snapshot_instance(tr);
	if (ret)
		goto fail_unlock;

	if (tr->current_trace->use_max_tr) {
		ret = -EBUSY;
		goto fail_unlock;
	}

	/*
	 * The cond_snapshot can only change to NULL without the
	 * trace_types_lock. We don't care if we race with it going
	 * to NULL, but we want to make sure that it's not set to
	 * something other than NULL when we get here, which we can
	 * do safely with only holding the trace_types_lock and not
	 * having to take the max_lock.
	 */
	if (tr->cond_snapshot) {
		ret = -EBUSY;
		goto fail_unlock;
	}

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);
	tr->cond_snapshot = cond_snapshot;
	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	mutex_unlock(&trace_types_lock);

	return ret;

 fail_unlock:
	mutex_unlock(&trace_types_lock);
	kfree(cond_snapshot);
	return ret;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_enable);

/**
 * tracing_snapshot_cond_disable - disable conditional snapshot for an instance
 * @tr:		The tracing instance
 *
 * Check whether the conditional snapshot for the given instance is
 * enabled; if so, free the cond_snapshot associated with it,
 * otherwise return -EINVAL.
 *
 * Returns 0 if successful, error otherwise.
 */
int tracing_snapshot_cond_disable(struct trace_array *tr)
{
	int ret = 0;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);

	if (!tr->cond_snapshot)
		ret = -EINVAL;
	else {
		kfree(tr->cond_snapshot);
		tr->cond_snapshot = NULL;
	}

	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_disable);
#else
void tracing_snapshot(void)
{
	WARN_ONCE(1, "Snapshot feature not enabled, but internal snapshot used");
}
EXPORT_SYMBOL_GPL(tracing_snapshot);
void tracing_snapshot_cond(struct trace_array *tr, void *cond_data)
{
	WARN_ONCE(1, "Snapshot feature not enabled, but internal conditional snapshot used");
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond);
int tracing_alloc_snapshot(void)
{
	WARN_ONCE(1, "Snapshot feature not enabled, but snapshot allocation used");
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(tracing_alloc_snapshot);
void tracing_snapshot_alloc(void)
{
	/* Give warning */
	tracing_snapshot();
}
EXPORT_SYMBOL_GPL(tracing_snapshot_alloc);
void *tracing_cond_snapshot_data(struct trace_array *tr)
{
	return NULL;
}
EXPORT_SYMBOL_GPL(tracing_cond_snapshot_data);
int tracing_snapshot_cond_enable(struct trace_array *tr, void *cond_data, cond_update_fn_t update)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_enable);
int tracing_snapshot_cond_disable(struct trace_array *tr)
{
	return false;
}
EXPORT_SYMBOL_GPL(tracing_snapshot_cond_disable);
#endif /* CONFIG_TRACER_SNAPSHOT */

void tracer_tracing_off(struct trace_array *tr)
{
	if (tr->array_buffer.buffer)
		ring_buffer_record_off(tr->array_buffer.buffer);
	/*
	 * This flag is looked at when buffers haven't been allocated
	 * yet, or by some tracers (like irqsoff), that just want to
	 * know if the ring buffer has been disabled, but it can handle
	 * races of where it gets disabled but we still do a record.
	 * As the check is in the fast path of the tracers, it is more
	 * important to be fast than accurate.
	 */
	tr->buffer_disabled = 1;
	/* Make the flag seen by readers */
	smp_wmb();
}

/**
 * tracing_off - turn off tracing buffers
 *
 * This function stops the tracing buffers from recording data.
 * It does not disable any overhead the tracers themselves may
 * be causing. This function simply causes all recording to
 * the ring buffers to fail.
 */
void tracing_off(void)
{
	tracer_tracing_off(&global_trace);
}
EXPORT_SYMBOL_GPL(tracing_off);

void disable_trace_on_warning(void)
{
	if (__disable_trace_on_warning) {
		trace_array_printk_buf(global_trace.array_buffer.buffer, _THIS_IP_,
			"Disabling tracing due to warning\n");
		tracing_off();
	}
}

/**
 * tracer_tracing_is_on - show real state of ring buffer enabled
 * @tr : the trace array to know if ring buffer is enabled
 *
 * Shows real state of the ring buffer if it is enabled or not.
 */
bool tracer_tracing_is_on(struct trace_array *tr)
{
	if (tr->array_buffer.buffer)
		return ring_buffer_record_is_on(tr->array_buffer.buffer);
	return !tr->buffer_disabled;
}

/**
 * tracing_is_on - show state of ring buffers enabled
 */
int tracing_is_on(void)
{
	return tracer_tracing_is_on(&global_trace);
}
EXPORT_SYMBOL_GPL(tracing_is_on);

static int __init set_buf_size(char *str)
{
	unsigned long buf_size;

	if (!str)
		return 0;
	buf_size = memparse(str, &str);
	/*
	 * nr_entries can not be zero and the startup
	 * tests require some buffer space. Therefore
	 * ensure we have at least 4096 bytes of buffer.
	 */
	trace_buf_size = max(4096UL, buf_size);
	return 1;
}
__setup("trace_buf_size=", set_buf_size);

static int __init set_tracing_thresh(char *str)
{
	unsigned long threshold;
	int ret;

	if (!str)
		return 0;
	ret = kstrtoul(str, 0, &threshold);
	if (ret < 0)
		return 0;
	tracing_thresh = threshold * 1000;
	return 1;
}
__setup("tracing_thresh=", set_tracing_thresh);

unsigned long nsecs_to_usecs(unsigned long nsecs)
{
	return nsecs / 1000;
}

/*
 * TRACE_FLAGS is defined as a tuple matching bit masks with strings.
 * It uses C(a, b) where 'a' is the eval (enum) name and 'b' is the string that
 * matches it. By defining "C(a, b) b", TRACE_FLAGS becomes a list
 * of strings in the order that the evals (enum) were defined.
 */
#undef C
#define C(a, b) b

/* These must match the bit positions in trace_iterator_flags */
static const char *trace_options[] = {
	TRACE_FLAGS
	NULL
};

static struct {
	u64 (*func)(void);
	const char *name;
	int in_ns;		/* is this clock in nanoseconds? */
} trace_clocks[] = {
	{ trace_clock_local,		"local",	1 },
	{ trace_clock_global,		"global",	1 },
	{ trace_clock_counter,		"counter",	0 },
	{ trace_clock_jiffies,		"uptime",	0 },
	{ trace_clock,			"perf",		1 },
	{ ktime_get_mono_fast_ns,	"mono",		1 },
	{ ktime_get_raw_fast_ns,	"mono_raw",	1 },
	{ ktime_get_boot_fast_ns,	"boot",		1 },
	ARCH_TRACE_CLOCKS
};

bool trace_clock_in_ns(struct trace_array *tr)
{
	if (trace_clocks[tr->clock_id].in_ns)
		return true;

	return false;
}

/*
 * trace_parser_get_init - gets the buffer for trace parser
 */
int trace_parser_get_init(struct trace_parser *parser, int size)
{
	memset(parser, 0, sizeof(*parser));

	parser->buffer = kmalloc(size, GFP_KERNEL);
	if (!parser->buffer)
		return 1;

	parser->size = size;
	return 0;
}

/*
 * trace_parser_put - frees the buffer for trace parser
 */
void trace_parser_put(struct trace_parser *parser)
{
	kfree(parser->buffer);
	parser->buffer = NULL;
}

/*
 * trace_get_user - reads the user input string separated by  space
 * (matched by isspace(ch))
 *
 * For each string found the 'struct trace_parser' is updated,
 * and the function returns.
 *
 * Returns number of bytes read.
 *
 * See kernel/trace/trace.h for 'struct trace_parser' details.
 */
int trace_get_user(struct trace_parser *parser, const char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char ch;
	size_t read = 0;
	ssize_t ret;

	if (!*ppos)
		trace_parser_clear(parser);

	ret = get_user(ch, ubuf++);
	if (ret)
		goto out;

	read++;
	cnt--;

	/*
	 * The parser is not finished with the last write,
	 * continue reading the user input without skipping spaces.
	 */
	if (!parser->cont) {
		/* skip white space */
		while (cnt && isspace(ch)) {
			ret = get_user(ch, ubuf++);
			if (ret)
				goto out;
			read++;
			cnt--;
		}

		parser->idx = 0;

		/* only spaces were written */
		if (isspace(ch) || !ch) {
			*ppos += read;
			ret = read;
			goto out;
		}
	}

	/* read the non-space input */
	while (cnt && !isspace(ch) && ch) {
		if (parser->idx < parser->size - 1)
			parser->buffer[parser->idx++] = ch;
		else {
			ret = -EINVAL;
			goto out;
		}
		ret = get_user(ch, ubuf++);
		if (ret)
			goto out;
		read++;
		cnt--;
	}

	/* We either got finished input or we have to wait for another call. */
	if (isspace(ch) || !ch) {
		parser->buffer[parser->idx] = 0;
		parser->cont = false;
	} else if (parser->idx < parser->size - 1) {
		parser->cont = true;
		parser->buffer[parser->idx++] = ch;
		/* Make sure the parsed string always terminates with '\0'. */
		parser->buffer[parser->idx] = 0;
	} else {
		ret = -EINVAL;
		goto out;
	}

	*ppos += read;
	ret = read;

out:
	return ret;
}

/* TODO add a seq_buf_to_buffer() */
static ssize_t trace_seq_to_buffer(struct trace_seq *s, void *buf, size_t cnt)
{
	int len;

	if (trace_seq_used(s) <= s->seq.readpos)
		return -EBUSY;

	len = trace_seq_used(s) - s->seq.readpos;
	if (cnt > len)
		cnt = len;
	memcpy(buf, s->buffer + s->seq.readpos, cnt);

	s->seq.readpos += cnt;
	return cnt;
}

unsigned long __read_mostly	tracing_thresh;
static const struct file_operations tracing_max_lat_fops;

#ifdef LATENCY_FS_NOTIFY

static struct workqueue_struct *fsnotify_wq;

static void latency_fsnotify_workfn(struct work_struct *work)
{
	struct trace_array *tr = container_of(work, struct trace_array,
					      fsnotify_work);
	fsnotify_inode(tr->d_max_latency->d_inode, FS_MODIFY);
}

static void latency_fsnotify_workfn_irq(struct irq_work *iwork)
{
	struct trace_array *tr = container_of(iwork, struct trace_array,
					      fsnotify_irqwork);
	queue_work(fsnotify_wq, &tr->fsnotify_work);
}

static void trace_create_maxlat_file(struct trace_array *tr,
				     struct dentry *d_tracer)
{
	INIT_WORK(&tr->fsnotify_work, latency_fsnotify_workfn);
	init_irq_work(&tr->fsnotify_irqwork, latency_fsnotify_workfn_irq);
	tr->d_max_latency = trace_create_file("tracing_max_latency",
					      TRACE_MODE_WRITE,
					      d_tracer, &tr->max_latency,
					      &tracing_max_lat_fops);
}

__init static int latency_fsnotify_init(void)
{
	fsnotify_wq = alloc_workqueue("tr_max_lat_wq",
				      WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!fsnotify_wq) {
		pr_err("Unable to allocate tr_max_lat_wq\n");
		return -ENOMEM;
	}
	return 0;
}

late_initcall_sync(latency_fsnotify_init);

void latency_fsnotify(struct trace_array *tr)
{
	if (!fsnotify_wq)
		return;
	/*
	 * We cannot call queue_work(&tr->fsnotify_work) from here because it's
	 * possible that we are called from __schedule() or do_idle(), which
	 * could cause a deadlock.
	 */
	irq_work_queue(&tr->fsnotify_irqwork);
}

#elif defined(CONFIG_TRACER_MAX_TRACE) || defined(CONFIG_HWLAT_TRACER)	\
	|| defined(CONFIG_OSNOISE_TRACER)

#define trace_create_maxlat_file(tr, d_tracer)				\
	trace_create_file("tracing_max_latency", TRACE_MODE_WRITE,	\
			  d_tracer, &tr->max_latency, &tracing_max_lat_fops)

#else
#define trace_create_maxlat_file(tr, d_tracer)	 do { } while (0)
#endif

#ifdef CONFIG_TRACER_MAX_TRACE
/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /sys/kernel/tracing/tracing_max_latency)
 */
static void
__update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct array_buffer *trace_buf = &tr->array_buffer;
	struct array_buffer *max_buf = &tr->max_buffer;
	struct trace_array_cpu *data = per_cpu_ptr(trace_buf->data, cpu);
	struct trace_array_cpu *max_data = per_cpu_ptr(max_buf->data, cpu);

	max_buf->cpu = cpu;
	max_buf->time_start = data->preempt_timestamp;

	max_data->saved_latency = tr->max_latency;
	max_data->critical_start = data->critical_start;
	max_data->critical_end = data->critical_end;

	strncpy(max_data->comm, tsk->comm, TASK_COMM_LEN);
	max_data->pid = tsk->pid;
	/*
	 * If tsk == current, then use current_uid(), as that does not use
	 * RCU. The irq tracer can be called out of RCU scope.
	 */
	if (tsk == current)
		max_data->uid = current_uid();
	else
		max_data->uid = task_uid(tsk);

	max_data->nice = tsk->static_prio - 20 - MAX_RT_PRIO;
	max_data->policy = tsk->policy;
	max_data->rt_priority = tsk->rt_priority;

	/* record this tasks comm */
	tracing_record_cmdline(tsk);
	latency_fsnotify(tr);
}

/**
 * update_max_tr - snapshot all trace buffers from global_trace to max_tr
 * @tr: tracer
 * @tsk: the task with the latency
 * @cpu: The cpu that initiated the trace.
 * @cond_data: User data associated with a conditional snapshot
 *
 * Flip the buffers between the @tr and the max_tr and record information
 * about which task was the cause of this latency.
 */
void
update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu,
	      void *cond_data)
{
	if (tr->stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());

	if (!tr->allocated_snapshot) {
		/* Only the nop tracer should hit this when disabling */
		WARN_ON_ONCE(tr->current_trace != &nop_trace);
		return;
	}

	arch_spin_lock(&tr->max_lock);

	/* Inherit the recordable setting from array_buffer */
	if (ring_buffer_record_is_set_on(tr->array_buffer.buffer))
		ring_buffer_record_on(tr->max_buffer.buffer);
	else
		ring_buffer_record_off(tr->max_buffer.buffer);

#ifdef CONFIG_TRACER_SNAPSHOT
	if (tr->cond_snapshot && !tr->cond_snapshot->update(tr, cond_data))
		goto out_unlock;
#endif
	swap(tr->array_buffer.buffer, tr->max_buffer.buffer);

	__update_max_tr(tr, tsk, cpu);

 out_unlock:
	arch_spin_unlock(&tr->max_lock);
}

/**
 * update_max_tr_single - only copy one trace over, and reset the rest
 * @tr: tracer
 * @tsk: task with the latency
 * @cpu: the cpu of the buffer to copy.
 *
 * Flip the trace of a single CPU buffer between the @tr and the max_tr.
 */
void
update_max_tr_single(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	int ret;

	if (tr->stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());
	if (!tr->allocated_snapshot) {
		/* Only the nop tracer should hit this when disabling */
		WARN_ON_ONCE(tr->current_trace != &nop_trace);
		return;
	}

	arch_spin_lock(&tr->max_lock);

	ret = ring_buffer_swap_cpu(tr->max_buffer.buffer, tr->array_buffer.buffer, cpu);

	if (ret == -EBUSY) {
		/*
		 * We failed to swap the buffer due to a commit taking
		 * place on this CPU. We fail to record, but we reset
		 * the max trace buffer (no one writes directly to it)
		 * and flag that it failed.
		 */
		trace_array_printk_buf(tr->max_buffer.buffer, _THIS_IP_,
			"Failed to swap buffers due to commit in progress\n");
	}

	WARN_ON_ONCE(ret && ret != -EAGAIN && ret != -EBUSY);

	__update_max_tr(tr, tsk, cpu);
	arch_spin_unlock(&tr->max_lock);
}
#endif /* CONFIG_TRACER_MAX_TRACE */

static int wait_on_pipe(struct trace_iterator *iter, int full)
{
	/* Iterators are static, they should be filled or empty */
	if (trace_buffer_iter(iter, iter->cpu_file))
		return 0;

	return ring_buffer_wait(iter->array_buffer->buffer, iter->cpu_file,
				full);
}

#ifdef CONFIG_FTRACE_STARTUP_TEST
static bool selftests_can_run;

struct trace_selftests {
	struct list_head		list;
	struct tracer			*type;
};

static LIST_HEAD(postponed_selftests);

static int save_selftest(struct tracer *type)
{
	struct trace_selftests *selftest;

	selftest = kmalloc(sizeof(*selftest), GFP_KERNEL);
	if (!selftest)
		return -ENOMEM;

	selftest->type = type;
	list_add(&selftest->list, &postponed_selftests);
	return 0;
}

static int run_tracer_selftest(struct tracer *type)
{
	struct trace_array *tr = &global_trace;
	struct tracer *saved_tracer = tr->current_trace;
	int ret;

	if (!type->selftest || tracing_selftest_disabled)
		return 0;

	/*
	 * If a tracer registers early in boot up (before scheduling is
	 * initialized and such), then do not run its selftests yet.
	 * Instead, run it a little later in the boot process.
	 */
	if (!selftests_can_run)
		return save_selftest(type);

	if (!tracing_is_on()) {
		pr_warn("Selftest for tracer %s skipped due to tracing disabled\n",
			type->name);
		return 0;
	}

	/*
	 * Run a selftest on this tracer.
	 * Here we reset the trace buffer, and set the current
	 * tracer to be this tracer. The tracer can then run some
	 * internal tracing to verify that everything is in order.
	 * If we fail, we do not register this tracer.
	 */
	tracing_reset_online_cpus(&tr->array_buffer);

	tr->current_trace = type;

#ifdef CONFIG_TRACER_MAX_TRACE
	if (type->use_max_tr) {
		/* If we expanded the buffers, make sure the max is expanded too */
		if (ring_buffer_expanded)
			ring_buffer_resize(tr->max_buffer.buffer, trace_buf_size,
					   RING_BUFFER_ALL_CPUS);
		tr->allocated_snapshot = true;
	}
#endif

	/* the test is responsible for initializing and enabling */
	pr_info("Testing tracer %s: ", type->name);
	ret = type->selftest(type, tr);
	/* the test is responsible for resetting too */
	tr->current_trace = saved_tracer;
	if (ret) {
		printk(KERN_CONT "FAILED!\n");
		/* Add the warning after printing 'FAILED' */
		WARN_ON(1);
		return -1;
	}
	/* Only reset on passing, to avoid touching corrupted buffers */
	tracing_reset_online_cpus(&tr->array_buffer);

#ifdef CONFIG_TRACER_MAX_TRACE
	if (type->use_max_tr) {
		tr->allocated_snapshot = false;

		/* Shrink the max buffer again */
		if (ring_buffer_expanded)
			ring_buffer_resize(tr->max_buffer.buffer, 1,
					   RING_BUFFER_ALL_CPUS);
	}
#endif

	printk(KERN_CONT "PASSED\n");
	return 0;
}

static __init int init_trace_selftests(void)
{
	struct trace_selftests *p, *n;
	struct tracer *t, **last;
	int ret;

	selftests_can_run = true;

	mutex_lock(&trace_types_lock);

	if (list_empty(&postponed_selftests))
		goto out;

	pr_info("Running postponed tracer tests:\n");

	tracing_selftest_running = true;
	list_for_each_entry_safe(p, n, &postponed_selftests, list) {
		/* This loop can take minutes when sanitizers are enabled, so
		 * lets make sure we allow RCU processing.
		 */
		cond_resched();
		ret = run_tracer_selftest(p->type);
		/* If the test fails, then warn and remove from available_tracers */
		if (ret < 0) {
			WARN(1, "tracer: %s failed selftest, disabling\n",
			     p->type->name);
			last = &trace_types;
			for (t = trace_types; t; t = t->next) {
				if (t == p->type) {
					*last = t->next;
					break;
				}
				last = &t->next;
			}
		}
		list_del(&p->list);
		kfree(p);
	}
	tracing_selftest_running = false;

 out:
	mutex_unlock(&trace_types_lock);

	return 0;
}
core_initcall(init_trace_selftests);
#else
static inline int run_tracer_selftest(struct tracer *type)
{
	return 0;
}
#endif /* CONFIG_FTRACE_STARTUP_TEST */

static void add_tracer_options(struct trace_array *tr, struct tracer *t);

static void __init apply_trace_boot_options(void);

/**
 * register_tracer - register a tracer with the ftrace system.
 * @type: the plugin for the tracer
 *
 * Register a new plugin tracer.
 */
int __init register_tracer(struct tracer *type)
{
	struct tracer *t;
	int ret = 0;

	if (!type->name) {
		pr_info("Tracer must have a name\n");
		return -1;
	}

	if (strlen(type->name) >= MAX_TRACER_SIZE) {
		pr_info("Tracer has a name longer than %d\n", MAX_TRACER_SIZE);
		return -1;
	}

	if (security_locked_down(LOCKDOWN_TRACEFS)) {
		pr_warn("Can not register tracer %s due to lockdown\n",
			   type->name);
		return -EPERM;
	}

	mutex_lock(&trace_types_lock);

	tracing_selftest_running = true;

	for (t = trace_types; t; t = t->next) {
		if (strcmp(type->name, t->name) == 0) {
			/* already found */
			pr_info("Tracer %s already registered\n",
				type->name);
			ret = -1;
			goto out;
		}
	}

	if (!type->set_flag)
		type->set_flag = &dummy_set_flag;
	if (!type->flags) {
		/*allocate a dummy tracer_flags*/
		type->flags = kmalloc(sizeof(*type->flags), GFP_KERNEL);
		if (!type->flags) {
			ret = -ENOMEM;
			goto out;
		}
		type->flags->val = 0;
		type->flags->opts = dummy_tracer_opt;
	} else
		if (!type->flags->opts)
			type->flags->opts = dummy_tracer_opt;

	/* store the tracer for __set_tracer_option */
	type->flags->trace = type;

	ret = run_tracer_selftest(type);
	if (ret < 0)
		goto out;

	type->next = trace_types;
	trace_types = type;
	add_tracer_options(&global_trace, type);

 out:
	tracing_selftest_running = false;
	mutex_unlock(&trace_types_lock);

	if (ret || !default_bootup_tracer)
		goto out_unlock;

	if (strncmp(default_bootup_tracer, type->name, MAX_TRACER_SIZE))
		goto out_unlock;

	printk(KERN_INFO "Starting tracer '%s'\n", type->name);
	/* Do we want this tracer to start on bootup? */
	tracing_set_tracer(&global_trace, type->name);
	default_bootup_tracer = NULL;

	apply_trace_boot_options();

	/* disable other selftests, since this will break it. */
	disable_tracing_selftest("running a tracer");

 out_unlock:
	return ret;
}

static void tracing_reset_cpu(struct array_buffer *buf, int cpu)
{
	struct trace_buffer *buffer = buf->buffer;

	if (!buffer)
		return;

	ring_buffer_record_disable(buffer);

	/* Make sure all commits have finished */
	synchronize_rcu();
	ring_buffer_reset_cpu(buffer, cpu);

	ring_buffer_record_enable(buffer);
}

void tracing_reset_online_cpus(struct array_buffer *buf)
{
	struct trace_buffer *buffer = buf->buffer;

	if (!buffer)
		return;

	ring_buffer_record_disable(buffer);

	/* Make sure all commits have finished */
	synchronize_rcu();

	buf->time_start = buffer_ftrace_now(buf, buf->cpu);

	ring_buffer_reset_online_cpus(buffer);

	ring_buffer_record_enable(buffer);
}

/* Must have trace_types_lock held */
void tracing_reset_all_online_cpus(void)
{
	struct trace_array *tr;

	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!tr->clear_trace)
			continue;
		tr->clear_trace = false;
		tracing_reset_online_cpus(&tr->array_buffer);
#ifdef CONFIG_TRACER_MAX_TRACE
		tracing_reset_online_cpus(&tr->max_buffer);
#endif
	}
}

/*
 * The tgid_map array maps from pid to tgid; i.e. the value stored at index i
 * is the tgid last observed corresponding to pid=i.
 */
static int *tgid_map;

/* The maximum valid index into tgid_map. */
static size_t tgid_map_max;

#define SAVED_CMDLINES_DEFAULT 128
#define NO_CMDLINE_MAP UINT_MAX
/*
 * Preemption must be disabled before acquiring trace_cmdline_lock.
 * The various trace_arrays' max_lock must be acquired in a context
 * where interrupt is disabled.
 */
static arch_spinlock_t trace_cmdline_lock = __ARCH_SPIN_LOCK_UNLOCKED;
struct saved_cmdlines_buffer {
	unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
	unsigned *map_cmdline_to_pid;
	unsigned cmdline_num;
	int cmdline_idx;
	char *saved_cmdlines;
};
static struct saved_cmdlines_buffer *savedcmd;

static inline char *get_saved_cmdlines(int idx)
{
	return &savedcmd->saved_cmdlines[idx * TASK_COMM_LEN];
}

static inline void set_cmdline(int idx, const char *cmdline)
{
	strncpy(get_saved_cmdlines(idx), cmdline, TASK_COMM_LEN);
}

static int allocate_cmdlines_buffer(unsigned int val,
				    struct saved_cmdlines_buffer *s)
{
	s->map_cmdline_to_pid = kmalloc_array(val,
					      sizeof(*s->map_cmdline_to_pid),
					      GFP_KERNEL);
	if (!s->map_cmdline_to_pid)
		return -ENOMEM;

	s->saved_cmdlines = kmalloc_array(TASK_COMM_LEN, val, GFP_KERNEL);
	if (!s->saved_cmdlines) {
		kfree(s->map_cmdline_to_pid);
		return -ENOMEM;
	}

	s->cmdline_idx = 0;
	s->cmdline_num = val;
	memset(&s->map_pid_to_cmdline, NO_CMDLINE_MAP,
	       sizeof(s->map_pid_to_cmdline));
	memset(s->map_cmdline_to_pid, NO_CMDLINE_MAP,
	       val * sizeof(*s->map_cmdline_to_pid));

	return 0;
}

static int trace_create_savedcmd(void)
{
	int ret;

	savedcmd = kmalloc(sizeof(*savedcmd), GFP_KERNEL);
	if (!savedcmd)
		return -ENOMEM;

	ret = allocate_cmdlines_buffer(SAVED_CMDLINES_DEFAULT, savedcmd);
	if (ret < 0) {
		kfree(savedcmd);
		savedcmd = NULL;
		return -ENOMEM;
	}

	return 0;
}

int is_tracing_stopped(void)
{
	return global_trace.stop_count;
}

/**
 * tracing_start - quick start of the tracer
 *
 * If tracing is enabled but was stopped by tracing_stop,
 * this will start the tracer back up.
 */
void tracing_start(void)
{
	struct trace_buffer *buffer;
	unsigned long flags;

	if (tracing_disabled)
		return;

	raw_spin_lock_irqsave(&global_trace.start_lock, flags);
	if (--global_trace.stop_count) {
		if (global_trace.stop_count < 0) {
			/* Someone screwed up their debugging */
			WARN_ON_ONCE(1);
			global_trace.stop_count = 0;
		}
		goto out;
	}

	/* Prevent the buffers from switching */
	arch_spin_lock(&global_trace.max_lock);

	buffer = global_trace.array_buffer.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

#ifdef CONFIG_TRACER_MAX_TRACE
	buffer = global_trace.max_buffer.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);
#endif

	arch_spin_unlock(&global_trace.max_lock);

 out:
	raw_spin_unlock_irqrestore(&global_trace.start_lock, flags);
}

static void tracing_start_tr(struct trace_array *tr)
{
	struct trace_buffer *buffer;
	unsigned long flags;

	if (tracing_disabled)
		return;

	/* If global, we need to also start the max tracer */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return tracing_start();

	raw_spin_lock_irqsave(&tr->start_lock, flags);

	if (--tr->stop_count) {
		if (tr->stop_count < 0) {
			/* Someone screwed up their debugging */
			WARN_ON_ONCE(1);
			tr->stop_count = 0;
		}
		goto out;
	}

	buffer = tr->array_buffer.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

 out:
	raw_spin_unlock_irqrestore(&tr->start_lock, flags);
}

/**
 * tracing_stop - quick stop of the tracer
 *
 * Light weight way to stop tracing. Use in conjunction with
 * tracing_start.
 */
void tracing_stop(void)
{
	struct trace_buffer *buffer;
	unsigned long flags;

	raw_spin_lock_irqsave(&global_trace.start_lock, flags);
	if (global_trace.stop_count++)
		goto out;

	/* Prevent the buffers from switching */
	arch_spin_lock(&global_trace.max_lock);

	buffer = global_trace.array_buffer.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

#ifdef CONFIG_TRACER_MAX_TRACE
	buffer = global_trace.max_buffer.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);
#endif

	arch_spin_unlock(&global_trace.max_lock);

 out:
	raw_spin_unlock_irqrestore(&global_trace.start_lock, flags);
}

static void tracing_stop_tr(struct trace_array *tr)
{
	struct trace_buffer *buffer;
	unsigned long flags;

	/* If global, we need to also stop the max tracer */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return tracing_stop();

	raw_spin_lock_irqsave(&tr->start_lock, flags);
	if (tr->stop_count++)
		goto out;

	buffer = tr->array_buffer.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

 out:
	raw_spin_unlock_irqrestore(&tr->start_lock, flags);
}

static int trace_save_cmdline(struct task_struct *tsk)
{
	unsigned tpid, idx;

	/* treat recording of idle task as a success */
	if (!tsk->pid)
		return 1;

	tpid = tsk->pid & (PID_MAX_DEFAULT - 1);

	/*
	 * It's not the end of the world if we don't get
	 * the lock, but we also don't want to spin
	 * nor do we want to disable interrupts,
	 * so if we miss here, then better luck next time.
	 *
	 * This is called within the scheduler and wake up, so interrupts
	 * had better been disabled and run queue lock been held.
	 */
	lockdep_assert_preemption_disabled();
	if (!arch_spin_trylock(&trace_cmdline_lock))
		return 0;

	idx = savedcmd->map_pid_to_cmdline[tpid];
	if (idx == NO_CMDLINE_MAP) {
		idx = (savedcmd->cmdline_idx + 1) % savedcmd->cmdline_num;

		savedcmd->map_pid_to_cmdline[tpid] = idx;
		savedcmd->cmdline_idx = idx;
	}

	savedcmd->map_cmdline_to_pid[idx] = tsk->pid;
	set_cmdline(idx, tsk->comm);

	arch_spin_unlock(&trace_cmdline_lock);

	return 1;
}

static void __trace_find_cmdline(int pid, char comm[])
{
	unsigned map;
	int tpid;

	if (!pid) {
		strcpy(comm, "<idle>");
		return;
	}

	if (WARN_ON_ONCE(pid < 0)) {
		strcpy(comm, "<XXX>");
		return;
	}

	tpid = pid & (PID_MAX_DEFAULT - 1);
	map = savedcmd->map_pid_to_cmdline[tpid];
	if (map != NO_CMDLINE_MAP) {
		tpid = savedcmd->map_cmdline_to_pid[map];
		if (tpid == pid) {
			strlcpy(comm, get_saved_cmdlines(map), TASK_COMM_LEN);
			return;
		}
	}
	strcpy(comm, "<...>");
}

void trace_find_cmdline(int pid, char comm[])
{
	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);

	__trace_find_cmdline(pid, comm);

	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
}

static int *trace_find_tgid_ptr(int pid)
{
	/*
	 * Pairs with the smp_store_release in set_tracer_flag() to ensure that
	 * if we observe a non-NULL tgid_map then we also observe the correct
	 * tgid_map_max.
	 */
	int *map = smp_load_acquire(&tgid_map);

	if (unlikely(!map || pid > tgid_map_max))
		return NULL;

	return &map[pid];
}

int trace_find_tgid(int pid)
{
	int *ptr = trace_find_tgid_ptr(pid);

	return ptr ? *ptr : 0;
}

static int trace_save_tgid(struct task_struct *tsk)
{
	int *ptr;

	/* treat recording of idle task as a success */
	if (!tsk->pid)
		return 1;

	ptr = trace_find_tgid_ptr(tsk->pid);
	if (!ptr)
		return 0;

	*ptr = tsk->tgid;
	return 1;
}

static bool tracing_record_taskinfo_skip(int flags)
{
	if (unlikely(!(flags & (TRACE_RECORD_CMDLINE | TRACE_RECORD_TGID))))
		return true;
	if (!__this_cpu_read(trace_taskinfo_save))
		return true;
	return false;
}

/**
 * tracing_record_taskinfo - record the task info of a task
 *
 * @task:  task to record
 * @flags: TRACE_RECORD_CMDLINE for recording comm
 *         TRACE_RECORD_TGID for recording tgid
 */
void tracing_record_taskinfo(struct task_struct *task, int flags)
{
	bool done;

	if (tracing_record_taskinfo_skip(flags))
		return;

	/*
	 * Record as much task information as possible. If some fail, continue
	 * to try to record the others.
	 */
	done = !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(task);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(task);

	/* If recording any information failed, retry again soon. */
	if (!done)
		return;

	__this_cpu_write(trace_taskinfo_save, false);
}

/**
 * tracing_record_taskinfo_sched_switch - record task info for sched_switch
 *
 * @prev: previous task during sched_switch
 * @next: next task during sched_switch
 * @flags: TRACE_RECORD_CMDLINE for recording comm
 *         TRACE_RECORD_TGID for recording tgid
 */
void tracing_record_taskinfo_sched_switch(struct task_struct *prev,
					  struct task_struct *next, int flags)
{
	bool done;

	if (tracing_record_taskinfo_skip(flags))
		return;

	/*
	 * Record as much task information as possible. If some fail, continue
	 * to try to record the others.
	 */
	done  = !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(prev);
	done &= !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(next);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(prev);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(next);

	/* If recording any information failed, retry again soon. */
	if (!done)
		return;

	__this_cpu_write(trace_taskinfo_save, false);
}

/* Helpers to record a specific task information */
void tracing_record_cmdline(struct task_struct *task)
{
	tracing_record_taskinfo(task, TRACE_RECORD_CMDLINE);
}

void tracing_record_tgid(struct task_struct *task)
{
	tracing_record_taskinfo(task, TRACE_RECORD_TGID);
}

/*
 * Several functions return TRACE_TYPE_PARTIAL_LINE if the trace_seq
 * overflowed, and TRACE_TYPE_HANDLED otherwise. This helper function
 * simplifies those functions and keeps them in sync.
 */
enum print_line_t trace_handle_return(struct trace_seq *s)
{
	return trace_seq_has_overflowed(s) ?
		TRACE_TYPE_PARTIAL_LINE : TRACE_TYPE_HANDLED;
}
EXPORT_SYMBOL_GPL(trace_handle_return);

static unsigned short migration_disable_value(void)
{
#if defined(CONFIG_SMP)
	return current->migration_disabled;
#else
	return 0;
#endif
}

unsigned int tracing_gen_ctx_irq_test(unsigned int irqs_status)
{
	unsigned int trace_flags = irqs_status;
	unsigned int pc;

	pc = preempt_count();

	if (pc & NMI_MASK)
		trace_flags |= TRACE_FLAG_NMI;
	if (pc & HARDIRQ_MASK)
		trace_flags |= TRACE_FLAG_HARDIRQ;
	if (in_serving_softirq())
		trace_flags |= TRACE_FLAG_SOFTIRQ;

	if (tif_need_resched())
		trace_flags |= TRACE_FLAG_NEED_RESCHED;
	if (test_preempt_need_resched())
		trace_flags |= TRACE_FLAG_PREEMPT_RESCHED;
	return (trace_flags << 16) | (min_t(unsigned int, pc & 0xff, 0xf)) |
		(min_t(unsigned int, migration_disable_value(), 0xf)) << 4;
}

struct ring_buffer_event *
trace_buffer_lock_reserve(struct trace_buffer *buffer,
			  int type,
			  unsigned long len,
			  unsigned int trace_ctx)
{
	return __trace_buffer_lock_reserve(buffer, type, len, trace_ctx);
}

DEFINE_PER_CPU(struct ring_buffer_event *, trace_buffered_event);
DEFINE_PER_CPU(int, trace_buffered_event_cnt);
static int trace_buffered_event_ref;

/**
 * trace_buffered_event_enable - enable buffering events
 *
 * When events are being filtered, it is quicker to use a temporary
 * buffer to write the event data into if there's a likely chance
 * that it will not be committed. The discard of the ring buffer
 * is not as fast as committing, and is much slower than copying
 * a commit.
 *
 * When an event is to be filtered, allocate per cpu buffers to
 * write the event data into, and if the event is filtered and discarded
 * it is simply dropped, otherwise, the entire data is to be committed
 * in one shot.
 */
void trace_buffered_event_enable(void)
{
	struct ring_buffer_event *event;
	struct page *page;
	int cpu;

	WARN_ON_ONCE(!mutex_is_locked(&event_mutex));

	if (trace_buffered_event_ref++)
		return;

	for_each_tracing_cpu(cpu) {
		page = alloc_pages_node(cpu_to_node(cpu),
					GFP_KERNEL | __GFP_NORETRY, 0);
		if (!page)
			goto failed;

		event = page_address(page);
		memset(event, 0, sizeof(*event));

		per_cpu(trace_buffered_event, cpu) = event;

		preempt_disable();
		if (cpu == smp_processor_id() &&
		    __this_cpu_read(trace_buffered_event) !=
		    per_cpu(trace_buffered_event, cpu))
			WARN_ON_ONCE(1);
		preempt_enable();
	}

	return;
 failed:
	trace_buffered_event_disable();
}

static void enable_trace_buffered_event(void *data)
{
	/* Probably not needed, but do it anyway */
	smp_rmb();
	this_cpu_dec(trace_buffered_event_cnt);
}

static void disable_trace_buffered_event(void *data)
{
	this_cpu_inc(trace_buffered_event_cnt);
}

/**
 * trace_buffered_event_disable - disable buffering events
 *
 * When a filter is removed, it is faster to not use the buffered
 * events, and to commit directly into the ring buffer. Free up
 * the temp buffers when there are no more users. This requires
 * special synchronization with current events.
 */
void trace_buffered_event_disable(void)
{
	int cpu;

	WARN_ON_ONCE(!mutex_is_locked(&event_mutex));

	if (WARN_ON_ONCE(!trace_buffered_event_ref))
		return;

	if (--trace_buffered_event_ref)
		return;

	preempt_disable();
	/* For each CPU, set the buffer as used. */
	smp_call_function_many(tracing_buffer_mask,
			       disable_trace_buffered_event, NULL, 1);
	preempt_enable();

	/* Wait for all current users to finish */
	synchronize_rcu();

	for_each_tracing_cpu(cpu) {
		free_page((unsigned long)per_cpu(trace_buffered_event, cpu));
		per_cpu(trace_buffered_event, cpu) = NULL;
	}
	/*
	 * Make sure trace_buffered_event is NULL before clearing
	 * trace_buffered_event_cnt.
	 */
	smp_wmb();

	preempt_disable();
	/* Do the work on each cpu */
	smp_call_function_many(tracing_buffer_mask,
			       enable_trace_buffered_event, NULL, 1);
	preempt_enable();
}

static struct trace_buffer *temp_buffer;

struct ring_buffer_event *
trace_event_buffer_lock_reserve(struct trace_buffer **current_rb,
			  struct trace_event_file *trace_file,
			  int type, unsigned long len,
			  unsigned int trace_ctx)
{
	struct ring_buffer_event *entry;
	struct trace_array *tr = trace_file->tr;
	int val;

	*current_rb = tr->array_buffer.buffer;

	if (!tr->no_filter_buffering_ref &&
	    (trace_file->flags & (EVENT_FILE_FL_SOFT_DISABLED | EVENT_FILE_FL_FILTERED)) &&
	    (entry = this_cpu_read(trace_buffered_event))) {
		/*
		 * Filtering is on, so try to use the per cpu buffer first.
		 * This buffer will simulate a ring_buffer_event,
		 * where the type_len is zero and the array[0] will
		 * hold the full length.
		 * (see include/linux/ring-buffer.h for details on
		 *  how the ring_buffer_event is structured).
		 *
		 * Using a temp buffer during filtering and copying it
		 * on a matched filter is quicker than writing directly
		 * into the ring buffer and then discarding it when
		 * it doesn't match. That is because the discard
		 * requires several atomic operations to get right.
		 * Copying on match and doing nothing on a failed match
		 * is still quicker than no copy on match, but having
		 * to discard out of the ring buffer on a failed match.
		 */
		int max_len = PAGE_SIZE - struct_size(entry, array, 1);

		val = this_cpu_inc_return(trace_buffered_event_cnt);

		/*
		 * Preemption is disabled, but interrupts and NMIs
		 * can still come in now. If that happens after
		 * the above increment, then it will have to go
		 * back to the old method of allocating the event
		 * on the ring buffer, and if the filter fails, it
		 * will have to call ring_buffer_discard_commit()
		 * to remove it.
		 *
		 * Need to also check the unlikely case that the
		 * length is bigger than the temp buffer size.
		 * If that happens, then the reserve is pretty much
		 * guaranteed to fail, as the ring buffer currently
		 * only allows events less than a page. But that may
		 * change in the future, so let the ring buffer reserve
		 * handle the failure in that case.
		 */
		if (val == 1 && likely(len <= max_len)) {
			trace_event_setup(entry, type, trace_ctx);
			entry->array[0] = len;
			return entry;
		}
		this_cpu_dec(trace_buffered_event_cnt);
	}

	entry = __trace_buffer_lock_reserve(*current_rb, type, len,
					    trace_ctx);
	/*
	 * If tracing is off, but we have triggers enabled
	 * we still need to look at the event data. Use the temp_buffer
	 * to store the trace event for the trigger to use. It's recursive
	 * safe and will not be recorded anywhere.
	 */
	if (!entry && trace_file->flags & EVENT_FILE_FL_TRIGGER_COND) {
		*current_rb = temp_buffer;
		entry = __trace_buffer_lock_reserve(*current_rb, type, len,
						    trace_ctx);
	}
	return entry;
}
EXPORT_SYMBOL_GPL(trace_event_buffer_lock_reserve);

static DEFINE_RAW_SPINLOCK(tracepoint_iter_lock);
static DEFINE_MUTEX(tracepoint_printk_mutex);

static void output_printk(struct trace_event_buffer *fbuffer)
{
	struct trace_event_call *event_call;
	struct trace_event_file *file;
	struct trace_event *event;
	unsigned long flags;
	struct trace_iterator *iter = tracepoint_print_iter;

	/* We should never get here if iter is NULL */
	if (WARN_ON_ONCE(!iter))
		return;

	event_call = fbuffer->trace_file->event_call;
	if (!event_call || !event_call->event.funcs ||
	    !event_call->event.funcs->trace)
		return;

	file = fbuffer->trace_file;
	if (test_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags) ||
	    (unlikely(file->flags & EVENT_FILE_FL_FILTERED) &&
	     !filter_match_preds(file->filter, fbuffer->entry)))
		return;

	event = &fbuffer->trace_file->event_call->event;

	raw_spin_lock_irqsave(&tracepoint_iter_lock, flags);
	trace_seq_init(&iter->seq);
	iter->ent = fbuffer->entry;
	event_call->event.funcs->trace(iter, 0, event);
	trace_seq_putc(&iter->seq, 0);
	printk("%s", iter->seq.buffer);

	raw_spin_unlock_irqrestore(&tracepoint_iter_lock, flags);
}

int tracepoint_printk_sysctl(struct ctl_table *table, int write,
			     void *buffer, size_t *lenp,
			     loff_t *ppos)
{
	int save_tracepoint_printk;
	int ret;

	mutex_lock(&tracepoint_printk_mutex);
	save_tracepoint_printk = tracepoint_printk;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	/*
	 * This will force exiting early, as tracepoint_printk
	 * is always zero when tracepoint_printk_iter is not allocated
	 */
	if (!tracepoint_print_iter)
		tracepoint_printk = 0;

	if (save_tracepoint_printk == tracepoint_printk)
		goto out;

	if (tracepoint_printk)
		static_key_enable(&tracepoint_printk_key.key);
	else
		static_key_disable(&tracepoint_printk_key.key);

 out:
	mutex_unlock(&tracepoint_printk_mutex);

	return ret;
}

void trace_event_buffer_commit(struct trace_event_buffer *fbuffer)
{
	enum event_trigger_type tt = ETT_NONE;
	struct trace_event_file *file = fbuffer->trace_file;

	if (__event_trigger_test_discard(file, fbuffer->buffer, fbuffer->event,
			fbuffer->entry, &tt))
		goto discard;

	if (static_key_false(&tracepoint_printk_key.key))
		output_printk(fbuffer);

	if (static_branch_unlikely(&trace_event_exports_enabled))
		ftrace_exports(fbuffer->event, TRACE_EXPORT_EVENT);

	trace_buffer_unlock_commit_regs(file->tr, fbuffer->buffer,
			fbuffer->event, fbuffer->trace_ctx, fbuffer->regs);

discard:
	if (tt)
		event_triggers_post_call(file, tt);

}
EXPORT_SYMBOL_GPL(trace_event_buffer_commit);

/*
 * Skip 3:
 *
 *   trace_buffer_unlock_commit_regs()
 *   trace_event_buffer_commit()
 *   trace_event_raw_event_xxx()
 */
# define STACK_SKIP 3

void trace_buffer_unlock_commit_regs(struct trace_array *tr,
				     struct trace_buffer *buffer,
				     struct ring_buffer_event *event,
				     unsigned int trace_ctx,
				     struct pt_regs *regs)
{
	__buffer_unlock_commit(buffer, event);

	/*
	 * If regs is not set, then skip the necessary functions.
	 * Note, we can still get here via blktrace, wakeup tracer
	 * and mmiotrace, but that's ok if they lose a function or
	 * two. They are not that meaningful.
	 */
	ftrace_trace_stack(tr, buffer, trace_ctx, regs ? 0 : STACK_SKIP, regs);
	ftrace_trace_userstack(tr, buffer, trace_ctx);
}

/*
 * Similar to trace_buffer_unlock_commit_regs() but do not dump stack.
 */
void
trace_buffer_unlock_commit_nostack(struct trace_buffer *buffer,
				   struct ring_buffer_event *event)
{
	__buffer_unlock_commit(buffer, event);
}

void
trace_function(struct trace_array *tr, unsigned long ip, unsigned long
	       parent_ip, unsigned int trace_ctx)
{
	struct trace_event_call *call = &event_function;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct ring_buffer_event *event;
	struct ftrace_entry *entry;

	event = __trace_buffer_lock_reserve(buffer, TRACE_FN, sizeof(*entry),
					    trace_ctx);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;

	if (!call_filter_check_discard(call, entry, buffer, event)) {
		if (static_branch_unlikely(&trace_function_exports_enabled))
			ftrace_exports(event, TRACE_EXPORT_FUNCTION);
		__buffer_unlock_commit(buffer, event);
	}
}

#ifdef CONFIG_STACKTRACE

/* Allow 4 levels of nesting: normal, softirq, irq, NMI */
#define FTRACE_KSTACK_NESTING	4

#define FTRACE_KSTACK_ENTRIES	(PAGE_SIZE / FTRACE_KSTACK_NESTING)

struct ftrace_stack {
	unsigned long		calls[FTRACE_KSTACK_ENTRIES];
};


struct ftrace_stacks {
	struct ftrace_stack	stacks[FTRACE_KSTACK_NESTING];
};

static DEFINE_PER_CPU(struct ftrace_stacks, ftrace_stacks);
static DEFINE_PER_CPU(int, ftrace_stack_reserve);

static void __ftrace_trace_stack(struct trace_buffer *buffer,
				 unsigned int trace_ctx,
				 int skip, struct pt_regs *regs)
{
	struct trace_event_call *call = &event_kernel_stack;
	struct ring_buffer_event *event;
	unsigned int size, nr_entries;
	struct ftrace_stack *fstack;
	struct stack_entry *entry;
	int stackidx;

	/*
	 * Add one, for this function and the call to save_stack_trace()
	 * If regs is set, then these functions will not be in the way.
	 */
#ifndef CONFIG_UNWINDER_ORC
	if (!regs)
		skip++;
#endif

	preempt_disable_notrace();

	stackidx = __this_cpu_inc_return(ftrace_stack_reserve) - 1;

	/* This should never happen. If it does, yell once and skip */
	if (WARN_ON_ONCE(stackidx >= FTRACE_KSTACK_NESTING))
		goto out;

	/*
	 * The above __this_cpu_inc_return() is 'atomic' cpu local. An
	 * interrupt will either see the value pre increment or post
	 * increment. If the interrupt happens pre increment it will have
	 * restored the counter when it returns.  We just need a barrier to
	 * keep gcc from moving things around.
	 */
	barrier();

	fstack = this_cpu_ptr(ftrace_stacks.stacks) + stackidx;
	size = ARRAY_SIZE(fstack->calls);

	if (regs) {
		nr_entries = stack_trace_save_regs(regs, fstack->calls,
						   size, skip);
	} else {
		nr_entries = stack_trace_save(fstack->calls, size, skip);
	}

	size = nr_entries * sizeof(unsigned long);
	event = __trace_buffer_lock_reserve(buffer, TRACE_STACK,
				    (sizeof(*entry) - sizeof(entry->caller)) + size,
				    trace_ctx);
	if (!event)
		goto out;
	entry = ring_buffer_event_data(event);

	memcpy(&entry->caller, fstack->calls, size);
	entry->size = nr_entries;

	if (!call_filter_check_discard(call, entry, buffer, event))
		__buffer_unlock_commit(buffer, event);

 out:
	/* Again, don't let gcc optimize things here */
	barrier();
	__this_cpu_dec(ftrace_stack_reserve);
	preempt_enable_notrace();

}

static inline void ftrace_trace_stack(struct trace_array *tr,
				      struct trace_buffer *buffer,
				      unsigned int trace_ctx,
				      int skip, struct pt_regs *regs)
{
	if (!(tr->trace_flags & TRACE_ITER_STACKTRACE))
		return;

	__ftrace_trace_stack(buffer, trace_ctx, skip, regs);
}

void __trace_stack(struct trace_array *tr, unsigned int trace_ctx,
		   int skip)
{
	struct trace_buffer *buffer = tr->array_buffer.buffer;

	if (rcu_is_watching()) {
		__ftrace_trace_stack(buffer, trace_ctx, skip, NULL);
		return;
	}

	/*
	 * When an NMI triggers, RCU is enabled via rcu_nmi_enter(),
	 * but if the above rcu_is_watching() failed, then the NMI
	 * triggered someplace critical, and rcu_irq_enter() should
	 * not be called from NMI.
	 */
	if (unlikely(in_nmi()))
		return;

	rcu_irq_enter_irqson();
	__ftrace_trace_stack(buffer, trace_ctx, skip, NULL);
	rcu_irq_exit_irqson();
}

/**
 * trace_dump_stack - record a stack back trace in the trace buffer
 * @skip: Number of functions to skip (helper handlers)
 */
void trace_dump_stack(int skip)
{
	if (tracing_disabled || tracing_selftest_running)
		return;

#ifndef CONFIG_UNWINDER_ORC
	/* Skip 1 to skip this function. */
	skip++;
#endif
	__ftrace_trace_stack(global_trace.array_buffer.buffer,
			     tracing_gen_ctx(), skip, NULL);
}
EXPORT_SYMBOL_GPL(trace_dump_stack);

#ifdef CONFIG_USER_STACKTRACE_SUPPORT
static DEFINE_PER_CPU(int, user_stack_count);

static void
ftrace_trace_userstack(struct trace_array *tr,
		       struct trace_buffer *buffer, unsigned int trace_ctx)
{
	struct trace_event_call *call = &event_user_stack;
	struct ring_buffer_event *event;
	struct userstack_entry *entry;

	if (!(tr->trace_flags & TRACE_ITER_USERSTACKTRACE))
		return;

	/*
	 * NMIs can not handle page faults, even with fix ups.
	 * The save user stack can (and often does) fault.
	 */
	if (unlikely(in_nmi()))
		return;

	/*
	 * prevent recursion, since the user stack tracing may
	 * trigger other kernel events.
	 */
	preempt_disable();
	if (__this_cpu_read(user_stack_count))
		goto out;

	__this_cpu_inc(user_stack_count);

	event = __trace_buffer_lock_reserve(buffer, TRACE_USER_STACK,
					    sizeof(*entry), trace_ctx);
	if (!event)
		goto out_drop_count;
	entry	= ring_buffer_event_data(event);

	entry->tgid		= current->tgid;
	memset(&entry->caller, 0, sizeof(entry->caller));

	stack_trace_save_user(entry->caller, FTRACE_STACK_ENTRIES);
	if (!call_filter_check_discard(call, entry, buffer, event))
		__buffer_unlock_commit(buffer, event);

 out_drop_count:
	__this_cpu_dec(user_stack_count);
 out:
	preempt_enable();
}
#else /* CONFIG_USER_STACKTRACE_SUPPORT */
static void ftrace_trace_userstack(struct trace_array *tr,
				   struct trace_buffer *buffer,
				   unsigned int trace_ctx)
{
}
#endif /* !CONFIG_USER_STACKTRACE_SUPPORT */

#endif /* CONFIG_STACKTRACE */

static inline void
func_repeats_set_delta_ts(struct func_repeats_entry *entry,
			  unsigned long long delta)
{
	entry->bottom_delta_ts = delta & U32_MAX;
	entry->top_delta_ts = (delta >> 32);
}

void trace_last_func_repeats(struct trace_array *tr,
			     struct trace_func_repeats *last_info,
			     unsigned int trace_ctx)
{
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct func_repeats_entry *entry;
	struct ring_buffer_event *event;
	u64 delta;

	event = __trace_buffer_lock_reserve(buffer, TRACE_FUNC_REPEATS,
					    sizeof(*entry), trace_ctx);
	if (!event)
		return;

	delta = ring_buffer_event_time_stamp(buffer, event) -
		last_info->ts_last_call;

	entry = ring_buffer_event_data(event);
	entry->ip = last_info->ip;
	entry->parent_ip = last_info->parent_ip;
	entry->count = last_info->count;
	func_repeats_set_delta_ts(entry, delta);

	__buffer_unlock_commit(buffer, event);
}

/* created for use with alloc_percpu */
struct trace_buffer_struct {
	int nesting;
	char buffer[4][TRACE_BUF_SIZE];
};

static struct trace_buffer_struct __percpu *trace_percpu_buffer;

/*
 * This allows for lockless recording.  If we're nested too deeply, then
 * this returns NULL.
 */
static char *get_trace_buf(void)
{
	struct trace_buffer_struct *buffer = this_cpu_ptr(trace_percpu_buffer);

	if (!trace_percpu_buffer || buffer->nesting >= 4)
		return NULL;

	buffer->nesting++;

	/* Interrupts must see nesting incremented before we use the buffer */
	barrier();
	return &buffer->buffer[buffer->nesting - 1][0];
}

static void put_trace_buf(void)
{
	/* Don't let the decrement of nesting leak before this */
	barrier();
	this_cpu_dec(trace_percpu_buffer->nesting);
}

static int alloc_percpu_trace_buffer(void)
{
	struct trace_buffer_struct __percpu *buffers;

	if (trace_percpu_buffer)
		return 0;

	buffers = alloc_percpu(struct trace_buffer_struct);
	if (MEM_FAIL(!buffers, "Could not allocate percpu trace_printk buffer"))
		return -ENOMEM;

	trace_percpu_buffer = buffers;
	return 0;
}

static int buffers_allocated;

void trace_printk_init_buffers(void)
{
	if (buffers_allocated)
		return;

	if (alloc_percpu_trace_buffer())
		return;

	/* trace_printk() is for debug use only. Don't use it in production. */

	pr_warn("\n");
	pr_warn("**********************************************************\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** trace_printk() being used. Allocating extra memory.  **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** This means that this is a DEBUG kernel and it is     **\n");
	pr_warn("** unsafe for production use.                           **\n");
	pr_warn("**                                                      **\n");
	pr_warn("** If you see this message and you are not debugging    **\n");
	pr_warn("** the kernel, report this immediately to your vendor!  **\n");
	pr_warn("**                                                      **\n");
	pr_warn("**   NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE   **\n");
	pr_warn("**********************************************************\n");

	/* Expand the buffers to set size */
	tracing_update_buffers();

	buffers_allocated = 1;

	/*
	 * trace_printk_init_buffers() can be called by modules.
	 * If that happens, then we need to start cmdline recording
	 * directly here. If the global_trace.buffer is already
	 * allocated here, then this was called by module code.
	 */
	if (global_trace.array_buffer.buffer)
		tracing_start_cmdline_record();
}
EXPORT_SYMBOL_GPL(trace_printk_init_buffers);

void trace_printk_start_comm(void)
{
	/* Start tracing comms if trace printk is set */
	if (!buffers_allocated)
		return;
	tracing_start_cmdline_record();
}

static void trace_printk_start_stop_comm(int enabled)
{
	if (!buffers_allocated)
		return;

	if (enabled)
		tracing_start_cmdline_record();
	else
		tracing_stop_cmdline_record();
}

/**
 * trace_vbprintk - write binary msg to tracing buffer
 * @ip:    The address of the caller
 * @fmt:   The string format to write to the buffer
 * @args:  Arguments for @fmt
 */
int trace_vbprintk(unsigned long ip, const char *fmt, va_list args)
{
	struct trace_event_call *call = &event_bprint;
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct trace_array *tr = &global_trace;
	struct bprint_entry *entry;
	unsigned int trace_ctx;
	char *tbuffer;
	int len = 0, size;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	trace_ctx = tracing_gen_ctx();
	preempt_disable_notrace();

	tbuffer = get_trace_buf();
	if (!tbuffer) {
		len = 0;
		goto out_nobuffer;
	}

	len = vbin_printf((u32 *)tbuffer, TRACE_BUF_SIZE/sizeof(int), fmt, args);

	if (len > TRACE_BUF_SIZE/sizeof(int) || len < 0)
		goto out_put;

	size = sizeof(*entry) + sizeof(u32) * len;
	buffer = tr->array_buffer.buffer;
	ring_buffer_nest_start(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_BPRINT, size,
					    trace_ctx);
	if (!event)
		goto out;
	entry = ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->fmt			= fmt;

	memcpy(entry->buf, tbuffer, sizeof(u32) * len);
	if (!call_filter_check_discard(call, entry, buffer, event)) {
		__buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(tr, buffer, trace_ctx, 6, NULL);
	}

out:
	ring_buffer_nest_end(buffer);
out_put:
	put_trace_buf();

out_nobuffer:
	preempt_enable_notrace();
	unpause_graph_tracing();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vbprintk);

__printf(3, 0)
static int
__trace_array_vprintk(struct trace_buffer *buffer,
		      unsigned long ip, const char *fmt, va_list args)
{
	struct trace_event_call *call = &event_print;
	struct ring_buffer_event *event;
	int len = 0, size;
	struct print_entry *entry;
	unsigned int trace_ctx;
	char *tbuffer;

	if (tracing_disabled || tracing_selftest_running)
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	trace_ctx = tracing_gen_ctx();
	preempt_disable_notrace();


	tbuffer = get_trace_buf();
	if (!tbuffer) {
		len = 0;
		goto out_nobuffer;
	}

	len = vscnprintf(tbuffer, TRACE_BUF_SIZE, fmt, args);

	size = sizeof(*entry) + len + 1;
	ring_buffer_nest_start(buffer);
	event = __trace_buffer_lock_reserve(buffer, TRACE_PRINT, size,
					    trace_ctx);
	if (!event)
		goto out;
	entry = ring_buffer_event_data(event);
	entry->ip = ip;

	memcpy(&entry->buf, tbuffer, len + 1);
	if (!call_filter_check_discard(call, entry, buffer, event)) {
		__buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(&global_trace, buffer, trace_ctx, 6, NULL);
	}

out:
	ring_buffer_nest_end(buffer);
	put_trace_buf();

out_nobuffer:
	preempt_enable_notrace();
	unpause_graph_tracing();

	return len;
}

__printf(3, 0)
int trace_array_vprintk(struct trace_array *tr,
			unsigned long ip, const char *fmt, va_list args)
{
	return __trace_array_vprintk(tr->array_buffer.buffer, ip, fmt, args);
}

/**
 * trace_array_printk - Print a message to a specific instance
 * @tr: The instance trace_array descriptor
 * @ip: The instruction pointer that this is called from.
 * @fmt: The format to print (printf format)
 *
 * If a subsystem sets up its own instance, they have the right to
 * printk strings into their tracing instance buffer using this
 * function. Note, this function will not write into the top level
 * buffer (use trace_printk() for that), as writing into the top level
 * buffer should only have events that can be individually disabled.
 * trace_printk() is only used for debugging a kernel, and should not
 * be ever incorporated in normal use.
 *
 * trace_array_printk() can be used, as it will not add noise to the
 * top level tracing buffer.
 *
 * Note, trace_array_init_printk() must be called on @tr before this
 * can be used.
 */
__printf(3, 0)
int trace_array_printk(struct trace_array *tr,
		       unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!tr)
		return -ENOENT;

	/* This is only allowed for created instances */
	if (tr == &global_trace)
		return 0;

	if (!(tr->trace_flags & TRACE_ITER_PRINTK))
		return 0;

	va_start(ap, fmt);
	ret = trace_array_vprintk(tr, ip, fmt, ap);
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(trace_array_printk);

/**
 * trace_array_init_printk - Initialize buffers for trace_array_printk()
 * @tr: The trace array to initialize the buffers for
 *
 * As trace_array_printk() only writes into instances, they are OK to
 * have in the kernel (unlike trace_printk()). This needs to be called
 * before trace_array_printk() can be used on a trace_array.
 */
int trace_array_init_printk(struct trace_array *tr)
{
	if (!tr)
		return -ENOENT;

	/* This is only allowed for created instances */
	if (tr == &global_trace)
		return -EINVAL;

	return alloc_percpu_trace_buffer();
}
EXPORT_SYMBOL_GPL(trace_array_init_printk);

__printf(3, 4)
int trace_array_printk_buf(struct trace_buffer *buffer,
			   unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!(global_trace.trace_flags & TRACE_ITER_PRINTK))
		return 0;

	va_start(ap, fmt);
	ret = __trace_array_vprintk(buffer, ip, fmt, ap);
	va_end(ap);
	return ret;
}

__printf(2, 0)
int trace_vprintk(unsigned long ip, const char *fmt, va_list args)
{
	return trace_array_vprintk(&global_trace, ip, fmt, args);
}
EXPORT_SYMBOL_GPL(trace_vprintk);

static void trace_iterator_increment(struct trace_iterator *iter)
{
	struct ring_buffer_iter *buf_iter = trace_buffer_iter(iter, iter->cpu);

	iter->idx++;
	if (buf_iter)
		ring_buffer_iter_advance(buf_iter);
}

static struct trace_entry *
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts,
		unsigned long *lost_events)
{
	struct ring_buffer_event *event;
	struct ring_buffer_iter *buf_iter = trace_buffer_iter(iter, cpu);

	if (buf_iter) {
		event = ring_buffer_iter_peek(buf_iter, ts);
		if (lost_events)
			*lost_events = ring_buffer_iter_dropped(buf_iter) ?
				(unsigned long)-1 : 0;
	} else {
		event = ring_buffer_peek(iter->array_buffer->buffer, cpu, ts,
					 lost_events);
	}

	if (event) {
		iter->ent_size = ring_buffer_event_length(event);
		return ring_buffer_event_data(event);
	}
	iter->ent_size = 0;
	return NULL;
}

static struct trace_entry *
__find_next_entry(struct trace_iterator *iter, int *ent_cpu,
		  unsigned long *missing_events, u64 *ent_ts)
{
	struct trace_buffer *buffer = iter->array_buffer->buffer;
	struct trace_entry *ent, *next = NULL;
	unsigned long lost_events = 0, next_lost = 0;
	int cpu_file = iter->cpu_file;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int next_size = 0;
	int cpu;

	/*
	 * If we are in a per_cpu trace file, don't bother by iterating over
	 * all cpu and peek directly.
	 */
	if (cpu_file > RING_BUFFER_ALL_CPUS) {
		if (ring_buffer_empty_cpu(buffer, cpu_file))
			return NULL;
		ent = peek_next_entry(iter, cpu_file, ent_ts, missing_events);
		if (ent_cpu)
			*ent_cpu = cpu_file;

		return ent;
	}

	for_each_tracing_cpu(cpu) {

		if (ring_buffer_empty_cpu(buffer, cpu))
			continue;

		ent = peek_next_entry(iter, cpu, &ts, &lost_events);

		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
			next_lost = lost_events;
			next_size = iter->ent_size;
		}
	}

	iter->ent_size = next_size;

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	if (missing_events)
		*missing_events = next_lost;

	return next;
}

#define STATIC_FMT_BUF_SIZE	128
static char static_fmt_buf[STATIC_FMT_BUF_SIZE];

static char *trace_iter_expand_format(struct trace_iterator *iter)
{
	char *tmp;

	/*
	 * iter->tr is NULL when used with tp_printk, which makes
	 * this get called where it is not safe to call krealloc().
	 */
	if (!iter->tr || iter->fmt == static_fmt_buf)
		return NULL;

	tmp = krealloc(iter->fmt, iter->fmt_size + STATIC_FMT_BUF_SIZE,
		       GFP_KERNEL);
	if (tmp) {
		iter->fmt_size += STATIC_FMT_BUF_SIZE;
		iter->fmt = tmp;
	}

	return tmp;
}

/* Returns true if the string is safe to dereference from an event */
static bool trace_safe_str(struct trace_iterator *iter, const char *str,
			   bool star, int len)
{
	unsigned long addr = (unsigned long)str;
	struct trace_event *trace_event;
	struct trace_event_call *event;

	/* Ignore strings with no length */
	if (star && !len)
		return true;

	/* OK if part of the event data */
	if ((addr >= (unsigned long)iter->ent) &&
	    (addr < (unsigned long)iter->ent + iter->ent_size))
		return true;

	/* OK if part of the temp seq buffer */
	if ((addr >= (unsigned long)iter->tmp_seq.buffer) &&
	    (addr < (unsigned long)iter->tmp_seq.buffer + PAGE_SIZE))
		return true;

	/* Core rodata can not be freed */
	if (is_kernel_rodata(addr))
		return true;

	if (trace_is_tracepoint_string(str))
		return true;

	/*
	 * Now this could be a module event, referencing core module
	 * data, which is OK.
	 */
	if (!iter->ent)
		return false;

	trace_event = ftrace_find_event(iter->ent->type);
	if (!trace_event)
		return false;

	event = container_of(trace_event, struct trace_event_call, event);
	if ((event->flags & TRACE_EVENT_FL_DYNAMIC) || !event->module)
		return false;

	/* Would rather have rodata, but this will suffice */
	if (within_module_core(addr, event->module))
		return true;

	return false;
}

static const char *show_buffer(struct trace_seq *s)
{
	struct seq_buf *seq = &s->seq;

	seq_buf_terminate(seq);

	return seq->buffer;
}

static DEFINE_STATIC_KEY_FALSE(trace_no_verify);

static int test_can_verify_check(const char *fmt, ...)
{
	char buf[16];
	va_list ap;
	int ret;

	/*
	 * The verifier is dependent on vsnprintf() modifies the va_list
	 * passed to it, where it is sent as a reference. Some architectures
	 * (like x86_32) passes it by value, which means that vsnprintf()
	 * does not modify the va_list passed to it, and the verifier
	 * would then need to be able to understand all the values that
	 * vsnprintf can use. If it is passed by value, then the verifier
	 * is disabled.
	 */
	va_start(ap, fmt);
	vsnprintf(buf, 16, "%d", ap);
	ret = va_arg(ap, int);
	va_end(ap);

	return ret;
}

static void test_can_verify(void)
{
	if (!test_can_verify_check("%d %d", 0, 1)) {
		pr_info("trace event string verifier disabled\n");
		static_branch_inc(&trace_no_verify);
	}
}

/**
 * trace_check_vprintf - Check dereferenced strings while writing to the seq buffer
 * @iter: The iterator that holds the seq buffer and the event being printed
 * @fmt: The format used to print the event
 * @ap: The va_list holding the data to print from @fmt.
 *
 * This writes the data into the @iter->seq buffer using the data from
 * @fmt and @ap. If the format has a %s, then the source of the string
 * is examined to make sure it is safe to print, otherwise it will
 * warn and print "[UNSAFE MEMORY]" in place of the dereferenced string
 * pointer.
 */
void trace_check_vprintf(struct trace_iterator *iter, const char *fmt,
			 va_list ap)
{
	const char *p = fmt;
	const char *str;
	int i, j;

	if (WARN_ON_ONCE(!fmt))
		return;

	if (static_branch_unlikely(&trace_no_verify))
		goto print;

	/* Don't bother checking when doing a ftrace_dump() */
	if (iter->fmt == static_fmt_buf)
		goto print;

	while (*p) {
		bool star = false;
		int len = 0;

		j = 0;

		/* We only care about %s and variants */
		for (i = 0; p[i]; i++) {
			if (i + 1 >= iter->fmt_size) {
				/*
				 * If we can't expand the copy buffer,
				 * just print it.
				 */
				if (!trace_iter_expand_format(iter))
					goto print;
			}

			if (p[i] == '\\' && p[i+1]) {
				i++;
				continue;
			}
			if (p[i] == '%') {
				/* Need to test cases like %08.*s */
				for (j = 1; p[i+j]; j++) {
					if (isdigit(p[i+j]) ||
					    p[i+j] == '.')
						continue;
					if (p[i+j] == '*') {
						star = true;
						continue;
					}
					break;
				}
				if (p[i+j] == 's')
					break;
				star = false;
			}
			j = 0;
		}
		/* If no %s found then just print normally */
		if (!p[i])
			break;

		/* Copy up to the %s, and print that */
		strncpy(iter->fmt, p, i);
		iter->fmt[i] = '\0';
		trace_seq_vprintf(&iter->seq, iter->fmt, ap);

		/*
		 * If iter->seq is full, the above call no longer guarantees
		 * that ap is in sync with fmt processing, and further calls
		 * to va_arg() can return wrong positional arguments.
		 *
		 * Ensure that ap is no longer used in this case.
		 */
		if (iter->seq.full) {
			p = "";
			break;
		}

		if (star)
			len = va_arg(ap, int);

		/* The ap now points to the string data of the %s */
		str = va_arg(ap, const char *);

		/*
		 * If you hit this warning, it is likely that the
		 * trace event in question used %s on a string that
		 * was saved at the time of the event, but may not be
		 * around when the trace is read. Use __string(),
		 * __assign_str() and __get_str() helpers in the TRACE_EVENT()
		 * instead. See samples/trace_events/trace-events-sample.h
		 * for reference.
		 */
		if (WARN_ONCE(!trace_safe_str(iter, str, star, len),
			      "fmt: '%s' current_buffer: '%s'",
			      fmt, show_buffer(&iter->seq))) {
			int ret;

			/* Try to safely read the string */
			if (star) {
				if (len + 1 > iter->fmt_size)
					len = iter->fmt_size - 1;
				if (len < 0)
					len = 0;
				ret = copy_from_kernel_nofault(iter->fmt, str, len);
				iter->fmt[len] = 0;
				star = false;
			} else {
				ret = strncpy_from_kernel_nofault(iter->fmt, str,
								  iter->fmt_size);
			}
			if (ret < 0)
				trace_seq_printf(&iter->seq, "(0x%px)", str);
			else
				trace_seq_printf(&iter->seq, "(0x%px:%s)",
						 str, iter->fmt);
			str = "[UNSAFE-MEMORY]";
			strcpy(iter->fmt, "%s");
		} else {
			strncpy(iter->fmt, p + i, j + 1);
			iter->fmt[j+1] = '\0';
		}
		if (star)
			trace_seq_printf(&iter->seq, iter->fmt, len, str);
		else
			trace_seq_printf(&iter->seq, iter->fmt, str);

		p += i + j + 1;
	}
 print:
	if (*p)
		trace_seq_vprintf(&iter->seq, p, ap);
}

const char *trace_event_format(struct trace_iterator *iter, const char *fmt)
{
	const char *p, *new_fmt;
	char *q;

	if (WARN_ON_ONCE(!fmt))
		return fmt;

	if (!iter->tr || iter->tr->trace_flags & TRACE_ITER_HASH_PTR)
		return fmt;

	p = fmt;
	new_fmt = q = iter->fmt;
	while (*p) {
		if (unlikely(q - new_fmt + 3 > iter->fmt_size)) {
			if (!trace_iter_expand_format(iter))
				return fmt;

			q += iter->fmt - new_fmt;
			new_fmt = iter->fmt;
		}

		*q++ = *p++;

		/* Replace %p with %px */
		if (p[-1] == '%') {
			if (p[0] == '%') {
				*q++ = *p++;
			} else if (p[0] == 'p' && !isalnum(p[1])) {
				*q++ = *p++;
				*q++ = 'x';
			}
		}
	}
	*q = '\0';

	return new_fmt;
}

#define STATIC_TEMP_BUF_SIZE	128
static char static_temp_buf[STATIC_TEMP_BUF_SIZE] __aligned(4);

/* Find the next real entry, without updating the iterator itself */
struct trace_entry *trace_find_next_entry(struct trace_iterator *iter,
					  int *ent_cpu, u64 *ent_ts)
{
	/* __find_next_entry will reset ent_size */
	int ent_size = iter->ent_size;
	struct trace_entry *entry;

	/*
	 * If called from ftrace_dump(), then the iter->temp buffer
	 * will be the static_temp_buf and not created from kmalloc.
	 * If the entry size is greater than the buffer, we can
	 * not save it. Just return NULL in that case. This is only
	 * used to add markers when two consecutive events' time
	 * stamps have a large delta. See trace_print_lat_context()
	 */
	if (iter->temp == static_temp_buf &&
	    STATIC_TEMP_BUF_SIZE < ent_size)
		return NULL;

	/*
	 * The __find_next_entry() may call peek_next_entry(), which may
	 * call ring_buffer_peek() that may make the contents of iter->ent
	 * undefined. Need to copy iter->ent now.
	 */
	if (iter->ent && iter->ent != iter->temp) {
		if ((!iter->temp || iter->temp_size < iter->ent_size) &&
		    !WARN_ON_ONCE(iter->temp == static_temp_buf)) {
			void *temp;
			temp = kmalloc(iter->ent_size, GFP_KERNEL);
			if (!temp)
				return NULL;
			kfree(iter->temp);
			iter->temp = temp;
			iter->temp_size = iter->ent_size;
		}
		memcpy(iter->temp, iter->ent, iter->ent_size);
		iter->ent = iter->temp;
	}
	entry = __find_next_entry(iter, ent_cpu, NULL, ent_ts);
	/* Put back the original ent_size */
	iter->ent_size = ent_size;

	return entry;
}

/* Find the next real entry, and increment the iterator to the next entry */
void *trace_find_next_entry_inc(struct trace_iterator *iter)
{
	iter->ent = __find_next_entry(iter, &iter->cpu,
				      &iter->lost_events, &iter->ts);

	if (iter->ent)
		trace_iterator_increment(iter);

	return iter->ent ? iter : NULL;
}

static void trace_consume(struct trace_iterator *iter)
{
	ring_buffer_consume(iter->array_buffer->buffer, iter->cpu, &iter->ts,
			    &iter->lost_events);
}

static void *s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	int i = (int)*pos;
	void *ent;

	WARN_ON_ONCE(iter->leftover);

	(*pos)++;

	/* can't go backwards */
	if (iter->idx > i)
		return NULL;

	if (iter->idx < 0)
		ent = trace_find_next_entry_inc(iter);
	else
		ent = iter;

	while (ent && iter->idx < i)
		ent = trace_find_next_entry_inc(iter);

	iter->pos = *pos;

	return ent;
}

void tracing_iter_reset(struct trace_iterator *iter, int cpu)
{
	struct ring_buffer_iter *buf_iter;
	unsigned long entries = 0;
	u64 ts;

	per_cpu_ptr(iter->array_buffer->data, cpu)->skipped_entries = 0;

	buf_iter = trace_buffer_iter(iter, cpu);
	if (!buf_iter)
		return;

	ring_buffer_iter_reset(buf_iter);

	/*
	 * We could have the case with the max latency tracers
	 * that a reset never took place on a cpu. This is evident
	 * by the timestamp being before the start of the buffer.
	 */
	while (ring_buffer_iter_peek(buf_iter, &ts)) {
		if (ts >= iter->array_buffer->time_start)
			break;
		entries++;
		ring_buffer_iter_advance(buf_iter);
	}

	per_cpu_ptr(iter->array_buffer->data, cpu)->skipped_entries = entries;
}

/*
 * The current tracer is copied to avoid a global locking
 * all around.
 */
static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;
	int cpu_file = iter->cpu_file;
	void *p = NULL;
	loff_t l = 0;
	int cpu;

	/*
	 * copy the tracer to avoid using a global lock all around.
	 * iter->trace is a copy of current_trace, the pointer to the
	 * name may be used instead of a strcmp(), as iter->trace->name
	 * will point to the same string as current_trace->name.
	 */
	mutex_lock(&trace_types_lock);
	if (unlikely(tr->current_trace && iter->trace->name != tr->current_trace->name))
		*iter->trace = *tr->current_trace;
	mutex_unlock(&trace_types_lock);

#ifdef CONFIG_TRACER_MAX_TRACE
	if (iter->snapshot && iter->trace->use_max_tr)
		return ERR_PTR(-EBUSY);
#endif

	if (*pos != iter->pos) {
		iter->ent = NULL;
		iter->cpu = 0;
		iter->idx = -1;

		if (cpu_file == RING_BUFFER_ALL_CPUS) {
			for_each_tracing_cpu(cpu)
				tracing_iter_reset(iter, cpu);
		} else
			tracing_iter_reset(iter, cpu_file);

		iter->leftover = 0;
		for (p = iter; p && l < *pos; p = s_next(m, p, &l))
			;

	} else {
		/*
		 * If we overflowed the seq_file before, then we want
		 * to just reuse the trace_seq buffer again.
		 */
		if (iter->leftover)
			p = iter;
		else {
			l = *pos - 1;
			p = s_next(m, p, &l);
		}
	}

	trace_event_read_lock();
	trace_access_lock(cpu_file);
	return p;
}

static void s_stop(struct seq_file *m, void *p)
{
	struct trace_iterator *iter = m->private;

#ifdef CONFIG_TRACER_MAX_TRACE
	if (iter->snapshot && iter->trace->use_max_tr)
		return;
#endif

	trace_access_unlock(iter->cpu_file);
	trace_event_read_unlock();
}

static void
get_total_entries_cpu(struct array_buffer *buf, unsigned long *total,
		      unsigned long *entries, int cpu)
{
	unsigned long count;

	count = ring_buffer_entries_cpu(buf->buffer, cpu);
	/*
	 * If this buffer has skipped entries, then we hold all
	 * entries for the trace and we need to ignore the
	 * ones before the time stamp.
	 */
	if (per_cpu_ptr(buf->data, cpu)->skipped_entries) {
		count -= per_cpu_ptr(buf->data, cpu)->skipped_entries;
		/* total is the same as the entries */
		*total = count;
	} else
		*total = count +
			ring_buffer_overrun_cpu(buf->buffer, cpu);
	*entries = count;
}

static void
get_total_entries(struct array_buffer *buf,
		  unsigned long *total, unsigned long *entries)
{
	unsigned long t, e;
	int cpu;

	*total = 0;
	*entries = 0;

	for_each_tracing_cpu(cpu) {
		get_total_entries_cpu(buf, &t, &e, cpu);
		*total += t;
		*entries += e;
	}
}

unsigned long trace_total_entries_cpu(struct trace_array *tr, int cpu)
{
	unsigned long total, entries;

	if (!tr)
		tr = &global_trace;

	get_total_entries_cpu(&tr->array_buffer, &total, &entries, cpu);

	return entries;
}

unsigned long trace_total_entries(struct trace_array *tr)
{
	unsigned long total, entries;

	if (!tr)
		tr = &global_trace;

	get_total_entries(&tr->array_buffer, &total, &entries);

	return entries;
}

static void print_lat_help_header(struct seq_file *m)
{
	seq_puts(m, "#                    _------=> CPU#            \n"
		    "#                   / _-----=> irqs-off        \n"
		    "#                  | / _----=> need-resched    \n"
		    "#                  || / _---=> hardirq/softirq \n"
		    "#                  ||| / _--=> preempt-depth   \n"
		    "#                  |||| / _-=> migrate-disable \n"
		    "#                  ||||| /     delay           \n"
		    "#  cmd     pid     |||||| time  |   caller     \n"
		    "#     \\   /        ||||||  \\    |    /       \n");
}

static void print_event_info(struct array_buffer *buf, struct seq_file *m)
{
	unsigned long total;
	unsigned long entries;

	get_total_entries(buf, &total, &entries);
	seq_printf(m, "# entries-in-buffer/entries-written: %lu/%lu   #P:%d\n",
		   entries, total, num_online_cpus());
	seq_puts(m, "#\n");
}

static void print_func_help_header(struct array_buffer *buf, struct seq_file *m,
				   unsigned int flags)
{
	bool tgid = flags & TRACE_ITER_RECORD_TGID;

	print_event_info(buf, m);

	seq_printf(m, "#           TASK-PID    %s CPU#     TIMESTAMP  FUNCTION\n", tgid ? "   TGID   " : "");
	seq_printf(m, "#              | |      %s   |         |         |\n",      tgid ? "     |    " : "");
}

static void print_func_help_header_irq(struct array_buffer *buf, struct seq_file *m,
				       unsigned int flags)
{
	bool tgid = flags & TRACE_ITER_RECORD_TGID;
	const char *space = "            ";
	int prec = tgid ? 12 : 2;

	print_event_info(buf, m);

	seq_printf(m, "#                            %.*s  _-----=> irqs-off\n", prec, space);
	seq_printf(m, "#                            %.*s / _----=> need-resched\n", prec, space);
	seq_printf(m, "#                            %.*s| / _---=> hardirq/softirq\n", prec, space);
	seq_printf(m, "#                            %.*s|| / _--=> preempt-depth\n", prec, space);
	seq_printf(m, "#                            %.*s||| / _-=> migrate-disable\n", prec, space);
	seq_printf(m, "#                            %.*s|||| /     delay\n", prec, space);
	seq_printf(m, "#           TASK-PID  %.*s CPU#  |||||  TIMESTAMP  FUNCTION\n", prec, "     TGID   ");
	seq_printf(m, "#              | |    %.*s   |   |||||     |         |\n", prec, "       |    ");
}

void
print_trace_header(struct seq_file *m, struct trace_iterator *iter)
{
	unsigned long sym_flags = (global_trace.trace_flags & TRACE_ITER_SYM_MASK);
	struct array_buffer *buf = iter->array_buffer;
	struct trace_array_cpu *data = per_cpu_ptr(buf->data, buf->cpu);
	struct tracer *type = iter->trace;
	unsigned long entries;
	unsigned long total;
	const char *name = "preemption";

	name = type->name;

	get_total_entries(buf, &total, &entries);

	seq_printf(m, "# %s latency trace v1.1.5 on %s\n",
		   name, UTS_RELEASE);
	seq_puts(m, "# -----------------------------------"
		 "---------------------------------\n");
	seq_printf(m, "# latency: %lu us, #%lu/%lu, CPU#%d |"
		   " (M:%s VP:%d, KP:%d, SP:%d HP:%d",
		   nsecs_to_usecs(data->saved_latency),
		   entries,
		   total,
		   buf->cpu,
#if defined(CONFIG_PREEMPT_NONE)
		   "server",
#elif defined(CONFIG_PREEMPT_VOLUNTARY)
		   "desktop",
#elif defined(CONFIG_PREEMPT)
		   "preempt",
#elif defined(CONFIG_PREEMPT_RT)
		   "preempt_rt",
#else
		   "unknown",
#endif
		   /* These are reserved for later use */
		   0, 0, 0, 0);
#ifdef CONFIG_SMP
	seq_printf(m, " #P:%d)\n", num_online_cpus());
#else
	seq_puts(m, ")\n");
#endif
	seq_puts(m, "#    -----------------\n");
	seq_printf(m, "#    | task: %.16s-%d "
		   "(uid:%d nice:%ld policy:%ld rt_prio:%ld)\n",
		   data->comm, data->pid,
		   from_kuid_munged(seq_user_ns(m), data->uid), data->nice,
		   data->policy, data->rt_priority);
	seq_puts(m, "#    -----------------\n");

	if (data->critical_start) {
		seq_puts(m, "#  => started at: ");
		seq_print_ip_sym(&iter->seq, data->critical_start, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "\n#  => ended at:   ");
		seq_print_ip_sym(&iter->seq, data->critical_end, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "\n#\n");
	}

	seq_puts(m, "#\n");
}

static void test_cpu_buff_start(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_array *tr = iter->tr;

	if (!(tr->trace_flags & TRACE_ITER_ANNOTATE))
		return;

	if (!(iter->iter_flags & TRACE_FILE_ANNOTATE))
		return;

	if (cpumask_available(iter->started) &&
	    cpumask_test_cpu(iter->cpu, iter->started))
		return;

	if (per_cpu_ptr(iter->array_buffer->data, iter->cpu)->skipped_entries)
		return;

	if (cpumask_available(iter->started))
		cpumask_set_cpu(iter->cpu, iter->started);

	/* Don't print started cpu buffer for the first entry of the trace */
	if (iter->idx > 1)
		trace_seq_printf(s, "##### CPU %u buffer started ####\n",
				iter->cpu);
}

static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (tr->trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	test_cpu_buff_start(iter);

	event = ftrace_find_event(entry->type);

	if (tr->trace_flags & TRACE_ITER_CONTEXT_INFO) {
		if (iter->iter_flags & TRACE_FILE_LAT_FMT)
			trace_print_lat_context(iter);
		else
			trace_print_context(iter);
	}

	if (trace_seq_has_overflowed(s))
		return TRACE_TYPE_PARTIAL_LINE;

	if (event)
		return event->funcs->trace(iter, sym_flags, event);

	trace_seq_printf(s, "Unknown type %d\n", entry->type);

	return trace_handle_return(s);
}

static enum print_line_t print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (tr->trace_flags & TRACE_ITER_CONTEXT_INFO)
		trace_seq_printf(s, "%d %d %llu ",
				 entry->pid, iter->cpu, iter->ts);

	if (trace_seq_has_overflowed(s))
		return TRACE_TYPE_PARTIAL_LINE;

	event = ftrace_find_event(entry->type);
	if (event)
		return event->funcs->raw(iter, 0, event);

	trace_seq_printf(s, "%d ?\n", entry->type);

	return trace_handle_return(s);
}

static enum print_line_t print_hex_fmt(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	unsigned char newline = '\n';
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (tr->trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_HEX_FIELD(s, entry->pid);
		SEQ_PUT_HEX_FIELD(s, iter->cpu);
		SEQ_PUT_HEX_FIELD(s, iter->ts);
		if (trace_seq_has_overflowed(s))
			return TRACE_TYPE_PARTIAL_LINE;
	}

	event = ftrace_find_event(entry->type);
	if (event) {
		enum print_line_t ret = event->funcs->hex(iter, 0, event);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
	}

	SEQ_PUT_FIELD(s, newline);

	return trace_handle_return(s);
}

static enum print_line_t print_bin_fmt(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (tr->trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_FIELD(s, entry->pid);
		SEQ_PUT_FIELD(s, iter->cpu);
		SEQ_PUT_FIELD(s, iter->ts);
		if (trace_seq_has_overflowed(s))
			return TRACE_TYPE_PARTIAL_LINE;
	}

	event = ftrace_find_event(entry->type);
	return event ? event->funcs->binary(iter, 0, event) :
		TRACE_TYPE_HANDLED;
}

int trace_empty(struct trace_iterator *iter)
{
	struct ring_buffer_iter *buf_iter;
	int cpu;

	/* If we are looking at one CPU buffer, only check that one */
	if (iter->cpu_file != RING_BUFFER_ALL_CPUS) {
		cpu = iter->cpu_file;
		buf_iter = trace_buffer_iter(iter, cpu);
		if (buf_iter) {
			if (!ring_buffer_iter_empty(buf_iter))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->array_buffer->buffer, cpu))
				return 0;
		}
		return 1;
	}

	for_each_tracing_cpu(cpu) {
		buf_iter = trace_buffer_iter(iter, cpu);
		if (buf_iter) {
			if (!ring_buffer_iter_empty(buf_iter))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->array_buffer->buffer, cpu))
				return 0;
		}
	}

	return 1;
}

/*  Called with trace_event_read_lock() held. */
enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	struct trace_array *tr = iter->tr;
	unsigned long trace_flags = tr->trace_flags;
	enum print_line_t ret;

	if (iter->lost_events) {
		if (iter->lost_events == (unsigned long)-1)
			trace_seq_printf(&iter->seq, "CPU:%d [LOST EVENTS]\n",
					 iter->cpu);
		else
			trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
					 iter->cpu, iter->lost_events);
		if (trace_seq_has_overflowed(&iter->seq))
			return TRACE_TYPE_PARTIAL_LINE;
	}

	if (iter->trace && iter->trace->print_line) {
		ret = iter->trace->print_line(iter);
		if (ret != TRACE_TYPE_UNHANDLED)
			return ret;
	}

	if (iter->ent->type == TRACE_BPUTS &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return trace_print_bputs_msg_only(iter);

	if (iter->ent->type == TRACE_BPRINT &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return trace_print_bprintk_msg_only(iter);

	if (iter->ent->type == TRACE_PRINT &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return trace_print_printk_msg_only(iter);

	if (trace_flags & TRACE_ITER_BIN)
		return print_bin_fmt(iter);

	if (trace_flags & TRACE_ITER_HEX)
		return print_hex_fmt(iter);

	if (trace_flags & TRACE_ITER_RAW)
		return print_raw_fmt(iter);

	return print_trace_fmt(iter);
}

void trace_latency_header(struct seq_file *m)
{
	struct trace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;

	/* print nothing if the buffers are empty */
	if (trace_empty(iter))
		return;

	if (iter->iter_flags & TRACE_FILE_LAT_FMT)
		print_trace_header(m, iter);

	if (!(tr->trace_flags & TRACE_ITER_VERBOSE))
		print_lat_help_header(m);
}

void trace_default_header(struct seq_file *m)
{
	struct trace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;
	unsigned long trace_flags = tr->trace_flags;

	if (!(trace_flags & TRACE_ITER_CONTEXT_INFO))
		return;

	if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
		/* print nothing if the buffers are empty */
		if (trace_empty(iter))
			return;
		print_trace_header(m, iter);
		if (!(trace_flags & TRACE_ITER_VERBOSE))
			print_lat_help_header(m);
	} else {
		if (!(trace_flags & TRACE_ITER_VERBOSE)) {
			if (trace_flags & TRACE_ITER_IRQ_INFO)
				print_func_help_header_irq(iter->array_buffer,
							   m, trace_flags);
			else
				print_func_help_header(iter->array_buffer, m,
						       trace_flags);
		}
	}
}

static void test_ftrace_alive(struct seq_file *m)
{
	if (!ftrace_is_dead())
		return;
	seq_puts(m, "# WARNING: FUNCTION TRACING IS CORRUPTED\n"
		    "#          MAY BE MISSING FUNCTION EVENTS\n");
}

#ifdef CONFIG_TRACER_MAX_TRACE
static void show_snapshot_main_help(struct seq_file *m)
{
	seq_puts(m, "# echo 0 > snapshot : Clears and frees snapshot buffer\n"
		    "# echo 1 > snapshot : Allocates snapshot buffer, if not already allocated.\n"
		    "#                      Takes a snapshot of the main buffer.\n"
		    "# echo 2 > snapshot : Clears snapshot buffer (but does not allocate or free)\n"
		    "#                      (Doesn't have to be '2' works with any number that\n"
		    "#                       is not a '0' or '1')\n");
}

static void show_snapshot_percpu_help(struct seq_file *m)
{
	seq_puts(m, "# echo 0 > snapshot : Invalid for per_cpu snapshot file.\n");
#ifdef CONFIG_RING_BUFFER_ALLOW_SWAP
	seq_puts(m, "# echo 1 > snapshot : Allocates snapshot buffer, if not already allocated.\n"
		    "#                      Takes a snapshot of the main buffer for this cpu.\n");
#else
	seq_puts(m, "# echo 1 > snapshot : Not supported with this kernel.\n"
		    "#                     Must use main snapshot file to allocate.\n");
#endif
	seq_puts(m, "# echo 2 > snapshot : Clears this cpu's snapshot buffer (but does not allocate)\n"
		    "#                      (Doesn't have to be '2' works with any number that\n"
		    "#                       is not a '0' or '1')\n");
}

static void print_snapshot_help(struct seq_file *m, struct trace_iterator *iter)
{
	if (iter->tr->allocated_snapshot)
		seq_puts(m, "#\n# * Snapshot is allocated *\n#\n");
	else
		seq_puts(m, "#\n# * Snapshot is freed *\n#\n");

	seq_puts(m, "# Snapshot commands:\n");
	if (iter->cpu_file == RING_BUFFER_ALL_CPUS)
		show_snapshot_main_help(m);
	else
		show_snapshot_percpu_help(m);
}
#else
/* Should never be called */
static inline void print_snapshot_help(struct seq_file *m, struct trace_iterator *iter) { }
#endif

static int s_show(struct seq_file *m, void *v)
{
	struct trace_iterator *iter = v;
	int ret;

	if (iter->ent == NULL) {
		if (iter->tr) {
			seq_printf(m, "# tracer: %s\n", iter->trace->name);
			seq_puts(m, "#\n");
			test_ftrace_alive(m);
		}
		if (iter->snapshot && trace_empty(iter))
			print_snapshot_help(m, iter);
		else if (iter->trace && iter->trace->print_header)
			iter->trace->print_header(m);
		else
			trace_default_header(m);

	} else if (iter->leftover) {
		/*
		 * If we filled the seq_file buffer earlier, we
		 * want to just show it now.
		 */
		ret = trace_print_seq(m, &iter->seq);

		/* ret should this time be zero, but you never know */
		iter->leftover = ret;

	} else {
		print_trace_line(iter);
		ret = trace_print_seq(m, &iter->seq);
		/*
		 * If we overflow the seq_file buffer, then it will
		 * ask us for this data again at start up.
		 * Use that instead.
		 *  ret is 0 if seq_file write succeeded.
		 *        -1 otherwise.
		 */
		iter->leftover = ret;
	}

	return 0;
}

/*
 * Should be used after trace_array_get(), trace_types_lock
 * ensures that i_cdev was already initialized.
 */
static inline int tracing_get_cpu(struct inode *inode)
{
	if (inode->i_cdev) /* See trace_create_cpu_file() */
		return (long)inode->i_cdev - 1;
	return RING_BUFFER_ALL_CPUS;
}

static const struct seq_operations tracer_seq_ops = {
	.start		= s_start,
	.next		= s_next,
	.stop		= s_stop,
	.show		= s_show,
};

static struct trace_iterator *
__tracing_open(struct inode *inode, struct file *file, bool snapshot)
{
	struct trace_array *tr = inode->i_private;
	struct trace_iterator *iter;
	int cpu;

	if (tracing_disabled)
		return ERR_PTR(-ENODEV);

	iter = __seq_open_private(file, &tracer_seq_ops, sizeof(*iter));
	if (!iter)
		return ERR_PTR(-ENOMEM);

	iter->buffer_iter = kcalloc(nr_cpu_ids, sizeof(*iter->buffer_iter),
				    GFP_KERNEL);
	if (!iter->buffer_iter)
		goto release;

	/*
	 * trace_find_next_entry() may need to save off iter->ent.
	 * It will place it into the iter->temp buffer. As most
	 * events are less than 128, allocate a buffer of that size.
	 * If one is greater, then trace_find_next_entry() will
	 * allocate a new buffer to adjust for the bigger iter->ent.
	 * It's not critical if it fails to get allocated here.
	 */
	iter->temp = kmalloc(128, GFP_KERNEL);
	if (iter->temp)
		iter->temp_size = 128;

	/*
	 * trace_event_printf() may need to modify given format
	 * string to replace %p with %px so that it shows real address
	 * instead of hash value. However, that is only for the event
	 * tracing, other tracer may not need. Defer the allocation
	 * until it is needed.
	 */
	iter->fmt = NULL;
	iter->fmt_size = 0;

	/*
	 * We make a copy of the current tracer to avoid concurrent
	 * changes on it while we are reading.
	 */
	mutex_lock(&trace_types_lock);
	iter->trace = kzalloc(sizeof(*iter->trace), GFP_KERNEL);
	if (!iter->trace)
		goto fail;

	*iter->trace = *tr->current_trace;

	if (!zalloc_cpumask_var(&iter->started, GFP_KERNEL))
		goto fail;

	iter->tr = tr;

#ifdef CONFIG_TRACER_MAX_TRACE
	/* Currently only the top directory has a snapshot */
	if (tr->current_trace->print_max || snapshot)
		iter->array_buffer = &tr->max_buffer;
	else
#endif
		iter->array_buffer = &tr->array_buffer;
	iter->snapshot = snapshot;
	iter->pos = -1;
	iter->cpu_file = tracing_get_cpu(inode);
	mutex_init(&iter->mutex);

	/* Notify the tracer early; before we stop tracing. */
	if (iter->trace->open)
		iter->trace->open(iter);

	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter->array_buffer->buffer))
		iter->iter_flags |= TRACE_FILE_ANNOTATE;

	/* Output in nanoseconds only if we are using a clock in nanoseconds. */
	if (trace_clocks[tr->clock_id].in_ns)
		iter->iter_flags |= TRACE_FILE_TIME_IN_NS;

	/*
	 * If pause-on-trace is enabled, then stop the trace while
	 * dumping, unless this is the "snapshot" file
	 */
	if (!iter->snapshot && (tr->trace_flags & TRACE_ITER_PAUSE_ON_TRACE))
		tracing_stop_tr(tr);

	if (iter->cpu_file == RING_BUFFER_ALL_CPUS) {
		for_each_tracing_cpu(cpu) {
			iter->buffer_iter[cpu] =
				ring_buffer_read_prepare(iter->array_buffer->buffer,
							 cpu, GFP_KERNEL);
		}
		ring_buffer_read_prepare_sync();
		for_each_tracing_cpu(cpu) {
			ring_buffer_read_start(iter->buffer_iter[cpu]);
			tracing_iter_reset(iter, cpu);
		}
	} else {
		cpu = iter->cpu_file;
		iter->buffer_iter[cpu] =
			ring_buffer_read_prepare(iter->array_buffer->buffer,
						 cpu, GFP_KERNEL);
		ring_buffer_read_prepare_sync();
		ring_buffer_read_start(iter->buffer_iter[cpu]);
		tracing_iter_reset(iter, cpu);
	}

	mutex_unlock(&trace_types_lock);

	return iter;

 fail:
	mutex_unlock(&trace_types_lock);
	kfree(iter->trace);
	kfree(iter->temp);
	kfree(iter->buffer_iter);
release:
	seq_release_private(inode, file);
	return ERR_PTR(-ENOMEM);
}

int tracing_open_generic(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	filp->private_data = inode->i_private;
	return 0;
}

bool tracing_is_disabled(void)
{
	return (tracing_disabled) ? true: false;
}

/*
 * Open and update trace_array ref count.
 * Must have the current trace_array passed to it.
 */
int tracing_open_generic_tr(struct inode *inode, struct file *filp)
{
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	filp->private_data = inode->i_private;

	return 0;
}

static int tracing_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	struct seq_file *m = file->private_data;
	struct trace_iterator *iter;
	int cpu;

	if (!(file->f_mode & FMODE_READ)) {
		trace_array_put(tr);
		return 0;
	}

	/* Writes do not use seq_file */
	iter = m->private;
	mutex_lock(&trace_types_lock);

	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}

	if (iter->trace && iter->trace->close)
		iter->trace->close(iter);

	if (!iter->snapshot && tr->stop_count)
		/* reenable tracing if it was previously enabled */
		tracing_start_tr(tr);

	__trace_array_put(tr);

	mutex_unlock(&trace_types_lock);

	mutex_destroy(&iter->mutex);
	free_cpumask_var(iter->started);
	kfree(iter->fmt);
	kfree(iter->temp);
	kfree(iter->trace);
	kfree(iter->buffer_iter);
	seq_release_private(inode, file);

	return 0;
}

static int tracing_release_generic_tr(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);
	return 0;
}

static int tracing_single_release_tr(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);

	return single_release(inode, file);
}

static int tracing_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	struct trace_iterator *iter;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	/* If this file was open for write, then erase contents */
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		int cpu = tracing_get_cpu(inode);
		struct array_buffer *trace_buf = &tr->array_buffer;

#ifdef CONFIG_TRACER_MAX_TRACE
		if (tr->current_trace->print_max)
			trace_buf = &tr->max_buffer;
#endif

		if (cpu == RING_BUFFER_ALL_CPUS)
			tracing_reset_online_cpus(trace_buf);
		else
			tracing_reset_cpu(trace_buf, cpu);
	}

	if (file->f_mode & FMODE_READ) {
		iter = __tracing_open(inode, file, false);
		if (IS_ERR(iter))
			ret = PTR_ERR(iter);
		else if (tr->trace_flags & TRACE_ITER_LATENCY_FMT)
			iter->iter_flags |= TRACE_FILE_LAT_FMT;
	}

	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

/*
 * Some tracers are not suitable for instance buffers.
 * A tracer is always available for the global array (toplevel)
 * or if it explicitly states that it is.
 */
static bool
trace_ok_for_array(struct tracer *t, struct trace_array *tr)
{
	return (tr->flags & TRACE_ARRAY_FL_GLOBAL) || t->allow_instances;
}

/* Find the next tracer that this trace array may use */
static struct tracer *
get_tracer_for_array(struct trace_array *tr, struct tracer *t)
{
	while (t && !trace_ok_for_array(t, tr))
		t = t->next;

	return t;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_array *tr = m->private;
	struct tracer *t = v;

	(*pos)++;

	if (t)
		t = get_tracer_for_array(tr, t->next);

	return t;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct trace_array *tr = m->private;
	struct tracer *t;
	loff_t l = 0;

	mutex_lock(&trace_types_lock);

	t = get_tracer_for_array(tr, trace_types);
	for (; t && l < *pos; t = t_next(m, t, &l))
			;

	return t;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&trace_types_lock);
}

static int t_show(struct seq_file *m, void *v)
{
	struct tracer *t = v;

	if (!t)
		return 0;

	seq_puts(m, t->name);
	if (t->next)
		seq_putc(m, ' ');
	else
		seq_putc(m, '\n');

	return 0;
}

static const struct seq_operations show_traces_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int show_traces_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	struct seq_file *m;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	ret = seq_open(file, &show_traces_seq_ops);
	if (ret) {
		trace_array_put(tr);
		return ret;
	}

	m = file->private_data;
	m->private = tr;

	return 0;
}

static int show_traces_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);
	return seq_release(inode, file);
}

static ssize_t
tracing_write_stub(struct file *filp, const char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	return count;
}

loff_t tracing_lseek(struct file *file, loff_t offset, int whence)
{
	int ret;

	if (file->f_mode & FMODE_READ)
		ret = seq_lseek(file, offset, whence);
	else
		file->f_pos = ret = 0;

	return ret;
}

static const struct file_operations tracing_fops = {
	.open		= tracing_open,
	.read		= seq_read,
	.write		= tracing_write_stub,
	.llseek		= tracing_lseek,
	.release	= tracing_release,
};

static const struct file_operations show_traces_fops = {
	.open		= show_traces_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= show_traces_release,
};

static ssize_t
tracing_cpumask_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct trace_array *tr = file_inode(filp)->i_private;
	char *mask_str;
	int len;

	len = snprintf(NULL, 0, "%*pb\n",
		       cpumask_pr_args(tr->tracing_cpumask)) + 1;
	mask_str = kmalloc(len, GFP_KERNEL);
	if (!mask_str)
		return -ENOMEM;

	len = snprintf(mask_str, len, "%*pb\n",
		       cpumask_pr_args(tr->tracing_cpumask));
	if (len >= count) {
		count = -EINVAL;
		goto out_err;
	}
	count = simple_read_from_buffer(ubuf, count, ppos, mask_str, len);

out_err:
	kfree(mask_str);

	return count;
}

int tracing_set_cpumask(struct trace_array *tr,
			cpumask_var_t tracing_cpumask_new)
{
	int cpu;

	if (!tr)
		return -EINVAL;

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpumask_test_cpu(cpu, tr->tracing_cpumask) &&
				!cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_inc(&per_cpu_ptr(tr->array_buffer.data, cpu)->disabled);
			ring_buffer_record_disable_cpu(tr->array_buffer.buffer, cpu);
		}
		if (!cpumask_test_cpu(cpu, tr->tracing_cpumask) &&
				cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_dec(&per_cpu_ptr(tr->array_buffer.data, cpu)->disabled);
			ring_buffer_record_enable_cpu(tr->array_buffer.buffer, cpu);
		}
	}
	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();

	cpumask_copy(tr->tracing_cpumask, tracing_cpumask_new);

	return 0;
}

static ssize_t
tracing_cpumask_write(struct file *filp, const char __user *ubuf,
		      size_t count, loff_t *ppos)
{
	struct trace_array *tr = file_inode(filp)->i_private;
	cpumask_var_t tracing_cpumask_new;
	int err;

	if (!zalloc_cpumask_var(&tracing_cpumask_new, GFP_KERNEL))
		return -ENOMEM;

	err = cpumask_parse_user(ubuf, count, tracing_cpumask_new);
	if (err)
		goto err_free;

	err = tracing_set_cpumask(tr, tracing_cpumask_new);
	if (err)
		goto err_free;

	free_cpumask_var(tracing_cpumask_new);

	return count;

err_free:
	free_cpumask_var(tracing_cpumask_new);

	return err;
}

static const struct file_operations tracing_cpumask_fops = {
	.open		= tracing_open_generic_tr,
	.read		= tracing_cpumask_read,
	.write		= tracing_cpumask_write,
	.release	= tracing_release_generic_tr,
	.llseek		= generic_file_llseek,
};

static int tracing_trace_options_show(struct seq_file *m, void *v)
{
	struct tracer_opt *trace_opts;
	struct trace_array *tr = m->private;
	u32 tracer_flags;
	int i;

	mutex_lock(&trace_types_lock);
	tracer_flags = tr->current_trace->flags->val;
	trace_opts = tr->current_trace->flags->opts;

	for (i = 0; trace_options[i]; i++) {
		if (tr->trace_flags & (1 << i))
			seq_printf(m, "%s\n", trace_options[i]);
		else
			seq_printf(m, "no%s\n", trace_options[i]);
	}

	for (i = 0; trace_opts[i].name; i++) {
		if (tracer_flags & trace_opts[i].bit)
			seq_printf(m, "%s\n", trace_opts[i].name);
		else
			seq_printf(m, "no%s\n", trace_opts[i].name);
	}
	mutex_unlock(&trace_types_lock);

	return 0;
}

static int __set_tracer_option(struct trace_array *tr,
			       struct tracer_flags *tracer_flags,
			       struct tracer_opt *opts, int neg)
{
	struct tracer *trace = tracer_flags->trace;
	int ret;

	ret = trace->set_flag(tr, tracer_flags->val, opts->bit, !neg);
	if (ret)
		return ret;

	if (neg)
		tracer_flags->val &= ~opts->bit;
	else
		tracer_flags->val |= opts->bit;
	return 0;
}

/* Try to assign a tracer specific option */
static int set_tracer_option(struct trace_array *tr, char *cmp, int neg)
{
	struct tracer *trace = tr->current_trace;
	struct tracer_flags *tracer_flags = trace->flags;
	struct tracer_opt *opts = NULL;
	int i;

	for (i = 0; tracer_flags->opts[i].name; i++) {
		opts = &tracer_flags->opts[i];

		if (strcmp(cmp, opts->name) == 0)
			return __set_tracer_option(tr, trace->flags, opts, neg);
	}

	return -EINVAL;
}

/* Some tracers require overwrite to stay enabled */
int trace_keep_overwrite(struct tracer *tracer, u32 mask, int set)
{
	if (tracer->enabled && (mask & TRACE_ITER_OVERWRITE) && !set)
		return -1;

	return 0;
}

int set_tracer_flag(struct trace_array *tr, unsigned int mask, int enabled)
{
	int *map;

	if ((mask == TRACE_ITER_RECORD_TGID) ||
	    (mask == TRACE_ITER_RECORD_CMD))
		lockdep_assert_held(&event_mutex);

	/* do nothing if flag is already set */
	if (!!(tr->trace_flags & mask) == !!enabled)
		return 0;

	/* Give the tracer a chance to approve the change */
	if (tr->current_trace->flag_changed)
		if (tr->current_trace->flag_changed(tr, mask, !!enabled))
			return -EINVAL;

	if (enabled)
		tr->trace_flags |= mask;
	else
		tr->trace_flags &= ~mask;

	if (mask == TRACE_ITER_RECORD_CMD)
		trace_event_enable_cmd_record(enabled);

	if (mask == TRACE_ITER_RECORD_TGID) {
		if (!tgid_map) {
			tgid_map_max = pid_max;
			map = kvcalloc(tgid_map_max + 1, sizeof(*tgid_map),
				       GFP_KERNEL);

			/*
			 * Pairs with smp_load_acquire() in
			 * trace_find_tgid_ptr() to ensure that if it observes
			 * the tgid_map we just allocated then it also observes
			 * the corresponding tgid_map_max value.
			 */
			smp_store_release(&tgid_map, map);
		}
		if (!tgid_map) {
			tr->trace_flags &= ~TRACE_ITER_RECORD_TGID;
			return -ENOMEM;
		}

		trace_event_enable_tgid_record(enabled);
	}

	if (mask == TRACE_ITER_EVENT_FORK)
		trace_event_follow_fork(tr, enabled);

	if (mask == TRACE_ITER_FUNC_FORK)
		ftrace_pid_follow_fork(tr, enabled);

	if (mask == TRACE_ITER_OVERWRITE) {
		ring_buffer_change_overwrite(tr->array_buffer.buffer, enabled);
#ifdef CONFIG_TRACER_MAX_TRACE
		ring_buffer_change_overwrite(tr->max_buffer.buffer, enabled);
#endif
	}

	if (mask == TRACE_ITER_PRINTK) {
		trace_printk_start_stop_comm(enabled);
		trace_printk_control(enabled);
	}

	return 0;
}

int trace_set_options(struct trace_array *tr, char *option)
{
	char *cmp;
	int neg = 0;
	int ret;
	size_t orig_len = strlen(option);
	int len;

	cmp = strstrip(option);

	len = str_has_prefix(cmp, "no");
	if (len)
		neg = 1;

	cmp += len;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	ret = match_string(trace_options, -1, cmp);
	/* If no option could be set, test the specific tracer options */
	if (ret < 0)
		ret = set_tracer_option(tr, cmp, neg);
	else
		ret = set_tracer_flag(tr, 1 << ret, !neg);

	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	/*
	 * If the first trailing whitespace is replaced with '\0' by strstrip,
	 * turn it back into a space.
	 */
	if (orig_len > strlen(option))
		option[strlen(option)] = ' ';

	return ret;
}

static void __init apply_trace_boot_options(void)
{
	char *buf = trace_boot_options_buf;
	char *option;

	while (true) {
		option = strsep(&buf, ",");

		if (!option)
			break;

		if (*option)
			trace_set_options(&global_trace, option);

		/* Put back the comma to allow this to be called again */
		if (buf)
			*(buf - 1) = ',';
	}
}

static ssize_t
tracing_trace_options_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct trace_array *tr = m->private;
	char buf[64];
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = trace_set_options(tr, buf);
	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

static int tracing_trace_options_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	ret = single_open(file, tracing_trace_options_show, inode->i_private);
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

static const struct file_operations tracing_iter_fops = {
	.open		= tracing_trace_options_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_single_release_tr,
	.write		= tracing_trace_options_write,
};

static const char readme_msg[] =
	"tracing mini-HOWTO:\n\n"
	"# echo 0 > tracing_on : quick way to disable tracing\n"
	"# echo 1 > tracing_on : quick way to re-enable tracing\n\n"
	" Important files:\n"
	"  trace\t\t\t- The static contents of the buffer\n"
	"\t\t\t  To clear the buffer write into this file: echo > trace\n"
	"  trace_pipe\t\t- A consuming read to see the contents of the buffer\n"
	"  current_tracer\t- function and latency tracers\n"
	"  available_tracers\t- list of configured tracers for current_tracer\n"
	"  error_log\t- error log for failed commands (that support it)\n"
	"  buffer_size_kb\t- view and modify size of per cpu buffer\n"
	"  buffer_total_size_kb  - view total size of all cpu buffers\n\n"
	"  trace_clock\t\t-change the clock used to order events\n"
	"       local:   Per cpu clock but may not be synced across CPUs\n"
	"      global:   Synced across CPUs but slows tracing down.\n"
	"     counter:   Not a clock, but just an increment\n"
	"      uptime:   Jiffy counter from time of boot\n"
	"        perf:   Same clock that perf events use\n"
#ifdef CONFIG_X86_64
	"     x86-tsc:   TSC cycle counter\n"
#endif
	"\n  timestamp_mode\t-view the mode used to timestamp events\n"
	"       delta:   Delta difference against a buffer-wide timestamp\n"
	"    absolute:   Absolute (standalone) timestamp\n"
	"\n  trace_marker\t\t- Writes into this file writes into the kernel buffer\n"
	"\n  trace_marker_raw\t\t- Writes into this file writes binary data into the kernel buffer\n"
	"  tracing_cpumask\t- Limit which CPUs to trace\n"
	"  instances\t\t- Make sub-buffers with: mkdir instances/foo\n"
	"\t\t\t  Remove sub-buffer with rmdir\n"
	"  trace_options\t\t- Set format or modify how tracing happens\n"
	"\t\t\t  Disable an option by prefixing 'no' to the\n"
	"\t\t\t  option name\n"
	"  saved_cmdlines_size\t- echo command number in here to store comm-pid list\n"
#ifdef CONFIG_DYNAMIC_FTRACE
	"\n  available_filter_functions - list of functions that can be filtered on\n"
	"  set_ftrace_filter\t- echo function name in here to only trace these\n"
	"\t\t\t  functions\n"
	"\t     accepts: func_full_name or glob-matching-pattern\n"
	"\t     modules: Can select a group via module\n"
	"\t      Format: :mod:<module-name>\n"
	"\t     example: echo :mod:ext3 > set_ftrace_filter\n"
	"\t    triggers: a command to perform when function is hit\n"
	"\t      Format: <function>:<trigger>[:count]\n"
	"\t     trigger: traceon, traceoff\n"
	"\t\t      enable_event:<system>:<event>\n"
	"\t\t      disable_event:<system>:<event>\n"
#ifdef CONFIG_STACKTRACE
	"\t\t      stacktrace\n"
#endif
#ifdef CONFIG_TRACER_SNAPSHOT
	"\t\t      snapshot\n"
#endif
	"\t\t      dump\n"
	"\t\t      cpudump\n"
	"\t     example: echo do_fault:traceoff > set_ftrace_filter\n"
	"\t              echo do_trap:traceoff:3 > set_ftrace_filter\n"
	"\t     The first one will disable tracing every time do_fault is hit\n"
	"\t     The second will disable tracing at most 3 times when do_trap is hit\n"
	"\t       The first time do trap is hit and it disables tracing, the\n"
	"\t       counter will decrement to 2. If tracing is already disabled,\n"
	"\t       the counter will not decrement. It only decrements when the\n"
	"\t       trigger did work\n"
	"\t     To remove trigger without count:\n"
	"\t       echo '!<function>:<trigger> > set_ftrace_filter\n"
	"\t     To remove trigger with a count:\n"
	"\t       echo '!<function>:<trigger>:0 > set_ftrace_filter\n"
	"  set_ftrace_notrace\t- echo function name in here to never trace.\n"
	"\t    accepts: func_full_name, *func_end, func_begin*, *func_middle*\n"
	"\t    modules: Can select a group via module command :mod:\n"
	"\t    Does not accept triggers\n"
#endif /* CONFIG_DYNAMIC_FTRACE */
#ifdef CONFIG_FUNCTION_TRACER
	"  set_ftrace_pid\t- Write pid(s) to only function trace those pids\n"
	"\t\t    (function)\n"
	"  set_ftrace_notrace_pid\t- Write pid(s) to not function trace those pids\n"
	"\t\t    (function)\n"
#endif
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	"  set_graph_function\t- Trace the nested calls of a function (function_graph)\n"
	"  set_graph_notrace\t- Do not trace the nested calls of a function (function_graph)\n"
	"  max_graph_depth\t- Trace a limited depth of nested calls (0 is unlimited)\n"
#endif
#ifdef CONFIG_TRACER_SNAPSHOT
	"\n  snapshot\t\t- Like 'trace' but shows the content of the static\n"
	"\t\t\t  snapshot buffer. Read the contents for more\n"
	"\t\t\t  information\n"
#endif
#ifdef CONFIG_STACK_TRACER
	"  stack_trace\t\t- Shows the max stack trace when active\n"
	"  stack_max_size\t- Shows current max stack size that was traced\n"
	"\t\t\t  Write into this file to reset the max size (trigger a\n"
	"\t\t\t  new trace)\n"
#ifdef CONFIG_DYNAMIC_FTRACE
	"  stack_trace_filter\t- Like set_ftrace_filter but limits what stack_trace\n"
	"\t\t\t  traces\n"
#endif
#endif /* CONFIG_STACK_TRACER */
#ifdef CONFIG_DYNAMIC_EVENTS
	"  dynamic_events\t\t- Create/append/remove/show the generic dynamic events\n"
	"\t\t\t  Write into this file to define/undefine new trace events.\n"
#endif
#ifdef CONFIG_KPROBE_EVENTS
	"  kprobe_events\t\t- Create/append/remove/show the kernel dynamic events\n"
	"\t\t\t  Write into this file to define/undefine new trace events.\n"
#endif
#ifdef CONFIG_UPROBE_EVENTS
	"  uprobe_events\t\t- Create/append/remove/show the userspace dynamic events\n"
	"\t\t\t  Write into this file to define/undefine new trace events.\n"
#endif
#if defined(CONFIG_KPROBE_EVENTS) || defined(CONFIG_UPROBE_EVENTS)
	"\t  accepts: event-definitions (one definition per line)\n"
	"\t   Format: p[:[<group>/]<event>] <place> [<args>]\n"
	"\t           r[maxactive][:[<group>/]<event>] <place> [<args>]\n"
#ifdef CONFIG_HIST_TRIGGERS
	"\t           s:[synthetic/]<event> <field> [<field>]\n"
#endif
	"\t           e[:[<group>/]<event>] <attached-group>.<attached-event> [<args>]\n"
	"\t           -:[<group>/]<event>\n"
#ifdef CONFIG_KPROBE_EVENTS
	"\t    place: [<module>:]<symbol>[+<offset>]|<memaddr>\n"
  "place (kretprobe): [<module>:]<symbol>[+<offset>]%return|<memaddr>\n"
#endif
#ifdef CONFIG_UPROBE_EVENTS
  "   place (uprobe): <path>:<offset>[%return][(ref_ctr_offset)]\n"
#endif
	"\t     args: <name>=fetcharg[:type]\n"
	"\t fetcharg: (%<register>|$<efield>), @<address>, @<symbol>[+|-<offset>],\n"
#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	"\t           $stack<index>, $stack, $retval, $comm, $arg<N>,\n"
#else
	"\t           $stack<index>, $stack, $retval, $comm,\n"
#endif
	"\t           +|-[u]<offset>(<fetcharg>), \\imm-value, \\\"imm-string\"\n"
	"\t     type: s8/16/32/64, u8/16/32/64, x8/16/32/64, string, symbol,\n"
	"\t           b<bit-width>@<bit-offset>/<container-size>, ustring,\n"
	"\t           <type>\\[<array-size>\\]\n"
#ifdef CONFIG_HIST_TRIGGERS
	"\t    field: <stype> <name>;\n"
	"\t    stype: u8/u16/u32/u64, s8/s16/s32/s64, pid_t,\n"
	"\t           [unsigned] char/int/long\n"
#endif
	"\t    efield: For event probes ('e' types), the field is on of the fields\n"
	"\t            of the <attached-group>/<attached-event>.\n"
#endif
	"  events/\t\t- Directory containing all trace event subsystems:\n"
	"      enable\t\t- Write 0/1 to enable/disable tracing of all events\n"
	"  events/<system>/\t- Directory containing all trace events for <system>:\n"
	"      enable\t\t- Write 0/1 to enable/disable tracing of all <system>\n"
	"\t\t\t  events\n"
	"      filter\t\t- If set, only events passing filter are traced\n"
	"  events/<system>/<event>/\t- Directory containing control files for\n"
	"\t\t\t  <event>:\n"
	"      enable\t\t- Write 0/1 to enable/disable tracing of <event>\n"
	"      filter\t\t- If set, only events passing filter are traced\n"
	"      trigger\t\t- If set, a command to perform when event is hit\n"
	"\t    Format: <trigger>[:count][if <filter>]\n"
	"\t   trigger: traceon, traceoff\n"
	"\t            enable_event:<system>:<event>\n"
	"\t            disable_event:<system>:<event>\n"
#ifdef CONFIG_HIST_TRIGGERS
	"\t            enable_hist:<system>:<event>\n"
	"\t            disable_hist:<system>:<event>\n"
#endif
#ifdef CONFIG_STACKTRACE
	"\t\t    stacktrace\n"
#endif
#ifdef CONFIG_TRACER_SNAPSHOT
	"\t\t    snapshot\n"
#endif
#ifdef CONFIG_HIST_TRIGGERS
	"\t\t    hist (see below)\n"
#endif
	"\t   example: echo traceoff > events/block/block_unplug/trigger\n"
	"\t            echo traceoff:3 > events/block/block_unplug/trigger\n"
	"\t            echo 'enable_event:kmem:kmalloc:3 if nr_rq > 1' > \\\n"
	"\t                  events/block/block_unplug/trigger\n"
	"\t   The first disables tracing every time block_unplug is hit.\n"
	"\t   The second disables tracing the first 3 times block_unplug is hit.\n"
	"\t   The third enables the kmalloc event the first 3 times block_unplug\n"
	"\t     is hit and has value of greater than 1 for the 'nr_rq' event field.\n"
	"\t   Like function triggers, the counter is only decremented if it\n"
	"\t    enabled or disabled tracing.\n"
	"\t   To remove a trigger without a count:\n"
	"\t     echo '!<trigger> > <system>/<event>/trigger\n"
	"\t   To remove a trigger with a count:\n"
	"\t     echo '!<trigger>:0 > <system>/<event>/trigger\n"
	"\t   Filters can be ignored when removing a trigger.\n"
#ifdef CONFIG_HIST_TRIGGERS
	"      hist trigger\t- If set, event hits are aggregated into a hash table\n"
	"\t    Format: hist:keys=<field1[,field2,...]>\n"
	"\t            [:values=<field1[,field2,...]>]\n"
	"\t            [:sort=<field1[,field2,...]>]\n"
	"\t            [:size=#entries]\n"
	"\t            [:pause][:continue][:clear]\n"
	"\t            [:name=histname1]\n"
	"\t            [:<handler>.<action>]\n"
	"\t            [if <filter>]\n\n"
	"\t    Note, special fields can be used as well:\n"
	"\t            common_timestamp - to record current timestamp\n"
	"\t            common_cpu - to record the CPU the event happened on\n"
	"\n"
	"\t    When a matching event is hit, an entry is added to a hash\n"
	"\t    table using the key(s) and value(s) named, and the value of a\n"
	"\t    sum called 'hitcount' is incremented.  Keys and values\n"
	"\t    correspond to fields in the event's format description.  Keys\n"
	"\t    can be any field, or the special string 'stacktrace'.\n"
	"\t    Compound keys consisting of up to two fields can be specified\n"
	"\t    by the 'keys' keyword.  Values must correspond to numeric\n"
	"\t    fields.  Sort keys consisting of up to two fields can be\n"
	"\t    specified using the 'sort' keyword.  The sort direction can\n"
	"\t    be modified by appending '.descending' or '.ascending' to a\n"
	"\t    sort field.  The 'size' parameter can be used to specify more\n"
	"\t    or fewer than the default 2048 entries for the hashtable size.\n"
	"\t    If a hist trigger is given a name using the 'name' parameter,\n"
	"\t    its histogram data will be shared with other triggers of the\n"
	"\t    same name, and trigger hits will update this common data.\n\n"
	"\t    Reading the 'hist' file for the event will dump the hash\n"
	"\t    table in its entirety to stdout.  If there are multiple hist\n"
	"\t    triggers attached to an event, there will be a table for each\n"
	"\t    trigger in the output.  The table displayed for a named\n"
	"\t    trigger will be the same as any other instance having the\n"
	"\t    same name.  The default format used to display a given field\n"
	"\t    can be modified by appending any of the following modifiers\n"
	"\t    to the field name, as applicable:\n\n"
	"\t            .hex        display a number as a hex value\n"
	"\t            .sym        display an address as a symbol\n"
	"\t            .sym-offset display an address as a symbol and offset\n"
	"\t            .execname   display a common_pid as a program name\n"
	"\t            .syscall    display a syscall id as a syscall name\n"
	"\t            .log2       display log2 value rather than raw number\n"
	"\t            .buckets=size  display values in groups of size rather than raw number\n"
	"\t            .usecs      display a common_timestamp in microseconds\n\n"
	"\t    The 'pause' parameter can be used to pause an existing hist\n"
	"\t    trigger or to start a hist trigger but not log any events\n"
	"\t    until told to do so.  'continue' can be used to start or\n"
	"\t    restart a paused hist trigger.\n\n"
	"\t    The 'clear' parameter will clear the contents of a running\n"
	"\t    hist trigger and leave its current paused/active state\n"
	"\t    unchanged.\n\n"
	"\t    The enable_hist and disable_hist triggers can be used to\n"
	"\t    have one event conditionally start and stop another event's\n"
	"\t    already-attached hist trigger.  The syntax is analogous to\n"
	"\t    the enable_event and disable_event triggers.\n\n"
	"\t    Hist trigger handlers and actions are executed whenever a\n"
	"\t    a histogram entry is added or updated.  They take the form:\n\n"
	"\t        <handler>.<action>\n\n"
	"\t    The available handlers are:\n\n"
	"\t        onmatch(matching.event)  - invoke on addition or update\n"
	"\t        onmax(var)               - invoke if var exceeds current max\n"
	"\t        onchange(var)            - invoke action if var changes\n\n"
	"\t    The available actions are:\n\n"
	"\t        trace(<synthetic_event>,param list)  - generate synthetic event\n"
	"\t        save(field,...)                      - save current event fields\n"
#ifdef CONFIG_TRACER_SNAPSHOT
	"\t        snapshot()                           - snapshot the trace buffer\n\n"
#endif
#ifdef CONFIG_SYNTH_EVENTS
	"  events/synthetic_events\t- Create/append/remove/show synthetic events\n"
	"\t  Write into this file to define/undefine new synthetic events.\n"
	"\t     example: echo 'myevent u64 lat; char name[]' >> synthetic_events\n"
#endif
#endif
;

static ssize_t
tracing_readme_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, cnt, ppos,
					readme_msg, strlen(readme_msg));
}

static const struct file_operations tracing_readme_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_readme_read,
	.llseek		= generic_file_llseek,
};

static void *saved_tgids_next(struct seq_file *m, void *v, loff_t *pos)
{
	int pid = ++(*pos);

	return trace_find_tgid_ptr(pid);
}

static void *saved_tgids_start(struct seq_file *m, loff_t *pos)
{
	int pid = *pos;

	return trace_find_tgid_ptr(pid);
}

static void saved_tgids_stop(struct seq_file *m, void *v)
{
}

static int saved_tgids_show(struct seq_file *m, void *v)
{
	int *entry = (int *)v;
	int pid = entry - tgid_map;
	int tgid = *entry;

	if (tgid == 0)
		return SEQ_SKIP;

	seq_printf(m, "%d %d\n", pid, tgid);
	return 0;
}

static const struct seq_operations tracing_saved_tgids_seq_ops = {
	.start		= saved_tgids_start,
	.stop		= saved_tgids_stop,
	.next		= saved_tgids_next,
	.show		= saved_tgids_show,
};

static int tracing_saved_tgids_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	return seq_open(filp, &tracing_saved_tgids_seq_ops);
}


static const struct file_operations tracing_saved_tgids_fops = {
	.open		= tracing_saved_tgids_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void *saved_cmdlines_next(struct seq_file *m, void *v, loff_t *pos)
{
	unsigned int *ptr = v;

	if (*pos || m->count)
		ptr++;

	(*pos)++;

	for (; ptr < &savedcmd->map_cmdline_to_pid[savedcmd->cmdline_num];
	     ptr++) {
		if (*ptr == -1 || *ptr == NO_CMDLINE_MAP)
			continue;

		return ptr;
	}

	return NULL;
}

static void *saved_cmdlines_start(struct seq_file *m, loff_t *pos)
{
	void *v;
	loff_t l = 0;

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);

	v = &savedcmd->map_cmdline_to_pid[0];
	while (l <= *pos) {
		v = saved_cmdlines_next(m, v, &l);
		if (!v)
			return NULL;
	}

	return v;
}

static void saved_cmdlines_stop(struct seq_file *m, void *v)
{
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
}

static int saved_cmdlines_show(struct seq_file *m, void *v)
{
	char buf[TASK_COMM_LEN];
	unsigned int *pid = v;

	__trace_find_cmdline(*pid, buf);
	seq_printf(m, "%d %s\n", *pid, buf);
	return 0;
}

static const struct seq_operations tracing_saved_cmdlines_seq_ops = {
	.start		= saved_cmdlines_start,
	.next		= saved_cmdlines_next,
	.stop		= saved_cmdlines_stop,
	.show		= saved_cmdlines_show,
};

static int tracing_saved_cmdlines_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	return seq_open(filp, &tracing_saved_cmdlines_seq_ops);
}

static const struct file_operations tracing_saved_cmdlines_fops = {
	.open		= tracing_saved_cmdlines_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static ssize_t
tracing_saved_cmdlines_size_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);
	r = scnprintf(buf, sizeof(buf), "%u\n", savedcmd->cmdline_num);
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static void free_saved_cmdlines_buffer(struct saved_cmdlines_buffer *s)
{
	kfree(s->saved_cmdlines);
	kfree(s->map_cmdline_to_pid);
	kfree(s);
}

static int tracing_resize_saved_cmdlines(unsigned int val)
{
	struct saved_cmdlines_buffer *s, *savedcmd_temp;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	if (allocate_cmdlines_buffer(val, s) < 0) {
		kfree(s);
		return -ENOMEM;
	}

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);
	savedcmd_temp = savedcmd;
	savedcmd = s;
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
	free_saved_cmdlines_buffer(savedcmd_temp);

	return 0;
}

static ssize_t
tracing_saved_cmdlines_size_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	/* must have at least 1 entry or less than PID_MAX_DEFAULT */
	if (!val || val > PID_MAX_DEFAULT)
		return -EINVAL;

	ret = tracing_resize_saved_cmdlines((unsigned int)val);
	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

static const struct file_operations tracing_saved_cmdlines_size_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_saved_cmdlines_size_read,
	.write		= tracing_saved_cmdlines_size_write,
};

#ifdef CONFIG_TRACE_EVAL_MAP_FILE
static union trace_eval_map_item *
update_eval_map(union trace_eval_map_item *ptr)
{
	if (!ptr->map.eval_string) {
		if (ptr->tail.next) {
			ptr = ptr->tail.next;
			/* Set ptr to the next real item (skip head) */
			ptr++;
		} else
			return NULL;
	}
	return ptr;
}

static void *eval_map_next(struct seq_file *m, void *v, loff_t *pos)
{
	union trace_eval_map_item *ptr = v;

	/*
	 * Paranoid! If ptr points to end, we don't want to increment past it.
	 * This really should never happen.
	 */
	(*pos)++;
	ptr = update_eval_map(ptr);
	if (WARN_ON_ONCE(!ptr))
		return NULL;

	ptr++;
	ptr = update_eval_map(ptr);

	return ptr;
}

static void *eval_map_start(struct seq_file *m, loff_t *pos)
{
	union trace_eval_map_item *v;
	loff_t l = 0;

	mutex_lock(&trace_eval_mutex);

	v = trace_eval_maps;
	if (v)
		v++;

	while (v && l < *pos) {
		v = eval_map_next(m, v, &l);
	}

	return v;
}

static void eval_map_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&trace_eval_mutex);
}

static int eval_map_show(struct seq_file *m, void *v)
{
	union trace_eval_map_item *ptr = v;

	seq_printf(m, "%s %ld (%s)\n",
		   ptr->map.eval_string, ptr->map.eval_value,
		   ptr->map.system);

	return 0;
}

static const struct seq_operations tracing_eval_map_seq_ops = {
	.start		= eval_map_start,
	.next		= eval_map_next,
	.stop		= eval_map_stop,
	.show		= eval_map_show,
};

static int tracing_eval_map_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	return seq_open(filp, &tracing_eval_map_seq_ops);
}

static const struct file_operations tracing_eval_map_fops = {
	.open		= tracing_eval_map_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static inline union trace_eval_map_item *
trace_eval_jmp_to_tail(union trace_eval_map_item *ptr)
{
	/* Return tail of array given the head */
	return ptr + ptr->head.length + 1;
}

static void
trace_insert_eval_map_file(struct module *mod, struct trace_eval_map **start,
			   int len)
{
	struct trace_eval_map **stop;
	struct trace_eval_map **map;
	union trace_eval_map_item *map_array;
	union trace_eval_map_item *ptr;

	stop = start + len;

	/*
	 * The trace_eval_maps contains the map plus a head and tail item,
	 * where the head holds the module and length of array, and the
	 * tail holds a pointer to the next list.
	 */
	map_array = kmalloc_array(len + 2, sizeof(*map_array), GFP_KERNEL);
	if (!map_array) {
		pr_warn("Unable to allocate trace eval mapping\n");
		return;
	}

	mutex_lock(&trace_eval_mutex);

	if (!trace_eval_maps)
		trace_eval_maps = map_array;
	else {
		ptr = trace_eval_maps;
		for (;;) {
			ptr = trace_eval_jmp_to_tail(ptr);
			if (!ptr->tail.next)
				break;
			ptr = ptr->tail.next;

		}
		ptr->tail.next = map_array;
	}
	map_array->head.mod = mod;
	map_array->head.length = len;
	map_array++;

	for (map = start; (unsigned long)map < (unsigned long)stop; map++) {
		map_array->map = **map;
		map_array++;
	}
	memset(map_array, 0, sizeof(*map_array));

	mutex_unlock(&trace_eval_mutex);
}

static void trace_create_eval_file(struct dentry *d_tracer)
{
	trace_create_file("eval_map", TRACE_MODE_READ, d_tracer,
			  NULL, &tracing_eval_map_fops);
}

#else /* CONFIG_TRACE_EVAL_MAP_FILE */
static inline void trace_create_eval_file(struct dentry *d_tracer) { }
static inline void trace_insert_eval_map_file(struct module *mod,
			      struct trace_eval_map **start, int len) { }
#endif /* !CONFIG_TRACE_EVAL_MAP_FILE */

static void trace_insert_eval_map(struct module *mod,
				  struct trace_eval_map **start, int len)
{
	struct trace_eval_map **map;

	if (len <= 0)
		return;

	map = start;

	trace_event_eval_update(map, len);

	trace_insert_eval_map_file(mod, start, len);
}

static ssize_t
tracing_set_trace_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[MAX_TRACER_SIZE+2];
	int r;

	mutex_lock(&trace_types_lock);
	r = sprintf(buf, "%s\n", tr->current_trace->name);
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

int tracer_init(struct tracer *t, struct trace_array *tr)
{
	tracing_reset_online_cpus(&tr->array_buffer);
	return t->init(tr);
}

static void set_buffer_entries(struct array_buffer *buf, unsigned long val)
{
	int cpu;

	for_each_tracing_cpu(cpu)
		per_cpu_ptr(buf->data, cpu)->entries = val;
}

#ifdef CONFIG_TRACER_MAX_TRACE
/* resize @tr's buffer to the size of @size_tr's entries */
static int resize_buffer_duplicate_size(struct array_buffer *trace_buf,
					struct array_buffer *size_buf, int cpu_id)
{
	int cpu, ret = 0;

	if (cpu_id == RING_BUFFER_ALL_CPUS) {
		for_each_tracing_cpu(cpu) {
			ret = ring_buffer_resize(trace_buf->buffer,
				 per_cpu_ptr(size_buf->data, cpu)->entries, cpu);
			if (ret < 0)
				break;
			per_cpu_ptr(trace_buf->data, cpu)->entries =
				per_cpu_ptr(size_buf->data, cpu)->entries;
		}
	} else {
		ret = ring_buffer_resize(trace_buf->buffer,
				 per_cpu_ptr(size_buf->data, cpu_id)->entries, cpu_id);
		if (ret == 0)
			per_cpu_ptr(trace_buf->data, cpu_id)->entries =
				per_cpu_ptr(size_buf->data, cpu_id)->entries;
	}

	return ret;
}
#endif /* CONFIG_TRACER_MAX_TRACE */

static int __tracing_resize_ring_buffer(struct trace_array *tr,
					unsigned long size, int cpu)
{
	int ret;

	/*
	 * If kernel or user changes the size of the ring buffer
	 * we use the size that was given, and we can forget about
	 * expanding it later.
	 */
	ring_buffer_expanded = true;

	/* May be called before buffers are initialized */
	if (!tr->array_buffer.buffer)
		return 0;

	ret = ring_buffer_resize(tr->array_buffer.buffer, size, cpu);
	if (ret < 0)
		return ret;

#ifdef CONFIG_TRACER_MAX_TRACE
	if (!(tr->flags & TRACE_ARRAY_FL_GLOBAL) ||
	    !tr->current_trace->use_max_tr)
		goto out;

	ret = ring_buffer_resize(tr->max_buffer.buffer, size, cpu);
	if (ret < 0) {
		int r = resize_buffer_duplicate_size(&tr->array_buffer,
						     &tr->array_buffer, cpu);
		if (r < 0) {
			/*
			 * AARGH! We are left with different
			 * size max buffer!!!!
			 * The max buffer is our "snapshot" buffer.
			 * When a tracer needs a snapshot (one of the
			 * latency tracers), it swaps the max buffer
			 * with the saved snap shot. We succeeded to
			 * update the size of the main buffer, but failed to
			 * update the size of the max buffer. But when we tried
			 * to reset the main buffer to the original size, we
			 * failed there too. This is very unlikely to
			 * happen, but if it does, warn and kill all
			 * tracing.
			 */
			WARN_ON(1);
			tracing_disabled = 1;
		}
		return ret;
	}

	if (cpu == RING_BUFFER_ALL_CPUS)
		set_buffer_entries(&tr->max_buffer, size);
	else
		per_cpu_ptr(tr->max_buffer.data, cpu)->entries = size;

 out:
#endif /* CONFIG_TRACER_MAX_TRACE */

	if (cpu == RING_BUFFER_ALL_CPUS)
		set_buffer_entries(&tr->array_buffer, size);
	else
		per_cpu_ptr(tr->array_buffer.data, cpu)->entries = size;

	return ret;
}

ssize_t tracing_resize_ring_buffer(struct trace_array *tr,
				  unsigned long size, int cpu_id)
{
	int ret;

	mutex_lock(&trace_types_lock);

	if (cpu_id != RING_BUFFER_ALL_CPUS) {
		/* make sure, this cpu is enabled in the mask */
		if (!cpumask_test_cpu(cpu_id, tracing_buffer_mask)) {
			ret = -EINVAL;
			goto out;
		}
	}

	ret = __tracing_resize_ring_buffer(tr, size, cpu_id);
	if (ret < 0)
		ret = -ENOMEM;

out:
	mutex_unlock(&trace_types_lock);

	return ret;
}


/**
 * tracing_update_buffers - used by tracing facility to expand ring buffers
 *
 * To save on memory when the tracing is never used on a system with it
 * configured in. The ring buffers are set to a minimum size. But once
 * a user starts to use the tracing facility, then they need to grow
 * to their default size.
 *
 * This function is to be called when a tracer is about to be used.
 */
int tracing_update_buffers(void)
{
	int ret = 0;

	mutex_lock(&trace_types_lock);
	if (!ring_buffer_expanded)
		ret = __tracing_resize_ring_buffer(&global_trace, trace_buf_size,
						RING_BUFFER_ALL_CPUS);
	mutex_unlock(&trace_types_lock);

	return ret;
}

struct trace_option_dentry;

static void
create_trace_option_files(struct trace_array *tr, struct tracer *tracer);

/*
 * Used to clear out the tracer before deletion of an instance.
 * Must have trace_types_lock held.
 */
static void tracing_set_nop(struct trace_array *tr)
{
	if (tr->current_trace == &nop_trace)
		return;
	
	tr->current_trace->enabled--;

	if (tr->current_trace->reset)
		tr->current_trace->reset(tr);

	tr->current_trace = &nop_trace;
}

static bool tracer_options_updated;

static void add_tracer_options(struct trace_array *tr, struct tracer *t)
{
	/* Only enable if the directory has been created already. */
	if (!tr->dir)
		return;

	/* Only create trace option files after update_tracer_options finish */
	if (!tracer_options_updated)
		return;

	create_trace_option_files(tr, t);
}

int tracing_set_tracer(struct trace_array *tr, const char *buf)
{
	struct tracer *t;
#ifdef CONFIG_TRACER_MAX_TRACE
	bool had_max_tr;
#endif
	int ret = 0;

	mutex_lock(&trace_types_lock);

	if (!ring_buffer_expanded) {
		ret = __tracing_resize_ring_buffer(tr, trace_buf_size,
						RING_BUFFER_ALL_CPUS);
		if (ret < 0)
			goto out;
		ret = 0;
	}

	for (t = trace_types; t; t = t->next) {
		if (strcmp(t->name, buf) == 0)
			break;
	}
	if (!t) {
		ret = -EINVAL;
		goto out;
	}
	if (t == tr->current_trace)
		goto out;

#ifdef CONFIG_TRACER_SNAPSHOT
	if (t->use_max_tr) {
		local_irq_disable();
		arch_spin_lock(&tr->max_lock);
		if (tr->cond_snapshot)
			ret = -EBUSY;
		arch_spin_unlock(&tr->max_lock);
		local_irq_enable();
		if (ret)
			goto out;
	}
#endif
	/* Some tracers won't work on kernel command line */
	if (system_state < SYSTEM_RUNNING && t->noboot) {
		pr_warn("Tracer '%s' is not allowed on command line, ignored\n",
			t->name);
		goto out;
	}

	/* Some tracers are only allowed for the top level buffer */
	if (!trace_ok_for_array(t, tr)) {
		ret = -EINVAL;
		goto out;
	}

	/* If trace pipe files are being read, we can't change the tracer */
	if (tr->trace_ref) {
		ret = -EBUSY;
		goto out;
	}

	trace_branch_disable();

	tr->current_trace->enabled--;

	if (tr->current_trace->reset)
		tr->current_trace->reset(tr);

	/* Current trace needs to be nop_trace before synchronize_rcu */
	tr->current_trace = &nop_trace;

#ifdef CONFIG_TRACER_MAX_TRACE
	had_max_tr = tr->allocated_snapshot;

	if (had_max_tr && !t->use_max_tr) {
		/*
		 * We need to make sure that the update_max_tr sees that
		 * current_trace changed to nop_trace to keep it from
		 * swapping the buffers after we resize it.
		 * The update_max_tr is called from interrupts disabled
		 * so a synchronized_sched() is sufficient.
		 */
		synchronize_rcu();
		free_snapshot(tr);
	}

	if (t->use_max_tr && !had_max_tr) {
		ret = tracing_alloc_snapshot_instance(tr);
		if (ret < 0)
			goto out;
	}
#endif

	if (t->init) {
		ret = tracer_init(t, tr);
		if (ret)
			goto out;
	}

	tr->current_trace = t;
	tr->current_trace->enabled++;
	trace_branch_enable(tr);
 out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

static ssize_t
tracing_set_trace_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[MAX_TRACER_SIZE+1];
	int i;
	size_t ret;
	int err;

	ret = cnt;

	if (cnt > MAX_TRACER_SIZE)
		cnt = MAX_TRACER_SIZE;

	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	err = tracing_set_tracer(tr, buf);
	if (err)
		return err;

	*ppos += ret;

	return ret;
}

static ssize_t
tracing_nsecs_read(unsigned long *ptr, char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = snprintf(buf, sizeof(buf), "%ld\n",
		     *ptr == (unsigned long)-1 ? -1 : nsecs_to_usecs(*ptr));
	if (r > sizeof(buf))
		r = sizeof(buf);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_nsecs_write(unsigned long *ptr, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	*ptr = val * 1000;

	return cnt;
}

static ssize_t
tracing_thresh_read(struct file *filp, char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return tracing_nsecs_read(&tracing_thresh, ubuf, cnt, ppos);
}

static ssize_t
tracing_thresh_write(struct file *filp, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	int ret;

	mutex_lock(&trace_types_lock);
	ret = tracing_nsecs_write(&tracing_thresh, ubuf, cnt, ppos);
	if (ret < 0)
		goto out;

	if (tr->current_trace->update_thresh) {
		ret = tr->current_trace->update_thresh(tr);
		if (ret < 0)
			goto out;
	}

	ret = cnt;
out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

#if defined(CONFIG_TRACER_MAX_TRACE) || defined(CONFIG_HWLAT_TRACER)

static ssize_t
tracing_max_lat_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	return tracing_nsecs_read(filp->private_data, ubuf, cnt, ppos);
}

static ssize_t
tracing_max_lat_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	return tracing_nsecs_write(filp->private_data, ubuf, cnt, ppos);
}

#endif

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	struct trace_array *tr = inode->i_private;
	struct trace_iterator *iter;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	mutex_lock(&trace_types_lock);

	/* create a buffer to store the information to pass to userspace */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		ret = -ENOMEM;
		__trace_array_put(tr);
		goto out;
	}

	trace_seq_init(&iter->seq);
	iter->trace = tr->current_trace;

	if (!alloc_cpumask_var(&iter->started, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto fail;
	}

	/* trace pipe does not show start of buffer */
	cpumask_setall(iter->started);

	if (tr->trace_flags & TRACE_ITER_LATENCY_FMT)
		iter->iter_flags |= TRACE_FILE_LAT_FMT;

	/* Output in nanoseconds only if we are using a clock in nanoseconds. */
	if (trace_clocks[tr->clock_id].in_ns)
		iter->iter_flags |= TRACE_FILE_TIME_IN_NS;

	iter->tr = tr;
	iter->array_buffer = &tr->array_buffer;
	iter->cpu_file = tracing_get_cpu(inode);
	mutex_init(&iter->mutex);
	filp->private_data = iter;

	if (iter->trace->pipe_open)
		iter->trace->pipe_open(iter);

	nonseekable_open(inode, filp);

	tr->trace_ref++;
out:
	mutex_unlock(&trace_types_lock);
	return ret;

fail:
	kfree(iter);
	__trace_array_put(tr);
	mutex_unlock(&trace_types_lock);
	return ret;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;
	struct trace_array *tr = inode->i_private;

	mutex_lock(&trace_types_lock);

	tr->trace_ref--;

	if (iter->trace->pipe_close)
		iter->trace->pipe_close(iter);

	mutex_unlock(&trace_types_lock);

	free_cpumask_var(iter->started);
	mutex_destroy(&iter->mutex);
	kfree(iter);

	trace_array_put(tr);

	return 0;
}

static __poll_t
trace_poll(struct trace_iterator *iter, struct file *filp, poll_table *poll_table)
{
	struct trace_array *tr = iter->tr;

	/* Iterators are static, they should be filled or empty */
	if (trace_buffer_iter(iter, iter->cpu_file))
		return EPOLLIN | EPOLLRDNORM;

	if (tr->trace_flags & TRACE_ITER_BLOCK)
		/*
		 * Always select as readable when in blocking mode
		 */
		return EPOLLIN | EPOLLRDNORM;
	else
		return ring_buffer_poll_wait(iter->array_buffer->buffer, iter->cpu_file,
					     filp, poll_table);
}

static __poll_t
tracing_poll_pipe(struct file *filp, poll_table *poll_table)
{
	struct trace_iterator *iter = filp->private_data;

	return trace_poll(iter, filp, poll_table);
}

/* Must be called with iter->mutex held. */
static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;
	int ret;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			return -EAGAIN;
		}

		/*
		 * We block until we read something and tracing is disabled.
		 * We still block if tracing is disabled, but we have never
		 * read anything. This allows a user to cat this file, and
		 * then enable tracing. But after we have read something,
		 * we give an EOF when tracing is again disabled.
		 *
		 * iter->pos will be 0 if we haven't read anything.
		 */
		if (!tracer_tracing_is_on(iter->tr) && iter->pos)
			break;

		mutex_unlock(&iter->mutex);

		ret = wait_on_pipe(iter, 0);

		mutex_lock(&iter->mutex);

		if (ret)
			return ret;
	}

	return 1;
}

/*
 * Consumer reader.
 */
static ssize_t
tracing_read_pipe(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	ssize_t sret;

	/*
	 * Avoid more than one consumer on a single file descriptor
	 * This is just a matter of traces coherency, the ring buffer itself
	 * is protected.
	 */
	mutex_lock(&iter->mutex);

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		goto out;

	trace_seq_init(&iter->seq);

	if (iter->trace->read) {
		sret = iter->trace->read(iter, filp, ubuf, cnt, ppos);
		if (sret)
			goto out;
	}

waitagain:
	sret = tracing_wait_pipe(filp);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (trace_empty(iter)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	cpumask_clear(iter->started);
	trace_seq_init(&iter->seq);
	iter->pos = -1;

	trace_event_read_lock();
	trace_access_lock(iter->cpu_file);
	while (trace_find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int save_len = iter->seq.seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.seq.len = save_len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (trace_seq_used(&iter->seq) >= cnt)
			break;

		/*
		 * Setting the full flag means we reached the trace_seq buffer
		 * size and we should leave by partial output condition above.
		 * One of the trace_seq_* functions is not used properly.
		 */
		WARN_ONCE(iter->seq.full, "full flag set for trace type %d",
			  iter->ent->type);
	}
	trace_access_unlock(iter->cpu_file);
	trace_event_read_unlock();

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.seq.readpos >= trace_seq_used(&iter->seq))
		trace_seq_init(&iter->seq);

	/*
	 * If there was nothing to send to user, in spite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

out:
	mutex_unlock(&iter->mutex);

	return sret;
}

static void tracing_spd_release_pipe(struct splice_pipe_desc *spd,
				     unsigned int idx)
{
	__free_page(spd->pages[idx]);
}

static size_t
tracing_fill_pipe_page(size_t rem, struct trace_iterator *iter)
{
	size_t count;
	int save_len;
	int ret;

	/* Seq buffer is page-sized, exactly what we need. */
	for (;;) {
		save_len = iter->seq.seq.len;
		ret = print_trace_line(iter);

		if (trace_seq_has_overflowed(&iter->seq)) {
			iter->seq.seq.len = save_len;
			break;
		}

		/*
		 * This should not be hit, because it should only
		 * be set if the iter->seq overflowed. But check it
		 * anyway to be safe.
		 */
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			iter->seq.seq.len = save_len;
			break;
		}

		count = trace_seq_used(&iter->seq) - save_len;
		if (rem < count) {
			rem = 0;
			iter->seq.seq.len = save_len;
			break;
		}

		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);
		rem -= count;
		if (!trace_find_next_entry_inc(iter))	{
			rem = 0;
			iter->ent = NULL;
			break;
		}
	}

	return rem;
}

static ssize_t tracing_splice_read_pipe(struct file *filp,
					loff_t *ppos,
					struct pipe_inode_info *pipe,
					size_t len,
					unsigned int flags)
{
	struct page *pages_def[PIPE_DEF_BUFFERS];
	struct partial_page partial_def[PIPE_DEF_BUFFERS];
	struct trace_iterator *iter = filp->private_data;
	struct splice_pipe_desc spd = {
		.pages		= pages_def,
		.partial	= partial_def,
		.nr_pages	= 0, /* This gets updated below. */
		.nr_pages_max	= PIPE_DEF_BUFFERS,
		.ops		= &default_pipe_buf_ops,
		.spd_release	= tracing_spd_release_pipe,
	};
	ssize_t ret;
	size_t rem;
	unsigned int i;

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

	mutex_lock(&iter->mutex);

	if (iter->trace->splice_read) {
		ret = iter->trace->splice_read(iter, filp,
					       ppos, pipe, len, flags);
		if (ret)
			goto out_err;
	}

	ret = tracing_wait_pipe(filp);
	if (ret <= 0)
		goto out_err;

	if (!iter->ent && !trace_find_next_entry_inc(iter)) {
		ret = -EFAULT;
		goto out_err;
	}

	trace_event_read_lock();
	trace_access_lock(iter->cpu_file);

	/* Fill as many pages as possible. */
	for (i = 0, rem = len; i < spd.nr_pages_max && rem; i++) {
		spd.pages[i] = alloc_page(GFP_KERNEL);
		if (!spd.pages[i])
			break;

		rem = tracing_fill_pipe_page(rem, iter);

		/* Copy the data into the page, so we can start over. */
		ret = trace_seq_to_buffer(&iter->seq,
					  page_address(spd.pages[i]),
					  trace_seq_used(&iter->seq));
		if (ret < 0) {
			__free_page(spd.pages[i]);
			break;
		}
		spd.partial[i].offset = 0;
		spd.partial[i].len = trace_seq_used(&iter->seq);

		trace_seq_init(&iter->seq);
	}

	trace_access_unlock(iter->cpu_file);
	trace_event_read_unlock();
	mutex_unlock(&iter->mutex);

	spd.nr_pages = i;

	if (i)
		ret = splice_to_pipe(pipe, &spd);
	else
		ret = 0;
out:
	splice_shrink_spd(&spd);
	return ret;

out_err:
	mutex_unlock(&iter->mutex);
	goto out;
}

static ssize_t
tracing_entries_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct inode *inode = file_inode(filp);
	struct trace_array *tr = inode->i_private;
	int cpu = tracing_get_cpu(inode);
	char buf[64];
	int r = 0;
	ssize_t ret;

	mutex_lock(&trace_types_lock);

	if (cpu == RING_BUFFER_ALL_CPUS) {
		int cpu, buf_size_same;
		unsigned long size;

		size = 0;
		buf_size_same = 1;
		/* check if all cpu sizes are same */
		for_each_tracing_cpu(cpu) {
			/* fill in the size from first enabled cpu */
			if (size == 0)
				size = per_cpu_ptr(tr->array_buffer.data, cpu)->entries;
			if (size != per_cpu_ptr(tr->array_buffer.data, cpu)->entries) {
				buf_size_same = 0;
				break;
			}
		}

		if (buf_size_same) {
			if (!ring_buffer_expanded)
				r = sprintf(buf, "%lu (expanded: %lu)\n",
					    size >> 10,
					    trace_buf_size >> 10);
			else
				r = sprintf(buf, "%lu\n", size >> 10);
		} else
			r = sprintf(buf, "X\n");
	} else
		r = sprintf(buf, "%lu\n", per_cpu_ptr(tr->array_buffer.data, cpu)->entries >> 10);

	mutex_unlock(&trace_types_lock);

	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	return ret;
}

static ssize_t
tracing_entries_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	struct inode *inode = file_inode(filp);
	struct trace_array *tr = inode->i_private;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	/* must have at least 1 entry */
	if (!val)
		return -EINVAL;

	/* value is in KB */
	val <<= 10;
	ret = tracing_resize_ring_buffer(tr, val, tracing_get_cpu(inode));
	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

static ssize_t
tracing_total_entries_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[64];
	int r, cpu;
	unsigned long size = 0, expanded_size = 0;

	mutex_lock(&trace_types_lock);
	for_each_tracing_cpu(cpu) {
		size += per_cpu_ptr(tr->array_buffer.data, cpu)->entries >> 10;
		if (!ring_buffer_expanded)
			expanded_size += trace_buf_size >> 10;
	}
	if (ring_buffer_expanded)
		r = sprintf(buf, "%lu\n", size);
	else
		r = sprintf(buf, "%lu (expanded: %lu)\n", size, expanded_size);
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_free_buffer_write(struct file *filp, const char __user *ubuf,
			  size_t cnt, loff_t *ppos)
{
	/*
	 * There is no need to read what the user has written, this function
	 * is just to make sure that there is no error when "echo" is used
	 */

	*ppos += cnt;

	return cnt;
}

static int
tracing_free_buffer_release(struct inode *inode, struct file *filp)
{
	struct trace_array *tr = inode->i_private;

	/* disable tracing ? */
	if (tr->trace_flags & TRACE_ITER_STOP_ON_FREE)
		tracer_tracing_off(tr);
	/* resize the ring buffer to 0 */
	tracing_resize_ring_buffer(tr, 0, RING_BUFFER_ALL_CPUS);

	trace_array_put(tr);

	return 0;
}

static ssize_t
tracing_mark_write(struct file *filp, const char __user *ubuf,
					size_t cnt, loff_t *fpos)
{
	struct trace_array *tr = filp->private_data;
	struct ring_buffer_event *event;
	enum event_trigger_type tt = ETT_NONE;
	struct trace_buffer *buffer;
	struct print_entry *entry;
	ssize_t written;
	int size;
	int len;

/* Used in tracing_mark_raw_write() as well */
#define FAULTED_STR "<faulted>"
#define FAULTED_SIZE (sizeof(FAULTED_STR) - 1) /* '\0' is already accounted for */

	if (tracing_disabled)
		return -EINVAL;

	if (!(tr->trace_flags & TRACE_ITER_MARKERS))
		return -EINVAL;

	if (cnt > TRACE_BUF_SIZE)
		cnt = TRACE_BUF_SIZE;

	BUILD_BUG_ON(TRACE_BUF_SIZE >= PAGE_SIZE);

	size = sizeof(*entry) + cnt + 2; /* add '\0' and possible '\n' */

	/* If less than "<faulted>", then make sure we can still add that */
	if (cnt < FAULTED_SIZE)
		size += FAULTED_SIZE - cnt;

	buffer = tr->array_buffer.buffer;
	event = __trace_buffer_lock_reserve(buffer, TRACE_PRINT, size,
					    tracing_gen_ctx());
	if (unlikely(!event))
		/* Ring buffer disabled, return as if not open for write */
		return -EBADF;

	entry = ring_buffer_event_data(event);
	entry->ip = _THIS_IP_;

	len = __copy_from_user_inatomic(&entry->buf, ubuf, cnt);
	if (len) {
		memcpy(&entry->buf, FAULTED_STR, FAULTED_SIZE);
		cnt = FAULTED_SIZE;
		written = -EFAULT;
	} else
		written = cnt;

	if (tr->trace_marker_file && !list_empty(&tr->trace_marker_file->triggers)) {
		/* do not add \n before testing triggers, but add \0 */
		entry->buf[cnt] = '\0';
		tt = event_triggers_call(tr->trace_marker_file, buffer, entry, event);
	}

	if (entry->buf[cnt - 1] != '\n') {
		entry->buf[cnt] = '\n';
		entry->buf[cnt + 1] = '\0';
	} else
		entry->buf[cnt] = '\0';

	if (static_branch_unlikely(&trace_marker_exports_enabled))
		ftrace_exports(event, TRACE_EXPORT_MARKER);
	__buffer_unlock_commit(buffer, event);

	if (tt)
		event_triggers_post_call(tr->trace_marker_file, tt);

	if (written > 0)
		*fpos += written;

	return written;
}

/* Limit it for now to 3K (including tag) */
#define RAW_DATA_MAX_SIZE (1024*3)

static ssize_t
tracing_mark_raw_write(struct file *filp, const char __user *ubuf,
					size_t cnt, loff_t *fpos)
{
	struct trace_array *tr = filp->private_data;
	struct ring_buffer_event *event;
	struct trace_buffer *buffer;
	struct raw_data_entry *entry;
	ssize_t written;
	int size;
	int len;

#define FAULT_SIZE_ID (FAULTED_SIZE + sizeof(int))

	if (tracing_disabled)
		return -EINVAL;

	if (!(tr->trace_flags & TRACE_ITER_MARKERS))
		return -EINVAL;

	/* The marker must at least have a tag id */
	if (cnt < sizeof(unsigned int) || cnt > RAW_DATA_MAX_SIZE)
		return -EINVAL;

	if (cnt > TRACE_BUF_SIZE)
		cnt = TRACE_BUF_SIZE;

	BUILD_BUG_ON(TRACE_BUF_SIZE >= PAGE_SIZE);

	size = sizeof(*entry) + cnt;
	if (cnt < FAULT_SIZE_ID)
		size += FAULT_SIZE_ID - cnt;

	buffer = tr->array_buffer.buffer;
	event = __trace_buffer_lock_reserve(buffer, TRACE_RAW_DATA, size,
					    tracing_gen_ctx());
	if (!event)
		/* Ring buffer disabled, return as if not open for write */
		return -EBADF;

	entry = ring_buffer_event_data(event);

	len = __copy_from_user_inatomic(&entry->id, ubuf, cnt);
	if (len) {
		entry->id = -1;
		memcpy(&entry->buf, FAULTED_STR, FAULTED_SIZE);
		written = -EFAULT;
	} else
		written = cnt;

	__buffer_unlock_commit(buffer, event);

	if (written > 0)
		*fpos += written;

	return written;
}

static int tracing_clock_show(struct seq_file *m, void *v)
{
	struct trace_array *tr = m->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_clocks); i++)
		seq_printf(m,
			"%s%s%s%s", i ? " " : "",
			i == tr->clock_id ? "[" : "", trace_clocks[i].name,
			i == tr->clock_id ? "]" : "");
	seq_putc(m, '\n');

	return 0;
}

int tracing_set_clock(struct trace_array *tr, const char *clockstr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_clocks); i++) {
		if (strcmp(trace_clocks[i].name, clockstr) == 0)
			break;
	}
	if (i == ARRAY_SIZE(trace_clocks))
		return -EINVAL;

	mutex_lock(&trace_types_lock);

	tr->clock_id = i;

	ring_buffer_set_clock(tr->array_buffer.buffer, trace_clocks[i].func);

	/*
	 * New clock may not be consistent with the previous clock.
	 * Reset the buffer so that it doesn't have incomparable timestamps.
	 */
	tracing_reset_online_cpus(&tr->array_buffer);

#ifdef CONFIG_TRACER_MAX_TRACE
	if (tr->max_buffer.buffer)
		ring_buffer_set_clock(tr->max_buffer.buffer, trace_clocks[i].func);
	tracing_reset_online_cpus(&tr->max_buffer);
#endif

	mutex_unlock(&trace_types_lock);

	return 0;
}

static ssize_t tracing_clock_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *fpos)
{
	struct seq_file *m = filp->private_data;
	struct trace_array *tr = m->private;
	char buf[64];
	const char *clockstr;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	clockstr = strstrip(buf);

	ret = tracing_set_clock(tr, clockstr);
	if (ret)
		return ret;

	*fpos += cnt;

	return cnt;
}

static int tracing_clock_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	ret = single_open(file, tracing_clock_show, inode->i_private);
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

static int tracing_time_stamp_mode_show(struct seq_file *m, void *v)
{
	struct trace_array *tr = m->private;

	mutex_lock(&trace_types_lock);

	if (ring_buffer_time_stamp_abs(tr->array_buffer.buffer))
		seq_puts(m, "delta [absolute]\n");
	else
		seq_puts(m, "[delta] absolute\n");

	mutex_unlock(&trace_types_lock);

	return 0;
}

static int tracing_time_stamp_mode_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	ret = single_open(file, tracing_time_stamp_mode_show, inode->i_private);
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

u64 tracing_event_time_stamp(struct trace_buffer *buffer, struct ring_buffer_event *rbe)
{
	if (rbe == this_cpu_read(trace_buffered_event))
		return ring_buffer_time_stamp(buffer);

	return ring_buffer_event_time_stamp(buffer, rbe);
}

/*
 * Set or disable using the per CPU trace_buffer_event when possible.
 */
int tracing_set_filter_buffering(struct trace_array *tr, bool set)
{
	int ret = 0;

	mutex_lock(&trace_types_lock);

	if (set && tr->no_filter_buffering_ref++)
		goto out;

	if (!set) {
		if (WARN_ON_ONCE(!tr->no_filter_buffering_ref)) {
			ret = -EINVAL;
			goto out;
		}

		--tr->no_filter_buffering_ref;
	}
 out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

struct ftrace_buffer_info {
	struct trace_iterator	iter;
	void			*spare;
	unsigned int		spare_cpu;
	unsigned int		read;
};

#ifdef CONFIG_TRACER_SNAPSHOT
static int tracing_snapshot_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	struct trace_iterator *iter;
	struct seq_file *m;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	if (file->f_mode & FMODE_READ) {
		iter = __tracing_open(inode, file, true);
		if (IS_ERR(iter))
			ret = PTR_ERR(iter);
	} else {
		/* Writes still need the seq_file to hold the private data */
		ret = -ENOMEM;
		m = kzalloc(sizeof(*m), GFP_KERNEL);
		if (!m)
			goto out;
		iter = kzalloc(sizeof(*iter), GFP_KERNEL);
		if (!iter) {
			kfree(m);
			goto out;
		}
		ret = 0;

		iter->tr = tr;
		iter->array_buffer = &tr->max_buffer;
		iter->cpu_file = tracing_get_cpu(inode);
		m->private = iter;
		file->private_data = m;
	}
out:
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

static ssize_t
tracing_snapshot_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct trace_iterator *iter = m->private;
	struct trace_array *tr = iter->tr;
	unsigned long val;
	int ret;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&trace_types_lock);

	if (tr->current_trace->use_max_tr) {
		ret = -EBUSY;
		goto out;
	}

	local_irq_disable();
	arch_spin_lock(&tr->max_lock);
	if (tr->cond_snapshot)
		ret = -EBUSY;
	arch_spin_unlock(&tr->max_lock);
	local_irq_enable();
	if (ret)
		goto out;

	switch (val) {
	case 0:
		if (iter->cpu_file != RING_BUFFER_ALL_CPUS) {
			ret = -EINVAL;
			break;
		}
		if (tr->allocated_snapshot)
			free_snapshot(tr);
		break;
	case 1:
/* Only allow per-cpu swap if the ring buffer supports it */
#ifndef CONFIG_RING_BUFFER_ALLOW_SWAP
		if (iter->cpu_file != RING_BUFFER_ALL_CPUS) {
			ret = -EINVAL;
			break;
		}
#endif
		if (tr->allocated_snapshot)
			ret = resize_buffer_duplicate_size(&tr->max_buffer,
					&tr->array_buffer, iter->cpu_file);
		else
			ret = tracing_alloc_snapshot_instance(tr);
		if (ret < 0)
			break;
		local_irq_disable();
		/* Now, we're going to swap */
		if (iter->cpu_file == RING_BUFFER_ALL_CPUS)
			update_max_tr(tr, current, smp_processor_id(), NULL);
		else
			update_max_tr_single(tr, current, iter->cpu_file);
		local_irq_enable();
		break;
	default:
		if (tr->allocated_snapshot) {
			if (iter->cpu_file == RING_BUFFER_ALL_CPUS)
				tracing_reset_online_cpus(&tr->max_buffer);
			else
				tracing_reset_cpu(&tr->max_buffer, iter->cpu_file);
		}
		break;
	}

	if (ret >= 0) {
		*ppos += cnt;
		ret = cnt;
	}
out:
	mutex_unlock(&trace_types_lock);
	return ret;
}

static int tracing_snapshot_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	int ret;

	ret = tracing_release(inode, file);

	if (file->f_mode & FMODE_READ)
		return ret;

	/* If write only, the seq_file is just a stub */
	if (m)
		kfree(m->private);
	kfree(m);

	return 0;
}

static int tracing_buffers_open(struct inode *inode, struct file *filp);
static ssize_t tracing_buffers_read(struct file *filp, char __user *ubuf,
				    size_t count, loff_t *ppos);
static int tracing_buffers_release(struct inode *inode, struct file *file);
static ssize_t tracing_buffers_splice_read(struct file *file, loff_t *ppos,
		   struct pipe_inode_info *pipe, size_t len, unsigned int flags);

static int snapshot_raw_open(struct inode *inode, struct file *filp)
{
	struct ftrace_buffer_info *info;
	int ret;

	/* The following checks for tracefs lockdown */
	ret = tracing_buffers_open(inode, filp);
	if (ret < 0)
		return ret;

	info = filp->private_data;

	if (info->iter.trace->use_max_tr) {
		tracing_buffers_release(inode, filp);
		return -EBUSY;
	}

	info->iter.snapshot = true;
	info->iter.array_buffer = &info->iter.tr->max_buffer;

	return ret;
}

#endif /* CONFIG_TRACER_SNAPSHOT */


static const struct file_operations tracing_thresh_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_thresh_read,
	.write		= tracing_thresh_write,
	.llseek		= generic_file_llseek,
};

#if defined(CONFIG_TRACER_MAX_TRACE) || defined(CONFIG_HWLAT_TRACER)
static const struct file_operations tracing_max_lat_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_max_lat_read,
	.write		= tracing_max_lat_write,
	.llseek		= generic_file_llseek,
};
#endif

static const struct file_operations set_tracer_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_set_trace_read,
	.write		= tracing_set_trace_write,
	.llseek		= generic_file_llseek,
};

static const struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.poll		= tracing_poll_pipe,
	.read		= tracing_read_pipe,
	.splice_read	= tracing_splice_read_pipe,
	.release	= tracing_release_pipe,
	.llseek		= no_llseek,
};

static const struct file_operations tracing_entries_fops = {
	.open		= tracing_open_generic_tr,
	.read		= tracing_entries_read,
	.write		= tracing_entries_write,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};

static const struct file_operations tracing_total_entries_fops = {
	.open		= tracing_open_generic_tr,
	.read		= tracing_total_entries_read,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};

static const struct file_operations tracing_free_buffer_fops = {
	.open		= tracing_open_generic_tr,
	.write		= tracing_free_buffer_write,
	.release	= tracing_free_buffer_release,
};

static const struct file_operations tracing_mark_fops = {
	.open		= tracing_open_generic_tr,
	.write		= tracing_mark_write,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};

static const struct file_operations tracing_mark_raw_fops = {
	.open		= tracing_open_generic_tr,
	.write		= tracing_mark_raw_write,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};

static const struct file_operations trace_clock_fops = {
	.open		= tracing_clock_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_single_release_tr,
	.write		= tracing_clock_write,
};

static const struct file_operations trace_time_stamp_mode_fops = {
	.open		= tracing_time_stamp_mode_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_single_release_tr,
};

#ifdef CONFIG_TRACER_SNAPSHOT
static const struct file_operations snapshot_fops = {
	.open		= tracing_snapshot_open,
	.read		= seq_read,
	.write		= tracing_snapshot_write,
	.llseek		= tracing_lseek,
	.release	= tracing_snapshot_release,
};

static const struct file_operations snapshot_raw_fops = {
	.open		= snapshot_raw_open,
	.read		= tracing_buffers_read,
	.release	= tracing_buffers_release,
	.splice_read	= tracing_buffers_splice_read,
	.llseek		= no_llseek,
};

#endif /* CONFIG_TRACER_SNAPSHOT */

/*
 * trace_min_max_write - Write a u64 value to a trace_min_max_param struct
 * @filp: The active open file structure
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function implements the write interface for a struct trace_min_max_param.
 * The filp->private_data must point to a trace_min_max_param structure that
 * defines where to write the value, the min and the max acceptable values,
 * and a lock to protect the write.
 */
static ssize_t
trace_min_max_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_min_max_param *param = filp->private_data;
	u64 val;
	int err;

	if (!param)
		return -EFAULT;

	err = kstrtoull_from_user(ubuf, cnt, 10, &val);
	if (err)
		return err;

	if (param->lock)
		mutex_lock(param->lock);

	if (param->min && val < *param->min)
		err = -EINVAL;

	if (param->max && val > *param->max)
		err = -EINVAL;

	if (!err)
		*param->val = val;

	if (param->lock)
		mutex_unlock(param->lock);

	if (err)
		return err;

	return cnt;
}

/*
 * trace_min_max_read - Read a u64 value from a trace_min_max_param struct
 * @filp: The active open file structure
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function implements the read interface for a struct trace_min_max_param.
 * The filp->private_data must point to a trace_min_max_param struct with valid
 * data.
 */
static ssize_t
trace_min_max_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_min_max_param *param = filp->private_data;
	char buf[U64_STR_SIZE];
	int len;
	u64 val;

	if (!param)
		return -EFAULT;

	val = *param->val;

	if (cnt > sizeof(buf))
		cnt = sizeof(buf);

	len = snprintf(buf, sizeof(buf), "%llu\n", val);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

const struct file_operations trace_min_max_fops = {
	.open		= tracing_open_generic,
	.read		= trace_min_max_read,
	.write		= trace_min_max_write,
};

#define TRACING_LOG_ERRS_MAX	8
#define TRACING_LOG_LOC_MAX	128

#define CMD_PREFIX "  Command: "

struct err_info {
	const char	**errs;	/* ptr to loc-specific array of err strings */
	u8		type;	/* index into errs -> specific err string */
	u8		pos;	/* MAX_FILTER_STR_VAL = 256 */
	u64		ts;
};

struct tracing_log_err {
	struct list_head	list;
	struct err_info		info;
	char			loc[TRACING_LOG_LOC_MAX]; /* err location */
	char			cmd[MAX_FILTER_STR_VAL]; /* what caused err */
};

static DEFINE_MUTEX(tracing_err_log_lock);

static struct tracing_log_err *get_tracing_log_err(struct trace_array *tr)
{
	struct tracing_log_err *err;

	if (tr->n_err_log_entries < TRACING_LOG_ERRS_MAX) {
		err = kzalloc(sizeof(*err), GFP_KERNEL);
		if (!err)
			err = ERR_PTR(-ENOMEM);
		else
			tr->n_err_log_entries++;

		return err;
	}

	err = list_first_entry(&tr->err_log, struct tracing_log_err, list);
	list_del(&err->list);

	return err;
}

/**
 * err_pos - find the position of a string within a command for error careting
 * @cmd: The tracing command that caused the error
 * @str: The string to position the caret at within @cmd
 *
 * Finds the position of the first occurrence of @str within @cmd.  The
 * return value can be passed to tracing_log_err() for caret placement
 * within @cmd.
 *
 * Returns the index within @cmd of the first occurrence of @str or 0
 * if @str was not found.
 */
unsigned int err_pos(char *cmd, const char *str)
{
	char *found;

	if (WARN_ON(!strlen(cmd)))
		return 0;

	found = strstr(cmd, str);
	if (found)
		return found - cmd;

	return 0;
}

/**
 * tracing_log_err - write an error to the tracing error log
 * @tr: The associated trace array for the error (NULL for top level array)
 * @loc: A string describing where the error occurred
 * @cmd: The tracing command that caused the error
 * @errs: The array of loc-specific static error strings
 * @type: The index into errs[], which produces the specific static err string
 * @pos: The position the caret should be placed in the cmd
 *
 * Writes an error into tracing/error_log of the form:
 *
 * <loc>: error: <text>
 *   Command: <cmd>
 *              ^
 *
 * tracing/error_log is a small log file containing the last
 * TRACING_LOG_ERRS_MAX errors (8).  Memory for errors isn't allocated
 * unless there has been a tracing error, and the error log can be
 * cleared and have its memory freed by writing the empty string in
 * truncation mode to it i.e. echo > tracing/error_log.
 *
 * NOTE: the @errs array along with the @type param are used to
 * produce a static error string - this string is not copied and saved
 * when the error is logged - only a pointer to it is saved.  See
 * existing callers for examples of how static strings are typically
 * defined for use with tracing_log_err().
 */
void tracing_log_err(struct trace_array *tr,
		     const char *loc, const char *cmd,
		     const char **errs, u8 type, u8 pos)
{
	struct tracing_log_err *err;

	if (!tr)
		tr = &global_trace;

	mutex_lock(&tracing_err_log_lock);
	err = get_tracing_log_err(tr);
	if (PTR_ERR(err) == -ENOMEM) {
		mutex_unlock(&tracing_err_log_lock);
		return;
	}

	snprintf(err->loc, TRACING_LOG_LOC_MAX, "%s: error: ", loc);
	snprintf(err->cmd, MAX_FILTER_STR_VAL,"\n" CMD_PREFIX "%s\n", cmd);

	err->info.errs = errs;
	err->info.type = type;
	err->info.pos = pos;
	err->info.ts = local_clock();

	list_add_tail(&err->list, &tr->err_log);
	mutex_unlock(&tracing_err_log_lock);
}

static void clear_tracing_err_log(struct trace_array *tr)
{
	struct tracing_log_err *err, *next;

	mutex_lock(&tracing_err_log_lock);
	list_for_each_entry_safe(err, next, &tr->err_log, list) {
		list_del(&err->list);
		kfree(err);
	}

	tr->n_err_log_entries = 0;
	mutex_unlock(&tracing_err_log_lock);
}

static void *tracing_err_log_seq_start(struct seq_file *m, loff_t *pos)
{
	struct trace_array *tr = m->private;

	mutex_lock(&tracing_err_log_lock);

	return seq_list_start(&tr->err_log, *pos);
}

static void *tracing_err_log_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_array *tr = m->private;

	return seq_list_next(v, &tr->err_log, pos);
}

static void tracing_err_log_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&tracing_err_log_lock);
}

static void tracing_err_log_show_pos(struct seq_file *m, u8 pos)
{
	u8 i;

	for (i = 0; i < sizeof(CMD_PREFIX) - 1; i++)
		seq_putc(m, ' ');
	for (i = 0; i < pos; i++)
		seq_putc(m, ' ');
	seq_puts(m, "^\n");
}

static int tracing_err_log_seq_show(struct seq_file *m, void *v)
{
	struct tracing_log_err *err = v;

	if (err) {
		const char *err_text = err->info.errs[err->info.type];
		u64 sec = err->info.ts;
		u32 nsec;

		nsec = do_div(sec, NSEC_PER_SEC);
		seq_printf(m, "[%5llu.%06u] %s%s", sec, nsec / 1000,
			   err->loc, err_text);
		seq_printf(m, "%s", err->cmd);
		tracing_err_log_show_pos(m, err->info.pos);
	}

	return 0;
}

static const struct seq_operations tracing_err_log_seq_ops = {
	.start  = tracing_err_log_seq_start,
	.next   = tracing_err_log_seq_next,
	.stop   = tracing_err_log_seq_stop,
	.show   = tracing_err_log_seq_show
};

static int tracing_err_log_open(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;
	int ret = 0;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	/* If this file was opened for write, then erase contents */
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC))
		clear_tracing_err_log(tr);

	if (file->f_mode & FMODE_READ) {
		ret = seq_open(file, &tracing_err_log_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = tr;
		} else {
			trace_array_put(tr);
		}
	}
	return ret;
}

static ssize_t tracing_err_log_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	return count;
}

static int tracing_err_log_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);

	if (file->f_mode & FMODE_READ)
		seq_release(inode, file);

	return 0;
}

static const struct file_operations tracing_err_log_fops = {
	.open           = tracing_err_log_open,
	.write		= tracing_err_log_write,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = tracing_err_log_release,
};

static int tracing_buffers_open(struct inode *inode, struct file *filp)
{
	struct trace_array *tr = inode->i_private;
	struct ftrace_buffer_info *info;
	int ret;

	ret = tracing_check_open_get_tr(tr);
	if (ret)
		return ret;

	info = kvzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		trace_array_put(tr);
		return -ENOMEM;
	}

	mutex_lock(&trace_types_lock);

	info->iter.tr		= tr;
	info->iter.cpu_file	= tracing_get_cpu(inode);
	info->iter.trace	= tr->current_trace;
	info->iter.array_buffer = &tr->array_buffer;
	info->spare		= NULL;
	/* Force reading ring buffer for first read */
	info->read		= (unsigned int)-1;

	filp->private_data = info;

	tr->trace_ref++;

	mutex_unlock(&trace_types_lock);

	ret = nonseekable_open(inode, filp);
	if (ret < 0)
		trace_array_put(tr);

	return ret;
}

static __poll_t
tracing_buffers_poll(struct file *filp, poll_table *poll_table)
{
	struct ftrace_buffer_info *info = filp->private_data;
	struct trace_iterator *iter = &info->iter;

	return trace_poll(iter, filp, poll_table);
}

static ssize_t
tracing_buffers_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct ftrace_buffer_info *info = filp->private_data;
	struct trace_iterator *iter = &info->iter;
	ssize_t ret = 0;
	ssize_t size;

	if (!count)
		return 0;

#ifdef CONFIG_TRACER_MAX_TRACE
	if (iter->snapshot && iter->tr->current_trace->use_max_tr)
		return -EBUSY;
#endif

	if (!info->spare) {
		info->spare = ring_buffer_alloc_read_page(iter->array_buffer->buffer,
							  iter->cpu_file);
		if (IS_ERR(info->spare)) {
			ret = PTR_ERR(info->spare);
			info->spare = NULL;
		} else {
			info->spare_cpu = iter->cpu_file;
		}
	}
	if (!info->spare)
		return ret;

	/* Do we have previous read data to read? */
	if (info->read < PAGE_SIZE)
		goto read;

 again:
	trace_access_lock(iter->cpu_file);
	ret = ring_buffer_read_page(iter->array_buffer->buffer,
				    &info->spare,
				    count,
				    iter->cpu_file, 0);
	trace_access_unlock(iter->cpu_file);

	if (ret < 0) {
		if (trace_empty(iter)) {
			if ((filp->f_flags & O_NONBLOCK))
				return -EAGAIN;

			ret = wait_on_pipe(iter, 0);
			if (ret)
				return ret;

			goto again;
		}
		return 0;
	}

	info->read = 0;
 read:
	size = PAGE_SIZE - info->read;
	if (size > count)
		size = count;

	ret = copy_to_user(ubuf, info->spare + info->read, size);
	if (ret == size)
		return -EFAULT;

	size -= ret;

	*ppos += size;
	info->read += size;

	return size;
}

static int tracing_buffers_release(struct inode *inode, struct file *file)
{
	struct ftrace_buffer_info *info = file->private_data;
	struct trace_iterator *iter = &info->iter;

	mutex_lock(&trace_types_lock);

	iter->tr->trace_ref--;

	__trace_array_put(iter->tr);

	iter->wait_index++;
	/* Make sure the waiters see the new wait_index */
	smp_wmb();

	ring_buffer_wake_waiters(iter->array_buffer->buffer, iter->cpu_file);

	if (info->spare)
		ring_buffer_free_read_page(iter->array_buffer->buffer,
					   info->spare_cpu, info->spare);
	kvfree(info);

	mutex_unlock(&trace_types_lock);

	return 0;
}

struct buffer_ref {
	struct trace_buffer	*buffer;
	void			*page;
	int			cpu;
	refcount_t		refcount;
};

static void buffer_ref_release(struct buffer_ref *ref)
{
	if (!refcount_dec_and_test(&ref->refcount))
		return;
	ring_buffer_free_read_page(ref->buffer, ref->cpu, ref->page);
	kfree(ref);
}

static void buffer_pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	buffer_ref_release(ref);
	buf->private = 0;
}

static bool buffer_pipe_buf_get(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	if (refcount_read(&ref->refcount) > INT_MAX/2)
		return false;

	refcount_inc(&ref->refcount);
	return true;
}

/* Pipe buffer operations for a buffer. */
static const struct pipe_buf_operations buffer_pipe_buf_ops = {
	.release		= buffer_pipe_buf_release,
	.get			= buffer_pipe_buf_get,
};

/*
 * Callback from splice_to_pipe(), if we need to release some pages
 * at the end of the spd in case we error'ed out in filling the pipe.
 */
static void buffer_spd_release(struct splice_pipe_desc *spd, unsigned int i)
{
	struct buffer_ref *ref =
		(struct buffer_ref *)spd->partial[i].private;

	buffer_ref_release(ref);
	spd->partial[i].private = 0;
}

static ssize_t
tracing_buffers_splice_read(struct file *file, loff_t *ppos,
			    struct pipe_inode_info *pipe, size_t len,
			    unsigned int flags)
{
	struct ftrace_buffer_info *info = file->private_data;
	struct trace_iterator *iter = &info->iter;
	struct partial_page partial_def[PIPE_DEF_BUFFERS];
	struct page *pages_def[PIPE_DEF_BUFFERS];
	struct splice_pipe_desc spd = {
		.pages		= pages_def,
		.partial	= partial_def,
		.nr_pages_max	= PIPE_DEF_BUFFERS,
		.ops		= &buffer_pipe_buf_ops,
		.spd_release	= buffer_spd_release,
	};
	struct buffer_ref *ref;
	int entries, i;
	ssize_t ret = 0;

#ifdef CONFIG_TRACER_MAX_TRACE
	if (iter->snapshot && iter->tr->current_trace->use_max_tr)
		return -EBUSY;
#endif

	if (*ppos & (PAGE_SIZE - 1))
		return -EINVAL;

	if (len & (PAGE_SIZE - 1)) {
		if (len < PAGE_SIZE)
			return -EINVAL;
		len &= PAGE_MASK;
	}

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

 again:
	trace_access_lock(iter->cpu_file);
	entries = ring_buffer_entries_cpu(iter->array_buffer->buffer, iter->cpu_file);

	for (i = 0; i < spd.nr_pages_max && len && entries; i++, len -= PAGE_SIZE) {
		struct page *page;
		int r;

		ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!ref) {
			ret = -ENOMEM;
			break;
		}

		refcount_set(&ref->refcount, 1);
		ref->buffer = iter->array_buffer->buffer;
		ref->page = ring_buffer_alloc_read_page(ref->buffer, iter->cpu_file);
		if (IS_ERR(ref->page)) {
			ret = PTR_ERR(ref->page);
			ref->page = NULL;
			kfree(ref);
			break;
		}
		ref->cpu = iter->cpu_file;

		r = ring_buffer_read_page(ref->buffer, &ref->page,
					  len, iter->cpu_file, 1);
		if (r < 0) {
			ring_buffer_free_read_page(ref->buffer, ref->cpu,
						   ref->page);
			kfree(ref);
			break;
		}

		page = virt_to_page(ref->page);

		spd.pages[i] = page;
		spd.partial[i].len = PAGE_SIZE;
		spd.partial[i].offset = 0;
		spd.partial[i].private = (unsigned long)ref;
		spd.nr_pages++;
		*ppos += PAGE_SIZE;

		entries = ring_buffer_entries_cpu(iter->array_buffer->buffer, iter->cpu_file);
	}

	trace_access_unlock(iter->cpu_file);
	spd.nr_pages = i;

	/* did we read anything? */
	if (!spd.nr_pages) {
		long wait_index;

		if (ret)
			goto out;

		ret = -EAGAIN;
		if ((file->f_flags & O_NONBLOCK) || (flags & SPLICE_F_NONBLOCK))
			goto out;

		wait_index = READ_ONCE(iter->wait_index);

		ret = wait_on_pipe(iter, iter->tr->buffer_percent);
		if (ret)
			goto out;

		/* No need to wait after waking up when tracing is off */
		if (!tracer_tracing_is_on(iter->tr))
			goto out;

		/* Make sure we see the new wait_index */
		smp_rmb();
		if (wait_index != iter->wait_index)
			goto out;

		goto again;
	}

	ret = splice_to_pipe(pipe, &spd);
out:
	splice_shrink_spd(&spd);

	return ret;
}

/* An ioctl call with cmd 0 to the ring buffer file will wake up all waiters */
static long tracing_buffers_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ftrace_buffer_info *info = file->private_data;
	struct trace_iterator *iter = &info->iter;

	if (cmd)
		return -ENOIOCTLCMD;

	mutex_lock(&trace_types_lock);

	iter->wait_index++;
	/* Make sure the waiters see the new wait_index */
	smp_wmb();

	ring_buffer_wake_waiters(iter->array_buffer->buffer, iter->cpu_file);

	mutex_unlock(&trace_types_lock);
	return 0;
}

static const struct file_operations tracing_buffers_fops = {
	.open		= tracing_buffers_open,
	.read		= tracing_buffers_read,
	.poll		= tracing_buffers_poll,
	.release	= tracing_buffers_release,
	.splice_read	= tracing_buffers_splice_read,
	.unlocked_ioctl = tracing_buffers_ioctl,
	.llseek		= no_llseek,
};

static ssize_t
tracing_stats_read(struct file *filp, char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(filp);
	struct trace_array *tr = inode->i_private;
	struct array_buffer *trace_buf = &tr->array_buffer;
	int cpu = tracing_get_cpu(inode);
	struct trace_seq *s;
	unsigned long cnt;
	unsigned long long t;
	unsigned long usec_rem;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	cnt = ring_buffer_entries_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "entries: %ld\n", cnt);

	cnt = ring_buffer_overrun_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "overrun: %ld\n", cnt);

	cnt = ring_buffer_commit_overrun_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "commit overrun: %ld\n", cnt);

	cnt = ring_buffer_bytes_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "bytes: %ld\n", cnt);

	if (trace_clocks[tr->clock_id].in_ns) {
		/* local or global for trace_clock */
		t = ns2usecs(ring_buffer_oldest_event_ts(trace_buf->buffer, cpu));
		usec_rem = do_div(t, USEC_PER_SEC);
		trace_seq_printf(s, "oldest event ts: %5llu.%06lu\n",
								t, usec_rem);

		t = ns2usecs(ring_buffer_time_stamp(trace_buf->buffer));
		usec_rem = do_div(t, USEC_PER_SEC);
		trace_seq_printf(s, "now ts: %5llu.%06lu\n", t, usec_rem);
	} else {
		/* counter or tsc mode for trace_clock */
		trace_seq_printf(s, "oldest event ts: %llu\n",
				ring_buffer_oldest_event_ts(trace_buf->buffer, cpu));

		trace_seq_printf(s, "now ts: %llu\n",
				ring_buffer_time_stamp(trace_buf->buffer));
	}

	cnt = ring_buffer_dropped_events_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "dropped events: %ld\n", cnt);

	cnt = ring_buffer_read_events_cpu(trace_buf->buffer, cpu);
	trace_seq_printf(s, "read events: %ld\n", cnt);

	count = simple_read_from_buffer(ubuf, count, ppos,
					s->buffer, trace_seq_used(s));

	kfree(s);

	return count;
}

static const struct file_operations tracing_stats_fops = {
	.open		= tracing_open_generic_tr,
	.read		= tracing_stats_read,
	.llseek		= generic_file_llseek,
	.release	= tracing_release_generic_tr,
};

#ifdef CONFIG_DYNAMIC_FTRACE

static ssize_t
tracing_read_dyn_info(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	ssize_t ret;
	char *buf;
	int r;

	/* 256 should be plenty to hold the amount needed */
	buf = kmalloc(256, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	r = scnprintf(buf, 256, "%ld pages:%ld groups: %ld\n",
		      ftrace_update_tot_cnt,
		      ftrace_number_of_pages,
		      ftrace_number_of_groups);

	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
	kfree(buf);
	return ret;
}

static const struct file_operations tracing_dyn_info_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_read_dyn_info,
	.llseek		= generic_file_llseek,
};
#endif /* CONFIG_DYNAMIC_FTRACE */

#if defined(CONFIG_TRACER_SNAPSHOT) && defined(CONFIG_DYNAMIC_FTRACE)
static void
ftrace_snapshot(unsigned long ip, unsigned long parent_ip,
		struct trace_array *tr, struct ftrace_probe_ops *ops,
		void *data)
{
	tracing_snapshot_instance(tr);
}

static void
ftrace_count_snapshot(unsigned long ip, unsigned long parent_ip,
		      struct trace_array *tr, struct ftrace_probe_ops *ops,
		      void *data)
{
	struct ftrace_func_mapper *mapper = data;
	long *count = NULL;

	if (mapper)
		count = (long *)ftrace_func_mapper_find_ip(mapper, ip);

	if (count) {

		if (*count <= 0)
			return;

		(*count)--;
	}

	tracing_snapshot_instance(tr);
}

static int
ftrace_snapshot_print(struct seq_file *m, unsigned long ip,
		      struct ftrace_probe_ops *ops, void *data)
{
	struct ftrace_func_mapper *mapper = data;
	long *count = NULL;

	seq_printf(m, "%ps:", (void *)ip);

	seq_puts(m, "snapshot");

	if (mapper)
		count = (long *)ftrace_func_mapper_find_ip(mapper, ip);

	if (count)
		seq_printf(m, ":count=%ld\n", *count);
	else
		seq_puts(m, ":unlimited\n");

	return 0;
}

static int
ftrace_snapshot_init(struct ftrace_probe_ops *ops, struct trace_array *tr,
		     unsigned long ip, void *init_data, void **data)
{
	struct ftrace_func_mapper *mapper = *data;

	if (!mapper) {
		mapper = allocate_ftrace_func_mapper();
		if (!mapper)
			return -ENOMEM;
		*data = mapper;
	}

	return ftrace_func_mapper_add_ip(mapper, ip, init_data);
}

static void
ftrace_snapshot_free(struct ftrace_probe_ops *ops, struct trace_array *tr,
		     unsigned long ip, void *data)
{
	struct ftrace_func_mapper *mapper = data;

	if (!ip) {
		if (!mapper)
			return;
		free_ftrace_func_mapper(mapper, NULL);
		return;
	}

	ftrace_func_mapper_remove_ip(mapper, ip);
}

static struct ftrace_probe_ops snapshot_probe_ops = {
	.func			= ftrace_snapshot,
	.print			= ftrace_snapshot_print,
};

static struct ftrace_probe_ops snapshot_count_probe_ops = {
	.func			= ftrace_count_snapshot,
	.print			= ftrace_snapshot_print,
	.init			= ftrace_snapshot_init,
	.free			= ftrace_snapshot_free,
};

static int
ftrace_trace_snapshot_callback(struct trace_array *tr, struct ftrace_hash *hash,
			       char *glob, char *cmd, char *param, int enable)
{
	struct ftrace_probe_ops *ops;
	void *count = (void *)-1;
	char *number;
	int ret;

	if (!tr)
		return -ENODEV;

	/* hash funcs only work with set_ftrace_filter */
	if (!enable)
		return -EINVAL;

	ops = param ? &snapshot_count_probe_ops :  &snapshot_probe_ops;

	if (glob[0] == '!')
		return unregister_ftrace_function_probe_func(glob+1, tr, ops);

	if (!param)
		goto out_reg;

	number = strsep(&param, ":");

	if (!strlen(number))
		goto out_reg;

	/*
	 * We use the callback data field (which is a pointer)
	 * as our counter.
	 */
	ret = kstrtoul(number, 0, (unsigned long *)&count);
	if (ret)
		return ret;

 out_reg:
	ret = tracing_alloc_snapshot_instance(tr);
	if (ret < 0)
		goto out;

	ret = register_ftrace_function_probe(glob, tr, ops, count);

 out:
	return ret < 0 ? ret : 0;
}

static struct ftrace_func_command ftrace_snapshot_cmd = {
	.name			= "snapshot",
	.func			= ftrace_trace_snapshot_callback,
};

static __init int register_snapshot_cmd(void)
{
	return register_ftrace_command(&ftrace_snapshot_cmd);
}
#else
static inline __init int register_snapshot_cmd(void) { return 0; }
#endif /* defined(CONFIG_TRACER_SNAPSHOT) && defined(CONFIG_DYNAMIC_FTRACE) */

static struct dentry *tracing_get_dentry(struct trace_array *tr)
{
	if (WARN_ON(!tr->dir))
		return ERR_PTR(-ENODEV);

	/* Top directory uses NULL as the parent */
	if (tr->flags & TRACE_ARRAY_FL_GLOBAL)
		return NULL;

	/* All sub buffers have a descriptor */
	return tr->dir;
}

static struct dentry *tracing_dentry_percpu(struct trace_array *tr, int cpu)
{
	struct dentry *d_tracer;

	if (tr->percpu_dir)
		return tr->percpu_dir;

	d_tracer = tracing_get_dentry(tr);
	if (IS_ERR(d_tracer))
		return NULL;

	tr->percpu_dir = tracefs_create_dir("per_cpu", d_tracer);

	MEM_FAIL(!tr->percpu_dir,
		  "Could not create tracefs directory 'per_cpu/%d'\n", cpu);

	return tr->percpu_dir;
}

static struct dentry *
trace_create_cpu_file(const char *name, umode_t mode, struct dentry *parent,
		      void *data, long cpu, const struct file_operations *fops)
{
	struct dentry *ret = trace_create_file(name, mode, parent, data, fops);

	if (ret) /* See tracing_get_cpu() */
		d_inode(ret)->i_cdev = (void *)(cpu + 1);
	return ret;
}

static void
tracing_init_tracefs_percpu(struct trace_array *tr, long cpu)
{
	struct dentry *d_percpu = tracing_dentry_percpu(tr, cpu);
	struct dentry *d_cpu;
	char cpu_dir[30]; /* 30 characters should be more than enough */

	if (!d_percpu)
		return;

	snprintf(cpu_dir, 30, "cpu%ld", cpu);
	d_cpu = tracefs_create_dir(cpu_dir, d_percpu);
	if (!d_cpu) {
		pr_warn("Could not create tracefs '%s' entry\n", cpu_dir);
		return;
	}

	/* per cpu trace_pipe */
	trace_create_cpu_file("trace_pipe", TRACE_MODE_READ, d_cpu,
				tr, cpu, &tracing_pipe_fops);

	/* per cpu trace */
	trace_create_cpu_file("trace", TRACE_MODE_WRITE, d_cpu,
				tr, cpu, &tracing_fops);

	trace_create_cpu_file("trace_pipe_raw", TRACE_MODE_READ, d_cpu,
				tr, cpu, &tracing_buffers_fops);

	trace_create_cpu_file("stats", TRACE_MODE_READ, d_cpu,
				tr, cpu, &tracing_stats_fops);

	trace_create_cpu_file("buffer_size_kb", TRACE_MODE_READ, d_cpu,
				tr, cpu, &tracing_entries_fops);

#ifdef CONFIG_TRACER_SNAPSHOT
	trace_create_cpu_file("snapshot", TRACE_MODE_WRITE, d_cpu,
				tr, cpu, &snapshot_fops);

	trace_create_cpu_file("snapshot_raw", TRACE_MODE_READ, d_cpu,
				tr, cpu, &snapshot_raw_fops);
#endif
}

#ifdef CONFIG_FTRACE_SELFTEST
/* Let selftest have access to static functions in this file */
#include "trace_selftest.c"
#endif

static ssize_t
trace_options_read(struct file *filp, char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	struct trace_option_dentry *topt = filp->private_data;
	char *buf;

	if (topt->flags->val & topt->opt->bit)
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
trace_options_write(struct file *filp, const char __user *ubuf, size_t cnt,
			 loff_t *ppos)
{
	struct trace_option_dentry *topt = filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	if (!!(topt->flags->val & topt->opt->bit) != val) {
		mutex_lock(&trace_types_lock);
		ret = __set_tracer_option(topt->tr, topt->flags,
					  topt->opt, !val);
		mutex_unlock(&trace_types_lock);
		if (ret)
			return ret;
	}

	*ppos += cnt;

	return cnt;
}


static const struct file_operations trace_options_fops = {
	.open = tracing_open_generic,
	.read = trace_options_read,
	.write = trace_options_write,
	.llseek	= generic_file_llseek,
};

/*
 * In order to pass in both the trace_array descriptor as well as the index
 * to the flag that the trace option file represents, the trace_array
 * has a character array of trace_flags_index[], which holds the index
 * of the bit for the flag it represents. index[0] == 0, index[1] == 1, etc.
 * The address of this character array is passed to the flag option file
 * read/write callbacks.
 *
 * In order to extract both the index and the trace_array descriptor,
 * get_tr_index() uses the following algorithm.
 *
 *   idx = *ptr;
 *
 * As the pointer itself contains the address of the index (remember
 * index[1] == 1).
 *
 * Then to get the trace_array descriptor, by subtracting that index
 * from the ptr, we get to the start of the index itself.
 *
 *   ptr - idx == &index[0]
 *
 * Then a simple container_of() from that pointer gets us to the
 * trace_array descriptor.
 */
static void get_tr_index(void *data, struct trace_array **ptr,
			 unsigned int *pindex)
{
	*pindex = *(unsigned char *)data;

	*ptr = container_of(data - *pindex, struct trace_array,
			    trace_flags_index);
}

static ssize_t
trace_options_core_read(struct file *filp, char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	void *tr_index = filp->private_data;
	struct trace_array *tr;
	unsigned int index;
	char *buf;

	get_tr_index(tr_index, &tr, &index);

	if (tr->trace_flags & (1 << index))
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
trace_options_core_write(struct file *filp, const char __user *ubuf, size_t cnt,
			 loff_t *ppos)
{
	void *tr_index = filp->private_data;
	struct trace_array *tr;
	unsigned int index;
	unsigned long val;
	int ret;

	get_tr_index(tr_index, &tr, &index);

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);
	ret = set_tracer_flag(tr, 1 << index, val);
	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

static const struct file_operations trace_options_core_fops = {
	.open = tracing_open_generic,
	.read = trace_options_core_read,
	.write = trace_options_core_write,
	.llseek = generic_file_llseek,
};

struct dentry *trace_create_file(const char *name,
				 umode_t mode,
				 struct dentry *parent,
				 void *data,
				 const struct file_operations *fops)
{
	struct dentry *ret;

	ret = tracefs_create_file(name, mode, parent, data, fops);
	if (!ret)
		pr_warn("Could not create tracefs '%s' entry\n", name);

	return ret;
}


static struct dentry *trace_options_init_dentry(struct trace_array *tr)
{
	struct dentry *d_tracer;

	if (tr->options)
		return tr->options;

	d_tracer = tracing_get_dentry(tr);
	if (IS_ERR(d_tracer))
		return NULL;

	tr->options = tracefs_create_dir("options", d_tracer);
	if (!tr->options) {
		pr_warn("Could not create tracefs directory 'options'\n");
		return NULL;
	}

	return tr->options;
}

static void
create_trace_option_file(struct trace_array *tr,
			 struct trace_option_dentry *topt,
			 struct tracer_flags *flags,
			 struct tracer_opt *opt)
{
	struct dentry *t_options;

	t_options = trace_options_init_dentry(tr);
	if (!t_options)
		return;

	topt->flags = flags;
	topt->opt = opt;
	topt->tr = tr;

	topt->entry = trace_create_file(opt->name, TRACE_MODE_WRITE,
					t_options, topt, &trace_options_fops);

}

static void
create_trace_option_files(struct trace_array *tr, struct tracer *tracer)
{
	struct trace_option_dentry *topts;
	struct trace_options *tr_topts;
	struct tracer_flags *flags;
	struct tracer_opt *opts;
	int cnt;
	int i;

	if (!tracer)
		return;

	flags = tracer->flags;

	if (!flags || !flags->opts)
		return;

	/*
	 * If this is an instance, only create flags for tracers
	 * the instance may have.
	 */
	if (!trace_ok_for_array(tracer, tr))
		return;

	for (i = 0; i < tr->nr_topts; i++) {
		/* Make sure there's no duplicate flags. */
		if (WARN_ON_ONCE(tr->topts[i].tracer->flags == tracer->flags))
			return;
	}

	opts = flags->opts;

	for (cnt = 0; opts[cnt].name; cnt++)
		;

	topts = kcalloc(cnt + 1, sizeof(*topts), GFP_KERNEL);
	if (!topts)
		return;

	tr_topts = krealloc(tr->topts, sizeof(*tr->topts) * (tr->nr_topts + 1),
			    GFP_KERNEL);
	if (!tr_topts) {
		kfree(topts);
		return;
	}

	tr->topts = tr_topts;
	tr->topts[tr->nr_topts].tracer = tracer;
	tr->topts[tr->nr_topts].topts = topts;
	tr->nr_topts++;

	for (cnt = 0; opts[cnt].name; cnt++) {
		create_trace_option_file(tr, &topts[cnt], flags,
					 &opts[cnt]);
		MEM_FAIL(topts[cnt].entry == NULL,
			  "Failed to create trace option: %s",
			  opts[cnt].name);
	}
}

static struct dentry *
create_trace_option_core_file(struct trace_array *tr,
			      const char *option, long index)
{
	struct dentry *t_options;

	t_options = trace_options_init_dentry(tr);
	if (!t_options)
		return NULL;

	return trace_create_file(option, TRACE_MODE_WRITE, t_options,
				 (void *)&tr->trace_flags_index[index],
				 &trace_options_core_fops);
}

static void create_trace_options_dir(struct trace_array *tr)
{
	struct dentry *t_options;
	bool top_level = tr == &global_trace;
	int i;

	t_options = trace_options_init_dentry(tr);
	if (!t_options)
		return;

	for (i = 0; trace_options[i]; i++) {
		if (top_level ||
		    !((1 << i) & TOP_LEVEL_TRACE_FLAGS))
			create_trace_option_core_file(tr, trace_options[i], i);
	}
}

static ssize_t
rb_simple_read(struct file *filp, char __user *ubuf,
	       size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[64];
	int r;

	r = tracer_tracing_is_on(tr);
	r = sprintf(buf, "%d\n", r);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
rb_simple_write(struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (buffer) {
		mutex_lock(&trace_types_lock);
		if (!!val == tracer_tracing_is_on(tr)) {
			val = 0; /* do nothing */
		} else if (val) {
			tracer_tracing_on(tr);
			if (tr->current_trace->start)
				tr->current_trace->start(tr);
		} else {
			tracer_tracing_off(tr);
			if (tr->current_trace->stop)
				tr->current_trace->stop(tr);
			/* Wake up any waiters */
			ring_buffer_wake_waiters(buffer, RING_BUFFER_ALL_CPUS);
		}
		mutex_unlock(&trace_types_lock);
	}

	(*ppos)++;

	return cnt;
}

static const struct file_operations rb_simple_fops = {
	.open		= tracing_open_generic_tr,
	.read		= rb_simple_read,
	.write		= rb_simple_write,
	.release	= tracing_release_generic_tr,
	.llseek		= default_llseek,
};

static ssize_t
buffer_percent_read(struct file *filp, char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[64];
	int r;

	r = tr->buffer_percent;
	r = sprintf(buf, "%d\n", r);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
buffer_percent_write(struct file *filp, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (val > 100)
		return -EINVAL;

	if (!val)
		val = 1;

	tr->buffer_percent = val;

	(*ppos)++;

	return cnt;
}

static const struct file_operations buffer_percent_fops = {
	.open		= tracing_open_generic_tr,
	.read		= buffer_percent_read,
	.write		= buffer_percent_write,
	.release	= tracing_release_generic_tr,
	.llseek		= default_llseek,
};

static struct dentry *trace_instance_dir;

static void
init_tracer_tracefs(struct trace_array *tr, struct dentry *d_tracer);

static int
allocate_trace_buffer(struct trace_array *tr, struct array_buffer *buf, int size)
{
	enum ring_buffer_flags rb_flags;

	rb_flags = tr->trace_flags & TRACE_ITER_OVERWRITE ? RB_FL_OVERWRITE : 0;

	buf->tr = tr;

	buf->buffer = ring_buffer_alloc(size, rb_flags);
	if (!buf->buffer)
		return -ENOMEM;

	buf->data = alloc_percpu(struct trace_array_cpu);
	if (!buf->data) {
		ring_buffer_free(buf->buffer);
		buf->buffer = NULL;
		return -ENOMEM;
	}

	/* Allocate the first page for all buffers */
	set_buffer_entries(&tr->array_buffer,
			   ring_buffer_size(tr->array_buffer.buffer, 0));

	return 0;
}

static int allocate_trace_buffers(struct trace_array *tr, int size)
{
	int ret;

	ret = allocate_trace_buffer(tr, &tr->array_buffer, size);
	if (ret)
		return ret;

#ifdef CONFIG_TRACER_MAX_TRACE
	ret = allocate_trace_buffer(tr, &tr->max_buffer,
				    allocate_snapshot ? size : 1);
	if (MEM_FAIL(ret, "Failed to allocate trace buffer\n")) {
		ring_buffer_free(tr->array_buffer.buffer);
		tr->array_buffer.buffer = NULL;
		free_percpu(tr->array_buffer.data);
		tr->array_buffer.data = NULL;
		return -ENOMEM;
	}
	tr->allocated_snapshot = allocate_snapshot;

	/*
	 * Only the top level trace array gets its snapshot allocated
	 * from the kernel command line.
	 */
	allocate_snapshot = false;
#endif

	return 0;
}

static void free_trace_buffer(struct array_buffer *buf)
{
	if (buf->buffer) {
		ring_buffer_free(buf->buffer);
		buf->buffer = NULL;
		free_percpu(buf->data);
		buf->data = NULL;
	}
}

static void free_trace_buffers(struct trace_array *tr)
{
	if (!tr)
		return;

	free_trace_buffer(&tr->array_buffer);

#ifdef CONFIG_TRACER_MAX_TRACE
	free_trace_buffer(&tr->max_buffer);
#endif
}

static void init_trace_flags_index(struct trace_array *tr)
{
	int i;

	/* Used by the trace options files */
	for (i = 0; i < TRACE_FLAGS_MAX_SIZE; i++)
		tr->trace_flags_index[i] = i;
}

static void __update_tracer_options(struct trace_array *tr)
{
	struct tracer *t;

	for (t = trace_types; t; t = t->next)
		add_tracer_options(tr, t);
}

static void update_tracer_options(struct trace_array *tr)
{
	mutex_lock(&trace_types_lock);
	tracer_options_updated = true;
	__update_tracer_options(tr);
	mutex_unlock(&trace_types_lock);
}

/* Must have trace_types_lock held */
struct trace_array *trace_array_find(const char *instance)
{
	struct trace_array *tr, *found = NULL;

	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (tr->name && strcmp(tr->name, instance) == 0) {
			found = tr;
			break;
		}
	}

	return found;
}

struct trace_array *trace_array_find_get(const char *instance)
{
	struct trace_array *tr;

	mutex_lock(&trace_types_lock);
	tr = trace_array_find(instance);
	if (tr)
		tr->ref++;
	mutex_unlock(&trace_types_lock);

	return tr;
}

static int trace_array_create_dir(struct trace_array *tr)
{
	int ret;

	tr->dir = tracefs_create_dir(tr->name, trace_instance_dir);
	if (!tr->dir)
		return -EINVAL;

	ret = event_trace_add_tracer(tr->dir, tr);
	if (ret) {
		tracefs_remove(tr->dir);
		return ret;
	}

	init_tracer_tracefs(tr, tr->dir);
	__update_tracer_options(tr);

	return ret;
}

static struct trace_array *trace_array_create(const char *name)
{
	struct trace_array *tr;
	int ret;

	ret = -ENOMEM;
	tr = kzalloc(sizeof(*tr), GFP_KERNEL);
	if (!tr)
		return ERR_PTR(ret);

	tr->name = kstrdup(name, GFP_KERNEL);
	if (!tr->name)
		goto out_free_tr;

	if (!alloc_cpumask_var(&tr->tracing_cpumask, GFP_KERNEL))
		goto out_free_tr;

	tr->trace_flags = global_trace.trace_flags & ~ZEROED_TRACE_FLAGS;

	cpumask_copy(tr->tracing_cpumask, cpu_all_mask);

	raw_spin_lock_init(&tr->start_lock);

	tr->max_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

	tr->current_trace = &nop_trace;

	INIT_LIST_HEAD(&tr->systems);
	INIT_LIST_HEAD(&tr->events);
	INIT_LIST_HEAD(&tr->hist_vars);
	INIT_LIST_HEAD(&tr->err_log);

	if (allocate_trace_buffers(tr, trace_buf_size) < 0)
		goto out_free_tr;

	if (ftrace_allocate_ftrace_ops(tr) < 0)
		goto out_free_tr;

	ftrace_init_trace_array(tr);

	init_trace_flags_index(tr);

	if (trace_instance_dir) {
		ret = trace_array_create_dir(tr);
		if (ret)
			goto out_free_tr;
	} else
		__trace_early_add_events(tr);

	list_add(&tr->list, &ftrace_trace_arrays);

	tr->ref++;

	return tr;

 out_free_tr:
	ftrace_free_ftrace_ops(tr);
	free_trace_buffers(tr);
	free_cpumask_var(tr->tracing_cpumask);
	kfree(tr->name);
	kfree(tr);

	return ERR_PTR(ret);
}

static int instance_mkdir(const char *name)
{
	struct trace_array *tr;
	int ret;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	ret = -EEXIST;
	if (trace_array_find(name))
		goto out_unlock;

	tr = trace_array_create(name);

	ret = PTR_ERR_OR_ZERO(tr);

out_unlock:
	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);
	return ret;
}

/**
 * trace_array_get_by_name - Create/Lookup a trace array, given its name.
 * @name: The name of the trace array to be looked up/created.
 *
 * Returns pointer to trace array with given name.
 * NULL, if it cannot be created.
 *
 * NOTE: This function increments the reference counter associated with the
 * trace array returned. This makes sure it cannot be freed while in use.
 * Use trace_array_put() once the trace array is no longer needed.
 * If the trace_array is to be freed, trace_array_destroy() needs to
 * be called after the trace_array_put(), or simply let user space delete
 * it from the tracefs instances directory. But until the
 * trace_array_put() is called, user space can not delete it.
 *
 */
struct trace_array *trace_array_get_by_name(const char *name)
{
	struct trace_array *tr;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (tr->name && strcmp(tr->name, name) == 0)
			goto out_unlock;
	}

	tr = trace_array_create(name);

	if (IS_ERR(tr))
		tr = NULL;
out_unlock:
	if (tr)
		tr->ref++;

	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);
	return tr;
}
EXPORT_SYMBOL_GPL(trace_array_get_by_name);

static int __remove_instance(struct trace_array *tr)
{
	int i;

	/* Reference counter for a newly created trace array = 1. */
	if (tr->ref > 1 || (tr->current_trace && tr->trace_ref))
		return -EBUSY;

	list_del(&tr->list);

	/* Disable all the flags that were enabled coming in */
	for (i = 0; i < TRACE_FLAGS_MAX_SIZE; i++) {
		if ((1 << i) & ZEROED_TRACE_FLAGS)
			set_tracer_flag(tr, 1 << i, 0);
	}

	tracing_set_nop(tr);
	clear_ftrace_function_probes(tr);
	event_trace_del_tracer(tr);
	ftrace_clear_pids(tr);
	ftrace_destroy_function_files(tr);
	tracefs_remove(tr->dir);
	free_percpu(tr->last_func_repeats);
	free_trace_buffers(tr);

	for (i = 0; i < tr->nr_topts; i++) {
		kfree(tr->topts[i].topts);
	}
	kfree(tr->topts);

	free_cpumask_var(tr->tracing_cpumask);
	kfree(tr->name);
	kfree(tr);

	return 0;
}

int trace_array_destroy(struct trace_array *this_tr)
{
	struct trace_array *tr;
	int ret;

	if (!this_tr)
		return -EINVAL;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	ret = -ENODEV;

	/* Making sure trace array exists before destroying it. */
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (tr == this_tr) {
			ret = __remove_instance(tr);
			break;
		}
	}

	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(trace_array_destroy);

static int instance_rmdir(const char *name)
{
	struct trace_array *tr;
	int ret;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	ret = -ENODEV;
	tr = trace_array_find(name);
	if (tr)
		ret = __remove_instance(tr);

	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);

	return ret;
}

static __init void create_trace_instances(struct dentry *d_tracer)
{
	struct trace_array *tr;

	trace_instance_dir = tracefs_create_instance_dir("instances", d_tracer,
							 instance_mkdir,
							 instance_rmdir);
	if (MEM_FAIL(!trace_instance_dir, "Failed to create instances directory\n"))
		return;

	mutex_lock(&event_mutex);
	mutex_lock(&trace_types_lock);

	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		if (!tr->name)
			continue;
		if (MEM_FAIL(trace_array_create_dir(tr) < 0,
			     "Failed to create instance directory\n"))
			break;
	}

	mutex_unlock(&trace_types_lock);
	mutex_unlock(&event_mutex);
}

static void
init_tracer_tracefs(struct trace_array *tr, struct dentry *d_tracer)
{
	struct trace_event_file *file;
	int cpu;

	trace_create_file("available_tracers", TRACE_MODE_READ, d_tracer,
			tr, &show_traces_fops);

	trace_create_file("current_tracer", TRACE_MODE_WRITE, d_tracer,
			tr, &set_tracer_fops);

	trace_create_file("tracing_cpumask", TRACE_MODE_WRITE, d_tracer,
			  tr, &tracing_cpumask_fops);

	trace_create_file("trace_options", TRACE_MODE_WRITE, d_tracer,
			  tr, &tracing_iter_fops);

	trace_create_file("trace", TRACE_MODE_WRITE, d_tracer,
			  tr, &tracing_fops);

	trace_create_file("trace_pipe", TRACE_MODE_READ, d_tracer,
			  tr, &tracing_pipe_fops);

	trace_create_file("buffer_size_kb", TRACE_MODE_WRITE, d_tracer,
			  tr, &tracing_entries_fops);

	trace_create_file("buffer_total_size_kb", TRACE_MODE_READ, d_tracer,
			  tr, &tracing_total_entries_fops);

	trace_create_file("free_buffer", 0200, d_tracer,
			  tr, &tracing_free_buffer_fops);

	trace_create_file("trace_marker", 0220, d_tracer,
			  tr, &tracing_mark_fops);

	file = __find_event_file(tr, "ftrace", "print");
	if (file && file->dir)
		trace_create_file("trigger", TRACE_MODE_WRITE, file->dir,
				  file, &event_trigger_fops);
	tr->trace_marker_file = file;

	trace_create_file("trace_marker_raw", 0220, d_tracer,
			  tr, &tracing_mark_raw_fops);

	trace_create_file("trace_clock", TRACE_MODE_WRITE, d_tracer, tr,
			  &trace_clock_fops);

	trace_create_file("tracing_on", TRACE_MODE_WRITE, d_tracer,
			  tr, &rb_simple_fops);

	trace_create_file("timestamp_mode", TRACE_MODE_READ, d_tracer, tr,
			  &trace_time_stamp_mode_fops);

	tr->buffer_percent = 50;

	trace_create_file("buffer_percent", TRACE_MODE_READ, d_tracer,
			tr, &buffer_percent_fops);

	create_trace_options_dir(tr);

	trace_create_maxlat_file(tr, d_tracer);

	if (ftrace_create_function_files(tr, d_tracer))
		MEM_FAIL(1, "Could not allocate function filter files");

#ifdef CONFIG_TRACER_SNAPSHOT
	trace_create_file("snapshot", TRACE_MODE_WRITE, d_tracer,
			  tr, &snapshot_fops);
#endif

	trace_create_file("error_log", TRACE_MODE_WRITE, d_tracer,
			  tr, &tracing_err_log_fops);

	for_each_tracing_cpu(cpu)
		tracing_init_tracefs_percpu(tr, cpu);

	ftrace_init_tracefs(tr, d_tracer);
}

static struct vfsmount *trace_automount(struct dentry *mntpt, void *ingore)
{
	struct vfsmount *mnt;
	struct file_system_type *type;

	/*
	 * To maintain backward compatibility for tools that mount
	 * debugfs to get to the tracing facility, tracefs is automatically
	 * mounted to the debugfs/tracing directory.
	 */
	type = get_fs_type("tracefs");
	if (!type)
		return NULL;
	mnt = vfs_submount(mntpt, type, "tracefs", NULL);
	put_filesystem(type);
	if (IS_ERR(mnt))
		return NULL;
	mntget(mnt);

	return mnt;
}

/**
 * tracing_init_dentry - initialize top level trace array
 *
 * This is called when creating files or directories in the tracing
 * directory. It is called via fs_initcall() by any of the boot up code
 * and expects to return the dentry of the top level tracing directory.
 */
int tracing_init_dentry(void)
{
	struct trace_array *tr = &global_trace;

	if (security_locked_down(LOCKDOWN_TRACEFS)) {
		pr_warn("Tracing disabled due to lockdown\n");
		return -EPERM;
	}

	/* The top level trace array uses  NULL as parent */
	if (tr->dir)
		return 0;

	if (WARN_ON(!tracefs_initialized()))
		return -ENODEV;

	/*
	 * As there may still be users that expect the tracing
	 * files to exist in debugfs/tracing, we must automount
	 * the tracefs file system there, so older tools still
	 * work with the newer kernel.
	 */
	tr->dir = debugfs_create_automount("tracing", NULL,
					   trace_automount, NULL);

	return 0;
}

extern struct trace_eval_map *__start_ftrace_eval_maps[];
extern struct trace_eval_map *__stop_ftrace_eval_maps[];

static struct workqueue_struct *eval_map_wq __initdata;
static struct work_struct eval_map_work __initdata;

static void __init eval_map_work_func(struct work_struct *work)
{
	int len;

	len = __stop_ftrace_eval_maps - __start_ftrace_eval_maps;
	trace_insert_eval_map(NULL, __start_ftrace_eval_maps, len);
}

static int __init trace_eval_init(void)
{
	INIT_WORK(&eval_map_work, eval_map_work_func);

	eval_map_wq = alloc_workqueue("eval_map_wq", WQ_UNBOUND, 0);
	if (!eval_map_wq) {
		pr_err("Unable to allocate eval_map_wq\n");
		/* Do work here */
		eval_map_work_func(&eval_map_work);
		return -ENOMEM;
	}

	queue_work(eval_map_wq, &eval_map_work);
	return 0;
}

static int __init trace_eval_sync(void)
{
	/* Make sure the eval map updates are finished */
	if (eval_map_wq)
		destroy_workqueue(eval_map_wq);
	return 0;
}

late_initcall_sync(trace_eval_sync);


#ifdef CONFIG_MODULES
static void trace_module_add_evals(struct module *mod)
{
	if (!mod->num_trace_evals)
		return;

	/*
	 * Modules with bad taint do not have events created, do
	 * not bother with enums either.
	 */
	if (trace_module_has_bad_taint(mod))
		return;

	trace_insert_eval_map(mod, mod->trace_evals, mod->num_trace_evals);
}

#ifdef CONFIG_TRACE_EVAL_MAP_FILE
static void trace_module_remove_evals(struct module *mod)
{
	union trace_eval_map_item *map;
	union trace_eval_map_item **last = &trace_eval_maps;

	if (!mod->num_trace_evals)
		return;

	mutex_lock(&trace_eval_mutex);

	map = trace_eval_maps;

	while (map) {
		if (map->head.mod == mod)
			break;
		map = trace_eval_jmp_to_tail(map);
		last = &map->tail.next;
		map = map->tail.next;
	}
	if (!map)
		goto out;

	*last = trace_eval_jmp_to_tail(map)->tail.next;
	kfree(map);
 out:
	mutex_unlock(&trace_eval_mutex);
}
#else
static inline void trace_module_remove_evals(struct module *mod) { }
#endif /* CONFIG_TRACE_EVAL_MAP_FILE */

static int trace_module_notify(struct notifier_block *self,
			       unsigned long val, void *data)
{
	struct module *mod = data;

	switch (val) {
	case MODULE_STATE_COMING:
		trace_module_add_evals(mod);
		break;
	case MODULE_STATE_GOING:
		trace_module_remove_evals(mod);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block trace_module_nb = {
	.notifier_call = trace_module_notify,
	.priority = 0,
};
#endif /* CONFIG_MODULES */

static __init int tracer_init_tracefs(void)
{
	int ret;

	trace_access_lock_init();

	ret = tracing_init_dentry();
	if (ret)
		return 0;

	event_trace_init();

	init_tracer_tracefs(&global_trace, NULL);
	ftrace_init_tracefs_toplevel(&global_trace, NULL);

	trace_create_file("tracing_thresh", TRACE_MODE_WRITE, NULL,
			&global_trace, &tracing_thresh_fops);

	trace_create_file("README", TRACE_MODE_READ, NULL,
			NULL, &tracing_readme_fops);

	trace_create_file("saved_cmdlines", TRACE_MODE_READ, NULL,
			NULL, &tracing_saved_cmdlines_fops);

	trace_create_file("saved_cmdlines_size", TRACE_MODE_WRITE, NULL,
			  NULL, &tracing_saved_cmdlines_size_fops);

	trace_create_file("saved_tgids", TRACE_MODE_READ, NULL,
			NULL, &tracing_saved_tgids_fops);

	trace_eval_init();

	trace_create_eval_file(NULL);

#ifdef CONFIG_MODULES
	register_module_notifier(&trace_module_nb);
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
	trace_create_file("dyn_ftrace_total_info", TRACE_MODE_READ, NULL,
			NULL, &tracing_dyn_info_fops);
#endif

	create_trace_instances(NULL);

	update_tracer_options(&global_trace);

	return 0;
}

fs_initcall(tracer_init_tracefs);

static int trace_panic_handler(struct notifier_block *this,
			       unsigned long event, void *unused)
{
	if (ftrace_dump_on_oops)
		ftrace_dump(ftrace_dump_on_oops);
	return NOTIFY_OK;
}

static struct notifier_block trace_panic_notifier = {
	.notifier_call  = trace_panic_handler,
	.next           = NULL,
	.priority       = 150   /* priority: INT_MAX >= x >= 0 */
};

static int trace_die_handler(struct notifier_block *self,
			     unsigned long val,
			     void *data)
{
	switch (val) {
	case DIE_OOPS:
		if (ftrace_dump_on_oops)
			ftrace_dump(ftrace_dump_on_oops);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block trace_die_notifier = {
	.notifier_call = trace_die_handler,
	.priority = 200
};

/*
 * printk is set to max of 1024, we really don't need it that big.
 * Nothing should be printing 1000 characters anyway.
 */
#define TRACE_MAX_PRINT		1000

/*
 * Define here KERN_TRACE so that we have one place to modify
 * it if we decide to change what log level the ftrace dump
 * should be at.
 */
#define KERN_TRACE		KERN_EMERG

void
trace_printk_seq(struct trace_seq *s)
{
	/* Probably should print a warning here. */
	if (s->seq.len >= TRACE_MAX_PRINT)
		s->seq.len = TRACE_MAX_PRINT;

	/*
	 * More paranoid code. Although the buffer size is set to
	 * PAGE_SIZE, and TRACE_MAX_PRINT is 1000, this is just
	 * an extra layer of protection.
	 */
	if (WARN_ON_ONCE(s->seq.len >= s->seq.size))
		s->seq.len = s->seq.size - 1;

	/* should be zero ended, but we are paranoid. */
	s->buffer[s->seq.len] = 0;

	printk(KERN_TRACE "%s", s->buffer);

	trace_seq_init(s);
}

void trace_init_global_iter(struct trace_iterator *iter)
{
	iter->tr = &global_trace;
	iter->trace = iter->tr->current_trace;
	iter->cpu_file = RING_BUFFER_ALL_CPUS;
	iter->array_buffer = &global_trace.array_buffer;

	if (iter->trace && iter->trace->open)
		iter->trace->open(iter);

	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter->array_buffer->buffer))
		iter->iter_flags |= TRACE_FILE_ANNOTATE;

	/* Output in nanoseconds only if we are using a clock in nanoseconds. */
	if (trace_clocks[iter->tr->clock_id].in_ns)
		iter->iter_flags |= TRACE_FILE_TIME_IN_NS;

	/* Can not use kmalloc for iter.temp and iter.fmt */
	iter->temp = static_temp_buf;
	iter->temp_size = STATIC_TEMP_BUF_SIZE;
	iter->fmt = static_fmt_buf;
	iter->fmt_size = STATIC_FMT_BUF_SIZE;
}

void ftrace_dump(enum ftrace_dump_mode oops_dump_mode)
{
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	static atomic_t dump_running;
	struct trace_array *tr = &global_trace;
	unsigned int old_userobj;
	unsigned long flags;
	int cnt = 0, cpu;

	/* Only allow one dump user at a time. */
	if (atomic_inc_return(&dump_running) != 1) {
		atomic_dec(&dump_running);
		return;
	}

	/*
	 * Always turn off tracing when we dump.
	 * We don't need to show trace output of what happens
	 * between multiple crashes.
	 *
	 * If the user does a sysrq-z, then they can re-enable
	 * tracing with echo 1 > tracing_on.
	 */
	tracing_off();

	local_irq_save(flags);

	/* Simulate the iterator */
	trace_init_global_iter(&iter);

	for_each_tracing_cpu(cpu) {
		atomic_inc(&per_cpu_ptr(iter.array_buffer->data, cpu)->disabled);
	}

	old_userobj = tr->trace_flags & TRACE_ITER_SYM_USEROBJ;

	/* don't look at user memory in panic mode */
	tr->trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	switch (oops_dump_mode) {
	case DUMP_ALL:
		iter.cpu_file = RING_BUFFER_ALL_CPUS;
		break;
	case DUMP_ORIG:
		iter.cpu_file = raw_smp_processor_id();
		break;
	case DUMP_NONE:
		goto out_enable;
	default:
		printk(KERN_TRACE "Bad dumping mode, switching to all CPUs dump\n");
		iter.cpu_file = RING_BUFFER_ALL_CPUS;
	}

	printk(KERN_TRACE "Dumping ftrace buffer:\n");

	/* Did function tracer already get disabled? */
	if (ftrace_is_dead()) {
		printk("# WARNING: FUNCTION TRACING IS CORRUPTED\n");
		printk("#          MAY BE MISSING FUNCTION EVENTS\n");
	}

	/*
	 * We need to stop all tracing on all CPUS to read
	 * the next buffer. This is a bit expensive, but is
	 * not done often. We fill all what we can read,
	 * and then release the locks again.
	 */

	while (!trace_empty(&iter)) {

		if (!cnt)
			printk(KERN_TRACE "---------------------------------\n");

		cnt++;

		trace_iterator_reset(&iter);
		iter.iter_flags |= TRACE_FILE_LAT_FMT;

		if (trace_find_next_entry_inc(&iter) != NULL) {
			int ret;

			ret = print_trace_line(&iter);
			if (ret != TRACE_TYPE_NO_CONSUME)
				trace_consume(&iter);
		}
		touch_nmi_watchdog();

		trace_printk_seq(&iter.seq);
	}

	if (!cnt)
		printk(KERN_TRACE "   (ftrace buffer empty)\n");
	else
		printk(KERN_TRACE "---------------------------------\n");

 out_enable:
	tr->trace_flags |= old_userobj;

	for_each_tracing_cpu(cpu) {
		atomic_dec(&per_cpu_ptr(iter.array_buffer->data, cpu)->disabled);
	}
	atomic_dec(&dump_running);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(ftrace_dump);

#define WRITE_BUFSIZE  4096

ssize_t trace_parse_run_command(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos,
				int (*createfn)(const char *))
{
	char *kbuf, *buf, *tmp;
	int ret = 0;
	size_t done = 0;
	size_t size;

	kbuf = kmalloc(WRITE_BUFSIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	while (done < count) {
		size = count - done;

		if (size >= WRITE_BUFSIZE)
			size = WRITE_BUFSIZE - 1;

		if (copy_from_user(kbuf, buffer + done, size)) {
			ret = -EFAULT;
			goto out;
		}
		kbuf[size] = '\0';
		buf = kbuf;
		do {
			tmp = strchr(buf, '\n');
			if (tmp) {
				*tmp = '\0';
				size = tmp - buf + 1;
			} else {
				size = strlen(buf);
				if (done + size < count) {
					if (buf != kbuf)
						break;
					/* This can accept WRITE_BUFSIZE - 2 ('\n' + '\0') */
					pr_warn("Line length is too long: Should be less than %d\n",
						WRITE_BUFSIZE - 2);
					ret = -EINVAL;
					goto out;
				}
			}
			done += size;

			/* Remove comments */
			tmp = strchr(buf, '#');

			if (tmp)
				*tmp = '\0';

			ret = createfn(buf);
			if (ret)
				goto out;
			buf += size;

		} while (done < count);
	}
	ret = done;

out:
	kfree(kbuf);

	return ret;
}

__init static int tracer_alloc_buffers(void)
{
	int ring_buf_size;
	int ret = -ENOMEM;


	if (security_locked_down(LOCKDOWN_TRACEFS)) {
		pr_warn("Tracing disabled due to lockdown\n");
		return -EPERM;
	}

	/*
	 * Make sure we don't accidentally add more trace options
	 * than we have bits for.
	 */
	BUILD_BUG_ON(TRACE_ITER_LAST_BIT > TRACE_FLAGS_MAX_SIZE);

	if (!alloc_cpumask_var(&tracing_buffer_mask, GFP_KERNEL))
		goto out;

	if (!alloc_cpumask_var(&global_trace.tracing_cpumask, GFP_KERNEL))
		goto out_free_buffer_mask;

	/* Only allocate trace_printk buffers if a trace_printk exists */
	if (&__stop___trace_bprintk_fmt != &__start___trace_bprintk_fmt)
		/* Must be called before global_trace.buffer is allocated */
		trace_printk_init_buffers();

	/* To save memory, keep the ring buffer size to its minimum */
	if (ring_buffer_expanded)
		ring_buf_size = trace_buf_size;
	else
		ring_buf_size = 1;

	cpumask_copy(tracing_buffer_mask, cpu_possible_mask);
	cpumask_copy(global_trace.tracing_cpumask, cpu_all_mask);

	raw_spin_lock_init(&global_trace.start_lock);

	/*
	 * The prepare callbacks allocates some memory for the ring buffer. We
	 * don't free the buffer if the CPU goes down. If we were to free
	 * the buffer, then the user would lose any trace that was in the
	 * buffer. The memory will be removed once the "instance" is removed.
	 */
	ret = cpuhp_setup_state_multi(CPUHP_TRACE_RB_PREPARE,
				      "trace/RB:preapre", trace_rb_cpu_prepare,
				      NULL);
	if (ret < 0)
		goto out_free_cpumask;
	/* Used for event triggers */
	ret = -ENOMEM;
	temp_buffer = ring_buffer_alloc(PAGE_SIZE, RB_FL_OVERWRITE);
	if (!temp_buffer)
		goto out_rm_hp_state;

	if (trace_create_savedcmd() < 0)
		goto out_free_temp_buffer;

	/* TODO: make the number of buffers hot pluggable with CPUS */
	if (allocate_trace_buffers(&global_trace, ring_buf_size) < 0) {
		MEM_FAIL(1, "tracer: failed to allocate ring buffer!\n");
		goto out_free_savedcmd;
	}

	if (global_trace.buffer_disabled)
		tracing_off();

	if (trace_boot_clock) {
		ret = tracing_set_clock(&global_trace, trace_boot_clock);
		if (ret < 0)
			pr_warn("Trace clock %s not defined, going back to default\n",
				trace_boot_clock);
	}

	/*
	 * register_tracer() might reference current_trace, so it
	 * needs to be set before we register anything. This is
	 * just a bootstrap of current_trace anyway.
	 */
	global_trace.current_trace = &nop_trace;

	global_trace.max_lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

	ftrace_init_global_array_ops(&global_trace);

	init_trace_flags_index(&global_trace);

	register_tracer(&nop_trace);

	/* Function tracing may start here (via kernel command line) */
	init_function_trace();

	/* All seems OK, enable tracing */
	tracing_disabled = 0;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &trace_panic_notifier);

	register_die_notifier(&trace_die_notifier);

	global_trace.flags = TRACE_ARRAY_FL_GLOBAL;

	INIT_LIST_HEAD(&global_trace.systems);
	INIT_LIST_HEAD(&global_trace.events);
	INIT_LIST_HEAD(&global_trace.hist_vars);
	INIT_LIST_HEAD(&global_trace.err_log);
	list_add(&global_trace.list, &ftrace_trace_arrays);

	apply_trace_boot_options();

	register_snapshot_cmd();

	test_can_verify();

	return 0;

out_free_savedcmd:
	free_saved_cmdlines_buffer(savedcmd);
out_free_temp_buffer:
	ring_buffer_free(temp_buffer);
out_rm_hp_state:
	cpuhp_remove_multi_state(CPUHP_TRACE_RB_PREPARE);
out_free_cpumask:
	free_cpumask_var(global_trace.tracing_cpumask);
out_free_buffer_mask:
	free_cpumask_var(tracing_buffer_mask);
out:
	return ret;
}

void __init early_trace_init(void)
{
	if (tracepoint_printk) {
		tracepoint_print_iter =
			kzalloc(sizeof(*tracepoint_print_iter), GFP_KERNEL);
		if (MEM_FAIL(!tracepoint_print_iter,
			     "Failed to allocate trace iterator\n"))
			tracepoint_printk = 0;
		else
			static_key_enable(&tracepoint_printk_key.key);
	}
	tracer_alloc_buffers();
}

void __init trace_init(void)
{
	trace_event_init();
}

__init static void clear_boot_tracer(void)
{
	/*
	 * The default tracer at boot buffer is an init section.
	 * This function is called in lateinit. If we did not
	 * find the boot tracer, then clear it out, to prevent
	 * later registration from accessing the buffer that is
	 * about to be freed.
	 */
	if (!default_bootup_tracer)
		return;

	printk(KERN_INFO "ftrace bootup tracer '%s' not registered.\n",
	       default_bootup_tracer);
	default_bootup_tracer = NULL;
}

#ifdef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
__init static void tracing_set_default_clock(void)
{
	/* sched_clock_stable() is determined in late_initcall */
	if (!trace_boot_clock && !sched_clock_stable()) {
		if (security_locked_down(LOCKDOWN_TRACEFS)) {
			pr_warn("Can not set tracing clock due to lockdown\n");
			return;
		}

		printk(KERN_WARNING
		       "Unstable clock detected, switching default tracing clock to \"global\"\n"
		       "If you want to keep using the local clock, then add:\n"
		       "  \"trace_clock=local\"\n"
		       "on the kernel command line\n");
		tracing_set_clock(&global_trace, "global");
	}
}
#else
static inline void tracing_set_default_clock(void) { }
#endif

__init static int late_trace_init(void)
{
	if (tracepoint_printk && tracepoint_printk_stop_on_boot) {
		static_key_disable(&tracepoint_printk_key.key);
		tracepoint_printk = 0;
	}

	tracing_set_default_clock();
	clear_boot_tracer();
	return 0;
}

late_initcall_sync(late_trace_init);
