/*	$NetBSD: if_cdce.c,v 1.4 2004/10/24 12:50:54 augustss Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000-2003 Bill Paul <wpaul@windriver.com>
 * Copyright (c) 2003-2005 Craig Boston
 * Copyright (c) 2004 Daniel Hartmeier
 * Copyright (c) 2009 Hans Petter Selasky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul, THE VOICES IN HIS HEAD OR
 * THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB Communication Device Class (Ethernet Networking Control Model)
 * http://www.usb.org/developers/devclass_docs/usbcdc11.pdf
 */

/*
 * USB Network Control Model (NCM)
 * http://www.usb.org/developers/devclass_docs/NCM10.zip
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/socket.h>
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

#include <net/if.h>
#include <net/if_var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR cdce_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_msctest.h>
#include "usb_if.h"

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_cdcereg.h>

static device_probe_t cdce_probe;
static device_attach_t cdce_attach;
static device_detach_t cdce_detach;
static device_suspend_t cdce_suspend;
static device_resume_t cdce_resume;
static usb_handle_request_t cdce_handle_request;

static usb_callback_t cdce_bulk_write_callback;
static usb_callback_t cdce_bulk_read_callback;
static usb_callback_t cdce_intr_read_callback;
static usb_callback_t cdce_intr_write_callback;

#if CDCE_HAVE_NCM
static usb_callback_t cdce_ncm_bulk_write_callback;
static usb_callback_t cdce_ncm_bulk_read_callback;
#endif

static uether_fn_t cdce_attach_post;
static uether_fn_t cdce_init;
static uether_fn_t cdce_stop;
static uether_fn_t cdce_start;
static uether_fn_t cdce_setmulti;
static uether_fn_t cdce_setpromisc;

static uint32_t	cdce_m_crc32(struct mbuf *, uint32_t, uint32_t);

#ifdef USB_DEBUG
static int cdce_debug = 0;
static int cdce_tx_interval = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, cdce, CTLFLAG_RW, 0, "USB CDC-Ethernet");
SYSCTL_INT(_hw_usb_cdce, OID_AUTO, debug, CTLFLAG_RWTUN, &cdce_debug, 0,
    "Debug level");
SYSCTL_INT(_hw_usb_cdce, OID_AUTO, interval, CTLFLAG_RWTUN, &cdce_tx_interval, 0,
    "NCM transmit interval in ms");
#endif

static const struct usb_config cdce_config[CDCE_N_TRANSFER] = {

	[CDCE_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 0,
		.frames = CDCE_FRAMES_MAX,
		.bufsize = (CDCE_FRAMES_MAX * MCLBYTES),
		.flags = {.pipe_bof = 1,.short_frames_ok = 1,.short_xfer_ok = 1,.ext_buffer = 1,},
		.callback = cdce_bulk_read_callback,
		.timeout = 0,	/* no timeout */
		.usb_mode = USB_MODE_DUAL,	/* both modes */
	},

	[CDCE_BULK_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 0,
		.frames = CDCE_FRAMES_MAX,
		.bufsize = (CDCE_FRAMES_MAX * MCLBYTES),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.ext_buffer = 1,},
		.callback = cdce_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
		.usb_mode = USB_MODE_DUAL,	/* both modes */
	},

	[CDCE_INTR_RX] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 1,
		.bufsize = CDCE_IND_SIZE_MAX,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.callback = cdce_intr_read_callback,
		.timeout = 0,
		.usb_mode = USB_MODE_HOST,
	},

	[CDCE_INTR_TX] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 1,
		.bufsize = CDCE_IND_SIZE_MAX,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.no_pipe_ok = 1,},
		.callback = cdce_intr_write_callback,
		.timeout = 10000,	/* 10 seconds */
		.usb_mode = USB_MODE_DEVICE,
	},
};

#if CDCE_HAVE_NCM
static const struct usb_config cdce_ncm_config[CDCE_N_TRANSFER] = {

	[CDCE_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 0,
		.frames = CDCE_NCM_RX_FRAMES_MAX,
		.bufsize = (CDCE_NCM_RX_FRAMES_MAX * CDCE_NCM_RX_MAXLEN),
		.flags = {.pipe_bof = 1,.short_frames_ok = 1,.short_xfer_ok = 1,},
		.callback = cdce_ncm_bulk_read_callback,
		.timeout = 0,	/* no timeout */
		.usb_mode = USB_MODE_DUAL,	/* both modes */
	},

	[CDCE_BULK_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 0,
		.frames = CDCE_NCM_TX_FRAMES_MAX,
		.bufsize = (CDCE_NCM_TX_FRAMES_MAX * CDCE_NCM_TX_MAXLEN),
		.flags = {.pipe_bof = 1,},
		.callback = cdce_ncm_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
		.usb_mode = USB_MODE_DUAL,	/* both modes */
	},

	[CDCE_INTR_RX] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 1,
		.bufsize = CDCE_IND_SIZE_MAX,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.callback = cdce_intr_read_callback,
		.timeout = 0,
		.usb_mode = USB_MODE_HOST,
	},

	[CDCE_INTR_TX] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 1,
		.bufsize = CDCE_IND_SIZE_MAX,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.no_pipe_ok = 1,},
		.callback = cdce_intr_write_callback,
		.timeout = 10000,	/* 10 seconds */
		.usb_mode = USB_MODE_DEVICE,
	},
};
#endif

static device_method_t cdce_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, cdce_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, cdce_probe),
	DEVMETHOD(device_attach, cdce_attach),
	DEVMETHOD(device_detach, cdce_detach),
	DEVMETHOD(device_suspend, cdce_suspend),
	DEVMETHOD(device_resume, cdce_resume),

	DEVMETHOD_END
};

