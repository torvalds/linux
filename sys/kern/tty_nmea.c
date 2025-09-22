/*	$OpenBSD: tty_nmea.c,v 1.51 2022/04/02 22:45:18 mlarkin Exp $ */

/*
 * Copyright (c) 2006, 2007, 2008 Marc Balmer <mbalmer@openbsd.org>
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
 * A tty line discipline to decode NMEA 0183 data to get the time
 * and GPS position data
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/time.h>

#ifdef NMEA_DEBUG
#define DPRINTFN(n, x)	do { if (nmeadebug > (n)) printf x; } while (0)
int nmeadebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

void	nmeaattach(int);

#define NMEAMAX		82
#define MAXFLDS		32
#define KNOTTOMS	(51444 / 100)
#ifdef NMEA_DEBUG
#define TRUSTTIME	30
#else
#define TRUSTTIME	(10 * 60)	/* 10 minutes */
#endif

int nmea_count, nmea_nxid;

struct nmea {
	char			cbuf[NMEAMAX];	/* receive buffer */
	struct ksensor		time;		/* the timedelta sensor */
	struct ksensor		signal;		/* signal status */
	struct ksensor		latitude;
	struct ksensor		longitude;
	struct ksensor		altitude;
	struct ksensor		speed;
	struct ksensordev	timedev;
	struct timespec		ts;		/* current timestamp */
	struct timespec		lts;		/* timestamp of last '$' seen */
	struct timeout		nmea_tout;	/* invalidate sensor */
	int64_t			gap;		/* gap between two sentences */
#ifdef NMEA_DEBUG
	int			gapno;
#endif
	int64_t			last;		/* last time rcvd */
	int			sync;		/* if 1, waiting for '$' */
	int			pos;		/* position in rcv buffer */
	int			no_pps;		/* no PPS although requested */
	char			mode;		/* GPS mode */
};

/* NMEA decoding */
void	nmea_scan(struct nmea *, struct tty *);
void	nmea_gprmc(struct nmea *, struct tty *, char *fld[], int fldcnt);
void	nmea_decode_gga(struct nmea *, struct tty *, char *fld[], int fldcnt);

/* date and time conversion */
int	nmea_date_to_nano(char *s, int64_t *nano);
int	nmea_time_to_nano(char *s, int64_t *nano);

/* longitude and latitude conversion */
int	nmea_degrees(int64_t *dst, char *src, int neg);
int	nmea_atoi(int64_t *dst, char *src);

/* degrade the timedelta sensor */
void	nmea_timeout(void *);

void
nmeaattach(int dummy)
{
	/* noop */
}

int
nmeaopen(dev_t dev, struct tty *tp, struct proc *p)
{
	struct nmea *np;
	int error;

	if (tp->t_line == NMEADISC)
		return (ENODEV);
	if ((error = suser(p)) != 0)
		return (error);
	np = malloc(sizeof(struct nmea), M_DEVBUF, M_WAITOK | M_ZERO);
	snprintf(np->timedev.xname, sizeof(np->timedev.xname), "nmea%d",
	    nmea_nxid++);
	nmea_count++;
	np->time.status = SENSOR_S_UNKNOWN;
	np->time.type = SENSOR_TIMEDELTA;
	np->time.flags = SENSOR_FINVALID;
	sensor_attach(&np->timedev, &np->time);

	np->signal.type = SENSOR_INDICATOR;
	np->signal.status = SENSOR_S_UNKNOWN;
	np->signal.value = 0;
	strlcpy(np->signal.desc, "Signal", sizeof(np->signal.desc));
	sensor_attach(&np->timedev, &np->signal);

	np->latitude.type = SENSOR_ANGLE;
	np->latitude.status = SENSOR_S_UNKNOWN;
	np->latitude.flags = SENSOR_FINVALID;
	np->latitude.value = 0;
	strlcpy(np->latitude.desc, "Latitude", sizeof(np->latitude.desc));
	sensor_attach(&np->timedev, &np->latitude);

	np->longitude.type = SENSOR_ANGLE;
	np->longitude.status = SENSOR_S_UNKNOWN;
	np->longitude.flags = SENSOR_FINVALID;
	np->longitude.value = 0;
	strlcpy(np->longitude.desc, "Longitude", sizeof(np->longitude.desc));
	sensor_attach(&np->timedev, &np->longitude);

	np->altitude.type = SENSOR_DISTANCE;
	np->altitude.status = SENSOR_S_UNKNOWN;
	np->altitude.flags = SENSOR_FINVALID;
	np->altitude.value = 0;
	strlcpy(np->altitude.desc, "Altitude", sizeof(np->altitude.desc));
	sensor_attach(&np->timedev, &np->altitude);

	np->speed.type = SENSOR_VELOCITY;
	np->speed.status = SENSOR_S_UNKNOWN;
	np->speed.flags = SENSOR_FINVALID;
	np->speed.value = 0;
	strlcpy(np->speed.desc, "Ground speed", sizeof(np->speed.desc));
	sensor_attach(&np->timedev, &np->speed);

	np->sync = 1;
	tp->t_sc = (caddr_t)np;

	error = linesw[TTYDISC].l_open(dev, tp, p);
	if (error) {
		free(np, M_DEVBUF, sizeof(*np));
		tp->t_sc = NULL;
	} else {
		sensordev_install(&np->timedev);
		timeout_set(&np->nmea_tout, nmea_timeout, np);
	}
	return (error);
}

