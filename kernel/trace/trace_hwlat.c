/*
 * trace_hwlatdetect.c - A simple Hardware Latency detector.
 *
 * Use this tracer to detect large system latencies induced by the behavior of
 * certain underlying system hardware or firmware, independent of Linux itself.
 * The code was developed originally to detect the presence of SMIs on Intel
 * and AMD systems, although there is no dependency upon x86 herein.
 *
 * The classical example usage of this tracer is in detecting the presence of
 * SMIs or System Management Interrupts on Intel and AMD systems. An SMI is a
 * somewhat special form of hardware interrupt spawned from earlier CPU debug
 * modes in which the (BIOS/EFI/etc.) firmware arranges for the South Bridge
 * LPC (or other device) to generate a special interrupt under certain
 * circumstances, for example, upon expiration of a special SMI timer device,
 * due to certain external thermal readings, on certain I/O address accesses,
 * and other situations. An SMI hits a special CPU pin, triggers a special
 * SMI mode (complete with special memory map), and the OS is unaware.
 *
 * Although certain hardware-inducing latencies are necessary (for example,
 * a modern system often requires an SMI handler for correct thermal control
 * and remote management) they can wreak havoc upon any OS-level performance
 * guarantees toward low-latency, especially when the OS is not even made
 * aware of the presence of these interrupts. For this reason, we need a
 * somewhat brute force mechanism to detect these interrupts. In this case,
 * we do it by hogging all of the CPU(s) for configurable timer intervals,
 * sampling the built-in CPU timer, looking for discontiguous readings.
 *
 * WARNING: This implementation necessarily introduces latencies. Therefore,
 *          you should NEVER use this tracer while running in a production
 *          environment requiring any kind of low-latency performance
 *          guarantee(s).
 *
 * Copyright (C) 2008-2009 Jon Masters, Red Hat, Inc. <jcm@redhat.com>
 * Copyright (C) 2013-2016 Steven Rostedt, Red Hat, Inc. <srostedt@redhat.com>
 *
 * Includes useful feedback from Clark Williams <clark@redhat.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kthread.h>
#include <linux/tracefs.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include "trace.h"

static struct trace_array	*hwlat_trace;

#define U64STR_SIZE		22			/* 20 digits max */

#define BANNER			"hwlat_detector: "
#define DEFAULT_SAMPLE_WINDOW	1000000			/* 1s */
#define DEFAULT_SAMPLE_WIDTH	500000			/* 0.5s */
#define DEFAULT_LAT_THRESHOLD	10			/* 10us */

/* sampling thread*/
static struct task_struct *hwlat_kthread;

static struct dentry *hwlat_sample_width;	/* sample width us */
static struct dentry *hwlat_sample_window;	/* sample window us */

/* Save the previous tracing_thresh value */
static unsigned long save_tracing_thresh;

/* NMI timestamp counters */
static u64 nmi_ts_start;
static u64 nmi_total_ts;
static int nmi_count;
static int nmi_cpu;

/* Tells NMIs to call back to the hwlat tracer to record timestamps */
bool trace_hwlat_callback_enabled;

/* If the user changed threshold, remember it */
static u64 last_tracing_thresh = DEFAULT_LAT_THRESHOLD * NSEC_PER_USEC;

/* Individual latency samples are stored here when detected. */
struct hwlat_sample {
	u64			seqnum;		/* unique sequence */
	u64			duration;	/* delta */
	u64			outer_duration;	/* delta (outer loop) */
	u64			nmi_total_ts;	/* Total time spent in NMIs */
	struct timespec64	timestamp;	/* wall time */
	int			nmi_count;	/* # NMIs during this sample */
};

/* keep the global state somewhere. */
static struct hwlat_data {

	struct mutex lock;		/* protect changes */

	u64	count;			/* total since reset */

	u64	sample_window;		/* total sampling window (on+off) */
	u64	sample_width;		/* active sampling portion of window */

} hwlat_data = {
	.sample_window		= DEFAULT_SAMPLE_WINDOW,
	.sample_width		= DEFAULT_SAMPLE_WIDTH,
};

