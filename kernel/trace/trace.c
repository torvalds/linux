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
#include <linux/utsrelease.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/debugfs.h>
#include <linux/pagemap.h>
#include <linux/hardirq.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/writeback.h>

#include <linux/stacktrace.h>
#include <linux/ring_buffer.h>
#include <linux/irqflags.h>

#include "trace.h"

#define TRACE_BUFFER_FLAGS	(RB_FL_OVERWRITE)

unsigned long __read_mostly	tracing_max_latency;
unsigned long __read_mostly	tracing_thresh;

/*
 * We need to change this state when a selftest is running.
 * A selftest will lurk into the ring-buffer to count the
 * entries inserted during the selftest although some concurrent
 * insertions into the ring-buffer such as ftrace_printk could occurred
 * at the same time, giving false positive or negative results.
 */
static bool __read_mostly tracing_selftest_running;

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
int tracing_disabled = 1;

static DEFINE_PER_CPU(local_t, ftrace_cpu_disabled);

static inline void ftrace_disable_cpu(void)
{
	preempt_disable();
	local_inc(&__get_cpu_var(ftrace_cpu_disabled));
}

static inline void ftrace_enable_cpu(void)
{
	local_dec(&__get_cpu_var(ftrace_cpu_disabled));
	preempt_enable();
}

static cpumask_var_t __read_mostly	tracing_buffer_mask;

#define for_each_tracing_cpu(cpu)	\
	for_each_cpu(cpu, tracing_buffer_mask)

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
 * /proc/sys/kernel/ftrace_dump_on_oops to true.
 */
int ftrace_dump_on_oops;

static int tracing_set_tracer(char *buf);

static int __init set_ftrace(char *str)
{
	tracing_set_tracer(str);
	return 1;
}
__setup("ftrace", set_ftrace);

static int __init set_ftrace_dump_on_oops(char *str)
{
	ftrace_dump_on_oops = 1;
	return 1;
}
__setup("ftrace_dump_on_oops", set_ftrace_dump_on_oops);

long
ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

cycle_t ftrace_now(int cpu)
{
	u64 ts = ring_buffer_time_stamp(cpu);
	ring_buffer_normalize_time_stamp(cpu, &ts);
	return ts;
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

static DEFINE_PER_CPU(struct trace_array_cpu, max_data);

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

/* function tracing enabled */
int				ftrace_function_enabled;

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
 * max_tracer_type_len is used to simplify the allocating of
 * buffers to read userspace tracer names. We keep track of
 * the longest tracer name registered.
 */
static int			max_tracer_type_len;

/*
 * trace_types_lock is used to protect the trace_types list.
 * This lock is also used to keep user access serialized.
 * Accesses from userspace will grab this lock while userspace
 * activities happen inside the kernel.
 */
static DEFINE_MUTEX(trace_types_lock);

/* trace_wait is a waitqueue for tasks blocked on trace_poll */
static DECLARE_WAIT_QUEUE_HEAD(trace_wait);

/* trace_flags holds trace_options default values */
unsigned long trace_flags = TRACE_ITER_PRINT_PARENT | TRACE_ITER_PRINTK |
	TRACE_ITER_ANNOTATE;

/**
 * trace_wake_up - wake up tasks waiting for trace input
 *
 * Simply wakes up any task that is blocked on the trace_wait
 * queue. These is used with trace_poll for tasks polling the trace.
 */
void trace_wake_up(void)
{
	/*
	 * The runqueue_is_locked() can fail, but this is the best we
	 * have for now:
	 */
	if (!(trace_flags & TRACE_ITER_BLOCK) && !runqueue_is_locked())
		wake_up(&trace_wait);
}

static int __init set_buf_size(char *str)
{
	unsigned long buf_size;
	int ret;

	if (!str)
		return 0;
	ret = strict_strtoul(str, 0, &buf_size);
	/* nr_entries can not be zero */
	if (ret < 0 || buf_size == 0)
		return 0;
	trace_buf_size = buf_size;
	return 1;
}
__setup("trace_buf_size=", set_buf_size);

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
	"sched-tree",
	"ftrace_printk",
	"ftrace_preempt",
	"branch",
	"annotate",
	"userstacktrace",
	"sym-userobj",
	"printk-msg-only",
	NULL
};

/*
 * ftrace_max_lock is used to protect the swapping of buffers
 * when taking a max snapshot. The buffers themselves are
 * protected by per_cpu spinlocks. But the action of the swap
 * needs its own lock.
 *
 * This is defined as a raw_spinlock_t in order to help
 * with performance when lockdep debugging is enabled.
 */
static raw_spinlock_t ftrace_max_lock =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /debugfs/tracing/latency_trace)
 */
static void
__update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct trace_array_cpu *data = tr->data[cpu];

	max_tr.cpu = cpu;
	max_tr.time_start = data->preempt_timestamp;

	data = max_tr.data[cpu];
	data->saved_latency = tracing_max_latency;

	memcpy(data->comm, tsk->comm, TASK_COMM_LEN);
	data->pid = tsk->pid;
	data->uid = task_uid(tsk);
	data->nice = tsk->static_prio - 20 - MAX_RT_PRIO;
	data->policy = tsk->policy;
	data->rt_priority = tsk->rt_priority;

	/* record this tasks comm */
	tracing_record_cmdline(current);
}

/**
 * trace_seq_printf - sequence printing of trace information
 * @s: trace sequence descriptor
 * @fmt: printf format string
 *
 * The tracer may use either sequence operations or its own
 * copy to user routines. To simplify formating of a trace
 * trace_seq_printf is used to store strings into a special
 * buffer (@s). Then the output may be either used by
 * the sequencer or pulled into another buffer.
 */
int
trace_seq_printf(struct trace_seq *s, const char *fmt, ...)
{
	int len = (PAGE_SIZE - 1) - s->len;
	va_list ap;
	int ret;

	if (!len)
		return 0;

	va_start(ap, fmt);
	ret = vsnprintf(s->buffer + s->len, len, fmt, ap);
	va_end(ap);

	/* If we can't write it all, don't bother writing anything */
	if (ret >= len)
		return 0;

	s->len += ret;

	return len;
}

/**
 * trace_seq_puts - trace sequence printing of simple string
 * @s: trace sequence descriptor
 * @str: simple string to record
 *
 * The tracer may use either the sequence operations or its own
 * copy to user routines. This function records a simple string
 * into a special buffer (@s) for later retrieval by a sequencer
 * or other mechanism.
 */
static int
trace_seq_puts(struct trace_seq *s, const char *str)
{
	int len = strlen(str);

	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}

static int
trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	if (s->len >= (PAGE_SIZE - 1))
		return 0;

	s->buffer[s->len++] = c;

	return 1;
}

static int
trace_seq_putmem(struct trace_seq *s, void *mem, size_t len)
{
	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, mem, len);
	s->len += len;

	return len;
}

#define MAX_MEMHEX_BYTES	8
#define HEX_CHARS		(MAX_MEMHEX_BYTES*2 + 1)

static int
trace_seq_putmem_hex(struct trace_seq *s, void *mem, size_t len)
{
	unsigned char hex[HEX_CHARS];
	unsigned char *data = mem;
	int i, j;

#ifdef __BIG_ENDIAN
	for (i = 0, j = 0; i < len; i++) {
#else
	for (i = len-1, j = 0; i >= 0; i--) {
#endif
		hex[j++] = hex_asc_hi(data[i]);
		hex[j++] = hex_asc_lo(data[i]);
	}
	hex[j++] = ' ';

	return trace_seq_putmem(s, hex, j);
}

static int
trace_seq_path(struct trace_seq *s, struct path *path)
{
	unsigned char *p;

	if (s->len >= (PAGE_SIZE - 1))
		return 0;
	p = d_path(path, s->buffer + s->len, PAGE_SIZE - s->len);
	if (!IS_ERR(p)) {
		p = mangle_path(s->buffer + s->len, p, "\n");
		if (p) {
			s->len = p - s->buffer;
			return 1;
		}
	} else {
		s->buffer[s->len++] = '?';
		return 1;
	}

	return 0;
}

static void
trace_seq_reset(struct trace_seq *s)
{
	s->len = 0;
	s->readpos = 0;
}

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret)
		return -EFAULT;

	s->readpos += len;
	return cnt;
}

static void
trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

	s->buffer[len] = 0;
	seq_puts(m, s->buffer);

	trace_seq_reset(s);
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
	struct ring_buffer *buf = tr->buffer;

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);

	tr->buffer = max_tr.buffer;
	max_tr.buffer = buf;

	ftrace_disable_cpu();
	ring_buffer_reset(tr->buffer);
	ftrace_enable_cpu();

	__update_max_tr(tr, tsk, cpu);
	__raw_spin_unlock(&ftrace_max_lock);
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

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);

	ftrace_disable_cpu();

	ring_buffer_reset(max_tr.buffer);
	ret = ring_buffer_swap_cpu(max_tr.buffer, tr->buffer, cpu);

	ftrace_enable_cpu();

	WARN_ON_ONCE(ret);

	__update_max_tr(tr, tsk, cpu);
	__raw_spin_unlock(&ftrace_max_lock);
}

/**
 * register_tracer - register a tracer with the ftrace system.
 * @type - the plugin for the tracer
 *
 * Register a new plugin tracer.
 */
