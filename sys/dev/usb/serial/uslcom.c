/*	$OpenBSD: uslcom.c,v 1.17 2007/11/24 10:52:12 jsg Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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

/*
 * Driver for Silicon Laboratories CP2101/CP2102/CP2103/CP2104/CP2105
 * USB-Serial adapters.  Based on datasheet AN571, publicly available from
 * http://www.silabs.com/Support%20Documents/TechnicalDocs/AN571.pdf
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_ioctl.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR uslcom_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#ifdef USB_DEBUG
static int uslcom_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uslcom, CTLFLAG_RW, 0, "USB uslcom");
SYSCTL_INT(_hw_usb_uslcom, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uslcom_debug, 0, "Debug level");
#endif

#define	USLCOM_BULK_BUF_SIZE	1024
#define	USLCOM_CONFIG_INDEX	0

/* Request types */
#define	USLCOM_WRITE		0x41
#define	USLCOM_READ		0xc1

/* Request codes */
#define	USLCOM_IFC_ENABLE	0x00
#define	USLCOM_SET_BAUDDIV	0x01
#define	USLCOM_SET_LINE_CTL	0x03
#define	USLCOM_SET_BREAK	0x05
#define	USLCOM_SET_MHS		0x07
#define	USLCOM_GET_MDMSTS	0x08
#define	USLCOM_SET_FLOW		0x13
#define	USLCOM_SET_BAUDRATE	0x1e
#define	USLCOM_VENDOR_SPECIFIC	0xff

/* USLCOM_IFC_ENABLE values */
#define	USLCOM_IFC_ENABLE_DIS	0x00
#define	USLCOM_IFC_ENABLE_EN	0x01

/* USLCOM_SET_MHS/USLCOM_GET_MDMSTS values */
#define	USLCOM_MHS_DTR_ON	0x0001
#define	USLCOM_MHS_DTR_SET	0x0100
#define	USLCOM_MHS_RTS_ON	0x0002
#define	USLCOM_MHS_RTS_SET	0x0200
#define	USLCOM_MHS_CTS		0x0010
#define	USLCOM_MHS_DSR		0x0020
#define	USLCOM_MHS_RI		0x0040
#define	USLCOM_MHS_DCD		0x0080

/* USLCOM_SET_BAUDDIV values */
#define	USLCOM_BAUDDIV_REF	3686400 /* 3.6864 MHz */

/* USLCOM_SET_LINE_CTL values */
#define	USLCOM_STOP_BITS_1	0x00
#define	USLCOM_STOP_BITS_2	0x02
#define	USLCOM_PARITY_NONE	0x00
#define	USLCOM_PARITY_ODD	0x10
#define	USLCOM_PARITY_EVEN	0x20
#define	USLCOM_SET_DATA_BITS(x)	((x) << 8)

/* USLCOM_SET_BREAK values */
#define	USLCOM_SET_BREAK_OFF	0x00
#define	USLCOM_SET_BREAK_ON	0x01

/* USLCOM_SET_FLOW values - 1st word */
#define	USLCOM_FLOW_DTR_ON	0x00000001 /* DTR static active */
#define	USLCOM_FLOW_CTS_HS	0x00000008 /* CTS handshake */
/* USLCOM_SET_FLOW values - 2nd word */
#define	USLCOM_FLOW_RTS_ON	0x00000040 /* RTS static active */
#define	USLCOM_FLOW_RTS_HS	0x00000080 /* RTS handshake */

/* USLCOM_VENDOR_SPECIFIC values */
#define	USLCOM_GET_PARTNUM	0x370B
#define	USLCOM_WRITE_LATCH	0x37E1
#define	USLCOM_READ_LATCH	0x00C2

/* USLCOM_GET_PARTNUM values from hardware */
#define	USLCOM_PARTNUM_CP2101	1
#define	USLCOM_PARTNUM_CP2102	2
#define	USLCOM_PARTNUM_CP2103	3
#define	USLCOM_PARTNUM_CP2104	4
#define	USLCOM_PARTNUM_CP2105	5

