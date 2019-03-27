/*	$OpenBSD: if_urndis.c,v 1.46 2013/12/09 15:45:29 pirofti Exp $ */

/*
 * Copyright (c) 2010 Jonathan Armani <armani@openbsd.org>
 * Copyright (c) 2010 Fabien Romano <fabien@openbsd.org>
 * Copyright (c) 2010 Michael Knudsen <mk@openbsd.org>
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@freebsd.org>
 * All rights reserved.
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
#include <net/rndis.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR urndis_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include "usb_if.h"

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_urndisreg.h>

#include <dev/usb/usb_cdc.h>

static device_probe_t urndis_probe;
static device_attach_t urndis_attach;
static device_detach_t urndis_detach;
static device_suspend_t urndis_suspend;
static device_resume_t urndis_resume;

static usb_callback_t urndis_bulk_write_callback;
static usb_callback_t urndis_bulk_read_callback;
static usb_callback_t urndis_intr_read_callback;

static uether_fn_t urndis_attach_post;
static uether_fn_t urndis_init;
static uether_fn_t urndis_stop;
static uether_fn_t urndis_start;
static uether_fn_t urndis_setmulti;
static uether_fn_t urndis_setpromisc;

static uint32_t	urndis_ctrl_query(struct urndis_softc *sc, uint32_t oid,
		    struct rndis_query_req *msg, uint16_t len,
		    const void **rbuf, uint16_t *rbufsz);
static uint32_t	urndis_ctrl_set(struct urndis_softc *sc, uint32_t oid,
		    struct rndis_set_req *msg, uint16_t len);
static uint32_t	urndis_ctrl_handle_init(struct urndis_softc *sc,
		    const struct rndis_comp_hdr *hdr);
static uint32_t	urndis_ctrl_handle_query(struct urndis_softc *sc,
		    const struct rndis_comp_hdr *hdr, const void **buf,
		    uint16_t *bufsz);
static uint32_t	urndis_ctrl_handle_reset(struct urndis_softc *sc,
		    const struct rndis_comp_hdr *hdr);
static uint32_t	urndis_ctrl_init(struct urndis_softc *sc);
static uint32_t	urndis_ctrl_halt(struct urndis_softc *sc);

#ifdef USB_DEBUG
static int urndis_debug = 0;
static	SYSCTL_NODE(_hw_usb, OID_AUTO, urndis, CTLFLAG_RW, 0, "USB RNDIS-Ethernet");
SYSCTL_INT(_hw_usb_urndis, OID_AUTO, debug, CTLFLAG_RWTUN, &urndis_debug, 0,
    "Debug level");
#endif

static const struct usb_config urndis_config[URNDIS_N_TRANSFER] = {
	[URNDIS_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 0,
		.frames = 1,
		.bufsize = RNDIS_RX_MAXLEN,
		.flags = {.short_xfer_ok = 1,},
		.callback = urndis_bulk_read_callback,
		.timeout = 0,		/* no timeout */
		.usb_mode = USB_MODE_HOST,
	},

	[URNDIS_BULK_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.if_index = 0,
		.frames = RNDIS_TX_FRAMES_MAX,
		.bufsize = (RNDIS_TX_FRAMES_MAX * RNDIS_TX_MAXLEN),
		.flags = {
			.force_short_xfer = 1,
		},
		.callback = urndis_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
		.usb_mode = USB_MODE_HOST,
	},

	[URNDIS_INTR_RX] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.if_index = 1,
		.bufsize = 0,	/* use wMaxPacketSize */
		.flags = {.short_xfer_ok = 1,.no_pipe_ok = 1,},
		.callback = urndis_intr_read_callback,
		.timeout = 0,
		.usb_mode = USB_MODE_HOST,
	},
};

static device_method_t urndis_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, urndis_probe),
	DEVMETHOD(device_attach, urndis_attach),
	DEVMETHOD(device_detach, urndis_detach),
	DEVMETHOD(device_suspend, urndis_suspend),
	DEVMETHOD(device_resume, urndis_resume),

	DEVMETHOD_END
};

