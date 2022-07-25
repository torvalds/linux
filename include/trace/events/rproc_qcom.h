/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rproc_qcom

#if !defined(_TRACE_RPROC_QCOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPROC_QCOM_H
#include <linux/tracepoint.h>

TRACE_EVENT(rproc_qcom_event,

	TP_PROTO(const char *name, const char *event, const char *subevent),

	TP_ARGS(name, event, subevent),

	TP_STRUCT__entry(
		__string(name, name)
		__string(event, event)
		__string(subevent, subevent)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__assign_str(event, event);
		__assign_str(subevent, subevent);
	),

	TP_printk("%s: %s: %s", __get_str(name), __get_str(event), __get_str(subevent))
);
#endif /* _TRACE_RPROC_QCOM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
