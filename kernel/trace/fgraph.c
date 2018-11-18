// SPDX-License-Identifier: GPL-2.0
/*
 * Infrastructure to took into function calls and returns.
 * Copyright (c) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 * Mostly borrowed from function tracer which
 * is Copyright (c) Steven Rostedt <srostedt@redhat.com>
 *
 * Highly modified by Steven Rostedt (VMware).
 */
#include <linux/ftrace.h>

#include "trace.h"

static bool kill_ftrace_graph;

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
