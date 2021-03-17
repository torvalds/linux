/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/trace/events/host1x.h
 *
 * host1x event logging to ftrace.
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM host1x

#if !defined(_TRACE_HOST1X_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOST1X_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

struct host1x_bo;

DECLARE_EVENT_CLASS(host1x,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field(const char *, name)),
	TP_fast_assign(__entry->name = name;),
	TP_printk("name=%s", __entry->name)
);

DEFINE_EVENT(host1x, host1x_channel_open,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DEFINE_EVENT(host1x, host1x_channel_release,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DEFINE_EVENT(host1x, host1x_cdma_begin,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

DEFINE_EVENT(host1x, host1x_cdma_end,
	TP_PROTO(const char *name),
	TP_ARGS(name)
);

TRACE_EVENT(host1x_cdma_push,
	TP_PROTO(const char *name, u32 op1, u32 op2),

	TP_ARGS(name, op1, op2),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, op1)
		__field(u32, op2)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->op1 = op1;
		__entry->op2 = op2;
	),

	TP_printk("name=%s, op1=%08x, op2=%08x",
		__entry->name, __entry->op1, __entry->op2)
);

TRACE_EVENT(host1x_cdma_push_wide,
	TP_PROTO(const char *name, u32 op1, u32 op2, u32 op3, u32 op4),

	TP_ARGS(name, op1, op2, op3, op4),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, op1)
		__field(u32, op2)
		__field(u32, op3)
		__field(u32, op4)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->op1 = op1;
		__entry->op2 = op2;
		__entry->op3 = op3;
		__entry->op4 = op4;
	),

	TP_printk("name=%s, op1=%08x, op2=%08x, op3=%08x op4=%08x",
		__entry->name, __entry->op1, __entry->op2, __entry->op3,
		__entry->op4)
);

TRACE_EVENT(host1x_cdma_push_gather,
	TP_PROTO(const char *name, struct host1x_bo *bo,
			u32 words, u32 offset, void *cmdbuf),

	TP_ARGS(name, bo, words, offset, cmdbuf),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(struct host1x_bo *, bo)
		__field(u32, words)
		__field(u32, offset)
		__field(bool, cmdbuf)
		__dynamic_array(u32, cmdbuf, words)
	),

	TP_fast_assign(
		if (cmdbuf) {
			memcpy(__get_dynamic_array(cmdbuf), cmdbuf+offset,
					words * sizeof(u32));
		}
		__entry->cmdbuf = cmdbuf;
		__entry->name = name;
		__entry->bo = bo;
		__entry->words = words;
		__entry->offset = offset;
	),

	TP_printk("name=%s, bo=%p, words=%u, offset=%d, contents=[%s]",
	  __entry->name, __entry->bo,
	  __entry->words, __entry->offset,
	  __print_hex(__get_dynamic_array(cmdbuf),
		  __entry->cmdbuf ? __entry->words * 4 : 0))
);

TRACE_EVENT(host1x_channel_submit,
	TP_PROTO(const char *name, u32 cmdbufs, u32 relocs, u32 syncpt_id,
		 u32 syncpt_incrs),

	TP_ARGS(name, cmdbufs, relocs, syncpt_id, syncpt_incrs),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, cmdbufs)
		__field(u32, relocs)
		__field(u32, syncpt_id)
		__field(u32, syncpt_incrs)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->cmdbufs = cmdbufs;
		__entry->relocs = relocs;
		__entry->syncpt_id = syncpt_id;
		__entry->syncpt_incrs = syncpt_incrs;
	),

	TP_printk("name=%s, cmdbufs=%u, relocs=%u, syncpt_id=%u, "
		  "syncpt_incrs=%u",
		  __entry->name, __entry->cmdbufs, __entry->relocs,
		  __entry->syncpt_id, __entry->syncpt_incrs)
);

TRACE_EVENT(host1x_channel_submitted,
	TP_PROTO(const char *name, u32 syncpt_base, u32 syncpt_max),

	TP_ARGS(name, syncpt_base, syncpt_max),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, syncpt_base)
		__field(u32, syncpt_max)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->syncpt_base = syncpt_base;
		__entry->syncpt_max = syncpt_max;
	),

	TP_printk("name=%s, syncpt_base=%d, syncpt_max=%d",
		__entry->name, __entry->syncpt_base, __entry->syncpt_max)
);

TRACE_EVENT(host1x_channel_submit_complete,
	TP_PROTO(const char *name, int count, u32 thresh),

	TP_ARGS(name, count, thresh),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(int, count)
		__field(u32, thresh)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->count = count;
		__entry->thresh = thresh;
	),

	TP_printk("name=%s, count=%d, thresh=%d",
		__entry->name, __entry->count, __entry->thresh)
);

TRACE_EVENT(host1x_wait_cdma,
	TP_PROTO(const char *name, u32 eventid),

	TP_ARGS(name, eventid),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(u32, eventid)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->eventid = eventid;
	),

	TP_printk("name=%s, event=%d", __entry->name, __entry->eventid)
);

TRACE_EVENT(host1x_syncpt_load_min,
	TP_PROTO(u32 id, u32 val),

	TP_ARGS(id, val),

	TP_STRUCT__entry(
		__field(u32, id)
		__field(u32, val)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->val = val;
	),

	TP_printk("id=%d, val=%d", __entry->id, __entry->val)
);

TRACE_EVENT(host1x_syncpt_wait_check,
	TP_PROTO(struct host1x_bo *bo, u32 offset, u32 syncpt_id, u32 thresh,
		 u32 min),

	TP_ARGS(bo, offset, syncpt_id, thresh, min),

	TP_STRUCT__entry(
		__field(struct host1x_bo *, bo)
		__field(u32, offset)
		__field(u32, syncpt_id)
		__field(u32, thresh)
		__field(u32, min)
	),

	TP_fast_assign(
		__entry->bo = bo;
		__entry->offset = offset;
		__entry->syncpt_id = syncpt_id;
		__entry->thresh = thresh;
		__entry->min = min;
	),

	TP_printk("bo=%p, offset=%05x, id=%d, thresh=%d, current=%d",
		__entry->bo, __entry->offset,
		__entry->syncpt_id, __entry->thresh,
		__entry->min)
);

#endif /*  _TRACE_HOST1X_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
