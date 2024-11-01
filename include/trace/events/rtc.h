#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtc

#if !defined(_TRACE_RTC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RTC_H

#include <linux/rtc.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(rtc_time_alarm_class,

	TP_PROTO(time64_t secs, int err),

	TP_ARGS(secs, err),

	TP_STRUCT__entry(
		__field(time64_t, secs)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->secs = secs;
		__entry->err = err;
	),

	TP_printk("UTC (%lld) (%d)",
		  __entry->secs, __entry->err
	)
);

DEFINE_EVENT(rtc_time_alarm_class, rtc_set_time,

	TP_PROTO(time64_t secs, int err),

	TP_ARGS(secs, err)
);

DEFINE_EVENT(rtc_time_alarm_class, rtc_read_time,

	TP_PROTO(time64_t secs, int err),

	TP_ARGS(secs, err)
);

DEFINE_EVENT(rtc_time_alarm_class, rtc_set_alarm,

	TP_PROTO(time64_t secs, int err),

	TP_ARGS(secs, err)
);

DEFINE_EVENT(rtc_time_alarm_class, rtc_read_alarm,

	TP_PROTO(time64_t secs, int err),

	TP_ARGS(secs, err)
);

TRACE_EVENT(rtc_irq_set_freq,

	TP_PROTO(int freq, int err),

	TP_ARGS(freq, err),

	TP_STRUCT__entry(
		__field(int, freq)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->freq = freq;
		__entry->err = err;
	),

	TP_printk("set RTC periodic IRQ frequency:%u (%d)",
		  __entry->freq, __entry->err
	)
);

TRACE_EVENT(rtc_irq_set_state,

	TP_PROTO(int enabled, int err),

	TP_ARGS(enabled, err),

	TP_STRUCT__entry(
		__field(int, enabled)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->enabled = enabled;
		__entry->err = err;
	),

	TP_printk("%s RTC 2^N Hz periodic IRQs (%d)",
		  __entry->enabled ? "enable" : "disable",
		  __entry->err
	)
);

TRACE_EVENT(rtc_alarm_irq_enable,

	TP_PROTO(unsigned int enabled, int err),

	TP_ARGS(enabled, err),

	TP_STRUCT__entry(
		__field(unsigned int, enabled)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->enabled = enabled;
		__entry->err = err;
	),

	TP_printk("%s RTC alarm IRQ (%d)",
		  __entry->enabled ? "enable" : "disable",
		  __entry->err
	)
);

DECLARE_EVENT_CLASS(rtc_offset_class,

	TP_PROTO(long offset, int err),

	TP_ARGS(offset, err),

	TP_STRUCT__entry(
		__field(long, offset)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->offset = offset;
		__entry->err = err;
	),

	TP_printk("RTC offset: %ld (%d)",
		  __entry->offset, __entry->err
	)
);

DEFINE_EVENT(rtc_offset_class, rtc_set_offset,

	TP_PROTO(long offset, int err),

	TP_ARGS(offset, err)
);

DEFINE_EVENT(rtc_offset_class, rtc_read_offset,

	TP_PROTO(long offset, int err),

	TP_ARGS(offset, err)
);

DECLARE_EVENT_CLASS(rtc_timer_class,

	TP_PROTO(struct rtc_timer *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field(struct rtc_timer *, timer)
		__field(ktime_t, expires)
		__field(ktime_t, period)
	),

	TP_fast_assign(
		__entry->timer = timer;
		__entry->expires = timer->node.expires;
		__entry->period = timer->period;
	),

	TP_printk("RTC timer:(%p) expires:%lld period:%lld",
		  __entry->timer, __entry->expires, __entry->period
	)
);

DEFINE_EVENT(rtc_timer_class, rtc_timer_enqueue,

	TP_PROTO(struct rtc_timer *timer),

	TP_ARGS(timer)
);

DEFINE_EVENT(rtc_timer_class, rtc_timer_dequeue,

	TP_PROTO(struct rtc_timer *timer),

	TP_ARGS(timer)
);

DEFINE_EVENT(rtc_timer_class, rtc_timer_fired,

	TP_PROTO(struct rtc_timer *timer),

	TP_ARGS(timer)
);

#endif /* _TRACE_RTC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
