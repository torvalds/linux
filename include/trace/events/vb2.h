/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vb2

#if !defined(_TRACE_VB2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VB2_H

#include <linux/tracepoint.h>
#include <media/videobuf2-core.h>

DECLARE_EVENT_CLASS(vb2_event_class,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb),

	TP_STRUCT__entry(
		__field(void *, owner)
		__field(u32, queued_count)
		__field(int, owned_by_drv_count)
		__field(u32, index)
		__field(u32, type)
		__field(u32, bytesused)
		__field(u64, timestamp)
	),

	TP_fast_assign(
		__entry->owner = q->owner;
		__entry->queued_count = q->queued_count;
		__entry->owned_by_drv_count =
			atomic_read(&q->owned_by_drv_count);
		__entry->index = vb->index;
		__entry->type = vb->type;
		__entry->bytesused = vb->planes[0].bytesused;
		__entry->timestamp = vb->timestamp;
	),

	TP_printk("owner = %p, queued = %u, owned_by_drv = %d, index = %u, "
		  "type = %u, bytesused = %u, timestamp = %llu", __entry->owner,
		  __entry->queued_count,
		  __entry->owned_by_drv_count,
		  __entry->index, __entry->type,
		  __entry->bytesused,
		  __entry->timestamp
	)
)

DEFINE_EVENT(vb2_event_class, vb2_buf_done,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_event_class, vb2_buf_queue,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_event_class, vb2_dqbuf,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_event_class, vb2_qbuf,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

#endif /* if !defined(_TRACE_VB2_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
