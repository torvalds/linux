#undef TRACE_SYSTEM
#define TRACE_SYSTEM power

#if !defined(_TRACE_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_POWER_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#ifndef _TRACE_POWER_ENUM_
#define _TRACE_POWER_ENUM_
enum {
	POWER_NONE = 0,
	POWER_CSTATE = 1,
	POWER_PSTATE = 2,
};
#endif



TRACE_EVENT(power_start,

	TP_PROTO(unsigned int type, unsigned int state),

	TP_ARGS(type, state),

	TP_STRUCT__entry(
		__field(	u64,		type		)
		__field(	u64,		state		)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->state = state;
	),

	TP_printk("type=%lu state=%lu", (unsigned long)__entry->type, (unsigned long)__entry->state)
);

TRACE_EVENT(power_end,

	TP_PROTO(int dummy),

	TP_ARGS(dummy),

	TP_STRUCT__entry(
		__field(	u64,		dummy		)
	),

	TP_fast_assign(
		__entry->dummy = 0xffff;
	),

	TP_printk("dummy=%lu", (unsigned long)__entry->dummy)

);


TRACE_EVENT(power_frequency,

	TP_PROTO(unsigned int type, unsigned int state),

	TP_ARGS(type, state),

	TP_STRUCT__entry(
		__field(	u64,		type		)
		__field(	u64,		state		)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->state = state;
	),

	TP_printk("type=%lu state=%lu", (unsigned long)__entry->type, (unsigned long) __entry->state)
);

#endif /* _TRACE_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
