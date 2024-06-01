/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2022 Intel Corporation
 *
 * Author: Noah Klayman <noah.klayman@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sof

#if !defined(_TRACE_SOF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SOF_H
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <sound/sof/stream.h>
#include "../../../sound/soc/sof/sof-audio.h"

DECLARE_EVENT_CLASS(sof_widget_template,
	TP_PROTO(struct snd_sof_widget *swidget),
	TP_ARGS(swidget),
	TP_STRUCT__entry(
		__string(name, swidget->widget->name)
		__field(int, use_count)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->use_count = swidget->use_count;
	),
	TP_printk("name=%s use_count=%d", __get_str(name), __entry->use_count)
);

DEFINE_EVENT(sof_widget_template, sof_widget_setup,
	TP_PROTO(struct snd_sof_widget *swidget),
	TP_ARGS(swidget)
);

DEFINE_EVENT(sof_widget_template, sof_widget_free,
	TP_PROTO(struct snd_sof_widget *swidget),
	TP_ARGS(swidget)
);

TRACE_EVENT(sof_ipc3_period_elapsed_position,
	TP_PROTO(struct snd_sof_dev *sdev, struct sof_ipc_stream_posn *posn),
	TP_ARGS(sdev, posn),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u64, host_posn)
		__field(u64, dai_posn)
		__field(u64, wallclock)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->host_posn = posn->host_posn;
		__entry->dai_posn = posn->dai_posn;
		__entry->wallclock = posn->wallclock;
	),
	TP_printk("device_name=%s host_posn=%#llx dai_posn=%#llx wallclock=%#llx",
		  __get_str(device_name), __entry->host_posn, __entry->dai_posn,
		  __entry->wallclock)
);

TRACE_EVENT(sof_pcm_pointer_position,
	TP_PROTO(struct snd_sof_dev *sdev,
		struct snd_sof_pcm *spcm,
		struct snd_pcm_substream *substream,
		snd_pcm_uframes_t dma_posn,
		snd_pcm_uframes_t dai_posn
	),
	TP_ARGS(sdev, spcm, substream, dma_posn, dai_posn),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__field(u32, pcm_id)
		__field(int, stream)
		__field(unsigned long, dma_posn)
		__field(unsigned long, dai_posn)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__entry->pcm_id = le32_to_cpu(spcm->pcm.pcm_id);
		__entry->stream = substream->stream;
		__entry->dma_posn = dma_posn;
		__entry->dai_posn = dai_posn;
	),
	TP_printk("device_name=%s pcm_id=%d stream=%d dma_posn=%lu dai_posn=%lu",
		  __get_str(device_name), __entry->pcm_id, __entry->stream,
		  __entry->dma_posn, __entry->dai_posn)
);

TRACE_EVENT(sof_stream_position_ipc_rx,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		__string(device_name, dev_name(dev))
	),
	TP_fast_assign(
		__assign_str(device_name);
	),
	TP_printk("device_name=%s", __get_str(device_name))
);

TRACE_EVENT(sof_ipc4_fw_config,
	TP_PROTO(struct snd_sof_dev *sdev, char *key, u32 value),
	TP_ARGS(sdev, key, value),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__string(key, key)
		__field(u32, value)
	),
	TP_fast_assign(
		__assign_str(device_name);
		__assign_str(key);
		__entry->value = value;
	),
	TP_printk("device_name=%s key=%s value=%d",
		  __get_str(device_name), __get_str(key), __entry->value)
);

#endif /* _TRACE_SOF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
