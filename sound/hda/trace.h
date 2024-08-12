/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda

#if !defined(__HDAC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HDAC_TRACE_H

#include <linux/tracepoint.h>
#include <linux/device.h>
#include <sound/hdaudio.h>

#ifndef HDAC_MSG_MAX
#define HDAC_MSG_MAX	500
#endif

struct hdac_bus;
struct hdac_codec;

TRACE_EVENT(hda_send_cmd,
	TP_PROTO(struct hdac_bus *bus, unsigned int cmd),
	TP_ARGS(bus, cmd),
	TP_STRUCT__entry(
		__string(name, dev_name((bus)->dev))
		__field(u32, cmd)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->cmd = cmd;
	),
	TP_printk("[%s:%d] val=0x%08x", __get_str(name), __entry->cmd >> 28, __entry->cmd)
);

TRACE_EVENT(hda_get_response,
	TP_PROTO(struct hdac_bus *bus, unsigned int addr, unsigned int res),
	TP_ARGS(bus, addr, res),
	TP_STRUCT__entry(
		__string(name, dev_name((bus)->dev))
		__field(u32, addr)
		__field(u32, res)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->addr = addr;
		__entry->res = res;
	),
	TP_printk("[%s:%d] val=0x%08x", __get_str(name), __entry->addr, __entry->res)
);

TRACE_EVENT(hda_unsol_event,
	TP_PROTO(struct hdac_bus *bus, u32 res, u32 res_ex),
	TP_ARGS(bus, res, res_ex),
	TP_STRUCT__entry(
		__string(name, dev_name((bus)->dev))
		__field(u32, res)
		__field(u32, res_ex)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->res = res;
		__entry->res_ex = res_ex;
	),
	TP_printk("[%s:%d] res=0x%08x, res_ex=0x%08x", __get_str(name),
		  __entry->res_ex & 0x0f, __entry->res, __entry->res_ex)
);

DECLARE_EVENT_CLASS(hdac_stream,
	TP_PROTO(struct hdac_bus *bus, struct hdac_stream *azx_dev),

	TP_ARGS(bus, azx_dev),

	TP_STRUCT__entry(
		__field(unsigned char, stream_tag)
	),

	TP_fast_assign(
		__entry->stream_tag = (azx_dev)->stream_tag;
	),

	TP_printk("stream_tag: %d", __entry->stream_tag)
);

DEFINE_EVENT(hdac_stream, snd_hdac_stream_start,
	TP_PROTO(struct hdac_bus *bus, struct hdac_stream *azx_dev),
	TP_ARGS(bus, azx_dev)
);

DEFINE_EVENT(hdac_stream, snd_hdac_stream_stop,
	TP_PROTO(struct hdac_bus *bus, struct hdac_stream *azx_dev),
	TP_ARGS(bus, azx_dev)
);

#endif /* __HDAC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
