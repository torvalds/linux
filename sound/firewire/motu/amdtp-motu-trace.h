/*
 * amdtp-motu-trace.h - tracepoint definitions to dump a part of packet data
 *
 * Copyright (c) 2017 Takashi Sakamoto
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM		snd_firewire_motu

#if !defined(_SND_FIREWIRE_MOTU_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SND_FIREWIRE_MOTU_TRACE_H

#include <linux/tracepoint.h>

static void copy_sph(u32 *frame, __be32 *buffer, unsigned int data_blocks,
		     unsigned int data_block_quadlets);
static void copy_message(u64 *frames, __be32 *buffer, unsigned int data_blocks,
			 unsigned int data_block_quadlets);

TRACE_EVENT(data_block_sph,
	TP_PROTO(struct amdtp_stream *s, unsigned int data_blocks, __be32 *buffer),
	TP_ARGS(s, data_blocks, buffer),
	TP_STRUCT__entry(
		__field(int, src)
		__field(int, dst)
		__field(unsigned int, data_blocks)
		__dynamic_array(u32, tstamps, data_blocks)
	),
	TP_fast_assign(
		if (s->direction == AMDTP_IN_STREAM) {
			__entry->src = fw_parent_device(s->unit)->node_id;
			__entry->dst = fw_parent_device(s->unit)->card->node_id;
		} else {
			__entry->src = fw_parent_device(s->unit)->card->node_id;
			__entry->dst = fw_parent_device(s->unit)->node_id;
		}
		__entry->data_blocks = data_blocks;
		copy_sph(__get_dynamic_array(tstamps), buffer, data_blocks, s->data_block_quadlets);
	),
	TP_printk(
		"%04x %04x %u %s",
		__entry->src,
		__entry->dst,
		__entry->data_blocks,
		__print_array(__get_dynamic_array(tstamps), __entry->data_blocks, 4)
	)
);

TRACE_EVENT(data_block_message,
	TP_PROTO(struct amdtp_stream *s, unsigned int data_blocks, __be32 *buffer),
	TP_ARGS(s, data_blocks, buffer),
	TP_STRUCT__entry(
		__field(int, src)
		__field(int, dst)
		__field(unsigned int, data_blocks)
		__dynamic_array(u64, messages, data_blocks)
	),
	TP_fast_assign(
		if (s->direction == AMDTP_IN_STREAM) {
			__entry->src = fw_parent_device(s->unit)->node_id;
			__entry->dst = fw_parent_device(s->unit)->card->node_id;
		} else {
			__entry->src = fw_parent_device(s->unit)->card->node_id;
			__entry->dst = fw_parent_device(s->unit)->node_id;
		}
		__entry->data_blocks = data_blocks;
		copy_message(__get_dynamic_array(messages), buffer, data_blocks, s->data_block_quadlets);
	),
	TP_printk(
		"%04x %04x %u %s",
		__entry->src,
		__entry->dst,
		__entry->data_blocks,
		__print_array(__get_dynamic_array(messages), __entry->data_blocks, 8)
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH	.
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	amdtp-motu-trace
#include <trace/define_trace.h>
