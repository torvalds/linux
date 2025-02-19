/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_avs

#if !defined(_TRACE_INTEL_AVS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTEL_AVS_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(avs_dsp_core_op,

	TP_PROTO(unsigned int reg, unsigned int mask, const char *op, bool flag),

	TP_ARGS(reg, mask, op, flag),

	TP_STRUCT__entry(
		__field(unsigned int,	reg	)
		__field(unsigned int,	mask	)
		__string(op,		op	)
		__field(bool,		flag	)
	),

	TP_fast_assign(
		__entry->reg = reg;
		__entry->mask = mask;
		__assign_str(op);
		__entry->flag = flag;
	),

	TP_printk("%s: %d, core mask: 0x%X, prev state: 0x%08X",
		  __get_str(op), __entry->flag, __entry->mask, __entry->reg)
);

#ifndef __TRACE_INTEL_AVS_TRACE_HELPER
#define __TRACE_INTEL_AVS_TRACE_HELPER

void trace_avs_msg_payload(const void *data, size_t size);

#define trace_avs_request(msg, sts, lec) \
({ \
	trace_avs_ipc_request_msg((msg)->header, sts, lec); \
	trace_avs_msg_payload((msg)->data, (msg)->size); \
})

#define trace_avs_reply(msg, sts, lec) \
({ \
	trace_avs_ipc_reply_msg((msg)->header, sts, lec); \
	trace_avs_msg_payload((msg)->data, (msg)->size); \
})

#define trace_avs_notify(msg, sts, lec) \
({ \
	trace_avs_ipc_notify_msg((msg)->header, sts, lec); \
	trace_avs_msg_payload((msg)->data, (msg)->size); \
})
#endif

DECLARE_EVENT_CLASS(avs_ipc_msg_hdr,

	TP_PROTO(u64 header, u32 sts, u32 lec),

	TP_ARGS(header, sts, lec),

	TP_STRUCT__entry(
		__field(u64,	header)
		__field(u32,	sts)
		__field(u32,	lec)
	),

	TP_fast_assign(
		__entry->header = header;
		__entry->sts = sts;
		__entry->lec = lec;
	),

	TP_printk("primary: 0x%08X, extension: 0x%08X,\n"
		  "status: 0x%08X, error: 0x%08X",
		  lower_32_bits(__entry->header), upper_32_bits(__entry->header),
		  __entry->sts, __entry->lec)
);

DEFINE_EVENT(avs_ipc_msg_hdr, avs_ipc_request_msg,
	TP_PROTO(u64 header, u32 sts, u32 lec),
	TP_ARGS(header, sts, lec)
);

DEFINE_EVENT(avs_ipc_msg_hdr, avs_ipc_reply_msg,
	TP_PROTO(u64 header, u32 sts, u32 lec),
	TP_ARGS(header, sts, lec)
);

DEFINE_EVENT(avs_ipc_msg_hdr, avs_ipc_notify_msg,
	TP_PROTO(u64 header, u32 sts, u32 lec),
	TP_ARGS(header, sts, lec)
);

TRACE_EVENT_CONDITION(avs_ipc_msg_payload,

	TP_PROTO(const u8 *data, size_t size, size_t offset, size_t total),

	TP_ARGS(data, size, offset, total),

	TP_CONDITION(data && size),

	TP_STRUCT__entry(
		__dynamic_array(u8,	buf,	size	)
		__field(size_t,		offset		)
		__field(size_t,		pos		)
		__field(size_t,		total		)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(buf), data + offset, size);
		__entry->offset = offset;
		__entry->pos = offset + size;
		__entry->total = total;
	),

	TP_printk("range %zu-%zu out of %zu bytes%s",
		  __entry->offset, __entry->pos, __entry->total,
		  __print_hex_dump("", DUMP_PREFIX_NONE, 16, 4,
				   __get_dynamic_array(buf),
				   __get_dynamic_array_len(buf), false))
);

TRACE_EVENT(avs_d0ix,

	TP_PROTO(const char *op, bool proceed, u64 header),

	TP_ARGS(op, proceed, header),

	TP_STRUCT__entry(
		__string(op,	op	)
		__field(bool,	proceed	)
		__field(u64,	header	)
	),

	TP_fast_assign(
		__assign_str(op);
		__entry->proceed = proceed;
		__entry->header = header;
	),

	TP_printk("%s%s for request: 0x%08X 0x%08X",
		  __entry->proceed ? "" : "ignore ", __get_str(op),
		  lower_32_bits(__entry->header), upper_32_bits(__entry->header))
);

#endif /* _TRACE_INTEL_AVS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
