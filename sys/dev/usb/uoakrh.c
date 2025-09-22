/*	$OpenBSD: uoakrh.c,v 1.20 2024/05/23 03:21:09 jsg Exp $   */

/*
 * Copyright (c) 2012 Yojiro UO <yuo@nui.org>
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

/* TORADEX OAK series sensors: Temperature/Humidity sensor driver */
/* http://developer.toradex.com/files/toradex-dev/uploads/media/Oak/Oak_ProgrammingGuide.pdf */

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

#include "uoak.h"

#ifdef OARKRH_DEBUG
int	uoakrhdebug = 0;
#define DPRINTFN(n, x)	do { if (uoakrhdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define UOAKRH_SAMPLE_RATE	200	/* ms */
#define UOAKRH_REFRESH_PERIOD	10	/* 10 sec : 0.1Hz */

struct uoakrh_sensor {
	struct ksensor	 temp;
	struct ksensor	 humi;
	int count;
	int tempval, humival;
	int resolution;
};

struct uoakrh_softc {
	struct uhidev		 sc_hdev;

	/* uoak common */
	struct uoak_softc	 sc_uoak_softc;

	/* sensor framework */
	struct uoakrh_sensor	 sc_sensor;
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;

	/* sensor setting */
	int			 sc_rh_heater;
};

const struct usb_devno uoakrh_devs[] = {
	{ USB_VENDOR_TORADEX, USB_PRODUCT_TORADEX_RH},
};
#define uoakrh_lookup(v, p) usb_lookup(uoakrh_devs, v, p)

int  uoakrh_match(struct device *, void *, void *);
void uoakrh_attach(struct device *, struct device *, void *);
int  uoakrh_detach(struct device *, int);

void uoakrh_intr(struct uhidev *, void *, u_int);
void uoakrh_refresh(void *);

int uoakrh_get_sensor_setting(struct uoakrh_softc *, enum uoak_target);

void uoakrh_dev_setting(void *, enum uoak_target);
void uoakrh_dev_print(void *, enum uoak_target);


struct cfdriver uoakrh_cd = {
	NULL, "uoakrh", DV_DULL
};

const struct cfattach uoakrh_ca = {
	sizeof(struct uoakrh_softc),
	uoakrh_match,
	uoakrh_attach,
	uoakrh_detach,
};

const struct uoak_methods uoakrh_methods = {
	uoakrh_dev_print,
	uoakrh_dev_setting
};


int
uoakrh_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (uoakrh_lookup(uha->uaa->vendor, uha->uaa->product) == NULL)
		return UMATCH_NONE;

	return (UMATCH_VENDOR_PRODUCT);
}

void
uoakrh_attach(struct device *parent, struct device *self, void *aux)
{
	struct uoakrh_softc *sc = (struct uoakrh_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;

	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int err, size, repid;
	void *desc;

	sc->sc_hdev.sc_intr = uoakrh_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	scc->sc_parent = sc;
	scc->sc_udev = dev;
	scc->sc_hdev = &sc->sc_hdev;
	scc->sc_methods = &uoakrh_methods;
	scc->sc_sensordev = &sc->sc_sensordev;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	scc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	scc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	scc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	/* device initialize */
	(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	err = uoak_set_sample_rate(scc, OAK_TARGET_RAM, UOAKRH_SAMPLE_RATE);
	if (err) {
		printf("%s: could not set sampling rate. exit\n",
		    sc->sc_hdev.sc_dev.dv_xname);
		return;
	}

	/* query and print device setting */
	uoak_get_devinfo(scc);
	uoak_print_devinfo(scc);

	DPRINTF((" config in RAM\n"));
	uoak_get_setting(scc, OAK_TARGET_RAM);
	uoak_print_setting(scc, OAK_TARGET_RAM);
#ifdef UOAKV_DEBUG
	DPRINTF((" config in FLASH\n"));
	uoak_get_setting(scc, OAK_TARGET_FLASH);
	uoak_print_setting(scc, OAK_TARGET_FLASH);
#endif

	/* attach sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.temp.type = SENSOR_TEMP;
	sc->sc_sensor.humi.type = SENSOR_HUMIDITY;
	sc->sc_sensor.temp.flags |= SENSOR_FINVALID;
	sc->sc_sensor.humi.flags |= SENSOR_FINVALID;

	/* add label with sensor serial# */
	(void)snprintf(sc->sc_sensor.temp.desc, sizeof(sc->sc_sensor.temp.desc),
	    "Temp.(#%s)", scc->sc_udi.udi_serial);
	(void)snprintf(sc->sc_sensor.humi.desc, sizeof(sc->sc_sensor.humi.desc),
	    "%%RH(#%s)", scc->sc_udi.udi_serial);
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor.temp);
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor.humi);

	/* start sensor */
	sc->sc_sensortask = sensor_task_register(sc, uoakrh_refresh, 
	    UOAKRH_REFRESH_PERIOD);
	if (sc->sc_sensortask == NULL) {
		printf(", unable to register update task\n");
		return;
	}
	sensordev_install(&sc->sc_sensordev);

	err = uhidev_open(&sc->sc_hdev);
	if (err) {
		printf("%s: could not open interrupt pipe, quit\n",
		    sc->sc_hdev.sc_dev.dv_xname);
		return;
	}
	scc->sc_ibuf = malloc(scc->sc_ilen, M_USBDEV, M_WAITOK);

	DPRINTF(("uoakrh_attach: complete\n"));
}

