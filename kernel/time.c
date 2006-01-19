/*
 *  linux/kernel/time.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  This file contains the interface functions for the various
 *  time related system calls: time, stime, gettimeofday, settimeofday,
 *			       adjtime
 */
/*
 * Modification history kernel/time.c
 * 
 * 1993-09-02    Philip Gladstone
 *      Created file with time related functions from sched.c and adjtimex() 
 * 1993-10-08    Torsten Duwe
 *      adjtime interface update and CMOS clock write code
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1589)
 * 1999-01-16    Ulrich Windl
 *	Introduced error checking for many cases in adjtimex().
 *	Updated NTP code according to technical memorandum Jan '96
 *	"A Kernel Model for Precision Timekeeping" by Dave Mills
 *	Allow time_constant larger than MAXTC(6) for NTP v4 (MAXTC == 10)
 *	(Even though the technical memorandum forbids it)
 * 2004-07-14	 Christoph Lameter
 *	Added getnstimeofday to allow the posix timer functions to return
 *	with nanosecond accuracy
 */

#include <linux/module.h>
#include <linux/timex.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>

/* 
 * The timezone where the local system is located.  Used as a default by some
 * programs who obtain this value by using gettimeofday.
 */
struct timezone sys_tz;

EXPORT_SYMBOL(sys_tz);

#ifdef __ARCH_WANT_SYS_TIME

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
asmlinkage long sys_time(time_t __user * tloc)
{
	time_t i;
	struct timeval tv;

	do_gettimeofday(&tv);
	i = tv.tv_sec;

	if (tloc) {
		if (put_user(i,tloc))
			i = -EFAULT;
	}
	return i;
}

/*
 * sys_stime() can be implemented in user-level using
 * sys_settimeofday().  Is this for backwards compatibility?  If so,
 * why not move it into the appropriate arch directory (for those
 * architectures that need it).
 */
 
asmlinkage long sys_stime(time_t __user *tptr)
{
	struct timespec tv;
	int err;

	if (get_user(tv.tv_sec, tptr))
		return -EFAULT;

	tv.tv_nsec = 0;

	err = security_settime(&tv, NULL);
	if (err)
		return err;

	do_settimeofday(&tv);
	return 0;
}

#endif /* __ARCH_WANT_SYS_TIME */

asmlinkage long sys_gettimeofday(struct timeval __user *tv, struct timezone __user *tz)
{
	if (likely(tv != NULL)) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (copy_to_user(tv, &ktv, sizeof(ktv)))
			return -EFAULT;
	}
	if (unlikely(tz != NULL)) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	return 0;
}

/*
 * Adjust the time obtained from the CMOS to be UTC time instead of
 * local time.
 * 
 * This is ugly, but preferable to the alternatives.  Otherwise we
 * would either need to write a program to do it in /etc/rc (and risk
 * confusion if the program gets run more than once; it would also be 
 * hard to make the program warp the clock precisely n hours)  or
 * compile in the timezone information into the kernel.  Bad, bad....
 *
 *              				- TYT, 1992-01-01
 *
 * The best thing to do is to keep the CMOS clock in universal time (UTC)
 * as real UNIX machines always do it. This avoids all headaches about
 * daylight saving times and warping kernel clocks.
 */
static inline void warp_clock(void)
{
	write_seqlock_irq(&xtime_lock);
	wall_to_monotonic.tv_sec -= sys_tz.tz_minuteswest * 60;
	xtime.tv_sec += sys_tz.tz_minuteswest * 60;
	time_interpolator_reset();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
}

/*
 * In case for some reason the CMOS clock has not already been running
 * in UTC, but in some local time: The first time we set the timezone,
 * we will warp the clock so that it is ticking UTC time instead of
 * local time. Presumably, if someone is setting the timezone then we
 * are running in an environment where the programs understand about
 * timezones. This should be done at boot time in the /etc/rc script,
 * as soon as possible, so that the clock can be set right. Otherwise,
 * various programs will get confused when the clock gets warped.
 */

int do_sys_settimeofday(struct timespec *tv, struct timezone *tz)
{
	static int firsttime = 1;
	int error = 0;

	if (!timespec_valid(tv))
		return -EINVAL;

	error = security_settime(tv, tz);
	if (error)
		return error;

	if (tz) {
		/* SMP safe, global irq locking makes it work. */
		sys_tz = *tz;
		if (firsttime) {
			firsttime = 0;
			if (!tv)
				warp_clock();
		}
	}
	if (tv)
	{
		/* SMP safe, again the code in arch/foo/time.c should
		 * globally block out interrupts when it runs.
		 */
		return do_settimeofday(tv);
	}
	return 0;
}

