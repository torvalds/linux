/* Include in trace.c */

#include <linux/stringify.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>

static inline int trace_valid_entry(struct trace_entry *entry)
{
	switch (entry->type) {
	case TRACE_FN:
	case TRACE_CTX:
	case TRACE_WAKE:
	case TRACE_STACK:
	case TRACE_PRINT:
	case TRACE_SPECIAL:
	case TRACE_BRANCH:
	case TRACE_GRAPH_ENT:
	case TRACE_GRAPH_RET:
	case TRACE_HW_BRANCHES:
	case TRACE_KSYM:
		return 1;
	}
	return 0;
}

static int trace_test_buffer_cpu(struct trace_array *tr, int cpu)
{
	struct ring_buffer_event *event;
	struct trace_entry *entry;
	unsigned int loops = 0;

	while ((event = ring_buffer_consume(tr->buffer, cpu, NULL))) {
		entry = ring_buffer_event_data(event);

		/*
		 * The ring buffer is a size of trace_buf_size, if
		 * we loop more than the size, there's something wrong
		 * with the ring buffer.
		 */
		if (loops++ > trace_buf_size) {
			printk(KERN_CONT ".. bad ring buffer ");
			goto failed;
		}
		if (!trace_valid_entry(entry)) {
			printk(KERN_CONT ".. invalid entry %d ",
				entry->type);
			goto failed;
		}
	}
	return 0;

 failed:
	/* disable tracing */
	tracing_disabled = 1;
	printk(KERN_CONT ".. corrupted trace buffer .. ");
	return -1;
}

/*
 * Test the trace buffer to see if all the elements
 * are still sane.
 */
static int trace_test_buffer(struct trace_array *tr, unsigned long *count)
{
	unsigned long flags, cnt = 0;
	int cpu, ret = 0;

	/* Don't allow flipping of max traces now */
	local_irq_save(flags);
	arch_spin_lock(&ftrace_max_lock);

	cnt = ring_buffer_entries(tr->buffer);

	/*
	 * The trace_test_buffer_cpu runs a while loop to consume all data.
	 * If the calling tracer is broken, and is constantly filling
	 * the buffer, this will run forever, and hard lock the box.
	 * We disable the ring buffer while we do this test to prevent
	 * a hard lock up.
	 */
	tracing_off();
	for_each_possible_cpu(cpu) {
		ret = trace_test_buffer_cpu(tr, cpu);
		if (ret)
			break;
	}
	tracing_on();
	arch_spin_unlock(&ftrace_max_lock);
	local_irq_restore(flags);

	if (count)
		*count = cnt;

	return ret;
}

static inline void warn_failed_init_tracer(struct tracer *trace, int init_ret)
{
	printk(KERN_WARNING "Failed to init %s tracer, init returned %d\n",
		trace->name, init_ret);
}
#ifdef CONFIG_FUNCTION_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE

/* Test dynamic code modification and ftrace filters */
int trace_selftest_startup_dynamic_tracing(struct tracer *trace,
					   struct trace_array *tr,
					   int (*func)(void))
{
	int save_ftrace_enabled = ftrace_enabled;
	int save_tracer_enabled = tracer_enabled;
	unsigned long count;
	char *func_name;
	int ret;

	/* The ftrace test PASSED */
	printk(KERN_CONT "PASSED\n");
	pr_info("Testing dynamic ftrace: ");

	/* enable tracing, and record the filter function */
	ftrace_enabled = 1;
	tracer_enabled = 1;

	/* passed in by parameter to fool gcc from optimizing */
	func();

	/*
	 * Some archs *cough*PowerPC*cough* add characters to the
	 * start of the function names. We simply put a '*' to
	 * accommodate them.
	 */
	func_name = "*" __stringify(DYN_FTRACE_TEST_NAME);

	/* filter only on our function */
	ftrace_set_filter(func_name, strlen(func_name), 1);

	/* enable tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		goto out;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);

	/* we should have nothing in the buffer */
	ret = trace_test_buffer(tr, &count);
	if (ret)
		goto out;

	if (count) {
		ret = -1;
		printk(KERN_CONT ".. filter did not filter .. ");
		goto out;
	}

	/* call our function again */
	func();

	/* sleep again */
	msleep(100);

	/* stop the tracing. */
	tracing_stop();
	ftrace_enabled = 0;

	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	/* we should only have one item */
	if (!ret && count != 1) {
		printk(KERN_CONT ".. filter failed count=%ld ..", count);
		ret = -1;
		goto out;
	}

 out:
	ftrace_enabled = save_ftrace_enabled;
	tracer_enabled = save_tracer_enabled;

	/* Enable tracing on all functions again */
	ftrace_set_filter(NULL, 0, 1);

	return ret;
}
#else
# define trace_selftest_startup_dynamic_tracing(trace, tr, func) ({ 0; })
#endif /* CONFIG_DYNAMIC_FTRACE */

