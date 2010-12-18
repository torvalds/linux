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

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	struct ftrace_raw_##name {				\
		struct trace_entry	ent;			\
		tstruct						\
		char			__data[0];		\
	};							\
	static struct ftrace_event_call			\
	__attribute__((__aligned__(4))) event_##name

#undef __cpparg
#define __cpparg(arg...) arg

/* Callbacks are meaningless to ftrace. */
#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,			\
		assign, print, reg, unreg)				\
	TRACE_EVENT(name, __cpparg(proto), __cpparg(args),		\
		__cpparg(tstruct), __cpparg(assign), __cpparg(print))	\

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

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
	struct ftrace_data_offsets_##call {				\
		tstruct;						\
	};

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

#undef __field_ext
#define __field_ext(type, item, filter_type)	__field(type, item)

#undef __array
#define __array(type, item, len)						\
	ret = trace_seq_printf(s, "\tfield:" #type " " #item "[" #len "];\t"	\
			       "offset:%u;\tsize:%u;\n",		\
			       (unsigned int)offsetof(typeof(field), item), \
			       (unsigned int)sizeof(field.item));	\
	if (!ret)							\
		return 0;

#undef __dynamic_array
#define __dynamic_array(type, item, len)				       \
	ret = trace_seq_printf(s, "\tfield:__data_loc " #type "[] " #item ";\t"\
			       "offset:%u;\tsize:%u;\n",		       \
			       (unsigned int)offsetof(typeof(field),	       \
					__data_loc_##item),		       \
			       (unsigned int)sizeof(field.__data_loc_##item)); \
	if (!ret)							       \
		return 0;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __entry
#define __entry REC

#undef __print_symbolic
#undef __get_dynamic_array
#undef __get_str

#undef TP_printk
#define TP_printk(fmt, args...) "\"%s\", %s\n", fmt, __stringify(args)

#undef TP_fast_assign
#define TP_fast_assign(args...) args

#undef TP_perf_assign
#define TP_perf_assign(args...)

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, func, print)		\
static int								\
ftrace_format_##call(struct ftrace_event_call *unused,			\
		      struct trace_seq *s)				\
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
 *	trace_seq_init(p);
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

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
static enum print_line_t						\
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
	trace_seq_init(p);						\
	ret = trace_seq_printf(s, #call ": " print);			\
	put_cpu();							\
	if (!ret)							\
		return TRACE_TYPE_PARTIAL_LINE;				\
									\
	return TRACE_TYPE_HANDLED;					\
}
	
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
				 sizeof(field.item), 0, FILTER_OTHER);	\
	if (ret)							\
		return ret;

