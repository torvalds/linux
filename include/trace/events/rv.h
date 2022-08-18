/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv

#if !defined(_TRACE_RV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RV_H

#include <linux/rv.h>
#include <linux/tracepoint.h>

#ifdef CONFIG_DA_MON_EVENTS_IMPLICIT
DECLARE_EVENT_CLASS(event_da_monitor,

	TP_PROTO(char *state, char *event, char *next_state, bool final_state),

	TP_ARGS(state, event, next_state, final_state),

	TP_STRUCT__entry(
		__array(	char,	state,		MAX_DA_NAME_LEN	)
		__array(	char,	event,		MAX_DA_NAME_LEN	)
		__array(	char,	next_state,	MAX_DA_NAME_LEN	)
		__field(	bool,	final_state			)
	),

	TP_fast_assign(
		memcpy(__entry->state,		state,		MAX_DA_NAME_LEN);
		memcpy(__entry->event,		event,		MAX_DA_NAME_LEN);
		memcpy(__entry->next_state,	next_state,	MAX_DA_NAME_LEN);
		__entry->final_state		= final_state;
	),

	TP_printk("%s x %s -> %s %s",
		__entry->state,
		__entry->event,
		__entry->next_state,
		__entry->final_state ? "(final)" : "")
);

DECLARE_EVENT_CLASS(error_da_monitor,

	TP_PROTO(char *state, char *event),

	TP_ARGS(state, event),

	TP_STRUCT__entry(
		__array(	char,	state,		MAX_DA_NAME_LEN	)
		__array(	char,	event,		MAX_DA_NAME_LEN	)
	),

	TP_fast_assign(
		memcpy(__entry->state,		state,		MAX_DA_NAME_LEN);
		memcpy(__entry->event,		event,		MAX_DA_NAME_LEN);
	),

	TP_printk("event %s not expected in the state %s",
		__entry->event,
		__entry->state)
);

#ifdef CONFIG_RV_MON_WIP
DEFINE_EVENT(event_da_monitor, event_wip,
	    TP_PROTO(char *state, char *event, char *next_state, bool final_state),
	    TP_ARGS(state, event, next_state, final_state));

DEFINE_EVENT(error_da_monitor, error_wip,
	     TP_PROTO(char *state, char *event),
	     TP_ARGS(state, event));
#endif /* CONFIG_RV_MON_WIP */
#endif /* CONFIG_DA_MON_EVENTS_IMPLICIT */

#ifdef CONFIG_DA_MON_EVENTS_ID
DECLARE_EVENT_CLASS(event_da_monitor_id,

	TP_PROTO(int id, char *state, char *event, char *next_state, bool final_state),

	TP_ARGS(id, state, event, next_state, final_state),

	TP_STRUCT__entry(
		__field(	int,	id				)
		__array(	char,	state,		MAX_DA_NAME_LEN	)
		__array(	char,	event,		MAX_DA_NAME_LEN	)
		__array(	char,	next_state,	MAX_DA_NAME_LEN	)
		__field(	bool,	final_state			)
	),

	TP_fast_assign(
		memcpy(__entry->state,		state,		MAX_DA_NAME_LEN);
		memcpy(__entry->event,		event,		MAX_DA_NAME_LEN);
		memcpy(__entry->next_state,	next_state,	MAX_DA_NAME_LEN);
		__entry->id			= id;
		__entry->final_state		= final_state;
	),

	TP_printk("%d: %s x %s -> %s %s",
		__entry->id,
		__entry->state,
		__entry->event,
		__entry->next_state,
		__entry->final_state ? "(final)" : "")
);

DECLARE_EVENT_CLASS(error_da_monitor_id,

	TP_PROTO(int id, char *state, char *event),

	TP_ARGS(id, state, event),

	TP_STRUCT__entry(
		__field(	int,	id				)
		__array(	char,	state,		MAX_DA_NAME_LEN	)
		__array(	char,	event,		MAX_DA_NAME_LEN	)
	),

	TP_fast_assign(
		memcpy(__entry->state,		state,		MAX_DA_NAME_LEN);
		memcpy(__entry->event,		event,		MAX_DA_NAME_LEN);
		__entry->id			= id;
	),

	TP_printk("%d: event %s not expected in the state %s",
		__entry->id,
		__entry->event,
		__entry->state)
);

#ifdef CONFIG_RV_MON_WWNR
/* id is the pid of the task */
DEFINE_EVENT(event_da_monitor_id, event_wwnr,
	     TP_PROTO(int id, char *state, char *event, char *next_state, bool final_state),
	     TP_ARGS(id, state, event, next_state, final_state));

DEFINE_EVENT(error_da_monitor_id, error_wwnr,
	     TP_PROTO(int id, char *state, char *event),
	     TP_ARGS(id, state, event));
#endif /* CONFIG_RV_MON_WWNR */

#endif /* CONFIG_DA_MON_EVENTS_ID */
#endif /* _TRACE_RV_H */

/* This part ust be outside protection */
#undef TRACE_INCLUDE_PATH
#include <trace/define_trace.h>
