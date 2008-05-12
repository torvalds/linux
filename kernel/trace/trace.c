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
#include <linux/gfp.h>
#include <linux/fs.h>

#include "trace.h"

unsigned long __read_mostly	tracing_max_latency = (cycle_t)ULONG_MAX;
unsigned long __read_mostly	tracing_thresh;

static int tracing_disabled = 1;

static long notrace
ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

static const int time_sync_freq_max = 128;
static const cycle_t time_sync_thresh = 100000;

static DEFINE_PER_CPU(cycle_t, time_offset);
static DEFINE_PER_CPU(cycle_t, prev_cpu_time);
static DEFINE_PER_CPU(int, time_sync_count);
static DEFINE_PER_CPU(int, time_sync_freq);

/*
 * Global lock which we take every now and then to synchronize
 * the CPUs time. This method is not warp-safe, but it's good
 * enough to synchronize slowly diverging time sources and thus
 * it's good enough for tracing:
 */
static DEFINE_SPINLOCK(time_sync_lock);
static cycle_t prev_global_time;

static notrace cycle_t __ftrace_now_sync(cycles_t time, int cpu)
{
	unsigned long flags;

	spin_lock_irqsave(&time_sync_lock, flags);

	/*
	 * Update the synchronization frequency:
	 */
	if (per_cpu(time_sync_freq, cpu) < time_sync_freq_max)
		per_cpu(time_sync_freq, cpu) *= 2;
	per_cpu(time_sync_count, cpu) = per_cpu(time_sync_freq, cpu);

	if (time < prev_global_time) {
		per_cpu(time_offset, cpu) += prev_global_time - time;
		time = prev_global_time;
	} else {
		prev_global_time = time;
	}

	spin_unlock_irqrestore(&time_sync_lock, flags);

	return time;
}

notrace cycle_t ftrace_now(int cpu)
{
	cycle_t prev_cpu_time, time, delta_time;

	prev_cpu_time = per_cpu(prev_cpu_time, cpu);
	time = sched_clock() + per_cpu(time_offset, cpu);
	delta_time = time-prev_cpu_time;

	if (unlikely(delta_time > time_sync_thresh ||
				--per_cpu(time_sync_count, cpu) <= 0))
		time = __ftrace_now_sync(time, cpu);

	return time;
}

static struct trace_array	global_trace;

static DEFINE_PER_CPU(struct trace_array_cpu, global_trace_cpu);

static struct trace_array	max_tr;

static DEFINE_PER_CPU(struct trace_array_cpu, max_data);

static int			tracer_enabled = 1;
static unsigned long		trace_nr_entries = 16384UL;

static struct tracer		*trace_types __read_mostly;
static struct tracer		*current_trace __read_mostly;
static int			max_tracer_type_len;

static DEFINE_MUTEX(trace_types_lock);

#define ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct trace_entry))

static int __init set_nr_entries(char *str)
{
	if (!str)
		return 0;
	trace_nr_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("trace_entries=", set_nr_entries);

unsigned long nsecs_to_usecs(unsigned long nsecs)
{
	return nsecs / 1000;
}

enum trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_FN,
	TRACE_CTX,
	TRACE_SPECIAL,

	__TRACE_LAST_TYPE
};

enum trace_flag_type {
	TRACE_FLAG_IRQS_OFF		= 0x01,
	TRACE_FLAG_NEED_RESCHED		= 0x02,
	TRACE_FLAG_HARDIRQ		= 0x04,
	TRACE_FLAG_SOFTIRQ		= 0x08,
};

enum trace_iterator_flags {
	TRACE_ITER_PRINT_PARENT		= 0x01,
	TRACE_ITER_SYM_OFFSET		= 0x02,
	TRACE_ITER_SYM_ADDR		= 0x04,
	TRACE_ITER_VERBOSE		= 0x08,
	TRACE_ITER_RAW			= 0x10,
	TRACE_ITER_BIN			= 0x20,
};

#define TRACE_ITER_SYM_MASK \
	(TRACE_ITER_PRINT_PARENT|TRACE_ITER_SYM_OFFSET|TRACE_ITER_SYM_ADDR)

/* These must match the bit postions above */
static const char *trace_options[] = {
	"print-parent",
	"sym-offset",
	"sym-addr",
	"verbose",
	"raw",
	"bin",
	NULL
};

static unsigned trace_flags;

static DEFINE_SPINLOCK(ftrace_max_lock);