int
uoakrh_detach(struct device *self, int flags)
{
	struct uoakrh_softc *sc = (struct uoakrh_softc *)self;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int rv = 0;

	wakeup(&sc->sc_sensortask);
	sensordev_deinstall(&sc->sc_sensordev);

	sensor_detach(&sc->sc_sensordev, &sc->sc_sensor.temp);
	sensor_detach(&sc->sc_sensordev, &sc->sc_sensor.humi);

	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);

	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	if (scc->sc_ibuf != NULL) {
		free(scc->sc_ibuf, M_USBDEV, scc->sc_ilen);
		scc->sc_ibuf = NULL;
	}

	return (rv);
}

void
uoakrh_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct uoakrh_softc *sc = (struct uoakrh_softc *)addr;
	struct uoakrh_sensor *s = &sc->sc_sensor;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int frame, temp, humi;

	if (scc->sc_ibuf == NULL)
		return;

	memcpy(scc->sc_ibuf, ibuf, len);
	frame = (scc->sc_ibuf[1] << 8) + (scc->sc_ibuf[0]);
	humi  = (scc->sc_ibuf[3] << 8) + (scc->sc_ibuf[2]);
	temp  = (scc->sc_ibuf[5] << 8) + (scc->sc_ibuf[4]);

	if (s->count == 0) { 
		s->tempval = temp;
		s->humival = humi;
	}

	/* calculate average value */
	s->tempval = (s->tempval * s->count + temp) / (s->count + 1);
	s->humival = (s->humival * s->count + humi) / (s->count + 1);

	s->count++;
}

void
uoakrh_refresh(void *arg)
{
	struct uoakrh_softc *sc = arg;
	struct uoakrh_sensor *s = &sc->sc_sensor;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	uint8_t led;

	/* blink LED for each cycle */
	if (uoak_led_status(scc, OAK_TARGET_RAM, &led) < 0)
		DPRINTF(("status query error\n"));
	if (led == OAK_LED_OFF) 
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	else 
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_OFF);

	/* update sensor value */
	s->temp.value = (uint64_t)(s->tempval) * 10000;
	s->humi.value = (uint64_t)(s->humival) * 10;
	s->temp.flags &= ~SENSOR_FINVALID;
	s->humi.flags &= ~SENSOR_FINVALID;
	s->count = 0;
}


int
uoakrh_get_sensor_setting(struct uoakrh_softc *sc, enum uoak_target target)
{
	uint8_t result;
	struct uoak_softc *scc = &sc->sc_uoak_softc;

	memset(&scc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	scc->sc_rcmd.target = target;
	scc->sc_rcmd.datasize = 0x1;
	USETW(&scc->sc_rcmd.cmd, OAK_CMD_SENSORSETTING);

	if (uoak_get_cmd(scc) < 0)
		return EIO;

	result =  scc->sc_buf[1];
	sc->sc_sensor.resolution = (result & OAK_RH_SENSOR_RES_MASK);
	sc->sc_rh_heater = (result & OAK_RH_SENSOR_HEATER_MASK) >> 2;

	return 0;
}

/* device specific functions */
void
uoakrh_dev_setting(void *parent, enum uoak_target target)
{
	struct uoakrh_softc *sc = (struct uoakrh_softc *)parent;

	/* get device specific configuration */
	(void)uoakrh_get_sensor_setting(sc, target);
}

void
uoakrh_dev_print(void *parent, enum uoak_target target)
{
	struct uoakrh_softc *sc = (struct uoakrh_softc *)parent;

	printf(", %s",
	    (sc->sc_sensor.resolution ? "8bit RH/12 bit" : "12bit RH/14bit"));
	printf(", heater %s", (sc->sc_rh_heater ? "ON" : "OFF"));
}
