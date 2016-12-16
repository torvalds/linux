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

#include <linux/rbtree.h>
#include <linux/list.h>
#include "thread.h"
#include "event.h"
#include "machine.h"
#include "util.h"
#include "debug.h"
#include "symbol.h"
#include "comm.h"
#include "call-path.h"
#include "thread-stack.h"

#define STACK_GROWTH 2048

/**
 * struct thread_stack_entry - thread stack entry.
 * @ret_addr: return address
 * @timestamp: timestamp (if known)
 * @ref: external reference (e.g. db_id of sample)
 * @branch_count: the branch count when the entry was created
 * @cp: call path
 * @no_call: a 'call' was not seen
 */
struct thread_stack_entry {
	u64 ret_addr;
	u64 timestamp;
	u64 ref;
	u64 branch_count;
	struct call_path *cp;
	bool no_call;
};

/**
 * struct thread_stack - thread stack constructed from 'call' and 'return'
 *                       branch samples.
 * @stack: array that holds the stack
 * @cnt: number of entries in the stack
 * @sz: current maximum stack size
 * @trace_nr: current trace number
 * @branch_count: running branch count
 * @kernel_start: kernel start address
 * @last_time: last timestamp
 * @crp: call/return processor
 * @comm: current comm
 */
struct thread_stack {
	struct thread_stack_entry *stack;
	size_t cnt;
	size_t sz;
	u64 trace_nr;
	u64 branch_count;
	u64 kernel_start;
	u64 last_time;
	struct call_return_processor *crp;
	struct comm *comm;
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

static struct thread_stack *thread_stack__new(struct thread *thread,
					      struct call_return_processor *crp)
{
	struct thread_stack *ts;

	ts = zalloc(sizeof(struct thread_stack));
	if (!ts)
		return NULL;

	if (thread_stack__grow(ts)) {
		free(ts);
		return NULL;
	}