static driver_t urndis_driver = {
	.name = "urndis",
	.methods = urndis_methods,
	.size = sizeof(struct urndis_softc),
};

static devclass_t urndis_devclass;

static const STRUCT_USB_HOST_ID urndis_host_devs[] = {
	/* Generic RNDIS class match */
	{USB_IFACE_CLASS(UICLASS_CDC),
		USB_IFACE_SUBCLASS(UISUBCLASS_ABSTRACT_CONTROL_MODEL),
		USB_IFACE_PROTOCOL(0xff)},
	{USB_IFACE_CLASS(UICLASS_WIRELESS), USB_IFACE_SUBCLASS(UISUBCLASS_RF),
		USB_IFACE_PROTOCOL(UIPROTO_RNDIS)},
	{USB_IFACE_CLASS(UICLASS_IAD), USB_IFACE_SUBCLASS(UISUBCLASS_SYNC),
		USB_IFACE_PROTOCOL(UIPROTO_ACTIVESYNC)},
	/* HP-WebOS */
	{USB_VENDOR(USB_VENDOR_PALM), USB_IFACE_CLASS(UICLASS_CDC),
		USB_IFACE_SUBCLASS(UISUBCLASS_ABSTRACT_CONTROL_MODEL),
		USB_IFACE_PROTOCOL(0xff)},
};

DRIVER_MODULE(urndis, uhub, urndis_driver, urndis_devclass, NULL, NULL);
MODULE_VERSION(urndis, 1);
MODULE_DEPEND(urndis, uether, 1, 1, 1);
MODULE_DEPEND(urndis, usb, 1, 1, 1);
MODULE_DEPEND(urndis, ether, 1, 1, 1);
USB_PNP_HOST_INFO(urndis_host_devs);

static const struct usb_ether_methods urndis_ue_methods = {
	.ue_attach_post = urndis_attach_post,
	.ue_start = urndis_start,
	.ue_init = urndis_init,
	.ue_stop = urndis_stop,
	.ue_setmulti = urndis_setmulti,
	.ue_setpromisc = urndis_setpromisc,
};

static int
urndis_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	return (usbd_lookup_id_by_uaa(urndis_host_devs, sizeof(urndis_host_devs), uaa));
}

static void
urndis_attach_post(struct usb_ether *ue)
{

	/* no-op */
}

