/*
 * ng_ubt.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_ubt.c,v 1.16 2003/10/10 19:15:06 max Exp $
 * $FreeBSD$
 */

/*
 * NOTE: ng_ubt2 driver has a split personality. On one side it is
 * a USB device driver and on the other it is a Netgraph node. This
 * driver will *NOT* create traditional /dev/ enties, only Netgraph 
 * node.
 *
 * NOTE ON LOCKS USED: ng_ubt2 drives uses 2 locks (mutexes)
 *
 * 1) sc_if_mtx - lock for device's interface #0 and #1. This lock is used
 *    by USB for any USB request going over device's interface #0 and #1,
 *    i.e. interrupt, control, bulk and isoc. transfers.
 * 
 * 2) sc_ng_mtx - this lock is used to protect shared (between USB, Netgraph
 *    and Taskqueue) data, such as outgoing mbuf queues, task flags and hook
 *    pointer. This lock *SHOULD NOT* be grabbed for a long time. In fact,
 *    think of it as a spin lock.
 *
 * NOTE ON LOCKING STRATEGY: ng_ubt2 driver operates in 3 different contexts.
 *
 * 1) USB context. This is where all the USB related stuff happens. All
 *    callbacks run in this context. All callbacks are called (by USB) with
 *    appropriate interface lock held. It is (generally) allowed to grab
 *    any additional locks.
 *
 * 2) Netgraph context. This is where all the Netgraph related stuff happens.
 *    Since we mark node as WRITER, the Netgraph node will be "locked" (from
 *    Netgraph point of view). Any variable that is only modified from the
 *    Netgraph context does not require any additional locking. It is generally
 *    *NOT* allowed to grab *ANY* additional locks. Whatever you do, *DO NOT*
 *    grab any lock in the Netgraph context that could cause de-scheduling of
 *    the Netgraph thread for significant amount of time. In fact, the only
 *    lock that is allowed in the Netgraph context is the sc_ng_mtx lock.
 *    Also make sure that any code that is called from the Netgraph context
 *    follows the rule above.
 *
 * 3) Taskqueue context. This is where ubt_task runs. Since we are generally
 *    NOT allowed to grab any lock that could cause de-scheduling in the
 *    Netgraph context, and, USB requires us to grab interface lock before
 *    doing things with transfers, it is safer to transition from the Netgraph
 *    context to the Taskqueue context before we can call into USB subsystem.
 *
 * So, to put everything together, the rules are as follows.
 *	It is OK to call from the USB context or the Taskqueue context into
 * the Netgraph context (i.e. call NG_SEND_xxx functions). In other words
 * it is allowed to call into the Netgraph context with locks held.
 *	Is it *NOT* OK to call from the Netgraph context into the USB context,
 * because USB requires us to grab interface locks, and, it is safer to
 * avoid it. So, to make things safer we set task flags to indicate which
 * actions we want to perform and schedule ubt_task which would run in the
 * Taskqueue context.
 *	Is is OK to call from the Taskqueue context into the USB context,
 * and, ubt_task does just that (i.e. grabs appropriate interface locks
 * before calling into USB).
 *	Access to the outgoing queues, task flags and hook pointer is
 * controlled by the sc_ng_mtx lock. It is an unavoidable evil. Again,
 * sc_ng_mtx should really be a spin lock (and it is very likely to an
 * equivalent of spin lock due to adaptive nature of FreeBSD mutexes).
 *	All USB callbacks accept softc pointer as a private data. USB ensures
 * that this pointer is valid.
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

#include "usbdevs.h"
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_busdma.h>

#include <sys/mbuf.h>
#include <sys/taskqueue.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_parse.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_ubt.h>
#include <netgraph/bluetooth/drivers/ubt/ng_ubt_var.h>

static int		ubt_modevent(module_t, int, void *);
static device_probe_t	ubt_probe;
static device_attach_t	ubt_attach;
static device_detach_t	ubt_detach;

static void		ubt_task_schedule(ubt_softc_p, int);
static task_fn_t	ubt_task;

#define	ubt_xfer_start(sc, i)	usbd_transfer_start((sc)->sc_xfer[(i)])

/* Netgraph methods */
static ng_constructor_t	ng_ubt_constructor;
static ng_shutdown_t	ng_ubt_shutdown;
static ng_newhook_t	ng_ubt_newhook;
static ng_connect_t	ng_ubt_connect;
static ng_disconnect_t	ng_ubt_disconnect;
static ng_rcvmsg_t	ng_ubt_rcvmsg;
static ng_rcvdata_t	ng_ubt_rcvdata;

/* Queue length */
static const struct ng_parse_struct_field	ng_ubt_node_qlen_type_fields[] =
{
	{ "queue", &ng_parse_int32_type, },
	{ "qlen",  &ng_parse_int32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_ubt_node_qlen_type =
{
	&ng_parse_struct_type,
	&ng_ubt_node_qlen_type_fields
};

/* Stat info */
static const struct ng_parse_struct_field	ng_ubt_node_stat_type_fields[] =
{
	{ "pckts_recv", &ng_parse_uint32_type, },
	{ "bytes_recv", &ng_parse_uint32_type, },
	{ "pckts_sent", &ng_parse_uint32_type, },
	{ "bytes_sent", &ng_parse_uint32_type, },
	{ "oerrors",    &ng_parse_uint32_type, },
	{ "ierrors",    &ng_parse_uint32_type, },
	{ NULL, }
};
static const struct ng_parse_type		ng_ubt_node_stat_type =
{
	&ng_parse_struct_type,
	&ng_ubt_node_stat_type_fields
};

/* Netgraph node command list */
static const struct ng_cmdlist			ng_ubt_cmdlist[] =
{
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_SET_DEBUG,
		"set_debug",
		&ng_parse_uint16_type,
		NULL
	},
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_GET_DEBUG,
		"get_debug",
		NULL,
		&ng_parse_uint16_type
	},
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_SET_QLEN,
		"set_qlen",
		&ng_ubt_node_qlen_type,
		NULL
	},
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_GET_QLEN,
		"get_qlen",
		&ng_ubt_node_qlen_type,
		&ng_ubt_node_qlen_type
	},
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_GET_STAT,
		"get_stat",
		NULL,
		&ng_ubt_node_stat_type
	},
	{
		NGM_UBT_COOKIE,
		NGM_UBT_NODE_RESET_STAT,
		"reset_stat",
		NULL,
		NULL
	},
	{ 0, }
};

/* Netgraph node type */
static struct ng_type	typestruct =
{
	.version = 	NG_ABI_VERSION,
	.name =		NG_UBT_NODE_TYPE,
	.constructor =	ng_ubt_constructor,
	.rcvmsg =	ng_ubt_rcvmsg,
	.shutdown =	ng_ubt_shutdown,
	.newhook =	ng_ubt_newhook,
	.connect =	ng_ubt_connect,
	.rcvdata =	ng_ubt_rcvdata,
	.disconnect =	ng_ubt_disconnect,
	.cmdlist =	ng_ubt_cmdlist
};

