/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_STALL
DEFINE_EVENT(event_da_monitor_id, event_stall,
	     TP_PROTO(int id, char *state, char *event, char *next_state, bool final_state),
	     TP_ARGS(id, state, event, next_state, final_state));

DEFINE_EVENT(error_da_monitor_id, error_stall,
	     TP_PROTO(int id, char *state, char *event),
	     TP_ARGS(id, state, event));

DEFINE_EVENT(error_env_da_monitor_id, error_env_stall,
	     TP_PROTO(int id, char *state, char *event, char *env),
	     TP_ARGS(id, state, event, env));
#endif /* CONFIG_RV_MON_STALL */
