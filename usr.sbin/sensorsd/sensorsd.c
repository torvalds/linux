/*	$OpenBSD: sensorsd.c,v 1.69 2023/03/08 04:43:15 guenther Exp $ */

/*
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2005 Matthew Gream <matthew.gream@pobox.com>
 * Copyright (c) 2006 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/sensors.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#define	RFBUFSIZ	28	/* buffer size for print_sensor */
#define	RFBUFCNT	4	/* ring buffers */
#define CHECK_PERIOD	20	/* check every n seconds */

enum sensorsd_s_status {
	SENSORSD_S_UNSPEC,	/* status is unspecified */
	SENSORSD_S_INVALID,	/* status is invalid, per SENSOR_FINVALID */
	SENSORSD_S_WITHIN,	/* status is within limits */
	SENSORSD_S_ABOVE,	/* status is above the higher limit */
	SENSORSD_S_BELOW	/* status is below the lower limit */
};

struct limits_t {
	TAILQ_ENTRY(limits_t)	entries;
	enum sensor_type	type;		/* sensor type */
	int			numt;		/* sensor number */
	int64_t			last_val;
	int64_t			lower;		/* lower limit */
	int64_t			upper;		/* upper limit */
	char			*command;	/* failure command */
	time_t			astatus_changed;
	time_t			ustatus_changed;
	enum sensor_status	astatus;	/* last automatic status */
	enum sensor_status	astatus2;
	enum sensorsd_s_status	ustatus;	/* last user-limit status */
	enum sensorsd_s_status	ustatus2;
	int			acount;		/* stat change counter */
	int			ucount;		/* stat change counter */
	u_int8_t		flags;		/* sensorsd limit flags */
#define SENSORSD_L_USERLIMIT		0x0001	/* user specified limit */
#define SENSORSD_L_ISTATUS		0x0002	/* ignore automatic status */
};

struct sdlim_t {
	TAILQ_ENTRY(sdlim_t)	entries;
	char			dxname[16];	/* device unix name */
	int			dev;		/* device number */
	int			sensor_cnt;
	TAILQ_HEAD(, limits_t)	limits;
};

void		 usage(void);
void		 create(void);
struct sdlim_t	*create_sdlim(struct sensordev *);
void		 destroy_sdlim(struct sdlim_t *);
void		 check(time_t);
void		 check_sdlim(struct sdlim_t *, time_t);
void		 execute(char *);
void		 report(time_t);
void		 report_sdlim(struct sdlim_t *, time_t);
static char	*print_sensor(enum sensor_type, int64_t);
void		 parse_config(char *);
void		 parse_config_sdlim(struct sdlim_t *, char *);
int64_t		 get_val(char *, int, enum sensor_type);
void		 reparse_cfg(int);

TAILQ_HEAD(sdlimhead_t, sdlim_t);
struct sdlimhead_t sdlims = TAILQ_HEAD_INITIALIZER(sdlims);

char			 *configfile, *configdb;
volatile sig_atomic_t	  reload = 0;
int			  debug = 0;