int
nmeaclose(struct tty *tp, int flags, struct proc *p)
{
	struct nmea *np = (struct nmea *)tp->t_sc;

	tp->t_line = TTYDISC;	/* switch back to termios */
	timeout_del(&np->nmea_tout);
	sensordev_deinstall(&np->timedev);
	free(np, M_DEVBUF, sizeof(*np));
	tp->t_sc = NULL;
	nmea_count--;
	if (nmea_count == 0)
		nmea_nxid = 0;
	return (linesw[TTYDISC].l_close(tp, flags, p));
}

/* Collect NMEA sentences from the tty. */
int
nmeainput(int c, struct tty *tp)
{
	struct nmea *np = (struct nmea *)tp->t_sc;
	struct timespec ts;
	int64_t gap;
	long tmin, tmax;

	switch (c) {
	case '$':
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

#ifdef NMEA_DEBUG
		if (nmeadebug > 0) {
			linesw[TTYDISC].l_rint('[', tp);
			linesw[TTYDISC].l_rint('0' + np->gapno++, tp);
			linesw[TTYDISC].l_rint(']', tp);
		}
#endif
		np->gap = gap;

		/*
		 * If a tty timestamp is available, make sure its value is
		 * reasonable by comparing against the timestamp just taken.
		 * If they differ by more than 2 seconds, assume no PPS signal
		 * is present, note the fact, and keep using the timestamp
		 * value.  When this happens, the sensor state is set to
		 * CRITICAL later when the GPRMC sentence is decoded.
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
	case '\r':
	case '\n':
		if (!np->sync) {
			np->cbuf[np->pos] = '\0';
			nmea_scan(np, tp);
			np->sync = 1;
		}
		break;
	default:
		if (!np->sync && np->pos < (NMEAMAX - 1))
			np->cbuf[np->pos++] = c;
		break;
	}
	/* pass data to termios */
	return (linesw[TTYDISC].l_rint(c, tp));
}

/* Scan the NMEA sentence just received. */
void
nmea_scan(struct nmea *np, struct tty *tp)
{
	int fldcnt = 0, cksum = 0, msgcksum, n;
	char *fld[MAXFLDS], *cs;

	/* split into fields and calculate the checksum */
	fld[fldcnt++] = &np->cbuf[0];	/* message type */
	for (cs = NULL, n = 0; n < np->pos && cs == NULL; n++) {
		switch (np->cbuf[n]) {
		case '*':
			np->cbuf[n] = '\0';
			cs = &np->cbuf[n + 1];
			break;
		case ',':
			if (fldcnt < MAXFLDS) {
				cksum ^= np->cbuf[n];
				np->cbuf[n] = '\0';
				fld[fldcnt++] = &np->cbuf[n + 1];
			} else {
				DPRINTF(("nr of fields in %s sentence exceeds "
				    "maximum of %d\n", fld[0], MAXFLDS));
				return;
			}
			break;
		default:
			cksum ^= np->cbuf[n];
		}
	}

	/*
	 * we only look at the messages coming from well-known sources or 'talkers',
	 * distinguished by the two-chars prefix, the most common being:
	 * GPS (GP)
	 * Glonass (GL)
	 * BeiDou (BD)
	 * Galileo (GA)
	 * 'Any kind/a mix of GNSS systems' (GN)
	 */
	if (strncmp(fld[0], "BD", 2) &&
	    strncmp(fld[0], "GA", 2) &&
	    strncmp(fld[0], "GL", 2) &&
	    strncmp(fld[0], "GN", 2) &&
	    strncmp(fld[0], "GP", 2))
		return;

	/* we look for the RMC & GGA messages */
	if (strncmp(fld[0] + 2, "RMC", 3) &&
	    strncmp(fld[0] + 2, "GGA", 3))
		return;

	/* if we have a checksum, verify it */
	if (cs != NULL) {
		msgcksum = 0;
		while (*cs) {
			if ((*cs >= '0' && *cs <= '9') ||
			    (*cs >= 'A' && *cs <= 'F')) {
				if (msgcksum)
					msgcksum <<= 4;
				if (*cs >= '0' && *cs<= '9')
					msgcksum += *cs - '0';
				else if (*cs >= 'A' && *cs <= 'F')
					msgcksum += 10 + *cs - 'A';
				cs++;
			} else {
				DPRINTF(("bad char %c in checksum\n", *cs));
				return;
			}
		}
		if (msgcksum != cksum) {
			DPRINTF(("checksum mismatch\n"));
			return;
		}
	}
	if (strncmp(fld[0] + 2, "RMC", 3) == 0)
		nmea_gprmc(np, tp, fld, fldcnt);
	if (strncmp(fld[0] + 2, "GGA", 3) == 0)
		nmea_decode_gga(np, tp, fld, fldcnt);
}

/* Decode the recommended minimum specific GPS/TRANSIT data. */
void
nmea_gprmc(struct nmea *np, struct tty *tp, char *fld[], int fldcnt)
{
	int64_t date_nano, time_nano, nmea_now;
	int jumped = 0;

	if (fldcnt < 12 || fldcnt > 14) {
		DPRINTF(("gprmc: field count mismatch, %d\n", fldcnt));
		return;
	}
	if (nmea_time_to_nano(fld[1], &time_nano)) {
		DPRINTF(("gprmc: illegal time, %s\n", fld[1]));
		return;
	}
	if (nmea_date_to_nano(fld[9], &date_nano)) {
		DPRINTF(("gprmc: illegal date, %s\n", fld[9]));
		return;
	}
	nmea_now = date_nano + time_nano;
	if (nmea_now <= np->last) {
		DPRINTF(("gprmc: time not monotonically increasing\n"));
		jumped = 1;
	}
	np->last = nmea_now;
	np->gap = 0LL;
#ifdef NMEA_DEBUG
	if (np->time.status == SENSOR_S_UNKNOWN) {
		np->time.status = SENSOR_S_OK;
		timeout_add_sec(&np->nmea_tout, TRUSTTIME);
	}
	np->gapno = 0;
	if (nmeadebug > 0) {
		linesw[TTYDISC].l_rint('[', tp);
		linesw[TTYDISC].l_rint('C', tp);
		linesw[TTYDISC].l_rint(']', tp);
	}
#endif

	np->time.value = np->ts.tv_sec * 1000000000LL +
	    np->ts.tv_nsec - nmea_now;
	np->time.tv.tv_sec = np->ts.tv_sec;
	np->time.tv.tv_usec = np->ts.tv_nsec / 1000L;

	if (fldcnt < 13)
		strlcpy(np->time.desc, "GPS", sizeof(np->time.desc));
	else if (*fld[12] != np->mode) {
		np->mode = *fld[12];
		switch (np->mode) {
		case 'S':
			strlcpy(np->time.desc, "GPS simulated",
			    sizeof(np->time.desc));
			break;
		case 'E':
			strlcpy(np->time.desc, "GPS estimated",
			    sizeof(np->time.desc));
			break;
		case 'A':
			strlcpy(np->time.desc, "GPS autonomous",
			    sizeof(np->time.desc));
			break;
		case 'D':
			strlcpy(np->time.desc, "GPS differential",
			    sizeof(np->time.desc));
			break;
		case 'N':
			strlcpy(np->time.desc, "GPS invalid",
			    sizeof(np->time.desc));
			break;
		default:
			strlcpy(np->time.desc, "GPS unknown",
			    sizeof(np->time.desc));
			DPRINTF(("gprmc: unknown mode '%c'\n", np->mode));
		}
	}
	switch (*fld[2]) {
	case 'A':	/* The GPS has a fix, (re)arm the timeout. */
			/* XXX is 'D' also a valid state? */
		np->time.status = SENSOR_S_OK;
		np->signal.value = 1;
		np->signal.status = SENSOR_S_OK;
		np->latitude.status = SENSOR_S_OK;
		np->longitude.status = SENSOR_S_OK;
		np->speed.status = SENSOR_S_OK;
		np->time.flags &= ~SENSOR_FINVALID;
		np->latitude.flags &= ~SENSOR_FINVALID;
		np->longitude.flags &= ~SENSOR_FINVALID;
		np->speed.flags &= ~SENSOR_FINVALID;
		break;
	case 'V':	/*
			 * The GPS indicates a warning status, do not add to
			 * the timeout, if the condition persist, the sensor
			 * will be degraded.  Signal the condition through
			 * the signal sensor.
			 */
		np->signal.value = 0;
		np->signal.status = SENSOR_S_CRIT;
		np->latitude.status = SENSOR_S_WARN;
		np->longitude.status = SENSOR_S_WARN;
		np->speed.status = SENSOR_S_WARN;
		break;
	}
	if (nmea_degrees(&np->latitude.value, fld[3], *fld[4] == 'S' ? 1 : 0))
		np->latitude.status = SENSOR_S_WARN;
	if (nmea_degrees(&np->longitude.value,fld[5], *fld[6] == 'W' ? 1 : 0))
		np->longitude.status = SENSOR_S_WARN;

	if (nmea_atoi(&np->speed.value, fld[7]))
		np->speed.status = SENSOR_S_WARN;
	/* convert from knot to um/s */
	np->speed.value *= KNOTTOMS;

	if (jumped)
		np->time.status = SENSOR_S_WARN;
	if (np->time.status == SENSOR_S_OK)
		timeout_add_sec(&np->nmea_tout, TRUSTTIME);
	/*
	 * If tty timestamping is requested, but no PPS signal is present, set
	 * the sensor state to CRITICAL.
	 */
	if (np->no_pps)
		np->time.status = SENSOR_S_CRIT;
}

/* Decode the GPS fix data for altitude.
 * - field 9 is the altitude in meters
 * $GNGGA,085901.00,1234.5678,N,00987.12345,E,1,12,0.84,1040.9,M,47.4,M,,*4B
 */
void
nmea_decode_gga(struct nmea *np, struct tty *tp, char *fld[], int fldcnt)
{
	if (fldcnt != 15) {
		DPRINTF(("GGA: field count mismatch, %d\n", fldcnt));
		return;
	}
#ifdef NMEA_DEBUG
	if (nmeadebug > 0) {
		linesw[TTYDISC].l_rint('[', tp);
		linesw[TTYDISC].l_rint('C', tp);
		linesw[TTYDISC].l_rint(']', tp);
	}
#endif

	np->altitude.status = SENSOR_S_OK;
	if (nmea_atoi(&np->altitude.value, fld[9]))
		np->altitude.status = SENSOR_S_WARN;

	/* convert to uMeter */
	np->altitude.value *= 1000;
	np->altitude.flags &= ~SENSOR_FINVALID;
}

/*
 * Convert nmea integer/decimal values in the form of XXXX.Y to an integer value
 * if it's a meter/altitude value, will be returned as mm
 */
int
nmea_atoi(int64_t *dst, char *src)
{
	char *p;
	int i = 3; /* take 3 digits */
	*dst = 0;

	for (p = src; *p && *p != '.' && *p >= '0' && *p <= '9' ; )
		*dst = *dst * 10 + (*p++ - '0');

	/* *p should be '.' at that point */
	if (*p != '.')
		return -1;	/* no decimal point, or bogus value ? */
	p++;

	/* read digits after decimal point, stop at first non-digit */
	for (; *p && i > 0 && *p >= '0' && *p <= '9' ; i--)
		*dst = *dst * 10 + (*p++ - '0');

	for (; i > 0 ; i--)
		*dst *= 10;

	DPRINTFN(2,("%s -> %lld\n", src, *dst));
	return 0;
}

/*
 * Convert a nmea position in the form DDDMM.MMMM to an
 * angle sensor value (degrees*1000000)
 */
int
nmea_degrees(int64_t *dst, char *src, int neg)
{
	size_t ppos;
	int i, n;
	int64_t deg = 0, min = 0;
	char *p;

	while (*src == '0')
		++src;	/* skip leading zeroes */

	for (p = src, ppos = 0; *p; ppos++)
		if (*p++ == '.')
			break;

	if (*p == '\0')
		return (-1);	/* no decimal point */

	for (n = 0; *src && n + 2 < ppos; n++)
		deg = deg * 10 + (*src++ - '0');

	for (; *src && n < ppos; n++)
		min = min * 10 + (*src++ - '0');

	src++;		/* skip decimal point */

	for (; *src && n < (ppos + 4); n++)
		min = min * 10 + (*src++ - '0');

	for (i=0; i < 6 + ppos - n; i++)
		min *= 10;

	deg = deg * 1000000 + (min/60);

	*dst = neg ? -deg : deg;
	return (0);
}

/*
 * Convert a NMEA 0183 formatted date string to seconds since the epoch.
 * The string must be of the form DDMMYY.
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
nmea_date_to_nano(char *s, int64_t *nano)
{
	struct clock_ymdhms ymd;
	time_t secs;
	char *p;
	int n;

	/* make sure the input contains only numbers and is six digits long */
	for (n = 0, p = s; n < 6 && *p && *p >= '0' && *p <= '9'; n++, p++)
		;
	if (n != 6 || (*p != '\0'))
		return (-1);

	ymd.dt_year = 2000 + (s[4] - '0') * 10 + (s[5] - '0');
	ymd.dt_mon = (s[2] - '0') * 10 + (s[3] - '0');
	ymd.dt_day = (s[0] - '0') * 10 + (s[1] - '0');
	ymd.dt_hour = ymd.dt_min = ymd.dt_sec = 0;

	secs = clock_ymdhms_to_secs(&ymd);
	*nano = secs * 1000000000LL;
	return (0);
}

