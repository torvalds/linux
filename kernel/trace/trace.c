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
#include <linux/debugfs.h>
#include <linux/pagemap.h>
#include <linux/hardirq.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/writeback.h>

#include <linux/stacktrace.h>

#include "trace.h"

unsigned long __read_mostly	tracing_max_latency = (cycle_t)ULONG_MAX;
unsigned long __read_mostly	tracing_thresh;

static unsigned long __read_mostly	tracing_nr_buffers;
static cpumask_t __read_mostly		tracing_buffer_mask;

#define for_each_tracing_cpu(cpu)	\
	for_each_cpu_mask(cpu, tracing_buffer_mask)

static int trace_alloc_page(void);
static int trace_free_page(void);

static int tracing_disabled = 1;

static unsigned long tracing_pages_allocated;

long
ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

cycle_t ftrace_now(int cpu)
{
	return cpu_clock(cpu);
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

/* function tracing enabled */
int				ftrace_function_enabled;

/*
 * trace_nr_entries is the number of entries that is allocated
 * for a buffer. Note, the number of entries is always rounded
 * to ENTRIES_PER_PAGE.
 */
static unsigned long		trace_nr_entries = 65536UL;

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

/* trace_flags holds iter_ctrl options */
unsigned long trace_flags = TRACE_ITER_PRINT_PARENT;

static notrace void no_trace_init(struct trace_array *tr)
{
	int cpu;

	ftrace_function_enabled = 0;
	if(tr->ctrl)
		for_each_online_cpu(cpu)
			tracing_reset(tr->data[cpu]);
	tracer_enabled = 0;
}

/* dummy trace to disable tracing */
static struct tracer no_tracer __read_mostly = {
	.name		= "none",
	.init		= no_trace_init
};


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

#define ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct trace_entry))

static int __init set_nr_entries(char *str)
{
	unsigned long nr_entries;
	int ret;

	if (!str)
		return 0;
	ret = strict_strtoul(str, 0, &nr_entries);
	/* nr_entries can not be zero */
	if (ret < 0 || nr_entries == 0)
		return 0;
	trace_nr_entries = nr_entries;
	return 1;
}
__setup("trace_entries=", set_nr_entries);

unsigned long nsecs_to_usecs(unsigned long nsecs)
{
	return nsecs / 1000;
}

/*
 * trace_flag_type is an enumeration that holds different
 * states when a trace occurs. These are:
 *  IRQS_OFF	- interrupts were disabled
 *  NEED_RESCED - reschedule is requested
 *  HARDIRQ	- inside an interrupt handler
 *  SOFTIRQ	- inside a softirq handler
 */
enum trace_flag_type {
	TRACE_FLAG_IRQS_OFF		= 0x01,
	TRACE_FLAG_NEED_RESCHED		= 0x02,
	TRACE_FLAG_HARDIRQ		= 0x04,
	TRACE_FLAG_SOFTIRQ		= 0x08,
};

/*
 * TRACE_ITER_SYM_MASK masks the options in trace_flags that
 * control the output of kernel symbols.
 */
#define TRACE_ITER_SYM_MASK \
	(TRACE_ITER_PRINT_PARENT|TRACE_ITER_SYM_OFFSET|TRACE_ITER_SYM_ADDR)

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
	data->uid = tsk->uid;
	data->nice = tsk->static_prio - 20 - MAX_RT_PRIO;
	data->policy = tsk->policy;
	data->rt_priority = tsk->rt_priority;

	/* record this tasks comm */
	tracing_record_cmdline(current);
}

#define CHECK_COND(cond)			\
	if (unlikely(cond)) {			\
		tracing_disabled = 1;		\
		WARN_ON(1);			\
		return -1;			\
	}

/**
 * check_pages - integrity check of trace buffers
 *
 * As a safty measure we check to make sure the data pages have not
 * been corrupted.
 */
int check_pages(struct trace_array_cpu *data)
{
	struct page *page, *tmp;

	CHECK_COND(data->trace_pages.next->prev != &data->trace_pages);
	CHECK_COND(data->trace_pages.prev->next != &data->trace_pages);

	list_for_each_entry_safe(page, tmp, &data->trace_pages, lru) {
		CHECK_COND(page->lru.next->prev != &page->lru);
		CHECK_COND(page->lru.prev->next != &page->lru);
	}

	return 0;
}

/**
 * head_page - page address of the first page in per_cpu buffer.
 *
 * head_page returns the page address of the first page in
 * a per_cpu buffer. This also preforms various consistency
 * checks to make sure the buffer has not been corrupted.
 */
