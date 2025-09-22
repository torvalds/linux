/*	$OpenBSD: umsm.c,v 1.127 2024/05/23 08:06:22 kevlo Exp $	*/

/*
 * Copyright (c) 2008 Yojiro UO <yuo@nui.org>
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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

/* Driver for Qualcomm MSM EVDO and UMTS communication devices */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/usbcdc.h>
#include <dev/usb/umassvar.h>
#undef DPRINTF	/* undef DPRINTF for umass */

#ifdef UMSM_DEBUG
int     umsmdebug = 0;
#define DPRINTFN(n, x)  do { if (umsmdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define UMSMBUFSZ	4096
#define	UMSM_INTR_INTERVAL	100	/* ms */
#define E220_MODE_CHANGE_REQUEST 0x2
#define TRUINSTALL_CHANGEMODE_REQUEST 0x0b

int umsm_match(struct device *, void *, void *);
void umsm_attach(struct device *, struct device *, void *);
int umsm_detach(struct device *, int);

int umsm_open(void *, int);
void umsm_close(void *, int);
void umsm_intr(struct usbd_xfer *, void *, usbd_status);
void umsm_get_status(void *, int, u_char *, u_char *);
void umsm_set(void *, int, int, int);

struct umsm_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	int			 sc_iface_no;
	struct device		*sc_subdev;
	uint16_t		 sc_flag;

	/* interrupt ep */
	int			 sc_intr_number;
	struct usbd_pipe	*sc_intr_pipe;
	u_char			*sc_intr_buf;
	int			 sc_isize;

	u_char			 sc_lsr;	/* Local status register */
	u_char			 sc_msr;	/* status register */
	u_char			 sc_dtr;	/* current DTR state */
	u_char			 sc_rts;	/* current RTS state */
};

usbd_status umsm_huawei_changemode(struct usbd_device *);
usbd_status umsm_truinstall_changemode(struct usbd_device *);
usbd_status umsm_umass_changemode(struct umsm_softc *);

const struct ucom_methods umsm_methods = {
	umsm_get_status,
	umsm_set,
	NULL,
	NULL,
	umsm_open,
	umsm_close,
	NULL,
	NULL,
};

struct umsm_type {
	struct usb_devno	umsm_dev;
	uint16_t		umsm_flag;
/* device type */
#define	DEV_NORMAL	0x0000
#define	DEV_HUAWEI	0x0001
#define	DEV_TRUINSTALL	0x0002
#define	DEV_UMASS1	0x0010
#define	DEV_UMASS2	0x0020
#define	DEV_UMASS3	0x0040
#define	DEV_UMASS4	0x0080
#define	DEV_UMASS5	0x0100
#define	DEV_UMASS6	0x0200
#define	DEV_UMASS7	0x0400
#define	DEV_UMASS8	0x1000
#define DEV_UMASS	(DEV_UMASS1 | DEV_UMASS2 | DEV_UMASS3 | DEV_UMASS4 | \
    DEV_UMASS5 | DEV_UMASS6 | DEV_UMASS7 | DEV_UMASS8)
};

