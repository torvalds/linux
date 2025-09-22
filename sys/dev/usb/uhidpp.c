/*	$OpenBSD: uhidpp.c,v 1.44 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2021 Anton Lindqvist <anton@openbsd.org>
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
#include <sys/mutex.h>
#include <sys/sensors.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

/* #define UHIDPP_DEBUG */
#ifdef UHIDPP_DEBUG

#define DPRINTF(x...) do {						\
	if (uhidpp_debug)						\
		printf(x);						\
} while (0)

#define DREPORT(prefix, repid, buf, len) do {				\
	if (uhidpp_debug)						\
		uhidd_dump_report((prefix), (repid), (buf), (len));	\
} while (0)

void uhidd_dump_report(const char *, uint8_t, const unsigned char *, u_int);

int uhidpp_debug = 1;

#else

#define DPRINTF(x...)
#define DREPORT(prefix, repid, buf, len)

#endif

#define HIDPP_REPORT_ID_SHORT			0x10
#define HIDPP_REPORT_ID_LONG			0x11

/*
 * Length of reports. Note that the effective length is always +1 as
 * uhidev_set_report() prepends the report ID.
 */
#define HIDPP_REPORT_SHORT_LENGTH		(7 - 1)
#define HIDPP_REPORT_LONG_LENGTH		(20 - 1)

/*
 * Maximum number of allowed parameters for reports. Note, the parameters always
 * starts at offset 3 for both RAP and FAP reports.
 */
#define HIDPP_REPORT_SHORT_PARAMS_MAX		(HIDPP_REPORT_SHORT_LENGTH - 3)
#define HIDPP_REPORT_LONG_PARAMS_MAX		(HIDPP_REPORT_LONG_LENGTH - 3)

#define HIDPP_DEVICE_ID_RECEIVER		0xff

#define HIDPP_FEAT_ROOT_ID			0x00
#define HIDPP_FEAT_ROOT_PING_FUNC		0x01
#define  HIDPP_PING_DATA			0x5a

#define HIDPP_SET_REGISTER			0x80
#define HIDPP_GET_REGISTER			0x81
#define HIDPP_SET_LONG_REGISTER			0x82
#define HIDPP_GET_LONG_REGISTER			0x83

#define HIDPP_REG_ENABLE_REPORTS			0x00
#define  HIDPP_ENABLE_REPORTS_DEVICE_BATTERY_STATUS	0x10
#define  HIDPP_ENABLE_REPORTS_RECEIVER_WIRELESS		0x01
#define  HIDPP_ENABLE_REPORTS_RECEIVER_SOFTWARE_PRESENT	0x08

#define HIDPP_REG_PAIRING_INFORMATION		0xb5

/* HID++ 1.0 error codes. */
#define HIDPP_ERROR				0x8f
#define HIDPP_ERROR_SUCCESS			0x00
#define HIDPP_ERROR_INVALID_SUBID		0x01
#define HIDPP_ERROR_INVALID_ADRESS		0x02
#define HIDPP_ERROR_INVALID_VALUE		0x03
#define HIDPP_ERROR_CONNECT_FAIL		0x04
#define HIDPP_ERROR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERROR_ALREADY_EXISTS		0x06
#define HIDPP_ERROR_BUSY			0x07
#define HIDPP_ERROR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERROR_RESOURCE_ERROR		0x09
#define HIDPP_ERROR_REQUEST_UNAVAILABLE		0x0a
#define HIDPP_ERROR_INVALID_PARAM_VALUE		0x0b
#define HIDPP_ERROR_WRONG_PIN_CODE		0x0c

/*
 * The software ID is added to feature access reports (FAP) and used to
 * distinguish responses from notifications. Note, the software ID must be
 * greater than zero which is reserved for notifications.
 * The effective software ID round robins within its allowed interval [1, 15]
 * making it easier to correlate requests and responses.
 */
#define HIDPP_SOFTWARE_ID_MIN			1
#define HIDPP_SOFTWARE_ID_MAX			15
#define HIDPP_SOFTWARE_ID_LEN			4

#define HIDPP20_FEAT_ROOT_ID			0x0000
#define HIDPP20_FEAT_ROOT_GET_FEATURE_FUNC	0x0000

#define HIDPP20_FEAT_FEATURE_ID			0x0001
#define HIDPP20_FEAT_FEATURE_COUNT_FUNC		0x0000
#define HIDPP20_FEAT_FEATURE_ID_FUNC		0x0001

#define HIDPP20_FEAT_BATTERY_ID			0x1000
#define HIDPP20_FEAT_BATTERY_LEVEL_STATUS_FUNC	0x0000
#define  HIDPP20_LEVEL_STATUS_CHARGING_DONE	0x0003
#define HIDPP20_FEAT_BATTERY_CAPABILITY_FUNC	0x0001
#define  HIDPP20_CAPABILITY_RECHARGEABLE	0x0004

#define HIDPP20_FEAT_UNIFIED_BATTERY_ID			0x1004
#define HIDPP20_FEAT_UNIFIED_BATTERY_CAPABILITIES_FUNC	0x0000
#define  HIDPP20_CAPABILITES_RECHARGEABLE		0x0002
#define HIDPP20_FEAT_UNIFIED_BATTERY_STATUS_FUNC	0x0001
#define  HIDPP20_BATTERY_STATUS_CRITICAL		0x0001
#define  HIDPP20_BATTERY_STATUS_LOW			0x0002
#define  HIDPP20_BATTERY_STATUS_GOOD			0x0004
#define  HIDPP20_BATTERY_STATUS_FULL			0x0008