enum {
	USLCOM_BULK_DT_WR,
	USLCOM_BULK_DT_RD,
	USLCOM_CTRL_DT_RD,
	USLCOM_N_TRANSFER,
};

struct uslcom_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;
	struct usb_callout sc_watchdog;

	struct usb_xfer *sc_xfer[USLCOM_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint8_t sc_msr;
	uint8_t sc_lsr;
	uint8_t sc_iface_no;
	uint8_t sc_partnum;
};

static device_probe_t uslcom_probe;
static device_attach_t uslcom_attach;
static device_detach_t uslcom_detach;
static void uslcom_free_softc(struct uslcom_softc *);

static usb_callback_t uslcom_write_callback;
static usb_callback_t uslcom_read_callback;
static usb_callback_t uslcom_control_callback;

static void uslcom_free(struct ucom_softc *);
static uint8_t uslcom_get_partnum(struct uslcom_softc *);
static void uslcom_cfg_open(struct ucom_softc *);
static void uslcom_cfg_close(struct ucom_softc *);
static void uslcom_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void uslcom_cfg_set_rts(struct ucom_softc *, uint8_t);
static void uslcom_cfg_set_break(struct ucom_softc *, uint8_t);
static void uslcom_cfg_param(struct ucom_softc *, struct termios *);
static void uslcom_cfg_get_status(struct ucom_softc *, uint8_t *, uint8_t *);
static int uslcom_ioctl(struct ucom_softc *, uint32_t, caddr_t, int,
    struct thread *);
static int uslcom_pre_param(struct ucom_softc *, struct termios *);
static void uslcom_start_read(struct ucom_softc *);
static void uslcom_stop_read(struct ucom_softc *);
static void uslcom_start_write(struct ucom_softc *);
static void uslcom_stop_write(struct ucom_softc *);
static void uslcom_poll(struct ucom_softc *ucom);

static const struct usb_config uslcom_config[USLCOM_N_TRANSFER] = {
	[USLCOM_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = USLCOM_BULK_BUF_SIZE,
               .flags = {.pipe_bof = 1,},
		.callback = &uslcom_write_callback,
	},

	[USLCOM_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = USLCOM_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &uslcom_read_callback,
	},
	[USLCOM_CTRL_DT_RD] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request) + 8,
		.flags = {.pipe_bof = 1,},
		.callback = &uslcom_control_callback,
		.timeout = 1000,	/* 1 second timeout */
	},
};

static struct ucom_callback uslcom_callback = {
	.ucom_cfg_get_status = &uslcom_cfg_get_status,
	.ucom_cfg_set_dtr = &uslcom_cfg_set_dtr,
	.ucom_cfg_set_rts = &uslcom_cfg_set_rts,
	.ucom_cfg_set_break = &uslcom_cfg_set_break,
	.ucom_cfg_open = &uslcom_cfg_open,
	.ucom_cfg_close = &uslcom_cfg_close,
	.ucom_cfg_param = &uslcom_cfg_param,
	.ucom_pre_param = &uslcom_pre_param,
	.ucom_ioctl = &uslcom_ioctl,
	.ucom_start_read = &uslcom_start_read,
	.ucom_stop_read = &uslcom_stop_read,
	.ucom_start_write = &uslcom_start_write,
	.ucom_stop_write = &uslcom_stop_write,
	.ucom_poll = &uslcom_poll,
	.ucom_free = &uslcom_free,
};

