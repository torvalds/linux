/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Snippet to be included in rv_trace.h
 */

#ifdef CONFIG_RV_MON_%%MODEL_NAME_UP%%
DEFINE_EVENT(event_%%MONITOR_CLASS%%, event_%%MODEL_NAME%%,
%%TRACEPOINT_ARGS_SKEL_EVENT%%);

DEFINE_EVENT(error_%%MONITOR_CLASS%%, error_%%MODEL_NAME%%,
%%TRACEPOINT_ARGS_SKEL_ERROR%%);
#endif /* CONFIG_RV_MON_%%MODEL_NAME_UP%% */
