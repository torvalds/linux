/*	$OpenBSD: uthum.c,v 1.40 2024/05/23 03:21:09 jsg Exp $   */

/*
 * Copyright (c) 2009, 2010 Yojiro UO <yuo@nui.org>
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

/* Driver for HID based TEMPer series Temperature(/Humidity) sensors */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

#ifdef UTHUM_DEBUG
#define DPRINTF(x)	do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

/* Device types */
#define UTHUM_TYPE_TEMPERHUM	0x535a
#define UTHUM_TYPE_TEMPERHUM_2	0x575a /* alternative TEMPerHUM */
#define UTHUM_TYPE_TEMPER1	0x5758 /* TEMPer1 and HID TEMPer */
#define UTHUM_TYPE_TEMPER2	0x5759
#define UTHUM_TYPE_TEMPERNTC	0x575b
#define UTHUM_TYPE_TEMPERHUM_3	0x5f5a
#define UTHUM_TYPE_UNKNOWN	0xffff

/* Common */
#define UTHUM_CAL_OFFSET	0x14
#define UTHUM_MAX_SENSORS	2
#define CMD_DEVTYPE		0x52
#define DEVTYPE_EOF		0x53

/* query commands */
#define CMD_GETDATA_NTC		0x41 /* TEMPerNTC NTC part */
#define CMD_RESET0		0x43 /* TEMPer, TEMPer[12], TEMPerNTC */
#define CMD_RESET1		0x44 /* TEMPer, TEMPer[12] */
#define CMD_GETDATA		0x48 /* TEMPerHUM */
#define CMD_GETDATA_OUTER	0x53 /* TEMPer, TEMPer[12], TEMPerNTC */
#define CMD_GETDATA_INNER	0x54 /* TEMPer, TEMPer[12], TEMPerNTC */
#define CMD_GETDATA_EOF		0x31
#define CMD_GETDATA_EOF2	0xaa

/* temperntc mode */
#define TEMPERNTC_MODE_BASE	0x61 /* 0x61 - 0x68 */
#define TEMPERNTC_MODE_MAX	0x68
#define CMD_TEMPERNTC_MODE_DONE	0x69
#define UTHUM_NTC_MIN_THRESHOLD	0xb300
#define UTHUM_NTC_MAX_THRESHOLD	0xf200

/* sensor name */
#define UTHUM_TEMPER_INNER	0
#define UTHUM_TEMPER_OUTER	1
#define UTHUM_TEMPER_NTC	1
#define UTHUM_TEMPERHUM_TEMP	0
#define UTHUM_TEMPERHUM_HUM	1

enum uthum_sensor_type {
	UTHUM_SENSOR_UNKNOWN,
	UTHUM_SENSOR_SHT1X,
	UTHUM_SENSOR_DS75,
	UTHUM_SENSOR_NTC,
	UTHUM_SENSOR_MAXTYPES,
};

static const char * const uthum_sensor_type_s[UTHUM_SENSOR_MAXTYPES] = {
	"unknown",
	"sht1x",
	"ds75/12bit",
	"NTC"
};

static uint8_t cmd_issue[8] =
	{ 0x0a, 0x0b, 0x0c, 0x0d, 0x00, 0x00, 0x02, 0x00 };
static uint8_t cmd_query[8] =
	{ 0x0a, 0x0b, 0x0c, 0x0d, 0x00, 0x00, 0x01, 0x00 };

struct uthum_sensor {
	struct ksensor sensor;
	int cal_offset;	/* mC or m%RH */
	int attached;
	enum uthum_sensor_type dev_type;
	int cur_state;	/* for TEMPerNTC */
};

struct uthum_softc {
	struct uhidev		 sc_hdev;
	struct usbd_device	*sc_udev;
	int			 sc_device_type;
	int			 sc_num_sensors;

	/* uhidev parameters */
	size_t			 sc_flen;	/* feature report length */
	size_t			 sc_ilen;	/* input report length */
	size_t			 sc_olen;	/* output report length */

	/* sensor framework */
	struct uthum_sensor	 sc_sensor[UTHUM_MAX_SENSORS];
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;
};

const struct usb_devno uthum_devs[] = {
	/* XXX: various TEMPer variants are using same VID/PID */
	{ USB_VENDOR_TENX, USB_PRODUCT_TENX_TEMPER},
};
#define uthum_lookup(v, p) usb_lookup(uthum_devs, v, p)

