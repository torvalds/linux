/*	$OpenBSD: uslcom.c,v 1.45 2024/05/23 03:21:09 jsg Exp $	*/

/*
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef USLCOM_DEBUG
#define DPRINTFN(n, x)  do { if (uslcomdebug > (n)) printf x; } while (0)
int	uslcomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define USLCOMBUFSZ		256

#define USLCOM_SET_DATA_BITS(x)	(x << 8)

#define USLCOM_WRITE		0x41
#define USLCOM_READ		0xc1

#define USLCOM_UART		0x00
#define USLCOM_SET_BAUD_DIV	0x01
#define USLCOM_DATA		0x03
#define USLCOM_BREAK		0x05
#define USLCOM_CTRL		0x07
#define USLCOM_SET_FLOW		0x13
#define USLCOM_SET_BAUD_RATE	0x1e

#define USLCOM_UART_DISABLE	0x00
#define USLCOM_UART_ENABLE	0x01

#define USLCOM_CTRL_DTR_ON	0x0001	
#define USLCOM_CTRL_DTR_SET	0x0100
#define USLCOM_CTRL_RTS_ON	0x0002
#define USLCOM_CTRL_RTS_SET	0x0200
#define USLCOM_CTRL_CTS		0x0010
#define USLCOM_CTRL_DSR		0x0020
#define USLCOM_CTRL_DCD		0x0080

#define USLCOM_BAUD_REF		3686400 /* 3.6864 MHz */

#define USLCOM_STOP_BITS_1	0x00
#define USLCOM_STOP_BITS_2	0x02

#define USLCOM_PARITY_NONE	0x00
#define USLCOM_PARITY_ODD	0x10
#define USLCOM_PARITY_EVEN	0x20

#define USLCOM_BREAK_OFF	0x00
#define USLCOM_BREAK_ON		0x01

/* USLCOM_SET_FLOW values - 1st word */
#define USLCOM_FLOW_DTR_ON	0x00000001 /* DTR static active */
#define USLCOM_FLOW_CTS_HS	0x00000008 /* CTS handshake */
/* USLCOM_SET_FLOW values - 2nd word */
#define USLCOM_FLOW_RTS_ON	0x00000040 /* RTS static active */
#define USLCOM_FLOW_RTS_HS	0x00000080 /* RTS handshake */

struct uslcom_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	struct device		*sc_subdev;

	u_char			 sc_msr;
	u_char			 sc_lsr;
};

void	uslcom_get_status(void *, int portno, u_char *lsr, u_char *msr);
void	uslcom_set(void *, int, int, int);
int	uslcom_param(void *, int, struct termios *);
int	uslcom_open(void *sc, int portno);
void	uslcom_close(void *, int);
void	uslcom_break(void *sc, int portno, int onoff);

const struct ucom_methods uslcom_methods = {
	uslcom_get_status,
	uslcom_set,
	uslcom_param,
	NULL,
	uslcom_open,
	uslcom_close,
	NULL,
	NULL,
};