asmlinkage long sys_settimeofday(struct timeval __user *tv,
				struct timezone __user *tz)
{
	struct timeval user_tv;
	struct timespec	new_ts;
	struct timezone new_tz;

	if (tv) {
		if (copy_from_user(&user_tv, tv, sizeof(*tv)))
			return -EFAULT;
		new_ts.tv_sec = user_tv.tv_sec;
		new_ts.tv_nsec = user_tv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&new_tz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &new_ts : NULL, tz ? &new_tz : NULL);
}

long pps_offset;		/* pps time offset (us) */
long pps_jitter = MAXTIME;	/* time dispersion (jitter) (us) */

long pps_freq;			/* frequency offset (scaled ppm) */
long pps_stabil = MAXFREQ;	/* frequency dispersion (scaled ppm) */

long pps_valid = PPS_VALID;	/* pps signal watchdog counter */

int pps_shift = PPS_SHIFT;	/* interval duration (s) (shift) */

long pps_jitcnt;		/* jitter limit exceeded */
long pps_calcnt;		/* calibration intervals */
long pps_errcnt;		/* calibration errors */
long pps_stbcnt;		/* stability limit exceeded */

/* hook for a loadable hardpps kernel module */
void (*hardpps_ptr)(struct timeval *);

/* we call this to notify the arch when the clock is being
 * controlled.  If no such arch routine, do nothing.
 */
void __attribute__ ((weak)) notify_arch_cmos_timer(void)
{
	return;
}

/* adjtimex mainly allows reading (and writing, if superuser) of
 * kernel time-keeping variables. used by xntpd.
 */
int do_adjtimex(struct timex *txc)
{
        long ltemp, mtemp, save_adjust;
	int result;

	/* In order to modify anything, you gotta be super-user! */
	if (txc->modes && !capable(CAP_SYS_TIME))
		return -EPERM;
		
	/* Now we validate the data before disabling interrupts */

	if ((txc->modes & ADJ_OFFSET_SINGLESHOT) == ADJ_OFFSET_SINGLESHOT)
	  /* singleshot must not be used with any other mode bits */
		if (txc->modes != ADJ_OFFSET_SINGLESHOT)
			return -EINVAL;

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
	save_adjust = time_next_adjust ? time_next_adjust : time_adjust;

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
		time_freq = txc->freq - pps_freq;
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
		time_constant = txc->constant;
	    }

	    if (txc->modes & ADJ_OFFSET) {	/* values checked earlier */
		if (txc->modes == ADJ_OFFSET_SINGLESHOT) {
		    /* adjtime() is independent from ntp_adjtime() */
		    if ((time_next_adjust = txc->offset) == 0)
			 time_adjust = 0;
		}
		else if ( time_status & (STA_PLL | STA_PPSTIME) ) {
		    ltemp = (time_status & (STA_PPSTIME | STA_PPSSIGNAL)) ==
		            (STA_PPSTIME | STA_PPSSIGNAL) ?
		            pps_offset : txc->offset;

		    /*
		     * Scale the phase adjustment and
		     * clamp to the operating range.
		     */
		    if (ltemp > MAXPHASE)
		        time_offset = MAXPHASE << SHIFT_UPDATE;
		    else if (ltemp < -MAXPHASE)
			time_offset = -(MAXPHASE << SHIFT_UPDATE);
		    else
		        time_offset = ltemp << SHIFT_UPDATE;

		    /*
		     * Select whether the frequency is to be controlled
		     * and in which mode (PLL or FLL). Clamp to the operating
		     * range. Ugly multiply/divide should be replaced someday.
		     */

		    if (time_status & STA_FREQHOLD || time_reftime == 0)
		        time_reftime = xtime.tv_sec;
		    mtemp = xtime.tv_sec - time_reftime;
		    time_reftime = xtime.tv_sec;
		    if (time_status & STA_FLL) {
		        if (mtemp >= MINSEC) {
			    ltemp = (time_offset / mtemp) << (SHIFT_USEC -
							      SHIFT_UPDATE);
			    time_freq += shift_right(ltemp, SHIFT_KH);
			} else /* calibration interval too short (p. 12) */
				result = TIME_ERROR;
		    } else {	/* PLL mode */
		        if (mtemp < MAXSEC) {
			    ltemp *= mtemp;
			    time_freq += shift_right(ltemp,(time_constant +
						       time_constant +
						       SHIFT_KF - SHIFT_USEC));
			} else /* calibration interval too long (p. 12) */
				result = TIME_ERROR;
		    }
		    time_freq = min(time_freq, time_tolerance);
		    time_freq = max(time_freq, -time_tolerance);
		} /* STA_PLL || STA_PPSTIME */
	    } /* txc->modes & ADJ_OFFSET */
	    if (txc->modes & ADJ_TICK) {
		tick_usec = txc->tick;
		tick_nsec = TICK_USEC_TO_NSEC(tick_usec);
	    }
	} /* txc->modes */
