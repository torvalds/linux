// SPDX-License-Identifier: GPL-2.0
/*
 * trace_hwlat.c - A simple Hardware Latency detector.
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
 * Includes useful feedback from Clark Williams <williams@redhat.com>
 *
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

static struct dentry *hwlat_sample_width;	/* sample width us */
static struct dentry *hwlat_sample_window;	/* sample window us */
static struct dentry *hwlat_thread_mode;	/* hwlat thread mode */

enum {
	MODE_NONE = 0,
	MODE_ROUND_ROBIN,
	MODE_PER_CPU,
	MODE_MAX
};
static char *thread_mode_str[] = { "none", "round-robin", "per-cpu" };

/* Save the previous tracing_thresh value */
static unsigned long save_tracing_thresh;

/* runtime kthread data */
struct hwlat_kthread_data {
	struct task_struct	*kthread;
	/* NMI timestamp counters */
	u64			nmi_ts_start;
	u64			nmi_total_ts;
	int			nmi_count;
	int			nmi_cpu;
};

static struct hwlat_kthread_data hwlat_single_cpu_data;
static DEFINE_PER_CPU(struct hwlat_kthread_data, hwlat_per_cpu_data);

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
	int			count;		/* # of iterations over thresh */
};

/* keep the global state somewhere. */
static struct hwlat_data {

	struct mutex lock;		/* protect changes */

	u64	count;			/* total since reset */

	u64	sample_window;		/* total sampling window (on+off) */
	u64	sample_width;		/* active sampling portion of window */

	int	thread_mode;		/* thread mode */

} hwlat_data = {
	.sample_window		= DEFAULT_SAMPLE_WINDOW,
	.sample_width		= DEFAULT_SAMPLE_WIDTH,
	.thread_mode		= MODE_ROUND_ROBIN
};

static struct hwlat_kthread_data *get_cpu_data(void)
{
	if (hwlat_data.thread_mode == MODE_PER_CPU)
		return this_cpu_ptr(&hwlat_per_cpu_data);
	else
		return &hwlat_single_cpu_data;
}

static bool hwlat_busy;

static void trace_hwlat_sample(struct hwlat_sample *sample)
{
	struct trace_array *tr = hwlat_trace;
	struct trace_event_call *call = &event_hwlat;
	struct trace_buffer *buffer = tr->array_buffer.buffer;
	struct ring_buffer_event *event;
	struct hwlat_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_HWLAT, sizeof(*entry),
					  tracing_gen_ctx());
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->seqnum			= sample->seqnum;
	entry->duration			= sample->duration;
	entry->outer_duration		= sample->outer_duration;
	entry->timestamp		= sample->timestamp;
	entry->nmi_total_ts		= sample->nmi_total_ts;
	entry->nmi_count		= sample->nmi_count;
	entry->count			= sample->count;

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
	struct hwlat_kthread_data *kdata = get_cpu_data();

	if (!kdata->kthread)
		return;

	/*
	 * Currently trace_clock_local() calls sched_clock() and the
	 * generic version is not NMI safe.
	 */
	if (!IS_ENABLED(CONFIG_GENERIC_SCHED_CLOCK)) {
		if (enter)
			kdata->nmi_ts_start = time_get();
		else
			kdata->nmi_total_ts += time_get() - kdata->nmi_ts_start;
	}

	if (enter)
		kdata->nmi_count++;
}

/*
 * hwlat_err - report a hwlat error.
 */
#define hwlat_err(msg) ({							\
	struct trace_array *tr = hwlat_trace;					\
										\
	trace_array_printk_buf(tr->array_buffer.buffer, _THIS_IP_, msg);	\
})

/**
 * get_sample - sample the CPU TSC and look for likely hardware latencies
 *
 * Used to repeatedly capture the CPU TSC (or similar), looking for potential
 * hardware-induced latency. Called with interrupts disabled and with
 * hwlat_data.lock held.
 */
