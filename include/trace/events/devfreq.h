/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM devfreq

#if !defined(_TRACE_DEVFREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DEVFREQ_H

#include <linux/devfreq.h>
#include <linux/tracepoint.h>

TRACE_EVENT(devfreq_monitor,
	TP_PROTO(struct devfreq *devfreq),

	TP_ARGS(devfreq),

	TP_STRUCT__entry(
		__field(unsigned long, freq)
		__field(unsigned long, busy_time)
		__field(unsigned long, total_time)
		__field(unsigned int, polling_ms)
		__string(dev_name, dev_name(&devfreq->dev))
	),

	TP_fast_assign(
		__entry->freq = devfreq->previous_freq;
		__entry->busy_time = devfreq->last_status.busy_time;
		__entry->total_time = devfreq->last_status.total_time;
		__entry->polling_ms = devfreq->profile->polling_ms;
		__assign_str(dev_name, dev_name(&devfreq->dev));
	),

	TP_printk("dev_name=%s freq=%lu polling_ms=%u load=%lu",
		__get_str(dev_name), __entry->freq, __entry->polling_ms,
		__entry->total_time == 0 ? 0 :
			(100 * __entry->busy_time) / __entry->total_time)
);
#endif /* _TRACE_DEVFREQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
