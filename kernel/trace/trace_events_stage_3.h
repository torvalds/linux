/*
 * Stage 3 of the trace events.
 *
 * Override the macros in <trace/trace_event_types.h> to include the following:
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
 * For those macros defined with TRACE_FORMAT:
 *
 * static struct ftrace_event_call __used
 * __attribute__((__aligned__(4)))
 * __attribute__((section("_ftrace_events"))) event_<call> = {
 *	.name			= "<call>",
 *	.regfunc		= ftrace_reg_event_<call>,
 *	.unregfunc		= ftrace_unreg_event_<call>,
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
static int ftrace_profile_enable_##call(struct ftrace_event_call *call) \
{									\
	int ret = 0;							\
									\
	if (!atomic_inc_return(&call->profile_count))			\
		ret = register_trace_##call(ftrace_profile_##call);	\
									\
	return ret;							\
}									\
									\
static void ftrace_profile_disable_##call(struct ftrace_event_call *call) \
{									\
	if (atomic_add_negative(-1, &call->profile_count))		\
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

#define _TRACE_FORMAT(call, proto, args, fmt)				\
static void ftrace_event_##call(proto)					\
{									\
	event_trace_printk(_RET_IP_, #call ": " fmt);			\
}									\
									\
static int ftrace_reg_event_##call(void)				\
{									\
	int ret;							\
									\
	ret = register_trace_##call(ftrace_event_##call);		\
	if (ret)							\
		pr_info("event trace: Could not activate trace point "	\
			"probe to " #call "\n");			\
	return ret;							\
}									\
									\
static void ftrace_unreg_event_##call(void)				\
{									\
	unregister_trace_##call(ftrace_event_##call);			\
}									\
									\
static struct ftrace_event_call event_##call;				\
									\
static int ftrace_init_event_##call(void)				\
{									\
	int id;								\
									\
	id = register_ftrace_event(NULL);				\
	if (!id)							\
		return -ENODEV;						\
	event_##call.id = id;						\
	return 0;							\
}

#undef TRACE_FORMAT
#define TRACE_FORMAT(call, proto, args, fmt)				\
_TRACE_FORMAT(call, PARAMS(proto), PARAMS(args), PARAMS(fmt))		\
_TRACE_PROFILE(call, PARAMS(proto), PARAMS(args))			\
static struct ftrace_event_call __used					\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.system			= __stringify(TRACE_SYSTEM),		\
	.raw_init		= ftrace_init_event_##call,		\
	.regfunc		= ftrace_reg_event_##call,		\
	.unregfunc		= ftrace_unreg_event_##call,		\
	_TRACE_PROFILE_INIT(call)					\
}

#undef __entry
#define __entry entry

#undef TRACE_EVENT
#define TRACE_EVENT(call, proto, args, tstruct, assign, print)		\
_TRACE_PROFILE(call, PARAMS(proto), PARAMS(args))			\
									\
static struct ftrace_event_call event_##call;				\
									\
static void ftrace_raw_event_##call(proto)				\
{									\
	struct ftrace_event_call *call = &event_##call;			\
	struct ring_buffer_event *event;				\
	struct ftrace_raw_##call *entry;				\
	unsigned long irq_flags;					\
	int pc;								\
									\
	local_save_flags(irq_flags);					\
	pc = preempt_count();						\
									\
	event = trace_current_buffer_lock_reserve(event_##call.id,	\
				  sizeof(struct ftrace_raw_##call),	\
				  irq_flags, pc);			\
	if (!event)							\
		return;							\
	entry	= ring_buffer_event_data(event);			\
									\
	assign;								\
									\
	if (call->preds && !filter_match_preds(call, entry))		\
		trace_current_buffer_discard_commit(event);		\
	else								\
		trace_nowake_buffer_unlock_commit(event, irq_flags, pc); \
									\
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
	return 0;							\
}									\
									\
static struct ftrace_event_call __used					\
__attribute__((__aligned__(4)))						\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name			= #call,				\
	.system			= __stringify(TRACE_SYSTEM),		\
	.raw_init		= ftrace_raw_init_event_##call,		\
	.regfunc		= ftrace_raw_reg_event_##call,		\
	.unregfunc		= ftrace_raw_unreg_event_##call,	\
	.show_format		= ftrace_format_##call,			\
	.define_fields		= ftrace_define_fields_##call,		\
	_TRACE_PROFILE_INIT(call)					\
}

#include <trace/trace_event_types.h>

#undef _TRACE_PROFILE
#undef _TRACE_PROFILE_INIT

