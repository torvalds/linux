/*	$OpenBSD: ofw_thermal.c,v 1.11 2025/09/16 08:52:11 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis
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

#include "kstat.h"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/stdint.h>
#include <sys/task.h>
#include <sys/timeout.h>
#include <sys/sched.h>
#include <sys/kstat.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_thermal.h>

LIST_HEAD(, thermal_sensor) thermal_sensors =
        LIST_HEAD_INITIALIZER(thermal_sensors);

LIST_HEAD(, cooling_device) cooling_devices =
        LIST_HEAD_INITIALIZER(cooling_devices);

struct taskq *tztq;

struct trippoint {
	int		tp_node;
	int32_t		tp_temperature;
	uint32_t	tp_hysteresis;
	int		tp_type;
	uint32_t	tp_phandle;
};

#define THERMAL_NONE		0
#define THERMAL_ACTIVE		1
#define THERMAL_PASSIVE		2
#define THERMAL_HOT		3
#define THERMAL_CRITICAL	4

static const char *trip_types[] = {
	[THERMAL_NONE]		= "none",
	[THERMAL_ACTIVE]	= "active",
	[THERMAL_PASSIVE]	= "passive",
	[THERMAL_HOT]		= "hot",
	[THERMAL_CRITICAL]	= "critical",
};

struct cmap {
	uint32_t	*cm_cdev;
	uint32_t	*cm_cdevend;
	uint32_t	cm_trip;
};

struct cdev {
	uint32_t	cd_phandle;
	int32_t		cd_level;
	int		cd_active;
	LIST_ENTRY(cdev) cd_list;
};

struct thermal_zone {
	int		tz_node;
	char		tz_name[64];
	struct task	tz_poll_task;
	struct timeout	tz_poll_to;
	uint32_t	*tz_sensors;
	uint32_t	tz_polling_delay;
	uint32_t	tz_polling_delay_passive;
	LIST_ENTRY(thermal_zone) tz_list;

	struct trippoint *tz_trips;
	int		tz_ntrips;
	struct trippoint *tz_tp;

	struct cmap	*tz_cmaps;
	int		tz_ncmaps;
	struct cmap	*tz_cm;

	LIST_HEAD(, cdev) tz_cdevs;

	int32_t		tz_temperature;

	struct rwlock	tz_lock;
	struct kstat	*tz_kstat;
};

#if NKSTAT > 0
static void	thermal_zone_kstat_attach(struct thermal_zone *);
static void	thermal_zone_kstat_update(struct thermal_zone *);
#endif /* NKSTAT > 0 */

LIST_HEAD(, thermal_zone) thermal_zones =
	LIST_HEAD_INITIALIZER(thermal_zones);

void
thermal_sensor_register(struct thermal_sensor *ts)
{
	ts->ts_cells = OF_getpropint(ts->ts_node, "#thermal-sensor-cells", 0);
	ts->ts_phandle = OF_getpropint(ts->ts_node, "phandle", 0);
	if (ts->ts_phandle == 0)
		return;

	LIST_INSERT_HEAD(&thermal_sensors, ts, ts_list);
}

void
thermal_sensor_update(struct thermal_sensor *ts, uint32_t *cells)
{
	struct thermal_zone *tz;

	LIST_FOREACH(tz, &thermal_zones, tz_list) {
		if (tz->tz_sensors[0] == ts->ts_phandle &&
		    memcmp(&tz->tz_sensors[1], cells,
		    ts->ts_cells * sizeof(uint32_t)) == 0)
			task_add(tztq, &tz->tz_poll_task);
	}
}

void
cooling_device_register(struct cooling_device *cd)
{
	cd->cd_cells = OF_getpropint(cd->cd_node, "#cooling-cells", 0);
	cd->cd_phandle = OF_getpropint(cd->cd_node, "phandle", 0);
	if (cd->cd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&cooling_devices, cd, cd_list);
}

