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

#include <linux/compiler.h>
#include <linux/time.h>

#include <asm/param.h>

/*
 * SHIFT_KG and SHIFT_KF establish the damping of the PLL and are chosen
 * for a slightly underdamped convergence characteristic. SHIFT_KH
 * establishes the damping of the FLL and is chosen by wisdom and black
 * art.
 *
 * MAXTC establishes the maximum time constant of the PLL. With the
 * SHIFT_KG and SHIFT_KF values given and a time constant range from
 * zero to MAXTC, the PLL will converge in 15 minutes to 16 hours,
 * respectively.
 */
#define SHIFT_PLL	4	/* PLL frequency factor (shift) */
#define SHIFT_FLL	2	/* FLL frequency factor (shift) */
#define MAXTC		10	/* maximum time constant (shift) */

/*
 * The SHIFT_UPDATE define establishes the decimal point of the
 * time_offset variable which represents the current offset with
 * respect to standard time.
 *
 * SHIFT_USEC defines the scaling (shift) of the time_freq and
 * time_tolerance variables, which represent the current frequency
 * offset and maximum frequency tolerance.
 */
#define SHIFT_UPDATE (SHIFT_HZ + 1) /* time offset scale (shift) */
#define SHIFT_USEC 16		/* frequency offset scale (shift) */
#define SHIFT_NSEC 12		/* kernel frequency offset scale */

#define MAXPHASE 512000L        /* max phase error (us) */
#define MAXFREQ (512L << SHIFT_USEC)  /* max frequency error (ppm) */
#define MAXFREQ_NSEC (512000L << SHIFT_NSEC) /* max frequency error (ppb) */
#define MINSEC 256		/* min interval between updates (s) */
#define MAXSEC 2048		/* max interval between updates (s) */
#define	NTP_PHASE_LIMIT	(MAXPHASE << 5)	/* beyond max. dispersion */

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

	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
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
#define ADJ_TICK		0x4000	/* tick value */
#define ADJ_OFFSET_SINGLESHOT	0x8001	/* old-fashioned adjtime */

/* xntp 3.4 compatibility names */
#define MOD_OFFSET	ADJ_OFFSET
#define MOD_FREQUENCY	ADJ_FREQUENCY
#define MOD_MAXERROR	ADJ_MAXERROR
#define MOD_ESTERROR	ADJ_ESTERROR
#define MOD_STATUS	ADJ_STATUS
#define MOD_TIMECONST	ADJ_TIMECONST
#define MOD_CLKB	ADJ_TICK
#define MOD_CLKA	ADJ_OFFSET_SINGLESHOT /* 0x8000 in original */


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

#define STA_RONLY (STA_PPSSIGNAL | STA_PPSJITTER | STA_PPSWANDER | \
    STA_PPSERROR | STA_CLOCKERR) /* read-only bits */

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
#include <asm/timex.h>

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

extern long time_freq;		/* frequency offset (scaled ppm) */

extern long time_adjust;	/* The amount of adjtime left */

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

#define TICK_LENGTH_SHIFT	32

#ifdef CONFIG_NO_HZ
#define NTP_INTERVAL_FREQ  (2)
#else
#define NTP_INTERVAL_FREQ  (HZ)
#endif
#define NTP_INTERVAL_LENGTH (NSEC_PER_SEC/NTP_INTERVAL_FREQ)

/* Returns how long ticks are at present, in ns / 2^(SHIFT_SCALE-10). */
extern u64 current_tick_length(void);

extern void second_overflow(void);
extern void update_ntp_one_tick(void);
extern int do_adjtimex(struct timex *);

/* Don't use! Compatibility define for existing users. */
#define tickadj	(500/HZ ? : 1)

#endif /* KERNEL */

#endif /* LINUX_TIMEX_H */