void *head_page(struct trace_array_cpu *data)
{
	struct page *page;

	if (list_empty(&data->trace_pages))
		return NULL;

	page = list_entry(data->trace_pages.next, struct page, lru);
	BUG_ON(&page->lru == &data->trace_pages);

	return page_address(page);
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

#define HEX_CHARS 17
static const char hex2asc[] = "0123456789abcdef";

static int
trace_seq_putmem_hex(struct trace_seq *s, void *mem, size_t len)
{
	unsigned char hex[HEX_CHARS];
	unsigned char *data = mem;
	unsigned char byte;
	int i, j;

	BUG_ON(len >= HEX_CHARS);

#ifdef __BIG_ENDIAN
	for (i = 0, j = 0; i < len; i++) {
#else
	for (i = len-1, j = 0; i >= 0; i--) {
#endif
		byte = data[i];

		hex[j++] = hex2asc[byte & 0x0f];
		hex[j++] = hex2asc[byte >> 4];
	}
	hex[j++] = ' ';

	return trace_seq_putmem(s, hex, j);
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

/*
 * flip the trace buffers between two trace descriptors.
 * This usually is the buffers between the global_trace and
 * the max_tr to record a snapshot of a current trace.
 *
 * The ftrace_max_lock must be held.
 */
static void
flip_trace(struct trace_array_cpu *tr1, struct trace_array_cpu *tr2)
{
	struct list_head flip_pages;

	INIT_LIST_HEAD(&flip_pages);

	memcpy(&tr1->trace_head_idx, &tr2->trace_head_idx,
		sizeof(struct trace_array_cpu) -
		offsetof(struct trace_array_cpu, trace_head_idx));

	check_pages(tr1);
	check_pages(tr2);
	list_splice_init(&tr1->trace_pages, &flip_pages);
	list_splice_init(&tr2->trace_pages, &tr1->trace_pages);
	list_splice_init(&flip_pages, &tr2->trace_pages);
	BUG_ON(!list_empty(&flip_pages));
	check_pages(tr1);
	check_pages(tr2);
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
	struct trace_array_cpu *data;
	int i;

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);
	/* clear out all the previous traces */
	for_each_tracing_cpu(i) {
		data = tr->data[i];
		flip_trace(max_tr.data[i], data);
		tracing_reset(data);
	}

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
	struct trace_array_cpu *data = tr->data[cpu];
	int i;

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);
	for_each_tracing_cpu(i)
		tracing_reset(max_tr.data[i]);

	flip_trace(max_tr.data[cpu], data);
	tracing_reset(data);

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

	mutex_lock(&trace_types_lock);
	for (t = trace_types; t; t = t->next) {
		if (strcmp(type->name, t->name) == 0) {
			/* already found */
			pr_info("Trace %s already registered\n",
				type->name);
			ret = -1;
			goto out;
		}
	}

#ifdef CONFIG_FTRACE_STARTUP_TEST
	if (type->selftest) {
		struct tracer *saved_tracer = current_trace;
		struct trace_array_cpu *data;
		struct trace_array *tr = &global_trace;
		int saved_ctrl = tr->ctrl;
		int i;
		/*
		 * Run a selftest on this tracer.
		 * Here we reset the trace buffer, and set the current
		 * tracer to be this tracer. The tracer can then run some
		 * internal tracing to verify that everything is in order.
		 * If we fail, we do not register this tracer.
		 */
		for_each_tracing_cpu(i) {
			data = tr->data[i];
			if (!head_page(data))
				continue;
			tracing_reset(data);
		}
		current_trace = type;
		tr->ctrl = 0;
		/* the test is responsible for initializing and enabling */
		pr_info("Testing tracer %s: ", type->name);
		ret = type->selftest(type, tr);
		/* the test is responsible for resetting too */
		current_trace = saved_tracer;
		tr->ctrl = saved_ctrl;
		if (ret) {
			printk(KERN_CONT "FAILED!\n");
			goto out;
		}
		/* Only reset on passing, to avoid touching corrupted buffers */
		for_each_tracing_cpu(i) {
			data = tr->data[i];
			if (!head_page(data))
				continue;
			tracing_reset(data);
		}
		printk(KERN_CONT "PASSED\n");
	}
#endif

	type->next = trace_types;
	trace_types = type;
	len = strlen(type->name);
	if (len > max_tracer_type_len)
		max_tracer_type_len = len;

 out:
	mutex_unlock(&trace_types_lock);

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

void tracing_reset(struct trace_array_cpu *data)
{
	data->trace_idx = 0;
	data->overrun = 0;
	data->trace_head = data->trace_tail = head_page(data);
	data->trace_head_idx = 0;
	data->trace_tail_idx = 0;
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

static char *trace_find_cmdline(int pid)
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

static inline struct list_head *
trace_next_list(struct trace_array_cpu *data, struct list_head *next)
{
	/*
	 * Roundrobin - but skip the head (which is not a real page):
	 */
	next = next->next;
	if (unlikely(next == &data->trace_pages))
		next = next->next;
	BUG_ON(next == &data->trace_pages);

	return next;
}

static inline void *
trace_next_page(struct trace_array_cpu *data, void *addr)
{
	struct list_head *next;
	struct page *page;

	page = virt_to_page(addr);

	next = trace_next_list(data, &page->lru);
	page = list_entry(next, struct page, lru);

	return page_address(page);
}

static inline struct trace_entry *
tracing_get_trace_entry(struct trace_array *tr, struct trace_array_cpu *data)
{
	unsigned long idx, idx_next;
	struct trace_entry *entry;

	data->trace_idx++;
	idx = data->trace_head_idx;
	idx_next = idx + 1;

	BUG_ON(idx * TRACE_ENTRY_SIZE >= PAGE_SIZE);

	entry = data->trace_head + idx * TRACE_ENTRY_SIZE;

	if (unlikely(idx_next >= ENTRIES_PER_PAGE)) {
		data->trace_head = trace_next_page(data, data->trace_head);
		idx_next = 0;
	}

	if (data->trace_head == data->trace_tail &&
	    idx_next == data->trace_tail_idx) {
		/* overrun */
		data->overrun++;
		data->trace_tail_idx++;
		if (data->trace_tail_idx >= ENTRIES_PER_PAGE) {
			data->trace_tail =
				trace_next_page(data, data->trace_tail);
			data->trace_tail_idx = 0;
		}
	}

	data->trace_head_idx = idx_next;

	return entry;
}

static inline void
tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags)
{
	struct task_struct *tsk = current;
	unsigned long pc;

	pc = preempt_count();

	entry->preempt_count	= pc & 0xff;
	entry->pid		= (tsk) ? tsk->pid : 0;
	entry->t		= ftrace_now(raw_smp_processor_id());
	entry->flags = (irqs_disabled_flags(flags) ? TRACE_FLAG_IRQS_OFF : 0) |
		((pc & HARDIRQ_MASK) ? TRACE_FLAG_HARDIRQ : 0) |
		((pc & SOFTIRQ_MASK) ? TRACE_FLAG_SOFTIRQ : 0) |
		(need_resched() ? TRACE_FLAG_NEED_RESCHED : 0);
}

void
trace_function(struct trace_array *tr, struct trace_array_cpu *data,
	       unsigned long ip, unsigned long parent_ip, unsigned long flags)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_FN;
	entry->fn.ip		= ip;
	entry->fn.parent_ip	= parent_ip;
	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);
}

void
ftrace(struct trace_array *tr, struct trace_array_cpu *data,
       unsigned long ip, unsigned long parent_ip, unsigned long flags)
{
	if (likely(!atomic_read(&data->disabled)))
		trace_function(tr, data, ip, parent_ip, flags);
}

#ifdef CONFIG_MMIOTRACE
void __trace_mmiotrace_rw(struct trace_array *tr, struct trace_array_cpu *data,
						struct mmiotrace_rw *rw)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);

	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, 0);
	entry->type		= TRACE_MMIO_RW;
	entry->mmiorw		= *rw;

	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);

	trace_wake_up();
}

