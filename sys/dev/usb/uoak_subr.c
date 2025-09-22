/*	$OpenBSD: uoak_subr.c,v 1.11 2024/05/23 03:21:09 jsg Exp $   */

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

/* TORADEX OAK series sensors: common functions */ 
/* http://developer.toradex.com/files/toradex-dev/uploads/media/Oak/Oak_ProgrammingGuide.pdf */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/uhidev.h>
#include "uoak.h"

#define UOAK_RETRY_DELAY	 100 /* 100ms, XXX too long? */
#define UOAK_RESPONSE_DELAY	 10  /* 10ms,  XXX too short? */
/*
 *  basic procedure to issue command to the OAK device.
 *  1) check the device is ready to accept command.
 *     if a report of a FEATURE_REPORT request is not start 0xff,
 *     wait for a while, and retry till the response start with 0xff.
 *  2) issue command.  (set or get)
 *  3) if the command will response, wait for a while, and issue
 *     FEATURE_REPORT. leading 0xff indicate the response is valid.
 *     if the first byte is not 0xff, retry.
 */
int
uoak_check_device_ready(struct uoak_softc *sc)
{
	int actlen;

	actlen = uhidev_get_report(sc->sc_hdev->sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev->sc_report_id, &sc->sc_buf, sc->sc_flen);
	if (actlen != sc->sc_flen)
		return EIO;

	if (sc->sc_buf[0] != 0xff)
		return -1;

	return 0;
}

int
uoak_set_cmd(struct uoak_softc *sc)
{
	int actlen;
	sc->sc_rcmd.dir = OAK_SET;

	while (uoak_check_device_ready(sc) < 0)
		usbd_delay_ms(sc->sc_udev, UOAK_RETRY_DELAY);

	actlen = uhidev_set_report(sc->sc_hdev->sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev->sc_report_id, &sc->sc_rcmd, sc->sc_flen);
	if (actlen != sc->sc_flen)
		return EIO;

	return 0;
}

int
uoak_get_cmd(struct uoak_softc *sc)
{
	int actlen;
	sc->sc_rcmd.dir = OAK_GET;

	/* check the device is ready to request */
	while (uoak_check_device_ready(sc) < 0) 
		usbd_delay_ms(sc->sc_udev, UOAK_RETRY_DELAY);

	/* issue request */
	actlen = uhidev_set_report(sc->sc_hdev->sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev->sc_report_id, &sc->sc_rcmd, sc->sc_flen);
	if (actlen != sc->sc_flen)
		return EIO;

	/* wait till the device ready to return the request */
	while (uoak_check_device_ready(sc) < 0) 
		usbd_delay_ms(sc->sc_udev, UOAK_RESPONSE_DELAY); 

	return 0;
}

/*
 * Functions to access device configurations.
 * OAK sensor have some storages to store its configuration.
 * (RAM, FLASH and others)
 */
int
uoak_get_device_name(struct uoak_softc *sc, enum uoak_target target)
{
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x15;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_DEVNAME);

	if (uoak_get_cmd(sc) < 0)
		return EIO;

	strlcpy(sc->sc_config[target].devname, sc->sc_buf+1, 
	    sizeof(sc->sc_config[target].devname));
	return 0;
}

int
uoak_get_report_mode(struct uoak_softc *sc, enum uoak_target target)
{
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x1;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_REPORTMODE);

	if (uoak_get_cmd(sc) < 0)
		return EIO;

	sc->sc_config[target].report_mode = sc->sc_buf[1];
	return 0;
}

int
uoak_get_report_rate(struct uoak_softc *sc, enum uoak_target target)
{
	uint16_t result;
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x2;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_REPORTRATE);

	if (uoak_get_cmd(sc) < 0)
		return EIO;

	result = (sc->sc_buf[2] << 8) + sc->sc_buf[1];
	sc->sc_config[target].report_rate = result;

	return 0;
}

int
uoak_get_sample_rate(struct uoak_softc *sc, enum uoak_target target)
{
	uint16_t result;
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x2;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_SAMPLERATE);

	if (uoak_get_cmd(sc) < 0)
		return EIO;

	result = (sc->sc_buf[2] << 8) + sc->sc_buf[1];
	sc->sc_config[target].sample_rate = result;

	return 0;
}

int
uoak_set_sample_rate(struct uoak_softc *sc, enum uoak_target target, int rate)
{
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x2;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_SAMPLERATE);

#if 0
	sc->sc_rcmd.val[0] = (uint8_t)(rate & 0xff);
	sc->sc_rcmd.val[1] = (uint8_t)((rate >> 8) & 0xff)
#else
	USETW(sc->sc_rcmd.val, rate);
#endif

	if (uoak_set_cmd(sc) < 0)
		return EIO;

	return 0;
}

/*
 * LED I/O
 */
