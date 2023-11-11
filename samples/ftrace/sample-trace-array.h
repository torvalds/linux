/* SPDX-License-Identifier: GPL-2.0 */

/*
 * If TRACE_SYSTEM is defined, that will be the directory created
 * in the ftrace directory under /sys/kernel/tracing/events/<system>
 *
 * The define_trace.h below will also look for a file name of
 * TRACE_SYSTEM.h where TRACE_SYSTEM is what is defined here.
 * In this case, it would look for sample-trace.h
 *
 * If the header name will be different than the system name
 * (as in this case), then you can override the header name that
 * define_trace.h will look up by defining TRACE_INCLUDE_FILE
 *
 * This file is called sample-trace-array.h but we want the system
 * to be called "sample-subsystem". Therefore we must define the name of this
 * file:
 *
 * #define TRACE_INCLUDE_FILE sample-trace-array
 *
 * As we do in the bottom of this file.
 *
 * Notice that TRACE_SYSTEM should be defined outside of #if
 * protection, just like TRACE_INCLUDE_FILE.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sample-subsystem

/*
 * TRACE_SYSTEM is expected to be a C valid variable (alpha-numeric
 * and underscore), although it may start with numbers. If for some
 * reason it is not, you need to add the following lines:
 */
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR sample_subsystem

/*
 * But the above is only needed if TRACE_SYSTEM is not alpha-numeric
 * and underscored. By default, TRACE_SYSTEM_VAR will be equal to
 * TRACE_SYSTEM. As TRACE_SYSTEM_VAR must be alpha-numeric, if
 * TRACE_SYSTEM is not, then TRACE_SYSTEM_VAR must be defined with
 * only alpha-numeric and underscores.
 *
 * The TRACE_SYSTEM_VAR is only used internally and not visible to
 * user space.
 */

/*
 * Notice that this file is not protected like a normal header.
 * We also must allow for rereading of this file. The
 *
 *  || defined(TRACE_HEADER_MULTI_READ)
 *
 * serves this purpose.
 */
#if !defined(_SAMPLE_TRACE_ARRAY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SAMPLE_TRACE_ARRAY_H

#include <linux/tracepoint.h>
TRACE_EVENT(sample_event,

	TP_PROTO(int count, unsigned long time),

	TP_ARGS(count, time),

	TP_STRUCT__entry(
		__field(int, count)
		__field(unsigned long, time)
	),

	TP_fast_assign(
		__entry->count = count;
		__entry->time = time;
	),

	TP_printk("count value=%d at jiffies=%lu", __entry->count,
		__entry->time)
	);
#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE sample-trace-array
#include <trace/define_trace.h>