static int
urndis_attach(device_t dev)
{
	static struct {
		union {
			struct rndis_query_req query;
			struct rndis_set_req set;
		} hdr;
		union {
			uint8_t eaddr[ETHER_ADDR_LEN];
			uint32_t filter;
		} ibuf;
	} msg;
	struct urndis_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_cdc_cm_descriptor *cmd;
	const void *buf;
	uint16_t bufsz;
	uint8_t iface_index[2] = { uaa->info.bIfaceIndex + 1, uaa->info.bIfaceIndex };
	int error;
	uint8_t i;

	sc->sc_ue.ue_udev = uaa->device;
	sc->sc_ifaceno_ctl = uaa->info.bIfaceNum;

	cmd = usbd_find_descriptor(uaa->device, NULL, uaa->info.bIfaceIndex,
	    UDESC_CS_INTERFACE, 0xFF, UDESCSUB_CDC_CM, 0xFF);
	if (cmd != NULL) {
		DPRINTF("Call Mode Descriptor found, dataif=%d\n", cmd->bDataInterface);
		iface_index[0] = cmd->bDataInterface;
	}

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* scan the alternate settings looking for a valid one */
	for (i = 0; i != 32; i++) {
		error = usbd_set_alt_interface_index(uaa->device,
		    iface_index[0], i);

		if (error != 0)
			break;

		error = usbd_transfer_setup(uaa->device,
		    iface_index, sc->sc_xfer, urndis_config,
		    URNDIS_N_TRANSFER, sc, &sc->sc_mtx);

		if (error == 0)
			break;
	}
	if ((error != 0) || (i == 32)) {
		device_printf(dev, "No valid alternate setting found\n");
		goto detach;
	}

	/* Initialize device - must be done before even querying it */
	URNDIS_LOCK(sc);
	error = urndis_ctrl_init(sc);
	URNDIS_UNLOCK(sc);
	if (error != (int)RNDIS_STATUS_SUCCESS) {
		device_printf(dev, "Unable to initialize hardware\n");
		goto detach;
	}

	/* Determine MAC address */
	memset(msg.ibuf.eaddr, 0, sizeof(msg.ibuf.eaddr));
	URNDIS_LOCK(sc);
	error = urndis_ctrl_query(sc, OID_802_3_PERMANENT_ADDRESS,
	    &msg.hdr.query, sizeof(msg.hdr.query) + sizeof(msg.ibuf.eaddr),
	    &buf, &bufsz);
	URNDIS_UNLOCK(sc);
	if (error != (int)RNDIS_STATUS_SUCCESS) {
		device_printf(dev, "Unable to get hardware address\n");
		goto detach;
	}
	if (bufsz != ETHER_ADDR_LEN) {
		device_printf(dev, "Invalid address length: %d bytes\n", bufsz);
		goto detach;
	}
	memcpy(&sc->sc_ue.ue_eaddr, buf, ETHER_ADDR_LEN);

	/* Initialize packet filter */
	sc->sc_filter = NDIS_PACKET_TYPE_BROADCAST |
	    NDIS_PACKET_TYPE_ALL_MULTICAST;
	msg.ibuf.filter = htole32(sc->sc_filter);
	URNDIS_LOCK(sc);
	error = urndis_ctrl_set(sc, OID_GEN_CURRENT_PACKET_FILTER,
	    &msg.hdr.set, sizeof(msg.hdr.set) + sizeof(msg.ibuf.filter));
	URNDIS_UNLOCK(sc);
	if (error != (int)RNDIS_STATUS_SUCCESS) {
		device_printf(dev, "Unable to set data filters\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &urndis_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "Could not attach interface\n");
		goto detach;
	}

	URNDIS_LOCK(sc);
	/* start interrupt endpoint, if any */
	usbd_transfer_start(sc->sc_xfer[URNDIS_INTR_RX]);
	URNDIS_UNLOCK(sc);

	return (0);			/* success */

detach:
	(void)urndis_detach(dev);
	return (ENXIO);			/* failure */
}

static int
urndis_detach(device_t dev)
{
	struct urndis_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	/* stop all USB transfers first */
	usbd_transfer_unsetup(sc->sc_xfer, URNDIS_N_TRANSFER);

	uether_ifdetach(ue);

	URNDIS_LOCK(sc);
	(void)urndis_ctrl_halt(sc);
	URNDIS_UNLOCK(sc);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
urndis_start(struct usb_ether *ue)
{
	struct urndis_softc *sc = uether_getsc(ue);

	/*
	 * Start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[URNDIS_BULK_TX]);
	usbd_transfer_start(sc->sc_xfer[URNDIS_BULK_RX]);
}

static void
urndis_init(struct usb_ether *ue)
{
	struct urndis_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	URNDIS_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* stall data write direction, which depends on USB mode */
	usbd_xfer_set_stall(sc->sc_xfer[URNDIS_BULK_TX]);

	/* start data transfers */
	urndis_start(ue);
}

static void
urndis_stop(struct usb_ether *ue)
{
	struct urndis_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	URNDIS_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[URNDIS_BULK_RX]);
	usbd_transfer_stop(sc->sc_xfer[URNDIS_BULK_TX]);
}

static void
urndis_setmulti(struct usb_ether *ue)
{

	/* no-op */
}

static void
urndis_setpromisc(struct usb_ether *ue)
{

	/* no-op */
}

static int
urndis_suspend(device_t dev)
{

	device_printf(dev, "Suspending\n");
	return (0);
}

static int
urndis_resume(device_t dev)
{

	device_printf(dev, "Resuming\n");
	return (0);
}

static usb_error_t
urndis_ctrl_msg(struct urndis_softc *sc, uint8_t rt, uint8_t r,
    uint16_t index, uint16_t value, void *buf, uint16_t buflen)
{
	usb_device_request_t req;

	req.bmRequestType = rt;
	req.bRequest = r;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, buflen);

	return (usbd_do_request_flags(sc->sc_ue.ue_udev,
	    &sc->sc_mtx, &req, buf, (rt & UT_READ) ?
	    USB_SHORT_XFER_OK : 0, NULL, 2000 /* ms */ ));
}

