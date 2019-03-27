/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996-2000 Whistle Communications, Inc.
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
 * 3. Neither the name of author nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY NICK HIBMA AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Driver for arbitrary double bulk pipe devices.
 * The driver assumes that there will be the same driver on the other side.
 *
 * XXX Some more information on what the framing of the IP packets looks like.
 *
 * To take full advantage of bulk transmission, packets should be chosen
 * between 1k and 5k in size (1k to make sure the sending side starts
 * streaming, and <5k to avoid overflowing the system with small TDs).
 */


/* probe/attach/detach:
 *  Connect the driver to the hardware and netgraph
 *
 *  The reason we submit a bulk in transfer is that USB does not know about
 *  interrupts. The bulk transfer continuously polls the device for data.
 *  While the device has no data available, the device NAKs the TDs. As soon
 *  as there is data, the transfer happens and the data comes flowing in.
 *
 *  In case you were wondering, interrupt transfers happen exactly that way.
 *  It therefore doesn't make sense to use the interrupt pipe to signal
 *  'data ready' and then schedule a bulk transfer to fetch it. That would
 *  incur a 2ms delay at least, without reducing bandwidth requirements.
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
#include "usbdevs.h"

#define	USB_DEBUG_VAR udbp_debug
#include <dev/usb/usb_debug.h>

#include <sys/mbuf.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>

#include <dev/usb/misc/udbp.h>

#ifdef USB_DEBUG
static int udbp_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, udbp, CTLFLAG_RW, 0, "USB udbp");
SYSCTL_INT(_hw_usb_udbp, OID_AUTO, debug, CTLFLAG_RWTUN,
    &udbp_debug, 0, "udbp debug level");
#endif

#define	UDBP_TIMEOUT	2000		/* timeout on outbound transfers, in
					 * msecs */
#define	UDBP_BUFFERSIZE	MCLBYTES	/* maximum number of bytes in one
					 * transfer */
#define	UDBP_T_WR       0
#define	UDBP_T_RD       1
#define	UDBP_T_WR_CS    2
#define	UDBP_T_RD_CS    3
#define	UDBP_T_MAX      4
#define	UDBP_Q_MAXLEN   50

struct udbp_softc {

	struct mtx sc_mtx;
	struct ng_bt_mbufq sc_xmitq_hipri;	/* hi-priority transmit queue */
	struct ng_bt_mbufq sc_xmitq;	/* low-priority transmit queue */

	struct usb_xfer *sc_xfer[UDBP_T_MAX];
	node_p	sc_node;		/* back pointer to node */
	hook_p	sc_hook;		/* pointer to the hook */
	struct mbuf *sc_bulk_in_buffer;

	uint32_t sc_packets_in;		/* packets in from downstream */
	uint32_t sc_packets_out;	/* packets out towards downstream */

	uint8_t	sc_flags;
#define	UDBP_FLAG_READ_STALL    0x01	/* read transfer stalled */
#define	UDBP_FLAG_WRITE_STALL   0x02	/* write transfer stalled */

	uint8_t	sc_name[16];
};

/* prototypes */

static int udbp_modload(module_t mod, int event, void *data);

static device_probe_t udbp_probe;
static device_attach_t udbp_attach;
static device_detach_t udbp_detach;

static usb_callback_t udbp_bulk_read_callback;
static usb_callback_t udbp_bulk_read_clear_stall_callback;
static usb_callback_t udbp_bulk_write_callback;
static usb_callback_t udbp_bulk_write_clear_stall_callback;

static void	udbp_bulk_read_complete(node_p, hook_p, void *, int);

static ng_constructor_t	ng_udbp_constructor;
static ng_rcvmsg_t	ng_udbp_rcvmsg;
static ng_shutdown_t	ng_udbp_rmnode;
static ng_newhook_t	ng_udbp_newhook;
static ng_connect_t	ng_udbp_connect;
static ng_rcvdata_t	ng_udbp_rcvdata;
static ng_disconnect_t	ng_udbp_disconnect;

/* Parse type for struct ngudbpstat */
static const struct ng_parse_struct_field
	ng_udbp_stat_type_fields[] = NG_UDBP_STATS_TYPE_INFO;

static const struct ng_parse_type ng_udbp_stat_type = {
	&ng_parse_struct_type,
	&ng_udbp_stat_type_fields
};

/* List of commands and how to convert arguments to/from ASCII */
static const struct ng_cmdlist ng_udbp_cmdlist[] = {
	{
		NGM_UDBP_COOKIE,
		NGM_UDBP_GET_STATUS,
		"getstatus",
		NULL,
		&ng_udbp_stat_type,
	},
	{
		NGM_UDBP_COOKIE,
		NGM_UDBP_SET_FLAG,
		"setflag",
		&ng_parse_int32_type,
		NULL
	},
	{0}
};

