/*	$OpenBSD: tty_endrun.c,v 1.8 2018/02/19 08:59:52 mpi Exp $ */

/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2009 Kevin Steves <stevesk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * A tty line discipline to decode the EndRun Technologies native
 * time-of-day message.
 * http://www.endruntechnologies.com/
 */

/*
 * EndRun Format:
 *
 * T YYYY DDD HH:MM:SS zZZ m<CR><LF>
 *
 * T is the Time Figure of Merit (TFOM) character (described below).
 * This is the on-time character, transmitted during the first
 * millisecond of each second.
 *
 * YYYY is the year
 * DDD is the day-of-year
 * : is the colon character (0x3A)
 * HH is the hour of the day
 * MM is the minute of the hour
 * SS is the second of the minute
 * z is the sign of the offset to UTC, + implies time is ahead of UTC.
 * ZZ is the magnitude of the offset to UTC in units of half-hours.
 * Non-zero only when the Timemode is Local.
 * m is the Timemode character and is one of:
 *   G = GPS
 *   L = Local
 *   U = UTC
 * <CR> is the ASCII carriage return character (0x0D)
 * <LF> is the ASCII line feed character (0x0A)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/time.h>

#ifdef ENDRUN_DEBUG
#define DPRINTFN(n, x)	do { if (endrundebug > (n)) printf x; } while (0)
int endrundebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

void	endrunattach(int);

#define ENDRUNLEN	27 /* strlen("6 2009 018 20:41:17 +00 U\r\n") */
#define NUMFLDS		6
#ifdef ENDRUN_DEBUG
#define TRUSTTIME	30
#else
#define TRUSTTIME	(10 * 60)	/* 10 minutes */
#endif

int endrun_count, endrun_nxid;

struct endrun {
	char			cbuf[ENDRUNLEN];	/* receive buffer */
	struct ksensor		time;		/* the timedelta sensor */
	struct ksensor		signal;		/* signal status */
	struct ksensordev	timedev;
	struct timespec		ts;		/* current timestamp */
	struct timespec		lts;		/* timestamp of last TFOM */
	struct timeout		endrun_tout;	/* invalidate sensor */
	int64_t			gap;		/* gap between two sentences */
	int64_t			last;		/* last time rcvd */
#define SYNC_SCAN	1	/* scanning for '\n' */
#define SYNC_EOL	2	/* '\n' seen, next char TFOM */
	int			sync;
	int			pos;		/* position in rcv buffer */
	int			no_pps;		/* no PPS although requested */
#ifdef ENDRUN_DEBUG
	char			tfom;
#endif
};

/* EndRun decoding */
void	endrun_scan(struct endrun *, struct tty *);
void	endrun_decode(struct endrun *, struct tty *, char *fld[], int fldcnt);

/* date and time conversion */
int	endrun_atoi(char *s, int len);
int	endrun_date_to_nano(char *s1, char *s2, int64_t *nano);
int	endrun_time_to_nano(char *s, int64_t *nano);
int	endrun_offset_to_nano(char *s, int64_t *nano);

/* degrade the timedelta sensor */
void	endrun_timeout(void *);

void
endrunattach(int dummy)
{
}

int
endrunopen(dev_t dev, struct tty *tp, struct proc *p)
{
	struct endrun *np;
	int error;

	DPRINTF(("endrunopen\n"));
	if (tp->t_line == ENDRUNDISC)
		return ENODEV;
	if ((error = suser(p)) != 0)
		return error;
	np = malloc(sizeof(struct endrun), M_DEVBUF, M_WAITOK|M_ZERO);
	snprintf(np->timedev.xname, sizeof(np->timedev.xname), "endrun%d",
	    endrun_nxid++);
	endrun_count++;
	np->time.status = SENSOR_S_UNKNOWN;
	np->time.type = SENSOR_TIMEDELTA;
#ifndef ENDRUN_DEBUG
	np->time.flags = SENSOR_FINVALID;
#endif
	sensor_attach(&np->timedev, &np->time);

	np->signal.type = SENSOR_PERCENT;
	np->signal.status = SENSOR_S_UNKNOWN;
	np->signal.value = 100000LL;
	strlcpy(np->signal.desc, "Signal", sizeof(np->signal.desc));
	sensor_attach(&np->timedev, &np->signal);

	np->sync = SYNC_SCAN;
#ifdef ENDRUN_DEBUG
	np->tfom = '0';
#endif
	tp->t_sc = (caddr_t)np;

	error = linesw[TTYDISC].l_open(dev, tp, p);
	if (error) {
		free(np, M_DEVBUF, sizeof(*np));
		tp->t_sc = NULL;
	} else {
		sensordev_install(&np->timedev);
		timeout_set(&np->endrun_tout, endrun_timeout, np);
	}

	return error;
}