void __trace_mmiotrace_map(struct trace_array *tr, struct trace_array_cpu *data,
						struct mmiotrace_map *map)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);

	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, 0);
	entry->type		= TRACE_MMIO_MAP;
	entry->mmiomap		= *map;

	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);

	trace_wake_up();
}
#endif

void __trace_stack(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long flags,
		   int skip)
{
	struct trace_entry *entry;
	struct stack_trace trace;

	if (!(trace_flags & TRACE_ITER_STACKTRACE))
		return;

	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_STACK;

	memset(&entry->stack, 0, sizeof(entry->stack));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= skip;
	trace.entries		= entry->stack.caller;

	save_stack_trace(&trace);
}

void
__trace_special(void *__tr, void *__data,
		unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_array_cpu *data = __data;
	struct trace_array *tr = __tr;
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, 0);
	entry->type		= TRACE_SPECIAL;
	entry->special.arg1	= arg1;
	entry->special.arg2	= arg2;
	entry->special.arg3	= arg3;
	__trace_stack(tr, data, irq_flags, 4);
	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);

	trace_wake_up();
}

void
tracing_sched_switch_trace(struct trace_array *tr,
			   struct trace_array_cpu *data,
			   struct task_struct *prev,
			   struct task_struct *next,
			   unsigned long flags)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_CTX;
	entry->ctx.prev_pid	= prev->pid;
	entry->ctx.prev_prio	= prev->prio;
	entry->ctx.prev_state	= prev->state;
	entry->ctx.next_pid	= next->pid;
	entry->ctx.next_prio	= next->prio;
	entry->ctx.next_state	= next->state;
	__trace_stack(tr, data, flags, 5);
	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);
}

void
tracing_sched_wakeup_trace(struct trace_array *tr,
			   struct trace_array_cpu *data,
			   struct task_struct *wakee,
			   struct task_struct *curr,
			   unsigned long flags)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_WAKE;
	entry->ctx.prev_pid	= curr->pid;
	entry->ctx.prev_prio	= curr->prio;
	entry->ctx.prev_state	= curr->state;
	entry->ctx.next_pid	= wakee->pid;
	entry->ctx.next_prio	= wakee->prio;
	entry->ctx.next_state	= wakee->state;
	__trace_stack(tr, data, flags, 6);
	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);

	trace_wake_up();
}

void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (tracing_disabled || current_trace == &no_tracer || !tr->ctrl)
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		__trace_special(tr, data, arg1, arg2, arg3);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#ifdef CONFIG_FTRACE
static void
function_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (unlikely(!ftrace_function_enabled))
		return;

	if (skip_trace(ip))
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		trace_function(tr, data, ip, parent_ip, flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = function_trace_call,
};

void tracing_start_function_trace(void)
{
	ftrace_function_enabled = 0;
	register_ftrace_function(&trace_ops);
	if (tracer_enabled)
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
};

static struct trace_entry *
trace_entry_idx(struct trace_array *tr, struct trace_array_cpu *data,
		struct trace_iterator *iter, int cpu)
{
	struct page *page;
	struct trace_entry *array;

	if (iter->next_idx[cpu] >= tr->entries ||
	    iter->next_idx[cpu] >= data->trace_idx ||
	    (data->trace_head == data->trace_tail &&
	     data->trace_head_idx == data->trace_tail_idx))
		return NULL;

	if (!iter->next_page[cpu]) {
		/* Initialize the iterator for this cpu trace buffer */
		WARN_ON(!data->trace_tail);
		page = virt_to_page(data->trace_tail);
		iter->next_page[cpu] = &page->lru;
		iter->next_page_idx[cpu] = data->trace_tail_idx;
	}

	page = list_entry(iter->next_page[cpu], struct page, lru);
	BUG_ON(&data->trace_pages == &page->lru);

	array = page_address(page);

	WARN_ON(iter->next_page_idx[cpu] >= ENTRIES_PER_PAGE);
	return &array[iter->next_page_idx[cpu]];
}

static struct trace_entry *
find_next_entry(struct trace_iterator *iter, int *ent_cpu)
{
	struct trace_array *tr = iter->tr;
	struct trace_entry *ent, *next = NULL;
	int next_cpu = -1;
	int cpu;

	for_each_tracing_cpu(cpu) {
		if (!head_page(tr->data[cpu]))
			continue;
		ent = trace_entry_idx(tr, tr->data[cpu], iter, cpu);
		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ent->t < next->t)) {
			next = ent;
			next_cpu = cpu;
		}
	}

	if (ent_cpu)
		*ent_cpu = next_cpu;

	return next;
}

static void trace_iterator_increment(struct trace_iterator *iter)
{
	iter->idx++;
	iter->next_idx[iter->cpu]++;
	iter->next_page_idx[iter->cpu]++;

	if (iter->next_page_idx[iter->cpu] >= ENTRIES_PER_PAGE) {
		struct trace_array_cpu *data = iter->tr->data[iter->cpu];

		iter->next_page_idx[iter->cpu] = 0;
		iter->next_page[iter->cpu] =
			trace_next_list(data, iter->next_page[iter->cpu]);
	}
}

static void trace_consume(struct trace_iterator *iter)
{
	struct trace_array_cpu *data = iter->tr->data[iter->cpu];

	data->trace_tail_idx++;
	if (data->trace_tail_idx >= ENTRIES_PER_PAGE) {
		data->trace_tail = trace_next_page(data, data->trace_tail);
		data->trace_tail_idx = 0;
	}

	/* Check if we empty it, then reset the index */
	if (data->trace_head == data->trace_tail &&
	    data->trace_head_idx == data->trace_tail_idx)
		data->trace_idx = 0;
}