static const struct umsm_type umsm_devs[] = {
	{{ USB_VENDOR_AIRPRIME,	USB_PRODUCT_AIRPRIME_PC5220 }, 0},
	{{ USB_VENDOR_AIRPRIME, USB_PRODUCT_AIRPRIME_AIRCARD_313U }, 0},

	{{ USB_VENDOR_ANYDATA,	USB_PRODUCT_ANYDATA_A2502 }, 0},
	{{ USB_VENDOR_ANYDATA,	USB_PRODUCT_ANYDATA_ADU_500A }, 0},
	{{ USB_VENDOR_ANYDATA,  USB_PRODUCT_ANYDATA_ADU_E100H }, 0},

	{{ USB_VENDOR_DELL,	USB_PRODUCT_DELL_EU870D }, 0},
	{{ USB_VENDOR_DELL,	USB_PRODUCT_DELL_U740 }, 0},
	{{ USB_VENDOR_DELL,	USB_PRODUCT_DELL_W5500 }, 0},

	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E161 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E173S_INIT }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E173S }, 0},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E180 }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E181 }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E182 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E1820 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E220 }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E303 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E353_INIT }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E510 }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E618 }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E392_INIT }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_EM770W }, 0},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_MOBILE }, DEV_HUAWEI},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K3765_INIT }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K3765 }, 0},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K3772_INIT }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K3772 }, 0},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_MU609 }, DEV_TRUINSTALL},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K4510 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_K4511 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E1750 }, DEV_UMASS5},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E1752 }, 0},
	{{ USB_VENDOR_HUAWEI,	USB_PRODUCT_HUAWEI_E3372 }, DEV_UMASS5},

	{{ USB_VENDOR_HYUNDAI,	USB_PRODUCT_HYUNDAI_UM175 }, 0},

	{{ USB_VENDOR_LONGCHEER, USB_PRODUCT_LONGCHEER_D21LCMASS }, DEV_UMASS3},
	{{ USB_VENDOR_LONGCHEER, USB_PRODUCT_LONGCHEER_D21LC }, 0},
	{{ USB_VENDOR_LONGCHEER, USB_PRODUCT_LONGCHEER_510FUMASS }, DEV_UMASS3},
	{{ USB_VENDOR_LONGCHEER, USB_PRODUCT_LONGCHEER_510FU }, 0},

	{{ USB_VENDOR_KYOCERA2,	USB_PRODUCT_KYOCERA2_KPC650 }, 0},

	{{ USB_VENDOR_MEDIATEK, USB_PRODUCT_MEDIATEK_UMASS }, DEV_UMASS8},
	{{ USB_VENDOR_MEDIATEK, USB_PRODUCT_MEDIATEK_DC_4COM }, 0},

	/* XXX Some qualcomm devices are missing */
	{{ USB_VENDOR_QUALCOMM,	USB_PRODUCT_QUALCOMM_MSM_DRIVER }, DEV_UMASS1},
	{{ USB_VENDOR_QUALCOMM,	USB_PRODUCT_QUALCOMM_MSM_DRIVER2 }, DEV_UMASS4},
	{{ USB_VENDOR_QUALCOMM,	USB_PRODUCT_QUALCOMM_MSM_HSDPA }, 0},
	{{ USB_VENDOR_QUALCOMM,	USB_PRODUCT_QUALCOMM_MSM_HSDPA2 }, 0},
	{{ USB_VENDOR_QUALCOMM,	USB_PRODUCT_QUALCOMM_MSM_HSDPA3 }, 0},

	{{ USB_VENDOR_QUANTA2, USB_PRODUCT_QUANTA2_UMASS }, DEV_UMASS4},
	{{ USB_VENDOR_QUANTA2, USB_PRODUCT_QUANTA2_Q101 }, 0},

	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EC21 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EC25 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EG91 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EG95 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_BG96 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EG06 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EM060K }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_AG15 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_AG35 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_AG520R }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_AG525R }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EG12 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_EG20 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_BG95 }, 0},
	{{ USB_VENDOR_QUECTEL, USB_PRODUCT_QUECTEL_RG5XXQ }, 0},

	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_AC2746 }, 0},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_UMASS_INSTALLER }, DEV_UMASS4},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_UMASS_INSTALLER2 }, DEV_UMASS6},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_UMASS_INSTALLER3 }, DEV_UMASS7},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_UMASS_INSTALLER4 }, DEV_UMASS4},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_K3565Z }, 0},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_MF112 }, DEV_UMASS4},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_MF633 }, 0},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_MF637 }, 0},
	{{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_MSA110UP }, 0},

	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_EXPRESSCARD }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MERLINV620 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MERLINV740 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_V720 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MERLINU740 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MERLINU740_2 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_U870 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_XU870 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_EU870D }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_X950D }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_ES620 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_U720 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_U727 }, DEV_UMASS1},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MC950D }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MERLINX950D }, DEV_UMASS4},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_ZEROCD2 }, DEV_UMASS4},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_U760 }, DEV_UMASS4},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MC760 }, 0},
	{{ USB_VENDOR_NOVATEL, USB_PRODUCT_NOVATEL_MC760CD }, DEV_UMASS4},

	{{ USB_VENDOR_NOVATEL1,	USB_PRODUCT_NOVATEL1_FLEXPACKGPS }, 0},

	{{ USB_VENDOR_NOKIA2, USB_PRODUCT_NOKIA2_CS15UMASS }, DEV_UMASS4},
	{{ USB_VENDOR_NOKIA2, USB_PRODUCT_NOKIA2_CS15 }, 0},

	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GT3GFUSION }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GT3GPLUS }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GT3GQUAD }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GT3GQUADPLUS }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GSICON72 }, DEV_UMASS1},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GTHSUPA380E }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_GTMAX36 }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_ICON225 }, DEV_UMASS2},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_ICON505 }, DEV_UMASS1},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_SCORPION }, 0},
	{{ USB_VENDOR_OPTION, USB_PRODUCT_OPTION_VODAFONEMC3G }, 0},

	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_EM5625 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC5720 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD_595 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC5725 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC597E }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_C597 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC595U }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD_580 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC5720_2 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC5725_2 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8755_2 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8765 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8755 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8775 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8755_3 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8775_2 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD_875 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8780 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8781 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8790 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC880 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC881 }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC880E }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC881E }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC880U }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC881U }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AC885U }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_C01SW }, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_USB305}, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC7304}, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_TRUINSTALL }, DEV_TRUINSTALL},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC8355}, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD_340U}, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_AIRCARD_770S}, 0},
	{{ USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC7455}, 0},

	{{ USB_VENDOR_SIMCOM, USB_PRODUCT_SIMCOM_SIM5320}, 0},
	{{ USB_VENDOR_SIMCOM, USB_PRODUCT_SIMCOM_SIM7600E}, 0},

	{{ USB_VENDOR_TCTMOBILE, USB_PRODUCT_TCTMOBILE_UMASS }, DEV_UMASS3},
	{{ USB_VENDOR_TCTMOBILE, USB_PRODUCT_TCTMOBILE_UMASS_2 }, DEV_UMASS3},
	{{ USB_VENDOR_TCTMOBILE, USB_PRODUCT_TCTMOBILE_UMSM }, 0},
	{{ USB_VENDOR_TCTMOBILE, USB_PRODUCT_TCTMOBILE_UMSM_2 }, 0},
	{{ USB_VENDOR_TCTMOBILE, USB_PRODUCT_TCTMOBILE_UMSM_3 }, 0},

	{{ USB_VENDOR_TOSHIBA, USB_PRODUCT_TOSHIBA_HSDPA }, 0},

	{{ USB_VENDOR_HP, USB_PRODUCT_HP_HS2300 }, 0},

	{{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CNU510 }, 0}, /* ??? */
	{{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CCU550 }, 0}, /* ??? */
	{{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CGU628 }, DEV_UMASS1},
	{{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CGU628_DISK }, 0},
	{{ USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CNU680 }, DEV_UMASS1},
};

