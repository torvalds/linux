/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 *
 * Author: Noah Klayman <noah.klayman@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sof_intel

#if !defined(_TRACE_SOF_INTEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SOF_INTEL_H
#include <linux/tracepoint.h>
#include "../../../sound/soc/sof/sof-audio.h"

TRACE_EVENT(sof_intel_hda_irq,
	TP_PROTO(struct snd_sof_dev *sdev, char *source),
	TP_ARGS(sdev, source),
	TP_STRUCT__entry(
		__string(device_name, dev_name(sdev->dev))
		__string(source, source)
	),
	TP_fast_assign(
		__assign_str(device_name, dev_name(sdev->dev));
		__assign_str(source, source);
	),
	TP_printk("device_name=%s source=%s",
		  __get_str(device_name), __get_str(source))
);

#endif /* _TRACE_SOF_INTEL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
