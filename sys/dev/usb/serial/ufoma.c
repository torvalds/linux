/*	$NetBSD: umodem.c,v 1.45 2002/09/23 05:51:23 simonb Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#define UFOMA_HANDSFREE
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005, Takanori Watanabe
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
 */

/*
 * TODO:
 * - Implement a Call Device for modems without multiplexed commands.
 */

/*
 * NOTE: all function names beginning like "ufoma_cfg_" can only
 * be called from within the config thread function !
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
#include <sys/sbuf.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

typedef struct ufoma_mobile_acm_descriptor {
	uint8_t	bFunctionLength;
	uint8_t	bDescriptorType;
	uint8_t	bDescriptorSubtype;
	uint8_t	bType;
	uint8_t	bMode[1];
} __packed usb_mcpc_acm_descriptor;

#define	UISUBCLASS_MCPC 0x88

#define	UDESC_VS_INTERFACE 0x44
#define	UDESCSUB_MCPC_ACM  0x11

#define	UMCPC_ACM_TYPE_AB1 0x1
#define	UMCPC_ACM_TYPE_AB2 0x2
#define	UMCPC_ACM_TYPE_AB5 0x5
#define	UMCPC_ACM_TYPE_AB6 0x6

#define	UMCPC_ACM_MODE_DEACTIVATED 0x0
#define	UMCPC_ACM_MODE_MODEM 0x1
#define	UMCPC_ACM_MODE_ATCOMMAND 0x2
#define	UMCPC_ACM_MODE_OBEX 0x60
#define	UMCPC_ACM_MODE_VENDOR1 0xc0
#define	UMCPC_ACM_MODE_VENDOR2 0xfe
#define	UMCPC_ACM_MODE_UNLINKED 0xff

#define	UMCPC_CM_MOBILE_ACM 0x0

#define	UMCPC_ACTIVATE_MODE 0x60
#define	UMCPC_GET_MODETABLE 0x61
#define	UMCPC_SET_LINK 0x62
#define	UMCPC_CLEAR_LINK 0x63

#define	UMCPC_REQUEST_ACKNOWLEDGE 0x31

#define	UFOMA_MAX_TIMEOUT 15		/* standard says 10 seconds */
#define	UFOMA_CMD_BUF_SIZE 64		/* bytes */

#define	UFOMA_BULK_BUF_SIZE 1024	/* bytes */

enum {
	UFOMA_CTRL_ENDPT_INTR,
	UFOMA_CTRL_ENDPT_READ,
	UFOMA_CTRL_ENDPT_WRITE,
	UFOMA_CTRL_ENDPT_MAX,
};

enum {
	UFOMA_BULK_ENDPT_WRITE,
	UFOMA_BULK_ENDPT_READ,	
	UFOMA_BULK_ENDPT_MAX,
};

struct ufoma_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;
	struct cv sc_cv;
	struct mtx sc_mtx;

	struct usb_xfer *sc_ctrl_xfer[UFOMA_CTRL_ENDPT_MAX];
	struct usb_xfer *sc_bulk_xfer[UFOMA_BULK_ENDPT_MAX];
	uint8_t *sc_modetable;
	device_t sc_dev;
	struct usb_device *sc_udev;

	uint32_t sc_unit;

	uint16_t sc_line;

	uint8_t	sc_num_msg;
	uint8_t	sc_nobulk;
	uint8_t	sc_ctrl_iface_no;
	uint8_t	sc_ctrl_iface_index;
	uint8_t	sc_data_iface_no;
	uint8_t	sc_data_iface_index;
	uint8_t	sc_cm_cap;
	uint8_t	sc_acm_cap;
	uint8_t	sc_lsr;
	uint8_t	sc_msr;
	uint8_t	sc_modetoactivate;
	uint8_t	sc_currentmode;
};

/* prototypes */

static device_probe_t ufoma_probe;
static device_attach_t ufoma_attach;
static device_detach_t ufoma_detach;
static void ufoma_free_softc(struct ufoma_softc *);