#define umsm_lookup(v, p) ((const struct umsm_type *)usb_lookup(umsm_devs, v, p))

struct cfdriver umsm_cd = {
	NULL, "umsm", DV_DULL
};

const struct cfattach umsm_ca = {
	sizeof(struct umsm_softc), umsm_match, umsm_attach, umsm_detach
};

int
umsm_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	const struct umsm_type *umsmt;
	usb_interface_descriptor_t *id;
	uint16_t flag;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	umsmt = umsm_lookup(uaa->vendor, uaa->product);
	if (umsmt == NULL)
		return UMATCH_NONE;

	/*
	 * Some devices (eg Huawei E220) have multiple interfaces and some
	 * of them are of class umass. Don't claim ownership in such case.
	 */

	id = usbd_get_interface_descriptor(uaa->iface);
	flag = umsmt->umsm_flag;

	if (id == NULL || id->bInterfaceClass == UICLASS_MASS) {
		/*
		 * Some high-speed modems require special care.
		 */
		if (flag & DEV_HUAWEI) {
			if (uaa->ifaceno != 2)
				return UMATCH_VENDOR_IFACESUBCLASS;
			else
				return UMATCH_NONE;
		} else if (flag & DEV_UMASS) {
			return UMATCH_VENDOR_IFACESUBCLASS;
		} else if (flag & DEV_TRUINSTALL) {
			return UMATCH_VENDOR_IFACESUBCLASS;
		} else
			return UMATCH_NONE;
	/*
	 * Some devices have interfaces which fail to attach but in
	 * addition seem to make the remaining interfaces unusable. Only
	 * attach whitelisted interfaces in this case.
	 */
	} else if ((uaa->vendor == USB_VENDOR_MEDIATEK &&
		    uaa->product == USB_PRODUCT_MEDIATEK_DC_4COM) &&
		   !(id->bInterfaceClass == UICLASS_VENDOR &&
		    ((id->bInterfaceSubClass == 0x02 &&
		      id->bInterfaceProtocol == 0x01) ||
		     (id->bInterfaceSubClass == 0x00 &&
		      id->bInterfaceProtocol == 0x00)))) {
		return UMATCH_NONE;

	/* See the Quectel LTE&5G Linux USB Driver User Guide */ 
	} else if (uaa->vendor == USB_VENDOR_QUECTEL) {
		/* Some interfaces can be used as network devices */
		if (id->bInterfaceClass != UICLASS_VENDOR)
			return UMATCH_NONE;

		/* Interface 4 can be used as a network device */
		if (uaa->ifaceno >= 4)
			return UMATCH_NONE;
	}

	return UMATCH_VENDOR_IFACESUBCLASS;
}

