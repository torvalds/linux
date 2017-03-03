/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal_virtual

#if !defined(_TRACE_VIRTUAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VIRTUAL_H

#include <linux/thermal.h>
#include <linux/tracepoint.h>

TRACE_EVENT(virtual_temperature,

	TP_PROTO(struct thermal_zone_device *virt_tz,
		struct thermal_zone_device *tz, int sens_temp,
		int est_temp),

	TP_ARGS(virt_tz, tz, sens_temp, est_temp),

	TP_STRUCT__entry(
		__string(virt_zone, virt_tz->type)
		__string(therm_zone, tz->type)
		__field(int, sens_temp)
		__field(int, est_temp)
	),

	TP_fast_assign(
		__assign_str(virt_zone, virt_tz->type);
		__assign_str(therm_zone, tz->type);
		__entry->sens_temp = sens_temp;
		__entry->est_temp = est_temp;
	),

	TP_printk("virt_zone=%s zone=%s temp=%d virtual zone estimated temp=%d",
		__get_str(virt_zone), __get_str(therm_zone),
		__entry->sens_temp,
		__entry->est_temp)
);

#endif /* _TRACE_VIRTUAL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