int register_tracer(struct tracer *type)
{
	struct tracer *t;
	int len;
	int ret = 0;

	if (!type->name) {
		pr_info("Tracer must have a name\n");
		return -1;
	}

	/*
	 * When this gets called we hold the BKL which means that
	 * preemption is disabled. Various trace selftests however
	 * need to disable and enable preemption for successful tests.
	 * So we drop the BKL here and grab it after the tests again.
	 */
	unlock_kernel();
	mutex_lock(&trace_types_lock);

	tracing_selftest_running = true;

	for (t = trace_types; t; t = t->next) {
		if (strcmp(type->name, t->name) == 0) {
			/* already found */
			pr_info("Trace %s already registered\n",
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

#ifdef CONFIG_FTRACE_STARTUP_TEST
	if (type->selftest) {
		struct tracer *saved_tracer = current_trace;
		struct trace_array *tr = &global_trace;
		int i;

		/*
		 * Run a selftest on this tracer.
		 * Here we reset the trace buffer, and set the current
		 * tracer to be this tracer. The tracer can then run some
		 * internal tracing to verify that everything is in order.
		 * If we fail, we do not register this tracer.
		 */
		for_each_tracing_cpu(i)
			tracing_reset(tr, i);

		current_trace = type;
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
		for_each_tracing_cpu(i)
			tracing_reset(tr, i);

		printk(KERN_CONT "PASSED\n");
	}
#endif

	type->next = trace_types;
	trace_types = type;
	len = strlen(type->name);
	if (len > max_tracer_type_len)
		max_tracer_type_len = len;

 out:
	tracing_selftest_running = false;
	mutex_unlock(&trace_types_lock);
	lock_kernel();

	return ret;
}

void unregister_tracer(struct tracer *type)
{
	struct tracer **t;
	int len;

	mutex_lock(&trace_types_lock);
	for (t = &trace_types; *t; t = &(*t)->next) {
		if (*t == type)
			goto found;
	}
	pr_info("Trace %s not registered\n", type->name);
	goto out;

 found:
	*t = (*t)->next;
	if (strlen(type->name) != max_tracer_type_len)
		goto out;

	max_tracer_type_len = 0;
	for (t = &trace_types; *t; t = &(*t)->next) {
		len = strlen((*t)->name);
		if (len > max_tracer_type_len)
			max_tracer_type_len = len;
	}
 out:
	mutex_unlock(&trace_types_lock);
}

void tracing_reset(struct trace_array *tr, int cpu)
{
	ftrace_disable_cpu();
	ring_buffer_reset_cpu(tr->buffer, cpu);
	ftrace_enable_cpu();
}

void tracing_reset_online_cpus(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);
}

#define SAVED_CMDLINES 128
static unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
static unsigned map_cmdline_to_pid[SAVED_CMDLINES];
static char saved_cmdlines[SAVED_CMDLINES][TASK_COMM_LEN];
static int cmdline_idx;
static DEFINE_SPINLOCK(trace_cmdline_lock);

/* temporary disable recording */
atomic_t trace_record_cmdline_disabled __read_mostly;

static void trace_init_cmdlines(void)
{
	memset(&map_pid_to_cmdline, -1, sizeof(map_pid_to_cmdline));
	memset(&map_cmdline_to_pid, -1, sizeof(map_cmdline_to_pid));
	cmdline_idx = 0;
}

static int trace_stop_count;
static DEFINE_SPINLOCK(tracing_start_lock);

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

	spin_lock_irqsave(&tracing_start_lock, flags);
	if (--trace_stop_count)
		goto out;

	if (trace_stop_count < 0) {
		/* Someone screwed up their debugging */
		WARN_ON_ONCE(1);
		trace_stop_count = 0;
		goto out;
	}


	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	ftrace_start();
 out:
	spin_unlock_irqrestore(&tracing_start_lock, flags);
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
	spin_lock_irqsave(&tracing_start_lock, flags);
	if (trace_stop_count++)
		goto out;

	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

 out:
	spin_unlock_irqrestore(&tracing_start_lock, flags);
}

void trace_stop_cmdline_recording(void);

static void trace_save_cmdline(struct task_struct *tsk)
{
	unsigned map;
	unsigned idx;

	if (!tsk->pid || unlikely(tsk->pid > PID_MAX_DEFAULT))
		return;

	/*
	 * It's not the end of the world if we don't get
	 * the lock, but we also don't want to spin
	 * nor do we want to disable interrupts,
	 * so if we miss here, then better luck next time.
	 */
	if (!spin_trylock(&trace_cmdline_lock))
		return;

	idx = map_pid_to_cmdline[tsk->pid];
	if (idx >= SAVED_CMDLINES) {
		idx = (cmdline_idx + 1) % SAVED_CMDLINES;

		map = map_cmdline_to_pid[idx];
		if (map <= PID_MAX_DEFAULT)
			map_pid_to_cmdline[map] = (unsigned)-1;

		map_pid_to_cmdline[tsk->pid] = idx;

		cmdline_idx = idx;
	}

	memcpy(&saved_cmdlines[idx], tsk->comm, TASK_COMM_LEN);

	spin_unlock(&trace_cmdline_lock);
}

char *trace_find_cmdline(int pid)
{
	char *cmdline = "<...>";
	unsigned map;

	if (!pid)
		return "<idle>";

	if (pid > PID_MAX_DEFAULT)
		goto out;

	map = map_pid_to_cmdline[pid];
	if (map >= SAVED_CMDLINES)
		goto out;

	cmdline = saved_cmdlines[map];

 out:
	return cmdline;
}

void tracing_record_cmdline(struct task_struct *tsk)
{
	if (atomic_read(&trace_record_cmdline_disabled))
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
	entry->tgid               	= (tsk) ? tsk->tgid : 0;
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

void
trace_function(struct trace_array *tr, struct trace_array_cpu *data,
	       unsigned long ip, unsigned long parent_ip, unsigned long flags,
	       int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_entry *entry;
	unsigned long irq_flags;

	/* If we are reading the ring buffer, don't trace */
	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type			= TRACE_FN;
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static void __trace_graph_entry(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct ftrace_graph_ent *trace,
				unsigned long flags,
				int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_graph_ent_entry *entry;
	unsigned long irq_flags;

	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return;

	event = ring_buffer_lock_reserve(global_trace.buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type			= TRACE_GRAPH_ENT;
	entry->graph_ent			= *trace;
	ring_buffer_unlock_commit(global_trace.buffer, event, irq_flags);
}

static void __trace_graph_return(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct ftrace_graph_ret *trace,
				unsigned long flags,
				int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_graph_ret_entry *entry;
	unsigned long irq_flags;

	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return;

	event = ring_buffer_lock_reserve(global_trace.buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type			= TRACE_GRAPH_RET;
	entry->ret				= *trace;
	ring_buffer_unlock_commit(global_trace.buffer, event, irq_flags);
}
#endif

void
ftrace(struct trace_array *tr, struct trace_array_cpu *data,
       unsigned long ip, unsigned long parent_ip, unsigned long flags,
       int pc)
{
	if (likely(!atomic_read(&data->disabled)))
		trace_function(tr, data, ip, parent_ip, flags, pc);
}

static void ftrace_trace_stack(struct trace_array *tr,
			       struct trace_array_cpu *data,
			       unsigned long flags,
			       int skip, int pc)
{
#ifdef CONFIG_STACKTRACE
	struct ring_buffer_event *event;
	struct stack_entry *entry;
	struct stack_trace trace;
	unsigned long irq_flags;

	if (!(trace_flags & TRACE_ITER_STACKTRACE))
		return;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type		= TRACE_STACK;

	memset(&entry->caller, 0, sizeof(entry->caller));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= skip;
	trace.entries		= entry->caller;

	save_stack_trace(&trace);
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
#endif
}

void __trace_stack(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long flags,
		   int skip)
{
	ftrace_trace_stack(tr, data, flags, skip, preempt_count());
}

static void ftrace_trace_userstack(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long flags, int pc)
{
#ifdef CONFIG_STACKTRACE
	struct ring_buffer_event *event;
	struct userstack_entry *entry;
	struct stack_trace trace;
	unsigned long irq_flags;

	if (!(trace_flags & TRACE_ITER_USERSTACKTRACE))
		return;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type		= TRACE_USER_STACK;

	memset(&entry->caller, 0, sizeof(entry->caller));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= 0;
	trace.entries		= entry->caller;

	save_stack_trace_user(&trace);
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
#endif
}

void __trace_userstack(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long flags)
{
	ftrace_trace_userstack(tr, data, flags, preempt_count());
}

static void
ftrace_trace_special(void *__tr, void *__data,
		     unsigned long arg1, unsigned long arg2, unsigned long arg3,
		     int pc)
{
	struct ring_buffer_event *event;
	struct trace_array_cpu *data = __data;
	struct trace_array *tr = __tr;
	struct special_entry *entry;
	unsigned long irq_flags;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, pc);
	entry->ent.type			= TRACE_SPECIAL;
	entry->arg1			= arg1;
	entry->arg2			= arg2;
	entry->arg3			= arg3;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
	ftrace_trace_stack(tr, data, irq_flags, 4, pc);
	ftrace_trace_userstack(tr, data, irq_flags, pc);

	trace_wake_up();
}

void
__trace_special(void *__tr, void *__data,
		unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	ftrace_trace_special(__tr, __data, arg1, arg2, arg3, preempt_count());
}

void
tracing_sched_switch_trace(struct trace_array *tr,
			   struct trace_array_cpu *data,
			   struct task_struct *prev,
			   struct task_struct *next,
			   unsigned long flags, int pc)
{
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;
	unsigned long irq_flags;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					   &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type			= TRACE_CTX;
	entry->prev_pid			= prev->pid;
	entry->prev_prio		= prev->prio;
	entry->prev_state		= prev->state;
	entry->next_pid			= next->pid;
	entry->next_prio		= next->prio;
	entry->next_state		= next->state;
	entry->next_cpu	= task_cpu(next);
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
	ftrace_trace_stack(tr, data, flags, 5, pc);
	ftrace_trace_userstack(tr, data, flags, pc);
}

void
tracing_sched_wakeup_trace(struct trace_array *tr,
			   struct trace_array_cpu *data,
			   struct task_struct *wakee,
			   struct task_struct *curr,
			   unsigned long flags, int pc)
{
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;
	unsigned long irq_flags;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					   &irq_flags);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type			= TRACE_WAKE;
	entry->prev_pid			= curr->pid;
	entry->prev_prio		= curr->prio;
	entry->prev_state		= curr->state;
	entry->next_pid			= wakee->pid;
	entry->next_prio		= wakee->prio;
	entry->next_state		= wakee->state;
	entry->next_cpu			= task_cpu(wakee);
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);
	ftrace_trace_stack(tr, data, flags, 6, pc);
	ftrace_trace_userstack(tr, data, flags, pc);

	trace_wake_up();
}

void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu;
	int pc;

	if (tracing_disabled)
		return;

	pc = preempt_count();
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (likely(atomic_inc_return(&data->disabled) == 1))
		ftrace_trace_special(tr, data, arg1, arg2, arg3, pc);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#ifdef CONFIG_FUNCTION_TRACER
static void
function_trace_call_preempt_only(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu, resched;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	pc = preempt_count();
	resched = ftrace_preempt_disable();
	local_save_flags(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		trace_function(tr, data, ip, parent_ip, flags, pc);

	atomic_dec(&data->disabled);
	ftrace_preempt_enable(resched);
}

static void
function_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	if (unlikely(!ftrace_function_enabled))
		return;

	/*
	 * Need to use raw, since this must be called before the
	 * recursive protection is performed.
	 */
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1)) {
		pc = preempt_count();
		trace_function(tr, data, ip, parent_ip, flags, pc);
	}

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
int trace_graph_entry(struct ftrace_graph_ent *trace)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	if (!ftrace_trace_task(current))
		return 0;

	if (!ftrace_graph_addr(trace->func))
		return 0;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);
	if (likely(disabled == 1)) {
		pc = preempt_count();
		__trace_graph_entry(tr, data, trace, flags, pc);
	}
	/* Only do the atomic if it is not already set */
	if (!test_tsk_trace_graph(current))
		set_tsk_trace_graph(current);
	atomic_dec(&data->disabled);
	local_irq_restore(flags);

	return 1;
}

