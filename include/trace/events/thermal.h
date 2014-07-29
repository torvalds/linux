#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal

#if !defined(_TRACE_THERMAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_H

#include <linux/thermal.h>
#include <linux/tracepoint.h>

TRACE_EVENT(thermal_temperature,

	TP_PROTO(struct thermal_zone_device *tz),

	TP_ARGS(tz),

	TP_STRUCT__entry(
		__string(thermal_zone, tz->type)
		__field(int, id)
		__field(int, temp_prev)
		__field(int, temp)
	),

	TP_fast_assign(
		__assign_str(thermal_zone, tz->type);
		__entry->id = tz->id;
		__entry->temp_prev = tz->last_temperature;
		__entry->temp = tz->temperature;
	),

	TP_printk("thermal_zone=%s id=%d temp_prev=%d temp=%d",
		__get_str(thermal_zone), __entry->id, __entry->temp_prev,
		__entry->temp)
);

TRACE_EVENT(cdev_update,

	TP_PROTO(struct thermal_cooling_device *cdev, unsigned long target),

	TP_ARGS(cdev, target),

	TP_STRUCT__entry(
		__string(type, cdev->type)
		__field(unsigned long, target)
	),

	TP_fast_assign(
		__assign_str(type, cdev->type);
		__entry->target = target;
	),

	TP_printk("type=%s target=%lu", __get_str(type), __entry->target)
);

#endif /* _TRACE_THERMAL_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
