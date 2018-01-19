/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hswadsp

#if !defined(_TRACE_HSWADSP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HSWADSP_H

#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/tracepoint.h>

struct sst_hsw;
struct sst_hsw_stream;
struct sst_hsw_ipc_stream_free_req;
struct sst_hsw_ipc_volume_req;
struct sst_hsw_ipc_stream_alloc_req;
struct sst_hsw_audio_data_format_ipc;
struct sst_hsw_ipc_stream_info_reply;
struct sst_hsw_ipc_device_config_req;

DECLARE_EVENT_CLASS(sst_irq,

	TP_PROTO(uint32_t status, uint32_t mask),

	TP_ARGS(status, mask),

	TP_STRUCT__entry(
		__field(	unsigned int,	status		)
		__field(	unsigned int,	mask		)
	),

	TP_fast_assign(
		__entry->status = status;
		__entry->mask = mask;
	),

	TP_printk("status 0x%8.8x mask 0x%8.8x",
		(unsigned int)__entry->status, (unsigned int)__entry->mask)
);

DEFINE_EVENT(sst_irq, sst_irq_busy,

	TP_PROTO(unsigned int status, unsigned int mask),

	TP_ARGS(status, mask)

);

DEFINE_EVENT(sst_irq, sst_irq_done,

	TP_PROTO(unsigned int status, unsigned int mask),

	TP_ARGS(status, mask)

);

DECLARE_EVENT_CLASS(ipc,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val),

	TP_STRUCT__entry(
		__string(	name,	name		)
		__field(	unsigned int,	val	)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->val = val;
	),

	TP_printk("%s 0x%8.8x", __get_str(name), (unsigned int)__entry->val)

);

DEFINE_EVENT(ipc, ipc_request,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val)

);

DEFINE_EVENT(ipc, ipc_reply,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val)

);

DEFINE_EVENT(ipc, ipc_pending_reply,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val)

);

DEFINE_EVENT(ipc, ipc_notification,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val)

);

DEFINE_EVENT(ipc, ipc_error,

	TP_PROTO(const char *name, int val),

	TP_ARGS(name, val)

);

DECLARE_EVENT_CLASS(stream_position,

	TP_PROTO(unsigned int id, unsigned int pos),

	TP_ARGS(id, pos),

	TP_STRUCT__entry(
		__field(	unsigned int,	id		)
		__field(	unsigned int,	pos		)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->pos = pos;
	),

	TP_printk("id %d position 0x%x",
		(unsigned int)__entry->id, (unsigned int)__entry->pos)
);

DEFINE_EVENT(stream_position, stream_read_position,

	TP_PROTO(unsigned int id, unsigned int pos),

	TP_ARGS(id, pos)

);

DEFINE_EVENT(stream_position, stream_write_position,

	TP_PROTO(unsigned int id, unsigned int pos),

	TP_ARGS(id, pos)

);

