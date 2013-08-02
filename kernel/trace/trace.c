/*
 * ring buffer based function tracer
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Originally taken from the RT patch by:
 *    Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/ring_buffer.h>
#include <generated/utsrelease.h>
#include <linux/stacktrace.h>
#include <linux/writeback.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/irqflags.h>
#include <linux/debugfs.h>
#include <linux/pagemap.h>
#include <linux/hardirq.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/splice.h>
#include <linux/kdebug.h>
#include <linux/string.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/nmi.h>
#include <linux/fs.h>

#include "trace.h"
#include "trace_output.h"

/*
 * On boot up, the ring buffer is set to the minimum size, so that
 * we do not waste memory on systems that are not using tracing.
 */
int ring_buffer_expanded;

/*
 * We need to change this state when a selftest is running.
 * A selftest will lurk into the ring-buffer to count the
 * entries inserted during the selftest although some concurrent
 * insertions into the ring-buffer such as trace_printk could occurred
 * at the same time, giving false positive or negative results.
 */
static bool __read_mostly tracing_selftest_running;

/*
 * If a tracer is running, we do not want to run SELFTEST.
 */
bool __read_mostly tracing_selftest_disabled;

/* For tracers that don't implement custom flags */
static struct tracer_opt dummy_tracer_opt[] = {
	{ }
};

static struct tracer_flags dummy_tracer_flags = {
	.val = 0,
	.opts = dummy_tracer_opt
};

static int dummy_set_flag(u32 old_flags, u32 bit, int set)
{
	return 0;
}

/*
 * Kill all tracing for good (never come back).
 * It is initialized to 1 but will turn to zero if the initialization
 * of the tracer is successful. But that is the only place that sets
 * this back to zero.
 */
static int tracing_disabled = 1;

DEFINE_PER_CPU(int, ftrace_cpu_disabled);

static inline void ftrace_disable_cpu(void)
{
	preempt_disable();
	__this_cpu_inc(ftrace_cpu_disabled);
}

static inline void ftrace_enable_cpu(void)
{
	__this_cpu_dec(ftrace_cpu_disabled);
	preempt_enable();
}

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

static int tracing_set_tracer(const char *buf);

#define MAX_TRACER_SIZE		100
static char bootup_tracer_buf[MAX_TRACER_SIZE] __initdata;
static char *default_bootup_tracer;

static int __init set_cmdline_ftrace(char *str)
{
	strncpy(bootup_tracer_buf, str, MAX_TRACER_SIZE);
	default_bootup_tracer = bootup_tracer_buf;
	/* We are using ftrace early, expand it */
	ring_buffer_expanded = 1;
	return 1;
}
__setup("ftrace=", set_cmdline_ftrace);

static int __init set_ftrace_dump_on_oops(char *str)
{
	if (*str++ != '=' || !*str) {
		ftrace_dump_on_oops = DUMP_ALL;
		return 1;
	}

	if (!strcmp("orig_cpu", str)) {
		ftrace_dump_on_oops = DUMP_ORIG;
                return 1;
        }

        return 0;
}
__setup("ftrace_dump_on_oops", set_ftrace_dump_on_oops);

unsigned long long ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

/*
 * The global_trace is the descriptor that holds the tracing
 * buffers for the live tracing. For each CPU, it contains
 * a link list of pages that will store trace entries. The
 * page descriptor of the pages in the memory is used to hold
 * the link list by linking the lru item in the page descriptor
 * to each of the pages in the buffer per CPU.
 *
 * For each active CPU there is a data field that holds the
 * pages for the buffer for that CPU. Each CPU has the same number
 * of pages allocated for its buffer.
 */
static struct trace_array	global_trace;

static DEFINE_PER_CPU(struct trace_array_cpu, global_trace_cpu);

int filter_current_check_discard(struct ring_buffer *buffer,
				 struct ftrace_event_call *call, void *rec,
				 struct ring_buffer_event *event)
{
	return filter_check_discard(call, rec, buffer, event);
}
EXPORT_SYMBOL_GPL(filter_current_check_discard);

cycle_t ftrace_now(int cpu)
{
	u64 ts;

	/* Early boot up does not have a buffer yet */
	if (!global_trace.buffer)
		return trace_clock_local();

	ts = ring_buffer_time_stamp(global_trace.buffer, cpu);
	ring_buffer_normalize_time_stamp(global_trace.buffer, cpu, &ts);

	return ts;
}

/*
 * The max_tr is used to snapshot the global_trace when a maximum
 * latency is reached. Some tracers will use this to store a maximum
 * trace while it continues examining live traces.
 *
 * The buffers for the max_tr are set up the same as the global_trace.
 * When a snapshot is taken, the link list of the max_tr is swapped
 * with the link list of the global_trace and the buffers are reset for
 * the global_trace so the tracing can continue.
 */
static struct trace_array	max_tr;

static DEFINE_PER_CPU(struct trace_array_cpu, max_tr_data);

/* tracer_enabled is used to toggle activation of a tracer */
static int			tracer_enabled = 1;

/**
 * tracing_is_enabled - return tracer_enabled status
 *
 * This function is used by other tracers to know the status
 * of the tracer_enabled flag.  Tracers may use this function
 * to know if it should enable their features when starting
 * up. See irqsoff tracer for an example (start_irqsoff_tracer).
 */
int tracing_is_enabled(void)
{
	return tracer_enabled;
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

/* current_trace points to the tracer that is currently active */
static struct tracer		*current_trace __read_mostly;

/*
 * trace_types_lock is used to protect the trace_types list.
 */
static DEFINE_MUTEX(trace_types_lock);

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
 *      (not reader page) in ring buffer, and this page will be rewrited
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
	if (cpu == TRACE_PIPE_ALL_CPU) {
		/* gain it for accessing the whole ring buffer. */
		down_write(&all_cpu_access_lock);
	} else {
		/* gain it for accessing a cpu ring buffer. */

		/* Firstly block other trace_access_lock(TRACE_PIPE_ALL_CPU). */
		down_read(&all_cpu_access_lock);

		/* Secondly block other access to this @cpu ring buffer. */
		mutex_lock(&per_cpu(cpu_access_lock, cpu));
	}
}

static inline void trace_access_unlock(int cpu)
{
	if (cpu == TRACE_PIPE_ALL_CPU) {
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

/* trace_wait is a waitqueue for tasks blocked on trace_poll */
static DECLARE_WAIT_QUEUE_HEAD(trace_wait);

/* trace_flags holds trace_options default values */
unsigned long trace_flags = TRACE_ITER_PRINT_PARENT | TRACE_ITER_PRINTK |
	TRACE_ITER_ANNOTATE | TRACE_ITER_CONTEXT_INFO | TRACE_ITER_SLEEP_TIME |
	TRACE_ITER_GRAPH_TIME | TRACE_ITER_RECORD_CMD | TRACE_ITER_OVERWRITE |
	TRACE_ITER_IRQ_INFO;

static int trace_stop_count;
static DEFINE_RAW_SPINLOCK(tracing_start_lock);

static void wakeup_work_handler(struct work_struct *work)
{
	wake_up(&trace_wait);
}

static DECLARE_DELAYED_WORK(wakeup_work, wakeup_work_handler);

/**
 * tracing_on - enable tracing buffers
 *
 * This function enables tracing buffers that may have been
 * disabled with tracing_off.
 */
void tracing_on(void)
{
	if (global_trace.buffer)
		ring_buffer_record_on(global_trace.buffer);
	/*
	 * This flag is only looked at when buffers haven't been
	 * allocated yet. We don't really care about the race
	 * between setting this flag and actually turning
	 * on the buffer.
	 */
	global_trace.buffer_disabled = 0;
}
EXPORT_SYMBOL_GPL(tracing_on);

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
	if (global_trace.buffer)
		ring_buffer_record_off(global_trace.buffer);
	/*
	 * This flag is only looked at when buffers haven't been
	 * allocated yet. We don't really care about the race
	 * between setting this flag and actually turning
	 * on the buffer.
	 */
	global_trace.buffer_disabled = 1;
}
EXPORT_SYMBOL_GPL(tracing_off);

/**
 * tracing_is_on - show state of ring buffers enabled
 */
int tracing_is_on(void)
{
	if (global_trace.buffer)
		return ring_buffer_record_is_on(global_trace.buffer);
	return !global_trace.buffer_disabled;
}
EXPORT_SYMBOL_GPL(tracing_is_on);

/**
 * trace_wake_up - wake up tasks waiting for trace input
 *
 * Schedules a delayed work to wake up any task that is blocked on the
 * trace_wait queue. These is used with trace_poll for tasks polling the
 * trace.
 */
void trace_wake_up(void)
{
	const unsigned long delay = msecs_to_jiffies(2);

	if (trace_flags & TRACE_ITER_BLOCK)
		return;
	schedule_delayed_work(&wakeup_work, delay);
}

static int __init set_buf_size(char *str)
{
	unsigned long buf_size;

	if (!str)
		return 0;
	buf_size = memparse(str, &str);
	/* nr_entries can not be zero */
	if (buf_size == 0)
		return 0;
	trace_buf_size = buf_size;
	return 1;
}
__setup("trace_buf_size=", set_buf_size);

static int __init set_tracing_thresh(char *str)
{
	unsigned long threshhold;
	int ret;

	if (!str)
		return 0;
	ret = strict_strtoul(str, 0, &threshhold);
	if (ret < 0)
		return 0;
	tracing_thresh = threshhold * 1000;
	return 1;
}
__setup("tracing_thresh=", set_tracing_thresh);

unsigned long nsecs_to_usecs(unsigned long nsecs)
{
	return nsecs / 1000;
}

/* These must match the bit postions in trace_iterator_flags */
static const char *trace_options[] = {
	"print-parent",
	"sym-offset",
	"sym-addr",
	"verbose",
	"raw",
	"hex",
	"bin",
	"block",
	"stacktrace",
	"trace_printk",
	"ftrace_preempt",
	"branch",
	"annotate",
	"userstacktrace",
	"sym-userobj",
	"printk-msg-only",
	"context-info",
	"latency-format",
	"sleep-time",
	"graph-time",
	"record-cmd",
	"overwrite",
	"disable_on_free",
	"irq-info",
	NULL
};

static struct {
	u64 (*func)(void);
	const char *name;
} trace_clocks[] = {
	{ trace_clock_local,	"local" },
	{ trace_clock_global,	"global" },
	{ trace_clock_counter,	"counter" },
};

int trace_clock_id;

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

		/* only spaces were written */
		if (isspace(ch)) {
			*ppos += read;
			ret = read;
			goto out;
		}

		parser->idx = 0;
	}

	/* read the non-space input */
	while (cnt && !isspace(ch)) {
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
	if (isspace(ch)) {
		parser->buffer[parser->idx] = 0;
		parser->cont = false;
	} else {
		parser->cont = true;
		parser->buffer[parser->idx++] = ch;
	}

	*ppos += read;
	ret = read;

out:
	return ret;
}

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret == cnt)
		return -EFAULT;

	cnt -= ret;

	s->readpos += cnt;
	return cnt;
}

static ssize_t trace_seq_to_buffer(struct trace_seq *s, void *buf, size_t cnt)
{
	int len;
	void *ret;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = memcpy(buf, s->buffer + s->readpos, cnt);
	if (!ret)
		return -EFAULT;

	s->readpos += cnt;
	return cnt;
}

/*
 * ftrace_max_lock is used to protect the swapping of buffers
 * when taking a max snapshot. The buffers themselves are
 * protected by per_cpu spinlocks. But the action of the swap
 * needs its own lock.
 *
 * This is defined as a arch_spinlock_t in order to help
 * with performance when lockdep debugging is enabled.
 *
 * It is also used in other places outside the update_max_tr
 * so it needs to be defined outside of the
 * CONFIG_TRACER_MAX_TRACE.
 */
static arch_spinlock_t ftrace_max_lock =
	(arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;

unsigned long __read_mostly	tracing_thresh;

#ifdef CONFIG_TRACER_MAX_TRACE
unsigned long __read_mostly	tracing_max_latency;

/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /sys/kernel/debug/tracing/latency_trace)
 */
static void
__update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct trace_array_cpu *data = tr->data[cpu];
	struct trace_array_cpu *max_data;

	max_tr.cpu = cpu;
	max_tr.time_start = data->preempt_timestamp;

	max_data = max_tr.data[cpu];
	max_data->saved_latency = tracing_max_latency;
	max_data->critical_start = data->critical_start;
	max_data->critical_end = data->critical_end;

	memcpy(max_data->comm, tsk->comm, TASK_COMM_LEN);
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
}

