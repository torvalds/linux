/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_DA_PEROBJ_PARENT
DEFINE_EVENT(event_da_monitor_id, event_da_perobj_parent,
	     TP_PROTO(int id, char *state, char *event, char *next_state, bool final_state),
	     TP_ARGS(id, state, event, next_state, final_state));

DEFINE_EVENT(error_da_monitor_id, error_da_perobj_parent,
	     TP_PROTO(int id, char *state, char *event),
	     TP_ARGS(id, state, event));
#endif /* CONFIG_RV_MON_DA_PEROBJ_PARENT */
