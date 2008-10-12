/*
 * trace stack traces
 *
 * Copyright (C) 2004-2008, Soeren Sandmann
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 */
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/fs.h>

#include <asm/stacktrace.h>

#include "trace.h"

static struct trace_array	*sysprof_trace;
static int __read_mostly	tracer_enabled;

/*
 * 1 msec sample interval by default:
 */
static unsigned long sample_period = 1000000;
static const unsigned int sample_max_depth = 512;

static DEFINE_MUTEX(sample_timer_lock);
/*
 * Per CPU hrtimers that do the profiling:
 */
static DEFINE_PER_CPU(struct hrtimer, stack_trace_hrtimer);

struct stack_frame {
	const void __user	*next_fp;
	unsigned long		return_address;
};

static int copy_stack_frame(const void __user *fp, struct stack_frame *frame)
{
	int ret;

	if (!access_ok(VERIFY_READ, fp, sizeof(*frame)))
		return 0;

	ret = 1;
	pagefault_disable();
	if (__copy_from_user_inatomic(frame, fp, sizeof(*frame)))
		ret = 0;
	pagefault_enable();

	return ret;
}

struct backtrace_info {
	struct trace_array_cpu	*data;
	struct trace_array	*tr;
	int			pos;
};

static void
backtrace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	/* Ignore warnings */
}

static void backtrace_warning(void *data, char *msg)
{
	/* Ignore warnings */
}

static int backtrace_stack(void *data, char *name)
{
	/* Don't bother with IRQ stacks for now */
	return -1;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct backtrace_info *info = data;

	if (info->pos < sample_max_depth && reliable) {
		__trace_special(info->tr, info->data, 1, addr, 0);

		info->pos++;
	}
}

const static struct stacktrace_ops backtrace_ops = {
	.warning		= backtrace_warning,
	.warning_symbol		= backtrace_warning_symbol,
	.stack			= backtrace_stack,
	.address		= backtrace_address,
};

static int
trace_kernel(struct pt_regs *regs, struct trace_array *tr,
	     struct trace_array_cpu *data)
{
	struct backtrace_info info;
	unsigned long bp;
	char *stack;

	info.tr = tr;
	info.data = data;
	info.pos = 1;

	__trace_special(info.tr, info.data, 1, regs->ip, 0);

	stack = ((char *)regs + sizeof(struct pt_regs));
#ifdef CONFIG_FRAME_POINTER
	bp = regs->bp;
#else
	bp = 0;
#endif

	dump_trace(NULL, regs, (void *)stack, bp, &backtrace_ops, &info);

	return info.pos;
}

static void timer_notify(struct pt_regs *regs, int cpu)
{
	struct trace_array_cpu *data;
	struct stack_frame frame;
	struct trace_array *tr;
	const void __user *fp;
	int is_user;
	int i;

	if (!regs)
		return;

	tr = sysprof_trace;
	data = tr->data[cpu];
	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return;

	if (is_user && current->state != TASK_RUNNING)
		return;

	__trace_special(tr, data, 0, 0, current->pid);

	if (!is_user)
		i = trace_kernel(regs, tr, data);
	else
		i = 0;

	/*
	 * Trace user stack if we are not a kernel thread
	 */
	if (current->mm && i < sample_max_depth) {
		regs = (struct pt_regs *)current->thread.sp0 - 1;

		fp = (void __user *)regs->bp;

		__trace_special(tr, data, 2, regs->ip, 0);

		while (i < sample_max_depth) {
			frame.next_fp = NULL;
			frame.return_address = 0;
			if (!copy_stack_frame(fp, &frame))
				break;
			if ((unsigned long)fp < regs->sp)
				break;

			__trace_special(tr, data, 2, frame.return_address,
					(unsigned long)fp);
			fp = frame.next_fp;

			i++;
		}

	}

	/*
	 * Special trace entry if we overflow the max depth:
	 */
	if (i == sample_max_depth)
		__trace_special(tr, data, -1, -1, -1);

	__trace_special(tr, data, 3, current->pid, i);
}

static enum hrtimer_restart stack_trace_timer_fn(struct hrtimer *hrtimer)
{
	/* trace here */
	timer_notify(get_irq_regs(), smp_processor_id());

	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));

	return HRTIMER_RESTART;
}

static void start_stack_timer(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(stack_trace_hrtimer, cpu);

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = stack_trace_timer_fn;
	hrtimer->cb_mode = HRTIMER_CB_IRQSAFE_PERCPU;

	hrtimer_start(hrtimer, ns_to_ktime(sample_period), HRTIMER_MODE_REL);
}

static void start_stack_timers(void)
{
	cpumask_t saved_mask = current->cpus_allowed;
	int cpu;

	for_each_online_cpu(cpu) {
		set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
		start_stack_timer(cpu);
	}
	set_cpus_allowed_ptr(current, &saved_mask);
}

static void stop_stack_timer(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(stack_trace_hrtimer, cpu);

	hrtimer_cancel(hrtimer);
}

static void stop_stack_timers(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		stop_stack_timer(cpu);
}

static void stack_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static void start_stack_trace(struct trace_array *tr)
{
	mutex_lock(&sample_timer_lock);
	stack_reset(tr);
	start_stack_timers();
	tracer_enabled = 1;
	mutex_unlock(&sample_timer_lock);
}

static void stop_stack_trace(struct trace_array *tr)
{
	mutex_lock(&sample_timer_lock);
	stop_stack_timers();
	tracer_enabled = 0;
	mutex_unlock(&sample_timer_lock);
}

static void stack_trace_init(struct trace_array *tr)
{
	sysprof_trace = tr;

	if (tr->ctrl)
		start_stack_trace(tr);
}

static void stack_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_stack_trace(tr);
}

static void stack_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_stack_trace(tr);
	else
		stop_stack_trace(tr);
}

static struct tracer stack_trace __read_mostly =
{
	.name		= "sysprof",
	.init		= stack_trace_init,
	.reset		= stack_trace_reset,
	.ctrl_update	= stack_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sysprof,
#endif
};

__init static int init_stack_trace(void)
{
	return register_tracer(&stack_trace);
}
device_initcall(init_stack_trace);

#define MAX_LONG_DIGITS 22

static ssize_t
sysprof_sample_read(struct file *filp, char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	char buf[MAX_LONG_DIGITS];
	int r;

	r = sprintf(buf, "%ld\n", nsecs_to_usecs(sample_period));

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
sysprof_sample_write(struct file *filp, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	char buf[MAX_LONG_DIGITS];
	unsigned long val;

	if (cnt > MAX_LONG_DIGITS-1)
		cnt = MAX_LONG_DIGITS-1;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	val = simple_strtoul(buf, NULL, 10);
	/*
	 * Enforce a minimum sample period of 100 usecs:
	 */
	if (val < 100)
		val = 100;

	mutex_lock(&sample_timer_lock);
	stop_stack_timers();
	sample_period = val * 1000;
	start_stack_timers();
	mutex_unlock(&sample_timer_lock);

	return cnt;
}

static struct file_operations sysprof_sample_fops = {
	.read		= sysprof_sample_read,
	.write		= sysprof_sample_write,
};

void init_tracer_sysprof_debugfs(struct dentry *d_tracer)
{
	struct dentry *entry;

	entry = debugfs_create_file("sysprof_sample_period", 0644,
			d_tracer, NULL, &sysprof_sample_fops);
	if (entry)
		return;
	pr_warning("Could not create debugfs 'dyn_ftrace_total_info' entry\n");
}