/* HID++ 2.0 error codes. */
#define HIDPP20_ERROR				0xff
#define HIDPP20_ERROR_NO_ERROR			0x00
#define HIDPP20_ERROR_UNKNOWN			0x01
#define HIDPP20_ERROR_INVALID_ARGUMENT		0x02
#define HIDPP20_ERROR_OUT_OF_RANGE		0x03
#define HIDPP20_ERROR_HARDWARE_ERROR		0x04
#define HIDPP20_ERROR_LOGITECH_INTERNAL		0x05
#define HIDPP20_ERROR_INVALID_FEATURE_INDEX	0x06
#define HIDPP20_ERROR_INVALID_FUNCTION_ID	0x07
#define HIDPP20_ERROR_BUSY			0x08
#define HIDPP20_ERROR_UNSUPPORTED		0x09

/*
 * Sentinels used for interrupt response synchronization. The values must be
 * disjoint from existing report IDs.
 */
#define UHIDPP_RESP_NONE			0
#define UHIDPP_RESP_WAIT			1
#define UHIDPP_RESP_ERROR			2

/* Maximum number of devices associated with a single receiver. */
#define UHIDPP_NDEVICES				6

/* Maximum number of pending notifications. */
#define UHIDPP_NNOTIFICATIONS			4

/* Number of sensors per paired device. */
#define UHIDPP_NSENSORS				3

/* Feature access report used by the HID++ 2.0 (and greater) protocol. */
struct fap {
	uint8_t feature_idx;
	uint8_t funcidx_swid;
	uint8_t params[HIDPP_REPORT_LONG_PARAMS_MAX];
};

/*
 * Register access report used by the HID++ 1.0 protocol. Receivers always uses
 * this type of report.
 */
struct rap {
	uint8_t sub_id;
	uint8_t reg_address;
	uint8_t params[HIDPP_REPORT_LONG_PARAMS_MAX];
};

struct uhidpp_report {
	uint8_t device_id;
	union {
		struct fap fap;
		struct rap rap;
	};
} __packed;

struct uhidpp_notification {
	struct uhidpp_report n_rep;
	unsigned int n_id;
};

struct uhidpp_device {
	uint8_t d_id;
	uint8_t d_connected;
	uint8_t d_major;
	uint8_t d_minor;
	uint8_t d_features;
#define UHIDPP_DEVICE_FEATURE_ROOT		0x01
#define UHIDPP_DEVICE_FEATURE_BATTERY		0x02
#define UHIDPP_DEVICE_FEATURE_UNIFIED_BATTERY	0x04

	struct {
		struct ksensor sens[UHIDPP_NSENSORS];
		uint8_t feature_idx;
		uint8_t nlevels;
		uint8_t unified_level_mask;
		uint8_t rechargeable;
	} d_battery;
};

/*
 * Locking:
 *	[I]	immutable
 *	[m]	sc_mtx
 */
struct uhidpp_softc {
	struct uhidev sc_hdev;
	struct usbd_device *sc_udev;

	struct mutex sc_mtx;

	struct uhidpp_device sc_devices[UHIDPP_NDEVICES];
					/* [m] connected devices */

	struct uhidpp_notification sc_notifications[UHIDPP_NNOTIFICATIONS];
					/* [m] pending notifications */

	struct usb_task sc_task;	/* [m] notification task */

	struct ksensordev sc_sensdev;	/* [m] */
	struct sensor_task *sc_senstsk;	/* [m] */

	struct uhidpp_report *sc_req;	/* [m] synchronous request buffer */
	struct uhidpp_report *sc_resp;	/* [m] synchronous response buffer */
	u_int sc_resp_state;		/* [m] synchronous response state */
	u_int sc_swid;			/* [m] request software id */

	enum {
		UHIDPP_RECEIVER_UNIFYING,
		UHIDPP_RECEIVER_BOLT,
	} sc_receiver;			/* [I] */
};

int uhidpp_match(struct device *, void *, void *);
void uhidpp_attach(struct device *, struct device *, void *);
int uhidpp_detach(struct device *, int flags);
void uhidpp_intr(struct uhidev *addr, void *ibuf, u_int len);
void uhidpp_refresh(void *);
void uhidpp_task(void *);
int uhidpp_sleep(struct uhidpp_softc *, uint64_t);

void uhidpp_device_connect(struct uhidpp_softc *, struct uhidpp_device *);
void uhidpp_device_refresh(struct uhidpp_softc *, struct uhidpp_device *);
int uhidpp_device_features(struct uhidpp_softc *, struct uhidpp_device *);

struct uhidpp_notification *uhidpp_claim_notification(struct uhidpp_softc *);
int uhidpp_consume_notification(struct uhidpp_softc *, struct uhidpp_report *);
int uhidpp_is_notification(struct uhidpp_softc *, struct uhidpp_report *);

static int uhidpp_has_sensors(struct uhidpp_softc *);

int hidpp_get_protocol_version(struct uhidpp_softc *, uint8_t, uint8_t *,
    uint8_t *);

int hidpp10_get_name(struct uhidpp_softc *, uint8_t, char *, size_t);
int hidpp10_get_type(struct uhidpp_softc *, uint8_t, const char **);
int hidpp10_enable_notifications(struct uhidpp_softc *, uint8_t);

int hidpp20_root_get_feature(struct uhidpp_softc *, uint8_t, uint16_t,
    uint8_t *, uint8_t *);
int hidpp20_feature_get_count(struct uhidpp_softc *, uint8_t, uint8_t,
    uint8_t *);
int hidpp20_feature_get_id(struct uhidpp_softc *, uint8_t, uint8_t, uint8_t,
    uint16_t *, uint8_t *);
int hidpp20_battery_get_level_status(struct uhidpp_softc *,
    struct uhidpp_device *);
int hidpp20_battery_get_capability(struct uhidpp_softc *,
    struct uhidpp_device *);
int hidpp20_battery_status_is_charging(uint8_t);
int hidpp20_unified_battery_get_capabilities(struct uhidpp_softc *,
    struct uhidpp_device *);
int hidpp20_unified_battery_get_status(struct uhidpp_softc *,
    struct uhidpp_device *);
int hidpp20_unified_battery_status_is_charging(uint8_t);