/* Netgraph node type descriptor */
static struct ng_type ng_udbp_typestruct = {
	.version = NG_ABI_VERSION,
	.name = NG_UDBP_NODE_TYPE,
	.constructor = ng_udbp_constructor,
	.rcvmsg = ng_udbp_rcvmsg,
	.shutdown = ng_udbp_rmnode,
	.newhook = ng_udbp_newhook,
	.connect = ng_udbp_connect,
	.rcvdata = ng_udbp_rcvdata,
	.disconnect = ng_udbp_disconnect,
	.cmdlist = ng_udbp_cmdlist,
};

/* USB config */
static const struct usb_config udbp_config[UDBP_T_MAX] = {

	[UDBP_T_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UDBP_BUFFERSIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &udbp_bulk_write_callback,
		.timeout = UDBP_TIMEOUT,
	},

	[UDBP_T_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UDBP_BUFFERSIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &udbp_bulk_read_callback,
	},

	[UDBP_T_WR_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &udbp_bulk_write_clear_stall_callback,
		.timeout = 1000,	/* 1 second */
		.interval = 50,	/* 50ms */
	},

	[UDBP_T_RD_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &udbp_bulk_read_clear_stall_callback,
		.timeout = 1000,	/* 1 second */
		.interval = 50,	/* 50ms */
	},
};

static devclass_t udbp_devclass;

static device_method_t udbp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, udbp_probe),
	DEVMETHOD(device_attach, udbp_attach),
	DEVMETHOD(device_detach, udbp_detach),

	DEVMETHOD_END
};

static driver_t udbp_driver = {
	.name = "udbp",
	.methods = udbp_methods,
	.size = sizeof(struct udbp_softc),
};

static const STRUCT_USB_HOST_ID udbp_devs[] = {
	{USB_VPI(USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U258, 0)},
	{USB_VPI(USB_VENDOR_NETCHIP, USB_PRODUCT_NETCHIP_TURBOCONNECT, 0)},
	{USB_VPI(USB_VENDOR_NETCHIP, USB_PRODUCT_NETCHIP_GADGETZERO, 0)},
	{USB_VPI(USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2301, 0)},
	{USB_VPI(USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2302, 0)},
	{USB_VPI(USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL27A1, 0)},
	{USB_VPI(USB_VENDOR_ANCHOR, USB_PRODUCT_ANCHOR_EZLINK, 0)},
	{USB_VPI(USB_VENDOR_GENESYS, USB_PRODUCT_GENESYS_GL620USB, 0)},
};

DRIVER_MODULE(udbp, uhub, udbp_driver, udbp_devclass, udbp_modload, 0);
MODULE_DEPEND(udbp, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
MODULE_DEPEND(udbp, usb, 1, 1, 1);
MODULE_VERSION(udbp, 1);
USB_PNP_HOST_INFO(udbp_devs);

static int
udbp_modload(module_t mod, int event, void *data)
{
	int error;

	switch (event) {
	case MOD_LOAD:
		error = ng_newtype(&ng_udbp_typestruct);
		if (error != 0) {
			printf("%s: Could not register "
			    "Netgraph node type, error=%d\n",
			    NG_UDBP_NODE_TYPE, error);
		}
		break;

	case MOD_UNLOAD:
		error = ng_rmtype(&ng_udbp_typestruct);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static int
udbp_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(udbp_devs, sizeof(udbp_devs), uaa));
}

static int
udbp_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct udbp_softc *sc = device_get_softc(dev);
	int error;

	device_set_usb_desc(dev);

	snprintf(sc->sc_name, sizeof(sc->sc_name),
	    "%s", device_get_nameunit(dev));

	mtx_init(&sc->sc_mtx, "udbp lock", NULL, MTX_DEF | MTX_RECURSE);

	error = usbd_transfer_setup(uaa->device, &uaa->info.bIfaceIndex,
	    sc->sc_xfer, udbp_config, UDBP_T_MAX, sc, &sc->sc_mtx);
	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}
	NG_BT_MBUFQ_INIT(&sc->sc_xmitq, UDBP_Q_MAXLEN);

	NG_BT_MBUFQ_INIT(&sc->sc_xmitq_hipri, UDBP_Q_MAXLEN);

	/* create Netgraph node */

	if (ng_make_node_common(&ng_udbp_typestruct, &sc->sc_node) != 0) {
		printf("%s: Could not create Netgraph node\n",
		    sc->sc_name);
		sc->sc_node = NULL;
		goto detach;
	}
	/* name node */

	if (ng_name_node(sc->sc_node, sc->sc_name) != 0) {
		printf("%s: Could not name node\n",
		    sc->sc_name);
		NG_NODE_UNREF(sc->sc_node);
		sc->sc_node = NULL;
		goto detach;
	}
	NG_NODE_SET_PRIVATE(sc->sc_node, sc);

	/* the device is now operational */

	return (0);			/* success */