int  uthum_match(struct device *, void *, void *);
void uthum_attach(struct device *, struct device *, void *);
int  uthum_detach(struct device *, int);

int  uthum_issue_cmd(struct uthum_softc *, uint8_t, int);
int  uthum_read_data(struct uthum_softc *, uint8_t, uint8_t *, size_t, int);
int  uthum_check_device_info(struct uthum_softc *);
void uthum_reset_device(struct uthum_softc *);
void uthum_setup_sensors(struct uthum_softc *);

void uthum_intr(struct uhidev *, void *, u_int);
void uthum_refresh(void *);
void uthum_refresh_temper(struct uthum_softc *, int);
void uthum_refresh_temperhum(struct uthum_softc *);
void uthum_refresh_temperntc(struct uthum_softc *, int);

int  uthum_ntc_getdata(struct uthum_softc *, int *);
int  uthum_ntc_tuning(struct uthum_softc *, int, int *);
int64_t uthum_ntc_temp(int64_t, int);
int  uthum_sht1x_temp(uint8_t, uint8_t);
int  uthum_sht1x_rh(uint8_t, uint8_t, int);
int  uthum_ds75_temp(uint8_t, uint8_t);
void uthum_print_sensorinfo(struct uthum_softc *, int);

struct cfdriver uthum_cd = {
	NULL, "uthum", DV_DULL
};

const struct cfattach uthum_ca = {
	sizeof(struct uthum_softc),
	uthum_match,
	uthum_attach,
	uthum_detach
};

int
uthum_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (uthum_lookup(uha->uaa->vendor, uha->uaa->product) == NULL)
		return UMATCH_NONE;

#if 0 /* attach only sensor part of HID as uthum* */
#define HUG_UNKNOWN_3	0x0003
	void *desc;
	int size;
	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (!hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_UNKNOWN_3)))
		return (UMATCH_NONE);
#undef HUG_UNKNOWN_3
#endif

	return (UMATCH_VENDOR_PRODUCT);
}

void
uthum_attach(struct device *parent, struct device *self, void *aux)
{
	struct uthum_softc *sc = (struct uthum_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;
	int i, size, repid;
	void *desc;

	sc->sc_udev = dev;
	sc->sc_hdev.sc_intr = uthum_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;
	sc->sc_num_sensors = 0;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	sc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	sc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	printf("\n");

	if (sc->sc_flen < 32) {
		/* not sensor interface, just attach */
		return;
	}

	/* maybe unsupported device */
	if (uthum_check_device_info(sc) < 0) {
		DPRINTF(("uthum: unknown device\n"));
		return;
	};

	/* attach sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	uthum_setup_sensors(sc);

	/* attach sensors */
	for (i = 0; i < UTHUM_MAX_SENSORS; i++) {
		if (sc->sc_sensor[i].dev_type == UTHUM_SENSOR_UNKNOWN)
			continue;
		uthum_print_sensorinfo(sc, i);
		sc->sc_sensor[i].sensor.flags |= SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i].sensor);
		sc->sc_sensor[i].attached = 1;
		sc->sc_num_sensors++;
	}

	if (sc->sc_num_sensors > 0) {
		/* 0.1Hz */
		sc->sc_sensortask = sensor_task_register(sc, uthum_refresh, 6);
		if (sc->sc_sensortask == NULL) {
			printf(", unable to register update task\n");
			return;
		}
		sensordev_install(&sc->sc_sensordev);
	}

	DPRINTF(("uthum_attach: complete\n"));
}

int
uthum_detach(struct device *self, int flags)
{
	struct uthum_softc *sc = (struct uthum_softc *)self;
	int i, rv = 0;

	if (sc->sc_num_sensors > 0) {
		wakeup(&sc->sc_sensortask);
		sensordev_deinstall(&sc->sc_sensordev);
		for (i = 0; i < UTHUM_MAX_SENSORS; i++) {
			if (sc->sc_sensor[i].attached)
				sensor_detach(&sc->sc_sensordev,
					&sc->sc_sensor[i].sensor);
		}
		if (sc->sc_sensortask != NULL)
			sensor_task_unregister(sc->sc_sensortask);
	}

	uthum_reset_device(sc);

	return (rv);
}