int hidpp_send_validate(uint8_t, int);
int hidpp_send_rap_report(struct uhidpp_softc *, uint8_t, uint8_t, uint8_t,
    uint8_t, uint8_t *, int, struct uhidpp_report *);
int hidpp_send_fap_report(struct uhidpp_softc *, uint8_t, uint8_t, uint8_t,
    uint8_t, uint8_t *, int, struct uhidpp_report *);
int hidpp_send_report(struct uhidpp_softc *, uint8_t, struct uhidpp_report *,
    struct uhidpp_report *);

static uint8_t
nlevels(uint8_t mask)
{
	uint8_t nbits = 0;

	for (; mask > 0; mask >>= 1) {
		if (mask & 1)
			nbits++;
	}
	return nbits;
}

struct cfdriver uhidpp_cd = {
	NULL, "uhidpp", DV_DULL
};

const struct cfattach uhidpp_ca = {
	sizeof(struct uhidpp_softc),
	uhidpp_match,
	uhidpp_attach,
	uhidpp_detach,
};

static const struct usb_devno uhidpp_devs[] = {
	{ USB_VENDOR_LOGITECH,	USB_PRODUCT_ANY },
};

int
uhidpp_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	void *desc;
	int descsiz, siz;

	if (!UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return UMATCH_NONE;

	if (usb_lookup(uhidpp_devs,
	    uha->uaa->vendor, uha->uaa->product) == NULL)
		return UMATCH_NONE;

	uhidev_get_report_desc(uha->parent, &desc, &descsiz);
	siz = hid_report_size(desc, descsiz, hid_output, HIDPP_REPORT_ID_SHORT);
	if (siz != HIDPP_REPORT_SHORT_LENGTH)
		return UMATCH_NONE;
	siz = hid_report_size(desc, descsiz, hid_output, HIDPP_REPORT_ID_LONG);
	if (siz != HIDPP_REPORT_LONG_LENGTH)
		return UMATCH_NONE;

	uha->claimed[HIDPP_REPORT_ID_SHORT] = 1;
	uha->claimed[HIDPP_REPORT_ID_LONG] = 1;
	return UMATCH_VENDOR_PRODUCT;
}

void
uhidpp_attach(struct device *parent, struct device *self, void *aux)
{
	struct uhidpp_softc *sc = (struct uhidpp_softc *)self;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	struct usb_attach_arg *uaa = uha->uaa;
	int npaired = 0;
	int error, i;

	sc->sc_hdev.sc_intr = uhidpp_intr;
	sc->sc_hdev.sc_udev = uaa->device;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;
	/* The largest supported report dictates the sizes. */
	sc->sc_hdev.sc_isize = HIDPP_REPORT_LONG_LENGTH;
	sc->sc_hdev.sc_osize = HIDPP_REPORT_LONG_LENGTH;

	sc->sc_udev = uaa->device;

	mtx_init(&sc->sc_mtx, IPL_USB);

	sc->sc_resp = NULL;
	sc->sc_resp_state = UHIDPP_RESP_NONE;
	sc->sc_swid = HIDPP_SOFTWARE_ID_MIN;

	error = uhidev_open(&sc->sc_hdev);
	if (error) {
		printf(" open error %d\n", error);
		return;
	}

	usb_init_task(&sc->sc_task, uhidpp_task, sc, USB_TASK_TYPE_GENERIC);

	mtx_enter(&sc->sc_mtx);

	/*
	 * Wire up report device handlers before issuing commands to the device
	 * in order to receive responses. Necessary as uhidev by default
	 * performs the wiring after the attach routine has returned.
	 */
	error = uhidev_set_report_dev(sc->sc_hdev.sc_parent, &sc->sc_hdev,
	    HIDPP_REPORT_ID_SHORT);
	if (error) {
		mtx_leave(&sc->sc_mtx);
		printf(" short report error %d\n", error);
		return;
	}
	error = uhidev_set_report_dev(sc->sc_hdev.sc_parent, &sc->sc_hdev,
	    HIDPP_REPORT_ID_LONG);
	if (error) {
		mtx_leave(&sc->sc_mtx);
		printf(" long report error %d\n", error);
		return;
	}

	if (uaa->product == 0xc548)
		sc->sc_receiver = UHIDPP_RECEIVER_BOLT;
	else
		sc->sc_receiver = UHIDPP_RECEIVER_UNIFYING;

	/* Probe paired devices. */
	for (i = 0; i < UHIDPP_NDEVICES; i++) {
		char name[16];
		struct uhidpp_device *dev = &sc->sc_devices[i];
		const char *type;
		uint8_t device_id = i + 1;

		dev->d_id = device_id;

		if (hidpp10_get_type(sc, device_id, &type) ||
		    hidpp10_get_name(sc, device_id, name, sizeof(name)))
			continue;

		if (npaired > 0)
			printf(",");
		printf(" device %d", device_id);
		printf(" %s", type);
		printf(" \"%s\"", name);
		npaired++;
	}
	if (npaired == 0)
		goto out;

	/* Enable notifications for the receiver. */
	error = hidpp10_enable_notifications(sc, HIDPP_DEVICE_ID_RECEIVER);
	if (error)
		printf(" error %d", error);

out:
	mtx_leave(&sc->sc_mtx);
	printf("\n");
}

int
uhidpp_detach(struct device *self, int flags)
{
	struct uhidpp_softc *sc = (struct uhidpp_softc *)self;
	int i;

	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	if (uhidpp_has_sensors(sc))
		sensor_task_unregister(sc->sc_senstsk);

	KASSERT(sc->sc_resp_state == UHIDPP_RESP_NONE);

	if (uhidpp_has_sensors(sc))
		sensordev_deinstall(&sc->sc_sensdev);

	for (i = 0; i < UHIDPP_NDEVICES; i++) {
		struct uhidpp_device *dev = &sc->sc_devices[i];
		struct ksensor *sens = dev->d_battery.sens;

		if (!dev->d_connected)
			continue;

		sensor_detach(&sc->sc_sensdev, &sens[0]);
		sensor_detach(&sc->sc_sensdev, &sens[1]);
		if (dev->d_battery.rechargeable)
			sensor_detach(&sc->sc_sensdev, &sens[2]);
	}

	uhidev_close(&sc->sc_hdev);

	return 0;
}

