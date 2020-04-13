/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_QLA_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_QLA_H_

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qla

#define QLA_MSG_MAX 256

DECLARE_EVENT_CLASS(qla_log_event,
	TP_PROTO(const char *buf,
		struct va_format *vaf),

	TP_ARGS(buf, vaf),

	TP_STRUCT__entry(
		__string(buf, buf)
		__dynamic_array(char, msg, QLA_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(buf, buf);
		vsnprintf(__get_str(msg), QLA_MSG_MAX, vaf->fmt, *vaf->va);
	),

	TP_printk("%s %s", __get_str(buf), __get_str(msg))
);

DEFINE_EVENT(qla_log_event, ql_dbg_log,
	TP_PROTO(const char *buf, struct va_format *vaf),
	TP_ARGS(buf, vaf)
);

#endif /* _TRACE_QLA_H */

#define TRACE_INCLUDE_FILE qla

#include <trace/define_trace.h>
