/*
 * linux/kernel/time/ntp.c
 *
 * NTP state machine interfaces and logic.
 *
 * This code was mainly moved from kernel/timer.c and kernel/time.c
 * Please see those files for relevant copyright info and historical
 * changelogs.
 */

#include <linux/mm.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/capability.h>
#include <linux/math64.h>
#include <linux/clocksource.h>
#include <linux/workqueue.h>
#include <asm/timex.h>

/*
 * Timekeeping variables
 */
unsigned long tick_usec = TICK_USEC; 		/* USER_HZ period (usec) */
unsigned long tick_nsec;			/* ACTHZ period (nsec) */
u64 tick_length;
static u64 tick_length_base;

static struct hrtimer leap_timer;

#define MAX_TICKADJ		500		/* microsecs */
#define MAX_TICKADJ_SCALED	(((u64)(MAX_TICKADJ * NSEC_PER_USEC) << \
				  NTP_SCALE_SHIFT) / NTP_INTERVAL_FREQ)

/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
static int time_state = TIME_OK;	/* clock synchronization status	*/
int time_status = STA_UNSYNC;		/* clock status bits		*/
static long time_tai;			/* TAI offset (s)		*/
static s64 time_offset;			/* time adjustment (ns)		*/
static long time_constant = 2;		/* pll time constant		*/
long time_maxerror = NTP_PHASE_LIMIT;	/* maximum error (us)		*/
long time_esterror = NTP_PHASE_LIMIT;	/* estimated error (us)		*/
static s64 time_freq;			/* frequency offset (scaled ns/s)*/
static long time_reftime;		/* time at last adjustment (s)	*/
long time_adjust;
static long ntp_tick_adj;

static void ntp_update_frequency(void)
{
	u64 second_length = (u64)(tick_usec * NSEC_PER_USEC * USER_HZ)
				<< NTP_SCALE_SHIFT;
	second_length += (s64)ntp_tick_adj << NTP_SCALE_SHIFT;
	second_length += time_freq;

	tick_length_base = second_length;

	tick_nsec = div_u64(second_length, HZ) >> NTP_SCALE_SHIFT;
	tick_length_base = div_u64(tick_length_base, NTP_INTERVAL_FREQ);
}

static void ntp_update_offset(long offset)
{
	long mtemp;
	s64 freq_adj;

	if (!(time_status & STA_PLL))
		return;

	if (!(time_status & STA_NANO))
		offset *= NSEC_PER_USEC;

	/*
	 * Scale the phase adjustment and
	 * clamp to the operating range.
	 */
	offset = min(offset, MAXPHASE);
	offset = max(offset, -MAXPHASE);

	/*
	 * Select how the frequency is to be controlled
	 * and in which mode (PLL or FLL).
	 */
	if (time_status & STA_FREQHOLD || time_reftime == 0)
		time_reftime = xtime.tv_sec;
	mtemp = xtime.tv_sec - time_reftime;
	time_reftime = xtime.tv_sec;

	freq_adj = (s64)offset * mtemp;
	freq_adj <<= NTP_SCALE_SHIFT - 2 * (SHIFT_PLL + 2 + time_constant);
	time_status &= ~STA_MODE;
	if (mtemp >= MINSEC && (time_status & STA_FLL || mtemp > MAXSEC)) {
		freq_adj += div_s64((s64)offset << (NTP_SCALE_SHIFT - SHIFT_FLL),
				    mtemp);
		time_status |= STA_MODE;
	}
	freq_adj += time_freq;
	freq_adj = min(freq_adj, MAXFREQ_SCALED);
	time_freq = max(freq_adj, -MAXFREQ_SCALED);

	time_offset = div_s64((s64)offset << NTP_SCALE_SHIFT, NTP_INTERVAL_FREQ);
}

/**
 * ntp_clear - Clears the NTP state variables
 *
 * Must be called while holding a write on the xtime_lock
 */
void ntp_clear(void)
{
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;

	ntp_update_frequency();

	tick_length = tick_length_base;
	time_offset = 0;
}

/*
 * Leap second processing. If in leap-insert state at the end of the
 * day, the system clock is set back one second; if in leap-delete
 * state, the system clock is set ahead one second.
 */
static enum hrtimer_restart ntp_leap_second(struct hrtimer *timer)
{
	enum hrtimer_restart res = HRTIMER_NORESTART;

	write_seqlock_irq(&xtime_lock);

