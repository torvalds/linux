/*	$NetBSD: umodem.c,v 1.45 2002/09/23 05:51:23 simonb Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2003, M. Warner Losh <imp@FreeBSD.org>.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Comm Class spec:  http://www.usb.org/developers/devclass_docs/usbccs10.pdf
 *                   http://www.usb.org/developers/devclass_docs/usbcdc11.pdf
 *                   http://www.usb.org/developers/devclass_docs/cdc_wmc10.zip
 */

/*
 * TODO:
 * - Add error recovery in various places; the big problem is what
 *   to do in a callback if there is an error.
 * - Implement a Call Device for modems without multiplexed commands.
 *
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
#include <dev/usb/usbhid.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"
#include "usb_if.h"

#include <dev/usb/usb_ioctl.h>

#define	USB_DEBUG_VAR umodem_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/quirk/usb_quirk.h>

#include <dev/usb/serial/usb_serial.h>

#ifdef USB_DEBUG
static int umodem_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, umodem, CTLFLAG_RW, 0, "USB umodem");
SYSCTL_INT(_hw_usb_umodem, OID_AUTO, debug, CTLFLAG_RWTUN,
    &umodem_debug, 0, "Debug level");
#endif

static const STRUCT_USB_DUAL_ID umodem_dual_devs[] = {
	/* Generic Modem class match */
	{USB_IFACE_CLASS(UICLASS_CDC),
		USB_IFACE_SUBCLASS(UISUBCLASS_ABSTRACT_CONTROL_MODEL),
		USB_IFACE_PROTOCOL(UIPROTO_CDC_AT)},
	{USB_IFACE_CLASS(UICLASS_CDC),
		USB_IFACE_SUBCLASS(UISUBCLASS_ABSTRACT_CONTROL_MODEL),
		USB_IFACE_PROTOCOL(UIPROTO_CDC_NONE)},
};

static const STRUCT_USB_HOST_ID umodem_host_devs[] = {
	/* Huawei Modem class match */
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x01)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x02)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x10)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x12)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x61)},
	{USB_VENDOR(USB_VENDOR_HUAWEI), USB_IFACE_CLASS(UICLASS_VENDOR),
		USB_IFACE_SUBCLASS(0x02), USB_IFACE_PROTOCOL(0x62)},
	{USB_VENDOR(USB_VENDOR_HUAWEI),USB_IFACE_CLASS(UICLASS_CDC),
		USB_IFACE_SUBCLASS(UISUBCLASS_ABSTRACT_CONTROL_MODEL),
		USB_IFACE_PROTOCOL(0xFF)},
	/* Kyocera AH-K3001V */
	{USB_VPI(USB_VENDOR_KYOCERA, USB_PRODUCT_KYOCERA_AHK3001V, 1)},
	{USB_VPI(USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_MC5720, 1)},
	{USB_VPI(USB_VENDOR_CURITEL, USB_PRODUCT_CURITEL_PC5740, 1)},
};

/*
 * As speeds for umodem devices increase, these numbers will need to
 * be increased. They should be good for G3 speeds and below.
 *
 * TODO: The TTY buffers should be increased!
 */
#define	UMODEM_BUF_SIZE 1024

enum {
	UMODEM_BULK_WR,
	UMODEM_BULK_RD,
	UMODEM_INTR_WR,
	UMODEM_INTR_RD,
	UMODEM_N_TRANSFER,
};

#define	UMODEM_MODVER			1	/* module version */

struct umodem_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UMODEM_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint16_t sc_line;

	uint8_t	sc_lsr;			/* local status register */
	uint8_t	sc_msr;			/* modem status register */
	uint8_t	sc_ctrl_iface_no;
	uint8_t	sc_data_iface_no;
	uint8_t sc_iface_index[2];
	uint8_t	sc_cm_over_data;
	uint8_t	sc_cm_cap;		/* CM capabilities */
	uint8_t	sc_acm_cap;		/* ACM capabilities */
	uint8_t	sc_line_coding[32];	/* used in USB device mode */
	uint8_t	sc_abstract_state[32];	/* used in USB device mode */
};