void
uhidpp_intr(struct uhidev *addr, void *buf, u_int len)
{
	struct uhidpp_softc *sc = (struct uhidpp_softc *)addr;
	struct uhidpp_report *rep = buf;
	int dowake = 0;
	uint8_t repid;

	/*
	 * Ugliness ahead as the report ID is stripped of by uhidev_intr() but
	 * needed to determine if an error occurred.
	 * Note that an error response is always a short report even if the
	 * command that caused the error is a long report.
	 */
	repid = ((uint8_t *)buf)[-1];

	DREPORT(__func__, repid, buf, len);

	mtx_enter(&sc->sc_mtx);
	if (uhidpp_is_notification(sc, rep)) {
		struct uhidpp_notification *ntf;

		ntf = uhidpp_claim_notification(sc);
		if (ntf != NULL) {
			memcpy(&ntf->n_rep, buf, len);
			usb_add_task(sc->sc_udev, &sc->sc_task);
		} else {
			DPRINTF("%s: too many notifications", __func__);
		}
	} else {
		KASSERT(sc->sc_resp_state == UHIDPP_RESP_WAIT);
		dowake = 1;
		sc->sc_resp_state = repid;
		memcpy(sc->sc_resp, buf, len);
	}
	mtx_leave(&sc->sc_mtx);
	if (dowake)
		wakeup(sc);
}

void
uhidpp_refresh(void *arg)
{
	struct uhidpp_softc *sc = arg;
	int i;

	mtx_enter(&sc->sc_mtx);
	for (i = 0; i < UHIDPP_NDEVICES; i++) {
		struct uhidpp_device *dev = &sc->sc_devices[i];

		if (dev->d_connected)
			uhidpp_device_refresh(sc, dev);
	}
	mtx_leave(&sc->sc_mtx);
}

void
uhidpp_task(void *arg)
{
	struct uhidpp_softc *sc = arg;

	mtx_enter(&sc->sc_mtx);
	for (;;) {
		struct uhidpp_report rep;
		struct uhidpp_device *dev;

		if (uhidpp_consume_notification(sc, &rep))
			break;

		DPRINTF("%s: device_id=%d, sub_id=%02x\n",
		    __func__, rep.device_id, rep.rap.sub_id);

		if (rep.device_id == 0 || rep.device_id > UHIDPP_NDEVICES) {
			DPRINTF("%s: invalid device\n", __func__);
			continue;
		}
		dev = &sc->sc_devices[rep.device_id - 1];

		switch (rep.rap.sub_id) {
		case 0x0e:	/* leds */
		case 0x40:	/* disconnect */
		case 0x4b:	/* pairing accepted */
			break;
		case 0x41:	/* connect */
			uhidpp_device_connect(sc, dev);
			break;
		}
	}
	mtx_leave(&sc->sc_mtx);
}

int
uhidpp_sleep(struct uhidpp_softc *sc, uint64_t nsecs)
{
	return msleep_nsec(sc, &sc->sc_mtx, PZERO, "uhidpp", nsecs);
}

void
uhidpp_device_connect(struct uhidpp_softc *sc, struct uhidpp_device *dev)
{
	struct ksensor *sens;
	int error;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	/* A connected device will continuously send connect events. */
	if (dev->d_connected)
		return;

	/*
	 * If features are already present, it must be a device lacking battery
	 * support.
	 */
	if (dev->d_features)
		return;

	error = hidpp_get_protocol_version(sc, dev->d_id,
	    &dev->d_major, &dev->d_minor);
	if (error) {
		DPRINTF("%s: protocol version failure: device_id=%d, "
		    "error=%d\n",
		    __func__, dev->d_id, error);
		return;
	}
	DPRINTF("%s: device_id=%d, version=%d.%d\n",
	    __func__, dev->d_id, dev->d_major, dev->d_minor);
	if (dev->d_major <= 1)
		return;

	error = uhidpp_device_features(sc, dev);
	if (error) {
		DPRINTF("%s: features failure: device_id=%d, "
		    "error=%d\n",
		    __func__, dev->d_id, error);
		return;
	}

	if (dev->d_features & UHIDPP_DEVICE_FEATURE_BATTERY)
		error = hidpp20_battery_get_capability(sc, dev);
	else if (dev->d_features & UHIDPP_DEVICE_FEATURE_UNIFIED_BATTERY)
		error = hidpp20_unified_battery_get_capabilities(sc, dev);
	if (error) {
		DPRINTF("%s: battery capability failure: device_id=%d, "
		    "error=%d\n", __func__, dev->d_id, error);
		return;
	}

	dev->d_connected = 1;

	sens = &dev->d_battery.sens[0];
	strlcpy(sens->desc, "battery level", sizeof(sens->desc));
	sens->type = SENSOR_PERCENT;
	sens->flags = SENSOR_FUNKNOWN;
	sensor_attach(&sc->sc_sensdev, sens);

	sens = &dev->d_battery.sens[1];
	strlcpy(sens->desc, "number of battery levels", sizeof(sens->desc));
	sens->type = SENSOR_INTEGER;
	sens->value = dev->d_battery.nlevels;
	sensor_attach(&sc->sc_sensdev, sens);

	if (dev->d_battery.rechargeable) {
		sens = &dev->d_battery.sens[2];
		strlcpy(sens->desc, "charger", sizeof(sens->desc));
		sens->type = SENSOR_INDICATOR;
		sens->value = 0;
		sensor_attach(&sc->sc_sensdev, sens);
	}

	uhidpp_device_refresh(sc, dev);

	/*
	 * There could be many devices connected to the same receiver, therefore
	 * only install the sensors once.
	 */
	if (uhidpp_has_sensors(sc))
		return;

	strlcpy(sc->sc_sensdev.xname, sc->sc_hdev.sc_dev.dv_xname,
	    sizeof(sc->sc_sensdev.xname));
	sensordev_install(&sc->sc_sensdev);

	/*
	 * The mutex must be temporarily released while calling
	 * sensor_task_register() as it might end up sleeping.
	 */
	KASSERT(sc->sc_senstsk == NULL);
	mtx_leave(&sc->sc_mtx);
	sc->sc_senstsk = sensor_task_register(sc, uhidpp_refresh, 60);
	mtx_enter(&sc->sc_mtx);
}