static usb_callback_t ufoma_ctrl_read_callback;
static usb_callback_t ufoma_ctrl_write_callback;
static usb_callback_t ufoma_intr_callback;
static usb_callback_t ufoma_bulk_write_callback;
static usb_callback_t ufoma_bulk_read_callback;

static void	*ufoma_get_intconf(struct usb_config_descriptor *,
		    struct usb_interface_descriptor *, uint8_t, uint8_t);
static void	ufoma_cfg_link_state(struct ufoma_softc *);
static void	ufoma_cfg_activate_state(struct ufoma_softc *, uint16_t);
static void	ufoma_free(struct ucom_softc *);
static void	ufoma_cfg_open(struct ucom_softc *);
static void	ufoma_cfg_close(struct ucom_softc *);
static void	ufoma_cfg_set_break(struct ucom_softc *, uint8_t);
static void	ufoma_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	ufoma_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	ufoma_cfg_set_rts(struct ucom_softc *, uint8_t);
static int	ufoma_pre_param(struct ucom_softc *, struct termios *);
static void	ufoma_cfg_param(struct ucom_softc *, struct termios *);
static int	ufoma_modem_setup(device_t, struct ufoma_softc *,
		    struct usb_attach_arg *);
static void	ufoma_start_read(struct ucom_softc *);
static void	ufoma_stop_read(struct ucom_softc *);
static void	ufoma_start_write(struct ucom_softc *);
static void	ufoma_stop_write(struct ucom_softc *);
static void	ufoma_poll(struct ucom_softc *ucom);

/*sysctl stuff*/
static int ufoma_sysctl_support(SYSCTL_HANDLER_ARGS);
static int ufoma_sysctl_current(SYSCTL_HANDLER_ARGS);
static int ufoma_sysctl_open(SYSCTL_HANDLER_ARGS);

static const struct usb_config
	ufoma_ctrl_config[UFOMA_CTRL_ENDPT_MAX] = {

	[UFOMA_CTRL_ENDPT_INTR] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = sizeof(struct usb_cdc_notification),
		.callback = &ufoma_intr_callback,
	},

	[UFOMA_CTRL_ENDPT_READ] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) + UFOMA_CMD_BUF_SIZE),
		.flags = {.short_xfer_ok = 1,},
		.callback = &ufoma_ctrl_read_callback,
		.timeout = 1000,	/* 1 second */
	},

	[UFOMA_CTRL_ENDPT_WRITE] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) + 1),
		.callback = &ufoma_ctrl_write_callback,
		.timeout = 1000,	/* 1 second */
	},
};

static const struct usb_config
	ufoma_bulk_config[UFOMA_BULK_ENDPT_MAX] = {

	[UFOMA_BULK_ENDPT_WRITE] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UFOMA_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &ufoma_bulk_write_callback,
	},

	[UFOMA_BULK_ENDPT_READ] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UFOMA_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &ufoma_bulk_read_callback,
	},
};

static const struct ucom_callback ufoma_callback = {
	.ucom_cfg_get_status = &ufoma_cfg_get_status,
	.ucom_cfg_set_dtr = &ufoma_cfg_set_dtr,
	.ucom_cfg_set_rts = &ufoma_cfg_set_rts,
	.ucom_cfg_set_break = &ufoma_cfg_set_break,
	.ucom_cfg_param = &ufoma_cfg_param,
	.ucom_cfg_open = &ufoma_cfg_open,
	.ucom_cfg_close = &ufoma_cfg_close,
	.ucom_pre_param = &ufoma_pre_param,
	.ucom_start_read = &ufoma_start_read,
	.ucom_stop_read = &ufoma_stop_read,
	.ucom_start_write = &ufoma_start_write,
	.ucom_stop_write = &ufoma_stop_write,
	.ucom_poll = &ufoma_poll,
	.ucom_free = &ufoma_free,
};

