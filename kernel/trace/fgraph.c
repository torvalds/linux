// SPDX-License-Identifier: GPL-2.0
/*
 * Infrastructure to took into function calls and returns.
 * Copyright (c) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 * Highly modified by Steven Rostedt (VMware).
 */
#include <linux/suspend.h>
#include <linux/ftrace.h>
#include <linux/slab.h>

#include <trace/events/sched.h>

#include "ftrace_internal.h"

#ifdef CONFIG_DYNAMIC_FTRACE
#define ASSIGN_OPS_HASH(opsname, val) \
	.func_hash		= val, \
	.local_hash.regex_lock	= __MUTEX_INITIALIZER(opsname.local_hash.regex_lock),
#else
#define ASSIGN_OPS_HASH(opsname, val)
#endif

static bool kill_ftrace_graph;
int ftrace_graph_active;

/* Both enabled by default (can be cleared by function_graph tracer flags */
static bool fgraph_sleep_time = true;

/**
 * ftrace_graph_is_dead - returns true if ftrace_graph_stop() was called
 *
 * ftrace_graph_stop() is called when a severe error is detected in
 * the function graph tracing. This function is called by the critical
 * paths of function graph to keep those paths from doing any more harm.
 */
bool ftrace_graph_is_dead(void)
{
	return kill_ftrace_graph;
}

/**
 * ftrace_graph_stop - set to permanently disable function graph tracincg
 *
 * In case of an error int function graph tracing, this is called
 * to try to keep function graph tracing from causing any more harm.
 * Usually this is pretty severe and this is called to try to at least
 * get a warning out to the user.
 */
void ftrace_graph_stop(void)
{
	kill_ftrace_graph = true;
}

/* Add a function return address to the trace stack on thread info.*/
static int
ftrace_push_return_trace(unsigned long ret, unsigned long func,
			 unsigned long frame_pointer, unsigned long *retp)
{
	unsigned long long calltime;
	int index;

	if (unlikely(ftrace_graph_is_dead()))
		return -EBUSY;

	if (!current->ret_stack)
		return -EBUSY;

	/*
	 * We must make sure the ret_stack is tested before we read
	 * anything else.
	 */
	smp_rmb();

	/* The return trace stack is full */
	if (current->curr_ret_stack == FTRACE_RETFUNC_DEPTH - 1) {
		atomic_inc(&current->trace_overrun);
		return -EBUSY;
	}

	calltime = trace_clock_local();

	index = ++current->curr_ret_stack;
	barrier();
	current->ret_stack[index].ret = ret;
	current->ret_stack[index].func = func;
	current->ret_stack[index].calltime = calltime;
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	current->ret_stack[index].fp = frame_pointer;
#endif
#ifdef HAVE_FUNCTION_GRAPH_RET_ADDR_PTR
	current->ret_stack[index].retp = retp;
#endif
	return 0;
}

int function_graph_enter(unsigned long ret, unsigned long func,
			 unsigned long frame_pointer, unsigned long *retp)
{
	struct ftrace_graph_ent trace;

	trace.func = func;
	trace.depth = ++current->curr_ret_depth;

	if (ftrace_push_return_trace(ret, func, frame_pointer, retp))
		goto out;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace))
		goto out_ret;

	return 0;
 out_ret:
	current->curr_ret_stack--;
 out:
	current->curr_ret_depth--;
	return -EBUSY;
}

/* Retrieve a function return address to the trace stack on thread info.*/
static void
ftrace_pop_return_trace(struct ftrace_graph_ret *trace, unsigned long *ret,
			unsigned long frame_pointer)
{
	int index;

	index = current->curr_ret_stack;

	if (unlikely(index < 0 || index >= FTRACE_RETFUNC_DEPTH)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic, otherwise we have no where to go */
		*ret = (unsigned long)panic;
		return;
	}

#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	/*
	 * The arch may choose to record the frame pointer used
	 * and check it here to make sure that it is what we expect it
	 * to be. If gcc does not set the place holder of the return
	 * address in the frame pointer, and does a copy instead, then
	 * the function graph trace will fail. This test detects this
	 * case.
	 *
	 * Currently, x86_32 with optimize for size (-Os) makes the latest
	 * gcc do the above.
	 *
	 * Note, -mfentry does not use frame pointers, and this test
	 *  is not needed if CC_USING_FENTRY is set.
	 */
	if (unlikely(current->ret_stack[index].fp != frame_pointer)) {
		ftrace_graph_stop();
		WARN(1, "Bad frame pointer: expected %lx, received %lx\n"
		     "  from func %ps return to %lx\n",
		     current->ret_stack[index].fp,
		     frame_pointer,
		     (void *)current->ret_stack[index].func,
		     current->ret_stack[index].ret);
		*ret = (unsigned long)panic;
		return;
	}
#endif

	*ret = current->ret_stack[index].ret;
	trace->func = current->ret_stack[index].func;
	trace->calltime = current->ret_stack[index].calltime;
	trace->overrun = atomic_read(&current->trace_overrun);
	trace->depth = current->curr_ret_depth--;
	/*
	 * We still want to trace interrupts coming in if
	 * max_depth is set to 1. Make sure the decrement is
	 * seen before ftrace_graph_return.
	 */
	barrier();
}