void trace_graph_return(struct ftrace_graph_ret *trace)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);
	if (likely(disabled == 1)) {
		pc = preempt_count();
		__trace_graph_return(tr, data, trace, flags, pc);
	}
	if (!trace->depth)
		clear_tsk_trace_graph(current);
	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = function_trace_call,
};

void tracing_start_function_trace(void)
{
	ftrace_function_enabled = 0;

	if (trace_flags & TRACE_ITER_PREEMPTONLY)
		trace_ops.func = function_trace_call_preempt_only;
	else
		trace_ops.func = function_trace_call;

	register_ftrace_function(&trace_ops);
	ftrace_function_enabled = 1;
}

void tracing_stop_function_trace(void)
{
	ftrace_function_enabled = 0;
	unregister_ftrace_function(&trace_ops);
}
#endif

enum trace_file_type {
	TRACE_FILE_LAT_FMT	= 1,
	TRACE_FILE_ANNOTATE	= 2,
};

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
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts)
{
	struct ring_buffer_event *event;
	struct ring_buffer_iter *buf_iter = iter->buffer_iter[cpu];

	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();

	if (buf_iter)
		event = ring_buffer_iter_peek(buf_iter, ts);
	else
		event = ring_buffer_peek(iter->tr->buffer, cpu, ts);

	ftrace_enable_cpu();

	return event ? ring_buffer_event_data(event) : NULL;
}

static struct trace_entry *
__find_next_entry(struct trace_iterator *iter, int *ent_cpu, u64 *ent_ts)
{
	struct ring_buffer *buffer = iter->tr->buffer;
	struct trace_entry *ent, *next = NULL;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int cpu;

	for_each_tracing_cpu(cpu) {

		if (ring_buffer_empty_cpu(buffer, cpu))
			continue;

		ent = peek_next_entry(iter, cpu, &ts);

		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
		}
	}

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	return next;
}

/* Find the next real entry, without updating the iterator itself */
static struct trace_entry *
find_next_entry(struct trace_iterator *iter, int *ent_cpu, u64 *ent_ts)
{
	return __find_next_entry(iter, ent_cpu, ent_ts);
}

/* Find the next real entry, and increment the iterator to the next entry */
static void *find_next_entry_inc(struct trace_iterator *iter)
{
	iter->ent = __find_next_entry(iter, &iter->cpu, &iter->ts);

	if (iter->ent)
		trace_iterator_increment(iter);

	return iter->ent ? iter : NULL;
}

static void trace_consume(struct trace_iterator *iter)
{
	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();
	ring_buffer_consume(iter->tr->buffer, iter->cpu, &iter->ts);
	ftrace_enable_cpu();
}

static void *s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	int i = (int)*pos;
	void *ent;

	(*pos)++;

	/* can't go backwards */
	if (iter->idx > i)
		return NULL;

	if (iter->idx < 0)
		ent = find_next_entry_inc(iter);
	else
		ent = iter;

	while (ent && iter->idx < i)
		ent = find_next_entry_inc(iter);

	iter->pos = *pos;

	return ent;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l = 0;
	int cpu;

	mutex_lock(&trace_types_lock);

	if (!current_trace || current_trace != iter->trace) {
		mutex_unlock(&trace_types_lock);
		return NULL;
	}

	atomic_inc(&trace_record_cmdline_disabled);

	if (*pos != iter->pos) {
		iter->ent = NULL;
		iter->cpu = 0;
		iter->idx = -1;

		ftrace_disable_cpu();

		for_each_tracing_cpu(cpu) {
			ring_buffer_iter_reset(iter->buffer_iter[cpu]);
		}

		ftrace_enable_cpu();

		for (p = iter; p && l < *pos; p = s_next(m, p, &l))
			;

	} else {
		l = *pos - 1;
		p = s_next(m, p, &l);
	}

	return p;
}

static void s_stop(struct seq_file *m, void *p)
{
	atomic_dec(&trace_record_cmdline_disabled);
	mutex_unlock(&trace_types_lock);
}

#ifdef CONFIG_KRETPROBES
static inline const char *kretprobed(const char *name)
{
	static const char tramp_name[] = "kretprobe_trampoline";
	int size = sizeof(tramp_name);

	if (strncmp(tramp_name, name, size) == 0)
		return "[unknown/kretprobe'd]";
	return name;
}
#else
static inline const char *kretprobed(const char *name)
{
	return name;
}
#endif /* CONFIG_KRETPROBES */

static int
seq_print_sym_short(struct trace_seq *s, const char *fmt, unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];
	const char *name;

	kallsyms_lookup(address, NULL, NULL, NULL, str);

	name = kretprobed(str);

	return trace_seq_printf(s, fmt, name);
#endif
	return 1;
}

static int
seq_print_sym_offset(struct trace_seq *s, const char *fmt,
		     unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];
	const char *name;

	sprint_symbol(str, address);
	name = kretprobed(str);

	return trace_seq_printf(s, fmt, name);
#endif
	return 1;
}

#ifndef CONFIG_64BIT
# define IP_FMT "%08lx"
#else
# define IP_FMT "%016lx"
#endif

int
seq_print_ip_sym(struct trace_seq *s, unsigned long ip, unsigned long sym_flags)
{
	int ret;

	if (!ip)
		return trace_seq_printf(s, "0");

	if (sym_flags & TRACE_ITER_SYM_OFFSET)
		ret = seq_print_sym_offset(s, "%s", ip);
	else
		ret = seq_print_sym_short(s, "%s", ip);

	if (!ret)
		return 0;

	if (sym_flags & TRACE_ITER_SYM_ADDR)
		ret = trace_seq_printf(s, " <" IP_FMT ">", ip);
	return ret;
}