static void *find_next_entry_inc(struct trace_iterator *iter)
{
	struct trace_entry *next;
	int next_cpu = -1;

	next = find_next_entry(iter, &next_cpu);

	iter->prev_ent = iter->ent;
	iter->prev_cpu = iter->cpu;

	iter->ent = next;
	iter->cpu = next_cpu;

	if (next)
		trace_iterator_increment(iter);

	return next ? iter : NULL;
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
	int i;

	mutex_lock(&trace_types_lock);

	if (!current_trace || current_trace != iter->trace) {
		mutex_unlock(&trace_types_lock);
		return NULL;
	}

	atomic_inc(&trace_record_cmdline_disabled);

	/* let the tracer grab locks here if needed */
	if (current_trace->start)
		current_trace->start(iter);

	if (*pos != iter->pos) {
		iter->ent = NULL;
		iter->cpu = 0;
		iter->idx = -1;
		iter->prev_ent = NULL;
		iter->prev_cpu = -1;

		for_each_tracing_cpu(i) {
			iter->next_idx[i] = 0;
			iter->next_page[i] = NULL;
		}

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
	struct trace_iterator *iter = m->private;

	atomic_dec(&trace_record_cmdline_disabled);

	/* let the tracer release locks here if needed */
	if (current_trace && current_trace == iter->trace && iter->trace->stop)
		iter->trace->stop(iter);

	mutex_unlock(&trace_types_lock);
}

#define KRETPROBE_MSG "[unknown/kretprobe'd]"

#ifdef CONFIG_KRETPROBES
static inline int kretprobed(unsigned long addr)
{
	return addr == (unsigned long)kretprobe_trampoline;
}
#else
static inline int kretprobed(unsigned long addr)
{
	return 0;
}
#endif /* CONFIG_KRETPROBES */

static int
seq_print_sym_short(struct trace_seq *s, const char *fmt, unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];

	kallsyms_lookup(address, NULL, NULL, NULL, str);

	return trace_seq_printf(s, fmt, str);
#endif
	return 1;
}

static int
seq_print_sym_offset(struct trace_seq *s, const char *fmt,
		     unsigned long address)
{
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];

	sprint_symbol(str, address);
	return trace_seq_printf(s, fmt, str);
#endif
	return 1;
}

#ifndef CONFIG_64BIT
# define IP_FMT "%08lx"
#else
# define IP_FMT "%016lx"
#endif

static int
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

static void print_lat_help_header(struct seq_file *m)
{
	seq_puts(m, "#                _------=> CPU#            \n");
	seq_puts(m, "#               / _-----=> irqs-off        \n");
	seq_puts(m, "#              | / _----=> need-resched    \n");
	seq_puts(m, "#              || / _---=> hardirq/softirq \n");
	seq_puts(m, "#              ||| / _--=> preempt-depth   \n");
	seq_puts(m, "#              |||| /                      \n");
	seq_puts(m, "#              |||||     delay             \n");
	seq_puts(m, "#  cmd     pid ||||| time  |   caller      \n");
	seq_puts(m, "#     \\   /    |||||   \\   |   /           \n");
}

static void print_func_help_header(struct seq_file *m)
{
	seq_puts(m, "#           TASK-PID   CPU#    TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |      |          |         |\n");
}


static void
print_trace_header(struct seq_file *m, struct trace_iterator *iter)
{
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_array *tr = iter->tr;
	struct trace_array_cpu *data = tr->data[tr->cpu];
	struct tracer *type = current_trace;
	unsigned long total   = 0;
	unsigned long entries = 0;
	int cpu;
	const char *name = "preemption";

	if (type)
		name = type->name;

	for_each_tracing_cpu(cpu) {
		if (head_page(tr->data[cpu])) {
			total += tr->data[cpu]->trace_idx;
			if (tr->data[cpu]->trace_idx > tr->entries)
				entries += tr->entries;
			else
				entries += tr->data[cpu]->trace_idx;
		}
	}

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
	trace_seq_printf(s, "%d", cpu);
	trace_seq_printf(s, "%c%c",
			(entry->flags & TRACE_FLAG_IRQS_OFF) ? 'd' : '.',
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
lat_print_timestamp(struct trace_seq *s, unsigned long long abs_usecs,
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

static int
print_lat_fmt(struct trace_iterator *iter, unsigned int trace_idx, int cpu)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *next_entry = find_next_entry(iter, NULL);
	unsigned long verbose = (trace_flags & TRACE_ITER_VERBOSE);
	struct trace_entry *entry = iter->ent;
	unsigned long abs_usecs;
	unsigned long rel_usecs;
	char *comm;
	int S, T;
	int i;
	unsigned state;

	if (!next_entry)
		next_entry = entry;
	rel_usecs = ns2usecs(next_entry->t - entry->t);
	abs_usecs = ns2usecs(entry->t - iter->tr->time_start);

	if (verbose) {
		comm = trace_find_cmdline(entry->pid);
		trace_seq_printf(s, "%16s %5d %d %d %08x %08x [%08lx]"
				 " %ld.%03ldms (+%ld.%03ldms): ",
				 comm,
				 entry->pid, cpu, entry->flags,
				 entry->preempt_count, trace_idx,
				 ns2usecs(entry->t),
				 abs_usecs/1000,
				 abs_usecs % 1000, rel_usecs/1000,
				 rel_usecs % 1000);
	} else {
		lat_print_generic(s, entry, cpu);
		lat_print_timestamp(s, abs_usecs, rel_usecs);
	}
	switch (entry->type) {
	case TRACE_FN:
		seq_print_ip_sym(s, entry->fn.ip, sym_flags);
		trace_seq_puts(s, " (");
		if (kretprobed(entry->fn.parent_ip))
			trace_seq_puts(s, KRETPROBE_MSG);
		else
			seq_print_ip_sym(s, entry->fn.parent_ip, sym_flags);
		trace_seq_puts(s, ")\n");
		break;
	case TRACE_CTX:
	case TRACE_WAKE:
		T = entry->ctx.next_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.next_state] : 'X';

		state = entry->ctx.prev_state ? __ffs(entry->ctx.prev_state) + 1 : 0;
		S = state < sizeof(state_to_char) - 1 ? state_to_char[state] : 'X';
		comm = trace_find_cmdline(entry->ctx.next_pid);
		trace_seq_printf(s, " %5d:%3d:%c %s %5d:%3d:%c %s\n",
				 entry->ctx.prev_pid,
				 entry->ctx.prev_prio,
				 S, entry->type == TRACE_CTX ? "==>" : "  +",
				 entry->ctx.next_pid,
				 entry->ctx.next_prio,
				 T, comm);
		break;
	case TRACE_SPECIAL:
		trace_seq_printf(s, "# %ld %ld %ld\n",
				 entry->special.arg1,
				 entry->special.arg2,
				 entry->special.arg3);
		break;
	case TRACE_STACK:
		for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
			if (i)
				trace_seq_puts(s, " <= ");
			seq_print_ip_sym(s, entry->stack.caller[i], sym_flags);
		}
		trace_seq_puts(s, "\n");
		break;
	default:
		trace_seq_printf(s, "Unknown type %d\n", entry->type);
	}
	return 1;
}

