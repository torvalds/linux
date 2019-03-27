/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Anybots Inc
 * written by Akinori Furukoshi <moonlightakkiy@yahoo.ca>
 *  - ucom part is based on u3g.c
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/bpf.h>
#include <net/ethernet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>

#include <net80211/ieee80211_ioctl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usie_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_msctest.h>

#include <dev/usb/serial/usb_serial.h>

#include <dev/usb/net/if_usievar.h>

#ifdef	USB_DEBUG
static int usie_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, usie, CTLFLAG_RW, 0, "sierra USB modem");
SYSCTL_INT(_hw_usb_usie, OID_AUTO, debug, CTLFLAG_RWTUN, &usie_debug, 0,
    "usie debug level");
#endif

/* Sierra Wireless Direct IP modems */
static const STRUCT_USB_HOST_ID usie_devs[] = {
#define	USIE_DEV(v, d) {				\
    USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##d) }
	USIE_DEV(SIERRA, MC8700),
	USIE_DEV(SIERRA, TRUINSTALL),
	USIE_DEV(AIRPRIME, USB308),
#undef	USIE_DEV
};

static device_probe_t usie_probe;
static device_attach_t usie_attach;
static device_detach_t usie_detach;
static void usie_free_softc(struct usie_softc *);

static void usie_free(struct ucom_softc *);
static void usie_uc_update_line_state(struct ucom_softc *, uint8_t);
static void usie_uc_cfg_get_status(struct ucom_softc *, uint8_t *, uint8_t *);
static void usie_uc_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void usie_uc_cfg_set_rts(struct ucom_softc *, uint8_t);
static void usie_uc_cfg_open(struct ucom_softc *);
static void usie_uc_cfg_close(struct ucom_softc *);
static void usie_uc_start_read(struct ucom_softc *);
static void usie_uc_stop_read(struct ucom_softc *);
static void usie_uc_start_write(struct ucom_softc *);
static void usie_uc_stop_write(struct ucom_softc *);

static usb_callback_t usie_uc_tx_callback;
static usb_callback_t usie_uc_rx_callback;
static usb_callback_t usie_uc_status_callback;
static usb_callback_t usie_if_tx_callback;
static usb_callback_t usie_if_rx_callback;
static usb_callback_t usie_if_status_callback;

static void usie_if_sync_to(void *);
static void usie_if_sync_cb(void *, int);
static void usie_if_status_cb(void *, int);

static void usie_if_start(struct ifnet *);
static int usie_if_output(struct ifnet *, struct mbuf *,
	const struct sockaddr *, struct route *);
static void usie_if_init(void *);
static void usie_if_stop(struct usie_softc *);
static int usie_if_ioctl(struct ifnet *, u_long, caddr_t);

static int usie_do_request(struct usie_softc *, struct usb_device_request *, void *);
static int usie_if_cmd(struct usie_softc *, uint8_t);
static void usie_cns_req(struct usie_softc *, uint32_t, uint16_t);
static void usie_cns_rsp(struct usie_softc *, struct usie_cns *);
static void usie_hip_rsp(struct usie_softc *, uint8_t *, uint32_t);
static int usie_driver_loaded(struct module *, int, void *);

static const struct usb_config usie_uc_config[USIE_UC_N_XFER] = {
	[USIE_UC_STATUS] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,		/* use wMaxPacketSize */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &usie_uc_status_callback,
	},
	[USIE_UC_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = USIE_BUFSIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.proxy_buffer = 1,},
		.callback = &usie_uc_rx_callback,
	},
	[USIE_UC_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = USIE_BUFSIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &usie_uc_tx_callback,
	}
};

static const struct usb_config usie_if_config[USIE_IF_N_XFER] = {
	[USIE_IF_STATUS] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,		/* use wMaxPacketSize */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &usie_if_status_callback,
	},
	[USIE_IF_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = USIE_BUFSIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &usie_if_rx_callback,
	},
	[USIE_IF_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = MAX(USIE_BUFSIZE, MCLBYTES),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &usie_if_tx_callback,
	}
};

static device_method_t usie_methods[] = {
	DEVMETHOD(device_probe, usie_probe),
	DEVMETHOD(device_attach, usie_attach),
	DEVMETHOD(device_detach, usie_detach),
	DEVMETHOD_END
};

static driver_t usie_driver = {
	.name = "usie",
	.methods = usie_methods,
	.size = sizeof(struct usie_softc),
};

static devclass_t usie_devclass;
static eventhandler_tag usie_etag;

DRIVER_MODULE(usie, uhub, usie_driver, usie_devclass, usie_driver_loaded, 0);
MODULE_DEPEND(usie, ucom, 1, 1, 1);
MODULE_DEPEND(usie, usb, 1, 1, 1);
MODULE_VERSION(usie, 1);
USB_PNP_HOST_INFO(usie_devs);

static const struct ucom_callback usie_uc_callback = {
	.ucom_cfg_get_status = &usie_uc_cfg_get_status,
	.ucom_cfg_set_dtr = &usie_uc_cfg_set_dtr,
	.ucom_cfg_set_rts = &usie_uc_cfg_set_rts,
	.ucom_cfg_open = &usie_uc_cfg_open,
	.ucom_cfg_close = &usie_uc_cfg_close,
	.ucom_start_read = &usie_uc_start_read,
	.ucom_stop_read = &usie_uc_stop_read,
	.ucom_start_write = &usie_uc_start_write,
	.ucom_stop_write = &usie_uc_stop_write,
	.ucom_free = &usie_free,
};

