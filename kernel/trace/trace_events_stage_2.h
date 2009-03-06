/*
 * Stage 2 of the trace events.
 *
 * Override the macros in <trace/trace_event_types.h> to include the following:
 *
 * enum print_line_t
 * ftrace_raw_output_<call>(struct trace_iterator *iter, int flags)
 * {
 *	struct trace_seq *s = &iter->seq;
 *	struct ftrace_raw_<call> *field; <-- defined in stage 1
 *	struct trace_entry *entry;
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
 *	ret = trace_seq_printf(s, <TPRAWFMT> "%s", <ARGS> "\n");
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

#undef TRACE_STRUCT
#define TRACE_STRUCT(args...) args

#undef TRACE_FIELD
#define TRACE_FIELD(type, item, assign) \
	field->item,

#undef TRACE_FIELD_SPECIAL
#define TRACE_FIELD_SPECIAL(type_item, item, cmd) \
	field->item,


#undef TPRAWFMT
#define TPRAWFMT(args...)	args

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
enum print_line_t							\
ftrace_raw_output_##call(struct trace_iterator *iter, int flags)	\
{									\
	struct trace_seq *s = &iter->seq;				\
	struct ftrace_raw_##call *field;				\
	struct trace_entry *entry;					\
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
	ret = trace_seq_printf(s, tpfmt "%s", tstruct "\n");		\
	if (!ret)							\
		return TRACE_TYPE_PARTIAL_LINE;				\
									\
	return TRACE_TYPE_HANDLED;					\
}

#include <trace/trace_event_types.h>

#include "trace_format.h"
#include <trace/trace_event_types.h>
