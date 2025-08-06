/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM tsm_mr

#if !defined(_TRACE_TSM_MR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TSM_MR_H

#include <linux/tracepoint.h>
#include <linux/tsm-mr.h>

TRACE_EVENT(tsm_mr_read,

	TP_PROTO(const struct tsm_measurement_register *mr),

	TP_ARGS(mr),

	TP_STRUCT__entry(
		__string(mr, mr->mr_name)
		__string(hash, mr->mr_flags & TSM_MR_F_NOHASH ?
			 "data" : hash_algo_name[mr->mr_hash])
		__dynamic_array(u8, d, mr->mr_size)
	),

	TP_fast_assign(
		__assign_str(mr);
		__assign_str(hash);
		memcpy(__get_dynamic_array(d), mr->mr_value, __get_dynamic_array_len(d));
	),

	TP_printk("[%s] %s:%s", __get_str(mr), __get_str(hash),
		  __print_hex_str(__get_dynamic_array(d), __get_dynamic_array_len(d)))
);

TRACE_EVENT(tsm_mr_refresh,

	TP_PROTO(const struct tsm_measurement_register *mr, int rc),

	TP_ARGS(mr, rc),

	TP_STRUCT__entry(
		__string(mr, mr->mr_name)
		__field(int, rc)
	),

	TP_fast_assign(
		__assign_str(mr);
		__entry->rc = rc;
	),

	TP_printk("[%s] %s:%d", __get_str(mr),
		  __entry->rc ? "failed" : "succeeded", __entry->rc)
);

TRACE_EVENT(tsm_mr_write,

	TP_PROTO(const struct tsm_measurement_register *mr, const u8 *data),

	TP_ARGS(mr, data),

	TP_STRUCT__entry(
		__string(mr, mr->mr_name)
		__string(hash, mr->mr_flags & TSM_MR_F_NOHASH ?
			 "data" : hash_algo_name[mr->mr_hash])
		__dynamic_array(u8, d, mr->mr_size)
	),

	TP_fast_assign(
		__assign_str(mr);
		__assign_str(hash);
		memcpy(__get_dynamic_array(d), data, __get_dynamic_array_len(d));
	),

	TP_printk("[%s] %s:%s", __get_str(mr), __get_str(hash),
		  __print_hex_str(__get_dynamic_array(d), __get_dynamic_array_len(d)))
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