int32_t
thermal_get_temperature_cells(uint32_t *cells)
{
	struct thermal_sensor *ts;
	uint32_t phandle = cells[0];

	LIST_FOREACH(ts, &thermal_sensors, ts_list) {
		if (ts->ts_phandle == phandle)
			break;
	}

	if (ts && ts->ts_get_temperature)
		return ts->ts_get_temperature(ts->ts_cookie, &cells[1]);

	return THERMAL_SENSOR_MAX;
}

int
thermal_set_limit_cells(uint32_t *cells, uint32_t temp)
{
	struct thermal_sensor *ts;
	uint32_t phandle = cells[0];

	LIST_FOREACH(ts, &thermal_sensors, ts_list) {
		if (ts->ts_phandle == phandle)
			break;
	}

	if (ts && ts->ts_set_limit)
		return ts->ts_set_limit(ts->ts_cookie, &cells[1], temp);

	return ENXIO;
}

void
thermal_zone_poll_timeout(void *arg)
{
	struct thermal_zone *tz = arg;

	task_add(tztq, &tz->tz_poll_task);
}

uint32_t *
cdev_next_cdev(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#cooling-cells", 2);
	return cells + ncells + 1;
}

uint32_t
cdev_get_level(uint32_t *cells)
{
	struct cooling_device *cd;
	uint32_t phandle = cells[0];

	LIST_FOREACH(cd, &cooling_devices, cd_list) {
		if (cd->cd_phandle == phandle)
			break;
	}

	if (cd && cd->cd_get_level)
		return cd->cd_get_level(cd->cd_cookie, &cells[1]);

	return 0;
}

void
cdev_set_level(uint32_t *cells, uint32_t level)
{
	struct cooling_device *cd;
	uint32_t phandle = cells[0];

	LIST_FOREACH(cd, &cooling_devices, cd_list) {
		if (cd->cd_phandle == phandle)
			break;
	}

	if (cd && cd->cd_set_level)
		cd->cd_set_level(cd->cd_cookie, &cells[1], level);
}


void
cmap_deactivate(struct thermal_zone *tz, struct cmap *cm)
{
	struct cdev *cd;
	uint32_t *cdev;

	if (cm == NULL)
		return;

	cdev = cm->cm_cdev;
	while (cdev && cdev < cm->cm_cdevend) {
		LIST_FOREACH(cd, &tz->tz_cdevs, cd_list) {
			if (cd->cd_phandle == cdev[0])
				break;
		}
		KASSERT(cd != NULL);
		cd->cd_active = 0;
		cdev = cdev_next_cdev(cdev);
	}
}

void
cmap_activate(struct thermal_zone *tz, struct cmap *cm, int32_t delta)
{
	struct cdev *cd;
	uint32_t *cdev;
	int32_t min, max;

	if (cm == NULL)
		return;

	cdev = cm->cm_cdev;
	while (cdev && cdev < cm->cm_cdevend) {
		LIST_FOREACH(cd, &tz->tz_cdevs, cd_list) {
			if (cd->cd_phandle == cdev[0])
				break;
		}
		KASSERT(cd != NULL);

		min = (cdev[1] == THERMAL_NO_LIMIT) ? 0 : cdev[1];
		max = (cdev[2] == THERMAL_NO_LIMIT) ? INT32_MAX : cdev[2];

		cd->cd_active = 1;
		cd->cd_level = cdev_get_level(cdev) + delta;
		cd->cd_level = MAX(cd->cd_level, min);
		cd->cd_level = MIN(cd->cd_level, max);
		cdev_set_level(cdev, cd->cd_level);
		cdev = cdev_next_cdev(cdev);
	}
}

void
cmap_finish(struct thermal_zone *tz)
{
	struct cdev *cd;

	LIST_FOREACH(cd, &tz->tz_cdevs, cd_list) {
		if (cd->cd_active == 0 && cd->cd_level != 0) {
			cdev_set_level(&cd->cd_phandle, 0);
			cd->cd_level = 0;
		}
	}
}