static usb_error_t
urndis_ctrl_send(struct urndis_softc *sc, void *buf, uint16_t len)
{
	usb_error_t err;

	err = urndis_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE,
	    UCDC_SEND_ENCAPSULATED_COMMAND, sc->sc_ifaceno_ctl, 0, buf, len);

	DPRINTF("%s\n", usbd_errstr(err));

	return (err);
}

static struct rndis_comp_hdr *
urndis_ctrl_recv(struct urndis_softc *sc)
{
	struct rndis_comp_hdr *hdr;
	usb_error_t err;

	err = urndis_ctrl_msg(sc, UT_READ_CLASS_INTERFACE,
	    UCDC_GET_ENCAPSULATED_RESPONSE, sc->sc_ifaceno_ctl, 0,
	    sc->sc_response_buf, RNDIS_RESPONSE_LEN);

	if (err != USB_ERR_NORMAL_COMPLETION)
		return (NULL);

	hdr = (struct rndis_comp_hdr *)sc->sc_response_buf;

	DPRINTF("type 0x%x len %u\n", le32toh(hdr->rm_type),
	    le32toh(hdr->rm_len));

	if (le32toh(hdr->rm_len) > RNDIS_RESPONSE_LEN) {
		DPRINTF("ctrl message error: wrong size %u > %u\n",
		    le32toh(hdr->rm_len), RNDIS_RESPONSE_LEN);
		return (NULL);
	}
	return (hdr);
}

static uint32_t
urndis_ctrl_handle(struct urndis_softc *sc, struct rndis_comp_hdr *hdr,
    const void **buf, uint16_t *bufsz)
{
	uint32_t rval;

	DPRINTF("\n");

	if (buf != NULL && bufsz != NULL) {
		*buf = NULL;
		*bufsz = 0;
	}
	switch (le32toh(hdr->rm_type)) {
	case REMOTE_NDIS_INITIALIZE_CMPLT:
		rval = urndis_ctrl_handle_init(sc, hdr);
		break;

	case REMOTE_NDIS_QUERY_CMPLT:
		rval = urndis_ctrl_handle_query(sc, hdr, buf, bufsz);
		break;

	case REMOTE_NDIS_RESET_CMPLT:
		rval = urndis_ctrl_handle_reset(sc, hdr);
		break;

	case REMOTE_NDIS_KEEPALIVE_CMPLT:
	case REMOTE_NDIS_SET_CMPLT:
		rval = le32toh(hdr->rm_status);
		break;

	default:
		device_printf(sc->sc_ue.ue_dev,
		    "ctrl message error: unknown event 0x%x\n",
		    le32toh(hdr->rm_type));
		rval = RNDIS_STATUS_FAILURE;
		break;
	}
	return (rval);
}

static uint32_t
urndis_ctrl_handle_init(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr)
{
	const struct rndis_init_comp *msg;

	msg = (const struct rndis_init_comp *)hdr;

	DPRINTF("len %u rid %u status 0x%x "
	    "ver_major %u ver_minor %u devflags 0x%x medium 0x%x pktmaxcnt %u "
	    "pktmaxsz %u align %u aflistoffset %u aflistsz %u\n",
	    le32toh(msg->rm_len),
	    le32toh(msg->rm_rid),
	    le32toh(msg->rm_status),
	    le32toh(msg->rm_ver_major),
	    le32toh(msg->rm_ver_minor),
	    le32toh(msg->rm_devflags),
	    le32toh(msg->rm_medium),
	    le32toh(msg->rm_pktmaxcnt),
	    le32toh(msg->rm_pktmaxsz),
	    le32toh(msg->rm_align),
	    le32toh(msg->rm_aflistoffset),
	    le32toh(msg->rm_aflistsz));

	if (le32toh(msg->rm_status) != RNDIS_STATUS_SUCCESS) {
		DPRINTF("init failed 0x%x\n", le32toh(msg->rm_status));
		return (le32toh(msg->rm_status));
	}
	if (le32toh(msg->rm_devflags) != RNDIS_DF_CONNECTIONLESS) {
		DPRINTF("wrong device type (current type: 0x%x)\n",
		    le32toh(msg->rm_devflags));
		return (RNDIS_STATUS_FAILURE);
	}
	if (le32toh(msg->rm_medium) != RNDIS_MEDIUM_802_3) {
		DPRINTF("medium not 802.3 (current medium: 0x%x)\n",
		    le32toh(msg->rm_medium));
		return (RNDIS_STATUS_FAILURE);
	}
	sc->sc_lim_pktsz = le32toh(msg->rm_pktmaxsz);

	return (le32toh(msg->rm_status));
}

