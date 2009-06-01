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

#undef __array
#define __array(type, item, len)	type	item[len];

#undef __field
#define __field(type, item)		type	item;

#undef __string
#define __string(item, src)		unsigned short	__str_loc_##item;

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	struct ftrace_raw_##name {				\
		struct trace_entry	ent;			\
		tstruct						\
		char			__str_data[0];		\
	};							\
	static struct ftrace_event_call event_##name

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)


/*
 * Stage 2 of the trace events.
 *
 * Include the following:
 *
 * struct ftrace_str_offsets_<call> {
 *	int				<str1>;
 *	int				<str2>;
 *	[...]
 * };
 *
 * The __string() macro will create each int <str>, this is to
 * keep the offset of each string from the beggining of the event
 * once we perform the strlen() of the src strings.
 *
 */

#undef __array
#define __array(type, item, len)

#undef __field
#define __field(type, item);

#undef __string
#define __string(item, src)	int item;

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
	struct ftrace_str_offsets_##call {				\
		tstruct;						\
	};

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
 *	struct trace_seq *p;
 *	int ret;
 *
 *	entry = iter->ent;
 *
 *	if (entry->type != event_<call>.id) {
 *		WARN_ON_ONCE(1);
 *		return TRACE_TYPE_UNHANDLED;
 *	}
 *
 *	field = (typeof(field))entry;
 *
 *	p = get_cpu_var(ftrace_event_seq);
 *	ret = trace_seq_printf(s, <TP_printk> "\n");
 *	put_cpu();
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

#undef __get_str
#define __get_str(field)	((char *)__entry + __entry->__str_loc_##field)

#undef __print_flags
#define __print_flags(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags flags[] =		\
			{ flag_array, { -1, NULL }};			\
		ftrace_print_flags_seq(p, delim, flag, flags);		\
	})