detach:
	udbp_detach(dev);
	return (ENOMEM);		/* failure */
}

static int
udbp_detach(device_t dev)
{
	struct udbp_softc *sc = device_get_softc(dev);

	/* destroy Netgraph node */

	if (sc->sc_node != NULL) {
		NG_NODE_SET_PRIVATE(sc->sc_node, NULL);
		ng_rmnode_self(sc->sc_node);
		sc->sc_node = NULL;
	}
	/* free USB transfers, if any */

	usbd_transfer_unsetup(sc->sc_xfer, UDBP_T_MAX);

	mtx_destroy(&sc->sc_mtx);

	/* destroy queues */

	NG_BT_MBUFQ_DESTROY(&sc->sc_xmitq);
	NG_BT_MBUFQ_DESTROY(&sc->sc_xmitq_hipri);

	/* extra check */

	if (sc->sc_bulk_in_buffer) {
		m_freem(sc->sc_bulk_in_buffer);
		sc->sc_bulk_in_buffer = NULL;
	}
	return (0);			/* success */
}

static void
udbp_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udbp_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	struct mbuf *m;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		/* allocate new mbuf */

		MGETHDR(m, M_NOWAIT, MT_DATA);

		if (m == NULL) {
			goto tr_setup;
		}

		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			goto tr_setup;
		}
		m->m_pkthdr.len = m->m_len = actlen;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, m->m_data, actlen);

		sc->sc_bulk_in_buffer = m;

		DPRINTF("received package %d bytes\n", actlen);

	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_bulk_in_buffer) {
			ng_send_fn(sc->sc_node, NULL, &udbp_bulk_read_complete, NULL, 0);
			return;
		}
		if (sc->sc_flags & UDBP_FLAG_READ_STALL) {
			usbd_transfer_start(sc->sc_xfer[UDBP_T_RD_CS]);
			return;
		}
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			sc->sc_flags |= UDBP_FLAG_READ_STALL;
			usbd_transfer_start(sc->sc_xfer[UDBP_T_RD_CS]);
		}
		return;

	}
}

static void
udbp_bulk_read_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udbp_softc *sc = usbd_xfer_softc(xfer);
	struct usb_xfer *xfer_other = sc->sc_xfer[UDBP_T_RD];

	if (usbd_clear_stall_callback(xfer, xfer_other)) {
		DPRINTF("stall cleared\n");
		sc->sc_flags &= ~UDBP_FLAG_READ_STALL;
		usbd_transfer_start(xfer_other);
	}
}

static void
udbp_bulk_read_complete(node_p node, hook_p hook, void *arg1, int arg2)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(node);
	struct mbuf *m;
	int error;

	if (sc == NULL) {
		return;
	}
	mtx_lock(&sc->sc_mtx);

	m = sc->sc_bulk_in_buffer;

	if (m) {

		sc->sc_bulk_in_buffer = NULL;

		if ((sc->sc_hook == NULL) ||
		    NG_HOOK_NOT_VALID(sc->sc_hook)) {
			DPRINTF("No upstream hook\n");
			goto done;
		}
		sc->sc_packets_in++;

		NG_SEND_DATA_ONLY(error, sc->sc_hook, m);

		m = NULL;
	}
done:
	if (m) {
		m_freem(m);
	}
	/* start USB bulk-in transfer, if not already started */

	usbd_transfer_start(sc->sc_xfer[UDBP_T_RD]);

	mtx_unlock(&sc->sc_mtx);
}