static inline int seq_print_user_ip(struct trace_seq *s, struct mm_struct *mm,
				    unsigned long ip, unsigned long sym_flags)
{
	struct file *file = NULL;
	unsigned long vmstart = 0;
	int ret = 1;

	if (mm) {
		const struct vm_area_struct *vma;

		down_read(&mm->mmap_sem);
		vma = find_vma(mm, ip);
		if (vma) {
			file = vma->vm_file;
			vmstart = vma->vm_start;
		}
		if (file) {
			ret = trace_seq_path(s, &file->f_path);
			if (ret)
				ret = trace_seq_printf(s, "[+0x%lx]", ip - vmstart);
		}
		up_read(&mm->mmap_sem);
	}
	if (ret && ((sym_flags & TRACE_ITER_SYM_ADDR) || !file))
		ret = trace_seq_printf(s, " <" IP_FMT ">", ip);
	return ret;
}

static int
seq_print_userip_objs(const struct userstack_entry *entry, struct trace_seq *s,
		      unsigned long sym_flags)
{
	struct mm_struct *mm = NULL;
	int ret = 1;
	unsigned int i;

	if (trace_flags & TRACE_ITER_SYM_USEROBJ) {
		struct task_struct *task;
		/*
		 * we do the lookup on the thread group leader,
		 * since individual threads might have already quit!
		 */
		rcu_read_lock();
		task = find_task_by_vpid(entry->ent.tgid);
		if (task)
			mm = get_task_mm(task);
		rcu_read_unlock();
	}

	for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
		unsigned long ip = entry->caller[i];

		if (ip == ULONG_MAX || !ret)
			break;
		if (i && ret)
			ret = trace_seq_puts(s, " <- ");
		if (!ip) {
			if (ret)
				ret = trace_seq_puts(s, "??");
			continue;
		}
		if (!ret)
			break;
		if (ret)
			ret = seq_print_user_ip(s, mm, ip, sym_flags);
	}

	if (mm)
		mmput(mm);
	return ret;
}

static void print_lat_help_header(struct seq_file *m)
{
	seq_puts(m, "#                  _------=> CPU#            \n");
	seq_puts(m, "#                 / _-----=> irqs-off        \n");
	seq_puts(m, "#                | / _----=> need-resched    \n");
	seq_puts(m, "#                || / _---=> hardirq/softirq \n");
	seq_puts(m, "#                ||| / _--=> preempt-depth   \n");
	seq_puts(m, "#                |||| /                      \n");
	seq_puts(m, "#                |||||     delay             \n");
	seq_puts(m, "#  cmd     pid   ||||| time  |   caller      \n");
	seq_puts(m, "#     \\   /      |||||   \\   |   /           \n");
}

static void print_func_help_header(struct seq_file *m)
{
	seq_puts(m, "#           TASK-PID    CPU#    TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |       |          |         |\n");
}