static const STRUCT_USB_HOST_ID uslcom_devs[] = {
#define	USLCOM_DEV(v,p)  { USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
    USLCOM_DEV(BALTECH, CARDREADER),
    USLCOM_DEV(CLIPSAL, 5000CT2),
    USLCOM_DEV(CLIPSAL, 5500PACA),
    USLCOM_DEV(CLIPSAL, 5500PCU),
    USLCOM_DEV(CLIPSAL, 560884),
    USLCOM_DEV(CLIPSAL, 5800PC),
    USLCOM_DEV(CLIPSAL, C5000CT2),
    USLCOM_DEV(CLIPSAL, L51xx),
    USLCOM_DEV(DATAAPEX, MULTICOM),
    USLCOM_DEV(DELL, DW700),
    USLCOM_DEV(DIGIANSWER, ZIGBEE802154),
    USLCOM_DEV(DYNASTREAM, ANTDEVBOARD),
    USLCOM_DEV(DYNASTREAM, ANTDEVBOARD2),
    USLCOM_DEV(DYNASTREAM, ANT2USB),
    USLCOM_DEV(ELV, USBI2C),
    USLCOM_DEV(FESTO, CMSP),
    USLCOM_DEV(FESTO, CPX_USB),
    USLCOM_DEV(FOXCONN, PIRELLI_DP_L10),
    USLCOM_DEV(FOXCONN, TCOM_TC_300),
    USLCOM_DEV(GEMALTO, PROXPU),
    USLCOM_DEV(JABLOTRON, PC60B),
    USLCOM_DEV(KAMSTRUP, OPTICALEYE),
    USLCOM_DEV(KAMSTRUP, MBUS_250D),
    USLCOM_DEV(LAKESHORE, 121),
    USLCOM_DEV(LAKESHORE, 218A),
    USLCOM_DEV(LAKESHORE, 219),
    USLCOM_DEV(LAKESHORE, 233),
    USLCOM_DEV(LAKESHORE, 235),
    USLCOM_DEV(LAKESHORE, 335),
    USLCOM_DEV(LAKESHORE, 336),
    USLCOM_DEV(LAKESHORE, 350),
    USLCOM_DEV(LAKESHORE, 371),
    USLCOM_DEV(LAKESHORE, 411),
    USLCOM_DEV(LAKESHORE, 425),
    USLCOM_DEV(LAKESHORE, 455A),
    USLCOM_DEV(LAKESHORE, 465),
    USLCOM_DEV(LAKESHORE, 475A),
    USLCOM_DEV(LAKESHORE, 625A),
    USLCOM_DEV(LAKESHORE, 642A),
    USLCOM_DEV(LAKESHORE, 648),
    USLCOM_DEV(LAKESHORE, 737),
    USLCOM_DEV(LAKESHORE, 776),
    USLCOM_DEV(LINKINSTRUMENTS, MSO19),
    USLCOM_DEV(LINKINSTRUMENTS, MSO28),
    USLCOM_DEV(LINKINSTRUMENTS, MSO28_2),
    USLCOM_DEV(MEI, CASHFLOW_SC),
    USLCOM_DEV(MEI, S2000),
    USLCOM_DEV(NETGEAR, M4100),
    USLCOM_DEV(OWEN, AC4),
    USLCOM_DEV(OWL, CM_160),
    USLCOM_DEV(PHILIPS, ACE1001),
    USLCOM_DEV(PLX, CA42),
    USLCOM_DEV(RENESAS, RX610),
    USLCOM_DEV(SEL, C662),
    USLCOM_DEV(SILABS, AC_SERV_CAN),
    USLCOM_DEV(SILABS, AC_SERV_CIS),
    USLCOM_DEV(SILABS, AC_SERV_IBUS),
    USLCOM_DEV(SILABS, AC_SERV_OBD),
    USLCOM_DEV(SILABS, AEROCOMM),
    USLCOM_DEV(SILABS, AMBER_AMB2560),
    USLCOM_DEV(SILABS, ARGUSISP),
    USLCOM_DEV(SILABS, ARKHAM_DS101_A),
    USLCOM_DEV(SILABS, ARKHAM_DS101_M),
    USLCOM_DEV(SILABS, ARYGON_MIFARE),
    USLCOM_DEV(SILABS, AVIT_USB_TTL),
    USLCOM_DEV(SILABS, B_G_H3000),
    USLCOM_DEV(SILABS, BALLUFF_RFID),
    USLCOM_DEV(SILABS, BEI_VCP),
    USLCOM_DEV(SILABS, BSM7DUSB),
    USLCOM_DEV(SILABS, BURNSIDE),
    USLCOM_DEV(SILABS, C2_EDGE_MODEM),
    USLCOM_DEV(SILABS, CP2102),
    USLCOM_DEV(SILABS, CP210X_2),
    USLCOM_DEV(SILABS, CP210X_3),
    USLCOM_DEV(SILABS, CP210X_4),
    USLCOM_DEV(SILABS, CRUMB128),
    USLCOM_DEV(SILABS, CYGNAL),
    USLCOM_DEV(SILABS, CYGNAL_DEBUG),
    USLCOM_DEV(SILABS, CYGNAL_GPS),
    USLCOM_DEV(SILABS, DEGREE),
    USLCOM_DEV(SILABS, DEKTEK_DTAPLUS),
    USLCOM_DEV(SILABS, EMS_C1007),
    USLCOM_DEV(SILABS, HAMLINKUSB),
    USLCOM_DEV(SILABS, HELICOM),
    USLCOM_DEV(SILABS, HUBZ),
    USLCOM_DEV(SILABS, IMS_USB_RS422),
    USLCOM_DEV(SILABS, INFINITY_MIC),
    USLCOM_DEV(SILABS, INGENI_ZIGBEE),
    USLCOM_DEV(SILABS, INSYS_MODEM),
    USLCOM_DEV(SILABS, IRZ_SG10),
    USLCOM_DEV(SILABS, KYOCERA_GPS),
    USLCOM_DEV(SILABS, LIPOWSKY_HARP),
    USLCOM_DEV(SILABS, LIPOWSKY_JTAG),
    USLCOM_DEV(SILABS, LIPOWSKY_LIN),
    USLCOM_DEV(SILABS, MC35PU),
    USLCOM_DEV(SILABS, MMB_ZIGBEE),
    USLCOM_DEV(SILABS, MJS_TOSLINK),
    USLCOM_DEV(SILABS, MSD_DASHHAWK),
    USLCOM_DEV(SILABS, MULTIPLEX_RC),
    USLCOM_DEV(SILABS, OPTRIS_MSPRO),
    USLCOM_DEV(SILABS, POLOLU),
    USLCOM_DEV(SILABS, PROCYON_AVS),
    USLCOM_DEV(SILABS, SB_PARAMOUNT_ME),
    USLCOM_DEV(SILABS, SUUNTO),
    USLCOM_DEV(SILABS, TAMSMASTER),
    USLCOM_DEV(SILABS, TELEGESIS_ETRX2),
    USLCOM_DEV(SILABS, TRACIENT),
    USLCOM_DEV(SILABS, TRAQMATE),
    USLCOM_DEV(SILABS, USBCOUNT50),
    USLCOM_DEV(SILABS, USBPULSE100),
    USLCOM_DEV(SILABS, USBSCOPE50),
    USLCOM_DEV(SILABS, USBWAVE12),
    USLCOM_DEV(SILABS, V_PREON32),
    USLCOM_DEV(SILABS, VSTABI),
    USLCOM_DEV(SILABS, WAVIT),
    USLCOM_DEV(SILABS, WMRBATT),
    USLCOM_DEV(SILABS, WMRRIGBLASTER),
    USLCOM_DEV(SILABS, WMRRIGTALK),
    USLCOM_DEV(SILABS, ZEPHYR_BIO),
    USLCOM_DEV(SILABS2, DCU11CLONE),
    USLCOM_DEV(SILABS3, GPRS_MODEM),
    USLCOM_DEV(SILABS4, 100EU_MODEM),
    USLCOM_DEV(SYNTECH, CYPHERLAB100),
    USLCOM_DEV(USI, MC60),
    USLCOM_DEV(VAISALA, CABLE),
    USLCOM_DEV(WAGO, SERVICECABLE),
    USLCOM_DEV(WAVESENSE, JAZZ),
    USLCOM_DEV(WESTMOUNTAIN, RIGBLASTER_ADVANTAGE),
    USLCOM_DEV(WIENERPLEINBAUS, PL512),
    USLCOM_DEV(WIENERPLEINBAUS, RCM),
    USLCOM_DEV(WIENERPLEINBAUS, MPOD),
    USLCOM_DEV(WIENERPLEINBAUS, CML),
#undef USLCOM_DEV
};

