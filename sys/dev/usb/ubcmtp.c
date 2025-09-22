/*	$OpenBSD: ubcmtp.c,v 1.26 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2013-2014, joshua stein <jcs@openbsd.org>
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

/*
 * Apple USB multitouch trackpad (Broadcom BCM5974) driver
 *
 * Protocol info/magic from bcm5974 Linux driver by Henrik Rydberg, et al.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>

#include <sys/systm.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbhid.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

/* #define UBCMTP_DEBUG */

#ifdef UBCMTP_DEBUG
#define DPRINTF(x...)	do { printf(x); } while (0);
#else
#define DPRINTF(x...)
#endif

/* magic to switch device from HID (default) mode into raw */
#define UBCMTP_WELLSPRING_MODE_RAW	0x01
#define UBCMTP_WELLSPRING_MODE_HID	0x08
#define UBCMTP_WELLSPRING_MODE_LEN	8
#define UBCMTP_WELLSPRING9_MODE_RAW	0x01
#define UBCMTP_WELLSPRING9_MODE_HID	0x00
#define UBCMTP_WELLSPRING9_MODE_LEN	2

struct ubcmtp_button {
	uint8_t		unused;
	uint8_t		button;
	uint8_t		rel_x;
	uint8_t		rel_y;
};

struct ubcmtp_finger {
	uint16_t	origin;
	uint16_t	abs_x;
	uint16_t	abs_y;
	uint16_t	rel_x;
	uint16_t	rel_y;
	uint16_t	tool_major;
	uint16_t	tool_minor;
	uint16_t	orientation;
	uint16_t	touch_major;
	uint16_t	touch_minor;
	uint16_t	unused[2];
	uint16_t	pressure;
	uint16_t	multi;
} __packed __attribute((aligned(2)));

#define UBCMTP_MAX_FINGERS	16
#define UBCMTP_ALL_FINGER_SIZE	(UBCMTP_MAX_FINGERS * sizeof(struct ubcmtp_finger))

#define UBCMTP_TYPE1		1
#define UBCMTP_TYPE1_TPOFF	(13 * sizeof(uint16_t))
#define UBCMTP_TYPE1_TPLEN	UBCMTP_TYPE1_TPOFF + UBCMTP_ALL_FINGER_SIZE
#define UBCMTP_TYPE1_TPIFACE	1
#define UBCMTP_TYPE1_BTIFACE	2

#define UBCMTP_TYPE2		2
#define UBCMTP_TYPE2_TPOFF	(15 * sizeof(uint16_t))
#define UBCMTP_TYPE2_TPLEN	UBCMTP_TYPE2_TPOFF + UBCMTP_ALL_FINGER_SIZE
#define UBCMTP_TYPE2_TPIFACE	1
#define UBCMTP_TYPE2_BTOFF	15

#define UBCMTP_TYPE3		3
#define UBCMTP_TYPE3_TPOFF	(19 * sizeof(uint16_t))
#define UBCMTP_TYPE3_TPLEN	UBCMTP_TYPE3_TPOFF + UBCMTP_ALL_FINGER_SIZE
#define UBCMTP_TYPE3_TPIFACE	2
#define UBCMTP_TYPE3_BTOFF	23

#define UBCMTP_TYPE4		4
#define UBCMTP_TYPE4_TPOFF	(24 * sizeof(uint16_t))
#define UBCMTP_TYPE4_TPLEN	UBCMTP_TYPE4_TPOFF + UBCMTP_ALL_FINGER_SIZE
#define UBCMTP_TYPE4_TPIFACE	2
#define UBCMTP_TYPE4_BTOFF	31
#define UBCMTP_TYPE4_FINGERPAD	(1 * sizeof(uint16_t))

#define UBCMTP_FINGER_ORIENT	16384
#define UBCMTP_SN_PRESSURE	45
#define UBCMTP_SN_WIDTH		25
#define UBCMTP_SN_COORD		250
#define UBCMTP_SN_ORIENT	10

/* Identify clickpads in ubcmtp_configure. */
#define IS_CLICKPAD(ubcmtp_type) (ubcmtp_type != UBCMTP_TYPE1)

/* Use a constant, synaptics-compatible pressure value for now. */
#define DEFAULT_PRESSURE	40