/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /debugfs/tracing/latency_trace)
 */
static notrace void
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

void check_pages(struct trace_array_cpu *data)
{
	struct page *page, *tmp;

	BUG_ON(data->trace_pages.next->prev != &data->trace_pages);
	BUG_ON(data->trace_pages.prev->next != &data->trace_pages);

	list_for_each_entry_safe(page, tmp, &data->trace_pages, lru) {
		BUG_ON(page->lru.next->prev != &page->lru);
		BUG_ON(page->lru.prev->next != &page->lru);
	}
}

void *head_page(struct trace_array_cpu *data)
{
	struct page *page;

	check_pages(data);
	if (list_empty(&data->trace_pages))
		return NULL;

	page = list_entry(data->trace_pages.next, struct page, lru);
	BUG_ON(&page->lru == &data->trace_pages);

	return page_address(page);
}

static notrace int
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
	if (ret > len)
		return 0;

	s->len += ret;

	return len;
}

static notrace int
trace_seq_puts(struct trace_seq *s, const char *str)
{
	int len = strlen(str);

	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, str, len);
	s->len += len;

	return len;
}

static notrace int
trace_seq_putc(struct trace_seq *s, unsigned char c)
{
	if (s->len >= (PAGE_SIZE - 1))
		return 0;

	s->buffer[s->len++] = c;

	return 1;
}

static notrace int
trace_seq_putmem(struct trace_seq *s, void *mem, size_t len)
{
	if (len > ((PAGE_SIZE - 1) - s->len))
		return 0;

	memcpy(s->buffer + s->len, mem, len);
	s->len += len;

	return len;
}

static notrace void
trace_seq_reset(struct trace_seq *s)
{
	s->len = 0;
}

static notrace void
trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

	s->buffer[len] = 0;
	seq_puts(m, s->buffer);

	trace_seq_reset(s);
}

notrace static void
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

notrace void
update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct trace_array_cpu *data;
	int i;

	WARN_ON_ONCE(!irqs_disabled());
	spin_lock(&ftrace_max_lock);
	/* clear out all the previous traces */
	for_each_possible_cpu(i) {
		data = tr->data[i];
		flip_trace(max_tr.data[i], data);
		tracing_reset(data);
	}

	__update_max_tr(tr, tsk, cpu);
	spin_unlock(&ftrace_max_lock);
}

/**
 * update_max_tr_single - only copy one trace over, and reset the rest
 * @tr - tracer
 * @tsk - task with the latency
 * @cpu - the cpu of the buffer to copy.
 */
notrace void
update_max_tr_single(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct trace_array_cpu *data = tr->data[cpu];
	int i;

	WARN_ON_ONCE(!irqs_disabled());
	spin_lock(&ftrace_max_lock);
	for_each_possible_cpu(i)
		tracing_reset(max_tr.data[i]);

	flip_trace(max_tr.data[cpu], data);
	tracing_reset(data);

	__update_max_tr(tr, tsk, cpu);
	spin_unlock(&ftrace_max_lock);
}

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
		for_each_possible_cpu(i) {
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
		for_each_possible_cpu(i) {
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

notrace void tracing_reset(struct trace_array_cpu *data)
{
	data->trace_idx = 0;
	data->trace_head = data->trace_tail = head_page(data);
	data->trace_head_idx = 0;
	data->trace_tail_idx = 0;
}

#ifdef CONFIG_FTRACE
static notrace void
function_trace_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (unlikely(!tracer_enabled))
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		ftrace(tr, data, ip, parent_ip, flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = function_trace_call,
};
#endif

notrace void tracing_start_function_trace(void)
{
	register_ftrace_function(&trace_ops);
}

notrace void tracing_stop_function_trace(void)
{
	unregister_ftrace_function(&trace_ops);
}

#define SAVED_CMDLINES 128
static unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
static unsigned map_cmdline_to_pid[SAVED_CMDLINES];
static char saved_cmdlines[SAVED_CMDLINES][TASK_COMM_LEN];
static int cmdline_idx;
static DEFINE_SPINLOCK(trace_cmdline_lock);
atomic_t trace_record_cmdline_disabled;

static void trace_init_cmdlines(void)
{
	memset(&map_pid_to_cmdline, -1, sizeof(map_pid_to_cmdline));
	memset(&map_cmdline_to_pid, -1, sizeof(map_cmdline_to_pid));
	cmdline_idx = 0;
}

notrace void trace_stop_cmdline_recording(void);

static notrace void trace_save_cmdline(struct task_struct *tsk)
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

static notrace char *trace_find_cmdline(int pid)
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

notrace void tracing_record_cmdline(struct task_struct *tsk)
{
	if (atomic_read(&trace_record_cmdline_disabled))
		return;

	trace_save_cmdline(tsk);
}

static inline notrace struct list_head *
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

static inline notrace void *
trace_next_page(struct trace_array_cpu *data, void *addr)
{
	struct list_head *next;
	struct page *page;

	page = virt_to_page(addr);

	next = trace_next_list(data, &page->lru);
	page = list_entry(next, struct page, lru);

	return page_address(page);
}

static inline notrace struct trace_entry *
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

static inline notrace void
tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags)
{
	struct task_struct *tsk = current;
	unsigned long pc;

	pc = preempt_count();

	entry->preempt_count	= pc & 0xff;
	entry->pid		= tsk->pid;
	entry->t		= ftrace_now(raw_smp_processor_id());
	entry->flags = (irqs_disabled_flags(flags) ? TRACE_FLAG_IRQS_OFF : 0) |
		((pc & HARDIRQ_MASK) ? TRACE_FLAG_HARDIRQ : 0) |
		((pc & SOFTIRQ_MASK) ? TRACE_FLAG_SOFTIRQ : 0) |
		(need_resched() ? TRACE_FLAG_NEED_RESCHED : 0);
}

notrace void
ftrace(struct trace_array *tr, struct trace_array_cpu *data,
       unsigned long ip, unsigned long parent_ip, unsigned long flags)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	spin_lock_irqsave(&data->lock, irq_flags);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_FN;
	entry->fn.ip		= ip;
	entry->fn.parent_ip	= parent_ip;
	spin_unlock_irqrestore(&data->lock, irq_flags);
}