int
endrunclose(struct tty *tp, int flags, struct proc *p)
{
	struct endrun *np = (struct endrun *)tp->t_sc;

	DPRINTF(("endrunclose\n"));
	tp->t_line = TTYDISC;	/* switch back to termios */
	timeout_del(&np->endrun_tout);
	sensordev_deinstall(&np->timedev);
	free(np, M_DEVBUF, sizeof(*np));
	tp->t_sc = NULL;
	endrun_count--;
	if (endrun_count == 0)
		endrun_nxid = 0;
	return linesw[TTYDISC].l_close(tp, flags, p);
}

/* collect EndRun sentence from tty */
int
endruninput(int c, struct tty *tp)
{
	struct endrun *np = (struct endrun *)tp->t_sc;
	struct timespec ts;
	int64_t gap;
	long tmin, tmax;

	if (np->sync == SYNC_EOL) {
		nanotime(&ts);
		np->pos = 0;
		np->sync = SYNC_SCAN;
		np->cbuf[np->pos++] = c; /* TFOM char */

		gap = (ts.tv_sec * 1000000000LL + ts.tv_nsec) -
		    (np->lts.tv_sec * 1000000000LL + np->lts.tv_nsec);

		np->lts.tv_sec = ts.tv_sec;
		np->lts.tv_nsec = ts.tv_nsec;

		if (gap <= np->gap)
			goto nogap;

		np->ts.tv_sec = ts.tv_sec;
		np->ts.tv_nsec = ts.tv_nsec;
		np->gap = gap;

		/*
		 * If a tty timestamp is available, make sure its value is
		 * reasonable by comparing against the timestamp just taken.
		 * If they differ by more than 2 seconds, assume no PPS signal
		 * is present, note the fact, and keep using the timestamp
		 * value.  When this happens, the sensor state is set to
		 * CRITICAL later when the EndRun sentence is decoded.
		 */
		if (tp->t_flags & (TS_TSTAMPDCDSET | TS_TSTAMPDCDCLR |
		    TS_TSTAMPCTSSET | TS_TSTAMPCTSCLR)) {
			tmax = lmax(np->ts.tv_sec, tp->t_tv.tv_sec);
			tmin = lmin(np->ts.tv_sec, tp->t_tv.tv_sec);
			if (tmax - tmin > 1)
				np->no_pps = 1;
			else {
				np->ts.tv_sec = tp->t_tv.tv_sec;
				np->ts.tv_nsec = tp->t_tv.tv_usec *
				    1000L;
				np->no_pps = 0;
			}
		}
	} else if (c == '\n') {
		if (np->pos == ENDRUNLEN - 1) {
			/* don't copy '\n' into cbuf */
			np->cbuf[np->pos] = '\0';
			endrun_scan(np, tp);
		}
		np->sync = SYNC_EOL;
	} else {
		if (np->pos < ENDRUNLEN - 1)
			np->cbuf[np->pos++] = c;
	}

nogap:
	/* pass data to termios */
	return linesw[TTYDISC].l_rint(c, tp);
}

