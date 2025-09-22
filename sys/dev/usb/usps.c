/*	$OpenBSD: usps.c,v 1.12 2024/05/23 03:21:09 jsg Exp $   */

/*
 * Copyright (c) 2011 Yojiro UO <yuo@nui.org>
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

/* Driver for usb smart power strip FX-5204PS */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#ifdef USPS_DEBUG
int	uspsdebug = 0;
#define DPRINTFN(n, x)	do { if (uspsdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define USPS_UPDATE_TICK	1 /* sec */
#define USPS_TIMEOUT		1000 /* ms */
#define USPS_INTR_TICKS		50 /* ms */

/* protocol */
#define USPS_CMD_START		0x01
#define USPS_CMD_VALUE		0x20
#define USPS_CMD_GET_FIRMWARE	0xc0
#define USPS_CMD_GET_SERIAL	0xc1
#define USPS_CMD_GET_VOLTAGE	0xb0
#define USPS_CMD_GET_TEMP	0xb4
#define USPS_CMD_GET_FREQ	0xa1
#define USPS_CMD_GET_UNK0	0xa2

#define USPS_MODE_WATTAGE	0x10
#define USPS_MODE_CURRENT	0x30

#define FX5204_NUM_PORTS	4

struct usps_port_sensor {
	struct ksensor ave;
	struct ksensor min;
	struct ksensor max;
	int vave, vmin, vmax;
};

struct usps_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	struct usbd_pipe	*sc_ipipe; 
	int			 sc_isize;
	struct usbd_xfer	*sc_xfer;
	uint8_t			 sc_buf[16];
	uint8_t			 *sc_intrbuf;

	uint16_t		 sc_flag;

	/* device info */
	uint8_t		 	 sc_firmware_version[2];
	uint32_t		 sc_device_serial;

	/* sensor framework */
	struct usps_port_sensor	 sc_port_sensor[FX5204_NUM_PORTS];
	struct usps_port_sensor	 sc_total_sensor;
	struct ksensor 		 sc_voltage_sensor;
	struct ksensor		 sc_frequency_sensor;
	struct ksensor		 sc_temp_sensor;
	struct ksensor		 sc_serial_sensor;
	struct ksensordev	 sc_sensordev;
	struct sensor_task	*sc_sensortask;

	int			 sc_count;
};

struct usps_port_pkt {
	uint8_t		header; /* should be 0x80 */
	uint16_t	seq;
	uint8_t		padding[5];
	uint16_t	port[4];
} __packed; /* 16 byte length struct */

static const struct usb_devno usps_devs[] = {
	{ USB_VENDOR_FUJITSUCOMP, USB_PRODUCT_FUJITSUCOMP_FX5204PS},
};
#define usps_lookup(v, p) usb_lookup(usps_devs, v, p)

int  usps_match(struct device *, void *, void *);
void usps_attach(struct device *, struct device *, void *);
int  usps_detach(struct device *, int);
void usps_intr(struct usbd_xfer *, void *, usbd_status);

usbd_status usps_cmd(struct usps_softc *, uint8_t, uint16_t, uint16_t);
usbd_status usps_set_measurement_mode(struct usps_softc *, int);

void usps_get_device_info(struct usps_softc *);
void usps_refresh(void *);
void usps_refresh_temp(struct usps_softc *);
void usps_refresh_power(struct usps_softc *);
void usps_refresh_ports(struct usps_softc *);

struct cfdriver usps_cd = {
	NULL, "usps", DV_DULL
};

const struct cfattach usps_ca = {
	sizeof(struct usps_softc), usps_match, usps_attach, usps_detach
};

int
usps_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	if (usps_lookup(uaa->vendor, uaa->product) == NULL)
		return UMATCH_NONE;

	return (UMATCH_VENDOR_PRODUCT);
}

