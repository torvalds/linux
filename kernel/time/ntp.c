// SPDX-License-Identifier: GPL-2.0
/*
 * NTP state machine interfaces and logic.
 *
 * This code was mainly moved from kernel/timer.c and kernel/time.c
 * Please see those files for relevant copyright info and historical
 * changelogs.
 */
#include <linux/capability.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/timex.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/audit.h>

#include "ntp_internal.h"
#include "timekeeping_internal.h"


/*
 * NTP timekeeping variables:
 *
 * Note: All of the NTP state is protected by the timekeeping locks.
 */


/* USER_HZ period (usecs): */
unsigned long			tick_usec = USER_TICK_USEC;

/* SHIFTED_HZ period (nsecs): */
unsigned long			tick_nsec;

static u64			tick_length;
static u64			tick_length_base;

#define SECS_PER_DAY		86400
#define MAX_TICKADJ		500LL		/* usecs */
#define MAX_TICKADJ_SCALED \
	(((MAX_TICKADJ * NSEC_PER_USEC) << NTP_SCALE_SHIFT) / NTP_INTERVAL_FREQ)
#define MAX_TAI_OFFSET		100000

/*
 * phase-lock loop variables
 */

/*
 * clock synchronization status
 *
 * (TIME_ERROR prevents overwriting the CMOS clock)
 */
static int			time_state = TIME_OK;

/* clock status bits:							*/
static int			time_status = STA_UNSYNC;

/* time adjustment (nsecs):						*/
static s64			time_offset;

/* pll time constant:							*/
static long			time_constant = 2;

/* maximum error (usecs):						*/
static long			time_maxerror = NTP_PHASE_LIMIT;

/* estimated error (usecs):						*/
static long			time_esterror = NTP_PHASE_LIMIT;

/* frequency offset (scaled nsecs/secs):				*/
static s64			time_freq;

/* time at last adjustment (secs):					*/
static time64_t		time_reftime;

static long			time_adjust;

/* constant (boot-param configurable) NTP tick adjustment (upscaled)	*/
static s64			ntp_tick_adj;

/* second value of the next pending leapsecond, or TIME64_MAX if no leap */
static time64_t			ntp_next_leap_sec = TIME64_MAX;

#ifdef CONFIG_NTP_PPS

/*
 * The following variables are used when a pulse-per-second (PPS) signal
 * is available. They establish the engineering parameters of the clock
 * discipline loop when controlled by the PPS signal.
 */
#define PPS_VALID	10	/* PPS signal watchdog max (s) */
#define PPS_POPCORN	4	/* popcorn spike threshold (shift) */
#define PPS_INTMIN	2	/* min freq interval (s) (shift) */
#define PPS_INTMAX	8	/* max freq interval (s) (shift) */
#define PPS_INTCOUNT	4	/* number of consecutive good intervals to
				   increase pps_shift or consecutive bad
				   intervals to decrease it */
#define PPS_MAXWANDER	100000	/* max PPS freq wander (ns/s) */

static int pps_valid;		/* signal watchdog counter */
static long pps_tf[3];		/* phase median filter */
static long pps_jitter;		/* current jitter (ns) */
static struct timespec64 pps_fbase; /* beginning of the last freq interval */
static int pps_shift;		/* current interval duration (s) (shift) */
static int pps_intcnt;		/* interval counter */
static s64 pps_freq;		/* frequency offset (scaled ns/s) */
static long pps_stabil;		/* current stability (scaled ns/s) */

/*
 * PPS signal quality monitors
 */
static long pps_calcnt;		/* calibration intervals */
static long pps_jitcnt;		/* jitter limit exceeded */
static long pps_stbcnt;		/* stability limit exceeded */
static long pps_errcnt;		/* calibration errors */


/* PPS kernel consumer compensates the whole phase error immediately.
 * Otherwise, reduce the offset by a fixed factor times the time constant.
 */
static inline s64 ntp_offset_chunk(s64 offset)
{
	if (time_status & STA_PPSTIME && time_status & STA_PPSSIGNAL)
		return offset;
	else
		return shift_right(offset, SHIFT_PLL + time_constant);
}

static inline void pps_reset_freq_interval(void)
{
	/* the PPS calibration interval may end
	   surprisingly early */
	pps_shift = PPS_INTMIN;
	pps_intcnt = 0;
}

/**
 * pps_clear - Clears the PPS state variables
 */
static inline void pps_clear(void)
{
	pps_reset_freq_interval();
	pps_tf[0] = 0;
	pps_tf[1] = 0;
	pps_tf[2] = 0;
	pps_fbase.tv_sec = pps_fbase.tv_nsec = 0;
	pps_freq = 0;
}

