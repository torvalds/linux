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
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/capability.h>
#include <asm/div64.h>
#include <asm/timex.h>

/*
 * Timekeeping variables
 */
unsigned long tick_usec = TICK_USEC; 		/* USER_HZ period (usec) */
unsigned long tick_nsec;			/* ACTHZ period (nsec) */
static u64 tick_length, tick_length_base;

#define MAX_TICKADJ		500		/* microsecs */
#define MAX_TICKADJ_SCALED	(((u64)(MAX_TICKADJ * NSEC_PER_USEC) << \
				  TICK_LENGTH_SHIFT) / NTP_INTERVAL_FREQ)

/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
static int time_state = TIME_OK;	/* clock synchronization status	*/
int time_status = STA_UNSYNC;		/* clock status bits		*/
static s64 time_offset;		/* time adjustment (ns)		*/
static long time_constant = 2;		/* pll time constant		*/
long time_maxerror = NTP_PHASE_LIMIT;	/* maximum error (us)		*/
long time_esterror = NTP_PHASE_LIMIT;	/* estimated error (us)		*/
long time_freq;				/* frequency offset (scaled ppm)*/
static long time_reftime;		/* time at last adjustment (s)	*/
long time_adjust;

#define CLOCK_TICK_OVERFLOW	(LATCH * HZ - CLOCK_TICK_RATE)
#define CLOCK_TICK_ADJUST	(((s64)CLOCK_TICK_OVERFLOW * NSEC_PER_SEC) / \
					(s64)CLOCK_TICK_RATE)

static void ntp_update_frequency(void)
{
	u64 second_length = (u64)(tick_usec * NSEC_PER_USEC * USER_HZ)
				<< TICK_LENGTH_SHIFT;
	second_length += (s64)CLOCK_TICK_ADJUST << TICK_LENGTH_SHIFT;
	second_length += (s64)time_freq << (TICK_LENGTH_SHIFT - SHIFT_NSEC);

	tick_length_base = second_length;

	do_div(second_length, HZ);
	tick_nsec = second_length >> TICK_LENGTH_SHIFT;

	do_div(tick_length_base, NTP_INTERVAL_FREQ);
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
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 */
void second_overflow(void)
{
	long time_adj;

	/* Bump the maxerror field */
	time_maxerror += MAXFREQ >> SHIFT_USEC;
	if (time_maxerror > NTP_PHASE_LIMIT) {
		time_maxerror = NTP_PHASE_LIMIT;
		time_status |= STA_UNSYNC;
	}

	/*
	 * Leap second processing. If in leap-insert state at the end of the
	 * day, the system clock is set back one second; if in leap-delete
	 * state, the system clock is set ahead one second. The microtime()
	 * routine or external clock driver will insure that reported time is
	 * always monotonic. The ugly divides should be replaced.
	 */
	switch (time_state) {
	case TIME_OK:
		if (time_status & STA_INS)
			time_state = TIME_INS;
		else if (time_status & STA_DEL)
			time_state = TIME_DEL;
		break;
	case TIME_INS:
		if (xtime.tv_sec % 86400 == 0) {
			xtime.tv_sec--;
			wall_to_monotonic.tv_sec++;
			time_state = TIME_OOP;
			printk(KERN_NOTICE "Clock: inserting leap second "
					"23:59:60 UTC\n");
		}
		break;
	case TIME_DEL:
		if ((xtime.tv_sec + 1) % 86400 == 0) {
			xtime.tv_sec++;
			wall_to_monotonic.tv_sec--;
			time_state = TIME_WAIT;
			printk(KERN_NOTICE "Clock: deleting leap second "
					"23:59:59 UTC\n");
		}
		break;
	case TIME_OOP:
		time_state = TIME_WAIT;
		break;
	case TIME_WAIT:
		if (!(time_status & (STA_INS | STA_DEL)))
		time_state = TIME_OK;
	}

	/*
	 * Compute the phase adjustment for the next second. The offset is
	 * reduced by a fixed factor times the time constant.
	 */
	tick_length = tick_length_base;
	time_adj = shift_right(time_offset, SHIFT_PLL + time_constant);
	time_offset -= time_adj;
	tick_length += (s64)time_adj << (TICK_LENGTH_SHIFT - SHIFT_UPDATE);

	if (unlikely(time_adjust)) {
		if (time_adjust > MAX_TICKADJ) {
			time_adjust -= MAX_TICKADJ;
			tick_length += MAX_TICKADJ_SCALED;
		} else if (time_adjust < -MAX_TICKADJ) {
			time_adjust += MAX_TICKADJ;
			tick_length -= MAX_TICKADJ_SCALED;
		} else {
			tick_length += (s64)(time_adjust * NSEC_PER_USEC /
					NTP_INTERVAL_FREQ) << TICK_LENGTH_SHIFT;
			time_adjust = 0;
		}
	}
}

/*
 * Return how long ticks are at the moment, that is, how much time
 * update_wall_time_one_tick will add to xtime next time we call it
 * (assuming no calls to do_adjtimex in the meantime).
 * The return value is in fixed-point nanoseconds shifted by the
 * specified number of bits to the right of the binary point.
 * This function has no side-effects.
 */
u64 current_tick_length(void)
{
	return tick_length;
}

#ifdef CONFIG_GENERIC_CMOS_UPDATE

/* Disable the cmos update - used by virtualization and embedded */
int no_sync_cmos_clock  __read_mostly;

static void sync_cmos_clock(unsigned long dummy);

static DEFINE_TIMER(sync_cmos_timer, sync_cmos_clock, 0, 0);

static void sync_cmos_clock(unsigned long dummy)
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
	mod_timer(&sync_cmos_timer, jiffies + timespec_to_jiffies(&next));
}

