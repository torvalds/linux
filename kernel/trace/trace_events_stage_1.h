/*
 * Stage 1 of the trace events.
 *
 * Override the macros in <trace/trace_event_types.h> to include the following:
 *
 * struct ftrace_raw_<call> {
 *	struct trace_entry		ent;
 *	<type>				<item>;
 *	[...]
 * };
 *
 * The <type> <item> is created by the TRACE_FIELD(type, item, assign)
 * macro. We simply do "type item;", and that will create the fields
 * in the structure.
 */

#undef TRACE_FORMAT
#define TRACE_FORMAT(call, proto, args, fmt)

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(name, proto, args, fmt, tstruct, tpfmt)	\
	struct ftrace_raw_##name {					\
		struct trace_entry	ent;				\
		tstruct							\
	};								\
	static struct ftrace_event_call event_##name

#undef TRACE_STRUCT
#define TRACE_STRUCT(args...) args

#define TRACE_FIELD(type, item, assign) \
	type item;
#define TRACE_FIELD_SPECIAL(type_item, item, cmd) \
	type_item;

#include <trace/trace_event_types.h>