static int print_trace_fmt(struct trace_iterator *iter)
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

	comm = trace_find_cmdline(iter->ent->pid);

	t = ns2usecs(entry->t);
	usec_rem = do_div(t, 1000000ULL);
	secs = (unsigned long)t;

	ret = trace_seq_printf(s, "%16s-%-5d ", comm, entry->pid);
	if (!ret)
		return 0;
	ret = trace_seq_printf(s, "[%02d] ", iter->cpu);
	if (!ret)
		return 0;
	ret = trace_seq_printf(s, "%5lu.%06lu: ", secs, usec_rem);
	if (!ret)
		return 0;

	switch (entry->type) {
	case TRACE_FN:
		ret = seq_print_ip_sym(s, entry->fn.ip, sym_flags);
		if (!ret)
			return 0;
		if ((sym_flags & TRACE_ITER_PRINT_PARENT) &&
						entry->fn.parent_ip) {
			ret = trace_seq_printf(s, " <-");
			if (!ret)
				return 0;
			if (kretprobed(entry->fn.parent_ip))
				ret = trace_seq_puts(s, KRETPROBE_MSG);
			else
				ret = seq_print_ip_sym(s, entry->fn.parent_ip,
						       sym_flags);
			if (!ret)
				return 0;
		}
		ret = trace_seq_printf(s, "\n");
		if (!ret)
			return 0;
		break;
	case TRACE_CTX:
	case TRACE_WAKE:
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		T = entry->ctx.next_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.next_state] : 'X';
		ret = trace_seq_printf(s, " %5d:%3d:%c %s %5d:%3d:%c\n",
				       entry->ctx.prev_pid,
				       entry->ctx.prev_prio,
				       S,
				       entry->type == TRACE_CTX ? "==>" : "  +",
				       entry->ctx.next_pid,
				       entry->ctx.next_prio,
				       T);
		if (!ret)
			return 0;
		break;
	case TRACE_SPECIAL:
		ret = trace_seq_printf(s, "# %ld %ld %ld\n",
				 entry->special.arg1,
				 entry->special.arg2,
				 entry->special.arg3);
		if (!ret)
			return 0;
		break;
	case TRACE_STACK:
		for (i = 0; i < FTRACE_STACK_ENTRIES; i++) {
			if (i) {
				ret = trace_seq_puts(s, " <= ");
				if (!ret)
					return 0;
			}
			ret = seq_print_ip_sym(s, entry->stack.caller[i],
					       sym_flags);
			if (!ret)
				return 0;
		}
		ret = trace_seq_puts(s, "\n");
		if (!ret)
			return 0;
		break;
	}
	return 1;
}

static int print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	int ret;
	int S, T;

	entry = iter->ent;

	ret = trace_seq_printf(s, "%d %d %llu ",
		entry->pid, iter->cpu, entry->t);
	if (!ret)
		return 0;

	switch (entry->type) {
	case TRACE_FN:
		ret = trace_seq_printf(s, "%x %x\n",
					entry->fn.ip, entry->fn.parent_ip);
		if (!ret)
			return 0;
		break;
	case TRACE_CTX:
	case TRACE_WAKE:
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		T = entry->ctx.next_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.next_state] : 'X';
		if (entry->type == TRACE_WAKE)
			S = '+';
		ret = trace_seq_printf(s, "%d %d %c %d %d %c\n",
				       entry->ctx.prev_pid,
				       entry->ctx.prev_prio,
				       S,
				       entry->ctx.next_pid,
				       entry->ctx.next_prio,
				       T);
		if (!ret)
			return 0;
		break;
	case TRACE_SPECIAL:
	case TRACE_STACK:
		ret = trace_seq_printf(s, "# %ld %ld %ld\n",
				 entry->special.arg1,
				 entry->special.arg2,
				 entry->special.arg3);
		if (!ret)
			return 0;
		break;
	}
	return 1;
}