TRACE_EVENT(hsw_stream_buffer,

	TP_PROTO(struct sst_hsw_stream *stream),

	TP_ARGS(stream),

	TP_STRUCT__entry(
		__field(	int,	id	)
		__field(	int,	pt_addr	)
		__field(	int,	num_pages	)
		__field(	int,	ring_size	)
		__field(	int,	ring_offset	)
		__field(	int,	first_pfn	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->pt_addr = stream->request.ringinfo.ring_pt_address;
		__entry->num_pages = stream->request.ringinfo.num_pages;
		__entry->ring_size = stream->request.ringinfo.ring_size;
		__entry->ring_offset = stream->request.ringinfo.ring_offset;
		__entry->first_pfn = stream->request.ringinfo.ring_first_pfn;
	),

	TP_printk("stream %d ring addr 0x%x pages %d size 0x%x offset 0x%x PFN 0x%x",
		(int) __entry->id,  (int)__entry->pt_addr,
		(int)__entry->num_pages, (int)__entry->ring_size,
		(int)__entry->ring_offset, (int)__entry->first_pfn)
);

TRACE_EVENT(hsw_stream_alloc_reply,

	TP_PROTO(struct sst_hsw_stream *stream),

	TP_ARGS(stream),

	TP_STRUCT__entry(
		__field(	int,	id	)
		__field(	int,	stream_id	)
		__field(	int,	mixer_id	)
		__field(	int,	peak0	)
		__field(	int,	peak1	)
		__field(	int,	vol0	)
		__field(	int,	vol1	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->stream_id = stream->reply.stream_hw_id;
		__entry->mixer_id = stream->reply.mixer_hw_id;
		__entry->peak0 = stream->reply.peak_meter_register_address[0];
		__entry->peak1 = stream->reply.peak_meter_register_address[1];
		__entry->vol0 = stream->reply.volume_register_address[0];
		__entry->vol1 = stream->reply.volume_register_address[1];
	),

	TP_printk("stream %d hw id %d mixer %d peak 0x%x:0x%x vol 0x%x,0x%x",
		(int) __entry->id, (int) __entry->stream_id, (int)__entry->mixer_id,
		(int)__entry->peak0, (int)__entry->peak1,
		(int)__entry->vol0, (int)__entry->vol1)
);

TRACE_EVENT(hsw_mixer_info_reply,

	TP_PROTO(struct sst_hsw_ipc_stream_info_reply *reply),

	TP_ARGS(reply),

	TP_STRUCT__entry(
		__field(	int,	mixer_id	)
		__field(	int,	peak0	)
		__field(	int,	peak1	)
		__field(	int,	vol0	)
		__field(	int,	vol1	)
	),

	TP_fast_assign(
		__entry->mixer_id = reply->mixer_hw_id;
		__entry->peak0 = reply->peak_meter_register_address[0];
		__entry->peak1 = reply->peak_meter_register_address[1];
		__entry->vol0 = reply->volume_register_address[0];
		__entry->vol1 = reply->volume_register_address[1];
	),

	TP_printk("mixer id %d peak 0x%x:0x%x vol 0x%x,0x%x",
		(int)__entry->mixer_id,
		(int)__entry->peak0, (int)__entry->peak1,
		(int)__entry->vol0, (int)__entry->vol1)
);

TRACE_EVENT(hsw_stream_data_format,

	TP_PROTO(struct sst_hsw_stream *stream,
		struct sst_hsw_audio_data_format_ipc *req),

	TP_ARGS(stream, req),

	TP_STRUCT__entry(
		__field(	uint32_t,	id	)
		__field(	uint32_t,	frequency	)
		__field(	uint32_t,	bitdepth	)
		__field(	uint32_t,	map	)
		__field(	uint32_t,	config	)
		__field(	uint32_t,	style	)
		__field(	uint8_t,	ch_num	)
		__field(	uint8_t,	valid_bit	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->frequency = req->frequency;
		__entry->bitdepth = req->bitdepth;
		__entry->map = req->map;
		__entry->config = req->config;
		__entry->style = req->style;
		__entry->ch_num = req->ch_num;
		__entry->valid_bit = req->valid_bit;
	),

	TP_printk("stream %d freq %d depth %d map 0x%x config 0x%x style 0x%x ch %d bits %d",
		(int) __entry->id, (uint32_t)__entry->frequency,
		(uint32_t)__entry->bitdepth, (uint32_t)__entry->map,
		(uint32_t)__entry->config, (uint32_t)__entry->style,
		(uint8_t)__entry->ch_num, (uint8_t)__entry->valid_bit)
);

TRACE_EVENT(hsw_stream_alloc_request,

	TP_PROTO(struct sst_hsw_stream *stream,
		struct sst_hsw_ipc_stream_alloc_req *req),

	TP_ARGS(stream, req),

	TP_STRUCT__entry(
		__field(	uint32_t,	id	)
		__field(	uint8_t,	path_id	)
		__field(	uint8_t,	stream_type	)
		__field(	uint8_t,	format_id	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->path_id = req->path_id;
		__entry->stream_type = req->stream_type;
		__entry->format_id = req->format_id;
	),

	TP_printk("stream %d path %d type %d format %d",
		(int) __entry->id, (uint8_t)__entry->path_id,
		(uint8_t)__entry->stream_type, (uint8_t)__entry->format_id)
);

TRACE_EVENT(hsw_stream_free_req,

	TP_PROTO(struct sst_hsw_stream *stream,
		struct sst_hsw_ipc_stream_free_req *req),

	TP_ARGS(stream, req),

	TP_STRUCT__entry(
		__field(	int,	id	)
		__field(	int,	stream_id	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->stream_id = req->stream_id;
	),

	TP_printk("stream %d hw id %d",
		(int) __entry->id, (int) __entry->stream_id)
);

TRACE_EVENT(hsw_volume_req,

	TP_PROTO(struct sst_hsw_stream *stream,
		struct sst_hsw_ipc_volume_req *req),

	TP_ARGS(stream, req),

	TP_STRUCT__entry(
		__field(	int,	id	)
		__field(	uint32_t,	channel	)
		__field(	uint32_t,	target_volume	)
		__field(	uint64_t,	curve_duration	)
		__field(	uint32_t,	curve_type	)
	),

	TP_fast_assign(
		__entry->id = stream->host_id;
		__entry->channel = req->channel;
		__entry->target_volume = req->target_volume;
		__entry->curve_duration = req->curve_duration;
		__entry->curve_type = req->curve_type;
	),

	TP_printk("stream %d chan 0x%x vol %d duration %llu type %d",
		(int) __entry->id, (uint32_t) __entry->channel,
		(uint32_t)__entry->target_volume,
		(uint64_t)__entry->curve_duration,
		(uint32_t)__entry->curve_type)
);

TRACE_EVENT(hsw_device_config_req,

	TP_PROTO(struct sst_hsw_ipc_device_config_req *req),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(	uint32_t,	ssp	)
		__field(	uint32_t,	clock_freq	)
		__field(	uint32_t,	mode	)
		__field(	uint16_t,	clock_divider	)
	),

	TP_fast_assign(
		__entry->ssp = req->ssp_interface;
		__entry->clock_freq = req->clock_frequency;
		__entry->mode = req->mode;
		__entry->clock_divider = req->clock_divider;
	),

	TP_printk("SSP %d Freq %d mode %d div %d",
		(uint32_t)__entry->ssp,
		(uint32_t)__entry->clock_freq, (uint32_t)__entry->mode,
		(uint32_t)__entry->clock_divider)
);

#endif /* _TRACE_HSWADSP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
