/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scmi

#if !defined(_TRACE_SCMI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SCMI_H

#include <linux/tracepoint.h>

TRACE_EVENT(scmi_fc_call,
	TP_PROTO(u8 protocol_id, u8 msg_id, u32 res_id, u32 val1, u32 val2),
	TP_ARGS(protocol_id, msg_id, res_id, val1, val2),

	TP_STRUCT__entry(
		__field(u8, protocol_id)
		__field(u8, msg_id)
		__field(u32, res_id)
		__field(u32, val1)
		__field(u32, val2)
	),

	TP_fast_assign(
		__entry->protocol_id = protocol_id;
		__entry->msg_id = msg_id;
		__entry->res_id = res_id;
		__entry->val1 = val1;
		__entry->val2 = val2;
	),

	TP_printk("pt=%02X msg_id=%02X res_id:%u vals=%u:%u",
		__entry->protocol_id, __entry->msg_id,
		__entry->res_id, __entry->val1, __entry->val2)
);

TRACE_EVENT(scmi_xfer_begin,
	TP_PROTO(int transfer_id, u8 msg_id, u8 protocol_id, u16 seq,
		 bool poll),
	TP_ARGS(transfer_id, msg_id, protocol_id, seq, poll),

	TP_STRUCT__entry(
		__field(int, transfer_id)
		__field(u8, msg_id)
		__field(u8, protocol_id)
		__field(u16, seq)
		__field(bool, poll)
	),

	TP_fast_assign(
		__entry->transfer_id = transfer_id;
		__entry->msg_id = msg_id;
		__entry->protocol_id = protocol_id;
		__entry->seq = seq;
		__entry->poll = poll;
	),

	TP_printk("pt=%02X msg_id=%02X seq=%04X transfer_id=%X poll=%u",
		__entry->protocol_id, __entry->msg_id, __entry->seq,
		__entry->transfer_id, __entry->poll)
);

TRACE_EVENT(scmi_xfer_response_wait,
	TP_PROTO(int transfer_id, u8 msg_id, u8 protocol_id, u16 seq,
		 u32 timeout, bool poll),
	TP_ARGS(transfer_id, msg_id, protocol_id, seq, timeout, poll),

	TP_STRUCT__entry(
		__field(int, transfer_id)
		__field(u8, msg_id)
		__field(u8, protocol_id)
		__field(u16, seq)
		__field(u32, timeout)
		__field(bool, poll)
	),

	TP_fast_assign(
		__entry->transfer_id = transfer_id;
		__entry->msg_id = msg_id;
		__entry->protocol_id = protocol_id;
		__entry->seq = seq;
		__entry->timeout = timeout;
		__entry->poll = poll;
	),

	TP_printk("pt=%02X msg_id=%02X seq=%04X transfer_id=%X tmo_ms=%u poll=%u",
		__entry->protocol_id, __entry->msg_id, __entry->seq,
		__entry->transfer_id, __entry->timeout, __entry->poll)
);

TRACE_EVENT(scmi_xfer_end,
	TP_PROTO(int transfer_id, u8 msg_id, u8 protocol_id, u16 seq,
		 int status),
	TP_ARGS(transfer_id, msg_id, protocol_id, seq, status),

	TP_STRUCT__entry(
		__field(int, transfer_id)
		__field(u8, msg_id)
		__field(u8, protocol_id)
		__field(u16, seq)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->transfer_id = transfer_id;
		__entry->msg_id = msg_id;
		__entry->protocol_id = protocol_id;
		__entry->seq = seq;
		__entry->status = status;
	),

	TP_printk("pt=%02X msg_id=%02X seq=%04X transfer_id=%X s=%d",
		__entry->protocol_id, __entry->msg_id, __entry->seq,
		__entry->transfer_id, __entry->status)
);

TRACE_EVENT(scmi_rx_done,
	TP_PROTO(int transfer_id, u8 msg_id, u8 protocol_id, u16 seq,
		 u8 msg_type),
	TP_ARGS(transfer_id, msg_id, protocol_id, seq, msg_type),

	TP_STRUCT__entry(
		__field(int, transfer_id)
		__field(u8, msg_id)
		__field(u8, protocol_id)
		__field(u16, seq)
		__field(u8, msg_type)
	),

	TP_fast_assign(
		__entry->transfer_id = transfer_id;
		__entry->msg_id = msg_id;
		__entry->protocol_id = protocol_id;
		__entry->seq = seq;
		__entry->msg_type = msg_type;
	),

	TP_printk("pt=%02X msg_id=%02X seq=%04X transfer_id=%X msg_type=%u",
		__entry->protocol_id, __entry->msg_id, __entry->seq,
		__entry->transfer_id, __entry->msg_type)
);

TRACE_EVENT(scmi_msg_dump,
	TP_PROTO(int id, u8 channel_id, u8 protocol_id, u8 msg_id,
		 unsigned char *tag, u16 seq, int status,
		 void *buf, size_t len),
	TP_ARGS(id, channel_id, protocol_id, msg_id, tag, seq, status,
		buf, len),

	TP_STRUCT__entry(
		__field(int, id)
		__field(u8, channel_id)
		__field(u8, protocol_id)
		__field(u8, msg_id)
		__array(char, tag, 5)
		__field(u16, seq)
		__field(int, status)
		__field(size_t, len)
		__dynamic_array(unsigned char, cmd, len)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->channel_id = channel_id;
		__entry->protocol_id = protocol_id;
		__entry->msg_id = msg_id;
		strscpy(__entry->tag, tag, 5);
		__entry->seq = seq;
		__entry->status = status;
		__entry->len = len;
		memcpy(__get_dynamic_array(cmd), buf, __entry->len);
	),

	TP_printk("id=%d ch=%02X pt=%02X t=%s msg_id=%02X seq=%04X s=%d pyld=%s",
		  __entry->id, __entry->channel_id, __entry->protocol_id,
		  __entry->tag, __entry->msg_id, __entry->seq, __entry->status,
		__print_hex_str(__get_dynamic_array(cmd), __entry->len))
);
#endif /* _TRACE_SCMI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