/* Decrease pps_valid to indicate that another second has passed since
 * the last PPS signal. When it reaches 0, indicate that PPS signal is
 * missing.
 */
static inline void pps_dec_valid(void)
{
	if (pps_valid > 0)
		pps_valid--;
	else {
		time_status &= ~(STA_PPSSIGNAL | STA_PPSJITTER |
				 STA_PPSWANDER | STA_PPSERROR);
		pps_clear();
	}
}

static inline void pps_set_freq(s64 freq)
{
	pps_freq = freq;
}

static inline int is_error_status(int status)
{
	return (status & (STA_UNSYNC|STA_CLOCKERR))
		/* PPS signal lost when either PPS time or
		 * PPS frequency synchronization requested
		 */
		|| ((status & (STA_PPSFREQ|STA_PPSTIME))
			&& !(status & STA_PPSSIGNAL))
		/* PPS jitter exceeded when
		 * PPS time synchronization requested */
		|| ((status & (STA_PPSTIME|STA_PPSJITTER))
			== (STA_PPSTIME|STA_PPSJITTER))
		/* PPS wander exceeded or calibration error when
		 * PPS frequency synchronization requested
		 */
		|| ((status & STA_PPSFREQ)
			&& (status & (STA_PPSWANDER|STA_PPSERROR)));
}

static inline void pps_fill_timex(struct __kernel_timex *txc)
{
	txc->ppsfreq	   = shift_right((pps_freq >> PPM_SCALE_INV_SHIFT) *
					 PPM_SCALE_INV, NTP_SCALE_SHIFT);
	txc->jitter	   = pps_jitter;
	if (!(time_status & STA_NANO))
		txc->jitter = pps_jitter / NSEC_PER_USEC;
	txc->shift	   = pps_shift;
	txc->stabil	   = pps_stabil;
	txc->jitcnt	   = pps_jitcnt;
	txc->calcnt	   = pps_calcnt;
	txc->errcnt	   = pps_errcnt;
	txc->stbcnt	   = pps_stbcnt;
}

#else /* !CONFIG_NTP_PPS */

static inline s64 ntp_offset_chunk(s64 offset)
{
	return shift_right(offset, SHIFT_PLL + time_constant);
}

static inline void pps_reset_freq_interval(void) {}
static inline void pps_clear(void) {}
static inline void pps_dec_valid(void) {}
static inline void pps_set_freq(s64 freq) {}

static inline int is_error_status(int status)
{
	return status & (STA_UNSYNC|STA_CLOCKERR);
}

static inline void pps_fill_timex(struct __kernel_timex *txc)
{
	/* PPS is not implemented, so these are zero */
	txc->ppsfreq	   = 0;
	txc->jitter	   = 0;
	txc->shift	   = 0;
	txc->stabil	   = 0;
	txc->jitcnt	   = 0;
	txc->calcnt	   = 0;
	txc->errcnt	   = 0;
	txc->stbcnt	   = 0;
}

#endif /* CONFIG_NTP_PPS */


/**
 * ntp_synced - Returns 1 if the NTP status is not UNSYNC
 *
 */
static inline int ntp_synced(void)
{
	return !(time_status & STA_UNSYNC);
}


/*
 * NTP methods:
 */

/*
 * Update (tick_length, tick_length_base, tick_nsec), based
 * on (tick_usec, ntp_tick_adj, time_freq):
 */
static void ntp_update_frequency(void)
{
	u64 second_length;
	u64 new_base;

	second_length		 = (u64)(tick_usec * NSEC_PER_USEC * USER_HZ)
						<< NTP_SCALE_SHIFT;

	second_length		+= ntp_tick_adj;
	second_length		+= time_freq;

	tick_nsec		 = div_u64(second_length, HZ) >> NTP_SCALE_SHIFT;
	new_base		 = div_u64(second_length, NTP_INTERVAL_FREQ);

	/*
	 * Don't wait for the next second_overflow, apply
	 * the change to the tick length immediately:
	 */
	tick_length		+= new_base - tick_length_base;
	tick_length_base	 = new_base;
}

static inline s64 ntp_update_offset_fll(s64 offset64, long secs)
{
	time_status &= ~STA_MODE;

	if (secs < MINSEC)
		return 0;

	if (!(time_status & STA_FLL) && (secs <= MAXSEC))
		return 0;

	time_status |= STA_MODE;

	return div64_long(offset64 << (NTP_SCALE_SHIFT - SHIFT_FLL), secs);
}

