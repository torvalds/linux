/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_catpt

#if !defined(__SND_SOC_INTEL_CATPT_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __SND_SOC_INTEL_CATPT_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(catpt_ipc_msg,

	TP_PROTO(u32 header),

	TP_ARGS(header),

	TP_STRUCT__entry(
		__field(u32, header)
	),

	TP_fast_assign(
		__entry->header = header;
	),

	TP_printk("0x%08x", __entry->header)
);

DEFINE_EVENT(catpt_ipc_msg, catpt_irq,
	TP_PROTO(u32 header),
	TP_ARGS(header)
);

DEFINE_EVENT(catpt_ipc_msg, catpt_ipc_request,
	TP_PROTO(u32 header),
	TP_ARGS(header)
);

DEFINE_EVENT(catpt_ipc_msg, catpt_ipc_reply,
	TP_PROTO(u32 header),
	TP_ARGS(header)
);

DEFINE_EVENT(catpt_ipc_msg, catpt_ipc_notify,
	TP_PROTO(u32 header),
	TP_ARGS(header)
);

TRACE_EVENT_CONDITION(catpt_ipc_payload,

	TP_PROTO(const u8 *data, size_t size),

	TP_ARGS(data, size),

	TP_CONDITION(data && size),

	TP_STRUCT__entry(
		__dynamic_array(u8, buf, size)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(buf), data, size);
	),

	TP_printk("%u byte(s)%s",
		  __get_dynamic_array_len(buf),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 16, 4,
				   __get_dynamic_array(buf),
				   __get_dynamic_array_len(buf), false))
);

#endif /* __SND_SOC_INTEL_CATPT_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