void
thermal_zone_poll(void *arg)
{
	struct thermal_zone *tz = arg;
	struct trippoint *tp, *newtp;
	struct cmap *cm, *newcm;
	uint32_t polling_delay;
	int32_t temp, delta;
	int i;

	tp = tz->tz_trips;
	temp = thermal_get_temperature_cells(tz->tz_sensors);
	if (temp == THERMAL_SENSOR_MAX)
		goto out;

	newtp = NULL;
	for (i = 0; i < tz->tz_ntrips; i++) {
		if (temp < tp->tp_temperature && tp != tz->tz_tp)
			break;
		if (temp < tp->tp_temperature - tp->tp_hysteresis)
			break;
		newtp = tp++;
	}

	/* Short circuit if we didn't hit a trip point. */
	if (newtp == NULL && tz->tz_tp == NULL)
		goto out;

	/*
	 * If the current temperature is above the trip temperature:
	 *  - increase the cooling level if the temperature is rising
	 *  - do nothing if the temperature is falling
	 * If the current temperature is below the trip temperature:
	 *  - do nothing if the temperature is rising
	 *  - decrease the cooling level if the temperature is falling
	 */
	delta = 0;
	if (newtp && tz->tz_temperature != THERMAL_SENSOR_MAX) {
		if (temp >= newtp->tp_temperature) {
			if (temp > tz->tz_temperature)
				delta = 1;
		} else {
			if (temp < tz->tz_temperature)
				delta = -1;
		}
	}

	newcm = NULL;
	cm = tz->tz_cmaps;
	for (i = 0; i < tz->tz_ncmaps; i++) {
		if (newtp && cm->cm_trip == newtp->tp_phandle) {
			newcm = cm;
			break;
		}
		cm++;
	}

	cmap_deactivate(tz, tz->tz_cm);
	cmap_activate(tz, newcm, delta);
	cmap_finish(tz);

	tz->tz_tp = newtp;
	tz->tz_cm = newcm;

out:
	tz->tz_temperature = temp;
#if NKSTAT > 0
	thermal_zone_kstat_update(tz);
#endif
	if (tz->tz_tp && tz->tz_tp->tp_type == THERMAL_PASSIVE)
		polling_delay = tz->tz_polling_delay_passive;
	else
		polling_delay = tz->tz_polling_delay;

	if (polling_delay > 0)
		timeout_add_msec(&tz->tz_poll_to, polling_delay);
	else if (tp)
		thermal_set_limit_cells(tz->tz_sensors, tp->tp_temperature);
}

static int
thermal_zone_triptype(const char *prop)
{
	size_t i;

	for (i = 0; i < nitems(trip_types); i++) {
		const char *name = trip_types[i];
		if (name == NULL)
			continue;

		if (strcmp(name, prop) == 0)
			return (i);
	}

	return (THERMAL_NONE);
}