notrace void
trace_special(struct trace_array *tr, struct trace_array_cpu *data,
	      unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	spin_lock_irqsave(&data->lock, irq_flags);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, 0);
	entry->type		= TRACE_SPECIAL;
	entry->special.arg1	= arg1;
	entry->special.arg2	= arg2;
	entry->special.arg3	= arg3;
	spin_unlock_irqrestore(&data->lock, irq_flags);
}

notrace void
tracing_sched_switch_trace(struct trace_array *tr,
			   struct trace_array_cpu *data,
			   struct task_struct *prev, struct task_struct *next,
			   unsigned long flags)
{
	struct trace_entry *entry;
	unsigned long irq_flags;

	spin_lock_irqsave(&data->lock, irq_flags);
	entry			= tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, flags);
	entry->type		= TRACE_CTX;
	entry->ctx.prev_pid	= prev->pid;
	entry->ctx.prev_prio	= prev->prio;
	entry->ctx.prev_state	= prev->state;
	entry->ctx.next_pid	= next->pid;
	entry->ctx.next_prio	= next->prio;
	spin_unlock_irqrestore(&data->lock, irq_flags);
}

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

static struct notrace trace_entry *
find_next_entry(struct trace_iterator *iter, int *ent_cpu)
{
	struct trace_array *tr = iter->tr;
	struct trace_entry *ent, *next = NULL;
	int next_cpu = -1;
	int cpu;

	for_each_possible_cpu(cpu) {
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

static notrace void trace_iterator_increment(struct trace_iterator *iter)
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

static notrace void trace_consume(struct trace_iterator *iter)
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

static notrace void *find_next_entry_inc(struct trace_iterator *iter)
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

static notrace void *s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	void *last_ent = iter->ent;
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

	if (last_ent && !ent)
		seq_puts(m, "\n\nvim:ft=help\n");

	return ent;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l = 0;
	int i;

	mutex_lock(&trace_types_lock);

	if (!current_trace || current_trace != iter->trace)
		return NULL;

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

		for_each_possible_cpu(i) {
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

static notrace int
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

static notrace void print_lat_help_header(struct seq_file *m)
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

static notrace void print_func_help_header(struct seq_file *m)
{
	seq_puts(m, "#           TASK-PID   CPU#    TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |      |          |         |\n");
}


static notrace void
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

	for_each_possible_cpu(cpu) {
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
#elif defined(CONFIG_PREEMPT_DESKTOP)
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

static notrace void
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
	if (hardirq && softirq)
		trace_seq_putc(s, 'H');
	else {
		if (hardirq)
			trace_seq_putc(s, 'h');
		else {
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

static notrace void
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

static notrace int
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
	int S;

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
		seq_print_ip_sym(s, entry->fn.parent_ip, sym_flags);
		trace_seq_puts(s, ")\n");
		break;
	case TRACE_CTX:
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		comm = trace_find_cmdline(entry->ctx.next_pid);
		trace_seq_printf(s, " %d:%d:%c --> %d:%d %s\n",
				 entry->ctx.prev_pid,
				 entry->ctx.prev_prio,
				 S,
				 entry->ctx.next_pid,
				 entry->ctx.next_prio,
				 comm);
		break;
	case TRACE_SPECIAL:
		trace_seq_printf(s, " %lx %lx %lx\n",
				 entry->special.arg1,
				 entry->special.arg2,
				 entry->special.arg3);
		break;
	default:
		trace_seq_printf(s, "Unknown type %d\n", entry->type);
	}
	return 1;
}

static notrace int print_trace_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *entry;
	unsigned long usec_rem;
	unsigned long long t;
	unsigned long secs;
	char *comm;
	int S;
	int ret;

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
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		ret = trace_seq_printf(s, " %d:%d:%c ==> %d:%d\n",
				       entry->ctx.prev_pid,
				       entry->ctx.prev_prio,
				       S,
				       entry->ctx.next_pid,
				       entry->ctx.next_prio);
		if (!ret)
			return 0;
		break;
	case TRACE_SPECIAL:
		ret = trace_seq_printf(s, " %lx %lx %lx\n",
				 entry->special.arg1,
				 entry->special.arg2,
				 entry->special.arg3);
		if (!ret)
			return 0;
		break;
	}
	return 1;
}

static notrace int print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	int ret;
	int S;

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
		S = entry->ctx.prev_state < sizeof(state_to_char) ?
			state_to_char[entry->ctx.prev_state] : 'X';
		ret = trace_seq_printf(s, "%d %d %c %d %d\n",
				       entry->ctx.prev_pid,
				       entry->ctx.prev_prio,
				       S,
				       entry->ctx.next_pid,
				       entry->ctx.next_prio);
		if (!ret)
			return 0;
		break;
	case TRACE_SPECIAL:
		ret = trace_seq_printf(s, " %lx %lx %lx\n",
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

static notrace int print_bin_fmt(struct trace_iterator *iter)
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
		break;
	case TRACE_SPECIAL:
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

	for_each_possible_cpu(cpu) {
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
	if (trace_flags & TRACE_ITER_BIN)
		return print_bin_fmt(iter);

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

static struct trace_iterator notrace *
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
		if (iter->tr->ctrl)
			tracer_enabled = 0;

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
	if (iter->tr->ctrl)
		tracer_enabled = 1;
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


static notrace void *
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
	.open = show_traces_open,
	.read = seq_read,
	.release = seq_release,
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

	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    buf, r);

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

	if (cnt > 63)
		cnt = 63;

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

	filp->f_pos += cnt;

	return cnt;
}

static struct file_operations tracing_iter_fops = {
	.open = tracing_open_generic,
	.read = tracing_iter_ctrl_read,
	.write = tracing_iter_ctrl_write,
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
	.open = tracing_open_generic,
	.read = tracing_readme_read,
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
	long val;
	char buf[64];

	if (cnt > 63)
		cnt = 63;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	val = simple_strtoul(buf, NULL, 10);

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

	r = snprintf(buf, 64, "%ld\n",
		     *ptr == (unsigned long)-1 ? -1 : nsecs_to_usecs(*ptr));
	if (r > 64)
		r = 64;
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_max_lat_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	long *ptr = filp->private_data;
	long val;
	char buf[64];

	if (cnt > 63)
		cnt = 63;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	val = simple_strtoul(buf, NULL, 10);

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

	iter->tr = &global_trace;

	filp->private_data = iter;

	return 0;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;

	kfree(iter);
	atomic_dec(&tracing_reader);

	return 0;
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
	static int start;
	unsigned long flags;
	int read = 0;
	int cpu;
	int len;
	int ret;

	/* return any leftover data */
	if (iter->seq.len > start) {
		len = iter->seq.len - start;
		if (cnt > len)
			cnt = len;
		ret = copy_to_user(ubuf, iter->seq.buffer + start, cnt);
		if (ret)
			cnt = -EFAULT;

		start += len;

		return cnt;
	}

	trace_seq_reset(&iter->seq);
	start = 0;

	while (trace_empty(iter)) {
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

		/* sleep for one second, and try again. */
		schedule_timeout(HZ);

		iter->tr->waiter = NULL;

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

		continue;
	}

	/* stop when tracing is finished */
	if (trace_empty(iter))
		return 0;

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	memset(iter, 0, sizeof(*iter));
	iter->tr = &global_trace;
	iter->pos = -1;

	/*
	 * We need to stop all tracing on all CPUS to read the
	 * the next buffer. This is a bit expensive, but is
	 * not done often. We fill all what we can read,
	 * and then release the locks again.
	 */

	cpus_clear(mask);
	local_irq_save(flags);
	for_each_possible_cpu(cpu) {
		data = iter->tr->data[cpu];

		if (!head_page(data) || !data->trace_idx)
			continue;

		atomic_inc(&data->disabled);
		spin_lock(&data->lock);
		cpu_set(cpu, mask);
	}

	while (find_next_entry_inc(iter) != NULL) {
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
		spin_unlock(&data->lock);
		atomic_dec(&data->disabled);
	}
	local_irq_restore(flags);

	/* Now copy what we have to the user */
	read = iter->seq.len;
	if (read > cnt)
		read = cnt;

	ret = copy_to_user(ubuf, iter->seq.buffer, read);

	if (read < iter->seq.len)
		start = read;
	else
		trace_seq_reset(&iter->seq);

	if (ret)
		read = -EFAULT;

	return read;
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
	.read		= tracing_read_pipe,
	.release	= tracing_release_pipe,
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

#ifdef CONFIG_DYNAMIC_FTRACE
	entry = debugfs_create_file("dyn_ftrace_total_info", 0444, d_tracer,
				    &ftrace_update_tot_cnt,
				    &tracing_read_long_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'dyn_ftrace_total_info' entry\n");
#endif
}

/* dummy trace to disable tracing */
static struct tracer no_tracer __read_mostly =
{
	.name		= "none",
};

static int trace_alloc_page(void)
{
	struct trace_array_cpu *data;
	struct page *page, *tmp;
	LIST_HEAD(pages);
	void *array;
	int i;

	/* first allocate a page for each CPU */
	for_each_possible_cpu(i) {
		array = (void *)__get_free_page(GFP_KERNEL);
		if (array == NULL) {
			printk(KERN_ERR "tracer: failed to allocate page"
			       "for trace buffer!\n");
			goto free_pages;
		}

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
		page = virt_to_page(array);
		list_add(&page->lru, &pages);
#endif
	}

	/* Now that we successfully allocate a page per CPU, add them */
	for_each_possible_cpu(i) {
		data = global_trace.data[i];
		spin_lock_init(&data->lock);
		lockdep_set_class(&data->lock, &data->lock_key);
		page = list_entry(pages.next, struct page, lru);
		list_del_init(&page->lru);
		list_add_tail(&page->lru, &data->trace_pages);
		ClearPageLRU(page);

#ifdef CONFIG_TRACER_MAX_TRACE
		data = max_tr.data[i];
		spin_lock_init(&data->lock);
		lockdep_set_class(&data->lock, &data->lock_key);
		page = list_entry(pages.next, struct page, lru);
		list_del_init(&page->lru);
		list_add_tail(&page->lru, &data->trace_pages);
		SetPageLRU(page);
#endif
	}
	global_trace.entries += ENTRIES_PER_PAGE;

	return 0;

 free_pages:
	list_for_each_entry_safe(page, tmp, &pages, lru) {
		list_del_init(&page->lru);
		__free_page(page);
	}
	return -ENOMEM;
}

__init static int tracer_alloc_buffers(void)
{
	struct trace_array_cpu *data;
	void *array;
	struct page *page;
	int pages = 0;
	int ret = -ENOMEM;
	int i;

	global_trace.ctrl = tracer_enabled;

	/* Allocate the first page for all buffers */
	for_each_possible_cpu(i) {
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

	pr_info("tracer: %d pages allocated for %ld",
		pages, trace_nr_entries);
	pr_info(" entries of %ld bytes\n", (long)TRACE_ENTRY_SIZE);
	pr_info("   actual entries %ld\n", global_trace.entries);

	tracer_init_debugfs();

	trace_init_cmdlines();

	register_tracer(&no_tracer);
	current_trace = &no_tracer;

	/* All seems OK, enable tracing */
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