#define SEQ_PUT_FIELD_RET(s, x)				\
do {							\
	if (!trace_seq_putmem(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

#define SEQ_PUT_HEX_FIELD_RET(s, x)			\
do {							\
	if (!trace_seq_putmem_hex(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

static int print_hex_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned char newline = '\n';
	struct trace_entry *entry;
	int S, T;

	entry = iter->ent;

	SEQ_PUT_HEX_FIELD_RET(s, entry->pid);
	SEQ_PUT_HEX_FIELD_RET(s, iter->cpu);
	SEQ_PUT_HEX_FIELD_RET(s, entry->t);

	switch (entry->type) {
	case TRACE_FN:
		SEQ_PUT_HEX_FIELD_RET(s, entry->fn.ip);
		SEQ_PUT_HEX_FIELD_RET(s, entry->fn.parent_ip);
		break;
	case TRACE_CTX:
	case TRACE_WAKE:
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		T = entry->ctx.next_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.next_state] : 'X';
		if (entry->type == TRACE_WAKE)
			S = '+';
		SEQ_PUT_HEX_FIELD_RET(s, entry->ctx.prev_pid);
		SEQ_PUT_HEX_FIELD_RET(s, entry->ctx.prev_prio);
		SEQ_PUT_HEX_FIELD_RET(s, S);
		SEQ_PUT_HEX_FIELD_RET(s, entry->ctx.next_pid);
		SEQ_PUT_HEX_FIELD_RET(s, entry->ctx.next_prio);
		SEQ_PUT_HEX_FIELD_RET(s, entry->fn.parent_ip);
		SEQ_PUT_HEX_FIELD_RET(s, T);
		break;
	case TRACE_SPECIAL:
	case TRACE_STACK:
		SEQ_PUT_HEX_FIELD_RET(s, entry->special.arg1);
		SEQ_PUT_HEX_FIELD_RET(s, entry->special.arg2);
		SEQ_PUT_HEX_FIELD_RET(s, entry->special.arg3);
		break;
	}
	SEQ_PUT_FIELD_RET(s, newline);

	return 1;
}

static int print_bin_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;

	entry = iter->ent;

	SEQ_PUT_FIELD_RET(s, entry->pid);
	SEQ_PUT_FIELD_RET(s, entry->cpu);
	SEQ_PUT_FIELD_RET(s, entry->t);

	switch (entry->type) {
	case TRACE_FN:
		SEQ_PUT_FIELD_RET(s, entry->fn.ip);
		SEQ_PUT_FIELD_RET(s, entry->fn.parent_ip);
		break;
	case TRACE_CTX:
		SEQ_PUT_FIELD_RET(s, entry->ctx.prev_pid);
		SEQ_PUT_FIELD_RET(s, entry->ctx.prev_prio);
		SEQ_PUT_FIELD_RET(s, entry->ctx.prev_state);
		SEQ_PUT_FIELD_RET(s, entry->ctx.next_pid);
		SEQ_PUT_FIELD_RET(s, entry->ctx.next_prio);
		SEQ_PUT_FIELD_RET(s, entry->ctx.next_state);
		break;
	case TRACE_SPECIAL:
	case TRACE_STACK:
		SEQ_PUT_FIELD_RET(s, entry->special.arg1);
		SEQ_PUT_FIELD_RET(s, entry->special.arg2);
		SEQ_PUT_FIELD_RET(s, entry->special.arg3);
		break;
	}
	return 1;
}

static int trace_empty(struct trace_iterator *iter)
{
	struct trace_array_cpu *data;
	int cpu;

	for_each_tracing_cpu(cpu) {
		data = iter->tr->data[cpu];

		if (head_page(data) && data->trace_idx &&
		    (data->trace_tail != data->trace_head ||
		     data->trace_tail_idx != data->trace_head_idx))
			return 0;
	}
	return 1;
}

static int print_trace_line(struct trace_iterator *iter)
{
	if (iter->trace && iter->trace->print_line)
		return iter->trace->print_line(iter);

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
		if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
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

	/* TODO stop tracer */
	*ret = seq_open(file, &tracer_seq_ops);
	if (!*ret) {
		struct seq_file *m = file->private_data;
		m->private = iter;

		/* stop the trace while dumping */
		if (iter->tr->ctrl) {
			tracer_enabled = 0;
			ftrace_function_enabled = 0;
		}

		if (iter->trace && iter->trace->open)
			iter->trace->open(iter);
	} else {
		kfree(iter);
		iter = NULL;
	}
	mutex_unlock(&trace_types_lock);

 out:
	return iter;
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

	mutex_lock(&trace_types_lock);
	if (iter->trace && iter->trace->close)
		iter->trace->close(iter);

	/* reenable tracing if it was previously enabled */
	if (iter->tr->ctrl) {
		tracer_enabled = 1;
		/*
		 * It is safe to enable function tracing even if it
		 * isn't used
		 */
		ftrace_function_enabled = 1;
	}
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
static cpumask_t tracing_cpumask = CPU_MASK_ALL;

/*
 * When tracing/tracing_cpu_mask is modified then this holds
 * the new bitmask we are about to install:
 */
static cpumask_t tracing_cpumask_new;

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

	mutex_lock(&tracing_cpumask_update_lock);
	err = cpumask_parse_user(ubuf, count, tracing_cpumask_new);
	if (err)
		goto err_unlock;

	raw_local_irq_disable();
	__raw_spin_lock(&ftrace_max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpu_isset(cpu, tracing_cpumask) &&
				!cpu_isset(cpu, tracing_cpumask_new)) {
			atomic_inc(&global_trace.data[cpu]->disabled);
		}
		if (!cpu_isset(cpu, tracing_cpumask) &&
				cpu_isset(cpu, tracing_cpumask_new)) {
			atomic_dec(&global_trace.data[cpu]->disabled);
		}
	}
	__raw_spin_unlock(&ftrace_max_lock);
	raw_local_irq_enable();

	tracing_cpumask = tracing_cpumask_new;

	mutex_unlock(&tracing_cpumask_update_lock);

	return count;

err_unlock:
	mutex_unlock(&tracing_cpumask_update_lock);

	return err;
}

static struct file_operations tracing_cpumask_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_cpumask_read,
	.write		= tracing_cpumask_write,
};

static ssize_t
tracing_iter_ctrl_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	char *buf;
	int r = 0;
	int len = 0;
	int i;

	/* calulate max size */
	for (i = 0; trace_options[i]; i++) {
		len += strlen(trace_options[i]);
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

	r += sprintf(buf + r, "\n");
	WARN_ON(r >= len + 2);

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);

	kfree(buf);

	return r;
}

static ssize_t
tracing_iter_ctrl_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[64];
	char *cmp = buf;
	int neg = 0;
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
	/*
	 * If no option could be set, return an error:
	 */
	if (!trace_options[i])
		return -EINVAL;

	filp->f_pos += cnt;

	return cnt;
}