/* Scan the EndRun sentence just received */
void
endrun_scan(struct endrun *np, struct tty *tp)
{
	int fldcnt = 0, n;
	char *fld[NUMFLDS], *cs;

	DPRINTFN(1, ("%s\n", np->cbuf));
	/* split into fields */
	fld[fldcnt++] = &np->cbuf[0];
	for (cs = NULL, n = 0; n < np->pos && cs == NULL; n++) {
		switch (np->cbuf[n]) {
		case '\r':
			np->cbuf[n] = '\0';
			cs = &np->cbuf[n + 1];
			break;
		case ' ':
			if (fldcnt < NUMFLDS) {
				np->cbuf[n] = '\0';
				fld[fldcnt++] = &np->cbuf[n + 1];
			} else {
				DPRINTF(("endrun: nr of fields in sentence "
				    "exceeds expected: %d\n", NUMFLDS));
				return;
			}
			break;
		}
	}
	endrun_decode(np, tp, fld, fldcnt);
}

/* Decode the time string */
void
endrun_decode(struct endrun *np, struct tty *tp, char *fld[], int fldcnt)
{
	int64_t date_nano, time_nano, offset_nano, endrun_now;
	char tfom;
	int jumped = 0;

	if (fldcnt != NUMFLDS) {
		DPRINTF(("endrun: field count mismatch, %d\n", fldcnt));
		return;
	}
	if (endrun_time_to_nano(fld[3], &time_nano) == -1) {
		DPRINTF(("endrun: illegal time, %s\n", fld[3]));
		return;
	}
	if (endrun_date_to_nano(fld[1], fld[2], &date_nano) == -1) {
		DPRINTF(("endrun: illegal date, %s %s\n", fld[1], fld[2]));
		return;
	}
	offset_nano = 0;
	/* only parse offset when timemode is local */
	if (fld[5][0] == 'L' &&
	    endrun_offset_to_nano(fld[4], &offset_nano) == -1) {
		DPRINTF(("endrun: illegal offset, %s\n", fld[4]));
		return;
	}

	endrun_now = date_nano + time_nano + offset_nano;
	if (endrun_now <= np->last) {
		DPRINTF(("endrun: time not monotonically increasing "
		    "last %lld now %lld\n",
		    (long long)np->last, (long long)endrun_now));
		jumped = 1;
	}
	np->last = endrun_now;
	np->gap = 0LL;
#ifdef ENDRUN_DEBUG
	if (np->time.status == SENSOR_S_UNKNOWN) {
		np->time.status = SENSOR_S_OK;
		timeout_add_sec(&np->endrun_tout, TRUSTTIME);
	}
#endif

	np->time.value = np->ts.tv_sec * 1000000000LL +
	    np->ts.tv_nsec - endrun_now;
	np->time.tv.tv_sec = np->ts.tv_sec;
	np->time.tv.tv_usec = np->ts.tv_nsec / 1000L;
	if (np->time.status == SENSOR_S_UNKNOWN) {
		np->time.status = SENSOR_S_OK;
		np->time.flags &= ~SENSOR_FINVALID;
		strlcpy(np->time.desc, "EndRun", sizeof(np->time.desc));
	}
	/*
	 * Only update the timeout if the clock reports the time as valid.
	 *
	 * Time Figure Of Merit (TFOM) values:
	 *
	 * 6  - time error is < 100 us
	 * 7  - time error is < 1 ms
	 * 8  - time error is < 10 ms
	 * 9  - time error is > 10 ms,
	 *      unsynchronized state if never locked to CDMA
	 */

	switch (tfom = fld[0][0]) {
	case '6':
	case '7':
	case '8':
		np->time.status = SENSOR_S_OK;
		np->signal.status = SENSOR_S_OK;
		break;
	case '9':
		np->signal.status = SENSOR_S_WARN;
		break;
	default:
		DPRINTF(("endrun: invalid TFOM: '%c'\n", tfom));
		np->signal.status = SENSOR_S_CRIT;
		break;
	}

#ifdef ENDRUN_DEBUG
	if (np->tfom != tfom) {
		DPRINTF(("endrun: TFOM changed from %c to %c\n",
		    np->tfom, tfom));
		np->tfom = tfom;
	}
#endif
	if (jumped)
		np->time.status = SENSOR_S_WARN;
	if (np->time.status == SENSOR_S_OK)
		timeout_add_sec(&np->endrun_tout, TRUSTTIME);

	/*
	 * If tty timestamping is requested, but no PPS signal is present, set
	 * the sensor state to CRITICAL.
	 */
	if (np->no_pps)
		np->time.status = SENSOR_S_CRIT;
}

