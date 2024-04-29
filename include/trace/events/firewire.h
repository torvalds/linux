// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#define TRACE_SYSTEM	firewire

#if !defined(_FIREWIRE_TRACE_EVENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _FIREWIRE_TRACE_EVENT_H

#include <linux/tracepoint.h>
#include <linux/firewire.h>

#include <linux/firewire-constants.h>

#include "../../../drivers/firewire/packet-header-definitions.h"

// The content of TP_printk field is preprocessed, then put to the module binary.
#define ASYNC_HEADER_GET_DESTINATION(header)	\
	(((header)[0] & ASYNC_HEADER_Q0_DESTINATION_MASK) >> ASYNC_HEADER_Q0_DESTINATION_SHIFT)

#define ASYNC_HEADER_GET_TLABEL(header)	\
	(((header)[0] & ASYNC_HEADER_Q0_TLABEL_MASK) >> ASYNC_HEADER_Q0_TLABEL_SHIFT)

#define ASYNC_HEADER_GET_TCODE(header)	\
	(((header)[0] & ASYNC_HEADER_Q0_TCODE_MASK) >> ASYNC_HEADER_Q0_TCODE_SHIFT)

#define ASYNC_HEADER_GET_SOURCE(header)	\
	(((header)[1] & ASYNC_HEADER_Q1_SOURCE_MASK) >> ASYNC_HEADER_Q1_SOURCE_SHIFT)

#define ASYNC_HEADER_GET_OFFSET(header)	\
	((((unsigned long long)((header)[1] & ASYNC_HEADER_Q1_OFFSET_HIGH_MASK)) >> ASYNC_HEADER_Q1_OFFSET_HIGH_SHIFT) << 32)| \
	(header)[2]

#define ASYNC_HEADER_GET_RCODE(header)	\
	(((header)[1] & ASYNC_HEADER_Q1_RCODE_MASK) >> ASYNC_HEADER_Q1_RCODE_SHIFT)

#define QUADLET_SIZE	4

DECLARE_EVENT_CLASS(async_outbound_initiate_template,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, generation, scode, header, data, data_count),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, generation)
		__field(u8, scode)
		__array(u32, header, ASYNC_HEADER_QUADLET_COUNT)
		__dynamic_array(u32, data, data_count)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->generation = generation;
		__entry->scode = scode;
		memcpy(__entry->header, header, QUADLET_SIZE * ASYNC_HEADER_QUADLET_COUNT);
		memcpy(__get_dynamic_array(data), data, __get_dynamic_array_len(data));
	),
	// This format is for the request subaction.
	TP_printk(
		"transaction=0x%llx generation=%u scode=%u dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x offset=0x%012llx header=%s data=%s",
		__entry->transaction,
		__entry->generation,
		__entry->scode,
		ASYNC_HEADER_GET_DESTINATION(__entry->header),
		ASYNC_HEADER_GET_TLABEL(__entry->header),
		ASYNC_HEADER_GET_TCODE(__entry->header),
		ASYNC_HEADER_GET_SOURCE(__entry->header),
		ASYNC_HEADER_GET_OFFSET(__entry->header),
		__print_array(__entry->header, ASYNC_HEADER_QUADLET_COUNT, QUADLET_SIZE),
		__print_array(__get_dynamic_array(data),
			      __get_dynamic_array_len(data) / QUADLET_SIZE, QUADLET_SIZE)
	)
);

// The value of status is one of ack codes and rcodes specific to Linux FireWire subsystem.
DECLARE_EVENT_CLASS(async_outbound_complete_template,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp),
	TP_ARGS(transaction, generation, scode, status, timestamp),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, generation)
		__field(u8, scode)
		__field(u8, status)
		__field(u16, timestamp)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->generation = generation;
		__entry->scode = scode;
		__entry->status = status;
		__entry->timestamp = timestamp;
	),
	TP_printk(
		"transaction=0x%llx generation=%u scode=%u status=%u timestamp=0x%04x",
		__entry->transaction,
		__entry->generation,
		__entry->scode,
		__entry->status,
		__entry->timestamp
	)
);

// The value of status is one of ack codes and rcodes specific to Linux FireWire subsystem.
DECLARE_EVENT_CLASS(async_inbound_template,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, generation, scode, status, timestamp, header, data, data_count),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, generation)
		__field(u8, scode)
		__field(u8, status)
		__field(u8, timestamp)
		__array(u32, header, ASYNC_HEADER_QUADLET_COUNT)
		__dynamic_array(u32, data, data_count)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->generation = generation;
		__entry->scode = scode;
		__entry->status = status;
		__entry->timestamp = timestamp;
		memcpy(__entry->header, header, QUADLET_SIZE * ASYNC_HEADER_QUADLET_COUNT);
		memcpy(__get_dynamic_array(data), data, __get_dynamic_array_len(data));
	),
	// This format is for the response subaction.
	TP_printk(
		"transaction=0x%llx generation=%u scode=%u status=%u timestamp=0x%04x dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x rcode=%u header=%s data=%s",
		__entry->transaction,
		__entry->generation,
		__entry->scode,
		__entry->status,
		__entry->timestamp,
		ASYNC_HEADER_GET_DESTINATION(__entry->header),
		ASYNC_HEADER_GET_TLABEL(__entry->header),
		ASYNC_HEADER_GET_TCODE(__entry->header),
		ASYNC_HEADER_GET_SOURCE(__entry->header),
		ASYNC_HEADER_GET_RCODE(__entry->header),
		__print_array(__entry->header, ASYNC_HEADER_QUADLET_COUNT, QUADLET_SIZE),
		__print_array(__get_dynamic_array(data),
			      __get_dynamic_array_len(data) / QUADLET_SIZE, QUADLET_SIZE)
	)
);

DEFINE_EVENT(async_outbound_initiate_template, async_request_outbound_initiate,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, generation, scode, header, data, data_count)
);

DEFINE_EVENT(async_outbound_complete_template, async_request_outbound_complete,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp),
	TP_ARGS(transaction, generation, scode, status, timestamp)
);

DEFINE_EVENT(async_inbound_template, async_response_inbound,
	TP_PROTO(u64 transaction, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, generation, scode, status, timestamp, header, data, data_count)
);

#undef ASYNC_HEADER_GET_DESTINATION
#undef ASYNC_HEADER_GET_TLABEL
#undef ASYNC_HEADER_GET_TCODE
#undef ASYNC_HEADER_GET_SOURCE
#undef ASYNC_HEADER_GET_OFFSET
#undef ASYNC_HEADER_GET_RCODE
#undef QUADLET_SIZE

#endif // _FIREWIRE_TRACE_EVENT_H

#include <trace/define_trace.h>