void
uthum_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	/* do nothing */
}

int
uthum_issue_cmd(struct uthum_softc *sc, uint8_t target_cmd, int delay)
{
	uint8_t cmdbuf[32];
	int i, actlen, olen;

	olen = MIN(sc->sc_olen, sizeof(cmdbuf));

	bzero(cmdbuf, sizeof(cmdbuf));
	memcpy(cmdbuf, cmd_issue, sizeof(cmd_issue));
	actlen = uhidev_set_report(sc->sc_hdev.sc_parent, UHID_OUTPUT_REPORT,
	    sc->sc_hdev.sc_report_id, cmdbuf, olen);
	if (actlen != olen)
		return EIO;

	bzero(cmdbuf, sizeof(cmdbuf));
	cmdbuf[0] = target_cmd;
	actlen = uhidev_set_report(sc->sc_hdev.sc_parent, UHID_OUTPUT_REPORT,
	    sc->sc_hdev.sc_report_id, cmdbuf, olen);
	if (actlen != olen)
		return EIO;

	bzero(cmdbuf, sizeof(cmdbuf));
	for (i = 0; i < 7; i++) {
		actlen = uhidev_set_report(sc->sc_hdev.sc_parent,
		    UHID_OUTPUT_REPORT, sc->sc_hdev.sc_report_id, cmdbuf, olen);
		if (actlen != olen)
			return EIO;
	}

	/* wait if required */
	if (delay > 0)
		tsleep_nsec(&sc->sc_sensortask, 0, "uthum",
		    MSEC_TO_NSEC(delay));

	return 0;
}

int
uthum_read_data(struct uthum_softc *sc, uint8_t target_cmd, uint8_t *buf,
	size_t len, int delay)
{
	uint8_t cmdbuf[32], report[256];
	int olen, flen;

	/* if return buffer is null, do nothing */
	if ((buf == NULL) || len == 0)
		return 0;

	if (uthum_issue_cmd(sc, target_cmd, 50))
		return 0;

	olen = MIN(sc->sc_olen, sizeof(cmdbuf));

	bzero(cmdbuf, sizeof(cmdbuf));
	memcpy(cmdbuf, cmd_query, sizeof(cmd_query));
	if (uhidev_set_report(sc->sc_hdev.sc_parent, UHID_OUTPUT_REPORT,
	    sc->sc_hdev.sc_report_id, cmdbuf, olen) != olen)
		return EIO;

	/* wait if required */
	if (delay > 0)
		tsleep_nsec(&sc->sc_sensortask, 0, "uthum",
		    MSEC_TO_NSEC(delay));

	/* get answer */
	flen = MIN(sc->sc_flen, sizeof(report));
	if (uhidev_get_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev.sc_report_id, report, flen) != flen)
		return EIO;
	memcpy(buf, report, len);
	return 0;
}

int
uthum_check_device_info(struct uthum_softc *sc)
{
	struct uthum_dev_info {
		uint16_t dev_type;
		uint8_t	 cal[2][2];  /* calibration offsets */
		uint8_t  footer;
		uint8_t  padding[25];
	} dinfo;
	int val, dev_type;
	int retry = 3;

	/* issue query to device */
	while (retry) {
		if (uthum_read_data(sc, CMD_DEVTYPE, (void *)&dinfo,
		    sizeof(struct uthum_dev_info), 0) != 0) {
			DPRINTF(("uthum: device information query fail.\n"));
			retry--;
			continue;
		}
		if (dinfo.footer !=  DEVTYPE_EOF) {
			/* it will be a bogus entry, retry. */
			retry--;
		} else
			break;
	}

	if (retry <= 0)
		return EIO;

	dev_type = betoh16(dinfo.dev_type);
	/* TEMPerHUM has 3 different device identifiers, unify them */
	if (dev_type == UTHUM_TYPE_TEMPERHUM_2 ||
	    dev_type == UTHUM_TYPE_TEMPERHUM_3)
		dev_type = UTHUM_TYPE_TEMPERHUM;

	/* check device type and calibration offset*/
	switch (dev_type) {
	case UTHUM_TYPE_TEMPER2:
	case UTHUM_TYPE_TEMPERHUM:
	case UTHUM_TYPE_TEMPERNTC:
		val = (dinfo.cal[1][0] - UTHUM_CAL_OFFSET) * 100;
		val += dinfo.cal[1][1] * 10;
		sc->sc_sensor[1].cal_offset = val;
		/* fall down, don't break */
	case UTHUM_TYPE_TEMPER1:
		val = (dinfo.cal[0][0] - UTHUM_CAL_OFFSET) * 100;
		val += dinfo.cal[0][1] * 10;
		sc->sc_sensor[0].cal_offset = val;
		sc->sc_device_type = dev_type;
		break;
	default:
		sc->sc_device_type = UTHUM_TYPE_UNKNOWN;
		printf("uthum: unknown device (devtype = 0x%.2x)\n",
		    dev_type);
		return EIO;
	}

	/* device specific init process */
	switch (dev_type) {
	case UTHUM_TYPE_TEMPERHUM:
		sc->sc_sensor[UTHUM_TEMPER_NTC].cur_state = 0;
		break;
	};

	uthum_reset_device(sc);

	return 0;
};

