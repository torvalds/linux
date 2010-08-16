/*
 * Stage 1 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * struct ftrace_raw_<call> {
 *	struct trace_entry		ent;
 *	<type>				<item>;
 *	<type2>				<item2>[<len>];
 *	[...]
 * };
 *
 * The <type> <item> is created by the __field(type, item) macro or
 * the __array(type2, item2, len) macro.
 * We simply do "type item;", and that will create the fields
 * in the structure.
 */

#include <linux/ftrace_event.h>

/*
 * DECLARE_EVENT_CLASS can be used to add a generic function
 * handlers for events. That is, if all events have the same
 * parameters and just have distinct trace points.
 * Each tracepoint can be defined with DEFINE_EVENT and that
 * will map the DECLARE_EVENT_CLASS to the tracepoint.
 *
 * TRACE_EVENT is a one to one mapping between tracepoint and template.
 */
#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print) \
	DECLARE_EVENT_CLASS(name,			       \
			     PARAMS(proto),		       \
			     PARAMS(args),		       \
			     PARAMS(tstruct),		       \
			     PARAMS(assign),		       \
			     PARAMS(print));		       \
	DEFINE_EVENT(name, name, PARAMS(proto), PARAMS(args));


#undef __field
#define __field(type, item)		type	item;

#undef __field_ext
#define __field_ext(type, item, filter_type)	type	item;

#undef __array
#define __array(type, item, len)	type	item[len];

#undef __dynamic_array
#define __dynamic_array(type, item, len) u32 __data_loc_##item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)	\
	struct ftrace_raw_##name {					\
		struct trace_entry	ent;				\
		tstruct							\
		char			__data[0];			\
	};								\
									\
	static struct ftrace_event_class event_class_##name;

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)	\
	static struct ftrace_event_call	__used		\
	__attribute__((__aligned__(4))) event_##name

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

/* Callbacks are meaningless to ftrace. */
#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,			\
		assign, print, reg, unreg)				\
	TRACE_EVENT(name, PARAMS(proto), PARAMS(args),			\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print))		\

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 2 of the trace events.
 *
 * Include the following:
 *
 * struct ftrace_data_offsets_<call> {
 *	u32				<item1>;
 *	u32				<item2>;
 *	[...]
 * };
 *
 * The __dynamic_array() macro will create each u32 <item>, this is
 * to keep the offset of each array from the beginning of the event.
 * The size of an array is also encoded, in the higher 16 bits of <item>.
 */

#undef __field
#define __field(type, item)

#undef __field_ext
#define __field_ext(type, item, filter_type)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)	u32 item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
	struct ftrace_data_offsets_##call {				\
		tstruct;						\
	};

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 3 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * enum print_line_t
 * ftrace_raw_output_<call>(struct trace_iterator *iter, int flags)
 * {
 *	struct trace_seq *s = &iter->seq;
 *	struct ftrace_raw_<call> *field; <-- defined in stage 1
 *	struct trace_entry *entry;
 *	struct trace_seq *p = &iter->tmp_seq;
 *	int ret;
 *
 *	entry = iter->ent;
 *
 *	if (entry->type != event_<call>->event.type) {
 *		WARN_ON_ONCE(1);
 *		return TRACE_TYPE_UNHANDLED;
 *	}
 *
 *	field = (typeof(field))entry;
 *
 *	trace_seq_init(p);
 *	ret = trace_seq_printf(s, "%s: ", <call>);
 *	if (ret)
 *		ret = trace_seq_printf(s, <TP_printk> "\n");
 *	if (!ret)
 *		return TRACE_TYPE_PARTIAL_LINE;
 *
 *	return TRACE_TYPE_HANDLED;
 * }
 *
 * This is the method used to print the raw event to the trace
 * output format. Note, this is not needed if the data is read
 * in binary.
 */

#undef __entry
#define __entry field

#undef TP_printk
#define TP_printk(fmt, args...) fmt "\n", args

#undef __get_dynamic_array
#define __get_dynamic_array(field)	\
		((void *)__entry + (__entry->__data_loc_##field & 0xffff))

#undef __get_str
#define __get_str(field) (char *)__get_dynamic_array(field)

#undef __print_flags
#define __print_flags(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags __flags[] =	\
			{ flag_array, { -1, NULL }};			\
		ftrace_print_flags_seq(p, delim, flag, __flags);	\
	})

#undef __print_symbolic
#define __print_symbolic(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags symbols[] =	\
			{ symbol_array, { -1, NULL }};			\
		ftrace_print_symbols_seq(p, value, symbols);		\
	})

