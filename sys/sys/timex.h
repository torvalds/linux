/*-
 ***********************************************************************
 *								       *
 * Copyright (c) David L. Mills 1993-2001			       *
 * Copyright (c) Poul-Henning Kamp 2000-2001                           *
 *								       *
 * Permission to use, copy, modify, and distribute this software and   *
 * its documentation for any purpose and without fee is hereby	       *
 * granted, provided that the above copyright notice appears in all    *
 * copies and that both the copyright notice and this permission       *
 * notice appear in supporting documentation, and that the name        *
 * University of Delaware not be used in advertising or publicity      *
 * pertaining to distribution of the software without specific,	       *
 * written prior permission. The University of Delaware makes no       *
 * representations about the suitability this software for any	       *
 * purpose. It is provided "as is" without express or implied	       *
 * warranty.							       *
 *								       *
 ***********************************************************************
 *
 * $FreeBSD$
 *
 * This header file defines the Network Time Protocol (NTP) interfaces
 * for user and daemon application programs.
 *
 * This file was originally created 17 Sep 93 by David L. Mills, Professor
 * of University of Delaware, building on work which had already been ongoing
 * for a decade and a half at that point in time.
 *
 * In 2000 the APIs got a upgrade from microseconds to nanoseconds,
 * a joint work between Poul-Henning Kamp and David L. Mills.
 *
 */

#ifndef _SYS_TIMEX_H_
#define _SYS_TIMEX_H_ 1

#define NTP_API		4		/* NTP API version */

#ifdef __FreeBSD__
#include <sys/_timespec.h>
#endif /* __FreeBSD__ */

/*
 * The following defines establish the performance envelope of the
 * kernel discipline loop. Phase or frequency errors greater than
 * NAXPHASE or MAXFREQ are clamped to these maxima. For update intervals
 * less than MINSEC, the loop always operates in PLL mode; while, for
 * update intervals greater than MAXSEC, the loop always operates in FLL
 * mode. Between these two limits the operating mode is selected by the
 * STA_FLL bit in the status word.
 */

#define MAXPHASE	500000000L	/* max phase error (ns) */
#define MAXFREQ		500000L		/* max freq error (ns/s) */
#define MINSEC		256		/* min FLL update interval (s) */
#define MAXSEC		2048		/* max PLL update interval (s) */
#define NANOSECOND	1000000000L	/* nanoseconds in one second */
#define SCALE_PPM	(65536 / 1000)	/* crude ns/s to scaled PPM */
#define MAXTC		10		/* max time constant */

/*
 * Control mode codes (timex.modes)
 */
#define MOD_OFFSET	0x0001		/* set time offset */
#define MOD_FREQUENCY	0x0002		/* set frequency offset */
#define MOD_MAXERROR	0x0004		/* set maximum time error */
#define MOD_ESTERROR	0x0008		/* set estimated time error */
#define MOD_STATUS	0x0010		/* set clock status bits */
#define MOD_TIMECONST	0x0020		/* set PLL time constant */
#define MOD_PPSMAX	0x0040		/* set PPS maximum averaging time */
#define MOD_TAI		0x0080		/* set TAI offset */
#define	MOD_MICRO	0x1000		/* select microsecond resolution */
#define	MOD_NANO	0x2000		/* select nanosecond resolution */
#define MOD_CLKB	0x4000		/* select clock B */
#define MOD_CLKA	0x8000		/* select clock A */

/*
 * Status codes (timex.status)
 */