void
uthum_reset_device(struct uthum_softc *sc)
{
	switch (sc->sc_device_type) {
	case UTHUM_TYPE_TEMPER1:
	case UTHUM_TYPE_TEMPERNTC:
		uthum_issue_cmd(sc, CMD_RESET0, 200);
		break;
	case UTHUM_TYPE_TEMPER2:
		uthum_issue_cmd(sc, CMD_RESET0, 200);
		uthum_issue_cmd(sc, CMD_RESET1, 200);
		break;
	}
}

void
uthum_setup_sensors(struct uthum_softc *sc)
{
	int i;

	for (i = 0; i < UTHUM_MAX_SENSORS; i++)
		sc->sc_sensor[i].dev_type = UTHUM_SENSOR_UNKNOWN;

	switch (sc->sc_device_type) {
	case UTHUM_TYPE_TEMPER2:	/* 2 temperature sensors */
		sc->sc_sensor[UTHUM_TEMPER_OUTER].dev_type =
		    UTHUM_SENSOR_DS75;
		sc->sc_sensor[UTHUM_TEMPER_OUTER].sensor.type =
		    SENSOR_TEMP;
		strlcpy(sc->sc_sensor[UTHUM_TEMPER_OUTER].sensor.desc,
		    "outer",
		    sizeof(sc->sc_sensor[UTHUM_TEMPER_OUTER].sensor.desc));
		/* fall down */
	case UTHUM_TYPE_TEMPER1:	/* 1 temperature sensor */
		sc->sc_sensor[UTHUM_TEMPER_INNER].dev_type =
		    UTHUM_SENSOR_DS75;
		sc->sc_sensor[UTHUM_TEMPER_INNER].sensor.type =
		    SENSOR_TEMP;
		strlcpy(sc->sc_sensor[UTHUM_TEMPER_INNER].sensor.desc,
		    "inner",
		    sizeof(sc->sc_sensor[UTHUM_TEMPER_INNER].sensor.desc));
		break;
	case UTHUM_TYPE_TEMPERHUM:
		/* 1 temperature sensor and 1 humidity sensor */
		for (i = 0; i < 2; i++)
			sc->sc_sensor[i].dev_type = UTHUM_SENSOR_SHT1X;
		sc->sc_sensor[UTHUM_TEMPERHUM_TEMP].sensor.type = SENSOR_TEMP;
		sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.type =
		    SENSOR_HUMIDITY;
		strlcpy(sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.desc,
		    "RH",
		    sizeof(sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.desc));
		break;
	case UTHUM_TYPE_TEMPERNTC:
		/* 2 temperature sensors */
		for (i = 0; i < 2; i++)
			sc->sc_sensor[i].sensor.type = SENSOR_TEMP;
		sc->sc_sensor[UTHUM_TEMPER_INNER].dev_type =
		    UTHUM_SENSOR_DS75;
		sc->sc_sensor[UTHUM_TEMPER_NTC].dev_type =
		    UTHUM_SENSOR_NTC;
		strlcpy(sc->sc_sensor[UTHUM_TEMPER_INNER].sensor.desc,
		    "inner",
		    sizeof(sc->sc_sensor[UTHUM_TEMPER_INNER].sensor.desc));
		strlcpy(sc->sc_sensor[UTHUM_TEMPER_NTC].sensor.desc,
		    "outer/ntc",
		    sizeof(sc->sc_sensor[UTHUM_TEMPER_NTC].sensor.desc));

		/* sensor state tuning */
		for (i = 0; i < 4; i++)
			uthum_issue_cmd(sc, TEMPERNTC_MODE_BASE, 50);
		sc->sc_sensor[UTHUM_TEMPER_NTC].cur_state = TEMPERNTC_MODE_BASE;
		if (uthum_ntc_tuning(sc, UTHUM_TEMPER_NTC, NULL))
			DPRINTF(("uthum: NTC sensor tuning failed\n"));
		uthum_issue_cmd(sc, CMD_TEMPERNTC_MODE_DONE, 100);
		break;
	default:
		/* do nothing */
		break;
	}
}