/****************************************************************************
 ****************************************************************************
 **                              USB specific
 ****************************************************************************
 ****************************************************************************/

/* USB methods */
static usb_callback_t	ubt_ctrl_write_callback;
static usb_callback_t	ubt_intr_read_callback;
static usb_callback_t	ubt_bulk_read_callback;
static usb_callback_t	ubt_bulk_write_callback;
static usb_callback_t	ubt_isoc_read_callback;
static usb_callback_t	ubt_isoc_write_callback;

static int		ubt_fwd_mbuf_up(ubt_softc_p, struct mbuf **);
static int		ubt_isoc_read_one_frame(struct usb_xfer *, int);

/*
 * USB config
 * 
 * The following desribes usb transfers that could be submitted on USB device.
 *
 * Interface 0 on the USB device must present the following endpoints
 *	1) Interrupt endpoint to receive HCI events
 *	2) Bulk IN endpoint to receive ACL data
 *	3) Bulk OUT endpoint to send ACL data
 *
 * Interface 1 on the USB device must present the following endpoints
 *	1) Isochronous IN endpoint to receive SCO data
 *	2) Isochronous OUT endpoint to send SCO data
 */

static const struct usb_config		ubt_config[UBT_N_TRANSFER] =
{
	/*
	 * Interface #0
 	 */

	/* Outgoing bulk transfer - ACL packets */
	[UBT_IF_0_BULK_DT_WR] = {
		.type =		UE_BULK,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_OUT,
		.if_index = 	0,
		.bufsize =	UBT_BULK_WRITE_BUFFER_SIZE,
		.flags =	{ .pipe_bof = 1, .force_short_xfer = 1, },
		.callback =	&ubt_bulk_write_callback,
	},
	/* Incoming bulk transfer - ACL packets */
	[UBT_IF_0_BULK_DT_RD] = {
		.type =		UE_BULK,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_IN,
		.if_index = 	0,
		.bufsize =	UBT_BULK_READ_BUFFER_SIZE,
		.flags =	{ .pipe_bof = 1, .short_xfer_ok = 1, },
		.callback =	&ubt_bulk_read_callback,
	},
	/* Incoming interrupt transfer - HCI events */
	[UBT_IF_0_INTR_DT_RD] = {
		.type =		UE_INTERRUPT,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_IN,
		.if_index = 	0,
		.flags =	{ .pipe_bof = 1, .short_xfer_ok = 1, },
		.bufsize =	UBT_INTR_BUFFER_SIZE,
		.callback =	&ubt_intr_read_callback,
	},
	/* Outgoing control transfer - HCI commands */
	[UBT_IF_0_CTRL_DT_WR] = {
		.type =		UE_CONTROL,
		.endpoint =	0x00,	/* control pipe */
		.direction =	UE_DIR_ANY,
		.if_index = 	0,
		.bufsize =	UBT_CTRL_BUFFER_SIZE,
		.callback =	&ubt_ctrl_write_callback,
		.timeout =	5000,	/* 5 seconds */
	},

	/*
	 * Interface #1
 	 */

	/* Incoming isochronous transfer #1 - SCO packets */
	[UBT_IF_1_ISOC_DT_RD1] = {
		.type =		UE_ISOCHRONOUS,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_IN,
		.if_index = 	1,
		.bufsize =	0,	/* use "wMaxPacketSize * frames" */
		.frames =	UBT_ISOC_NFRAMES,
		.flags =	{ .short_xfer_ok = 1, },
		.callback =	&ubt_isoc_read_callback,
	},
	/* Incoming isochronous transfer #2 - SCO packets */
	[UBT_IF_1_ISOC_DT_RD2] = {
		.type =		UE_ISOCHRONOUS,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_IN,
		.if_index = 	1,
		.bufsize =	0,	/* use "wMaxPacketSize * frames" */
		.frames =	UBT_ISOC_NFRAMES,
		.flags =	{ .short_xfer_ok = 1, },
		.callback =	&ubt_isoc_read_callback,
	},
	/* Outgoing isochronous transfer #1 - SCO packets */
	[UBT_IF_1_ISOC_DT_WR1] = {
		.type =		UE_ISOCHRONOUS,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_OUT,
		.if_index = 	1,
		.bufsize =	0,	/* use "wMaxPacketSize * frames" */
		.frames =	UBT_ISOC_NFRAMES,
		.flags =	{ .short_xfer_ok = 1, },
		.callback =	&ubt_isoc_write_callback,
	},
	/* Outgoing isochronous transfer #2 - SCO packets */
	[UBT_IF_1_ISOC_DT_WR2] = {
		.type =		UE_ISOCHRONOUS,
		.endpoint =	UE_ADDR_ANY,
		.direction =	UE_DIR_OUT,
		.if_index = 	1,
		.bufsize =	0,	/* use "wMaxPacketSize * frames" */
		.frames =	UBT_ISOC_NFRAMES,
		.flags =	{ .short_xfer_ok = 1, },
		.callback =	&ubt_isoc_write_callback,
	},
};

/*
 * If for some reason device should not be attached then put
 * VendorID/ProductID pair into the list below. The format is
 * as follows:
 *
 *	{ USB_VPI(VENDOR_ID, PRODUCT_ID, 0) },
 *
 * where VENDOR_ID and PRODUCT_ID are hex numbers.
 */

static const STRUCT_USB_HOST_ID ubt_ignore_devs[] = 
{
	/* AVM USB Bluetooth-Adapter BlueFritz! v1.0 */
	{ USB_VPI(USB_VENDOR_AVM, 0x2200, 0) },

	/* Atheros 3011 with sflash firmware */
	{ USB_VPI(0x0cf3, 0x3002, 0) },
	{ USB_VPI(0x0cf3, 0xe019, 0) },
	{ USB_VPI(0x13d3, 0x3304, 0) },
	{ USB_VPI(0x0930, 0x0215, 0) },
	{ USB_VPI(0x0489, 0xe03d, 0) },
	{ USB_VPI(0x0489, 0xe027, 0) },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_VPI(0x03f0, 0x311d, 0) },

	/* Atheros 3012 with sflash firmware */
	{ USB_VPI(0x0cf3, 0x3004, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0cf3, 0x311d, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x13d3, 0x3375, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x04ca, 0x3005, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x04ca, 0x3006, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x04ca, 0x3008, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x13d3, 0x3362, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0cf3, 0xe004, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0930, 0x0219, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0489, 0xe057, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x13d3, 0x3393, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0489, 0xe04e, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0489, 0xe056, 0), USB_DEV_BCD_LTEQ(1) },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_VPI(0x0489, 0xe02c, 0), USB_DEV_BCD_LTEQ(1) },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_VPI(0x0489, 0xe03c, 0), USB_DEV_BCD_LTEQ(1) },
	{ USB_VPI(0x0489, 0xe036, 0), USB_DEV_BCD_LTEQ(1) },
};