static struct file_operations tracing_iter_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_iter_ctrl_read,
	.write		= tracing_iter_ctrl_write,
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
	"# cat /debug/tracing/iter_ctrl\n"
	"noprint-parent nosym-offset nosym-addr noverbose\n"
	"# echo print-parent > /debug/tracing/iter_ctrl\n"
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
	struct trace_array *tr = filp->private_data;
	char buf[64];
	int r;

	r = sprintf(buf, "%ld\n", tr->ctrl);
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
	if (tr->ctrl ^ val) {
		if (val)
			tracer_enabled = 1;
		else
			tracer_enabled = 0;

		tr->ctrl = val;

		if (current_trace && current_trace->ctrl_update)
			current_trace->ctrl_update(tr);
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

static ssize_t
tracing_set_trace_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = &global_trace;
	struct tracer *t;
	char buf[max_tracer_type_len+1];
	int i;

	if (cnt > max_tracer_type_len)
		cnt = max_tracer_type_len;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	mutex_lock(&trace_types_lock);
	for (t = trace_types; t; t = t->next) {
		if (strcmp(t->name, buf) == 0)
			break;
	}
	if (!t || t == current_trace)
		goto out;

	if (current_trace && current_trace->reset)
		current_trace->reset(tr);

	current_trace = t;
	if (t->init)
		t->init(tr);

 out:
	mutex_unlock(&trace_types_lock);

	filp->f_pos += cnt;

	return cnt;
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

	mutex_lock(&trace_types_lock);
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
	struct trace_array_cpu *data;
	static cpumask_t mask;
	unsigned long flags;
#ifdef CONFIG_FTRACE
	int ftrace_save;
#endif
	int cpu;
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;
	sret = 0;

	trace_seq_reset(&iter->seq);

	mutex_lock(&trace_types_lock);
	if (iter->trace->read) {
		sret = iter->trace->read(iter, filp, ubuf, cnt, ppos);
		if (sret)
			goto out;
	}

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

	/*
	 * We need to stop all tracing on all CPUS to read the
	 * the next buffer. This is a bit expensive, but is
	 * not done often. We fill all what we can read,
	 * and then release the locks again.
	 */

	cpus_clear(mask);
	local_irq_save(flags);
#ifdef CONFIG_FTRACE
	ftrace_save = ftrace_enabled;
	ftrace_enabled = 0;
#endif
	smp_wmb();
	for_each_tracing_cpu(cpu) {
		data = iter->tr->data[cpu];

		if (!head_page(data) || !data->trace_idx)
			continue;

		atomic_inc(&data->disabled);
		cpu_set(cpu, mask);
	}

	for_each_cpu_mask(cpu, mask) {
		data = iter->tr->data[cpu];
		__raw_spin_lock(&data->lock);

		if (data->overrun > iter->last_overrun[cpu])
			iter->overrun[cpu] +=
				data->overrun - iter->last_overrun[cpu];
		iter->last_overrun[cpu] = data->overrun;
	}

	while (find_next_entry_inc(iter) != NULL) {
		int ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (!ret) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}

		trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;
	}

	for_each_cpu_mask(cpu, mask) {
		data = iter->tr->data[cpu];
		__raw_spin_unlock(&data->lock);
	}

	for_each_cpu_mask(cpu, mask) {
		data = iter->tr->data[cpu];
		atomic_dec(&data->disabled);
	}
#ifdef CONFIG_FTRACE
	ftrace_enabled = ftrace_save;
#endif
	local_irq_restore(flags);

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_reset(&iter->seq);
	if (sret == -EBUSY)
		sret = 0;

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

	r = sprintf(buf, "%lu\n", tr->entries);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_entries_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	unsigned long val;
	char buf[64];
	int i, ret;

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

	if (current_trace != &no_tracer) {
		cnt = -EBUSY;
		pr_info("ftrace: set current_tracer to none"
			" before modifying buffer size\n");
		goto out;
	}

	if (val > global_trace.entries) {
		long pages_requested;
		unsigned long freeable_pages;

		/* make sure we have enough memory before mapping */
		pages_requested =
			(val + (ENTRIES_PER_PAGE-1)) / ENTRIES_PER_PAGE;

		/* account for each buffer (and max_tr) */
		pages_requested *= tracing_nr_buffers * 2;

		/* Check for overflow */
		if (pages_requested < 0) {
			cnt = -ENOMEM;
			goto out;
		}

		freeable_pages = determine_dirtyable_memory();

		/* we only allow to request 1/4 of useable memory */
		if (pages_requested >
		    ((freeable_pages + tracing_pages_allocated) / 4)) {
			cnt = -ENOMEM;
			goto out;
		}

		while (global_trace.entries < val) {
			if (trace_alloc_page()) {
				cnt = -ENOMEM;
				goto out;
			}
			/* double check that we don't go over the known pages */
			if (tracing_pages_allocated > pages_requested)
				break;
		}

	} else {
		/* include the number of entries in val (inc of page entries) */
		while (global_trace.entries > val + (ENTRIES_PER_PAGE - 1))
			trace_free_page();
	}

	/* check integrity */
	for_each_tracing_cpu(i)
		check_pages(global_trace.data[i]);

	filp->f_pos += cnt;

	/* If check pages failed, return ENOMEM */
	if (tracing_disabled)
		cnt = -ENOMEM;
 out:
	max_tr.entries = global_trace.entries;
	mutex_unlock(&trace_types_lock);

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

#ifdef CONFIG_DYNAMIC_FTRACE