static uint32_t
urndis_ctrl_handle_query(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr, const void **buf, uint16_t *bufsz)
{
	const struct rndis_query_comp *msg;
	uint64_t limit;

	msg = (const struct rndis_query_comp *)hdr;

	DPRINTF("len %u rid %u status 0x%x "
	    "buflen %u bufoff %u\n",
	    le32toh(msg->rm_len),
	    le32toh(msg->rm_rid),
	    le32toh(msg->rm_status),
	    le32toh(msg->rm_infobuflen),
	    le32toh(msg->rm_infobufoffset));

	*buf = NULL;
	*bufsz = 0;
	if (le32toh(msg->rm_status) != RNDIS_STATUS_SUCCESS) {
		DPRINTF("query failed 0x%x\n", le32toh(msg->rm_status));
		return (le32toh(msg->rm_status));
	}
	limit = le32toh(msg->rm_infobuflen);
	limit += le32toh(msg->rm_infobufoffset);
	limit += RNDIS_HEADER_OFFSET;

	if (limit > (uint64_t)le32toh(msg->rm_len)) {
		DPRINTF("ctrl message error: invalid query info "
		    "len/offset/end_position(%u/%u/%u) -> "
		    "go out of buffer limit %u\n",
		    le32toh(msg->rm_infobuflen),
		    le32toh(msg->rm_infobufoffset),
		    le32toh(msg->rm_infobuflen) +
		    le32toh(msg->rm_infobufoffset) + RNDIS_HEADER_OFFSET,
		    le32toh(msg->rm_len));
		return (RNDIS_STATUS_FAILURE);
	}
	*buf = ((const uint8_t *)msg) + RNDIS_HEADER_OFFSET +
	    le32toh(msg->rm_infobufoffset);
	*bufsz = le32toh(msg->rm_infobuflen);

	return (le32toh(msg->rm_status));
}

static uint32_t
urndis_ctrl_handle_reset(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr)
{
	const struct rndis_reset_comp *msg;
	uint32_t rval;

	msg = (const struct rndis_reset_comp *)hdr;

	rval = le32toh(msg->rm_status);

	DPRINTF("len %u status 0x%x "
	    "adrreset %u\n",
	    le32toh(msg->rm_len),
	    rval,
	    le32toh(msg->rm_adrreset));

	if (rval != RNDIS_STATUS_SUCCESS) {
		DPRINTF("reset failed 0x%x\n", rval);
		return (rval);
	}
	if (msg->rm_adrreset != 0) {
		struct {
			struct rndis_set_req hdr;
			uint32_t filter;
		} msg_filter;

		msg_filter.filter = htole32(sc->sc_filter);

		rval = urndis_ctrl_set(sc, OID_GEN_CURRENT_PACKET_FILTER,
		    &msg_filter.hdr, sizeof(msg_filter));

		if (rval != RNDIS_STATUS_SUCCESS) {
			DPRINTF("unable to reset data filters\n");
			return (rval);
		}
	}
	return (rval);
}

