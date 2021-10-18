/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpufreq

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CPUFREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CPUFREQ_H

#include <linux/cpufreq.h>
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_show_max_freq,
	TP_PROTO(struct cpufreq_policy *policy, unsigned int *max_freq),
	TP_ARGS(policy, max_freq), 1);

DECLARE_HOOK(android_vh_freq_table_limits,
	TP_PROTO(struct cpufreq_policy *policy, unsigned int min_freq,
		 unsigned int max_freq),
	TP_ARGS(policy, min_freq, max_freq));

DECLARE_RESTRICTED_HOOK(android_rvh_cpufreq_transition,
	TP_PROTO(struct cpufreq_policy *policy),
	TP_ARGS(policy), 1);

#endif /* _TRACE_HOOK_CPUFREQ_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