void
usps_attach(struct device *parent, struct device *self, void *aux)
{
	struct usps_softc *sc = (struct usps_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int ep_ibulk, ep_obulk, ep_intr;
	usbd_status err;
	int i;

	sc->sc_udev = uaa->device;

#define USPS_USB_IFACE 0

	/* get interface handle */
	if ((err = usbd_device2interface_handle(sc->sc_udev, USPS_USB_IFACE,
		&sc->sc_iface)) != 0) {
		printf("%s: failed to get interface %d: %s\n",
		    sc->sc_dev.dv_xname, USPS_USB_IFACE, usbd_errstr(err));
		return;
	}

	/* find endpoints */
	ep_ibulk = ep_obulk = ep_intr = -1;
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: failed to get endpoint %d descriptor\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_ibulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_obulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT){
			ep_intr = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (ep_intr == -1) {
		printf("%s: no data endpoint found\n", sc->sc_dev.dv_xname);
		return;
	}

	usps_get_device_info(sc);
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	/* attach sensor */
	sc->sc_voltage_sensor.type = SENSOR_VOLTS_AC;
	sc->sc_frequency_sensor.type = SENSOR_FREQ;
	sc->sc_temp_sensor.type = SENSOR_TEMP;
	sc->sc_serial_sensor.type = SENSOR_INTEGER;
	sensor_attach(&sc->sc_sensordev, &sc->sc_voltage_sensor);
	sensor_attach(&sc->sc_sensordev, &sc->sc_frequency_sensor);
	sensor_attach(&sc->sc_sensordev, &sc->sc_temp_sensor);
	sensor_attach(&sc->sc_sensordev, &sc->sc_serial_sensor);

	sc->sc_serial_sensor.value = sc->sc_device_serial;
	strlcpy(sc->sc_serial_sensor.desc, "unit serial#",
	    sizeof(sc->sc_serial_sensor.desc));

	/* 
	 * XXX: the device has mode of par port sensor, Watt of Ampair.
	 * currently only watt mode is selected.
	 */
	usps_set_measurement_mode(sc, USPS_MODE_WATTAGE);
	for (i = 0; i < FX5204_NUM_PORTS; i++) {
		sc->sc_port_sensor[i].ave.type = SENSOR_WATTS;
		sc->sc_port_sensor[i].min.type = SENSOR_WATTS;
		sc->sc_port_sensor[i].max.type = SENSOR_WATTS;
		sensor_attach(&sc->sc_sensordev, &sc->sc_port_sensor[i].ave);
		sensor_attach(&sc->sc_sensordev, &sc->sc_port_sensor[i].min);
		sensor_attach(&sc->sc_sensordev, &sc->sc_port_sensor[i].max);
		(void)snprintf(sc->sc_port_sensor[i].ave.desc,
		    sizeof(sc->sc_port_sensor[i].ave.desc),
		    "port#%d (average)", i);
		(void)snprintf(sc->sc_port_sensor[i].min.desc,
		    sizeof(sc->sc_port_sensor[i].min.desc),
		    "port#%d (min)", i);
		(void)snprintf(sc->sc_port_sensor[i].max.desc,
		    sizeof(sc->sc_port_sensor[i].max.desc),
		    "port#%d (max)", i);
	}

	sc->sc_total_sensor.ave.type = SENSOR_WATTS;
	sc->sc_total_sensor.min.type = SENSOR_WATTS;
	sc->sc_total_sensor.max.type = SENSOR_WATTS;
	sensor_attach(&sc->sc_sensordev, &sc->sc_total_sensor.ave);
	sensor_attach(&sc->sc_sensordev, &sc->sc_total_sensor.min);
	sensor_attach(&sc->sc_sensordev, &sc->sc_total_sensor.max);
	(void)snprintf(sc->sc_total_sensor.ave.desc,
	    sizeof(sc->sc_total_sensor.ave.desc), "total (average)");
	(void)snprintf(sc->sc_total_sensor.min.desc,
	    sizeof(sc->sc_total_sensor.ave.desc), "total (min)");
	(void)snprintf(sc->sc_total_sensor.max.desc,
	    sizeof(sc->sc_total_sensor.ave.desc), "total (max)");

	sc->sc_sensortask = sensor_task_register(sc, usps_refresh,
	    USPS_UPDATE_TICK);
	if (sc->sc_sensortask == NULL) {
		printf(", unable to register update task\n");
		goto fail;
	}

	printf("%s: device#=%d, firmware version=V%02dL%02d\n", 
	    sc->sc_dev.dv_xname, sc->sc_device_serial,
	    sc->sc_firmware_version[0],
	    sc->sc_firmware_version[1]);

	sensordev_install(&sc->sc_sensordev);

	/* open interrupt endpoint */
	sc->sc_intrbuf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
	if (sc->sc_intrbuf == NULL) 
		goto fail;
	err = usbd_open_pipe_intr(sc->sc_iface, ep_intr, 
	    USBD_SHORT_XFER_OK, &sc->sc_ipipe, sc, sc->sc_intrbuf,
	    sc->sc_isize, usps_intr, USPS_INTR_TICKS);
	if (err) {
		printf("%s: could not open intr pipe %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		goto fail;
	}

	DPRINTF(("usps_attach: complete\n"));
	return;

fail:
	if (sc->sc_ipipe != NULL)
		usbd_close_pipe(sc->sc_ipipe);
	if (sc->sc_xfer != NULL)
		usbd_free_xfer(sc->sc_xfer);
	if (sc->sc_intrbuf != NULL)
		free(sc->sc_intrbuf, M_USBDEV, sc->sc_isize);
}

int
usps_detach(struct device *self, int flags)
{
	struct usps_softc *sc = (struct usps_softc *)self;
	int i, rv = 0, s;

	usbd_deactivate(sc->sc_udev);

	s = splusb();
	if (sc->sc_ipipe != NULL) {
		usbd_close_pipe(sc->sc_ipipe);
		if (sc->sc_intrbuf != NULL)
			free(sc->sc_intrbuf, M_USBDEV, sc->sc_isize);
		sc->sc_ipipe = NULL;
	}
	if (sc->sc_xfer != NULL)
		usbd_free_xfer(sc->sc_xfer);
	splx(s);

	wakeup(&sc->sc_sensortask);
	sensordev_deinstall(&sc->sc_sensordev);
	sensor_detach(&sc->sc_sensordev, &sc->sc_voltage_sensor);
	sensor_detach(&sc->sc_sensordev, &sc->sc_frequency_sensor);
	sensor_detach(&sc->sc_sensordev, &sc->sc_temp_sensor);
	sensor_detach(&sc->sc_sensordev, &sc->sc_serial_sensor);
	for (i = 0; i < FX5204_NUM_PORTS; i++) {
		sensor_detach(&sc->sc_sensordev, &sc->sc_port_sensor[i].ave);
		sensor_detach(&sc->sc_sensordev, &sc->sc_port_sensor[i].min);
		sensor_detach(&sc->sc_sensordev, &sc->sc_port_sensor[i].max);
	}
	sensor_detach(&sc->sc_sensordev, &sc->sc_total_sensor.ave);
	sensor_detach(&sc->sc_sensordev, &sc->sc_total_sensor.min);
	sensor_detach(&sc->sc_sensordev, &sc->sc_total_sensor.max);

	if (sc->sc_sensortask != NULL)
		sensor_task_unregister(sc->sc_sensortask);

	return (rv);
}

usbd_status
usps_cmd(struct usps_softc *sc, uint8_t cmd, uint16_t val, uint16_t len)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = cmd;
	USETW(req.wValue, val);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	err = usbd_do_request(sc->sc_udev, &req, &sc->sc_buf);
	if (err) {
		printf("%s: could not issue sensor cmd: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	return (0);
}

usbd_status
usps_set_measurement_mode(struct usps_softc *sc, int mode)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = USPS_CMD_START;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, &sc->sc_buf);
	if (err) {
		printf("%s: fail to set sensor mode: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	req.bRequest = USPS_CMD_VALUE;
	USETW(req.wValue, mode);

	err = usbd_do_request(sc->sc_udev, &req, &sc->sc_buf);
	if (err) {
		printf("%s: could not set sensor mode: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	return (0);
}

void
usps_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct usps_softc *sc = priv;
	struct usps_port_pkt *pkt;
	struct usps_port_sensor *ps;
	int i, total;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ipipe);
		return;
	}

	/* process intr packet */
	if (sc->sc_intrbuf == NULL) 
		return;

	pkt = (struct usps_port_pkt *)sc->sc_intrbuf;

	total = 0;
	for (i = 0; i < FX5204_NUM_PORTS; i++) {
		ps = &sc->sc_port_sensor[i];
		if (sc->sc_count == 0) 
			ps->vmax = ps->vmin = pkt->port[i];
		if (pkt->port[i] > ps->vmax)
			ps->vmax = pkt->port[i];
		if (pkt->port[i] < ps->vmin)
			ps->vmin = pkt->port[i];
		ps->vave =
		    (ps->vave * sc->sc_count + pkt->port[i])/(sc->sc_count +1);
		total += pkt->port[i];
	}

	/* calculate ports total */
	ps = &sc->sc_total_sensor;
	if (sc->sc_count == 0)
		ps->vmax = ps->vmin = total;
	if (total > ps->vmax)
		ps->vmax = total;
	if (total < ps->vmin)
		ps->vmin = total;
	ps->vave = (ps->vave * sc->sc_count + total)/(sc->sc_count +1);

	sc->sc_count++;
}

void
usps_get_device_info(struct usps_softc *sc)
{
	int serial;

	/* get Firmware version */
	usps_cmd(sc, USPS_CMD_GET_FIRMWARE, 0, 2);
	sc->sc_firmware_version[0] = 
	    (sc->sc_buf[0]>>4) * 10 + (sc->sc_buf[0] & 0xf);
	sc->sc_firmware_version[1] =
	    (sc->sc_buf[1]>>4) * 10 + (sc->sc_buf[1] & 0xf);

	/* get device serial number */
	usps_cmd(sc, USPS_CMD_GET_SERIAL, 0, 3);

	serial = 0;
	serial += ((sc->sc_buf[0]>>4) * 10 + (sc->sc_buf[0] & 0xf)) * 10000;
	serial += ((sc->sc_buf[1]>>4) * 10 + (sc->sc_buf[1] & 0xf)) * 100;
	serial += ((sc->sc_buf[2]>>4) * 10 + (sc->sc_buf[2] & 0xf));
	sc->sc_device_serial = serial;
}

void
usps_refresh(void *arg)
{
	struct usps_softc *sc = arg;

	usps_refresh_temp(sc);
	usps_refresh_power(sc);
	usps_refresh_ports(sc);
}

void
usps_refresh_ports(struct usps_softc *sc)
{
	int i;
	struct usps_port_sensor *ps;

	/* update port values */
	for (i = 0; i < FX5204_NUM_PORTS; i++) {
		ps = &sc->sc_port_sensor[i];
		ps->ave.value = ps->vave * 1000000;
		ps->min.value = ps->vmin * 1000000;
		ps->max.value = ps->vmax * 1000000;
	}

	/* update total value */
	ps = &sc->sc_total_sensor;
	ps->ave.value = ps->vave * 1000000;
	ps->min.value = ps->vmin * 1000000;
	ps->max.value = ps->vmax * 1000000;

	sc->sc_count = 0;
}

void
usps_refresh_temp(struct usps_softc *sc)
{
	int temp;

	if (usps_cmd(sc, USPS_CMD_GET_TEMP, 0, 2) != 0) {
		DPRINTF(("%s: temperature data read error\n",
		    sc->sc_dev.dv_xname));
		sc->sc_temp_sensor.flags |= SENSOR_FINVALID;
		return;
	}
	temp = (sc->sc_buf[1] << 8) + sc->sc_buf[0];
	sc->sc_temp_sensor.value = (temp * 10000) + 273150000;
	sc->sc_temp_sensor.flags &= ~SENSOR_FINVALID;
}

void
usps_refresh_power(struct usps_softc *sc)
{
	int v;
	uint val;
	uint64_t f;

	/* update source voltage */
	if (usps_cmd(sc, USPS_CMD_GET_VOLTAGE, 0, 1) != 0) {
		DPRINTF(("%s: voltage data read error\n", sc->sc_dev.dv_xname));
		sc->sc_voltage_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	v = sc->sc_buf[0] * 1000000;
	sc->sc_voltage_sensor.value = v;
	sc->sc_voltage_sensor.flags &= ~SENSOR_FINVALID;

	/* update source frequency */
	if (usps_cmd(sc, USPS_CMD_GET_FREQ, 0, 8) != 0) {
		DPRINTF(("%s: frequency data read error\n",
		    sc->sc_dev.dv_xname));
		sc->sc_frequency_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	if (sc->sc_buf[7] == 0 && sc->sc_buf[6] == 0) {
		/* special case */
		f = 0;
	} else {
		val = (sc->sc_buf[1] << 8) + sc->sc_buf[0];
		if (val == 0) {
			/* guard against "division by zero" */
			sc->sc_frequency_sensor.flags |= SENSOR_FINVALID;
			return;
		}
		f = 2000000L;
		f *=  1000000L;
		f /= val;
	}

	sc->sc_frequency_sensor.value = f;
	sc->sc_frequency_sensor.flags &= ~SENSOR_FINVALID;
}