/**
 * update_max_tr - snapshot all trace buffers from global_trace to max_tr
 * @tr: tracer
 * @tsk: the task with the latency
 * @cpu: The cpu that initiated the trace.
 *
 * Flip the buffers between the @tr and the max_tr and record information
 * about which task was the cause of this latency.
 */
void
update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct ring_buffer *buf;

	if (trace_stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());
	if (!current_trace->use_max_tr) {
		WARN_ON_ONCE(1);
		return;
	}
	arch_spin_lock(&ftrace_max_lock);

	buf = tr->buffer;
	tr->buffer = max_tr.buffer;
	max_tr.buffer = buf;

	__update_max_tr(tr, tsk, cpu);
	arch_spin_unlock(&ftrace_max_lock);
}

/**
 * update_max_tr_single - only copy one trace over, and reset the rest
 * @tr - tracer
 * @tsk - task with the latency
 * @cpu - the cpu of the buffer to copy.
 *
 * Flip the trace of a single CPU buffer between the @tr and the max_tr.
 */
void
update_max_tr_single(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	int ret;

	if (trace_stop_count)
		return;

	WARN_ON_ONCE(!irqs_disabled());
	if (!current_trace->use_max_tr) {
		WARN_ON_ONCE(1);
		return;
	}

	arch_spin_lock(&ftrace_max_lock);

	ftrace_disable_cpu();

	ret = ring_buffer_swap_cpu(max_tr.buffer, tr->buffer, cpu);

	if (ret == -EBUSY) {
		/*
		 * We failed to swap the buffer due to a commit taking
		 * place on this CPU. We fail to record, but we reset
		 * the max trace buffer (no one writes directly to it)
		 * and flag that it failed.
		 */
		trace_array_printk(&max_tr, _THIS_IP_,
			"Failed to swap buffers due to commit in progress\n");
	}

	ftrace_enable_cpu();

	WARN_ON_ONCE(ret && ret != -EAGAIN && ret != -EBUSY);

	__update_max_tr(tr, tsk, cpu);
	arch_spin_unlock(&ftrace_max_lock);
}
#endif /* CONFIG_TRACER_MAX_TRACE */

/**
 * register_tracer - register a tracer with the ftrace system.
 * @type - the plugin for the tracer
 *
 * Register a new plugin tracer.
 */
int register_tracer(struct tracer *type)
__releases(kernel_lock)
__acquires(kernel_lock)
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
	if (!type->flags)
		type->flags = &dummy_tracer_flags;
	else
		if (!type->flags->opts)
			type->flags->opts = dummy_tracer_opt;
	if (!type->wait_pipe)
		type->wait_pipe = default_wait_pipe;


#ifdef CONFIG_FTRACE_STARTUP_TEST
	if (type->selftest && !tracing_selftest_disabled) {
		struct tracer *saved_tracer = current_trace;
		struct trace_array *tr = &global_trace;

		/*
		 * Run a selftest on this tracer.
		 * Here we reset the trace buffer, and set the current
		 * tracer to be this tracer. The tracer can then run some
		 * internal tracing to verify that everything is in order.
		 * If we fail, we do not register this tracer.
		 */
		tracing_reset_online_cpus(tr);

		current_trace = type;

		/* If we expanded the buffers, make sure the max is expanded too */
		if (ring_buffer_expanded && type->use_max_tr)
			ring_buffer_resize(max_tr.buffer, trace_buf_size);

		/* the test is responsible for initializing and enabling */
		pr_info("Testing tracer %s: ", type->name);
		ret = type->selftest(type, tr);
		/* the test is responsible for resetting too */
		current_trace = saved_tracer;
		if (ret) {
			printk(KERN_CONT "FAILED!\n");
			goto out;
		}
		/* Only reset on passing, to avoid touching corrupted buffers */
		tracing_reset_online_cpus(tr);

		/* Shrink the max buffer again */
		if (ring_buffer_expanded && type->use_max_tr)
			ring_buffer_resize(max_tr.buffer, 1);

		printk(KERN_CONT "PASSED\n");
	}
#endif

	type->next = trace_types;
	trace_types = type;

 out:
	tracing_selftest_running = false;
	mutex_unlock(&trace_types_lock);

	if (ret || !default_bootup_tracer)
		goto out_unlock;

	if (strncmp(default_bootup_tracer, type->name, MAX_TRACER_SIZE))
		goto out_unlock;

	printk(KERN_INFO "Starting tracer '%s'\n", type->name);
	/* Do we want this tracer to start on bootup? */
	tracing_set_tracer(type->name);
	default_bootup_tracer = NULL;
	/* disable other selftests, since this will break it. */
	tracing_selftest_disabled = 1;
#ifdef CONFIG_FTRACE_STARTUP_TEST
	printk(KERN_INFO "Disabling FTRACE selftests due to running tracer '%s'\n",
	       type->name);
#endif

 out_unlock:
	return ret;
}

void unregister_tracer(struct tracer *type)
{
	struct tracer **t;

	mutex_lock(&trace_types_lock);
	for (t = &trace_types; *t; t = &(*t)->next) {
		if (*t == type)
			goto found;
	}
	pr_info("Tracer %s not registered\n", type->name);
	goto out;

 found:
	*t = (*t)->next;

	if (type == current_trace && tracer_enabled) {
		tracer_enabled = 0;
		tracing_stop();
		if (current_trace->stop)
			current_trace->stop(&global_trace);
		current_trace = &nop_trace;
	}
out:
	mutex_unlock(&trace_types_lock);
}

static void __tracing_reset(struct ring_buffer *buffer, int cpu)
{
	ftrace_disable_cpu();
	ring_buffer_reset_cpu(buffer, cpu);
	ftrace_enable_cpu();
}

void tracing_reset(struct trace_array *tr, int cpu)
{
	struct ring_buffer *buffer = tr->buffer;

	ring_buffer_record_disable(buffer);

	/* Make sure all commits have finished */
	synchronize_sched();
	__tracing_reset(buffer, cpu);

	ring_buffer_record_enable(buffer);
}

void tracing_reset_online_cpus(struct trace_array *tr)
{
	struct ring_buffer *buffer = tr->buffer;
	int cpu;

	ring_buffer_record_disable(buffer);

	/* Make sure all commits have finished */
	synchronize_sched();

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		__tracing_reset(buffer, cpu);

	ring_buffer_record_enable(buffer);
}

void tracing_reset_current(int cpu)
{
	tracing_reset(&global_trace, cpu);
}

void tracing_reset_current_online_cpus(void)
{
	tracing_reset_online_cpus(&global_trace);
}

#define SAVED_CMDLINES 128
#define NO_CMDLINE_MAP UINT_MAX
static unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
static unsigned map_cmdline_to_pid[SAVED_CMDLINES];
static char saved_cmdlines[SAVED_CMDLINES][TASK_COMM_LEN];
static int cmdline_idx;
static arch_spinlock_t trace_cmdline_lock = __ARCH_SPIN_LOCK_UNLOCKED;

/* temporary disable recording */
static atomic_t trace_record_cmdline_disabled __read_mostly;

static void trace_init_cmdlines(void)
{
	memset(&map_pid_to_cmdline, NO_CMDLINE_MAP, sizeof(map_pid_to_cmdline));
	memset(&map_cmdline_to_pid, NO_CMDLINE_MAP, sizeof(map_cmdline_to_pid));
	cmdline_idx = 0;
}

int is_tracing_stopped(void)
{
	return trace_stop_count;
}

/**
 * ftrace_off_permanent - disable all ftrace code permanently
 *
 * This should only be called when a serious anomally has
 * been detected.  This will turn off the function tracing,
 * ring buffers, and other tracing utilites. It takes no
 * locks and can be called from any context.
 */
void ftrace_off_permanent(void)
{
	tracing_disabled = 1;
	ftrace_stop();
	tracing_off_permanent();
}

/**
 * tracing_start - quick start of the tracer
 *
 * If tracing is enabled but was stopped by tracing_stop,
 * this will start the tracer back up.
 */
void tracing_start(void)
{
	struct ring_buffer *buffer;
	unsigned long flags;

	if (tracing_disabled)
		return;

	raw_spin_lock_irqsave(&tracing_start_lock, flags);
	if (--trace_stop_count) {
		if (trace_stop_count < 0) {
			/* Someone screwed up their debugging */
			WARN_ON_ONCE(1);
			trace_stop_count = 0;
		}
		goto out;
	}

	/* Prevent the buffers from switching */
	arch_spin_lock(&ftrace_max_lock);

	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	arch_spin_unlock(&ftrace_max_lock);

	ftrace_start();
 out:
	raw_spin_unlock_irqrestore(&tracing_start_lock, flags);
}

/**
 * tracing_stop - quick stop of the tracer
 *
 * Light weight way to stop tracing. Use in conjunction with
 * tracing_start.
 */
void tracing_stop(void)
{
	struct ring_buffer *buffer;
	unsigned long flags;

	ftrace_stop();
	raw_spin_lock_irqsave(&tracing_start_lock, flags);
	if (trace_stop_count++)
		goto out;

	/* Prevent the buffers from switching */
	arch_spin_lock(&ftrace_max_lock);

	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

	arch_spin_unlock(&ftrace_max_lock);

 out:
	raw_spin_unlock_irqrestore(&tracing_start_lock, flags);
}

void trace_stop_cmdline_recording(void);

static void trace_save_cmdline(struct task_struct *tsk)
{
	unsigned pid, idx;

	if (!tsk->pid || unlikely(tsk->pid > PID_MAX_DEFAULT))
		return;

	/*
	 * It's not the end of the world if we don't get
	 * the lock, but we also don't want to spin
	 * nor do we want to disable interrupts,
	 * so if we miss here, then better luck next time.
	 */
	if (!arch_spin_trylock(&trace_cmdline_lock))
		return;

	idx = map_pid_to_cmdline[tsk->pid];
	if (idx == NO_CMDLINE_MAP) {
		idx = (cmdline_idx + 1) % SAVED_CMDLINES;

		/*
		 * Check whether the cmdline buffer at idx has a pid
		 * mapped. We are going to overwrite that entry so we
		 * need to clear the map_pid_to_cmdline. Otherwise we
		 * would read the new comm for the old pid.
		 */
		pid = map_cmdline_to_pid[idx];
		if (pid != NO_CMDLINE_MAP)
			map_pid_to_cmdline[pid] = NO_CMDLINE_MAP;

		map_cmdline_to_pid[idx] = tsk->pid;
		map_pid_to_cmdline[tsk->pid] = idx;

		cmdline_idx = idx;
	}

	memcpy(&saved_cmdlines[idx], tsk->comm, TASK_COMM_LEN);

	arch_spin_unlock(&trace_cmdline_lock);
}

void trace_find_cmdline(int pid, char comm[])
{
	unsigned map;

	if (!pid) {
		strcpy(comm, "<idle>");
		return;
	}

	if (WARN_ON_ONCE(pid < 0)) {
		strcpy(comm, "<XXX>");
		return;
	}

	if (pid > PID_MAX_DEFAULT) {
		strcpy(comm, "<...>");
		return;
	}

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);
	map = map_pid_to_cmdline[pid];
	if (map != NO_CMDLINE_MAP)
		strcpy(comm, saved_cmdlines[map]);
	else
		strcpy(comm, "<...>");

	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
}

void tracing_record_cmdline(struct task_struct *tsk)
{
	if (atomic_read(&trace_record_cmdline_disabled) || !tracer_enabled ||
	    !tracing_is_on())
		return;

	trace_save_cmdline(tsk);
}

void
tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags,
			     int pc)
{
	struct task_struct *tsk = current;

	entry->preempt_count		= pc & 0xff;
	entry->pid			= (tsk) ? tsk->pid : 0;
	entry->padding			= 0;
	entry->flags =
#ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT
		(irqs_disabled_flags(flags) ? TRACE_FLAG_IRQS_OFF : 0) |
#else
		TRACE_FLAG_IRQS_NOSUPPORT |
#endif
		((pc & HARDIRQ_MASK) ? TRACE_FLAG_HARDIRQ : 0) |
		((pc & SOFTIRQ_MASK) ? TRACE_FLAG_SOFTIRQ : 0) |
		(need_resched() ? TRACE_FLAG_NEED_RESCHED : 0);
}
EXPORT_SYMBOL_GPL(tracing_generic_entry_update);

struct ring_buffer_event *
trace_buffer_lock_reserve(struct ring_buffer *buffer,
			  int type,
			  unsigned long len,
			  unsigned long flags, int pc)
{
	struct ring_buffer_event *event;

	event = ring_buffer_lock_reserve(buffer, len);
	if (event != NULL) {
		struct trace_entry *ent = ring_buffer_event_data(event);

		tracing_generic_entry_update(ent, flags, pc);
		ent->type = type;
	}

	return event;
}

