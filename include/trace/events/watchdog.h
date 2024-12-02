/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM watchdog

#if !defined(_TRACE_WATCHDOG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WATCHDOG_H

#include <linux/watchdog.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(watchdog_template,

	TP_PROTO(struct watchdog_device *wdd, int err),

	TP_ARGS(wdd, err),

	TP_STRUCT__entry(
		__field(int, id)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->id = wdd->id;
		__entry->err = err;
	),

	TP_printk("watchdog%d err=%d", __entry->id, __entry->err)
);

DEFINE_EVENT(watchdog_template, watchdog_start,
	TP_PROTO(struct watchdog_device *wdd, int err),
	TP_ARGS(wdd, err));

DEFINE_EVENT(watchdog_template, watchdog_ping,
	TP_PROTO(struct watchdog_device *wdd, int err),
	TP_ARGS(wdd, err));

DEFINE_EVENT(watchdog_template, watchdog_stop,
	TP_PROTO(struct watchdog_device *wdd, int err),
	TP_ARGS(wdd, err));

TRACE_EVENT(watchdog_set_timeout,

	TP_PROTO(struct watchdog_device *wdd, unsigned int timeout, int err),

	TP_ARGS(wdd, timeout, err),

	TP_STRUCT__entry(
		__field(int, id)
		__field(unsigned int, timeout)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->id = wdd->id;
		__entry->timeout = timeout;
		__entry->err = err;
	),

	TP_printk("watchdog%d timeout=%u err=%d", __entry->id, __entry->timeout, __entry->err)
);

#endif /* !defined(_TRACE_WATCHDOG_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
