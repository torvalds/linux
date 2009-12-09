/*****************************************************************************
 *                                                                           *
 * Copyright (c) David L. Mills 1993                                         *
 *                                                                           *
 * Permission to use, copy, modify, and distribute this software and its     *
 * documentation for any purpose and without fee is hereby granted, provided *
 * that the above copyright notice appears in all copies and that both the   *
 * copyright notice and this permission notice appear in supporting          *
 * documentation, and that the name University of Delaware not be used in    *
 * advertising or publicity pertaining to distribution of the software       *
 * without specific, written prior permission.  The University of Delaware   *
 * makes no representations about the suitability this software for any      *
 * purpose.  It is provided "as is" without express or implied warranty.     *
 *                                                                           *
 *****************************************************************************/

/*
 * Modification history timex.h
 *
 * 29 Dec 97	Russell King
 *	Moved CLOCK_TICK_RATE, CLOCK_TICK_FACTOR and FINETUNE to asm/timex.h
 *	for ARM machines
 *
 *  9 Jan 97    Adrian Sun
 *      Shifted LATCH define to allow access to alpha machines.
 *
 * 26 Sep 94	David L. Mills
 *	Added defines for hybrid phase/frequency-lock loop.
 *
 * 19 Mar 94	David L. Mills
 *	Moved defines from kernel routines to header file and added new
 *	defines for PPS phase-lock loop.
 *
 * 20 Feb 94	David L. Mills
 *	Revised status codes and structures for external clock and PPS
 *	signal discipline.
 *
 * 28 Nov 93	David L. Mills
 *	Adjusted parameters to improve stability and increase poll
 *	interval.
 *
 * 17 Sep 93    David L. Mills
 *      Created file $NTP/include/sys/timex.h
 * 07 Oct 93    Torsten Duwe
 *      Derived linux/timex.h
 * 1995-08-13    Torsten Duwe
 *      kernel PLL updated to 1994-12-13 specs (rfc-1589)
 * 1997-08-30    Ulrich Windl
 *      Added new constant NTP_PHASE_LIMIT
 * 2004-08-12    Christoph Lameter
 *      Reworked time interpolation logic
 */
#ifndef _LINUX_TIMEX_H
#define _LINUX_TIMEX_H

#include <linux/time.h>

#define NTP_API		4	/* NTP API version */

/*
 * syscall interface - used (mainly by NTP daemon)
 * to discipline kernel clock oscillator
 */
struct timex {
	unsigned int modes;	/* mode selector */
	long offset;		/* time offset (usec) */
	long freq;		/* frequency offset (scaled ppm) */
	long maxerror;		/* maximum error (usec) */
	long esterror;		/* estimated error (usec) */
	int status;		/* clock command/status */
	long constant;		/* pll time constant */
	long precision;		/* clock precision (usec) (read only) */
	long tolerance;		/* clock frequency tolerance (ppm)
				 * (read only)
				 */
	struct timeval time;	/* (read only) */
	long tick;		/* (modified) usecs between clock ticks */

	long ppsfreq;           /* pps frequency (scaled ppm) (ro) */
	long jitter;            /* pps jitter (us) (ro) */
	int shift;              /* interval duration (s) (shift) (ro) */
	long stabil;            /* pps stability (scaled ppm) (ro) */
	long jitcnt;            /* jitter limit exceeded (ro) */
	long calcnt;            /* calibration intervals (ro) */
	long errcnt;            /* calibration errors (ro) */
	long stbcnt;            /* stability limit exceeded (ro) */

	int tai;		/* TAI offset (ro) */

	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32;
};

/*
 * Mode codes (timex.mode)
 */
#define ADJ_OFFSET		0x0001	/* time offset */
#define ADJ_FREQUENCY		0x0002	/* frequency offset */
#define ADJ_MAXERROR		0x0004	/* maximum time error */
#define ADJ_ESTERROR		0x0008	/* estimated time error */
#define ADJ_STATUS		0x0010	/* clock status */
#define ADJ_TIMECONST		0x0020	/* pll time constant */
#define ADJ_TAI			0x0080	/* set TAI offset */
#define ADJ_MICRO		0x1000	/* select microsecond resolution */
#define ADJ_NANO		0x2000	/* select nanosecond resolution */
#define ADJ_TICK		0x4000	/* tick value */