static void
usie_autoinst(void *arg, struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;
	struct usb_device_request req;
	int err;

	if (uaa->dev_state != UAA_DEV_READY)
		return;

	iface = usbd_get_iface(udev, 0);
	if (iface == NULL)
		return;

	id = iface->idesc;
	if (id == NULL || id->bInterfaceClass != UICLASS_MASS)
		return;

	if (usbd_lookup_id_by_uaa(usie_devs, sizeof(usie_devs), uaa) != 0)
		return;			/* no device match */

	if (bootverbose) {
		DPRINTF("Ejecting %s %s\n",
		    usb_get_manufacturer(udev),
		    usb_get_product(udev));
	}
	req.bmRequestType = UT_VENDOR;
	req.bRequest = UR_SET_INTERFACE;
	USETW(req.wValue, UF_DEVICE_REMOTE_WAKEUP);
	USETW(req.wIndex, UHF_PORT_CONNECTION);
	USETW(req.wLength, 0);

	/* at this moment there is no mutex */
	err = usbd_do_request_flags(udev, NULL, &req,
	    NULL, 0, NULL, 250 /* ms */ );

	/* success, mark the udev as disappearing */
	if (err == 0)
		uaa->dev_state = UAA_DEV_EJECTING;
}

static int
usie_probe(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != USIE_CNFG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != USIE_IFACE_INDEX)
		return (ENXIO);
	if (uaa->info.bInterfaceClass != UICLASS_VENDOR)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(usie_devs, sizeof(usie_devs), uaa));
}

static int
usie_attach(device_t self)
{
	struct usie_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct ifnet *ifp;
	struct usb_interface *iface;
	struct usb_interface_descriptor *id;
	struct usb_device_request req;
	int err;
	uint16_t fwattr;
	uint8_t iface_index;
	uint8_t ifidx;
	uint8_t start;

	device_set_usb_desc(self);
	sc->sc_udev = uaa->device;
	sc->sc_dev = self;

	mtx_init(&sc->sc_mtx, "usie", MTX_NETWORK_LOCK, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	TASK_INIT(&sc->sc_if_status_task, 0, usie_if_status_cb, sc);
	TASK_INIT(&sc->sc_if_sync_task, 0, usie_if_sync_cb, sc);

	usb_callout_init_mtx(&sc->sc_if_sync_ch, &sc->sc_mtx, 0);

	mtx_lock(&sc->sc_mtx);

	/* set power mode to D0 */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = USIE_POWER;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if (usie_do_request(sc, &req, NULL)) {
		mtx_unlock(&sc->sc_mtx);
		goto detach;
	}
	/* read fw attr */
	fwattr = 0;
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = USIE_FW_ATTR;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(fwattr));
	if (usie_do_request(sc, &req, &fwattr)) {
		mtx_unlock(&sc->sc_mtx);
		goto detach;
	}
	mtx_unlock(&sc->sc_mtx);

	/* check DHCP supports */
	DPRINTF("fwattr=%x\n", fwattr);
	if (!(fwattr & USIE_FW_DHCP)) {
		device_printf(self, "DHCP is not supported. A firmware upgrade might be needed.\n");
	}

	/* find available interfaces */
	sc->sc_nucom = 0;
	for (ifidx = 0; ifidx < USIE_IFACE_MAX; ifidx++) {
		iface = usbd_get_iface(uaa->device, ifidx);
		if (iface == NULL)
			break;

		id = usbd_get_interface_descriptor(iface);
		if ((id == NULL) || (id->bInterfaceClass != UICLASS_VENDOR))
			continue;

		/* setup Direct IP transfer */
		if (id->bInterfaceNumber >= 7 && id->bNumEndpoints == 3) {
			sc->sc_if_ifnum = id->bInterfaceNumber;
			iface_index = ifidx;

			DPRINTF("ifnum=%d, ifidx=%d\n",
			    sc->sc_if_ifnum, ifidx);

			err = usbd_transfer_setup(uaa->device,
			    &iface_index, sc->sc_if_xfer, usie_if_config,
			    USIE_IF_N_XFER, sc, &sc->sc_mtx);

			if (err == 0)
				continue;

			device_printf(self,
			    "could not allocate USB transfers on "
			    "iface_index=%d, err=%s\n",
			    iface_index, usbd_errstr(err));
			goto detach;
		}

		/* setup ucom */
		if (sc->sc_nucom >= USIE_UCOM_MAX)
			continue;

		usbd_set_parent_iface(uaa->device, ifidx,
		    uaa->info.bIfaceIndex);

		DPRINTF("NumEndpoints=%d bInterfaceNumber=%d\n",
		    id->bNumEndpoints, id->bInterfaceNumber);

		if (id->bNumEndpoints == 2) {
			sc->sc_uc_xfer[sc->sc_nucom][0] = NULL;
			start = 1;
		} else
			start = 0;

		err = usbd_transfer_setup(uaa->device, &ifidx,
		    sc->sc_uc_xfer[sc->sc_nucom] + start,
		    usie_uc_config + start, USIE_UC_N_XFER - start,
		    &sc->sc_ucom[sc->sc_nucom], &sc->sc_mtx);

		if (err != 0) {
			DPRINTF("usbd_transfer_setup error=%s\n", usbd_errstr(err));
			continue;
		}

		mtx_lock(&sc->sc_mtx);
		for (; start < USIE_UC_N_XFER; start++)
			usbd_xfer_set_stall(sc->sc_uc_xfer[sc->sc_nucom][start]);
		mtx_unlock(&sc->sc_mtx);

		sc->sc_uc_ifnum[sc->sc_nucom] = id->bInterfaceNumber;

		sc->sc_nucom++;		/* found a port */
	}

	if (sc->sc_nucom == 0) {
		device_printf(self, "no comports found\n");
		goto detach;
	}

	err = ucom_attach(&sc->sc_super_ucom, sc->sc_ucom,
	    sc->sc_nucom, sc, &usie_uc_callback, &sc->sc_mtx);

	if (err != 0) {
		DPRINTF("ucom_attach failed\n");
		goto detach;
	}
	DPRINTF("Found %d interfaces.\n", sc->sc_nucom);

	/* setup ifnet (Direct IP) */
	sc->sc_ifp = ifp = if_alloc(IFT_OTHER);

	if (ifp == NULL) {
		device_printf(self, "Could not allocate a network interface\n");
		goto detach;
	}
	if_initname(ifp, "usie", device_get_unit(self));

	ifp->if_softc = sc;
	ifp->if_mtu = USIE_MTU_MAX;
	ifp->if_flags |= IFF_NOARP;
	ifp->if_init = usie_if_init;
	ifp->if_ioctl = usie_if_ioctl;
	ifp->if_start = usie_if_start;
	ifp->if_output = usie_if_output;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	if_attach(ifp);
	bpfattach(ifp, DLT_RAW, 0);

	if (fwattr & USIE_PM_AUTO) {
		usbd_set_power_mode(uaa->device, USB_POWER_MODE_SAVE);
		DPRINTF("enabling automatic suspend and resume\n");
	} else {
		usbd_set_power_mode(uaa->device, USB_POWER_MODE_ON);
		DPRINTF("USB power is always ON\n");
	}

	DPRINTF("device attached\n");
	return (0);

detach:
	usie_detach(self);
	return (ENOMEM);
}