struct ubcmtp_limit {
	int limit;
	int min;
	int max;
};

struct ubcmtp_dev {
	int vendor;			/* vendor */
	int ansi, iso, jis;		/* 3 types of product */
	int type;			/* 1 (normal) or 2 (integrated btn) */
	struct ubcmtp_limit l_pressure;	/* finger pressure */
	struct ubcmtp_limit l_width;	/* finger width */
	struct ubcmtp_limit l_x;
	struct ubcmtp_limit l_y;
	struct ubcmtp_limit l_orientation;
};

static const struct ubcmtp_dev ubcmtp_devices[] = {
	/* type 1 devices with separate buttons */
	{
		USB_VENDOR_APPLE,
		/* MacbookAir */
		USB_PRODUCT_APPLE_WELLSPRING_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING_ISO,
		USB_PRODUCT_APPLE_WELLSPRING_JIS,
		UBCMTP_TYPE1,
		{ UBCMTP_SN_PRESSURE, 0, 256 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4824, 5342 },
		{ UBCMTP_SN_COORD, -172, 5820 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookProPenryn */
		USB_PRODUCT_APPLE_WELLSPRING2_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING2_ISO,
		USB_PRODUCT_APPLE_WELLSPRING2_JIS,
		UBCMTP_TYPE1,
		{ UBCMTP_SN_PRESSURE, 0, 256 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4824, 4824 },
		{ UBCMTP_SN_COORD, -172, 4290 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	/* type 2 devices with integrated buttons */
	{
		USB_VENDOR_APPLE,
		/* Macbook5,1 */
		USB_PRODUCT_APPLE_WELLSPRING3_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING3_ISO,
		USB_PRODUCT_APPLE_WELLSPRING3_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4460, 5166 },
		{ UBCMTP_SN_COORD, -75, 6700 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookAir3,1 */
		USB_PRODUCT_APPLE_WELLSPRING4A_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING4A_ISO,
		USB_PRODUCT_APPLE_WELLSPRING4A_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4616, 5112 },
		{ UBCMTP_SN_COORD, -142, 5234 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookAir3,2 */
		USB_PRODUCT_APPLE_WELLSPRING4_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING4_ISO,
		USB_PRODUCT_APPLE_WELLSPRING4_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4620, 5140 },
		{ UBCMTP_SN_COORD, -150, 6600 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* Macbook8 */
		USB_PRODUCT_APPLE_WELLSPRING5_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING5_ISO,
		USB_PRODUCT_APPLE_WELLSPRING5_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4415, 5050 },
		{ UBCMTP_SN_COORD, -55, 6680 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* Macbook8,2 */
		USB_PRODUCT_APPLE_WELLSPRING5A_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING5A_ISO,
		USB_PRODUCT_APPLE_WELLSPRING5A_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4750, 5280 },
		{ UBCMTP_SN_COORD, -150, 6730 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookAir4,2 */
		USB_PRODUCT_APPLE_WELLSPRING6_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING6_ISO,
		USB_PRODUCT_APPLE_WELLSPRING6_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4620, 5140 },
		{ UBCMTP_SN_COORD, -150, 6600 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookAir4,1 */
		USB_PRODUCT_APPLE_WELLSPRING6A_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING6A_ISO,
		USB_PRODUCT_APPLE_WELLSPRING6A_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4620, 5140 },
		{ UBCMTP_SN_COORD, -150, 6600 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookPro10,1 */
		USB_PRODUCT_APPLE_WELLSPRING7_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING7_ISO,
		USB_PRODUCT_APPLE_WELLSPRING7_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4750, 5280 },
		{ UBCMTP_SN_COORD, -150, 6730 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookPro10,2 */
		USB_PRODUCT_APPLE_WELLSPRING7A_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING7A_ISO,
		USB_PRODUCT_APPLE_WELLSPRING7A_JIS,
		UBCMTP_TYPE2,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4750, 5280 },
		{ UBCMTP_SN_COORD, -150, 6730 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookAir6,1 */
		USB_PRODUCT_APPLE_WELLSPRING8_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING8_ISO,
		USB_PRODUCT_APPLE_WELLSPRING8_JIS,
		UBCMTP_TYPE3,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4620, 5140 },
		{ UBCMTP_SN_COORD, -150, 6600 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
	{
		USB_VENDOR_APPLE,
		/* MacbookPro12,1 */
		USB_PRODUCT_APPLE_WELLSPRING9_ANSI,
		USB_PRODUCT_APPLE_WELLSPRING9_ISO,
		USB_PRODUCT_APPLE_WELLSPRING9_JIS,
		UBCMTP_TYPE4,
		{ UBCMTP_SN_PRESSURE, 0, 300 },
		{ UBCMTP_SN_WIDTH, 0, 2048 },
		{ UBCMTP_SN_COORD, -4828, 5345 },
		{ UBCMTP_SN_COORD, -203, 6803 },
		{ UBCMTP_SN_ORIENT, -UBCMTP_FINGER_ORIENT, UBCMTP_FINGER_ORIENT },
	},
};

static struct wsmouse_param ubcmtp_wsmousecfg[] = {
	{ WSMOUSECFG_MTBTN_MAXDIST, 0 }, /* 0: Compute a default value. */
};

struct ubcmtp_softc {
	struct device		sc_dev;		/* base device */

	const struct ubcmtp_dev	*dev_type;

	struct usbd_device	*sc_udev;
	struct device		*sc_wsmousedev;

	struct usbd_interface	*sc_tp_iface;	/* trackpad interface */
	struct usbd_pipe	*sc_tp_pipe;	/* trackpad pipe */
	int			sc_tp_epaddr;	/* endpoint addr */
	int			tp_maxlen;	/* max size of tp data */
	int			tp_offset;	/* finger offset into data */
	int			tp_fingerpad;	/* padding between finger data */
	uint8_t			*tp_pkt;

	struct usbd_interface	*sc_bt_iface;	/* button interface */
	struct usbd_pipe	*sc_bt_pipe;	/* button pipe */
	int			sc_bt_epaddr;	/* endpoint addr */
	int			bt_maxlen;	/* max size of button data */
	uint8_t			*bt_pkt;

	uint32_t		sc_status;
#define UBCMTP_ENABLED		1

	struct mtpoint		frame[UBCMTP_MAX_FINGERS];
	int			contacts;
	int			btn;
};

int	ubcmtp_enable(void *);
void	ubcmtp_disable(void *);
int	ubcmtp_ioctl(void *, unsigned long, caddr_t, int, struct proc *);
int	ubcmtp_raw_mode(struct ubcmtp_softc *, int);
int	ubcmtp_setup_pipes(struct ubcmtp_softc *);
void	ubcmtp_bt_intr(struct usbd_xfer *, void *, usbd_status);
void	ubcmtp_tp_intr(struct usbd_xfer *, void *, usbd_status);

int	ubcmtp_match(struct device *, void *, void *);
void	ubcmtp_attach(struct device *, struct device *, void *);
int	ubcmtp_detach(struct device *, int);
int	ubcmtp_activate(struct device *, int);
int	ubcmtp_configure(struct ubcmtp_softc *);

const struct wsmouse_accessops ubcmtp_accessops = {
	ubcmtp_enable,
	ubcmtp_ioctl,
	ubcmtp_disable,
};

struct cfdriver ubcmtp_cd = {
	NULL, "ubcmtp", DV_DULL
};

const struct cfattach ubcmtp_ca = {
	sizeof(struct ubcmtp_softc), ubcmtp_match, ubcmtp_attach, ubcmtp_detach,
	ubcmtp_activate,
};

int
ubcmtp_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	int i;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	for (i = 0; i < nitems(ubcmtp_devices); i++) {
		if (uaa->vendor == ubcmtp_devices[i].vendor && (
		    uaa->product == ubcmtp_devices[i].ansi ||
		    uaa->product == ubcmtp_devices[i].iso ||
		    uaa->product == ubcmtp_devices[i].jis)) {
			if (uaa->nifaces < 2)
				return (UMATCH_NONE);
			if ((ubcmtp_devices[i].type != UBCMTP_TYPE2) &&
			    (uaa->nifaces < 3))
				return (UMATCH_NONE);

			/*
			 * The USB keyboard/mouse device will have one keyboard
			 * HID and two mouse HIDs, though only one will have a
			 * protocol of mouse -- we only want to take control of
			 * that one.
			 */
			id = usbd_get_interface_descriptor(uaa->iface);
			if (id->bInterfaceProtocol == UIPROTO_BOOT_MOUSE)
				return (UMATCH_VENDOR_PRODUCT_CONF_IFACE);
		}
	}

	return (UMATCH_NONE);
}

void
ubcmtp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ubcmtp_softc *sc = (struct ubcmtp_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct wsmousedev_attach_args a;
	usb_device_descriptor_t *udd;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_status = 0;
	sc->tp_fingerpad = 0;

	if ((udd = usbd_get_device_descriptor(dev)) == NULL) {
		printf("ubcmtp: failed getting device descriptor\n");
		return;
	}

	for (i = 0; i < nitems(ubcmtp_devices); i++) {
		if (uaa->vendor == ubcmtp_devices[i].vendor && (
		    uaa->product == ubcmtp_devices[i].ansi ||
		    uaa->product == ubcmtp_devices[i].iso ||
		    uaa->product == ubcmtp_devices[i].jis)) {
			sc->dev_type = &ubcmtp_devices[i];
			DPRINTF("%s: attached to 0x%x/0x%x type %d\n",
			    sc->sc_dev.dv_xname, uaa->vendor, uaa->product,
			    sc->dev_type->type);
			break;
		}
	}

	if (sc->dev_type == NULL) {
		/* how did we match then? */
		printf("%s: failed looking up device in table\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	switch (sc->dev_type->type) {
	case UBCMTP_TYPE1:
		sc->tp_maxlen = UBCMTP_TYPE1_TPLEN;
		sc->tp_offset = UBCMTP_TYPE1_TPOFF;
		sc->sc_tp_iface = uaa->ifaces[UBCMTP_TYPE1_TPIFACE];
		usbd_claim_iface(sc->sc_udev, UBCMTP_TYPE1_TPIFACE);

		/* button offsets */
		sc->bt_maxlen = sizeof(struct ubcmtp_button);
		sc->sc_bt_iface = uaa->ifaces[UBCMTP_TYPE1_BTIFACE];
		usbd_claim_iface(sc->sc_udev, UBCMTP_TYPE1_BTIFACE);
		break;

	case UBCMTP_TYPE2:
		sc->tp_maxlen = UBCMTP_TYPE2_TPLEN;
		sc->tp_offset = UBCMTP_TYPE2_TPOFF;
		sc->sc_tp_iface = uaa->ifaces[UBCMTP_TYPE2_TPIFACE];
		usbd_claim_iface(sc->sc_udev, UBCMTP_TYPE2_TPIFACE);
		break;

	case UBCMTP_TYPE3:
		sc->tp_maxlen = UBCMTP_TYPE3_TPLEN;
		sc->tp_offset = UBCMTP_TYPE3_TPOFF;
		sc->sc_tp_iface = uaa->ifaces[UBCMTP_TYPE3_TPIFACE];
		usbd_claim_iface(sc->sc_udev, UBCMTP_TYPE3_TPIFACE);
		break;

	case UBCMTP_TYPE4:
		sc->tp_maxlen = UBCMTP_TYPE4_TPLEN;
		sc->tp_offset = UBCMTP_TYPE4_TPOFF;
		sc->sc_tp_iface = uaa->ifaces[UBCMTP_TYPE4_TPIFACE];
		sc->tp_fingerpad = UBCMTP_TYPE4_FINGERPAD;
		usbd_claim_iface(sc->sc_udev, UBCMTP_TYPE4_TPIFACE);
		break;
	}

	a.accessops = &ubcmtp_accessops;
	a.accesscookie = sc;

	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);
	if (sc->sc_wsmousedev != NULL && ubcmtp_configure(sc))
		ubcmtp_disable(sc);
}

int
ubcmtp_detach(struct device *self, int flags)
{
	struct ubcmtp_softc *sc = (struct ubcmtp_softc *)self;
	int ret = 0;

	if (sc->sc_wsmousedev != NULL)
		ret = config_detach(sc->sc_wsmousedev, flags);

	return (ret);
}

int
ubcmtp_activate(struct device *self, int act)
{
	struct ubcmtp_softc *sc = (struct ubcmtp_softc *)self;
	int rv = 0;

	if (act == DVACT_DEACTIVATE) {
		if (sc->sc_wsmousedev != NULL)
			rv = config_deactivate(sc->sc_wsmousedev);
		usbd_deactivate(sc->sc_udev);
	}

	return (rv);
}

int
ubcmtp_configure(struct ubcmtp_softc *sc)
{
	struct wsmousehw *hw = wsmouse_get_hw(sc->sc_wsmousedev);

	hw->type = WSMOUSE_TYPE_TOUCHPAD;
	hw->hw_type = (IS_CLICKPAD(sc->dev_type->type)
	    ? WSMOUSEHW_CLICKPAD : WSMOUSEHW_TOUCHPAD);
	hw->x_min = sc->dev_type->l_x.min;
	hw->x_max = sc->dev_type->l_x.max;
	hw->y_min = sc->dev_type->l_y.min;
	hw->y_max = sc->dev_type->l_y.max;
	hw->mt_slots = UBCMTP_MAX_FINGERS;
	hw->flags = WSMOUSEHW_MT_TRACKING;

	return wsmouse_configure(sc->sc_wsmousedev,
	    ubcmtp_wsmousecfg, nitems(ubcmtp_wsmousecfg));
}

int
ubcmtp_enable(void *v)
{
	struct ubcmtp_softc *sc = v;

	if (sc->sc_status & UBCMTP_ENABLED)
		return (EBUSY);

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	if (ubcmtp_raw_mode(sc, 1) != 0) {
		printf("%s: failed to enter raw mode\n", sc->sc_dev.dv_xname);
		return (1);
	}

	if (ubcmtp_setup_pipes(sc) == 0) {
		sc->sc_status |= UBCMTP_ENABLED;
		return (0);
	} else
		return (1);
}

void
ubcmtp_disable(void *v)
{
	struct ubcmtp_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev) || !(sc->sc_status & UBCMTP_ENABLED))
		return;

	sc->sc_status &= ~UBCMTP_ENABLED;

	ubcmtp_raw_mode(sc, 0);

	if (sc->sc_tp_pipe != NULL) {
		usbd_close_pipe(sc->sc_tp_pipe);
		sc->sc_tp_pipe = NULL;
	}
	if (sc->sc_bt_pipe != NULL) {
		usbd_close_pipe(sc->sc_bt_pipe);
		sc->sc_bt_pipe = NULL;
	}

	if (sc->tp_pkt != NULL) {
		free(sc->tp_pkt, M_USBDEV, sc->tp_maxlen);
		sc->tp_pkt = NULL;
	}
	if (sc->bt_pkt != NULL) {
		free(sc->bt_pkt, M_USBDEV, sc->bt_maxlen);
		sc->bt_pkt = NULL;
	}
}

int
ubcmtp_ioctl(void *v, unsigned long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ubcmtp_softc *sc = v;
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	int wsmode;

	DPRINTF("%s: in %s with cmd 0x%lx\n", sc->sc_dev.dv_xname, __func__,
	    cmd);

	switch (cmd) {
	case WSMOUSEIO_GTYPE: {
		struct wsmousehw *hw = wsmouse_get_hw(sc->sc_wsmousedev);
		*(u_int *)data = hw->type;
		break;
	}

	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = sc->dev_type->l_x.min;
		wsmc->maxx = sc->dev_type->l_x.max;
		wsmc->miny = sc->dev_type->l_y.min;
		wsmc->maxy = sc->dev_type->l_y.max;
		wsmc->swapxy = 0;
		wsmc->resx = 0;
		wsmc->resy = 0;
		break;

	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE) {
			printf("%s: invalid mode %d\n", sc->sc_dev.dv_xname,
			    wsmode);
			return (EINVAL);
		}
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);

		DPRINTF("%s: changing mode to %s\n",
		    sc->sc_dev.dv_xname, (wsmode == WSMOUSE_COMPAT ? "compat" :
		    "native"));

		break;

	default:
		return (-1);
	}

	return (0);
}