static driver_t cdce_driver = {
	.name = "cdce",
	.methods = cdce_methods,
	.size = sizeof(struct cdce_softc),
};

static devclass_t cdce_devclass;
static eventhandler_tag cdce_etag;

static int  cdce_driver_loaded(struct module *, int, void *);

static const STRUCT_USB_HOST_ID cdce_switch_devs[] = {
	{USB_VPI(USB_VENDOR_HUAWEI, USB_PRODUCT_HUAWEI_E3272_INIT, MSC_EJECT_HUAWEI2)},
};

static const STRUCT_USB_HOST_ID cdce_host_devs[] = {
	{USB_VPI(USB_VENDOR_ACERLABS, USB_PRODUCT_ACERLABS_M5632, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_AMBIT, USB_PRODUCT_AMBIT_NTL_250, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_IPAQLINUX, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_GMATE, USB_PRODUCT_GMATE_YP3X00, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_MOTOROLA2, USB_PRODUCT_MOTOROLA2_USBLAN, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_MOTOROLA2, USB_PRODUCT_MOTOROLA2_USBLAN2, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_NETCHIP, USB_PRODUCT_NETCHIP_ETHERNETGADGET, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2501, CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SL5500, CDCE_FLAG_ZAURUS)},
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SL5600, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SLA300, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SLC700, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},
	{USB_VPI(USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SLC750, CDCE_FLAG_ZAURUS | CDCE_FLAG_NO_UNION)},

	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x16),
		USB_DRIVER_INFO(0)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x46),
		USB_DRIVER_INFO(0)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x76),
		USB_DRIVER_INFO(0)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x03), USB_IFACE_PROTOCOL(0x16),
		USB_DRIVER_INFO(0)},
};

static const STRUCT_USB_DUAL_ID cdce_dual_devs[] = {
	{USB_IF_CSI(UICLASS_CDC, UISUBCLASS_ETHERNET_NETWORKING_CONTROL_MODEL, 0)},
	{USB_IF_CSI(UICLASS_CDC, UISUBCLASS_MOBILE_DIRECT_LINE_MODEL, 0)},
	{USB_IF_CSI(UICLASS_CDC, UISUBCLASS_NETWORK_CONTROL_MODEL, 0)},
};

DRIVER_MODULE(cdce, uhub, cdce_driver, cdce_devclass, cdce_driver_loaded, 0);
MODULE_VERSION(cdce, 1);
MODULE_DEPEND(cdce, uether, 1, 1, 1);
MODULE_DEPEND(cdce, usb, 1, 1, 1);
MODULE_DEPEND(cdce, ether, 1, 1, 1);
USB_PNP_DEVICE_INFO(cdce_switch_devs);
USB_PNP_HOST_INFO(cdce_host_devs);
USB_PNP_DUAL_INFO(cdce_dual_devs);

static const struct usb_ether_methods cdce_ue_methods = {
	.ue_attach_post = cdce_attach_post,
	.ue_start = cdce_start,
	.ue_init = cdce_init,
	.ue_stop = cdce_stop,
	.ue_setmulti = cdce_setmulti,
	.ue_setpromisc = cdce_setpromisc,
};