leave:	if ((time_status & (STA_UNSYNC|STA_CLOCKERR)) != 0
	    || ((time_status & (STA_PPSFREQ|STA_PPSTIME)) != 0
		&& (time_status & STA_PPSSIGNAL) == 0)
	    /* p. 24, (b) */
	    || ((time_status & (STA_PPSTIME|STA_PPSJITTER))
		== (STA_PPSTIME|STA_PPSJITTER))
	    /* p. 24, (c) */
	    || ((time_status & STA_PPSFREQ) != 0
		&& (time_status & (STA_PPSWANDER|STA_PPSERROR)) != 0))
	    /* p. 24, (d) */
		result = TIME_ERROR;
	
	if ((txc->modes & ADJ_OFFSET_SINGLESHOT) == ADJ_OFFSET_SINGLESHOT)
	    txc->offset	   = save_adjust;
	else {
	    txc->offset = shift_right(time_offset, SHIFT_UPDATE);
	}
	txc->freq	   = time_freq + pps_freq;
	txc->maxerror	   = time_maxerror;
	txc->esterror	   = time_esterror;
	txc->status	   = time_status;
	txc->constant	   = time_constant;
	txc->precision	   = time_precision;
	txc->tolerance	   = time_tolerance;
	txc->tick	   = tick_usec;
	txc->ppsfreq	   = pps_freq;
	txc->jitter	   = pps_jitter >> PPS_AVG;
	txc->shift	   = pps_shift;
	txc->stabil	   = pps_stabil;
	txc->jitcnt	   = pps_jitcnt;
	txc->calcnt	   = pps_calcnt;
	txc->errcnt	   = pps_errcnt;
	txc->stbcnt	   = pps_stbcnt;
	write_sequnlock_irq(&xtime_lock);
	do_gettimeofday(&txc->time);
	notify_arch_cmos_timer();
	return(result);
}

asmlinkage long sys_adjtimex(struct timex __user *txc_p)
{
	struct timex txc;		/* Local copy of parameter */
	int ret;

	/* Copy the user data space into the kernel copy
	 * structure. But bear in mind that the structures
	 * may change
	 */
	if(copy_from_user(&txc, txc_p, sizeof(struct timex)))
		return -EFAULT;
	ret = do_adjtimex(&txc);
	return copy_to_user(txc_p, &txc, sizeof(struct timex)) ? -EFAULT : ret;
}

inline struct timespec current_kernel_time(void)
{
        struct timespec now;
        unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);
		
		now = xtime;
	} while (read_seqretry(&xtime_lock, seq));

	return now; 
}

EXPORT_SYMBOL(current_kernel_time);

/**
 * current_fs_time - Return FS time
 * @sb: Superblock.
 *
 * Return the current time truncated to the time granuality supported by
 * the fs.
 */
struct timespec current_fs_time(struct super_block *sb)
{
	struct timespec now = current_kernel_time();
	return timespec_trunc(now, sb->s_time_gran);
}
EXPORT_SYMBOL(current_fs_time);

/**
 * timespec_trunc - Truncate timespec to a granuality
 * @t: Timespec
 * @gran: Granuality in ns.
 *
 * Truncate a timespec to a granuality. gran must be smaller than a second.
 * Always rounds down.
 *
 * This function should be only used for timestamps returned by
 * current_kernel_time() or CURRENT_TIME, not with do_gettimeofday() because
 * it doesn't handle the better resolution of the later.
 */
