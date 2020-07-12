/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal_ipa_power

#if !defined(_TRACE_THERMAL_IPA_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_IPA_POWER_H

#include <linux/tracepoint.h>

TRACE_EVENT(thermal_ipa_get_static_power,
	TP_PROTO(u32 leakage, u32 coefficient, s32 temp,
		 u32 temp_scaling_factor, u32 volt, u32 volt_scaling_factor,
		 u32 static_power),

	TP_ARGS(leakage, coefficient, temp, temp_scaling_factor, volt,
		volt_scaling_factor, static_power),

	TP_STRUCT__entry(
		__field(u32,	leakage)
		__field(u32,	coefficient)
		__field(s32,	temp)
		__field(u32,	temp_scaling_factor)
		__field(u32,	volt)
		__field(u32,	volt_scaling_factor)
		__field(u32,	static_power)
	),

	TP_fast_assign(
		__entry->leakage = leakage;
		__entry->coefficient = coefficient;
		__entry->temp = temp;
		__entry->temp_scaling_factor = temp_scaling_factor;
		__entry->volt = volt;
		__entry->volt_scaling_factor = volt_scaling_factor;
		__entry->static_power = static_power;
	),
	TP_printk("lkg=%u c=%u t=%d ts=%u v=%u vs=%u static_power=%u",
		   __entry->leakage, __entry->coefficient, __entry->temp,
		  __entry->temp_scaling_factor, __entry->volt,
		  __entry->volt_scaling_factor, __entry->static_power)
);


#endif /* _TRACE_THERMAL_IPA_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