int
uthum_ntc_getdata(struct uthum_softc *sc, int *val)
{
	uint8_t buf[8];

	if (val == NULL)
		return EIO;

	/* get sensor value */
	if (uthum_read_data(sc, CMD_GETDATA_NTC, buf, sizeof(buf), 10) != 0) {
		DPRINTF(("uthum: data read fail\n"));
		return EIO;
	}

	/* check data integrity */
	if (buf[2] !=  CMD_GETDATA_EOF2) {
		DPRINTF(("uthum: broken ntc data 0x%.2x 0x%.2x 0x%.2x\n",
		    buf[0], buf[1], buf[2]));
		return EIO;
	}

	*val = (buf[0] << 8) + buf[1];
	return 0;
}

int
uthum_ntc_tuning(struct uthum_softc *sc, int sensor, int *val)
{
	struct uthum_sensor *s;
	int done, state, ostate, curval;
	int retry = 3;

	s = &sc->sc_sensor[sensor];
	state = s->cur_state;

	/* get current sensor value */
	if (val == NULL) {
		while (retry) {
			if (uthum_ntc_getdata(sc, &curval)) {
				retry--;
				continue;
			} else
				break;
		}
		if (retry <= 0)
			return EIO;
	} else {
		curval = *val;
	}

	/* no state change is required */
	if ((curval >= UTHUM_NTC_MIN_THRESHOLD) &&
	    (curval <= UTHUM_NTC_MAX_THRESHOLD)) {
		return 0;
	}

	if (((curval < UTHUM_NTC_MIN_THRESHOLD) &&
	     (state == TEMPERNTC_MODE_MAX)) ||
	    ((curval > UTHUM_NTC_MAX_THRESHOLD) &&
	     (state == TEMPERNTC_MODE_BASE)))
		return 0;

	DPRINTF(("uthum: ntc tuning start. cur state = 0x%.2x, val = 0x%.4x\n",
	    state, curval));

	/* tuning loop */
	ostate = state;
	done = 0;
	while (!done) {
		if (curval < UTHUM_NTC_MIN_THRESHOLD) {
			if (state == TEMPERNTC_MODE_MAX)
				done++;
			else
				state++;
		} else if (curval > UTHUM_NTC_MAX_THRESHOLD) {
			if (state == TEMPERNTC_MODE_BASE)
				done++;
			else
				state--;
		} else {
			uthum_ntc_getdata(sc, &curval);
			if ((curval >= UTHUM_NTC_MIN_THRESHOLD) &&
			    (curval <= UTHUM_NTC_MAX_THRESHOLD))
				done++;
		}

		/* update state */
		if (state != ostate) {
			uthum_issue_cmd(sc, state, 50);
			uthum_issue_cmd(sc, state, 50);
			uthum_ntc_getdata(sc, &curval);
		}
		ostate = state;
	}

	DPRINTF(("uthum: ntc tuning done. state change: 0x%.2x->0x%.2x\n",
	    s->cur_state, state));
	s->cur_state = state;
	if (val != NULL)
		*val = curval;

	return 0;
}

void
uthum_refresh(void *arg)
{
	struct uthum_softc *sc = arg;
	int i;

	switch (sc->sc_device_type) {
	case UTHUM_TYPE_TEMPER1:
	case UTHUM_TYPE_TEMPER2:
	case UTHUM_TYPE_TEMPERNTC:
		for (i = 0; i < sc->sc_num_sensors; i++) {
			if (sc->sc_sensor[i].dev_type == UTHUM_SENSOR_DS75)
				uthum_refresh_temper(sc, i);
			else if (sc->sc_sensor[i].dev_type == UTHUM_SENSOR_NTC)
				uthum_refresh_temperntc(sc, i);
		}
		break;
	case UTHUM_TYPE_TEMPERHUM:
		uthum_refresh_temperhum(sc);
		break;
	default:
		break;
		/* never reach */
	}
}