static device_method_t uslcom_methods[] = {
	DEVMETHOD(device_probe, uslcom_probe),
	DEVMETHOD(device_attach, uslcom_attach),
	DEVMETHOD(device_detach, uslcom_detach),
	DEVMETHOD_END
};

static devclass_t uslcom_devclass;

static driver_t uslcom_driver = {
	.name = "uslcom",
	.methods = uslcom_methods,
	.size = sizeof(struct uslcom_softc),
};

DRIVER_MODULE(uslcom, uhub, uslcom_driver, uslcom_devclass, NULL, 0);
MODULE_DEPEND(uslcom, ucom, 1, 1, 1);
MODULE_DEPEND(uslcom, usb, 1, 1, 1);
MODULE_VERSION(uslcom, 1);
USB_PNP_HOST_INFO(uslcom_devs);

static void
uslcom_watchdog(void *arg)
{
	struct uslcom_softc *sc = arg;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	usbd_transfer_start(sc->sc_xfer[USLCOM_CTRL_DT_RD]);

	usb_callout_reset(&sc->sc_watchdog,
	    hz / 4, &uslcom_watchdog, sc);
}

static int
uslcom_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != USLCOM_CONFIG_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(uslcom_devs, sizeof(uslcom_devs), uaa));
}

static int
uslcom_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uslcom_softc *sc = device_get_softc(dev);
	int error;

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uslcom", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);
	usb_callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0);

	sc->sc_udev = uaa->device;
	/* use the interface number from the USB interface descriptor */
	sc->sc_iface_no = uaa->info.bIfaceNum;

	error = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, uslcom_config,
	    USLCOM_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		DPRINTF("one or more missing USB endpoints, "
		    "error=%s\n", usbd_errstr(error));
		goto detach;
	}
	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[USLCOM_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[USLCOM_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	sc->sc_partnum = uslcom_get_partnum(sc);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &uslcom_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	uslcom_detach(dev);
	return (ENXIO);
}

static int
uslcom_detach(device_t dev)
{
	struct uslcom_softc *sc = device_get_softc(dev);

	DPRINTF("sc=%p\n", sc);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, USLCOM_N_TRANSFER);

	usb_callout_drain(&sc->sc_watchdog);

	device_claim_softc(dev);

	uslcom_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(uslcom);

static void
uslcom_free_softc(struct uslcom_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
uslcom_free(struct ucom_softc *ucom)
{
	uslcom_free_softc(ucom->sc_parent);
}

static void
uslcom_cfg_open(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_IFC_ENABLE;
	USETW(req.wValue, USLCOM_IFC_ENABLE_EN);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

        if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("UART enable failed (ignored)\n");
	}

	/* start polling status */
	uslcom_watchdog(sc);
}

static void
uslcom_cfg_close(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	/* stop polling status */
	usb_callout_stop(&sc->sc_watchdog);

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_IFC_ENABLE;
	USETW(req.wValue, USLCOM_IFC_ENABLE_DIS);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("UART disable failed (ignored)\n");
	}
}