static device_probe_t umodem_probe;
static device_attach_t umodem_attach;
static device_detach_t umodem_detach;
static usb_handle_request_t umodem_handle_request;

static void umodem_free_softc(struct umodem_softc *);

static usb_callback_t umodem_intr_read_callback;
static usb_callback_t umodem_intr_write_callback;
static usb_callback_t umodem_write_callback;
static usb_callback_t umodem_read_callback;

static void	umodem_free(struct ucom_softc *);
static void	umodem_start_read(struct ucom_softc *);
static void	umodem_stop_read(struct ucom_softc *);
static void	umodem_start_write(struct ucom_softc *);
static void	umodem_stop_write(struct ucom_softc *);
static void	umodem_get_caps(struct usb_attach_arg *, uint8_t *, uint8_t *);
static void	umodem_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static int	umodem_pre_param(struct ucom_softc *, struct termios *);
static void	umodem_cfg_param(struct ucom_softc *, struct termios *);
static int	umodem_ioctl(struct ucom_softc *, uint32_t, caddr_t, int,
		    struct thread *);
static void	umodem_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	umodem_cfg_set_rts(struct ucom_softc *, uint8_t);
static void	umodem_cfg_set_break(struct ucom_softc *, uint8_t);
static void	*umodem_get_desc(struct usb_attach_arg *, uint8_t, uint8_t);
static usb_error_t umodem_set_comm_feature(struct usb_device *, uint8_t,
		    uint16_t, uint16_t);
static void	umodem_poll(struct ucom_softc *ucom);
static void	umodem_find_data_iface(struct usb_attach_arg *uaa,
		    uint8_t, uint8_t *, uint8_t *);

static const struct usb_config umodem_config[UMODEM_N_TRANSFER] = {

	[UMODEM_BULK_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 0,
		.bufsize = UMODEM_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &umodem_write_callback,
		.usb_mode = USB_MODE_DUAL,
	},

	[UMODEM_BULK_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 0,
		.bufsize = UMODEM_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &umodem_read_callback,
		.usb_mode = USB_MODE_DUAL,
	},

	[UMODEM_INTR_WR] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 1,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &umodem_intr_write_callback,
		.usb_mode = USB_MODE_DEVICE,
	},

	[UMODEM_INTR_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 1,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &umodem_intr_read_callback,
		.usb_mode = USB_MODE_HOST,
	},
};

static const struct ucom_callback umodem_callback = {
	.ucom_cfg_get_status = &umodem_cfg_get_status,
	.ucom_cfg_set_dtr = &umodem_cfg_set_dtr,
	.ucom_cfg_set_rts = &umodem_cfg_set_rts,
	.ucom_cfg_set_break = &umodem_cfg_set_break,
	.ucom_cfg_param = &umodem_cfg_param,
	.ucom_pre_param = &umodem_pre_param,
	.ucom_ioctl = &umodem_ioctl,
	.ucom_start_read = &umodem_start_read,
	.ucom_stop_read = &umodem_stop_read,
	.ucom_start_write = &umodem_start_write,
	.ucom_stop_write = &umodem_stop_write,
	.ucom_poll = &umodem_poll,
	.ucom_free = &umodem_free,
};

static device_method_t umodem_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, umodem_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, umodem_probe),
	DEVMETHOD(device_attach, umodem_attach),
	DEVMETHOD(device_detach, umodem_detach),
	DEVMETHOD_END
};

static devclass_t umodem_devclass;

static driver_t umodem_driver = {
	.name = "umodem",
	.methods = umodem_methods,
	.size = sizeof(struct umodem_softc),
};

DRIVER_MODULE(umodem, uhub, umodem_driver, umodem_devclass, NULL, 0);
MODULE_DEPEND(umodem, ucom, 1, 1, 1);
MODULE_DEPEND(umodem, usb, 1, 1, 1);
MODULE_VERSION(umodem, UMODEM_MODVER);
USB_PNP_DUAL_INFO(umodem_dual_devs);
USB_PNP_HOST_INFO(umodem_host_devs);

