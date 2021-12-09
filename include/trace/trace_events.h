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

#define __app__(x, y) str__##x##y
#define __app(x, y) __app__(x, y)

#define TRACE_SYSTEM_STRING __app(TRACE_SYSTEM_VAR,__trace_system_name)

#define TRACE_MAKE_SYSTEM_STR()				\
	static const char TRACE_SYSTEM_STRING[] =	\
		__stringify(TRACE_SYSTEM)

TRACE_MAKE_SYSTEM_STR();

#undef TRACE_DEFINE_ENUM
#define TRACE_DEFINE_ENUM(a)				\
	static struct trace_eval_map __used __initdata	\
	__##TRACE_SYSTEM##_##a =			\
	{						\
		.system = TRACE_SYSTEM_STRING,		\
		.eval_string = #a,			\
		.eval_value = a				\
	};						\
	static struct trace_eval_map __used		\
	__section("_ftrace_eval_map")			\
	*TRACE_SYSTEM##_##a = &__##TRACE_SYSTEM##_##a

#undef TRACE_DEFINE_SIZEOF
#define TRACE_DEFINE_SIZEOF(a)				\
	static struct trace_eval_map __used __initdata	\
	__##TRACE_SYSTEM##_##a =			\
	{						\
		.system = TRACE_SYSTEM_STRING,		\
		.eval_string = "sizeof(" #a ")",	\
		.eval_value = sizeof(a)			\
	};						\
	static struct trace_eval_map __used		\
	__section("_ftrace_eval_map")			\
	*TRACE_SYSTEM##_##a = &__##TRACE_SYSTEM##_##a

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

#undef __field_struct
#define __field_struct(type, item)	type	item;

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)	type	item;

#undef __array
#define __array(type, item, len)	type	item[len];

#undef __dynamic_array
#define __dynamic_array(type, item, len) u32 __data_loc_##item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(char, item, -1)

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)	\
	struct trace_event_raw_##name {					\
		struct trace_entry	ent;				\
		tstruct							\
		char			__data[0];			\
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

#undef TRACE_DEFINE_ENUM
#define TRACE_DEFINE_ENUM(a)

#undef TRACE_DEFINE_SIZEOF
#define TRACE_DEFINE_SIZEOF(a)

#undef __field
#define __field(type, item)

#undef __field_ext
#define __field_ext(type, item, filter_type)

#undef __field_struct
#define __field_struct(type, item)

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)	u32 item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

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

#undef __entry
#define __entry field

#undef TP_printk
#define TP_printk(fmt, args...) fmt "\n", args

#undef __get_dynamic_array
#define __get_dynamic_array(field)	\
		((void *)__entry + (__entry->__data_loc_##field & 0xffff))

#undef __get_dynamic_array_len
#define __get_dynamic_array_len(field)	\
		((__entry->__data_loc_##field >> 16) & 0xffff)

#undef __get_str
#define __get_str(field) ((char *)__get_dynamic_array(field))

#undef __get_bitmask
#define __get_bitmask(field)						\
	({								\
		void *__bitmask = __get_dynamic_array(field);		\
		unsigned int __bitmask_size;				\
		__bitmask_size = __get_dynamic_array_len(field);	\
		trace_print_bitmask_seq(p, __bitmask, __bitmask_size);	\
	})

#undef __print_flags
#define __print_flags(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags __flags[] =	\
			{ flag_array, { -1, NULL }};			\
		trace_print_flags_seq(p, delim, flag, __flags);	\
	})

#undef __print_symbolic
#define __print_symbolic(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags symbols[] =	\
			{ symbol_array, { -1, NULL }};			\
		trace_print_symbols_seq(p, value, symbols);		\
	})

#undef __print_flags_u64
#undef __print_symbolic_u64
#if BITS_PER_LONG == 32
#define __print_flags_u64(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags_u64 __flags[] =	\
			{ flag_array, { -1, NULL } };			\
		trace_print_flags_seq_u64(p, delim, flag, __flags);	\
	})

#define __print_symbolic_u64(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags_u64 symbols[] =	\
			{ symbol_array, { -1, NULL } };			\
		trace_print_symbols_seq_u64(p, value, symbols);	\
	})
#else
#define __print_flags_u64(flag, delim, flag_array...)			\
			__print_flags(flag, delim, flag_array)

#define __print_symbolic_u64(value, symbol_array...)			\
			__print_symbolic(value, symbol_array)
#endif

#undef __print_hex
#define __print_hex(buf, buf_len)					\
	trace_print_hex_seq(p, buf, buf_len, false)

#undef __print_hex_str
#define __print_hex_str(buf, buf_len)					\
	trace_print_hex_seq(p, buf, buf_len, true)

#undef __print_array
#define __print_array(array, count, el_size)				\
	({								\
		BUILD_BUG_ON(el_size != 1 && el_size != 2 &&		\
			     el_size != 4 && el_size != 8);		\
		trace_print_array_seq(p, array, count, el_size);	\
	})

