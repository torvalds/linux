/*
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include "trace.h"

#define STACK_TRACE_ENTRIES 500

static unsigned long stack_dump_trace[STACK_TRACE_ENTRIES+1] =
	 { [0 ... (STACK_TRACE_ENTRIES)] = ULONG_MAX };
static unsigned stack_dump_index[STACK_TRACE_ENTRIES];

static struct stack_trace max_stack_trace = {
	.max_entries		= STACK_TRACE_ENTRIES,
	.entries		= stack_dump_trace,
};

static unsigned long max_stack_size;
static raw_spinlock_t max_stack_lock =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

static int stack_trace_disabled __read_mostly;
static DEFINE_PER_CPU(int, trace_active);

static inline void check_stack(void)
{
	unsigned long this_size, flags;
	unsigned long *p, *top, *start;
	int i;

	this_size = ((unsigned long)&this_size) & (THREAD_SIZE-1);
	this_size = THREAD_SIZE - this_size;

	if (this_size <= max_stack_size)
		return;

	/* we do not handle interrupt stacks yet */
	if (!object_is_on_stack(&this_size))
		return;

	raw_local_irq_save(flags);
	__raw_spin_lock(&max_stack_lock);

	/* a race could have already updated it */
	if (this_size <= max_stack_size)
		goto out;

	max_stack_size = this_size;

	max_stack_trace.nr_entries	= 0;
	max_stack_trace.skip		= 3;

	save_stack_trace(&max_stack_trace);

	/*
	 * Now find where in the stack these are.
	 */
	i = 0;
	start = &this_size;
	top = (unsigned long *)
		(((unsigned long)start & ~(THREAD_SIZE-1)) + THREAD_SIZE);

	/*
	 * Loop through all the entries. One of the entries may
	 * for some reason be missed on the stack, so we may
	 * have to account for them. If they are all there, this
	 * loop will only happen once. This code only takes place
	 * on a new max, so it is far from a fast path.
	 */
	while (i < max_stack_trace.nr_entries) {

		stack_dump_index[i] = this_size;
		p = start;

		for (; p < top && i < max_stack_trace.nr_entries; p++) {
			if (*p == stack_dump_trace[i]) {
				this_size = stack_dump_index[i++] =
					(top - p) * sizeof(unsigned long);
				/* Start the search from here */
				start = p + 1;
			}
		}

		i++;
	}

 out:
	__raw_spin_unlock(&max_stack_lock);
	raw_local_irq_restore(flags);
}

static void
stack_trace_call(unsigned long ip, unsigned long parent_ip)
{
	int cpu, resched;

	if (unlikely(!ftrace_enabled || stack_trace_disabled))
		return;

	resched = need_resched();
	preempt_disable_notrace();

	cpu = raw_smp_processor_id();
	/* no atomic needed, we only modify this variable by this cpu */
	if (per_cpu(trace_active, cpu)++ != 0)
		goto out;

	check_stack();

 out:
	per_cpu(trace_active, cpu)--;
	/* prevent recursion in schedule */
	if (resched)
		preempt_enable_no_resched_notrace();
	else
		preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = stack_trace_call,
};

static ssize_t
stack_max_size_read(struct file *filp, char __user *ubuf,
		    size_t count, loff_t *ppos)
{
	unsigned long *ptr = filp->private_data;
	char buf[64];
	int r;

	r = snprintf(buf, sizeof(buf), "%ld\n", *ptr);
	if (r > sizeof(buf))
		r = sizeof(buf);
	return simple_read_from_buffer(ubuf, count, ppos, buf, r);
}

static ssize_t
stack_max_size_write(struct file *filp, const char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	long *ptr = filp->private_data;
	unsigned long val, flags;
	char buf[64];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	raw_local_irq_save(flags);
	__raw_spin_lock(&max_stack_lock);
	*ptr = val;
	__raw_spin_unlock(&max_stack_lock);
	raw_local_irq_restore(flags);

	return count;
}

static struct file_operations stack_max_size_fops = {
	.open		= tracing_open_generic,
	.read		= stack_max_size_read,
	.write		= stack_max_size_write,
};

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	long i;

	(*pos)++;

	if (v == SEQ_START_TOKEN)
		i = 0;
	else {
		i = *(long *)v;
		i++;
	}

	if (i >= max_stack_trace.nr_entries ||
	    stack_dump_trace[i] == ULONG_MAX)
		return NULL;

	m->private = (void *)i;

	return &m->private;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	void *t = SEQ_START_TOKEN;
	loff_t l = 0;

	local_irq_disable();
	__raw_spin_lock(&max_stack_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (; t && l < *pos; t = t_next(m, t, &l))
		;

	return t;
}

static void t_stop(struct seq_file *m, void *p)
{
	__raw_spin_unlock(&max_stack_lock);
	local_irq_enable();
}

static int trace_lookup_stack(struct seq_file *m, long i)
{
	unsigned long addr = stack_dump_trace[i];
#ifdef CONFIG_KALLSYMS
	char str[KSYM_SYMBOL_LEN];

	sprint_symbol(str, addr);

	return seq_printf(m, "%s\n", str);
#else
	return seq_printf(m, "%p\n", (void*)addr);
#endif
}

static int t_show(struct seq_file *m, void *v)
{
	long i;
	int size;

	if (v == SEQ_START_TOKEN) {
		seq_printf(m, "        Depth   Size      Location"
			   "    (%d entries)\n"
			   "        -----   ----      --------\n",
			   max_stack_trace.nr_entries);
		return 0;
	}

	i = *(long *)v;

	if (i >= max_stack_trace.nr_entries ||
	    stack_dump_trace[i] == ULONG_MAX)
		return 0;

	if (i+1 == max_stack_trace.nr_entries ||
	    stack_dump_trace[i+1] == ULONG_MAX)
		size = stack_dump_index[i];
	else
		size = stack_dump_index[i] - stack_dump_index[i+1];

	seq_printf(m, "%3ld) %8d   %5d   ", i, stack_dump_index[i], size);

	trace_lookup_stack(m, i);

	return 0;
}

static struct seq_operations stack_trace_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int stack_trace_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &stack_trace_seq_ops);

	return ret;
}

static struct file_operations stack_trace_fops = {
	.open		= stack_trace_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

static __init int stack_trace_init(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("stack_max_size", 0644, d_tracer,
				    &max_stack_size, &stack_max_size_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'stack_max_size' entry\n");

	entry = debugfs_create_file("stack_trace", 0444, d_tracer,
				    NULL, &stack_trace_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'stack_trace' entry\n");

	register_ftrace_function(&trace_ops);

	return 0;
}

device_initcall(stack_trace_init);