static void
udbp_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udbp_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	struct mbuf *m;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		sc->sc_packets_out++;

	case USB_ST_SETUP:
		if (sc->sc_flags & UDBP_FLAG_WRITE_STALL) {
			usbd_transfer_start(sc->sc_xfer[UDBP_T_WR_CS]);
			return;
		}
		/* get next mbuf, if any */

		NG_BT_MBUFQ_DEQUEUE(&sc->sc_xmitq_hipri, m);
		if (m == NULL) {
			NG_BT_MBUFQ_DEQUEUE(&sc->sc_xmitq, m);
			if (m == NULL) {
				DPRINTF("Data queue is empty\n");
				return;
			}
		}
		if (m->m_pkthdr.len > MCLBYTES) {
			DPRINTF("truncating large packet "
			    "from %d to %d bytes\n", m->m_pkthdr.len,
			    MCLBYTES);
			m->m_pkthdr.len = MCLBYTES;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);

		usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len);

		DPRINTF("packet out: %d bytes\n", m->m_pkthdr.len);

		m_freem(m);

		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			sc->sc_flags |= UDBP_FLAG_WRITE_STALL;
			usbd_transfer_start(sc->sc_xfer[UDBP_T_WR_CS]);
		}
		return;

	}
}

static void
udbp_bulk_write_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udbp_softc *sc = usbd_xfer_softc(xfer);
	struct usb_xfer *xfer_other = sc->sc_xfer[UDBP_T_WR];

	if (usbd_clear_stall_callback(xfer, xfer_other)) {
		DPRINTF("stall cleared\n");
		sc->sc_flags &= ~UDBP_FLAG_WRITE_STALL;
		usbd_transfer_start(xfer_other);
	}
}

/***********************************************************************
 * Start of Netgraph methods
 **********************************************************************/

/*
 * If this is a device node so this work is done in the attach()
 * routine and the constructor will return EINVAL as you should not be able
 * to create nodes that depend on hardware (unless you can add the hardware :)
 */
static int
ng_udbp_constructor(node_p node)
{
	return (EINVAL);
}

/*
 * Give our ok for a hook to be added...
 * If we are not running this might kick a device into life.
 * Possibly decode information out of the hook name.
 * Add the hook's private info to the hook structure.
 * (if we had some). In this example, we assume that there is a
 * an array of structs, called 'channel' in the private info,
 * one for each active channel. The private
 * pointer of each hook points to the appropriate UDBP_hookinfo struct
 * so that the source of an input packet is easily identified.
 */
static int
ng_udbp_newhook(node_p node, hook_p hook, const char *name)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(node);
	int32_t error = 0;

	if (strcmp(name, NG_UDBP_HOOK_NAME)) {
		return (EINVAL);
	}
	mtx_lock(&sc->sc_mtx);

	if (sc->sc_hook != NULL) {
		error = EISCONN;
	} else {
		sc->sc_hook = hook;
		NG_HOOK_SET_PRIVATE(hook, NULL);
	}

	mtx_unlock(&sc->sc_mtx);

	return (error);
}

/*
 * Get a netgraph control message.
 * Check it is one we understand. If needed, send a response.
 * We could save the address for an async action later, but don't here.
 * Always free the message.
 * The response should be in a malloc'd region that the caller can 'free'.
 * A response is not required.
 * Theoretically you could respond defferently to old message types if
 * the cookie in the header didn't match what we consider to be current
 * (so that old userland programs could continue to work).
 */
static int
ng_udbp_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(node);
	struct ng_mesg *resp = NULL;
	int error = 0;
	struct ng_mesg *msg;

	NGI_GET_MSG(item, msg);
	/* Deal with message according to cookie and command */
	switch (msg->header.typecookie) {
	case NGM_UDBP_COOKIE:
		switch (msg->header.cmd) {
		case NGM_UDBP_GET_STATUS:
			{
				struct ngudbpstat *stats;

				NG_MKRESPONSE(resp, msg, sizeof(*stats), M_NOWAIT);
				if (!resp) {
					error = ENOMEM;
					break;
				}
				stats = (struct ngudbpstat *)resp->data;
				mtx_lock(&sc->sc_mtx);
				stats->packets_in = sc->sc_packets_in;
				stats->packets_out = sc->sc_packets_out;
				mtx_unlock(&sc->sc_mtx);
				break;
			}
		case NGM_UDBP_SET_FLAG:
			if (msg->header.arglen != sizeof(uint32_t)) {
				error = EINVAL;
				break;
			}
			DPRINTF("flags = 0x%08x\n",
			    *((uint32_t *)msg->data));
			break;
		default:
			error = EINVAL;	/* unknown command */
			break;
		}
		break;
	default:
		error = EINVAL;		/* unknown cookie type */
		break;
	}

	/* Take care of synchronous response, if any */
	NG_RESPOND_MSG(error, node, item, resp);
	NG_FREE_MSG(msg);
	return (error);
}

/*
 * Accept data from the hook and queue it for output.
 */