static ssize_t
tracing_read_long(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	unsigned long *p = filp->private_data;
	char buf[64];
	int r;

	r = sprintf(buf, "%ld\n", *p);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static struct file_operations tracing_read_long_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_read_long,
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

static __init void tracer_init_debugfs(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("tracing_enabled", 0644, d_tracer,
				    &global_trace, &tracing_ctrl_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'tracing_enabled' entry\n");

	entry = debugfs_create_file("iter_ctrl", 0644, d_tracer,
				    NULL, &tracing_iter_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'iter_ctrl' entry\n");

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
		pr_warning("Could not create debugfs 'trace' entry\n");

	entry = debugfs_create_file("current_tracer", 0444, d_tracer,
				    &global_trace, &set_tracer_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace' entry\n");

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
			   "'tracing_threash' entry\n");
	entry = debugfs_create_file("README", 0644, d_tracer,
				    NULL, &tracing_readme_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'README' entry\n");

	entry = debugfs_create_file("trace_pipe", 0644, d_tracer,
				    NULL, &tracing_pipe_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_threash' entry\n");

	entry = debugfs_create_file("trace_entries", 0644, d_tracer,
				    &global_trace, &tracing_entries_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_threash' entry\n");

#ifdef CONFIG_DYNAMIC_FTRACE
	entry = debugfs_create_file("dyn_ftrace_total_info", 0444, d_tracer,
				    &ftrace_update_tot_cnt,
				    &tracing_read_long_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'dyn_ftrace_total_info' entry\n");
#endif
#ifdef CONFIG_SYSPROF_TRACER
	init_tracer_sysprof_debugfs(d_tracer);
#endif
}

static int trace_alloc_page(void)
{
	struct trace_array_cpu *data;
	struct page *page, *tmp;
	LIST_HEAD(pages);
	void *array;
	unsigned pages_allocated = 0;
	int i;

	/* first allocate a page for each CPU */
	for_each_tracing_cpu(i) {
		array = (void *)__get_free_page(GFP_KERNEL);
		if (array == NULL) {
			printk(KERN_ERR "tracer: failed to allocate page"
			       "for trace buffer!\n");
			goto free_pages;
		}

		pages_allocated++;
		page = virt_to_page(array);
		list_add(&page->lru, &pages);

/* Only allocate if we are actually using the max trace */
#ifdef CONFIG_TRACER_MAX_TRACE
		array = (void *)__get_free_page(GFP_KERNEL);
		if (array == NULL) {
			printk(KERN_ERR "tracer: failed to allocate page"
			       "for trace buffer!\n");
			goto free_pages;
		}
		pages_allocated++;
		page = virt_to_page(array);
		list_add(&page->lru, &pages);
#endif
	}

	/* Now that we successfully allocate a page per CPU, add them */
	for_each_tracing_cpu(i) {
		data = global_trace.data[i];
		page = list_entry(pages.next, struct page, lru);
		list_del_init(&page->lru);
		list_add_tail(&page->lru, &data->trace_pages);
		ClearPageLRU(page);

#ifdef CONFIG_TRACER_MAX_TRACE
		data = max_tr.data[i];
		page = list_entry(pages.next, struct page, lru);
		list_del_init(&page->lru);
		list_add_tail(&page->lru, &data->trace_pages);
		SetPageLRU(page);
#endif
	}
	tracing_pages_allocated += pages_allocated;
	global_trace.entries += ENTRIES_PER_PAGE;

	return 0;

 free_pages:
	list_for_each_entry_safe(page, tmp, &pages, lru) {
		list_del_init(&page->lru);
		__free_page(page);
	}
	return -ENOMEM;
}

static int trace_free_page(void)
{
	struct trace_array_cpu *data;
	struct page *page;
	struct list_head *p;
	int i;
	int ret = 0;

	/* free one page from each buffer */
	for_each_tracing_cpu(i) {
		data = global_trace.data[i];
		p = data->trace_pages.next;
		if (p == &data->trace_pages) {
			/* should never happen */
			WARN_ON(1);
			tracing_disabled = 1;
			ret = -1;
			break;
		}
		page = list_entry(p, struct page, lru);
		ClearPageLRU(page);
		list_del(&page->lru);
		tracing_pages_allocated--;
		tracing_pages_allocated--;
		__free_page(page);

		tracing_reset(data);

#ifdef CONFIG_TRACER_MAX_TRACE
		data = max_tr.data[i];
		p = data->trace_pages.next;
		if (p == &data->trace_pages) {
			/* should never happen */
			WARN_ON(1);
			tracing_disabled = 1;
			ret = -1;
			break;
		}
		page = list_entry(p, struct page, lru);
		ClearPageLRU(page);
		list_del(&page->lru);
		__free_page(page);

		tracing_reset(data);
#endif
	}
	global_trace.entries -= ENTRIES_PER_PAGE;

	return ret;
}

__init static int tracer_alloc_buffers(void)
{
	struct trace_array_cpu *data;
	void *array;
	struct page *page;
	int pages = 0;
	int ret = -ENOMEM;
	int i;

	/* TODO: make the number of buffers hot pluggable with CPUS */
	tracing_nr_buffers = num_possible_cpus();
	tracing_buffer_mask = cpu_possible_map;

	/* Allocate the first page for all buffers */
	for_each_tracing_cpu(i) {
		data = global_trace.data[i] = &per_cpu(global_trace_cpu, i);
		max_tr.data[i] = &per_cpu(max_data, i);

		array = (void *)__get_free_page(GFP_KERNEL);
		if (array == NULL) {
			printk(KERN_ERR "tracer: failed to allocate page"
			       "for trace buffer!\n");
			goto free_buffers;
		}

		/* set the array to the list */
		INIT_LIST_HEAD(&data->trace_pages);
		page = virt_to_page(array);
		list_add(&page->lru, &data->trace_pages);
		/* use the LRU flag to differentiate the two buffers */
		ClearPageLRU(page);

		data->lock = (raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;
		max_tr.data[i]->lock = (raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

/* Only allocate if we are actually using the max trace */
#ifdef CONFIG_TRACER_MAX_TRACE
		array = (void *)__get_free_page(GFP_KERNEL);
		if (array == NULL) {
			printk(KERN_ERR "tracer: failed to allocate page"
			       "for trace buffer!\n");
			goto free_buffers;
		}

		INIT_LIST_HEAD(&max_tr.data[i]->trace_pages);
		page = virt_to_page(array);
		list_add(&page->lru, &max_tr.data[i]->trace_pages);
		SetPageLRU(page);
#endif
	}

	/*
	 * Since we allocate by orders of pages, we may be able to
	 * round up a bit.
	 */
	global_trace.entries = ENTRIES_PER_PAGE;
	pages++;

	while (global_trace.entries < trace_nr_entries) {
		if (trace_alloc_page())
			break;
		pages++;
	}
	max_tr.entries = global_trace.entries;

	pr_info("tracer: %d pages allocated for %ld entries of %ld bytes\n",
		pages, trace_nr_entries, (long)TRACE_ENTRY_SIZE);
	pr_info("   actual entries %ld\n", global_trace.entries);

	tracer_init_debugfs();

	trace_init_cmdlines();

	register_tracer(&no_tracer);
	current_trace = &no_tracer;

	/* All seems OK, enable tracing */
	global_trace.ctrl = tracer_enabled;
	tracing_disabled = 0;

	return 0;

 free_buffers:
	for (i-- ; i >= 0; i--) {
		struct page *page, *tmp;
		struct trace_array_cpu *data = global_trace.data[i];

		if (data) {
			list_for_each_entry_safe(page, tmp,
						 &data->trace_pages, lru) {
				list_del_init(&page->lru);
				__free_page(page);
			}
		}

#ifdef CONFIG_TRACER_MAX_TRACE
		data = max_tr.data[i];
		if (data) {
			list_for_each_entry_safe(page, tmp,
						 &data->trace_pages, lru) {
				list_del_init(&page->lru);
				__free_page(page);
			}
		}
#endif
	}
	return ret;
}
fs_initcall(tracer_alloc_buffers);
