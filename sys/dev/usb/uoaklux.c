/*	$OpenBSD: uoaklux.c,v 1.18 2024/05/23 03:21:09 jsg Exp $   */

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

/* TORADEX OAK series sensors: lux sensor driver */
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

#ifdef UOAKLUX_DEBUG
int	uoakluxdebug = 0;
#define DPRINTFN(n, x)	do { if (uoakluxdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define UOAKLUX_SAMPLE_RATE	200	/* ms */
#define UOAKLUX_REFRESH_PERIOD	5	/* 5 sec : 0.2Hz */

struct uoaklux_sensor {
	struct uoak_sensor lux;
	/* lux sensor setting */
	uint8_t		 gain;
	int		 inttime;

};

struct uoaklux_softc {
	struct uhidev		 sc_hdev;

	/* uoak common */
	struct uoak_softc	 sc_uoak_softc;

	/* sensor framework */
	struct uoaklux_sensor	 sc_sensor;
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;
};

const struct usb_devno uoaklux_devs[] = {
	{ USB_VENDOR_TORADEX, USB_PRODUCT_TORADEX_LUX},
};
#define uoaklux_lookup(v, p) usb_lookup(uoaklux_devs, v, p)

int  uoaklux_match(struct device *, void *, void *);
void uoaklux_attach(struct device *, struct device *, void *);
int  uoaklux_detach(struct device *, int);

void uoaklux_intr(struct uhidev *, void *, u_int);
void uoaklux_refresh(void *);

int uoaklux_get_sensor_setting(struct uoaklux_softc *, enum uoak_target);

void uoaklux_dev_setting(void *, enum uoak_target);
void uoaklux_dev_print(void *, enum uoak_target);


struct cfdriver uoaklux_cd = {
	NULL, "uoaklux", DV_DULL
};

const struct cfattach uoaklux_ca = {
	sizeof(struct uoaklux_softc),
	uoaklux_match,
	uoaklux_attach,
	uoaklux_detach,
};

const struct uoak_methods uoaklux_methods = {
	uoaklux_dev_print,
	uoaklux_dev_setting
};


int
uoaklux_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (uoaklux_lookup(uha->uaa->vendor, uha->uaa->product) == NULL)
		return UMATCH_NONE;

	return (UMATCH_VENDOR_PRODUCT);
}

void
uoaklux_attach(struct device *parent, struct device *self, void *aux)
{
	struct uoaklux_softc *sc = (struct uoaklux_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;

	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int err, size, repid;
	void *desc;

	sc->sc_hdev.sc_intr = uoaklux_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	scc->sc_parent = sc;
	scc->sc_udev = dev;
	scc->sc_hdev = &sc->sc_hdev;
	scc->sc_methods = &uoaklux_methods;
	scc->sc_sensordev = &sc->sc_sensordev;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	scc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	scc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	scc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	/*device initialize */
	(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	err = uoak_set_sample_rate(scc, OAK_TARGET_RAM, UOAKLUX_SAMPLE_RATE);
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
#ifdef UOAKLUX_DEBUG
	DPRINTF((" config in FLASh\n"));
	uoak_get_setting(scc, OAK_TARGET_FLASH);
	uoak_print_setting(scc, OAK_TARGET_FLASH);
#endif

	/* attach sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	uoak_sensor_attach(scc, &sc->sc_sensor.lux, SENSOR_LUX);

	/* start sensor */
	sc->sc_sensortask = sensor_task_register(sc, uoaklux_refresh, 
	    UOAKLUX_REFRESH_PERIOD);
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

	DPRINTF(("uoaklux_attach: complete\n"));
}


int
uoaklux_detach(struct device *self, int flags)
{
	struct uoaklux_softc *sc = (struct uoaklux_softc *)self;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int rv = 0;

	wakeup(&sc->sc_sensortask);
	sensordev_deinstall(&sc->sc_sensordev);

	uoak_sensor_detach(scc, &sc->sc_sensor.lux);

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
uoaklux_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct uoaklux_softc *sc = (struct uoaklux_softc *)addr;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int frame, val;

	if (scc->sc_ibuf == NULL)
		return;

	memcpy(scc->sc_ibuf, ibuf, len);
	frame = (scc->sc_ibuf[1] << 8) + (scc->sc_ibuf[0]);
	val = (scc->sc_ibuf[3] << 8) + (scc->sc_ibuf[2]);
	uoak_sensor_update(&sc->sc_sensor.lux, val);
}

void
uoaklux_refresh(void *arg)
{
	struct uoaklux_softc *sc = arg;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	uint8_t led;

	/* blink LED for each cycle */
	if (uoak_led_status(scc, OAK_TARGET_RAM, &led) < 0)
		DPRINTF(("status query error\n"));
	if (led == OAK_LED_OFF) 
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	else 
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_OFF);

	uoak_sensor_refresh(&sc->sc_sensor.lux, 1000000, 0);
}

int
uoaklux_get_sensor_setting(struct uoaklux_softc *sc, enum uoak_target target)
{
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	uint8_t result;

	memset(&scc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	scc->sc_rcmd.target = target;
	scc->sc_rcmd.datasize = 0x1;
	USETW(&scc->sc_rcmd.cmd, OAK_CMD_SENSORSETTING);

	if (uoak_get_cmd(scc) < 0)
		return EIO;

	result =  scc->sc_buf[1];

	sc->sc_sensor.gain = ((result & OAK_LUX_SENSOR_GAIN_MASK) >> 3);
	sc->sc_sensor.inttime = (result & OAK_LUX_SENSOR_INTTIME_MASK);

	return 0;
}

/* device specific functions */
void
uoaklux_dev_setting(void *parent, enum uoak_target target)
{
	struct uoaklux_softc *sc = (struct uoaklux_softc *)parent;

	/* get device specific configuration */
	(void)uoaklux_get_sensor_setting(sc, target);
}

void
uoaklux_dev_print(void *parent, enum uoak_target target)
{
	struct uoaklux_softc *sc = (struct uoaklux_softc *)parent;

	printf(", %s gain", (sc->sc_sensor.gain ? "HIGH" : "LOW"));
	printf(", speed ");
	switch(sc->sc_sensor.inttime) {
	case OAK_LUX_SENSOR_INTTIME_13_7ms:
		printf("13.7ms");
		break;
	case OAK_LUX_SENSOR_INTTIME_101ms:
		printf("101ms");
		break;
	case OAK_LUX_SENSOR_INTTIME_402ms:
		printf("402ms");
		break;
	default:
		printf("unknown");
		break;
	}
}