static device_method_t ufoma_methods[] = {
	/* Device methods */
	DEVMETHOD(device_probe, ufoma_probe),
	DEVMETHOD(device_attach, ufoma_attach),
	DEVMETHOD(device_detach, ufoma_detach),
	DEVMETHOD_END
};

static devclass_t ufoma_devclass;

static driver_t ufoma_driver = {
	.name = "ufoma",
	.methods = ufoma_methods,
	.size = sizeof(struct ufoma_softc),
};

static const STRUCT_USB_HOST_ID ufoma_devs[] = {
	{USB_IFACE_CLASS(UICLASS_CDC),
	 USB_IFACE_SUBCLASS(UISUBCLASS_MCPC),},
};

DRIVER_MODULE(ufoma, uhub, ufoma_driver, ufoma_devclass, NULL, 0);
MODULE_DEPEND(ufoma, ucom, 1, 1, 1);
MODULE_DEPEND(ufoma, usb, 1, 1, 1);
MODULE_VERSION(ufoma, 1);
USB_PNP_HOST_INFO(ufoma_devs);

static int
ufoma_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_interface_descriptor *id;
	struct usb_config_descriptor *cd;
	usb_mcpc_acm_descriptor *mad;
	int error;

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	error = usbd_lookup_id_by_uaa(ufoma_devs, sizeof(ufoma_devs), uaa);
	if (error)
		return (error);

	id = usbd_get_interface_descriptor(uaa->iface);
	cd = usbd_get_config_descriptor(uaa->device);

	if (id == NULL || cd == NULL)
		return (ENXIO);

	mad = ufoma_get_intconf(cd, id, UDESC_VS_INTERFACE, UDESCSUB_MCPC_ACM);
	if (mad == NULL)
		return (ENXIO);

#ifndef UFOMA_HANDSFREE
	if ((mad->bType == UMCPC_ACM_TYPE_AB5) ||
	    (mad->bType == UMCPC_ACM_TYPE_AB6))
		return (ENXIO);
#endif
	return (BUS_PROBE_GENERIC);
}

static int
ufoma_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ufoma_softc *sc = device_get_softc(dev);
	struct usb_config_descriptor *cd;
	struct usb_interface_descriptor *id;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;

	usb_mcpc_acm_descriptor *mad;
	uint8_t elements;
	int32_t error;

	sc->sc_udev = uaa->device;
	sc->sc_dev = dev;
	sc->sc_unit = device_get_unit(dev);

	mtx_init(&sc->sc_mtx, "ufoma", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);
	cv_init(&sc->sc_cv, "CWAIT");

	device_set_usb_desc(dev);

	DPRINTF("\n");

	/* setup control transfers */

	cd = usbd_get_config_descriptor(uaa->device);
	id = usbd_get_interface_descriptor(uaa->iface);
	sc->sc_ctrl_iface_no = id->bInterfaceNumber;
	sc->sc_ctrl_iface_index = uaa->info.bIfaceIndex;

	error = usbd_transfer_setup(uaa->device,
	    &sc->sc_ctrl_iface_index, sc->sc_ctrl_xfer,
	    ufoma_ctrl_config, UFOMA_CTRL_ENDPT_MAX, sc, &sc->sc_mtx);

	if (error) {
		device_printf(dev, "allocating control USB "
		    "transfers failed\n");
		goto detach;
	}
	mad = ufoma_get_intconf(cd, id, UDESC_VS_INTERFACE, UDESCSUB_MCPC_ACM);
	if (mad == NULL) {
		goto detach;
	}
	if (mad->bFunctionLength < sizeof(*mad)) {
		device_printf(dev, "invalid MAD descriptor\n");
		goto detach;
	}
	if ((mad->bType == UMCPC_ACM_TYPE_AB5) ||
	    (mad->bType == UMCPC_ACM_TYPE_AB6)) {
		sc->sc_nobulk = 1;
	} else {
		sc->sc_nobulk = 0;
		if (ufoma_modem_setup(dev, sc, uaa)) {
			goto detach;
		}
	}

	elements = (mad->bFunctionLength - sizeof(*mad) + 1);

	/* initialize mode variables */

	sc->sc_modetable = malloc(elements + 1, M_USBDEV, M_WAITOK);

	if (sc->sc_modetable == NULL) {
		goto detach;
	}
	sc->sc_modetable[0] = (elements + 1);
	memcpy(&sc->sc_modetable[1], mad->bMode, elements);

	sc->sc_currentmode = UMCPC_ACM_MODE_UNLINKED;
	sc->sc_modetoactivate = mad->bMode[0];

	/* clear stall at first run, if any */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_WRITE]);
	usbd_xfer_set_stall(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_READ]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &ufoma_callback, &sc->sc_mtx);
	if (error) {
		DPRINTF("ucom_attach failed\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	/*Sysctls*/
	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);

	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "supportmode",
			CTLFLAG_RD|CTLTYPE_STRING, sc, 0, ufoma_sysctl_support,
			"A", "Supporting port role");

	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "currentmode",
			CTLFLAG_RD|CTLTYPE_STRING, sc, 0, ufoma_sysctl_current,
			"A", "Current port role");

	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "openmode",
			CTLFLAG_RW|CTLTYPE_STRING, sc, 0, ufoma_sysctl_open,
			"A", "Mode to transit when port is opened");
	SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "comunit",
			CTLFLAG_RD, &(sc->sc_super_ucom.sc_unit), 0, 
			"Unit number as USB serial");

	return (0);			/* success */