/*
 * Hibernation protection.
 * The state of the current task is too much unstable during
 * suspend/restore to disk. We want to protect against that.
 */
static int
ftrace_suspend_notifier_call(struct notifier_block *bl, unsigned long state,
							void *unused)
{
	switch (state) {
	case PM_HIBERNATION_PREPARE:
		pause_graph_tracing();
		break;

	case PM_POST_HIBERNATION:
		unpause_graph_tracing();
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block ftrace_suspend_notifier = {
	.notifier_call = ftrace_suspend_notifier_call,
};

/*
 * Send the trace to the ring-buffer.
 * @return the original return address.
 */
unsigned long ftrace_return_to_handler(unsigned long frame_pointer)
{
	struct ftrace_graph_ret trace;
	unsigned long ret;

	ftrace_pop_return_trace(&trace, &ret, frame_pointer);
	trace.rettime = trace_clock_local();
	ftrace_graph_return(&trace);
	/*
	 * The ftrace_graph_return() may still access the current
	 * ret_stack structure, we need to make sure the update of
	 * curr_ret_stack is after that.
	 */
	barrier();
	current->curr_ret_stack--;

	if (unlikely(!ret)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic. What else to do? */
		ret = (unsigned long)panic;
	}

	return ret;
}

/**
 * ftrace_graph_get_ret_stack - return the entry of the shadow stack
 * @task: The task to read the shadow stack from
 * @idx: Index down the shadow stack
 *
 * Return the ret_struct on the shadow stack of the @task at the
 * call graph at @idx starting with zero. If @idx is zero, it
 * will return the last saved ret_stack entry. If it is greater than
 * zero, it will return the corresponding ret_stack for the depth
 * of saved return addresses.
 */
struct ftrace_ret_stack *
ftrace_graph_get_ret_stack(struct task_struct *task, int idx)
{
	idx = task->curr_ret_stack - idx;

	if (idx >= 0 && idx <= task->curr_ret_stack)
		return &task->ret_stack[idx];

	return NULL;
}

/**
 * ftrace_graph_ret_addr - convert a potentially modified stack return address
 *			   to its original value
 *
 * This function can be called by stack unwinding code to convert a found stack
 * return address ('ret') to its original value, in case the function graph
 * tracer has modified it to be 'return_to_handler'.  If the address hasn't
 * been modified, the unchanged value of 'ret' is returned.
 *
 * 'idx' is a state variable which should be initialized by the caller to zero
 * before the first call.
 *
 * 'retp' is a pointer to the return address on the stack.  It's ignored if
 * the arch doesn't have HAVE_FUNCTION_GRAPH_RET_ADDR_PTR defined.
 */
#ifdef HAVE_FUNCTION_GRAPH_RET_ADDR_PTR
unsigned long ftrace_graph_ret_addr(struct task_struct *task, int *idx,
				    unsigned long ret, unsigned long *retp)
{
	int index = task->curr_ret_stack;
	int i;

	if (ret != (unsigned long)return_to_handler)
		return ret;

	if (index < 0)
		return ret;

	for (i = 0; i <= index; i++)
		if (task->ret_stack[i].retp == retp)
			return task->ret_stack[i].ret;

	return ret;
}
#else /* !HAVE_FUNCTION_GRAPH_RET_ADDR_PTR */
unsigned long ftrace_graph_ret_addr(struct task_struct *task, int *idx,
				    unsigned long ret, unsigned long *retp)
{
	int task_idx;

	if (ret != (unsigned long)return_to_handler)
		return ret;

	task_idx = task->curr_ret_stack;

	if (!task->ret_stack || task_idx < *idx)
		return ret;

	task_idx -= *idx;
	(*idx)++;

	return task->ret_stack[task_idx].ret;
}
#endif /* HAVE_FUNCTION_GRAPH_RET_ADDR_PTR */

static struct ftrace_ops graph_ops = {
	.func			= ftrace_stub,
	.flags			= FTRACE_OPS_FL_RECURSION_SAFE |
				   FTRACE_OPS_FL_INITIALIZED |
				   FTRACE_OPS_FL_PID |
				   FTRACE_OPS_FL_STUB,
#ifdef FTRACE_GRAPH_TRAMP_ADDR
	.trampoline		= FTRACE_GRAPH_TRAMP_ADDR,
	/* trampoline_size is only needed for dynamically allocated tramps */
#endif
	ASSIGN_OPS_HASH(graph_ops, &global_ops.local_hash)
};

void ftrace_graph_sleep_time_control(bool enable)
{
	fgraph_sleep_time = enable;
}

int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace)
{
	return 0;
}

/* The callbacks that hook a function */
trace_func_graph_ret_t ftrace_graph_return =
			(trace_func_graph_ret_t)ftrace_stub;
trace_func_graph_ent_t ftrace_graph_entry = ftrace_graph_entry_stub;
static trace_func_graph_ent_t __ftrace_graph_entry = ftrace_graph_entry_stub;

/* Try to assign a return stack array on FTRACE_RETSTACK_ALLOC_SIZE tasks. */
static int alloc_retstack_tasklist(struct ftrace_ret_stack **ret_stack_list)
{
	int i;
	int ret = 0;
	int start = 0, end = FTRACE_RETSTACK_ALLOC_SIZE;
	struct task_struct *g, *t;

	for (i = 0; i < FTRACE_RETSTACK_ALLOC_SIZE; i++) {
		ret_stack_list[i] =
			kmalloc_array(FTRACE_RETFUNC_DEPTH,
				      sizeof(struct ftrace_ret_stack),
				      GFP_KERNEL);
		if (!ret_stack_list[i]) {
			start = 0;
			end = i;
			ret = -ENOMEM;
			goto free;
		}
	}

	read_lock(&tasklist_lock);
	do_each_thread(g, t) {
		if (start == end) {
			ret = -EAGAIN;
			goto unlock;
		}

		if (t->ret_stack == NULL) {
			atomic_set(&t->tracing_graph_pause, 0);
			atomic_set(&t->trace_overrun, 0);
			t->curr_ret_stack = -1;
			t->curr_ret_depth = -1;
			/* Make sure the tasks see the -1 first: */
			smp_wmb();
			t->ret_stack = ret_stack_list[start++];
		}
	} while_each_thread(g, t);

unlock:
	read_unlock(&tasklist_lock);
free:
	for (i = start; i < end; i++)
		kfree(ret_stack_list[i]);
	return ret;
}

static void
ftrace_graph_probe_sched_switch(void *ignore, bool preempt,
			struct task_struct *prev, struct task_struct *next)
{
	unsigned long long timestamp;
	int index;

	/*
	 * Does the user want to count the time a function was asleep.
	 * If so, do not update the time stamps.
	 */
	if (fgraph_sleep_time)
		return;

	timestamp = trace_clock_local();

	prev->ftrace_timestamp = timestamp;

	/* only process tasks that we timestamped */
	if (!next->ftrace_timestamp)
		return;

	/*
	 * Update all the counters in next to make up for the
	 * time next was sleeping.
	 */
	timestamp -= next->ftrace_timestamp;

	for (index = next->curr_ret_stack; index >= 0; index--)
		next->ret_stack[index].calltime += timestamp;
}

static int ftrace_graph_entry_test(struct ftrace_graph_ent *trace)
{
	if (!ftrace_ops_test(&global_ops, trace->func, NULL))
		return 0;
	return __ftrace_graph_entry(trace);
}

/*
 * The function graph tracer should only trace the functions defined
 * by set_ftrace_filter and set_ftrace_notrace. If another function
 * tracer ops is registered, the graph tracer requires testing the
 * function against the global ops, and not just trace any function
 * that any ftrace_ops registered.
 */
void update_function_graph_func(void)
{
	struct ftrace_ops *op;
	bool do_test = false;

	/*
	 * The graph and global ops share the same set of functions
	 * to test. If any other ops is on the list, then
	 * the graph tracing needs to test if its the function
	 * it should call.
	 */
	do_for_each_ftrace_op(op, ftrace_ops_list) {
		if (op != &global_ops && op != &graph_ops &&
		    op != &ftrace_list_end) {
			do_test = true;
			/* in double loop, break out with goto */
			goto out;
		}
	} while_for_each_ftrace_op(op);
 out:
	if (do_test)
		ftrace_graph_entry = ftrace_graph_entry_test;
	else
		ftrace_graph_entry = __ftrace_graph_entry;
}

static DEFINE_PER_CPU(struct ftrace_ret_stack *, idle_ret_stack);

static void
graph_init_task(struct task_struct *t, struct ftrace_ret_stack *ret_stack)
{
	atomic_set(&t->tracing_graph_pause, 0);
	atomic_set(&t->trace_overrun, 0);
	t->ftrace_timestamp = 0;
	/* make curr_ret_stack visible before we add the ret_stack */
	smp_wmb();
	t->ret_stack = ret_stack;
}

/*
 * Allocate a return stack for the idle task. May be the first
 * time through, or it may be done by CPU hotplug online.
 */
void ftrace_graph_init_idle_task(struct task_struct *t, int cpu)
{
	t->curr_ret_stack = -1;
	t->curr_ret_depth = -1;
	/*
	 * The idle task has no parent, it either has its own
	 * stack or no stack at all.
	 */
	if (t->ret_stack)
		WARN_ON(t->ret_stack != per_cpu(idle_ret_stack, cpu));

	if (ftrace_graph_active) {
		struct ftrace_ret_stack *ret_stack;

		ret_stack = per_cpu(idle_ret_stack, cpu);
		if (!ret_stack) {
			ret_stack =
				kmalloc_array(FTRACE_RETFUNC_DEPTH,
					      sizeof(struct ftrace_ret_stack),
					      GFP_KERNEL);
			if (!ret_stack)
				return;
			per_cpu(idle_ret_stack, cpu) = ret_stack;
		}
		graph_init_task(t, ret_stack);
	}
}

/* Allocate a return stack for newly created task */
void ftrace_graph_init_task(struct task_struct *t)
{
	/* Make sure we do not use the parent ret_stack */
	t->ret_stack = NULL;
	t->curr_ret_stack = -1;
	t->curr_ret_depth = -1;

	if (ftrace_graph_active) {
		struct ftrace_ret_stack *ret_stack;

		ret_stack = kmalloc_array(FTRACE_RETFUNC_DEPTH,
					  sizeof(struct ftrace_ret_stack),
					  GFP_KERNEL);
		if (!ret_stack)
			return;
		graph_init_task(t, ret_stack);
	}
}

void ftrace_graph_exit_task(struct task_struct *t)
{
	struct ftrace_ret_stack	*ret_stack = t->ret_stack;

	t->ret_stack = NULL;
	/* NULL must become visible to IRQs before we free it: */
	barrier();

	kfree(ret_stack);
}

/* Allocate a return stack for each task */
static int start_graph_tracing(void)
{
	struct ftrace_ret_stack **ret_stack_list;
	int ret, cpu;

	ret_stack_list = kmalloc_array(FTRACE_RETSTACK_ALLOC_SIZE,
				       sizeof(struct ftrace_ret_stack *),
				       GFP_KERNEL);

	if (!ret_stack_list)
		return -ENOMEM;

	/* The cpu_boot init_task->ret_stack will never be freed */
	for_each_online_cpu(cpu) {
		if (!idle_task(cpu)->ret_stack)
			ftrace_graph_init_idle_task(idle_task(cpu), cpu);
	}

	do {
		ret = alloc_retstack_tasklist(ret_stack_list);
	} while (ret == -EAGAIN);

	if (!ret) {
		ret = register_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);
		if (ret)
			pr_info("ftrace_graph: Couldn't activate tracepoint"
				" probe to kernel_sched_switch\n");
	}

	kfree(ret_stack_list);
	return ret;
}