#undef __print_hex
#define __print_hex(buf, buf_len) ftrace_print_hex_seq(p, buf, buf_len)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static notrace enum print_line_t					\
ftrace_raw_output_##call(struct trace_iterator *iter, int flags,	\
			 struct trace_event *trace_event)		\
{									\
	struct ftrace_event_call *event;				\
	struct trace_seq *s = &iter->seq;				\
	struct ftrace_raw_##call *field;				\
	struct trace_entry *entry;					\
	struct trace_seq *p = &iter->tmp_seq;				\
	int ret;							\
									\
	event = container_of(trace_event, struct ftrace_event_call,	\
			     event);					\
									\
	entry = iter->ent;						\
									\
	if (entry->type != event->event.type) {				\
		WARN_ON_ONCE(1);					\
		return TRACE_TYPE_UNHANDLED;				\
	}								\
									\
	field = (typeof(field))entry;					\
									\
	trace_seq_init(p);						\
	ret = trace_seq_printf(s, "%s: ", event->name);			\
	if (ret)							\
		ret = trace_seq_printf(s, print);			\
	if (!ret)							\
		return TRACE_TYPE_PARTIAL_LINE;				\
									\
	return TRACE_TYPE_HANDLED;					\
}									\
static struct trace_event_functions ftrace_event_type_funcs_##call = {	\
	.trace			= ftrace_raw_output_##call,		\
};

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, call, proto, args, print)		\
static notrace enum print_line_t					\
ftrace_raw_output_##call(struct trace_iterator *iter, int flags,	\
			 struct trace_event *event)			\
{									\
	struct trace_seq *s = &iter->seq;				\
	struct ftrace_raw_##template *field;				\
	struct trace_entry *entry;					\
	struct trace_seq *p = &iter->tmp_seq;				\
	int ret;							\
									\
	entry = iter->ent;						\
									\
	if (entry->type != event_##call.event.type) {			\
		WARN_ON_ONCE(1);					\
		return TRACE_TYPE_UNHANDLED;				\
	}								\
									\
	field = (typeof(field))entry;					\
									\
	trace_seq_init(p);						\
	ret = trace_seq_printf(s, "%s: ", #call);			\
	if (ret)							\
		ret = trace_seq_printf(s, print);			\
	if (!ret)							\
		return TRACE_TYPE_PARTIAL_LINE;				\
									\
	return TRACE_TYPE_HANDLED;					\
}									\
static struct trace_event_functions ftrace_event_type_funcs_##call = {	\
	.trace			= ftrace_raw_output_##call,		\
};

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __field_ext
#define __field_ext(type, item, filter_type)				\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), filter_type);	\
	if (ret)							\
		return ret;

#undef __field
#define __field(type, item)	__field_ext(type, item, FILTER_OTHER)