static void ntp_update_offset(long offset)
{
	s64 freq_adj;
	s64 offset64;
	long secs;

	if (!(time_status & STA_PLL))
		return;

	if (!(time_status & STA_NANO)) {
		/* Make sure the multiplication below won't overflow */
		offset = clamp(offset, -USEC_PER_SEC, USEC_PER_SEC);
		offset *= NSEC_PER_USEC;
	}

	/*
	 * Scale the phase adjustment and
	 * clamp to the operating range.
	 */
	offset = clamp(offset, -MAXPHASE, MAXPHASE);

	/*
	 * Select how the frequency is to be controlled
	 * and in which mode (PLL or FLL).
	 */
	secs = (long)(__ktime_get_real_seconds() - time_reftime);
	if (unlikely(time_status & STA_FREQHOLD))
		secs = 0;

	time_reftime = __ktime_get_real_seconds();

	offset64    = offset;
	freq_adj    = ntp_update_offset_fll(offset64, secs);

	/*
	 * Clamp update interval to reduce PLL gain with low
	 * sampling rate (e.g. intermittent network connection)
	 * to avoid instability.
	 */
	if (unlikely(secs > 1 << (SHIFT_PLL + 1 + time_constant)))
		secs = 1 << (SHIFT_PLL + 1 + time_constant);

	freq_adj    += (offset64 * secs) <<
			(NTP_SCALE_SHIFT - 2 * (SHIFT_PLL + 2 + time_constant));

	freq_adj    = min(freq_adj + time_freq, MAXFREQ_SCALED);

	time_freq   = max(freq_adj, -MAXFREQ_SCALED);

	time_offset = div_s64(offset64 << NTP_SCALE_SHIFT, NTP_INTERVAL_FREQ);
}

/**
 * ntp_clear - Clears the NTP state variables
 */
void ntp_clear(void)
{
	time_adjust	= 0;		/* stop active adjtime() */
	time_status	|= STA_UNSYNC;
	time_maxerror	= NTP_PHASE_LIMIT;
	time_esterror	= NTP_PHASE_LIMIT;

	ntp_update_frequency();

	tick_length	= tick_length_base;
	time_offset	= 0;

	ntp_next_leap_sec = TIME64_MAX;
	/* Clear PPS state variables */
	pps_clear();
}


u64 ntp_tick_length(void)
{
	return tick_length;
}

/**
 * ntp_get_next_leap - Returns the next leapsecond in CLOCK_REALTIME ktime_t
 *
 * Provides the time of the next leapsecond against CLOCK_REALTIME in
 * a ktime_t format. Returns KTIME_MAX if no leapsecond is pending.
 */