static void notify_cmos_timer(void)
{
	if (!no_sync_cmos_clock)
		mod_timer(&sync_cmos_timer, jiffies + 1);
}

#else
static inline void notify_cmos_timer(void) { }
#endif

/* adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
int do_adjtimex(struct timex *txc)
{
	long mtemp, save_adjust, rem;
	s64 freq_adj, temp64;
	int result;

	/* In order to modify anything, you gotta be super-user! */
	if (txc->modes && !capable(CAP_SYS_TIME))
		return -EPERM;

	/* Now we validate the data before disabling interrupts */

	if ((txc->modes & ADJ_OFFSET_SINGLESHOT) == ADJ_OFFSET_SINGLESHOT) {
	  /* singleshot must not be used with any other mode bits */
		if (txc->modes != ADJ_OFFSET_SINGLESHOT &&
					txc->modes != ADJ_OFFSET_SS_READ)
			return -EINVAL;
	}

	if (txc->modes != ADJ_OFFSET_SINGLESHOT && (txc->modes & ADJ_OFFSET))
	  /* adjustment Offset limited to +- .512 seconds */
		if (txc->offset <= - MAXPHASE || txc->offset >= MAXPHASE )
			return -EINVAL;

	/* if the quartz is off by more than 10% something is VERY wrong ! */
	if (txc->modes & ADJ_TICK)
		if (txc->tick <  900000/USER_HZ ||
		    txc->tick > 1100000/USER_HZ)
			return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	result = time_state;	/* mostly `TIME_OK' */

	/* Save for later - semantics of adjtime is to return old value */
	save_adjust = time_adjust;

#if 0	/* STA_CLOCKERR is never set yet */
	time_status &= ~STA_CLOCKERR;		/* reset STA_CLOCKERR */