detach:
	ufoma_detach(dev);
	return (ENXIO);			/* failure */
}

static int
ufoma_detach(device_t dev)
{
	struct ufoma_softc *sc = device_get_softc(dev);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_ctrl_xfer, UFOMA_CTRL_ENDPT_MAX);
	usbd_transfer_unsetup(sc->sc_bulk_xfer, UFOMA_BULK_ENDPT_MAX);

	if (sc->sc_modetable) {
		free(sc->sc_modetable, M_USBDEV);
	}
	cv_destroy(&sc->sc_cv);

	device_claim_softc(dev);

	ufoma_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(ufoma);

static void
ufoma_free_softc(struct ufoma_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
ufoma_free(struct ucom_softc *ucom)
{
	ufoma_free_softc(ucom->sc_parent);
}

static void *
ufoma_get_intconf(struct usb_config_descriptor *cd, struct usb_interface_descriptor *id,
    uint8_t type, uint8_t subtype)
{
	struct usb_descriptor *desc = (void *)id;

	while ((desc = usb_desc_foreach(cd, desc))) {

		if (desc->bDescriptorType == UDESC_INTERFACE) {
			return (NULL);
		}
		if ((desc->bDescriptorType == type) &&
		    (desc->bDescriptorSubtype == subtype)) {
			break;
		}
	}
	return (desc);
}

static void
ufoma_cfg_link_state(struct ufoma_softc *sc)
{
	struct usb_device_request req;
	int32_t error;

	req.bmRequestType = UT_WRITE_VENDOR_INTERFACE;
	req.bRequest = UMCPC_SET_LINK;
	USETW(req.wValue, UMCPC_CM_MOBILE_ACM);
	USETW(req.wIndex, sc->sc_ctrl_iface_no);
	USETW(req.wLength, sc->sc_modetable[0]);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, sc->sc_modetable, 0, 1000);

	error = cv_timedwait(&sc->sc_cv, &sc->sc_mtx, hz);

	if (error) {
		DPRINTF("NO response\n");
	}
}

static void
ufoma_cfg_activate_state(struct ufoma_softc *sc, uint16_t state)
{
	struct usb_device_request req;
	int32_t error;

	req.bmRequestType = UT_WRITE_VENDOR_INTERFACE;
	req.bRequest = UMCPC_ACTIVATE_MODE;
	USETW(req.wValue, state);
	USETW(req.wIndex, sc->sc_ctrl_iface_no);
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);

	error = cv_timedwait(&sc->sc_cv, &sc->sc_mtx,
	    (UFOMA_MAX_TIMEOUT * hz));
	if (error) {
		DPRINTF("No response\n");
	}
}

