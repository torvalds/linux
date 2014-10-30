/*
 * thread-stack.c: Synthesize a thread's stack using call / return events
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include "thread.h"
#include "event.h"
#include "util.h"
#include "debug.h"
#include "thread-stack.h"

#define STACK_GROWTH 4096

struct thread_stack_entry {
	u64 ret_addr;
};

struct thread_stack {
	struct thread_stack_entry *stack;
	size_t cnt;
	size_t sz;
	u64 trace_nr;
};

static int thread_stack__grow(struct thread_stack *ts)
{
	struct thread_stack_entry *new_stack;
	size_t sz, new_sz;

	new_sz = ts->sz + STACK_GROWTH;
	sz = new_sz * sizeof(struct thread_stack_entry);

	new_stack = realloc(ts->stack, sz);
	if (!new_stack)
		return -ENOMEM;

	ts->stack = new_stack;
	ts->sz = new_sz;

	return 0;
}

static struct thread_stack *thread_stack__new(void)
{
	struct thread_stack *ts;

	ts = zalloc(sizeof(struct thread_stack));
	if (!ts)
		return NULL;

	if (thread_stack__grow(ts)) {
		free(ts);
		return NULL;
	}

	return ts;
}

static int thread_stack__push(struct thread_stack *ts, u64 ret_addr)
{
	int err = 0;

	if (ts->cnt == ts->sz) {
		err = thread_stack__grow(ts);
		if (err) {
			pr_warning("Out of memory: discarding thread stack\n");
			ts->cnt = 0;
		}
	}

	ts->stack[ts->cnt++].ret_addr = ret_addr;

	return err;
}

static void thread_stack__pop(struct thread_stack *ts, u64 ret_addr)
{
	size_t i;

	/*
	 * In some cases there may be functions which are not seen to return.
	 * For example when setjmp / longjmp has been used.  Or the perf context
	 * switch in the kernel which doesn't stop and start tracing in exactly
	 * the same code path.  When that happens the return address will be
	 * further down the stack.  If the return address is not found at all,
	 * we assume the opposite (i.e. this is a return for a call that wasn't
	 * seen for some reason) and leave the stack alone.
	 */
	for (i = ts->cnt; i; ) {
		if (ts->stack[--i].ret_addr == ret_addr) {
			ts->cnt = i;
			return;
		}
	}
}

int thread_stack__event(struct thread *thread, u32 flags, u64 from_ip,
			u64 to_ip, u16 insn_len, u64 trace_nr)
{
	if (!thread)
		return -EINVAL;

	if (!thread->ts) {
		thread->ts = thread_stack__new();
		if (!thread->ts) {
			pr_warning("Out of memory: no thread stack\n");
			return -ENOMEM;
		}
		thread->ts->trace_nr = trace_nr;
	}

	/*
	 * When the trace is discontinuous, the trace_nr changes.  In that case
	 * the stack might be completely invalid.  Better to report nothing than
	 * to report something misleading, so reset the stack count to zero.
	 */
	if (trace_nr != thread->ts->trace_nr) {
		thread->ts->trace_nr = trace_nr;
		thread->ts->cnt = 0;
	}

	if (flags & PERF_IP_FLAG_CALL) {
		u64 ret_addr;

		if (!to_ip)
			return 0;
		ret_addr = from_ip + insn_len;
		if (ret_addr == to_ip)
			return 0; /* Zero-length calls are excluded */
		return thread_stack__push(thread->ts, ret_addr);
	} else if (flags & PERF_IP_FLAG_RETURN) {
		if (!from_ip)
			return 0;
		thread_stack__pop(thread->ts, to_ip);
	}

	return 0;
}

void thread_stack__free(struct thread *thread)
{
	if (thread->ts) {
		zfree(&thread->ts->stack);
		zfree(&thread->ts);
	}
}

void thread_stack__sample(struct thread *thread, struct ip_callchain *chain,
			  size_t sz, u64 ip)
{
	size_t i;

	if (!thread || !thread->ts)
		chain->nr = 1;
	else
		chain->nr = min(sz, thread->ts->cnt + 1);

	chain->ips[0] = ip;

	for (i = 1; i < chain->nr; i++)
		chain->ips[i] = thread->ts->stack[thread->ts->cnt - i].ret_addr;
}
