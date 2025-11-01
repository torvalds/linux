/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_OPID
DEFINE_EVENT(event_da_monitor, event_opid,
	     TP_PROTO(char *state, char *event, char *next_state, bool final_state),
	     TP_ARGS(state, event, next_state, final_state));

DEFINE_EVENT(error_da_monitor, error_opid,
	     TP_PROTO(char *state, char *event),
	     TP_ARGS(state, event));
#endif /* CONFIG_RV_MON_OPID */
