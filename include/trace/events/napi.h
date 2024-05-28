/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM napi

#if !defined(_TRACE_NAPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NAPI_H

#include <linux/netdevice.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>

#define NO_DEV "(no_device)"

TRACE_EVENT(napi_poll,

	TP_PROTO(struct napi_struct *napi, int work, int budget),

	TP_ARGS(napi, work, budget),

	TP_STRUCT__entry(
		__field(	struct napi_struct *,	napi)
		__string(	dev_name, napi->dev ? napi->dev->name : NO_DEV)
		__field(	int,			work)
		__field(	int,			budget)
	),

	TP_fast_assign(
		__entry->napi = napi;
		__assign_str(dev_name, napi->dev ? napi->dev->name : NO_DEV);
		__entry->work = work;
		__entry->budget = budget;
	),

	TP_printk("napi poll on napi struct %p for device %s work %d budget %d",
		  __entry->napi, __get_str(dev_name),
		  __entry->work, __entry->budget)
);

TRACE_EVENT(dql_stall_detected,

	TP_PROTO(unsigned short thrs, unsigned int len,
		 unsigned long last_reap, unsigned long hist_head,
		 unsigned long now, unsigned long *hist),

	TP_ARGS(thrs, len, last_reap, hist_head, now, hist),

	TP_STRUCT__entry(
		__field(	unsigned short,		thrs)
		__field(	unsigned int,		len)
		__field(	unsigned long,		last_reap)
		__field(	unsigned long,		hist_head)
		__field(	unsigned long,		now)
		__array(	unsigned long,		hist, 4)
	),

	TP_fast_assign(
		__entry->thrs = thrs;
		__entry->len = len;
		__entry->last_reap = last_reap;
		__entry->hist_head = hist_head * BITS_PER_LONG;
		__entry->now = now;
		memcpy(__entry->hist, hist, sizeof(entry->hist));
	),

	TP_printk("thrs %u  len %u  last_reap %lu  hist_head %lu  now %lu  hist %016lx %016lx %016lx %016lx",
		  __entry->thrs, __entry->len,
		  __entry->last_reap, __entry->hist_head, __entry->now,
		  __entry->hist[0], __entry->hist[1],
		  __entry->hist[2], __entry->hist[3])
);

#undef NO_DEV

#endif /* _TRACE_NAPI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
