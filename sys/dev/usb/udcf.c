/*	$OpenBSD: udcf.c,v 1.66 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2006, 2007, 2008 Marc Balmer <mbalmer@openbsd.org>
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

#ifdef UDCF_DEBUG
#define DPRINTFN(n, x)	do { if (udcfdebug > (n)) printf x; } while (0)
int udcfdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

#define UDCF_READ_IDX	0x1f

#define UDCF_CTRL_IDX	0x33
#define UDCF_CTRL_VAL	0x98

#define FT232R_RESET	0x00	/* reset USB request */
#define FT232R_STATUS	0x05	/* get modem status USB request */
#define FT232R_RI	0x40	/* ring indicator */

/* max. skew of received time diff vs. measured time diff in percent. */
#define MAX_SKEW	5

#define CLOCK_DCF77	"DCF77"

struct udcf_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* data interface */

	struct timeout		sc_to;
	struct usb_task		sc_task;

	struct timeout		sc_bv_to;	/* bit-value detect */
	struct timeout		sc_db_to;	/* debounce */
	struct timeout		sc_mg_to;	/* minute-gap detect */
	struct timeout		sc_sl_to;	/* signal-loss detect */
	struct timeout		sc_it_to;	/* invalidate time */
	struct usb_task		sc_bv_task;
	struct usb_task		sc_mg_task;
	struct usb_task		sc_sl_task;

	usb_device_request_t	sc_req;

	int			sc_sync;	/* 1 during sync */
	u_int64_t		sc_mask;	/* 64 bit mask */
	u_int64_t		sc_tbits;	/* Time bits */
	int			sc_minute;
	int			sc_level;
	time_t			sc_last_mg;
	int			(*sc_signal)(struct udcf_softc *);

	time_t			sc_current;	/* current time */
	time_t			sc_next;	/* time to become valid next */
	time_t			sc_last;
	int			sc_nrecv;	/* consecutive valid times */
	struct timeval		sc_last_tv;	/* uptime of last valid time */
	struct ksensor		sc_sensor;
#ifdef UDCF_DEBUG
	struct ksensor		sc_skew;	/* recv vs local skew */
#endif
	struct ksensordev	sc_sensordev;
};

/* timeouts in milliseconds: */
#define	T_BV		150	/* bit value detection (150ms) */
#define	T_SYNC		950	/* sync (950ms) */
#define	T_MG		1500	/* minute gap detection (1500ms) */
#define	T_MGSYNC	450	/* resync after a minute gap (450ms) */
#define	T_SL		3000	/* detect signal loss (3sec) */
#define	T_WAIT		5000	/* wait (5sec) */
#define	T_WARN		300000	/* degrade sensor status to warning (5min) */
#define	T_CRIT		900000	/* degrade sensor status to critical (15min) */

void	udcf_intr(void *);
void	udcf_probe(void *);

void	udcf_bv_intr(void *);
void	udcf_mg_intr(void *);
void	udcf_sl_intr(void *);
void	udcf_it_intr(void *);
void	udcf_bv_probe(void *);
void	udcf_mg_probe(void *);
void	udcf_sl_probe(void *);

int udcf_match(struct device *, void *, void *);
void udcf_attach(struct device *, struct device *, void *);
int udcf_detach(struct device *, int);

int udcf_nc_signal(struct udcf_softc *);
int udcf_nc_init_hw(struct udcf_softc *);
int udcf_ft232r_signal(struct udcf_softc *);
int udcf_ft232r_init_hw(struct udcf_softc *);

struct cfdriver udcf_cd = {
	NULL, "udcf", DV_DULL
};

const struct cfattach udcf_ca = {
	sizeof(struct udcf_softc), udcf_match, udcf_attach, udcf_detach,
};

static const struct usb_devno udcf_devs[] = {
	{ USB_VENDOR_GUDE, USB_PRODUCT_GUDE_DCF },
	{ USB_VENDOR_FTDI, USB_PRODUCT_FTDI_DCF }
};