static void
print_trace_header(struct seq_file *m, struct trace_iterator *iter)
{
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_array *tr = iter->tr;
	struct trace_array_cpu *data = tr->data[tr->cpu];
	struct tracer *type = current_trace;
	unsigned long total;
	unsigned long entries;
	const char *name = "preemption";

	if (type)
		name = type->name;

	entries = ring_buffer_entries(iter->tr->buffer);
	total = entries +
		ring_buffer_overruns(iter->tr->buffer);

	seq_printf(m, "%s latency trace v1.1.5 on %s\n",
		   name, UTS_RELEASE);
	seq_puts(m, "-----------------------------------"
		 "---------------------------------\n");
	seq_printf(m, " latency: %lu us, #%lu/%lu, CPU#%d |"
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
	seq_puts(m, "    -----------------\n");
	seq_printf(m, "    | task: %.16s-%d "
		   "(uid:%d nice:%ld policy:%ld rt_prio:%ld)\n",
		   data->comm, data->pid, data->uid, data->nice,
		   data->policy, data->rt_priority);
	seq_puts(m, "    -----------------\n");

	if (data->critical_start) {
		seq_puts(m, " => started at: ");
		seq_print_ip_sym(&iter->seq, data->critical_start, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "\n => ended at:   ");
		seq_print_ip_sym(&iter->seq, data->critical_end, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "\n");
	}

	seq_puts(m, "\n");
}

static void
lat_print_generic(struct trace_seq *s, struct trace_entry *entry, int cpu)
{
	int hardirq, softirq;
	char *comm;

	comm = trace_find_cmdline(entry->pid);

	trace_seq_printf(s, "%8.8s-%-5d ", comm, entry->pid);
	trace_seq_printf(s, "%3d", cpu);
	trace_seq_printf(s, "%c%c",
			(entry->flags & TRACE_FLAG_IRQS_OFF) ? 'd' :
			 (entry->flags & TRACE_FLAG_IRQS_NOSUPPORT) ? 'X' : '.',
			((entry->flags & TRACE_FLAG_NEED_RESCHED) ? 'N' : '.'));

	hardirq = entry->flags & TRACE_FLAG_HARDIRQ;
	softirq = entry->flags & TRACE_FLAG_SOFTIRQ;
	if (hardirq && softirq) {
		trace_seq_putc(s, 'H');
	} else {
		if (hardirq) {
			trace_seq_putc(s, 'h');
		} else {
			if (softirq)
				trace_seq_putc(s, 's');
			else
				trace_seq_putc(s, '.');
		}
	}

	if (entry->preempt_count)
		trace_seq_printf(s, "%x", entry->preempt_count);
	else
		trace_seq_puts(s, ".");
}

unsigned long preempt_mark_thresh = 100;

static void
lat_print_timestamp(struct trace_seq *s, u64 abs_usecs,
		    unsigned long rel_usecs)
{
	trace_seq_printf(s, " %4lldus", abs_usecs);
	if (rel_usecs > preempt_mark_thresh)
		trace_seq_puts(s, "!: ");
	else if (rel_usecs > 1)
		trace_seq_puts(s, "+: ");
	else
		trace_seq_puts(s, " : ");
}

static const char state_to_char[] = TASK_STATE_TO_CHAR_STR;

static int task_state_char(unsigned long state)
{
	int bit = state ? __ffs(state) + 1 : 0;

	return bit < sizeof(state_to_char) - 1 ? state_to_char[bit] : '?';
}

/*
 * The message is supposed to contain an ending newline.
 * If the printing stops prematurely, try to add a newline of our own.
 */
void trace_seq_print_cont(struct trace_seq *s, struct trace_iterator *iter)
{
	struct trace_entry *ent;
	struct trace_field_cont *cont;
	bool ok = true;

	ent = peek_next_entry(iter, iter->cpu, NULL);
	if (!ent || ent->type != TRACE_CONT) {
		trace_seq_putc(s, '\n');
		return;
	}

	do {
		cont = (struct trace_field_cont *)ent;
		if (ok)
			ok = (trace_seq_printf(s, "%s", cont->buf) > 0);

		ftrace_disable_cpu();

		if (iter->buffer_iter[iter->cpu])
			ring_buffer_read(iter->buffer_iter[iter->cpu], NULL);
		else
			ring_buffer_consume(iter->tr->buffer, iter->cpu, NULL);

		ftrace_enable_cpu();

		ent = peek_next_entry(iter, iter->cpu, NULL);
	} while (ent && ent->type == TRACE_CONT);

	if (!ok)
		trace_seq_putc(s, '\n');
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

	cpumask_set_cpu(iter->cpu, iter->started);
	trace_seq_printf(s, "##### CPU %u buffer started ####\n", iter->cpu);
}

static enum print_line_t
print_lat_fmt(struct trace_iterator *iter, unsigned int trace_idx, int cpu)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *next_entry;
	unsigned long verbose = (trace_flags & TRACE_ITER_VERBOSE);
	struct trace_entry *entry = iter->ent;
	unsigned long abs_usecs;
	unsigned long rel_usecs;
	u64 next_ts;
	char *comm;
	int S, T;
	int i;

	if (entry->type == TRACE_CONT)
		return TRACE_TYPE_HANDLED;

	test_cpu_buff_start(iter);

	next_entry = find_next_entry(iter, NULL, &next_ts);
	if (!next_entry)
		next_ts = iter->ts;
	rel_usecs = ns2usecs(next_ts - iter->ts);
	abs_usecs = ns2usecs(iter->ts - iter->tr->time_start);

	if (verbose) {
		comm = trace_find_cmdline(entry->pid);
		trace_seq_printf(s, "%16s %5d %3d %d %08x %08x [%08lx]"
				 " %ld.%03ldms (+%ld.%03ldms): ",
				 comm,
				 entry->pid, cpu, entry->flags,
				 entry->preempt_count, trace_idx,
				 ns2usecs(iter->ts),
				 abs_usecs/1000,
				 abs_usecs % 1000, rel_usecs/1000,
				 rel_usecs % 1000);
	} else {
		lat_print_generic(s, entry, cpu);
		lat_print_timestamp(s, abs_usecs, rel_usecs);
	}
	switch (entry->type) {
	case TRACE_FN: {
		struct ftrace_entry *field;

		trace_assign_type(field, entry);

		seq_print_ip_sym(s, field->ip, sym_flags);
		trace_seq_puts(s, " (");
		seq_print_ip_sym(s, field->parent_ip, sym_flags);
		trace_seq_puts(s, ")\n");
		break;
	}
	case TRACE_CTX:
	case TRACE_WAKE: {
		struct ctx_switch_entry *field;

		trace_assign_type(field, entry);

		T = task_state_char(field->next_state);
		S = task_state_char(field->prev_state);
		comm = trace_find_cmdline(field->next_pid);
		trace_seq_printf(s, " %5d:%3d:%c %s [%03d] %5d:%3d:%c %s\n",
				 field->prev_pid,
				 field->prev_prio,
				 S, entry->type == TRACE_CTX ? "==>" : "  +",
				 field->next_cpu,
				 field->next_pid,
				 field->next_prio,
				 T, comm);
		break;
	}
	case TRACE_SPECIAL: {
		struct special_entry *field;

		trace_assign_type(field, entry);

		trace_seq_printf(s, "# %ld %ld %ld\n",
				 field->arg1,
				 field->arg2,
				 field->arg3);
		break;
	}
	case TRACE_STACK: {
		struct stack_entry *field;

		trace_assign_type(field, entry);

		for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
			if (i)
				trace_seq_puts(s, " <= ");
			seq_print_ip_sym(s, field->caller[i], sym_flags);
		}
		trace_seq_puts(s, "\n");
		break;
	}
	case TRACE_PRINT: {
		struct print_entry *field;

		trace_assign_type(field, entry);

		seq_print_ip_sym(s, field->ip, sym_flags);
		trace_seq_printf(s, ": %s", field->buf);
		if (entry->flags & TRACE_FLAG_CONT)
			trace_seq_print_cont(s, iter);
		break;
	}
	case TRACE_BRANCH: {
		struct trace_branch *field;

		trace_assign_type(field, entry);

		trace_seq_printf(s, "[%s] %s:%s:%d\n",
				 field->correct ? "  ok  " : " MISS ",
				 field->func,
				 field->file,
				 field->line);
		break;
	}
	case TRACE_USER_STACK: {
		struct userstack_entry *field;

		trace_assign_type(field, entry);

		seq_print_userip_objs(field, s, sym_flags);
		trace_seq_putc(s, '\n');
		break;
	}
	default:
		trace_seq_printf(s, "Unknown type %d\n", entry->type);
	}
	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *entry;
	unsigned long usec_rem;
	unsigned long long t;
	unsigned long secs;
	char *comm;
	int ret;
	int S, T;
	int i;

	entry = iter->ent;

	if (entry->type == TRACE_CONT)
		return TRACE_TYPE_HANDLED;

	test_cpu_buff_start(iter);

	comm = trace_find_cmdline(iter->ent->pid);

	t = ns2usecs(iter->ts);
	usec_rem = do_div(t, 1000000ULL);
	secs = (unsigned long)t;

	ret = trace_seq_printf(s, "%16s-%-5d ", comm, entry->pid);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	ret = trace_seq_printf(s, "[%03d] ", iter->cpu);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;
	ret = trace_seq_printf(s, "%5lu.%06lu: ", secs, usec_rem);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	switch (entry->type) {
	case TRACE_FN: {
		struct ftrace_entry *field;

		trace_assign_type(field, entry);

		ret = seq_print_ip_sym(s, field->ip, sym_flags);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		if ((sym_flags & TRACE_ITER_PRINT_PARENT) &&
						field->parent_ip) {
			ret = trace_seq_printf(s, " <-");
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
			ret = seq_print_ip_sym(s,
					       field->parent_ip,
					       sym_flags);
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}
		ret = trace_seq_printf(s, "\n");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_CTX:
	case TRACE_WAKE: {
		struct ctx_switch_entry *field;

		trace_assign_type(field, entry);

		T = task_state_char(field->next_state);
		S = task_state_char(field->prev_state);
		ret = trace_seq_printf(s, " %5d:%3d:%c %s [%03d] %5d:%3d:%c\n",
				       field->prev_pid,
				       field->prev_prio,
				       S,
				       entry->type == TRACE_CTX ? "==>" : "  +",
				       field->next_cpu,
				       field->next_pid,
				       field->next_prio,
				       T);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_SPECIAL: {
		struct special_entry *field;

		trace_assign_type(field, entry);

		ret = trace_seq_printf(s, "# %ld %ld %ld\n",
				 field->arg1,
				 field->arg2,
				 field->arg3);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_STACK: {
		struct stack_entry *field;

		trace_assign_type(field, entry);

		for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
			if (i) {
				ret = trace_seq_puts(s, " <= ");
				if (!ret)
					return TRACE_TYPE_PARTIAL_LINE;
			}
			ret = seq_print_ip_sym(s, field->caller[i],
					       sym_flags);
			if (!ret)
				return TRACE_TYPE_PARTIAL_LINE;
		}
		ret = trace_seq_puts(s, "\n");
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_PRINT: {
		struct print_entry *field;

		trace_assign_type(field, entry);

		seq_print_ip_sym(s, field->ip, sym_flags);
		trace_seq_printf(s, ": %s", field->buf);
		if (entry->flags & TRACE_FLAG_CONT)
			trace_seq_print_cont(s, iter);
		break;
	}
	case TRACE_GRAPH_RET: {
		return print_graph_function(iter);
	}
	case TRACE_GRAPH_ENT: {
		return print_graph_function(iter);
	}
	case TRACE_BRANCH: {
		struct trace_branch *field;

		trace_assign_type(field, entry);

		trace_seq_printf(s, "[%s] %s:%s:%d\n",
				 field->correct ? "  ok  " : " MISS ",
				 field->func,
				 field->file,
				 field->line);
		break;
	}
	case TRACE_USER_STACK: {
		struct userstack_entry *field;

		trace_assign_type(field, entry);

		ret = seq_print_userip_objs(field, s, sym_flags);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		ret = trace_seq_putc(s, '\n');
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	}
	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	int ret;
	int S, T;

	entry = iter->ent;

	if (entry->type == TRACE_CONT)
		return TRACE_TYPE_HANDLED;

	ret = trace_seq_printf(s, "%d %d %llu ",
		entry->pid, iter->cpu, iter->ts);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	switch (entry->type) {
	case TRACE_FN: {
		struct ftrace_entry *field;

		trace_assign_type(field, entry);

		ret = trace_seq_printf(s, "%x %x\n",
					field->ip,
					field->parent_ip);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_CTX:
	case TRACE_WAKE: {
		struct ctx_switch_entry *field;

		trace_assign_type(field, entry);

		T = task_state_char(field->next_state);
		S = entry->type == TRACE_WAKE ? '+' :
			task_state_char(field->prev_state);
		ret = trace_seq_printf(s, "%d %d %c %d %d %d %c\n",
				       field->prev_pid,
				       field->prev_prio,
				       S,
				       field->next_cpu,
				       field->next_pid,
				       field->next_prio,
				       T);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_SPECIAL:
	case TRACE_USER_STACK:
	case TRACE_STACK: {
		struct special_entry *field;

		trace_assign_type(field, entry);

		ret = trace_seq_printf(s, "# %ld %ld %ld\n",
				 field->arg1,
				 field->arg2,
				 field->arg3);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		break;
	}
	case TRACE_PRINT: {
		struct print_entry *field;

		trace_assign_type(field, entry);

		trace_seq_printf(s, "# %lx %s", field->ip, field->buf);
		if (entry->flags & TRACE_FLAG_CONT)
			trace_seq_print_cont(s, iter);
		break;
	}
	}
	return TRACE_TYPE_HANDLED;
}

#define SEQ_PUT_FIELD_RET(s, x)				\
do {							\
	if (!trace_seq_putmem(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

#define SEQ_PUT_HEX_FIELD_RET(s, x)			\
do {							\
	BUILD_BUG_ON(sizeof(x) > MAX_MEMHEX_BYTES);	\
	if (!trace_seq_putmem_hex(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

static enum print_line_t print_hex_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned char newline = '\n';
	struct trace_entry *entry;
	int S, T;

	entry = iter->ent;

	if (entry->type == TRACE_CONT)
		return TRACE_TYPE_HANDLED;

	SEQ_PUT_HEX_FIELD_RET(s, entry->pid);
	SEQ_PUT_HEX_FIELD_RET(s, iter->cpu);
	SEQ_PUT_HEX_FIELD_RET(s, iter->ts);

	switch (entry->type) {
	case TRACE_FN: {
		struct ftrace_entry *field;

		trace_assign_type(field, entry);

		SEQ_PUT_HEX_FIELD_RET(s, field->ip);
		SEQ_PUT_HEX_FIELD_RET(s, field->parent_ip);
		break;
	}
	case TRACE_CTX:
	case TRACE_WAKE: {
		struct ctx_switch_entry *field;

		trace_assign_type(field, entry);

		T = task_state_char(field->next_state);
		S = entry->type == TRACE_WAKE ? '+' :
			task_state_char(field->prev_state);
		SEQ_PUT_HEX_FIELD_RET(s, field->prev_pid);
		SEQ_PUT_HEX_FIELD_RET(s, field->prev_prio);
		SEQ_PUT_HEX_FIELD_RET(s, S);
		SEQ_PUT_HEX_FIELD_RET(s, field->next_cpu);
		SEQ_PUT_HEX_FIELD_RET(s, field->next_pid);
		SEQ_PUT_HEX_FIELD_RET(s, field->next_prio);
		SEQ_PUT_HEX_FIELD_RET(s, T);
		break;
	}
	case TRACE_SPECIAL:
	case TRACE_USER_STACK:
	case TRACE_STACK: {
		struct special_entry *field;

		trace_assign_type(field, entry);

		SEQ_PUT_HEX_FIELD_RET(s, field->arg1);
		SEQ_PUT_HEX_FIELD_RET(s, field->arg2);
		SEQ_PUT_HEX_FIELD_RET(s, field->arg3);
		break;
	}
	}
	SEQ_PUT_FIELD_RET(s, newline);

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_printk_msg_only(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry = iter->ent;
	struct print_entry *field;
	int ret;

	trace_assign_type(field, entry);

	ret = trace_seq_printf(s, field->buf);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	if (entry->flags & TRACE_FLAG_CONT)
		trace_seq_print_cont(s, iter);

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_bin_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;

	entry = iter->ent;

	if (entry->type == TRACE_CONT)
		return TRACE_TYPE_HANDLED;

	SEQ_PUT_FIELD_RET(s, entry->pid);
	SEQ_PUT_FIELD_RET(s, entry->cpu);
	SEQ_PUT_FIELD_RET(s, iter->ts);

	switch (entry->type) {
	case TRACE_FN: {
		struct ftrace_entry *field;

		trace_assign_type(field, entry);

		SEQ_PUT_FIELD_RET(s, field->ip);
		SEQ_PUT_FIELD_RET(s, field->parent_ip);
		break;
	}
	case TRACE_CTX: {
		struct ctx_switch_entry *field;

		trace_assign_type(field, entry);

		SEQ_PUT_FIELD_RET(s, field->prev_pid);
		SEQ_PUT_FIELD_RET(s, field->prev_prio);
		SEQ_PUT_FIELD_RET(s, field->prev_state);
		SEQ_PUT_FIELD_RET(s, field->next_pid);
		SEQ_PUT_FIELD_RET(s, field->next_prio);
		SEQ_PUT_FIELD_RET(s, field->next_state);
		break;
	}
	case TRACE_SPECIAL:
	case TRACE_USER_STACK:
	case TRACE_STACK: {
		struct special_entry *field;

		trace_assign_type(field, entry);

		SEQ_PUT_FIELD_RET(s, field->arg1);
		SEQ_PUT_FIELD_RET(s, field->arg2);
		SEQ_PUT_FIELD_RET(s, field->arg3);
		break;
	}
	}
	return 1;
}

static int trace_empty(struct trace_iterator *iter)
{
	int cpu;

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

static enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	enum print_line_t ret;

	if (iter->trace && iter->trace->print_line) {
		ret = iter->trace->print_line(iter);
		if (ret != TRACE_TYPE_UNHANDLED)
			return ret;
	}

	if (iter->ent->type == TRACE_PRINT &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return print_printk_msg_only(iter);

	if (trace_flags & TRACE_ITER_BIN)
		return print_bin_fmt(iter);

	if (trace_flags & TRACE_ITER_HEX)
		return print_hex_fmt(iter);

	if (trace_flags & TRACE_ITER_RAW)
		return print_raw_fmt(iter);

	if (iter->iter_flags & TRACE_FILE_LAT_FMT)
		return print_lat_fmt(iter, iter->idx, iter->cpu);

	return print_trace_fmt(iter);
}

static int s_show(struct seq_file *m, void *v)
{
	struct trace_iterator *iter = v;

	if (iter->ent == NULL) {
		if (iter->tr) {
			seq_printf(m, "# tracer: %s\n", iter->trace->name);
			seq_puts(m, "#\n");
		}
		if (iter->trace && iter->trace->print_header)
			iter->trace->print_header(m);
		else if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
			/* print nothing if the buffers are empty */
			if (trace_empty(iter))
				return 0;
			print_trace_header(m, iter);
			if (!(trace_flags & TRACE_ITER_VERBOSE))
				print_lat_help_header(m);
		} else {
			if (!(trace_flags & TRACE_ITER_VERBOSE))
				print_func_help_header(m);
		}
	} else {
		print_trace_line(iter);
		trace_print_seq(m, &iter->seq);
	}

	return 0;
}

static struct seq_operations tracer_seq_ops = {
	.start		= s_start,
	.next		= s_next,
	.stop		= s_stop,
	.show		= s_show,
};

static struct trace_iterator *
__tracing_open(struct inode *inode, struct file *file, int *ret)
{
	struct trace_iterator *iter;
	struct seq_file *m;
	int cpu;

	if (tracing_disabled) {
		*ret = -ENODEV;
		return NULL;
	}

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		*ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&trace_types_lock);
	if (current_trace && current_trace->print_max)
		iter->tr = &max_tr;
	else
		iter->tr = inode->i_private;
	iter->trace = current_trace;
	iter->pos = -1;

	/* Notify the tracer early; before we stop tracing. */
	if (iter->trace && iter->trace->open)
		iter->trace->open(iter);

	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter->tr->buffer))
		iter->iter_flags |= TRACE_FILE_ANNOTATE;


	for_each_tracing_cpu(cpu) {

		iter->buffer_iter[cpu] =
			ring_buffer_read_start(iter->tr->buffer, cpu);

		if (!iter->buffer_iter[cpu])
			goto fail_buffer;
	}

	/* TODO stop tracer */
	*ret = seq_open(file, &tracer_seq_ops);
	if (*ret)
		goto fail_buffer;

	m = file->private_data;
	m->private = iter;

	/* stop the trace while dumping */
	tracing_stop();

	mutex_unlock(&trace_types_lock);

 out:
	return iter;

 fail_buffer:
	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}
	mutex_unlock(&trace_types_lock);
	kfree(iter);

	return ERR_PTR(-ENOMEM);
}

int tracing_open_generic(struct inode *inode, struct file *filp)
{
	if (tracing_disabled)
		return -ENODEV;

	filp->private_data = inode->i_private;
	return 0;
}

int tracing_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct trace_iterator *iter = m->private;
	int cpu;

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
	kfree(iter);
	return 0;
}

static int tracing_open(struct inode *inode, struct file *file)
{
	int ret;

	__tracing_open(inode, file, &ret);

	return ret;
}

static int tracing_lt_open(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter;
	int ret;

	iter = __tracing_open(inode, file, &ret);

	if (!ret)
		iter->iter_flags |= TRACE_FILE_LAT_FMT;

	return ret;
}


static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct tracer *t = m->private;

	(*pos)++;

	if (t)
		t = t->next;

	m->private = t;

	return t;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct tracer *t = m->private;
	loff_t l = 0;

	mutex_lock(&trace_types_lock);
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

	seq_printf(m, "%s", t->name);
	if (t->next)
		seq_putc(m, ' ');
	else
		seq_putc(m, '\n');

	return 0;
}

static struct seq_operations show_traces_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int show_traces_open(struct inode *inode, struct file *file)
{
	int ret;

	if (tracing_disabled)
		return -ENODEV;

	ret = seq_open(file, &show_traces_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = trace_types;
	}

	return ret;
}

static struct file_operations tracing_fops = {
	.open		= tracing_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_release,
};

static struct file_operations tracing_lt_fops = {
	.open		= tracing_lt_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_release,
};

static struct file_operations show_traces_fops = {
	.open		= show_traces_open,
	.read		= seq_read,
	.release	= seq_release,
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

	mutex_lock(&tracing_cpumask_update_lock);
	err = cpumask_parse_user(ubuf, count, tracing_cpumask_new);
	if (err)
		goto err_unlock;

	local_irq_disable();
	__raw_spin_lock(&ftrace_max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpumask_test_cpu(cpu, tracing_cpumask) &&
				!cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_inc(&global_trace.data[cpu]->disabled);
		}
		if (!cpumask_test_cpu(cpu, tracing_cpumask) &&
				cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_dec(&global_trace.data[cpu]->disabled);
		}
	}
	__raw_spin_unlock(&ftrace_max_lock);
	local_irq_enable();

	cpumask_copy(tracing_cpumask, tracing_cpumask_new);

	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask_new);

	return count;

err_unlock:
	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask);

	return err;
}

static struct file_operations tracing_cpumask_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_cpumask_read,
	.write		= tracing_cpumask_write,
};

static ssize_t
tracing_trace_options_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	int i;
	char *buf;
	int r = 0;
	int len = 0;
	u32 tracer_flags = current_trace->flags->val;
	struct tracer_opt *trace_opts = current_trace->flags->opts;


	/* calulate max size */
	for (i = 0; trace_options[i]; i++) {
		len += strlen(trace_options[i]);
		len += 3; /* "no" and space */
	}

	/*
	 * Increase the size with names of options specific
	 * of the current tracer.
	 */
	for (i = 0; trace_opts[i].name; i++) {
		len += strlen(trace_opts[i].name);
		len += 3; /* "no" and space */
	}

	/* +2 for \n and \0 */
	buf = kmalloc(len + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; trace_options[i]; i++) {
		if (trace_flags & (1 << i))
			r += sprintf(buf + r, "%s ", trace_options[i]);
		else
			r += sprintf(buf + r, "no%s ", trace_options[i]);
	}

	for (i = 0; trace_opts[i].name; i++) {
		if (tracer_flags & trace_opts[i].bit)
			r += sprintf(buf + r, "%s ",
				trace_opts[i].name);
		else
			r += sprintf(buf + r, "no%s ",
				trace_opts[i].name);
	}

	r += sprintf(buf + r, "\n");
	WARN_ON(r >= len + 2);

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);

	kfree(buf);

	return r;
}