static inline void
__trace_buffer_unlock_commit(struct ring_buffer *buffer,
			     struct ring_buffer_event *event,
			     unsigned long flags, int pc,
			     int wake)
{
	ring_buffer_unlock_commit(buffer, event);

	ftrace_trace_stack(buffer, flags, 6, pc);
	ftrace_trace_userstack(buffer, flags, pc);

	if (wake)
		trace_wake_up();
}

void trace_buffer_unlock_commit(struct ring_buffer *buffer,
				struct ring_buffer_event *event,
				unsigned long flags, int pc)
{
	__trace_buffer_unlock_commit(buffer, event, flags, pc, 1);
}

struct ring_buffer_event *
trace_current_buffer_lock_reserve(struct ring_buffer **current_rb,
				  int type, unsigned long len,
				  unsigned long flags, int pc)
{
	*current_rb = global_trace.buffer;
	return trace_buffer_lock_reserve(*current_rb,
					 type, len, flags, pc);
}
EXPORT_SYMBOL_GPL(trace_current_buffer_lock_reserve);

void trace_current_buffer_unlock_commit(struct ring_buffer *buffer,
					struct ring_buffer_event *event,
					unsigned long flags, int pc)
{
	__trace_buffer_unlock_commit(buffer, event, flags, pc, 1);
}
EXPORT_SYMBOL_GPL(trace_current_buffer_unlock_commit);

void trace_nowake_buffer_unlock_commit(struct ring_buffer *buffer,
				       struct ring_buffer_event *event,
				       unsigned long flags, int pc)
{
	__trace_buffer_unlock_commit(buffer, event, flags, pc, 0);
}
EXPORT_SYMBOL_GPL(trace_nowake_buffer_unlock_commit);

void trace_nowake_buffer_unlock_commit_regs(struct ring_buffer *buffer,
					    struct ring_buffer_event *event,
					    unsigned long flags, int pc,
					    struct pt_regs *regs)
{
	ring_buffer_unlock_commit(buffer, event);

	ftrace_trace_stack_regs(buffer, flags, 0, pc, regs);
	ftrace_trace_userstack(buffer, flags, pc);
}
EXPORT_SYMBOL_GPL(trace_nowake_buffer_unlock_commit_regs);

void trace_current_buffer_discard_commit(struct ring_buffer *buffer,
					 struct ring_buffer_event *event)
{
	ring_buffer_discard_commit(buffer, event);
}
EXPORT_SYMBOL_GPL(trace_current_buffer_discard_commit);

void
trace_function(struct trace_array *tr,
	       unsigned long ip, unsigned long parent_ip, unsigned long flags,
	       int pc)
{
	struct ftrace_event_call *call = &event_function;
	struct ring_buffer *buffer = tr->buffer;
	struct ring_buffer_event *event;
	struct ftrace_entry *entry;

	/* If we are reading the ring buffer, don't trace */
	if (unlikely(__this_cpu_read(ftrace_cpu_disabled)))
		return;

	event = trace_buffer_lock_reserve(buffer, TRACE_FN, sizeof(*entry),
					  flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;

	if (!filter_check_discard(call, entry, buffer, event))
		ring_buffer_unlock_commit(buffer, event);
}

void
ftrace(struct trace_array *tr, struct trace_array_cpu *data,
       unsigned long ip, unsigned long parent_ip, unsigned long flags,
       int pc)
{
	if (likely(!atomic_read(&data->disabled)))
		trace_function(tr, ip, parent_ip, flags, pc);
}

#ifdef CONFIG_STACKTRACE

#define FTRACE_STACK_MAX_ENTRIES (PAGE_SIZE / sizeof(unsigned long))
struct ftrace_stack {
	unsigned long		calls[FTRACE_STACK_MAX_ENTRIES];
};

static DEFINE_PER_CPU(struct ftrace_stack, ftrace_stack);
static DEFINE_PER_CPU(int, ftrace_stack_reserve);

static void __ftrace_trace_stack(struct ring_buffer *buffer,
				 unsigned long flags,
				 int skip, int pc, struct pt_regs *regs)
{
	struct ftrace_event_call *call = &event_kernel_stack;
	struct ring_buffer_event *event;
	struct stack_entry *entry;
	struct stack_trace trace;
	int use_stack;
	int size = FTRACE_STACK_ENTRIES;

	trace.nr_entries	= 0;
	trace.skip		= skip;

	/*
	 * Since events can happen in NMIs there's no safe way to
	 * use the per cpu ftrace_stacks. We reserve it and if an interrupt
	 * or NMI comes in, it will just have to use the default
	 * FTRACE_STACK_SIZE.
	 */
	preempt_disable_notrace();

	use_stack = ++__get_cpu_var(ftrace_stack_reserve);
	/*
	 * We don't need any atomic variables, just a barrier.
	 * If an interrupt comes in, we don't care, because it would
	 * have exited and put the counter back to what we want.
	 * We just need a barrier to keep gcc from moving things
	 * around.
	 */
	barrier();
	if (use_stack == 1) {
		trace.entries		= &__get_cpu_var(ftrace_stack).calls[0];
		trace.max_entries	= FTRACE_STACK_MAX_ENTRIES;

		if (regs)
			save_stack_trace_regs(regs, &trace);
		else
			save_stack_trace(&trace);

		if (trace.nr_entries > size)
			size = trace.nr_entries;
	} else
		/* From now on, use_stack is a boolean */
		use_stack = 0;

	size *= sizeof(unsigned long);

	event = trace_buffer_lock_reserve(buffer, TRACE_STACK,
					  sizeof(*entry) + size, flags, pc);
	if (!event)
		goto out;
	entry = ring_buffer_event_data(event);

	memset(&entry->caller, 0, size);

	if (use_stack)
		memcpy(&entry->caller, trace.entries,
		       trace.nr_entries * sizeof(unsigned long));
	else {
		trace.max_entries	= FTRACE_STACK_ENTRIES;
		trace.entries		= entry->caller;
		if (regs)
			save_stack_trace_regs(regs, &trace);
		else
			save_stack_trace(&trace);
	}

	entry->size = trace.nr_entries;

	if (!filter_check_discard(call, entry, buffer, event))
		ring_buffer_unlock_commit(buffer, event);

 out:
	/* Again, don't let gcc optimize things here */
	barrier();
	__get_cpu_var(ftrace_stack_reserve)--;
	preempt_enable_notrace();

}

void ftrace_trace_stack_regs(struct ring_buffer *buffer, unsigned long flags,
			     int skip, int pc, struct pt_regs *regs)
{
	if (!(trace_flags & TRACE_ITER_STACKTRACE))
		return;

	__ftrace_trace_stack(buffer, flags, skip, pc, regs);
}

void ftrace_trace_stack(struct ring_buffer *buffer, unsigned long flags,
			int skip, int pc)
{
	if (!(trace_flags & TRACE_ITER_STACKTRACE))
		return;

	__ftrace_trace_stack(buffer, flags, skip, pc, NULL);
}

void __trace_stack(struct trace_array *tr, unsigned long flags, int skip,
		   int pc)
{
	__ftrace_trace_stack(tr->buffer, flags, skip, pc, NULL);
}

/**
 * trace_dump_stack - record a stack back trace in the trace buffer
 */
void trace_dump_stack(void)
{
	unsigned long flags;

	if (tracing_disabled || tracing_selftest_running)
		return;

	local_save_flags(flags);

	/* skipping 3 traces, seems to get us at the caller of this function */
	__ftrace_trace_stack(global_trace.buffer, flags, 3, preempt_count(), NULL);
}

static DEFINE_PER_CPU(int, user_stack_count);

void
ftrace_trace_userstack(struct ring_buffer *buffer, unsigned long flags, int pc)
{
	struct ftrace_event_call *call = &event_user_stack;
	struct ring_buffer_event *event;
	struct userstack_entry *entry;
	struct stack_trace trace;

	if (!(trace_flags & TRACE_ITER_USERSTACKTRACE))
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

	event = trace_buffer_lock_reserve(buffer, TRACE_USER_STACK,
					  sizeof(*entry), flags, pc);
	if (!event)
		goto out_drop_count;
	entry	= ring_buffer_event_data(event);

	entry->tgid		= current->tgid;
	memset(&entry->caller, 0, sizeof(entry->caller));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= 0;
	trace.entries		= entry->caller;

	save_stack_trace_user(&trace);
	if (!filter_check_discard(call, entry, buffer, event))
		ring_buffer_unlock_commit(buffer, event);

 out_drop_count:
	__this_cpu_dec(user_stack_count);
 out:
	preempt_enable();
}

#ifdef UNUSED
static void __trace_userstack(struct trace_array *tr, unsigned long flags)
{
	ftrace_trace_userstack(tr, flags, preempt_count());
}
#endif /* UNUSED */

#endif /* CONFIG_STACKTRACE */

/**
 * trace_vbprintk - write binary msg to tracing buffer
 *
 */
int trace_vbprintk(unsigned long ip, const char *fmt, va_list args)
{
	static arch_spinlock_t trace_buf_lock =
		(arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;
	static u32 trace_buf[TRACE_BUF_SIZE];

	struct ftrace_event_call *call = &event_bprint;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	struct bprint_entry *entry;
	unsigned long flags;
	int disable;
	int cpu, len = 0, size, pc;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	pc = preempt_count();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (unlikely(disable != 1))
		goto out;

	/* Lockdep uses trace_printk for lock tracing */
	local_irq_save(flags);
	arch_spin_lock(&trace_buf_lock);
	len = vbin_printf(trace_buf, TRACE_BUF_SIZE, fmt, args);

	if (len > TRACE_BUF_SIZE || len < 0)
		goto out_unlock;

	size = sizeof(*entry) + sizeof(u32) * len;
	buffer = tr->buffer;
	event = trace_buffer_lock_reserve(buffer, TRACE_BPRINT, size,
					  flags, pc);
	if (!event)
		goto out_unlock;
	entry = ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->fmt			= fmt;

	memcpy(entry->buf, trace_buf, sizeof(u32) * len);
	if (!filter_check_discard(call, entry, buffer, event)) {
		ring_buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(buffer, flags, 6, pc);
	}

out_unlock:
	arch_spin_unlock(&trace_buf_lock);
	local_irq_restore(flags);

out:
	atomic_dec_return(&data->disabled);
	preempt_enable_notrace();
	unpause_graph_tracing();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vbprintk);

int trace_array_printk(struct trace_array *tr,
		       unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!(trace_flags & TRACE_ITER_PRINTK))
		return 0;

	va_start(ap, fmt);
	ret = trace_array_vprintk(tr, ip, fmt, ap);
	va_end(ap);
	return ret;
}

int trace_array_vprintk(struct trace_array *tr,
			unsigned long ip, const char *fmt, va_list args)
{
	static arch_spinlock_t trace_buf_lock = __ARCH_SPIN_LOCK_UNLOCKED;
	static char trace_buf[TRACE_BUF_SIZE];

	struct ftrace_event_call *call = &event_print;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	struct trace_array_cpu *data;
	int cpu, len = 0, size, pc;
	struct print_entry *entry;
	unsigned long irq_flags;
	int disable;

	if (tracing_disabled || tracing_selftest_running)
		return 0;

	pc = preempt_count();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	disable = atomic_inc_return(&data->disabled);
	if (unlikely(disable != 1))
		goto out;

	pause_graph_tracing();
	raw_local_irq_save(irq_flags);
	arch_spin_lock(&trace_buf_lock);
	len = vsnprintf(trace_buf, TRACE_BUF_SIZE, fmt, args);

	size = sizeof(*entry) + len + 1;
	buffer = tr->buffer;
	event = trace_buffer_lock_reserve(buffer, TRACE_PRINT, size,
					  irq_flags, pc);
	if (!event)
		goto out_unlock;
	entry = ring_buffer_event_data(event);
	entry->ip = ip;

	memcpy(&entry->buf, trace_buf, len);
	entry->buf[len] = '\0';
	if (!filter_check_discard(call, entry, buffer, event)) {
		ring_buffer_unlock_commit(buffer, event);
		ftrace_trace_stack(buffer, irq_flags, 6, pc);
	}

 out_unlock:
	arch_spin_unlock(&trace_buf_lock);
	raw_local_irq_restore(irq_flags);
	unpause_graph_tracing();
 out:
	atomic_dec_return(&data->disabled);
	preempt_enable_notrace();

	return len;
}

int trace_vprintk(unsigned long ip, const char *fmt, va_list args)
{
	return trace_array_vprintk(&global_trace, ip, fmt, args);
}
EXPORT_SYMBOL_GPL(trace_vprintk);

static void trace_iterator_increment(struct trace_iterator *iter)
{
	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();

	iter->idx++;
	if (iter->buffer_iter[iter->cpu])
		ring_buffer_read(iter->buffer_iter[iter->cpu], NULL);

	ftrace_enable_cpu();
}

static struct trace_entry *
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts,
		unsigned long *lost_events)
{
	struct ring_buffer_event *event;
	struct ring_buffer_iter *buf_iter = iter->buffer_iter[cpu];

	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();

	if (buf_iter)
		event = ring_buffer_iter_peek(buf_iter, ts);
	else
		event = ring_buffer_peek(iter->tr->buffer, cpu, ts,
					 lost_events);

	ftrace_enable_cpu();

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
	struct ring_buffer *buffer = iter->tr->buffer;
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
	if (cpu_file > TRACE_PIPE_ALL_CPU) {
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

/* Find the next real entry, without updating the iterator itself */
struct trace_entry *trace_find_next_entry(struct trace_iterator *iter,
					  int *ent_cpu, u64 *ent_ts)
{
	return __find_next_entry(iter, ent_cpu, NULL, ent_ts);
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
	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();
	ring_buffer_consume(iter->tr->buffer, iter->cpu, &iter->ts,
			    &iter->lost_events);
	ftrace_enable_cpu();
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
	struct trace_array *tr = iter->tr;
	struct ring_buffer_event *event;
	struct ring_buffer_iter *buf_iter;
	unsigned long entries = 0;
	u64 ts;

	tr->data[cpu]->skipped_entries = 0;

	if (!iter->buffer_iter[cpu])
		return;

	buf_iter = iter->buffer_iter[cpu];
	ring_buffer_iter_reset(buf_iter);

	/*
	 * We could have the case with the max latency tracers
	 * that a reset never took place on a cpu. This is evident
	 * by the timestamp being before the start of the buffer.
	 */
	while ((event = ring_buffer_iter_peek(buf_iter, &ts))) {
		if (ts >= iter->tr->time_start)
			break;
		entries++;
		ring_buffer_read(buf_iter, NULL);
	}

	tr->data[cpu]->skipped_entries = entries;
}

/*
 * The current tracer is copied to avoid a global locking
 * all around.
 */
static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	static struct tracer *old_tracer;
	int cpu_file = iter->cpu_file;
	void *p = NULL;
	loff_t l = 0;
	int cpu;

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

	atomic_inc(&trace_record_cmdline_disabled);

	if (*pos != iter->pos) {
		iter->ent = NULL;
		iter->cpu = 0;
		iter->idx = -1;

		ftrace_disable_cpu();

		if (cpu_file == TRACE_PIPE_ALL_CPU) {
			for_each_tracing_cpu(cpu)
				tracing_iter_reset(iter, cpu);
		} else
			tracing_iter_reset(iter, cpu_file);

		ftrace_enable_cpu();

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

	atomic_dec(&trace_record_cmdline_disabled);
	trace_access_unlock(iter->cpu_file);
	trace_event_read_unlock();
}

static void
get_total_entries(struct trace_array *tr, unsigned long *total, unsigned long *entries)
{
	unsigned long count;
	int cpu;

	*total = 0;
	*entries = 0;

	for_each_tracing_cpu(cpu) {
		count = ring_buffer_entries_cpu(tr->buffer, cpu);
		/*
		 * If this buffer has skipped entries, then we hold all
		 * entries for the trace and we need to ignore the
		 * ones before the time stamp.
		 */
		if (tr->data[cpu]->skipped_entries) {
			count -= tr->data[cpu]->skipped_entries;
			/* total is the same as the entries */
			*total += count;
		} else
			*total += count +
				ring_buffer_overrun_cpu(tr->buffer, cpu);
		*entries += count;
	}
}

static void print_lat_help_header(struct seq_file *m)
{
	seq_puts(m, "#                  _------=> CPU#            \n");
	seq_puts(m, "#                 / _-----=> irqs-off        \n");
	seq_puts(m, "#                | / _----=> need-resched    \n");
	seq_puts(m, "#                || / _---=> hardirq/softirq \n");
	seq_puts(m, "#                ||| / _--=> preempt-depth   \n");
	seq_puts(m, "#                |||| /     delay             \n");
	seq_puts(m, "#  cmd     pid   ||||| time  |   caller      \n");
	seq_puts(m, "#     \\   /      |||||  \\    |   /           \n");
}

static void print_event_info(struct trace_array *tr, struct seq_file *m)
{
	unsigned long total;
	unsigned long entries;

	get_total_entries(tr, &total, &entries);
	seq_printf(m, "# entries-in-buffer/entries-written: %lu/%lu   #P:%d\n",
		   entries, total, num_online_cpus());
	seq_puts(m, "#\n");
}

static void print_func_help_header(struct trace_array *tr, struct seq_file *m)
{
	print_event_info(tr, m);
	seq_puts(m, "#           TASK-PID   CPU#      TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |       |          |         |\n");
}

static void print_func_help_header_irq(struct trace_array *tr, struct seq_file *m)
{
	print_event_info(tr, m);
	seq_puts(m, "#                              _-----=> irqs-off\n");
	seq_puts(m, "#                             / _----=> need-resched\n");
	seq_puts(m, "#                            | / _---=> hardirq/softirq\n");
	seq_puts(m, "#                            || / _--=> preempt-depth\n");
	seq_puts(m, "#                            ||| /     delay\n");
	seq_puts(m, "#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |       |   ||||       |         |\n");
}

void
print_trace_header(struct seq_file *m, struct trace_iterator *iter)
{
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_array *tr = iter->tr;
	struct trace_array_cpu *data = tr->data[tr->cpu];
	struct tracer *type = current_trace;
	unsigned long entries;
	unsigned long total;
	const char *name = "preemption";

	if (type)
		name = type->name;

	get_total_entries(tr, &total, &entries);

	seq_printf(m, "# %s latency trace v1.1.5 on %s\n",
		   name, UTS_RELEASE);
	seq_puts(m, "# -----------------------------------"
		 "---------------------------------\n");
	seq_printf(m, "# latency: %lu us, #%lu/%lu, CPU#%d |"
		   " (M:%s VP:%d, KP:%d, SP:%d HP:%d",
		   nsecs_to_usecs(data->saved_latency),
		   entries,
		   total,
		   tr->cpu,
#if defined(CONFIG_PREEMPT_NONE)
		   "server",
#elif defined(CONFIG_PREEMPT_VOLUNTARY)
		   "desktop",
#elif defined(CONFIG_PREEMPT)
		   "preempt",
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
		   data->comm, data->pid, data->uid, data->nice,
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

	if (!(trace_flags & TRACE_ITER_ANNOTATE))
		return;

	if (!(iter->iter_flags & TRACE_FILE_ANNOTATE))
		return;

	if (cpumask_test_cpu(iter->cpu, iter->started))
		return;

	if (iter->tr->data[iter->cpu]->skipped_entries)
		return;

	cpumask_set_cpu(iter->cpu, iter->started);

	/* Don't print started cpu buffer for the first entry of the trace */
	if (iter->idx > 1)
		trace_seq_printf(s, "##### CPU %u buffer started ####\n",
				iter->cpu);
}

static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	test_cpu_buff_start(iter);

	event = ftrace_find_event(entry->type);

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
			if (!trace_print_lat_context(iter))
				goto partial;
		} else {
			if (!trace_print_context(iter))
				goto partial;
		}
	}

	if (event)
		return event->funcs->trace(iter, sym_flags, event);

	if (!trace_seq_printf(s, "Unknown type %d\n", entry->type))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		if (!trace_seq_printf(s, "%d %d %llu ",
				      entry->pid, iter->cpu, iter->ts))
			goto partial;
	}

	event = ftrace_find_event(entry->type);
	if (event)
		return event->funcs->raw(iter, 0, event);

	if (!trace_seq_printf(s, "%d ?\n", entry->type))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_hex_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned char newline = '\n';
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_HEX_FIELD_RET(s, entry->pid);
		SEQ_PUT_HEX_FIELD_RET(s, iter->cpu);
		SEQ_PUT_HEX_FIELD_RET(s, iter->ts);
	}

	event = ftrace_find_event(entry->type);
	if (event) {
		enum print_line_t ret = event->funcs->hex(iter, 0, event);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
	}

	SEQ_PUT_FIELD_RET(s, newline);

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_bin_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_FIELD_RET(s, entry->pid);
		SEQ_PUT_FIELD_RET(s, iter->cpu);
		SEQ_PUT_FIELD_RET(s, iter->ts);
	}

	event = ftrace_find_event(entry->type);
	return event ? event->funcs->binary(iter, 0, event) :
		TRACE_TYPE_HANDLED;
}

