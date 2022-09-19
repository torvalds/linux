/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 *
 * Author: Noah Klayman <noah.klayman@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sof

#if !defined(_TRACE_SOF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SOF_H
#include <linux/tracepoint.h>
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
		__assign_str(name, swidget->widget->name);
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

#endif /* _TRACE_SOF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