/* Try to assign a tracer specific option */
static int set_tracer_option(struct tracer *trace, char *cmp, int neg)
{
	struct tracer_flags *trace_flags = trace->flags;
	struct tracer_opt *opts = NULL;
	int ret = 0, i = 0;
	int len;

	for (i = 0; trace_flags->opts[i].name; i++) {
		opts = &trace_flags->opts[i];
		len = strlen(opts->name);

		if (strncmp(cmp, opts->name, len) == 0) {
			ret = trace->set_flag(trace_flags->val,
				opts->bit, !neg);
			break;
		}
	}
	/* Not found */
	if (!trace_flags->opts[i].name)
		return -EINVAL;

	/* Refused to handle */
	if (ret)
		return ret;

	if (neg)
		trace_flags->val &= ~opts->bit;
	else
		trace_flags->val |= opts->bit;

	return 0;
}

static ssize_t
tracing_trace_options_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[64];
	char *cmp = buf;
	int neg = 0;
	int ret;
	int i;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	if (strncmp(buf, "no", 2) == 0) {
		neg = 1;
		cmp += 2;
	}

	for (i = 0; trace_options[i]; i++) {
		int len = strlen(trace_options[i]);

		if (strncmp(cmp, trace_options[i], len) == 0) {
			if (neg)
				trace_flags &= ~(1 << i);
			else
				trace_flags |= (1 << i);
			break;
		}
	}

	/* If no option could be set, test the specific tracer options */
	if (!trace_options[i]) {
		ret = set_tracer_option(current_trace, cmp, neg);
		if (ret)
			return ret;
	}

	filp->f_pos += cnt;

	return cnt;
}

