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

#undef TRACE_FORMAT
#define TRACE_FORMAT(call, proto, args, fmt)

#undef __array
#define __array(type, item, len)	type	item[len];

#undef __field
#define __field(type, item)		type	item;

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	struct ftrace_raw_##name {				\
		struct trace_entry	ent;			\
		tstruct						\
	};							\
	static struct ftrace_event_call event_##name

#include <trace/trace_events.h>