static uint32_t
urndis_ctrl_init(struct urndis_softc *sc)
{
	struct rndis_init_req msg;
	struct rndis_comp_hdr *hdr;
	uint32_t rval;

	msg.rm_type = htole32(REMOTE_NDIS_INITIALIZE_MSG);
	msg.rm_len = htole32(sizeof(msg));
	msg.rm_rid = 0;
	msg.rm_ver_major = htole32(RNDIS_VERSION_MAJOR);
	msg.rm_ver_minor = htole32(1);
	msg.rm_max_xfersz = htole32(RNDIS_RX_MAXLEN);

	DPRINTF("type %u len %u rid %u ver_major %u "
	    "ver_minor %u max_xfersz %u\n",
	    le32toh(msg.rm_type),
	    le32toh(msg.rm_len),
	    le32toh(msg.rm_rid),
	    le32toh(msg.rm_ver_major),
	    le32toh(msg.rm_ver_minor),
	    le32toh(msg.rm_max_xfersz));

	rval = urndis_ctrl_send(sc, &msg, sizeof(msg));

	if (rval != RNDIS_STATUS_SUCCESS) {
		DPRINTF("init failed\n");
		return (rval);
	}
	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		DPRINTF("unable to get init response\n");
		return (RNDIS_STATUS_FAILURE);
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);

	return (rval);
}

static uint32_t
urndis_ctrl_halt(struct urndis_softc *sc)
{
	struct rndis_halt_req msg;
	uint32_t rval;

	msg.rm_type = htole32(REMOTE_NDIS_HALT_MSG);
	msg.rm_len = htole32(sizeof(msg));
	msg.rm_rid = 0;

	DPRINTF("type %u len %u rid %u\n",
	    le32toh(msg.rm_type),
	    le32toh(msg.rm_len),
	    le32toh(msg.rm_rid));

	rval = urndis_ctrl_send(sc, &msg, sizeof(msg));

	if (rval != RNDIS_STATUS_SUCCESS)
		DPRINTF("halt failed\n");

	return (rval);
}

/*
 * NB: Querying a device has the requirement of using an input buffer the size
 *     of the expected reply or larger, except for variably sized replies.
 */
static uint32_t
urndis_ctrl_query(struct urndis_softc *sc, uint32_t oid,
    struct rndis_query_req *msg, uint16_t len, const void **rbuf,
    uint16_t *rbufsz)
{
	struct rndis_comp_hdr *hdr;
	uint32_t datalen, rval;

	msg->rm_type = htole32(REMOTE_NDIS_QUERY_MSG);
	msg->rm_len = htole32(len);
	msg->rm_rid = 0;		/* XXX */
	msg->rm_oid = htole32(oid);
	datalen = len - sizeof(*msg);
	msg->rm_infobuflen = htole32(datalen);
	if (datalen != 0) {
		msg->rm_infobufoffset = htole32(sizeof(*msg) -
		    RNDIS_HEADER_OFFSET);
	} else {
		msg->rm_infobufoffset = 0;
	}
	msg->rm_devicevchdl = 0;

	DPRINTF("type %u len %u rid %u oid 0x%x "
	    "infobuflen %u infobufoffset %u devicevchdl %u\n",
	    le32toh(msg->rm_type),
	    le32toh(msg->rm_len),
	    le32toh(msg->rm_rid),
	    le32toh(msg->rm_oid),
	    le32toh(msg->rm_infobuflen),
	    le32toh(msg->rm_infobufoffset),
	    le32toh(msg->rm_devicevchdl));

	rval = urndis_ctrl_send(sc, msg, len);

	if (rval != RNDIS_STATUS_SUCCESS) {
		DPRINTF("query failed\n");
		return (rval);
	}
	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		DPRINTF("unable to get query response\n");
		return (RNDIS_STATUS_FAILURE);
	}
	rval = urndis_ctrl_handle(sc, hdr, rbuf, rbufsz);

	return (rval);
}

static uint32_t
urndis_ctrl_set(struct urndis_softc *sc, uint32_t oid,
    struct rndis_set_req *msg, uint16_t len)
{
	struct rndis_comp_hdr *hdr;
	uint32_t datalen, rval;

	msg->rm_type = htole32(REMOTE_NDIS_SET_MSG);
	msg->rm_len = htole32(len);
	msg->rm_rid = 0;		/* XXX */
	msg->rm_oid = htole32(oid);
	datalen = len - sizeof(*msg);
	msg->rm_infobuflen = htole32(datalen);
	if (datalen != 0) {
		msg->rm_infobufoffset = htole32(sizeof(*msg) -
		    RNDIS_HEADER_OFFSET);
	} else {
		msg->rm_infobufoffset = 0;
	}
	msg->rm_devicevchdl = 0;