#undef __array
#define __array(type, item, len)					\
	BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);				\
	ret = trace_define_field(event_call, #type "[" #len "]", #item,	\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), FILTER_OTHER);	\
	if (ret)							\
		return ret;

#undef __dynamic_array
#define __dynamic_array(type, item, len)				       \
	ret = trace_define_field(event_call, "__data_loc " #type "[]", #item,  \
				 offsetof(typeof(field), __data_loc_##item),   \
				 sizeof(field.__data_loc_##item),	       \
				 is_signed_type(type), FILTER_OTHER);

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, func, print)	\
static int notrace							\
ftrace_define_fields_##call(struct ftrace_event_call *event_call)	\
{									\
	struct ftrace_raw_##call field;					\
	int ret;							\
									\
	tstruct;							\
									\
	return ret;							\
}

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * remember the offset of each array from the beginning of the event.
 */

#undef __entry
#define __entry entry

#undef __field
#define __field(type, item)

#undef __field_ext
#define __field_ext(type, item, filter_type)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__data_offsets->item = __data_size +				\
			       offsetof(typeof(*entry), __data);	\
	__data_offsets->item |= (len * sizeof(type)) << 16;		\
	__data_size += (len) * sizeof(type);

#undef __string
#define __string(item, src) __dynamic_array(char, item, strlen(src) + 1)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static inline notrace int ftrace_get_offsets_##call(			\
	struct ftrace_data_offsets_##call *__data_offsets, proto)       \
{									\
	int __data_size = 0;						\
	struct ftrace_raw_##call __maybe_unused *entry;			\
									\
	tstruct;							\
									\
	return __data_size;						\
}

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 4 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * For those macros defined with TRACE_EVENT:
 *
 * static struct ftrace_event_call event_<call>;
 *
 * static void ftrace_raw_event_<call>(void *__data, proto)
 * {
 *	struct ftrace_event_call *event_call = __data;
 *	struct ftrace_data_offsets_<call> __maybe_unused __data_offsets;
 *	struct ring_buffer_event *event;
 *	struct ftrace_raw_<call> *entry; <-- defined in stage 1
 *	struct ring_buffer *buffer;
 *	unsigned long irq_flags;
 *	int __data_size;
 *	int pc;
 *
 *	local_save_flags(irq_flags);
 *	pc = preempt_count();
 *
 *	__data_size = ftrace_get_offsets_<call>(&__data_offsets, args);
 *
 *	event = trace_current_buffer_lock_reserve(&buffer,
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
 *	if (!filter_current_check_discard(buffer, event_call, entry, event))
 *		trace_current_buffer_unlock_commit(buffer,
 *						   event, irq_flags, pc);
 * }
 *
 * static struct trace_event ftrace_event_type_<call> = {
 *	.trace			= ftrace_raw_output_<call>, <-- stage 2
 * };
 *
 * static const char print_fmt_<call>[] = <TP_printk>;
 *
 * static struct ftrace_event_class __used event_class_<template> = {
 *	.system			= "<system>",
 *	.define_fields		= ftrace_define_fields_<call>,
 *	.fields			= LIST_HEAD_INIT(event_class_##call.fields),
 *	.raw_init		= trace_event_raw_init,
 *	.probe			= ftrace_raw_event_##call,
 *	.reg			= ftrace_event_reg,
 * };
 *
 * static struct ftrace_event_call __used
 * __attribute__((__aligned__(4)))
 * __attribute__((section("_ftrace_events"))) event_<call> = {
 *	.name			= "<call>",
 *	.class			= event_class_<template>,
 *	.event			= &ftrace_event_type_<call>,
 *	.print_fmt		= print_fmt_<call>,
 * };
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

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__entry->__data_loc_##item = __data_offsets.item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)       	\

#undef __assign_str
#define __assign_str(dst, src)						\
	strcpy(__get_str(dst), src);

#undef TP_fast_assign
#define TP_fast_assign(args...) args

#undef TP_perf_assign
#define TP_perf_assign(args...)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
									\
static notrace void							\
ftrace_raw_event_##call(void *__data, proto)				\
{									\
	struct ftrace_event_call *event_call = __data;			\
	struct ftrace_data_offsets_##call __maybe_unused __data_offsets;\
	struct ring_buffer_event *event;				\
	struct ftrace_raw_##call *entry;				\
	struct ring_buffer *buffer;					\
	unsigned long irq_flags;					\
	int __data_size;						\
	int pc;								\
									\
	local_save_flags(irq_flags);					\
	pc = preempt_count();						\
									\
	__data_size = ftrace_get_offsets_##call(&__data_offsets, args); \
									\
	event = trace_current_buffer_lock_reserve(&buffer,		\
				 event_call->event.type,		\
				 sizeof(*entry) + __data_size,		\
				 irq_flags, pc);			\
	if (!event)							\
		return;							\
	entry	= ring_buffer_event_data(event);			\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	if (!filter_current_check_discard(buffer, event_call, entry, event)) \
		trace_nowake_buffer_unlock_commit(buffer,		\
						  event, irq_flags, pc); \
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
	check_trace_callback_type_##call(ftrace_raw_event_##template);	\
}

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __entry
#define __entry REC

#undef __print_flags
#undef __print_symbolic
#undef __get_dynamic_array
#undef __get_str

#undef TP_printk
#define TP_printk(fmt, args...) "\"" fmt "\", "  __stringify(args)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
_TRACE_PERF_PROTO(call, PARAMS(proto));					\
static const char print_fmt_##call[] = print;				\
static struct ftrace_event_class __used event_class_##call = {		\
	.system			= __stringify(TRACE_SYSTEM),		\
	.define_fields		= ftrace_define_fields_##call,		\
	.fields			= LIST_HEAD_INIT(event_class_##call.fields),\
	.raw_init		= trace_event_raw_init,			\
	.probe			= ftrace_raw_event_##call,		\
	.reg			= ftrace_event_reg,			\
	_TRACE_PERF_INIT(call)						\
};

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, call, proto, args)			\
									\
static struct ftrace_event_call __used					\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.class			= &event_class_##template,		\
	.event.funcs		= &ftrace_event_type_funcs_##template,	\
	.print_fmt		= print_fmt_##template,			\
};

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, call, proto, args, print)		\
									\
static const char print_fmt_##call[] = print;				\
									\
static struct ftrace_event_call __used					\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.class			= &event_class_##template,		\
	.event.funcs		= &ftrace_event_type_funcs_##call,	\
	.print_fmt		= print_fmt_##call,			\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Define the insertion callback to perf events
 *
 * The job is very similar to ftrace_raw_event_<call> except that we don't
 * insert in the ring buffer but in a perf counter.
 *
 * static void ftrace_perf_<call>(proto)
 * {
 *	struct ftrace_data_offsets_<call> __maybe_unused __data_offsets;
 *	struct ftrace_event_call *event_call = &event_<call>;
 *	extern void perf_tp_event(int, u64, u64, void *, int);
 *	struct ftrace_raw_##call *entry;
 *	struct perf_trace_buf *trace_buf;
 *	u64 __addr = 0, __count = 1;
 *	unsigned long irq_flags;
 *	struct trace_entry *ent;
 *	int __entry_size;
 *	int __data_size;
 *	int __cpu
 *	int pc;
 *
 *	pc = preempt_count();
 *
 *	__data_size = ftrace_get_offsets_<call>(&__data_offsets, args);
 *
 *	// Below we want to get the aligned size by taking into account
 *	// the u32 field that will later store the buffer size
 *	__entry_size = ALIGN(__data_size + sizeof(*entry) + sizeof(u32),
 *			     sizeof(u64));
 *	__entry_size -= sizeof(u32);
 *
 *	// Protect the non nmi buffer
 *	// This also protects the rcu read side
 *	local_irq_save(irq_flags);
 *	__cpu = smp_processor_id();
 *
 *	if (in_nmi())
 *		trace_buf = rcu_dereference_sched(perf_trace_buf_nmi);
 *	else
 *		trace_buf = rcu_dereference_sched(perf_trace_buf);
 *
 *	if (!trace_buf)
 *		goto end;
 *
 *	trace_buf = per_cpu_ptr(trace_buf, __cpu);
 *
 * 	// Avoid recursion from perf that could mess up the buffer
 * 	if (trace_buf->recursion++)
 *		goto end_recursion;
 *
 * 	raw_data = trace_buf->buf;
 *
 *	// Make recursion update visible before entering perf_tp_event
 *	// so that we protect from perf recursions.
 *
 *	barrier();
 *
 *	//zero dead bytes from alignment to avoid stack leak to userspace:
 *	*(u64 *)(&raw_data[__entry_size - sizeof(u64)]) = 0ULL;
 *	entry = (struct ftrace_raw_<call> *)raw_data;
 *	ent = &entry->ent;
 *	tracing_generic_entry_update(ent, irq_flags, pc);
 *	ent->type = event_call->id;
 *
 *	<tstruct> <- do some jobs with dynamic arrays
 *
 *	<assign>  <- affect our values
 *
 *	perf_tp_event(event_call->id, __addr, __count, entry,
 *		     __entry_size);  <- submit them to perf counter
 *
 * }
 */

#ifdef CONFIG_PERF_EVENTS

#undef __entry
#define __entry entry

#undef __get_dynamic_array
#define __get_dynamic_array(field)	\
		((void *)__entry + (__entry->__data_loc_##field & 0xffff))

#undef __get_str
#define __get_str(field) (char *)__get_dynamic_array(field)

#undef __perf_addr
#define __perf_addr(a) __addr = (a)

#undef __perf_count
#define __perf_count(c) __count = (c)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static notrace void							\
perf_trace_##call(void *__data, proto)					\
{									\
	struct ftrace_event_call *event_call = __data;			\
	struct ftrace_data_offsets_##call __maybe_unused __data_offsets;\
	struct ftrace_raw_##call *entry;				\
	struct pt_regs __regs;						\
	u64 __addr = 0, __count = 1;					\
	struct hlist_head *head;					\
	int __entry_size;						\
	int __data_size;						\
	int rctx;							\
									\
	perf_fetch_caller_regs(&__regs);				\
									\
	__data_size = ftrace_get_offsets_##call(&__data_offsets, args); \
	__entry_size = ALIGN(__data_size + sizeof(*entry) + sizeof(u32),\
			     sizeof(u64));				\
	__entry_size -= sizeof(u32);					\
									\
	if (WARN_ONCE(__entry_size > PERF_MAX_TRACE_SIZE,		\
		      "profile buffer not large enough"))		\
		return;							\
									\
	entry = (struct ftrace_raw_##call *)perf_trace_buf_prepare(	\
		__entry_size, event_call->event.type, &__regs, &rctx);	\
	if (!entry)							\
		return;							\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	head = this_cpu_ptr(event_call->perf_events);			\
	perf_trace_buf_submit(entry, __entry_size, rctx, __addr,	\
		__count, &__regs, head);				\
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

#undef _TRACE_PROFILE_INIT

