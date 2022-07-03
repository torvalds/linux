/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gh_virtio_backend

#if !defined(_TRACE_GH_VIRTIO_BACKEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GH_VIRTIO_BACKEND_H

#include <linux/tracepoint.h>

TRACE_EVENT(gh_virtio_backend_irq_inj,

	TP_PROTO(int label, int rc),

	TP_ARGS(label, rc),

	TP_STRUCT__entry(
		__field(int,		label)
		__field(int,		rc)
	),

	TP_fast_assign(
		__entry->label		= label;
		__entry->rc		= rc;
	),

	TP_printk("device %d inj_irq rc %d", __entry->label, __entry->rc)
);

TRACE_EVENT(gh_virtio_backend_queue_notify,

	TP_PROTO(int label, int qno),

	TP_ARGS(label, qno),

	TP_STRUCT__entry(
		__field(int,		label)
		__field(int,		qno)
	),

	TP_fast_assign(
		__entry->label		= label;
		__entry->qno		= qno;
	),

	TP_printk("device %d queue_notify on %d", __entry->label, __entry->qno)
);

TRACE_EVENT(gh_virtio_backend_wait_event,

	TP_PROTO(int label, int cur_event, int org_event, int cur_event_data, int org_event_data),

	TP_ARGS(label, cur_event, org_event, cur_event_data, org_event_data),

	TP_STRUCT__entry(
		__field(int,	label)
		__field(int,	cur_event)
		__field(int,	org_event)
		__field(int,	cur_event_data)
		__field(int,	org_event_data)
	),

	TP_fast_assign(
		__entry->label		= label;
		__entry->cur_event	= cur_event;
		__entry->cur_event_data	= cur_event_data;
		__entry->org_event	= org_event;
		__entry->org_event_data	= org_event_data;
	),

	TP_printk("device %d cur_evt/org_evt %x/%x, cur_evt_data/org_evt_data %x/%x",
		__entry->label, __entry->cur_event, __entry->org_event,
		__entry->cur_event_data, __entry->org_event_data)
);

TRACE_EVENT(gh_virtio_backend_irq,

	TP_PROTO(int label, int event, int event_data, int rc),

	TP_ARGS(label, event, event_data, rc),

	TP_STRUCT__entry(
		__field(int,		label)
		__field(int,		event)
		__field(int,		event_data)
		__field(int,		rc)
	),

	TP_fast_assign(
		__entry->label		= label;
		__entry->event		= event;
		__entry->event_data	= event_data;
		__entry->rc		= rc;
	),

	TP_printk("device %d irq (rc %d) event %x event_data %x",
		__entry->label, __entry->rc, __entry->event, __entry->event_data)
);


#endif /* _TRACE_GH_VIRTIO_BACKEND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