/*
 * Convert NMEA 0183 formatted time string to nanoseconds since midnight.
 * The string must be of the form HHMMSS[.[sss]] (e.g. 143724 or 143723.615).
 * Return 0 on success, -1 if illegal characters are encountered.
 */
int
nmea_time_to_nano(char *s, int64_t *nano)
{
	long fac = 36000L, div = 6L, secs = 0L, frac = 0L;
	char ul = '2';
	int n;

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
		return (-1);

	/* Handle the fractions of a second, up to a maximum of 6 digits. */
	div = 1L;
	if (*s == '.') {
		for (++s; div < 1000000 && *s && *s >= '0' && *s <= '9'; s++) {
			frac *= 10;
			frac += (*s - '0');
			div *= 10;
		}
	}

	if (*s != '\0')
		return (-1);

	*nano = secs * 1000000000LL + (int64_t)frac * (1000000000 / div);
	return (0);
}

/*
 * Degrade the sensor state if we received no NMEA sentences for more than
 * TRUSTTIME seconds.
 */
void
nmea_timeout(void *xnp)
{
	struct nmea *np = xnp;

	np->signal.value = 0;
	np->signal.status = SENSOR_S_CRIT;
	if (np->time.status == SENSOR_S_OK) {
		np->time.status = SENSOR_S_WARN;
		np->latitude.status = SENSOR_S_WARN;
		np->longitude.status = SENSOR_S_WARN;
		np->altitude.status = SENSOR_S_WARN;
		np->speed.status = SENSOR_S_WARN;
		/*
		 * further degrade in TRUSTTIME seconds if no new valid NMEA
		 * sentences are received.
		 */
		timeout_add_sec(&np->nmea_tout, TRUSTTIME);
	} else {
		np->time.status = SENSOR_S_CRIT;
		np->latitude.status = SENSOR_S_CRIT;
		np->longitude.status = SENSOR_S_CRIT;
		np->altitude.status = SENSOR_S_CRIT;
		np->speed.status = SENSOR_S_CRIT;
	}
}