ktime_t ntp_get_next_leap(void)
{
	ktime_t ret;

	if ((time_state == TIME_INS) && (time_status & STA_INS))
		return ktime_set(ntp_next_leap_sec, 0);
	ret = KTIME_MAX;
	return ret;
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 * Also handles leap second processing, and returns leap offset
 */
int second_overflow(time64_t secs)
{
	s64 delta;
	int leap = 0;
	s32 rem;

	/*
	 * Leap second processing. If in leap-insert state at the end of the
	 * day, the system clock is set back one second; if in leap-delete
	 * state, the system clock is set ahead one second.
	 */
	switch (time_state) {
	case TIME_OK:
		if (time_status & STA_INS) {
			time_state = TIME_INS;
			div_s64_rem(secs, SECS_PER_DAY, &rem);
			ntp_next_leap_sec = secs + SECS_PER_DAY - rem;
		} else if (time_status & STA_DEL) {
			time_state = TIME_DEL;
			div_s64_rem(secs + 1, SECS_PER_DAY, &rem);
			ntp_next_leap_sec = secs + SECS_PER_DAY - rem;
		}
		break;
	case TIME_INS:
		if (!(time_status & STA_INS)) {
			ntp_next_leap_sec = TIME64_MAX;
			time_state = TIME_OK;
		} else if (secs == ntp_next_leap_sec) {
			leap = -1;
			time_state = TIME_OOP;
			printk(KERN_NOTICE
				"Clock: inserting leap second 23:59:60 UTC\n");
		}
		break;
	case TIME_DEL:
		if (!(time_status & STA_DEL)) {
			ntp_next_leap_sec = TIME64_MAX;
			time_state = TIME_OK;
		} else if (secs == ntp_next_leap_sec) {
			leap = 1;
			ntp_next_leap_sec = TIME64_MAX;
			time_state = TIME_WAIT;
			printk(KERN_NOTICE
				"Clock: deleting leap second 23:59:59 UTC\n");
		}
		break;
	case TIME_OOP:
		ntp_next_leap_sec = TIME64_MAX;
		time_state = TIME_WAIT;
		break;
	case TIME_WAIT:
		if (!(time_status & (STA_INS | STA_DEL)))
			time_state = TIME_OK;
		break;
	}


	/* Bump the maxerror field */
	time_maxerror += MAXFREQ / NSEC_PER_USEC;
	if (time_maxerror > NTP_PHASE_LIMIT) {
		time_maxerror = NTP_PHASE_LIMIT;
		time_status |= STA_UNSYNC;
	}

	/* Compute the phase adjustment for the next second */
	tick_length	 = tick_length_base;

	delta		 = ntp_offset_chunk(time_offset);
	time_offset	-= delta;
	tick_length	+= delta;

	/* Check PPS signal */
	pps_dec_valid();

	if (!time_adjust)
		goto out;

	if (time_adjust > MAX_TICKADJ) {
		time_adjust -= MAX_TICKADJ;
		tick_length += MAX_TICKADJ_SCALED;
		goto out;
	}

	if (time_adjust < -MAX_TICKADJ) {
		time_adjust += MAX_TICKADJ;
		tick_length -= MAX_TICKADJ_SCALED;
		goto out;
	}

	tick_length += (s64)(time_adjust * NSEC_PER_USEC / NTP_INTERVAL_FREQ)
							 << NTP_SCALE_SHIFT;
	time_adjust = 0;

out:
	return leap;
}

#if defined(CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC)
static void sync_hw_clock(struct work_struct *work);
static DECLARE_WORK(sync_work, sync_hw_clock);
static struct hrtimer sync_hrtimer;
#define SYNC_PERIOD_NS (11ULL * 60 * NSEC_PER_SEC)

static enum hrtimer_restart sync_timer_callback(struct hrtimer *timer)
{
	queue_work(system_power_efficient_wq, &sync_work);

	return HRTIMER_NORESTART;
}

static void sched_sync_hw_clock(unsigned long offset_nsec, bool retry)
{
	ktime_t exp = ktime_set(ktime_get_real_seconds(), 0);

	if (retry)
		exp = ktime_add_ns(exp, 2ULL * NSEC_PER_SEC - offset_nsec);
	else
		exp = ktime_add_ns(exp, SYNC_PERIOD_NS - offset_nsec);

	hrtimer_start(&sync_hrtimer, exp, HRTIMER_MODE_ABS);
}

/*
 * Check whether @now is correct versus the required time to update the RTC
 * and calculate the value which needs to be written to the RTC so that the
 * next seconds increment of the RTC after the write is aligned with the next
 * seconds increment of clock REALTIME.
 *
 * tsched     t1 write(t2.tv_sec - 1sec))	t2 RTC increments seconds
 *
 * t2.tv_nsec == 0
 * tsched = t2 - set_offset_nsec
 * newval = t2 - NSEC_PER_SEC
 *
 * ==> neval = tsched + set_offset_nsec - NSEC_PER_SEC
 *
 * As the execution of this code is not guaranteed to happen exactly at
 * tsched this allows it to happen within a fuzzy region:
 *
 *	abs(now - tsched) < FUZZ
 *
 * If @now is not inside the allowed window the function returns false.
 */
static inline bool rtc_tv_nsec_ok(unsigned long set_offset_nsec,
				  struct timespec64 *to_set,
				  const struct timespec64 *now)
{
	/* Allowed error in tv_nsec, arbitarily set to 5 jiffies in ns. */
	const unsigned long TIME_SET_NSEC_FUZZ = TICK_NSEC * 5;
	struct timespec64 delay = {.tv_sec = -1,
				   .tv_nsec = set_offset_nsec};

	*to_set = timespec64_add(*now, delay);

	if (to_set->tv_nsec < TIME_SET_NSEC_FUZZ) {
		to_set->tv_nsec = 0;
		return true;
	}

	if (to_set->tv_nsec > NSEC_PER_SEC - TIME_SET_NSEC_FUZZ) {
		to_set->tv_sec++;
		to_set->tv_nsec = 0;
		return true;
	}
	return false;
}

#ifdef CONFIG_GENERIC_CMOS_UPDATE
int __weak update_persistent_clock64(struct timespec64 now64)
{
	return -ENODEV;
}
#else
static inline int update_persistent_clock64(struct timespec64 now64)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_RTC_SYSTOHC
/* Save NTP synchronized time to the RTC */
static int update_rtc(struct timespec64 *to_set, unsigned long *offset_nsec)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	int err = -ENODEV;

	rtc = rtc_class_open(CONFIG_RTC_SYSTOHC_DEVICE);
	if (!rtc)
		return -ENODEV;

	if (!rtc->ops || !rtc->ops->set_time)
		goto out_close;

	/* First call might not have the correct offset */
	if (*offset_nsec == rtc->set_offset_nsec) {
		rtc_time64_to_tm(to_set->tv_sec, &tm);
		err = rtc_set_time(rtc, &tm);
	} else {
		/* Store the update offset and let the caller try again */
		*offset_nsec = rtc->set_offset_nsec;
		err = -EAGAIN;
	}
out_close:
	rtc_class_close(rtc);
	return err;
}
#else
static inline int update_rtc(struct timespec64 *to_set, unsigned long *offset_nsec)
{
	return -ENODEV;
}
#endif