static int get_sample(void)
{
	struct hwlat_kthread_data *kdata = get_cpu_data();
	struct trace_array *tr = hwlat_trace;
	struct hwlat_sample s;
	time_type start, t1, t2, last_t2;
	s64 diff, outer_diff, total, last_total = 0;
	u64 sample = 0;
	u64 thresh = tracing_thresh;
	u64 outer_sample = 0;
	int ret = -1;
	unsigned int count = 0;

	do_div(thresh, NSEC_PER_USEC); /* modifies interval value */

	kdata->nmi_total_ts = 0;
	kdata->nmi_count = 0;
	/* Make sure NMIs see this first */
	barrier();

	trace_hwlat_callback_enabled = true;

	init_time(last_t2, 0);
	start = time_get(); /* start timestamp */
	outer_diff = 0;

	do {

		t1 = time_get();	/* we'll look for a discontinuity */
		t2 = time_get();

		if (time_u64(last_t2)) {
			/* Check the delta from outer loop (t2 to next t1) */
			outer_diff = time_to_us(time_sub(t1, last_t2));
			/* This shouldn't happen */
			if (outer_diff < 0) {
				hwlat_err(BANNER "time running backwards\n");
				goto out;
			}
			if (outer_diff > outer_sample)
				outer_sample = outer_diff;
		}
		last_t2 = t2;

		total = time_to_us(time_sub(t2, start)); /* sample width */

		/* Check for possible overflows */
		if (total < last_total) {
			hwlat_err("Time total overflowed\n");
			break;
		}
		last_total = total;

		/* This checks the inner loop (t1 to t2) */
		diff = time_to_us(time_sub(t2, t1));     /* current diff */

		if (diff > thresh || outer_diff > thresh) {
			if (!count)
				ktime_get_real_ts64(&s.timestamp);
			count++;
		}

		/* This shouldn't happen */
		if (diff < 0) {
			hwlat_err(BANNER "time running backwards\n");
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
		u64 latency;

		ret = 1;

		/* We read in microseconds */
		if (kdata->nmi_total_ts)
			do_div(kdata->nmi_total_ts, NSEC_PER_USEC);

		hwlat_data.count++;
		s.seqnum = hwlat_data.count;
		s.duration = sample;
		s.outer_duration = outer_sample;
		s.nmi_total_ts = kdata->nmi_total_ts;
		s.nmi_count = kdata->nmi_count;
		s.count = count;
		trace_hwlat_sample(&s);

		latency = max(sample, outer_sample);

		/* Keep a running maximum ever recorded hardware latency */
		if (latency > tr->max_latency) {
			tr->max_latency = latency;
			latency_fsnotify(tr);
		}
	}

out:
	return ret;
}

static struct cpumask save_cpumask;

static void move_to_next_cpu(void)
{
	struct cpumask *current_mask = &save_cpumask;
	struct trace_array *tr = hwlat_trace;
	int next_cpu;

	/*
	 * If for some reason the user modifies the CPU affinity
	 * of this thread, then stop migrating for the duration
	 * of the current test.
	 */
	if (!cpumask_equal(current_mask, current->cpus_ptr))
		goto change_mode;

	cpus_read_lock();
	cpumask_and(current_mask, cpu_online_mask, tr->tracing_cpumask);
	next_cpu = cpumask_next(raw_smp_processor_id(), current_mask);
	cpus_read_unlock();

	if (next_cpu >= nr_cpu_ids)
		next_cpu = cpumask_first(current_mask);

	if (next_cpu >= nr_cpu_ids) /* Shouldn't happen! */
		goto change_mode;

	cpumask_clear(current_mask);
	cpumask_set_cpu(next_cpu, current_mask);

	sched_setaffinity(0, current_mask);
	return;

 change_mode:
	hwlat_data.thread_mode = MODE_NONE;
	pr_info(BANNER "cpumask changed while in round-robin mode, switching to mode none\n");
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

		if (hwlat_data.thread_mode == MODE_ROUND_ROBIN)
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

/*
 * stop_stop_kthread - Inform the hardware latency sampling/detector kthread to stop
 *
 * This kicks the running hardware latency sampling/detector kernel thread and
 * tells it to stop sampling now. Use this on unload and at system shutdown.
 */
static void stop_single_kthread(void)
{
	struct hwlat_kthread_data *kdata = get_cpu_data();
	struct task_struct *kthread;

	cpus_read_lock();
	kthread = kdata->kthread;

	if (!kthread)
		goto out_put_cpus;

	kthread_stop(kthread);
	kdata->kthread = NULL;

out_put_cpus:
	cpus_read_unlock();
}


/*
 * start_single_kthread - Kick off the hardware latency sampling/detector kthread
 *
 * This starts the kernel thread that will sit and sample the CPU timestamp
 * counter (TSC or similar) and look for potential hardware latencies.
 */
static int start_single_kthread(struct trace_array *tr)
{
	struct hwlat_kthread_data *kdata = get_cpu_data();
	struct cpumask *current_mask = &save_cpumask;
	struct task_struct *kthread;
	int next_cpu;

	cpus_read_lock();
	if (kdata->kthread)
		goto out_put_cpus;

	kthread = kthread_create(kthread_fn, NULL, "hwlatd");
	if (IS_ERR(kthread)) {
		pr_err(BANNER "could not start sampling thread\n");
		cpus_read_unlock();
		return -ENOMEM;
	}

	/* Just pick the first CPU on first iteration */
	cpumask_and(current_mask, cpu_online_mask, tr->tracing_cpumask);

	if (hwlat_data.thread_mode == MODE_ROUND_ROBIN) {
		next_cpu = cpumask_first(current_mask);
		cpumask_clear(current_mask);
		cpumask_set_cpu(next_cpu, current_mask);

	}

	sched_setaffinity(kthread->pid, current_mask);

	kdata->kthread = kthread;
	wake_up_process(kthread);

out_put_cpus:
	cpus_read_unlock();
	return 0;
}

/*
 * stop_cpu_kthread - Stop a hwlat cpu kthread
 */
static void stop_cpu_kthread(unsigned int cpu)
{
	struct task_struct *kthread;

	kthread = per_cpu(hwlat_per_cpu_data, cpu).kthread;
	if (kthread)
		kthread_stop(kthread);
	per_cpu(hwlat_per_cpu_data, cpu).kthread = NULL;
}

/*
 * stop_per_cpu_kthreads - Inform the hardware latency sampling/detector kthread to stop
 *
 * This kicks the running hardware latency sampling/detector kernel threads and
 * tells it to stop sampling now. Use this on unload and at system shutdown.
 */
static void stop_per_cpu_kthreads(void)
{
	unsigned int cpu;

	cpus_read_lock();
	for_each_online_cpu(cpu)
		stop_cpu_kthread(cpu);
	cpus_read_unlock();
}

/*
 * start_cpu_kthread - Start a hwlat cpu kthread
 */
static int start_cpu_kthread(unsigned int cpu)
{
	struct task_struct *kthread;
	char comm[24];

	snprintf(comm, 24, "hwlatd/%d", cpu);

	kthread = kthread_create_on_cpu(kthread_fn, NULL, cpu, comm);
	if (IS_ERR(kthread)) {
		pr_err(BANNER "could not start sampling thread\n");
		return -ENOMEM;
	}

	per_cpu(hwlat_per_cpu_data, cpu).kthread = kthread;
	wake_up_process(kthread);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void hwlat_hotplug_workfn(struct work_struct *dummy)
{
	struct trace_array *tr = hwlat_trace;
	unsigned int cpu = smp_processor_id();

	mutex_lock(&trace_types_lock);
	mutex_lock(&hwlat_data.lock);
	cpus_read_lock();

	if (!hwlat_busy || hwlat_data.thread_mode != MODE_PER_CPU)
		goto out_unlock;

	if (!cpumask_test_cpu(cpu, tr->tracing_cpumask))
		goto out_unlock;

	start_cpu_kthread(cpu);

out_unlock:
	cpus_read_unlock();
	mutex_unlock(&hwlat_data.lock);
	mutex_unlock(&trace_types_lock);
}

static DECLARE_WORK(hwlat_hotplug_work, hwlat_hotplug_workfn);

/*
 * hwlat_cpu_init - CPU hotplug online callback function
 */
static int hwlat_cpu_init(unsigned int cpu)
{
	schedule_work_on(cpu, &hwlat_hotplug_work);
	return 0;
}

/*
 * hwlat_cpu_die - CPU hotplug offline callback function
 */
static int hwlat_cpu_die(unsigned int cpu)
{
	stop_cpu_kthread(cpu);
	return 0;
}

static void hwlat_init_hotplug_support(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "trace/hwlat:online",
				hwlat_cpu_init, hwlat_cpu_die);
	if (ret < 0)
		pr_warn(BANNER "Error to init cpu hotplug support\n");

	return;
}
#else /* CONFIG_HOTPLUG_CPU */
static void hwlat_init_hotplug_support(void)
{
	return;
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * start_per_cpu_kthreads - Kick off the hardware latency sampling/detector kthreads
 *
 * This starts the kernel threads that will sit on potentially all cpus and
 * sample the CPU timestamp counter (TSC or similar) and look for potential
 * hardware latencies.
 */
static int start_per_cpu_kthreads(struct trace_array *tr)
{
	struct cpumask *current_mask = &save_cpumask;
	unsigned int cpu;
	int retval;

	cpus_read_lock();
	/*
	 * Run only on CPUs in which hwlat is allowed to run.
	 */
	cpumask_and(current_mask, cpu_online_mask, tr->tracing_cpumask);

	for_each_online_cpu(cpu)
		per_cpu(hwlat_per_cpu_data, cpu).kthread = NULL;

	for_each_cpu(cpu, current_mask) {
		retval = start_cpu_kthread(cpu);
		if (retval)
			goto out_error;
	}
	cpus_read_unlock();

	return 0;

out_error:
	cpus_read_unlock();
	stop_per_cpu_kthreads();
	return retval;
}

static void *s_mode_start(struct seq_file *s, loff_t *pos)
{
	int mode = *pos;

	mutex_lock(&hwlat_data.lock);

	if (mode >= MODE_MAX)
		return NULL;

	return pos;
}

static void *s_mode_next(struct seq_file *s, void *v, loff_t *pos)
{
	int mode = ++(*pos);

	if (mode >= MODE_MAX)
		return NULL;

	return pos;
}

static int s_mode_show(struct seq_file *s, void *v)
{
	loff_t *pos = v;
	int mode = *pos;

	if (mode == hwlat_data.thread_mode)
		seq_printf(s, "[%s]", thread_mode_str[mode]);
	else
		seq_printf(s, "%s", thread_mode_str[mode]);

	if (mode != MODE_MAX)
		seq_puts(s, " ");

	return 0;
}

static void s_mode_stop(struct seq_file *s, void *v)
{
	seq_puts(s, "\n");
	mutex_unlock(&hwlat_data.lock);
}

static const struct seq_operations thread_mode_seq_ops = {
	.start		= s_mode_start,
	.next		= s_mode_next,
	.show		= s_mode_show,
	.stop		= s_mode_stop
};

static int hwlat_mode_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &thread_mode_seq_ops);
};

static void hwlat_tracer_start(struct trace_array *tr);
static void hwlat_tracer_stop(struct trace_array *tr);

/**
 * hwlat_mode_write - Write function for "mode" entry
 * @filp: The active open file structure
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in @file
 *
 * This function provides a write implementation for the "mode" interface
 * to the hardware latency detector. hwlatd has different operation modes.
 * The "none" sets the allowed cpumask for a single hwlatd thread at the
 * startup and lets the scheduler handle the migration. The default mode is
 * the "round-robin" one, in which a single hwlatd thread runs, migrating
 * among the allowed CPUs in a round-robin fashion. The "per-cpu" mode
 * creates one hwlatd thread per allowed CPU.
 */
static ssize_t hwlat_mode_write(struct file *filp, const char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = hwlat_trace;
	const char *mode;
	char buf[64];
	int ret, i;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	mode = strstrip(buf);

	ret = -EINVAL;

	/*
	 * trace_types_lock is taken to avoid concurrency on start/stop
	 * and hwlat_busy.
	 */
	mutex_lock(&trace_types_lock);
	if (hwlat_busy)
		hwlat_tracer_stop(tr);

	mutex_lock(&hwlat_data.lock);

	for (i = 0; i < MODE_MAX; i++) {
		if (strcmp(mode, thread_mode_str[i]) == 0) {
			hwlat_data.thread_mode = i;
			ret = cnt;
		}
	}

	mutex_unlock(&hwlat_data.lock);

	if (hwlat_busy)
		hwlat_tracer_start(tr);
	mutex_unlock(&trace_types_lock);

	*ppos += cnt;



	return ret;
}

/*
 * The width parameter is read/write using the generic trace_min_max_param
 * method. The *val is protected by the hwlat_data lock and is upper
 * bounded by the window parameter.
 */
static struct trace_min_max_param hwlat_width = {
	.lock		= &hwlat_data.lock,
	.val		= &hwlat_data.sample_width,
	.max		= &hwlat_data.sample_window,
	.min		= NULL,
};

/*
 * The window parameter is read/write using the generic trace_min_max_param
 * method. The *val is protected by the hwlat_data lock and is lower
 * bounded by the width parameter.
 */
static struct trace_min_max_param hwlat_window = {
	.lock		= &hwlat_data.lock,
	.val		= &hwlat_data.sample_window,
	.max		= NULL,
	.min		= &hwlat_data.sample_width,
};

static const struct file_operations thread_mode_fops = {
	.open		= hwlat_mode_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.write		= hwlat_mode_write
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
	int ret;
	struct dentry *top_dir;

	ret = tracing_init_dentry();
	if (ret)
		return -ENOMEM;

	top_dir = tracefs_create_dir("hwlat_detector", NULL);
	if (!top_dir)
		return -ENOMEM;

	hwlat_sample_window = tracefs_create_file("window", TRACE_MODE_WRITE,
						  top_dir,
						  &hwlat_window,
						  &trace_min_max_fops);
	if (!hwlat_sample_window)
		goto err;

	hwlat_sample_width = tracefs_create_file("width", TRACE_MODE_WRITE,
						 top_dir,
						 &hwlat_width,
						 &trace_min_max_fops);
	if (!hwlat_sample_width)
		goto err;

	hwlat_thread_mode = trace_create_file("mode", TRACE_MODE_WRITE,
					      top_dir,
					      NULL,
					      &thread_mode_fops);
	if (!hwlat_thread_mode)
		goto err;

	return 0;

 err:
	tracefs_remove(top_dir);
	return -ENOMEM;
}

static void hwlat_tracer_start(struct trace_array *tr)
{
	int err;

	if (hwlat_data.thread_mode == MODE_PER_CPU)
		err = start_per_cpu_kthreads(tr);
	else
		err = start_single_kthread(tr);
	if (err)
		pr_err(BANNER "Cannot start hwlat kthread\n");
}

static void hwlat_tracer_stop(struct trace_array *tr)
{
	if (hwlat_data.thread_mode == MODE_PER_CPU)
		stop_per_cpu_kthreads();
	else
		stop_single_kthread();
}

static int hwlat_tracer_init(struct trace_array *tr)
{
	/* Only allow one instance to enable this */
	if (hwlat_busy)
		return -EBUSY;

	hwlat_trace = tr;

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
	hwlat_tracer_stop(tr);

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

	hwlat_init_hotplug_support();

	init_tracefs();

	return 0;
}
late_initcall(init_hwlat_tracer);