int trace_empty(struct trace_iterator *iter)
{
	int cpu;

	/* If we are looking at one CPU buffer, only check that one */
	if (iter->cpu_file != TRACE_PIPE_ALL_CPU) {
		cpu = iter->cpu_file;
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
				return 0;
		}
		return 1;
	}

	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
				return 0;
		}
	}

	return 1;
}

/*  Called with trace_event_read_lock() held. */
enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	enum print_line_t ret;

	if (iter->lost_events &&
	    !trace_seq_printf(&iter->seq, "CPU:%d [LOST %lu EVENTS]\n",
				 iter->cpu, iter->lost_events))
		return TRACE_TYPE_PARTIAL_LINE;

	if (iter->trace && iter->trace->print_line) {
		ret = iter->trace->print_line(iter);
		if (ret != TRACE_TYPE_UNHANDLED)
			return ret;
	}

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

	/* print nothing if the buffers are empty */
	if (trace_empty(iter))
		return;

	if (iter->iter_flags & TRACE_FILE_LAT_FMT)
		print_trace_header(m, iter);

	if (!(trace_flags & TRACE_ITER_VERBOSE))
		print_lat_help_header(m);
}

void trace_default_header(struct seq_file *m)
{
	struct trace_iterator *iter = m->private;

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
				print_func_help_header_irq(iter->tr, m);
			else
				print_func_help_header(iter->tr, m);
		}
	}
}

static void test_ftrace_alive(struct seq_file *m)
{
	if (!ftrace_is_dead())
		return;
	seq_printf(m, "# WARNING: FUNCTION TRACING IS CORRUPTED\n");
	seq_printf(m, "#          MAY BE MISSING FUNCTION EVENTS\n");
}

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
		if (iter->trace && iter->trace->print_header)
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

static const struct seq_operations tracer_seq_ops = {
	.start		= s_start,
	.next		= s_next,
	.stop		= s_stop,
	.show		= s_show,
};

static struct trace_iterator *
__tracing_open(struct inode *inode, struct file *file)
{
	long cpu_file = (long) inode->i_private;
	void *fail_ret = ERR_PTR(-ENOMEM);
	struct trace_iterator *iter;
	struct seq_file *m;
	int cpu, ret;

	if (tracing_disabled)
		return ERR_PTR(-ENODEV);

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return ERR_PTR(-ENOMEM);

	/*
	 * We make a copy of the current tracer to avoid concurrent
	 * changes on it while we are reading.
	 */
	mutex_lock(&trace_types_lock);
	iter->trace = kzalloc(sizeof(*iter->trace), GFP_KERNEL);
	if (!iter->trace)
		goto fail;

	if (current_trace)
		*iter->trace = *current_trace;

	if (!zalloc_cpumask_var(&iter->started, GFP_KERNEL))
		goto fail;

	if (current_trace && current_trace->print_max)
		iter->tr = &max_tr;
	else
		iter->tr = &global_trace;
	iter->pos = -1;
	mutex_init(&iter->mutex);
	iter->cpu_file = cpu_file;

	/* Notify the tracer early; before we stop tracing. */
	if (iter->trace && iter->trace->open)
		iter->trace->open(iter);

	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter->tr->buffer))
		iter->iter_flags |= TRACE_FILE_ANNOTATE;

	/* stop the trace while dumping */
	tracing_stop();

	if (iter->cpu_file == TRACE_PIPE_ALL_CPU) {
		for_each_tracing_cpu(cpu) {
			iter->buffer_iter[cpu] =
				ring_buffer_read_prepare(iter->tr->buffer, cpu);
		}
		ring_buffer_read_prepare_sync();
		for_each_tracing_cpu(cpu) {
			ring_buffer_read_start(iter->buffer_iter[cpu]);
			tracing_iter_reset(iter, cpu);
		}
	} else {
		cpu = iter->cpu_file;
		iter->buffer_iter[cpu] =
			ring_buffer_read_prepare(iter->tr->buffer, cpu);
		ring_buffer_read_prepare_sync();
		ring_buffer_read_start(iter->buffer_iter[cpu]);
		tracing_iter_reset(iter, cpu);
	}

	ret = seq_open(file, &tracer_seq_ops);
	if (ret < 0) {
		fail_ret = ERR_PTR(ret);
		goto fail_buffer;
	}

	m = file->private_data;
	m->private = iter;

	mutex_unlock(&trace_types_lock);

	return iter;

 fail_buffer:
	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}
	free_cpumask_var(iter->started);
	tracing_start();
 fail:
	mutex_unlock(&trace_types_lock);
	kfree(iter->trace);
	kfree(iter);

	return fail_ret;
}

