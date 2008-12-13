/*
 * unlikely profiler
 *
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 */
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/irqflags.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/hash.h>
#include <linux/fs.h>
#include <asm/local.h>
#include "trace.h"

#ifdef CONFIG_BRANCH_TRACER

static int branch_tracing_enabled __read_mostly;
static DEFINE_MUTEX(branch_tracing_mutex);
static struct trace_array *branch_tracer;

static void
probe_likely_condition(struct ftrace_branch_data *f, int val, int expect)
{
	struct trace_array *tr = branch_tracer;
	struct ring_buffer_event *event;
	struct trace_branch *entry;
	unsigned long flags, irq_flags;
	int cpu, pc;
	const char *p;

	/*
	 * I would love to save just the ftrace_likely_data pointer, but
	 * this code can also be used by modules. Ugly things can happen
	 * if the module is unloaded, and then we go and read the
	 * pointer.  This is slower, but much safer.
	 */

	if (unlikely(!tr))
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	if (atomic_inc_return(&tr->data[cpu]->disabled) != 1)
		goto out;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		goto out;

	pc = preempt_count();
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, flags, pc);
	entry->ent.type		= TRACE_BRANCH;

	/* Strip off the path, only save the file */
	p = f->file + strlen(f->file);
	while (p >= f->file && *p != '/')
		p--;
	p++;

	strncpy(entry->func, f->func, TRACE_FUNC_SIZE);
	strncpy(entry->file, p, TRACE_FILE_SIZE);
	entry->func[TRACE_FUNC_SIZE] = 0;
	entry->file[TRACE_FILE_SIZE] = 0;
	entry->line = f->line;
	entry->correct = val == expect;

	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);

 out:
	atomic_dec(&tr->data[cpu]->disabled);
	local_irq_restore(flags);
}

static inline
void trace_likely_condition(struct ftrace_branch_data *f, int val, int expect)
{
	if (!branch_tracing_enabled)
		return;

	probe_likely_condition(f, val, expect);
}

int enable_branch_tracing(struct trace_array *tr)
{
	int ret = 0;

	mutex_lock(&branch_tracing_mutex);
	branch_tracer = tr;
	/*
	 * Must be seen before enabling. The reader is a condition
	 * where we do not need a matching rmb()
	 */
	smp_wmb();
	branch_tracing_enabled++;
	mutex_unlock(&branch_tracing_mutex);

	return ret;
}

void disable_branch_tracing(void)
{
	mutex_lock(&branch_tracing_mutex);

	if (!branch_tracing_enabled)
		goto out_unlock;

	branch_tracing_enabled--;

 out_unlock:
	mutex_unlock(&branch_tracing_mutex);
}

static void start_branch_trace(struct trace_array *tr)
{
	enable_branch_tracing(tr);
}

static void stop_branch_trace(struct trace_array *tr)
{
	disable_branch_tracing();
}

static int branch_trace_init(struct trace_array *tr)
{
	int cpu;

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);

	start_branch_trace(tr);
	return 0;
}

static void branch_trace_reset(struct trace_array *tr)
{
	stop_branch_trace(tr);
}

struct tracer branch_trace __read_mostly =
{
	.name		= "branch",
	.init		= branch_trace_init,
	.reset		= branch_trace_reset,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_branch,
#endif
};

__init static int init_branch_trace(void)
{
	return register_tracer(&branch_trace);
}

device_initcall(init_branch_trace);
#else
static inline
void trace_likely_condition(struct ftrace_branch_data *f, int val, int expect)
{
}
#endif /* CONFIG_BRANCH_TRACER */

void ftrace_likely_update(struct ftrace_branch_data *f, int val, int expect)
{
	/*
	 * I would love to have a trace point here instead, but the
	 * trace point code is so inundated with unlikely and likely
	 * conditions that the recursive nightmare that exists is too
	 * much to try to get working. At least for now.
	 */
	trace_likely_condition(f, val, expect);

	/* FIXME: Make this atomic! */
	if (val == expect)
		f->correct++;
	else
		f->incorrect++;
}
EXPORT_SYMBOL(ftrace_likely_update);

struct ftrace_pointer {
	void		*start;
	void		*stop;
	int		hit;
};

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	const struct ftrace_pointer *f = m->private;
	struct ftrace_branch_data *p = v;

	(*pos)++;

	if (v == (void *)1)
		return f->start;

	++p;

	if ((void *)p >= (void *)f->stop)
		return NULL;

	return p;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	void *t = (void *)1;
	loff_t l = 0;

	for (; t && l < *pos; t = t_next(m, t, &l))
		;

	return t;
}

static void t_stop(struct seq_file *m, void *p)
{
}

static int t_show(struct seq_file *m, void *v)
{
	const struct ftrace_pointer *fp = m->private;
	struct ftrace_branch_data *p = v;
	const char *f;
	long percent;

	if (v == (void *)1) {
		if (fp->hit)
			seq_printf(m, "   miss      hit    %% ");
		else
			seq_printf(m, " correct incorrect  %% ");
		seq_printf(m, "       Function                "
			      "  File              Line\n"
			      " ------- ---------  - "
			      "       --------                "
			      "  ----              ----\n");
		return 0;
	}

	/* Only print the file, not the path */
	f = p->file + strlen(p->file);
	while (f >= p->file && *f != '/')
		f--;
	f++;

	/*
	 * The miss is overlayed on correct, and hit on incorrect.
	 */
	if (p->correct) {
		percent = p->incorrect * 100;
		percent /= p->correct + p->incorrect;
	} else
		percent = p->incorrect ? 100 : -1;

	seq_printf(m, "%8lu %8lu ",  p->correct, p->incorrect);
	if (percent < 0)
		seq_printf(m, "  X ");
	else
		seq_printf(m, "%3ld ", percent);
	seq_printf(m, "%-30.30s %-20.20s %d\n", p->func, f, p->line);
	return 0;
}

static struct seq_operations tracing_likely_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int tracing_branch_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &tracing_likely_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = (void *)inode->i_private;
	}

	return ret;
}

static const struct file_operations tracing_branch_fops = {
	.open		= tracing_branch_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

#ifdef CONFIG_PROFILE_ALL_BRANCHES
extern unsigned long __start_branch_profile[];
extern unsigned long __stop_branch_profile[];

static const struct ftrace_pointer ftrace_branch_pos = {
	.start			= __start_branch_profile,
	.stop			= __stop_branch_profile,
	.hit			= 1,
};

#endif /* CONFIG_PROFILE_ALL_BRANCHES */

extern unsigned long __start_annotated_branch_profile[];
extern unsigned long __stop_annotated_branch_profile[];

static const struct ftrace_pointer ftrace_annotated_branch_pos = {
	.start			= __start_annotated_branch_profile,
	.stop			= __stop_annotated_branch_profile,
};

static __init int ftrace_branch_init(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("profile_annotated_branch", 0444, d_tracer,
				    (void *)&ftrace_annotated_branch_pos,
				    &tracing_branch_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'profile_annotatet_branch' entry\n");

#ifdef CONFIG_PROFILE_ALL_BRANCHES
	entry = debugfs_create_file("profile_branch", 0444, d_tracer,
				    (void *)&ftrace_branch_pos,
				    &tracing_branch_fops);
	if (!entry)
		pr_warning("Could not create debugfs"
			   " 'profile_branch' entry\n");
#endif

	return 0;
}

device_initcall(ftrace_branch_init);