#if CDCE_HAVE_NCM
/*------------------------------------------------------------------------*
 *	cdce_ncm_init
 *
 * Return values:
 * 0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static uint8_t
cdce_ncm_init(struct cdce_softc *sc)
{
	struct usb_ncm_parameters temp;
	struct usb_device_request req;
	struct usb_ncm_func_descriptor *ufd;
	uint8_t value[8];
	int err;

	ufd = usbd_find_descriptor(sc->sc_ue.ue_udev, NULL,
	    sc->sc_ifaces_index[1], UDESC_CS_INTERFACE, 0xFF,
	    UCDC_NCM_FUNC_DESC_SUBTYPE, 0xFF);

	/* verify length of NCM functional descriptor */
	if (ufd != NULL) {
		if (ufd->bLength < sizeof(*ufd))
			ufd = NULL;
		else
			DPRINTFN(1, "Found NCM functional descriptor.\n");
	}

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UCDC_NCM_GET_NTB_PARAMETERS;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_ifaces_index[1];
	req.wIndex[1] = 0;
	USETW(req.wLength, sizeof(temp));

	err = usbd_do_request_flags(sc->sc_ue.ue_udev, NULL, &req,
	    &temp, 0, NULL, 1000 /* ms */);
	if (err)
		return (1);

	/* Read correct set of parameters according to device mode */

	if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
		sc->sc_ncm.rx_max = UGETDW(temp.dwNtbInMaxSize);
		sc->sc_ncm.tx_max = UGETDW(temp.dwNtbOutMaxSize);
		sc->sc_ncm.tx_remainder = UGETW(temp.wNdpOutPayloadRemainder);
		sc->sc_ncm.tx_modulus = UGETW(temp.wNdpOutDivisor);
		sc->sc_ncm.tx_struct_align = UGETW(temp.wNdpOutAlignment);
		sc->sc_ncm.tx_nframe = UGETW(temp.wNtbOutMaxDatagrams);
	} else {
		sc->sc_ncm.rx_max = UGETDW(temp.dwNtbOutMaxSize);
		sc->sc_ncm.tx_max = UGETDW(temp.dwNtbInMaxSize);
		sc->sc_ncm.tx_remainder = UGETW(temp.wNdpInPayloadRemainder);
		sc->sc_ncm.tx_modulus = UGETW(temp.wNdpInDivisor);
		sc->sc_ncm.tx_struct_align = UGETW(temp.wNdpInAlignment);
		sc->sc_ncm.tx_nframe = UGETW(temp.wNtbOutMaxDatagrams);
	}

	/* Verify maximum receive length */

	if ((sc->sc_ncm.rx_max < 32) || 
	    (sc->sc_ncm.rx_max > CDCE_NCM_RX_MAXLEN)) {
		DPRINTFN(1, "Using default maximum receive length\n");
		sc->sc_ncm.rx_max = CDCE_NCM_RX_MAXLEN;
	}

	/* Verify maximum transmit length */

	if ((sc->sc_ncm.tx_max < 32) ||
	    (sc->sc_ncm.tx_max > CDCE_NCM_TX_MAXLEN)) {
		DPRINTFN(1, "Using default maximum transmit length\n");
		sc->sc_ncm.tx_max = CDCE_NCM_TX_MAXLEN;
	}

	/* 
	 * Verify that the structure alignment is:
	 * - power of two
	 * - not greater than the maximum transmit length
	 * - not less than four bytes
	 */
	if ((sc->sc_ncm.tx_struct_align < 4) ||
	    (sc->sc_ncm.tx_struct_align != 
	     ((-sc->sc_ncm.tx_struct_align) & sc->sc_ncm.tx_struct_align)) ||
	    (sc->sc_ncm.tx_struct_align >= sc->sc_ncm.tx_max)) {
		DPRINTFN(1, "Using default other alignment: 4 bytes\n");
		sc->sc_ncm.tx_struct_align = 4;
	}

	/* 
	 * Verify that the payload alignment is:
	 * - power of two
	 * - not greater than the maximum transmit length
	 * - not less than four bytes
	 */
	if ((sc->sc_ncm.tx_modulus < 4) ||
	    (sc->sc_ncm.tx_modulus !=
	     ((-sc->sc_ncm.tx_modulus) & sc->sc_ncm.tx_modulus)) ||
	    (sc->sc_ncm.tx_modulus >= sc->sc_ncm.tx_max)) {
		DPRINTFN(1, "Using default transmit modulus: 4 bytes\n");
		sc->sc_ncm.tx_modulus = 4;
	}

	/* Verify that the payload remainder */

	if ((sc->sc_ncm.tx_remainder >= sc->sc_ncm.tx_modulus)) {
		DPRINTFN(1, "Using default transmit remainder: 0 bytes\n");
		sc->sc_ncm.tx_remainder = 0;
	}

	/*
	 * Offset the TX remainder so that IP packet payload starts at
	 * the tx_modulus. This is not too clear in the specification.
	 */

	sc->sc_ncm.tx_remainder = 
	    (sc->sc_ncm.tx_remainder - ETHER_HDR_LEN) &
	    (sc->sc_ncm.tx_modulus - 1);

	/* Verify max datagrams */

	if (sc->sc_ncm.tx_nframe == 0 ||
	    sc->sc_ncm.tx_nframe > (CDCE_NCM_SUBFRAMES_MAX - 1)) {
		DPRINTFN(1, "Using default max "
		    "subframes: %u units\n", CDCE_NCM_SUBFRAMES_MAX - 1);
		/* need to reserve one entry for zero padding */
		sc->sc_ncm.tx_nframe = (CDCE_NCM_SUBFRAMES_MAX - 1);
	}

	/* Additional configuration, will fail in device side mode, which is OK. */

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_NCM_SET_NTB_INPUT_SIZE;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_ifaces_index[1];
	req.wIndex[1] = 0;

	if (ufd != NULL &&
	    (ufd->bmNetworkCapabilities & UCDC_NCM_CAP_MAX_DGRAM)) {
		USETW(req.wLength, 8);
		USETDW(value, sc->sc_ncm.rx_max);
		USETW(value + 4, (CDCE_NCM_SUBFRAMES_MAX - 1));
		USETW(value + 6, 0);
	} else {
		USETW(req.wLength, 4);
		USETDW(value, sc->sc_ncm.rx_max);
 	}

	err = usbd_do_request_flags(sc->sc_ue.ue_udev, NULL, &req,
	    &value, 0, NULL, 1000 /* ms */);
	if (err) {
		DPRINTFN(1, "Setting input size "
		    "to %u failed.\n", sc->sc_ncm.rx_max);
	}

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_NCM_SET_CRC_MODE;
	USETW(req.wValue, 0);	/* no CRC */
	req.wIndex[0] = sc->sc_ifaces_index[1];
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	err = usbd_do_request_flags(sc->sc_ue.ue_udev, NULL, &req,
	    NULL, 0, NULL, 1000 /* ms */);
	if (err) {
		DPRINTFN(1, "Setting CRC mode to off failed.\n");
	}

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_NCM_SET_NTB_FORMAT;
	USETW(req.wValue, 0);	/* NTB-16 */
	req.wIndex[0] = sc->sc_ifaces_index[1];
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	err = usbd_do_request_flags(sc->sc_ue.ue_udev, NULL, &req,
	    NULL, 0, NULL, 1000 /* ms */);
	if (err) {
		DPRINTFN(1, "Setting NTB format to 16-bit failed.\n");
	}

	return (0);		/* success */
}
#endif

static void
cdce_test_autoinst(void *arg, struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;

	if (uaa->dev_state != UAA_DEV_READY)
		return;

	iface = usbd_get_iface(udev, 0);
	if (iface == NULL)
		return;
	id = iface->idesc;
	if (id == NULL || id->bInterfaceClass != UICLASS_MASS)
		return;
	if (usbd_lookup_id_by_uaa(cdce_switch_devs, sizeof(cdce_switch_devs), uaa))
		return;		/* no device match */

	if (usb_msc_eject(udev, 0, USB_GET_DRIVER_INFO(uaa)) == 0) {
		/* success, mark the udev as disappearing */
		uaa->dev_state = UAA_DEV_EJECTING;
	}
}

