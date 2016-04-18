#undef TRACE_SYSTEM
#define TRACE_SYSTEM v4l2

#if !defined(_TRACE_V4L2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_V4L2_H

#include <linux/tracepoint.h>
#include <media/videobuf2-v4l2.h>

/* Enums require being exported to userspace, for user tool parsing */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

#define show_type(type)							\
	__print_symbolic(type, SHOW_TYPE)

#define SHOW_TYPE							\
	EM( V4L2_BUF_TYPE_VIDEO_CAPTURE,	"VIDEO_CAPTURE" )	\
	EM( V4L2_BUF_TYPE_VIDEO_OUTPUT,		"VIDEO_OUTPUT" )	\
	EM( V4L2_BUF_TYPE_VIDEO_OVERLAY,	"VIDEO_OVERLAY" )	\
	EM( V4L2_BUF_TYPE_VBI_CAPTURE,		"VBI_CAPTURE" )		\
	EM( V4L2_BUF_TYPE_VBI_OUTPUT,		"VBI_OUTPUT" )		\
	EM( V4L2_BUF_TYPE_SLICED_VBI_CAPTURE,   "SLICED_VBI_CAPTURE" )	\
	EM( V4L2_BUF_TYPE_SLICED_VBI_OUTPUT,    "SLICED_VBI_OUTPUT" )	\
	EM( V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY, "VIDEO_OUTPUT_OVERLAY" ) \
	EM( V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "VIDEO_CAPTURE_MPLANE" ) \
	EM( V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,  "VIDEO_OUTPUT_MPLANE" )	\
	EM( V4L2_BUF_TYPE_SDR_CAPTURE,          "SDR_CAPTURE" )		\
	EM( V4L2_BUF_TYPE_SDR_OUTPUT,           "SDR_OUTPUT" )		\
	EMe(V4L2_BUF_TYPE_PRIVATE,		"PRIVATE" )

SHOW_TYPE

#define show_field(field)						\
	__print_symbolic(field, SHOW_FIELD)

#define SHOW_FIELD							\
	EM( V4L2_FIELD_ANY,		"ANY" )				\
	EM( V4L2_FIELD_NONE,		"NONE" )			\
	EM( V4L2_FIELD_TOP,		"TOP" )				\
	EM( V4L2_FIELD_BOTTOM,		"BOTTOM" )			\
	EM( V4L2_FIELD_INTERLACED,	"INTERLACED" )			\
	EM( V4L2_FIELD_SEQ_TB,		"SEQ_TB" )			\
	EM( V4L2_FIELD_SEQ_BT,		"SEQ_BT" )			\
	EM( V4L2_FIELD_ALTERNATE,	"ALTERNATE" )			\
	EM( V4L2_FIELD_INTERLACED_TB,	"INTERLACED_TB" )		\
	EMe( V4L2_FIELD_INTERLACED_BT,	"INTERLACED_BT" )

SHOW_FIELD

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

/* V4L2_TC_TYPE_* are macros, not defines, they do not need processing */

#define show_timecode_type(type)					\
	__print_symbolic(type,						\
		{ V4L2_TC_TYPE_24FPS,		"24FPS" },		\
		{ V4L2_TC_TYPE_25FPS,		"25FPS" },		\
		{ V4L2_TC_TYPE_30FPS,		"30FPS" },		\
		{ V4L2_TC_TYPE_50FPS,		"50FPS" },		\
		{ V4L2_TC_TYPE_60FPS,		"60FPS" })