void
uhidpp_device_refresh(struct uhidpp_softc *sc, struct uhidpp_device *dev)
{
	int error;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	if (dev->d_major <= 1)
		return;

	if (dev->d_features & UHIDPP_DEVICE_FEATURE_BATTERY)
		error = hidpp20_battery_get_level_status(sc, dev);
	else if (dev->d_features & UHIDPP_DEVICE_FEATURE_UNIFIED_BATTERY)
		error = hidpp20_unified_battery_get_status(sc, dev);
	else
		error = -ENOTSUP;
	if (error) {
		DPRINTF("%s: battery status failure: device_id=%d, error=%d\n",
		    __func__, dev->d_id, error);
	}
}

/*
 * Enumerate all supported HID++ 2.0 features for the given device.
 */
int
uhidpp_device_features(struct uhidpp_softc *sc, struct uhidpp_device *dev)
{
	int error;
	uint8_t count, feature_idx, feature_type, i;

	/* All devices support the root feature. */
	dev->d_features |= UHIDPP_DEVICE_FEATURE_ROOT;

	error = hidpp20_root_get_feature(sc, dev->d_id,
	    HIDPP20_FEAT_FEATURE_ID,
	    &feature_idx, &feature_type);
	if (error) {
		DPRINTF("%s: feature index failure: device_id=%d, error=%d\n",
		    __func__, dev->d_id, error);
		return error;
	}

	error = hidpp20_feature_get_count(sc, dev->d_id, feature_idx, &count);
	if (error) {
		DPRINTF("%s: feature count failure: device_id=%d, error=%d\n",
		    __func__, dev->d_id, error);
		return error;
	}

	for (i = 1; i <= count; i++) {
		uint16_t id;
		uint8_t type;

		error = hidpp20_feature_get_id(sc, dev->d_id, feature_idx, i,
		    &id, &type);
		if (error)
			continue;

		if (id == HIDPP20_FEAT_BATTERY_ID) {
			dev->d_features |= UHIDPP_DEVICE_FEATURE_BATTERY;
			dev->d_battery.feature_idx = i;
		} else if (id == HIDPP20_FEAT_UNIFIED_BATTERY_ID) {
			dev->d_features |= UHIDPP_DEVICE_FEATURE_UNIFIED_BATTERY;
			dev->d_battery.feature_idx = i;
		}

		DPRINTF("%s: idx=%d, id=%x, type=%x device_id=%d\n",
		    __func__, i, id, type, dev->d_id);
	}
	DPRINTF("%s: device_id=%d, count=%d, features=%x\n",
	    __func__, dev->d_id, count, dev->d_features);

	if ((dev->d_features & ~UHIDPP_DEVICE_FEATURE_ROOT) == 0)
		return -ENODEV;
	return 0;
}

/*
 * Returns the next available notification slot, if available.
 */
struct uhidpp_notification *
uhidpp_claim_notification(struct uhidpp_softc *sc)
{
	struct uhidpp_notification *ntf = NULL;
	int nclaimed = 0;
	int i;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	for (i = 0; i < UHIDPP_NNOTIFICATIONS; i++) {
		struct uhidpp_notification *tmp = &sc->sc_notifications[i];

		if (tmp->n_id > 0)
			nclaimed++;
		else if (ntf == NULL)
			ntf = tmp;
	}

	if (ntf == NULL)
		return NULL;
	ntf->n_id = nclaimed + 1;
	return ntf;
}

/*
 * Consume the first unhandled notification, if present.
 */
int
uhidpp_consume_notification(struct uhidpp_softc *sc, struct uhidpp_report *rep)
{
	struct uhidpp_notification *ntf = NULL;
	int i;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	for (i = 0; i < UHIDPP_NNOTIFICATIONS; i++) {
		struct uhidpp_notification *tmp = &sc->sc_notifications[i];

		if (tmp->n_id > 0 && (ntf == NULL || tmp->n_id < ntf->n_id))
			ntf = tmp;
	}
	if (ntf == NULL)
		return 1;

	memcpy(rep, &ntf->n_rep, sizeof(*rep));
	ntf->n_id = 0;
	return 0;
}

/*
 * Returns non-zero if the given report is a notification. Otherwise, it must be
 * a response.
 */
int
uhidpp_is_notification(struct uhidpp_softc *sc, struct uhidpp_report *rep)
{
	/* Not waiting for a response. */
	if (sc->sc_req == NULL)
		return 1;

	/* Everything except the parameters must be repeated in a response. */
	if (sc->sc_req->device_id == rep->device_id &&
	    sc->sc_req->rap.sub_id == rep->rap.sub_id &&
	    sc->sc_req->rap.reg_address == rep->rap.reg_address)
		return 0;

	/* An error must always be a response. */
	if ((rep->rap.sub_id == HIDPP_ERROR ||
	    rep->fap.feature_idx == HIDPP20_ERROR) &&
	    rep->fap.funcidx_swid == sc->sc_req->fap.feature_idx &&
	    rep->fap.params[0] == sc->sc_req->fap.funcidx_swid)
		return 0;

	return 1;
}