static uint8_t
uslcom_get_partnum(struct uslcom_softc *sc)
{
	struct usb_device_request req;
	uint8_t partnum;

	/* Find specific chip type */
	partnum = 0;
	req.bmRequestType = USLCOM_READ;
	req.bRequest = USLCOM_VENDOR_SPECIFIC;
	USETW(req.wValue, USLCOM_GET_PARTNUM);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, sizeof(partnum));

	if (usbd_do_request_flags(sc->sc_udev, NULL,
	    &req, &partnum, 0, NULL, 1000)) {
		DPRINTF("GET_PARTNUM failed\n");
	}

	return(partnum);
}

static void
uslcom_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t ctl;

	DPRINTF("onoff = %d\n", onoff);

	ctl = onoff ? USLCOM_MHS_DTR_ON : 0;
	ctl |= USLCOM_MHS_DTR_SET;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_MHS;
	USETW(req.wValue, ctl);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("Setting DTR failed (ignored)\n");
	}
}

static void
uslcom_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t ctl;

	DPRINTF("onoff = %d\n", onoff);

	ctl = onoff ? USLCOM_MHS_RTS_ON : 0;
	ctl |= USLCOM_MHS_RTS_SET;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_MHS;
	USETW(req.wValue, ctl);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("Setting DTR failed (ignored)\n");
	}
}