int tracing_open_generic(struct inode *inode, struct file *filp)
{
	if (tracing_disabled)
		return -ENODEV;

	filp->private_data = inode->i_private;
	return 0;
}

static int tracing_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct trace_iterator *iter;
	int cpu;

	if (!(file->f_mode & FMODE_READ))
		return 0;

	iter = m->private;

	mutex_lock(&trace_types_lock);
	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}

	if (iter->trace && iter->trace->close)
		iter->trace->close(iter);

	/* reenable tracing if it was previously enabled */
	tracing_start();
	mutex_unlock(&trace_types_lock);

	seq_release(inode, file);
	mutex_destroy(&iter->mutex);
	free_cpumask_var(iter->started);
	kfree(iter->trace);
	kfree(iter);
	return 0;
}

static int tracing_open(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter;
	int ret = 0;

	/* If this file was open for write, then erase contents */
	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC)) {
		long cpu = (long) inode->i_private;

		if (cpu == TRACE_PIPE_ALL_CPU)
			tracing_reset_online_cpus(&global_trace);
		else
			tracing_reset(&global_trace, cpu);
	}

	if (file->f_mode & FMODE_READ) {
		iter = __tracing_open(inode, file);
		if (IS_ERR(iter))
			ret = PTR_ERR(iter);
		else if (trace_flags & TRACE_ITER_LATENCY_FMT)
			iter->iter_flags |= TRACE_FILE_LAT_FMT;
	}
	return ret;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct tracer *t = v;

	(*pos)++;

	if (t)
		t = t->next;

	return t;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct tracer *t;
	loff_t l = 0;

	mutex_lock(&trace_types_lock);
	for (t = trace_types; t && l < *pos; t = t_next(m, t, &l))
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

	seq_printf(m, "%s", t->name);
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
	if (tracing_disabled)
		return -ENODEV;

	return seq_open(file, &show_traces_seq_ops);
}

static ssize_t
tracing_write_stub(struct file *filp, const char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	return count;
}

static loff_t tracing_seek(struct file *file, loff_t offset, int origin)
{
	if (file->f_mode & FMODE_READ)
		return seq_lseek(file, offset, origin);
	else
		return 0;
}

static const struct file_operations tracing_fops = {
	.open		= tracing_open,
	.read		= seq_read,
	.write		= tracing_write_stub,
	.llseek		= tracing_seek,
	.release	= tracing_release,
};

static const struct file_operations show_traces_fops = {
	.open		= show_traces_open,
	.read		= seq_read,
	.release	= seq_release,
	.llseek		= seq_lseek,
};

/*
 * Only trace on a CPU if the bitmask is set:
 */
static cpumask_var_t tracing_cpumask;

/*
 * The tracer itself will not take this lock, but still we want
 * to provide a consistent cpumask to user-space:
 */
static DEFINE_MUTEX(tracing_cpumask_update_lock);

/*
 * Temporary storage for the character representation of the
 * CPU bitmask (and one more byte for the newline):
 */
static char mask_str[NR_CPUS + 1];

static ssize_t
tracing_cpumask_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	int len;

	mutex_lock(&tracing_cpumask_update_lock);

	len = cpumask_scnprintf(mask_str, count, tracing_cpumask);
	if (count - len < 2) {
		count = -EINVAL;
		goto out_err;
	}
	len += sprintf(mask_str + len, "\n");
	count = simple_read_from_buffer(ubuf, count, ppos, mask_str, NR_CPUS+1);

out_err:
	mutex_unlock(&tracing_cpumask_update_lock);

	return count;
}

static ssize_t
tracing_cpumask_write(struct file *filp, const char __user *ubuf,
		      size_t count, loff_t *ppos)
{
	int err, cpu;
	cpumask_var_t tracing_cpumask_new;

	if (!alloc_cpumask_var(&tracing_cpumask_new, GFP_KERNEL))
		return -ENOMEM;

	err = cpumask_parse_user(ubuf, count, tracing_cpumask_new);
	if (err)
		goto err_unlock;

	mutex_lock(&tracing_cpumask_update_lock);

	local_irq_disable();
	arch_spin_lock(&ftrace_max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpumask_test_cpu(cpu, tracing_cpumask) &&
				!cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_inc(&global_trace.data[cpu]->disabled);
			ring_buffer_record_disable_cpu(global_trace.buffer, cpu);
		}
		if (!cpumask_test_cpu(cpu, tracing_cpumask) &&
				cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_dec(&global_trace.data[cpu]->disabled);
			ring_buffer_record_enable_cpu(global_trace.buffer, cpu);
		}
	}
	arch_spin_unlock(&ftrace_max_lock);
	local_irq_enable();

	cpumask_copy(tracing_cpumask, tracing_cpumask_new);

	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask_new);

	return count;

err_unlock:
	free_cpumask_var(tracing_cpumask_new);

	return err;
}

static const struct file_operations tracing_cpumask_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_cpumask_read,
	.write		= tracing_cpumask_write,
	.llseek		= generic_file_llseek,
};