/*
 * If we have an externally synchronized Linux clock, then update RTC clock
 * accordingly every ~11 minutes. Generally RTCs can only store second
 * precision, but many RTCs will adjust the phase of their second tick to
 * match the moment of update. This infrastructure arranges to call to the RTC
 * set at the correct moment to phase synchronize the RTC second tick over
 * with the kernel clock.
 */
static void sync_hw_clock(struct work_struct *work)
{
	/*
	 * The default synchronization offset is 500ms for the deprecated
	 * update_persistent_clock64() under the assumption that it uses
	 * the infamous CMOS clock (MC146818).
	 */
	static unsigned long offset_nsec = NSEC_PER_SEC / 2;
	struct timespec64 now, to_set;
	int res = -EAGAIN;

	/*
	 * Don't update if STA_UNSYNC is set and if ntp_notify_cmos_timer()
	 * managed to schedule the work between the timer firing and the
	 * work being able to rearm the timer. Wait for the timer to expire.
	 */
	if (!ntp_synced() || hrtimer_is_queued(&sync_hrtimer))
		return;

	ktime_get_real_ts64(&now);
	/* If @now is not in the allowed window, try again */
	if (!rtc_tv_nsec_ok(offset_nsec, &to_set, &now))
		goto rearm;

	/* Take timezone adjusted RTCs into account */
	if (persistent_clock_is_local)
		to_set.tv_sec -= (sys_tz.tz_minuteswest * 60);

	/* Try the legacy RTC first. */
	res = update_persistent_clock64(to_set);
	if (res != -ENODEV)
		goto rearm;

	/* Try the RTC class */
	res = update_rtc(&to_set, &offset_nsec);
	if (res == -ENODEV)
		return;
rearm:
	sched_sync_hw_clock(offset_nsec, res != 0);
}

void ntp_notify_cmos_timer(void)
{
	/*
	 * When the work is currently executed but has not yet the timer
	 * rearmed this queues the work immediately again. No big issue,
	 * just a pointless work scheduled.
	 */
	if (ntp_synced() && !hrtimer_is_queued(&sync_hrtimer))
		queue_work(system_power_efficient_wq, &sync_work);
}

