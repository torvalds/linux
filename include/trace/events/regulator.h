/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM regulator

#if !defined(_TRACE_REGULATOR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_REGULATOR_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

/*
 * Events which just log themselves and the regulator name for enable/disable
 * type tracking.
 */
DECLARE_EVENT_CLASS(regulator_basic,

	TP_PROTO(const char *name),

	TP_ARGS(name),

	TP_STRUCT__entry(
		__string(	name,	name	)
	),

	TP_fast_assign(
		__assign_str(name);
	),

	TP_printk("name=%s", __get_str(name))

);

DEFINE_EVENT(regulator_basic, regulator_enable,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_enable_delay,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_enable_complete,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_disable,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_disable_complete,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_bypass_enable,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_bypass_enable_complete,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_bypass_disable,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

DEFINE_EVENT(regulator_basic, regulator_bypass_disable_complete,

	TP_PROTO(const char *name),

	TP_ARGS(name)

);

/*
 * Events that take a range of numerical values, mostly for voltages
 * and so on.
 */
DECLARE_EVENT_CLASS(regulator_range,

	TP_PROTO(const char *name, int min, int max),

	TP_ARGS(name, min, max),

	TP_STRUCT__entry(
		__string(	name,		name		)
		__field(        int,            min             )
		__field(        int,            max             )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->min  = min;
		__entry->max  = max;
	),

	TP_printk("name=%s (%d-%d)", __get_str(name),
		  (int)__entry->min, (int)__entry->max)
);

DEFINE_EVENT(regulator_range, regulator_set_voltage,

	TP_PROTO(const char *name, int min, int max),

	TP_ARGS(name, min, max)

);


/*
 * Events that take a single value, mostly for readback and refcounts.
 */
DECLARE_EVENT_CLASS(regulator_value,

	TP_PROTO(const char *name, unsigned int val),

	TP_ARGS(name, val),

	TP_STRUCT__entry(
		__string(	name,		name		)
		__field(        unsigned int,   val             )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->val  = val;
	),

	TP_printk("name=%s, val=%u", __get_str(name),
		  (int)__entry->val)
);

DEFINE_EVENT(regulator_value, regulator_set_voltage_complete,

	TP_PROTO(const char *name, unsigned int value),

	TP_ARGS(name, value)

);

#endif /* _TRACE_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
