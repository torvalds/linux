// SPDX-License-Identifier: GPL-2.0-only
/*
 * thread-stack.c: Synthesize a thread's stack using call / return events
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <stdlib.h>
#include "thread.h"
#include "event.h"
#include "machine.h"
#include "env.h"
#include "debug.h"
#include "symbol.h"
#include "comm.h"
#include "call-path.h"
#include "thread-stack.h"

#define STACK_GROWTH 2048

/*
 * State of retpoline detection.
 *
 * RETPOLINE_NONE: no retpoline detection
 * X86_RETPOLINE_POSSIBLE: x86 retpoline possible
 * X86_RETPOLINE_DETECTED: x86 retpoline detected
 */
enum retpoline_state_t {
	RETPOLINE_NONE,
	X86_RETPOLINE_POSSIBLE,
	X86_RETPOLINE_DETECTED,
};

/**
 * struct thread_stack_entry - thread stack entry.
 * @ret_addr: return address
 * @timestamp: timestamp (if known)
 * @ref: external reference (e.g. db_id of sample)
 * @branch_count: the branch count when the entry was created
 * @insn_count: the instruction count when the entry was created
 * @cyc_count the cycle count when the entry was created
 * @db_id: id used for db-export
 * @cp: call path
 * @no_call: a 'call' was not seen
 * @trace_end: a 'call' but trace ended
 * @non_call: a branch but not a 'call' to the start of a different symbol
 */
struct thread_stack_entry {
	u64 ret_addr;
	u64 timestamp;
	u64 ref;
	u64 branch_count;
	u64 insn_count;
	u64 cyc_count;
	u64 db_id;
	struct call_path *cp;
	bool no_call;
	bool trace_end;
	bool non_call;
};

/**
 * struct thread_stack - thread stack constructed from 'call' and 'return'
 *                       branch samples.
 * @stack: array that holds the stack
 * @cnt: number of entries in the stack
 * @sz: current maximum stack size
 * @trace_nr: current trace number
 * @branch_count: running branch count
 * @insn_count: running  instruction count
 * @cyc_count running  cycle count
 * @kernel_start: kernel start address
 * @last_time: last timestamp
 * @crp: call/return processor
 * @comm: current comm
 * @arr_sz: size of array if this is the first element of an array
 * @rstate: used to detect retpolines
 */
struct thread_stack {
	struct thread_stack_entry *stack;
	size_t cnt;
	size_t sz;
	u64 trace_nr;
	u64 branch_count;
	u64 insn_count;
	u64 cyc_count;
	u64 kernel_start;
	u64 last_time;
	struct call_return_processor *crp;
	struct comm *comm;
	unsigned int arr_sz;
	enum retpoline_state_t rstate;
};

/*
 * Assume pid == tid == 0 identifies the idle task as defined by
 * perf_session__register_idle_thread(). The idle task is really 1 task per cpu,
 * and therefore requires a stack for each cpu.
 */
static inline bool thread_stack__per_cpu(struct thread *thread)
{
	return !(thread->tid || thread->pid_);
}

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

static int thread_stack__init(struct thread_stack *ts, struct thread *thread,
			      struct call_return_processor *crp)
{
	int err;

	err = thread_stack__grow(ts);
	if (err)
		return err;

	if (thread->mg && thread->mg->machine) {
		struct machine *machine = thread->mg->machine;
		const char *arch = perf_env__arch(machine->env);

		ts->kernel_start = machine__kernel_start(machine);
		if (!strcmp(arch, "x86"))
			ts->rstate = X86_RETPOLINE_POSSIBLE;
	} else {
		ts->kernel_start = 1ULL << 63;
	}
	ts->crp = crp;

	return 0;
}

static struct thread_stack *thread_stack__new(struct thread *thread, int cpu,
					      struct call_return_processor *crp)
{
	struct thread_stack *ts = thread->ts, *new_ts;
	unsigned int old_sz = ts ? ts->arr_sz : 0;
	unsigned int new_sz = 1;

	if (thread_stack__per_cpu(thread) && cpu > 0)
		new_sz = roundup_pow_of_two(cpu + 1);

	if (!ts || new_sz > old_sz) {
		new_ts = calloc(new_sz, sizeof(*ts));
		if (!new_ts)
			return NULL;
		if (ts)
			memcpy(new_ts, ts, old_sz * sizeof(*ts));
		new_ts->arr_sz = new_sz;
		zfree(&thread->ts);
		thread->ts = new_ts;
		ts = new_ts;
	}