/* List of supported bluetooth devices */
static const STRUCT_USB_HOST_ID ubt_devs[] =
{
	/* Generic Bluetooth class devices */
	{ USB_IFACE_CLASS(UDCLASS_WIRELESS),
	  USB_IFACE_SUBCLASS(UDSUBCLASS_RF),
	  USB_IFACE_PROTOCOL(UDPROTO_BLUETOOTH) },

	/* AVM USB Bluetooth-Adapter BlueFritz! v2.0 */
	{ USB_VPI(USB_VENDOR_AVM, 0x3800, 0) },

	/* Broadcom USB dongles, mostly BCM20702 and BCM20702A0 */
	{ USB_VENDOR(USB_VENDOR_BROADCOM),
	  USB_IFACE_CLASS(UICLASS_VENDOR),
	  USB_IFACE_SUBCLASS(UDSUBCLASS_RF),
	  USB_IFACE_PROTOCOL(UDPROTO_BLUETOOTH) },

	/* Apple-specific (Broadcom) devices */
	{ USB_VENDOR(USB_VENDOR_APPLE),
	  USB_IFACE_CLASS(UICLASS_VENDOR),
	  USB_IFACE_SUBCLASS(UDSUBCLASS_RF),
	  USB_IFACE_PROTOCOL(UDPROTO_BLUETOOTH) },

	/* Foxconn - Hon Hai */
	{ USB_VENDOR(USB_VENDOR_FOXCONN),
	  USB_IFACE_CLASS(UICLASS_VENDOR),
	  USB_IFACE_SUBCLASS(UDSUBCLASS_RF),
	  USB_IFACE_PROTOCOL(UDPROTO_BLUETOOTH) },

	/* MediaTek MT76x0E */
	{ USB_VPI(USB_VENDOR_MEDIATEK, 0x763f, 0) },

	/* Broadcom SoftSailing reporting vendor specific */
	{ USB_VPI(USB_VENDOR_BROADCOM, 0x21e1, 0) },

	/* Apple MacBookPro 7,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x8213, 0) },

	/* Apple iMac11,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x8215, 0) },

	/* Apple MacBookPro6,2 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x8218, 0) },

	/* Apple MacBookAir3,1, MacBookAir3,2 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x821b, 0) },

	/* Apple MacBookAir4,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x821f, 0) },

	/* MacBookAir6,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x828f, 0) },

	/* Apple MacBookPro8,2 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x821a, 0) },

	/* Apple MacMini5,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x8281, 0) },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_VPI(USB_VENDOR_TDK, 0x030a, 0) },

	/* ALPS Modules with non-standard ID */
	{ USB_VPI(USB_VENDOR_ALPS, 0x3001, 0) },
	{ USB_VPI(USB_VENDOR_ALPS, 0x3002, 0) },

	{ USB_VPI(USB_VENDOR_ERICSSON2, 0x1002, 0) },

	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_VPI(USB_VENDOR_CANYON, 0x0000, 0) },

	/* Broadcom BCM20702A0 */
	{ USB_VPI(USB_VENDOR_ASUS, 0x17b5, 0) },
	{ USB_VPI(USB_VENDOR_ASUS, 0x17cb, 0) },
	{ USB_VPI(USB_VENDOR_LITEON, 0x2003, 0) },
	{ USB_VPI(USB_VENDOR_FOXCONN, 0xe042, 0) },
	{ USB_VPI(USB_VENDOR_DELL, 0x8197, 0) },
};

/*
 * Probe for a USB Bluetooth device.
 * USB context.
 */

static int
ubt_probe(device_t dev)
{
	struct usb_attach_arg	*uaa = device_get_ivars(dev);
	int error;

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	if (usbd_lookup_id_by_uaa(ubt_ignore_devs,
			sizeof(ubt_ignore_devs), uaa) == 0)
		return (ENXIO);

	error = usbd_lookup_id_by_uaa(ubt_devs, sizeof(ubt_devs), uaa);
	if (error == 0)
		return (BUS_PROBE_GENERIC);
	return (error);
} /* ubt_probe */

/*
 * Attach the device.
 * USB context.
 */

static int
ubt_attach(device_t dev)
{
	struct usb_attach_arg		*uaa = device_get_ivars(dev);
	struct ubt_softc		*sc = device_get_softc(dev);
	struct usb_endpoint_descriptor	*ed;
	struct usb_interface_descriptor *id;
	struct usb_interface		*iface;
	uint16_t			wMaxPacketSize;
	uint8_t				alt_index, i, j;
	uint8_t				iface_index[2] = { 0, 1 };

	device_set_usb_desc(dev);

	sc->sc_dev = dev;
	sc->sc_debug = NG_UBT_WARN_LEVEL;

	/* 
	 * Create Netgraph node
	 */

	if (ng_make_node_common(&typestruct, &sc->sc_node) != 0) {
		UBT_ALERT(sc, "could not create Netgraph node\n");
		return (ENXIO);
	}

	/* Name Netgraph node */
	if (ng_name_node(sc->sc_node, device_get_nameunit(dev)) != 0) {
		UBT_ALERT(sc, "could not name Netgraph node\n");
		NG_NODE_UNREF(sc->sc_node);
		return (ENXIO);
	}
	NG_NODE_SET_PRIVATE(sc->sc_node, sc);
	NG_NODE_FORCE_WRITER(sc->sc_node);

	/*
	 * Initialize device softc structure
	 */

	/* initialize locks */
	mtx_init(&sc->sc_ng_mtx, "ubt ng", NULL, MTX_DEF);
	mtx_init(&sc->sc_if_mtx, "ubt if", NULL, MTX_DEF | MTX_RECURSE);

	/* initialize packet queues */
	NG_BT_MBUFQ_INIT(&sc->sc_cmdq, UBT_DEFAULT_QLEN);
	NG_BT_MBUFQ_INIT(&sc->sc_aclq, UBT_DEFAULT_QLEN);
	NG_BT_MBUFQ_INIT(&sc->sc_scoq, UBT_DEFAULT_QLEN);

	/* initialize glue task */
	TASK_INIT(&sc->sc_task, 0, ubt_task, sc);

	/*
	 * Configure Bluetooth USB device. Discover all required USB
	 * interfaces and endpoints.
	 *
	 * USB device must present two interfaces:
	 * 1) Interface 0 that has 3 endpoints
	 *	1) Interrupt endpoint to receive HCI events
	 *	2) Bulk IN endpoint to receive ACL data
	 *	3) Bulk OUT endpoint to send ACL data
	 *
	 * 2) Interface 1 then has 2 endpoints
	 *	1) Isochronous IN endpoint to receive SCO data
 	 *	2) Isochronous OUT endpoint to send SCO data
	 *
	 * Interface 1 (with isochronous endpoints) has several alternate
	 * configurations with different packet size.
	 */

	/*
	 * For interface #1 search alternate settings, and find
	 * the descriptor with the largest wMaxPacketSize
	 */

	wMaxPacketSize = 0;
	alt_index = 0;
	i = 0;
	j = 0;
	ed = NULL;

	/* 
	 * Search through all the descriptors looking for the largest
	 * packet size:
	 */
	while ((ed = (struct usb_endpoint_descriptor *)usb_desc_foreach(
	    usbd_get_config_descriptor(uaa->device), 
	    (struct usb_descriptor *)ed))) {

		if ((ed->bDescriptorType == UDESC_INTERFACE) &&
		    (ed->bLength >= sizeof(*id))) {
			id = (struct usb_interface_descriptor *)ed;
			i = id->bInterfaceNumber;
			j = id->bAlternateSetting;
		}

		if ((ed->bDescriptorType == UDESC_ENDPOINT) &&
		    (ed->bLength >= sizeof(*ed)) &&
		    (i == 1)) {
			uint16_t temp;

			temp = UGETW(ed->wMaxPacketSize);
			if (temp > wMaxPacketSize) {
				wMaxPacketSize = temp;
				alt_index = j;
			}
		}
	}

	/* Set alt configuration on interface #1 only if we found it */
	if (wMaxPacketSize > 0 &&
	    usbd_set_alt_interface_index(uaa->device, 1, alt_index)) {
		UBT_ALERT(sc, "could not set alternate setting %d " \
			"for interface 1!\n", alt_index);
		goto detach;
	}

	/* Setup transfers for both interfaces */
	if (usbd_transfer_setup(uaa->device, iface_index, sc->sc_xfer,
			ubt_config, UBT_N_TRANSFER, sc, &sc->sc_if_mtx)) {
		UBT_ALERT(sc, "could not allocate transfers\n");
		goto detach;
	}

	/* Claim all interfaces belonging to the Bluetooth part */
	for (i = 1;; i++) {
		iface = usbd_get_iface(uaa->device, i);
		if (iface == NULL)
			break;
		id = usbd_get_interface_descriptor(iface);

		if ((id != NULL) &&
		    (id->bInterfaceClass == UICLASS_WIRELESS) &&
		    (id->bInterfaceSubClass == UISUBCLASS_RF) &&
		    (id->bInterfaceProtocol == UIPROTO_BLUETOOTH)) {
			usbd_set_parent_iface(uaa->device, i,
			    uaa->info.bIfaceIndex);
		}
	}
	return (0); /* success */

detach:
	ubt_detach(dev);

	return (ENXIO);
} /* ubt_attach */

