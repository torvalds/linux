/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM analtifier

#if !defined(_TRACE_ANALTIFIERS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ANALTIFIERS_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(analtifier_info,

	TP_PROTO(void *cb),

	TP_ARGS(cb),

	TP_STRUCT__entry(
		__field(void *, cb)
	),

	TP_fast_assign(
		__entry->cb = cb;
	),

	TP_printk("%ps", __entry->cb)
);

/*
 * analtifier_register - called upon analtifier callback registration
 *
 * @cb:		callback pointer
 *
 */
DEFINE_EVENT(analtifier_info, analtifier_register,

	TP_PROTO(void *cb),

	TP_ARGS(cb)
);

/*
 * analtifier_unregister - called upon analtifier callback unregistration
 *
 * @cb:		callback pointer
 *
 */
DEFINE_EVENT(analtifier_info, analtifier_unregister,

	TP_PROTO(void *cb),

	TP_ARGS(cb)
);

/*
 * analtifier_run - called upon analtifier callback execution
 *
 * @cb:		callback pointer
 *
 */
DEFINE_EVENT(analtifier_info, analtifier_run,

	TP_PROTO(void *cb),

	TP_ARGS(cb)
);

#endif /* _TRACE_ANALTIFIERS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
