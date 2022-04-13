/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Like the headers that use TRACE_EVENT(), the TRACE_CUSTOM_EVENT()
 * needs a header that allows for multiple inclusions.
 *
 * Test for a unique name (here we have _TRACE_CUSTOM_SCHED_H),
 * also allowing to continue if TRACE_CUSTOM_MULTI_READ is defined.
 */
#if !defined(_TRACE_CUSTOM_SCHED_H) || defined(TRACE_CUSTOM_MULTI_READ)
#define _TRACE_CUSTOM_SCHED_H

/* Include linux/trace_events.h for initial defines of TRACE_CUSTOM_EVENT() */
#include <linux/trace_events.h>

/*
 * TRACE_CUSTOM_EVENT() is just like TRACE_EVENT(). The first parameter
 * is the event name of an existing event where the TRACE_EVENT has been included
 * in the C file before including this file.
 */
TRACE_CUSTOM_EVENT(sched_switch,

	/*
	 * The TP_PROTO() and TP_ARGS must match the trace event
	 * that the custom event is using.
	 */
	TP_PROTO(bool preempt,
		 unsigned int prev_state,
		 struct task_struct *prev,
		 struct task_struct *next),

	TP_ARGS(preempt, prev_state, prev, next),

	/*
	 * The next fields are where the customization happens.
	 * The TP_STRUCT__entry() defines what will be recorded
	 * in the ring buffer when the custom event triggers.
	 *
	 * The rest is just like the TRACE_EVENT() macro except that
	 * it uses the custom entry.
	 */
	TP_STRUCT__entry(
		__field(	unsigned short,		prev_prio	)
		__field(	unsigned short,		next_prio	)
		__field(	pid_t,	next_pid			)
	),

	TP_fast_assign(
		__entry->prev_prio	= prev->prio;
		__entry->next_pid	= next->pid;
		__entry->next_prio	= next->prio;
	),

	TP_printk("prev_prio=%d next_pid=%d next_prio=%d",
		  __entry->prev_prio, __entry->next_pid, __entry->next_prio)
)


TRACE_CUSTOM_EVENT(sched_waking,

	TP_PROTO(struct task_struct *p),

	TP_ARGS(p),

	TP_STRUCT__entry(
		__field(	pid_t,			pid	)
		__field(	unsigned short,		prio	)
	),

	TP_fast_assign(
		__entry->pid	= p->pid;
		__entry->prio	= p->prio;
	),

	TP_printk("pid=%d prio=%d", __entry->pid, __entry->prio)
)
#endif
/*
 * Just like the headers that create TRACE_EVENTs, the below must
 * be outside the protection of the above #if block.
 */

/*
 * It is required that the Makefile includes:
 *    CFLAGS_<c_file>.o := -I$(src)
 */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .

/*
 * It is requred that the TRACE_INCLUDE_FILE be the same
 * as this file without the ".h".
 */
#define TRACE_INCLUDE_FILE trace_custom_sched
#include <trace/define_custom_trace.h>
