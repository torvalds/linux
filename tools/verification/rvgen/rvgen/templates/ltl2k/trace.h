/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_%%MODEL_NAME_UP%%
DEFINE_EVENT(event_%%MONITOR_CLASS%%, event_%%MODEL_NAME%%,
	     TP_PROTO(struct task_struct *task, char *states, char *atoms, char *next),
	     TP_ARGS(task, states, atoms, next));
DEFINE_EVENT(error_%%MONITOR_CLASS%%, error_%%MODEL_NAME%%,
	     TP_PROTO(struct task_struct *task),
	     TP_ARGS(task));
#endif /* CONFIG_RV_MON_%%MODEL_NAME_UP%% */