#endif
	/* If there are input parameters, then process them */
	if (txc->modes)
	{
	    if (txc->modes & ADJ_STATUS)	/* only set allowed bits */
		time_status =  (txc->status & ~STA_RONLY) |
			      (time_status & STA_RONLY);

	    if (txc->modes & ADJ_FREQUENCY) {	/* p. 22 */
		if (txc->freq > MAXFREQ || txc->freq < -MAXFREQ) {
		    result = -EINVAL;
		    goto leave;
		}
		time_freq = ((s64)txc->freq * NSEC_PER_USEC)
				>> (SHIFT_USEC - SHIFT_NSEC);
	    }

	    if (txc->modes & ADJ_MAXERROR) {
		if (txc->maxerror < 0 || txc->maxerror >= NTP_PHASE_LIMIT) {
		    result = -EINVAL;
		    goto leave;
		}
		time_maxerror = txc->maxerror;
	    }

	    if (txc->modes & ADJ_ESTERROR) {
		if (txc->esterror < 0 || txc->esterror >= NTP_PHASE_LIMIT) {
		    result = -EINVAL;
		    goto leave;
		}
		time_esterror = txc->esterror;
	    }

	    if (txc->modes & ADJ_TIMECONST) {	/* p. 24 */
		if (txc->constant < 0) {	/* NTP v4 uses values > 6 */
		    result = -EINVAL;
		    goto leave;
		}
		time_constant = min(txc->constant + 4, (long)MAXTC);
	    }

	    if (txc->modes & ADJ_OFFSET) {	/* values checked earlier */
		if (txc->modes == ADJ_OFFSET_SINGLESHOT) {
		    /* adjtime() is independent from ntp_adjtime() */
		    time_adjust = txc->offset;
		}
		else if (time_status & STA_PLL) {
		    time_offset = txc->offset * NSEC_PER_USEC;

		    /*
		     * Scale the phase adjustment and
		     * clamp to the operating range.
		     */
		    time_offset = min(time_offset, (s64)MAXPHASE * NSEC_PER_USEC);
		    time_offset = max(time_offset, (s64)-MAXPHASE * NSEC_PER_USEC);

		    /*
		     * Select whether the frequency is to be controlled
		     * and in which mode (PLL or FLL). Clamp to the operating
		     * range. Ugly multiply/divide should be replaced someday.
		     */

		    if (time_status & STA_FREQHOLD || time_reftime == 0)
		        time_reftime = xtime.tv_sec;
		    mtemp = xtime.tv_sec - time_reftime;
		    time_reftime = xtime.tv_sec;

		    freq_adj = time_offset * mtemp;
		    freq_adj = shift_right(freq_adj, time_constant * 2 +
					   (SHIFT_PLL + 2) * 2 - SHIFT_NSEC);
		    if (mtemp >= MINSEC && (time_status & STA_FLL || mtemp > MAXSEC)) {
			temp64 = time_offset << (SHIFT_NSEC - SHIFT_FLL);
			if (time_offset < 0) {
			    temp64 = -temp64;
			    do_div(temp64, mtemp);
			    freq_adj -= temp64;
			} else {
			    do_div(temp64, mtemp);
			    freq_adj += temp64;
			}
		    }
		    freq_adj += time_freq;
		    freq_adj = min(freq_adj, (s64)MAXFREQ_NSEC);
		    time_freq = max(freq_adj, (s64)-MAXFREQ_NSEC);
		    time_offset = div_long_long_rem_signed(time_offset,
							   NTP_INTERVAL_FREQ,
							   &rem);
		    time_offset <<= SHIFT_UPDATE;
		} /* STA_PLL */
	    } /* txc->modes & ADJ_OFFSET */
	    if (txc->modes & ADJ_TICK)
		tick_usec = txc->tick;

	    if (txc->modes & (ADJ_TICK|ADJ_FREQUENCY|ADJ_OFFSET))
		    ntp_update_frequency();
	} /* txc->modes */
leave:	if ((time_status & (STA_UNSYNC|STA_CLOCKERR)) != 0)
		result = TIME_ERROR;

	if ((txc->modes == ADJ_OFFSET_SINGLESHOT) ||
			(txc->modes == ADJ_OFFSET_SS_READ))
		txc->offset = save_adjust;
	else
		txc->offset = ((long)shift_right(time_offset, SHIFT_UPDATE)) *
	    			NTP_INTERVAL_FREQ / 1000;
	txc->freq	   = (time_freq / NSEC_PER_USEC) <<
				(SHIFT_USEC - SHIFT_NSEC);
	txc->maxerror	   = time_maxerror;
	txc->esterror	   = time_esterror;
	txc->status	   = time_status;
	txc->constant	   = time_constant;
	txc->precision	   = 1;
	txc->tolerance	   = MAXFREQ;
	txc->tick	   = tick_usec;

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
	do_gettimeofday(&txc->time);
	notify_cmos_timer();
	return(result);
}