static void
ufoma_ctrl_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ufoma_softc *sc = usbd_xfer_softc(xfer);
	struct usb_device_request req;
	struct usb_page_cache *pc0, *pc1;
	int len, aframes, nframes;

	usbd_xfer_status(xfer, NULL, NULL, &aframes, &nframes);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
		if (aframes != nframes)
			goto tr_setup;
		pc1 = usbd_xfer_get_frame(xfer, 1);
		len = usbd_xfer_frame_len(xfer, 1);
		if (len > 0)
			ucom_put_data(&sc->sc_ucom, pc1, 0, len);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_num_msg) {
			sc->sc_num_msg--;

			req.bmRequestType = UT_READ_CLASS_INTERFACE;
			req.bRequest = UCDC_GET_ENCAPSULATED_RESPONSE;
			USETW(req.wIndex, sc->sc_ctrl_iface_no);
			USETW(req.wValue, 0);
			USETW(req.wLength, UFOMA_CMD_BUF_SIZE);

			pc0 = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc0, 0, &req, sizeof(req));

			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 1, UFOMA_CMD_BUF_SIZE);
			usbd_xfer_set_frames(xfer, 2);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		DPRINTF("error = %s\n",
		    usbd_errstr(error));

		if (error == USB_ERR_CANCELLED) {
			return;
		}
		goto tr_transferred;
	}
}

static void
ufoma_ctrl_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ufoma_softc *sc = usbd_xfer_softc(xfer);
	struct usb_device_request req;
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
	case USB_ST_SETUP:
		pc = usbd_xfer_get_frame(xfer, 1);
		if (ucom_get_data(&sc->sc_ucom, pc, 0, 1, &actlen)) {

			req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
			req.bRequest = UCDC_SEND_ENCAPSULATED_COMMAND;
			USETW(req.wIndex, sc->sc_ctrl_iface_no);
			USETW(req.wValue, 0);
			USETW(req.wLength, 1);

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_in(pc, 0, &req, sizeof(req));

			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 1, 1);
			usbd_xfer_set_frames(xfer, 2);

			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		DPRINTF("error = %s\n", usbd_errstr(error));

		if (error == USB_ERR_CANCELLED) {
			return;
		}
		goto tr_transferred;
	}
}

