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

TRACE_EVENT(in_packet,
	TP_PROTO(const struct amdtp_stream *s, u32 cycles, u32 *cip_header, unsigned int payload_length, unsigned int index),
	TP_ARGS(s, cycles, cip_header, payload_length, index),
	TP_STRUCT__entry(
		__field(unsigned int, second)
		__field(unsigned int, cycle)
		__field(int, channel)
		__field(int, src)
		__field(int, dest)
		__field(u32, cip_header0)
		__field(u32, cip_header1)
		__field(unsigned int, payload_quadlets)
		__field(unsigned int, packet_index)
		__field(unsigned int, irq)
		__field(unsigned int, index)
	),
	TP_fast_assign(
		__entry->second = cycles / CYCLES_PER_SECOND;
		__entry->cycle = cycles % CYCLES_PER_SECOND;
		__entry->channel = s->context->channel;
		__entry->src = fw_parent_device(s->unit)->node_id;
		__entry->dest = fw_parent_device(s->unit)->card->node_id;
		__entry->cip_header0 = cip_header[0];
		__entry->cip_header1 = cip_header[1];
		__entry->payload_quadlets = payload_length / 4;
		__entry->packet_index = s->packet_index;
		__entry->irq = !!in_interrupt();
		__entry->index = index;
	),
	TP_printk(
		"%02u %04u %04x %04x %02d %08x %08x %03u %02u %01u %02u",
		__entry->second,
		__entry->cycle,
		__entry->src,
		__entry->dest,
		__entry->channel,
		__entry->cip_header0,
		__entry->cip_header1,
		__entry->payload_quadlets,
		__entry->packet_index,
		__entry->irq,
		__entry->index)
);

TRACE_EVENT(out_packet,
	TP_PROTO(const struct amdtp_stream *s, u32 cycles, __be32 *cip_header, unsigned int payload_length, unsigned int index),
	TP_ARGS(s, cycles, cip_header, payload_length, index),
	TP_STRUCT__entry(
		__field(unsigned int, second)
		__field(unsigned int, cycle)
		__field(int, channel)
		__field(int, src)
		__field(int, dest)
		__field(u32, cip_header0)
		__field(u32, cip_header1)
		__field(unsigned int, payload_quadlets)
		__field(unsigned int, packet_index)
		__field(unsigned int, irq)
		__field(unsigned int, index)
	),
	TP_fast_assign(
		__entry->second = cycles / CYCLES_PER_SECOND;
		__entry->cycle = cycles % CYCLES_PER_SECOND;
		__entry->channel = s->context->channel;
		__entry->src = fw_parent_device(s->unit)->card->node_id;
		__entry->dest = fw_parent_device(s->unit)->node_id;
		__entry->cip_header0 = be32_to_cpu(cip_header[0]);
		__entry->cip_header1 = be32_to_cpu(cip_header[1]);
		__entry->payload_quadlets = payload_length / 4;
		__entry->packet_index = s->packet_index;
		__entry->irq = !!in_interrupt();
		__entry->index = index;
	),
	TP_printk(
		"%02u %04u %04x %04x %02d %08x %08x %03u %02u %01u %02u",
		__entry->second,
		__entry->cycle,
		__entry->src,
		__entry->dest,
		__entry->channel,
		__entry->cip_header0,
		__entry->cip_header1,
		__entry->payload_quadlets,
		__entry->packet_index,
		__entry->irq,
		__entry->index)
);

TRACE_EVENT(in_packet_without_header,
	TP_PROTO(const struct amdtp_stream *s, u32 cycles, unsigned int payload_quadlets, unsigned int data_blocks, unsigned int index),
	TP_ARGS(s, cycles, payload_quadlets, data_blocks, index),
	TP_STRUCT__entry(
		__field(unsigned int, second)
		__field(unsigned int, cycle)
		__field(int, channel)
		__field(int, src)
		__field(int, dest)
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
		__entry->src = fw_parent_device(s->unit)->node_id;
		__entry->dest = fw_parent_device(s->unit)->card->node_id;
		__entry->payload_quadlets = payload_quadlets;
		__entry->data_blocks = data_blocks,
		__entry->data_block_counter = s->data_block_counter,
		__entry->packet_index = s->packet_index;
		__entry->irq = !!in_interrupt();
		__entry->index = index;
	),
	TP_printk(
		"%02u %04u %04x %04x %02d %03u %02u %03u %02u %01u %02u",
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
		__entry->index)
);

TRACE_EVENT(out_packet_without_header,
	TP_PROTO(const struct amdtp_stream *s, u32 cycles, unsigned int payload_length, unsigned int data_blocks, unsigned int index),
	TP_ARGS(s, cycles, payload_length, data_blocks, index),
	TP_STRUCT__entry(
		__field(unsigned int, second)
		__field(unsigned int, cycle)
		__field(int, channel)
		__field(int, src)
		__field(int, dest)
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
		__entry->src = fw_parent_device(s->unit)->card->node_id;
		__entry->dest = fw_parent_device(s->unit)->node_id;
		__entry->payload_quadlets = payload_length / 4;
		__entry->data_blocks = data_blocks,
		__entry->data_block_counter = s->data_block_counter,
		__entry->packet_index = s->packet_index;
		__entry->irq = !!in_interrupt();
		__entry->index = index;
	),
	TP_printk(
		"%02u %04u %04x %04x %02d %03u %02u %03u %02u %01u %02u",
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
		__entry->index)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH	.
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	amdtp-stream-trace
#include <trace/define_trace.h>
