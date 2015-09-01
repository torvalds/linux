/*
 * Stage 4 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * For those macros defined with TRACE_EVENT:
 *
 * static struct trace_event_call event_<call>;
 *
 * static void trace_event_raw_event_<call>(void *__data, proto)
 * {
 *	struct trace_event_file *trace_file = __data;
 *	struct trace_event_call *event_call = trace_file->event_call;
 *	struct trace_event_data_offsets_<call> __maybe_unused __data_offsets;
 *	unsigned long eflags = trace_file->flags;
 *	enum event_trigger_type __tt = ETT_NONE;
 *	struct ring_buffer_event *event;
 *	struct trace_event_raw_<call> *entry; <-- defined in stage 1
 *	struct ring_buffer *buffer;
 *	unsigned long irq_flags;
 *	int __data_size;
 *	int pc;
 *
 *	if (!(eflags & EVENT_FILE_FL_TRIGGER_COND)) {
 *		if (eflags & EVENT_FILE_FL_TRIGGER_MODE)
 *			event_triggers_call(trace_file, NULL);
 *		if (eflags & EVENT_FILE_FL_SOFT_DISABLED)
 *			return;
 *	}
 *
 *	local_save_flags(irq_flags);
 *	pc = preempt_count();
 *
 *	__data_size = trace_event_get_offsets_<call>(&__data_offsets, args);
 *
 *	event = trace_event_buffer_lock_reserve(&buffer, trace_file,
 *				  event_<call>->event.type,
 *				  sizeof(*entry) + __data_size,
 *				  irq_flags, pc);
 *	if (!event)
 *		return;
 *	entry	= ring_buffer_event_data(event);
 *
 *	{ <assign>; }  <-- Here we assign the entries by the __field and
 *			   __array macros.
 *
 *	if (eflags & EVENT_FILE_FL_TRIGGER_COND)
 *		__tt = event_triggers_call(trace_file, entry);
 *
 *	if (test_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT,
 *		     &trace_file->flags))
 *		ring_buffer_discard_commit(buffer, event);
 *	else if (!filter_check_discard(trace_file, entry, buffer, event))
 *		trace_buffer_unlock_commit(buffer, event, irq_flags, pc);
 *
 *	if (__tt)
 *		event_triggers_post_call(trace_file, __tt);
 * }
 *
 * static struct trace_event ftrace_event_type_<call> = {
 *	.trace			= trace_raw_output_<call>, <-- stage 2
 * };
 *
 * static char print_fmt_<call>[] = <TP_printk>;
 *
 * static struct trace_event_class __used event_class_<template> = {
 *	.system			= "<system>",
 *	.define_fields		= trace_event_define_fields_<call>,
 *	.fields			= LIST_HEAD_INIT(event_class_##call.fields),
 *	.raw_init		= trace_event_raw_init,
 *	.probe			= trace_event_raw_event_##call,
 *	.reg			= trace_event_reg,
 * };
 *
 * static struct trace_event_call event_<call> = {
 *	.class			= event_class_<template>,
 *	{
 *		.tp			= &__tracepoint_<call>,
 *	},
 *	.event			= &ftrace_event_type_<call>,
 *	.print_fmt		= print_fmt_<call>,
 *	.flags			= TRACE_EVENT_FL_TRACEPOINT,
 * };
 * // its only safe to use pointers when doing linker tricks to
 * // create an array.
 * static struct trace_event_call __used
 * __attribute__((section("_ftrace_events"))) *__event_<call> = &event_<call>;
 *
 */

#ifdef CONFIG_PERF_EVENTS

#define _TRACE_PERF_PROTO(call, proto)					\
	static notrace void						\
	perf_trace_##call(void *__data, proto);

#define _TRACE_PERF_INIT(call)						\
	.perf_probe		= perf_trace_##call,

#else
#define _TRACE_PERF_PROTO(call, proto)
#define _TRACE_PERF_INIT(call)
#endif /* CONFIG_PERF_EVENTS */

#undef __entry
#define __entry entry

#undef __field
#define __field(type, item)

#undef __field_struct
#define __field_struct(type, item)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__entry->__data_loc_##item = __data_offsets.item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __assign_str
#define __assign_str(dst, src)						\
	strcpy(__get_str(dst), (src) ? (const char *)(src) : "(null)");

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef __get_bitmask
#define __get_bitmask(field) (char *)__get_dynamic_array(field)

#undef __assign_bitmask
#define __assign_bitmask(dst, src, nr_bits)					\
	memcpy(__get_bitmask(dst), (src), __bitmask_size_in_bytes(nr_bits))

#undef TP_fast_assign
#define TP_fast_assign(args...) args