static int
usie_detach(device_t self)
{
	struct usie_softc *sc = device_get_softc(self);
	uint8_t x;

	/* detach ifnet */
	if (sc->sc_ifp != NULL) {
		usie_if_stop(sc);
		usbd_transfer_unsetup(sc->sc_if_xfer, USIE_IF_N_XFER);
		bpfdetach(sc->sc_ifp);
		if_detach(sc->sc_ifp);
		if_free(sc->sc_ifp);
		sc->sc_ifp = NULL;
	}
	/* detach ucom */
	if (sc->sc_nucom > 0)
		ucom_detach(&sc->sc_super_ucom, sc->sc_ucom);

	/* stop all USB transfers */
	usbd_transfer_unsetup(sc->sc_if_xfer, USIE_IF_N_XFER);

	for (x = 0; x != USIE_UCOM_MAX; x++)
		usbd_transfer_unsetup(sc->sc_uc_xfer[x], USIE_UC_N_XFER);


	device_claim_softc(self);

	usie_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(usie);

static void
usie_free_softc(struct usie_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
usie_free(struct ucom_softc *ucom)
{
	usie_free_softc(ucom->sc_parent);
}

static void
usie_uc_update_line_state(struct ucom_softc *ucom, uint8_t ls)
{
	struct usie_softc *sc = ucom->sc_parent;
	struct usb_device_request req;

	if (sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_STATUS] == NULL)
		return;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = USIE_LINK_STATE;
	USETW(req.wValue, ls);
	USETW(req.wIndex, sc->sc_uc_ifnum[ucom->sc_subunit]);
	USETW(req.wLength, 0);

	DPRINTF("sc_uc_ifnum=%d\n", sc->sc_uc_ifnum[ucom->sc_subunit]);

	usie_do_request(sc, &req, NULL);
}

static void
usie_uc_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct usie_softc *sc = ucom->sc_parent;

	*msr = sc->sc_msr;
	*lsr = sc->sc_lsr;
}

static void
usie_uc_cfg_set_dtr(struct ucom_softc *ucom, uint8_t flag)
{
	uint8_t dtr;

	dtr = flag ? USIE_LS_DTR : 0;
	usie_uc_update_line_state(ucom, dtr);
}

static void
usie_uc_cfg_set_rts(struct ucom_softc *ucom, uint8_t flag)
{
	uint8_t rts;

	rts = flag ? USIE_LS_RTS : 0;
	usie_uc_update_line_state(ucom, rts);
}

static void
usie_uc_cfg_open(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	/* usbd_transfer_start() is NULL safe */

	usbd_transfer_start(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_STATUS]);
}

static void
usie_uc_cfg_close(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_STATUS]);
}

static void
usie_uc_start_read(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_RX]);
}

static void
usie_uc_stop_read(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_RX]);
}

static void
usie_uc_start_write(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_TX]);
}

static void
usie_uc_stop_write(struct ucom_softc *ucom)
{
	struct usie_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_uc_xfer[ucom->sc_subunit][USIE_UC_TX]);
}

