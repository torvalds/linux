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
	case TRACE_BRANCH:
	case TRACE_GRAPH_ENT:
	case TRACE_GRAPH_RET:
		return 1;
	}
	return 0;
}

static int trace_test_buffer_cpu(struct trace_buffer *buf, int cpu)
{
	struct ring_buffer_event *event;
	struct trace_entry *entry;
	unsigned int loops = 0;

	while ((event = ring_buffer_consume(buf->buffer, cpu, NULL, NULL))) {
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
static int trace_test_buffer(struct trace_buffer *buf, unsigned long *count)
{
	unsigned long flags, cnt = 0;
	int cpu, ret = 0;

	/* Don't allow flipping of max traces now */
	local_irq_save(flags);
	arch_spin_lock(&ftrace_max_lock);

	cnt = ring_buffer_entries(buf->buffer);

	/*
	 * The trace_test_buffer_cpu runs a while loop to consume all data.
	 * If the calling tracer is broken, and is constantly filling
	 * the buffer, this will run forever, and hard lock the box.
	 * We disable the ring buffer while we do this test to prevent
	 * a hard lock up.
	 */
	tracing_off();
	for_each_possible_cpu(cpu) {
		ret = trace_test_buffer_cpu(buf, cpu);
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

static int trace_selftest_test_probe1_cnt;
static void trace_selftest_test_probe1_func(unsigned long ip,
					    unsigned long pip,
					    struct ftrace_ops *op,
					    struct pt_regs *pt_regs)
{
	trace_selftest_test_probe1_cnt++;
}

static int trace_selftest_test_probe2_cnt;
static void trace_selftest_test_probe2_func(unsigned long ip,
					    unsigned long pip,
					    struct ftrace_ops *op,
					    struct pt_regs *pt_regs)
{
	trace_selftest_test_probe2_cnt++;
}

static int trace_selftest_test_probe3_cnt;
static void trace_selftest_test_probe3_func(unsigned long ip,
					    unsigned long pip,
					    struct ftrace_ops *op,
					    struct pt_regs *pt_regs)
{
	trace_selftest_test_probe3_cnt++;
}

static int trace_selftest_test_global_cnt;
static void trace_selftest_test_global_func(unsigned long ip,
					    unsigned long pip,
					    struct ftrace_ops *op,
					    struct pt_regs *pt_regs)
{
	trace_selftest_test_global_cnt++;
}

static int trace_selftest_test_dyn_cnt;
static void trace_selftest_test_dyn_func(unsigned long ip,
					 unsigned long pip,
					 struct ftrace_ops *op,
					 struct pt_regs *pt_regs)
{
	trace_selftest_test_dyn_cnt++;
}

static struct ftrace_ops test_probe1 = {
	.func			= trace_selftest_test_probe1_func,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE,
};

static struct ftrace_ops test_probe2 = {
	.func			= trace_selftest_test_probe2_func,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE,
};

static struct ftrace_ops test_probe3 = {
	.func			= trace_selftest_test_probe3_func,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE,
};

static struct ftrace_ops test_global = {
	.func		= trace_selftest_test_global_func,
	.flags		= FTRACE_OPS_FL_GLOBAL | FTRACE_OPS_FL_RECURSION_SAFE,
};

static void print_counts(void)
{
	printk("(%d %d %d %d %d) ",
	       trace_selftest_test_probe1_cnt,
	       trace_selftest_test_probe2_cnt,
	       trace_selftest_test_probe3_cnt,
	       trace_selftest_test_global_cnt,
	       trace_selftest_test_dyn_cnt);
}

static void reset_counts(void)
{
	trace_selftest_test_probe1_cnt = 0;
	trace_selftest_test_probe2_cnt = 0;
	trace_selftest_test_probe3_cnt = 0;
	trace_selftest_test_global_cnt = 0;
	trace_selftest_test_dyn_cnt = 0;
}

static int trace_selftest_ops(int cnt)
{
	int save_ftrace_enabled = ftrace_enabled;
	struct ftrace_ops *dyn_ops;
	char *func1_name;
	char *func2_name;
	int len1;
	int len2;
	int ret = -1;

	printk(KERN_CONT "PASSED\n");
	pr_info("Testing dynamic ftrace ops #%d: ", cnt);

	ftrace_enabled = 1;
	reset_counts();

	/* Handle PPC64 '.' name */
	func1_name = "*" __stringify(DYN_FTRACE_TEST_NAME);
	func2_name = "*" __stringify(DYN_FTRACE_TEST_NAME2);
	len1 = strlen(func1_name);
	len2 = strlen(func2_name);

	/*
	 * Probe 1 will trace function 1.
	 * Probe 2 will trace function 2.
	 * Probe 3 will trace functions 1 and 2.
	 */
	ftrace_set_filter(&test_probe1, func1_name, len1, 1);
	ftrace_set_filter(&test_probe2, func2_name, len2, 1);
	ftrace_set_filter(&test_probe3, func1_name, len1, 1);
	ftrace_set_filter(&test_probe3, func2_name, len2, 0);

	register_ftrace_function(&test_probe1);
	register_ftrace_function(&test_probe2);
	register_ftrace_function(&test_probe3);
	register_ftrace_function(&test_global);

	DYN_FTRACE_TEST_NAME();

	print_counts();

	if (trace_selftest_test_probe1_cnt != 1)
		goto out;
	if (trace_selftest_test_probe2_cnt != 0)
		goto out;
	if (trace_selftest_test_probe3_cnt != 1)
		goto out;
	if (trace_selftest_test_global_cnt == 0)
		goto out;

	DYN_FTRACE_TEST_NAME2();

	print_counts();

	if (trace_selftest_test_probe1_cnt != 1)
		goto out;
	if (trace_selftest_test_probe2_cnt != 1)
		goto out;
	if (trace_selftest_test_probe3_cnt != 2)
		goto out;

	/* Add a dynamic probe */
	dyn_ops = kzalloc(sizeof(*dyn_ops), GFP_KERNEL);
	if (!dyn_ops) {
		printk("MEMORY ERROR ");
		goto out;
	}

	dyn_ops->func = trace_selftest_test_dyn_func;

	register_ftrace_function(dyn_ops);

	trace_selftest_test_global_cnt = 0;

	DYN_FTRACE_TEST_NAME();

	print_counts();

	if (trace_selftest_test_probe1_cnt != 2)
		goto out_free;
	if (trace_selftest_test_probe2_cnt != 1)
		goto out_free;
	if (trace_selftest_test_probe3_cnt != 3)
		goto out_free;
	if (trace_selftest_test_global_cnt == 0)
		goto out;
	if (trace_selftest_test_dyn_cnt == 0)
		goto out_free;

	DYN_FTRACE_TEST_NAME2();

	print_counts();

	if (trace_selftest_test_probe1_cnt != 2)
		goto out_free;
	if (trace_selftest_test_probe2_cnt != 2)
		goto out_free;
	if (trace_selftest_test_probe3_cnt != 4)
		goto out_free;

	ret = 0;
 out_free:
	unregister_ftrace_function(dyn_ops);
	kfree(dyn_ops);

 out:
	/* Purposely unregister in the same order */
	unregister_ftrace_function(&test_probe1);
	unregister_ftrace_function(&test_probe2);
	unregister_ftrace_function(&test_probe3);
	unregister_ftrace_function(&test_global);

	/* Make sure everything is off */
	reset_counts();
	DYN_FTRACE_TEST_NAME();
	DYN_FTRACE_TEST_NAME();

	if (trace_selftest_test_probe1_cnt ||
	    trace_selftest_test_probe2_cnt ||
	    trace_selftest_test_probe3_cnt ||
	    trace_selftest_test_global_cnt ||
	    trace_selftest_test_dyn_cnt)
		ret = -1;

	ftrace_enabled = save_ftrace_enabled;

	return ret;
}

/* Test dynamic code modification and ftrace filters */
int trace_selftest_startup_dynamic_tracing(struct tracer *trace,
					   struct trace_array *tr,
					   int (*func)(void))
{
	int save_ftrace_enabled = ftrace_enabled;
	unsigned long count;
	char *func_name;
	int ret;

	/* The ftrace test PASSED */
	printk(KERN_CONT "PASSED\n");
	pr_info("Testing dynamic ftrace: ");

	/* enable tracing, and record the filter function */
	ftrace_enabled = 1;

	/* passed in by parameter to fool gcc from optimizing */
	func();

	/*
	 * Some archs *cough*PowerPC*cough* add characters to the
	 * start of the function names. We simply put a '*' to
	 * accommodate them.
	 */
	func_name = "*" __stringify(DYN_FTRACE_TEST_NAME);

	/* filter only on our function */
	ftrace_set_global_filter(func_name, strlen(func_name), 1);

	/* enable tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		goto out;
	}

	/* Sleep for a 1/10 of a second */
	msleep(100);

	/* we should have nothing in the buffer */
	ret = trace_test_buffer(&tr->trace_buffer, &count);
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
	ret = trace_test_buffer(&tr->trace_buffer, &count);
	tracing_start();

	/* we should only have one item */
	if (!ret && count != 1) {
		trace->reset(tr);
		printk(KERN_CONT ".. filter failed count=%ld ..", count);
		ret = -1;
		goto out;
	}

	/* Test the ops with global tracing running */
	ret = trace_selftest_ops(1);
	trace->reset(tr);

 out:
	ftrace_enabled = save_ftrace_enabled;

	/* Enable tracing on all functions again */
	ftrace_set_global_filter(NULL, 0, 1);

	/* Test the ops with global tracing off */
	if (!ret)
		ret = trace_selftest_ops(2);

	return ret;
}

static int trace_selftest_recursion_cnt;
static void trace_selftest_test_recursion_func(unsigned long ip,
					       unsigned long pip,
					       struct ftrace_ops *op,
					       struct pt_regs *pt_regs)
{
	/*
	 * This function is registered without the recursion safe flag.
	 * The ftrace infrastructure should provide the recursion
	 * protection. If not, this will crash the kernel!
	 */
	if (trace_selftest_recursion_cnt++ > 10)
		return;
	DYN_FTRACE_TEST_NAME();
}

static void trace_selftest_test_recursion_safe_func(unsigned long ip,
						    unsigned long pip,
						    struct ftrace_ops *op,
						    struct pt_regs *pt_regs)
{
	/*
	 * We said we would provide our own recursion. By calling
	 * this function again, we should recurse back into this function
	 * and count again. But this only happens if the arch supports
	 * all of ftrace features and nothing else is using the function
	 * tracing utility.
	 */
	if (trace_selftest_recursion_cnt++)
		return;
	DYN_FTRACE_TEST_NAME();
}

static struct ftrace_ops test_rec_probe = {
	.func			= trace_selftest_test_recursion_func,
};

static struct ftrace_ops test_recsafe_probe = {
	.func			= trace_selftest_test_recursion_safe_func,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE,
};

static int
trace_selftest_function_recursion(void)
{
	int save_ftrace_enabled = ftrace_enabled;
	char *func_name;
	int len;
	int ret;

	/* The previous test PASSED */
	pr_cont("PASSED\n");
	pr_info("Testing ftrace recursion: ");


	/* enable tracing, and record the filter function */
	ftrace_enabled = 1;

	/* Handle PPC64 '.' name */
	func_name = "*" __stringify(DYN_FTRACE_TEST_NAME);
	len = strlen(func_name);

	ret = ftrace_set_filter(&test_rec_probe, func_name, len, 1);
	if (ret) {
		pr_cont("*Could not set filter* ");
		goto out;
	}

	ret = register_ftrace_function(&test_rec_probe);
	if (ret) {
		pr_cont("*could not register callback* ");
		goto out;
	}

	DYN_FTRACE_TEST_NAME();

	unregister_ftrace_function(&test_rec_probe);

	ret = -1;
	if (trace_selftest_recursion_cnt != 1) {
		pr_cont("*callback not called once (%d)* ",
			trace_selftest_recursion_cnt);
		goto out;
	}

	trace_selftest_recursion_cnt = 1;

	pr_cont("PASSED\n");
	pr_info("Testing ftrace recursion safe: ");

	ret = ftrace_set_filter(&test_recsafe_probe, func_name, len, 1);
	if (ret) {
		pr_cont("*Could not set filter* ");
		goto out;
	}

	ret = register_ftrace_function(&test_recsafe_probe);
	if (ret) {
		pr_cont("*could not register callback* ");
		goto out;
	}

	DYN_FTRACE_TEST_NAME();

	unregister_ftrace_function(&test_recsafe_probe);

	ret = -1;
	if (trace_selftest_recursion_cnt != 2) {
		pr_cont("*callback not called expected 2 times (%d)* ",
			trace_selftest_recursion_cnt);
		goto out;
	}

	ret = 0;
out:
	ftrace_enabled = save_ftrace_enabled;

	return ret;
}
#else
# define trace_selftest_startup_dynamic_tracing(trace, tr, func) ({ 0; })
# define trace_selftest_function_recursion() ({ 0; })
#endif /* CONFIG_DYNAMIC_FTRACE */

static enum {
	TRACE_SELFTEST_REGS_START,
	TRACE_SELFTEST_REGS_FOUND,
	TRACE_SELFTEST_REGS_NOT_FOUND,
} trace_selftest_regs_stat;

static void trace_selftest_test_regs_func(unsigned long ip,
					  unsigned long pip,
					  struct ftrace_ops *op,
					  struct pt_regs *pt_regs)
{
	if (pt_regs)
		trace_selftest_regs_stat = TRACE_SELFTEST_REGS_FOUND;
	else
		trace_selftest_regs_stat = TRACE_SELFTEST_REGS_NOT_FOUND;
}

static struct ftrace_ops test_regs_probe = {
	.func		= trace_selftest_test_regs_func,
	.flags		= FTRACE_OPS_FL_RECURSION_SAFE | FTRACE_OPS_FL_SAVE_REGS,
};

static int
trace_selftest_function_regs(void)
{
	int save_ftrace_enabled = ftrace_enabled;
	char *func_name;
	int len;
	int ret;
	int supported = 0;

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	supported = 1;
#endif

	/* The previous test PASSED */
	pr_cont("PASSED\n");
	pr_info("Testing ftrace regs%s: ",
		!supported ? "(no arch support)" : "");

	/* enable tracing, and record the filter function */
	ftrace_enabled = 1;

	/* Handle PPC64 '.' name */
	func_name = "*" __stringify(DYN_FTRACE_TEST_NAME);
	len = strlen(func_name);

	ret = ftrace_set_filter(&test_regs_probe, func_name, len, 1);
	/*
	 * If DYNAMIC_FTRACE is not set, then we just trace all functions.
	 * This test really doesn't care.
	 */
	if (ret && ret != -ENODEV) {
		pr_cont("*Could not set filter* ");
		goto out;
	}

	ret = register_ftrace_function(&test_regs_probe);
	/*
	 * Now if the arch does not support passing regs, then this should
	 * have failed.
	 */
	if (!supported) {
		if (!ret) {
			pr_cont("*registered save-regs without arch support* ");
			goto out;
		}
		test_regs_probe.flags |= FTRACE_OPS_FL_SAVE_REGS_IF_SUPPORTED;
		ret = register_ftrace_function(&test_regs_probe);
	}
	if (ret) {
		pr_cont("*could not register callback* ");
		goto out;
	}


	DYN_FTRACE_TEST_NAME();

	unregister_ftrace_function(&test_regs_probe);

	ret = -1;

	switch (trace_selftest_regs_stat) {
	case TRACE_SELFTEST_REGS_START:
		pr_cont("*callback never called* ");
		goto out;

	case TRACE_SELFTEST_REGS_FOUND:
		if (supported)
			break;
		pr_cont("*callback received regs without arch support* ");
		goto out;

	case TRACE_SELFTEST_REGS_NOT_FOUND:
		if (!supported)
			break;
		pr_cont("*callback received NULL regs* ");
		goto out;
	}

	ret = 0;
out:
	ftrace_enabled = save_ftrace_enabled;

	return ret;
}

/*
 * Simple verification test of ftrace function tracer.
 * Enable ftrace, sleep 1/10 second, and then read the trace
 * buffer to see if all is in order.
 */
__init int
trace_selftest_startup_function(struct tracer *trace, struct trace_array *tr)
{
	int save_ftrace_enabled = ftrace_enabled;
	unsigned long count;
	int ret;

#ifdef CONFIG_DYNAMIC_FTRACE
	if (ftrace_filter_param) {
		printk(KERN_CONT " ... kernel command line filter set: force PASS ... ");
		return 0;
	}
#endif

	/* make sure msleep has been recorded */
	msleep(1);

	/* start the tracing */
	ftrace_enabled = 1;

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
	ret = trace_test_buffer(&tr->trace_buffer, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
		goto out;
	}

	ret = trace_selftest_startup_dynamic_tracing(trace, tr,
						     DYN_FTRACE_TEST_NAME);
	if (ret)
		goto out;

	ret = trace_selftest_function_recursion();
	if (ret)
		goto out;

	ret = trace_selftest_function_regs();
 out:
	ftrace_enabled = save_ftrace_enabled;

	/* kill ftrace totally if we failed */
	if (ret)
		ftrace_kill();

	return ret;
}
#endif /* CONFIG_FUNCTION_TRACER */


#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/* Maximum number of functions to trace before diagnosing a hang */
#define GRAPH_MAX_FUNC_TEST	100000000

static unsigned int graph_hang_thresh;

/* Wrap the real function entry probe to avoid possible hanging */
static int trace_graph_entry_watchdog(struct ftrace_graph_ent *trace)
{
	/* This is harmlessly racy, we want to approximately detect a hang */
	if (unlikely(++graph_hang_thresh > GRAPH_MAX_FUNC_TEST)) {
		ftrace_graph_stop();
		printk(KERN_WARNING "BUG: Function graph tracer hang!\n");
		if (ftrace_dump_on_oops) {
			ftrace_dump(DUMP_ALL);
			/* ftrace_dump() disables tracing */
			tracing_on();
		}
		return 0;
	}

	return trace_graph_entry(trace);
}

/*
 * Pretty much the same than for the function tracer from which the selftest
 * has been borrowed.
 */
__init int
trace_selftest_startup_function_graph(struct tracer *trace,
					struct trace_array *tr)
{
	int ret;
	unsigned long count;

#ifdef CONFIG_DYNAMIC_FTRACE
	if (ftrace_filter_param) {
		printk(KERN_CONT " ... kernel command line filter set: force PASS ... ");
		return 0;
	}
#endif

	/*
	 * Simulate the init() callback but we attach a watchdog callback
	 * to detect and recover from possible hangs
	 */
	tracing_reset_online_cpus(&tr->trace_buffer);
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
	ret = trace_test_buffer(&tr->trace_buffer, &count);

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
	ret = trace_test_buffer(&tr->trace_buffer, NULL);
	if (!ret)
		ret = trace_test_buffer(&tr->max_buffer, &count);
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
	ret = trace_test_buffer(&tr->trace_buffer, NULL);
	if (!ret)
		ret = trace_test_buffer(&tr->max_buffer, &count);
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
	ret = trace_test_buffer(&tr->trace_buffer, NULL);
	if (ret)
		goto out;

	ret = trace_test_buffer(&tr->max_buffer, &count);
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
	ret = trace_test_buffer(&tr->trace_buffer, NULL);
	if (ret)
		goto out;

	ret = trace_test_buffer(&tr->max_buffer, &count);

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
	/* Make this a -deadline thread */
	static const struct sched_attr attr = {
		.sched_policy = SCHED_DEADLINE,
		.sched_runtime = 100000ULL,
		.sched_deadline = 10000000ULL,
		.sched_period = 10000000ULL
	};
	struct completion *x = data;

	sched_setattr(current, &attr);

	/* Make it know we have a new prio */
	complete(x);

	/* now go to sleep and let the test wake us up */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

	complete(x);

	/* we are awake, now wait to disappear */
	while (!kthread_should_stop()) {
		/*
		 * This will likely be the system top priority
		 * task, do short sleeps to let others run.
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
	struct completion is_ready;
	unsigned long count;
	int ret;

	init_completion(&is_ready);

	/* create a -deadline thread */
	p = kthread_run(trace_wakeup_test_thread, &is_ready, "ftrace-test");
	if (IS_ERR(p)) {
		printk(KERN_CONT "Failed to create ftrace wakeup test thread ");
		return -1;
	}

	/* make sure the thread is running at -deadline policy */
	wait_for_completion(&is_ready);

	/* start the tracing */
	ret = tracer_init(trace, tr);
	if (ret) {
		warn_failed_init_tracer(trace, ret);
		return ret;
	}

	/* reset the max latency */
	tracing_max_latency = 0;

	while (p->on_rq) {
		/*
		 * Sleep to make sure the -deadline thread is asleep too.
		 * On virtual machines we can't rely on timings,
		 * but we want to make sure this test still works.
		 */
		msleep(100);
	}

	init_completion(&is_ready);

	wake_up_process(p);

	/* Wait for the task to wake up */
	wait_for_completion(&is_ready);

	/* stop the tracing. */
	tracing_stop();
	/* check both trace buffers */
	ret = trace_test_buffer(&tr->trace_buffer, NULL);
	printk("ret = %d\n", ret);
	if (!ret)
		ret = trace_test_buffer(&tr->max_buffer, &count);


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
	ret = trace_test_buffer(&tr->trace_buffer, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_CONTEXT_SWITCH_TRACER */

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
	ret = trace_test_buffer(&tr->trace_buffer, &count);
	trace->reset(tr);
	tracing_start();

	if (!ret && !count) {
		printk(KERN_CONT ".. no entries found ..");
		ret = -1;
	}

	return ret;
}
#endif /* CONFIG_BRANCH_TRACER */

