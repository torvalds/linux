/*	$OpenBSD: umbg.c,v 1.29 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/time.h>
#include <sys/sensors.h>
#include <sys/timeout.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#ifdef UMBG_DEBUG
#define DPRINTFN(n, x)	do { if (umbgdebug > (n)) printf x; } while (0)
int umbgdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

#ifdef UMBG_DEBUG
#define TRUSTTIME	((long) 60)
#else
#define TRUSTTIME	((long) 12 * 60 * 60)	/* degrade OK > WARN > CRIT */
#endif

struct umbg_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* data interface */

	int			sc_bulkin_no;
	struct usbd_pipe	*sc_bulkin_pipe;
	int			sc_bulkout_no;
	struct usbd_pipe	*sc_bulkout_pipe;

	struct timeout		sc_to;		/* get time from device */
	struct usb_task		sc_task;

	struct timeout		sc_it_to;	/* invalidate sensor */

	usb_device_request_t	sc_req;

	struct ksensor		sc_timedelta;	/* timedelta */
	struct ksensor		sc_signal;	/* signal quality and status */
	struct ksensordev	sc_sensordev;
};

struct mbg_time {
	u_int8_t		hundreds;
	u_int8_t		sec;
	u_int8_t		min;
	u_int8_t		hour;
	u_int8_t		mday;
	u_int8_t		wday;	/* 1 (monday) - 7 (sunday) */
	u_int8_t		mon;
	u_int8_t		year;	/* 0 - 99 */
	u_int8_t		status;
	u_int8_t		signal;
	int8_t			utc_off;
};

struct mbg_time_hr {
	u_int32_t		sec;		/* always UTC */
	u_int32_t		frac;		/* fractions of second */
	int32_t			utc_off;	/* informal only, in seconds */
	u_int16_t		status;
	u_int8_t		signal;
};

/* mbg_time.status bits */
#define MBG_FREERUN		0x01	/* clock running on xtal */
#define MBG_DST_ENA		0x02	/* DST enabled */
#define MBG_SYNC		0x04	/* clock synced at least once */
#define MBG_DST_CHG		0x08	/* DST change announcement */
#define MBG_UTC			0x10	/* special UTC firmware is installed */
#define MBG_LEAP		0x20	/* announcement of a leap second */
#define MBG_IFTM		0x40	/* current time was set from host */
#define MBG_INVALID		0x80	/* time invalid, batt. was disconn. */

/* commands */
#define MBG_GET_TIME		0x00
#define MBG_GET_SYNC_TIME	0x02
#define MBG_GET_TIME_HR		0x03
#define MBG_SET_TIME		0x10
#define MBG_GET_TZCODE		0x32
#define MBG_SET_TZCODE		0x33
#define MBG_GET_FW_ID_1		0x40
#define MBG_GET_FW_ID_2		0x41
#define MBG_GET_SERNUM		0x42

/* timezone codes (for MBG_{GET|SET}_TZCODE) */
#define MBG_TZCODE_CET_CEST	0x00
#define MBG_TZCODE_CET		0x01
#define MBG_TZCODE_UTC		0x02
#define MBG_TZCODE_EET_EEST	0x03

/* misc. constants */
#define MBG_FIFO_LEN		16
#define MBG_ID_LEN		(2 * MBG_FIFO_LEN + 1)
#define MBG_BUSY		0x01
#define MBG_SIG_BIAS		55
#define MBG_SIG_MAX		68
#define NSECPERSEC		1000000000LL	/* nanoseconds per second */
#define HRDIVISOR		0x100000000LL	/* for hi-res timestamp */

static int t_wait, t_trust;

void umbg_intr(void *);
void umbg_it_intr(void *);

int umbg_match(struct device *, void *, void *);
void umbg_attach(struct device *, struct device *, void *);
int umbg_detach(struct device *, int);

void umbg_task(void *);

int umbg_read(struct umbg_softc *, u_int8_t cmd, char *buf, size_t len,
    struct timespec *tstamp);

struct cfdriver umbg_cd = {
	NULL, "umbg", DV_DULL
};

const struct cfattach umbg_ca = {
	sizeof(struct umbg_softc), umbg_match, umbg_attach, umbg_detach
};

int
umbg_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	return uaa->vendor == USB_VENDOR_MEINBERG && (
	    uaa->product == USB_PRODUCT_MEINBERG_USB5131 ||
	    uaa->product == USB_PRODUCT_MEINBERG_DCF600USB) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