/*
 * Simple verification test of ftrace function tracer.
 * Enable ftrace, sleep 1/10 second, and then read the trace
 * buffer to see if all is in order.
 */
int
trace_selftest_startup_function(struct tracer *trace, struct trace_array *tr)
{
	int save_ftrace_enabled = ftrace_enabled;
	int save_tracer_enabled = tracer_enabled;
	unsigned long count;
	int ret;

	/* make sure msleep has been recorded */
	msleep(1);

	/* start the tracing */
	ftrace_enabled = 1;
	tracer_enabled = 1;

	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		goto out;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tracing_stop();
	ftrace_enabled = 0;

	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
		goto out;
	}

	ret = trace_selftest_startup_dynamic_tracing(trace, tr,
						     DYN_FTRACE_TEST_NAME);

 out:
	ftrace_enabled = save_ftrace_enabled;
	tracer_enabled = save_tracer_enabled;

	/* kill ftrace totally if we failed */
	if (ret)
		ftrace_kill();

	return ret;
}
#endif /* CONFIG_FUNCTION_TRACER */


#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/* Maximum number of functions to trace before diagnosing a hang */
#define GRAPH_MAX_FUNC_TEST	100000000

static void __ftrace_dump(bool disable_tracing);
static unsigned int graph_hang_thresh;

/* Wrap the real function entry probe to avoid possible hanging */
static int trace_graph_entry_watchdog(struct ftrace_graph_ent *trace)
{
	/* This is harmlessly racy, we want to approximately detect a hang */
	if (unlikely(++graph_hang_thresh > GRAPH_MAX_FUNC_TEST)) {
		ftrace_graph_stop();
		printk(KERN_WARNING "BUG: Function graph tracer hang!\n");
		if (ftrace_dump_on_oops)
			__ftrace_dump(false);
		return 0;
	}

	return trace_graph_entry(trace);
}

/*
 * Pretty much the same than for the function tracer from which the selftest
 * has been borrowed.
 */
int
trace_selftest_startup_function_graph(struct tracer *trace,
					struct trace_array *tr)
{
	int ret;
	unsigned long count;

	/*
	 * Simulate the init() callback but we attach a watchdog callback
	 * to detect and recover from possible hangs
	 */
	tracing_reset_online_cpus(tr);
	set_graph_array(tr);
	ret = register_ftrace_graph(&trace_graph_return,
				    &trace_graph_entry_watchdog);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		goto out;
	}
	tracing_start_cmdline_record();

	/* Sleep for a 1/10 of a second */
	msleep(100);

	/* Have we just recovered from a hang? */
	if (graph_hang_thresh > GRAPH_MAX_FUNC_TEST) {
		tracing_selftest_disabled = true;
		ret = -1;
		goto out;
	}

	tracing_stop();

	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);

	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
		goto out;
	}

	/* Don't test dynamic tracing, the function tracer already did */

out:
	/* Stop it if we failed */
	if (ret)
		ftrace_graph_stop();

	return ret;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */


#ifdef CONFIG_IRQSOFF_TRACER
int
trace_selftest_startup_irqsoff(struct tracer *trace, struct trace_array *tr)
{
	unsigned long save_max = tracing_max_latency;
	unsigned long count;
	int ret;

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* reset the max latency */
	tracing_max_latency = 0;
	/* disable interrupts for a bit */
	local_irq_disable();
	udelay(100);
	local_irq_enable();

	/*
	 * Stop the tracer to avoid a warning subsequent
	 * to buffer flipping failure because tracing_stop()
	 * disables the tr and max buffers, making flipping impossible
	 * in case of parallels max irqs off latencies.
	 */
	trace->stop(tr);
	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	tracing_max_latency = save_max;

	return ret;
}
#endif /* CONFIG_IRQSOFF_TRACER */

