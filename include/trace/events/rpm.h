/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpm

#if !defined(_TRACE_RUNTIME_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RUNTIME_POWER_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

struct device;

/*
 * The rpm_internal events are used for tracing some important
 * runtime pm internal functions.
 */
DECLARE_EVENT_CLASS(rpm_internal,

	TP_PROTO(struct device *dev, int flags),

	TP_ARGS(dev, flags),

	TP_STRUCT__entry(
		__string(       name,		dev_name(dev)	)
		__field(        int,            flags           )
		__field(        int ,   	usage_count	)
		__field(        int ,   	disable_depth   )
		__field(        int ,   	runtime_auto	)
		__field(        int ,   	request_pending	)
		__field(        int ,   	irq_safe	)
		__field(        int ,   	child_count 	)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->flags = flags;
		__entry->usage_count = atomic_read(
			&dev->power.usage_count);
		__entry->disable_depth = dev->power.disable_depth;
		__entry->runtime_auto = dev->power.runtime_auto;
		__entry->request_pending = dev->power.request_pending;
		__entry->irq_safe = dev->power.irq_safe;
		__entry->child_count = atomic_read(
			&dev->power.child_count);
	),

	TP_printk("%s flags-%x cnt-%-2d dep-%-2d auto-%-1d p-%-1d"
			" irq-%-1d child-%d",
			__get_str(name), __entry->flags,
			__entry->usage_count,
			__entry->disable_depth,
			__entry->runtime_auto,
			__entry->request_pending,
			__entry->irq_safe,
			__entry->child_count
		 )
);
DEFINE_EVENT(rpm_internal, rpm_suspend,

	TP_PROTO(struct device *dev, int flags),

	TP_ARGS(dev, flags)
);
DEFINE_EVENT(rpm_internal, rpm_resume,

	TP_PROTO(struct device *dev, int flags),

	TP_ARGS(dev, flags)
);
DEFINE_EVENT(rpm_internal, rpm_idle,

	TP_PROTO(struct device *dev, int flags),

	TP_ARGS(dev, flags)
);
DEFINE_EVENT(rpm_internal, rpm_usage,

	TP_PROTO(struct device *dev, int flags),

	TP_ARGS(dev, flags)
);

TRACE_EVENT(rpm_return_int,
	TP_PROTO(struct device *dev, unsigned long ip, int ret),
	TP_ARGS(dev, ip, ret),

	TP_STRUCT__entry(
		__string(       name,		dev_name(dev))
		__field(	unsigned long,		ip	)
		__field(	int,			ret	)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->ip = ip;
		__entry->ret = ret;
	),

	TP_printk("%pS:%s ret=%d", (void *)__entry->ip, __get_str(name),
		__entry->ret)
);

#define RPM_STATUS_STRINGS \
	EM(RPM_INVALID, "RPM_INVALID") \
	EM(RPM_ACTIVE, "RPM_ACTIVE") \
	EM(RPM_RESUMING, "RPM_RESUMING") \
	EM(RPM_SUSPENDED, "RPM_SUSPENDED") \
	EMe(RPM_SUSPENDING, "RPM_SUSPENDING")

/* Enums require being exported to userspace, for user tool parsing. */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

RPM_STATUS_STRINGS

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{ a, b },
#define EMe(a, b)	{ a, b }

TRACE_EVENT(rpm_status,
	TP_PROTO(struct device *dev, enum rpm_status status),
	TP_ARGS(dev, status),

	TP_STRUCT__entry(
		__string(name,	dev_name(dev))
		__field(int,	status)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->status = status;
	),

	TP_printk("%s status=%s", __get_str(name),
		__print_symbolic(__entry->status, RPM_STATUS_STRINGS))
);

#endif /* _TRACE_RUNTIME_POWER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