static void __init ntp_init_cmos_sync(void)
{
	hrtimer_init(&sync_hrtimer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	sync_hrtimer.function = sync_timer_callback;
}
#else /* CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC) */
static inline void __init ntp_init_cmos_sync(void) { }
#endif /* !CONFIG_GENERIC_CMOS_UPDATE) || defined(CONFIG_RTC_SYSTOHC) */

/*
 * Propagate a new txc->status value into the NTP state:
 */
static inline void process_adj_status(const struct __kernel_timex *txc)
{
	if ((time_status & STA_PLL) && !(txc->status & STA_PLL)) {
		time_state = TIME_OK;
		time_status = STA_UNSYNC;
		ntp_next_leap_sec = TIME64_MAX;
		/* restart PPS frequency calibration */
		pps_reset_freq_interval();
	}

	/*
	 * If we turn on PLL adjustments then reset the
	 * reference time to current time.
	 */
	if (!(time_status & STA_PLL) && (txc->status & STA_PLL))
		time_reftime = __ktime_get_real_seconds();

	/* only set allowed bits */
	time_status &= STA_RONLY;
	time_status |= txc->status & ~STA_RONLY;
}


static inline void process_adjtimex_modes(const struct __kernel_timex *txc,
					  s32 *time_tai)
{
	if (txc->modes & ADJ_STATUS)
		process_adj_status(txc);

	if (txc->modes & ADJ_NANO)
		time_status |= STA_NANO;

	if (txc->modes & ADJ_MICRO)
		time_status &= ~STA_NANO;

	if (txc->modes & ADJ_FREQUENCY) {
		time_freq = txc->freq * PPM_SCALE;
		time_freq = min(time_freq, MAXFREQ_SCALED);
		time_freq = max(time_freq, -MAXFREQ_SCALED);
		/* update pps_freq */
		pps_set_freq(time_freq);
	}

	if (txc->modes & ADJ_MAXERROR)
		time_maxerror = txc->maxerror;

	if (txc->modes & ADJ_ESTERROR)
		time_esterror = txc->esterror;

	if (txc->modes & ADJ_TIMECONST) {
		time_constant = txc->constant;
		if (!(time_status & STA_NANO))
			time_constant += 4;
		time_constant = min(time_constant, (long)MAXTC);
		time_constant = max(time_constant, 0l);
	}

	if (txc->modes & ADJ_TAI &&
			txc->constant >= 0 && txc->constant <= MAX_TAI_OFFSET)
		*time_tai = txc->constant;

	if (txc->modes & ADJ_OFFSET)
		ntp_update_offset(txc->offset);

	if (txc->modes & ADJ_TICK)
		tick_usec = txc->tick;

	if (txc->modes & (ADJ_TICK|ADJ_FREQUENCY|ADJ_OFFSET))
		ntp_update_frequency();
}


/*
 * adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
int __do_adjtimex(struct __kernel_timex *txc, const struct timespec64 *ts,
		  s32 *time_tai, struct audit_ntp_data *ad)
{
	int result;

	if (txc->modes & ADJ_ADJTIME) {
		long save_adjust = time_adjust;

		if (!(txc->modes & ADJ_OFFSET_READONLY)) {
			/* adjtime() is independent from ntp_adjtime() */
			time_adjust = txc->offset;
			ntp_update_frequency();

			audit_ntp_set_old(ad, AUDIT_NTP_ADJUST,	save_adjust);
			audit_ntp_set_new(ad, AUDIT_NTP_ADJUST,	time_adjust);
		}
		txc->offset = save_adjust;
	} else {
		/* If there are input parameters, then process them: */
		if (txc->modes) {
			audit_ntp_set_old(ad, AUDIT_NTP_OFFSET,	time_offset);
			audit_ntp_set_old(ad, AUDIT_NTP_FREQ,	time_freq);
			audit_ntp_set_old(ad, AUDIT_NTP_STATUS,	time_status);
			audit_ntp_set_old(ad, AUDIT_NTP_TAI,	*time_tai);
			audit_ntp_set_old(ad, AUDIT_NTP_TICK,	tick_usec);

			process_adjtimex_modes(txc, time_tai);

			audit_ntp_set_new(ad, AUDIT_NTP_OFFSET,	time_offset);
			audit_ntp_set_new(ad, AUDIT_NTP_FREQ,	time_freq);
			audit_ntp_set_new(ad, AUDIT_NTP_STATUS,	time_status);
			audit_ntp_set_new(ad, AUDIT_NTP_TAI,	*time_tai);
			audit_ntp_set_new(ad, AUDIT_NTP_TICK,	tick_usec);
		}

		txc->offset = shift_right(time_offset * NTP_INTERVAL_FREQ,
				  NTP_SCALE_SHIFT);
		if (!(time_status & STA_NANO))
			txc->offset = (u32)txc->offset / NSEC_PER_USEC;
	}

	result = time_state;	/* mostly `TIME_OK' */
	/* check for errors */
	if (is_error_status(time_status))
		result = TIME_ERROR;

	txc->freq	   = shift_right((time_freq >> PPM_SCALE_INV_SHIFT) *
					 PPM_SCALE_INV, NTP_SCALE_SHIFT);
	txc->maxerror	   = time_maxerror;
	txc->esterror	   = time_esterror;
	txc->status	   = time_status;
	txc->constant	   = time_constant;
	txc->precision	   = 1;
	txc->tolerance	   = MAXFREQ_SCALED / PPM_SCALE;
	txc->tick	   = tick_usec;
	txc->tai	   = *time_tai;

	/* fill PPS status fields */
	pps_fill_timex(txc);

	txc->time.tv_sec = ts->tv_sec;
	txc->time.tv_usec = ts->tv_nsec;
	if (!(time_status & STA_NANO))
		txc->time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;

	/* Handle leapsec adjustments */
	if (unlikely(ts->tv_sec >= ntp_next_leap_sec)) {
		if ((time_state == TIME_INS) && (time_status & STA_INS)) {
			result = TIME_OOP;
			txc->tai++;
			txc->time.tv_sec--;
		}
		if ((time_state == TIME_DEL) && (time_status & STA_DEL)) {
			result = TIME_WAIT;
			txc->tai--;
			txc->time.tv_sec++;
		}
		if ((time_state == TIME_OOP) &&
					(ts->tv_sec == ntp_next_leap_sec)) {
			result = TIME_WAIT;
		}
	}

	return result;
}