static int
uhidpp_has_sensors(struct uhidpp_softc *sc)
{
	return sc->sc_sensdev.xname[0] != '\0';
}

int
hidpp_get_protocol_version(struct uhidpp_softc *sc, uint8_t device_id,
    uint8_t *major, uint8_t *minor)
{
	struct uhidpp_report resp;
	uint8_t params[3] = { 0, 0, HIDPP_PING_DATA };
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_SHORT,
	    device_id,
	    HIDPP_FEAT_ROOT_ID,
	    HIDPP_FEAT_ROOT_PING_FUNC,
	    params, sizeof(params), &resp);
	if (error == HIDPP_ERROR_INVALID_SUBID) {
		*major = 1;
		*minor = 0;
		return 0;
	}
	if (error)
		return error;
	if (resp.rap.params[2] != HIDPP_PING_DATA)
		return -EPROTO;

	*major = resp.fap.params[0];
	*minor = resp.fap.params[1];
	return 0;
}

int
hidpp10_get_name(struct uhidpp_softc *sc, uint8_t device_id,
    char *buf, size_t bufsiz)
{
	struct uhidpp_report resp;
	int error;
	const uint8_t *name;
	uint8_t len;

	if (sc->sc_receiver == UHIDPP_RECEIVER_BOLT) {
		uint8_t params[2] = { 0x60 + device_id, 0x01 };

		error = hidpp_send_rap_report(sc,
		    HIDPP_REPORT_ID_SHORT,
		    HIDPP_DEVICE_ID_RECEIVER,
		    HIDPP_GET_LONG_REGISTER,
		    HIDPP_REG_PAIRING_INFORMATION,
		    params, sizeof(params), &resp);
		if (error)
			return error;
		len = resp.rap.params[2];
		name = &resp.rap.params[3];
	} else {
		uint8_t params[1] = { 0x40 + (device_id - 1) };

		error = hidpp_send_rap_report(sc,
		    HIDPP_REPORT_ID_SHORT,
		    HIDPP_DEVICE_ID_RECEIVER,
		    HIDPP_GET_LONG_REGISTER,
		    HIDPP_REG_PAIRING_INFORMATION,
		    params, sizeof(params), &resp);
		if (error)
			return error;
		len = resp.rap.params[1];
		name = &resp.rap.params[2];
	}

	if (len > bufsiz - 1)
		len = bufsiz - 1;
	memcpy(buf, name, len);
	buf[len] = '\0';
	return 0;
}

int
hidpp10_get_type(struct uhidpp_softc *sc, uint8_t device_id, const char **buf)
{
	struct uhidpp_report resp;
	int error;
	uint8_t type;

	if (sc->sc_receiver == UHIDPP_RECEIVER_BOLT) {
		uint8_t params[1] = { 0x50 + device_id };

		error = hidpp_send_rap_report(sc,
		    HIDPP_REPORT_ID_SHORT,
		    HIDPP_DEVICE_ID_RECEIVER,
		    HIDPP_GET_LONG_REGISTER,
		    HIDPP_REG_PAIRING_INFORMATION,
		    params, sizeof(params), &resp);
		if (error)
			return error;
		type = resp.rap.params[1] & 0xf;
	} else {
		uint8_t params[1] = { 0x20 + (device_id - 1) };

		error = hidpp_send_rap_report(sc,
		    HIDPP_REPORT_ID_SHORT,
		    HIDPP_DEVICE_ID_RECEIVER,
		    HIDPP_GET_LONG_REGISTER,
		    HIDPP_REG_PAIRING_INFORMATION,
		    params, sizeof(params), &resp);
		if (error)
			return error;
		type = resp.rap.params[7];
	}

	switch (type) {
	case 0x00:
		*buf = "unknown";
		return 0;
	case 0x01:
		*buf = "keyboard";
		return 0;
	case 0x02:
		*buf = "mouse";
		return 0;
	case 0x03:
		*buf = "numpad";
		return 0;
	case 0x04:
		*buf = "presenter";
		return 0;
	case 0x08:
		*buf = "trackball";
		return 0;
	case 0x09:
		*buf = "touchpad";
		return 0;
	}
	return -ENOENT;
}

int
hidpp10_enable_notifications(struct uhidpp_softc *sc, uint8_t device_id)
{
	struct uhidpp_report resp;
	uint8_t params[3];

	/* Device reporting flags. */
	params[0] = HIDPP_ENABLE_REPORTS_DEVICE_BATTERY_STATUS;
	/* Receiver reporting flags. */
	params[1] = HIDPP_ENABLE_REPORTS_RECEIVER_WIRELESS |
	    HIDPP_ENABLE_REPORTS_RECEIVER_SOFTWARE_PRESENT;
	/* Device reporting flags (continued). */
	params[2] = 0;

	return hidpp_send_rap_report(sc,
	    HIDPP_REPORT_ID_SHORT,
	    device_id,
	    HIDPP_SET_REGISTER,
	    HIDPP_REG_ENABLE_REPORTS,
	    params, sizeof(params), &resp);
}

int
hidpp20_root_get_feature(struct uhidpp_softc *sc, uint8_t device_id,
    uint16_t feature, uint8_t *feature_idx, uint8_t *feature_type)
{
	struct uhidpp_report resp;
	uint8_t params[2] = { feature >> 8, feature & 0xff };
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    device_id,
	    HIDPP20_FEAT_ROOT_ID,
	    HIDPP20_FEAT_ROOT_GET_FEATURE_FUNC,
	    params, sizeof(params), &resp);
	if (error)
		return error;

	if (resp.fap.params[0] == 0)
		return -ENOENT;

	*feature_idx = resp.fap.params[0];
	*feature_type = resp.fap.params[1];
	return 0;
}