static int
ng_udbp_rcvdata(hook_p hook, item_p item)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct ng_bt_mbufq *queue_ptr;
	struct mbuf *m;
	struct ng_tag_prio *ptag;
	int error;

	if (sc == NULL) {
		NG_FREE_ITEM(item);
		return (EHOSTDOWN);
	}
	NGI_GET_M(item, m);
	NG_FREE_ITEM(item);

	/*
	 * Now queue the data for when it can be sent
	 */
	ptag = (void *)m_tag_locate(m, NGM_GENERIC_COOKIE,
	    NG_TAG_PRIO, NULL);

	if (ptag && (ptag->priority > NG_PRIO_CUTOFF))
		queue_ptr = &sc->sc_xmitq_hipri;
	else
		queue_ptr = &sc->sc_xmitq;

	mtx_lock(&sc->sc_mtx);

	if (NG_BT_MBUFQ_FULL(queue_ptr)) {
		NG_BT_MBUFQ_DROP(queue_ptr);
		NG_FREE_M(m);
		error = ENOBUFS;
	} else {
		NG_BT_MBUFQ_ENQUEUE(queue_ptr, m);
		/*
		 * start bulk-out transfer, if not already started:
		 */
		usbd_transfer_start(sc->sc_xfer[UDBP_T_WR]);
		error = 0;
	}

	mtx_unlock(&sc->sc_mtx);

	return (error);
}

/*
 * Do local shutdown processing..
 * We are a persistent device, we refuse to go away, and
 * only remove our links and reset ourself.
 */
static int
ng_udbp_rmnode(node_p node)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(node);

	/* Let old node go */
	NG_NODE_SET_PRIVATE(node, NULL);
	NG_NODE_UNREF(node);		/* forget it ever existed */

	if (sc == NULL) {
		goto done;
	}
	/* Create Netgraph node */
	if (ng_make_node_common(&ng_udbp_typestruct, &sc->sc_node) != 0) {
		printf("%s: Could not create Netgraph node\n",
		    sc->sc_name);
		sc->sc_node = NULL;
		goto done;
	}
	/* Name node */
	if (ng_name_node(sc->sc_node, sc->sc_name) != 0) {
		printf("%s: Could not name Netgraph node\n",
		    sc->sc_name);
		NG_NODE_UNREF(sc->sc_node);
		sc->sc_node = NULL;
		goto done;
	}
	NG_NODE_SET_PRIVATE(sc->sc_node, sc);

done:
	if (sc) {
		mtx_unlock(&sc->sc_mtx);
	}
	return (0);
}

/*
 * This is called once we've already connected a new hook to the other node.
 * It gives us a chance to balk at the last minute.
 */
static int
ng_udbp_connect(hook_p hook)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	/* probably not at splnet, force outward queueing */
	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));

	mtx_lock(&sc->sc_mtx);

	sc->sc_flags |= (UDBP_FLAG_READ_STALL |
	    UDBP_FLAG_WRITE_STALL);

	/* start bulk-in transfer */
	usbd_transfer_start(sc->sc_xfer[UDBP_T_RD]);

	/* start bulk-out transfer */
	usbd_transfer_start(sc->sc_xfer[UDBP_T_WR]);

	mtx_unlock(&sc->sc_mtx);

	return (0);
}

/*
 * Dook disconnection
 *
 * For this type, removal of the last link destroys the node
 */
static int
ng_udbp_disconnect(hook_p hook)
{
	struct udbp_softc *sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	int error = 0;

	if (sc != NULL) {

		mtx_lock(&sc->sc_mtx);

		if (hook != sc->sc_hook) {
			error = EINVAL;
		} else {

			/* stop bulk-in transfer */
			usbd_transfer_stop(sc->sc_xfer[UDBP_T_RD_CS]);
			usbd_transfer_stop(sc->sc_xfer[UDBP_T_RD]);

			/* stop bulk-out transfer */
			usbd_transfer_stop(sc->sc_xfer[UDBP_T_WR_CS]);
			usbd_transfer_stop(sc->sc_xfer[UDBP_T_WR]);

			/* cleanup queues */
			NG_BT_MBUFQ_DRAIN(&sc->sc_xmitq);
			NG_BT_MBUFQ_DRAIN(&sc->sc_xmitq_hipri);

			if (sc->sc_bulk_in_buffer) {
				m_freem(sc->sc_bulk_in_buffer);
				sc->sc_bulk_in_buffer = NULL;
			}
			sc->sc_hook = NULL;
		}

		mtx_unlock(&sc->sc_mtx);
	}
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	    && (NG_NODE_IS_VALID(NG_HOOK_NODE(hook))))
		ng_rmnode_self(NG_HOOK_NODE(hook));

	return (error);
}
