/*	$OpenBSD: uoakv.c,v 1.18 2024/05/23 03:21:09 jsg Exp $   */

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

/* TORADEX OAK series sensors: 8channel +/-10V ADC driver */
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

#ifdef UOAKV_DEBUG
int	uoakvdebug = 0;
#define DPRINTFN(n, x)	do { if (uoakvdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define UOAKV_SAMPLE_RATE	100	/* ms */
#define UOAKV_REFRESH_PERIOD	1	/* 1 sec : 1Hz */

struct uoakv_sensor {
	struct uoak_sensor v;
	/* ADC setting */
	unsigned int offset[OAK_V_TARGET_MAX];	/* absolute offset (mV) */
};

struct uoakv_softc {
	struct uhidev		 sc_hdev;

	/* uoak common */
	struct uoak_softc	 sc_uoak_softc;

	/* sensor framework */
	struct uoakv_sensor	 sc_sensor[OAK_V_MAXSENSORS];
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;

	/* sensor setting */
	int			 sc_inputmode[OAK_V_TARGET_MAX];

};

const struct usb_devno uoakv_devs[] = {
	{ USB_VENDOR_TORADEX, USB_PRODUCT_TORADEX_10V},
};
#define uoakv_lookup(v, p) usb_lookup(uoakv_devs, v, p)

int  uoakv_match(struct device *, void *, void *);
void uoakv_attach(struct device *, struct device *, void *);
int  uoakv_detach(struct device *, int);

void uoakv_intr(struct uhidev *, void *, u_int);
void uoakv_refresh(void *);

int uoakv_get_channel_setting(struct uoakv_softc *, enum uoak_target, int);
int uoakv_get_sensor_setting(struct uoakv_softc *, enum uoak_target);

void uoakv_dev_setting(void *, enum uoak_target);
void uoakv_dev_print(void *, enum uoak_target);


struct cfdriver uoakv_cd = {
	NULL, "uoakv", DV_DULL
};

const struct cfattach uoakv_ca = {
	sizeof(struct uoakv_softc),
	uoakv_match,
	uoakv_attach,
	uoakv_detach,

};

const struct uoak_methods uoakv_methods = {
	uoakv_dev_print,
	uoakv_dev_setting
};

int
uoakv_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (uoakv_lookup(uha->uaa->vendor, uha->uaa->product) == NULL)
		return UMATCH_NONE;

	return (UMATCH_VENDOR_PRODUCT);
}

void
uoakv_attach(struct device *parent, struct device *self, void *aux)
{
	struct uoakv_softc *sc = (struct uoakv_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;

	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int i, err, size, repid;
	void *desc;

	sc->sc_hdev.sc_intr = uoakv_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	scc->sc_parent = sc;
	scc->sc_udev = dev;
	scc->sc_hdev = &sc->sc_hdev;
	scc->sc_methods = &uoakv_methods;
	scc->sc_sensordev = &sc->sc_sensordev;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	scc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	scc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	scc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	/* device initialize */
	(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	err = uoak_set_sample_rate(scc, OAK_TARGET_RAM, UOAKV_SAMPLE_RATE);
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
	DPRINTF((" config in FRASH\n"));
	uoak_get_setting(scc, OAK_TARGET_FLASH);
	uoak_print_setting(scc, OAK_TARGET_FLASH);
#endif

	/* attach sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < OAK_V_MAXSENSORS; i++)
		uoak_sensor_attach(scc, &sc->sc_sensor[i].v, SENSOR_VOLTS_DC);

	/* start sensor */
	sc->sc_sensortask = sensor_task_register(sc, uoakv_refresh,
	    UOAKV_REFRESH_PERIOD);
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

	DPRINTF(("uoakv_attach: complete\n"));
}

int
uoakv_detach(struct device *self, int flags)
{
	struct uoakv_softc *sc = (struct uoakv_softc *)self;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int i, rv = 0;

	wakeup(&sc->sc_sensortask);
	sensordev_deinstall(&sc->sc_sensordev);

	for (i = 0; i < OAK_V_MAXSENSORS; i++)
		uoak_sensor_detach(scc, &sc->sc_sensor[i].v);

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
uoakv_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct uoakv_softc *sc = (struct uoakv_softc *)addr;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	int i, idx, frame;
	int16_t val;

	if (scc->sc_ibuf == NULL)
		return;

	memcpy(scc->sc_ibuf, ibuf, len);
	frame = (scc->sc_ibuf[1] << 8) + scc->sc_ibuf[0];

	for (i = 0; i < OAK_V_MAXSENSORS; i++) {
		idx = (i + 1) * 2;
		val = (int16_t)((scc->sc_ibuf[idx+1] << 8) | scc->sc_ibuf[idx]);
		uoak_sensor_update(&sc->sc_sensor[i].v, val);
	}
}

void
uoakv_refresh(void *arg)
{
	struct uoakv_softc *sc = arg;
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	uint8_t led;
	int i;

	/* blink LED for each cycle */
	if (uoak_led_status(scc, OAK_TARGET_RAM, &led) < 0)
		DPRINTF(("status query error\n"));
	if (led == OAK_LED_OFF)
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_ON);
	else
		(void)uoak_led_ctrl(scc, OAK_TARGET_RAM, OAK_LED_OFF);

	for (i = 0; i < OAK_V_MAXSENSORS; i++)
		uoak_sensor_refresh(&sc->sc_sensor[i].v, 1000, 0);
}

int
uoakv_get_channel_setting(struct uoakv_softc *sc, enum uoak_target target,
  int ch)
{
	struct uoak_softc *scc = &sc->sc_uoak_softc;
	uint16_t cmd, result;

	memset(&scc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	scc->sc_rcmd.target = target;
	scc->sc_rcmd.datasize = 0x2;

#define OAK_V_CHANNEL_IDX_OFFSET 3
	cmd = (ch + OAK_V_CHANNEL_IDX_OFFSET);
	USETW(&scc->sc_rcmd.cmd, cmd);

	if (uoak_get_cmd(scc) < 0)
		return EIO;

	result = (scc->sc_buf[2] << 8) + scc->sc_buf[1];
	sc->sc_sensor[ch].offset[target] = result;

	return 0;
}

int
uoakv_get_sensor_setting(struct uoakv_softc *sc, enum uoak_target target)
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
	sc->sc_inputmode[target] = (result & OAK_V_SENSOR_INPUTMODEMASK);

	return 0;
}

/* device specific functions */
void
uoakv_dev_setting(void *parent, enum uoak_target target)
{
	struct uoakv_softc *sc = (struct uoakv_softc *)parent;
	int i;

	/* get device specific configuration */
	(void)uoakv_get_sensor_setting(sc, target);
	for (i = 0; i < OAK_V_MAXSENSORS; i++)
		(void)uoakv_get_channel_setting(sc, target, i);
}

void
uoakv_dev_print(void *parent, enum uoak_target target)
{
	struct uoakv_softc *sc = (struct uoakv_softc *)parent;
	int i;

	printf(", %s", (sc->sc_inputmode[target] ?
	    "Pseudo-Differential" : "Single-Ended"));

	printf(", ADC channel offsets:\n");
	printf("%s: ", sc->sc_hdev.sc_dev.dv_xname);
	for (i = 0; i < OAK_V_MAXSENSORS; i++)
		printf("ch%02d %2d.%02d, ", i,
		    sc->sc_sensor[i].offset[target] / 100,
		    sc->sc_sensor[i].offset[target] % 100);
}