static int
cdce_driver_loaded(struct module *mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* register our autoinstall handler */
		cdce_etag = EVENTHANDLER_REGISTER(usb_dev_configured,
		    cdce_test_autoinst, NULL, EVENTHANDLER_PRI_ANY);
		return (0);
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(usb_dev_configured, cdce_etag);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static int
cdce_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	error = usbd_lookup_id_by_uaa(cdce_host_devs, sizeof(cdce_host_devs), uaa);
	if (error)
		error = usbd_lookup_id_by_uaa(cdce_dual_devs, sizeof(cdce_dual_devs), uaa);
	return (error);
}

static void
cdce_attach_post(struct usb_ether *ue)
{
	/* no-op */
	return;
}

static int
cdce_attach(device_t dev)
{
	struct cdce_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_interface *iface;
	const struct usb_cdc_union_descriptor *ud;
	const struct usb_interface_descriptor *id;
	const struct usb_cdc_ethernet_descriptor *ued;
	const struct usb_config *pcfg;
	uint32_t seed;
	int error;
	uint8_t i;
	uint8_t data_iface_no;
	char eaddr_str[5 * ETHER_ADDR_LEN];	/* approx */

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);
	sc->sc_ue.ue_udev = uaa->device;

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	ud = usbd_find_descriptor
	    (uaa->device, NULL, uaa->info.bIfaceIndex,
	    UDESC_CS_INTERFACE, 0xFF, UDESCSUB_CDC_UNION, 0xFF);

	if ((ud == NULL) || (ud->bLength < sizeof(*ud)) ||
	    (sc->sc_flags & CDCE_FLAG_NO_UNION)) {
		DPRINTFN(1, "No union descriptor!\n");
		sc->sc_ifaces_index[0] = uaa->info.bIfaceIndex;
		sc->sc_ifaces_index[1] = uaa->info.bIfaceIndex;
		goto alloc_transfers;
	}
	data_iface_no = ud->bSlaveInterface[0];

	for (i = 0;; i++) {

		iface = usbd_get_iface(uaa->device, i);

		if (iface) {

			id = usbd_get_interface_descriptor(iface);

			if (id && (id->bInterfaceNumber == data_iface_no)) {
				sc->sc_ifaces_index[0] = i;
				sc->sc_ifaces_index[1] = uaa->info.bIfaceIndex;
				usbd_set_parent_iface(uaa->device, i, uaa->info.bIfaceIndex);
				break;
			}
		} else {
			device_printf(dev, "no data interface found\n");
			goto detach;
		}
	}

	/*
	 * <quote>
	 *
	 *  The Data Class interface of a networking device shall have
	 *  a minimum of two interface settings. The first setting
	 *  (the default interface setting) includes no endpoints and
	 *  therefore no networking traffic is exchanged whenever the
	 *  default interface setting is selected. One or more
	 *  additional interface settings are used for normal
	 *  operation, and therefore each includes a pair of endpoints
	 *  (one IN, and one OUT) to exchange network traffic. Select
	 *  an alternate interface setting to initialize the network
	 *  aspects of the device and to enable the exchange of
	 *  network traffic.
	 *
	 * </quote>
	 *
	 * Some devices, most notably cable modems, include interface
	 * settings that have no IN or OUT endpoint, therefore loop
	 * through the list of all available interface settings
	 * looking for one with both IN and OUT endpoints.
	 */

alloc_transfers:

	pcfg = cdce_config;	/* Default Configuration */

	for (i = 0; i != 32; i++) {

		error = usbd_set_alt_interface_index(uaa->device,
		    sc->sc_ifaces_index[0], i);
		if (error)
			break;
#if CDCE_HAVE_NCM
		if ((i == 0) && (cdce_ncm_init(sc) == 0))
			pcfg = cdce_ncm_config;
#endif
		error = usbd_transfer_setup(uaa->device,
		    sc->sc_ifaces_index, sc->sc_xfer,
		    pcfg, CDCE_N_TRANSFER, sc, &sc->sc_mtx);

		if (error == 0)
			break;
	}

	if (error || (i == 32)) {
		device_printf(dev, "No valid alternate "
		    "setting found\n");
		goto detach;
	}

	ued = usbd_find_descriptor
	    (uaa->device, NULL, uaa->info.bIfaceIndex,
	    UDESC_CS_INTERFACE, 0xFF, UDESCSUB_CDC_ENF, 0xFF);

	if ((ued == NULL) || (ued->bLength < sizeof(*ued))) {
		error = USB_ERR_INVAL;
	} else {
		error = usbd_req_get_string_any(uaa->device, NULL, 
		    eaddr_str, sizeof(eaddr_str), ued->iMacAddress);
	}

	if (error) {

		/* fake MAC address */

		device_printf(dev, "faking MAC address\n");
		seed = ticks;
		sc->sc_ue.ue_eaddr[0] = 0x2a;
		memcpy(&sc->sc_ue.ue_eaddr[1], &seed, sizeof(uint32_t));
		sc->sc_ue.ue_eaddr[5] = device_get_unit(dev);

	} else {

		memset(sc->sc_ue.ue_eaddr, 0, sizeof(sc->sc_ue.ue_eaddr));

		for (i = 0; i != (ETHER_ADDR_LEN * 2); i++) {

			char c = eaddr_str[i];

			if ('0' <= c && c <= '9')
				c -= '0';
			else if (c != 0)
				c -= 'A' - 10;
			else
				break;

			c &= 0xf;

			if ((i & 1) == 0)
				c <<= 4;
			sc->sc_ue.ue_eaddr[i / 2] |= c;
		}

		if (uaa->usb_mode == USB_MODE_DEVICE) {
			/*
			 * Do not use the same MAC address like the peer !
			 */
			sc->sc_ue.ue_eaddr[5] ^= 0xFF;
		}
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &cdce_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	cdce_detach(dev);
	return (ENXIO);			/* failure */
}