void
umsm_attach(struct device *parent, struct device *self, void *aux)
{
	struct umsm_softc *sc = (struct umsm_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;

	bzero(&uca, sizeof(uca));
	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;
	sc->sc_flag  = umsm_lookup(uaa->vendor, uaa->product)->umsm_flag;

	id = usbd_get_interface_descriptor(sc->sc_iface);

	/*
	 * Some 3G modems have multiple interfaces and some of them
	 * are umass class. Don't claim ownership in such case.
	 */
	if (id == NULL || id->bInterfaceClass == UICLASS_MASS) {
		/*
		 * Some 3G modems require a special request to
		 * enable their modem function.
		 */
		if ((sc->sc_flag & DEV_HUAWEI) && uaa->ifaceno == 0) {
                        umsm_huawei_changemode(uaa->device);
			printf("%s: umass only mode. need to reattach\n",
				sc->sc_dev.dv_xname);
		} else if (sc->sc_flag & DEV_TRUINSTALL) {
			umsm_truinstall_changemode(uaa->device);
			printf("%s: truinstall mode. need to reattach\n",
				sc->sc_dev.dv_xname);
		} else if ((sc->sc_flag & DEV_UMASS) && uaa->ifaceno == 0) {
			umsm_umass_changemode(sc);
		}

		/*
		 * The device will reset its own bus from the device side
		 * when its mode was changed, so just return.
		 */
		return;
	}

	sc->sc_iface_no = id->bInterfaceNumber;
	uca.bulkin = uca.bulkout = -1;
	sc->sc_intr_number = sc->sc_isize = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
			DPRINTF(("%s: find interrupt endpoint for %s\n",
				__func__, sc->sc_dev.dv_xname));
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkin = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkout = ed->bEndpointAddress;
	}
	if (uca.bulkin == -1 || uca.bulkout == -1) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	sc->sc_dtr = sc->sc_rts = -1;

	/* We need to force size as some devices lie */
	uca.ibufsize = UMSMBUFSZ;
	uca.obufsize = UMSMBUFSZ;
	uca.ibufsizepad = UMSMBUFSZ;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_iface;
	uca.methods = &umsm_methods;
	uca.arg = sc;
	uca.info = NULL;
	uca.portno = UCOM_UNK_PORTNO;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
umsm_detach(struct device *self, int flags)
{
	struct umsm_softc *sc = (struct umsm_softc *)self;
	int rv = 0;

	/* close the interrupt endpoint if that is opened */
	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}

	usbd_deactivate(sc->sc_udev);
	if (sc->sc_subdev != NULL) {
		rv = config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return (rv);
}

int
umsm_open(void *addr, int portno)
{
	struct umsm_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return (ENXIO);

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_iface,
		    sc->sc_intr_number,
		    USBD_SHORT_XFER_OK,
		    &sc->sc_intr_pipe,
		    sc,
		    sc->sc_intr_buf,
		    sc->sc_isize,
		    umsm_intr,
		    UMSM_INTR_INTERVAL);
		if (err) {
			printf("%s: cannot open interrupt pipe (addr %d)\n",
			    sc->sc_dev.dv_xname,
			    sc->sc_intr_number);
			return (EIO);
		}
	}

	return (0);
}

void
umsm_close(void *addr, int portno)
{
	struct umsm_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
			    sc->sc_dev.dv_xname,
			    usbd_errstr(err));
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}
}