static int
uslcom_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	uint32_t maxspeed;

	switch (sc->sc_partnum) {
	case USLCOM_PARTNUM_CP2104:
	case USLCOM_PARTNUM_CP2105:
		maxspeed = 2000000;
		break;
	case USLCOM_PARTNUM_CP2101:
	case USLCOM_PARTNUM_CP2102:
	case USLCOM_PARTNUM_CP2103:
	default:
		maxspeed = 921600;
		break;
	}
	if (t->c_ospeed <= 0 || t->c_ospeed > maxspeed)
		return (EINVAL);
	return (0);
}

static void
uslcom_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint32_t baudrate, flowctrl[4];
	uint16_t data;

	DPRINTF("\n");

	baudrate = t->c_ospeed;
	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_BAUDRATE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, sizeof(baudrate));

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, &baudrate, 0, 1000)) {
		printf("Set baudrate failed (ignored)\n");
	}

	if (t->c_cflag & CSTOPB)
		data = USLCOM_STOP_BITS_2;
	else
		data = USLCOM_STOP_BITS_1;
	if (t->c_cflag & PARENB) {
		if (t->c_cflag & PARODD)
			data |= USLCOM_PARITY_ODD;
		else
			data |= USLCOM_PARITY_EVEN;
	} else
		data |= USLCOM_PARITY_NONE;
	switch (t->c_cflag & CSIZE) {
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
	req.bRequest = USLCOM_SET_LINE_CTL;
	USETW(req.wValue, data);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("Set format failed (ignored)\n");
	}

	if (t->c_cflag & CRTSCTS) {
		flowctrl[0] = htole32(USLCOM_FLOW_DTR_ON | USLCOM_FLOW_CTS_HS);
		flowctrl[1] = htole32(USLCOM_FLOW_RTS_HS);
	} else {
		flowctrl[0] = htole32(USLCOM_FLOW_DTR_ON);
		flowctrl[1] = htole32(USLCOM_FLOW_RTS_ON);
	}
	flowctrl[2] = 0;
	flowctrl[3] = 0;
	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_FLOW;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, sizeof(flowctrl));

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, flowctrl, 0, 1000)) {
		DPRINTF("Set flowcontrol failed (ignored)\n");
	}
}

static void
uslcom_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct uslcom_softc *sc = ucom->sc_parent;

	DPRINTF("\n");

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
uslcom_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t brk = onoff ? USLCOM_SET_BREAK_ON : USLCOM_SET_BREAK_OFF;

	req.bmRequestType = USLCOM_WRITE;
	req.bRequest = USLCOM_SET_BREAK;
	USETW(req.wValue, brk);
	USETW(req.wIndex, sc->sc_iface_no);
	USETW(req.wLength, 0);

	if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, NULL, 0, 1000)) {
		DPRINTF("Set BREAK failed (ignored)\n");
	}
}

