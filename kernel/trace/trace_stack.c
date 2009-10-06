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
#include <linux/sysctl.h>
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
static DEFINE_MUTEX(stack_sysctl_mutex);

int stack_tracer_enabled;
static int last_stack_tracer_enabled;

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

	local_irq_save(flags);
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
		int found = 0;

		stack_dump_index[i] = this_size;
		p = start;

		for (; p < top && i < max_stack_trace.nr_entries; p++) {
			if (*p == stack_dump_trace[i]) {
				this_size = stack_dump_index[i++] =
					(top - p) * sizeof(unsigned long);
				found = 1;
				/* Start the search from here */
				start = p + 1;
			}
		}

		if (!found)
			i++;
	}

 out:
	__raw_spin_unlock(&max_stack_lock);
	local_irq_restore(flags);
}

static void
stack_trace_call(unsigned long ip, unsigned long parent_ip)
{
	int cpu, resched;

	if (unlikely(!ftrace_enabled || stack_trace_disabled))
		return;

	resched = ftrace_preempt_disable();

	cpu = raw_smp_processor_id();
	/* no atomic needed, we only modify this variable by this cpu */
	if (per_cpu(trace_active, cpu)++ != 0)
		goto out;

	check_stack();

 out:
	per_cpu(trace_active, cpu)--;
	/* prevent recursion in schedule */
	ftrace_preempt_enable(resched);
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

	local_irq_save(flags);
	__raw_spin_lock(&max_stack_lock);
	*ptr = val;
	__raw_spin_unlock(&max_stack_lock);
	local_irq_restore(flags);

	return count;
}

static const struct file_operations stack_max_size_fops = {
	.open		= tracing_open_generic,
	.read		= stack_max_size_read,
	.write		= stack_max_size_write,
};

static void *
__next(struct seq_file *m, loff_t *pos)
{
	long n = *pos - 1;

	if (n >= max_stack_trace.nr_entries || stack_dump_trace[n] == ULONG_MAX)
		return NULL;

	m->private = (void *)n;
	return &m->private;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return __next(m, pos);
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	local_irq_disable();
	__raw_spin_lock(&max_stack_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return __next(m, pos);
}

static void t_stop(struct seq_file *m, void *p)
{
	__raw_spin_unlock(&max_stack_lock);
	local_irq_enable();
}

static int trace_lookup_stack(struct seq_file *m, long i)
{
	unsigned long addr = stack_dump_trace[i];

	return seq_printf(m, "%pF\n", (void *)addr);
}

static void print_disabled(struct seq_file *m)
{
	seq_puts(m, "#\n"
		 "#  Stack tracer disabled\n"
		 "#\n"
		 "# To enable the stack tracer, either add 'stacktrace' to the\n"
		 "# kernel command line\n"
		 "# or 'echo 1 > /proc/sys/kernel/stack_tracer_enabled'\n"
		 "#\n");
}

static int t_show(struct seq_file *m, void *v)
{
	long i;
	int size;

	if (v == SEQ_START_TOKEN) {
		seq_printf(m, "        Depth    Size   Location"
			   "    (%d entries)\n"
			   "        -----    ----   --------\n",
			   max_stack_trace.nr_entries - 1);

		if (!stack_tracer_enabled && !max_stack_size)
			print_disabled(m);

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

static const struct seq_operations stack_trace_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int stack_trace_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &stack_trace_seq_ops);
}

static const struct file_operations stack_trace_fops = {
	.open		= stack_trace_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

int
stack_trace_sysctl(struct ctl_table *table, int write,
		   void __user *buffer, size_t *lenp,
		   loff_t *ppos)
{
	int ret;

	mutex_lock(&stack_sysctl_mutex);

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (ret || !write ||
	    (last_stack_tracer_enabled == !!stack_tracer_enabled))
		goto out;

	last_stack_tracer_enabled = !!stack_tracer_enabled;

	if (stack_tracer_enabled)
		register_ftrace_function(&trace_ops);
	else
		unregister_ftrace_function(&trace_ops);

 out:
	mutex_unlock(&stack_sysctl_mutex);
	return ret;
}

static __init int enable_stacktrace(char *str)
{
	stack_tracer_enabled = 1;
	last_stack_tracer_enabled = 1;
	return 1;
}
__setup("stacktrace", enable_stacktrace);

static __init int stack_trace_init(void)
{
	struct dentry *d_tracer;

	d_tracer = tracing_init_dentry();

	trace_create_file("stack_max_size", 0644, d_tracer,
			&max_stack_size, &stack_max_size_fops);

	trace_create_file("stack_trace", 0444, d_tracer,
			NULL, &stack_trace_fops);

	if (stack_tracer_enabled)
		register_ftrace_function(&trace_ops);

	return 0;
}

device_initcall(stack_trace_init);
