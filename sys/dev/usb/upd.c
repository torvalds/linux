/*	$OpenBSD: upd.c,v 1.34 2025/03/02 08:18:12 landry Exp $ */

/*
 * Copyright (c) 2015 David Higgs <higgsd@gmail.com>
 * Copyright (c) 2014 Andre de Oliveira <andre@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for USB Power Devices sensors
 * https://usb.org/sites/default/files/pdcv10.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/uhidev.h>

#ifdef UPD_DEBUG
#define DPRINTF(x)	do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

#define DEVNAME(sc)	((sc)->sc_hdev.sc_dev.dv_xname)

struct upd_usage_entry {
	uint8_t			usage_pg;
	uint8_t			usage_id;
	enum sensor_type	senstype;
	char			*usage_name; /* sensor string */
	int			nchildren;
	struct upd_usage_entry	*children;
};

static struct upd_usage_entry upd_usage_batdep[] = {
	{ HUP_BATTERY,	HUB_REL_STATEOF_CHARGE,
	    SENSOR_PERCENT,	 "RelativeStateOfCharge" },
	{ HUP_BATTERY,	HUB_ABS_STATEOF_CHARGE,
	    SENSOR_PERCENT,	 "AbsoluteStateOfCharge" },
	{ HUP_BATTERY,	HUB_REM_CAPACITY,
	    SENSOR_PERCENT,	 "RemainingCapacity" },
	{ HUP_BATTERY,	HUB_FULLCHARGE_CAPACITY,
	    SENSOR_PERCENT,	 "FullChargeCapacity" },
	{ HUP_POWER,	HUP_PERCENT_LOAD,
	    SENSOR_PERCENT,	 "PercentLoad" },
	{ HUP_BATTERY,	HUB_CHARGING,
	    SENSOR_INDICATOR,	 "Charging" },
	{ HUP_BATTERY,	HUB_DISCHARGING,
	    SENSOR_INDICATOR,	 "Discharging" },
	{ HUP_BATTERY,	HUB_ATRATE_TIMETOFULL,
	    SENSOR_TIMEDELTA,	 "AtRateTimeToFull" },
	{ HUP_BATTERY,	HUB_ATRATE_TIMETOEMPTY,
	    SENSOR_TIMEDELTA,	 "AtRateTimeToEmpty" },
	{ HUP_BATTERY,	HUB_RUNTIMETO_EMPTY,
	    SENSOR_TIMEDELTA,	 "RunTimeToEmpty" },
	{ HUP_BATTERY,	HUB_NEED_REPLACEMENT,
	    SENSOR_INDICATOR,	 "NeedReplacement" },
};
static struct upd_usage_entry upd_usage_roots[] = {
	{ HUP_BATTERY,	HUB_BATTERY_PRESENT,
	    SENSOR_INDICATOR,	 "BatteryPresent",
	    nitems(upd_usage_batdep),	upd_usage_batdep },
	{ HUP_POWER,	HUP_SHUTDOWN_IMMINENT,
	    SENSOR_INDICATOR,	 "ShutdownImminent" },
	{ HUP_BATTERY,	HUB_AC_PRESENT,
	    SENSOR_INDICATOR,	 "ACPresent" },
	{ HUP_POWER,	HUP_OVERLOAD,
	    SENSOR_INDICATOR,	 "Overload" },
};
#define UPD_MAX_SENSORS	(nitems(upd_usage_batdep) + nitems(upd_usage_roots))

SLIST_HEAD(upd_sensor_head, upd_sensor);

struct upd_report {
	size_t			size;		/* Size of the report */
	struct upd_sensor_head	sensors;	/* List in dependency order */
	int			pending;	/* Waiting for an answer */
};

struct upd_sensor {
	struct ksensor		ksensor;
	struct hid_item		hitem;
	int			attached;	/* Is there a matching report */
	struct upd_sensor_head	children;	/* list of children sensors */
	SLIST_ENTRY(upd_sensor)	dep_next;	/* next in the child list */
	SLIST_ENTRY(upd_sensor)	rep_next;	/* next in the report list */
};