	DPRINTF("type %u len %u rid %u oid 0x%x "
	    "infobuflen %u infobufoffset %u devicevchdl %u\n",
	    le32toh(msg->rm_type),
	    le32toh(msg->rm_len),
	    le32toh(msg->rm_rid),
	    le32toh(msg->rm_oid),
	    le32toh(msg->rm_infobuflen),
	    le32toh(msg->rm_infobufoffset),
	    le32toh(msg->rm_devicevchdl));

	rval = urndis_ctrl_send(sc, msg, len);

	if (rval != RNDIS_STATUS_SUCCESS) {
		DPRINTF("set failed\n");
		return (rval);
	}
	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		DPRINTF("unable to get set response\n");
		return (RNDIS_STATUS_FAILURE);
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);
	if (rval != RNDIS_STATUS_SUCCESS)
		DPRINTF("set failed 0x%x\n", rval);

	return (rval);
}

static void
urndis_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urndis_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc = usbd_xfer_get_frame(xfer, 0);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct rndis_packet_msg msg;
	struct mbuf *m;
	int actlen;
	int aframes;
	int offset;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

		DPRINTFN(1, "received %u bytes in %u frames\n", actlen, aframes);

		for (offset = 0; actlen >= (uint32_t)sizeof(msg);) {
			/* copy out header */
			usbd_copy_out(pc, offset, &msg, sizeof(msg));

			if (le32toh(0x1234567U) != 0x1234567U) {
				/* swap endianness */
				msg.rm_type = le32toh(msg.rm_type);
				msg.rm_len = le32toh(msg.rm_len);
				msg.rm_dataoffset = le32toh(msg.rm_dataoffset);
				msg.rm_datalen = le32toh(msg.rm_datalen);
				msg.rm_oobdataoffset = le32toh(msg.rm_oobdataoffset);
				msg.rm_oobdatalen = le32toh(msg.rm_oobdatalen);
				msg.rm_oobdataelements = le32toh(msg.rm_oobdataelements);
				msg.rm_pktinfooffset = le32toh(msg.rm_pktinfooffset);
				msg.rm_pktinfolen = le32toh(msg.rm_pktinfolen);
				msg.rm_vchandle = le32toh(msg.rm_vchandle);
				msg.rm_reserved = le32toh(msg.rm_reserved);
			}

			DPRINTF("len %u data(off:%u len:%u) "
			    "oobdata(off:%u len:%u nb:%u) perpacket(off:%u len:%u)\n",
			    msg.rm_len, msg.rm_dataoffset, msg.rm_datalen,
			    msg.rm_oobdataoffset, msg.rm_oobdatalen,
			    msg.rm_oobdataelements, msg.rm_pktinfooffset,
			    msg.rm_pktinfooffset);

			/* sanity check the RNDIS header */
			if (msg.rm_type != REMOTE_NDIS_PACKET_MSG) {
				DPRINTF("invalid type 0x%x != 0x%x\n",
				    msg.rm_type, REMOTE_NDIS_PACKET_MSG);
				goto tr_setup;
			} else if (msg.rm_len < (uint32_t)sizeof(msg)) {
				DPRINTF("invalid msg len %u < %u\n",
				    msg.rm_len, (unsigned)sizeof(msg));
				goto tr_setup;
			} else if (msg.rm_len > (uint32_t)actlen) {
				DPRINTF("invalid msg len %u > buffer "
				    "len %u\n", msg.rm_len, actlen);
				goto tr_setup;
			} else if (msg.rm_dataoffset >= (uint32_t)actlen) {
				DPRINTF("invalid msg dataoffset %u > buffer "
				    "dataoffset %u\n", msg.rm_dataoffset, actlen);
				goto tr_setup;
			} else if (msg.rm_datalen > (uint32_t)actlen) {
				DPRINTF("invalid msg datalen %u > buffer "
				    "datalen %u\n", msg.rm_datalen, actlen);
				goto tr_setup;
			} else if ((msg.rm_dataoffset + msg.rm_datalen +
			    (uint32_t)__offsetof(struct rndis_packet_msg,
			    rm_dataoffset)) > (uint32_t)actlen) {
				DPRINTF("invalid dataoffset %u larger than %u\n",
				    msg.rm_dataoffset + msg.rm_datalen +
				    (uint32_t)__offsetof(struct rndis_packet_msg,
				    rm_dataoffset), actlen);
				goto tr_setup;
			} else if (msg.rm_datalen < (uint32_t)sizeof(struct ether_header)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				DPRINTF("invalid ethernet size "
				    "%u < %u\n", msg.rm_datalen, (unsigned)sizeof(struct ether_header));
				goto tr_setup;
			} else if (msg.rm_datalen > (uint32_t)(MCLBYTES - ETHER_ALIGN)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				DPRINTF("invalid ethernet size "
				    "%u > %u\n",
				    msg.rm_datalen, (unsigned)MCLBYTES);
				goto tr_setup;
			} else if (msg.rm_datalen > (uint32_t)(MHLEN - ETHER_ALIGN)) {
				m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
			} else {
				m = m_gethdr(M_NOWAIT, MT_DATA);
			}

			/* check if we have a buffer */
			if (m != NULL) {
				m->m_len = m->m_pkthdr.len = msg.rm_datalen + ETHER_ALIGN;
				m_adj(m, ETHER_ALIGN);

				usbd_copy_out(pc, offset + msg.rm_dataoffset +
				    __offsetof(struct rndis_packet_msg,
				    rm_dataoffset), m->m_data, msg.rm_datalen);

				/* enqueue */
				uether_rxmbuf(&sc->sc_ue, m, msg.rm_datalen);
			} else {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			}
			offset += msg.rm_len;
			actlen -= msg.rm_len;
		}

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, RNDIS_RX_MAXLEN);
		usbd_xfer_set_frames(xfer, 1);
		usbd_transfer_submit(xfer);
		uether_rxflush(&sc->sc_ue);	/* must be last */
		break;

	default:			/* Error */
		DPRINTFN(1, "error = %s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			usbd_xfer_set_frames(xfer, 0);
			usbd_transfer_submit(xfer);
		}
		break;
	}
}

