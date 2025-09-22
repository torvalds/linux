/*	$OpenBSD: tty_msts.c,v 1.21 2018/02/19 08:59:52 mpi Exp $ */

/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
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
 *  A tty line discipline to decode the Meinberg Standard Time String
 *  to get the time (http://www.meinberg.de/english/specs/timestr.htm).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/time.h>

#ifdef MSTS_DEBUG
#define DPRINTFN(n, x)	do { if (mstsdebug > (n)) printf x; } while (0)
int mstsdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

void	mstsattach(int);

#define MSTSMAX	32
#define MAXFLDS	4
#ifdef MSTS_DEBUG
#define TRUSTTIME	30
#else
#define TRUSTTIME	(10 * 60)	/* 10 minutes */
#endif

int msts_count, msts_nxid;

struct msts {
	char			cbuf[MSTSMAX];	/* receive buffer */
	struct ksensor		time;		/* the timedelta sensor */
	struct ksensor		signal;		/* signal status */
	struct ksensordev	timedev;
	struct timespec		ts;		/* current timestamp */
	struct timespec		lts;		/* timestamp of last <STX> */
	struct timeout		msts_tout;	/* invalidate sensor */
	int64_t			gap;		/* gap between two sentences */
	int64_t			last;		/* last time rcvd */
	int			sync;		/* if 1, waiting for <STX> */
	int			pos;		/* position in rcv buffer */
	int			no_pps;		/* no PPS although requested */
};

/* MSTS decoding */
void	msts_scan(struct msts *, struct tty *);
void	msts_decode(struct msts *, struct tty *, char *fld[], int fldcnt);

/* date and time conversion */
int	msts_date_to_nano(char *s, int64_t *nano);
int	msts_time_to_nano(char *s, int64_t *nano);

/* degrade the timedelta sensor */
void	msts_timeout(void *);

void
mstsattach(int dummy)
{
}

int
mstsopen(dev_t dev, struct tty *tp, struct proc *p)
{
	struct msts *np;
	int error;

	DPRINTF(("mstsopen\n"));
	if (tp->t_line == MSTSDISC)
		return ENODEV;
	if ((error = suser(p)) != 0)
		return error;
	np = malloc(sizeof(struct msts), M_DEVBUF, M_WAITOK|M_ZERO);
	snprintf(np->timedev.xname, sizeof(np->timedev.xname), "msts%d",
	    msts_nxid++);
	msts_count++;
	np->time.status = SENSOR_S_UNKNOWN;
	np->time.type = SENSOR_TIMEDELTA;
#ifndef MSTS_DEBUG
	np->time.flags = SENSOR_FINVALID;
#endif
	sensor_attach(&np->timedev, &np->time);

	np->signal.type = SENSOR_PERCENT;
	np->signal.status = SENSOR_S_UNKNOWN;
	np->signal.value = 100000LL;
	strlcpy(np->signal.desc, "Signal", sizeof(np->signal.desc));
	sensor_attach(&np->timedev, &np->signal);

	np->sync = 1;
	tp->t_sc = (caddr_t)np;

	error = linesw[TTYDISC].l_open(dev, tp, p);
	if (error) {
		free(np, M_DEVBUF, sizeof(*np));
		tp->t_sc = NULL;
	} else {
		sensordev_install(&np->timedev);
		timeout_set(&np->msts_tout, msts_timeout, np);
	}

	return error;
}

int
mstsclose(struct tty *tp, int flags, struct proc *p)
{
	struct msts *np = (struct msts *)tp->t_sc;

	tp->t_line = TTYDISC;	/* switch back to termios */
	timeout_del(&np->msts_tout);
	sensordev_deinstall(&np->timedev);
	free(np, M_DEVBUF, sizeof(*np));
	tp->t_sc = NULL;
	msts_count--;
	if (msts_count == 0)
		msts_nxid = 0;
	return linesw[TTYDISC].l_close(tp, flags, p);
}

/* collect MSTS sentence from tty */
int
mstsinput(int c, struct tty *tp)
{
	struct msts *np = (struct msts *)tp->t_sc;
	struct timespec ts;
	int64_t gap;
	long tmin, tmax;

	switch (c) {
	case 2:		/* ASCII <STX> */
		nanotime(&ts);
		np->pos = np->sync = 0;
		gap = (ts.tv_sec * 1000000000LL + ts.tv_nsec) -
		    (np->lts.tv_sec * 1000000000LL + np->lts.tv_nsec);

		np->lts.tv_sec = ts.tv_sec;
		np->lts.tv_nsec = ts.tv_nsec;

		if (gap <= np->gap)
			break;

		np->ts.tv_sec = ts.tv_sec;
		np->ts.tv_nsec = ts.tv_nsec;
		np->gap = gap;

		/*
		 * If a tty timestamp is available, make sure its value is
		 * reasonable by comparing against the timestamp just taken.
		 * If they differ by more than 2 seconds, assume no PPS signal
		 * is present, note the fact, and keep using the timestamp
		 * value.  When this happens, the sensor state is set to
		 * CRITICAL later when the MSTS sentence is decoded.
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
		break;
	case 3:		/* ASCII <ETX> */
		if (!np->sync) {
			np->cbuf[np->pos] = '\0';
			msts_scan(np, tp);
			np->sync = 1;
		}
		break;
	default:
		if (!np->sync && np->pos < (MSTSMAX - 1))
			np->cbuf[np->pos++] = c;
		break;
	}
	/* pass data to termios */
	return linesw[TTYDISC].l_rint(c, tp);
}

/* Scan the MSTS sentence just received */
void
msts_scan(struct msts *np, struct tty *tp)
{
	int fldcnt = 0, n;
	char *fld[MAXFLDS], *cs;

	/* split into fields */
	fld[fldcnt++] = &np->cbuf[0];
	for (cs = NULL, n = 0; n < np->pos && cs == NULL; n++) {
		switch (np->cbuf[n]) {
		case 3:		/* ASCII <ETX> */
			np->cbuf[n] = '\0';
			cs = &np->cbuf[n + 1];
			break;
		case ';':
			if (fldcnt < MAXFLDS) {
				np->cbuf[n] = '\0';
				fld[fldcnt++] = &np->cbuf[n + 1];
			} else {
				DPRINTF(("nr of fields in sentence exceeds "
				    "maximum of %d\n", MAXFLDS));
				return;
			}
			break;
		}
	}
	msts_decode(np, tp, fld, fldcnt);
}

/* Decode the time string */
void
msts_decode(struct msts *np, struct tty *tp, char *fld[], int fldcnt)
{
	int64_t date_nano, time_nano, msts_now;
	int jumped = 0;

	if (fldcnt != MAXFLDS) {
		DPRINTF(("msts: field count mismatch, %d\n", fldcnt));
		return;
	}
	if (msts_time_to_nano(fld[2], &time_nano)) {
		DPRINTF(("msts: illegal time, %s\n", fld[2]));
		return;
	}
	if (msts_date_to_nano(fld[0], &date_nano)) {
		DPRINTF(("msts: illegal date, %s\n", fld[0]));
		return;
	}
	msts_now = date_nano + time_nano;
	if ( fld[3][2] == ' ' )		/* received time in CET */
		msts_now = msts_now - 3600 * 1000000000LL;
	if ( fld[3][2] == 'S' )		/* received time in CEST */
		msts_now = msts_now - 2 * 3600 * 1000000000LL;
	if (msts_now <= np->last) {
		DPRINTF(("msts: time not monotonically increasing\n"));
		jumped = 1;
	}
	np->last = msts_now;
	np->gap = 0LL;
#ifdef MSTS_DEBUG
	if (np->time.status == SENSOR_S_UNKNOWN) {
		np->time.status = SENSOR_S_OK;
		timeout_add_sec(&np->msts_tout, TRUSTTIME);
	}
#endif

	np->time.value = np->ts.tv_sec * 1000000000LL +
	    np->ts.tv_nsec - msts_now;
	np->time.tv.tv_sec = np->ts.tv_sec;
	np->time.tv.tv_usec = np->ts.tv_nsec / 1000L;
	if (np->time.status == SENSOR_S_UNKNOWN) {
		np->time.status = SENSOR_S_OK;
		np->time.flags &= ~SENSOR_FINVALID;
		strlcpy(np->time.desc, "MSTS", sizeof(np->time.desc));
	}
	/*
	 * only update the timeout if the clock reports the time a valid,
	 * the status is reported in fld[3][0] and fld[3][1] as follows:
	 * fld[3][0] == '#'				critical
	 * fld[3][0] == ' ' && fld[3][1] == '*'		warning
	 * fld[3][0] == ' ' && fld[3][1] == ' '		ok
	 */
	if (fld[3][0] == ' ' && fld[3][1] == ' ') {
		np->time.status = SENSOR_S_OK;
		np->signal.status = SENSOR_S_OK;
	} else
		np->signal.status = SENSOR_S_WARN;

	if (jumped)
		np->time.status = SENSOR_S_WARN;
	if (np->time.status == SENSOR_S_OK)
		timeout_add_sec(&np->msts_tout, TRUSTTIME);

	/*
	 * If tty timestamping is requested, but no PPS signal is present, set
	 * the sensor state to CRITICAL.
	 */
	if (np->no_pps)
		np->time.status = SENSOR_S_CRIT;
}

/*
 * Convert date field from MSTS to nanoseconds since the epoch.
 * The string must be of the form D:DD.MM.YY .
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
msts_date_to_nano(char *s, int64_t *nano)
{
	struct clock_ymdhms ymd;
	time_t secs;
	char *p;
	int n;

	if (s[0] != 'D' || s[1] != ':' || s[4] != '.' || s[7] != '.')
		return -1;

	/* shift numbers to DDMMYY */
	s[0]=s[2];
	s[1]=s[3];
	s[2]=s[5];
	s[3]=s[6];
	s[4]=s[8];
	s[5]=s[9];
	s[6]='\0';

	/* make sure the input contains only numbers and is six digits long */
	for (n = 0, p = s; n < 6 && *p && *p >= '0' && *p <= '9'; n++, p++)
		;
	if (n != 6 || (*p != '\0'))
		return -1;

	ymd.dt_year = 2000 + (s[4] - '0') * 10 + (s[5] - '0');
	ymd.dt_mon = (s[2] - '0') * 10 + (s[3] - '0');
	ymd.dt_day = (s[0] - '0') * 10 + (s[1] - '0');
	ymd.dt_hour = ymd.dt_min = ymd.dt_sec = 0;

	secs = clock_ymdhms_to_secs(&ymd);
	*nano = secs * 1000000000LL;
	return 0;
}