void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-d] [-c check] [-f file]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	time_t		 last_report = 0, this_check;
	int		 ch, check_period = CHECK_PERIOD;
	const char	*errstr;

	while ((ch = getopt(argc, argv, "c:df:")) != -1) {
		switch (ch) {
		case 'c':
			check_period = strtonum(optarg, 1, 600, &errstr);
			if (errstr)
				errx(1, "check %s", errstr);
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			configfile = realpath(optarg, NULL);
			if (configfile == NULL)
				err(1, "configuration file %s", optarg);
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (configfile == NULL)
		if (asprintf(&configfile, "/etc/sensorsd.conf") == -1)
			err(1, "out of memory");
	if (asprintf(&configdb, "%s.db", configfile) == -1)
		err(1, "out of memory");

	chdir("/");
	if (unveil(configfile, "r") == -1)
		err(1, "unveil %s", configfile);
	if (unveil(configdb, "r") == -1)
		err(1, "unveil %s", configdb);
	if (unveil("/", "x") == -1)
		err(1, "unveil /");

	if (pledge("stdio rpath proc exec", NULL) == -1)
		err(1, "pledge");

	openlog("sensorsd", LOG_PID | LOG_NDELAY, LOG_DAEMON);

	create();

	parse_config(configfile);

	if (debug == 0 && daemon(1, 0) == -1)
		err(1, "unable to fork");

	signal(SIGHUP, reparse_cfg);
	signal(SIGCHLD, SIG_IGN);

	for (;;) {
		if (reload) {
			parse_config(configfile);
			syslog(LOG_INFO, "configuration reloaded");
			reload = 0;
		}
		this_check = time(NULL);
		if (!(last_report < this_check))
			this_check = last_report + 1;
		check(this_check);
		report(last_report);
		last_report = this_check;
		sleep(check_period);
	}
}

void
create(void)
{
	struct sensordev sensordev;
	struct sdlim_t	*sdlim;
	size_t		 sdlen = sizeof(sensordev);
	int		 mib[3], dev, sensor_cnt = 0;

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;

	for (dev = 0; ; dev++) {
		mib[2] = dev;
		if (sysctl(mib, 3, &sensordev, &sdlen, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			warn("sysctl");
		}
		sdlim = create_sdlim(&sensordev);
		TAILQ_INSERT_TAIL(&sdlims, sdlim, entries);
		sensor_cnt += sdlim->sensor_cnt;
	}

	syslog(LOG_INFO, "startup, system has %d sensors", sensor_cnt);
}

struct sdlim_t *
create_sdlim(struct sensordev *snsrdev)
{
	struct sensor	 sensor;
	struct sdlim_t	*sdlim;
	struct limits_t	*limit;
	size_t		 slen = sizeof(sensor);
	int		 mib[5], numt;
	enum sensor_type type;

	if ((sdlim = calloc(1, sizeof(struct sdlim_t))) == NULL)
		err(1, "calloc");

	strlcpy(sdlim->dxname, snsrdev->xname, sizeof(sdlim->dxname));

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	mib[2] = sdlim->dev = snsrdev->num;

	TAILQ_INIT(&sdlim->limits);

	for (type = 0; type < SENSOR_MAX_TYPES; type++) {
		mib[3] = type;
		for (numt = 0; numt < snsrdev->maxnumt[type]; numt++) {
			mib[4] = numt;
			if (sysctl(mib, 5, &sensor, &slen, NULL, 0) == -1) {
				if (errno != ENOENT)
					warn("sysctl");
				continue;
			}
			if ((limit = calloc(1, sizeof(struct limits_t))) ==
			    NULL)
				err(1, "calloc");
			limit->type = type;
			limit->numt = numt;
			TAILQ_INSERT_TAIL(&sdlim->limits, limit, entries);
			sdlim->sensor_cnt++;
		}
	}

	return (sdlim);
}

void
destroy_sdlim(struct sdlim_t *sdlim)
{
	struct limits_t		*limit;

	while ((limit = TAILQ_FIRST(&sdlim->limits)) != NULL) {
		TAILQ_REMOVE(&sdlim->limits, limit, entries);
		free(limit->command);
		free(limit);
	}
	free(sdlim);
}

void
check(time_t this_check)
{
	struct sensordev	 sensordev;
	struct sdlim_t		*sdlim, *next;
	int			 mib[3];
	int			 h, t, i;
	size_t			 sdlen = sizeof(sensordev);

	if (TAILQ_EMPTY(&sdlims)) {
		h = 0;
		t = -1;
	} else {
		h = TAILQ_FIRST(&sdlims)->dev;
		t = TAILQ_LAST(&sdlims, sdlimhead_t)->dev;
	}
	sdlim = TAILQ_FIRST(&sdlims);

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	/* look ahead for 4 more sensordevs */
	for (i = h; i <= t + 4; i++) {
		if (sdlim != NULL && i > sdlim->dev)
			sdlim = TAILQ_NEXT(sdlim, entries);
		if (sdlim == NULL && i <= t)
			syslog(LOG_ALERT, "inconsistent sdlim logic");
		mib[2] = i;
		if (sysctl(mib, 3, &sensordev, &sdlen, NULL, 0) == -1) {
			if (errno != ENOENT)
				warn("sysctl");
			if (sdlim != NULL && i == sdlim->dev) {
				next = TAILQ_NEXT(sdlim, entries);
				TAILQ_REMOVE(&sdlims, sdlim, entries);
				syslog(LOG_INFO, "%s has disappeared",
				    sdlim->dxname);
				destroy_sdlim(sdlim);
				sdlim = next;
			}
			continue;
		}
		if (sdlim != NULL && i == sdlim->dev) {
			if (strcmp(sdlim->dxname, sensordev.xname) == 0) {
				check_sdlim(sdlim, this_check);
				continue;
			} else {
				next = TAILQ_NEXT(sdlim, entries);
				TAILQ_REMOVE(&sdlims, sdlim, entries);
				syslog(LOG_INFO, "%s has been replaced",
				    sdlim->dxname);
				destroy_sdlim(sdlim);
				sdlim = next;
			}
		}
		next = create_sdlim(&sensordev);
		/* inserting next before sdlim */
		if (sdlim != NULL)
			TAILQ_INSERT_BEFORE(sdlim, next, entries);
		else
			TAILQ_INSERT_TAIL(&sdlims, next, entries);
		syslog(LOG_INFO, "%s has appeared", next->dxname);
		sdlim = next;
		parse_config_sdlim(sdlim, configfile);
		check_sdlim(sdlim, this_check);
	}

	if (TAILQ_EMPTY(&sdlims))
		return;
	/* Ensure that our queue is consistent. */
	for (sdlim = TAILQ_FIRST(&sdlims);
	    (next = TAILQ_NEXT(sdlim, entries)) != NULL;
	    sdlim = next)
		if (sdlim->dev > next->dev)
			syslog(LOG_ALERT, "inconsistent sdlims queue");
}

void
check_sdlim(struct sdlim_t *sdlim, time_t this_check)
{
	struct sensor		 sensor;
	struct limits_t		*limit;
	size_t		 	 len;
	int		 	 mib[5];

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	mib[2] = sdlim->dev;
	len = sizeof(sensor);

	TAILQ_FOREACH(limit, &sdlim->limits, entries) {
		if ((limit->flags & SENSORSD_L_ISTATUS) &&
		    !(limit->flags & SENSORSD_L_USERLIMIT))
			continue;

		mib[3] = limit->type;
		mib[4] = limit->numt;
		if (sysctl(mib, 5, &sensor, &len, NULL, 0) == -1)
			err(1, "sysctl");

		if (!(limit->flags & SENSORSD_L_ISTATUS)) {
			enum sensor_status	newastatus = sensor.status;

			if (limit->astatus != newastatus) {
				if (limit->astatus2 != newastatus) {
					limit->astatus2 = newastatus;
					limit->acount = 0;
				} else if (++limit->acount >= 3) {
					limit->last_val = sensor.value;
					limit->astatus2 =
					    limit->astatus = newastatus;
					limit->astatus_changed = this_check;
				}
			}
		}

		if (limit->flags & SENSORSD_L_USERLIMIT) {
			enum sensorsd_s_status 	 newustatus;

			if (sensor.flags & SENSOR_FINVALID)
				newustatus = SENSORSD_S_INVALID;
			else if (sensor.value > limit->upper)
				newustatus = SENSORSD_S_ABOVE;
			else if (sensor.value < limit->lower)
				newustatus = SENSORSD_S_BELOW;
			else
				newustatus = SENSORSD_S_WITHIN;

			if (limit->ustatus != newustatus) {
				if (limit->ustatus2 != newustatus) {
					limit->ustatus2 = newustatus;
					limit->ucount = 0;
				} else if (++limit->ucount >= 3) {
					limit->last_val = sensor.value;
					limit->ustatus2 =
					    limit->ustatus = newustatus;
					limit->ustatus_changed = this_check;
				}
			}
		}
	}
}

void
execute(char *command)
{
	char *argp[] = {"sh", "-c", command, NULL};

	switch (fork()) {
	case -1:
		syslog(LOG_CRIT, "execute: fork() failed");
		break;
	case 0:
		execv("/bin/sh", argp);
		_exit(1);
		/* NOTREACHED */
	default:
		break;
	}
}

void
report(time_t last_report)
{
	struct sdlim_t	*sdlim;

	TAILQ_FOREACH(sdlim, &sdlims, entries)
		report_sdlim(sdlim, last_report);
}

void
report_sdlim(struct sdlim_t *sdlim, time_t last_report)
{
	struct limits_t	*limit;

	TAILQ_FOREACH(limit, &sdlim->limits, entries) {
		if ((limit->astatus_changed <= last_report) &&
		    (limit->ustatus_changed <= last_report))
			continue;

		if (limit->astatus_changed > last_report) {
			const char *as = NULL;

			switch (limit->astatus) {
			case SENSOR_S_UNSPEC:
				as = "";
				break;
			case SENSOR_S_OK:
				as = ", OK";
				break;
			case SENSOR_S_WARN:
				as = ", WARN";
				break;
			case SENSOR_S_CRIT:
				as = ", CRITICAL";
				break;
			case SENSOR_S_UNKNOWN:
				as = ", UNKNOWN";
				break;
			}
			syslog(limit->astatus == SENSOR_S_OK ? LOG_INFO :
			    LOG_ALERT, "%s.%s%d: %s%s",
			    sdlim->dxname, sensor_type_s[limit->type],
			    limit->numt,
			    print_sensor(limit->type, limit->last_val), as);
		}

		if (limit->ustatus_changed > last_report) {
			char us[BUFSIZ];

			switch (limit->ustatus) {
			case SENSORSD_S_UNSPEC:
				snprintf(us, sizeof(us),
				    "ustatus uninitialised");
				break;
			case SENSORSD_S_INVALID:
				snprintf(us, sizeof(us), "marked invalid");
				break;
			case SENSORSD_S_WITHIN:
				snprintf(us, sizeof(us),
				    "within limits: %s",
				    print_sensor(limit->type, limit->last_val));
				break;
			case SENSORSD_S_ABOVE:
				snprintf(us, sizeof(us),
				    "exceeds limits: %s is above %s",
				    print_sensor(limit->type, limit->last_val),
				    print_sensor(limit->type, limit->upper));
				break;
			case SENSORSD_S_BELOW:
				snprintf(us, sizeof(us),
				    "exceeds limits: %s is below %s",
				    print_sensor(limit->type, limit->last_val),
				    print_sensor(limit->type, limit->lower));
				break;
			}
			syslog(limit->ustatus == SENSORSD_S_WITHIN ? LOG_INFO :
			    LOG_ALERT, "%s.%s%d: %s",
			    sdlim->dxname, sensor_type_s[limit->type],
			    limit->numt, us);
		}

		if (limit->command) {
			int i = 0, n = 0, r;
			char *cmd = limit->command;
			char buf[BUFSIZ];
			int len = sizeof(buf);

			buf[0] = '\0';
			for (i = n = 0; n < len; ++i) {
				if (cmd[i] == '\0') {
					buf[n++] = '\0';
					break;
				}
				if (cmd[i] != '%') {
					buf[n++] = limit->command[i];
					continue;
				}
				i++;
				if (cmd[i] == '\0') {
					buf[n++] = '\0';
					break;
				}

				switch (cmd[i]) {
				case 'x':
					r = snprintf(&buf[n], len - n, "%s",
					    sdlim->dxname);
					break;
				case 't':
					r = snprintf(&buf[n], len - n, "%s",
					    sensor_type_s[limit->type]);
					break;
				case 'n':
					r = snprintf(&buf[n], len - n, "%d",
					    limit->numt);
					break;
				case 'l':
				{
					char *s = "";
					switch (limit->ustatus) {
					case SENSORSD_S_UNSPEC:
						s = "uninitialised";
						break;
					case SENSORSD_S_INVALID:
						s = "invalid";
						break;
					case SENSORSD_S_WITHIN:
						s = "within";
						break;
					case SENSORSD_S_ABOVE:
						s = "above";
						break;
					case SENSORSD_S_BELOW:
						s = "below";
						break;
					}
					r = snprintf(&buf[n], len - n, "%s",
					    s);
					break;
				}
				case 's':
				{
					char *s;
					switch (limit->astatus) {
					case SENSOR_S_UNSPEC:
						s = "UNSPEC";
						break;
					case SENSOR_S_OK:
						s = "OK";
						break;
					case SENSOR_S_WARN:
						s = "WARNING";
						break;
					case SENSOR_S_CRIT:
						s = "CRITICAL";
						break;
					default:
						s = "UNKNOWN";
					}
					r = snprintf(&buf[n], len - n, "%s",
					    s);
					break;
				}
				case '2':
					r = snprintf(&buf[n], len - n, "%s",
					    print_sensor(limit->type,
					    limit->last_val));
					break;
				case '3':
					r = snprintf(&buf[n], len - n, "%s",
					    print_sensor(limit->type,
					    limit->lower));
					break;
				case '4':
					r = snprintf(&buf[n], len - n, "%s",
					    print_sensor(limit->type,
					    limit->upper));
					break;
				default:
					r = snprintf(&buf[n], len - n, "%%%c",
					    cmd[i]);
					break;
				}
				if (r == -1 || (r >= len - n)) {
					syslog(LOG_CRIT, "could not parse "
					    "command");
					return;
				}
				if (r > 0)
					n += r;
			}
			if (buf[0])
				execute(buf);
		}
	}
}

const char *drvstat[] = {
	NULL, "empty", "ready", "powerup", "online", "idle", "active",
	"rebuild", "powerdown", "fail", "pfail"
};

static char *
print_sensor(enum sensor_type type, int64_t value)
{
	static char	 rfbuf[RFBUFCNT][RFBUFSIZ];	/* ring buffer */
	static int	 idx;
	char		*fbuf;

	fbuf = rfbuf[idx++];
	if (idx == RFBUFCNT)
		idx = 0;

	switch (type) {
	case SENSOR_TEMP:
		snprintf(fbuf, RFBUFSIZ, "%.2f degC",
		    (value - 273150000) / 1000000.0);
		break;
	case SENSOR_FANRPM:
		snprintf(fbuf, RFBUFSIZ, "%lld RPM", value);
		break;
	case SENSOR_VOLTS_DC:
		snprintf(fbuf, RFBUFSIZ, "%.2f V DC", value / 1000000.0);
		break;
	case SENSOR_VOLTS_AC:
		snprintf(fbuf, RFBUFSIZ, "%.2f V AC", value / 1000000.0);
		break;
	case SENSOR_WATTS:
		snprintf(fbuf, RFBUFSIZ, "%.2f W", value / 1000000.0);
		break;
	case SENSOR_AMPS:
		snprintf(fbuf, RFBUFSIZ, "%.2f A", value / 1000000.0);
		break;
	case SENSOR_WATTHOUR:
		snprintf(fbuf, RFBUFSIZ, "%.2f Wh", value / 1000000.0);
		break;
	case SENSOR_AMPHOUR:
		snprintf(fbuf, RFBUFSIZ, "%.2f Ah", value / 1000000.0);
		break;
	case SENSOR_INDICATOR:
		snprintf(fbuf, RFBUFSIZ, "%s", value? "On" : "Off");
		break;
	case SENSOR_INTEGER:
		snprintf(fbuf, RFBUFSIZ, "%lld", value);
		break;
	case SENSOR_PERCENT:
		snprintf(fbuf, RFBUFSIZ, "%.2f%%", value / 1000.0);
		break;
	case SENSOR_LUX:
		snprintf(fbuf, RFBUFSIZ, "%.2f lx", value / 1000000.0);
		break;
	case SENSOR_DRIVE:
		if (0 < value && value < sizeof(drvstat)/sizeof(drvstat[0]))
			snprintf(fbuf, RFBUFSIZ, "%s", drvstat[value]);
		else
			snprintf(fbuf, RFBUFSIZ, "%lld ???", value);
		break;
	case SENSOR_TIMEDELTA:
		snprintf(fbuf, RFBUFSIZ, "%.6f secs", value / 1000000000.0);
		break;
	case SENSOR_HUMIDITY:
		snprintf(fbuf, RFBUFSIZ, "%.2f%%", value / 1000.0);
		break;
	case SENSOR_FREQ:
		snprintf(fbuf, RFBUFSIZ, "%.2f Hz", value / 1000000.0);
		break;
	case SENSOR_ANGLE:
		snprintf(fbuf, RFBUFSIZ, "%lld", value);
		break;
	case SENSOR_DISTANCE:
		snprintf(fbuf, RFBUFSIZ, "%.3f m", value / 1000000.0);
		break;
	case SENSOR_PRESSURE:
		snprintf(fbuf, RFBUFSIZ, "%.2f Pa", value / 1000.0);
		break;
	case SENSOR_ACCEL:
		snprintf(fbuf, RFBUFSIZ, "%2.4f m/s^2", value / 1000000.0);
		break;
	case SENSOR_VELOCITY:
		snprintf(fbuf, RFBUFSIZ, "%4.3f m/s", value / 1000000.0);
		break;
	default:
		snprintf(fbuf, RFBUFSIZ, "%lld ???", value);
	}

	return (fbuf);
}

void
parse_config(char *cf)
{
	struct sdlim_t	 *sdlim;

	TAILQ_FOREACH(sdlim, &sdlims, entries)
		parse_config_sdlim(sdlim, cf);
}

void
parse_config_sdlim(struct sdlim_t *sdlim, char *cf)
{
	struct limits_t	 *p;
	char		 *buf = NULL, *ebuf = NULL;
	char		  node[48];
	char		 *cfa[2];
	
	cfa[0] = cf;
	cfa[1] = NULL;

	TAILQ_FOREACH(p, &sdlim->limits, entries) {
		snprintf(node, sizeof(node), "hw.sensors.%s.%s%d",
		    sdlim->dxname, sensor_type_s[p->type], p->numt);
		p->flags = 0;
		if (cgetent(&buf, cfa, node) != 0)
			if (cgetent(&buf, cfa, sensor_type_s[p->type]) != 0)
				continue;
		if (cgetcap(buf, "istatus", ':'))
			p->flags |= SENSORSD_L_ISTATUS;
		if (cgetstr(buf, "low", &ebuf) < 0)
			ebuf = NULL;
		p->lower = get_val(ebuf, 0, p->type);
		if (cgetstr(buf, "high", &ebuf) < 0)
			ebuf = NULL;
		p->upper = get_val(ebuf, 1, p->type);
		if (cgetstr(buf, "command", &ebuf) < 0)
			ebuf = NULL;
		if (ebuf != NULL) {
			p->command = ebuf;
			ebuf = NULL;
		}
		free(buf);
		buf = NULL;
		if (p->lower != LLONG_MIN || p->upper != LLONG_MAX)
			p->flags |= SENSORSD_L_USERLIMIT;
	}
}

int64_t
get_val(char *buf, int upper, enum sensor_type type)
{
	double	 val;
	int64_t	 rval = 0;
	char	*p;

	if (buf == NULL) {
		if (upper)
			return (LLONG_MAX);
		else
			return (LLONG_MIN);
	}

	val = strtod(buf, &p);
	if (buf == p)
		err(1, "incorrect value: %s", buf);

	switch (type) {
	case SENSOR_TEMP:
		switch (*p) {
		case 'C':
			printf("C");
			rval = val * 1000 * 1000 + 273150000;
			break;
		case 'F':
			printf("F");
			rval = (val * 1000 * 1000 + 459670000) / 9 * 5;
			break;
		default:
			errx(1, "unknown unit %s for temp sensor", p);
		}
		break;
	case SENSOR_FANRPM:
		rval = val;
		break;
	case SENSOR_VOLTS_DC:
	case SENSOR_VOLTS_AC:
		if (*p != 'V')
			errx(1, "unknown unit %s for voltage sensor", p);
		rval = val * 1000 * 1000;
		break;
	case SENSOR_PERCENT:
		rval = val * 1000.0;
		break;
	case SENSOR_INDICATOR:
	case SENSOR_INTEGER:
	case SENSOR_DRIVE:
	case SENSOR_ANGLE:
		rval = val;
		break;
	case SENSOR_WATTS:
	case SENSOR_AMPS:
	case SENSOR_WATTHOUR:
	case SENSOR_AMPHOUR:
	case SENSOR_LUX:
	case SENSOR_FREQ:
	case SENSOR_ACCEL:
	case SENSOR_DISTANCE:
	case SENSOR_VELOCITY:
		rval = val * 1000 * 1000;
		break;
	case SENSOR_TIMEDELTA:
		rval = val * 1000 * 1000 * 1000;
		break;
	case SENSOR_HUMIDITY:
	case SENSOR_PRESSURE:
		rval = val * 1000.0;
		break;
	default:
		errx(1, "unsupported sensor type");
		/* not reached */
	}
	free(buf);
	return (rval);
}

void
reparse_cfg(int signo)
{
	reload = 1;
}