void
thermal_zone_init(int node)
{
	struct thermal_zone *tz;
	struct trippoint *tp;
	struct cmap *cm;
	struct cdev *cd;
	int len, i;

	len = OF_getproplen(node, "thermal-sensors");
	if (len <= 0)
		return;

	if (OF_getnodebyname(node, "trips") == 0)
		return;
	if (OF_getnodebyname(node, "cooling-maps") == 0)
		return;

	tz = malloc(sizeof(struct thermal_zone), M_DEVBUF, M_ZERO | M_WAITOK);
	tz->tz_node = node;
	rw_init(&tz->tz_lock, "tzlk");

	OF_getprop(node, "name", &tz->tz_name, sizeof(tz->tz_name));
	tz->tz_name[sizeof(tz->tz_name) - 1] = 0;
	tz->tz_sensors = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(node, "thermal-sensors", tz->tz_sensors, len);
	tz->tz_polling_delay = OF_getpropint(node, "polling-delay", 0);
	tz->tz_polling_delay_passive =
	    OF_getpropint(node, "polling-delay-passive", tz->tz_polling_delay);

	task_set(&tz->tz_poll_task, thermal_zone_poll, tz);
	timeout_set(&tz->tz_poll_to, thermal_zone_poll_timeout, tz);

	/*
	 * Trip points for this thermal zone.
	 */
	node = OF_getnodebyname(tz->tz_node, "trips");
	for (node = OF_child(node); node != 0; node = OF_peer(node))
		tz->tz_ntrips++;

	tz->tz_trips = mallocarray(tz->tz_ntrips, sizeof(struct trippoint),
	    M_DEVBUF, M_ZERO | M_WAITOK);

	node = OF_getnodebyname(tz->tz_node, "trips");
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		char type[32] = "none";
		int32_t temp;

		temp = OF_getpropint(node, "temperature", THERMAL_SENSOR_MAX);

		/* Sorted insertion, since tree might not be */
		for (i = 0; i < tz->tz_ntrips; i++) {
			/* No trip point should be 0 degC, take it */
			if (tz->tz_trips[i].tp_temperature == 0)
				break;
			/* We should be bigger than the one before us */
			if (tz->tz_trips[i].tp_temperature < temp)
				continue;
			/* Free current slot */
			memmove(&tz->tz_trips[i + 1], &tz->tz_trips[i],
			    (tz->tz_ntrips - (i + 1)) * sizeof(*tp));
			break;
		}
		tp = &tz->tz_trips[i];
		tp->tp_node = node;
		tp->tp_temperature = temp;
		tp->tp_hysteresis = OF_getpropint(node, "hysteresis", 0);
		OF_getprop(node, "type", type, sizeof(type));
		tp->tp_type = thermal_zone_triptype(type);
		tp->tp_phandle = OF_getpropint(node, "phandle", 0);
		tp++;
	}

	/*
	 * Cooling maps for this thermal zone.
	 */
	node = OF_getnodebyname(tz->tz_node, "cooling-maps");
	for (node = OF_child(node); node != 0; node = OF_peer(node))
		tz->tz_ncmaps++;

	tz->tz_cmaps = mallocarray(tz->tz_ncmaps, sizeof(struct cmap),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	cm = tz->tz_cmaps;

	node = OF_getnodebyname(tz->tz_node, "cooling-maps");
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		len = OF_getproplen(node, "cooling-device");
		if (len <= 0)
			continue;
		cm->cm_cdev = malloc(len, M_DEVBUF, M_ZERO | M_WAITOK);
		OF_getpropintarray(node, "cooling-device", cm->cm_cdev, len);
		cm->cm_cdevend = cm->cm_cdev + len / sizeof(uint32_t);
		cm->cm_trip = OF_getpropint(node, "trip", 0);
		cm++;
	}

	/*
	 * Create a list of all the possible cooling devices from the
	 * cooling maps for this thermal zone, and initialize their
	 * state.
	 */
	LIST_INIT(&tz->tz_cdevs);
	cm = tz->tz_cmaps;
	for (i = 0; i < tz->tz_ncmaps; i++) {
		uint32_t *cdev;

		cdev = cm->cm_cdev;
		while (cdev && cdev < cm->cm_cdevend) {
			LIST_FOREACH(cd, &tz->tz_cdevs, cd_list) {
				if (cd->cd_phandle == cdev[0])
					break;
			}
			if (cd == NULL) {
				cd = malloc(sizeof(struct cdev), M_DEVBUF,
				    M_ZERO | M_WAITOK);
				cd->cd_phandle = cdev[0];
				cd->cd_level = 0;
				cd->cd_active = 0;
				cdev_set_level(cdev, cd->cd_level);
				LIST_INSERT_HEAD(&tz->tz_cdevs, cd, cd_list);
			}
			cdev = cdev_next_cdev(cdev);
		}
		cm++;
	}

	LIST_INSERT_HEAD(&thermal_zones, tz, tz_list);

#if NKSTAT > 0
	thermal_zone_kstat_attach(tz);
#endif

	/* Poll once to get things going. */
	thermal_zone_poll(tz);
}

void
thermal_init(void)
{
	int node = OF_finddevice("/thermal-zones");

	if (node == -1)
		return;

	tztq = taskq_create("tztq", 1, IPL_SOFTCLOCK, 0);

	for (node = OF_child(node); node != 0; node = OF_peer(node))
		thermal_zone_init(node);
}

#if NKSTAT > 0