static int
umodem_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	DPRINTFN(11, "\n");

	error = usbd_lookup_id_by_uaa(umodem_host_devs,
	    sizeof(umodem_host_devs), uaa);
	if (error) {
		error = usbd_lookup_id_by_uaa(umodem_dual_devs,
		    sizeof(umodem_dual_devs), uaa);
		if (error)
			return (error);
	}
	return (BUS_PROBE_GENERIC);
}

static int
umodem_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umodem_softc *sc = device_get_softc(dev);
	struct usb_cdc_cm_descriptor *cmd;
	struct usb_cdc_union_descriptor *cud;
	uint8_t i;
	int error;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "umodem", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_ctrl_iface_no = uaa->info.bIfaceNum;
	sc->sc_iface_index[1] = uaa->info.bIfaceIndex;
	sc->sc_udev = uaa->device;

	umodem_get_caps(uaa, &sc->sc_cm_cap, &sc->sc_acm_cap);

	/* get the data interface number */

	cmd = umodem_get_desc(uaa, UDESC_CS_INTERFACE, UDESCSUB_CDC_CM);

	if ((cmd == NULL) || (cmd->bLength < sizeof(*cmd))) {

		cud = usbd_find_descriptor(uaa->device, NULL,
		    uaa->info.bIfaceIndex, UDESC_CS_INTERFACE,
		    0xFF, UDESCSUB_CDC_UNION, 0xFF);

		if ((cud == NULL) || (cud->bLength < sizeof(*cud))) {
			DPRINTF("Missing descriptor. "
			    "Assuming data interface is next.\n");
			if (sc->sc_ctrl_iface_no == 0xFF) {
				goto detach;
			} else {
				uint8_t class_match = 0;

				/* set default interface number */
				sc->sc_data_iface_no = 0xFF;

				/* try to find the data interface backwards */
				umodem_find_data_iface(uaa,
				    uaa->info.bIfaceIndex - 1,
				    &sc->sc_data_iface_no, &class_match);

				/* try to find the data interface forwards */
				umodem_find_data_iface(uaa,
				    uaa->info.bIfaceIndex + 1,
				    &sc->sc_data_iface_no, &class_match);

				/* check if nothing was found */
				if (sc->sc_data_iface_no == 0xFF)
					goto detach;
			}
		} else {
			sc->sc_data_iface_no = cud->bSlaveInterface[0];
		}
	} else {
		sc->sc_data_iface_no = cmd->bDataInterface;
	}

	device_printf(dev, "data interface %d, has %sCM over "
	    "data, has %sbreak\n",
	    sc->sc_data_iface_no,
	    sc->sc_cm_cap & USB_CDC_CM_OVER_DATA ? "" : "no ",
	    sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK ? "" : "no ");

	/* get the data interface too */

	for (i = 0;; i++) {
		struct usb_interface *iface;
		struct usb_interface_descriptor *id;

		iface = usbd_get_iface(uaa->device, i);

		if (iface) {

			id = usbd_get_interface_descriptor(iface);

			if (id && (id->bInterfaceNumber == sc->sc_data_iface_no)) {
				sc->sc_iface_index[0] = i;
				usbd_set_parent_iface(uaa->device, i, uaa->info.bIfaceIndex);
				break;
			}
		} else {
			device_printf(dev, "no data interface\n");
			goto detach;
		}
	}

	if (usb_test_quirk(uaa, UQ_ASSUME_CM_OVER_DATA)) {
		sc->sc_cm_over_data = 1;
	} else {
		if (sc->sc_cm_cap & USB_CDC_CM_OVER_DATA) {
			if (sc->sc_acm_cap & USB_CDC_ACM_HAS_FEATURE) {

				error = umodem_set_comm_feature
				(uaa->device, sc->sc_ctrl_iface_no,
				 UCDC_ABSTRACT_STATE, UCDC_DATA_MULTIPLEXED);

				/* ignore any errors */
			}
			sc->sc_cm_over_data = 1;
		}
	}
	error = usbd_transfer_setup(uaa->device,
	    sc->sc_iface_index, sc->sc_xfer,
	    umodem_config, UMODEM_N_TRANSFER,
	    sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "Can't setup transfer\n");
		goto detach;
	}

	/* clear stall at first run, if USB host mode */
	if (uaa->usb_mode == USB_MODE_HOST) {
		mtx_lock(&sc->sc_mtx);
		usbd_xfer_set_stall(sc->sc_xfer[UMODEM_BULK_WR]);
		usbd_xfer_set_stall(sc->sc_xfer[UMODEM_BULK_RD]);
		mtx_unlock(&sc->sc_mtx);
	}

	ucom_set_usb_mode(&sc->sc_super_ucom, uaa->usb_mode);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &umodem_callback, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "Can't attach com\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	umodem_detach(dev);
	return (ENXIO);
}