static void
usie_uc_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucom_softc *ucom = usbd_xfer_softc(xfer);
	struct usie_softc *sc = ucom->sc_parent;
	struct usb_page_cache *pc;
	uint32_t actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);

		/* handle CnS response */
		if (ucom == sc->sc_ucom && actlen >= USIE_HIPCNS_MIN) {

			DPRINTF("transferred=%u\n", actlen);

			/* check if it is really CnS reply */
			usbd_copy_out(pc, 0, sc->sc_resp_temp, 1);

			if (sc->sc_resp_temp[0] == USIE_HIP_FRM_CHR) {

				/* verify actlen */
				if (actlen > USIE_BUFSIZE)
					actlen = USIE_BUFSIZE;

				/* get complete message */
				usbd_copy_out(pc, 0, sc->sc_resp_temp, actlen);
				usie_hip_rsp(sc, sc->sc_resp_temp, actlen);

				/* need to fall though */
				goto tr_setup;
			}
			/* else call ucom_put_data() */
		}
		/* standard ucom transfer */
		ucom_put_data(ucom, pc, 0, actlen);

		/* fall though */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
usie_uc_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucom_softc *ucom = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);

		/* handle CnS request */
		struct mbuf *m = usbd_xfer_get_priv(xfer);

		if (m != NULL) {
			usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);
			usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len);
			usbd_xfer_set_priv(xfer, NULL);
			usbd_transfer_submit(xfer);
			m_freem(m);
			break;
		}
		/* standard ucom transfer */
		if (ucom_get_data(ucom, pc, 0, USIE_BUFSIZE, &actlen)) {
			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
usie_uc_status_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usb_page_cache *pc;
	struct {
		struct usb_device_request req;
		uint16_t param;
	}      st;
	uint32_t actlen;
	uint16_t param;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(4, "info received, actlen=%u\n", actlen);

		if (actlen < sizeof(st)) {
			DPRINTF("data too short actlen=%u\n", actlen);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &st, sizeof(st));

		if (st.req.bmRequestType == 0xa1 && st.req.bRequest == 0x20) {
			struct ucom_softc *ucom = usbd_xfer_softc(xfer);
			struct usie_softc *sc = ucom->sc_parent;

			param = le16toh(st.param);
			DPRINTF("param=%x\n", param);
			sc->sc_msr = sc->sc_lsr = 0;
			sc->sc_msr |= (param & USIE_DCD) ? SER_DCD : 0;
			sc->sc_msr |= (param & USIE_DSR) ? SER_DSR : 0;
			sc->sc_msr |= (param & USIE_RI) ? SER_RI : 0;
			sc->sc_msr |= (param & USIE_CTS) ? 0 : SER_CTS;
			sc->sc_msr |= (param & USIE_RTS) ? SER_RTS : 0;
			sc->sc_msr |= (param & USIE_DTR) ? SER_DTR : 0;
		}
		/* fall though */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF("USB transfer error, %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
usie_if_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usie_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = sc->sc_ifp;
	struct mbuf *m0;
	struct mbuf *m = NULL;
	struct usie_desc *rxd;
	uint32_t actlen;
	uint16_t err;
	uint16_t pkt;
	uint16_t ipl;
	uint16_t len;
	uint16_t diff;
	uint8_t pad;
	uint8_t ipv;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(15, "rx done, actlen=%u\n", actlen);

		if (actlen < sizeof(struct usie_hip)) {
			DPRINTF("data too short %u\n", actlen);
			goto tr_setup;
		}
		m = sc->sc_rxm;
		sc->sc_rxm = NULL;

		/* fall though */
	case USB_ST_SETUP:
tr_setup:

		if (sc->sc_rxm == NULL) {
			sc->sc_rxm = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
			    MJUMPAGESIZE /* could be bigger than MCLBYTES */ );
		}
		if (sc->sc_rxm == NULL) {
			DPRINTF("could not allocate Rx mbuf\n");
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			usbd_xfer_set_stall(xfer);
			usbd_xfer_set_frames(xfer, 0);
		} else {
			/*
			 * Directly loading a mbuf cluster into DMA to
			 * save some data copying. This works because
			 * there is only one cluster.
			 */
			usbd_xfer_set_frame_data(xfer, 0,
			    mtod(sc->sc_rxm, caddr_t), MIN(MJUMPAGESIZE, USIE_RXSZ_MAX));
			usbd_xfer_set_frames(xfer, 1);
		}
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF("USB transfer error, %s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		if (sc->sc_rxm != NULL) {
			m_freem(sc->sc_rxm);
			sc->sc_rxm = NULL;
		}
		break;
	}

	if (m == NULL)
		return;

	mtx_unlock(&sc->sc_mtx);

	m->m_pkthdr.len = m->m_len = actlen;

	err = pkt = 0;

	/* HW can aggregate multiple frames in a single USB xfer */
	for (;;) {
		rxd = mtod(m, struct usie_desc *);

		len = be16toh(rxd->hip.len) & USIE_HIP_IP_LEN_MASK;
		pad = (rxd->hip.id & USIE_HIP_PAD) ? 1 : 0;
		ipl = (len - pad - ETHER_HDR_LEN);
		if (ipl >= len) {
			DPRINTF("Corrupt frame\n");
			m_freem(m);
			break;
		}
		diff = sizeof(struct usie_desc) + ipl + pad;

		if (((rxd->hip.id & USIE_HIP_MASK) != USIE_HIP_IP) ||
		    (be16toh(rxd->desc_type) & USIE_TYPE_MASK) != USIE_IP_RX) {
			DPRINTF("received wrong type of packet\n");
			m->m_data += diff;
			m->m_pkthdr.len = (m->m_len -= diff);
			err++;
			if (m->m_pkthdr.len > 0)
				continue;
			m_freem(m);
			break;
		}
		switch (be16toh(rxd->ethhdr.ether_type)) {
		case ETHERTYPE_IP:
			ipv = NETISR_IP;
			break;
#ifdef INET6
		case ETHERTYPE_IPV6:
			ipv = NETISR_IPV6;
			break;
#endif
		default:
			DPRINTF("unsupported ether type\n");
			err++;
			break;
		}

		/* the last packet */
		if (m->m_pkthdr.len <= diff) {
			m->m_data += (sizeof(struct usie_desc) + pad);
			m->m_pkthdr.len = m->m_len = ipl;
			m->m_pkthdr.rcvif = ifp;
			BPF_MTAP(sc->sc_ifp, m);
			netisr_dispatch(ipv, m);
			break;
		}
		/* copy aggregated frames to another mbuf */
		m0 = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (__predict_false(m0 == NULL)) {
			DPRINTF("could not allocate mbuf\n");
			err++;
			m_freem(m);
			break;
		}
		m_copydata(m, sizeof(struct usie_desc) + pad, ipl, mtod(m0, caddr_t));
		m0->m_pkthdr.rcvif = ifp;
		m0->m_pkthdr.len = m0->m_len = ipl;

		BPF_MTAP(sc->sc_ifp, m0);
		netisr_dispatch(ipv, m0);

		m->m_data += diff;
		m->m_pkthdr.len = (m->m_len -= diff);
	}

	mtx_lock(&sc->sc_mtx);

	if_inc_counter(ifp, IFCOUNTER_IERRORS, err);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, pkt);
}

