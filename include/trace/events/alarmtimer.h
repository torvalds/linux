/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM alarmtimer

#if !defined(_TRACE_ALARMTIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ALARMTIMER_H

#include <linux/alarmtimer.h>
#include <linux/rtc.h>
#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(ALARM_REALTIME);
TRACE_DEFINE_ENUM(ALARM_BOOTTIME);
TRACE_DEFINE_ENUM(ALARM_REALTIME_FREEZER);
TRACE_DEFINE_ENUM(ALARM_BOOTTIME_FREEZER);

#define show_alarm_type(type)	__print_flags(type, " | ",	\
	{ 1 << ALARM_REALTIME, "REALTIME" },			\
	{ 1 << ALARM_BOOTTIME, "BOOTTIME" },			\
	{ 1 << ALARM_REALTIME_FREEZER, "REALTIME Freezer" },	\
	{ 1 << ALARM_BOOTTIME_FREEZER, "BOOTTIME Freezer" })

#ifdef CONFIG_RTC_CLASS
TRACE_EVENT(alarmtimer_suspend,

	TP_PROTO(ktime_t expires, int flag),

	TP_ARGS(expires, flag),

	TP_STRUCT__entry(
		__field(s64, expires)
		__field(unsigned char, alarm_type)
	),

	TP_fast_assign(
		__entry->expires = expires;
		__entry->alarm_type = flag;
	),

	TP_printk("alarmtimer type:%s expires:%llu",
		  show_alarm_type((1 << __entry->alarm_type)),
		  __entry->expires
	)
);
#endif /* CONFIG_RTC_CLASS */

DECLARE_EVENT_CLASS(alarm_class,

	TP_PROTO(struct alarm *alarm, ktime_t now),

	TP_ARGS(alarm, now),

	TP_STRUCT__entry(
		__field(void *,	alarm)
		__field(unsigned char, alarm_type)
		__field(s64, expires)
		__field(s64, now)
	),

	TP_fast_assign(
		__entry->alarm = alarm;
		__entry->alarm_type = alarm->type;
		__entry->expires = alarm->node.expires;
		__entry->now = now;
	),

	TP_printk("alarmtimer:%p type:%s expires:%llu now:%llu",
		  __entry->alarm,
		  show_alarm_type((1 << __entry->alarm_type)),
		  __entry->expires,
		  __entry->now
	)
);

DEFINE_EVENT(alarm_class, alarmtimer_fired,

	TP_PROTO(struct alarm *alarm, ktime_t now),

	TP_ARGS(alarm, now)
);

DEFINE_EVENT(alarm_class, alarmtimer_start,

	TP_PROTO(struct alarm *alarm, ktime_t now),

	TP_ARGS(alarm, now)
);

DEFINE_EVENT(alarm_class, alarmtimer_cancel,

	TP_PROTO(struct alarm *alarm, ktime_t now),

	TP_ARGS(alarm, now)
);

#endif /* _TRACE_ALARMTIMER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