	switch (time_state) {
	case TIME_OK:
		break;
	case TIME_INS:
		xtime.tv_sec--;
		wall_to_monotonic.tv_sec++;
		time_state = TIME_OOP;
		printk(KERN_NOTICE "Clock: "
		       "inserting leap second 23:59:60 UTC\n");
		leap_timer.expires = ktime_add_ns(leap_timer.expires,
						  NSEC_PER_SEC);
		res = HRTIMER_RESTART;
		break;
	case TIME_DEL:
		xtime.tv_sec++;
		time_tai--;
		wall_to_monotonic.tv_sec--;
		time_state = TIME_WAIT;
		printk(KERN_NOTICE "Clock: "
		       "deleting leap second 23:59:59 UTC\n");
		break;
	case TIME_OOP:
		time_tai++;
		time_state = TIME_WAIT;
		/* fall through */
	case TIME_WAIT:
		if (!(time_status & (STA_INS | STA_DEL)))
			time_state = TIME_OK;
		break;
	}
	update_vsyscall(&xtime, clock);

	write_sequnlock_irq(&xtime_lock);

	return res;
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 */
void second_overflow(void)
{
	s64 time_adj;

	/* Bump the maxerror field */
	time_maxerror += MAXFREQ / NSEC_PER_USEC;
	if (time_maxerror > NTP_PHASE_LIMIT) {
		time_maxerror = NTP_PHASE_LIMIT;
		time_status |= STA_UNSYNC;
	}

	/*
	 * Compute the phase adjustment for the next second. The offset is
	 * reduced by a fixed factor times the time constant.
	 */
	tick_length = tick_length_base;
	time_adj = shift_right(time_offset, SHIFT_PLL + time_constant);
	time_offset -= time_adj;
	tick_length += time_adj;

	if (unlikely(time_adjust)) {
		if (time_adjust > MAX_TICKADJ) {
			time_adjust -= MAX_TICKADJ;
			tick_length += MAX_TICKADJ_SCALED;
		} else if (time_adjust < -MAX_TICKADJ) {
			time_adjust += MAX_TICKADJ;
			tick_length -= MAX_TICKADJ_SCALED;
		} else {
			tick_length += (s64)(time_adjust * NSEC_PER_USEC /
					NTP_INTERVAL_FREQ) << NTP_SCALE_SHIFT;
			time_adjust = 0;
		}
	}
}

#ifdef CONFIG_GENERIC_CMOS_UPDATE

/* Disable the cmos update - used by virtualization and embedded */
int no_sync_cmos_clock  __read_mostly;

static void sync_cmos_clock(struct work_struct *work);

static DECLARE_DELAYED_WORK(sync_cmos_work, sync_cmos_clock);

static void sync_cmos_clock(struct work_struct *work)
{
	struct timespec now, next;
	int fail = 1;

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 * This code is run on a timer.  If the clock is set, that timer
	 * may not expire at the correct time.  Thus, we adjust...
	 */
	if (!ntp_synced())
		/*
		 * Not synced, exit, do not restart a timer (if one is
		 * running, let it run out).
		 */
		return;

	getnstimeofday(&now);
	if (abs(now.tv_nsec - (NSEC_PER_SEC / 2)) <= tick_nsec / 2)
		fail = update_persistent_clock(now);

	next.tv_nsec = (NSEC_PER_SEC / 2) - now.tv_nsec;
	if (next.tv_nsec <= 0)
		next.tv_nsec += NSEC_PER_SEC;

	if (!fail)
		next.tv_sec = 659;
	else
		next.tv_sec = 0;

	if (next.tv_nsec >= NSEC_PER_SEC) {
		next.tv_sec++;
		next.tv_nsec -= NSEC_PER_SEC;
	}
	schedule_delayed_work(&sync_cmos_work, timespec_to_jiffies(&next));
}

static void notify_cmos_timer(void)
{
	if (!no_sync_cmos_clock)
		schedule_delayed_work(&sync_cmos_work, 0);
}

#else
static inline void notify_cmos_timer(void) { }
#endif

/* adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
int do_adjtimex(struct timex *txc)
{
	struct timespec ts;
	int result;

	/* Validate the data before disabling interrupts */
	if (txc->modes & ADJ_ADJTIME) {
		/* singleshot must not be used with any other mode bits */
		if (!(txc->modes & ADJ_OFFSET_SINGLESHOT))
			return -EINVAL;
		if (!(txc->modes & ADJ_OFFSET_READONLY) &&
		    !capable(CAP_SYS_TIME))
			return -EPERM;
	} else {
		/* In order to modify anything, you gotta be super-user! */
		 if (txc->modes && !capable(CAP_SYS_TIME))
			return -EPERM;

		/* if the quartz is off by more than 10% something is VERY wrong! */
		if (txc->modes & ADJ_TICK &&
		    (txc->tick <  900000/USER_HZ ||
		     txc->tick > 1100000/USER_HZ))
				return -EINVAL;

		if (txc->modes & ADJ_STATUS && time_state != TIME_OK)
			hrtimer_cancel(&leap_timer);
	}