#undef __perf_addr
#define __perf_addr(a)	(a)

#undef __perf_count
#define __perf_count(c)	(c)

#undef __perf_task
#define __perf_task(t)	(t)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
									\
static notrace void							\
trace_event_raw_event_##call(void *__data, proto)			\
{									\
	struct trace_event_file *trace_file = __data;			\
	struct trace_event_data_offsets_##call __maybe_unused __data_offsets;\
	struct trace_event_buffer fbuffer;				\
	struct trace_event_raw_##call *entry;				\
	int __data_size;						\
									\
	if (trace_trigger_soft_disabled(trace_file))			\
		return;							\
									\
	__data_size = trace_event_get_offsets_##call(&__data_offsets, args); \
									\
	entry = trace_event_buffer_reserve(&fbuffer, trace_file,	\
				 sizeof(*entry) + __data_size);		\
									\
	if (!entry)							\
		return;							\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	trace_event_buffer_commit(&fbuffer);				\
}
/*
 * The ftrace_test_probe is compiled out, it is only here as a build time check
 * to make sure that if the tracepoint handling changes, the ftrace probe will
 * fail to compile unless it too is updated.
 */

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, call, proto, args)			\
static inline void ftrace_test_probe_##call(void)			\
{									\
	check_trace_callback_type_##call(trace_event_raw_event_##template); \
}

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __entry
#define __entry REC

#undef __print_flags
#undef __print_symbolic
#undef __print_hex
#undef __get_dynamic_array
#undef __get_dynamic_array_len
#undef __get_str
#undef __get_bitmask
#undef __print_array

#undef TP_printk
#define TP_printk(fmt, args...) "\"" fmt "\", "  __stringify(args)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
_TRACE_PERF_PROTO(call, PARAMS(proto));					\
static char print_fmt_##call[] = print;					\
static struct trace_event_class __used __refdata event_class_##call = { \
	.system			= TRACE_SYSTEM_STRING,			\
	.define_fields		= trace_event_define_fields_##call,	\
	.fields			= LIST_HEAD_INIT(event_class_##call.fields),\
	.raw_init		= trace_event_raw_init,			\
	.probe			= trace_event_raw_event_##call,		\
	.reg			= trace_event_reg,			\
	_TRACE_PERF_INIT(call)						\
};

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, call, proto, args)			\
									\
static struct trace_event_call __used event_##call = {			\
	.class			= &event_class_##template,		\
	{								\
		.tp			= &__tracepoint_##call,		\
	},								\
	.event.funcs		= &trace_event_type_funcs_##template,	\
	.print_fmt		= print_fmt_##template,			\
	.flags			= TRACE_EVENT_FL_TRACEPOINT,		\
};									\
static struct trace_event_call __used					\
__attribute__((section("_ftrace_events"))) *__event_##call = &event_##call

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, call, proto, args, print)		\
									\
static char print_fmt_##call[] = print;					\
									\
static struct trace_event_call __used event_##call = {			\
	.class			= &event_class_##template,		\
	{								\
		.tp			= &__tracepoint_##call,		\
	},								\
	.event.funcs		= &trace_event_type_funcs_##call,	\
	.print_fmt		= print_fmt_##call,			\
	.flags			= TRACE_EVENT_FL_TRACEPOINT,		\
};									\
static struct trace_event_call __used					\
__attribute__((section("_ftrace_events"))) *__event_##call = &event_##call

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

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
#define __get_str(field) (char *)__get_dynamic_array(field)

#undef __get_bitmask
#define __get_bitmask(field) (char *)__get_dynamic_array(field)

#undef __perf_addr
#define __perf_addr(a)	(__addr = (a))

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
	u64 __addr = 0, __count = 1;					\
	struct task_struct *__task = NULL;				\
	struct hlist_head *head;					\
	int __entry_size;						\
	int __data_size;						\
	int rctx;							\
									\
	__data_size = trace_event_get_offsets_##call(&__data_offsets, args); \
									\
	head = this_cpu_ptr(event_call->perf_events);			\
	if (__builtin_constant_p(!__task) && !__task &&			\
				hlist_empty(head))			\
		return;							\
									\
	__entry_size = ALIGN(__data_size + sizeof(*entry) + sizeof(u32),\
			     sizeof(u64));				\
	__entry_size -= sizeof(u32);					\
									\
	entry = perf_trace_buf_prepare(__entry_size,			\
			event_call->event.type, &__regs, &rctx);	\
	if (!entry)							\
		return;							\
									\
	perf_fetch_caller_regs(__regs);					\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	perf_trace_buf_submit(entry, __entry_size, rctx, __addr,	\
		__count, __regs, head, __task);				\
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