#define show_flags(flags)						      \
	__print_flags(flags, "|",					      \
		{ V4L2_BUF_FLAG_MAPPED,		     "MAPPED" },	      \
		{ V4L2_BUF_FLAG_QUEUED,		     "QUEUED" },	      \
		{ V4L2_BUF_FLAG_DONE,		     "DONE" },		      \
		{ V4L2_BUF_FLAG_KEYFRAME,	     "KEYFRAME" },	      \
		{ V4L2_BUF_FLAG_PFRAME,		     "PFRAME" },	      \
		{ V4L2_BUF_FLAG_BFRAME,		     "BFRAME" },	      \
		{ V4L2_BUF_FLAG_ERROR,		     "ERROR" },		      \
		{ V4L2_BUF_FLAG_TIMECODE,	     "TIMECODE" },	      \
		{ V4L2_BUF_FLAG_PREPARED,	     "PREPARED" },	      \
		{ V4L2_BUF_FLAG_NO_CACHE_INVALIDATE, "NO_CACHE_INVALIDATE" }, \
		{ V4L2_BUF_FLAG_NO_CACHE_CLEAN,	     "NO_CACHE_CLEAN" },      \
		{ V4L2_BUF_FLAG_TIMESTAMP_MASK,	     "TIMESTAMP_MASK" },      \
		{ V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN,   "TIMESTAMP_UNKNOWN" },   \
		{ V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, "TIMESTAMP_MONOTONIC" }, \
		{ V4L2_BUF_FLAG_TIMESTAMP_COPY,	     "TIMESTAMP_COPY" },      \
		{ V4L2_BUF_FLAG_LAST,                "LAST" })

#define show_timecode_flags(flags)					  \
	__print_flags(flags, "|",					  \
		{ V4L2_TC_FLAG_DROPFRAME,       "DROPFRAME" },		  \
		{ V4L2_TC_FLAG_COLORFRAME,      "COLORFRAME" },		  \
		{ V4L2_TC_USERBITS_USERDEFINED,	"USERBITS_USERDEFINED" }, \
		{ V4L2_TC_USERBITS_8BITCHARS,	"USERBITS_8BITCHARS" })

DECLARE_EVENT_CLASS(v4l2_event_class,
	TP_PROTO(int minor, struct v4l2_buffer *buf),

	TP_ARGS(minor, buf),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(u32, index)
		__field(u32, type)
		__field(u32, bytesused)
		__field(u32, flags)
		__field(u32, field)
		__field(s64, timestamp)
		__field(u32, timecode_type)
		__field(u32, timecode_flags)
		__field(u8, timecode_frames)
		__field(u8, timecode_seconds)
		__field(u8, timecode_minutes)
		__field(u8, timecode_hours)
		__field(u8, timecode_userbits0)
		__field(u8, timecode_userbits1)
		__field(u8, timecode_userbits2)
		__field(u8, timecode_userbits3)
		__field(u32, sequence)
	),

	TP_fast_assign(
		__entry->minor = minor;
		__entry->index = buf->index;
		__entry->type = buf->type;
		__entry->bytesused = buf->bytesused;
		__entry->flags = buf->flags;
		__entry->field = buf->field;
		__entry->timestamp = timeval_to_ns(&buf->timestamp);
		__entry->timecode_type = buf->timecode.type;
		__entry->timecode_flags = buf->timecode.flags;
		__entry->timecode_frames = buf->timecode.frames;
		__entry->timecode_seconds = buf->timecode.seconds;
		__entry->timecode_minutes = buf->timecode.minutes;
		__entry->timecode_hours = buf->timecode.hours;
		__entry->timecode_userbits0 = buf->timecode.userbits[0];
		__entry->timecode_userbits1 = buf->timecode.userbits[1];
		__entry->timecode_userbits2 = buf->timecode.userbits[2];
		__entry->timecode_userbits3 = buf->timecode.userbits[3];
		__entry->sequence = buf->sequence;
	),

	TP_printk("minor = %d, index = %u, type = %s, bytesused = %u, "
		  "flags = %s, field = %s, timestamp = %llu, "
		  "timecode = { type = %s, flags = %s, frames = %u, "
		  "seconds = %u, minutes = %u, hours = %u, "
		  "userbits = { %u %u %u %u } }, sequence = %u", __entry->minor,
		  __entry->index, show_type(__entry->type),
		  __entry->bytesused,
		  show_flags(__entry->flags),
		  show_field(__entry->field),
		  __entry->timestamp,
		  show_timecode_type(__entry->timecode_type),
		  show_timecode_flags(__entry->timecode_flags),
		  __entry->timecode_frames,
		  __entry->timecode_seconds,
		  __entry->timecode_minutes,
		  __entry->timecode_hours,
		  __entry->timecode_userbits0,
		  __entry->timecode_userbits1,
		  __entry->timecode_userbits2,
		  __entry->timecode_userbits3,
		  __entry->sequence
	)
)