void
umsm_intr(struct usbd_xfer *xfer, void *priv,
	usbd_status status)
{
	struct umsm_softc *sc = priv;
	struct usb_cdc_notification *buf;
	u_char mstatus;

	buf = (struct usb_cdc_notification *)sc->sc_intr_buf;
	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		DPRINTF(("%s: umsm_intr: abnormal status: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	if (buf->bmRequestType != UCDC_NOTIFICATION) {
#if 1 /* test */
		printf("%s: this device is not using CDC notify message in intr pipe.\n"
		    "Please send your dmesg to <bugs@openbsd.org>, thanks.\n",
		    sc->sc_dev.dv_xname);
		printf("%s: intr buffer 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		    sc->sc_dev.dv_xname,
		    sc->sc_intr_buf[0], sc->sc_intr_buf[1],
		    sc->sc_intr_buf[2], sc->sc_intr_buf[3],
		    sc->sc_intr_buf[4], sc->sc_intr_buf[5],
		    sc->sc_intr_buf[6]);
#else
		DPRINTF(("%s: umsm_intr: unknown message type(0x%02x)\n",
		    sc->sc_dev.dv_xname, buf->bmRequestType));
#endif
		return;
	}

	if (buf->bNotification == UCDC_N_SERIAL_STATE) {
		/* invalid message length, discard it */
		if (UGETW(buf->wLength) != 2)
			return;
		/* XXX: sc_lsr is always 0 */
		sc->sc_lsr = sc->sc_msr = 0;
		mstatus = buf->data[0];
		if (ISSET(mstatus, UCDC_N_SERIAL_RI))
			sc->sc_msr |= UMSR_RI;
		if (ISSET(mstatus, UCDC_N_SERIAL_DSR))
			sc->sc_msr |= UMSR_DSR;
		if (ISSET(mstatus, UCDC_N_SERIAL_DCD))
			sc->sc_msr |= UMSR_DCD;
	} else if (buf->bNotification != UCDC_N_CONNECTION_SPEED_CHANGE) {
		DPRINTF(("%s: umsm_intr: unknown notify message (0x%02x)\n",
		    sc->sc_dev.dv_xname, buf->bNotification));
		return;
	}

	ucom_status_change((struct ucom_softc *)sc->sc_subdev);
}

void
umsm_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct umsm_softc *sc = addr;

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}

void
umsm_set(void *addr, int portno, int reg, int onoff)
{
	struct umsm_softc *sc = addr;
	usb_device_request_t req;
	int ls;

	switch (reg) {
	case UCOM_SET_DTR:
		if (sc->sc_dtr == onoff)
			return;
		sc->sc_dtr = onoff;
		break;
	case UCOM_SET_RTS:
		if (sc->sc_rts == onoff)
			return;
		sc->sc_rts = onoff;
		break;
	default:
		return;
	}

	/* build a usb request */
	ls = (sc->sc_dtr ? UCDC_LINE_DTR : 0) |
	     (sc->sc_rts ? UCDC_LINE_RTS : 0);
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, ls);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	(void)usbd_do_request(sc->sc_udev, &req, 0);
}

usbd_status
umsm_huawei_changemode(struct usbd_device *dev)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UF_DEVICE_REMOTE_WAKEUP);
	USETW(req.wIndex, E220_MODE_CHANGE_REQUEST);
	USETW(req.wLength, 0);

	err = usbd_do_request(dev, &req, 0);
	if (err)
		return (EIO);

	return (0);
}