static void
ufoma_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ufoma_softc *sc = usbd_xfer_softc(xfer);
	struct usb_cdc_notification pkt;
	struct usb_page_cache *pc;
	uint16_t wLen;
	uint16_t temp;
	uint8_t mstatus;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen < 8) {
			DPRINTF("too short message\n");
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
		if ((pkt.bmRequestType == UT_READ_VENDOR_INTERFACE) &&
		    (pkt.bNotification == UMCPC_REQUEST_ACKNOWLEDGE)) {
			temp = UGETW(pkt.wValue);
			sc->sc_currentmode = (temp >> 8);
			if (!(temp & 0xff)) {
				DPRINTF("Mode change failed!\n");
			}
			cv_signal(&sc->sc_cv);
		}
		if (pkt.bmRequestType != UCDC_NOTIFICATION) {
			goto tr_setup;
		}
		switch (pkt.bNotification) {
		case UCDC_N_RESPONSE_AVAILABLE:
			if (!(sc->sc_nobulk)) {
				DPRINTF("Wrong serial state!\n");
				break;
			}
			if (sc->sc_num_msg != 0xFF) {
				sc->sc_num_msg++;
			}
			usbd_transfer_start(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_READ]);
			break;

		case UCDC_N_SERIAL_STATE:
			if (sc->sc_nobulk) {
				DPRINTF("Wrong serial state!\n");
				break;
			}
			/*
		         * Set the serial state in ucom driver based on
		         * the bits from the notify message
		         */
			if (actlen < 2) {
				DPRINTF("invalid notification "
				    "length, %d bytes!\n", actlen);
				break;
			}
			DPRINTF("notify bytes = 0x%02x, 0x%02x\n",
			    pkt.data[0], pkt.data[1]);

			/* currently, lsr is always zero. */
			sc->sc_lsr = 0;
			sc->sc_msr = 0;

			mstatus = pkt.data[0];

			if (mstatus & UCDC_N_SERIAL_RI) {
				sc->sc_msr |= SER_RI;
			}
			if (mstatus & UCDC_N_SERIAL_DSR) {
				sc->sc_msr |= SER_DSR;
			}
			if (mstatus & UCDC_N_SERIAL_DCD) {
				sc->sc_msr |= SER_DCD;
			}
			ucom_status_change(&sc->sc_ucom);
			break;

		default:
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
ufoma_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ufoma_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UFOMA_BULK_BUF_SIZE, &actlen)) {
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
ufoma_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ufoma_softc *sc = usbd_xfer_softc(xfer);
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
ufoma_cfg_open(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	/* empty input queue */

	if (sc->sc_num_msg != 0xFF) {
		sc->sc_num_msg++;
	}
	if (sc->sc_currentmode == UMCPC_ACM_MODE_UNLINKED) {
		ufoma_cfg_link_state(sc);
	}
	if (sc->sc_currentmode == UMCPC_ACM_MODE_DEACTIVATED) {
		ufoma_cfg_activate_state(sc, sc->sc_modetoactivate);
	}
}

static void
ufoma_cfg_close(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	ufoma_cfg_activate_state(sc, UMCPC_ACM_MODE_DEACTIVATED);
}

static void
ufoma_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ufoma_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	uint16_t wValue;

	if (sc->sc_nobulk ||
	    (sc->sc_currentmode == UMCPC_ACM_MODE_OBEX)) {
		return;
	}
	if (!(sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK)) {
		return;
	}
	wValue = onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_BREAK;
	USETW(req.wValue, wValue);
	req.wIndex[0] = sc->sc_ctrl_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
}

static void
ufoma_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
ufoma_cfg_set_line_state(struct ufoma_softc *sc)
{
	struct usb_device_request req;

	/* Don't send line state emulation request for OBEX port */
	if (sc->sc_currentmode == UMCPC_ACM_MODE_OBEX) {
		return;
	}
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
ufoma_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	if (sc->sc_nobulk) {
		return;
	}
	if (onoff)
		sc->sc_line |= UCDC_LINE_DTR;
	else
		sc->sc_line &= ~UCDC_LINE_DTR;

	ufoma_cfg_set_line_state(sc);
}

static void
ufoma_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	if (sc->sc_nobulk) {
		return;
	}
	if (onoff)
		sc->sc_line |= UCDC_LINE_RTS;
	else
		sc->sc_line &= ~UCDC_LINE_RTS;

	ufoma_cfg_set_line_state(sc);
}

static int
ufoma_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	return (0);			/* we accept anything */
}

static void
ufoma_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct ufoma_softc *sc = ucom->sc_parent;
	struct usb_device_request req;
	struct usb_cdc_line_state ls;

	if (sc->sc_nobulk ||
	    (sc->sc_currentmode == UMCPC_ACM_MODE_OBEX)) {
		return;
	}
	DPRINTF("\n");

	memset(&ls, 0, sizeof(ls));

	USETDW(ls.dwDTERate, t->c_ospeed);

	if (t->c_cflag & CSTOPB) {
		ls.bCharFormat = UCDC_STOP_BIT_2;
	} else {
		ls.bCharFormat = UCDC_STOP_BIT_1;
	}

	if (t->c_cflag & PARENB) {
		if (t->c_cflag & PARODD) {
			ls.bParityType = UCDC_PARITY_ODD;
		} else {
			ls.bParityType = UCDC_PARITY_EVEN;
		}
	} else {
		ls.bParityType = UCDC_PARITY_NONE;
	}

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

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_LINE_CODING;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_ctrl_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, UCDC_LINE_STATE_LENGTH);

	ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, &ls, 0, 1000);
}