static void
usie_if_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usie_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	struct ifnet *ifp = sc->sc_ifp;
	struct mbuf *m;
	uint16_t size;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* fall though */
	case USB_ST_SETUP:
tr_setup:

		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		if (m->m_pkthdr.len > (int)(MCLBYTES - ETHER_HDR_LEN +
		    ETHER_CRC_LEN - sizeof(sc->sc_txd))) {
			DPRINTF("packet len is too big: %d\n",
			    m->m_pkthdr.len);
			break;
		}
		pc = usbd_xfer_get_frame(xfer, 0);

		sc->sc_txd.hip.len = htobe16(m->m_pkthdr.len +
		    ETHER_HDR_LEN + ETHER_CRC_LEN);
		size = sizeof(sc->sc_txd);

		usbd_copy_in(pc, 0, &sc->sc_txd, size);
		usbd_m_copy_in(pc, size, m, 0, m->m_pkthdr.len);
		usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len +
		    size + ETHER_CRC_LEN);

		BPF_MTAP(ifp, m);

		m_freem(m);

		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF("USB transfer error, %s\n",
		    usbd_errstr(error));
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		break;
	}
}

static void
usie_if_status_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usie_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	struct usb_cdc_notification cdc;
	uint32_t actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(4, "info received, actlen=%d\n", actlen);

		/* usb_cdc_notification - .data[16] */
		if (actlen < (sizeof(cdc) - 16)) {
			DPRINTF("data too short %d\n", actlen);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &cdc, (sizeof(cdc) - 16));

		DPRINTFN(4, "bNotification=%x\n", cdc.bNotification);

		if (cdc.bNotification & UCDC_N_RESPONSE_AVAILABLE) {
			taskqueue_enqueue(taskqueue_thread,
			    &sc->sc_if_status_task);
		}
		/* fall though */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF("USB transfer error, %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
usie_if_sync_to(void *arg)
{
	struct usie_softc *sc = arg;

	taskqueue_enqueue(taskqueue_thread, &sc->sc_if_sync_task);
}

static void
usie_if_sync_cb(void *arg, int pending)
{
	struct usie_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);

	/* call twice */
	usie_if_cmd(sc, USIE_HIP_SYNC2M);
	usie_if_cmd(sc, USIE_HIP_SYNC2M);

	usb_callout_reset(&sc->sc_if_sync_ch, 2 * hz, usie_if_sync_to, sc);

	mtx_unlock(&sc->sc_mtx);
}