static int
cdce_detach(device_t dev)
{
	struct cdce_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	/* stop all USB transfers first */
	usbd_transfer_unsetup(sc->sc_xfer, CDCE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
cdce_start(struct usb_ether *ue)
{
	struct cdce_softc *sc = uether_getsc(ue);

	/*
	 * Start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[CDCE_BULK_TX]);
	usbd_transfer_start(sc->sc_xfer[CDCE_BULK_RX]);
}

static void
cdce_free_queue(struct mbuf **ppm, uint8_t n)
{
	uint8_t x;
	for (x = 0; x != n; x++) {
		if (ppm[x] != NULL) {
			m_freem(ppm[x]);
			ppm[x] = NULL;
		}
	}
}

static void
cdce_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct mbuf *m;
	struct mbuf *mt;
	uint32_t crc;
	uint8_t x;
	int actlen, aframes;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTFN(1, "\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete: %u bytes in %u frames\n",
		    actlen, aframes);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* free all previous TX buffers */
		cdce_free_queue(sc->sc_tx_buf, CDCE_FRAMES_MAX);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		for (x = 0; x != CDCE_FRAMES_MAX; x++) {

			IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

			if (m == NULL)
				break;

			if (sc->sc_flags & CDCE_FLAG_ZAURUS) {
				/*
				 * Zaurus wants a 32-bit CRC appended
				 * to every frame
				 */

				crc = cdce_m_crc32(m, 0, m->m_pkthdr.len);
				crc = htole32(crc);

				if (!m_append(m, 4, (void *)&crc)) {
					m_freem(m);
					if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
					continue;
				}
			}
			if (m->m_len != m->m_pkthdr.len) {
				mt = m_defrag(m, M_NOWAIT);
				if (mt == NULL) {
					m_freem(m);
					if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
					continue;
				}
				m = mt;
			}
			if (m->m_pkthdr.len > MCLBYTES) {
				m->m_pkthdr.len = MCLBYTES;
			}
			sc->sc_tx_buf[x] = m;
			usbd_xfer_set_frame_data(xfer, x, m->m_data, m->m_len);

			/*
			 * If there's a BPF listener, bounce a copy of
			 * this frame to him:
			 */
			BPF_MTAP(ifp, m);
		}
		if (x != 0) {
			usbd_xfer_set_frames(xfer, x);

			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		/* free all previous TX buffers */
		cdce_free_queue(sc->sc_tx_buf, CDCE_FRAMES_MAX);

		/* count output errors */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
				/* try to clear stall first */
				usbd_xfer_set_stall(xfer);
			}
			goto tr_setup;
		}
		break;
	}
}

static int32_t
cdce_m_crc32_cb(void *arg, void *src, uint32_t count)
{
	uint32_t *p_crc = arg;

	*p_crc = crc32_raw(src, count, *p_crc);
	return (0);
}

static uint32_t
cdce_m_crc32(struct mbuf *m, uint32_t src_offset, uint32_t src_len)
{
	uint32_t crc = 0xFFFFFFFF;
	int error;

	error = m_apply(m, src_offset, src_len, cdce_m_crc32_cb, &crc);
	return (crc ^ 0xFFFFFFFF);
}

static void
cdce_init(struct usb_ether *ue)
{
	struct cdce_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	CDCE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* start interrupt transfer */
	usbd_transfer_start(sc->sc_xfer[CDCE_INTR_RX]);
	usbd_transfer_start(sc->sc_xfer[CDCE_INTR_TX]);

	/*
	 * Stall data write direction, which depends on USB mode.
	 *
	 * Some USB host stacks (e.g. Mac OS X) don't clears stall
	 * bit as it should, so set it in our host mode only.
	 */
	if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST)
		usbd_xfer_set_stall(sc->sc_xfer[CDCE_BULK_TX]);

	/* start data transfers */
	cdce_start(ue);
}

static void
cdce_stop(struct usb_ether *ue)
{
	struct cdce_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	CDCE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[CDCE_BULK_RX]);
	usbd_transfer_stop(sc->sc_xfer[CDCE_BULK_TX]);
	usbd_transfer_stop(sc->sc_xfer[CDCE_INTR_RX]);
	usbd_transfer_stop(sc->sc_xfer[CDCE_INTR_TX]);
}

static void
cdce_setmulti(struct usb_ether *ue)
{
	/* no-op */
	return;
}

static void
cdce_setpromisc(struct usb_ether *ue)
{
	/* no-op */
	return;
}

static int
cdce_suspend(device_t dev)
{
	device_printf(dev, "Suspending\n");
	return (0);
}

static int
cdce_resume(device_t dev)
{
	device_printf(dev, "Resuming\n");
	return (0);
}