void
uthum_refresh_temperhum(struct uthum_softc *sc)
{
	uint8_t buf[8];
	int temp, rh;

	if (uthum_read_data(sc, CMD_GETDATA, buf, sizeof(buf), 1000) != 0) {
		DPRINTF(("uthum: data read fail\n"));
		sc->sc_sensor[UTHUM_TEMPERHUM_TEMP].sensor.flags
		    |= SENSOR_FINVALID;
		sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.flags
		    |= SENSOR_FINVALID;
		return;
	}

	temp = uthum_sht1x_temp(buf[0], buf[1]);
	rh = uthum_sht1x_rh(buf[2], buf[3], temp);

	/* apply calibration offsets */
	temp += sc->sc_sensor[UTHUM_TEMPERHUM_TEMP].cal_offset;
	rh += sc->sc_sensor[UTHUM_TEMPERHUM_HUM].cal_offset;

	sc->sc_sensor[UTHUM_TEMPERHUM_TEMP].sensor.value =
	    (temp * 10000) + 273150000;
	sc->sc_sensor[UTHUM_TEMPERHUM_TEMP].sensor.flags &= ~SENSOR_FINVALID;
	sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.value = rh;
	sc->sc_sensor[UTHUM_TEMPERHUM_HUM].sensor.flags &= ~SENSOR_FINVALID;
}

void
uthum_refresh_temper(struct uthum_softc *sc, int sensor)
{
	uint8_t buf[8];
	uint8_t cmd;
	int temp;

	if (sensor == UTHUM_TEMPER_INNER)
		cmd = CMD_GETDATA_INNER;
	else if (sensor == UTHUM_TEMPER_OUTER)
		cmd = CMD_GETDATA_OUTER;
	else
		return;

	/* get sensor value */
	if (uthum_read_data(sc, cmd, buf, sizeof(buf), 1000) != 0) {
		DPRINTF(("uthum: data read fail\n"));
		sc->sc_sensor[sensor].sensor.flags |= SENSOR_FINVALID;
		return;
	}

	/* check integrity */
	if (buf[2] !=  CMD_GETDATA_EOF) {
		DPRINTF(("uthum: broken ds75 data: 0x%.2x 0x%.2x 0x%.2x\n",
		    buf[0], buf[1], buf[2]));
		sc->sc_sensor[sensor].sensor.flags |= SENSOR_FINVALID;
		return;
	}
	temp = uthum_ds75_temp(buf[0], buf[1]);

	/* apply calibration offset */
	temp += sc->sc_sensor[sensor].cal_offset;

	sc->sc_sensor[sensor].sensor.value = (temp * 10000) + 273150000;
	sc->sc_sensor[sensor].sensor.flags &= ~SENSOR_FINVALID;
}

void
uthum_refresh_temperntc(struct uthum_softc *sc, int sensor)
{
	int val;
	int64_t temp;

	/* get sensor data */
	if (uthum_ntc_getdata(sc, &val)) {
		DPRINTF(("uthum: ntc data read fail\n"));
		sc->sc_sensor[sensor].sensor.flags |= SENSOR_FINVALID;
		return;
	}

	/* adjust sensor state */
	if ((val < UTHUM_NTC_MIN_THRESHOLD) ||
	    (val > UTHUM_NTC_MAX_THRESHOLD)) {
		if (uthum_ntc_tuning(sc, UTHUM_TEMPER_NTC, &val)) {
			DPRINTF(("uthum: NTC sensor tuning failed\n"));
			sc->sc_sensor[sensor].sensor.flags |= SENSOR_FINVALID;
			return;
		}
	}

	temp = uthum_ntc_temp(val, sc->sc_sensor[sensor].cur_state);
	if (temp == 0) {
		/* XXX: work around. */
		sc->sc_sensor[sensor].sensor.flags |= SENSOR_FINVALID;
	} else {
		/* apply calibration offset */
		temp += sc->sc_sensor[sensor].cal_offset * 10000;
		sc->sc_sensor[sensor].sensor.value = temp;
		sc->sc_sensor[sensor].sensor.flags &= ~SENSOR_FINVALID;
	}
}