/*
 * Convert time field from MSTS to nanoseconds since midnight.
 * The string must be of the form U:HH.MM.SS .
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
msts_time_to_nano(char *s, int64_t *nano)
{
	long fac = 36000L, div = 6L, secs = 0L;
	char ul = '2';
	int n;

	if (s[0] != 'U' || s[1] != ':' || s[4] != '.' || s[7] != '.')
		return -1;

	/* shift numbers to HHMMSS */
	s[0]=s[2];
	s[1]=s[3];
	s[2]=s[5];
	s[3]=s[6];
	s[4]=s[8];
	s[5]=s[9];
	s[6]='\0';

	for (n = 0, secs = 0; fac && *s && *s >= '0' && *s <= ul; s++, n++) {
		secs += (*s - '0') * fac;
		div = 16 - div;
		fac /= div;
		switch (n) {
		case 0:
			if (*s <= '1')
				ul = '9';
			else
				ul = '3';
			break;
		case 1:
		case 3:
			ul = '5';
			break;
		case 2:
		case 4:
			ul = '9';
			break;
		}
	}
	if (fac)
		return -1;

	if (*s != '\0')
		return -1;

	*nano = secs * 1000000000LL;
	return 0;
}

/*
 * Degrade the sensor state if we received no MSTS string for more than
 * TRUSTTIME seconds.
 */
void
msts_timeout(void *xnp)
{
	struct msts *np = xnp;

	if (np->time.status == SENSOR_S_OK) {
		np->time.status = SENSOR_S_WARN;
		/*
		 * further degrade in TRUSTTIME seconds if no new valid MSTS
		 * strings are received.
		 */
		timeout_add_sec(&np->msts_tout, TRUSTTIME);
	} else
		np->time.status = SENSOR_S_CRIT;
}