	if (thread->mg && thread->mg->machine)
		ts->kernel_start = machine__kernel_start(thread->mg->machine);
	else
		ts->kernel_start = 1ULL << 63;
	ts->crp = crp;

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

static bool thread_stack__in_kernel(struct thread_stack *ts)
{
	if (!ts->cnt)
		return false;

	return ts->stack[ts->cnt - 1].cp->in_kernel;
}

static int thread_stack__call_return(struct thread *thread,
				     struct thread_stack *ts, size_t idx,
				     u64 timestamp, u64 ref, bool no_return)
{
	struct call_return_processor *crp = ts->crp;
	struct thread_stack_entry *tse;
	struct call_return cr = {
		.thread = thread,
		.comm = ts->comm,
		.db_id = 0,
	};

	tse = &ts->stack[idx];
	cr.cp = tse->cp;
	cr.call_time = tse->timestamp;
	cr.return_time = timestamp;
	cr.branch_count = ts->branch_count - tse->branch_count;
	cr.call_ref = tse->ref;
	cr.return_ref = ref;
	if (tse->no_call)
		cr.flags |= CALL_RETURN_NO_CALL;
	if (no_return)
		cr.flags |= CALL_RETURN_NO_RETURN;

	return crp->process(&cr, crp->data);
}

static int __thread_stack__flush(struct thread *thread, struct thread_stack *ts)
{
	struct call_return_processor *crp = ts->crp;
	int err;

	if (!crp) {
		ts->cnt = 0;
		return 0;
	}

	while (ts->cnt) {
		err = thread_stack__call_return(thread, ts, --ts->cnt,
						ts->last_time, 0, true);
		if (err) {
			pr_err("Error flushing thread stack!\n");
			ts->cnt = 0;
			return err;
		}
	}

	return 0;
}

int thread_stack__flush(struct thread *thread)
{
	if (thread->ts)
		return __thread_stack__flush(thread, thread->ts);

	return 0;
}

int thread_stack__event(struct thread *thread, u32 flags, u64 from_ip,
			u64 to_ip, u16 insn_len, u64 trace_nr)
{
	if (!thread)
		return -EINVAL;

	if (!thread->ts) {
		thread->ts = thread_stack__new(thread, NULL);
		if (!thread->ts) {
			pr_warning("Out of memory: no thread stack\n");
			return -ENOMEM;
		}
		thread->ts->trace_nr = trace_nr;
	}

	/*
	 * When the trace is discontinuous, the trace_nr changes.  In that case
	 * the stack might be completely invalid.  Better to report nothing than
	 * to report something misleading, so flush the stack.
	 */
	if (trace_nr != thread->ts->trace_nr) {
		if (thread->ts->trace_nr)
			__thread_stack__flush(thread, thread->ts);
		thread->ts->trace_nr = trace_nr;
	}

	/* Stop here if thread_stack__process() is in use */
	if (thread->ts->crp)
		return 0;

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

void thread_stack__set_trace_nr(struct thread *thread, u64 trace_nr)
{
	if (!thread || !thread->ts)
		return;

	if (trace_nr != thread->ts->trace_nr) {
		if (thread->ts->trace_nr)
			__thread_stack__flush(thread, thread->ts);
		thread->ts->trace_nr = trace_nr;
	}
}

void thread_stack__free(struct thread *thread)
{
	if (thread->ts) {
		__thread_stack__flush(thread, thread->ts);
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

struct call_return_processor *
call_return_processor__new(int (*process)(struct call_return *cr, void *data),
			   void *data)
{
	struct call_return_processor *crp;

	crp = zalloc(sizeof(struct call_return_processor));
	if (!crp)
		return NULL;
	crp->cpr = call_path_root__new();
	if (!crp->cpr)
		goto out_free;
	crp->process = process;
	crp->data = data;
	return crp;

out_free:
	free(crp);
	return NULL;
}

void call_return_processor__free(struct call_return_processor *crp)
{
	if (crp) {
		call_path_root__free(crp->cpr);
		free(crp);
	}
}

static int thread_stack__push_cp(struct thread_stack *ts, u64 ret_addr,
				 u64 timestamp, u64 ref, struct call_path *cp,
				 bool no_call)
{
	struct thread_stack_entry *tse;
	int err;

	if (ts->cnt == ts->sz) {
		err = thread_stack__grow(ts);
		if (err)
			return err;
	}

	tse = &ts->stack[ts->cnt++];
	tse->ret_addr = ret_addr;
	tse->timestamp = timestamp;
	tse->ref = ref;
	tse->branch_count = ts->branch_count;
	tse->cp = cp;
	tse->no_call = no_call;

	return 0;
}

static int thread_stack__pop_cp(struct thread *thread, struct thread_stack *ts,
				u64 ret_addr, u64 timestamp, u64 ref,
				struct symbol *sym)
{
	int err;

	if (!ts->cnt)
		return 1;

	if (ts->cnt == 1) {
		struct thread_stack_entry *tse = &ts->stack[0];

		if (tse->cp->sym == sym)
			return thread_stack__call_return(thread, ts, --ts->cnt,
							 timestamp, ref, false);
	}

	if (ts->stack[ts->cnt - 1].ret_addr == ret_addr) {
		return thread_stack__call_return(thread, ts, --ts->cnt,
						 timestamp, ref, false);
	} else {
		size_t i = ts->cnt - 1;

		while (i--) {
			if (ts->stack[i].ret_addr != ret_addr)
				continue;
			i += 1;
			while (ts->cnt > i) {
				err = thread_stack__call_return(thread, ts,
								--ts->cnt,
								timestamp, ref,
								true);
				if (err)
					return err;
			}
			return thread_stack__call_return(thread, ts, --ts->cnt,
							 timestamp, ref, false);
		}
	}

	return 1;
}

static int thread_stack__bottom(struct thread *thread, struct thread_stack *ts,
				struct perf_sample *sample,
				struct addr_location *from_al,
				struct addr_location *to_al, u64 ref)
{
	struct call_path_root *cpr = ts->crp->cpr;
	struct call_path *cp;
	struct symbol *sym;
	u64 ip;

	if (sample->ip) {
		ip = sample->ip;
		sym = from_al->sym;
	} else if (sample->addr) {
		ip = sample->addr;
		sym = to_al->sym;
	} else {
		return 0;
	}

	cp = call_path__findnew(cpr, &cpr->call_path, sym, ip,
				ts->kernel_start);
	if (!cp)
		return -ENOMEM;

	return thread_stack__push_cp(thread->ts, ip, sample->time, ref, cp,
				     true);
}

static int thread_stack__no_call_return(struct thread *thread,
					struct thread_stack *ts,
					struct perf_sample *sample,
					struct addr_location *from_al,
					struct addr_location *to_al, u64 ref)
{
	struct call_path_root *cpr = ts->crp->cpr;
	struct call_path *cp, *parent;
	u64 ks = ts->kernel_start;
	int err;

	if (sample->ip >= ks && sample->addr < ks) {
		/* Return to userspace, so pop all kernel addresses */
		while (thread_stack__in_kernel(ts)) {
			err = thread_stack__call_return(thread, ts, --ts->cnt,
							sample->time, ref,
							true);
			if (err)
				return err;
		}

		/* If the stack is empty, push the userspace address */
		if (!ts->cnt) {
			cp = call_path__findnew(cpr, &cpr->call_path,
						to_al->sym, sample->addr,
						ts->kernel_start);
			if (!cp)
				return -ENOMEM;
			return thread_stack__push_cp(ts, 0, sample->time, ref,
						     cp, true);
		}
	} else if (thread_stack__in_kernel(ts) && sample->ip < ks) {
		/* Return to userspace, so pop all kernel addresses */
		while (thread_stack__in_kernel(ts)) {
			err = thread_stack__call_return(thread, ts, --ts->cnt,
							sample->time, ref,
							true);
			if (err)
				return err;
		}
	}

	if (ts->cnt)
		parent = ts->stack[ts->cnt - 1].cp;
	else
		parent = &cpr->call_path;

	/* This 'return' had no 'call', so push and pop top of stack */
	cp = call_path__findnew(cpr, parent, from_al->sym, sample->ip,
				ts->kernel_start);
	if (!cp)
		return -ENOMEM;

	err = thread_stack__push_cp(ts, sample->addr, sample->time, ref, cp,
				    true);
	if (err)
		return err;

	return thread_stack__pop_cp(thread, ts, sample->addr, sample->time, ref,
				    to_al->sym);
}

static int thread_stack__trace_begin(struct thread *thread,
				     struct thread_stack *ts, u64 timestamp,
				     u64 ref)
{
	struct thread_stack_entry *tse;
	int err;

	if (!ts->cnt)
		return 0;

	/* Pop trace end */
	tse = &ts->stack[ts->cnt - 1];
	if (tse->cp->sym == NULL && tse->cp->ip == 0) {
		err = thread_stack__call_return(thread, ts, --ts->cnt,
						timestamp, ref, false);
		if (err)
			return err;
	}

	return 0;
}

static int thread_stack__trace_end(struct thread_stack *ts,
				   struct perf_sample *sample, u64 ref)
{
	struct call_path_root *cpr = ts->crp->cpr;
	struct call_path *cp;
	u64 ret_addr;

	/* No point having 'trace end' on the bottom of the stack */
	if (!ts->cnt || (ts->cnt == 1 && ts->stack[0].ref == ref))
		return 0;

	cp = call_path__findnew(cpr, ts->stack[ts->cnt - 1].cp, NULL, 0,
				ts->kernel_start);
	if (!cp)
		return -ENOMEM;

	ret_addr = sample->ip + sample->insn_len;

	return thread_stack__push_cp(ts, ret_addr, sample->time, ref, cp,
				     false);
}

int thread_stack__process(struct thread *thread, struct comm *comm,
			  struct perf_sample *sample,
			  struct addr_location *from_al,
			  struct addr_location *to_al, u64 ref,
			  struct call_return_processor *crp)
{
	struct thread_stack *ts = thread->ts;
	int err = 0;

	if (ts) {
		if (!ts->crp) {
			/* Supersede thread_stack__event() */
			thread_stack__free(thread);
			thread->ts = thread_stack__new(thread, crp);
			if (!thread->ts)
				return -ENOMEM;
			ts = thread->ts;
			ts->comm = comm;
		}
	} else {
		thread->ts = thread_stack__new(thread, crp);
		if (!thread->ts)
			return -ENOMEM;
		ts = thread->ts;
		ts->comm = comm;
	}

	/* Flush stack on exec */
	if (ts->comm != comm && thread->pid_ == thread->tid) {
		err = __thread_stack__flush(thread, ts);
		if (err)
			return err;
		ts->comm = comm;
	}

	/* If the stack is empty, put the current symbol on the stack */
	if (!ts->cnt) {
		err = thread_stack__bottom(thread, ts, sample, from_al, to_al,
					   ref);
		if (err)
			return err;
	}

	ts->branch_count += 1;
	ts->last_time = sample->time;

	if (sample->flags & PERF_IP_FLAG_CALL) {
		struct call_path_root *cpr = ts->crp->cpr;
		struct call_path *cp;
		u64 ret_addr;

		if (!sample->ip || !sample->addr)
			return 0;

		ret_addr = sample->ip + sample->insn_len;
		if (ret_addr == sample->addr)
			return 0; /* Zero-length calls are excluded */

		cp = call_path__findnew(cpr, ts->stack[ts->cnt - 1].cp,
					to_al->sym, sample->addr,
					ts->kernel_start);
		if (!cp)
			return -ENOMEM;
		err = thread_stack__push_cp(ts, ret_addr, sample->time, ref,
					    cp, false);
	} else if (sample->flags & PERF_IP_FLAG_RETURN) {
		if (!sample->ip || !sample->addr)
			return 0;

		err = thread_stack__pop_cp(thread, ts, sample->addr,
					   sample->time, ref, from_al->sym);
		if (err) {
			if (err < 0)
				return err;
			err = thread_stack__no_call_return(thread, ts, sample,
							   from_al, to_al, ref);
		}
	} else if (sample->flags & PERF_IP_FLAG_TRACE_BEGIN) {
		err = thread_stack__trace_begin(thread, ts, sample->time, ref);
	} else if (sample->flags & PERF_IP_FLAG_TRACE_END) {
		err = thread_stack__trace_end(ts, sample, ref);
	}

	return err;
}

size_t thread_stack__depth(struct thread *thread)
{
	if (!thread->ts)
		return 0;
	return thread->ts->cnt;
}
