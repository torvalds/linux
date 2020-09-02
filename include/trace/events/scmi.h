/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scmi

#if !defined(_TRACE_SCMI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SCMI_H

#include <linux/tracepoint.h>

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

	TP_printk("transfer_id=%d msg_id=%u protocol_id=%u seq=%u poll=%u",
		__entry->transfer_id, __entry->msg_id, __entry->protocol_id,
		__entry->seq, __entry->poll)
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

	TP_printk("transfer_id=%d msg_id=%u protocol_id=%u seq=%u status=%d",
		__entry->transfer_id, __entry->msg_id, __entry->protocol_id,
		__entry->seq, __entry->status)
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

	TP_printk("transfer_id=%d msg_id=%u protocol_id=%u seq=%u msg_type=%u",
		__entry->transfer_id, __entry->msg_id, __entry->protocol_id,
		__entry->seq, __entry->msg_type)
);
#endif /* _TRACE_SCMI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