DEFINE_EVENT(v4l2_event_class, v4l2_dqbuf,
	TP_PROTO(int minor, struct v4l2_buffer *buf),
	TP_ARGS(minor, buf)
);

DEFINE_EVENT(v4l2_event_class, v4l2_qbuf,
	TP_PROTO(int minor, struct v4l2_buffer *buf),
	TP_ARGS(minor, buf)
);

DECLARE_EVENT_CLASS(vb2_v4l2_event_class,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(u32, flags)
		__field(u32, field)
		__field(u64, timestamp)
		__field(u32, timecode_type)
		__field(u32, timecode_flags)
		__field(u8, timecode_frames)
		__field(u8, timecode_seconds)
		__field(u8, timecode_minutes)
		__field(u8, timecode_hours)
		__field(u8, timecode_userbits0)
		__field(u8, timecode_userbits1)
		__field(u8, timecode_userbits2)
		__field(u8, timecode_userbits3)
		__field(u32, sequence)
	),

	TP_fast_assign(
		struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
		struct v4l2_fh *owner = q->owner;

		__entry->minor = owner ? owner->vdev->minor : -1;
		__entry->flags = vbuf->flags;
		__entry->field = vbuf->field;
		__entry->timestamp = vb->timestamp;
		__entry->timecode_type = vbuf->timecode.type;
		__entry->timecode_flags = vbuf->timecode.flags;
		__entry->timecode_frames = vbuf->timecode.frames;
		__entry->timecode_seconds = vbuf->timecode.seconds;
		__entry->timecode_minutes = vbuf->timecode.minutes;
		__entry->timecode_hours = vbuf->timecode.hours;
		__entry->timecode_userbits0 = vbuf->timecode.userbits[0];
		__entry->timecode_userbits1 = vbuf->timecode.userbits[1];
		__entry->timecode_userbits2 = vbuf->timecode.userbits[2];
		__entry->timecode_userbits3 = vbuf->timecode.userbits[3];
		__entry->sequence = vbuf->sequence;
	),

	TP_printk("minor=%d flags = %s, field = %s, "
		  "timestamp = %llu, timecode = { type = %s, flags = %s, "
		  "frames = %u, seconds = %u, minutes = %u, hours = %u, "
		  "userbits = { %u %u %u %u } }, sequence = %u", __entry->minor,
		  show_flags(__entry->flags),
		  show_field(__entry->field),
		  __entry->timestamp,
		  show_timecode_type(__entry->timecode_type),
		  show_timecode_flags(__entry->timecode_flags),
		  __entry->timecode_frames,
		  __entry->timecode_seconds,
		  __entry->timecode_minutes,
		  __entry->timecode_hours,
		  __entry->timecode_userbits0,
		  __entry->timecode_userbits1,
		  __entry->timecode_userbits2,
		  __entry->timecode_userbits3,
		  __entry->sequence
	)
)

DEFINE_EVENT(vb2_v4l2_event_class, vb2_v4l2_buf_done,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_v4l2_event_class, vb2_v4l2_buf_queue,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_v4l2_event_class, vb2_v4l2_dqbuf,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

DEFINE_EVENT(vb2_v4l2_event_class, vb2_v4l2_qbuf,
	TP_PROTO(struct vb2_queue *q, struct vb2_buffer *vb),
	TP_ARGS(q, vb)
);

#endif /* if !defined(_TRACE_V4L2_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
