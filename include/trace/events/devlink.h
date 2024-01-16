/* SPDX-License-Identifier: GPL-2.0 */
#if IS_ENABLED(CONFIG_NET_DEVLINK)

#undef TRACE_SYSTEM
#define TRACE_SYSTEM devlink

#if !defined(_TRACE_DEVLINK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DEVLINK_H

#include <linux/device.h>
#include <net/devlink.h>
#include <linux/tracepoint.h>

/*
 * Tracepoint for devlink hardware message:
 */
TRACE_EVENT(devlink_hwmsg,
	TP_PROTO(const struct devlink *devlink, bool incoming,
		 unsigned long type, const u8 *buf, size_t len),

	TP_ARGS(devlink, incoming, type, buf, len),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__field(bool, incoming)
		__field(unsigned long, type)
		__dynamic_array(u8, buf, len)
		__field(size_t, len)
	),

	TP_fast_assign(
		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__entry->incoming = incoming;
		__entry->type = type;
		memcpy(__get_dynamic_array(buf), buf, len);
		__entry->len = len;
	),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s incoming=%d type=%lu buf=0x[%*phD] len=%zu",
		  __get_str(bus_name), __get_str(dev_name),
		  __get_str(driver_name), __entry->incoming, __entry->type,
		  (int) __entry->len, __get_dynamic_array(buf), __entry->len)
);

/*
 * Tracepoint for devlink hardware error:
 */
TRACE_EVENT(devlink_hwerr,
	TP_PROTO(const struct devlink *devlink, int err, const char *msg),

	TP_ARGS(devlink, err, msg),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__field(int, err)
		__string(msg, msg)
		),

	TP_fast_assign(
		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__entry->err = err;
		__assign_str(msg, msg);
		),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s err=%d %s",
			__get_str(bus_name), __get_str(dev_name),
			__get_str(driver_name), __entry->err, __get_str(msg))
);

/*
 * Tracepoint for devlink health message:
 */
TRACE_EVENT(devlink_health_report,
	TP_PROTO(const struct devlink *devlink, const char *reporter_name,
		 const char *msg),

	TP_ARGS(devlink, reporter_name, msg),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__string(reporter_name, reporter_name)
		__string(msg, msg)
	),

	TP_fast_assign(
		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__assign_str(reporter_name, reporter_name);
		__assign_str(msg, msg);
	),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s reporter_name=%s: %s",
		  __get_str(bus_name), __get_str(dev_name),
		  __get_str(driver_name), __get_str(reporter_name),
		  __get_str(msg))
);

/*
 * Tracepoint for devlink health recover aborted message:
 */
TRACE_EVENT(devlink_health_recover_aborted,
	TP_PROTO(const struct devlink *devlink, const char *reporter_name,
		 bool health_state, u64 time_since_last_recover),

	TP_ARGS(devlink, reporter_name, health_state, time_since_last_recover),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__string(reporter_name, reporter_name)
		__field(bool, health_state)
		__field(u64, time_since_last_recover)
	),

	TP_fast_assign(
		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__assign_str(reporter_name, reporter_name);
		__entry->health_state = health_state;
		__entry->time_since_last_recover = time_since_last_recover;
	),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s reporter_name=%s: health_state=%d time_since_last_recover=%llu recover aborted",
		  __get_str(bus_name), __get_str(dev_name),
		  __get_str(driver_name), __get_str(reporter_name),
		  __entry->health_state,
		  __entry->time_since_last_recover)
);

/*
 * Tracepoint for devlink health reporter state update:
 */
TRACE_EVENT(devlink_health_reporter_state_update,
	TP_PROTO(const struct devlink *devlink, const char *reporter_name,
		 bool new_state),

	TP_ARGS(devlink, reporter_name, new_state),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__string(reporter_name, reporter_name)
		__field(u8, new_state)
	),

	TP_fast_assign(
		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__assign_str(reporter_name, reporter_name);
		__entry->new_state = new_state;
	),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s reporter_name=%s: new_state=%d",
		  __get_str(bus_name), __get_str(dev_name),
		  __get_str(driver_name), __get_str(reporter_name),
		  __entry->new_state)
);

/*
 * Tracepoint for devlink packet trap:
 */
TRACE_EVENT(devlink_trap_report,
	TP_PROTO(const struct devlink *devlink, struct sk_buff *skb,
		 const struct devlink_trap_metadata *metadata),

	TP_ARGS(devlink, skb, metadata),

	TP_STRUCT__entry(
		__string(bus_name, devlink_to_dev(devlink)->bus->name)
		__string(dev_name, dev_name(devlink_to_dev(devlink)))
		__string(driver_name, devlink_to_dev(devlink)->driver->name)
		__string(trap_name, metadata->trap_name)
		__string(trap_group_name, metadata->trap_group_name)
		__array(char, input_dev_name, IFNAMSIZ)
	),

	TP_fast_assign(
		struct net_device *input_dev = metadata->input_dev;

		__assign_str(bus_name, devlink_to_dev(devlink)->bus->name);
		__assign_str(dev_name, dev_name(devlink_to_dev(devlink)));
		__assign_str(driver_name, devlink_to_dev(devlink)->driver->name);
		__assign_str(trap_name, metadata->trap_name);
		__assign_str(trap_group_name, metadata->trap_group_name);
		strscpy(__entry->input_dev_name, input_dev ? input_dev->name : "NULL", IFNAMSIZ);
	),

	TP_printk("bus_name=%s dev_name=%s driver_name=%s trap_name=%s "
		  "trap_group_name=%s input_dev_name=%s", __get_str(bus_name),
		  __get_str(dev_name), __get_str(driver_name),
		  __get_str(trap_name), __get_str(trap_group_name),
		  __entry->input_dev_name)
);

#endif /* _TRACE_DEVLINK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

#else /* CONFIG_NET_DEVLINK */

#if !defined(_TRACE_DEVLINK_H)
#define _TRACE_DEVLINK_H

#include <net/devlink.h>

static inline void trace_devlink_hwmsg(const struct devlink *devlink,
				       bool incoming, unsigned long type,
				       const u8 *buf, size_t len)
{
}

static inline void trace_devlink_hwerr(const struct devlink *devlink,
				       int err, const char *msg)
{
}
#endif /* _TRACE_DEVLINK_H */

#endif