static void
umodem_find_data_iface(struct usb_attach_arg *uaa,
    uint8_t iface_index, uint8_t *p_data_no, uint8_t *p_match_class)
{
	struct usb_interface_descriptor *id;
	struct usb_interface *iface;
	
	iface = usbd_get_iface(uaa->device, iface_index);

	/* check for end of interfaces */
	if (iface == NULL)
		return;

	id = usbd_get_interface_descriptor(iface);

	/* check for non-matching interface class */
	if (id->bInterfaceClass != UICLASS_CDC_DATA ||
	    id->bInterfaceSubClass != UISUBCLASS_DATA) {
		/* if we got a class match then return */
		if (*p_match_class)
			return;
	} else {
		*p_match_class = 1;
	}

	DPRINTFN(11, "Match at index %u\n", iface_index);

	*p_data_no = id->bInterfaceNumber;
}

static void
umodem_start_read(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	/* start interrupt endpoint, if any */
	usbd_transfer_start(sc->sc_xfer[UMODEM_INTR_RD]);

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[UMODEM_BULK_RD]);
}

static void
umodem_stop_read(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	/* stop interrupt endpoint, if any */
	usbd_transfer_stop(sc->sc_xfer[UMODEM_INTR_RD]);

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[UMODEM_BULK_RD]);
}

static void
umodem_start_write(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UMODEM_INTR_WR]);
	usbd_transfer_start(sc->sc_xfer[UMODEM_BULK_WR]);
}

static void
umodem_stop_write(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UMODEM_INTR_WR]);
	usbd_transfer_stop(sc->sc_xfer[UMODEM_BULK_WR]);
}

static void
umodem_get_caps(struct usb_attach_arg *uaa, uint8_t *cm, uint8_t *acm)
{
	struct usb_cdc_cm_descriptor *cmd;
	struct usb_cdc_acm_descriptor *cad;

	cmd = umodem_get_desc(uaa, UDESC_CS_INTERFACE, UDESCSUB_CDC_CM);
	if ((cmd == NULL) || (cmd->bLength < sizeof(*cmd))) {
		DPRINTF("no CM desc (faking one)\n");
		*cm = USB_CDC_CM_DOES_CM | USB_CDC_CM_OVER_DATA;
	} else
		*cm = cmd->bmCapabilities;

	cad = umodem_get_desc(uaa, UDESC_CS_INTERFACE, UDESCSUB_CDC_ACM);
	if ((cad == NULL) || (cad->bLength < sizeof(*cad))) {
		DPRINTF("no ACM desc\n");
		*acm = 0;
	} else
		*acm = cad->bmCapabilities;
}

static void
umodem_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct umodem_softc *sc = ucom->sc_parent;

	DPRINTF("\n");

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static int
umodem_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	return (0);			/* we accept anything */
}