static void
usie_if_status_cb(void *arg, int pending)
{
	struct usie_softc *sc = arg;
	struct ifnet *ifp = sc->sc_ifp;
	struct usb_device_request req;
	struct usie_hip *hip;
	struct usie_lsi *lsi;
	uint16_t actlen;
	uint8_t ntries;
	uint8_t pad;

	mtx_lock(&sc->sc_mtx);

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UCDC_GET_ENCAPSULATED_RESPONSE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_if_ifnum);
	USETW(req.wLength, sizeof(sc->sc_status_temp));

	for (ntries = 0; ntries != 10; ntries++) {
		int err;

		err = usbd_do_request_flags(sc->sc_udev,
		    &sc->sc_mtx, &req, sc->sc_status_temp, USB_SHORT_XFER_OK,
		    &actlen, USB_DEFAULT_TIMEOUT);

		if (err == 0)
			break;

		DPRINTF("Control request failed: %s %d/10\n",
		    usbd_errstr(err), ntries);

		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(10));
	}

	if (ntries == 10) {
		mtx_unlock(&sc->sc_mtx);
		DPRINTF("Timeout\n");
		return;
	}

	hip = (struct usie_hip *)sc->sc_status_temp;

	pad = (hip->id & USIE_HIP_PAD) ? 1 : 0;

	DPRINTF("hip.id=%x hip.len=%d actlen=%u pad=%d\n",
	    hip->id, be16toh(hip->len), actlen, pad);

	switch (hip->id & USIE_HIP_MASK) {
	case USIE_HIP_SYNC2H:
		usie_if_cmd(sc, USIE_HIP_SYNC2M);
		break;
	case USIE_HIP_RESTR:
		usb_callout_stop(&sc->sc_if_sync_ch);
		break;
	case USIE_HIP_UMTS:
		lsi = (struct usie_lsi *)(
		    sc->sc_status_temp + sizeof(struct usie_hip) + pad);

		DPRINTF("lsi.proto=%x lsi.len=%d\n", lsi->proto,
		    be16toh(lsi->len));

		if (lsi->proto != USIE_LSI_UMTS)
			break;

		if (lsi->area == USIE_LSI_AREA_NO ||
		    lsi->area == USIE_LSI_AREA_NODATA) {
			device_printf(sc->sc_dev, "no service available\n");
			break;
		}
		if (lsi->state == USIE_LSI_STATE_IDLE) {
			DPRINTF("lsi.state=%x\n", lsi->state);
			break;
		}
		DPRINTF("ctx=%x\n", hip->param);
		sc->sc_txd.hip.param = hip->param;

		sc->sc_net.addr_len = lsi->pdp_addr_len;
		memcpy(&sc->sc_net.dns1_addr, &lsi->dns1_addr, 16);
		memcpy(&sc->sc_net.dns2_addr, &lsi->dns2_addr, 16);
		memcpy(sc->sc_net.pdp_addr, lsi->pdp_addr, 16);
		memcpy(sc->sc_net.gw_addr, lsi->gw_addr, 16);
		ifp->if_flags |= IFF_UP;
		ifp->if_drv_flags |= IFF_DRV_RUNNING;

		device_printf(sc->sc_dev, "IP Addr=%d.%d.%d.%d\n",
		    *lsi->pdp_addr, *(lsi->pdp_addr + 1),
		    *(lsi->pdp_addr + 2), *(lsi->pdp_addr + 3));
		device_printf(sc->sc_dev, "Gateway Addr=%d.%d.%d.%d\n",
		    *lsi->gw_addr, *(lsi->gw_addr + 1),
		    *(lsi->gw_addr + 2), *(lsi->gw_addr + 3));
		device_printf(sc->sc_dev, "Prim NS Addr=%d.%d.%d.%d\n",
		    *lsi->dns1_addr, *(lsi->dns1_addr + 1),
		    *(lsi->dns1_addr + 2), *(lsi->dns1_addr + 3));
		device_printf(sc->sc_dev, "Scnd NS Addr=%d.%d.%d.%d\n",
		    *lsi->dns2_addr, *(lsi->dns2_addr + 1),
		    *(lsi->dns2_addr + 2), *(lsi->dns2_addr + 3));

		usie_cns_req(sc, USIE_CNS_ID_RSSI, USIE_CNS_OB_RSSI);
		break;

	case USIE_HIP_RCGI:
		/* ignore, workaround for sloppy windows */
		break;
	default:
		DPRINTF("undefined msgid: %x\n", hip->id);
		break;
	}

	mtx_unlock(&sc->sc_mtx);
}

static void
usie_if_start(struct ifnet *ifp)
{
	struct usie_softc *sc = ifp->if_softc;

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		DPRINTF("Not running\n");
		return;
	}
	mtx_lock(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_if_xfer[USIE_IF_TX]);
	mtx_unlock(&sc->sc_mtx);

	DPRINTFN(3, "interface started\n");
}

static int
usie_if_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
    struct route *ro)
{
	int err;

	DPRINTF("proto=%x\n", dst->sa_family);

	switch (dst->sa_family) {
#ifdef INET6
	case AF_INET6;
	/* fall though */
#endif
	case AF_INET:
		break;

		/* silently drop dhclient packets */
	case AF_UNSPEC:
		m_freem(m);
		return (0);

		/* drop other packet types */
	default:
		m_freem(m);
		return (EAFNOSUPPORT);
	}

	err = (ifp->if_transmit)(ifp, m);
	if (err) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (ENOBUFS);
	}
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

	return (0);
}

static void
usie_if_init(void *arg)
{
	struct usie_softc *sc = arg;
	struct ifnet *ifp = sc->sc_ifp;
	uint8_t i;

	mtx_lock(&sc->sc_mtx);

	/* write tx descriptor */
	sc->sc_txd.hip.id = USIE_HIP_CTX;
	sc->sc_txd.hip.param = 0;	/* init value */
	sc->sc_txd.desc_type = htobe16(USIE_IP_TX);

	for (i = 0; i != USIE_IF_N_XFER; i++)
		usbd_xfer_set_stall(sc->sc_if_xfer[i]);

	usbd_transfer_start(sc->sc_uc_xfer[USIE_HIP_IF][USIE_UC_RX]);
	usbd_transfer_start(sc->sc_if_xfer[USIE_IF_STATUS]);
	usbd_transfer_start(sc->sc_if_xfer[USIE_IF_RX]);

	/* if not running, initiate the modem */
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
		usie_cns_req(sc, USIE_CNS_ID_INIT, USIE_CNS_OB_LINK_UPDATE);

	mtx_unlock(&sc->sc_mtx);

	DPRINTF("ifnet initialized\n");
}

