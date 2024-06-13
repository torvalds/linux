/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * amdtp-stream-trace.h - tracepoint definitions to dump a part of packet data
 *
 * Copyright (c) 2016 Takashi Sakamoto
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM		snd_firewire_lib

#if !defined(_AMDTP_STREAM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _AMDTP_STREAM_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(amdtp_packet,
	TP_PROTO(const struct amdtp_stream *s, u32 cycles, const __be32 *cip_header, unsigned int payload_length, unsigned int data_blocks, unsigned int data_block_counter, unsigned int packet_index, unsigned int index),
	TP_ARGS(s, cycles, cip_header, payload_length, data_blocks, data_block_counter, packet_index, index),
	TP_STRUCT__entry(
		__field(unsigned int, second)
		__field(unsigned int, cycle)
		__field(int, channel)
		__field(int, src)
		__field(int, dest)
		__dynamic_array(u8, cip_header, cip_header ? 8 : 0)
		__field(unsigned int, payload_quadlets)
		__field(unsigned int, data_blocks)
		__field(unsigned int, data_block_counter)
		__field(unsigned int, packet_index)
		__field(unsigned int, irq)
		__field(unsigned int, index)
	),
	TP_fast_assign(
		__entry->second = cycles / CYCLES_PER_SECOND;
		__entry->cycle = cycles % CYCLES_PER_SECOND;
		__entry->channel = s->context->channel;
		if (s->direction == AMDTP_IN_STREAM) {
			__entry->src = fw_parent_device(s->unit)->node_id;
			__entry->dest = fw_parent_device(s->unit)->card->node_id;
		} else {
			__entry->src = fw_parent_device(s->unit)->card->node_id;
			__entry->dest = fw_parent_device(s->unit)->node_id;
		}
		if (cip_header) {
			memcpy(__get_dynamic_array(cip_header), cip_header,
			       __get_dynamic_array_len(cip_header));
		}
		__entry->payload_quadlets = payload_length / sizeof(__be32);
		__entry->data_blocks = data_blocks;
		__entry->data_block_counter = data_block_counter,
		__entry->packet_index = packet_index;
		__entry->irq = !!in_softirq();
		__entry->index = index;
	),
	TP_printk(
		"%02u %04u %04x %04x %02d %03u %02u %03u %02u %01u %02u %s",
		__entry->second,
		__entry->cycle,
		__entry->src,
		__entry->dest,
		__entry->channel,
		__entry->payload_quadlets,
		__entry->data_blocks,
		__entry->data_block_counter,
		__entry->packet_index,
		__entry->irq,
		__entry->index,
		__print_array(__get_dynamic_array(cip_header),
			      __get_dynamic_array_len(cip_header), 1))
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH	.
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	amdtp-stream-trace
#include <trace/define_trace.h>
