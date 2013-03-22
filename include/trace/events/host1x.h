/*
 * include/trace/events/host1x.h
 *
 * host1x event logging to ftrace.
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM host1x

#if !defined(_TRACE_HOST1X_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOST1X_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(host1x,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field(const char *, name)),
	TP_fast_assign(__entry->name = name;),
	TP_printk("name=%s", __entry->name)
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

#endif /*  _TRACE_HOST1X_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
