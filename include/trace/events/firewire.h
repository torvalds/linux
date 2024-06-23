// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#define TRACE_SYSTEM	firewire

#if !defined(_FIREWIRE_TRACE_EVENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _FIREWIRE_TRACE_EVENT_H

#include <linux/tracepoint.h>
#include <linux/firewire.h>

#include <linux/firewire-constants.h>

// Some macros are defined in 'drivers/firewire/packet-header-definitions.h'.

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
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, header, data, data_count),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, scode)
		__array(u32, header, ASYNC_HEADER_QUADLET_COUNT)
		__dynamic_array(u32, data, data_count)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->scode = scode;
		memcpy(__entry->header, header, QUADLET_SIZE * ASYNC_HEADER_QUADLET_COUNT);
		memcpy(__get_dynamic_array(data), data, __get_dynamic_array_len(data));
	),
	// This format is for the request subaction.
	TP_printk(
		"transaction=0x%llx card_index=%u generation=%u scode=%u dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x offset=0x%012llx header=%s data=%s",
		__entry->transaction,
		__entry->card_index,
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
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, scode)
		__field(u8, status)
		__field(u16, timestamp)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->scode = scode;
		__entry->status = status;
		__entry->timestamp = timestamp;
	),
	TP_printk(
		"transaction=0x%llx card_index=%u generation=%u scode=%u status=%u timestamp=0x%04x",
		__entry->transaction,
		__entry->card_index,
		__entry->generation,
		__entry->scode,
		__entry->status,
		__entry->timestamp
	)
);

// The value of status is one of ack codes and rcodes specific to Linux FireWire subsystem.
DECLARE_EVENT_CLASS(async_inbound_template,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp, header, data, data_count),
	TP_STRUCT__entry(
		__field(u64, transaction)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, scode)
		__field(u8, status)
		__field(u16, timestamp)
		__array(u32, header, ASYNC_HEADER_QUADLET_COUNT)
		__dynamic_array(u32, data, data_count)
	),
	TP_fast_assign(
		__entry->transaction = transaction;
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->scode = scode;
		__entry->status = status;
		__entry->timestamp = timestamp;
		memcpy(__entry->header, header, QUADLET_SIZE * ASYNC_HEADER_QUADLET_COUNT);
		memcpy(__get_dynamic_array(data), data, __get_dynamic_array_len(data));
	),
	// This format is for the response subaction.
	TP_printk(
		"transaction=0x%llx card_index=%u generation=%u scode=%u status=%u timestamp=0x%04x dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x rcode=%u header=%s data=%s",
		__entry->transaction,
		__entry->card_index,
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
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, header, data, data_count)
);

DEFINE_EVENT(async_outbound_complete_template, async_request_outbound_complete,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp)
);

DEFINE_EVENT(async_inbound_template, async_response_inbound,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp, header, data, data_count)
);

DEFINE_EVENT_PRINT(async_inbound_template, async_request_inbound,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp, header, data, data_count),
	TP_printk(
		"transaction=0x%llx card_index=%u generation=%u scode=%u status=%u timestamp=0x%04x dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x offset=0x%012llx header=%s data=%s",
		__entry->transaction,
		__entry->card_index,
		__entry->generation,
		__entry->scode,
		__entry->status,
		__entry->timestamp,
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

DEFINE_EVENT_PRINT(async_outbound_initiate_template, async_response_outbound_initiate,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, const u32 *header, const u32 *data, unsigned int data_count),
	TP_ARGS(transaction, card_index, generation, scode, header, data, data_count),
	TP_printk(
		"transaction=0x%llx card_index=%u generation=%u scode=%u dst_id=0x%04x tlabel=%u tcode=%u src_id=0x%04x rcode=%u header=%s data=%s",
		__entry->transaction,
		__entry->card_index,
		__entry->generation,
		__entry->scode,
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

DEFINE_EVENT(async_outbound_complete_template, async_response_outbound_complete,
	TP_PROTO(u64 transaction, unsigned int card_index, unsigned int generation, unsigned int scode, unsigned int status, unsigned int timestamp),
	TP_ARGS(transaction, card_index, generation, scode, status, timestamp)
);

#undef ASYNC_HEADER_GET_DESTINATION
#undef ASYNC_HEADER_GET_TLABEL
#undef ASYNC_HEADER_GET_TCODE
#undef ASYNC_HEADER_GET_SOURCE
#undef ASYNC_HEADER_GET_OFFSET
#undef ASYNC_HEADER_GET_RCODE

TRACE_EVENT(async_phy_outbound_initiate,
	TP_PROTO(u64 packet, unsigned int card_index, unsigned int generation, u32 first_quadlet, u32 second_quadlet),
	TP_ARGS(packet, card_index, generation, first_quadlet, second_quadlet),
	TP_STRUCT__entry(
		__field(u64, packet)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u32, first_quadlet)
		__field(u32, second_quadlet)
	),
	TP_fast_assign(
		__entry->packet = packet;
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->first_quadlet = first_quadlet;
		__entry->second_quadlet = second_quadlet
	),
	TP_printk(
		"packet=0x%llx card_index=%u generation=%u first_quadlet=0x%08x second_quadlet=0x%08x",
		__entry->packet,
		__entry->card_index,
		__entry->generation,
		__entry->first_quadlet,
		__entry->second_quadlet
	)
);

TRACE_EVENT(async_phy_outbound_complete,
	TP_PROTO(u64 packet, unsigned int card_index, unsigned int generation, unsigned int status, unsigned int timestamp),
	TP_ARGS(packet, card_index, generation, status, timestamp),
	TP_STRUCT__entry(
		__field(u64, packet)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, status)
		__field(u16, timestamp)
	),
	TP_fast_assign(
		__entry->packet = packet;
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->status = status;
		__entry->timestamp = timestamp;
	),
	TP_printk(
		"packet=0x%llx card_index=%u generation=%u status=%u timestamp=0x%04x",
		__entry->packet,
		__entry->card_index,
		__entry->generation,
		__entry->status,
		__entry->timestamp
	)
);

