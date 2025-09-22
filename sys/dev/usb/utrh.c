/*	$OpenBSD: utrh.c,v 1.27 2024/05/23 03:21:09 jsg Exp $   */

/*
 * Copyright (c) 2009 Yojiro UO <yuo@nui.org>
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

/* Driver for Strawberry linux USBRH Temperature/Humidity sensor */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

#ifdef UTRH_DEBUG
#define DPRINTF(x)	do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

/* sensors */
#define UTRH_TEMP		0
#define UTRH_HUMIDITY		1
#define UTRH_MAX_SENSORS	2

struct utrh_softc {
	struct uhidev		 sc_hdev;
	struct usbd_device	*sc_udev;

	/* uhidev parameters */
	size_t			 sc_flen;	/* feature report length */
	size_t			 sc_ilen;	/* input report length */
	size_t			 sc_olen;	/* output report length */

	uint8_t			*sc_ibuf;

	/* sensor framework */
	struct ksensor		 sc_sensor[UTRH_MAX_SENSORS];
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;

	uint8_t			 sc_num_sensors;
};

const struct usb_devno utrh_devs[] = {
	{ USB_VENDOR_STRAWBERRYLINUX, USB_PRODUCT_STRAWBERRYLINUX_USBRH},
};

int utrh_match(struct device *, void *, void *);
void utrh_attach(struct device *, struct device *, void *);
int utrh_detach(struct device *, int);

int utrh_sht1x_temp(unsigned int);
int utrh_sht1x_rh(unsigned int, int);

void utrh_intr(struct uhidev *, void *, u_int);
void utrh_refresh(void *);

struct cfdriver utrh_cd = {
	NULL, "utrh", DV_DULL
};

const struct cfattach utrh_ca = {
	sizeof(struct utrh_softc),
	utrh_match,
	utrh_attach,
	utrh_detach
};

int
utrh_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	return (usb_lookup(utrh_devs, uha->uaa->vendor, uha->uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
utrh_attach(struct device *parent, struct device *self, void *aux)
{
	struct utrh_softc *sc = (struct utrh_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;
	int size, repid, err;
	void *desc;

	sc->sc_udev = dev;
	sc->sc_hdev.sc_intr = utrh_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;
	sc->sc_num_sensors = 0;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	sc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	sc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	err = uhidev_open(&sc->sc_hdev);
	if (err) {
		printf("utrh_open: uhidev_open %d\n", err);
		return;
	}
	sc->sc_ibuf = malloc(sc->sc_ilen, M_USBDEV, M_WAITOK);

	printf("\n");

	/* attach sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor[UTRH_TEMP].type = SENSOR_TEMP;
	sc->sc_sensor[UTRH_TEMP].flags = SENSOR_FINVALID;

	strlcpy(sc->sc_sensor[UTRH_HUMIDITY].desc, "RH",
	    sizeof(sc->sc_sensor[UTRH_HUMIDITY].desc));
	sc->sc_sensor[UTRH_HUMIDITY].type = SENSOR_HUMIDITY;
	sc->sc_sensor[UTRH_HUMIDITY].flags = SENSOR_FINVALID;

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[UTRH_TEMP]);
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[UTRH_HUMIDITY]);
	sc->sc_num_sensors = 2;

	if (sc->sc_num_sensors > 0) {
		sc->sc_sensortask = sensor_task_register(sc, utrh_refresh, 6);
		if (sc->sc_sensortask == NULL) {
			printf(", unable to register update task\n");
			return;
		}
		sensordev_install(&sc->sc_sensordev);
	}

	DPRINTF(("utrh_attach: complete\n"));
}

int
utrh_detach(struct device *self, int flags)
{
	struct utrh_softc *sc = (struct utrh_softc *)self;
	int i, rv = 0;

	if (sc->sc_num_sensors > 0) {
		wakeup(&sc->sc_sensortask);
		sensordev_deinstall(&sc->sc_sensordev);
		for (i = 0; i < sc->sc_num_sensors; i++)
			sensor_detach(&sc->sc_sensordev, &sc->sc_sensor[i]);
		if (sc->sc_sensortask != NULL)
			sensor_task_unregister(sc->sc_sensortask);
	}

	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_ilen);
		sc->sc_ibuf = NULL;
	}

	return (rv);
}

void
utrh_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct utrh_softc *sc = (struct utrh_softc *)addr;

	if (sc->sc_ibuf == NULL)
		return;

	/* receive sensor data */
	memcpy(sc->sc_ibuf, ibuf, len);
	return;
}

void
utrh_refresh(void *arg)
{
	struct utrh_softc *sc = arg;
	unsigned int temp_tick, humidity_tick;
	int temp, rh, flen, olen;
	uint8_t ledbuf[7];

	flen = MIN(sc->sc_flen, sizeof(ledbuf));

	/* turn on LED 1*/
	bzero(ledbuf, sizeof(ledbuf));
	ledbuf[0] = 0x3;
	ledbuf[1] = 0x1;
	if (uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev.sc_report_id, ledbuf, flen) != flen)
		printf("LED request failed\n");

	/* issue query */
	uint8_t cmdbuf[] = {0x31, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00};
	olen = MIN(sc->sc_olen, sizeof(cmdbuf));
	if (uhidev_set_report(sc->sc_hdev.sc_parent, UHID_OUTPUT_REPORT,
	    sc->sc_hdev.sc_report_id, cmdbuf, olen) != olen)
		return;

	/* wait till sensor data are updated, 1s will be enough */
	tsleep_nsec(&sc->sc_sensortask, 0, "utrh", SEC_TO_NSEC(1));

	/* turn off LED 1 */
	ledbuf[1] = 0x0;
	if (uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev.sc_report_id, ledbuf, flen) != flen)
		printf("LED request failed\n");

	temp_tick = (sc->sc_ibuf[2] * 256 + sc->sc_ibuf[3]) & 0x3fff;
	humidity_tick = (sc->sc_ibuf[0] * 256 + sc->sc_ibuf[1]) & 0x0fff;

	temp = utrh_sht1x_temp(temp_tick);
	rh = utrh_sht1x_rh(humidity_tick, temp);

	sc->sc_sensor[UTRH_TEMP].value = (temp * 10000) + 273150000;
	sc->sc_sensor[UTRH_TEMP].flags &= ~SENSOR_FINVALID;
	sc->sc_sensor[UTRH_HUMIDITY].value = rh;
	sc->sc_sensor[UTRH_HUMIDITY].flags &= ~SENSOR_FINVALID;
}

/* return C-degree * 100 value */
int
utrh_sht1x_temp(unsigned int nticks)
{
	return (nticks - 4010);
}

/* return %RH * 1000 */
int
utrh_sht1x_rh(unsigned int nticks, int temp)
{
	int rh_l, rh;

	rh_l = (-40000 + 405 * nticks) - ((7 * nticks * nticks) / 250);
	rh = ((temp - 2500) * (1 + (nticks >> 7)) + rh_l) / 10;
	return rh;
}
