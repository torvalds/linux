/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM_VAR

#ifdef CONFIG_PERF_EVENTS

#undef __entry
#define __entry entry

#undef __get_dynamic_array
#define __get_dynamic_array(field)	\
		((void *)__entry + (__entry->__data_loc_##field & 0xffff))

#undef __get_dynamic_array_len
#define __get_dynamic_array_len(field)	\
		((__entry->__data_loc_##field >> 16) & 0xffff)

#undef __get_str
#define __get_str(field) ((char *)__get_dynamic_array(field))

#undef __get_bitmask
#define __get_bitmask(field) (char *)__get_dynamic_array(field)

#undef __get_rel_dynamic_array
#define __get_rel_dynamic_array(field)	\
		((void *)__entry +					\
		 offsetof(typeof(*__entry), __rel_loc_##field) +	\
		 sizeof(__entry->__rel_loc_##field) +			\
		 (__entry->__rel_loc_##field & 0xffff))

#undef __get_rel_dynamic_array_len
#define __get_rel_dynamic_array_len(field)	\
		((__entry->__rel_loc_##field >> 16) & 0xffff)

#undef __get_rel_str
#define __get_rel_str(field) ((char *)__get_rel_dynamic_array(field))

#undef __get_rel_bitmask
#define __get_rel_bitmask(field) (char *)__get_rel_dynamic_array(field)

#undef __perf_count
#define __perf_count(c)	(__count = (c))

#undef __perf_task
#define __perf_task(t)	(__task = (t))

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static notrace void							\
perf_trace_##call(void *__data, proto)					\
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
#endif /* CONFIG_PERF_EVENTS */