#undef __print_symbolic
#define __print_symbolic(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags symbols[] =	\
			{ symbol_array, { -1, NULL }};			\
		ftrace_print_symbols_seq(p, value, symbols);		\
	})

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
enum print_line_t							\
ftrace_raw_output_##call(struct trace_iterator *iter, int flags)	\
{									\
	struct trace_seq *s = &iter->seq;				\
	struct ftrace_raw_##call *field;				\
	struct trace_entry *entry;					\
	struct trace_seq *p;						\
	int ret;							\
									\
	entry = iter->ent;						\
									\
	if (entry->type != event_##call.id) {				\
		WARN_ON_ONCE(1);					\
		return TRACE_TYPE_UNHANDLED;				\
	}								\
									\
	field = (typeof(field))entry;					\
									\
	p = &get_cpu_var(ftrace_event_seq);				\
	ret = trace_seq_printf(s, #call ": " print);			\
	put_cpu();							\
	if (!ret)							\
		return TRACE_TYPE_PARTIAL_LINE;				\
									\
	return TRACE_TYPE_HANDLED;					\
}
	
#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Setup the showing format of trace point.
 *
 * int
 * ftrace_format_##call(struct trace_seq *s)
 * {
 *	struct ftrace_raw_##call field;
 *	int ret;
 *
 *	ret = trace_seq_printf(s, #type " " #item ";"
 *			       " offset:%u; size:%u;\n",
 *			       offsetof(struct ftrace_raw_##call, item),
 *			       sizeof(field.type));
 *
 * }
 */

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef __field
#define __field(type, item)					\
	ret = trace_seq_printf(s, "\tfield:" #type " " #item ";\t"	\
			       "offset:%u;\tsize:%u;\n",		\
			       (unsigned int)offsetof(typeof(field), item), \
			       (unsigned int)sizeof(field.item));	\
	if (!ret)							\
		return 0;

#undef __array
#define __array(type, item, len)						\
	ret = trace_seq_printf(s, "\tfield:" #type " " #item "[" #len "];\t"	\
			       "offset:%u;\tsize:%u;\n",		\
			       (unsigned int)offsetof(typeof(field), item), \
			       (unsigned int)sizeof(field.item));	\
	if (!ret)							\
		return 0;

#undef __string
#define __string(item, src)						       \
	ret = trace_seq_printf(s, "\tfield:__str_loc " #item ";\t"	       \
			       "offset:%u;\tsize:%u;\n",		       \
			       (unsigned int)offsetof(typeof(field),	       \
					__str_loc_##item),		       \
			       (unsigned int)sizeof(field.__str_loc_##item));  \
	if (!ret)							       \
		return 0;

#undef __entry
#define __entry REC

#undef TP_printk
#define TP_printk(fmt, args...) "%s, %s\n", #fmt, __stringify(args)

#undef TP_fast_assign
#define TP_fast_assign(args...) args

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, func, print)		\
static int								\
ftrace_format_##call(struct trace_seq *s)				\
{									\
	struct ftrace_raw_##call field __attribute__((unused));		\
	int ret = 0;							\
									\
	tstruct;							\
									\
	trace_seq_printf(s, "\nprint fmt: " print);			\
									\
	return ret;							\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef __field
#define __field(type, item)						\
	ret = trace_define_field(event_call, #type, #item,		\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item), is_signed_type(type));	\
	if (ret)							\
		return ret;

#undef __array
#define __array(type, item, len)					\
	BUILD_BUG_ON(len > MAX_FILTER_STR_VAL);				\
	ret = trace_define_field(event_call, #type "[" #len "]", #item,	\
				 offsetof(typeof(field), item),		\
				 sizeof(field.item), 0);		\
	if (ret)							\
		return ret;

#undef __string
#define __string(item, src)						       \
	ret = trace_define_field(event_call, "__str_loc", #item,	       \
				offsetof(typeof(field), __str_loc_##item),     \
				 sizeof(field.__str_loc_##item), 0);

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, func, print)		\
int									\
ftrace_define_fields_##call(void)					\
{									\
	struct ftrace_raw_##call field;					\
	struct ftrace_event_call *event_call = &event_##call;		\
	int ret;							\
									\
	__common_field(int, type, 1);					\
	__common_field(unsigned char, flags, 0);			\
	__common_field(unsigned char, preempt_count, 0);		\
	__common_field(int, pid, 1);					\
	__common_field(int, tgid, 1);					\
									\
	tstruct;							\
									\
	return ret;							\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/*
 * Stage 4 of the trace events.
 *
 * Override the macros in <trace/trace_events.h> to include the following:
 *
 * static void ftrace_event_<call>(proto)
 * {
 *	event_trace_printk(_RET_IP_, "<call>: " <fmt>);
 * }
 *
 * static int ftrace_reg_event_<call>(void)
 * {
 *	int ret;
 *
 *	ret = register_trace_<call>(ftrace_event_<call>);
 *	if (!ret)
 *		pr_info("event trace: Could not activate trace point "
 *			"probe to  <call>");
 *	return ret;
 * }
 *
 * static void ftrace_unreg_event_<call>(void)
 * {
 *	unregister_trace_<call>(ftrace_event_<call>);
 * }
 *
 *
 * For those macros defined with TRACE_EVENT:
 *
 * static struct ftrace_event_call event_<call>;
 *
 * static void ftrace_raw_event_<call>(proto)
 * {
 *	struct ring_buffer_event *event;
 *	struct ftrace_raw_<call> *entry; <-- defined in stage 1
 *	unsigned long irq_flags;
 *	int pc;
 *
 *	local_save_flags(irq_flags);
 *	pc = preempt_count();
 *
 *	event = trace_current_buffer_lock_reserve(event_<call>.id,
 *				  sizeof(struct ftrace_raw_<call>),
 *				  irq_flags, pc);
 *	if (!event)
 *		return;
 *	entry	= ring_buffer_event_data(event);
 *
 *	<assign>;  <-- Here we assign the entries by the __field and
 *			__array macros.
 *
 *	trace_current_buffer_unlock_commit(event, irq_flags, pc);
 * }
 *
 * static int ftrace_raw_reg_event_<call>(void)
 * {
 *	int ret;
 *
 *	ret = register_trace_<call>(ftrace_raw_event_<call>);
 *	if (!ret)
 *		pr_info("event trace: Could not activate trace point "
 *			"probe to <call>");
 *	return ret;
 * }
 *
 * static void ftrace_unreg_event_<call>(void)
 * {
 *	unregister_trace_<call>(ftrace_raw_event_<call>);
 * }
 *
 * static struct trace_event ftrace_event_type_<call> = {
 *	.trace			= ftrace_raw_output_<call>, <-- stage 2
 * };
 *
 * static int ftrace_raw_init_event_<call>(void)
 * {
 *	int id;
 *
 *	id = register_ftrace_event(&ftrace_event_type_<call>);
 *	if (!id)
 *		return -ENODEV;
 *	event_<call>.id = id;
 *	return 0;
 * }
 *
 * static struct ftrace_event_call __used
 * __attribute__((__aligned__(4)))
 * __attribute__((section("_ftrace_events"))) event_<call> = {
 *	.name			= "<call>",
 *	.system			= "<system>",
 *	.raw_init		= ftrace_raw_init_event_<call>,
 *	.regfunc		= ftrace_reg_event_<call>,
 *	.unregfunc		= ftrace_unreg_event_<call>,
 *	.show_format		= ftrace_format_<call>,
 * }
 *
 */

#undef TP_FMT
#define TP_FMT(fmt, args...)	fmt "\n", ##args

#ifdef CONFIG_EVENT_PROFILE
#define _TRACE_PROFILE(call, proto, args)				\
static void ftrace_profile_##call(proto)				\
{									\
	extern void perf_tpcounter_event(int);				\
	perf_tpcounter_event(event_##call.id);				\
}									\
									\
static int ftrace_profile_enable_##call(struct ftrace_event_call *event_call) \
{									\
	int ret = 0;							\
									\
	if (!atomic_inc_return(&event_call->profile_count))		\
		ret = register_trace_##call(ftrace_profile_##call);	\
									\
	return ret;							\
}									\
									\
static void ftrace_profile_disable_##call(struct ftrace_event_call *event_call)\
{									\
	if (atomic_add_negative(-1, &event_call->profile_count))	\
		unregister_trace_##call(ftrace_profile_##call);		\
}

#define _TRACE_PROFILE_INIT(call)					\
	.profile_count = ATOMIC_INIT(-1),				\
	.profile_enable = ftrace_profile_enable_##call,			\
	.profile_disable = ftrace_profile_disable_##call,

#else
#define _TRACE_PROFILE(call, proto, args)
#define _TRACE_PROFILE_INIT(call)
#endif

#undef __entry
#define __entry entry

#undef __field
#define __field(type, item)

#undef __array
#define __array(type, item, len)

#undef __string
#define __string(item, src)						\
	__str_offsets.item = __str_size +				\
			     offsetof(typeof(*entry), __str_data);	\
	__str_size += strlen(src) + 1;

#undef __assign_str
#define __assign_str(dst, src)						\
	__entry->__str_loc_##dst = __str_offsets.dst;			\
	strcpy(__get_str(dst), src);

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
_TRACE_PROFILE(call, PARAMS(proto), PARAMS(args))			\
									\
static struct ftrace_event_call event_##call;				\
									\
static void ftrace_raw_event_##call(proto)				\
{									\
	struct ftrace_str_offsets_##call __maybe_unused __str_offsets;	\
	struct ftrace_event_call *event_call = &event_##call;		\
	struct ring_buffer_event *event;				\
	struct ftrace_raw_##call *entry;				\
	unsigned long irq_flags;					\
	int __str_size = 0;						\
	int pc;								\
									\
	local_save_flags(irq_flags);					\
	pc = preempt_count();						\
									\
	tstruct;							\
									\
	event = trace_current_buffer_lock_reserve(event_##call.id,	\
				 sizeof(struct ftrace_raw_##call) + __str_size,\
				 irq_flags, pc);			\
	if (!event)							\
		return;							\
	entry	= ring_buffer_event_data(event);			\
									\
	{ assign; }							\
									\
	if (!filter_current_check_discard(event_call, entry, event))	\
		trace_nowake_buffer_unlock_commit(event, irq_flags, pc); \
}									\
									\
static int ftrace_raw_reg_event_##call(void)				\
{									\
	int ret;							\
									\
	ret = register_trace_##call(ftrace_raw_event_##call);		\
	if (ret)							\
		pr_info("event trace: Could not activate trace point "	\
			"probe to " #call "\n");			\
	return ret;							\
}									\
									\
static void ftrace_raw_unreg_event_##call(void)				\
{									\
	unregister_trace_##call(ftrace_raw_event_##call);		\
}									\
									\
static struct trace_event ftrace_event_type_##call = {			\
	.trace			= ftrace_raw_output_##call,		\
};									\
									\
static int ftrace_raw_init_event_##call(void)				\
{									\
	int id;								\
									\
	id = register_ftrace_event(&ftrace_event_type_##call);		\
	if (!id)							\
		return -ENODEV;						\
	event_##call.id = id;						\
	INIT_LIST_HEAD(&event_##call.fields);				\
	init_preds(&event_##call);					\
	return 0;							\
}									\
									\
static struct ftrace_event_call __used					\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.system			= __stringify(TRACE_SYSTEM),		\
	.event			= &ftrace_event_type_##call,		\
	.raw_init		= ftrace_raw_init_event_##call,		\
	.regfunc		= ftrace_raw_reg_event_##call,		\
	.unregfunc		= ftrace_raw_unreg_event_##call,	\
	.show_format		= ftrace_format_##call,			\
	.define_fields		= ftrace_define_fields_##call,		\
	_TRACE_PROFILE_INIT(call)					\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef _TRACE_PROFILE
#undef _TRACE_PROFILE_INIT

