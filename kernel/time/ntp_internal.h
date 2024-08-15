/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NTP_INTERNAL_H
#define _LINUX_NTP_INTERNAL_H

extern void ntp_init(void);
extern void ntp_clear(void);
/* Returns how long ticks are at present, in ns / 2^NTP_SCALE_SHIFT. */
extern u64 ntp_tick_length(void);
extern ktime_t ntp_get_next_leap(void);
extern int second_overflow(time64_t secs);
extern int __do_adjtimex(struct __kernel_timex *txc,
			 const struct timespec64 *ts,
			 s32 *time_tai, struct audit_ntp_data *ad);
extern void __hardpps(const struct timespec64 *phase_ts, const struct timespec64 *raw_ts);

#if defined(CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC)
extern void ntp_notify_cmos_timer(void);
#else
static inline void ntp_notify_cmos_timer(void) { }
#endif

#endif /* _LINUX_NTP_INTERNAL_H */
