/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hwmon

#if !defined(_TRACE_HWMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HWMON_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hwmon_attr_class,

	TP_PROTO(int index, const char *attr_name, long val),

	TP_ARGS(index, attr_name, val),

	TP_STRUCT__entry(
		__field(int, index)
		__string(attr_name, attr_name)
		__field(long, val)
	),

	TP_fast_assign(
		__entry->index = index;
		__assign_str(attr_name);
		__entry->val = val;
	),

	TP_printk("index=%d, attr_name=%s, val=%ld",
		  __entry->index,  __get_str(attr_name), __entry->val)
);

DEFINE_EVENT(hwmon_attr_class, hwmon_attr_show,

	TP_PROTO(int index, const char *attr_name, long val),

	TP_ARGS(index, attr_name, val)
);

DEFINE_EVENT(hwmon_attr_class, hwmon_attr_store,

	TP_PROTO(int index, const char *attr_name, long val),

	TP_ARGS(index, attr_name, val)
);

TRACE_EVENT(hwmon_attr_show_string,

	TP_PROTO(int index, const char *attr_name, const char *s),

	TP_ARGS(index, attr_name, s),

	TP_STRUCT__entry(
		__field(int, index)
		__string(attr_name, attr_name)
		__string(label, s)
	),

	TP_fast_assign(
		__entry->index = index;
		__assign_str(attr_name);
		__assign_str(label);
	),

	TP_printk("index=%d, attr_name=%s, val=%s",
		  __entry->index, __get_str(attr_name), __get_str(label))
);

#endif /* _TRACE_HWMON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