usbd_status
umsm_truinstall_changemode(struct usbd_device *dev)
{
	usb_device_request_t req;
	usbd_status err;
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = TRUINSTALL_CHANGEMODE_REQUEST;
	USETW(req.wValue, 0x1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(dev, &req, 0);
	if (err)
		return (EIO);

	return (0);
}

usbd_status
umsm_umass_changemode(struct umsm_softc *sc)
{
#define UMASS_CMD_REZERO_UNIT		0x01
#define UMASS_CMD_START_STOP		0x1b
#define UMASS_CMDPARAM_EJECT		0x02
#define UMASS_SERVICE_ACTION_OUT	0x9f
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct usbd_xfer *xfer;
	struct usbd_pipe *cmdpipe;
	usbd_status err;
	u_int32_t n;
	void *bufp;
	int target_ep, i;

	struct umass_bbb_cbw cbw;
	static int dCBWTag = 0x12345678;

	USETDW(cbw.dCBWSignature, CBWSIGNATURE);
	USETDW(cbw.dCBWTag, dCBWTag);
	cbw.bCBWLUN   = 0;
	cbw.bCDBLength= 6;
	bzero(cbw.CBWCDB, sizeof(cbw.CBWCDB));

	switch (sc->sc_flag) {
	case DEV_UMASS1:
		USETDW(cbw.dCBWDataTransferLength, 0x0);
		cbw.bCBWFlags = CBWFLAGS_OUT;
		cbw.CBWCDB[0] = UMASS_CMD_REZERO_UNIT;
		cbw.CBWCDB[1] = 0x0;	/* target LUN: 0 */
		break;
	case DEV_UMASS2:
		USETDW(cbw.dCBWDataTransferLength, 0x1);
		cbw.bCBWFlags = CBWFLAGS_IN;
		cbw.CBWCDB[0] = UMASS_CMD_REZERO_UNIT;
		cbw.CBWCDB[1] = 0x0;	/* target LUN: 0 */
		break;
	case DEV_UMASS3: /* longcheer */
		USETDW(cbw.dCBWDataTransferLength, 0x80);
		cbw.bCBWFlags = CBWFLAGS_IN;
		cbw.CBWCDB[0] = 0x06;
		cbw.CBWCDB[1] = 0xf5;
		cbw.CBWCDB[2] = 0x04;
		cbw.CBWCDB[3] = 0x02;
		cbw.CBWCDB[4] = 0x52;
		cbw.CBWCDB[5] = 0x70;
		break;
	case DEV_UMASS4:
		USETDW(cbw.dCBWDataTransferLength, 0x0);
		cbw.bCBWFlags = CBWFLAGS_OUT;
		cbw.CBWCDB[0] = UMASS_CMD_START_STOP;
		cbw.CBWCDB[1] = 0x00;	/* target LUN: 0 */
		cbw.CBWCDB[4] = UMASS_CMDPARAM_EJECT;
		break;
	case DEV_UMASS5:
		cbw.bCBWFlags = CBWFLAGS_OUT;
		cbw.CBWCDB[0] = 0x11;
		cbw.CBWCDB[1] = 0x06;
		break;
	case DEV_UMASS6:	/* ZTE */
		USETDW(cbw.dCBWDataTransferLength, 0x20);
		cbw.bCBWFlags = CBWFLAGS_IN;
		cbw.bCDBLength= 12;
		cbw.CBWCDB[0] = 0x85;
		cbw.CBWCDB[1] = 0x01;
		cbw.CBWCDB[2] = 0x01;
		cbw.CBWCDB[3] = 0x01;
		cbw.CBWCDB[4] = 0x18;
		cbw.CBWCDB[5] = 0x01;
		cbw.CBWCDB[6] = 0x01;
		cbw.CBWCDB[7] = 0x01;
		cbw.CBWCDB[8] = 0x01;
		cbw.CBWCDB[9] = 0x01;
		break;
	case DEV_UMASS7:	/* ZTE */
		USETDW(cbw.dCBWDataTransferLength, 0xc0);
		cbw.bCBWFlags = CBWFLAGS_IN;
		cbw.CBWCDB[0] = UMASS_SERVICE_ACTION_OUT;
		cbw.CBWCDB[1] = 0x03;
		break;
	case DEV_UMASS8:
		USETDW(cbw.dCBWDataTransferLength, 0x0);
		cbw.bCBWFlags = CBWFLAGS_OUT;
		cbw.CBWCDB[0] = 0xf0;
		cbw.CBWCDB[1] = 0x01;
		cbw.CBWCDB[2] = 0x03;
		break;
	default:
		DPRINTF(("%s: unknown device type.\n", sc->sc_dev.dv_xname));
		break;
	}

	/* get command endpoint address */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			return (USBD_IOERROR);
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			target_ep = ed->bEndpointAddress;
	}

	/* open command endpoint */
	err = usbd_open_pipe(sc->sc_iface, target_ep,
		USBD_EXCLUSIVE_USE, &cmdpipe);
	if (err) {
		DPRINTF(("%s: open pipe for modem change cmd failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err)));
		return (err);
	}

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL) {
		usbd_close_pipe(cmdpipe);
		return (USBD_NOMEM);
	} else {
		bufp = usbd_alloc_buffer(xfer, UMASS_BBB_CBW_SIZE);
		if (bufp == NULL)
			err = USBD_NOMEM;
		else {
			n = UMASS_BBB_CBW_SIZE;
			memcpy(bufp, &cbw, UMASS_BBB_CBW_SIZE);
			usbd_setup_xfer(xfer, cmdpipe, 0, bufp, n,
			    USBD_NO_COPY | USBD_SYNCHRONOUS, 0, NULL);
			err = usbd_transfer(xfer);
			if (err) {
				usbd_clear_endpoint_stall(cmdpipe);
				DPRINTF(("%s: send error:%s", __func__,
				    usbd_errstr(err)));
			}
		}
		usbd_close_pipe(cmdpipe);
		usbd_free_buffer(xfer);
		usbd_free_xfer(xfer);
	}

	return (err);
}