int register_ftrace_graph(struct fgraph_ops *gops)
{
	int ret = 0;

	mutex_lock(&ftrace_lock);

	/* we currently allow only one tracer registered at a time */
	if (ftrace_graph_active) {
		ret = -EBUSY;
		goto out;
	}

	register_pm_notifier(&ftrace_suspend_notifier);

	ftrace_graph_active++;
	ret = start_graph_tracing();
	if (ret) {
		ftrace_graph_active--;
		goto out;
	}

	ftrace_graph_return = gops->retfunc;

	/*
	 * Update the indirect function to the entryfunc, and the
	 * function that gets called to the entry_test first. Then
	 * call the update fgraph entry function to determine if
	 * the entryfunc should be called directly or not.
	 */
	__ftrace_graph_entry = gops->entryfunc;
	ftrace_graph_entry = ftrace_graph_entry_test;
	update_function_graph_func();

	ret = ftrace_startup(&graph_ops, FTRACE_START_FUNC_RET);
out:
	mutex_unlock(&ftrace_lock);
	return ret;
}

void unregister_ftrace_graph(struct fgraph_ops *gops)
{
	mutex_lock(&ftrace_lock);

	if (unlikely(!ftrace_graph_active))
		goto out;

	ftrace_graph_active--;
	ftrace_graph_return = (trace_func_graph_ret_t)ftrace_stub;
	ftrace_graph_entry = ftrace_graph_entry_stub;
	__ftrace_graph_entry = ftrace_graph_entry_stub;
	ftrace_shutdown(&graph_ops, FTRACE_STOP_FUNC_RET);
	unregister_pm_notifier(&ftrace_suspend_notifier);
	unregister_trace_sched_switch(ftrace_graph_probe_sched_switch, NULL);

 out:
	mutex_unlock(&ftrace_lock);
}