/*
 * Detach the device.
 * USB context.
 */

int
ubt_detach(device_t dev)
{
	struct ubt_softc	*sc = device_get_softc(dev);
	node_p			node = sc->sc_node;

	/* Destroy Netgraph node */
	if (node != NULL) {
		sc->sc_node = NULL;
		NG_NODE_REALLY_DIE(node);
		ng_rmnode_self(node);
	}

	/* Make sure ubt_task in gone */
	taskqueue_drain(taskqueue_swi, &sc->sc_task);

	/* Free USB transfers, if any */
	usbd_transfer_unsetup(sc->sc_xfer, UBT_N_TRANSFER);

	/* Destroy queues */
	UBT_NG_LOCK(sc);
	NG_BT_MBUFQ_DESTROY(&sc->sc_cmdq);
	NG_BT_MBUFQ_DESTROY(&sc->sc_aclq);
	NG_BT_MBUFQ_DESTROY(&sc->sc_scoq);
	UBT_NG_UNLOCK(sc);

	mtx_destroy(&sc->sc_if_mtx);
	mtx_destroy(&sc->sc_ng_mtx);

	return (0);
} /* ubt_detach */

/* 
 * Called when outgoing control request (HCI command) has completed, i.e.
 * HCI command was sent to the device.
 * USB context.
 */

static void
ubt_ctrl_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc		*sc = usbd_xfer_softc(xfer);
	struct usb_device_request	req;
	struct mbuf			*m;
	struct usb_page_cache		*pc;
	int				actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		UBT_INFO(sc, "sent %d bytes to control pipe\n", actlen);
		UBT_STAT_BYTES_SENT(sc, actlen);
		UBT_STAT_PCKTS_SENT(sc);
		/* FALLTHROUGH */

	case USB_ST_SETUP:
send_next:
		/* Get next command mbuf, if any */
		UBT_NG_LOCK(sc);
		NG_BT_MBUFQ_DEQUEUE(&sc->sc_cmdq, m);
		UBT_NG_UNLOCK(sc);

		if (m == NULL) {
			UBT_INFO(sc, "HCI command queue is empty\n");
			break;	/* transfer complete */
		}

		/* Initialize a USB control request and then schedule it */
		bzero(&req, sizeof(req));
		req.bmRequestType = UBT_HCI_REQUEST;
		USETW(req.wLength, m->m_pkthdr.len);

		UBT_INFO(sc, "Sending control request, " \
			"bmRequestType=0x%02x, wLength=%d\n",
			req.bmRequestType, UGETW(req.wLength));

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));
		pc = usbd_xfer_get_frame(xfer, 1);
		usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);

		usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
		usbd_xfer_set_frame_len(xfer, 1, m->m_pkthdr.len);
		usbd_xfer_set_frames(xfer, 2);

		NG_FREE_M(m);

		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			UBT_WARN(sc, "control transfer failed: %s\n",
				usbd_errstr(error));

			UBT_STAT_OERROR(sc);
			goto send_next;
		}

		/* transfer cancelled */
		break;
	}
} /* ubt_ctrl_write_callback */

/* 
 * Called when incoming interrupt transfer (HCI event) has completed, i.e.
 * HCI event was received from the device.
 * USB context.
 */

static void
ubt_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	struct mbuf		*m;
	ng_hci_event_pkt_t	*hdr;
	struct usb_page_cache	*pc;
	int			actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	m = NULL;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		/* Allocate a new mbuf */
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		if (!(MCLGET(m, M_NOWAIT))) {
			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		/* Add HCI packet type */
		*mtod(m, uint8_t *)= NG_HCI_EVENT_PKT;
		m->m_pkthdr.len = m->m_len = 1;

		if (actlen > MCLBYTES - 1)
			actlen = MCLBYTES - 1;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, mtod(m, uint8_t *) + 1, actlen);
		m->m_pkthdr.len += actlen;
		m->m_len += actlen;

		UBT_INFO(sc, "got %d bytes from interrupt pipe\n",
			actlen);

		/* Validate packet and send it up the stack */
		if (m->m_pkthdr.len < (int)sizeof(*hdr)) {
			UBT_INFO(sc, "HCI event packet is too short\n");

			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		hdr = mtod(m, ng_hci_event_pkt_t *);
		if (hdr->length != (m->m_pkthdr.len - sizeof(*hdr))) {
			UBT_ERR(sc, "Invalid HCI event packet size, " \
				"length=%d, pktlen=%d\n",
				hdr->length, m->m_pkthdr.len);

			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		UBT_INFO(sc, "got complete HCI event frame, pktlen=%d, " \
			"length=%d\n", m->m_pkthdr.len, hdr->length);

		UBT_STAT_PCKTS_RECV(sc);
		UBT_STAT_BYTES_RECV(sc, m->m_pkthdr.len);

		ubt_fwd_mbuf_up(sc, &m);
		/* m == NULL at this point */
		/* FALLTHROUGH */

	case USB_ST_SETUP:
submit_next:
		NG_FREE_M(m); /* checks for m != NULL */

		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			UBT_WARN(sc, "interrupt transfer failed: %s\n",
				usbd_errstr(error));

			/* Try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto submit_next;
		}
			/* transfer cancelled */
		break;
	}
} /* ubt_intr_read_callback */

