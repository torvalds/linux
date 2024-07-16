/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2022 Intel Corporation
 *
 * Author: Noah Klayman <noah.klayman@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sof_intel

#if !defined(_TRACE_SOF_INTEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SOF_INTEL_H
#include <linux/tracepoint.h>
#include <sound/hdaudio.h>
#include "../../../sound/soc/sof/sof-audio.h"

TRACE_EVENT(sof_intel_hda_irq,
	TP_PROTO(struct snd_sof_dev *sdev, char *source),
	TP_ARGS(sdev, source),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__string(source, source)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__assign_str(source);
	),
	TP_printk("device_name=%s source=%s",
		  __get_str(device_name), __get_str(source))
);

DECLARE_EVENT_CLASS(sof_intel_ipc_firmware_template,
	TP_ARGS(struct snd_sof_dev *sdev, u32 msg, u32 msg_ext),
	TP_PROTO(sdev, msg, msg_ext),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u32, msg)
		__field(u32, msg_ext)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->msg = msg;
		__entry->msg_ext = msg_ext;
	),
	TP_printk("device_name=%s msg=%#x msg_ext=%#x",
		  __get_str(device_name), __entry->msg, __entry->msg_ext)
);

DEFINE_EVENT(sof_intel_ipc_firmware_template, sof_intel_ipc_firmware_response,
	TP_PROTO(struct snd_sof_dev *sdev, u32 msg, u32 msg_ext),
	TP_ARGS(sdev, msg, msg_ext)
);

DEFINE_EVENT(sof_intel_ipc_firmware_template, sof_intel_ipc_firmware_initiated,
	TP_PROTO(struct snd_sof_dev *sdev, u32 msg, u32 msg_ext),
	TP_ARGS(sdev, msg, msg_ext)
);

TRACE_EVENT(sof_intel_D0I3C_updated,
	TP_PROTO(struct snd_sof_dev *sdev, u8 reg),
	TP_ARGS(sdev, reg),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u8, reg)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->reg = reg;
	),
	TP_printk("device_name=%s register=%#x",
		  __get_str(device_name), __entry->reg)
);

TRACE_EVENT(sof_intel_hda_irq_ipc_check,
	TP_PROTO(struct snd_sof_dev *sdev, u32 irq_status),
	TP_ARGS(sdev, irq_status),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u32, irq_status)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->irq_status = irq_status;
	),
	TP_printk("device_name=%s irq_status=%#x",
		  __get_str(device_name), __entry->irq_status)
);

TRACE_EVENT(sof_intel_hda_dsp_pcm,
	TP_PROTO(struct snd_sof_dev *sdev,
		struct hdac_stream *hstream,
		struct snd_pcm_substream *substream,
		snd_pcm_uframes_t pos
	),
	TP_ARGS(sdev, hstream, substream, pos),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u32, hstream_index)
		__field(u32, substream)
		__field(unsigned long, pos)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->hstream_index = hstream->index;
		__entry->substream = substream->stream;
		__entry->pos = pos;
	),
	TP_printk("device_name=%s hstream_index=%d substream=%d pos=%lu",
		  __get_str(device_name), __entry->hstream_index,
		  __entry->substream, __entry->pos)
);

TRACE_EVENT(sof_intel_hda_dsp_stream_status,
	TP_PROTO(struct device *dev, struct hdac_stream *s, u32 status),
	TP_ARGS(dev, s, status),
	TP_STRUCT__entry(
		__string(device_name, dev_name(dev))
		__field(u32, stream)
		__field(u32, status)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->stream = s->index;
		__entry->status = status;
	),
	TP_printk("device_name=%s stream=%d status=%#x",
		  __get_str(device_name), __entry->stream, __entry->status)
);

TRACE_EVENT(sof_intel_hda_dsp_check_stream_irq,
	TP_PROTO(struct snd_sof_dev *sdev, u32 status),
	TP_ARGS(sdev, status),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u32, status)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->status = status;
	),
	TP_printk("device_name=%s status=%#x",
		  __get_str(device_name), __entry->status)
);

#endif /* _TRACE_SOF_INTEL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