int
udcf_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg		*uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	return (usb_lookup(udcf_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
udcf_attach(struct device *parent, struct device *self, void *aux)
{
	struct udcf_softc		*sc = (struct udcf_softc *)self;
	struct usb_attach_arg		*uaa = aux;
	struct usbd_device		*dev = uaa->device;
	struct usbd_interface		*iface;
	usbd_status			 err;

	switch (uaa->product) {
	case USB_PRODUCT_GUDE_DCF:
		sc->sc_signal = udcf_nc_signal;
		strlcpy(sc->sc_sensor.desc, "DCF77",
		    sizeof(sc->sc_sensor.desc));
		break;
	case USB_PRODUCT_FTDI_DCF:
		sc->sc_signal = udcf_ft232r_signal;
		strlcpy(sc->sc_sensor.desc, "DCF77",
		    sizeof(sc->sc_sensor.desc));
		break;
	}

	usb_init_task(&sc->sc_task, udcf_probe, sc, USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_bv_task, udcf_bv_probe, sc, USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_mg_task, udcf_mg_probe, sc, USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_sl_task, udcf_sl_probe, sc, USB_TASK_TYPE_GENERIC);

	timeout_set(&sc->sc_to, udcf_intr, sc);
	timeout_set(&sc->sc_bv_to, udcf_bv_intr, sc);
	timeout_set(&sc->sc_mg_to, udcf_mg_intr, sc);
	timeout_set(&sc->sc_sl_to, udcf_sl_intr, sc);
	timeout_set(&sc->sc_it_to, udcf_it_intr, sc);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TIMEDELTA;
	sc->sc_sensor.status = SENSOR_S_UNKNOWN;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

#ifdef UDCF_DEBUG
	sc->sc_skew.type = SENSOR_TIMEDELTA;
	sc->sc_skew.status = SENSOR_S_UNKNOWN;
	strlcpy(sc->sc_skew.desc, "local clock skew",
	    sizeof(sc->sc_skew.desc));
	sensor_attach(&sc->sc_sensordev, &sc->sc_skew);
#endif
	sensordev_install(&sc->sc_sensordev);

	sc->sc_udev = dev;
	if ((err = usbd_device2interface_handle(dev, 0, &iface))) {
		DPRINTF(("%s: failed to get interface, err=%s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err)));
		goto fishy;
	}

	sc->sc_iface = iface;

	sc->sc_level = 0;
	sc->sc_minute = 0;
	sc->sc_last_mg = 0L;

	sc->sc_sync = 1;

	sc->sc_current = 0L;
	sc->sc_next = 0L;
	sc->sc_nrecv = 0;
	sc->sc_last = 0L;
	sc->sc_last_tv.tv_sec = 0L;

	switch (uaa->product) {
	case USB_PRODUCT_GUDE_DCF:
		if (udcf_nc_init_hw(sc))
			goto fishy;
		break;
	case USB_PRODUCT_FTDI_DCF:
		if (udcf_ft232r_init_hw(sc))
			goto fishy;
		break;
	}

	/* Give the receiver some slack to stabilize */
	timeout_add_msec(&sc->sc_to, T_WAIT);

	/* Detect signal loss */
	timeout_add_msec(&sc->sc_sl_to, T_WAIT + T_SL);

	DPRINTF(("synchronizing\n"));
	return;

fishy:
	DPRINTF(("udcf_attach failed\n"));
	usbd_deactivate(sc->sc_udev);
}

int
udcf_detach(struct device *self, int flags)
{
	struct udcf_softc	*sc = (struct udcf_softc *)self;

	if (timeout_initialized(&sc->sc_to))
		timeout_del(&sc->sc_to);
	if (timeout_initialized(&sc->sc_bv_to))
		timeout_del(&sc->sc_bv_to);
	if (timeout_initialized(&sc->sc_mg_to))
		timeout_del(&sc->sc_mg_to);
	if (timeout_initialized(&sc->sc_sl_to))
		timeout_del(&sc->sc_sl_to);
	if (timeout_initialized(&sc->sc_it_to))
		timeout_del(&sc->sc_it_to);

	/* Unregister the clock with the kernel */
	sensordev_deinstall(&sc->sc_sensordev);
	usb_rem_task(sc->sc_udev, &sc->sc_task);
	usb_rem_task(sc->sc_udev, &sc->sc_bv_task);
	usb_rem_task(sc->sc_udev, &sc->sc_mg_task);
	usb_rem_task(sc->sc_udev, &sc->sc_sl_task);

	return 0;
}

/* udcf_intr runs in an interrupt context */
void
udcf_intr(void *xsc)
{
	struct udcf_softc *sc = xsc;
	usb_add_task(sc->sc_udev, &sc->sc_task);
}

/* bit value detection */
void
udcf_bv_intr(void *xsc)
{
	struct udcf_softc *sc = xsc;
	usb_add_task(sc->sc_udev, &sc->sc_bv_task);
}

/* minute gap detection */
void
udcf_mg_intr(void *xsc)
{
	struct udcf_softc *sc = xsc;
	usb_add_task(sc->sc_udev, &sc->sc_mg_task);
}

/* signal loss detection */
void
udcf_sl_intr(void *xsc)
{
	struct udcf_softc *sc = xsc;
	usb_add_task(sc->sc_udev, &sc->sc_sl_task);
}

/*
 * initialize the Expert mouseCLOCK USB devices, they use a NetCologne
 * chip to interface the receiver.  Power must be supplied to the
 * receiver and the receiver must be turned on.
 */
int
udcf_nc_init_hw(struct udcf_softc *sc)
{
	usbd_status			 err;
	usb_device_request_t		 req;
	uWord				 result;
	int				 actlen;

	/* Prepare the USB request to probe the value */
	sc->sc_req.bmRequestType = UT_READ_VENDOR_DEVICE;
	sc->sc_req.bRequest = 1;
	USETW(sc->sc_req.wValue, 0);
	USETW(sc->sc_req.wIndex, UDCF_READ_IDX);
	USETW(sc->sc_req.wLength, 1);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = 0;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if ((err = usbd_do_request_flags(sc->sc_udev, &req, &result,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT))) {
		DPRINTF(("failed to turn on power for receiver\n"));
		return -1;
	}

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = 0;
	USETW(req.wValue, UDCF_CTRL_VAL);
	USETW(req.wIndex, UDCF_CTRL_IDX);
	USETW(req.wLength, 0);
	if ((err = usbd_do_request_flags(sc->sc_udev, &req, &result,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT))) {
		DPRINTF(("failed to turn on receiver\n"));
		return -1;
	}
	return 0;
}

/*
 * initialize the Expert mouseCLOCK USB II devices, they use an FTDI
 * FT232R chip to interface the receiver.  Only reset the chip.
 */
int
udcf_ft232r_init_hw(struct udcf_softc *sc)
{
	usbd_status		err;
	usb_device_request_t	req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = FT232R_RESET;
	/* 0 resets the SIO */
	USETW(req.wValue,FT232R_RESET);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		DPRINTF(("failed to reset ftdi\n"));
		return -1;
	}
	return 0;
}

/*
 * return 1 during high-power-, 0 during low-power-emission
 * If bit 0 is set, the transmitter emits at full power.
 * During the low-power emission we decode a zero bit.
 */
int
udcf_nc_signal(struct udcf_softc *sc)
{
	int		actlen;
	unsigned char	data;

	if (usbd_do_request_flags(sc->sc_udev, &sc->sc_req, &data,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT))
		/* This happens if we pull the receiver */
		return -1;
	return data & 0x01;
}

/* pick up the signal level through the FTDI FT232R chip */
int
udcf_ft232r_signal(struct udcf_softc *sc)
{
	usb_device_request_t	req;
	int			actlen;
	u_int16_t		data;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = FT232R_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 2);
	if (usbd_do_request_flags(sc->sc_udev, &req, &data,
	    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT)) {
		DPRINTFN(2, ("error reading ftdi modem status\n"));
		return -1;
	}
	DPRINTFN(2, ("ftdi status 0x%04x\n", data));
	return data & FT232R_RI ? 0 : 1;
}

/* udcf_probe runs in a process context. */
void
udcf_probe(void *xsc)
{
	struct udcf_softc	*sc = xsc;
	struct timespec		 now;
	int			 data;

	if (usbd_is_dying(sc->sc_udev))
		return;

	data = sc->sc_signal(sc);
	if (data == -1)
		return;

	if (data) {
		sc->sc_level = 1;
		timeout_add(&sc->sc_to, 1);
		return;
	}

	if (sc->sc_level == 0)
		return;

	/* the beginning of a second */
	sc->sc_level = 0;
	if (sc->sc_minute == 1) {
		if (sc->sc_sync) {
			DPRINTF(("start collecting bits\n"));
			sc->sc_sync = 0;
		} else {
			/* provide the timedelta */
			microtime(&sc->sc_sensor.tv);
			nanotime(&now);
			sc->sc_current = sc->sc_next;
			sc->sc_sensor.value = (int64_t)(now.tv_sec -
			    sc->sc_current) * 1000000000LL + now.tv_nsec;

			sc->sc_sensor.status = SENSOR_S_OK;

			/*
			 * if no valid time information is received
			 * during the next 5 minutes, the sensor state
			 * will be degraded to SENSOR_S_WARN
			 */
			timeout_add_msec(&sc->sc_it_to, T_WARN);
		}
		sc->sc_minute = 0;
	}

	timeout_add_msec(&sc->sc_to, T_SYNC);	/* resync in 950 ms */

	/* no clock and bit detection during sync */
	if (!sc->sc_sync) {
		/* detect bit value */
		timeout_add_msec(&sc->sc_bv_to, T_BV);
	}
	timeout_add_msec(&sc->sc_mg_to, T_MG);	/* detect minute gap */
	timeout_add_msec(&sc->sc_sl_to, T_SL);	/* detect signal loss */
}

/* detect the bit value */
void
udcf_bv_probe(void *xsc)
{
	struct udcf_softc	*sc = xsc;
	int			 data;

	if (usbd_is_dying(sc->sc_udev))
		return;

	data = sc->sc_signal(sc);
	if (data == -1) {
		DPRINTF(("bit detection failed\n"));
		return;
	}	

	DPRINTFN(1, (data ? "0" : "1"));
	if (!(data))
		sc->sc_tbits |= sc->sc_mask;
	sc->sc_mask <<= 1;
}

/* detect the minute gap */
void
udcf_mg_probe(void *xsc)
{
	struct udcf_softc	*sc = xsc;
	struct clock_ymdhms	 ymdhm;
	struct timeval		 monotime;
	int			 tdiff_recv, tdiff_local;
	int			 skew;
	int			 minute_bits, hour_bits, day_bits;
	int			 month_bits, year_bits, wday;
	int			 p1, p2, p3;
	int			 p1_bit, p2_bit, p3_bit;
	int			 r_bit, a1_bit, a2_bit, z1_bit, z2_bit;
	int			 s_bit, m_bit;
	u_int32_t		 parity = 0x6996;

	if (sc->sc_sync) {
		sc->sc_minute = 1;
		goto cleanbits;
	}

	if (gettime() - sc->sc_last_mg < 57) {
		DPRINTF(("\nunexpected gap, resync\n"));
		sc->sc_sync = sc->sc_minute = 1;
		goto cleanbits;	
	}

	/* extract bits w/o parity */
	m_bit = sc->sc_tbits & 1;
	r_bit = sc->sc_tbits >> 15 & 1;
	a1_bit = sc->sc_tbits >> 16 & 1;
	z1_bit = sc->sc_tbits >> 17 & 1;
	z2_bit = sc->sc_tbits >> 18 & 1;
	a2_bit = sc->sc_tbits >> 19 & 1;
	s_bit = sc->sc_tbits >> 20 & 1;
	p1_bit = sc->sc_tbits >> 28 & 1;
	p2_bit = sc->sc_tbits >> 35 & 1;
	p3_bit = sc->sc_tbits >> 58 & 1;

	minute_bits = sc->sc_tbits >> 21 & 0x7f;	
	hour_bits = sc->sc_tbits >> 29 & 0x3f;
	day_bits = sc->sc_tbits >> 36 & 0x3f;
	wday = (sc->sc_tbits >> 42) & 0x07;
	month_bits = sc->sc_tbits >> 45 & 0x1f;
	year_bits = sc->sc_tbits >> 50 & 0xff;

	/* validate time information */
	p1 = (parity >> (minute_bits & 0x0f) & 1) ^
	    (parity >> (minute_bits >> 4) & 1);

	p2 = (parity >> (hour_bits & 0x0f) & 1) ^
	    (parity >> (hour_bits >> 4) & 1);

	p3 = (parity >> (day_bits & 0x0f) & 1) ^
	    (parity >> (day_bits >> 4) & 1) ^
	    ((parity >> wday) & 1) ^ (parity >> (month_bits & 0x0f) & 1) ^
	    (parity >> (month_bits >> 4) & 1) ^
	    (parity >> (year_bits & 0x0f) & 1) ^
	    (parity >> (year_bits >> 4) & 1);

	if (m_bit == 0 && s_bit == 1 && p1 == p1_bit && p2 == p2_bit &&
	    p3 == p3_bit && (z1_bit ^ z2_bit)) {

		/* Decode time */
		if ((ymdhm.dt_year = 2000 + FROMBCD(year_bits)) > 2037) {
			DPRINTF(("year out of range, resync\n"));
			sc->sc_sync = 1;
			goto cleanbits;
		}
		ymdhm.dt_min = FROMBCD(minute_bits);
		ymdhm.dt_hour = FROMBCD(hour_bits);
		ymdhm.dt_day = FROMBCD(day_bits);
		ymdhm.dt_mon = FROMBCD(month_bits);
		ymdhm.dt_sec = 0;

		sc->sc_next = clock_ymdhms_to_secs(&ymdhm);
		getmicrouptime(&monotime);

		/* convert to coordinated universal time */
		sc->sc_next -= z1_bit ? 7200 : 3600;

		DPRINTF(("\n%02d.%02d.%04d %02d:%02d:00 %s",
		    ymdhm.dt_day, ymdhm.dt_mon, ymdhm.dt_year,
		    ymdhm.dt_hour, ymdhm.dt_min, z1_bit ? "CEST" : "CET"));
		DPRINTF((r_bit ? ", call bit" : ""));
		DPRINTF((a1_bit ? ", dst chg ann." : ""));
		DPRINTF((a2_bit ? ", leap sec ann." : ""));
		DPRINTF(("\n"));

		if (sc->sc_last) {
			tdiff_recv = sc->sc_next - sc->sc_last;
			tdiff_local = monotime.tv_sec - sc->sc_last_tv.tv_sec;
			skew = abs(tdiff_local - tdiff_recv);
#ifdef UDCF_DEBUG
			if (sc->sc_skew.status == SENSOR_S_UNKNOWN)
				sc->sc_skew.status = SENSOR_S_CRIT;
			sc->sc_skew.value = skew * 1000000000LL;
			getmicrotime(&sc->sc_skew.tv);
#endif
			DPRINTF(("local = %d, recv = %d, skew = %d\n",
			    tdiff_local, tdiff_recv, skew));

			if (skew && skew * 100LL / tdiff_local > MAX_SKEW) {
				DPRINTF(("skew out of tolerated range\n"));
				goto cleanbits;
			} else {
				if (sc->sc_nrecv < 2) {
					sc->sc_nrecv++;
					DPRINTF(("got frame %d\n",
					    sc->sc_nrecv));
				} else {
					DPRINTF(("data is valid\n"));
					sc->sc_minute = 1;
				}
			}
		} else {
			DPRINTF(("received the first frame\n"));
			sc->sc_nrecv = 1;
		}

		/* record the time received and when it was received */
		sc->sc_last = sc->sc_next;
		sc->sc_last_tv.tv_sec = monotime.tv_sec;
	} else {
		DPRINTF(("\nparity error, resync\n"));
		sc->sc_sync = sc->sc_minute = 1;
	}

cleanbits:
	timeout_add_msec(&sc->sc_to, T_MGSYNC);	/* re-sync in 450 ms */
	sc->sc_last_mg = gettime();
	sc->sc_tbits = 0LL;
	sc->sc_mask = 1LL;
}

/* detect signal loss */
void
udcf_sl_probe(void *xsc)
{
	struct udcf_softc *sc = xsc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTF(("no signal\n"));
	sc->sc_sync = 1;
	timeout_add_msec(&sc->sc_to, T_WAIT);
	timeout_add_msec(&sc->sc_sl_to, T_WAIT + T_SL);
}

/* invalidate timedelta (called in an interrupt context) */
void
udcf_it_intr(void *xsc)
{
	struct udcf_softc *sc = xsc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (sc->sc_sensor.status == SENSOR_S_OK) {
		sc->sc_sensor.status = SENSOR_S_WARN;
		/*
		 * further degrade in 15 minutes if we dont receive any new
		 * time information
		 */
		timeout_add_msec(&sc->sc_it_to, T_CRIT);
	} else {
		sc->sc_sensor.status = SENSOR_S_CRIT;
		sc->sc_nrecv = 0;
	}
}