/*
 * Called when incoming bulk transfer (ACL packet) has completed, i.e.
 * ACL packet was received from the device.
 * USB context.
 */

static void
ubt_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	struct mbuf		*m;
	ng_hci_acldata_pkt_t	*hdr;
	struct usb_page_cache	*pc;
	int len;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	m = NULL;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		/* Allocate new mbuf */
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL) {
			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		if (!(MCLGET(m, M_NOWAIT))) {
			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		/* Add HCI packet type */
		*mtod(m, uint8_t *)= NG_HCI_ACL_DATA_PKT;
		m->m_pkthdr.len = m->m_len = 1;

		if (actlen > MCLBYTES - 1)
			actlen = MCLBYTES - 1;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, mtod(m, uint8_t *) + 1, actlen);
		m->m_pkthdr.len += actlen;
		m->m_len += actlen;

		UBT_INFO(sc, "got %d bytes from bulk-in pipe\n",
			actlen);

		/* Validate packet and send it up the stack */
		if (m->m_pkthdr.len < (int)sizeof(*hdr)) {
			UBT_INFO(sc, "HCI ACL packet is too short\n");

			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		hdr = mtod(m, ng_hci_acldata_pkt_t *);
		len = le16toh(hdr->length);
		if (len != (int)(m->m_pkthdr.len - sizeof(*hdr))) {
			UBT_ERR(sc, "Invalid ACL packet size, length=%d, " \
				"pktlen=%d\n", len, m->m_pkthdr.len);

			UBT_STAT_IERROR(sc);
			goto submit_next;
		}

		UBT_INFO(sc, "got complete ACL data packet, pktlen=%d, " \
			"length=%d\n", m->m_pkthdr.len, len);

		UBT_STAT_PCKTS_RECV(sc);
		UBT_STAT_BYTES_RECV(sc, m->m_pkthdr.len);

		ubt_fwd_mbuf_up(sc, &m);
		/* m == NULL at this point */
		/* FALLTHOUGH */

	case USB_ST_SETUP:
submit_next:
		NG_FREE_M(m); /* checks for m != NULL */

		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			UBT_WARN(sc, "bulk-in transfer failed: %s\n",
				usbd_errstr(error));

			/* Try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto submit_next;
		}
			/* transfer cancelled */
		break;
	}
} /* ubt_bulk_read_callback */

/*
 * Called when outgoing bulk transfer (ACL packet) has completed, i.e.
 * ACL packet was sent to the device.
 * USB context.
 */

static void
ubt_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	struct mbuf		*m;
	struct usb_page_cache	*pc;
	int			actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		UBT_INFO(sc, "sent %d bytes to bulk-out pipe\n", actlen);
		UBT_STAT_BYTES_SENT(sc, actlen);
		UBT_STAT_PCKTS_SENT(sc);
		/* FALLTHROUGH */

	case USB_ST_SETUP:
send_next:
		/* Get next mbuf, if any */
		UBT_NG_LOCK(sc);
		NG_BT_MBUFQ_DEQUEUE(&sc->sc_aclq, m);
		UBT_NG_UNLOCK(sc);

		if (m == NULL) {
			UBT_INFO(sc, "ACL data queue is empty\n");
			break; /* transfer completed */
		}

		/*
		 * Copy ACL data frame back to a linear USB transfer buffer
		 * and schedule transfer
		 */

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);
		usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len);

		UBT_INFO(sc, "bulk-out transfer has been started, len=%d\n",
			m->m_pkthdr.len);

		NG_FREE_M(m);

		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			UBT_WARN(sc, "bulk-out transfer failed: %s\n",
				usbd_errstr(error));

			UBT_STAT_OERROR(sc);

			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto send_next;
		}
			/* transfer cancelled */
		break;
	}
} /* ubt_bulk_write_callback */

/*
 * Called when incoming isoc transfer (SCO packet) has completed, i.e.
 * SCO packet was received from the device.
 * USB context.
 */

static void
ubt_isoc_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	int			n;
	int actlen, nframes;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, &nframes);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		for (n = 0; n < nframes; n ++)
			if (ubt_isoc_read_one_frame(xfer, n) < 0)
				break;
		/* FALLTHROUGH */

	case USB_ST_SETUP:
read_next:
		for (n = 0; n < nframes; n ++)
			usbd_xfer_set_frame_len(xfer, n,
			    usbd_xfer_max_framelen(xfer));

		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
                if (error != USB_ERR_CANCELLED) {
                        UBT_STAT_IERROR(sc);
                        goto read_next;
                }

		/* transfer cancelled */
		break;
	}
} /* ubt_isoc_read_callback */

/*
 * Helper function. Called from ubt_isoc_read_callback() to read
 * SCO data from one frame.
 * USB context.
 */

static int
ubt_isoc_read_one_frame(struct usb_xfer *xfer, int frame_no)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	struct usb_page_cache	*pc;
	struct mbuf		*m;
	int			len, want, got, total;

	/* Get existing SCO reassembly buffer */
	pc = usbd_xfer_get_frame(xfer, 0);
	m = sc->sc_isoc_in_buffer;
	total = usbd_xfer_frame_len(xfer, frame_no);

	/* While we have data in the frame */
	while (total > 0) {
		if (m == NULL) {
			/* Start new reassembly buffer */
			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m == NULL) {
				UBT_STAT_IERROR(sc);
				return (-1);	/* XXX out of sync! */
			}

			if (!(MCLGET(m, M_NOWAIT))) {
				UBT_STAT_IERROR(sc);
				NG_FREE_M(m);
				return (-1);	/* XXX out of sync! */
			}

			/* Expect SCO header */
			*mtod(m, uint8_t *) = NG_HCI_SCO_DATA_PKT;
			m->m_pkthdr.len = m->m_len = got = 1;
			want = sizeof(ng_hci_scodata_pkt_t);
		} else {
			/*
			 * Check if we have SCO header and if so 
			 * adjust amount of data we want
			 */
			got = m->m_pkthdr.len;
			want = sizeof(ng_hci_scodata_pkt_t);

			if (got >= want)
				want += mtod(m, ng_hci_scodata_pkt_t *)->length;
		}

		/* Append frame data to the SCO reassembly buffer */
		len = total;
		if (got + len > want)
			len = want - got;

		usbd_copy_out(pc, frame_no * usbd_xfer_max_framelen(xfer),
			mtod(m, uint8_t *) + m->m_pkthdr.len, len);

		m->m_pkthdr.len += len;
		m->m_len += len;
		total -= len;

		/* Check if we got everything we wanted, if not - continue */
		if (got != want)
			continue;

		/* If we got here then we got complete SCO frame */
		UBT_INFO(sc, "got complete SCO data frame, pktlen=%d, " \
			"length=%d\n", m->m_pkthdr.len,
			mtod(m, ng_hci_scodata_pkt_t *)->length);

		UBT_STAT_PCKTS_RECV(sc);
		UBT_STAT_BYTES_RECV(sc, m->m_pkthdr.len);

		ubt_fwd_mbuf_up(sc, &m);
		/* m == NULL at this point */
	}

	/* Put SCO reassembly buffer back */
	sc->sc_isoc_in_buffer = m;

	return (0);
} /* ubt_isoc_read_one_frame */

