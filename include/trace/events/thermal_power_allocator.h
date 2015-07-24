#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal_power_allocator

#if !defined(_TRACE_THERMAL_POWER_ALLOCATOR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_POWER_ALLOCATOR_H

#include <linux/tracepoint.h>

TRACE_EVENT(thermal_power_allocator,
	TP_PROTO(struct thermal_zone_device *tz, u32 *req_power,
		 u32 total_req_power, u32 *granted_power,
		 u32 total_granted_power, size_t num_actors,
		 u32 power_range, u32 max_allocatable_power,
		 int current_temp, s32 delta_temp),
	TP_ARGS(tz, req_power, total_req_power, granted_power,
		total_granted_power, num_actors, power_range,
		max_allocatable_power, current_temp, delta_temp),
	TP_STRUCT__entry(
		__field(int,           tz_id          )
		__dynamic_array(u32,   req_power, num_actors    )
		__field(u32,           total_req_power          )
		__dynamic_array(u32,   granted_power, num_actors)
		__field(u32,           total_granted_power      )
		__field(size_t,        num_actors               )
		__field(u32,           power_range              )
		__field(u32,           max_allocatable_power    )
		__field(int,           current_temp             )
		__field(s32,           delta_temp               )
	),
	TP_fast_assign(
		__entry->tz_id = tz->id;
		memcpy(__get_dynamic_array(req_power), req_power,
			num_actors * sizeof(*req_power));
		__entry->total_req_power = total_req_power;
		memcpy(__get_dynamic_array(granted_power), granted_power,
			num_actors * sizeof(*granted_power));
		__entry->total_granted_power = total_granted_power;
		__entry->num_actors = num_actors;
		__entry->power_range = power_range;
		__entry->max_allocatable_power = max_allocatable_power;
		__entry->current_temp = current_temp;
		__entry->delta_temp = delta_temp;
	),

	TP_printk("thermal_zone_id=%d req_power={%s} total_req_power=%u granted_power={%s} total_granted_power=%u power_range=%u max_allocatable_power=%u current_temperature=%d delta_temperature=%d",
		__entry->tz_id,
		__print_array(__get_dynamic_array(req_power),
                              __entry->num_actors, 4),
		__entry->total_req_power,
		__print_array(__get_dynamic_array(granted_power),
                              __entry->num_actors, 4),
		__entry->total_granted_power, __entry->power_range,
		__entry->max_allocatable_power, __entry->current_temp,
		__entry->delta_temp)
);

TRACE_EVENT(thermal_power_allocator_pid,
	TP_PROTO(struct thermal_zone_device *tz, s32 err, s32 err_integral,
		 s64 p, s64 i, s64 d, s32 output),
	TP_ARGS(tz, err, err_integral, p, i, d, output),
	TP_STRUCT__entry(
		__field(int, tz_id       )
		__field(s32, err         )
		__field(s32, err_integral)
		__field(s64, p           )
		__field(s64, i           )
		__field(s64, d           )
		__field(s32, output      )
	),
	TP_fast_assign(
		__entry->tz_id = tz->id;
		__entry->err = err;
		__entry->err_integral = err_integral;
		__entry->p = p;
		__entry->i = i;
		__entry->d = d;
		__entry->output = output;
	),

	TP_printk("thermal_zone_id=%d err=%d err_integral=%d p=%lld i=%lld d=%lld output=%d",
		  __entry->tz_id, __entry->err, __entry->err_integral,
		  __entry->p, __entry->i, __entry->d, __entry->output)
);
#endif /* _TRACE_THERMAL_POWER_ALLOCATOR_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