	if (thread_stack__per_cpu(thread) && cpu > 0 &&
	    (unsigned int)cpu < ts->arr_sz)
		ts += cpu;

	if (!ts->stack &&
	    thread_stack__init(ts, thread, crp))
		return NULL;

	return ts;
}

static struct thread_stack *thread__cpu_stack(struct thread *thread, int cpu)
{
	struct thread_stack *ts = thread->ts;

	if (cpu < 0)
		cpu = 0;

	if (!ts || (unsigned int)cpu >= ts->arr_sz)
		return NULL;

	ts += cpu;

	if (!ts->stack)
		return NULL;

	return ts;
}

static inline struct thread_stack *thread__stack(struct thread *thread,
						    int cpu)
{
	if (!thread)
		return NULL;

	if (thread_stack__per_cpu(thread))
		return thread__cpu_stack(thread, cpu);

	return thread->ts;
}

static int thread_stack__push(struct thread_stack *ts, u64 ret_addr,
			      bool trace_end)
{
	int err = 0;

	if (ts->cnt == ts->sz) {
		err = thread_stack__grow(ts);
		if (err) {
			pr_warning("Out of memory: discarding thread stack\n");
			ts->cnt = 0;
		}
	}

	ts->stack[ts->cnt].trace_end = trace_end;
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

static void thread_stack__pop_trace_end(struct thread_stack *ts)
{
	size_t i;

	for (i = ts->cnt; i; ) {
		if (ts->stack[--i].trace_end)
			ts->cnt = i;
		else
			return;
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
	u64 *parent_db_id;

	tse = &ts->stack[idx];
	cr.cp = tse->cp;
	cr.call_time = tse->timestamp;
	cr.return_time = timestamp;
	cr.branch_count = ts->branch_count - tse->branch_count;
	cr.insn_count = ts->insn_count - tse->insn_count;
	cr.cyc_count = ts->cyc_count - tse->cyc_count;
	cr.db_id = tse->db_id;
	cr.call_ref = tse->ref;
	cr.return_ref = ref;
	if (tse->no_call)
		cr.flags |= CALL_RETURN_NO_CALL;
	if (no_return)
		cr.flags |= CALL_RETURN_NO_RETURN;
	if (tse->non_call)
		cr.flags |= CALL_RETURN_NON_CALL;

	/*
	 * The parent db_id must be assigned before exporting the child. Note
	 * it is not possible to export the parent first because its information
	 * is not yet complete because its 'return' has not yet been processed.
	 */
	parent_db_id = idx ? &(tse - 1)->db_id : NULL;

	return crp->process(&cr, parent_db_id, crp->data);
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
	struct thread_stack *ts = thread->ts;
	unsigned int pos;
	int err = 0;

	if (ts) {
		for (pos = 0; pos < ts->arr_sz; pos++) {
			int ret = __thread_stack__flush(thread, ts + pos);

			if (ret)
				err = ret;
		}
	}

	return err;
}

int thread_stack__event(struct thread *thread, int cpu, u32 flags, u64 from_ip,
			u64 to_ip, u16 insn_len, u64 trace_nr)
{
	struct thread_stack *ts = thread__stack(thread, cpu);

	if (!thread)
		return -EINVAL;

	if (!ts) {
		ts = thread_stack__new(thread, cpu, NULL);
		if (!ts) {
			pr_warning("Out of memory: no thread stack\n");
			return -ENOMEM;
		}
		ts->trace_nr = trace_nr;
	}

	/*
	 * When the trace is discontinuous, the trace_nr changes.  In that case
	 * the stack might be completely invalid.  Better to report nothing than
	 * to report something misleading, so flush the stack.
	 */
	if (trace_nr != ts->trace_nr) {
		if (ts->trace_nr)
			__thread_stack__flush(thread, ts);
		ts->trace_nr = trace_nr;
	}

	/* Stop here if thread_stack__process() is in use */
	if (ts->crp)
		return 0;

	if (flags & PERF_IP_FLAG_CALL) {
		u64 ret_addr;

		if (!to_ip)
			return 0;
		ret_addr = from_ip + insn_len;
		if (ret_addr == to_ip)
			return 0; /* Zero-length calls are excluded */
		return thread_stack__push(ts, ret_addr,
					  flags & PERF_IP_FLAG_TRACE_END);
	} else if (flags & PERF_IP_FLAG_TRACE_BEGIN) {
		/*
		 * If the caller did not change the trace number (which would
		 * have flushed the stack) then try to make sense of the stack.
		 * Possibly, tracing began after returning to the current
		 * address, so try to pop that. Also, do not expect a call made
		 * when the trace ended, to return, so pop that.
		 */
		thread_stack__pop(ts, to_ip);
		thread_stack__pop_trace_end(ts);
	} else if ((flags & PERF_IP_FLAG_RETURN) && from_ip) {
		thread_stack__pop(ts, to_ip);
	}

	return 0;
}

void thread_stack__set_trace_nr(struct thread *thread, int cpu, u64 trace_nr)
{
	struct thread_stack *ts = thread__stack(thread, cpu);

	if (!ts)
		return;

	if (trace_nr != ts->trace_nr) {
		if (ts->trace_nr)
			__thread_stack__flush(thread, ts);
		ts->trace_nr = trace_nr;
	}
}

static void __thread_stack__free(struct thread *thread, struct thread_stack *ts)
{
	__thread_stack__flush(thread, ts);
	zfree(&ts->stack);
}

static void thread_stack__reset(struct thread *thread, struct thread_stack *ts)
{
	unsigned int arr_sz = ts->arr_sz;

	__thread_stack__free(thread, ts);
	memset(ts, 0, sizeof(*ts));
	ts->arr_sz = arr_sz;
}

void thread_stack__free(struct thread *thread)
{
	struct thread_stack *ts = thread->ts;
	unsigned int pos;

	if (ts) {
		for (pos = 0; pos < ts->arr_sz; pos++)
			__thread_stack__free(thread, ts + pos);
		zfree(&thread->ts);
	}
}

static inline u64 callchain_context(u64 ip, u64 kernel_start)
{
	return ip < kernel_start ? PERF_CONTEXT_USER : PERF_CONTEXT_KERNEL;
}

void thread_stack__sample(struct thread *thread, int cpu,
			  struct ip_callchain *chain,
			  size_t sz, u64 ip, u64 kernel_start)
{
	struct thread_stack *ts = thread__stack(thread, cpu);
	u64 context = callchain_context(ip, kernel_start);
	u64 last_context;
	size_t i, j;

	if (sz < 2) {
		chain->nr = 0;
		return;
	}

	chain->ips[0] = context;
	chain->ips[1] = ip;

	if (!ts) {
		chain->nr = 2;
		return;
	}

	last_context = context;

	for (i = 2, j = 1; i < sz && j <= ts->cnt; i++, j++) {
		ip = ts->stack[ts->cnt - j].ret_addr;
		context = callchain_context(ip, kernel_start);
		if (context != last_context) {
			if (i >= sz - 1)
				break;
			chain->ips[i++] = context;
			last_context = context;
		}
		chain->ips[i] = ip;
	}

	chain->nr = i;
}

struct call_return_processor *
call_return_processor__new(int (*process)(struct call_return *cr, u64 *parent_db_id, void *data),
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
				 bool no_call, bool trace_end)
{
	struct thread_stack_entry *tse;
	int err;

	if (!cp)
		return -ENOMEM;

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
	tse->insn_count = ts->insn_count;
	tse->cyc_count = ts->cyc_count;
	tse->cp = cp;
	tse->no_call = no_call;
	tse->trace_end = trace_end;
	tse->non_call = false;
	tse->db_id = 0;

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

	if (ts->stack[ts->cnt - 1].ret_addr == ret_addr &&
	    !ts->stack[ts->cnt - 1].non_call) {
		return thread_stack__call_return(thread, ts, --ts->cnt,
						 timestamp, ref, false);
	} else {
		size_t i = ts->cnt - 1;

		while (i--) {
			if (ts->stack[i].ret_addr != ret_addr ||
			    ts->stack[i].non_call)
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

static int thread_stack__bottom(struct thread_stack *ts,
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

	return thread_stack__push_cp(ts, ip, sample->time, ref, cp,
				     true, false);
}

static int thread_stack__pop_ks(struct thread *thread, struct thread_stack *ts,
				struct perf_sample *sample, u64 ref)
{
	u64 tm = sample->time;
	int err;

	/* Return to userspace, so pop all kernel addresses */
	while (thread_stack__in_kernel(ts)) {
		err = thread_stack__call_return(thread, ts, --ts->cnt,
						tm, ref, true);
		if (err)
			return err;
	}

	return 0;
}

static int thread_stack__no_call_return(struct thread *thread,
					struct thread_stack *ts,
					struct perf_sample *sample,
					struct addr_location *from_al,
					struct addr_location *to_al, u64 ref)
{
	struct call_path_root *cpr = ts->crp->cpr;
	struct call_path *root = &cpr->call_path;
	struct symbol *fsym = from_al->sym;
	struct symbol *tsym = to_al->sym;
	struct call_path *cp, *parent;
	u64 ks = ts->kernel_start;
	u64 addr = sample->addr;
	u64 tm = sample->time;
	u64 ip = sample->ip;
	int err;

	if (ip >= ks && addr < ks) {
		/* Return to userspace, so pop all kernel addresses */
		err = thread_stack__pop_ks(thread, ts, sample, ref);
		if (err)
			return err;

		/* If the stack is empty, push the userspace address */
		if (!ts->cnt) {
			cp = call_path__findnew(cpr, root, tsym, addr, ks);
			return thread_stack__push_cp(ts, 0, tm, ref, cp, true,
						     false);
		}
	} else if (thread_stack__in_kernel(ts) && ip < ks) {
		/* Return to userspace, so pop all kernel addresses */
		err = thread_stack__pop_ks(thread, ts, sample, ref);
		if (err)
			return err;
	}

	if (ts->cnt)
		parent = ts->stack[ts->cnt - 1].cp;
	else
		parent = root;

	if (parent->sym == from_al->sym) {
		/*
		 * At the bottom of the stack, assume the missing 'call' was
		 * before the trace started. So, pop the current symbol and push
		 * the 'to' symbol.
		 */
		if (ts->cnt == 1) {
			err = thread_stack__call_return(thread, ts, --ts->cnt,
							tm, ref, false);
			if (err)
				return err;
		}

		if (!ts->cnt) {
			cp = call_path__findnew(cpr, root, tsym, addr, ks);

			return thread_stack__push_cp(ts, addr, tm, ref, cp,
						     true, false);
		}

		/*
		 * Otherwise assume the 'return' is being used as a jump (e.g.
		 * retpoline) and just push the 'to' symbol.
		 */
		cp = call_path__findnew(cpr, parent, tsym, addr, ks);

		err = thread_stack__push_cp(ts, 0, tm, ref, cp, true, false);
		if (!err)
			ts->stack[ts->cnt - 1].non_call = true;

		return err;
	}

	/*
	 * Assume 'parent' has not yet returned, so push 'to', and then push and
	 * pop 'from'.
	 */

	cp = call_path__findnew(cpr, parent, tsym, addr, ks);

	err = thread_stack__push_cp(ts, addr, tm, ref, cp, true, false);
	if (err)
		return err;

	cp = call_path__findnew(cpr, cp, fsym, ip, ks);

	err = thread_stack__push_cp(ts, ip, tm, ref, cp, true, false);
	if (err)
		return err;

	return thread_stack__call_return(thread, ts, --ts->cnt, tm, ref, false);
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
	if (tse->trace_end) {
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

	ret_addr = sample->ip + sample->insn_len;

	return thread_stack__push_cp(ts, ret_addr, sample->time, ref, cp,
				     false, true);
}

static bool is_x86_retpoline(const char *name)
{
	const char *p = strstr(name, "__x86_indirect_thunk_");

	return p == name || !strcmp(name, "__indirect_thunk_start");
}

/*
 * x86 retpoline functions pollute the call graph. This function removes them.
 * This does not handle function return thunks, nor is there any improvement
 * for the handling of inline thunks or extern thunks.
 */
static int thread_stack__x86_retpoline(struct thread_stack *ts,
				       struct perf_sample *sample,
				       struct addr_location *to_al)
{
	struct thread_stack_entry *tse = &ts->stack[ts->cnt - 1];
	struct call_path_root *cpr = ts->crp->cpr;
	struct symbol *sym = tse->cp->sym;
	struct symbol *tsym = to_al->sym;
	struct call_path *cp;

	if (sym && is_x86_retpoline(sym->name)) {
		/*
		 * This is a x86 retpoline fn. It pollutes the call graph by
		 * showing up everywhere there is an indirect branch, but does
		 * not itself mean anything. Here the top-of-stack is removed,
		 * by decrementing the stack count, and then further down, the
		 * resulting top-of-stack is replaced with the actual target.
		 * The result is that the retpoline functions will no longer
		 * appear in the call graph. Note this only affects the call
		 * graph, since all the original branches are left unchanged.
		 */
		ts->cnt -= 1;
		sym = ts->stack[ts->cnt - 2].cp->sym;
		if (sym && sym == tsym && to_al->addr != tsym->start) {
			/*
			 * Target is back to the middle of the symbol we came
			 * from so assume it is an indirect jmp and forget it
			 * altogether.
			 */
			ts->cnt -= 1;
			return 0;
		}
	} else if (sym && sym == tsym) {
		/*
		 * Target is back to the symbol we came from so assume it is an
		 * indirect jmp and forget it altogether.
		 */
		ts->cnt -= 1;
		return 0;
	}

	cp = call_path__findnew(cpr, ts->stack[ts->cnt - 2].cp, tsym,
				sample->addr, ts->kernel_start);
	if (!cp)
		return -ENOMEM;

	/* Replace the top-of-stack with the actual target */
	ts->stack[ts->cnt - 1].cp = cp;

	return 0;
}

int thread_stack__process(struct thread *thread, struct comm *comm,
			  struct perf_sample *sample,
			  struct addr_location *from_al,
			  struct addr_location *to_al, u64 ref,
			  struct call_return_processor *crp)
{
	struct thread_stack *ts = thread__stack(thread, sample->cpu);
	enum retpoline_state_t rstate;
	int err = 0;

	if (ts && !ts->crp) {
		/* Supersede thread_stack__event() */
		thread_stack__reset(thread, ts);
		ts = NULL;
	}

	if (!ts) {
		ts = thread_stack__new(thread, sample->cpu, crp);
		if (!ts)
			return -ENOMEM;
		ts->comm = comm;
	}

	rstate = ts->rstate;
	if (rstate == X86_RETPOLINE_DETECTED)
		ts->rstate = X86_RETPOLINE_POSSIBLE;

	/* Flush stack on exec */
	if (ts->comm != comm && thread->pid_ == thread->tid) {
		err = __thread_stack__flush(thread, ts);
		if (err)
			return err;
		ts->comm = comm;
	}

	/* If the stack is empty, put the current symbol on the stack */
	if (!ts->cnt) {
		err = thread_stack__bottom(ts, sample, from_al, to_al, ref);
		if (err)
			return err;
	}

	ts->branch_count += 1;
	ts->insn_count += sample->insn_cnt;
	ts->cyc_count += sample->cyc_cnt;
	ts->last_time = sample->time;

	if (sample->flags & PERF_IP_FLAG_CALL) {
		bool trace_end = sample->flags & PERF_IP_FLAG_TRACE_END;
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
		err = thread_stack__push_cp(ts, ret_addr, sample->time, ref,
					    cp, false, trace_end);

		/*
		 * A call to the same symbol but not the start of the symbol,
		 * may be the start of a x86 retpoline.
		 */
		if (!err && rstate == X86_RETPOLINE_POSSIBLE && to_al->sym &&
		    from_al->sym == to_al->sym &&
		    to_al->addr != to_al->sym->start)
			ts->rstate = X86_RETPOLINE_DETECTED;

	} else if (sample->flags & PERF_IP_FLAG_RETURN) {
		if (!sample->addr) {
			u32 return_from_kernel = PERF_IP_FLAG_SYSCALLRET |
						 PERF_IP_FLAG_INTERRUPT;

			if (!(sample->flags & return_from_kernel))
				return 0;

			/* Pop kernel stack */
			return thread_stack__pop_ks(thread, ts, sample, ref);
		}

		if (!sample->ip)
			return 0;

		/* x86 retpoline 'return' doesn't match the stack */
		if (rstate == X86_RETPOLINE_DETECTED && ts->cnt > 2 &&
		    ts->stack[ts->cnt - 1].ret_addr != sample->addr)
			return thread_stack__x86_retpoline(ts, sample, to_al);

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
	} else if (sample->flags & PERF_IP_FLAG_BRANCH &&
		   from_al->sym != to_al->sym && to_al->sym &&
		   to_al->addr == to_al->sym->start) {
		struct call_path_root *cpr = ts->crp->cpr;
		struct call_path *cp;

		/*
		 * The compiler might optimize a call/ret combination by making
		 * it a jmp. Make that visible by recording on the stack a
		 * branch to the start of a different symbol. Note, that means
		 * when a ret pops the stack, all jmps must be popped off first.
		 */
		cp = call_path__findnew(cpr, ts->stack[ts->cnt - 1].cp,
					to_al->sym, sample->addr,
					ts->kernel_start);
		err = thread_stack__push_cp(ts, 0, sample->time, ref, cp, false,
					    false);
		if (!err)
			ts->stack[ts->cnt - 1].non_call = true;
	}

	return err;
}

size_t thread_stack__depth(struct thread *thread, int cpu)
{
	struct thread_stack *ts = thread__stack(thread, cpu);

	if (!ts)
		return 0;
	return ts->cnt;
}