/*
 * Called when outgoing isoc transfer (SCO packet) has completed, i.e.
 * SCO packet was sent to the device.
 * USB context.
 */

static void
ubt_isoc_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubt_softc	*sc = usbd_xfer_softc(xfer);
	struct usb_page_cache	*pc;
	struct mbuf		*m;
	int			n, space, offset;
	int			actlen, nframes;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, &nframes);
	pc = usbd_xfer_get_frame(xfer, 0);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		UBT_INFO(sc, "sent %d bytes to isoc-out pipe\n", actlen);
		UBT_STAT_BYTES_SENT(sc, actlen);
		UBT_STAT_PCKTS_SENT(sc);
		/* FALLTHROUGH */

	case USB_ST_SETUP:
send_next:
		offset = 0;
		space = usbd_xfer_max_framelen(xfer) * nframes;
		m = NULL;

		while (space > 0) {
			if (m == NULL) {
				UBT_NG_LOCK(sc);
				NG_BT_MBUFQ_DEQUEUE(&sc->sc_scoq, m);
				UBT_NG_UNLOCK(sc);

				if (m == NULL)
					break;
			}

			n = min(space, m->m_pkthdr.len);
			if (n > 0) {
				usbd_m_copy_in(pc, offset, m,0, n);
				m_adj(m, n);

				offset += n;
				space -= n;
			}

			if (m->m_pkthdr.len == 0)
				NG_FREE_M(m); /* sets m = NULL */
		}

		/* Put whatever is left from mbuf back on queue */
		if (m != NULL) {
			UBT_NG_LOCK(sc);
			NG_BT_MBUFQ_PREPEND(&sc->sc_scoq, m);
			UBT_NG_UNLOCK(sc);
		}

		/*
		 * Calculate sizes for isoc frames.
		 * Note that offset could be 0 at this point (i.e. we have
		 * nothing to send). That is fine, as we have isoc. transfers
		 * going in both directions all the time. In this case it
		 * would be just empty isoc. transfer.
		 */

		for (n = 0; n < nframes; n ++) {
			usbd_xfer_set_frame_len(xfer, n,
			    min(offset, usbd_xfer_max_framelen(xfer)));
			offset -= usbd_xfer_frame_len(xfer, n);
		}

		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			UBT_STAT_OERROR(sc);
			goto send_next;
		}

		/* transfer cancelled */
		break;
	}
}

/*
 * Utility function to forward provided mbuf upstream (i.e. up the stack).
 * Modifies value of the mbuf pointer (sets it to NULL).
 * Save to call from any context.
 */

static int
ubt_fwd_mbuf_up(ubt_softc_p sc, struct mbuf **m)
{
	hook_p	hook;
	int	error;

	/*
	 * Close the race with Netgraph hook newhook/disconnect methods.
	 * Save the hook pointer atomically. Two cases are possible:
	 *
	 * 1) The hook pointer is NULL. It means disconnect method got
	 *    there first. In this case we are done.
	 *
	 * 2) The hook pointer is not NULL. It means that hook pointer
	 *    could be either in valid or invalid (i.e. in the process
	 *    of disconnect) state. In any case grab an extra reference
	 *    to protect the hook pointer.
	 *
	 * It is ok to pass hook in invalid state to NG_SEND_DATA_ONLY() as
	 * it checks for it. Drop extra reference after NG_SEND_DATA_ONLY().
	 */

	UBT_NG_LOCK(sc);
	if ((hook = sc->sc_hook) != NULL)
		NG_HOOK_REF(hook);
	UBT_NG_UNLOCK(sc);

	if (hook == NULL) {
		NG_FREE_M(*m);
		return (ENETDOWN);
	}

	NG_SEND_DATA_ONLY(error, hook, *m);
	NG_HOOK_UNREF(hook);

	if (error != 0)
		UBT_STAT_IERROR(sc);

	return (error);
} /* ubt_fwd_mbuf_up */

/****************************************************************************
 ****************************************************************************
 **                                 Glue 
 ****************************************************************************
 ****************************************************************************/

/*
 * Schedule glue task. Should be called with sc_ng_mtx held. 
 * Netgraph context.
 */

static void
ubt_task_schedule(ubt_softc_p sc, int action)
{
	mtx_assert(&sc->sc_ng_mtx, MA_OWNED);

	/*
	 * Try to handle corner case when "start all" and "stop all"
	 * actions can both be set before task is executed.
	 *
	 * The rules are
	 *
	 * sc_task_flags	action		new sc_task_flags
	 * ------------------------------------------------------
	 * 0			start		start
	 * 0			stop		stop
	 * start		start		start
	 * start		stop		stop
	 * stop			start		stop|start
	 * stop			stop		stop
	 * stop|start		start		stop|start
	 * stop|start		stop		stop
	 */

	if (action != 0) {
		if ((action & UBT_FLAG_T_STOP_ALL) != 0)
			sc->sc_task_flags &= ~UBT_FLAG_T_START_ALL;

		sc->sc_task_flags |= action;
	}

	if (sc->sc_task_flags & UBT_FLAG_T_PENDING)
		return;

	if (taskqueue_enqueue(taskqueue_swi, &sc->sc_task) == 0) {
		sc->sc_task_flags |= UBT_FLAG_T_PENDING;
		return;
	}

	/* XXX: i think this should never happen */
} /* ubt_task_schedule */

/*
 * Glue task. Examines sc_task_flags and does things depending on it.
 * Taskqueue context.
 */