static int tracing_trace_options_show(struct seq_file *m, void *v)
{
	struct tracer_opt *trace_opts;
	u32 tracer_flags;
	int i;

	mutex_lock(&trace_types_lock);
	tracer_flags = current_trace->flags->val;
	trace_opts = current_trace->flags->opts;

	for (i = 0; trace_options[i]; i++) {
		if (trace_flags & (1 << i))
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

static int __set_tracer_option(struct tracer *trace,
			       struct tracer_flags *tracer_flags,
			       struct tracer_opt *opts, int neg)
{
	int ret;

	ret = trace->set_flag(tracer_flags->val, opts->bit, !neg);
	if (ret)
		return ret;

	if (neg)
		tracer_flags->val &= ~opts->bit;
	else
		tracer_flags->val |= opts->bit;
	return 0;
}

/* Try to assign a tracer specific option */
static int set_tracer_option(struct tracer *trace, char *cmp, int neg)
{
	struct tracer_flags *tracer_flags = trace->flags;
	struct tracer_opt *opts = NULL;
	int i;

	for (i = 0; tracer_flags->opts[i].name; i++) {
		opts = &tracer_flags->opts[i];

		if (strcmp(cmp, opts->name) == 0)
			return __set_tracer_option(trace, trace->flags,
						   opts, neg);
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

int set_tracer_flag(unsigned int mask, int enabled)
{
	/* do nothing if flag is already set */
	if (!!(trace_flags & mask) == !!enabled)
		return 0;

	/* Give the tracer a chance to approve the change */
	if (current_trace->flag_changed)
		if (current_trace->flag_changed(current_trace, mask, !!enabled))
			return -EINVAL;

	if (enabled)
		trace_flags |= mask;
	else
		trace_flags &= ~mask;

	if (mask == TRACE_ITER_RECORD_CMD)
		trace_event_enable_cmd_record(enabled);

	if (mask == TRACE_ITER_OVERWRITE)
		ring_buffer_change_overwrite(global_trace.buffer, enabled);

	return 0;
}

static ssize_t
tracing_trace_options_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[64];
	char *cmp;
	int neg = 0;
	int ret = -ENODEV;
	int i;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;
	cmp = strstrip(buf);

	if (strncmp(cmp, "no", 2) == 0) {
		neg = 1;
		cmp += 2;
	}

	mutex_lock(&trace_types_lock);

	for (i = 0; trace_options[i]; i++) {
		if (strcmp(cmp, trace_options[i]) == 0) {
			ret = set_tracer_flag(1 << i, !neg);
			break;
		}
	}

	/* If no option could be set, test the specific tracer options */
	if (!trace_options[i])
		ret = set_tracer_option(current_trace, cmp, neg);

	mutex_unlock(&trace_types_lock);

	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

static int tracing_trace_options_open(struct inode *inode, struct file *file)
{
	if (tracing_disabled)
		return -ENODEV;
	return single_open(file, tracing_trace_options_show, NULL);
}

static const struct file_operations tracing_iter_fops = {
	.open		= tracing_trace_options_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= tracing_trace_options_write,
};

static const char readme_msg[] =
	"tracing mini-HOWTO:\n\n"
	"# mount -t debugfs nodev /sys/kernel/debug\n\n"
	"# cat /sys/kernel/debug/tracing/available_tracers\n"
	"wakeup wakeup_rt preemptirqsoff preemptoff irqsoff function nop\n\n"
	"# cat /sys/kernel/debug/tracing/current_tracer\n"
	"nop\n"
	"# echo wakeup > /sys/kernel/debug/tracing/current_tracer\n"
	"# cat /sys/kernel/debug/tracing/current_tracer\n"
	"wakeup\n"
	"# cat /sys/kernel/debug/tracing/trace_options\n"
	"noprint-parent nosym-offset nosym-addr noverbose\n"
	"# echo print-parent > /sys/kernel/debug/tracing/trace_options\n"
	"# echo 1 > /sys/kernel/debug/tracing/tracing_on\n"
	"# cat /sys/kernel/debug/tracing/trace > /tmp/trace.txt\n"
	"# echo 0 > /sys/kernel/debug/tracing/tracing_on\n"
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

static ssize_t
tracing_saved_cmdlines_read(struct file *file, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char *buf_comm;
	char *file_buf;
	char *buf;
	int len = 0;
	int pid;
	int i;

	file_buf = kmalloc(SAVED_CMDLINES*(16+TASK_COMM_LEN), GFP_KERNEL);
	if (!file_buf)
		return -ENOMEM;

	buf_comm = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
	if (!buf_comm) {
		kfree(file_buf);
		return -ENOMEM;
	}

	buf = file_buf;

	for (i = 0; i < SAVED_CMDLINES; i++) {
		int r;

		pid = map_cmdline_to_pid[i];
		if (pid == -1 || pid == NO_CMDLINE_MAP)
			continue;

		trace_find_cmdline(pid, buf_comm);
		r = sprintf(buf, "%d %s\n", pid, buf_comm);
		buf += r;
		len += r;
	}

	len = simple_read_from_buffer(ubuf, cnt, ppos,
				      file_buf, len);

	kfree(file_buf);
	kfree(buf_comm);

	return len;
}

static const struct file_operations tracing_saved_cmdlines_fops = {
    .open       = tracing_open_generic,
    .read       = tracing_saved_cmdlines_read,
    .llseek	= generic_file_llseek,
};

static ssize_t
tracing_ctrl_read(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = sprintf(buf, "%u\n", tracer_enabled);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_ctrl_write(struct file *filp, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	val = !!val;

	mutex_lock(&trace_types_lock);
	if (tracer_enabled ^ val) {

		/* Only need to warn if this is used to change the state */
		WARN_ONCE(1, "tracing_enabled is deprecated. Use tracing_on");

		if (val) {
			tracer_enabled = 1;
			if (current_trace->start)
				current_trace->start(tr);
			tracing_start();
		} else {
			tracer_enabled = 0;
			tracing_stop();
			if (current_trace->stop)
				current_trace->stop(tr);
		}
	}
	mutex_unlock(&trace_types_lock);

	*ppos += cnt;

	return cnt;
}

static ssize_t
tracing_set_trace_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	char buf[MAX_TRACER_SIZE+2];
	int r;

	mutex_lock(&trace_types_lock);
	if (current_trace)
		r = sprintf(buf, "%s\n", current_trace->name);
	else
		r = sprintf(buf, "\n");
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

int tracer_init(struct tracer *t, struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	return t->init(tr);
}

static int __tracing_resize_ring_buffer(unsigned long size)
{
	int ret;

	/*
	 * If kernel or user changes the size of the ring buffer
	 * we use the size that was given, and we can forget about
	 * expanding it later.
	 */
	ring_buffer_expanded = 1;

	ret = ring_buffer_resize(global_trace.buffer, size);
	if (ret < 0)
		return ret;

	if (!current_trace->use_max_tr)
		goto out;

	ret = ring_buffer_resize(max_tr.buffer, size);
	if (ret < 0) {
		int r;

		r = ring_buffer_resize(global_trace.buffer,
				       global_trace.entries);
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

	max_tr.entries = size;
 out:
	global_trace.entries = size;

	return ret;
}

static ssize_t tracing_resize_ring_buffer(unsigned long size)
{
	int cpu, ret = size;

	mutex_lock(&trace_types_lock);

	tracing_stop();

	/* disable all cpu buffers */
	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_inc(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_inc(&max_tr.data[cpu]->disabled);
	}

	if (size != global_trace.entries)
		ret = __tracing_resize_ring_buffer(size);

	if (ret < 0)
		ret = -ENOMEM;

	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_dec(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_dec(&max_tr.data[cpu]->disabled);
	}

	tracing_start();
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
		ret = __tracing_resize_ring_buffer(trace_buf_size);
	mutex_unlock(&trace_types_lock);

	return ret;
}

struct trace_option_dentry;

static struct trace_option_dentry *
create_trace_option_files(struct tracer *tracer);

static void
destroy_trace_option_files(struct trace_option_dentry *topts);

static int tracing_set_tracer(const char *buf)
{
	static struct trace_option_dentry *topts;
	struct trace_array *tr = &global_trace;
	struct tracer *t;
	int ret = 0;

	mutex_lock(&trace_types_lock);

	if (!ring_buffer_expanded) {
		ret = __tracing_resize_ring_buffer(trace_buf_size);
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
	if (t == current_trace)
		goto out;

	trace_branch_disable();

	current_trace->enabled = false;

	if (current_trace && current_trace->reset)
		current_trace->reset(tr);
	if (current_trace && current_trace->use_max_tr) {
		/*
		 * We don't free the ring buffer. instead, resize it because
		 * The max_tr ring buffer has some state (e.g. ring->clock) and
		 * we want preserve it.
		 */
		ring_buffer_resize(max_tr.buffer, 1);
		max_tr.entries = 1;
	}
	destroy_trace_option_files(topts);

	current_trace = t;

	topts = create_trace_option_files(current_trace);
	if (current_trace->use_max_tr) {
		ret = ring_buffer_resize(max_tr.buffer, global_trace.entries);
		if (ret < 0)
			goto out;
		max_tr.entries = global_trace.entries;
	}

	if (t->init) {
		ret = tracer_init(t, tr);
		if (ret)
			goto out;
	}

	current_trace->enabled = true;
	trace_branch_enable(tr);
 out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

static ssize_t
tracing_set_trace_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[MAX_TRACER_SIZE+1];
	int i;
	size_t ret;
	int err;

	ret = cnt;

	if (cnt > MAX_TRACER_SIZE)
		cnt = MAX_TRACER_SIZE;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	err = tracing_set_tracer(buf);
	if (err)
		return err;

	*ppos += ret;

	return ret;
}

static ssize_t
tracing_max_lat_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	unsigned long *ptr = filp->private_data;
	char buf[64];
	int r;

	r = snprintf(buf, sizeof(buf), "%ld\n",
		     *ptr == (unsigned long)-1 ? -1 : nsecs_to_usecs(*ptr));
	if (r > sizeof(buf))
		r = sizeof(buf);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_max_lat_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	unsigned long *ptr = filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	*ptr = val * 1000;

	return cnt;
}

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	long cpu_file = (long) inode->i_private;
	struct trace_iterator *iter;
	int ret = 0;

	if (tracing_disabled)
		return -ENODEV;

	mutex_lock(&trace_types_lock);

	/* create a buffer to store the information to pass to userspace */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * We make a copy of the current tracer to avoid concurrent
	 * changes on it while we are reading.
	 */
	iter->trace = kmalloc(sizeof(*iter->trace), GFP_KERNEL);
	if (!iter->trace) {
		ret = -ENOMEM;
		goto fail;
	}
	if (current_trace)
		*iter->trace = *current_trace;

	if (!alloc_cpumask_var(&iter->started, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto fail;
	}

	/* trace pipe does not show start of buffer */
	cpumask_setall(iter->started);

	if (trace_flags & TRACE_ITER_LATENCY_FMT)
		iter->iter_flags |= TRACE_FILE_LAT_FMT;

	iter->cpu_file = cpu_file;
	iter->tr = &global_trace;
	mutex_init(&iter->mutex);
	filp->private_data = iter;

	if (iter->trace->pipe_open)
		iter->trace->pipe_open(iter);

	nonseekable_open(inode, filp);
out:
	mutex_unlock(&trace_types_lock);
	return ret;

fail:
	kfree(iter->trace);
	kfree(iter);
	mutex_unlock(&trace_types_lock);
	return ret;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;

	mutex_lock(&trace_types_lock);

	if (iter->trace->pipe_close)
		iter->trace->pipe_close(iter);

	mutex_unlock(&trace_types_lock);

	free_cpumask_var(iter->started);
	mutex_destroy(&iter->mutex);
	kfree(iter->trace);
	kfree(iter);

	return 0;
}

static unsigned int
tracing_poll_pipe(struct file *filp, poll_table *poll_table)
{
	struct trace_iterator *iter = filp->private_data;

	if (trace_flags & TRACE_ITER_BLOCK) {
		/*
		 * Always select as readable when in blocking mode
		 */
		return POLLIN | POLLRDNORM;
	} else {
		if (!trace_empty(iter))
			return POLLIN | POLLRDNORM;
		poll_wait(filp, &trace_wait, poll_table);
		if (!trace_empty(iter))
			return POLLIN | POLLRDNORM;

		return 0;
	}
}


void default_wait_pipe(struct trace_iterator *iter)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&trace_wait, &wait, TASK_INTERRUPTIBLE);

	if (trace_empty(iter))
		schedule();

	finish_wait(&trace_wait, &wait);
}

/*
 * This is a make-shift waitqueue.
 * A tracer might use this callback on some rare cases:
 *
 *  1) the current tracer might hold the runqueue lock when it wakes up
 *     a reader, hence a deadlock (sched, function, and function graph tracers)
 *  2) the function tracers, trace all functions, we don't want
 *     the overhead of calling wake_up and friends
 *     (and tracing them too)
 *
 *     Anyway, this is really very primitive wakeup.
 */
void poll_wait_pipe(struct trace_iterator *iter)
{
	set_current_state(TASK_INTERRUPTIBLE);
	/* sleep for 100 msecs, and try again. */
	schedule_timeout(HZ / 10);
}

/* Must be called with trace_types_lock mutex held. */
static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			return -EAGAIN;
		}

		mutex_unlock(&iter->mutex);

		iter->trace->wait_pipe(iter);

		mutex_lock(&iter->mutex);

		if (signal_pending(current))
			return -EINTR;

		/*
		 * We block until we read something and tracing is disabled.
		 * We still block if tracing is disabled, but we have never
		 * read anything. This allows a user to cat this file, and
		 * then enable tracing. But after we have read something,
		 * we give an EOF when tracing is again disabled.
		 *
		 * iter->pos will be 0 if we haven't read anything.
		 */
		if (!tracer_enabled && iter->pos)
			break;
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
	static struct tracer *old_tracer;
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;

	trace_seq_init(&iter->seq);

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

	/*
	 * Avoid more than one consumer on a single file descriptor
	 * This is just a matter of traces coherency, the ring buffer itself
	 * is protected.
	 */
	mutex_lock(&iter->mutex);
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
	iter->pos = -1;

	trace_event_read_lock();
	trace_access_lock(iter->cpu_file);
	while (trace_find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (iter->seq.len >= cnt)
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
	if (iter->seq.readpos >= iter->seq.len)
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

static void tracing_pipe_buf_release(struct pipe_inode_info *pipe,
				     struct pipe_buffer *buf)
{
	__free_page(buf->page);
}

static void tracing_spd_release_pipe(struct splice_pipe_desc *spd,
				     unsigned int idx)
{
	__free_page(spd->pages[idx]);
}

static const struct pipe_buf_operations tracing_pipe_buf_ops = {
	.can_merge		= 0,
	.map			= generic_pipe_buf_map,
	.unmap			= generic_pipe_buf_unmap,
	.confirm		= generic_pipe_buf_confirm,
	.release		= tracing_pipe_buf_release,
	.steal			= generic_pipe_buf_steal,
	.get			= generic_pipe_buf_get,
};

static size_t
tracing_fill_pipe_page(size_t rem, struct trace_iterator *iter)
{
	size_t count;
	int ret;

	/* Seq buffer is page-sized, exactly what we need. */
	for (;;) {
		count = iter->seq.len;
		ret = print_trace_line(iter);
		count = iter->seq.len - count;
		if (rem < count) {
			rem = 0;
			iter->seq.len -= count;
			break;
		}
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			iter->seq.len -= count;
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
		.flags		= flags,
		.ops		= &tracing_pipe_buf_ops,
		.spd_release	= tracing_spd_release_pipe,
	};
	static struct tracer *old_tracer;
	ssize_t ret;
	size_t rem;
	unsigned int i;

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

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
	for (i = 0, rem = len; i < pipe->buffers && rem; i++) {
		spd.pages[i] = alloc_page(GFP_KERNEL);
		if (!spd.pages[i])
			break;

		rem = tracing_fill_pipe_page(rem, iter);

		/* Copy the data into the page, so we can start over. */
		ret = trace_seq_to_buffer(&iter->seq,
					  page_address(spd.pages[i]),
					  iter->seq.len);
		if (ret < 0) {
			__free_page(spd.pages[i]);
			break;
		}
		spd.partial[i].offset = 0;
		spd.partial[i].len = iter->seq.len;

		trace_seq_init(&iter->seq);
	}

	trace_access_unlock(iter->cpu_file);
	trace_event_read_unlock();
	mutex_unlock(&iter->mutex);

	spd.nr_pages = i;

	ret = splice_to_pipe(pipe, &spd);
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
	struct trace_array *tr = filp->private_data;
	char buf[96];
	int r;

	mutex_lock(&trace_types_lock);
	if (!ring_buffer_expanded)
		r = sprintf(buf, "%lu (expanded: %lu)\n",
			    tr->entries >> 10,
			    trace_buf_size >> 10);
	else
		r = sprintf(buf, "%lu\n", tr->entries >> 10);
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_entries_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
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

	ret = tracing_resize_ring_buffer(val);
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
		size += tr->entries >> 10;
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
	/* disable tracing ? */
	if (trace_flags & TRACE_ITER_STOP_ON_FREE)
		tracing_off();
	/* resize the ring buffer to 0 */
	tracing_resize_ring_buffer(0);

	return 0;
}

static ssize_t
tracing_mark_write(struct file *filp, const char __user *ubuf,
					size_t cnt, loff_t *fpos)
{
	unsigned long addr = (unsigned long)ubuf;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	struct print_entry *entry;
	unsigned long irq_flags;
	struct page *pages[2];
	int nr_pages = 1;
	ssize_t written;
	void *page1;
	void *page2;
	int offset;
	int size;
	int len;
	int ret;

	if (tracing_disabled)
		return -EINVAL;

	if (cnt > TRACE_BUF_SIZE)
		cnt = TRACE_BUF_SIZE;

	/*
	 * Userspace is injecting traces into the kernel trace buffer.
	 * We want to be as non intrusive as possible.
	 * To do so, we do not want to allocate any special buffers
	 * or take any locks, but instead write the userspace data
	 * straight into the ring buffer.
	 *
	 * First we need to pin the userspace buffer into memory,
	 * which, most likely it is, because it just referenced it.
	 * But there's no guarantee that it is. By using get_user_pages_fast()
	 * and kmap_atomic/kunmap_atomic() we can get access to the
	 * pages directly. We then write the data directly into the
	 * ring buffer.
	 */
	BUILD_BUG_ON(TRACE_BUF_SIZE >= PAGE_SIZE);

	/* check if we cross pages */
	if ((addr & PAGE_MASK) != ((addr + cnt) & PAGE_MASK))
		nr_pages = 2;

	offset = addr & (PAGE_SIZE - 1);
	addr &= PAGE_MASK;

	ret = get_user_pages_fast(addr, nr_pages, 0, pages);
	if (ret < nr_pages) {
		while (--ret >= 0)
			put_page(pages[ret]);
		written = -EFAULT;
		goto out;
	}

	page1 = kmap_atomic(pages[0]);
	if (nr_pages == 2)
		page2 = kmap_atomic(pages[1]);

	local_save_flags(irq_flags);
	size = sizeof(*entry) + cnt + 2; /* possible \n added */
	buffer = global_trace.buffer;
	event = trace_buffer_lock_reserve(buffer, TRACE_PRINT, size,
					  irq_flags, preempt_count());
	if (!event) {
		/* Ring buffer disabled, return as if not open for write */
		written = -EBADF;
		goto out_unlock;
	}

	entry = ring_buffer_event_data(event);
	entry->ip = _THIS_IP_;

	if (nr_pages == 2) {
		len = PAGE_SIZE - offset;
		memcpy(&entry->buf, page1 + offset, len);
		memcpy(&entry->buf[len], page2, cnt - len);
	} else
		memcpy(&entry->buf, page1 + offset, cnt);

	if (entry->buf[cnt - 1] != '\n') {
		entry->buf[cnt] = '\n';
		entry->buf[cnt + 1] = '\0';
	} else
		entry->buf[cnt] = '\0';

	ring_buffer_unlock_commit(buffer, event);

	written = cnt;

	*fpos += written;

 out_unlock:
	if (nr_pages == 2)
		kunmap_atomic(page2);
	kunmap_atomic(page1);
	while (nr_pages > 0)
		put_page(pages[--nr_pages]);
 out:
	return written;
}

static int tracing_clock_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_clocks); i++)
		seq_printf(m,
			"%s%s%s%s", i ? " " : "",
			i == trace_clock_id ? "[" : "", trace_clocks[i].name,
			i == trace_clock_id ? "]" : "");
	seq_putc(m, '\n');

	return 0;
}

static ssize_t tracing_clock_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *fpos)
{
	char buf[64];
	const char *clockstr;
	int i;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	clockstr = strstrip(buf);

	for (i = 0; i < ARRAY_SIZE(trace_clocks); i++) {
		if (strcmp(trace_clocks[i].name, clockstr) == 0)
			break;
	}
	if (i == ARRAY_SIZE(trace_clocks))
		return -EINVAL;

	trace_clock_id = i;

	mutex_lock(&trace_types_lock);

	ring_buffer_set_clock(global_trace.buffer, trace_clocks[i].func);
	if (max_tr.buffer)
		ring_buffer_set_clock(max_tr.buffer, trace_clocks[i].func);

	mutex_unlock(&trace_types_lock);

	*fpos += cnt;

	return cnt;
}