#ifdef CONFIG_PREEMPT_TRACER
int
trace_selftest_startup_preemptoff(struct tracer *trace, struct trace_array *tr)
{
	unsigned long save_max = tracing_max_latency;
	unsigned long count;
	int ret;

	/*
	 * Now that the big kernel lock is no longer preemptable,
	 * and this is called with the BKL held, it will always
	 * fail. If preemption is already disabled, simply
	 * pass the test. When the BKL is removed, or becomes
	 * preemptible again, we will once again test this,
	 * so keep it in.
	 */
	if (preempt_count()) {
		printk(KERN_CONT "can not test ... force ");
		return 0;
	}

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* reset the max latency */
	tracing_max_latency = 0;
	/* disable preemption for a bit */
	preempt_disable();
	udelay(100);
	preempt_enable();

	/*
	 * Stop the tracer to avoid a warning subsequent
	 * to buffer flipping failure because tracing_stop()
	 * disables the tr and max buffers, making flipping impossible
	 * in case of parallels max preempt off latencies.
	 */
	trace->stop(tr);
	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	tracing_max_latency = save_max;

	return ret;
}
#endif /* CONFIG_PREEMPT_TRACER */

#if defined(CONFIG_IRQSOFF_TRACER) && defined(CONFIG_PREEMPT_TRACER)
int
trace_selftest_startup_preemptirqsoff(struct tracer *trace, struct trace_array *tr)
{
	unsigned long save_max = tracing_max_latency;
	unsigned long count;
	int ret;

	/*
	 * Now that the big kernel lock is no longer preemptable,
	 * and this is called with the BKL held, it will always
	 * fail. If preemption is already disabled, simply
	 * pass the test. When the BKL is removed, or becomes
	 * preemptible again, we will once again test this,
	 * so keep it in.
	 */
	if (preempt_count()) {
		printk(KERN_CONT "can not test ... force ");
		return 0;
	}

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		goto out_no_start;
	}

	/* reset the max latency */
	tracing_max_latency = 0;

	/* disable preemption and interrupts for a bit */
	preempt_disable();
	local_irq_disable();
	udelay(100);
	preempt_enable();
	/* reverse the order of preempt vs irqs */
	local_irq_enable();

	/*
	 * Stop the tracer to avoid a warning subsequent
	 * to buffer flipping failure because tracing_stop()
	 * disables the tr and max buffers, making flipping impossible
	 * in case of parallels max irqs/preempt off latencies.
	 */
	trace->stop(tr);
	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (ret)
		goto out;

	ret = trace_test_buffer(&max_tr, &count);
	if (ret)
		goto out;

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
		goto out;
	}

	/* do the test by disabling interrupts first this time */
	tracing_max_latency = 0;
	tracing_start();
	trace->start(tr);

	preempt_disable();
	local_irq_disable();
	udelay(100);
	preempt_enable();
	/* reverse the order of preempt vs irqs */
	local_irq_enable();

	trace->stop(tr);
	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (ret)
		goto out;

	ret = trace_test_buffer(&max_tr, &count);

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
		goto out;
	}

out:
	tracing_start();
out_no_start:
	trace->reset(tr);
	tracing_max_latency = save_max;

	return ret;
}
#endif /* CONFIG_IRQSOFF_TRACER && CONFIG_PREEMPT_TRACER */

#ifdef CONFIG_NOP_TRACER
int
trace_selftest_startup_nop(struct tracer *trace, struct trace_array *tr)
{
	/* What could possibly go wrong? */
	return 0;
}
#endif

#ifdef CONFIG_SCHED_TRACER
static int trace_wakeup_test_thread(void *data)
{
	/* Make this a RT thread, doesn't need to be too high */
	struct sched_param param = { .sched_priority = 5 };
	struct completion *x = data;

	sched_setscheduler(current, SCHED_FIFO, &param);

	/* Make it know we have a new prio */
	complete(x);

	/* now go to sleep and let the test wake us up */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	/* we are awake, now wait to disappear */
	while (!kthread_should_stop()) {
		/*
		 * This is an RT task, do short sleeps to let
		 * others run.
		 */
		msleep(100);
	}

	return 0;
}