static void
cdce_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct mbuf *m;
	uint8_t x;
	int actlen;
	int aframes;
	int len;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("received %u bytes in %u frames\n", actlen, aframes);

		for (x = 0; x != aframes; x++) {

			m = sc->sc_rx_buf[x];
			sc->sc_rx_buf[x] = NULL;
			len = usbd_xfer_frame_len(xfer, x);

			/* Strip off CRC added by Zaurus, if any */
			if ((sc->sc_flags & CDCE_FLAG_ZAURUS) && len >= 14)
				len -= 4;

			if (len < (int)sizeof(struct ether_header)) {
				m_freem(m);
				continue;
			}
			/* queue up mbuf */
			uether_rxmbuf(&sc->sc_ue, m, len);
		}

		/* FALLTHROUGH */
	case USB_ST_SETUP:
		/* 
		 * TODO: Implement support for multi frame transfers,
		 * when the USB hardware supports it.
		 */
		for (x = 0; x != 1; x++) {
			if (sc->sc_rx_buf[x] == NULL) {
				m = uether_newbuf();
				if (m == NULL)
					goto tr_stall;
				sc->sc_rx_buf[x] = m;
			} else {
				m = sc->sc_rx_buf[x];
			}

			usbd_xfer_set_frame_data(xfer, x, m->m_data, m->m_len);
		}
		/* set number of frames and start hardware */
		usbd_xfer_set_frames(xfer, x);
		usbd_transfer_submit(xfer);
		/* flush any received frames */
		uether_rxflush(&sc->sc_ue);
		break;

	default:			/* Error */
		DPRINTF("error = %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
tr_stall:
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
				/* try to clear stall first */
				usbd_xfer_set_stall(xfer);
				usbd_xfer_set_frames(xfer, 0);
				usbd_transfer_submit(xfer);
			}
			break;
		}

		/* need to free the RX-mbufs when we are cancelled */
		cdce_free_queue(sc->sc_rx_buf, CDCE_FRAMES_MAX);
		break;
	}
}

static void
cdce_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("Received %d bytes\n", actlen);

		/* TODO: decode some indications */

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* start clear stall */
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST)
				usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
cdce_intr_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct usb_cdc_notification req;
	struct usb_page_cache *pc;
	uint32_t speed;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("Transferred %d bytes\n", actlen);

		switch (sc->sc_notify_state) {
		case CDCE_NOTIFY_NETWORK_CONNECTION:
			sc->sc_notify_state = CDCE_NOTIFY_SPEED_CHANGE;
			break;
		case CDCE_NOTIFY_SPEED_CHANGE:
			sc->sc_notify_state = CDCE_NOTIFY_DONE;
			break;
		default:
			break;
		}

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		/*
		 * Inform host about connection. Required according to USB CDC
		 * specification and communicating to Mac OS X USB host stack.
		 * Some of the values seems ignored by Mac OS X though.
		 */
		if (sc->sc_notify_state == CDCE_NOTIFY_NETWORK_CONNECTION) {
			req.bmRequestType = UCDC_NOTIFICATION;
			req.bNotification = UCDC_N_NETWORK_CONNECTION;
			req.wIndex[0] = sc->sc_ifaces_index[1];
			req.wIndex[1] = 0;
			USETW(req.wValue, 1); /* Connected */
			USETW(req.wLength, 0);

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &req, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frames(xfer, 1);
			usbd_transfer_submit(xfer); 

		} else if (sc->sc_notify_state == CDCE_NOTIFY_SPEED_CHANGE) {
			req.bmRequestType = UCDC_NOTIFICATION;
			req.bNotification = UCDC_N_CONNECTION_SPEED_CHANGE;
			req.wIndex[0] = sc->sc_ifaces_index[1];
			req.wIndex[1] = 0;
			USETW(req.wValue, 0);
			USETW(req.wLength, 8);

			/* Peak theoretical bulk trasfer rate in bits/s */
			if (usbd_get_speed(sc->sc_ue.ue_udev) != USB_SPEED_FULL)
				speed = (13 * 512 * 8 * 1000 * 8);
			else
				speed = (19 * 64 * 1 * 1000 * 8);

			USETDW(req.data + 0, speed); /* Upstream bit rate */
			USETDW(req.data + 4, speed); /* Downstream bit rate */
 
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &req, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frames(xfer, 1);
			usbd_transfer_submit(xfer); 
		}
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
				/* start clear stall */
				usbd_xfer_set_stall(xfer);
			}
			goto tr_setup;
		}
		break;
	}
}

static int
cdce_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	struct cdce_softc *sc = device_get_softc(dev);
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	/*
	 * When Mac OS X resumes after suspending it expects
	 * to be notified again after this request.
	 */
	if (req->bmRequestType == UT_WRITE_CLASS_INTERFACE && \
	    req->bRequest == UCDC_NCM_SET_ETHERNET_PACKET_FILTER) {

		if (is_complete == 1) {
			mtx_lock(&sc->sc_mtx);
			sc->sc_notify_state = CDCE_NOTIFY_SPEED_CHANGE;
			usbd_transfer_start(sc->sc_xfer[CDCE_INTR_TX]);
			mtx_unlock(&sc->sc_mtx);
		}

		return (0);
	}

	return (ENXIO);			/* use builtin handler */
}

#if CDCE_HAVE_NCM
static void
cdce_ncm_tx_zero(struct usb_page_cache *pc,
    uint32_t start, uint32_t end)
{
	if (start >= CDCE_NCM_TX_MAXLEN)
		return;
	if (end > CDCE_NCM_TX_MAXLEN)
		end = CDCE_NCM_TX_MAXLEN;

	usbd_frame_zero(pc, start, end - start);
}

