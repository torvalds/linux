/*
 * Stage 1 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
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
	static struct trace_enum_map __used __initdata	\
	__##TRACE_SYSTEM##_##a =			\
	{						\
		.system = TRACE_SYSTEM_STRING,		\
		.enum_string = #a,			\
		.enum_value = a				\
	};						\
	static struct trace_enum_map __used		\
	__attribute__((section("_ftrace_enum_map")))	\
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
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_FLAGS
#define TRACE_EVENT_FLAGS(event, flag)

#undef TRACE_EVENT_PERF_PERM
#define TRACE_EVENT_PERF_PERM(event, expr...)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 3 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * enum print_line_t
 * trace_raw_output_<call>(struct trace_iterator *iter, int flags)
 * {
 *	struct trace_seq *s = &iter->seq;
 *	struct trace_event_raw_<call> *field; <-- defined in stage 1
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

#undef __get_dynamic_array_len
#define __get_dynamic_array_len(field)	\
		((__entry->__data_loc_##field >> 16) & 0xffff)

#undef __get_str
#define __get_str(field) (char *)__get_dynamic_array(field)

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

#undef __print_symbolic_u64
#if BITS_PER_LONG == 32
#define __print_symbolic_u64(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags_u64 symbols[] =	\
			{ symbol_array, { -1, NULL } };			\
		trace_print_symbols_seq_u64(p, value, symbols);	\
	})
#else
#define __print_symbolic_u64(value, symbol_array...)			\
			__print_symbolic(value, symbol_array)
#endif

#undef __print_hex
#define __print_hex(buf, buf_len) trace_print_hex_seq(p, buf, buf_len)

#undef __print_array
#define __print_array(array, count, el_size)				\
	({								\
		BUILD_BUG_ON(el_size != 1 && el_size != 2 &&		\
			     el_size != 4 && el_size != 8);		\
		trace_print_array_seq(p, array, count, el_size);	\
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
	trace_seq_printf(s, print);					\
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
#define __field_ext(type, item, filter_type)				\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), filter_type);	\
	if (ret)							\
		return ret;

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)			\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 0, filter_type);			\
	if (ret)							\
		return ret;

#undef __field
#define __field(type, item)	__field_ext(type, item, FILTER_OTHER)

#undef __field_struct
#define __field_struct(type, item) __field_struct_ext(type, item, FILTER_OTHER)

#undef __array
#define __array(type, item, len)					\
	do {								\
		char *type_str = #type"["__stringify(len)"]";		\
		BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);			\
		ret = trace_define_field(event_call, type_str, #item,	\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item),			\
				 is_signed_type(type), FILTER_OTHER);	\
		if (ret)						\
			return ret;					\
	} while (0);

#undef __dynamic_array
#define __dynamic_array(type, item, len)				       \
	ret = trace_define_field(event_call, "__data_loc " #type "[]", #item,  \
				 offsetof(typeof(field), __data_loc_##item),   \
				 sizeof(field.__data_loc_##item),	       \
				 is_signed_type(type), FILTER_OTHER);

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, func, print)	\
static int notrace __init						\
trace_event_define_fields_##call(struct trace_event_call *event_call)	\
{									\
	struct trace_event_raw_##call field;				\
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

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

