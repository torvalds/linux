/*	$OpenBSD: sensors.c,v 1.54 2019/11/11 06:32:52 otto Exp $ */

/*
 * Copyright (c) 2006 Henning Brauer <henning@openbsd.org>
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
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>
#include <sys/device.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ntpd.h"

#define MAXDEVNAMLEN		16

int	sensor_probe(int, char *, struct sensor *);
void	sensor_add(int, char *);
void	sensor_remove(struct ntp_sensor *);
void	sensor_update(struct ntp_sensor *);

void
sensor_init(void)
{
	TAILQ_INIT(&conf->ntp_sensors);
}

int
sensor_scan(void)
{
	int		i, n, err;
	char		d[MAXDEVNAMLEN];
	struct sensor	s;

	n = 0;
	for (i = 0; ; i++)
		if ((err = sensor_probe(i, d, &s))) {
			if (err == 0)
				continue;
			if (err == -1)	/* no further sensors */
				break;
			sensor_add(i, d);
			n++;
		}

	return n;
}

/*
 * 1 = time sensor!
 * 0 = sensor exists... but is not a time sensor
 * -1: no sensor here, and no further sensors after this
 */
int
sensor_probe(int devid, char *dxname, struct sensor *sensor)
{
	int			mib[5];
	size_t			slen, sdlen;
	struct sensordev	sensordev;

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	mib[2] = devid;
	mib[3] = SENSOR_TIMEDELTA;
	mib[4] = 0;

	sdlen = sizeof(sensordev);
	if (sysctl(mib, 3, &sensordev, &sdlen, NULL, 0) == -1) {
		if (errno == ENXIO)
			return (0);
		if (errno == ENOENT)
			return (-1);
		log_warn("sensor_probe sysctl");
	}

	if (sensordev.maxnumt[SENSOR_TIMEDELTA] == 0)
		return (0);

	strlcpy(dxname, sensordev.xname, MAXDEVNAMLEN);

	slen = sizeof(*sensor);
	if (sysctl(mib, 5, sensor, &slen, NULL, 0) == -1) {
		if (errno != ENOENT)
			log_warn("sensor_probe sysctl");
		return (0);
	}

	return (1);
}

void
sensor_add(int sensordev, char *dxname)
{
	struct ntp_sensor	*s;
	struct ntp_conf_sensor	*cs;

	/* check whether it is already there */
	TAILQ_FOREACH(s, &conf->ntp_sensors, entry)
		if (!strcmp(s->device, dxname))
			return;

	/* check whether it is requested in the config file */
	for (cs = TAILQ_FIRST(&conf->ntp_conf_sensors); cs != NULL &&
	    strcmp(cs->device, dxname) && strcmp(cs->device, "*");
	    cs = TAILQ_NEXT(cs, entry))
		; /* nothing */
	if (cs == NULL)
		return;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		fatal("sensor_add calloc");

	s->next = getmonotime();
	s->weight = cs->weight;
	s->correction = cs->correction;
	s->stratum = cs->stratum - 1;
	s->trusted = cs->trusted;
	if ((s->device = strdup(dxname)) == NULL)
		fatal("sensor_add strdup");
	s->sensordevid = sensordev;

	if (cs->refstr == NULL)
		memcpy(&s->refid, SENSOR_DEFAULT_REFID, sizeof(s->refid));
	else {
		s->refid = 0;
		strncpy((char *)&s->refid, cs->refstr, sizeof(s->refid));
	}

	TAILQ_INSERT_TAIL(&conf->ntp_sensors, s, entry);

	log_debug("sensor %s added (weight %d, correction %.6f, refstr %.4u, "
	     "stratum %d)", s->device, s->weight, s->correction / 1e6,
	     s->refid, s->stratum);
}

void
sensor_remove(struct ntp_sensor *s)
{
	TAILQ_REMOVE(&conf->ntp_sensors, s, entry);
	free(s->device);
	free(s);
}

void
sensor_query(struct ntp_sensor *s)
{
	char		 dxname[MAXDEVNAMLEN];
	struct sensor	 sensor;
	double		 sens_time;

	if (conf->settime)
		s->next = getmonotime() + SENSOR_QUERY_INTERVAL_SETTIME;
	else
		s->next = getmonotime() + SENSOR_QUERY_INTERVAL;

	/* rcvd is walltime here, monotime in client.c. not used elsewhere */
	if (s->update.rcvd < time(NULL) - SENSOR_DATA_MAXAGE)
		s->update.good = 0;

	if (!sensor_probe(s->sensordevid, dxname, &sensor)) {
		sensor_remove(s);
		return;
	}

	if (sensor.flags & SENSOR_FINVALID ||
	    sensor.status != SENSOR_S_OK)
		return;

	if (strcmp(dxname, s->device)) {
		sensor_remove(s);
		return;
	}

	if (sensor.tv.tv_sec == s->last)	/* already seen */
		return;

	s->last = sensor.tv.tv_sec;
	
	if (!s->trusted && !TAILQ_EMPTY(&conf->constraints)) {
		if (conf->constraint_median == 0) {
			return;
		}
		sens_time = gettime() + (sensor.value / -1e9) +
		    (s->correction / 1e6);
		if (constraint_check(sens_time) != 0) {
			log_info("sensor %s: constraint check failed", s->device);
			return;
		}
	}
	/*
	 * TD = device time
	 * TS = system time
	 * sensor.value = TS - TD in ns
	 * if value is positive, system time is ahead
	 */
	s->offsets[s->shift].offset = (sensor.value / -1e9) - getoffset() +
	    (s->correction / 1e6);
	s->offsets[s->shift].rcvd = sensor.tv.tv_sec;
	s->offsets[s->shift].good = 1;

	s->offsets[s->shift].status.send_refid = s->refid;
	/* stratum increased when sent out */
	s->offsets[s->shift].status.stratum = s->stratum;
	s->offsets[s->shift].status.rootdelay = 0;
	s->offsets[s->shift].status.rootdispersion = 0;
	s->offsets[s->shift].status.reftime = sensor.tv.tv_sec;
	s->offsets[s->shift].status.synced = 1;

	log_debug("sensor %s: offset %f", s->device,
	    s->offsets[s->shift].offset);

	if (++s->shift >= SENSOR_OFFSETS) {
		s->shift = 0;
		sensor_update(s);
	}

}

void
sensor_update(struct ntp_sensor *s)
{
	struct ntp_offset	**offsets;
	int			  i;

	if ((offsets = calloc(SENSOR_OFFSETS, sizeof(struct ntp_offset *))) ==
	    NULL)
		fatal("calloc sensor_update");

	for (i = 0; i < SENSOR_OFFSETS; i++)
		offsets[i] = &s->offsets[i];

	qsort(offsets, SENSOR_OFFSETS, sizeof(struct ntp_offset *),
	    offset_compare);

	i = SENSOR_OFFSETS / 2;
	memcpy(&s->update, offsets[i], sizeof(s->update));
	if (SENSOR_OFFSETS % 2 == 0) {
		s->update.offset =
		    (offsets[i - 1]->offset + offsets[i]->offset) / 2;
	}
	free(offsets);

	log_debug("sensor update %s: offset %f", s->device, s->update.offset);
	priv_adjtime();
}