static int tracing_clock_open(struct inode *inode, struct file *file)
{
	if (tracing_disabled)
		return -ENODEV;
	return single_open(file, tracing_clock_show, NULL);
}

static const struct file_operations tracing_max_lat_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_max_lat_read,
	.write		= tracing_max_lat_write,
	.llseek		= generic_file_llseek,
};

static const struct file_operations tracing_ctrl_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_ctrl_read,
	.write		= tracing_ctrl_write,
	.llseek		= generic_file_llseek,
};

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
	.open		= tracing_open_generic,
	.read		= tracing_entries_read,
	.write		= tracing_entries_write,
	.llseek		= generic_file_llseek,
};

static const struct file_operations tracing_total_entries_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_total_entries_read,
	.llseek		= generic_file_llseek,
};

static const struct file_operations tracing_free_buffer_fops = {
	.write		= tracing_free_buffer_write,
	.release	= tracing_free_buffer_release,
};

static const struct file_operations tracing_mark_fops = {
	.open		= tracing_open_generic,
	.write		= tracing_mark_write,
	.llseek		= generic_file_llseek,
};

static const struct file_operations trace_clock_fops = {
	.open		= tracing_clock_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= tracing_clock_write,
};

struct ftrace_buffer_info {
	struct trace_array	*tr;
	void			*spare;
	int			cpu;
	unsigned int		read;
};

static int tracing_buffers_open(struct inode *inode, struct file *filp)
{
	int cpu = (int)(long)inode->i_private;
	struct ftrace_buffer_info *info;

	if (tracing_disabled)
		return -ENODEV;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->tr	= &global_trace;
	info->cpu	= cpu;
	info->spare	= NULL;
	/* Force reading ring buffer for first read */
	info->read	= (unsigned int)-1;

	filp->private_data = info;

	return nonseekable_open(inode, filp);
}

static ssize_t
tracing_buffers_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct ftrace_buffer_info *info = filp->private_data;
	ssize_t ret;
	size_t size;

	if (!count)
		return 0;

	if (!info->spare)
		info->spare = ring_buffer_alloc_read_page(info->tr->buffer, info->cpu);
	if (!info->spare)
		return -ENOMEM;

	/* Do we have previous read data to read? */
	if (info->read < PAGE_SIZE)
		goto read;

	trace_access_lock(info->cpu);
	ret = ring_buffer_read_page(info->tr->buffer,
				    &info->spare,
				    count,
				    info->cpu, 0);
	trace_access_unlock(info->cpu);
	if (ret < 0)
		return 0;

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

	if (info->spare)
		ring_buffer_free_read_page(info->tr->buffer, info->spare);
	kfree(info);

	return 0;
}

struct buffer_ref {
	struct ring_buffer	*buffer;
	void			*page;
	int			ref;
};

static void buffer_pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	if (--ref->ref)
		return;

	ring_buffer_free_read_page(ref->buffer, ref->page);
	kfree(ref);
	buf->private = 0;
}

static int buffer_pipe_buf_steal(struct pipe_inode_info *pipe,
				 struct pipe_buffer *buf)
{
	return 1;
}

static void buffer_pipe_buf_get(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	ref->ref++;
}

/* Pipe buffer operations for a buffer. */
static const struct pipe_buf_operations buffer_pipe_buf_ops = {
	.can_merge		= 0,
	.map			= generic_pipe_buf_map,
	.unmap			= generic_pipe_buf_unmap,
	.confirm		= generic_pipe_buf_confirm,
	.release		= buffer_pipe_buf_release,
	.steal			= buffer_pipe_buf_steal,
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

	if (--ref->ref)
		return;

	ring_buffer_free_read_page(ref->buffer, ref->page);
	kfree(ref);
	spd->partial[i].private = 0;
}

static ssize_t
tracing_buffers_splice_read(struct file *file, loff_t *ppos,
			    struct pipe_inode_info *pipe, size_t len,
			    unsigned int flags)
{
	struct ftrace_buffer_info *info = file->private_data;
	struct partial_page partial_def[PIPE_DEF_BUFFERS];
	struct page *pages_def[PIPE_DEF_BUFFERS];
	struct splice_pipe_desc spd = {
		.pages		= pages_def,
		.partial	= partial_def,
		.nr_pages_max	= PIPE_DEF_BUFFERS,
		.flags		= flags,
		.ops		= &buffer_pipe_buf_ops,
		.spd_release	= buffer_spd_release,
	};
	struct buffer_ref *ref;
	int entries, size, i;
	size_t ret;

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

	if (*ppos & (PAGE_SIZE - 1)) {
		WARN_ONCE(1, "Ftrace: previous read must page-align\n");
		ret = -EINVAL;
		goto out;
	}

	if (len & (PAGE_SIZE - 1)) {
		WARN_ONCE(1, "Ftrace: splice_read should page-align\n");
		if (len < PAGE_SIZE) {
			ret = -EINVAL;
			goto out;
		}
		len &= PAGE_MASK;
	}

	trace_access_lock(info->cpu);
	entries = ring_buffer_entries_cpu(info->tr->buffer, info->cpu);

	for (i = 0; i < pipe->buffers && len && entries; i++, len -= PAGE_SIZE) {
		struct page *page;
		int r;

		ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!ref)
			break;

		ref->ref = 1;
		ref->buffer = info->tr->buffer;
		ref->page = ring_buffer_alloc_read_page(ref->buffer, info->cpu);
		if (!ref->page) {
			kfree(ref);
			break;
		}

		r = ring_buffer_read_page(ref->buffer, &ref->page,
					  len, info->cpu, 1);
		if (r < 0) {
			ring_buffer_free_read_page(ref->buffer, ref->page);
			kfree(ref);
			break;
		}

		/*
		 * zero out any left over data, this is going to
		 * user land.
		 */
		size = ring_buffer_page_len(ref->page);
		if (size < PAGE_SIZE)
			memset(ref->page + size, 0, PAGE_SIZE - size);

		page = virt_to_page(ref->page);

		spd.pages[i] = page;
		spd.partial[i].len = PAGE_SIZE;
		spd.partial[i].offset = 0;
		spd.partial[i].private = (unsigned long)ref;
		spd.nr_pages++;
		*ppos += PAGE_SIZE;

		entries = ring_buffer_entries_cpu(info->tr->buffer, info->cpu);
	}

	trace_access_unlock(info->cpu);
	spd.nr_pages = i;

	/* did we read anything? */
	if (!spd.nr_pages) {
		if (flags & SPLICE_F_NONBLOCK)
			ret = -EAGAIN;
		else
			ret = 0;
		/* TODO: block */
		goto out;
	}

	ret = splice_to_pipe(pipe, &spd);
	splice_shrink_spd(&spd);
out:
	return ret;
}

static const struct file_operations tracing_buffers_fops = {
	.open		= tracing_buffers_open,
	.read		= tracing_buffers_read,
	.release	= tracing_buffers_release,
	.splice_read	= tracing_buffers_splice_read,
	.llseek		= no_llseek,
};

static ssize_t
tracing_stats_read(struct file *filp, char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	unsigned long cpu = (unsigned long)filp->private_data;
	struct trace_array *tr = &global_trace;
	struct trace_seq *s;
	unsigned long cnt;
	unsigned long long t;
	unsigned long usec_rem;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	cnt = ring_buffer_entries_cpu(tr->buffer, cpu);
	trace_seq_printf(s, "entries: %ld\n", cnt);

	cnt = ring_buffer_overrun_cpu(tr->buffer, cpu);
	trace_seq_printf(s, "overrun: %ld\n", cnt);

	cnt = ring_buffer_commit_overrun_cpu(tr->buffer, cpu);
	trace_seq_printf(s, "commit overrun: %ld\n", cnt);

	cnt = ring_buffer_bytes_cpu(tr->buffer, cpu);
	trace_seq_printf(s, "bytes: %ld\n", cnt);

	t = ns2usecs(ring_buffer_oldest_event_ts(tr->buffer, cpu));
	usec_rem = do_div(t, USEC_PER_SEC);
	trace_seq_printf(s, "oldest event ts: %5llu.%06lu\n", t, usec_rem);

	t = ns2usecs(ring_buffer_time_stamp(tr->buffer, cpu));
	usec_rem = do_div(t, USEC_PER_SEC);
	trace_seq_printf(s, "now ts: %5llu.%06lu\n", t, usec_rem);

	count = simple_read_from_buffer(ubuf, count, ppos, s->buffer, s->len);

	kfree(s);

	return count;
}

static const struct file_operations tracing_stats_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_stats_read,
	.llseek		= generic_file_llseek,
};

#ifdef CONFIG_DYNAMIC_FTRACE

int __weak ftrace_arch_read_dyn_info(char *buf, int size)
{
	return 0;
}

static ssize_t
tracing_read_dyn_info(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	static char ftrace_dyn_info_buffer[1024];
	static DEFINE_MUTEX(dyn_info_mutex);
	unsigned long *p = filp->private_data;
	char *buf = ftrace_dyn_info_buffer;
	int size = ARRAY_SIZE(ftrace_dyn_info_buffer);
	int r;

	mutex_lock(&dyn_info_mutex);
	r = sprintf(buf, "%ld ", *p);

	r += ftrace_arch_read_dyn_info(buf+r, (size-1)-r);
	buf[r++] = '\n';

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);

	mutex_unlock(&dyn_info_mutex);

	return r;
}

static const struct file_operations tracing_dyn_info_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_read_dyn_info,
	.llseek		= generic_file_llseek,
};
#endif

static struct dentry *d_tracer;

struct dentry *tracing_init_dentry(void)
{
	static int once;

	if (d_tracer)
		return d_tracer;

	if (!debugfs_initialized())
		return NULL;

	d_tracer = debugfs_create_dir("tracing", NULL);

	if (!d_tracer && !once) {
		once = 1;
		pr_warning("Could not create debugfs directory 'tracing'\n");
		return NULL;
	}

	return d_tracer;
}

static struct dentry *d_percpu;

struct dentry *tracing_dentry_percpu(void)
{
	static int once;
	struct dentry *d_tracer;

	if (d_percpu)
		return d_percpu;

	d_tracer = tracing_init_dentry();

	if (!d_tracer)
		return NULL;

	d_percpu = debugfs_create_dir("per_cpu", d_tracer);

	if (!d_percpu && !once) {
		once = 1;
		pr_warning("Could not create debugfs directory 'per_cpu'\n");
		return NULL;
	}

	return d_percpu;
}