static void
urndis_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct rndis_packet_msg msg;
	struct urndis_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct mbuf *m;
	unsigned x;
	int actlen;
	int aframes;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTFN(1, "\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "%u bytes in %u frames\n", actlen, aframes);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		memset(&msg, 0, sizeof(msg));

		for (x = 0; x != RNDIS_TX_FRAMES_MAX; x++) {
			struct usb_page_cache *pc = usbd_xfer_get_frame(xfer, x);

			usbd_xfer_set_frame_offset(xfer, x * RNDIS_TX_MAXLEN, x);

next_pkt:
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

			if (m == NULL)
				break;

			if ((m->m_pkthdr.len + sizeof(msg)) > RNDIS_TX_MAXLEN) {
				DPRINTF("Too big packet\n");
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

				/* Free buffer */
				m_freem(m);
				goto next_pkt;
			}
			msg.rm_type = htole32(REMOTE_NDIS_PACKET_MSG);
			msg.rm_len = htole32(sizeof(msg) + m->m_pkthdr.len);

			msg.rm_dataoffset = htole32(RNDIS_DATA_OFFSET);
			msg.rm_datalen = htole32(m->m_pkthdr.len);

			/* copy in all data */
			usbd_copy_in(pc, 0, &msg, sizeof(msg));
			usbd_m_copy_in(pc, sizeof(msg), m, 0, m->m_pkthdr.len);
			usbd_xfer_set_frame_len(xfer, x, sizeof(msg) + m->m_pkthdr.len);

			/*
			 * If there's a BPF listener, bounce a copy of
			 * this frame to him:
			 */
			BPF_MTAP(ifp, m);

			/* Free buffer */
			m_freem(m);
		}
		if (x != 0) {
			usbd_xfer_set_frames(xfer, x);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n", usbd_errstr(error));

		/* count output errors */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
urndis_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
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
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}