static void
usie_if_stop(struct usie_softc *sc)
{
	usb_callout_drain(&sc->sc_if_sync_ch);

	mtx_lock(&sc->sc_mtx);

	/* usie_cns_req() clears IFF_* flags */
	usie_cns_req(sc, USIE_CNS_ID_STOP, USIE_CNS_OB_LINK_UPDATE);

	usbd_transfer_stop(sc->sc_if_xfer[USIE_IF_TX]);
	usbd_transfer_stop(sc->sc_if_xfer[USIE_IF_RX]);
	usbd_transfer_stop(sc->sc_if_xfer[USIE_IF_STATUS]);

	/* shutdown device */
	usie_if_cmd(sc, USIE_HIP_DOWN);

	mtx_unlock(&sc->sc_mtx);
}

static int
usie_if_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct usie_softc *sc = ifp->if_softc;
	struct ieee80211req *ireq;
	struct ieee80211req_sta_info si;
	struct ifmediareq *ifmr;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
				usie_if_init(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				usie_if_stop(sc);
		}
		break;

	case SIOCSIFCAP:
		if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			device_printf(sc->sc_dev,
			    "Connect to the network first.\n");
			break;
		}
		mtx_lock(&sc->sc_mtx);
		usie_cns_req(sc, USIE_CNS_ID_RSSI, USIE_CNS_OB_RSSI);
		mtx_unlock(&sc->sc_mtx);
		break;

	case SIOCG80211:
		ireq = (struct ieee80211req *)data;

		if (ireq->i_type != IEEE80211_IOC_STA_INFO)
			break;

		memset(&si, 0, sizeof(si));
		si.isi_len = sizeof(si);
		/*
		 * ifconfig expects RSSI in 0.5dBm units
		 * relative to the noise floor.
		 */
		si.isi_rssi = 2 * sc->sc_rssi;
		if (copyout(&si, (uint8_t *)ireq->i_data + 8,
		    sizeof(struct ieee80211req_sta_info)))
			DPRINTF("copyout failed\n");
		DPRINTF("80211\n");
		break;

	case SIOCGIFMEDIA:		/* to fool ifconfig */
		ifmr = (struct ifmediareq *)data;
		ifmr->ifm_count = 1;
		DPRINTF("media\n");
		break;

	case SIOCSIFADDR:
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

static int
usie_do_request(struct usie_softc *sc, struct usb_device_request *req,
    void *data)
{
	int err = 0;
	int ntries;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	for (ntries = 0; ntries != 10; ntries++) {
		err = usbd_do_request(sc->sc_udev,
		    &sc->sc_mtx, req, data);
		if (err == 0)
			break;

		DPRINTF("Control request failed: %s %d/10\n",
		    usbd_errstr(err), ntries);

		usb_pause_mtx(&sc->sc_mtx, USB_MS_TO_TICKS(10));
	}
	return (err);
}

static int
usie_if_cmd(struct usie_softc *sc, uint8_t cmd)
{
	struct usb_device_request req;
	struct usie_hip msg;

	msg.len = 0;
	msg.id = cmd;
	msg.param = 0;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_ENCAPSULATED_COMMAND;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_if_ifnum);
	USETW(req.wLength, sizeof(msg));

	DPRINTF("cmd=%x\n", cmd);

	return (usie_do_request(sc, &req, &msg));
}

static void
usie_cns_req(struct usie_softc *sc, uint32_t id, uint16_t obj)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct mbuf *m;
	struct usb_xfer *xfer;
	struct usie_hip *hip;
	struct usie_cns *cns;
	uint8_t *param;
	uint8_t *tmp;
	uint8_t cns_len;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (__predict_false(m == NULL)) {
		DPRINTF("could not allocate mbuf\n");
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return;
	}
	/* to align usie_hip{} on 32 bit */
	m->m_data += 3;
	param = mtod(m, uint8_t *);
	*param++ = USIE_HIP_FRM_CHR;
	hip = (struct usie_hip *)param;
	cns = (struct usie_cns *)(hip + 1);

	tmp = param + USIE_HIPCNS_MIN - 2;

	switch (obj) {
	case USIE_CNS_OB_LINK_UPDATE:
		cns_len = 2;
		cns->op = USIE_CNS_OP_SET;
		*tmp++ = 1;		/* profile ID, always use 1 for now */
		*tmp++ = id == USIE_CNS_ID_INIT ? 1 : 0;
		break;

	case USIE_CNS_OB_PROF_WRITE:
		cns_len = 245;
		cns->op = USIE_CNS_OP_SET;
		*tmp++ = 1;		/* profile ID, always use 1 for now */
		*tmp++ = 2;
		memcpy(tmp, &sc->sc_net, 34);
		memset(tmp + 35, 0, 245 - 36);
		tmp += 243;
		break;

	case USIE_CNS_OB_RSSI:
		cns_len = 0;
		cns->op = USIE_CNS_OP_REQ;
		break;

	default:
		DPRINTF("unsupported CnS object type\n");
		return;
	}
	*tmp = USIE_HIP_FRM_CHR;

	hip->len = htobe16(sizeof(struct usie_cns) + cns_len);
	hip->id = USIE_HIP_CNS2M;
	hip->param = 0;			/* none for CnS */

	cns->obj = htobe16(obj);
	cns->id = htobe32(id);
	cns->len = cns_len;
	cns->rsv0 = cns->rsv1 = 0;	/* always '0' */

	param = (uint8_t *)(cns + 1);

	DPRINTF("param: %16D\n", param, ":");

	m->m_pkthdr.len = m->m_len = USIE_HIPCNS_MIN + cns_len + 2;

	xfer = sc->sc_uc_xfer[USIE_HIP_IF][USIE_UC_TX];

	if (usbd_xfer_get_priv(xfer) == NULL) {
		usbd_xfer_set_priv(xfer, m);
		usbd_transfer_start(xfer);
	} else {
		DPRINTF("Dropped CNS event\n");
		m_freem(m);
	}
}