static int
ufoma_modem_setup(device_t dev, struct ufoma_softc *sc,
    struct usb_attach_arg *uaa)
{
	struct usb_config_descriptor *cd;
	struct usb_cdc_acm_descriptor *acm;
	struct usb_cdc_cm_descriptor *cmd;
	struct usb_interface_descriptor *id;
	struct usb_interface *iface;
	uint8_t i;
	int32_t error;

	cd = usbd_get_config_descriptor(uaa->device);
	id = usbd_get_interface_descriptor(uaa->iface);

	cmd = ufoma_get_intconf(cd, id, UDESC_CS_INTERFACE, UDESCSUB_CDC_CM);

	if ((cmd == NULL) ||
	    (cmd->bLength < sizeof(*cmd))) {
		return (EINVAL);
	}
	sc->sc_cm_cap = cmd->bmCapabilities;
	sc->sc_data_iface_no = cmd->bDataInterface;

	acm = ufoma_get_intconf(cd, id, UDESC_CS_INTERFACE, UDESCSUB_CDC_ACM);

	if ((acm == NULL) ||
	    (acm->bLength < sizeof(*acm))) {
		return (EINVAL);
	}
	sc->sc_acm_cap = acm->bmCapabilities;

	device_printf(dev, "data interface %d, has %sCM over data, "
	    "has %sbreak\n",
	    sc->sc_data_iface_no,
	    sc->sc_cm_cap & USB_CDC_CM_OVER_DATA ? "" : "no ",
	    sc->sc_acm_cap & USB_CDC_ACM_HAS_BREAK ? "" : "no ");

	/* get the data interface too */

	for (i = 0;; i++) {

		iface = usbd_get_iface(uaa->device, i);

		if (iface) {

			id = usbd_get_interface_descriptor(iface);

			if (id && (id->bInterfaceNumber == sc->sc_data_iface_no)) {
				sc->sc_data_iface_index = i;
				usbd_set_parent_iface(uaa->device, i, uaa->info.bIfaceIndex);
				break;
			}
		} else {
			device_printf(dev, "no data interface\n");
			return (EINVAL);
		}
	}

	error = usbd_transfer_setup(uaa->device,
	    &sc->sc_data_iface_index, sc->sc_bulk_xfer,
	    ufoma_bulk_config, UFOMA_BULK_ENDPT_MAX, sc, &sc->sc_mtx);

	if (error) {
		device_printf(dev, "allocating BULK USB "
		    "transfers failed\n");
		return (EINVAL);
	}
	return (0);
}

static void
ufoma_start_read(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	/* start interrupt transfer */
	usbd_transfer_start(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_INTR]);

	/* start data transfer */
	if (sc->sc_nobulk) {
		usbd_transfer_start(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_READ]);
	} else {
		usbd_transfer_start(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_READ]);
	}
}

static void
ufoma_stop_read(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	/* stop interrupt transfer */
	usbd_transfer_stop(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_INTR]);

	/* stop data transfer */
	if (sc->sc_nobulk) {
		usbd_transfer_stop(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_READ]);
	} else {
		usbd_transfer_stop(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_READ]);
	}
}

static void
ufoma_start_write(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	if (sc->sc_nobulk) {
		usbd_transfer_start(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_WRITE]);
	} else {
		usbd_transfer_start(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_WRITE]);
	}
}

static void
ufoma_stop_write(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;

	if (sc->sc_nobulk) {
		usbd_transfer_stop(sc->sc_ctrl_xfer[UFOMA_CTRL_ENDPT_WRITE]);
	} else {
		usbd_transfer_stop(sc->sc_bulk_xfer[UFOMA_BULK_ENDPT_WRITE]);
	}
}