struct upd_softc {
	struct uhidev		 sc_hdev;
	int			 sc_num_sensors;
	u_int			 sc_max_repid;
	char			 sc_buf[256];

	/* sensor framework */
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;
	struct upd_report	*sc_reports;
	struct upd_sensor	*sc_sensors;
	struct upd_sensor_head	 sc_root_sensors;
};

int  upd_match(struct device *, void *, void *);
void upd_attach(struct device *, struct device *, void *);
void upd_attach_sensor_tree(struct upd_softc *, void *, int, int,
    struct upd_usage_entry *, struct upd_sensor_head *);
int  upd_detach(struct device *, int);

void upd_intr(struct uhidev *, void *, uint);
void upd_refresh(void *);
void upd_request_children(struct upd_softc *, struct upd_sensor_head *);
void upd_update_report_cb(void *, int, void *, int);

void upd_sensor_invalidate(struct upd_softc *, struct upd_sensor *);
void upd_sensor_update(struct upd_softc *, struct upd_sensor *, uint8_t *, int);
int upd_lookup_usage_entry(void *, int, struct upd_usage_entry *,
    struct hid_item *);
struct upd_sensor *upd_lookup_sensor(struct upd_softc *, int, int);

struct cfdriver upd_cd = {
	NULL, "upd", DV_DULL
};

const struct cfattach upd_ca = {
	sizeof(struct upd_softc), upd_match, upd_attach, upd_detach
};

int
upd_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int			  size;
	void			 *desc;
	struct hid_item		  item;
	int			  ret = UMATCH_NONE;
	int			  i;

	if (!UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (ret);

	DPRINTF(("upd: vendor=0x%04x, product=0x%04x\n", uha->uaa->vendor,
	    uha->uaa->product));

	/* need at least one sensor from root of tree */
	uhidev_get_report_desc(uha->parent, &desc, &size);
	for (i = 0; i < nitems(upd_usage_roots); i++)
		if (upd_lookup_usage_entry(desc, size,
		    upd_usage_roots + i, &item)) {
			ret = UMATCH_VENDOR_PRODUCT;
			uha->claimed[item.report_ID] = 1;
		}

	return (ret);
}

void
upd_attach(struct device *parent, struct device *self, void *aux)
{
	struct upd_softc	 *sc = (struct upd_softc *)self;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int			  size;
	int			  i;
	void			 *desc;

	sc->sc_hdev.sc_intr = upd_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	SLIST_INIT(&sc->sc_root_sensors);

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_max_repid = uha->parent->sc_nrepid;
	DPRINTF(("\nupd: devname=%s sc_max_repid=%d\n",
	    DEVNAME(sc), sc->sc_max_repid));

	sc->sc_reports = mallocarray(sc->sc_max_repid,
	    sizeof(struct upd_report), M_USBDEV, M_WAITOK | M_ZERO);
	for (i = 0; i < sc->sc_max_repid; i++)
		SLIST_INIT(&sc->sc_reports[i].sensors);
	sc->sc_sensors = mallocarray(UPD_MAX_SENSORS,
	    sizeof(struct upd_sensor), M_USBDEV, M_WAITOK | M_ZERO);
	for (i = 0; i < UPD_MAX_SENSORS; i++)
		SLIST_INIT(&sc->sc_sensors[i].children);

	sc->sc_num_sensors = 0;
	uhidev_get_report_desc(uha->parent, &desc, &size);
	upd_attach_sensor_tree(sc, desc, size, nitems(upd_usage_roots),
	    upd_usage_roots, &sc->sc_root_sensors);
	DPRINTF(("upd: sc_num_sensors=%d\n", sc->sc_num_sensors));

	sc->sc_sensortask = sensor_task_register(sc, upd_refresh, 6);
	if (sc->sc_sensortask == NULL) {
		printf(", unable to register update task\n");
		return;
	}
	sensordev_install(&sc->sc_sensordev);

	printf("\n");

	DPRINTF(("upd_attach: complete\n"));
}