#define STA_PLL		0x0001		/* enable PLL updates (rw) */
#define STA_PPSFREQ	0x0002		/* enable PPS freq discipline (rw) */
#define STA_PPSTIME	0x0004		/* enable PPS time discipline (rw) */
#define STA_FLL		0x0008		/* enable FLL mode (rw) */
#define STA_INS		0x0010		/* insert leap (rw) */
#define STA_DEL		0x0020		/* delete leap (rw) */
#define STA_UNSYNC	0x0040		/* clock unsynchronized (rw) */
#define STA_FREQHOLD	0x0080		/* hold frequency (rw) */
#define STA_PPSSIGNAL	0x0100		/* PPS signal present (ro) */
#define STA_PPSJITTER	0x0200		/* PPS signal jitter exceeded (ro) */
#define STA_PPSWANDER	0x0400		/* PPS signal wander exceeded (ro) */
#define STA_PPSERROR	0x0800		/* PPS signal calibration error (ro) */
#define STA_CLOCKERR	0x1000		/* clock hardware fault (ro) */
#define STA_NANO	0x2000		/* resolution (0 = us, 1 = ns) (ro) */
#define STA_MODE	0x4000		/* mode (0 = PLL, 1 = FLL) (ro) */
#define STA_CLK		0x8000		/* clock source (0 = A, 1 = B) (ro) */

#define STA_RONLY (STA_PPSSIGNAL | STA_PPSJITTER | STA_PPSWANDER | \
    STA_PPSERROR | STA_CLOCKERR | STA_NANO | STA_MODE | STA_CLK)

/*
 * Clock states (ntptimeval.time_state)
 */
#define TIME_OK		0		/* no leap second warning */
#define TIME_INS	1		/* insert leap second warning */
#define TIME_DEL	2		/* delete leap second warning */
#define TIME_OOP	3		/* leap second in progress */
#define TIME_WAIT	4		/* leap second has occurred */
#define TIME_ERROR	5		/* error (see status word) */

/*
 * NTP user interface -- ntp_gettime(2) - used to read kernel clock values
 */
struct ntptimeval {
	struct timespec time;		/* current time (ns) (ro) */
	long maxerror;			/* maximum error (us) (ro) */
	long esterror;			/* estimated error (us) (ro) */
	long tai;			/* TAI offset */
	int time_state;			/* time status */
};

/*
 * NTP daemon interface -- ntp_adjtime(2) -- used to discipline CPU clock
 * oscillator and control/determine status.
 *
 * Note: The offset, precision and jitter members are in microseconds if
 * STA_NANO is zero and nanoseconds if not.
 */
struct timex {
	unsigned int modes;		/* clock mode bits (wo) */
	long	offset;			/* time offset (ns/us) (rw) */
	long	freq;			/* frequency offset (scaled PPM) (rw) */
	long	maxerror;		/* maximum error (us) (rw) */
	long	esterror;		/* estimated error (us) (rw) */
	int	status;			/* clock status bits (rw) */
	long	constant;		/* poll interval (log2 s) (rw) */
	long	precision;		/* clock precision (ns/us) (ro) */
	long	tolerance;		/* clock frequency tolerance (scaled
				 	 * PPM) (ro) */
	/*
	 * The following read-only structure members are implemented
	 * only if the PPS signal discipline is configured in the
	 * kernel. They are included in all configurations to insure
	 * portability.
	 */
	long	ppsfreq;		/* PPS frequency (scaled PPM) (ro) */
	long	jitter;			/* PPS jitter (ns/us) (ro) */
	int	shift;			/* interval duration (s) (shift) (ro) */
	long	stabil;			/* PPS stability (scaled PPM) (ro) */
	long	jitcnt;			/* jitter limit exceeded (ro) */
	long	calcnt;			/* calibration intervals (ro) */
	long	errcnt;			/* calibration errors (ro) */
	long	stbcnt;			/* stability limit exceeded (ro) */
};

#ifdef __FreeBSD__

#ifdef _KERNEL
void	ntp_update_second(int64_t *adjustment, time_t *newsec);
#else /* !_KERNEL */
#include <sys/cdefs.h>

__BEGIN_DECLS
int	ntp_adjtime(struct timex *);
int	ntp_gettime(struct ntptimeval *);
__END_DECLS
#endif /* _KERNEL */

#endif /* __FreeBSD__ */

#endif /* !_SYS_TIMEX_H_ */