#ifdef __KERNEL__
#define ADJ_ADJTIME		0x8000	/* switch between adjtime/adjtimex modes */
#define ADJ_OFFSET_SINGLESHOT	0x0001	/* old-fashioned adjtime */
#define ADJ_OFFSET_READONLY	0x2000	/* read-only adjtime */
#else
#define ADJ_OFFSET_SINGLESHOT	0x8001	/* old-fashioned adjtime */
#define ADJ_OFFSET_SS_READ	0xa001	/* read-only adjtime */
#endif

/* xntp 3.4 compatibility names */
#define MOD_OFFSET	ADJ_OFFSET
#define MOD_FREQUENCY	ADJ_FREQUENCY
#define MOD_MAXERROR	ADJ_MAXERROR
#define MOD_ESTERROR	ADJ_ESTERROR
#define MOD_STATUS	ADJ_STATUS
#define MOD_TIMECONST	ADJ_TIMECONST


/*
 * Status codes (timex.status)
 */
#define STA_PLL		0x0001	/* enable PLL updates (rw) */
#define STA_PPSFREQ	0x0002	/* enable PPS freq discipline (rw) */
#define STA_PPSTIME	0x0004	/* enable PPS time discipline (rw) */
#define STA_FLL		0x0008	/* select frequency-lock mode (rw) */

#define STA_INS		0x0010	/* insert leap (rw) */
#define STA_DEL		0x0020	/* delete leap (rw) */
#define STA_UNSYNC	0x0040	/* clock unsynchronized (rw) */
#define STA_FREQHOLD	0x0080	/* hold frequency (rw) */

#define STA_PPSSIGNAL	0x0100	/* PPS signal present (ro) */
#define STA_PPSJITTER	0x0200	/* PPS signal jitter exceeded (ro) */
#define STA_PPSWANDER	0x0400	/* PPS signal wander exceeded (ro) */
#define STA_PPSERROR	0x0800	/* PPS signal calibration error (ro) */

#define STA_CLOCKERR	0x1000	/* clock hardware fault (ro) */
#define STA_NANO	0x2000	/* resolution (0 = us, 1 = ns) (ro) */
#define STA_MODE	0x4000	/* mode (0 = PLL, 1 = FLL) (ro) */
#define STA_CLK		0x8000	/* clock source (0 = A, 1 = B) (ro) */

/* read-only bits */
#define STA_RONLY (STA_PPSSIGNAL | STA_PPSJITTER | STA_PPSWANDER | \
	STA_PPSERROR | STA_CLOCKERR | STA_NANO | STA_MODE | STA_CLK)

/*
 * Clock states (time_state)
 */
#define TIME_OK		0	/* clock synchronized, no leap second */
#define TIME_INS	1	/* insert leap second */
#define TIME_DEL	2	/* delete leap second */
#define TIME_OOP	3	/* leap second in progress */
#define TIME_WAIT	4	/* leap second has occurred */
#define TIME_ERROR	5	/* clock not synchronized */
#define TIME_BAD	TIME_ERROR /* bw compat */

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/param.h>

#include <asm/timex.h>

