/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM_VAR

#ifdef CONFIG_PERF_EVENTS

#include "stages/stage6_event_callback.h"

#undef __perf_count
#define __perf_count(c)	(__count = (c))

#undef __perf_task
#define __perf_task(t)	(__task = (t))

#undef __DECLARE_EVENT_CLASS
#define __DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print) \
static notrace void							\
do_perf_trace_##call(void *__data, proto)				\
{									\
	struct trace_event_call *event_call = __data;			\
	struct trace_event_data_offsets_##call __maybe_unused __data_offsets;\
	struct trace_event_raw_##call *entry;				\
	struct pt_regs *__regs;						\
	u64 __count = 1;						\
	struct task_struct *__task = NULL;				\
	struct hlist_head *head;					\
	int __entry_size;						\
	int __data_size;						\
	int rctx;							\
									\
	__data_size = trace_event_get_offsets_##call(&__data_offsets, args); \
									\
	head = this_cpu_ptr(event_call->perf_events);			\
	if (!bpf_prog_array_valid(event_call) &&			\
	    __builtin_constant_p(!__task) && !__task &&			\
	    hlist_empty(head))						\
		return;							\
									\
	__entry_size = ALIGN(__data_size + sizeof(*entry) + sizeof(u32),\
			     sizeof(u64));				\
	__entry_size -= sizeof(u32);					\
									\
	entry = perf_trace_buf_alloc(__entry_size, &__regs, &rctx);	\
	if (!entry)							\
		return;							\
									\
	perf_fetch_caller_regs(__regs);					\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	perf_trace_run_bpf_submit(entry, __entry_size, rctx,		\
				  event_call, __count, __regs,		\
				  head, __task);			\
}

/*
 * Define unused __count and __task variables to use @args to pass
 * arguments to do_perf_trace_##call. This is needed because the
 * macros __perf_count and __perf_task introduce the side-effect to
 * store copies into those local variables.
 */
#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
__DECLARE_EVENT_CLASS(call, PARAMS(proto), PARAMS(args), PARAMS(tstruct), \
		      PARAMS(assign), PARAMS(print))			\
static notrace void							\
perf_trace_##call(void *__data, proto)					\
{									\
	u64 __count __attribute__((unused));				\
	struct task_struct *__task __attribute__((unused));		\
									\
	do_perf_trace_##call(__data, args);				\
}

#undef DECLARE_EVENT_SYSCALL_CLASS
#define DECLARE_EVENT_SYSCALL_CLASS(call, proto, args, tstruct, assign, print) \
__DECLARE_EVENT_CLASS(call, PARAMS(proto), PARAMS(args), PARAMS(tstruct), \
		      PARAMS(assign), PARAMS(print))			\
static notrace void							\
perf_trace_##call(void *__data, proto)					\
{									\
	u64 __count __attribute__((unused));				\
	struct task_struct *__task __attribute__((unused));		\
									\
	might_fault();							\
	preempt_disable_notrace();					\
	do_perf_trace_##call(__data, args);				\
	preempt_enable_notrace();					\
}

/*
 * This part is compiled out, it is only here as a build time check
 * to make sure that if the tracepoint handling changes, the
 * perf probe will fail to compile unless it too is updated.
 */
#undef DEFINE_EVENT
#define DEFINE_EVENT(template, call, proto, args)			\
static inline void perf_test_probe_##call(void)				\
{									\
	check_trace_callback_type_##call(perf_trace_##template);	\
}


#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __DECLARE_EVENT_CLASS

#endif /* CONFIG_PERF_EVENTS */
