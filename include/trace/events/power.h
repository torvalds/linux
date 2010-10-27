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

DECLARE_EVENT_CLASS(power,

	TP_PROTO(unsigned int type, unsigned int state, unsigned int cpu_id),

	TP_ARGS(type, state, cpu_id),

	TP_STRUCT__entry(
		__field(	u64,		type		)
		__field(	u64,		state		)
		__field(	u64,		cpu_id		)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->state = state;
		__entry->cpu_id = cpu_id;
	),

	TP_printk("type=%lu state=%lu cpu_id=%lu", (unsigned long)__entry->type,
		(unsigned long)__entry->state, (unsigned long)__entry->cpu_id)
);

DEFINE_EVENT(power, power_start,

	TP_PROTO(unsigned int type, unsigned int state, unsigned int cpu_id),

	TP_ARGS(type, state, cpu_id)
);

DEFINE_EVENT(power, power_frequency,

	TP_PROTO(unsigned int type, unsigned int state, unsigned int cpu_id),

	TP_ARGS(type, state, cpu_id)
);

TRACE_EVENT(power_end,

	TP_PROTO(unsigned int cpu_id),

	TP_ARGS(cpu_id),

	TP_STRUCT__entry(
		__field(	u64,		cpu_id		)
	),

	TP_fast_assign(
		__entry->cpu_id = cpu_id;
	),

	TP_printk("cpu_id=%lu", (unsigned long)__entry->cpu_id)

);

#endif /* _TRACE_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
