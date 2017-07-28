#undef TRACE_SYSTEM
#define TRACE_SYSTEM vsock

#if !defined(_TRACE_VSOCK_VIRTIO_TRANSPORT_COMMON_H) || \
    defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VSOCK_VIRTIO_TRANSPORT_COMMON_H

#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(VIRTIO_VSOCK_TYPE_STREAM);

#define show_type(val) \
	__print_symbolic(val, { VIRTIO_VSOCK_TYPE_STREAM, "STREAM" })

TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_INVALID);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_REQUEST);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_RESPONSE);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_RST);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_SHUTDOWN);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_RW);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_CREDIT_UPDATE);
TRACE_DEFINE_ENUM(VIRTIO_VSOCK_OP_CREDIT_REQUEST);

#define show_op(val) \
	__print_symbolic(val, \
			 { VIRTIO_VSOCK_OP_INVALID, "INVALID" }, \
			 { VIRTIO_VSOCK_OP_REQUEST, "REQUEST" }, \
			 { VIRTIO_VSOCK_OP_RESPONSE, "RESPONSE" }, \
			 { VIRTIO_VSOCK_OP_RST, "RST" }, \
			 { VIRTIO_VSOCK_OP_SHUTDOWN, "SHUTDOWN" }, \
			 { VIRTIO_VSOCK_OP_RW, "RW" }, \
			 { VIRTIO_VSOCK_OP_CREDIT_UPDATE, "CREDIT_UPDATE" }, \
			 { VIRTIO_VSOCK_OP_CREDIT_REQUEST, "CREDIT_REQUEST" })

TRACE_EVENT(virtio_transport_alloc_pkt,
	TP_PROTO(
		 __u32 src_cid, __u32 src_port,
		 __u32 dst_cid, __u32 dst_port,
		 __u32 len,
		 __u16 type,
		 __u16 op,
		 __u32 flags
	),
	TP_ARGS(
		src_cid, src_port,
		dst_cid, dst_port,
		len,
		type,
		op,
		flags
	),
	TP_STRUCT__entry(
		__field(__u32, src_cid)
		__field(__u32, src_port)
		__field(__u32, dst_cid)
		__field(__u32, dst_port)
		__field(__u32, len)
		__field(__u16, type)
		__field(__u16, op)
		__field(__u32, flags)
	),
	TP_fast_assign(
		__entry->src_cid = src_cid;
		__entry->src_port = src_port;
		__entry->dst_cid = dst_cid;
		__entry->dst_port = dst_port;
		__entry->len = len;
		__entry->type = type;
		__entry->op = op;
		__entry->flags = flags;
	),
	TP_printk("%u:%u -> %u:%u len=%u type=%s op=%s flags=%#x",
		  __entry->src_cid, __entry->src_port,
		  __entry->dst_cid, __entry->dst_port,
		  __entry->len,
		  show_type(__entry->type),
		  show_op(__entry->op),
		  __entry->flags)
);

TRACE_EVENT(virtio_transport_recv_pkt,
	TP_PROTO(
		 __u32 src_cid, __u32 src_port,
		 __u32 dst_cid, __u32 dst_port,
		 __u32 len,
		 __u16 type,
		 __u16 op,
		 __u32 flags,
		 __u32 buf_alloc,
		 __u32 fwd_cnt
	),
	TP_ARGS(
		src_cid, src_port,
		dst_cid, dst_port,
		len,
		type,
		op,
		flags,
		buf_alloc,
		fwd_cnt
	),
	TP_STRUCT__entry(
		__field(__u32, src_cid)
		__field(__u32, src_port)
		__field(__u32, dst_cid)
		__field(__u32, dst_port)
		__field(__u32, len)
		__field(__u16, type)
		__field(__u16, op)
		__field(__u32, flags)
		__field(__u32, buf_alloc)
		__field(__u32, fwd_cnt)
	),
	TP_fast_assign(
		__entry->src_cid = src_cid;
		__entry->src_port = src_port;
		__entry->dst_cid = dst_cid;
		__entry->dst_port = dst_port;
		__entry->len = len;
		__entry->type = type;
		__entry->op = op;
		__entry->flags = flags;
		__entry->buf_alloc = buf_alloc;
		__entry->fwd_cnt = fwd_cnt;
	),
	TP_printk("%u:%u -> %u:%u len=%u type=%s op=%s flags=%#x "
		  "buf_alloc=%u fwd_cnt=%u",
		  __entry->src_cid, __entry->src_port,
		  __entry->dst_cid, __entry->dst_port,
		  __entry->len,
		  show_type(__entry->type),
		  show_op(__entry->op),
		  __entry->flags,
		  __entry->buf_alloc,
		  __entry->fwd_cnt)
);

#endif /* _TRACE_VSOCK_VIRTIO_TRANSPORT_COMMON_H */

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vsock_virtio_transport_common

/* This part must be outside protection */
#include <trace/define_trace.h>