#undef __print_hex_dump
#define __print_hex_dump(prefix_str, prefix_type,			\
			 rowsize, groupsize, buf, len, ascii)		\
	trace_print_hex_dump_seq(p, prefix_str, prefix_type,		\
				 rowsize, groupsize, buf, len, ascii)

#undef __print_ns_to_secs
#define __print_ns_to_secs(value)			\
	({						\
		u64 ____val = (u64)(value);		\
		do_div(____val, NSEC_PER_SEC);		\
		____val;				\
	})

#undef __print_ns_without_secs
#define __print_ns_without_secs(value)			\
	({						\
		u64 ____val = (u64)(value);		\
		(u32) do_div(____val, NSEC_PER_SEC);	\
	})

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

#undef __field_ext
#define __field_ext(_type, _item, _filter_type) {			\
	.type = #_type, .name = #_item,					\
	.size = sizeof(_type), .align = __alignof__(_type),		\
	.is_signed = is_signed_type(_type), .filter_type = _filter_type },

#undef __field_struct_ext
#define __field_struct_ext(_type, _item, _filter_type) {		\
	.type = #_type, .name = #_item,					\
	.size = sizeof(_type), .align = __alignof__(_type),		\
	0, .filter_type = _filter_type },

#undef __field
#define __field(type, item)	__field_ext(type, item, FILTER_OTHER)

#undef __field_struct
#define __field_struct(type, item) __field_struct_ext(type, item, FILTER_OTHER)

#undef __array
#define __array(_type, _item, _len) {					\
	.type = #_type"["__stringify(_len)"]", .name = #_item,		\
	.size = sizeof(_type[_len]), .align = __alignof__(_type),	\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __dynamic_array
#define __dynamic_array(_type, _item, _len) {				\
	.type = "__data_loc " #_type "[]", .name = #_item,		\
	.size = 4, .align = 4,						\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, func, print)	\
static struct trace_event_fields trace_event_fields_##call[] = {	\
	tstruct								\
	{} };

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)

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

#undef __field_struct
#define __field_struct(type, item)

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__item_length = (len) * sizeof(type);				\
	__data_offsets->item = __data_size +				\
			       offsetof(typeof(*entry), __data);	\
	__data_offsets->item |= __item_length << 16;			\
	__data_size += __item_length;

#undef __string
#define __string(item, src) __dynamic_array(char, item,			\
		    strlen((src) ? (const char *)(src) : "(null)") + 1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, (len) + 1)

/*
 * __bitmask_size_in_bytes_raw is the number of bytes needed to hold
 * num_possible_cpus().
 */
#define __bitmask_size_in_bytes_raw(nr_bits)	\
	(((nr_bits) + 7) / 8)

#define __bitmask_size_in_longs(nr_bits)			\
	((__bitmask_size_in_bytes_raw(nr_bits) +		\
	  ((BITS_PER_LONG / 8) - 1)) / (BITS_PER_LONG / 8))

/*
 * __bitmask_size_in_bytes is the number of bytes needed to hold
 * num_possible_cpus() padded out to the nearest long. This is what
 * is saved in the buffer, just to be consistent.
 */
#define __bitmask_size_in_bytes(nr_bits)				\
	(__bitmask_size_in_longs(nr_bits) * (BITS_PER_LONG / 8))

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item,	\
					 __bitmask_size_in_longs(nr_bits))

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

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __assign_str
#define __assign_str(dst, src)						\
	strcpy(__get_str(dst), (src) ? (const char *)(src) : "(null)");

#undef __assign_str_len
#define __assign_str_len(dst, src, len)					\
	do {								\
		memcpy(__get_str(dst), (src), (len));			\
		__get_str(dst)[len] = '\0';				\
	} while(0)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef __get_bitmask
#define __get_bitmask(field) (char *)__get_dynamic_array(field)

#undef __assign_bitmask
#define __assign_bitmask(dst, src, nr_bits)					\
	memcpy(__get_bitmask(dst), (src), __bitmask_size_in_bytes(nr_bits))

#undef TP_fast_assign
#define TP_fast_assign(args...) args

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

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __entry
#define __entry REC

#undef __print_flags
#undef __print_symbolic
#undef __print_hex
#undef __print_hex_str
#undef __get_dynamic_array
#undef __get_dynamic_array_len
#undef __get_str
#undef __get_bitmask
#undef __print_array
#undef __print_hex_dump

/*
 * The below is not executed in the kernel. It is only what is
 * displayed in the print format for userspace to parse.
 */
#undef __print_ns_to_secs
#define __print_ns_to_secs(val) (val) / 1000000000UL

#undef __print_ns_without_secs
#define __print_ns_without_secs(val) (val) % 1000000000UL

#undef TP_printk
#define TP_printk(fmt, args...) "\"" fmt "\", "  __stringify(args)

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