#undef __dynamic_array
#define __dynamic_array(type, item, len)				       \
	ret = trace_define_field(event_call, "__data_loc " #type "[]", #item,  \
				 offsetof(typeof(field), __data_loc_##item),   \
				 sizeof(field.__data_loc_##item), 0,	       \
				 FILTER_OTHER);

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, func, print)		\
static int								\
ftrace_define_fields_##call(struct ftrace_event_call *event_call)	\
{									\
	struct ftrace_raw_##call field;					\
	int ret;							\
									\
	ret = trace_define_common_fields(event_call);			\
	if (ret)							\
		return ret;						\
									\
	tstruct;							\
									\
	return ret;							\
}

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
#define __string(item, src) __dynamic_array(char, item, strlen(src) + 1)       \

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
static inline int ftrace_get_offsets_##call(				\
	struct ftrace_data_offsets_##call *__data_offsets, proto)       \
{									\
	int __data_size = 0;						\
	struct ftrace_raw_##call __maybe_unused *entry;			\
									\
	tstruct;							\
									\
	return __data_size;						\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#ifdef CONFIG_EVENT_PROFILE

/*
 * Generate the functions needed for tracepoint perf_event support.
 *
 * NOTE: The insertion profile callback (ftrace_profile_<call>) is defined later
 *
 * static int ftrace_profile_enable_<call>(void)
 * {
 * 	return register_trace_<call>(ftrace_profile_<call>);
 * }
 *
 * static void ftrace_profile_disable_<call>(void)
 * {
 * 	unregister_trace_<call>(ftrace_profile_<call>);
 * }
 *
 */

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
									\
static void ftrace_profile_##call(proto);				\
									\
static int ftrace_profile_enable_##call(void)				\
{									\
	return register_trace_##call(ftrace_profile_##call);		\
}									\
									\
static void ftrace_profile_disable_##call(void)				\
{									\
	unregister_trace_##call(ftrace_profile_##call);			\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#endif

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
 *	struct ring_buffer *buffer;
 *	unsigned long irq_flags;
 *	int pc;
 *
 *	local_save_flags(irq_flags);
 *	pc = preempt_count();
 *
 *	event = trace_current_buffer_lock_reserve(&buffer,
 *				  event_<call>.id,
 *				  sizeof(struct ftrace_raw_<call>),
 *				  irq_flags, pc);
 *	if (!event)
 *		return;
 *	entry	= ring_buffer_event_data(event);
 *
 *	<assign>;  <-- Here we assign the entries by the __field and
 *			__array macros.
 *
 *	trace_current_buffer_unlock_commit(buffer, event, irq_flags, pc);
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

#define _TRACE_PROFILE_INIT(call)					\
	.profile_count = ATOMIC_INIT(-1),				\
	.profile_enable = ftrace_profile_enable_##call,			\
	.profile_disable = ftrace_profile_disable_##call,

#else
#define _TRACE_PROFILE_INIT(call)
#endif

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

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
									\
static struct ftrace_event_call event_##call;				\
									\
static void ftrace_raw_event_##call(proto)				\
{									\
	struct ftrace_data_offsets_##call __maybe_unused __data_offsets;\
	struct ftrace_event_call *event_call = &event_##call;		\
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
				 event_##call.id,			\
				 sizeof(*entry) + __data_size,		\
				 irq_flags, pc);			\
	if (!event)							\
		return;							\
	entry	= ring_buffer_event_data(event);			\
									\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	if (!filter_current_check_discard(buffer, event_call, entry, event)) \
		trace_nowake_buffer_unlock_commit(buffer,		\
						  event, irq_flags, pc); \
}									\
									\
static int ftrace_raw_reg_event_##call(void *ptr)			\
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
static void ftrace_raw_unreg_event_##call(void *ptr)			\
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

/*
 * Define the insertion callback to profile events
 *
 * The job is very similar to ftrace_raw_event_<call> except that we don't
 * insert in the ring buffer but in a perf counter.
 *
 * static void ftrace_profile_<call>(proto)
 * {
 *	struct ftrace_data_offsets_<call> __maybe_unused __data_offsets;
 *	struct ftrace_event_call *event_call = &event_<call>;
 *	extern void perf_tp_event(int, u64, u64, void *, int);
 *	struct ftrace_raw_##call *entry;
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
 *		raw_data = rcu_dereference(trace_profile_buf_nmi);
 *	else
 *		raw_data = rcu_dereference(trace_profile_buf);
 *
 *	if (!raw_data)
 *		goto end;
 *
 *	raw_data = per_cpu_ptr(raw_data, __cpu);
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

#ifdef CONFIG_EVENT_PROFILE

#undef __perf_addr
#define __perf_addr(a) __addr = (a)

#undef __perf_count
#define __perf_count(c) __count = (c)

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
static void ftrace_profile_##call(proto)				\
{									\
	struct ftrace_data_offsets_##call __maybe_unused __data_offsets;\
	struct ftrace_event_call *event_call = &event_##call;		\
	extern void perf_tp_event(int, u64, u64, void *, int);	\
	struct ftrace_raw_##call *entry;				\
	u64 __addr = 0, __count = 1;					\
	unsigned long irq_flags;					\
	struct trace_entry *ent;					\
	int __entry_size;						\
	int __data_size;						\
	char *raw_data;							\
	int __cpu;							\
	int pc;								\
									\
	pc = preempt_count();						\
									\
	__data_size = ftrace_get_offsets_##call(&__data_offsets, args); \
	__entry_size = ALIGN(__data_size + sizeof(*entry) + sizeof(u32),\
			     sizeof(u64));				\
	__entry_size -= sizeof(u32);					\
									\
	if (WARN_ONCE(__entry_size > FTRACE_MAX_PROFILE_SIZE,		\
		      "profile buffer not large enough"))		\
		return;							\
									\
	local_irq_save(irq_flags);					\
	__cpu = smp_processor_id();					\
									\
	if (in_nmi())							\
		raw_data = rcu_dereference(trace_profile_buf_nmi);		\
	else								\
		raw_data = rcu_dereference(trace_profile_buf);		\
									\
	if (!raw_data)							\
		goto end;						\
									\
	raw_data = per_cpu_ptr(raw_data, __cpu);			\
									\
	*(u64 *)(&raw_data[__entry_size - sizeof(u64)]) = 0ULL;		\
	entry = (struct ftrace_raw_##call *)raw_data;			\
	ent = &entry->ent;						\
	tracing_generic_entry_update(ent, irq_flags, pc);		\
	ent->type = event_call->id;					\
									\
	tstruct								\
									\
	{ assign; }							\
									\
	perf_tp_event(event_call->id, __addr, __count, entry,		\
			     __entry_size);				\
									\
end:									\
	local_irq_restore(irq_flags);					\
									\
}

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
#endif /* CONFIG_EVENT_PROFILE */

#undef _TRACE_PROFILE_INIT