int
uoak_led_status(struct uoak_softc *sc, enum uoak_target target, uint8_t *mode)
{
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));
	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x1;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_LEDMODE);

	if (uoak_get_cmd(sc) < 0)
		return EIO;

	*mode =  sc->sc_buf[1];
	return 0;
}

int
uoak_led_ctrl(struct uoak_softc *sc, enum uoak_target target, uint8_t mode)
{
	memset(&sc->sc_rcmd, 0, sizeof(struct uoak_rcmd));

	sc->sc_rcmd.target = target;
	sc->sc_rcmd.datasize = 0x1;
	USETW(&sc->sc_rcmd.cmd, OAK_CMD_LEDMODE);
	sc->sc_rcmd.val[0] = mode;

	return uoak_set_cmd(sc);
}

/* device setting: query and pretty print */
void
uoak_get_devinfo(struct uoak_softc *sc)
{
	/* get device serial# */
	usbd_fill_deviceinfo(sc->sc_udev, &sc->sc_udi);
}

void
uoak_get_setting(struct uoak_softc *sc, enum uoak_target target)
{
	/* get device level */
	(void)uoak_get_device_name(sc, target);

	/* get global sensor configuration */
	(void)uoak_get_report_mode(sc, target);
	(void)uoak_get_sample_rate(sc, target);
	(void)uoak_get_report_rate(sc, target);

	/* get device specific information */
	if (sc->sc_methods->dev_setting != NULL)
		sc->sc_methods->dev_setting(sc->sc_parent, target);
}

void
uoak_print_devinfo(struct uoak_softc *sc)
{
	printf(": serial %s", sc->sc_udi.udi_serial);
}

void
uoak_print_setting(struct uoak_softc *sc, enum uoak_target target)
{
	switch (sc->sc_config[target].report_mode) {
	case OAK_REPORTMODE_AFTERSAMPLING:
		printf(" sampling %dms",
		    sc->sc_config[target].sample_rate);
		break;
	case OAK_REPORTMODE_AFTERCHANGE:
		printf(" reports changes");
		break;
	case OAK_REPORTMODE_FIXEDRATE:
		printf(" rate %dms", 
		    sc->sc_config[target].report_rate);
		break;
	default:
		printf(" unknown sampling");
		break;
	}

	/* print device specific information */
	if (sc->sc_methods->dev_print != NULL)
		sc->sc_methods->dev_print(sc->sc_parent, target);
	printf("\n");
}

void
uoak_sensor_attach(struct uoak_softc *sc, struct uoak_sensor *s,
  enum sensor_type type)
{
	if (s == NULL)
		return;

	s->avg.type = type;
	s->max.type = type;
	s->min.type = type;
	s->avg.flags |= SENSOR_FINVALID;
	s->max.flags |= SENSOR_FINVALID;
	s->min.flags |= SENSOR_FINVALID;

	(void)snprintf(s->avg.desc, sizeof(s->avg.desc),
	    "avg(#%s)", sc->sc_udi.udi_serial);
	(void)snprintf(s->max.desc, sizeof(s->max.desc),
	    "max(#%s)", sc->sc_udi.udi_serial);
	(void)snprintf(s->min.desc, sizeof(s->min.desc),
	    "min(#%s)", sc->sc_udi.udi_serial);

	sensor_attach(sc->sc_sensordev, &s->avg);
	sensor_attach(sc->sc_sensordev, &s->max);
	sensor_attach(sc->sc_sensordev, &s->min);
}

void
uoak_sensor_detach(struct uoak_softc *sc, struct uoak_sensor *s)
{
	if (s == NULL)
		return;

	sensor_attach(sc->sc_sensordev, &s->avg);
	sensor_attach(sc->sc_sensordev, &s->max);
	sensor_attach(sc->sc_sensordev, &s->min);
}

void
uoak_sensor_update(struct uoak_sensor *s, int val)
{
	if (s == NULL)
		return;

	/* reset */
	if (s->count == 0) {
		s->vmax = s->vmin = s->vavg = val;
		s->count++;
		return;
	}

	/* update min/max */
	if (val > s->vmax)
		s->vmax = val;
	else if (val < s->vmin)
		s->vmin = val;

	/* calc average */
	s->vavg = (s->vavg * s->count + val) / (s->count + 1);

	s->count++;
}

void
uoak_sensor_refresh(struct uoak_sensor *s, int mag, int offset)
{
	if (s == NULL)
		return;
	/* update value */
	s->avg.value = s->vavg * mag + offset;
	s->max.value = s->vmax * mag + offset;
	s->min.value = s->vmin * mag + offset;

	/* update flag */
	s->avg.flags &= ~SENSOR_FINVALID;
	s->max.flags &= ~SENSOR_FINVALID;
	s->min.flags &= ~SENSOR_FINVALID;
	s->count = 0;
}

