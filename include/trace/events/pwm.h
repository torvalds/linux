/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pwm

#if !defined(_TRACE_PWM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PWM_H

#include <linux/pwm.h>
#include <linux/tracepoint.h>

#define TP_PROTO_pwm(args...)							\
	TP_PROTO(struct pwm_device *pwm, args)

#define TP_ARGS_pwm(args...)							\
	TP_ARGS(pwm, args)

#define TP_STRUCT__entry_pwm(args...)						\
	TP_STRUCT__entry(							\
		__field(unsigned int, chipid)					\
		__field(unsigned int, hwpwm)					\
		args)

#define TP_fast_assign_pwm(args...)						\
	TP_fast_assign(								\
		__entry->chipid = pwm->chip->id;				\
		__entry->hwpwm = pwm->hwpwm;					\
		args)

#define TP_printk_pwm(fmt, args...)						\
	TP_printk("pwmchip%u.%u: " fmt, __entry->chipid, __entry->hwpwm, args)

#define __field_pwmwf(wf)							\
	__field(u64, wf ## _period_length_ns)					\
	__field(u64, wf ## _duty_length_ns)					\
	__field(u64, wf ## _duty_offset_ns)					\

#define fast_assign_pwmwf(wf)							\
	__entry->wf ## _period_length_ns = wf->period_length_ns;		\
	__entry->wf ## _duty_length_ns = wf->duty_length_ns;			\
	__entry->wf ## _duty_offset_ns = wf->duty_offset_ns

#define printk_pwmwf_format(wf)							\
	"%lld/%lld [+%lld]"

#define printk_pwmwf_formatargs(wf)						\
	__entry->wf ## _duty_length_ns, __entry->wf ## _period_length_ns, __entry->wf ## _duty_offset_ns

TRACE_EVENT(pwm_round_waveform_tohw,

	TP_PROTO_pwm(const struct pwm_waveform *wf, void *wfhw, int err),

	TP_ARGS_pwm(wf, wfhw, err),

	TP_STRUCT__entry_pwm(
		__field_pwmwf(wf)
		__field(void *, wfhw)
		__field(int, err)
	),

	TP_fast_assign_pwm(
		fast_assign_pwmwf(wf);
		__entry->wfhw = wfhw;
		__entry->err = err;
	),

	TP_printk_pwm(printk_pwmwf_format(wf) " > %p err=%d",
		printk_pwmwf_formatargs(wf), __entry->wfhw, __entry->err)
);

TRACE_EVENT(pwm_round_waveform_fromhw,

	TP_PROTO_pwm(const void *wfhw, struct pwm_waveform *wf, int err),

	TP_ARGS_pwm(wfhw, wf, err),

	TP_STRUCT__entry_pwm(
		__field(const void *, wfhw)
		__field_pwmwf(wf)
		__field(int, err)
	),

	TP_fast_assign_pwm(
		__entry->wfhw = wfhw;
		fast_assign_pwmwf(wf);
		__entry->err = err;
	),

	TP_printk_pwm("%p > " printk_pwmwf_format(wf) " err=%d",
		__entry->wfhw, printk_pwmwf_formatargs(wf), __entry->err)
);

TRACE_EVENT(pwm_read_waveform,

	TP_PROTO_pwm(void *wfhw, int err),

	TP_ARGS_pwm(wfhw, err),

	TP_STRUCT__entry_pwm(
		__field(void *, wfhw)
		__field(int, err)
	),

	TP_fast_assign_pwm(
		__entry->wfhw = wfhw;
		__entry->err = err;
	),

	TP_printk_pwm("%p err=%d",
		__entry->wfhw, __entry->err)
);

TRACE_EVENT(pwm_write_waveform,

	TP_PROTO_pwm(const void *wfhw, int err),

	TP_ARGS_pwm(wfhw, err),

	TP_STRUCT__entry_pwm(
		__field(const void *, wfhw)
		__field(int, err)
	),

	TP_fast_assign_pwm(
		__entry->wfhw = wfhw;
		__entry->err = err;
	),

	TP_printk_pwm("%p err=%d",
		__entry->wfhw, __entry->err)
);


DECLARE_EVENT_CLASS(pwm,

	TP_PROTO(struct pwm_device *pwm, const struct pwm_state *state, int err),

	TP_ARGS(pwm, state, err),

	TP_STRUCT__entry_pwm(
		__field(u64, period)
		__field(u64, duty_cycle)
		__field(enum pwm_polarity, polarity)
		__field(bool, enabled)
		__field(int, err)
	),

	TP_fast_assign_pwm(
		__entry->period = state->period;
		__entry->duty_cycle = state->duty_cycle;
		__entry->polarity = state->polarity;
		__entry->enabled = state->enabled;
		__entry->err = err;
	),

	TP_printk_pwm("period=%llu duty_cycle=%llu polarity=%d enabled=%d err=%d",
		  __entry->period, __entry->duty_cycle,
		  __entry->polarity, __entry->enabled, __entry->err)

);

DEFINE_EVENT(pwm, pwm_apply,

	TP_PROTO(struct pwm_device *pwm, const struct pwm_state *state, int err),

	TP_ARGS(pwm, state, err)
);

DEFINE_EVENT(pwm, pwm_get,

	TP_PROTO(struct pwm_device *pwm, const struct pwm_state *state, int err),

	TP_ARGS(pwm, state, err)
);

#endif /* _TRACE_PWM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