static void tracing_init_debugfs_percpu(long cpu)
{
	struct dentry *d_percpu = tracing_dentry_percpu();
	struct dentry *d_cpu;
	char cpu_dir[30]; /* 30 characters should be more than enough */

	snprintf(cpu_dir, 30, "cpu%ld", cpu);
	d_cpu = debugfs_create_dir(cpu_dir, d_percpu);
	if (!d_cpu) {
		pr_warning("Could not create debugfs '%s' entry\n", cpu_dir);
		return;
	}

	/* per cpu trace_pipe */
	trace_create_file("trace_pipe", 0444, d_cpu,
			(void *) cpu, &tracing_pipe_fops);

	/* per cpu trace */
	trace_create_file("trace", 0644, d_cpu,
			(void *) cpu, &tracing_fops);

	trace_create_file("trace_pipe_raw", 0444, d_cpu,
			(void *) cpu, &tracing_buffers_fops);

	trace_create_file("stats", 0444, d_cpu,
			(void *) cpu, &tracing_stats_fops);
}

#ifdef CONFIG_FTRACE_SELFTEST
/* Let selftest have access to static functions in this file */
#include "trace_selftest.c"
#endif

struct trace_option_dentry {
	struct tracer_opt		*opt;
	struct tracer_flags		*flags;
	struct dentry			*entry;
};

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
		ret = __set_tracer_option(current_trace, topt->flags,
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

static ssize_t
trace_options_core_read(struct file *filp, char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	long index = (long)filp->private_data;
	char *buf;

	if (trace_flags & (1 << index))
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
trace_options_core_write(struct file *filp, const char __user *ubuf, size_t cnt,
			 loff_t *ppos)
{
	long index = (long)filp->private_data;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&trace_types_lock);
	ret = set_tracer_flag(1 << index, val);
	mutex_unlock(&trace_types_lock);

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

	ret = debugfs_create_file(name, mode, parent, data, fops);
	if (!ret)
		pr_warning("Could not create debugfs '%s' entry\n", name);

	return ret;
}


static struct dentry *trace_options_init_dentry(void)
{
	struct dentry *d_tracer;
	static struct dentry *t_options;

	if (t_options)
		return t_options;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return NULL;

	t_options = debugfs_create_dir("options", d_tracer);
	if (!t_options) {
		pr_warning("Could not create debugfs directory 'options'\n");
		return NULL;
	}

	return t_options;
}

static void
create_trace_option_file(struct trace_option_dentry *topt,
			 struct tracer_flags *flags,
			 struct tracer_opt *opt)
{
	struct dentry *t_options;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return;

	topt->flags = flags;
	topt->opt = opt;

	topt->entry = trace_create_file(opt->name, 0644, t_options, topt,
				    &trace_options_fops);

}

static struct trace_option_dentry *
create_trace_option_files(struct tracer *tracer)
{
	struct trace_option_dentry *topts;
	struct tracer_flags *flags;
	struct tracer_opt *opts;
	int cnt;

	if (!tracer)
		return NULL;

	flags = tracer->flags;

	if (!flags || !flags->opts)
		return NULL;

	opts = flags->opts;

	for (cnt = 0; opts[cnt].name; cnt++)
		;

	topts = kcalloc(cnt + 1, sizeof(*topts), GFP_KERNEL);
	if (!topts)
		return NULL;

	for (cnt = 0; opts[cnt].name; cnt++)
		create_trace_option_file(&topts[cnt], flags,
					 &opts[cnt]);

	return topts;
}

static void
destroy_trace_option_files(struct trace_option_dentry *topts)
{
	int cnt;

	if (!topts)
		return;

	for (cnt = 0; topts[cnt].opt; cnt++) {
		if (topts[cnt].entry)
			debugfs_remove(topts[cnt].entry);
	}

	kfree(topts);
}

static struct dentry *
create_trace_option_core_file(const char *option, long index)
{
	struct dentry *t_options;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return NULL;

	return trace_create_file(option, 0644, t_options, (void *)index,
				    &trace_options_core_fops);
}

static __init void create_trace_options_dir(void)
{
	struct dentry *t_options;
	int i;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return;

	for (i = 0; trace_options[i]; i++)
		create_trace_option_core_file(trace_options[i], i);
}

static ssize_t
rb_simple_read(struct file *filp, char __user *ubuf,
	       size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	struct ring_buffer *buffer = tr->buffer;
	char buf[64];
	int r;

	if (buffer)
		r = ring_buffer_record_is_on(buffer);
	else
		r = 0;

	r = sprintf(buf, "%d\n", r);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
rb_simple_write(struct file *filp, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	struct ring_buffer *buffer = tr->buffer;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	if (buffer) {
		if (val)
			ring_buffer_record_on(buffer);
		else
			ring_buffer_record_off(buffer);
	}

	(*ppos)++;

	return cnt;
}

static const struct file_operations rb_simple_fops = {
	.open		= tracing_open_generic,
	.read		= rb_simple_read,
	.write		= rb_simple_write,
	.llseek		= default_llseek,
};

static __init int tracer_init_debugfs(void)
{
	struct dentry *d_tracer;
	int cpu;

	trace_access_lock_init();

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	trace_create_file("tracing_enabled", 0644, d_tracer,
			&global_trace, &tracing_ctrl_fops);

	trace_create_file("trace_options", 0644, d_tracer,
			NULL, &tracing_iter_fops);

	trace_create_file("tracing_cpumask", 0644, d_tracer,
			NULL, &tracing_cpumask_fops);

	trace_create_file("trace", 0644, d_tracer,
			(void *) TRACE_PIPE_ALL_CPU, &tracing_fops);

	trace_create_file("available_tracers", 0444, d_tracer,
			&global_trace, &show_traces_fops);

	trace_create_file("current_tracer", 0644, d_tracer,
			&global_trace, &set_tracer_fops);

#ifdef CONFIG_TRACER_MAX_TRACE
	trace_create_file("tracing_max_latency", 0644, d_tracer,
			&tracing_max_latency, &tracing_max_lat_fops);
#endif

	trace_create_file("tracing_thresh", 0644, d_tracer,
			&tracing_thresh, &tracing_max_lat_fops);

	trace_create_file("README", 0444, d_tracer,
			NULL, &tracing_readme_fops);

	trace_create_file("trace_pipe", 0444, d_tracer,
			(void *) TRACE_PIPE_ALL_CPU, &tracing_pipe_fops);

	trace_create_file("buffer_size_kb", 0644, d_tracer,
			&global_trace, &tracing_entries_fops);

	trace_create_file("buffer_total_size_kb", 0444, d_tracer,
			&global_trace, &tracing_total_entries_fops);

	trace_create_file("free_buffer", 0644, d_tracer,
			&global_trace, &tracing_free_buffer_fops);

	trace_create_file("trace_marker", 0220, d_tracer,
			NULL, &tracing_mark_fops);

	trace_create_file("saved_cmdlines", 0444, d_tracer,
			NULL, &tracing_saved_cmdlines_fops);

	trace_create_file("trace_clock", 0644, d_tracer, NULL,
			  &trace_clock_fops);

	trace_create_file("tracing_on", 0644, d_tracer,
			    &global_trace, &rb_simple_fops);

#ifdef CONFIG_DYNAMIC_FTRACE
	trace_create_file("dyn_ftrace_total_info", 0444, d_tracer,
			&ftrace_update_tot_cnt, &tracing_dyn_info_fops);
#endif

	create_trace_options_dir();

	for_each_tracing_cpu(cpu)
		tracing_init_debugfs_percpu(cpu);

	return 0;
}

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
	if (s->len >= 1000)
		s->len = 1000;

	/* should be zero ended, but we are paranoid. */
	s->buffer[s->len] = 0;

	printk(KERN_TRACE "%s", s->buffer);

	trace_seq_init(s);
}

void trace_init_global_iter(struct trace_iterator *iter)
{
	iter->tr = &global_trace;
	iter->trace = current_trace;
	iter->cpu_file = TRACE_PIPE_ALL_CPU;
}

void ftrace_dump(enum ftrace_dump_mode oops_dump_mode)
{
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	static atomic_t dump_running;
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

	trace_init_global_iter(&iter);

	for_each_tracing_cpu(cpu) {
		atomic_inc(&iter.tr->data[cpu]->disabled);
	}

	old_userobj = trace_flags & TRACE_ITER_SYM_USEROBJ;

	/* don't look at user memory in panic mode */
	trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	/* Simulate the iterator */
	iter.tr = &global_trace;
	iter.trace = current_trace;

	switch (oops_dump_mode) {
	case DUMP_ALL:
		iter.cpu_file = TRACE_PIPE_ALL_CPU;
		break;
	case DUMP_ORIG:
		iter.cpu_file = raw_smp_processor_id();
		break;
	case DUMP_NONE:
		goto out_enable;
	default:
		printk(KERN_TRACE "Bad dumping mode, switching to all CPUs dump\n");
		iter.cpu_file = TRACE_PIPE_ALL_CPU;
	}

	printk(KERN_TRACE "Dumping ftrace buffer:\n");

	/* Did function tracer already get disabled? */
	if (ftrace_is_dead()) {
		printk("# WARNING: FUNCTION TRACING IS CORRUPTED\n");
		printk("#          MAY BE MISSING FUNCTION EVENTS\n");
	}

	/*
	 * We need to stop all tracing on all CPUS to read the
	 * the next buffer. This is a bit expensive, but is
	 * not done often. We fill all what we can read,
	 * and then release the locks again.
	 */

	while (!trace_empty(&iter)) {

		if (!cnt)
			printk(KERN_TRACE "---------------------------------\n");

		cnt++;

		/* reset all but tr, trace, and overruns */
		memset(&iter.seq, 0,
		       sizeof(struct trace_iterator) -
		       offsetof(struct trace_iterator, seq));
		iter.iter_flags |= TRACE_FILE_LAT_FMT;
		iter.pos = -1;

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
	trace_flags |= old_userobj;

	for_each_tracing_cpu(cpu) {
		atomic_dec(&iter.tr->data[cpu]->disabled);
	}
 	atomic_dec(&dump_running);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(ftrace_dump);

__init static int tracer_alloc_buffers(void)
{
	int ring_buf_size;
	enum ring_buffer_flags rb_flags;
	int i;
	int ret = -ENOMEM;


	if (!alloc_cpumask_var(&tracing_buffer_mask, GFP_KERNEL))
		goto out;

	if (!alloc_cpumask_var(&tracing_cpumask, GFP_KERNEL))
		goto out_free_buffer_mask;

	/* To save memory, keep the ring buffer size to its minimum */
	if (ring_buffer_expanded)
		ring_buf_size = trace_buf_size;
	else
		ring_buf_size = 1;

	rb_flags = trace_flags & TRACE_ITER_OVERWRITE ? RB_FL_OVERWRITE : 0;

	cpumask_copy(tracing_buffer_mask, cpu_possible_mask);
	cpumask_copy(tracing_cpumask, cpu_all_mask);

	/* TODO: make the number of buffers hot pluggable with CPUS */
	global_trace.buffer = ring_buffer_alloc(ring_buf_size, rb_flags);
	if (!global_trace.buffer) {
		printk(KERN_ERR "tracer: failed to allocate ring buffer!\n");
		WARN_ON(1);
		goto out_free_cpumask;
	}
	global_trace.entries = ring_buffer_size(global_trace.buffer);
	if (global_trace.buffer_disabled)
		tracing_off();


#ifdef CONFIG_TRACER_MAX_TRACE
	max_tr.buffer = ring_buffer_alloc(1, rb_flags);
	if (!max_tr.buffer) {
		printk(KERN_ERR "tracer: failed to allocate max ring buffer!\n");
		WARN_ON(1);
		ring_buffer_free(global_trace.buffer);
		goto out_free_cpumask;
	}
	max_tr.entries = 1;
#endif

	/* Allocate the first page for all buffers */
	for_each_tracing_cpu(i) {
		global_trace.data[i] = &per_cpu(global_trace_cpu, i);
		max_tr.data[i] = &per_cpu(max_tr_data, i);
	}

	trace_init_cmdlines();

	register_tracer(&nop_trace);
	current_trace = &nop_trace;
	/* All seems OK, enable tracing */
	tracing_disabled = 0;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &trace_panic_notifier);

	register_die_notifier(&trace_die_notifier);

	return 0;

out_free_cpumask:
	free_cpumask_var(tracing_cpumask);
out_free_buffer_mask:
	free_cpumask_var(tracing_buffer_mask);
out:
	return ret;
}

__init static int clear_boot_tracer(void)
{
	/*
	 * The default tracer at boot buffer is an init section.
	 * This function is called in lateinit. If we did not
	 * find the boot tracer, then clear it out, to prevent
	 * later registration from accessing the buffer that is
	 * about to be freed.
	 */
	if (!default_bootup_tracer)
		return 0;

	printk(KERN_INFO "ftrace bootup tracer '%s' not registered.\n",
	       default_bootup_tracer);
	default_bootup_tracer = NULL;

	return 0;
}

early_initcall(tracer_alloc_buffers);
fs_initcall(tracer_init_debugfs);
late_initcall(clear_boot_tracer);
