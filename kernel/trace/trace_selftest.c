/* Include in trace.c */

#include <linux/kthread.h>

static inline int trace_valid_entry(struct trace_entry *entry)
{
	switch (entry->type) {
	case TRACE_FN:
	case TRACE_CTX:
		return 1;
	}
	return 0;
}

static int
trace_test_buffer_cpu(struct trace_array *tr, struct trace_array_cpu *data)
{
	struct page *page;
	struct trace_entry *entries;
	int idx = 0;
	int i;

	page = list_entry(data->trace_pages.next, struct page, lru);
	entries = page_address(page);

	if (data->trace != entries)
		goto failed;

	/*
	 * The starting trace buffer always has valid elements,
	 * if any element exits.
	 */
	entries = data->trace;

	for (i = 0; i < tr->entries; i++) {

		if (i < data->trace_idx &&
		    !trace_valid_entry(&entries[idx])) {
			printk(KERN_CONT ".. invalid entry %d ", entries[idx].type);
			goto failed;
		}

		idx++;
		if (idx >= ENTRIES_PER_PAGE) {
			page = virt_to_page(entries);
			if (page->lru.next == &data->trace_pages) {
				if (i != tr->entries - 1) {
					printk(KERN_CONT ".. entries buffer mismatch");
					goto failed;
				}
			} else {
				page = list_entry(page->lru.next, struct page, lru);
				entries = page_address(page);
			}
			idx = 0;
		}
	}

	page = virt_to_page(entries);
	if (page->lru.next != &data->trace_pages) {
		printk(KERN_CONT ".. too many entries");
		goto failed;
	}

	return 0;

 failed:
	printk(KERN_CONT ".. corrupted trace buffer .. ");
	return -1;
}

/*
 * Test the trace buffer to see if all the elements
 * are still sane.
 */
static int trace_test_buffer(struct trace_array *tr, unsigned long *count)
{
	unsigned long cnt = 0;
	int cpu;
	int ret = 0;

	for_each_possible_cpu(cpu) {
		if (!tr->data[cpu]->trace)
			continue;

		cnt += tr->data[cpu]->trace_idx;
		printk("%d: count = %ld\n", cpu, cnt);

		ret = trace_test_buffer_cpu(tr, tr->data[cpu]);
		if (ret)
			break;
	}

	if (count)
		*count = cnt;

	return ret;
}

#ifdef CONFIG_FTRACE
/*
 * Simple verification test of ftrace function tracer.
 * Enable ftrace, sleep 1/10 second, and then read the trace
 * buffer to see if all is in order.
 */
int
trace_selftest_startup_function(struct tracer *trace, struct trace_array *tr)
{
	unsigned long count;
	int ret;

	/* make sure functions have been recorded */
	ret = ftrace_force_update();
	if (ret) {
		printk(KERN_CONT ".. ftraced failed .. ");
		return ret;
	}

	/* start the tracing */
	tr->ctrl = 1;
	trace->init(tr);
	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_FTRACE */

#ifdef CONFIG_IRQSOFF_TRACER
int
trace_selftest_startup_irqsoff(struct tracer *trace, struct trace_array *tr)
{
	unsigned long save_max = tracing_max_latency;
	unsigned long count;
	int ret;

	/* start the tracing */
	tr->ctrl = 1;
	trace->init(tr);
	/* reset the max latency */
	tracing_max_latency = 0;
	/* disable interrupts for a bit */
	local_irq_disable();
	udelay(100);
	local_irq_enable();
	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);
	trace->reset(tr);

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

	/* start the tracing */
	tr->ctrl = 1;
	trace->init(tr);
	/* reset the max latency */
	tracing_max_latency = 0;
	/* disable preemption for a bit */
	preempt_disable();
	udelay(100);
	preempt_enable();
	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);
	trace->reset(tr);

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

	/* start the tracing */
	tr->ctrl = 1;
	trace->init(tr);

	/* reset the max latency */
	tracing_max_latency = 0;

	/* disable preemption and interrupts for a bit */
	preempt_disable();
	local_irq_disable();
	udelay(100);
	preempt_enable();
	/* reverse the order of preempt vs irqs */
	local_irq_enable();

	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
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
	tr->ctrl = 1;
	trace->ctrl_update(tr);
	preempt_disable();
	local_irq_disable();
	udelay(100);
	preempt_enable();
	/* reverse the order of preempt vs irqs */
	local_irq_enable();

	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
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
	trace->reset(tr);
	tracing_max_latency = save_max;

	return ret;
}
#endif /* CONFIG_IRQSOFF_TRACER && CONFIG_PREEMPT_TRACER */

#ifdef CONFIG_SCHED_TRACER
static int trace_wakeup_test_thread(void *data)
{
	struct completion *x = data;

	/* Make this a RT thread, doesn't need to be too high */

	rt_mutex_setprio(current, MAX_RT_PRIO - 5);

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
	if (!IS_ERR(p)) {
		printk(KERN_CONT "Failed to create ftrace wakeup test thread ");
		return -1;
	}

	/* make sure the thread is running at an RT prio */
	wait_for_completion(&isrt);

	/* start the tracing */
	tr->ctrl = 1;
	trace->init(tr);
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

	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
	/* check both trace buffers */
	ret = trace_test_buffer(tr, NULL);
	if (!ret)
		ret = trace_test_buffer(&max_tr, &count);


	trace->reset(tr);

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
	tr->ctrl = 1;
	trace->init(tr);
	/* Sleep for a 1/10 of a second */
	msleep(100);
	/* stop the tracing. */
	tr->ctrl = 0;
	trace->ctrl_update(tr);
	/* check the trace buffer */
	ret = trace_test_buffer(tr, &count);
	trace->reset(tr);

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_CONTEXT_SWITCH_TRACER */

#ifdef CONFIG_DYNAMIC_FTRACE
#endif /* CONFIG_DYNAMIC_FTRACE */