#ifdef	CONFIG_NTP_PPS

/* actually struct pps_normtime is good old struct timespec, but it is
 * semantically different (and it is the reason why it was invented):
 * pps_normtime.nsec has a range of ( -NSEC_PER_SEC / 2, NSEC_PER_SEC / 2 ]
 * while timespec.tv_nsec has a range of [0, NSEC_PER_SEC) */
struct pps_normtime {
	s64		sec;	/* seconds */
	long		nsec;	/* nanoseconds */
};

/* normalize the timestamp so that nsec is in the
   ( -NSEC_PER_SEC / 2, NSEC_PER_SEC / 2 ] interval */
static inline struct pps_normtime pps_normalize_ts(struct timespec64 ts)
{
	struct pps_normtime norm = {
		.sec = ts.tv_sec,
		.nsec = ts.tv_nsec
	};

	if (norm.nsec > (NSEC_PER_SEC >> 1)) {
		norm.nsec -= NSEC_PER_SEC;
		norm.sec++;
	}

	return norm;
}

/* get current phase correction and jitter */
static inline long pps_phase_filter_get(long *jitter)
{
	*jitter = pps_tf[0] - pps_tf[1];
	if (*jitter < 0)
		*jitter = -*jitter;

	/* TODO: test various filters */
	return pps_tf[0];
}

/* add the sample to the phase filter */
static inline void pps_phase_filter_add(long err)
{
	pps_tf[2] = pps_tf[1];
	pps_tf[1] = pps_tf[0];
	pps_tf[0] = err;
}

/* decrease frequency calibration interval length.
 * It is halved after four consecutive unstable intervals.
 */
static inline void pps_dec_freq_interval(void)
{
	if (--pps_intcnt <= -PPS_INTCOUNT) {
		pps_intcnt = -PPS_INTCOUNT;
		if (pps_shift > PPS_INTMIN) {
			pps_shift--;
			pps_intcnt = 0;
		}
	}
}

/* increase frequency calibration interval length.
 * It is doubled after four consecutive stable intervals.
 */
static inline void pps_inc_freq_interval(void)
{
	if (++pps_intcnt >= PPS_INTCOUNT) {
		pps_intcnt = PPS_INTCOUNT;
		if (pps_shift < PPS_INTMAX) {
			pps_shift++;
			pps_intcnt = 0;
		}
	}
}

/* update clock frequency based on MONOTONIC_RAW clock PPS signal
 * timestamps
 *
 * At the end of the calibration interval the difference between the
 * first and last MONOTONIC_RAW clock timestamps divided by the length
 * of the interval becomes the frequency update. If the interval was
 * too long, the data are discarded.
 * Returns the difference between old and new frequency values.
 */
static long hardpps_update_freq(struct pps_normtime freq_norm)
{
	long delta, delta_mod;
	s64 ftemp;

	/* check if the frequency interval was too long */
	if (freq_norm.sec > (2 << pps_shift)) {
		time_status |= STA_PPSERROR;
		pps_errcnt++;
		pps_dec_freq_interval();
		printk_deferred(KERN_ERR
			"hardpps: PPSERROR: interval too long - %lld s\n",
			freq_norm.sec);
		return 0;
	}

	/* here the raw frequency offset and wander (stability) is
	 * calculated. If the wander is less than the wander threshold
	 * the interval is increased; otherwise it is decreased.
	 */
	ftemp = div_s64(((s64)(-freq_norm.nsec)) << NTP_SCALE_SHIFT,
			freq_norm.sec);
	delta = shift_right(ftemp - pps_freq, NTP_SCALE_SHIFT);
	pps_freq = ftemp;
	if (delta > PPS_MAXWANDER || delta < -PPS_MAXWANDER) {
		printk_deferred(KERN_WARNING
				"hardpps: PPSWANDER: change=%ld\n", delta);
		time_status |= STA_PPSWANDER;
		pps_stbcnt++;
		pps_dec_freq_interval();
	} else {	/* good sample */
		pps_inc_freq_interval();
	}

	/* the stability metric is calculated as the average of recent
	 * frequency changes, but is used only for performance
	 * monitoring
	 */
	delta_mod = delta;
	if (delta_mod < 0)
		delta_mod = -delta_mod;
	pps_stabil += (div_s64(((s64)delta_mod) <<
				(NTP_SCALE_SHIFT - SHIFT_USEC),
				NSEC_PER_USEC) - pps_stabil) >> PPS_INTMIN;

	/* if enabled, the system clock frequency is updated */
	if ((time_status & STA_PPSFREQ) != 0 &&
	    (time_status & STA_FREQHOLD) == 0) {
		time_freq = pps_freq;
		ntp_update_frequency();
	}

	return delta;
}