static const struct usb_devno uslcom_devs[] = {
	{ USB_VENDOR_ARUBA,		USB_PRODUCT_ARUBA_CP210X },
	{ USB_VENDOR_BALTECH,		USB_PRODUCT_BALTECH_CARDREADER },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_5000CT2 },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_5500PACA },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_5500PCU },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_560884 },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_5800PC },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_C5000CT2 },
	{ USB_VENDOR_CLIPSAL,		USB_PRODUCT_CLIPSAL_L51XX },
	{ USB_VENDOR_CORSAIR,		USB_PRODUCT_CORSAIR_CP210X },
	{ USB_VENDOR_DATAAPEX,		USB_PRODUCT_DATAAPEX_MULTICOM },
	{ USB_VENDOR_DELL,		USB_PRODUCT_DELL_DW700 },
	{ USB_VENDOR_DIGIANSWER,	USB_PRODUCT_DIGIANSWER_ZIGBEE802154 },
	{ USB_VENDOR_DYNASTREAM,	USB_PRODUCT_DYNASTREAM_ANT2USB },
	{ USB_VENDOR_DYNASTREAM,	USB_PRODUCT_DYNASTREAM_ANTDEVBOARD },
	{ USB_VENDOR_DYNASTREAM,	USB_PRODUCT_DYNASTREAM_ANTDEVBOARD2 },
	{ USB_VENDOR_ELV,		USB_PRODUCT_ELV_USBI2C },
	{ USB_VENDOR_FESTO,		USB_PRODUCT_FESTO_CMSP },
	{ USB_VENDOR_FESTO,		USB_PRODUCT_FESTO_CPX_USB },
	{ USB_VENDOR_FOXCONN,		USB_PRODUCT_FOXCONN_PIRELLI_DP_L10 },
	{ USB_VENDOR_FOXCONN,		USB_PRODUCT_FOXCONN_TCOM_TC_300 },
	{ USB_VENDOR_GEMPLUS,		USB_PRODUCT_GEMPLUS_PROXPU },
	{ USB_VENDOR_JABLOTRON,		USB_PRODUCT_JABLOTRON_PC60B },
	{ USB_VENDOR_KAMSTRUP,		USB_PRODUCT_KAMSTRUP_MBUS_250D },
	{ USB_VENDOR_KAMSTRUP,		USB_PRODUCT_KAMSTRUP_OPTICALEYE },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M121 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M218A },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M219 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M233 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M235 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M335 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M336 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M350 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M371 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M411 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M425 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M455A },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M465 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M475A },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M625A },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M642A },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M648 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M737 },
	{ USB_VENDOR_LAKESHORE,		USB_PRODUCT_LAKESHORE_M776 },
	{ USB_VENDOR_LINKINSTRUMENTS,	USB_PRODUCT_LINKINSTRUMENTS_MSO19 },
	{ USB_VENDOR_LINKINSTRUMENTS,	USB_PRODUCT_LINKINSTRUMENTS_MSO28 },
	{ USB_VENDOR_LINKINSTRUMENTS,	USB_PRODUCT_LINKINSTRUMENTS_MSO28_2 },
	{ USB_VENDOR_MEI,		USB_PRODUCT_MEI_CASHFLOW_SC },
	{ USB_VENDOR_MEI,		USB_PRODUCT_MEI_S2000 },
	{ USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_M7100 },
	{ USB_VENDOR_OREGONSCI,		USB_PRODUCT_OREGONSCI_OWL_CM160 },
	{ USB_VENDOR_OWEN,		USB_PRODUCT_OWEN_AC4 },
	{ USB_VENDOR_PHILIPS,		USB_PRODUCT_PHILIPS_ACE1001 },
	{ USB_VENDOR_RENESAS,		USB_PRODUCT_RENESAS_RX610 },
	{ USB_VENDOR_SEL,		USB_PRODUCT_SEL_C662 },
	{ USB_VENDOR_SELUXIT,		USB_PRODUCT_SELUXIT_RF },
	{ USB_VENDOR_SIEMENS4,		USB_PRODUCT_SIEMENS4_RUGGEDCOM },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AC_SERV_CAN },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AC_SERV_CIS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AC_SERV_IBUS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AC_SERV_OBD },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AEROCOMM },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AMBER_AMB2560 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ARGUSISP },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ARKHAM_DS101_A },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ARKHAM_DS101_M },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ARYGON_MIFARE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_AVIT_USB_TTL },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_BALLUFF_RFID },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_BEI_VCP },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_BSM7DUSB },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_B_G_H3000 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_C2_EDGE_MODEM },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CELDEVKIT },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CP210X_1 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CP210X_2 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CP210X_3 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CRUMB128 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CYGNAL },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CYGNAL_DEBUG },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_CYGNAL_GPS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_DEGREECONT },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_DEKTEK_DTAPLUS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_DESKTOPMOBILE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_EDG1228 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_EM357 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_EM357LR },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_EMS_C1007 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_GSM2228 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_HAMLINKUSB },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_HUBZ },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_IMS_USB_RS422 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_INFINITY_MIC },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_INSYS_MODEM },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_IPLINK1220 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_IRZ_SG10 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_JUNIPER_BX_CONS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_KCF_PRN },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_KETRA_N1 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_KYOCERA_GPS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_HARP },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_JTAG },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_LIPOWSKY_LIN },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_MC35PU },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_MJS_TOSLINK },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_MMB_ZIGBEE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_MSD_DASHHAWK },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_MULTIPLEX_RC },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_OPTRIS_MSPRO },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_PII_ZIGBEE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_PLUGDRIVE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_POLOLU },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_PREON32 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_PROCYON_AVS },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_RIGBLASTER },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_RIGTALK },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_SB_PARAMOUNT_ME },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_SB_PARAMOUNT_ME2 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_SUUNTO },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_TAMSMASTER },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_TELEGESIS_ETRX2 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_TRACIENT },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_TRAQMATE },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_USBCOUNT50 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_USBPULSE100 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_USBSCOPE50 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_USBWAVE12 },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_VSTABI },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_WAVIT },
	{ USB_VENDOR_SILABS,		USB_PRODUCT_SILABS_ZEPHYR_BIO },
	{ USB_VENDOR_SILABS2,		USB_PRODUCT_SILABS2_DCU11CLONE },
	{ USB_VENDOR_SILABS3,		USB_PRODUCT_SILABS3_GPRS_MODEM },
	{ USB_VENDOR_SILABS4,		USB_PRODUCT_SILABS4_100EU_MODEM },
	{ USB_VENDOR_SILABS5,		USB_PRODUCT_SILABS5_EM358X },
	{ USB_VENDOR_SYNTECH,		USB_PRODUCT_SYNTECH_CIPHERLAB100 },
	{ USB_VENDOR_USI,		USB_PRODUCT_USI_MC60 },
	{ USB_VENDOR_VAISALA,		USB_PRODUCT_VAISALA_USBINSTCABLE },
	{ USB_VENDOR_VOTI,		USB_PRODUCT_VOTI_SELETEK_1 },
	{ USB_VENDOR_VOTI,		USB_PRODUCT_VOTI_SELETEK_2 },
	{ USB_VENDOR_WAGO,		USB_PRODUCT_WAGO_SERVICECABLE },
	{ USB_VENDOR_WAVESENSE,		USB_PRODUCT_WAVESENSE_JAZZ },
	{ USB_VENDOR_WIENERPLEINBAUS,	USB_PRODUCT_WIENERPLEINBAUS_CML },
	{ USB_VENDOR_WIENERPLEINBAUS,	USB_PRODUCT_WIENERPLEINBAUS_MPOD },
	{ USB_VENDOR_WIENERPLEINBAUS,	USB_PRODUCT_WIENERPLEINBAUS_PL512 },
	{ USB_VENDOR_WIENERPLEINBAUS,	USB_PRODUCT_WIENERPLEINBAUS_RCM },
	{ USB_VENDOR_WMR,		USB_PRODUCT_WMR_RIGBLASTER },
};