static void
umodem_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct umodem_softc *sc = ucom->sc_parent;
	struct usb_cdc_line_state ls;
	struct usb_device_request req;

	DPRINTF("sc=%p\n", sc);

	memset(&ls, 0, sizeof(ls));

	USETDW(ls.dwDTERate, t->c_ospeed);

	ls.bCharFormat = (t->c_cflag & CSTOPB) ?
	    UCDC_STOP_BIT_2 : UCDC_STOP_BIT_1;

	ls.bParityType = (t->c_cflag & PARENB) ?
	    ((t->c_cflag & PARODD) ?
	    UCDC_PARITY_ODD : UCDC_PARITY_EVEN) : UCDC_PARITY_NONE;

	switch (t->c_cflag & CSIZE) {
	case CS5:
		ls.bDataBits = 5;
		break;
	case CS6:
		ls.bDataBits = 6;
		break;
	case CS7:
		ls.bDataBits = 7;
		break;
	case CS8:
		ls.bDataBits = 8;
		break;
	}

	DPRINTF("rate=%d fmt=%d parity=%d bits=%d\n",
	    UGETDW(ls.dwDTERate), ls.bCharFormat,
	    ls.bParityType, ls.bDataBits);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_LINE_CODING;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_ctrl_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, sizeof(ls));

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, &ls, 0, 1000);
}

static int
umodem_ioctl(struct ucom_softc *ucom, uint32_t cmd, caddr_t data,
    int flag, struct thread *td)
{
	struct umodem_softc *sc = ucom->sc_parent;
	int error = 0;

	DPRINTF("cmd=0x%08x\n", cmd);

	switch (cmd) {
	case USB_GET_CM_OVER_DATA:
		*(int *)data = sc->sc_cm_over_data;
		break;

	case USB_SET_CM_OVER_DATA:
		if (*(int *)data != sc->sc_cm_over_data) {
			/* XXX change it */
		}
		break;

	default:
		DPRINTF("unknown\n");
		error = ENOIOCTL;
		break;
	}

	return (error);
}

static void
umodem_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	DPRINTF("onoff=%d\n", onoff);

	if (onoff)
		sc->sc_line |= UCDC_LINE_DTR;
	else
		sc->sc_line &= ~UCDC_LINE_DTR;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, sc->sc_line);
	req.wIndex[0] = sc->sc_ctrl_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
umodem_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	DPRINTF("onoff=%d\n", onoff);

	if (onoff)
		sc->sc_line |= UCDC_LINE_RTS;
	else
		sc->sc_line &= ~UCDC_LINE_RTS;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, sc->sc_line);
	req.wIndex[0] = sc->sc_ctrl_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
umodem_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umodem_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t temp;

	DPRINTF("onoff=%d\n", onoff);

	if (sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK) {

		temp = onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF;

		req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
		req.bRequest = UCDC_SEND_BREAK;
		USETW(req.wValue, temp);
		req.wIndex[0] = sc->sc_ctrl_iface_no;
		req.wIndex[1] = 0;
		USETW(req.wLength, 0);

		ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
		    &req, NULL, 0, 1000);
	}
}

