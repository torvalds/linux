/* SPDX-License-Identifier: GPL-2.0 */
#ifdef PROTECT_TRACE_INCLUDE_PATH
#undef PROTECT_TRACE_INCLUDE_PATH


#else /* PROTECT_TRACE_INCLUDE_PATH */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM delayacct

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DELAYACCT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DELAYACCT_H

#include <trace/hooks/vendor_hooks.h>

struct task_struct;
struct taskstats;
DECLARE_RESTRICTED_HOOK(android_rvh_delayacct_init,
	TP_PROTO(void *unused),
	TP_ARGS(unused), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_delayacct_tsk_init,
	TP_PROTO(struct task_struct *tsk),
	TP_ARGS(tsk), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_delayacct_tsk_free,
	TP_PROTO(struct task_struct *tsk),
	TP_ARGS(tsk), 1);
DECLARE_HOOK(android_vh_delayacct_blkio_start,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_blkio_end,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p));
DECLARE_HOOK(android_vh_delayacct_add_tsk,
	TP_PROTO(struct taskstats *d, struct task_struct *tsk, int *ret),
	TP_ARGS(d, tsk, ret));
DECLARE_HOOK(android_vh_delayacct_blkio_ticks,
	TP_PROTO(struct task_struct *tsk, __u64 *ret),
	TP_ARGS(tsk, ret));
DECLARE_HOOK(android_vh_delayacct_freepages_start,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_freepages_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_thrashing_start,
	TP_PROTO(bool *in_thrashing),
	TP_ARGS(in_thrashing));
DECLARE_HOOK(android_vh_delayacct_thrashing_end,
	TP_PROTO(bool *in_thrashing),
	TP_ARGS(in_thrashing));
DECLARE_HOOK(android_vh_delayacct_swapin_start,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_swapin_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_compact_start,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_compact_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_wpcopy_start,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_delayacct_wpcopy_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

#endif /* _TRACE_HOOK_DELAYACCT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

#endif /* PROTECT_TRACE_INCLUDE_PATH */