int uslcom_match(struct device *, void *, void *);
void uslcom_attach(struct device *, struct device *, void *);
int uslcom_detach(struct device *, int);

struct cfdriver uslcom_cd = {
	NULL, "uslcom", DV_DULL
};

const struct cfattach uslcom_ca = {
	sizeof(struct uslcom_softc), uslcom_match, uslcom_attach, uslcom_detach
};

int
uslcom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	return (usb_lookup(uslcom_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uslcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uslcom_softc *sc = (struct uslcom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i;

	bzero(&uca, sizeof(uca));
	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	id = usbd_get_interface_descriptor(sc->sc_iface);

	uca.bulkin = uca.bulkout = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
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

	uca.portno = id->bInterfaceNumber;
	uca.ibufsize = USLCOMBUFSZ;
	uca.obufsize = USLCOMBUFSZ;
	uca.ibufsizepad = USLCOMBUFSZ;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_iface;
	uca.methods = &uslcom_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uslcom_detach(struct device *self, int flags)
{
	struct uslcom_softc *sc = (struct uslcom_softc *)self;
	int rv = 0;

	if (sc->sc_subdev != NULL) {
		rv = config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return (rv);
}

int
uslcom_open(void *vsc, int portno)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	usbd_status err;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_UART;
	USETW(req.wValue, USLCOM_UART_ENABLE);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	return (0);
}

void
uslcom_close(void *vsc, int portno)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;

	if (usbd_is_dying(sc->sc_udev))
		return;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_UART;
	USETW(req.wValue, USLCOM_UART_DISABLE);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);
}

void
uslcom_set(void *vsc, int portno, int reg, int onoff)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	int ctl;

	switch (reg) {
	case UCOM_SET_DTR:
		ctl = onoff ? USLCOM_CTRL_DTR_ON : 0;
		ctl |= USLCOM_CTRL_DTR_SET;
		break;
	case UCOM_SET_RTS:
		ctl = onoff ? USLCOM_CTRL_RTS_ON : 0;
		ctl |= USLCOM_CTRL_RTS_SET;
		break;
	case UCOM_SET_BREAK:
		uslcom_break(sc, portno, onoff);
		return;
	default:
		return;
	}
	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_CTRL;
	USETW(req.wValue, ctl);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);
}