int
hidpp20_feature_get_count(struct uhidpp_softc *sc, uint8_t device_id,
    uint8_t feature_idx, uint8_t *count)
{
	struct uhidpp_report resp;
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    device_id,
	    feature_idx,
	    HIDPP20_FEAT_FEATURE_COUNT_FUNC,
	    NULL, 0, &resp);
	if (error)
		return error;

	*count = resp.fap.params[0];
	return 0;
}

int
hidpp20_feature_get_id(struct uhidpp_softc *sc, uint8_t device_id,
    uint8_t feature_idx, uint8_t idx, uint16_t *id, uint8_t *type)
{
	struct uhidpp_report resp;
	uint8_t params[1] = { idx };
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    device_id,
	    feature_idx,
	    HIDPP20_FEAT_FEATURE_ID_FUNC,
	    params, sizeof(params), &resp);
	if (error)
		return error;

	*id = bemtoh16(resp.fap.params);
	*type = resp.fap.params[2];
	return 0;
}

int
hidpp20_battery_get_level_status(struct uhidpp_softc *sc,
    struct uhidpp_device *dev)
{
	struct uhidpp_report resp;
	int charging, error;
	uint8_t level, status;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    dev->d_id,
	    dev->d_battery.feature_idx,
	    HIDPP20_FEAT_BATTERY_LEVEL_STATUS_FUNC,
	    NULL, 0, &resp);
	if (error)
		return error;

	level = resp.fap.params[0];
	/* next_level = resp.fap.params[1]; */
	status = resp.fap.params[2];
	/*
	 * While charging, the reported level cannot be trusted. However, fake
	 * the battery state once the charging is done.
	 */
	if (status == HIDPP20_LEVEL_STATUS_CHARGING_DONE) {
		level = 100;
		status = 0;
	}

	charging = hidpp20_battery_status_is_charging(status);

	dev->d_battery.sens[0].value = level * 1000;
	dev->d_battery.sens[0].flags &= ~SENSOR_FUNKNOWN;
	if (dev->d_battery.nlevels < 10) {
		/*
		 * According to the HID++ 2.0 specification, less than
		 * 10 levels should be mapped to the following 4 levels:
		 *
		 * [0, 10]   critical
		 * [11, 30]  low
		 * [31, 80]  good
		 * [81, 100] full
		 *
		 * Since sensors are limited to 3 valid statuses, clamp
		 * it even further. Unless the battery is charging in
		 * which the level cannot be trusted.
		 */
		if (charging)
			dev->d_battery.sens[0].status = SENSOR_S_UNKNOWN;
		else if (level <= 10)
			dev->d_battery.sens[0].status = SENSOR_S_CRIT;
		else if (level <= 30)
			dev->d_battery.sens[0].status = SENSOR_S_WARN;
		else
			dev->d_battery.sens[0].status = SENSOR_S_OK;
	} else {
		/*
		 * XXX the device supports battery mileage. The current
		 * level must be checked against resp.fap.params[3]
		 * given by hidpp20_battery_get_capability().
		 */
		dev->d_battery.sens[0].status = SENSOR_S_UNKNOWN;
	}

	if (dev->d_battery.rechargeable)
		dev->d_battery.sens[2].value = charging;

	return 0;
}

int
hidpp20_battery_get_capability(struct uhidpp_softc *sc,
    struct uhidpp_device *dev)
{
	struct uhidpp_report resp;
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    dev->d_id,
	    dev->d_battery.feature_idx,
	    HIDPP20_FEAT_BATTERY_CAPABILITY_FUNC,
	    NULL, 0, &resp);
	if (error)
		return error;
	dev->d_battery.nlevels = resp.fap.params[0];
	dev->d_battery.rechargeable = resp.fap.params[1] &
	    HIDPP20_CAPABILITY_RECHARGEABLE;
	return 0;
}

int
hidpp20_unified_battery_get_capabilities(struct uhidpp_softc *sc,
    struct uhidpp_device *dev)
{
	struct uhidpp_report resp;
	int error;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    dev->d_id,
	    dev->d_battery.feature_idx,
	    HIDPP20_FEAT_UNIFIED_BATTERY_CAPABILITIES_FUNC,
	    NULL, 0, &resp);
	if (error)
		return error;
	dev->d_battery.nlevels = nlevels(resp.fap.params[0]);
	dev->d_battery.unified_level_mask = resp.fap.params[0];
	dev->d_battery.rechargeable = resp.fap.params[1] &
	    HIDPP20_CAPABILITES_RECHARGEABLE;
	return 0;
}

int
hidpp20_unified_battery_get_status(struct uhidpp_softc *sc,
    struct uhidpp_device *dev)
{
	struct uhidpp_report resp;
	int charging, error;
	uint8_t level, percentage, status;

	error = hidpp_send_fap_report(sc,
	    HIDPP_REPORT_ID_LONG,
	    dev->d_id,
	    dev->d_battery.feature_idx,
	    HIDPP20_FEAT_UNIFIED_BATTERY_STATUS_FUNC,
	    NULL, 0, &resp);
	if (error)
		return error;
	percentage = resp.fap.params[0];
	level = resp.fap.params[1] & dev->d_battery.unified_level_mask;
	status = resp.fap.params[2];
	/* external_power_status = resp.fap.params[3]; */

	charging = hidpp20_unified_battery_status_is_charging(status);
	dev->d_battery.sens[0].value = percentage * 1000;
	dev->d_battery.sens[0].flags &= ~SENSOR_FUNKNOWN;
	dev->d_battery.sens[0].status = SENSOR_S_UNKNOWN;
	/* Do not trust the level while charging. */
	if (!charging) {
		if (level & HIDPP20_BATTERY_STATUS_CRITICAL)
			dev->d_battery.sens[0].status = SENSOR_S_CRIT;
		else if (level & HIDPP20_BATTERY_STATUS_LOW)
			dev->d_battery.sens[0].status = SENSOR_S_WARN;
		else if (level & HIDPP20_BATTERY_STATUS_GOOD)
			dev->d_battery.sens[0].status = SENSOR_S_OK;
		else if (level & HIDPP20_BATTERY_STATUS_FULL)
			dev->d_battery.sens[0].status = SENSOR_S_OK;
	}
	if (dev->d_battery.rechargeable)
		dev->d_battery.sens[2].value = charging;
	return 0;
}