/* correct REALTIME clock phase error against PPS signal */
static void hardpps_update_phase(long error)
{
	long correction = -error;
	long jitter;

	/* add the sample to the median filter */
	pps_phase_filter_add(correction);
	correction = pps_phase_filter_get(&jitter);

	/* Nominal jitter is due to PPS signal noise. If it exceeds the
	 * threshold, the sample is discarded; otherwise, if so enabled,
	 * the time offset is updated.
	 */
	if (jitter > (pps_jitter << PPS_POPCORN)) {
		printk_deferred(KERN_WARNING
				"hardpps: PPSJITTER: jitter=%ld, limit=%ld\n",
				jitter, (pps_jitter << PPS_POPCORN));
		time_status |= STA_PPSJITTER;
		pps_jitcnt++;
	} else if (time_status & STA_PPSTIME) {
		/* correct the time using the phase offset */
		time_offset = div_s64(((s64)correction) << NTP_SCALE_SHIFT,
				NTP_INTERVAL_FREQ);
		/* cancel running adjtime() */
		time_adjust = 0;
	}
	/* update jitter */
	pps_jitter += (jitter - pps_jitter) >> PPS_INTMIN;
}

/*
 * __hardpps() - discipline CPU clock oscillator to external PPS signal
 *
 * This routine is called at each PPS signal arrival in order to
 * discipline the CPU clock oscillator to the PPS signal. It takes two
 * parameters: REALTIME and MONOTONIC_RAW clock timestamps. The former
 * is used to correct clock phase error and the latter is used to
 * correct the frequency.
 *
 * This code is based on David Mills's reference nanokernel
 * implementation. It was mostly rewritten but keeps the same idea.
 */
void __hardpps(const struct timespec64 *phase_ts, const struct timespec64 *raw_ts)
{
	struct pps_normtime pts_norm, freq_norm;

	pts_norm = pps_normalize_ts(*phase_ts);

	/* clear the error bits, they will be set again if needed */
	time_status &= ~(STA_PPSJITTER | STA_PPSWANDER | STA_PPSERROR);

	/* indicate signal presence */
	time_status |= STA_PPSSIGNAL;
	pps_valid = PPS_VALID;

	/* when called for the first time,
	 * just start the frequency interval */
	if (unlikely(pps_fbase.tv_sec == 0)) {
		pps_fbase = *raw_ts;
		return;
	}

	/* ok, now we have a base for frequency calculation */
	freq_norm = pps_normalize_ts(timespec64_sub(*raw_ts, pps_fbase));

	/* check that the signal is in the range
	 * [1s - MAXFREQ us, 1s + MAXFREQ us], otherwise reject it */
	if ((freq_norm.sec == 0) ||
			(freq_norm.nsec > MAXFREQ * freq_norm.sec) ||
			(freq_norm.nsec < -MAXFREQ * freq_norm.sec)) {
		time_status |= STA_PPSJITTER;
		/* restart the frequency calibration interval */
		pps_fbase = *raw_ts;
		printk_deferred(KERN_ERR "hardpps: PPSJITTER: bad pulse\n");
		return;
	}

	/* signal is ok */

	/* check if the current frequency interval is finished */
	if (freq_norm.sec >= (1 << pps_shift)) {
		pps_calcnt++;
		/* restart the frequency calibration interval */
		pps_fbase = *raw_ts;
		hardpps_update_freq(freq_norm);
	}

	hardpps_update_phase(pts_norm.nsec);

}
#endif	/* CONFIG_NTP_PPS */

static int __init ntp_tick_adj_setup(char *str)
{
	int rc = kstrtos64(str, 0, &ntp_tick_adj);
	if (rc)
		return rc;

	ntp_tick_adj <<= NTP_SCALE_SHIFT;
	return 1;
}

__setup("ntp_tick_adj=", ntp_tick_adj_setup);

void __init ntp_init(void)
{
	ntp_clear();
	ntp_init_cmos_sync();
}