static void
usie_cns_rsp(struct usie_softc *sc, struct usie_cns *cns)
{
	struct ifnet *ifp = sc->sc_ifp;

	DPRINTF("received CnS\n");

	switch (be16toh(cns->obj)) {
	case USIE_CNS_OB_LINK_UPDATE:
		if (be32toh(cns->id) & USIE_CNS_ID_INIT)
			usie_if_sync_to(sc);
		else if (be32toh(cns->id) & USIE_CNS_ID_STOP) {
			ifp->if_flags &= ~IFF_UP;
			ifp->if_drv_flags &=
			    ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
		} else
			DPRINTF("undefined link update\n");
		break;

	case USIE_CNS_OB_RSSI:
		sc->sc_rssi = be16toh(*(int16_t *)(cns + 1));
		if (sc->sc_rssi <= 0)
			device_printf(sc->sc_dev, "No signal\n");
		else {
			device_printf(sc->sc_dev, "RSSI=%ddBm\n",
			    sc->sc_rssi - 110);
		}
		break;

	case USIE_CNS_OB_PROF_WRITE:
		break;

	case USIE_CNS_OB_PDP_READ:
		break;

	default:
		DPRINTF("undefined CnS\n");
		break;
	}
}

static void
usie_hip_rsp(struct usie_softc *sc, uint8_t *rsp, uint32_t len)
{
	struct usie_hip *hip;
	struct usie_cns *cns;
	uint32_t i;
	uint32_t j;
	uint32_t off;
	uint8_t tmp[USIE_HIPCNS_MAX] __aligned(4);

	for (off = 0; (off + USIE_HIPCNS_MIN) <= len; off++) {

		uint8_t pad;

		while ((off < len) && (rsp[off] == USIE_HIP_FRM_CHR))
			off++;

		/* Unstuff the bytes */
		for (i = j = 0; ((i + off) < len) &&
		    (j < USIE_HIPCNS_MAX); i++) {

			if (rsp[i + off] == USIE_HIP_FRM_CHR)
				break;

			if (rsp[i + off] == USIE_HIP_ESC_CHR) {
				if ((i + off + 1) >= len)
					break;
				tmp[j++] = rsp[i++ + off + 1] ^ 0x20;
			} else {
				tmp[j++] = rsp[i + off];
			}
		}

		off += i;

		DPRINTF("frame len=%d\n", j);

		if (j < sizeof(struct usie_hip)) {
			DPRINTF("too little data\n");
			break;
		}
		/*
		 * Make sure we are not reading the stack if something
		 * is wrong.
		 */
		memset(tmp + j, 0, sizeof(tmp) - j);

		hip = (struct usie_hip *)tmp;

		DPRINTF("hip: len=%d msgID=%02x, param=%02x\n",
		    be16toh(hip->len), hip->id, hip->param);

		pad = (hip->id & USIE_HIP_PAD) ? 1 : 0;

		if ((hip->id & USIE_HIP_MASK) == USIE_HIP_CNS2H) {
			cns = (struct usie_cns *)(((uint8_t *)(hip + 1)) + pad);

			if (j < (sizeof(struct usie_cns) +
			    sizeof(struct usie_hip) + pad)) {
				DPRINTF("too little data\n");
				break;
			}
			DPRINTF("cns: obj=%04x, op=%02x, rsv0=%02x, "
			    "app=%08x, rsv1=%02x, len=%d\n",
			    be16toh(cns->obj), cns->op, cns->rsv0,
			    be32toh(cns->id), cns->rsv1, cns->len);

			if (cns->op & USIE_CNS_OP_ERR)
				DPRINTF("CnS error response\n");
			else
				usie_cns_rsp(sc, cns);

			i = sizeof(struct usie_hip) + pad + sizeof(struct usie_cns);
			j = cns->len;
		} else {
			i = sizeof(struct usie_hip) + pad;
			j = be16toh(hip->len);
		}
#ifdef	USB_DEBUG
		if (usie_debug == 0)
			continue;

		while (i < USIE_HIPCNS_MAX && j > 0) {
			DPRINTF("param[0x%02x] = 0x%02x\n", i, tmp[i]);
			i++;
			j--;
		}
#endif
	}
}

static int
usie_driver_loaded(struct module *mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* register autoinstall handler */
		usie_etag = EVENTHANDLER_REGISTER(usb_dev_configured,
		    usie_autoinst, NULL, EVENTHANDLER_PRI_ANY);
		break;
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(usb_dev_configured, usie_etag);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