static int
uslcom_ioctl(struct ucom_softc *ucom, uint32_t cmd, caddr_t data,
    int flag, struct thread *td)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	int error = 0;
	uint8_t latch;

	DPRINTF("cmd=0x%08x\n", cmd);

	switch (cmd) {
	case USB_GET_GPIO:
		if (sc->sc_partnum < USLCOM_PARTNUM_CP2103) {
			error = ENODEV;
			break;
		}
		req.bmRequestType = USLCOM_READ;
		req.bRequest = USLCOM_VENDOR_SPECIFIC;
		USETW(req.wValue, USLCOM_READ_LATCH);
		USETW(req.wIndex, sc->sc_iface_no);
		USETW(req.wLength, sizeof(latch));

		if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
		    &req, &latch, 0, 1000)) {
			DPRINTF("Get LATCH failed\n");
			error = EIO;
		}
		*(int *)data = latch;
		break;

	case USB_SET_GPIO:
		if (sc->sc_partnum < USLCOM_PARTNUM_CP2103)
			error = ENODEV;
		else if ((sc->sc_partnum == USLCOM_PARTNUM_CP2103) ||
		    (sc->sc_partnum == USLCOM_PARTNUM_CP2104)) {
			req.bmRequestType = USLCOM_WRITE;
			req.bRequest = USLCOM_VENDOR_SPECIFIC;
			USETW(req.wValue, USLCOM_WRITE_LATCH);
			USETW(req.wIndex, (*(int *)data));
			USETW(req.wLength, 0);

			if (ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
			    &req, NULL, 0, 1000)) {
				DPRINTF("Set LATCH failed\n");
				error = EIO;
			}
		} else
			error = ENODEV;	/* Not yet */
		break;

	default:
		DPRINTF("Unknown IOCTL\n");
		error = ENOIOCTL;
		break;
	}
	return (error);
}

static void
uslcom_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uslcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    USLCOM_BULK_BUF_SIZE, &actlen)) {

			DPRINTF("actlen = %d\n", actlen);

			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
uslcom_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uslcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(&sc->sc_ucom, pc, 0, actlen);

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
uslcom_control_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uslcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	struct usb_device_request req;
	uint8_t msr = 0;
	uint8_t buf;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 1);
		usbd_copy_out(pc, 0, &buf, sizeof(buf));
		if (buf & USLCOM_MHS_CTS)
			msr |= SER_CTS;
		if (buf & USLCOM_MHS_DSR)
			msr |= SER_DSR;
		if (buf & USLCOM_MHS_RI)
			msr |= SER_RI;
		if (buf & USLCOM_MHS_DCD)
			msr |= SER_DCD;

		if (msr != sc->sc_msr) {
			DPRINTF("status change msr=0x%02x "
			    "(was 0x%02x)\n", msr, sc->sc_msr);
			sc->sc_msr = msr;
			ucom_status_change(&sc->sc_ucom);
		}
		break;

	case USB_ST_SETUP:
		req.bmRequestType = USLCOM_READ;
		req.bRequest = USLCOM_GET_MDMSTS;
		USETW(req.wValue, 0);
		USETW(req.wIndex, sc->sc_iface_no);
		USETW(req.wLength, sizeof(buf));

		usbd_xfer_set_frames(xfer, 2);
		usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
		usbd_xfer_set_frame_len(xfer, 1, sizeof(buf));

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));
		usbd_transfer_submit(xfer);
		break;

	default:		/* error */
		if (error != USB_ERR_CANCELLED)
			DPRINTF("error=%s\n", usbd_errstr(error));
		break;
	}
}

static void
uslcom_start_read(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[USLCOM_BULK_DT_RD]);
}

static void
uslcom_stop_read(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[USLCOM_BULK_DT_RD]);
}

static void
uslcom_start_write(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[USLCOM_BULK_DT_WR]);
}

static void
uslcom_stop_write(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[USLCOM_BULK_DT_WR]);
}

static void
uslcom_poll(struct ucom_softc *ucom)
{
	struct uslcom_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, USLCOM_N_TRANSFER);
}
