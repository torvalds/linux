/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_SLEEP
DEFINE_EVENT(event_ltl_monitor_id, event_sleep,
	     TP_PROTO(struct task_struct *task, char *states, char *atoms, char *next),
	     TP_ARGS(task, states, atoms, next));
DEFINE_EVENT(error_ltl_monitor_id, error_sleep,
	     TP_PROTO(struct task_struct *task),
	     TP_ARGS(task));
#endif /* CONFIG_RV_MON_SLEEP */
