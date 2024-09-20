#undef TRACE_SYSTEM
#define TRACE_SYSTEM iscsi

#if !defined(_TRACE_ISCSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ISCSI_H

#include <linux/tracepoint.h>

/* max debug message length */
#define ISCSI_MSG_MAX	256

/*
 * Declare tracepoint helper function.
 */
void iscsi_dbg_trace(void (*trace)(struct device *dev, struct va_format *),
		     struct device *dev, const char *fmt, ...);

/*
 * Declare event class for iscsi debug messages.
 */
DECLARE_EVENT_CLASS(iscsi_log_msg,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf),

	TP_STRUCT__entry(
		__string(dname, 	dev_name(dev)		)
		__vstring(msg,		vaf->fmt, vaf->va)
	),

	TP_fast_assign(
		__assign_str(dname);
		__assign_vstr(msg, vaf->fmt, vaf->va);
	),

	TP_printk("%s: %s",__get_str(dname),  __get_str(msg)
	)
);

/*
 * Define event to capture iscsi connection debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_conn,
	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi session debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_session,
	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi error handling debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_eh,
        TP_PROTO(struct device *dev, struct va_format *vaf),

        TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi tcp debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_tcp,
        TP_PROTO(struct device *dev, struct va_format *vaf),

        TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi sw tcp debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_sw_tcp,
	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi transport session debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_trans_session,
	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

/*
 * Define event to capture iscsi transport connection debug messages.
 */
DEFINE_EVENT(iscsi_log_msg, iscsi_dbg_trans_conn,
	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

#endif /* _TRACE_ISCSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