/* return C-degree * 100 value */
int
uthum_ds75_temp(uint8_t msb, uint8_t lsb)
{
	int val;

	/* DS75: 12bit precision mode : 0.0625 degrees Celsius ticks */

	val = (msb << 8) | lsb;
	if (val >= 32768)
		val = val - 65536;
	val = (val * 100) >> 8;

	return val;
}

/* return C-degree * 100 value */
int
uthum_sht1x_temp(uint8_t msb, uint8_t lsb)
{
	int nticks;

	/* sensor device VDD-bias value table
	 * ----------------------------------------------
	 * VDD	2.5V	3.0V	3.5V	4.0V	5.0V
	 * bias	-3940	-3960	-3970	-3980	-4010
	 * ----------------------------------------------
	 *
	 * as the VDD of the SHT10 on my TEMPerHUM is 3.43V +/- 0.05V,
	 * bias -3970 will be best for that device.
	 */

	nticks = (msb * 256 + lsb) & 0x3fff;
	return (nticks - 3970);
}

/* return %RH * 1000 */
int
uthum_sht1x_rh(uint8_t msb, uint8_t lsb, int temp)
{
	int nticks, rh_l;

	nticks = (msb * 256 + lsb) & 0x0fff;
	rh_l = (-40000 + 405 * nticks) - ((7 * nticks * nticks) / 250);

	return ((temp - 2500) * (1 + (nticks >> 7)) + rh_l) / 10;
}

/* return muK */
int64_t
uthum_ntc_temp(int64_t val, int state)
{
	int64_t temp = 0;

	switch (state) {
	case TEMPERNTC_MODE_BASE:	/* 0x61 */
	case TEMPERNTC_MODE_BASE+1:	/* 0x62 */
	case TEMPERNTC_MODE_BASE+2:	/* 0x63 */
	case TEMPERNTC_MODE_BASE+3:	/* 0x64 */
		/* XXX, no data */
		temp = -273150000;
		break;
	case TEMPERNTC_MODE_BASE+4:	/* 0x65 */
		temp = ((val * val * 2977) / 100000) - (val * 4300) + 152450000;
		break;
	case TEMPERNTC_MODE_BASE+5:	/* 0x66 */
		temp = ((val * val * 3887) / 100000) - (val * 5300) + 197590000;
		break;
	case TEMPERNTC_MODE_BASE+6:	/* 0x67 */
		temp = ((val * val * 3495) / 100000) - (val * 5000) + 210590000;
		break;
	case TEMPERNTC_MODE_BASE+7:	/* 0x68 */
		if (val < UTHUM_NTC_MIN_THRESHOLD)
			temp = (val * -1700) + 149630000;
		else
			temp = ((val * val * 3257) / 100000) - (val * 4900) +
			    230470000;
		break;
	default:
		DPRINTF(("NTC state error, unknown state 0x%.2x\n", state));
		break;
	}

	/* convert muC->muK value */
	return temp + 273150000;
}

void
uthum_print_sensorinfo(struct uthum_softc *sc, int num)
{
	struct uthum_sensor *s;
	s = &sc->sc_sensor[num];

	printf("%s: ", sc->sc_hdev.sc_dev.dv_xname);
	switch (s->sensor.type) {
	case SENSOR_TEMP:
		printf("type %s (temperature)",
		    uthum_sensor_type_s[s->dev_type]);
		if (s->cal_offset)
			printf(", calibration offset %c%d.%d degC",
			    (s->cal_offset < 0) ? '-' : '+',
			    abs(s->cal_offset / 100),
			    abs(s->cal_offset % 100));
		break;
	case SENSOR_HUMIDITY:
		printf("type %s (humidity)",
		    uthum_sensor_type_s[s->dev_type]);
		if (s->cal_offset)
			printf("calibration offset %c%d.%d %%RH",
			    (s->cal_offset < 0) ? '-' : '+',
			    abs(s->cal_offset / 100),
			    abs(s->cal_offset % 100));
		break;
	default:
		printf("unknown");
	}
	printf("\n");
}