/*
 * SHIFT_PLL is used as a dampening factor to define how much we
 * adjust the frequency correction for a given offset in PLL mode.
 * It also used in dampening the offset correction, to define how
 * much of the current value in time_offset we correct for each
 * second. Changing this value changes the stiffness of the ntp
 * adjustment code. A lower value makes it more flexible, reducing
 * NTP convergence time. A higher value makes it stiffer, increasing
 * convergence time, but making the clock more stable.
 *
 * In David Mills' nanokernel reference implementation SHIFT_PLL is 4.
 * However this seems to increase convergence time much too long.
 *
 * https://lists.ntp.org/pipermail/hackers/2008-January/003487.html
 *
 * In the above mailing list discussion, it seems the value of 4
 * was appropriate for other Unix systems with HZ=100, and that
 * SHIFT_PLL should be decreased as HZ increases. However, Linux's
 * clock steering implementation is HZ independent.
 *
 * Through experimentation, a SHIFT_PLL value of 2 was found to allow
 * for fast convergence (very similar to the NTPv3 code used prior to
 * v2.6.19), with good clock stability.
 *
 *
 * SHIFT_FLL is used as a dampening factor to define how much we
 * adjust the frequency correction for a given offset in FLL mode.
 * In David Mills' nanokernel reference implementation SHIFT_FLL is 2.
 *
 * MAXTC establishes the maximum time constant of the PLL.
 */
#define SHIFT_PLL	2	/* PLL frequency factor (shift) */
#define SHIFT_FLL	2	/* FLL frequency factor (shift) */
#define MAXTC		10	/* maximum time constant (shift) */

/*
 * SHIFT_USEC defines the scaling (shift) of the time_freq and
 * time_tolerance variables, which represent the current frequency
 * offset and maximum frequency tolerance.
 */
#define SHIFT_USEC 16		/* frequency offset scale (shift) */
#define PPM_SCALE ((s64)NSEC_PER_USEC << (NTP_SCALE_SHIFT - SHIFT_USEC))
#define PPM_SCALE_INV_SHIFT 19
#define PPM_SCALE_INV ((1LL << (PPM_SCALE_INV_SHIFT + NTP_SCALE_SHIFT)) / \
		       PPM_SCALE + 1)

#define MAXPHASE 500000000L	/* max phase error (ns) */
#define MAXFREQ 500000		/* max frequency error (ns/s) */
#define MAXFREQ_SCALED ((s64)MAXFREQ << NTP_SCALE_SHIFT)
#define MINSEC 256		/* min interval between updates (s) */
#define MAXSEC 2048		/* max interval between updates (s) */
#define NTP_PHASE_LIMIT ((MAXPHASE / NSEC_PER_USEC) << 5) /* beyond max. dispersion */

/*
 * kernel variables
 * Note: maximum error = NTP synch distance = dispersion + delay / 2;
 * estimated error = NTP dispersion.
 */
extern unsigned long tick_usec;		/* USER_HZ period (usec) */
extern unsigned long tick_nsec;		/* ACTHZ          period (nsec) */
extern int tickadj;			/* amount of adjustment per tick */

/*
 * phase-lock loop variables
 */
extern int time_status;		/* clock synchronization status bits */
extern long time_maxerror;	/* maximum error */
extern long time_esterror;	/* estimated error */

extern long time_adjust;	/* The amount of adjtime left */

extern void ntp_init(void);
extern void ntp_clear(void);

/**
 * ntp_synced - Returns 1 if the NTP status is not UNSYNC
 *
 */
static inline int ntp_synced(void)
{
	return !(time_status & STA_UNSYNC);
}

/* Required to safely shift negative values */
#define shift_right(x, s) ({	\
	__typeof__(x) __x = (x);	\
	__typeof__(s) __s = (s);	\
	__x < 0 ? -(-__x >> __s) : __x >> __s;	\
})

#define NTP_SCALE_SHIFT		32

#define NTP_INTERVAL_FREQ  (HZ)
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)

/* Returns how long ticks are at present, in ns / 2^NTP_SCALE_SHIFT. */
extern u64 tick_length;

extern void second_overflow(void);
extern void update_ntp_one_tick(void);
extern int do_adjtimex(struct timex *);

/* Don't use! Compatibility define for existing users. */
#define tickadj	(500/HZ ? : 1)

int read_current_timer(unsigned long *timer_val);

/* The clock frequency of the i8253/i8254 PIT */
#define PIT_TICK_RATE 1193182ul

#endif /* KERNEL */

#endif /* LINUX_TIMEX_H */