TRACE_EVENT(async_phy_inbound,
	TP_PROTO(u64 packet, unsigned int card_index, unsigned int generation, unsigned int status, unsigned int timestamp, u32 first_quadlet, u32 second_quadlet),
	TP_ARGS(packet, card_index, generation, status, timestamp, first_quadlet, second_quadlet),
	TP_STRUCT__entry(
		__field(u64, packet)
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, status)
		__field(u16, timestamp)
		__field(u32, first_quadlet)
		__field(u32, second_quadlet)
	),
	TP_fast_assign(
		__entry->packet = packet;
		__entry->generation = generation;
		__entry->status = status;
		__entry->timestamp = timestamp;
		__entry->first_quadlet = first_quadlet;
		__entry->second_quadlet = second_quadlet
	),
	TP_printk(
		"packet=0x%llx card_index=%u generation=%u status=%u timestamp=0x%04x first_quadlet=0x%08x second_quadlet=0x%08x",
		__entry->packet,
		__entry->card_index,
		__entry->generation,
		__entry->status,
		__entry->timestamp,
		__entry->first_quadlet,
		__entry->second_quadlet
	)
);

DECLARE_EVENT_CLASS(bus_reset_arrange_template,
	TP_PROTO(unsigned int card_index, unsigned int generation, bool short_reset),
	TP_ARGS(card_index, generation, short_reset),
	TP_STRUCT__entry(
		__field(u8, card_index)
		__field(u8, generation)
		__field(bool, short_reset)
	),
	TP_fast_assign(
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->short_reset = short_reset;
	),
	TP_printk(
		"card_index=%u generation=%u short_reset=%s",
		__entry->card_index,
		__entry->generation,
		__entry->short_reset ? "true" : "false"
	)
);

DEFINE_EVENT(bus_reset_arrange_template, bus_reset_initiate,
	TP_PROTO(unsigned int card_index, unsigned int generation, bool short_reset),
	TP_ARGS(card_index, generation, short_reset)
);

DEFINE_EVENT(bus_reset_arrange_template, bus_reset_schedule,
	TP_PROTO(unsigned int card_index, unsigned int generation, bool short_reset),
	TP_ARGS(card_index, generation, short_reset)
);

DEFINE_EVENT(bus_reset_arrange_template, bus_reset_postpone,
	TP_PROTO(unsigned int card_index, unsigned int generation, bool short_reset),
	TP_ARGS(card_index, generation, short_reset)
);

TRACE_EVENT(bus_reset_handle,
	TP_PROTO(unsigned int card_index, unsigned int generation, unsigned int node_id, bool bm_abdicate, u32 *self_ids, unsigned int self_id_count),
	TP_ARGS(card_index, generation, node_id, bm_abdicate, self_ids, self_id_count),
	TP_STRUCT__entry(
		__field(u8, card_index)
		__field(u8, generation)
		__field(u8, node_id)
		__field(bool, bm_abdicate)
		__dynamic_array(u32, self_ids, self_id_count)
	),
	TP_fast_assign(
		__entry->card_index = card_index;
		__entry->generation = generation;
		__entry->node_id = node_id;
		__entry->bm_abdicate = bm_abdicate;
		memcpy(__get_dynamic_array(self_ids), self_ids, __get_dynamic_array_len(self_ids));
	),
	TP_printk(
		"card_index=%u generation=%u node_id=0x%04x bm_abdicate=%s self_ids=%s",
		__entry->card_index,
		__entry->generation,
		__entry->node_id,
		__entry->bm_abdicate ? "true" : "false",
		__print_array(__get_dynamic_array(self_ids),
			      __get_dynamic_array_len(self_ids) / QUADLET_SIZE, QUADLET_SIZE)
	)
);