static void trace_hwlat_sample(struct hwlat_sample *sample)
{
	struct trace_array *tr = hwlat_trace;
	struct trace_event_call *call = &event_hwlat;
	struct ring_buffer *buffer = tr->trace_buffer.buffer;
	struct ring_buffer_event *event;
	struct hwlat_entry *entry;
	unsigned long flags;
	int pc;

	pc = preempt_count();
	local_save_flags(flags);

	event = trace_buffer_lock_reserve(buffer, TRACE_HWLAT, sizeof(*entry),
					  flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->seqnum			= sample->seqnum;
	entry->duration			= sample->duration;
	entry->outer_duration		= sample->outer_duration;
	entry->timestamp		= sample->timestamp;
	entry->nmi_total_ts		= sample->nmi_total_ts;
	entry->nmi_count		= sample->nmi_count;

	if (!call_filter_check_discard(call, entry, buffer, event))
		trace_buffer_unlock_commit_nostack(buffer, event);
}

/* Macros to encapsulate the time capturing infrastructure */
#define time_type	u64
#define time_get()	trace_clock_local()
#define time_to_us(x)	div_u64(x, 1000)
#define time_sub(a, b)	((a) - (b))
#define init_time(a, b)	(a = b)
#define time_u64(a)	a

void trace_hwlat_callback(bool enter)
{
	if (smp_processor_id() != nmi_cpu)
		return;

	/*
	 * Currently trace_clock_local() calls sched_clock() and the
	 * generic version is not NMI safe.
	 */
	if (!IS_ENABLED(CONFIG_GENERIC_SCHED_CLOCK)) {
		if (enter)
			nmi_ts_start = time_get();
		else
			nmi_total_ts = time_get() - nmi_ts_start;
	}

	if (enter)
		nmi_count++;
}

/**
 * get_sample - sample the CPU TSC and look for likely hardware latencies
 *
 * Used to repeatedly capture the CPU TSC (or similar), looking for potential
 * hardware-induced latency. Called with interrupts disabled and with
 * hwlat_data.lock held.
 */
static int get_sample(void)
{
	struct trace_array *tr = hwlat_trace;
	time_type start, t1, t2, last_t2;
	s64 diff, total, last_total = 0;
	u64 sample = 0;
	u64 thresh = tracing_thresh;
	u64 outer_sample = 0;
	int ret = -1;

	do_div(thresh, NSEC_PER_USEC); /* modifies interval value */

	nmi_cpu = smp_processor_id();
	nmi_total_ts = 0;
	nmi_count = 0;
	/* Make sure NMIs see this first */
	barrier();

	trace_hwlat_callback_enabled = true;

	init_time(last_t2, 0);
	start = time_get(); /* start timestamp */

	do {

		t1 = time_get();	/* we'll look for a discontinuity */
		t2 = time_get();

		if (time_u64(last_t2)) {
			/* Check the delta from outer loop (t2 to next t1) */
			diff = time_to_us(time_sub(t1, last_t2));
			/* This shouldn't happen */
			if (diff < 0) {
				pr_err(BANNER "time running backwards\n");
				goto out;
			}
			if (diff > outer_sample)
				outer_sample = diff;
		}
		last_t2 = t2;

		total = time_to_us(time_sub(t2, start)); /* sample width */

		/* Check for possible overflows */
		if (total < last_total) {
			pr_err("Time total overflowed\n");
			break;
		}
		last_total = total;

		/* This checks the inner loop (t1 to t2) */
		diff = time_to_us(time_sub(t2, t1));     /* current diff */

		/* This shouldn't happen */
		if (diff < 0) {
			pr_err(BANNER "time running backwards\n");
			goto out;
		}

		if (diff > sample)
			sample = diff; /* only want highest value */

	} while (total <= hwlat_data.sample_width);

	barrier(); /* finish the above in the view for NMIs */
	trace_hwlat_callback_enabled = false;
	barrier(); /* Make sure nmi_total_ts is no longer updated */

	ret = 0;

	/* If we exceed the threshold value, we have found a hardware latency */
	if (sample > thresh || outer_sample > thresh) {
		struct hwlat_sample s;

		ret = 1;

		/* We read in microseconds */
		if (nmi_total_ts)
			do_div(nmi_total_ts, NSEC_PER_USEC);

		hwlat_data.count++;
		s.seqnum = hwlat_data.count;
		s.duration = sample;
		s.outer_duration = outer_sample;
		ktime_get_real_ts64(&s.timestamp);
		s.nmi_total_ts = nmi_total_ts;
		s.nmi_count = nmi_count;
		trace_hwlat_sample(&s);

		/* Keep a running maximum ever recorded hardware latency */
		if (sample > tr->max_latency)
			tr->max_latency = sample;
	}

out:
	return ret;
}

static struct cpumask save_cpumask;
static bool disable_migrate;

static void move_to_next_cpu(void)
{
	struct cpumask *current_mask = &save_cpumask;
	int next_cpu;

	if (disable_migrate)
		return;
	/*
	 * If for some reason the user modifies the CPU affinity
	 * of this thread, than stop migrating for the duration
	 * of the current test.
	 */
	if (!cpumask_equal(current_mask, &current->cpus_allowed))
		goto disable;

	get_online_cpus();
	cpumask_and(current_mask, cpu_online_mask, tracing_buffer_mask);
	next_cpu = cpumask_next(smp_processor_id(), current_mask);
	put_online_cpus();

	if (next_cpu >= nr_cpu_ids)
		next_cpu = cpumask_first(current_mask);

	if (next_cpu >= nr_cpu_ids) /* Shouldn't happen! */
		goto disable;

	cpumask_clear(current_mask);
	cpumask_set_cpu(next_cpu, current_mask);

	sched_setaffinity(0, current_mask);
	return;

 disable:
	disable_migrate = true;
}

/*
 * kthread_fn - The CPU time sampling/hardware latency detection kernel thread
 *
 * Used to periodically sample the CPU TSC via a call to get_sample. We
 * disable interrupts, which does (intentionally) introduce latency since we
 * need to ensure nothing else might be running (and thus preempting).
 * Obviously this should never be used in production environments.
 *
 * Executes one loop interaction on each CPU in tracing_cpumask sysfs file.
 */
static int kthread_fn(void *data)
{
	u64 interval;

	while (!kthread_should_stop()) {

		move_to_next_cpu();

		local_irq_disable();
		get_sample();
		local_irq_enable();

		mutex_lock(&hwlat_data.lock);
		interval = hwlat_data.sample_window - hwlat_data.sample_width;
		mutex_unlock(&hwlat_data.lock);

		do_div(interval, USEC_PER_MSEC); /* modifies interval value */

		/* Always sleep for at least 1ms */
		if (interval < 1)
			interval = 1;

		if (msleep_interruptible(interval))
			break;
	}

	return 0;
}

/**
 * start_kthread - Kick off the hardware latency sampling/detector kthread
 *
 * This starts the kernel thread that will sit and sample the CPU timestamp
 * counter (TSC or similar) and look for potential hardware latencies.
 */
static int start_kthread(struct trace_array *tr)
{
	struct cpumask *current_mask = &save_cpumask;
	struct task_struct *kthread;
	int next_cpu;

	/* Just pick the first CPU on first iteration */
	current_mask = &save_cpumask;
	get_online_cpus();
	cpumask_and(current_mask, cpu_online_mask, tracing_buffer_mask);
	put_online_cpus();
	next_cpu = cpumask_first(current_mask);

	kthread = kthread_create(kthread_fn, NULL, "hwlatd");
	if (IS_ERR(kthread)) {
		pr_err(BANNER "could not start sampling thread\n");
		return -ENOMEM;
	}

	cpumask_clear(current_mask);
	cpumask_set_cpu(next_cpu, current_mask);
	sched_setaffinity(kthread->pid, current_mask);

	hwlat_kthread = kthread;
	wake_up_process(kthread);

	return 0;
}

/**
 * stop_kthread - Inform the hardware latency samping/detector kthread to stop
 *
 * This kicks the running hardware latency sampling/detector kernel thread and
 * tells it to stop sampling now. Use this on unload and at system shutdown.
 */
static void stop_kthread(void)
{
	if (!hwlat_kthread)
		return;
	kthread_stop(hwlat_kthread);
	hwlat_kthread = NULL;
}

/*
 * hwlat_read - Wrapper read function for reading both window and width
 * @filp: The active open file structure
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a generic read implementation for the global state
 * "hwlat_data" structure filesystem entries.
 */
static ssize_t hwlat_read(struct file *filp, char __user *ubuf,
			  size_t cnt, loff_t *ppos)
{
	char buf[U64STR_SIZE];
	u64 *entry = filp->private_data;
	u64 val;
	int len;

	if (!entry)
		return -EFAULT;

	if (cnt > sizeof(buf))
		cnt = sizeof(buf);

	val = *entry;

	len = snprintf(buf, sizeof(buf), "%llu\n", val);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

/**
 * hwlat_width_write - Write function for "width" entry
 * @filp: The active open file structure
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in @file
 *
 * This function provides a write implementation for the "width" interface
 * to the hardware latency detector. It can be used to configure
 * for how many us of the total window us we will actively sample for any
 * hardware-induced latency periods. Obviously, it is not possible to
 * sample constantly and have the system respond to a sample reader, or,
 * worse, without having the system appear to have gone out to lunch. It
 * is enforced that width is less that the total window size.
 */
static ssize_t
hwlat_width_write(struct file *filp, const char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	u64 val;
	int err;

	err = kstrtoull_from_user(ubuf, cnt, 10, &val);
	if (err)
		return err;

	mutex_lock(&hwlat_data.lock);
	if (val < hwlat_data.sample_window)
		hwlat_data.sample_width = val;
	else
		err = -EINVAL;
	mutex_unlock(&hwlat_data.lock);

	if (err)
		return err;

	return cnt;
}

/**
 * hwlat_window_write - Write function for "window" entry
 * @filp: The active open file structure
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in @file
 *
 * This function provides a write implementation for the "window" interface
 * to the hardware latency detetector. The window is the total time
 * in us that will be considered one sample period. Conceptually, windows
 * occur back-to-back and contain a sample width period during which
 * actual sampling occurs. Can be used to write a new total window size. It
 * is enfoced that any value written must be greater than the sample width
 * size, or an error results.
 */
static ssize_t
hwlat_window_write(struct file *filp, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	u64 val;
	int err;

	err = kstrtoull_from_user(ubuf, cnt, 10, &val);
	if (err)
		return err;

	mutex_lock(&hwlat_data.lock);
	if (hwlat_data.sample_width < val)
		hwlat_data.sample_window = val;
	else
		err = -EINVAL;
	mutex_unlock(&hwlat_data.lock);

	if (err)
		return err;

	return cnt;
}

static const struct file_operations width_fops = {
	.open		= tracing_open_generic,
	.read		= hwlat_read,
	.write		= hwlat_width_write,
};

static const struct file_operations window_fops = {
	.open		= tracing_open_generic,
	.read		= hwlat_read,
	.write		= hwlat_window_write,
};

/**
 * init_tracefs - A function to initialize the tracefs interface files
 *
 * This function creates entries in tracefs for "hwlat_detector".
 * It creates the hwlat_detector directory in the tracing directory,
 * and within that directory is the count, width and window files to
 * change and view those values.
 */
static int init_tracefs(void)
{
	struct dentry *d_tracer;
	struct dentry *top_dir;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return -ENOMEM;

	top_dir = tracefs_create_dir("hwlat_detector", d_tracer);
	if (!top_dir)
		return -ENOMEM;

	hwlat_sample_window = tracefs_create_file("window", 0640,
						  top_dir,
						  &hwlat_data.sample_window,
						  &window_fops);
	if (!hwlat_sample_window)
		goto err;

	hwlat_sample_width = tracefs_create_file("width", 0644,
						 top_dir,
						 &hwlat_data.sample_width,
						 &width_fops);
	if (!hwlat_sample_width)
		goto err;

	return 0;

 err:
	tracefs_remove_recursive(top_dir);
	return -ENOMEM;
}

static void hwlat_tracer_start(struct trace_array *tr)
{
	int err;

	err = start_kthread(tr);
	if (err)
		pr_err(BANNER "Cannot start hwlat kthread\n");
}

static void hwlat_tracer_stop(struct trace_array *tr)
{
	stop_kthread();
}

static bool hwlat_busy;

static int hwlat_tracer_init(struct trace_array *tr)
{
	/* Only allow one instance to enable this */
	if (hwlat_busy)
		return -EBUSY;

	hwlat_trace = tr;

	disable_migrate = false;
	hwlat_data.count = 0;
	tr->max_latency = 0;
	save_tracing_thresh = tracing_thresh;

	/* tracing_thresh is in nsecs, we speak in usecs */
	if (!tracing_thresh)
		tracing_thresh = last_tracing_thresh;

	if (tracer_tracing_is_on(tr))
		hwlat_tracer_start(tr);

	hwlat_busy = true;

	return 0;
}

static void hwlat_tracer_reset(struct trace_array *tr)
{
	stop_kthread();

	/* the tracing threshold is static between runs */
	last_tracing_thresh = tracing_thresh;

	tracing_thresh = save_tracing_thresh;
	hwlat_busy = false;
}

static struct tracer hwlat_tracer __read_mostly =
{
	.name		= "hwlat",
	.init		= hwlat_tracer_init,
	.reset		= hwlat_tracer_reset,
	.start		= hwlat_tracer_start,
	.stop		= hwlat_tracer_stop,
	.allow_instances = true,
};

__init static int init_hwlat_tracer(void)
{
	int ret;

	mutex_init(&hwlat_data.lock);

	ret = register_tracer(&hwlat_tracer);
	if (ret)
		return ret;

	init_tracefs();

	return 0;
}
late_initcall(init_hwlat_tracer);
