/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_THERMAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_THERMAL_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#ifdef __GENKSYMS__
struct thermal_zone_device;
#else
/* struct thermal_zone_device */
#include <linux/thermal.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_enable_thermal_genl_check,
	TP_PROTO(int event, int tz_id, int *enable_thermal_genl),
	TP_ARGS(event, tz_id, enable_thermal_genl));

DECLARE_HOOK(android_vh_thermal_pm_notify_suspend,
	     TP_PROTO(struct thermal_zone_device *tz, int *irq_wakeable),
	     TP_ARGS(tz, irq_wakeable));

#endif /* _TRACE_HOOK_THERMAL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