static const char *
thermal_zone_tripname(int type)
{
	if (type >= nitems(trip_types))
		return (NULL);

	return (trip_types[type]);
}

struct thermal_zone_kstats {
	struct kstat_kv		tzk_name; /* istr could be short */
	struct kstat_kv		tzk_temp;
	struct kstat_kv		tzk_tp;
	struct kstat_kv		tzk_tp_type;
	struct kstat_kv		tzk_cooling;
};

static void
thermal_zone_kstat_update(struct thermal_zone *tz)
{
	struct kstat *ks = tz->tz_kstat;
	struct thermal_zone_kstats *tzk;

	if (ks == NULL)
		return;

	tzk = ks->ks_data;

	rw_enter_write(&tz->tz_lock);
	if (tz->tz_temperature == THERMAL_SENSOR_MAX)
		tzk->tzk_temp.kv_type = KSTAT_KV_T_NULL;
	else {
		tzk->tzk_temp.kv_type = KSTAT_KV_T_TEMP;
		kstat_kv_temp(&tzk->tzk_temp) = 273150000 +
		    1000 * tz->tz_temperature;
	}

	if (tz->tz_tp == NULL) {
		kstat_kv_u32(&tzk->tzk_tp) = 0;
		strlcpy(kstat_kv_istr(&tzk->tzk_tp_type), "none",
		    sizeof(kstat_kv_istr(&tzk->tzk_tp_type)));
	} else {
		int triptype = tz->tz_tp->tp_type;
		const char *tripname = thermal_zone_tripname(triptype);

		kstat_kv_u32(&tzk->tzk_tp) = tz->tz_tp->tp_node;

		if (tripname == NULL) {
			snprintf(kstat_kv_istr(&tzk->tzk_tp_type),
			    sizeof(kstat_kv_istr(&tzk->tzk_tp_type)),
			    "%u", triptype);
		} else {
			strlcpy(kstat_kv_istr(&tzk->tzk_tp_type), tripname,
			    sizeof(kstat_kv_istr(&tzk->tzk_tp_type)));
		}
	}

	kstat_kv_bool(&tzk->tzk_cooling) = (tz->tz_cm != NULL);

	getnanouptime(&ks->ks_updated);
	rw_exit_write(&tz->tz_lock);
}

static void
thermal_zone_kstat_attach(struct thermal_zone *tz)
{
	struct kstat *ks;
	struct thermal_zone_kstats *tzk;
	static unsigned int unit = 0;

	ks = kstat_create("dt", 0, "thermal-zone", unit++, KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("unable to create thermal-zone kstats for %s",
		    tz->tz_name);
		return;
	}

	tzk = malloc(sizeof(*tzk), M_DEVBUF, M_WAITOK|M_ZERO);

	kstat_kv_init(&tzk->tzk_name, "name", KSTAT_KV_T_ISTR);
	strlcpy(kstat_kv_istr(&tzk->tzk_name), tz->tz_name,
	    sizeof(kstat_kv_istr(&tzk->tzk_name)));
	kstat_kv_init(&tzk->tzk_temp, "temperature", KSTAT_KV_T_NULL);

	/* XXX dt node is not be the most useful info here. */
	kstat_kv_init(&tzk->tzk_tp, "trip-point-node", KSTAT_KV_T_UINT32);
	kstat_kv_init(&tzk->tzk_tp_type, "trip-type", KSTAT_KV_T_ISTR);
	strlcpy(kstat_kv_istr(&tzk->tzk_tp_type), "unknown",
	    sizeof(kstat_kv_istr(&tzk->tzk_tp_type)));

	kstat_kv_init(&tzk->tzk_cooling, "active-cooling", KSTAT_KV_T_BOOL);
	kstat_kv_bool(&tzk->tzk_cooling) = 0;

	ks->ks_softc = tz;
	ks->ks_data = tzk;
	ks->ks_datalen = sizeof(*tzk);
	ks->ks_read = kstat_read_nop;
	kstat_set_rlock(ks, &tz->tz_lock);

	tz->tz_kstat = ks;
	kstat_install(ks);
}
#endif /* NKSTAT > 0 */