int
endrun_atoi(char *s, int len)
{
	int n;
	char *p;

	/* make sure the input contains only numbers */
	for (n = 0, p = s; n < len && *p && *p >= '0' && *p <= '9'; n++, p++)
		;
	if (n != len || *p != '\0')
		return -1;

	for (n = 0; *s; s++)
		n = n * 10 + *s - '0';

	return n;
}

/*
 * Convert date fields from EndRun to nanoseconds since the epoch.
 * The year string must be of the form YYYY .
 * The day of year string must be of the form DDD .
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
endrun_date_to_nano(char *y, char *doy, int64_t *nano)
{
	struct clock_ymdhms clock;
	time_t secs;
	int n, i;
	int year_days = 365;
	int month_days[] = {
		0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

#define FEBRUARY		2

#define LEAPYEAR(x)		\
	((x) % 4 == 0 &&	\
	(x) % 100 != 0) ||	\
	(x) % 400 == 0

	if ((n = endrun_atoi(y, 4)) == -1)
		return -1;
	clock.dt_year = n;

	if (LEAPYEAR(n)) {
		month_days[FEBRUARY]++;
		year_days++;
	}

	if ((n = endrun_atoi(doy, 3)) == -1 || n == 0 || n > year_days)
		return -1;

	/* convert day of year to month, day */
	for (i = 1; n > month_days[i]; i++) {
		n -= month_days[i];
	}
	clock.dt_mon = i;
	clock.dt_day = n;

	DPRINTFN(1, ("mm/dd %d/%d\n", i, n));

	clock.dt_hour = clock.dt_min = clock.dt_sec = 0;

	secs = clock_ymdhms_to_secs(&clock);
	*nano = secs * 1000000000LL;
	return 0;
}

/*
 * Convert time field from EndRun to nanoseconds since midnight.
 * The string must be of the form HH:MM:SS .
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
endrun_time_to_nano(char *s, int64_t *nano)
{
	struct clock_ymdhms clock;
	time_t secs;
	int n;

	if (s[2] != ':' || s[5] != ':')
		return -1;

	s[2] = '\0';
	s[5] = '\0';

	if ((n = endrun_atoi(&s[0], 2)) == -1 || n > 23)
		return -1;
	clock.dt_hour = n;
	if ((n = endrun_atoi(&s[3], 2)) == -1 || n > 59)
		return -1;
	clock.dt_min = n;
	if ((n = endrun_atoi(&s[6], 2)) == -1 || n > 60)
		return -1;
	clock.dt_sec = n;

	DPRINTFN(1, ("hh:mm:ss %d:%d:%d\n", (int)clock.dt_hour,
	    (int)clock.dt_min,
	    (int)clock.dt_sec));
	secs = clock.dt_hour * 3600
	    + clock.dt_min * 60
	    + clock.dt_sec;
	    
	DPRINTFN(1, ("secs %lu\n", (unsigned long)secs));

	*nano = secs * 1000000000LL;
	return 0;
}

int
endrun_offset_to_nano(char *s, int64_t *nano)
{
	time_t secs;
	int n;

	if (!(s[0] == '+' || s[0] == '-'))
		return -1;

	if ((n = endrun_atoi(&s[1], 2)) == -1)
		return -1;
	secs = n * 30 * 60;

	*nano = secs * 1000000000LL;
	if (s[0] == '+')
		*nano = -*nano;

	DPRINTFN(1, ("offset secs %lu nanosecs %lld\n",
	    (unsigned long)secs, (long long)*nano));

	return 0;
}

/*
 * Degrade the sensor state if we received no EndRun string for more than
 * TRUSTTIME seconds.
 */
void
endrun_timeout(void *xnp)
{
	struct endrun *np = xnp;

	if (np->time.status == SENSOR_S_OK) {
		np->time.status = SENSOR_S_WARN;
		/*
		 * further degrade in TRUSTTIME seconds if no new valid EndRun
		 * strings are received.
		 */
		timeout_add_sec(&np->endrun_tout, TRUSTTIME);
	} else
		np->time.status = SENSOR_S_CRIT;
}