void
upd_attach_sensor_tree(struct upd_softc *sc, void *desc, int size,
    int nentries, struct upd_usage_entry *entries,
    struct upd_sensor_head *queue)
{
	struct hid_item		  item;
	struct upd_usage_entry	 *entry;
	struct upd_sensor	 *sensor;
	struct upd_report	 *report;
	int			  i;

	for (i = 0; i < nentries; i++) {
		entry = entries + i;
		if (!upd_lookup_usage_entry(desc, size, entry, &item)) {
			/* dependency missing, add children to parent */
			upd_attach_sensor_tree(sc, desc, size,
			    entry->nchildren, entry->children, queue);
			continue;
		}

		DPRINTF(("%s: found %s on repid=%d\n", DEVNAME(sc),
		    entry->usage_name, item.report_ID));
		if (item.report_ID < 0 ||
		    item.report_ID >= sc->sc_max_repid)
			continue;

		sensor = &sc->sc_sensors[sc->sc_num_sensors];
		memcpy(&sensor->hitem, &item, sizeof(struct hid_item));
		strlcpy(sensor->ksensor.desc, entry->usage_name,
		    sizeof(sensor->ksensor.desc));
		sensor->ksensor.type = entry->senstype;
		sensor->ksensor.flags |= SENSOR_FINVALID;
		sensor->ksensor.status = SENSOR_S_UNKNOWN;
		sensor->ksensor.value = 0;
		sensor_attach(&sc->sc_sensordev, &sensor->ksensor);
		sensor->attached = 1;
		SLIST_INSERT_HEAD(queue, sensor, dep_next);
		sc->sc_num_sensors++;

		upd_attach_sensor_tree(sc, desc, size, entry->nchildren,
		    entry->children, &sensor->children);

		report = &sc->sc_reports[item.report_ID];
		if (SLIST_EMPTY(&report->sensors))
			report->size = hid_report_size(desc,
			    size, item.kind, item.report_ID);
		SLIST_INSERT_HEAD(&report->sensors, sensor, rep_next);
	}
}

int
upd_detach(struct device *self, int flags)
{
	struct upd_softc	*sc = (struct upd_softc *)self;
	struct upd_sensor	*sensor;
	int			 i;

	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);

	sensordev_deinstall(&sc->sc_sensordev);

	for (i = 0; i < sc->sc_num_sensors; i++) {
		sensor = &sc->sc_sensors[i];
		if (sensor->attached)
			sensor_detach(&sc->sc_sensordev, &sensor->ksensor);
	}

	free(sc->sc_reports, M_USBDEV, sc->sc_max_repid * sizeof(struct upd_report));
	free(sc->sc_sensors, M_USBDEV, UPD_MAX_SENSORS * sizeof(struct upd_sensor));
	return (0);
}

void
upd_refresh(void *arg)
{
	struct upd_softc	*sc = arg;
	int			 s;

	/* request root sensors, do not let async handlers fire yet */
	s = splusb();
	upd_request_children(sc, &sc->sc_root_sensors);
	splx(s);
}

void
upd_request_children(struct upd_softc *sc, struct upd_sensor_head *queue)
{
	struct upd_sensor	*sensor;
	struct upd_report	*report;
	int			 len, repid;

	SLIST_FOREACH(sensor, queue, dep_next) {
		repid = sensor->hitem.report_ID;
		report = &sc->sc_reports[repid];

		/* already requested */
		if (report->pending)
			continue;
		report->pending = 1;

		len = uhidev_get_report_async(sc->sc_hdev.sc_parent,
		    UHID_FEATURE_REPORT, repid, sc->sc_buf, report->size, sc,
		    upd_update_report_cb);

		/* request failed, force-invalidate all sensors in report */
		if (len < 0) {
			upd_update_report_cb(sc, repid, NULL, -1);
			report->pending = 0;
		}
	}
}

int
upd_lookup_usage_entry(void *desc, int size, struct upd_usage_entry *entry,
    struct hid_item *item)
{
	struct hid_data	*hdata;
	int 		 ret = 0;

	for (hdata = hid_start_parse(desc, size, hid_feature);
	     hid_get_item(hdata, item); ) {
		if (item->kind == hid_feature &&
		    entry->usage_pg == HID_GET_USAGE_PAGE(item->usage) &&
		    entry->usage_id == HID_GET_USAGE(item->usage)) {
			ret = 1;
			break;
		}
	}
	hid_end_parse(hdata);