struct timespec timespec_trunc(struct timespec t, unsigned gran)
{
	/*
	 * Division is pretty slow so avoid it for common cases.
	 * Currently current_kernel_time() never returns better than
	 * jiffies resolution. Exploit that.
	 */
	if (gran <= jiffies_to_usecs(1) * 1000) {
		/* nothing */
	} else if (gran == 1000000000) {
		t.tv_nsec = 0;
	} else {
		t.tv_nsec -= t.tv_nsec % gran;
	}
	return t;
}
EXPORT_SYMBOL(timespec_trunc);

#ifdef CONFIG_TIME_INTERPOLATION
void getnstimeofday (struct timespec *tv)
{
	unsigned long seq,sec,nsec;

	do {
		seq = read_seqbegin(&xtime_lock);
		sec = xtime.tv_sec;
		nsec = xtime.tv_nsec+time_interpolator_get_offset();
	} while (unlikely(read_seqretry(&xtime_lock, seq)));

	while (unlikely(nsec >= NSEC_PER_SEC)) {
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	tv->tv_sec = sec;
	tv->tv_nsec = nsec;
}
EXPORT_SYMBOL_GPL(getnstimeofday);

int do_settimeofday (struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	{
		wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
		wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

		set_normalized_timespec(&xtime, sec, nsec);
		set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

		time_adjust = 0;		/* stop active adjtime() */
		time_status |= STA_UNSYNC;
		time_maxerror = NTP_PHASE_LIMIT;
		time_esterror = NTP_PHASE_LIMIT;
		time_interpolator_reset();
	}
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}
EXPORT_SYMBOL(do_settimeofday);

void do_gettimeofday (struct timeval *tv)
{
	unsigned long seq, nsec, usec, sec, offset;
	do {
		seq = read_seqbegin(&xtime_lock);
		offset = time_interpolator_get_offset();
		sec = xtime.tv_sec;
		nsec = xtime.tv_nsec;
	} while (unlikely(read_seqretry(&xtime_lock, seq)));

	usec = (nsec + offset) / 1000;

	while (unlikely(usec >= USEC_PER_SEC)) {
		usec -= USEC_PER_SEC;
		++sec;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);


#else
/*
 * Simulate gettimeofday using do_gettimeofday which only allows a timeval
 * and therefore only yields usec accuracy
 */
void getnstimeofday(struct timespec *tv)
{
	struct timeval x;

	do_gettimeofday(&x);
	tv->tv_sec = x.tv_sec;
	tv->tv_nsec = x.tv_usec * NSEC_PER_USEC;
}
EXPORT_SYMBOL_GPL(getnstimeofday);
#endif

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
unsigned long
mktime(const unsigned int year0, const unsigned int mon0,
       const unsigned int day, const unsigned int hour,
       const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	/* 1..12 -> 11,12,1..10 */
	if (0 >= (int) (mon -= 2)) {
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return ((((unsigned long)
		  (year/4 - year/100 + year/400 + 367*mon/12 + day) +
		  year*365 - 719499
	    )*24 + hour /* now have hours */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}

EXPORT_SYMBOL(mktime);

/**
 * set_normalized_timespec - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of
 * 	0 <= tv_nsec < NSEC_PER_SEC
 * For negative values only the tv_sec field is negative !
 */
void set_normalized_timespec(struct timespec *ts, time_t sec, long nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

/**
 * ns_to_timespec - Convert nanoseconds to timespec
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timespec representation of the nsec parameter.
 */
inline struct timespec ns_to_timespec(const nsec_t nsec)
{
	struct timespec ts;

	if (nsec)
		ts.tv_sec = div_long_long_rem_signed(nsec, NSEC_PER_SEC,
						     &ts.tv_nsec);
	else
		ts.tv_sec = ts.tv_nsec = 0;

	return ts;
}

/**
 * ns_to_timeval - Convert nanoseconds to timeval
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timeval representation of the nsec parameter.
 */
struct timeval ns_to_timeval(const nsec_t nsec)
{
	struct timespec ts = ns_to_timespec(nsec);
	struct timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t) ts.tv_nsec / 1000;

	return tv;
}

#if (BITS_PER_LONG < 64)
u64 get_jiffies_64(void)
{
	unsigned long seq;
	u64 ret;

	do {
		seq = read_seqbegin(&xtime_lock);
		ret = jiffies_64;
	} while (read_seqretry(&xtime_lock, seq));
	return ret;
}

EXPORT_SYMBOL(get_jiffies_64);
#endif

EXPORT_SYMBOL(jiffies);