int
uslcom_param(void *vsc, int portno, struct termios *t)
{
	struct uslcom_softc *sc = (struct uslcom_softc *)vsc;
	usbd_status err;
	usb_device_request_t req;
	uint32_t baudrate, flowctrl[4];
	int data;

	if (t->c_ospeed <= 0 || t->c_ospeed > 2000000)
		return (EINVAL);

	baudrate = t->c_ospeed;
	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_BAUD_RATE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, portno);
	USETW(req.wLength, sizeof(baudrate));
	err = usbd_do_request(sc->sc_udev, &req, &baudrate);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CSTOPB))
		data = USLCOM_STOP_BITS_2;
	else
		data = USLCOM_STOP_BITS_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= USLCOM_PARITY_ODD;
		else
			data |= USLCOM_PARITY_EVEN;
	} else
		data |= USLCOM_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= USLCOM_SET_DATA_BITS(5);
		break;
	case CS6:
		data |= USLCOM_SET_DATA_BITS(6);
		break;
	case CS7:
		data |= USLCOM_SET_DATA_BITS(7);
		break;
	case CS8:
		data |= USLCOM_SET_DATA_BITS(8);
		break;
	}

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_DATA;
	USETW(req.wValue, data);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CRTSCTS)) {
		/*  rts/cts flow ctl */
		flowctrl[0] = htole32(USLCOM_FLOW_DTR_ON | USLCOM_FLOW_CTS_HS);
		flowctrl[1] = htole32(USLCOM_FLOW_RTS_HS);
	} else {
		/* disable flow ctl */
		flowctrl[0] = htole32(USLCOM_FLOW_DTR_ON);
		flowctrl[1] = htole32(USLCOM_FLOW_RTS_ON);
	}
	flowctrl[2] = 0;
	flowctrl[3] = 0;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_FLOW;
	USETW(req.wValue, 0);
	USETW(req.wIndex, portno);
	USETW(req.wLength, sizeof(flowctrl));
	err = usbd_do_request(sc->sc_udev, &req, flowctrl);
	if (err)
		return (EIO);

	return (0);
}

void
uslcom_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct uslcom_softc *sc = vsc;
	
	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

void
uslcom_break(void *vsc, int portno, int onoff)
{
	struct uslcom_softc *sc = vsc;
	usb_device_request_t req;
	int brk = onoff ? USLCOM_BREAK_ON : USLCOM_BREAK_OFF;	

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_BREAK;
	USETW(req.wValue, brk);
	USETW(req.wIndex, portno);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);
}