int
hidpp20_unified_battery_status_is_charging(uint8_t status)
{
	switch (status) {
	case 1: /* charging */
	case 2: /* charging slow */
		return 1;
	default:
		return 0;
	}
}

int
hidpp20_battery_status_is_charging(uint8_t status)
{
	switch (status) {
	case 1:	/* recharging */
	case 2:	/* charge in final stage */
	case 4:	/* recharging below optimal speed */
		return 1;

	case 3:	/* charge complete */
		return 1;

	case 0:	/* discharging */
	case 5:	/* invalid battery type */
	case 6:	/* thermal error */
	case 7:	/* other charging error */
	default:
		return 0;
	}
}

int
hidpp_send_validate(uint8_t report_id, int nparams)
{
	if (report_id == HIDPP_REPORT_ID_SHORT) {
		if (nparams > HIDPP_REPORT_SHORT_PARAMS_MAX)
			return -EMSGSIZE;
	} else if (report_id == HIDPP_REPORT_ID_LONG) {
		if (nparams > HIDPP_REPORT_LONG_PARAMS_MAX)
			return -EMSGSIZE;
	} else {
		return -EINVAL;
	}
	return 0;
}

int
hidpp_send_fap_report(struct uhidpp_softc *sc, uint8_t report_id,
    uint8_t device_id, uint8_t feature_idx, uint8_t func_idx,
    uint8_t *params, int nparams, struct uhidpp_report *resp)
{
	struct uhidpp_report req;
	int error;

	error = hidpp_send_validate(report_id, nparams);
	if (error)
		return error;

	memset(&req, 0, sizeof(req));
	req.device_id = device_id;
	req.fap.feature_idx = feature_idx;
	sc->sc_swid = sc->sc_swid == HIDPP_SOFTWARE_ID_MAX ?
	    HIDPP_SOFTWARE_ID_MIN : sc->sc_swid + 1;
	req.fap.funcidx_swid =
	    (func_idx << HIDPP_SOFTWARE_ID_LEN) | sc->sc_swid;
	memcpy(req.fap.params, params, nparams);
	return hidpp_send_report(sc, report_id, &req, resp);
}

int
hidpp_send_rap_report(struct uhidpp_softc *sc, uint8_t report_id,
    uint8_t device_id, uint8_t sub_id, uint8_t reg_address,
    uint8_t *params, int nparams, struct uhidpp_report *resp)
{
	struct uhidpp_report req;
	int error;

	error = hidpp_send_validate(report_id, nparams);
	if (error)
		return error;

	memset(&req, 0, sizeof(req));
	req.device_id = device_id;
	req.rap.sub_id = sub_id;
	req.rap.reg_address = reg_address;
	memcpy(req.rap.params, params, nparams);
	return hidpp_send_report(sc, report_id, &req, resp);
}

int
hidpp_send_report(struct uhidpp_softc *sc, uint8_t report_id,
    struct uhidpp_report *req, struct uhidpp_report *resp)
{
	int error = 0;
	int len, n;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	if (report_id == HIDPP_REPORT_ID_SHORT)
		len = HIDPP_REPORT_SHORT_LENGTH;
	else if (report_id == HIDPP_REPORT_ID_LONG)
		len = HIDPP_REPORT_LONG_LENGTH;
	else
		return -EINVAL;

	DREPORT(__func__, report_id, (const unsigned char *)req, len);

	/* Wait until any ongoing command has completed. */
	while (sc->sc_resp_state != UHIDPP_RESP_NONE)
		uhidpp_sleep(sc, INFSLP);
	sc->sc_req = req;
	sc->sc_resp = resp;
	sc->sc_resp_state = UHIDPP_RESP_WAIT;
	/*
	 * The mutex must be temporarily released while calling
	 * uhidev_set_report() as it might end up sleeping.
	 */
	mtx_leave(&sc->sc_mtx);

	n = uhidev_set_report(sc->sc_hdev.sc_parent, UHID_OUTPUT_REPORT,
	    report_id, req, len);

	mtx_enter(&sc->sc_mtx);
	if (len != n) {
		error = -EBUSY;
		goto out;
	}
	/*
	 * The interrupt could already have been received while the mutex was
	 * released. Otherwise, wait for it.
	 */
	if (sc->sc_resp_state == UHIDPP_RESP_WAIT) {
		/* Timeout taken from the hid-logitech-hidpp Linux driver. */
		error = uhidpp_sleep(sc, SEC_TO_NSEC(5));
		if (error) {
			error = -error;
			goto out;
		}
	}

	if (sc->sc_resp_state == UHIDPP_RESP_ERROR)
		error = -EIO;
	else if (sc->sc_resp_state == HIDPP_REPORT_ID_SHORT &&
	    resp->rap.sub_id == HIDPP_ERROR)
		error = resp->rap.params[1];
	else if (sc->sc_resp_state == HIDPP_REPORT_ID_LONG &&
	    resp->fap.feature_idx == HIDPP20_ERROR)
		error = resp->fap.params[1];

out:
	sc->sc_req = NULL;
	sc->sc_resp = NULL;
	sc->sc_resp_state = UHIDPP_RESP_NONE;
	wakeup(sc);
	return error;
}

#ifdef UHIDPP_DEBUG

void
uhidd_dump_report(const char *prefix, uint8_t repid, const unsigned char *buf,
    u_int buflen)
{
	u_int i;

	printf("%s: %02x ", prefix, repid);
	for (i = 0; i < buflen; i++) {
		printf("%02x%s", buf[i],
		    i == 2 ? " [" : (i + 1 < buflen ? " " : ""));
	}
	printf("]\n");
}

#endif