static struct file_operations tracing_iter_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_trace_options_read,
	.write		= tracing_trace_options_write,
};

static const char readme_msg[] =
	"tracing mini-HOWTO:\n\n"
	"# mkdir /debug\n"
	"# mount -t debugfs nodev /debug\n\n"
	"# cat /debug/tracing/available_tracers\n"
	"wakeup preemptirqsoff preemptoff irqsoff ftrace sched_switch none\n\n"
	"# cat /debug/tracing/current_tracer\n"
	"none\n"
	"# echo sched_switch > /debug/tracing/current_tracer\n"
	"# cat /debug/tracing/current_tracer\n"
	"sched_switch\n"
	"# cat /debug/tracing/trace_options\n"
	"noprint-parent nosym-offset nosym-addr noverbose\n"
	"# echo print-parent > /debug/tracing/trace_options\n"
	"# echo 1 > /debug/tracing/tracing_enabled\n"
	"# cat /debug/tracing/trace > /tmp/trace.txt\n"
	"echo 0 > /debug/tracing/tracing_enabled\n"
;

static ssize_t
tracing_readme_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, cnt, ppos,
					readme_msg, strlen(readme_msg));
}

static struct file_operations tracing_readme_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_readme_read,
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
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = !!val;

	mutex_lock(&trace_types_lock);
	if (tracer_enabled ^ val) {
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

	filp->f_pos += cnt;

	return cnt;
}

static ssize_t
tracing_set_trace_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	char buf[max_tracer_type_len+2];
	int r;

	mutex_lock(&trace_types_lock);
	if (current_trace)
		r = sprintf(buf, "%s\n", current_trace->name);
	else
		r = sprintf(buf, "\n");
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static int tracing_set_tracer(char *buf)
{
	struct trace_array *tr = &global_trace;
	struct tracer *t;
	int ret = 0;

	mutex_lock(&trace_types_lock);
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
	if (current_trace && current_trace->reset)
		current_trace->reset(tr);

	current_trace = t;
	if (t->init) {
		ret = t->init(tr);
		if (ret)
			goto out;
	}

	trace_branch_enable(tr);
 out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

static ssize_t
tracing_set_trace_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[max_tracer_type_len+1];
	int i;
	size_t ret;
	int err;

	ret = cnt;

	if (cnt > max_tracer_type_len)
		cnt = max_tracer_type_len;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	err = tracing_set_tracer(buf);
	if (err)
		return err;

	filp->f_pos += ret;

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
	long *ptr = filp->private_data;
	char buf[64];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	*ptr = val * 1000;

	return cnt;
}

static atomic_t tracing_reader;

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	struct trace_iterator *iter;

	if (tracing_disabled)
		return -ENODEV;

	/* We only allow for reader of the pipe */
	if (atomic_inc_return(&tracing_reader) != 1) {
		atomic_dec(&tracing_reader);
		return -EBUSY;
	}

	/* create a buffer to store the information to pass to userspace */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	if (!alloc_cpumask_var(&iter->started, GFP_KERNEL)) {
		kfree(iter);
		return -ENOMEM;
	}

	mutex_lock(&trace_types_lock);

	/* trace pipe does not show start of buffer */
	cpumask_setall(iter->started);

	iter->tr = &global_trace;
	iter->trace = current_trace;
	filp->private_data = iter;

	if (iter->trace->pipe_open)
		iter->trace->pipe_open(iter);
	mutex_unlock(&trace_types_lock);

	return 0;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;

	free_cpumask_var(iter->started);
	kfree(iter);
	atomic_dec(&tracing_reader);

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

/*
 * Consumer reader.
 */
static ssize_t
tracing_read_pipe(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;

	trace_seq_reset(&iter->seq);

	mutex_lock(&trace_types_lock);
	if (iter->trace->read) {
		sret = iter->trace->read(iter, filp, ubuf, cnt, ppos);
		if (sret)
			goto out;
	}

waitagain:
	sret = 0;
	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			sret = -EAGAIN;
			goto out;
		}

		/*
		 * This is a make-shift waitqueue. The reason we don't use
		 * an actual wait queue is because:
		 *  1) we only ever have one waiter
		 *  2) the tracing, traces all functions, we don't want
		 *     the overhead of calling wake_up and friends
		 *     (and tracing them too)
		 *     Anyway, this is really very primitive wakeup.
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		iter->tr->waiter = current;

		mutex_unlock(&trace_types_lock);

		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ/10);

		mutex_lock(&trace_types_lock);

		iter->tr->waiter = NULL;

		if (signal_pending(current)) {
			sret = -EINTR;
			goto out;
		}

		if (iter->trace != current_trace)
			goto out;

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

		continue;
	}

	/* stop when tracing is finished */
	if (trace_empty(iter))
		goto out;

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	iter->pos = -1;

	while (find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}

		trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;
	}

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_reset(&iter->seq);

	/*
	 * If there was nothing to send to user, inspite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

out:
	mutex_unlock(&trace_types_lock);

	return sret;
}

static ssize_t
tracing_entries_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[64];
	int r;

	r = sprintf(buf, "%lu\n", tr->entries >> 10);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_entries_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	unsigned long val;
	char buf[64];
	int ret, cpu;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	/* must have at least 1 entry */
	if (!val)
		return -EINVAL;

	mutex_lock(&trace_types_lock);

	tracing_stop();

	/* disable all cpu buffers */
	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_inc(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_inc(&max_tr.data[cpu]->disabled);
	}

	/* value is in KB */
	val <<= 10;

	if (val != global_trace.entries) {
		ret = ring_buffer_resize(global_trace.buffer, val);
		if (ret < 0) {
			cnt = ret;
			goto out;
		}

		ret = ring_buffer_resize(max_tr.buffer, val);
		if (ret < 0) {
			int r;
			cnt = ret;
			r = ring_buffer_resize(global_trace.buffer,
					       global_trace.entries);
			if (r < 0) {
				/* AARGH! We are left with different
				 * size max buffer!!!! */
				WARN_ON(1);
				tracing_disabled = 1;
			}
			goto out;
		}

		global_trace.entries = val;
	}

	filp->f_pos += cnt;

	/* If check pages failed, return ENOMEM */
	if (tracing_disabled)
		cnt = -ENOMEM;
 out:
	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_dec(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_dec(&max_tr.data[cpu]->disabled);
	}

	tracing_start();
	max_tr.entries = global_trace.entries;
	mutex_unlock(&trace_types_lock);

	return cnt;
}

static int mark_printk(const char *fmt, ...)
{
	int ret;
	va_list args;
	va_start(args, fmt);
	ret = trace_vprintk(0, -1, fmt, args);
	va_end(args);
	return ret;
}

static ssize_t
tracing_mark_write(struct file *filp, const char __user *ubuf,
					size_t cnt, loff_t *fpos)
{
	char *buf;
	char *end;

	if (tracing_disabled)
		return -EINVAL;

	if (cnt > TRACE_BUF_SIZE)
		cnt = TRACE_BUF_SIZE;

	buf = kmalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		kfree(buf);
		return -EFAULT;
	}

	/* Cut from the first nil or newline. */
	buf[cnt] = '\0';
	end = strchr(buf, '\n');
	if (end)
		*end = '\0';

	cnt = mark_printk("%s\n", buf);
	kfree(buf);
	*fpos += cnt;

	return cnt;
}

static struct file_operations tracing_max_lat_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_max_lat_read,
	.write		= tracing_max_lat_write,
};

static struct file_operations tracing_ctrl_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_ctrl_read,
	.write		= tracing_ctrl_write,
};

static struct file_operations set_tracer_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_set_trace_read,
	.write		= tracing_set_trace_write,
};

static struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.poll		= tracing_poll_pipe,
	.read		= tracing_read_pipe,
	.release	= tracing_release_pipe,
};

static struct file_operations tracing_entries_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_entries_read,
	.write		= tracing_entries_write,
};

static struct file_operations tracing_mark_fops = {
	.open		= tracing_open_generic,
	.write		= tracing_mark_write,
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

static struct file_operations tracing_dyn_info_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_read_dyn_info,
};
#endif

static struct dentry *d_tracer;

struct dentry *tracing_init_dentry(void)
{
	static int once;

	if (d_tracer)
		return d_tracer;

	d_tracer = debugfs_create_dir("tracing", NULL);

	if (!d_tracer && !once) {
		once = 1;
		pr_warning("Could not create debugfs directory 'tracing'\n");
		return NULL;
	}

	return d_tracer;
}

#ifdef CONFIG_FTRACE_SELFTEST
/* Let selftest have access to static functions in this file */
#include "trace_selftest.c"
#endif

static __init int tracer_init_debugfs(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("tracing_enabled", 0644, d_tracer,
				    &global_trace, &tracing_ctrl_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'tracing_enabled' entry\n");

	entry = debugfs_create_file("trace_options", 0644, d_tracer,
				    NULL, &tracing_iter_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace_options' entry\n");

	entry = debugfs_create_file("tracing_cpumask", 0644, d_tracer,
				    NULL, &tracing_cpumask_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'tracing_cpumask' entry\n");

	entry = debugfs_create_file("latency_trace", 0444, d_tracer,
				    &global_trace, &tracing_lt_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'latency_trace' entry\n");

	entry = debugfs_create_file("trace", 0444, d_tracer,
				    &global_trace, &tracing_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace' entry\n");

	entry = debugfs_create_file("available_tracers", 0444, d_tracer,
				    &global_trace, &show_traces_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'available_tracers' entry\n");

	entry = debugfs_create_file("current_tracer", 0444, d_tracer,
				    &global_trace, &set_tracer_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'current_tracer' entry\n");

	entry = debugfs_create_file("tracing_max_latency", 0644, d_tracer,
				    &tracing_max_latency,
				    &tracing_max_lat_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_max_latency' entry\n");

	entry = debugfs_create_file("tracing_thresh", 0644, d_tracer,
				    &tracing_thresh, &tracing_max_lat_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_thresh' entry\n");
	entry = debugfs_create_file("README", 0644, d_tracer,
				    NULL, &tracing_readme_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'README' entry\n");

	entry = debugfs_create_file("trace_pipe", 0644, d_tracer,
				    NULL, &tracing_pipe_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'trace_pipe' entry\n");

	entry = debugfs_create_file("buffer_size_kb", 0644, d_tracer,
				    &global_trace, &tracing_entries_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'buffer_size_kb' entry\n");

	entry = debugfs_create_file("trace_marker", 0220, d_tracer,
				    NULL, &tracing_mark_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'trace_marker' entry\n");

#ifdef CONFIG_DYNAMIC_FTRACE
	entry = debugfs_create_file("dyn_ftrace_total_info", 0444, d_tracer,
				    &ftrace_update_tot_cnt,
				    &tracing_dyn_info_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'dyn_ftrace_total_info' entry\n");
#endif
#ifdef CONFIG_SYSPROF_TRACER
	init_tracer_sysprof_debugfs(d_tracer);
#endif
	return 0;
}

int trace_vprintk(unsigned long ip, int depth, const char *fmt, va_list args)
{
	static DEFINE_SPINLOCK(trace_buf_lock);
	static char trace_buf[TRACE_BUF_SIZE];

	struct ring_buffer_event *event;
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	int cpu, len = 0, size, pc;
	struct print_entry *entry;
	unsigned long irq_flags;

	if (tracing_disabled || tracing_selftest_running)
		return 0;

	pc = preempt_count();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (unlikely(atomic_read(&data->disabled)))
		goto out;

	pause_graph_tracing();
	spin_lock_irqsave(&trace_buf_lock, irq_flags);
	len = vsnprintf(trace_buf, TRACE_BUF_SIZE, fmt, args);

	len = min(len, TRACE_BUF_SIZE-1);
	trace_buf[len] = 0;

	size = sizeof(*entry) + len + 1;
	event = ring_buffer_lock_reserve(tr->buffer, size, &irq_flags);
	if (!event)
		goto out_unlock;
	entry = ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, irq_flags, pc);
	entry->ent.type			= TRACE_PRINT;
	entry->ip			= ip;
	entry->depth			= depth;

	memcpy(&entry->buf, trace_buf, len);
	entry->buf[len] = 0;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);

 out_unlock:
	spin_unlock_irqrestore(&trace_buf_lock, irq_flags);
	unpause_graph_tracing();
 out:
	preempt_enable_notrace();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vprintk);

int __ftrace_printk(unsigned long ip, const char *fmt, ...)
{
	int ret;
	va_list ap;

	if (!(trace_flags & TRACE_ITER_PRINTK))
		return 0;

	va_start(ap, fmt);
	ret = trace_vprintk(ip, task_curr_ret_stack(current), fmt, ap);
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(__ftrace_printk);

static int trace_panic_handler(struct notifier_block *this,
			       unsigned long event, void *unused)
{
	if (ftrace_dump_on_oops)
		ftrace_dump();
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
			ftrace_dump();
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

static void
trace_printk_seq(struct trace_seq *s)
{
	/* Probably should print a warning here. */
	if (s->len >= 1000)
		s->len = 1000;

	/* should be zero ended, but we are paranoid. */
	s->buffer[s->len] = 0;

	printk(KERN_TRACE "%s", s->buffer);

	trace_seq_reset(s);
}

void ftrace_dump(void)
{
	static DEFINE_SPINLOCK(ftrace_dump_lock);
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	static int dump_ran;
	unsigned long flags;
	int cnt = 0, cpu;

	/* only one dump */
	spin_lock_irqsave(&ftrace_dump_lock, flags);
	if (dump_ran)
		goto out;

	dump_ran = 1;

	/* No turning back! */
	tracing_off();
	ftrace_kill();

	for_each_tracing_cpu(cpu) {
		atomic_inc(&global_trace.data[cpu]->disabled);
	}

	/* don't look at user memory in panic mode */
	trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	printk(KERN_TRACE "Dumping ftrace buffer:\n");

	iter.tr = &global_trace;
	iter.trace = current_trace;

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

		if (find_next_entry_inc(&iter) != NULL) {
			print_trace_line(&iter);
			trace_consume(&iter);
		}

		trace_printk_seq(&iter.seq);
	}

	if (!cnt)
		printk(KERN_TRACE "   (ftrace buffer empty)\n");
	else
		printk(KERN_TRACE "---------------------------------\n");

 out:
	spin_unlock_irqrestore(&ftrace_dump_lock, flags);
}

__init static int tracer_alloc_buffers(void)
{
	struct trace_array_cpu *data;
	int i;
	int ret = -ENOMEM;

	if (!alloc_cpumask_var(&tracing_buffer_mask, GFP_KERNEL))
		goto out;

	if (!alloc_cpumask_var(&tracing_cpumask, GFP_KERNEL))
		goto out_free_buffer_mask;

	cpumask_copy(tracing_buffer_mask, cpu_possible_mask);
	cpumask_copy(tracing_cpumask, cpu_all_mask);

	/* TODO: make the number of buffers hot pluggable with CPUS */
	global_trace.buffer = ring_buffer_alloc(trace_buf_size,
						   TRACE_BUFFER_FLAGS);
	if (!global_trace.buffer) {
		printk(KERN_ERR "tracer: failed to allocate ring buffer!\n");
		WARN_ON(1);
		goto out_free_cpumask;
	}
	global_trace.entries = ring_buffer_size(global_trace.buffer);


#ifdef CONFIG_TRACER_MAX_TRACE
	max_tr.buffer = ring_buffer_alloc(trace_buf_size,
					     TRACE_BUFFER_FLAGS);
	if (!max_tr.buffer) {
		printk(KERN_ERR "tracer: failed to allocate max ring buffer!\n");
		WARN_ON(1);
		ring_buffer_free(global_trace.buffer);
		goto out_free_cpumask;
	}
	max_tr.entries = ring_buffer_size(max_tr.buffer);
	WARN_ON(max_tr.entries != global_trace.entries);
#endif

	/* Allocate the first page for all buffers */
	for_each_tracing_cpu(i) {
		data = global_trace.data[i] = &per_cpu(global_trace_cpu, i);
		max_tr.data[i] = &per_cpu(max_data, i);
	}

	trace_init_cmdlines();

	register_tracer(&nop_trace);
#ifdef CONFIG_BOOT_TRACER
	register_tracer(&boot_tracer);
	current_trace = &boot_tracer;
	current_trace->init(&global_trace);
#else
	current_trace = &nop_trace;
#endif

	/* All seems OK, enable tracing */
	tracing_disabled = 0;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &trace_panic_notifier);

	register_die_notifier(&trace_die_notifier);
	ret = 0;

out_free_cpumask:
	free_cpumask_var(tracing_cpumask);
out_free_buffer_mask:
	free_cpumask_var(tracing_buffer_mask);
out:
	return ret;
}
early_initcall(tracer_alloc_buffers);
fs_initcall(tracer_init_debugfs);