	return (ret);
}

struct upd_sensor *
upd_lookup_sensor(struct upd_softc *sc, int page, int usage)
{
	struct upd_sensor	*sensor = NULL;
	int			 i;

	for (i = 0; i < sc->sc_num_sensors; i++) {
		sensor = &sc->sc_sensors[i];
		if (page == HID_GET_USAGE_PAGE(sensor->hitem.usage) &&
		    usage == HID_GET_USAGE(sensor->hitem.usage))
			return (sensor);
	}
	return (NULL);
}

void
upd_update_report_cb(void *priv, int repid, void *data, int len)
{
	struct upd_softc	*sc = priv;
	struct upd_report	*report = &sc->sc_reports[repid];
	struct upd_sensor	*sensor;

	/* handle buggy firmware */
	if (len > 0 && report->size != len)
		report->size = len;

	if (data == NULL || len <= 0) {
		SLIST_FOREACH(sensor, &report->sensors, rep_next)
			upd_sensor_invalidate(sc, sensor);
	} else {
		SLIST_FOREACH(sensor, &report->sensors, rep_next)
			upd_sensor_update(sc, sensor, data, len);
	}
	report->pending = 0;
}

void
upd_sensor_invalidate(struct upd_softc *sc, struct upd_sensor *sensor)
{
	struct upd_sensor	*child;

	sensor->ksensor.status = SENSOR_S_UNKNOWN;
	sensor->ksensor.flags |= SENSOR_FINVALID;

	SLIST_FOREACH(child, &sensor->children, dep_next)
		upd_sensor_invalidate(sc, child);
}

void
upd_sensor_update(struct upd_softc *sc, struct upd_sensor *sensor,
    uint8_t *buf, int len)
{
	struct upd_sensor	*child;
	int64_t			 hdata, adjust;

	switch (HID_GET_USAGE(sensor->hitem.usage)) {
	case HUB_REL_STATEOF_CHARGE:
	case HUB_ABS_STATEOF_CHARGE:
	case HUB_REM_CAPACITY:
	case HUB_FULLCHARGE_CAPACITY:
	case HUP_PERCENT_LOAD:
		adjust = 1000; /* scale adjust */
		break;
	case HUB_ATRATE_TIMETOFULL:
	case HUB_ATRATE_TIMETOEMPTY:
	case HUB_RUNTIMETO_EMPTY:
		/* spec says minutes, not seconds */
		adjust = 1000000000LL;
		break;
	default:
		adjust = 1; /* no scale adjust */
		break;
	}

	hdata = hid_get_data(buf, len, &sensor->hitem.loc);
	switch (HID_GET_USAGE(sensor->hitem.usage)) {
	case HUB_RUNTIMETO_EMPTY:
		/*
		 * If the value is reported as a 4-byte item,
		 * assume the lowest 8 bits of the value are
		 * extra, unnecessary, precision, and discard
		 * them.
		 * This happens to match what sysutils/nut
		 * reports.
		 */
		if (len == 4)
			hdata = hdata >> 8;
		break;
	}
	if (sensor->ksensor.type == SENSOR_INDICATOR)
		sensor->ksensor.value = hdata ? 1 : 0;
	else
		sensor->ksensor.value = hdata * adjust;
	sensor->ksensor.status = SENSOR_S_OK;
	sensor->ksensor.flags &= ~SENSOR_FINVALID;

	/* if battery not present, invalidate children */
	if (HID_GET_USAGE_PAGE(sensor->hitem.usage) == HUP_BATTERY &&
	    HID_GET_USAGE(sensor->hitem.usage) == HUB_BATTERY_PRESENT &&
	    sensor->ksensor.value == 0) {
		SLIST_FOREACH(child, &sensor->children, dep_next)
			upd_sensor_invalidate(sc, child);
		return;
	}

	upd_request_children(sc, &sensor->children);
}

void
upd_intr(struct uhidev *uh, void *p, uint len)
{
	/* noop */
}