	getnstimeofday(&ts);

	write_seqlock_irq(&xtime_lock);

	/* If there are input parameters, then process them */
	if (txc->modes & ADJ_ADJTIME) {
		long save_adjust = time_adjust;

		if (!(txc->modes & ADJ_OFFSET_READONLY)) {
			/* adjtime() is independent from ntp_adjtime() */
			time_adjust = txc->offset;
			ntp_update_frequency();
		}
		txc->offset = save_adjust;
		goto adj_done;
	}
	if (txc->modes) {
		long sec;

		if (txc->modes & ADJ_STATUS) {
			if ((time_status & STA_PLL) &&
			    !(txc->status & STA_PLL)) {
				time_state = TIME_OK;
				time_status = STA_UNSYNC;
			}
			/* only set allowed bits */
			time_status &= STA_RONLY;
			time_status |= txc->status & ~STA_RONLY;

			switch (time_state) {
			case TIME_OK:
			start_timer:
				sec = ts.tv_sec;
				if (time_status & STA_INS) {
					time_state = TIME_INS;
					sec += 86400 - sec % 86400;
					hrtimer_start(&leap_timer, ktime_set(sec, 0), HRTIMER_MODE_ABS);
				} else if (time_status & STA_DEL) {
					time_state = TIME_DEL;
					sec += 86400 - (sec + 1) % 86400;
					hrtimer_start(&leap_timer, ktime_set(sec, 0), HRTIMER_MODE_ABS);
				}
				break;
			case TIME_INS:
			case TIME_DEL:
				time_state = TIME_OK;
				goto start_timer;
				break;
			case TIME_WAIT:
				if (!(time_status & (STA_INS | STA_DEL)))
					time_state = TIME_OK;
				break;
			case TIME_OOP:
				hrtimer_restart(&leap_timer);
				break;
			}
		}

		if (txc->modes & ADJ_NANO)
			time_status |= STA_NANO;
		if (txc->modes & ADJ_MICRO)
			time_status &= ~STA_NANO;

		if (txc->modes & ADJ_FREQUENCY) {
			time_freq = (s64)txc->freq * PPM_SCALE;
			time_freq = min(time_freq, MAXFREQ_SCALED);
			time_freq = max(time_freq, -MAXFREQ_SCALED);
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

		if (txc->modes & ADJ_TAI && txc->constant > 0)
			time_tai = txc->constant;

		if (txc->modes & ADJ_OFFSET)
			ntp_update_offset(txc->offset);
		if (txc->modes & ADJ_TICK)
			tick_usec = txc->tick;

		if (txc->modes & (ADJ_TICK|ADJ_FREQUENCY|ADJ_OFFSET))
			ntp_update_frequency();
	}

	txc->offset = shift_right(time_offset * NTP_INTERVAL_FREQ,
				  NTP_SCALE_SHIFT);
	if (!(time_status & STA_NANO))
		txc->offset /= NSEC_PER_USEC;

adj_done:
	result = time_state;	/* mostly `TIME_OK' */
	if (time_status & (STA_UNSYNC|STA_CLOCKERR))
		result = TIME_ERROR;

	txc->freq	   = shift_right((time_freq >> PPM_SCALE_INV_SHIFT) *
					 (s64)PPM_SCALE_INV, NTP_SCALE_SHIFT);
	txc->maxerror	   = time_maxerror;
	txc->esterror	   = time_esterror;
	txc->status	   = time_status;
	txc->constant	   = time_constant;
	txc->precision	   = 1;
	txc->tolerance	   = MAXFREQ_SCALED / PPM_SCALE;
	txc->tick	   = tick_usec;
	txc->tai	   = time_tai;

	/* PPS is not implemented, so these are zero */
	txc->ppsfreq	   = 0;
	txc->jitter	   = 0;
	txc->shift	   = 0;
	txc->stabil	   = 0;
	txc->jitcnt	   = 0;
	txc->calcnt	   = 0;
	txc->errcnt	   = 0;
	txc->stbcnt	   = 0;
	write_sequnlock_irq(&xtime_lock);

	txc->time.tv_sec = ts.tv_sec;
	txc->time.tv_usec = ts.tv_nsec;
	if (!(time_status & STA_NANO))
		txc->time.tv_usec /= NSEC_PER_USEC;

	notify_cmos_timer();

	return result;
}

static int __init ntp_tick_adj_setup(char *str)
{
	ntp_tick_adj = simple_strtol(str, NULL, 0);
	return 1;
}

__setup("ntp_tick_adj=", ntp_tick_adj_setup);

void __init ntp_init(void)
{
	ntp_clear();
	hrtimer_init(&leap_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	leap_timer.function = ntp_leap_second;
}