static void
ubt_task(void *context, int pending)
{
	ubt_softc_p	sc = context;
	int		task_flags, i;

	UBT_NG_LOCK(sc);
	task_flags = sc->sc_task_flags;
	sc->sc_task_flags = 0;
	UBT_NG_UNLOCK(sc);

	/*
	 * Stop all USB transfers synchronously.
	 * Stop interface #0 and #1 transfers at the same time and in the
	 * same loop. usbd_transfer_drain() will do appropriate locking.
	 */

	if (task_flags & UBT_FLAG_T_STOP_ALL)
		for (i = 0; i < UBT_N_TRANSFER; i ++)
			usbd_transfer_drain(sc->sc_xfer[i]);

	/* Start incoming interrupt and bulk, and all isoc. USB transfers */
	if (task_flags & UBT_FLAG_T_START_ALL) {
		/*
		 * Interface #0
		 */

		mtx_lock(&sc->sc_if_mtx);

		ubt_xfer_start(sc, UBT_IF_0_INTR_DT_RD);
		ubt_xfer_start(sc, UBT_IF_0_BULK_DT_RD);

		/*
		 * Interface #1
		 * Start both read and write isoc. transfers by default.
		 * Get them going all the time even if we have nothing
		 * to send to avoid any delays.
		 */

		ubt_xfer_start(sc, UBT_IF_1_ISOC_DT_RD1);
		ubt_xfer_start(sc, UBT_IF_1_ISOC_DT_RD2);
		ubt_xfer_start(sc, UBT_IF_1_ISOC_DT_WR1);
		ubt_xfer_start(sc, UBT_IF_1_ISOC_DT_WR2);

		mtx_unlock(&sc->sc_if_mtx);
	}

 	/* Start outgoing control transfer */
	if (task_flags & UBT_FLAG_T_START_CTRL) {
		mtx_lock(&sc->sc_if_mtx);
		ubt_xfer_start(sc, UBT_IF_0_CTRL_DT_WR);
		mtx_unlock(&sc->sc_if_mtx);
	}

	/* Start outgoing bulk transfer */
	if (task_flags & UBT_FLAG_T_START_BULK) {
		mtx_lock(&sc->sc_if_mtx);
		ubt_xfer_start(sc, UBT_IF_0_BULK_DT_WR);
		mtx_unlock(&sc->sc_if_mtx);
	}
} /* ubt_task */

/****************************************************************************
 ****************************************************************************
 **                        Netgraph specific
 ****************************************************************************
 ****************************************************************************/

/*
 * Netgraph node constructor. Do not allow to create node of this type.
 * Netgraph context.
 */

static int
ng_ubt_constructor(node_p node)
{
	return (EINVAL);
} /* ng_ubt_constructor */

/*
 * Netgraph node destructor. Destroy node only when device has been detached.
 * Netgraph context.
 */

static int
ng_ubt_shutdown(node_p node)
{
	if (node->nd_flags & NGF_REALLY_DIE) {
		/*
                 * We came here because the USB device is being
		 * detached, so stop being persistent.
                 */
		NG_NODE_SET_PRIVATE(node, NULL);
		NG_NODE_UNREF(node);
	} else
		NG_NODE_REVIVE(node); /* tell ng_rmnode we are persisant */

	return (0);
} /* ng_ubt_shutdown */

/*
 * Create new hook. There can only be one.
 * Netgraph context.
 */

static int
ng_ubt_newhook(node_p node, hook_p hook, char const *name)
{
	struct ubt_softc	*sc = NG_NODE_PRIVATE(node);

	if (strcmp(name, NG_UBT_HOOK) != 0)
		return (EINVAL);

	UBT_NG_LOCK(sc);
	if (sc->sc_hook != NULL) {
		UBT_NG_UNLOCK(sc);

		return (EISCONN);
	}

	sc->sc_hook = hook;
	UBT_NG_UNLOCK(sc);

	return (0);
} /* ng_ubt_newhook */

/*
 * Connect hook. Start incoming USB transfers.
 * Netgraph context.
 */

static int
ng_ubt_connect(hook_p hook)
{
	struct ubt_softc	*sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	NG_HOOK_FORCE_QUEUE(NG_HOOK_PEER(hook));

	UBT_NG_LOCK(sc);
	ubt_task_schedule(sc, UBT_FLAG_T_START_ALL);
	UBT_NG_UNLOCK(sc);

	return (0);
} /* ng_ubt_connect */

/*
 * Disconnect hook.
 * Netgraph context.
 */

static int
ng_ubt_disconnect(hook_p hook)
{
	struct ubt_softc	*sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));

	UBT_NG_LOCK(sc);

	if (hook != sc->sc_hook) {
		UBT_NG_UNLOCK(sc);

		return (EINVAL);
	}

	sc->sc_hook = NULL;

	/* Kick off task to stop all USB xfers */
	ubt_task_schedule(sc, UBT_FLAG_T_STOP_ALL);

	/* Drain queues */
	NG_BT_MBUFQ_DRAIN(&sc->sc_cmdq);
	NG_BT_MBUFQ_DRAIN(&sc->sc_aclq);
	NG_BT_MBUFQ_DRAIN(&sc->sc_scoq);

	UBT_NG_UNLOCK(sc);

	return (0);
} /* ng_ubt_disconnect */
	
/*
 * Process control message.
 * Netgraph context.
 */

