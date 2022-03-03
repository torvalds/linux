/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stage 1 of the trace events.
 *
 * Override the macros in the event tracepoint header <trace/events/XXX.h>
 * to include the following:
 *
 * struct trace_event_raw_<call> {
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

#include <linux/trace_events.h>

#ifndef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR TRACE_SYSTEM
#endif

#include "stages/init.h"

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

#include "stages/stage1_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)	\
	struct trace_event_raw_##name {					\
		struct trace_entry	ent;				\
		tstruct							\
		char			__data[];			\
	};								\
									\
	static struct trace_event_class event_class_##name;

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)	\
	static struct trace_event_call	__used		\
	__attribute__((__aligned__(4))) event_##name

#undef DEFINE_EVENT_FN
#define DEFINE_EVENT_FN(template, name, proto, args, reg, unreg)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

/* Callbacks are meaningless to ftrace. */
#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,			\
		assign, print, reg, unreg)				\
	TRACE_EVENT(name, PARAMS(proto), PARAMS(args),			\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print))		\

#undef TRACE_EVENT_FN_COND
#define TRACE_EVENT_FN_COND(name, proto, args, cond, tstruct,	\
		assign, print, reg, unreg)				\
	TRACE_EVENT_CONDITION(name, PARAMS(proto), PARAMS(args), PARAMS(cond),		\
		PARAMS(tstruct), PARAMS(assign), PARAMS(print))		\

#undef TRACE_EVENT_FLAGS
#define TRACE_EVENT_FLAGS(name, value)					\
	__TRACE_EVENT_FLAGS(name, value)

#undef TRACE_EVENT_PERF_PERM
#define TRACE_EVENT_PERF_PERM(name, expr...)				\
	__TRACE_EVENT_PERF_PERM(name, expr)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 2 of the trace events.
 *
 * Include the following:
 *
 * struct trace_event_data_offsets_<call> {
 *	u32				<item1>;
 *	u32				<item2>;
 *	[...]
 * };
 *
 * The __dynamic_array() macro will create each u32 <item>, this is
 * to keep the offset of each array from the beginning of the event.
 * The size of an array is also encoded, in the higher 16 bits of <item>.
 */

#include "stages/stage2_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
	struct trace_event_data_offsets_##call {			\
		tstruct;						\
	};

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)

#undef TRACE_EVENT_FLAGS
#define TRACE_EVENT_FLAGS(event, flag)

#undef TRACE_EVENT_PERF_PERM
#define TRACE_EVENT_PERF_PERM(event, expr...)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 3 of the trace events.
 *
 * Override the macros in the event tracepoint header <trace/events/XXX.h>
 * to include the following:
 *
 * enum print_line_t
 * trace_raw_output_<call>(struct trace_iterator *iter, int flags)
 * {
 *	struct trace_seq *s = &iter->seq;
 *	struct trace_event_raw_<call> *field; <-- defined in stage 1
 *	struct trace_seq *p = &iter->tmp_seq;
 *
 * -------(for event)-------
 *
 *	struct trace_entry *entry;
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
 *	return trace_output_call(iter, <call>, <TP_printk> "\n");
 *
 * ------(or, for event class)------
 *
 *	int ret;
 *
 *	field = (typeof(field))iter->ent;
 *
 *	ret = trace_raw_output_prep(iter, trace_event);
 *	if (ret != TRACE_TYPE_HANDLED)
 *		return ret;
 *
 *	trace_event_printf(iter, <TP_printk> "\n");
 *
 *	return trace_handle_return(s);
 * -------
 *  }
 *
 * This is the method used to print the raw event to the trace
 * output format. Note, this is not needed if the data is read
 * in binary.
 */

#include "stages/stage3_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static notrace enum print_line_t					\
trace_raw_output_##call(struct trace_iterator *iter, int flags,		\
			struct trace_event *trace_event)		\
{									\
	struct trace_seq *s = &iter->seq;				\
	struct trace_seq __maybe_unused *p = &iter->tmp_seq;		\
	struct trace_event_raw_##call *field;				\
	int ret;							\
									\
	field = (typeof(field))iter->ent;				\
									\
	ret = trace_raw_output_prep(iter, trace_event);			\
	if (ret != TRACE_TYPE_HANDLED)					\
		return ret;						\
									\
	trace_event_printf(iter, print);				\
									\
	return trace_handle_return(s);					\
}									\
static struct trace_event_functions trace_event_type_funcs_##call = {	\
	.trace			= trace_raw_output_##call,		\
};

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, call, proto, args, print)		\
static notrace enum print_line_t					\
trace_raw_output_##call(struct trace_iterator *iter, int flags,		\
			 struct trace_event *event)			\
{									\
	struct trace_event_raw_##template *field;			\
	struct trace_entry *entry;					\
	struct trace_seq *p = &iter->tmp_seq;				\
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
	return trace_output_call(iter, #call, print);			\
}									\
static struct trace_event_functions trace_event_type_funcs_##call = {	\
	.trace			= trace_raw_output_##call,		\
};

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#include "stages/stage4_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, func, print)	\
static struct trace_event_fields trace_event_fields_##call[] = {	\
	tstruct								\
	{} };

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#include "stages/stage5_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
static inline notrace int trace_event_get_offsets_##call(		\
	struct trace_event_data_offsets_##call *__data_offsets, proto)	\
{									\
	int __data_size = 0;						\
	int __maybe_unused __item_length;				\
	struct trace_event_raw_##call __maybe_unused *entry;		\
									\
	tstruct;							\
									\
	return __data_size;						\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 4 of the trace events.
 *
 * Override the macros in the event tracepoint header <trace/events/XXX.h>
 * to include the following:
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
 *	struct trace_buffer *buffer;
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
 *	.fields_array		= trace_event_fields_<call>,
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
 * __section("_ftrace_events") *__event_<call> = &event_<call>;
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

#include "stages/stage6_defines.h"

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

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#include "stages/stage7_defines.h"

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
_TRACE_PERF_PROTO(call, PARAMS(proto));					\
static char print_fmt_##call[] = print;					\
static struct trace_event_class __used __refdata event_class_##call = { \
	.system			= TRACE_SYSTEM_STRING,			\
	.fields_array		= trace_event_fields_##call,		\
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
__section("_ftrace_events") *__event_##call = &event_##call

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
__section("_ftrace_events") *__event_##call = &event_##call

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