umbg_attach(struct device *parent, struct device *self, void *aux)
{
	struct umbg_softc *sc = (struct umbg_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface = uaa->iface;
	struct mbg_time tframe;
	usb_endpoint_descriptor_t *ed;
	usbd_status err;
	int signal;
	const char *desc;
#ifdef UMBG_DEBUG
	char fw_id[MBG_ID_LEN];
#endif
	sc->sc_udev = dev;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_timedelta.type = SENSOR_TIMEDELTA;
	sc->sc_timedelta.status = SENSOR_S_UNKNOWN;
	
	switch (uaa->product) {
	case USB_PRODUCT_MEINBERG_DCF600USB:
		desc = "DCF600USB";
		break;
	case USB_PRODUCT_MEINBERG_USB5131:
		desc = "USB5131";
		break;
	default:
		desc = "Unspecified Radio clock";
	}
	strlcpy(sc->sc_timedelta.desc, desc,
	    sizeof(sc->sc_timedelta.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_timedelta);

	sc->sc_signal.type = SENSOR_PERCENT;
	strlcpy(sc->sc_signal.desc, "Signal", sizeof(sc->sc_signal.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_signal);
	sensordev_install(&sc->sc_sensordev);

	usb_init_task(&sc->sc_task, umbg_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->sc_to, umbg_intr, sc);
	timeout_set(&sc->sc_it_to, umbg_it_intr, sc);

	if ((err = usbd_device2interface_handle(dev, 0, &iface))) {
		printf("%s: failed to get interface, err=%s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		goto fishy;
	}

	ed = usbd_interface2endpoint_descriptor(iface, 0);
	sc->sc_bulkin_no = ed->bEndpointAddress;
	ed = usbd_interface2endpoint_descriptor(iface, 1);
	sc->sc_bulkout_no = ed->bEndpointAddress;

	sc->sc_iface = iface;

	err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkin_no,
	    USBD_EXCLUSIVE_USE, &sc->sc_bulkin_pipe);
	if (err) {
		printf("%s: open rx pipe failed: %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(err));
		goto fishy;
	}

	err = usbd_open_pipe(sc->sc_iface, sc->sc_bulkout_no,
	    USBD_EXCLUSIVE_USE, &sc->sc_bulkout_pipe);
	if (err) {
		printf("%s: open tx pipe failed: %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(err));
		goto fishy;
	}

	printf("%s: ", sc->sc_dev.dv_xname);
	if (umbg_read(sc, MBG_GET_TIME, (char *)&tframe,
	    sizeof(struct mbg_time), NULL)) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		printf("unknown status");
	} else {
		sc->sc_signal.status = SENSOR_S_OK;
		signal = tframe.signal - MBG_SIG_BIAS;
		if (signal < 0)
			signal = 0;
		else if (signal > MBG_SIG_MAX)
			signal = MBG_SIG_MAX;
		sc->sc_signal.value = signal;

		if (tframe.status & MBG_SYNC)
			printf("synchronized");
		else
			printf("not synchronized");
		if (tframe.status & MBG_FREERUN) {
			sc->sc_signal.status = SENSOR_S_WARN;
			printf(", freerun");
		}
		if (tframe.status & MBG_IFTM)
			printf(", time set from host");
	}
#ifdef UMBG_DEBUG
	if (umbg_read(sc, MBG_GET_FW_ID_1, fw_id, MBG_FIFO_LEN, NULL) ||
	    umbg_read(sc, MBG_GET_FW_ID_2, &fw_id[MBG_FIFO_LEN], MBG_FIFO_LEN,
	    NULL))
		printf(", firmware unknown");
	else {
		fw_id[MBG_ID_LEN - 1] = '\0';
		printf(", firmware %s", fw_id);
	}
#endif
	printf("\n");

	t_wait = 5;

	t_trust = TRUSTTIME;

	usb_add_task(sc->sc_udev, &sc->sc_task);
	return;

fishy:
	usbd_deactivate(sc->sc_udev);
}

int
umbg_detach(struct device *self, int flags)
{
	struct umbg_softc *sc = (struct umbg_softc *)self;
	usbd_status err;

	if (timeout_initialized(&sc->sc_to))
		timeout_del(&sc->sc_to);
	if (timeout_initialized(&sc->sc_it_to))
		timeout_del(&sc->sc_it_to);

	usb_rem_task(sc->sc_udev, &sc->sc_task);

	if (sc->sc_bulkin_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_bulkin_pipe);
		if (err)
			printf("%s: close rx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_bulkin_pipe = NULL;
	}
	if (sc->sc_bulkout_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_bulkout_pipe);
		if (err)
			printf("%s: close tx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_bulkout_pipe = NULL;
	}

	/* Unregister the clock with the kernel */
	sensordev_deinstall(&sc->sc_sensordev);

	return 0;
}

void
umbg_intr(void *xsc)
{
	struct umbg_softc *sc = xsc;
	usb_add_task(sc->sc_udev, &sc->sc_task);
}

/* umbg_task_hr() read a high resolution timestamp from the device. */
void
umbg_task(void *arg)
{
	struct umbg_softc *sc = (struct umbg_softc *)arg;
	struct mbg_time_hr tframe;
	struct timespec tstamp;
	int64_t tlocal, trecv;
	int signal;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (umbg_read(sc, MBG_GET_TIME_HR, (char *)&tframe, sizeof(tframe),
	    &tstamp)) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		goto bail_out;
	}
	if (tframe.status & MBG_INVALID) {
		sc->sc_signal.status = SENSOR_S_CRIT;
		goto bail_out;
	}

	tlocal = tstamp.tv_sec * NSECPERSEC + tstamp.tv_nsec;
	trecv = letoh32(tframe.sec) * NSECPERSEC +
	    (letoh32(tframe.frac) * NSECPERSEC >> 32);

	sc->sc_timedelta.value = tlocal - trecv;
	if (sc->sc_timedelta.status == SENSOR_S_UNKNOWN ||
		!(letoh16(tframe.status) & MBG_FREERUN)) {
		sc->sc_timedelta.status = SENSOR_S_OK;
		timeout_add_sec(&sc->sc_it_to, t_trust);
	}

	sc->sc_timedelta.tv.tv_sec = tstamp.tv_sec;
	sc->sc_timedelta.tv.tv_usec = tstamp.tv_nsec / 1000;

	signal = tframe.signal - MBG_SIG_BIAS;
	if (signal < 0)
		signal = 0;
	else if (signal > MBG_SIG_MAX)
		signal = MBG_SIG_MAX;

	sc->sc_signal.value = signal * 100000 / MBG_SIG_MAX;
	sc->sc_signal.status = letoh16(tframe.status) & MBG_FREERUN ?
	    SENSOR_S_WARN : SENSOR_S_OK;
	sc->sc_signal.tv.tv_sec = sc->sc_timedelta.tv.tv_sec;
	sc->sc_signal.tv.tv_usec = sc->sc_timedelta.tv.tv_usec;

bail_out:
	timeout_add_sec(&sc->sc_to, t_wait);
	
}

/* send a command and read back results */
int
umbg_read(struct umbg_softc *sc, u_int8_t cmd, char *buf, size_t len,
    struct timespec *tstamp)
{
	usbd_status err;
	struct usbd_xfer *xfer;

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL) {
		DPRINTF(("%s: alloc xfer failed\n", sc->sc_dev.dv_xname));
		return -1;
	}

	usbd_setup_xfer(xfer, sc->sc_bulkout_pipe, NULL, &cmd, sizeof(cmd),
	    USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS, USBD_DEFAULT_TIMEOUT, NULL);
	if (tstamp)
		nanotime(tstamp);
	err = usbd_transfer(xfer);
	if (err) {
		DPRINTF(("%s: sending of command failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err)));
		usbd_free_xfer(xfer);
		return -1;
	}

	usbd_setup_xfer(xfer, sc->sc_bulkin_pipe, NULL, buf, len,
	    USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS, USBD_DEFAULT_TIMEOUT, NULL);

	err = usbd_transfer(xfer);
	usbd_free_xfer(xfer);
	if (err) {
		DPRINTF(("%s: reading data failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err)));
		return -1;
	}
	return 0;
}

void
umbg_it_intr(void *xsc)
{
	struct umbg_softc *sc = xsc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (sc->sc_timedelta.status == SENSOR_S_OK) {
		sc->sc_timedelta.status = SENSOR_S_WARN;
		/*
		 * further degrade in TRUSTTIME seconds if the clocks remains
		 * free running.
		 */
		timeout_add_sec(&sc->sc_it_to, t_trust);
	} else
		sc->sc_timedelta.status = SENSOR_S_CRIT;
}