static uint8_t
cdce_ncm_fill_tx_frames(struct usb_xfer *xfer, uint8_t index)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc = usbd_xfer_get_frame(xfer, index);
	struct mbuf *m;
	uint32_t rem;
	uint32_t offset;
	uint32_t last_offset;
	uint16_t n;
	uint8_t retval;

	usbd_xfer_set_frame_offset(xfer, index * CDCE_NCM_TX_MAXLEN, index);

	offset = sizeof(sc->sc_ncm.hdr) +
	    sizeof(sc->sc_ncm.dpt) + sizeof(sc->sc_ncm.dp);

	/* Store last valid offset before alignment */
	last_offset = offset;

	/* Align offset */
	offset = CDCE_NCM_ALIGN(sc->sc_ncm.tx_remainder,
	    offset, sc->sc_ncm.tx_modulus);

	/* Zero pad */
	cdce_ncm_tx_zero(pc, last_offset, offset);

	/* buffer full */
	retval = 2;

	for (n = 0; n != sc->sc_ncm.tx_nframe; n++) {

		/* check if end of transmit buffer is reached */

		if (offset >= sc->sc_ncm.tx_max)
			break;

		/* compute maximum buffer size */

		rem = sc->sc_ncm.tx_max - offset;

		IFQ_DRV_DEQUEUE(&(ifp->if_snd), m);

		if (m == NULL) {
			/* buffer not full */
			retval = 1;
			break;
		}

		if (m->m_pkthdr.len > (int)rem) {
			if (n == 0) {
				/* The frame won't fit in our buffer */
				DPRINTFN(1, "Frame too big to be transmitted!\n");
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				n--;
				continue;
			}
			/* Wait till next buffer becomes ready */
			IFQ_DRV_PREPEND(&(ifp->if_snd), m);
			break;
		}
		usbd_m_copy_in(pc, offset, m, 0, m->m_pkthdr.len);

		USETW(sc->sc_ncm.dp[n].wFrameLength, m->m_pkthdr.len);
		USETW(sc->sc_ncm.dp[n].wFrameIndex, offset);

		/* Update offset */
		offset += m->m_pkthdr.len;

		/* Store last valid offset before alignment */
		last_offset = offset;

		/* Align offset */
		offset = CDCE_NCM_ALIGN(sc->sc_ncm.tx_remainder,
		    offset, sc->sc_ncm.tx_modulus);

		/* Zero pad */
		cdce_ncm_tx_zero(pc, last_offset, offset);

		/*
		 * If there's a BPF listener, bounce a copy
		 * of this frame to him:
		 */
		BPF_MTAP(ifp, m);

		/* Free mbuf */

		m_freem(m);

		/* Pre-increment interface counter */

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	}

	if (n == 0)
		return (0);

	rem = (sizeof(sc->sc_ncm.dpt) + (4 * n) + 4);

	USETW(sc->sc_ncm.dpt.wLength, rem);

	/* zero the rest of the data pointer entries */
	for (; n != CDCE_NCM_SUBFRAMES_MAX; n++) {
		USETW(sc->sc_ncm.dp[n].wFrameLength, 0);
		USETW(sc->sc_ncm.dp[n].wFrameIndex, 0);
	}

	offset = last_offset;

	/* Align offset */
	offset = CDCE_NCM_ALIGN(0, offset, CDCE_NCM_TX_MINLEN);

	/* Optimise, save bandwidth and force short termination */
	if (offset >= sc->sc_ncm.tx_max)
		offset = sc->sc_ncm.tx_max;
	else
		offset ++;

	/* Zero pad */
	cdce_ncm_tx_zero(pc, last_offset, offset);

	/* set frame length */
	usbd_xfer_set_frame_len(xfer, index, offset);

	/* Fill out 16-bit header */
	sc->sc_ncm.hdr.dwSignature[0] = 'N';
	sc->sc_ncm.hdr.dwSignature[1] = 'C';
	sc->sc_ncm.hdr.dwSignature[2] = 'M';
	sc->sc_ncm.hdr.dwSignature[3] = 'H';
	USETW(sc->sc_ncm.hdr.wHeaderLength, sizeof(sc->sc_ncm.hdr));
	USETW(sc->sc_ncm.hdr.wBlockLength, offset);
	USETW(sc->sc_ncm.hdr.wSequence, sc->sc_ncm.tx_seq);
	USETW(sc->sc_ncm.hdr.wDptIndex, sizeof(sc->sc_ncm.hdr));

	sc->sc_ncm.tx_seq++;

	/* Fill out 16-bit frame table header */
	sc->sc_ncm.dpt.dwSignature[0] = 'N';
	sc->sc_ncm.dpt.dwSignature[1] = 'C';
	sc->sc_ncm.dpt.dwSignature[2] = 'M';
	sc->sc_ncm.dpt.dwSignature[3] = '0';
	USETW(sc->sc_ncm.dpt.wNextNdpIndex, 0);		/* reserved */

	usbd_copy_in(pc, 0, &(sc->sc_ncm.hdr), sizeof(sc->sc_ncm.hdr));
	usbd_copy_in(pc, sizeof(sc->sc_ncm.hdr), &(sc->sc_ncm.dpt),
	    sizeof(sc->sc_ncm.dpt));
	usbd_copy_in(pc, sizeof(sc->sc_ncm.hdr) + sizeof(sc->sc_ncm.dpt),
	    &(sc->sc_ncm.dp), sizeof(sc->sc_ncm.dp));
	return (retval);
}

static void
cdce_ncm_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	uint16_t x;
	uint8_t temp;
	int actlen;
	int aframes;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

		DPRINTFN(10, "transfer complete: "
		    "%u bytes in %u frames\n", actlen, aframes);

	case USB_ST_SETUP:
		for (x = 0; x != CDCE_NCM_TX_FRAMES_MAX; x++) {
			temp = cdce_ncm_fill_tx_frames(xfer, x);
			if (temp == 0)
				break;
			if (temp == 1) {
				x++;
				break;
			}
		}

		if (x != 0) {
#ifdef USB_DEBUG
			usbd_xfer_set_interval(xfer, cdce_tx_interval);
#endif
			usbd_xfer_set_frames(xfer, x);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		DPRINTFN(10, "Transfer error: %s\n",
		    usbd_errstr(error));

		/* update error counter */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
				/* try to clear stall first */
				usbd_xfer_set_stall(xfer);
				usbd_xfer_set_frames(xfer, 0);
				usbd_transfer_submit(xfer);
			}
		}
		break;
	}
}