static struct umcpc_modetostr_tab{
	int mode;
	char *str;
}umcpc_modetostr_tab[]={
	{UMCPC_ACM_MODE_DEACTIVATED, "deactivated"},
	{UMCPC_ACM_MODE_MODEM, "modem"},
	{UMCPC_ACM_MODE_ATCOMMAND, "handsfree"},
	{UMCPC_ACM_MODE_OBEX, "obex"},
	{UMCPC_ACM_MODE_VENDOR1, "vendor1"},
	{UMCPC_ACM_MODE_VENDOR2, "vendor2"},
	{UMCPC_ACM_MODE_UNLINKED, "unlinked"},
	{0, NULL}
};

static char *ufoma_mode_to_str(int mode)
{
	int i;
	for(i = 0 ;umcpc_modetostr_tab[i].str != NULL; i++){
		if(umcpc_modetostr_tab[i].mode == mode){
			return umcpc_modetostr_tab[i].str;
		}
	}
	return NULL;
}

static int ufoma_str_to_mode(char *str)
{
	int i;
	for(i = 0 ;umcpc_modetostr_tab[i].str != NULL; i++){
		if(strcmp(str, umcpc_modetostr_tab[i].str)==0){
			return umcpc_modetostr_tab[i].mode;
		}
	}
	return -1;
}

static int ufoma_sysctl_support(SYSCTL_HANDLER_ARGS)
{
	struct ufoma_softc *sc = (struct ufoma_softc *)oidp->oid_arg1;
	struct sbuf sb;
	int i;
	char *mode;

	sbuf_new(&sb, NULL, 1, SBUF_AUTOEXTEND);
	for(i = 1; i < sc->sc_modetable[0]; i++){
		mode = ufoma_mode_to_str(sc->sc_modetable[i]);
		if(mode !=NULL){
			sbuf_cat(&sb, mode);
		}else{
			sbuf_printf(&sb, "(%02x)", sc->sc_modetable[i]);
		}
		if(i < (sc->sc_modetable[0]-1))
			sbuf_cat(&sb, ",");
	}
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);
	sbuf_delete(&sb);
	
	return 0;
}
static int ufoma_sysctl_current(SYSCTL_HANDLER_ARGS)
{
	struct ufoma_softc *sc = (struct ufoma_softc *)oidp->oid_arg1;
	char *mode;
	char subbuf[]="(XXX)";
	mode = ufoma_mode_to_str(sc->sc_currentmode);
	if(!mode){
		mode = subbuf;
		snprintf(subbuf, sizeof(subbuf), "(%02x)", sc->sc_currentmode);
	}
	sysctl_handle_string(oidp, mode, strlen(mode), req);
	
	return 0;
	
}
static int ufoma_sysctl_open(SYSCTL_HANDLER_ARGS)
{
	struct ufoma_softc *sc = (struct ufoma_softc *)oidp->oid_arg1;
	char *mode;
	char subbuf[40];
	int newmode;
	int error;
	int i;

	mode = ufoma_mode_to_str(sc->sc_modetoactivate);
	if(mode){
		strncpy(subbuf, mode, sizeof(subbuf));
	}else{
		snprintf(subbuf, sizeof(subbuf), "(%02x)", sc->sc_modetoactivate);
	}
	error = sysctl_handle_string(oidp, subbuf, sizeof(subbuf), req);
	if(error != 0 || req->newptr == NULL){
		return error;
	}
	
	if((newmode = ufoma_str_to_mode(subbuf)) == -1){
		return EINVAL;
	}
	
	for(i = 1 ; i < sc->sc_modetable[0] ; i++){
		if(sc->sc_modetable[i] == newmode){
			sc->sc_modetoactivate = newmode;
			return 0;
		}
	}
	
	return EINVAL;
}

static void
ufoma_poll(struct ucom_softc *ucom)
{
	struct ufoma_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_ctrl_xfer, UFOMA_CTRL_ENDPT_MAX);
	usbd_transfer_poll(sc->sc_bulk_xfer, UFOMA_BULK_ENDPT_MAX);
}