int
ubcmtp_raw_mode(struct ubcmtp_softc *sc, int enable)
{
	usb_device_request_t r;
	usbd_status err;
	uint8_t buf[8];

	/* type 3 has no raw mode */
	if (sc->dev_type->type == UBCMTP_TYPE3)
		return (0);

	r.bRequest = UR_GET_REPORT;
	r.bmRequestType = UT_READ_CLASS_INTERFACE;
	if (sc->dev_type->type < UBCMTP_TYPE4) {
		USETW2(r.wValue, UHID_FEATURE_REPORT, 0);
		USETW(r.wIndex, 0);
		USETW(r.wLength, UBCMTP_WELLSPRING_MODE_LEN);
	} else {
		USETW2(r.wValue, UHID_FEATURE_REPORT, 2);
		USETW(r.wIndex, 2);
		USETW(r.wLength, UBCMTP_WELLSPRING9_MODE_LEN);
	}

	err = usbd_do_request(sc->sc_udev, &r, buf);
	if (err != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: failed to get feature report\n",
		    sc->sc_dev.dv_xname, __func__);
		return (err);
	}

	/* toggle magic byte and write everything back */
	if (sc->dev_type->type < UBCMTP_TYPE4)
		buf[0] = (enable ? UBCMTP_WELLSPRING_MODE_RAW :
		    UBCMTP_WELLSPRING_MODE_HID);
	else
		buf[1] = (enable ? UBCMTP_WELLSPRING9_MODE_RAW :
		    UBCMTP_WELLSPRING9_MODE_HID);

	r.bRequest = UR_SET_REPORT;
	r.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	if (sc->dev_type->type < UBCMTP_TYPE4) {
		USETW2(r.wValue, UHID_FEATURE_REPORT, 0);
		USETW(r.wIndex, 0);
		USETW(r.wLength, UBCMTP_WELLSPRING_MODE_LEN);
	} else {
		USETW2(r.wValue, UHID_FEATURE_REPORT, 2);
		USETW(r.wIndex, 2);
		USETW(r.wLength, UBCMTP_WELLSPRING9_MODE_LEN);
	}

	err = usbd_do_request(sc->sc_udev, &r, buf);
	if (err != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: failed to toggle raw mode\n",
		    sc->sc_dev.dv_xname, __func__);
		return (err);
	}

	return (0);
}