// Some macros are defined in 'drivers/firewire/phy-packet-definitions.h'.

// The content of TP_printk field is preprocessed, then put to the module binary.

#define PHY_PACKET_SELF_ID_GET_PHY_ID(quads)		\
	((((const u32 *)quads)[0] & SELF_ID_PHY_ID_MASK) >> SELF_ID_PHY_ID_SHIFT)

#define PHY_PACKET_SELF_ID_GET_LINK_ACTIVE(quads)	\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_LINK_ACTIVE_MASK) >> SELF_ID_ZERO_LINK_ACTIVE_SHIFT)

#define PHY_PACKET_SELF_ID_GET_GAP_COUNT(quads)		\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_GAP_COUNT_MASK) >> SELF_ID_ZERO_GAP_COUNT_SHIFT)

#define PHY_PACKET_SELF_ID_GET_SCODE(quads)		\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_SCODE_MASK) >> SELF_ID_ZERO_SCODE_SHIFT)

#define PHY_PACKET_SELF_ID_GET_CONTENDER(quads)		\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_CONTENDER_MASK) >> SELF_ID_ZERO_CONTENDER_SHIFT)

#define PHY_PACKET_SELF_ID_GET_POWER_CLASS(quads)	\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_POWER_CLASS_MASK) >> SELF_ID_ZERO_POWER_CLASS_SHIFT)

#define PHY_PACKET_SELF_ID_GET_INITIATED_RESET(quads)	\
	((((const u32 *)quads)[0] & SELF_ID_ZERO_INITIATED_RESET_MASK) >> SELF_ID_ZERO_INITIATED_RESET_SHIFT)

void copy_port_status(u8 *port_status, unsigned int port_capacity, const u32 *self_id_sequence,
		      unsigned int quadlet_count);

TRACE_EVENT(self_id_sequence,
	TP_PROTO(unsigned int card_index, const u32 *self_id_sequence, unsigned int quadlet_count, unsigned int generation),
	TP_ARGS(card_index, self_id_sequence, quadlet_count, generation),
	TP_STRUCT__entry(
		__field(u8, card_index)
		__field(u8, generation)
		__dynamic_array(u8, port_status, self_id_sequence_get_port_capacity(quadlet_count))
		__dynamic_array(u32, self_id_sequence, quadlet_count)
	),
	TP_fast_assign(
		__entry->card_index = card_index;
		__entry->generation = generation;
		copy_port_status(__get_dynamic_array(port_status), __get_dynamic_array_len(port_status),
				 self_id_sequence, quadlet_count);
		memcpy(__get_dynamic_array(self_id_sequence), self_id_sequence,
					   __get_dynamic_array_len(self_id_sequence));
	),
	TP_printk(
		"card_index=%u generation=%u phy_id=0x%02x link_active=%s gap_count=%u scode=%u contender=%s power_class=%u initiated_reset=%s port_status=%s self_id_sequence=%s",
		__entry->card_index,
		__entry->generation,
		PHY_PACKET_SELF_ID_GET_PHY_ID(__get_dynamic_array(self_id_sequence)),
		PHY_PACKET_SELF_ID_GET_LINK_ACTIVE(__get_dynamic_array(self_id_sequence)) ? "true" : "false",
		PHY_PACKET_SELF_ID_GET_GAP_COUNT(__get_dynamic_array(self_id_sequence)),
		PHY_PACKET_SELF_ID_GET_SCODE(__get_dynamic_array(self_id_sequence)),
		PHY_PACKET_SELF_ID_GET_CONTENDER(__get_dynamic_array(self_id_sequence)) ? "true" : "false",
		PHY_PACKET_SELF_ID_GET_POWER_CLASS(__get_dynamic_array(self_id_sequence)),
		PHY_PACKET_SELF_ID_GET_INITIATED_RESET(__get_dynamic_array(self_id_sequence)) ? "true" : "false",
		__print_array(__get_dynamic_array(port_status), __get_dynamic_array_len(port_status), 1),
		__print_array(__get_dynamic_array(self_id_sequence),
			      __get_dynamic_array_len(self_id_sequence) / QUADLET_SIZE, QUADLET_SIZE)
	)
);

#undef PHY_PACKET_SELF_ID_GET_PHY_ID
#undef PHY_PACKET_SELF_ID_GET_LINK_ACTIVE
#undef PHY_PACKET_SELF_ID_GET_GAP_COUNT
#undef PHY_PACKET_SELF_ID_GET_SCODE
#undef PHY_PACKET_SELF_ID_GET_CONTENDER
#undef PHY_PACKET_SELF_ID_GET_POWER_CLASS
#undef PHY_PACKET_SELF_ID_GET_INITIATED_RESET

#undef QUADLET_SIZE

#endif // _FIREWIRE_TRACE_EVENT_H

#include <trace/define_trace.h>