static int
ng_ubt_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ubt_softc	*sc = NG_NODE_PRIVATE(node);
	struct ng_mesg		*msg, *rsp = NULL;
	struct ng_bt_mbufq	*q;
	int			error = 0, queue, qlen;

	NGI_GET_MSG(item, msg);

	switch (msg->header.typecookie) {
	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		case NGM_TEXT_STATUS:
			NG_MKRESPONSE(rsp, msg, NG_TEXTRESPONSE, M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			snprintf(rsp->data, NG_TEXTRESPONSE,
				"Hook: %s\n" \
				"Task flags: %#x\n" \
				"Debug: %d\n" \
				"CMD queue: [have:%d,max:%d]\n" \
				"ACL queue: [have:%d,max:%d]\n" \
				"SCO queue: [have:%d,max:%d]",
				(sc->sc_hook != NULL) ? NG_UBT_HOOK : "",
				sc->sc_task_flags,
				sc->sc_debug,
				sc->sc_cmdq.len,
				sc->sc_cmdq.maxlen,
				sc->sc_aclq.len,
				sc->sc_aclq.maxlen,
				sc->sc_scoq.len,
				sc->sc_scoq.maxlen);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case NGM_UBT_COOKIE:
		switch (msg->header.cmd) {
		case NGM_UBT_NODE_SET_DEBUG:
			if (msg->header.arglen != sizeof(ng_ubt_node_debug_ep)){
				error = EMSGSIZE;
				break;
			}

			sc->sc_debug = *((ng_ubt_node_debug_ep *) (msg->data));
			break;

		case NGM_UBT_NODE_GET_DEBUG:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_ubt_node_debug_ep),
			    M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			*((ng_ubt_node_debug_ep *) (rsp->data)) = sc->sc_debug;
			break;

		case NGM_UBT_NODE_SET_QLEN:
			if (msg->header.arglen != sizeof(ng_ubt_node_qlen_ep)) {
				error = EMSGSIZE;
				break;
			}

			queue = ((ng_ubt_node_qlen_ep *) (msg->data))->queue;
			qlen = ((ng_ubt_node_qlen_ep *) (msg->data))->qlen;

			switch (queue) {
			case NGM_UBT_NODE_QUEUE_CMD:
				q = &sc->sc_cmdq;
				break;

			case NGM_UBT_NODE_QUEUE_ACL:
				q = &sc->sc_aclq;
				break;

			case NGM_UBT_NODE_QUEUE_SCO:
				q = &sc->sc_scoq;
				break;

			default:
				error = EINVAL;
				goto done;
				/* NOT REACHED */
			}

			q->maxlen = qlen;
			break;

		case NGM_UBT_NODE_GET_QLEN:
			if (msg->header.arglen != sizeof(ng_ubt_node_qlen_ep)) {
				error = EMSGSIZE;
				break;
			}

			queue = ((ng_ubt_node_qlen_ep *) (msg->data))->queue;

			switch (queue) {
			case NGM_UBT_NODE_QUEUE_CMD:
				q = &sc->sc_cmdq;
				break;

			case NGM_UBT_NODE_QUEUE_ACL:
				q = &sc->sc_aclq;
				break;

			case NGM_UBT_NODE_QUEUE_SCO:
				q = &sc->sc_scoq;
				break;

			default:
				error = EINVAL;
				goto done;
				/* NOT REACHED */
			}

			NG_MKRESPONSE(rsp, msg, sizeof(ng_ubt_node_qlen_ep),
				M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			((ng_ubt_node_qlen_ep *) (rsp->data))->queue = queue;
			((ng_ubt_node_qlen_ep *) (rsp->data))->qlen = q->maxlen;
			break;

		case NGM_UBT_NODE_GET_STAT:
			NG_MKRESPONSE(rsp, msg, sizeof(ng_ubt_node_stat_ep),
			    M_NOWAIT);
			if (rsp == NULL) {
				error = ENOMEM;
				break;
			}

			bcopy(&sc->sc_stat, rsp->data,
				sizeof(ng_ubt_node_stat_ep));
			break;

		case NGM_UBT_NODE_RESET_STAT:
			UBT_STAT_RESET(sc);
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = EINVAL;
		break;
	}
done:
	NG_RESPOND_MSG(error, node, item, rsp);
	NG_FREE_MSG(msg);

	return (error);
} /* ng_ubt_rcvmsg */

/*
 * Process data.
 * Netgraph context.
 */

static int
ng_ubt_rcvdata(hook_p hook, item_p item)
{
	struct ubt_softc	*sc = NG_NODE_PRIVATE(NG_HOOK_NODE(hook));
	struct mbuf		*m;
	struct ng_bt_mbufq	*q;
	int			action, error = 0;

	if (hook != sc->sc_hook) {
		error = EINVAL;
		goto done;
	}

	/* Deatch mbuf and get HCI frame type */
	NGI_GET_M(item, m);

	/*
	 * Minimal size of the HCI frame is 4 bytes: 1 byte frame type,
	 * 2 bytes connection handle and at least 1 byte of length.
	 * Panic on data frame that has size smaller than 4 bytes (it
	 * should not happen)
	 */

	if (m->m_pkthdr.len < 4)
		panic("HCI frame size is too small! pktlen=%d\n",
			m->m_pkthdr.len);

	/* Process HCI frame */
	switch (*mtod(m, uint8_t *)) {	/* XXX call m_pullup ? */
	case NG_HCI_CMD_PKT:
		if (m->m_pkthdr.len - 1 > (int)UBT_CTRL_BUFFER_SIZE)
			panic("HCI command frame size is too big! " \
				"buffer size=%zd, packet len=%d\n",
				UBT_CTRL_BUFFER_SIZE, m->m_pkthdr.len);

		q = &sc->sc_cmdq;
		action = UBT_FLAG_T_START_CTRL;
		break;

	case NG_HCI_ACL_DATA_PKT:
		if (m->m_pkthdr.len - 1 > UBT_BULK_WRITE_BUFFER_SIZE)
			panic("ACL data frame size is too big! " \
				"buffer size=%d, packet len=%d\n",
				UBT_BULK_WRITE_BUFFER_SIZE, m->m_pkthdr.len);

		q = &sc->sc_aclq;
		action = UBT_FLAG_T_START_BULK;
		break;

	case NG_HCI_SCO_DATA_PKT:
		q = &sc->sc_scoq;
		action = 0;
		break;

	default:
		UBT_ERR(sc, "Dropping unsupported HCI frame, type=0x%02x, " \
			"pktlen=%d\n", *mtod(m, uint8_t *), m->m_pkthdr.len);

		NG_FREE_M(m);
		error = EINVAL;
		goto done;
		/* NOT REACHED */
	}

	UBT_NG_LOCK(sc);
	if (NG_BT_MBUFQ_FULL(q)) {
		NG_BT_MBUFQ_DROP(q);
		UBT_NG_UNLOCK(sc);
		
		UBT_ERR(sc, "Dropping HCI frame 0x%02x, len=%d. Queue full\n",
			*mtod(m, uint8_t *), m->m_pkthdr.len);

		NG_FREE_M(m);
	} else {
		/* Loose HCI packet type, enqueue mbuf and kick off task */
		m_adj(m, sizeof(uint8_t));
		NG_BT_MBUFQ_ENQUEUE(q, m);
		ubt_task_schedule(sc, action);
		UBT_NG_UNLOCK(sc);
	}
done:
	NG_FREE_ITEM(item);

	return (error);
} /* ng_ubt_rcvdata */

/****************************************************************************
 ****************************************************************************
 **                              Module
 ****************************************************************************
 ****************************************************************************/

/*
 * Load/Unload the driver module
 */

static int
ubt_modevent(module_t mod, int event, void *data)
{
	int	error;

	switch (event) {
	case MOD_LOAD:
		error = ng_newtype(&typestruct);
		if (error != 0)
			printf("%s: Could not register Netgraph node type, " \
				"error=%d\n", NG_UBT_NODE_TYPE, error);
		break;

	case MOD_UNLOAD:
		error = ng_rmtype(&typestruct);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
} /* ubt_modevent */

static devclass_t	ubt_devclass;

static device_method_t	ubt_methods[] =
{
	DEVMETHOD(device_probe,	ubt_probe),
	DEVMETHOD(device_attach, ubt_attach),
	DEVMETHOD(device_detach, ubt_detach),
	DEVMETHOD_END
};

static driver_t		ubt_driver =
{
	.name =	   "ubt",
	.methods = ubt_methods,
	.size =	   sizeof(struct ubt_softc),
};

DRIVER_MODULE(ng_ubt, uhub, ubt_driver, ubt_devclass, ubt_modevent, 0);
MODULE_VERSION(ng_ubt, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_ubt, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
MODULE_DEPEND(ng_ubt, ng_hci, NG_BLUETOOTH_VERSION, NG_BLUETOOTH_VERSION, NG_BLUETOOTH_VERSION);
MODULE_DEPEND(ng_ubt, usb, 1, 1, 1);
USB_PNP_HOST_INFO(ubt_devs);