static void
cdce_ncm_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cdce_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc = usbd_xfer_get_frame(xfer, 0);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct mbuf *m;
	int sumdata;
	int sumlen;
	int actlen;
	int aframes;
	int temp;
	int nframes;
	int x;
	int offset;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		usbd_xfer_status(xfer, &actlen, &sumlen, &aframes, NULL);

		DPRINTFN(1, "received %u bytes in %u frames\n",
		    actlen, aframes);

		if (actlen < (int)(sizeof(sc->sc_ncm.hdr) +
		    sizeof(sc->sc_ncm.dpt))) {
			DPRINTFN(1, "frame too short\n");
			goto tr_setup;
		}
		usbd_copy_out(pc, 0, &(sc->sc_ncm.hdr),
		    sizeof(sc->sc_ncm.hdr));

		if ((sc->sc_ncm.hdr.dwSignature[0] != 'N') ||
		    (sc->sc_ncm.hdr.dwSignature[1] != 'C') ||
		    (sc->sc_ncm.hdr.dwSignature[2] != 'M') ||
		    (sc->sc_ncm.hdr.dwSignature[3] != 'H')) {
			DPRINTFN(1, "invalid HDR signature: "
			    "0x%02x:0x%02x:0x%02x:0x%02x\n",
			    sc->sc_ncm.hdr.dwSignature[0],
			    sc->sc_ncm.hdr.dwSignature[1],
			    sc->sc_ncm.hdr.dwSignature[2],
			    sc->sc_ncm.hdr.dwSignature[3]);
			goto tr_stall;
		}
		temp = UGETW(sc->sc_ncm.hdr.wBlockLength);
		if (temp > sumlen) {
			DPRINTFN(1, "unsupported block length %u/%u\n",
			    temp, sumlen);
			goto tr_stall;
		}
		temp = UGETW(sc->sc_ncm.hdr.wDptIndex);
		if ((int)(temp + sizeof(sc->sc_ncm.dpt)) > actlen) {
			DPRINTFN(1, "invalid DPT index: 0x%04x\n", temp);
			goto tr_stall;
		}
		usbd_copy_out(pc, temp, &(sc->sc_ncm.dpt),
		    sizeof(sc->sc_ncm.dpt));

		if ((sc->sc_ncm.dpt.dwSignature[0] != 'N') ||
		    (sc->sc_ncm.dpt.dwSignature[1] != 'C') ||
		    (sc->sc_ncm.dpt.dwSignature[2] != 'M') ||
		    (sc->sc_ncm.dpt.dwSignature[3] != '0')) {
			DPRINTFN(1, "invalid DPT signature"
			    "0x%02x:0x%02x:0x%02x:0x%02x\n",
			    sc->sc_ncm.dpt.dwSignature[0],
			    sc->sc_ncm.dpt.dwSignature[1],
			    sc->sc_ncm.dpt.dwSignature[2],
			    sc->sc_ncm.dpt.dwSignature[3]);
			goto tr_stall;
		}
		nframes = UGETW(sc->sc_ncm.dpt.wLength) / 4;

		/* Subtract size of header and last zero padded entry */
		if (nframes >= (2 + 1))
			nframes -= (2 + 1);
		else
			nframes = 0;

		DPRINTFN(1, "nframes = %u\n", nframes);

		temp += sizeof(sc->sc_ncm.dpt);

		if ((temp + (4 * nframes)) > actlen)
			goto tr_stall;

		if (nframes > CDCE_NCM_SUBFRAMES_MAX) {
			DPRINTFN(1, "Truncating number of frames from %u to %u\n",
			    nframes, CDCE_NCM_SUBFRAMES_MAX);
			nframes = CDCE_NCM_SUBFRAMES_MAX;
		}
		usbd_copy_out(pc, temp, &(sc->sc_ncm.dp), (4 * nframes));

		sumdata = 0;

		for (x = 0; x != nframes; x++) {

			offset = UGETW(sc->sc_ncm.dp[x].wFrameIndex);
			temp = UGETW(sc->sc_ncm.dp[x].wFrameLength);

			if ((offset == 0) ||
			    (temp < (int)sizeof(struct ether_header)) ||
			    (temp > (MCLBYTES - ETHER_ALIGN))) {
				DPRINTFN(1, "NULL frame detected at %d\n", x);
				m = NULL;
				/* silently ignore this frame */
				continue;
			} else if ((offset + temp) > actlen) {
				DPRINTFN(1, "invalid frame "
				    "detected at %d\n", x);
				m = NULL;
				/* silently ignore this frame */
				continue;
			} else if (temp > (int)(MHLEN - ETHER_ALIGN)) {
				m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			} else {
				m = m_gethdr(M_NOWAIT, MT_DATA);
			}

			DPRINTFN(16, "frame %u, offset = %u, length = %u \n",
			    x, offset, temp);

			/* check if we have a buffer */
			if (m) {
				m->m_len = m->m_pkthdr.len = temp + ETHER_ALIGN;
				m_adj(m, ETHER_ALIGN);

				usbd_copy_out(pc, offset, m->m_data, temp);

				/* enqueue */
				uether_rxmbuf(&sc->sc_ue, m, temp);

				sumdata += temp;
			} else {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			}
		}

		DPRINTFN(1, "Efficiency: %u/%u bytes\n", sumdata, actlen);

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, sc->sc_ncm.rx_max);
		usbd_xfer_set_frames(xfer, 1);
		usbd_transfer_submit(xfer);
		uether_rxflush(&sc->sc_ue);	/* must be last */
		break;

	default:			/* Error */
		DPRINTFN(1, "error = %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
tr_stall:
			if (usbd_get_mode(sc->sc_ue.ue_udev) == USB_MODE_HOST) {
				/* try to clear stall first */
				usbd_xfer_set_stall(xfer);
				usbd_xfer_set_frames(xfer, 0);
				usbd_transfer_submit(xfer);
			}
		}
		break;
	}
}
#endif