int
ubcmtp_setup_pipes(struct ubcmtp_softc *sc)
{
	usbd_status err;
	usb_endpoint_descriptor_t *ed;

	if (sc->dev_type->type == UBCMTP_TYPE1) {
		/* setup physical button pipe */

		ed = usbd_interface2endpoint_descriptor(sc->sc_bt_iface, 0);
		if (ed == NULL) {
			printf("%s: failed getting button endpoint descriptor\n",
			    sc->sc_dev.dv_xname);
			goto fail1;
		}
		sc->sc_bt_epaddr = ed->bEndpointAddress;
		sc->bt_pkt = malloc(sc->bt_maxlen, M_USBDEV, M_WAITOK);
		if (sc->bt_pkt == NULL)
			goto fail1;

		DPRINTF("%s: button iface at 0x%x, max size %d\n",
		    sc->sc_dev.dv_xname, sc->sc_bt_epaddr, sc->bt_maxlen);

		err = usbd_open_pipe_intr(sc->sc_bt_iface, sc->sc_bt_epaddr,
		    USBD_SHORT_XFER_OK, &sc->sc_bt_pipe, sc, sc->bt_pkt,
		    sc->bt_maxlen, ubcmtp_bt_intr, USBD_DEFAULT_INTERVAL);
		if (err != USBD_NORMAL_COMPLETION) {
			printf("%s: failed opening button pipe\n",
			    sc->sc_dev.dv_xname);
			goto fail1;
		}
	}

	/* setup trackpad data pipe */

	ed = usbd_interface2endpoint_descriptor(sc->sc_tp_iface, 0);
	if (ed == NULL) {
		printf("%s: failed getting trackpad data endpoint descriptor\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}
	sc->sc_tp_epaddr = ed->bEndpointAddress;
	sc->tp_pkt = malloc(sc->tp_maxlen, M_USBDEV, M_WAITOK);
	if (sc->tp_pkt == NULL)
		goto fail2;

	DPRINTF("%s: trackpad data iface at 0x%x, max size %d\n",
	    sc->sc_dev.dv_xname, sc->sc_tp_epaddr, sc->tp_maxlen);

	err = usbd_open_pipe_intr(sc->sc_tp_iface, sc->sc_tp_epaddr,
	    USBD_SHORT_XFER_OK, &sc->sc_tp_pipe, sc, sc->tp_pkt, sc->tp_maxlen,
	    ubcmtp_tp_intr, USBD_DEFAULT_INTERVAL);
	if (err != USBD_NORMAL_COMPLETION) {
		printf("%s: error opening trackpad data pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}

	return (0);

fail2:
	if (sc->sc_tp_pipe != NULL)
		usbd_close_pipe(sc->sc_tp_pipe);
	if (sc->tp_pkt != NULL)
		free(sc->tp_pkt, M_USBDEV, sc->tp_maxlen);
fail1:
	if (sc->sc_bt_pipe != NULL)
		usbd_close_pipe(sc->sc_bt_pipe);
	if (sc->bt_pkt != NULL)
		free(sc->bt_pkt, M_USBDEV, sc->bt_maxlen);

	return (1);
}

void
ubcmtp_tp_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ubcmtp_softc *sc = priv;
	struct ubcmtp_finger *finger;
	u_int32_t pktlen;
	int off, s, btn, contacts = 0;

	if (usbd_is_dying(sc->sc_udev) || !(sc->sc_status & UBCMTP_ENABLED))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF("%s: %s with status 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, status);

		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tp_pipe);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &pktlen, NULL);

	if (sc->tp_pkt == NULL || pktlen < sc->tp_offset)
		return;

	contacts = 0;
	for (off = sc->tp_offset; off < pktlen;
	    off += (sizeof(struct ubcmtp_finger) + sc->tp_fingerpad)) {
		finger = (struct ubcmtp_finger *)(sc->tp_pkt + off);

		if ((int16_t)letoh16(finger->touch_major) == 0)
			continue; /* finger lifted */

		sc->frame[contacts].x = (int16_t)letoh16(finger->abs_x);
		sc->frame[contacts].y = (int16_t)letoh16(finger->abs_y);
		sc->frame[contacts].pressure = DEFAULT_PRESSURE;
		contacts++;
	}

	btn = sc->btn;
	if (sc->dev_type->type == UBCMTP_TYPE2)
		sc->btn = !!((int16_t)letoh16(sc->tp_pkt[UBCMTP_TYPE2_BTOFF]));
	else if (sc->dev_type->type == UBCMTP_TYPE3)
		sc->btn = !!((int16_t)letoh16(sc->tp_pkt[UBCMTP_TYPE3_BTOFF]));
	else if (sc->dev_type->type == UBCMTP_TYPE4)
		sc->btn = !!((int16_t)letoh16(sc->tp_pkt[UBCMTP_TYPE4_BTOFF]));

	if (contacts || sc->contacts || sc->btn != btn) {
		sc->contacts = contacts;
		s = spltty();
		wsmouse_buttons(sc->sc_wsmousedev, sc->btn);
		wsmouse_mtframe(sc->sc_wsmousedev, sc->frame, contacts);
		wsmouse_input_sync(sc->sc_wsmousedev);
		splx(s);
	}
}

/* hardware button interrupt */
void
ubcmtp_bt_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ubcmtp_softc *sc = priv;
	struct ubcmtp_button *pkt;
	u_int32_t len;

	if (usbd_is_dying(sc->sc_udev) || !(sc->sc_status & UBCMTP_ENABLED))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF("%s: %s with status 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, status);
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_tp_pipe);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (sc->bt_pkt == NULL || len < sizeof(struct ubcmtp_button))
		return;

	pkt = (struct ubcmtp_button *)(sc->bt_pkt);

	DPRINTF("%s: button interrupt (%d, %d, %d, %d)", sc->sc_dev.dv_xname,
	    pkt->unused, pkt->button, pkt->rel_x, pkt->rel_y);

	if (pkt->button != sc->btn) {
		sc->btn = pkt->button;
		wsmouse_buttons(sc->sc_wsmousedev, sc->btn);
		wsmouse_input_sync(sc->sc_wsmousedev);
	}
}