int
trace_selftest_startup_wakeup(struct tracer *trace, struct trace_array *tr)
{
	unsigned long save_max = tracing_max_latency;
	struct task_struct *p;
	struct completion isrt;
	unsigned long count;
	int ret;

	init_completion(&isrt);

	/* create a high prio thread */
	p = kthread_run(trace_wakeup_test_thread, &isrt, "ftrace-test");
	if (IS_ERR(p)) {
		printk(KERN_CONT "Failed to create ftrace wakeup test thread ");
		return -1;
	}

	/* make sure the thread is running at an RT prio */
	wait_for_completion(&isrt);

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* reset the max latency */
	tracing_max_latency = 0;

	/* sleep to let the RT thread sleep too */
	msleep(100);

	/*
	 * Yes this is slightly racy. It is possible that for some
	 * strange reason that the RT thread we created, did not
	 * call schedule for 100ms after doing the completion,
	 * and we do a wakeup on a task that already is awake.
	 * But that is extremely unlikely, and the worst thing that
	 * happens in such a case, is that we disable tracing.
	 * Honestly, if this race does happen something is horrible
	 * wrong with the system.
	 */

	wake_up_process(p);

	/* give a little time to let the thread wake up */
	msleep(100);

	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);


	trace->reset(tr);
	tracing_start();

	tracing_max_latency = save_max;

	/* kill the thread */
	kthread_stop(p);

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_SCHED_TRACER */

#ifdef CONFIG_CONTEXT_SWITCH_TRACER
int
trace_selftest_startup_sched_switch(struct tracer *trace, struct trace_array *tr)
{
	unsigned long count;
	int ret;

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tracing_stop();
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_CONTEXT_SWITCH_TRACER */

#ifdef CONFIG_SYSPROF_TRACER
int
trace_selftest_startup_sysprof(struct tracer *trace, struct trace_array *tr)
{
	unsigned long count;
	int ret;

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tracing_stop();
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_SYSPROF_TRACER */

#ifdef CONFIG_BRANCH_TRACER
int
trace_selftest_startup_branch(struct tracer *trace, struct trace_array *tr)
{
	unsigned long count;
	int ret;

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tracing_stop();
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_BRANCH_TRACER */

#ifdef CONFIG_HW_BRANCH_TRACER
int
trace_selftest_startup_hw_branches(struct tracer *trace,
				   struct trace_array *tr)
{
	struct trace_iterator *iter;
	struct tracer tracer;
	unsigned long count;
	int ret;

	if (!trace->open) {
		printk(KERN_CONT "missing open function...");
		return -1;
	}

	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/*
	 * The hw-branch tracer needs to collect the trace from the various
	 * cpu trace buffers - before tracing is stopped.
	 */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	memcpy(&tracer, trace, sizeof(tracer));

	iter->trace = &tracer;
	iter->tr = tr;
	iter->pos = -1;
	mutex_init(&iter->mutex);

	trace->open(iter);

	mutex_destroy(&iter->mutex);
	kfree(iter);

	tracing_stop();

	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT "no entries found..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_HW_BRANCH_TRACER */

#ifdef CONFIG_KSYM_TRACER
static int ksym_selftest_dummy;

int
trace_selftest_startup_ksym(struct tracer *trace, struct trace_array *tr)
{
	unsigned long count;
	int ret;

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	ksym_selftest_dummy = 0;
	/* Register the read-write tracing request */

	ret = process_new_ksym_entry("ksym_selftest_dummy",
				     HW_BREAKPOINT_R | HW_BREAKPOINT_W,
					(unsigned long)(&ksym_selftest_dummy));

	if (ret < 0) {
		printk(KERN_CONT "ksym_trace read-write startup test failed\n");
		goto ret_path;
	}
	/* Perform a read and a write operation over the dummy variable to
	 * trigger the tracer
	 */
	if (ksym_selftest_dummy == 0)
		ksym_selftest_dummy++;

	/* stop the tracing. */
	tracing_stop();
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);
	tracing_start();

	/* read & write operations - one each is performed on the dummy variable
	 * triggering two entries in the trace buffer
	 */
	if (!ret && count != 2) {
		printk(KERN_CONT "Ksym tracer startup test failed");
		ret = -1;
	}

ret_path:
	return ret;
}
#endif /* CONFIG_KSYM_TRACER */