static void
umodem_intr_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("Transferred %d bytes\n", actlen);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* start clear stall */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
umodem_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usb_cdc_notification pkt;
	struct umodem_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint16_t wLen;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen < 8) {
			DPRINTF("received short packet, "
			    "%d bytes\n", actlen);
			goto tr_setup;
		}
		if (actlen > (int)sizeof(pkt)) {
			DPRINTF("truncating message\n");
			actlen = sizeof(pkt);
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &pkt, actlen);

		actlen -= 8;

		wLen = UGETW(pkt.wLength);
		if (actlen > wLen) {
			actlen = wLen;
		}
		if (pkt.bmRequestType != UCDC_NOTIFICATION) {
			DPRINTF("unknown message type, "
			    "0x%02x, on notify pipe!\n",
			    pkt.bmRequestType);
			goto tr_setup;
		}
		switch (pkt.bNotification) {
		case UCDC_N_SERIAL_STATE:
			/*
			 * Set the serial state in ucom driver based on
			 * the bits from the notify message
			 */
			if (actlen < 2) {
				DPRINTF("invalid notification "
				    "length, %d bytes!\n", actlen);
				break;
			}
			DPRINTF("notify bytes = %02x%02x\n",
			    pkt.data[0],
			    pkt.data[1]);

			/* Currently, lsr is always zero. */
			sc->sc_lsr = 0;
			sc->sc_msr = 0;

			if (pkt.data[0] & UCDC_N_SERIAL_RI) {
				sc->sc_msr |= SER_RI;
			}
			if (pkt.data[0] & UCDC_N_SERIAL_DSR) {
				sc->sc_msr |= SER_DSR;
			}
			if (pkt.data[0] & UCDC_N_SERIAL_DCD) {
				sc->sc_msr |= SER_DCD;
			}
			ucom_status_change(&sc->sc_ucom);
			break;

		default:
			DPRINTF("unknown notify message: 0x%02x\n",
			    pkt.bNotification);
			break;
		}

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
umodem_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umodem_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UMODEM_BUF_SIZE, &actlen)) {

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
umodem_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umodem_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("actlen=%d\n", actlen);

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

static void *
umodem_get_desc(struct usb_attach_arg *uaa, uint8_t type, uint8_t subtype)
{
	return (usbd_find_descriptor(uaa->device, NULL, uaa->info.bIfaceIndex,
	    type, 0xFF, subtype, 0xFF));
}

static usb_error_t
umodem_set_comm_feature(struct usb_device *udev, uint8_t iface_no,
    uint16_t feature, uint16_t state)
{
	struct usb_device_request req;
	struct usb_cdc_abstract_state ast;

	DPRINTF("feature=%d state=%d\n",
	    feature, state);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_COMM_FEATURE;
	USETW(req.wValue, feature);
	req.wIndex[0] = iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, UCDC_ABSTRACT_STATE_LENGTH);
	USETW(ast.wState, state);

	return (usbd_do_request(udev, NULL, &req, &ast));
}

static int
umodem_detach(device_t dev)
{
	struct umodem_softc *sc = device_get_softc(dev);

	DPRINTF("sc=%p\n", sc);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UMODEM_N_TRANSFER);

	device_claim_softc(dev);

	umodem_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(umodem);

static void
umodem_free_softc(struct umodem_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
umodem_free(struct ucom_softc *ucom)
{
	umodem_free_softc(ucom->sc_parent);
}

static void
umodem_poll(struct ucom_softc *ucom)
{
	struct umodem_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UMODEM_N_TRANSFER);
}

static int
umodem_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	struct umodem_softc *sc = device_get_softc(dev);
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	DPRINTF("sc=%p\n", sc);

	if (!is_complete) {
		if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UCDC_SET_LINE_CODING) &&
		    (req->wIndex[0] == sc->sc_ctrl_iface_no) &&
		    (req->wIndex[1] == 0x00) &&
		    (req->wValue[0] == 0x00) &&
		    (req->wValue[1] == 0x00)) {
			if (offset == 0) {
				*plen = sizeof(sc->sc_line_coding);
				*pptr = &sc->sc_line_coding;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->wIndex[0] == sc->sc_ctrl_iface_no) &&
		    (req->wIndex[1] == 0x00) &&
		    (req->bRequest == UCDC_SET_COMM_FEATURE)) {
			if (offset == 0) {
				*plen = sizeof(sc->sc_abstract_state);
				*pptr = &sc->sc_abstract_state;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->wIndex[0] == sc->sc_ctrl_iface_no) &&
		    (req->wIndex[1] == 0x00) &&
		    (req->bRequest == UCDC_SET_CONTROL_LINE_STATE)) {
			*plen = 0;
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->wIndex[0] == sc->sc_ctrl_iface_no) &&
		    (req->wIndex[1] == 0x00) &&
		    (req->bRequest == UCDC_SEND_BREAK)) {
			*plen = 0;
			return (0);
		}
	}
	return (ENXIO);			/* use builtin handler */
}
