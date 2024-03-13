/* SPDX-License-Identifier: MIT */

/*
 * usbif.h
 *
 * USB I/O interface for Xen guest OSes.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 */

#ifndef __XEN_PUBLIC_IO_USBIF_H__
#define __XEN_PUBLIC_IO_USBIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
 * Detailed Interface Description
 * ==============================
 * The pvUSB interface is using a split driver design: a frontend driver in
 * the guest and a backend driver in a driver domain (normally dom0) having
 * access to the physical USB device(s) being passed to the guest.
 *
 * The frontend and backend drivers use XenStore to initiate the connection
 * between them, the I/O activity is handled via two shared ring pages and an
 * event channel. As the interface between frontend and backend is at the USB
 * host connector level, multiple (up to 31) physical USB devices can be
 * handled by a single connection.
 *
 * The Xen pvUSB device name is "qusb", so the frontend's XenStore entries are
 * to be found under "device/qusb", while the backend's XenStore entries are
 * under "backend/<guest-dom-id>/qusb".
 *
 * When a new pvUSB connection is established, the frontend needs to setup the
 * two shared ring pages for communication and the event channel. The ring
 * pages need to be made available to the backend via the grant table
 * interface.
 *
 * One of the shared ring pages is used by the backend to inform the frontend
 * about USB device plug events (device to be added or removed). This is the
 * "conn-ring".
 *
 * The other ring page is used for USB I/O communication (requests and
 * responses). This is the "urb-ring".
 *
 * Feature and Parameter Negotiation
 * =================================
 * The two halves of a Xen pvUSB driver utilize nodes within the XenStore to
 * communicate capabilities and to negotiate operating parameters. This
 * section enumerates these nodes which reside in the respective front and
 * backend portions of the XenStore, following the XenBus convention.
 *
 * Any specified default value is in effect if the corresponding XenBus node
 * is not present in the XenStore.
 *
 * XenStore nodes in sections marked "PRIVATE" are solely for use by the
 * driver side whose XenBus tree contains them.
 *
 *****************************************************************************
 *                            Backend XenBus Nodes
 *****************************************************************************
 *
 *------------------ Backend Device Identification (PRIVATE) ------------------
 *
 * num-ports
 *      Values:         unsigned [1...31]
 *
 *      Number of ports for this (virtual) USB host connector.
 *
 * usb-ver
 *      Values:         unsigned [1...2]
 *
 *      USB version of this host connector: 1 = USB 1.1, 2 = USB 2.0.
 *
 * port/[1...31]
 *      Values:         string
 *
 *      Physical USB device connected to the given port, e.g. "3-1.5".
 *
 *****************************************************************************
 *                            Frontend XenBus Nodes
 *****************************************************************************
 *
 *----------------------- Request Transport Parameters -----------------------
 *
 * event-channel
 *      Values:         unsigned
 *
 *      The identifier of the Xen event channel used to signal activity
 *      in the ring buffer.
 *
 * urb-ring-ref
 *      Values:         unsigned
 *
 *      The Xen grant reference granting permission for the backend to map
 *      the sole page in a single page sized ring buffer. This is the ring
 *      buffer for urb requests.
 *
 * conn-ring-ref
 *      Values:         unsigned
 *
 *      The Xen grant reference granting permission for the backend to map
 *      the sole page in a single page sized ring buffer. This is the ring
 *      buffer for connection/disconnection requests.
 *
 * protocol
 *      Values:         string (XEN_IO_PROTO_ABI_*)
 *      Default Value:  XEN_IO_PROTO_ABI_NATIVE
 *
 *      The machine ABI rules governing the format of all ring request and
 *      response structures.
 *
 * Protocol Description
 * ====================
 *
 *-------------------------- USB device plug events --------------------------
 *
 * USB device plug events are send via the "conn-ring" shared page. As only
 * events are being sent, the respective requests from the frontend to the
 * backend are just dummy ones.
 * The events sent to the frontend have the following layout:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |    portnum     |     speed      | 4
 * +----------------+----------------+----------------+----------------+
 *   id - uint16_t, event id (taken from the actual frontend dummy request)
 *   portnum - uint8_t, port number (1 ... 31)
 *   speed - uint8_t, device XENUSB_SPEED_*, XENUSB_SPEED_NONE == unplug
 *
 * The dummy request:
 *         0                1        octet
 * +----------------+----------------+
 * |               id                | 2
 * +----------------+----------------+
 *   id - uint16_t, guest supplied value (no need for being unique)
 *
 *-------------------------- USB I/O request ---------------------------------
 *
 * A single USB I/O request on the "urb-ring" has the following layout:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |         nr_buffer_segs          | 4
 * +----------------+----------------+----------------+----------------+
 * |                               pipe                                | 8
 * +----------------+----------------+----------------+----------------+
 * |         transfer_flags          |          buffer_length          | 12
 * +----------------+----------------+----------------+----------------+
 * |                       request type specific                       | 16
 * |                               data                                | 20
 * +----------------+----------------+----------------+----------------+
 * |                              seg[0]                               | 24
 * |                               data                                | 28
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |             seg[XENUSB_MAX_SEGMENTS_PER_REQUEST - 1]              | 144
 * |                               data                                | 148
 * +----------------+----------------+----------------+----------------+
 * Bit field bit number 0 is always least significant bit, undefined bits must
 * be zero.
 *   id - uint16_t, guest supplied value
 *   nr_buffer_segs - uint16_t, number of segment entries in seg[] array
 *   pipe - uint32_t, bit field with multiple information:
 *     bits 0-4: port request to send to
 *     bit 5: unlink request with specified id (cancel I/O) if set (see below)
 *     bit 7: direction (1 = read from device)
 *     bits 8-14: device number on port
 *     bits 15-18: endpoint of device
 *     bits 30-31: request type: 00 = isochronous, 01 = interrupt,
 *                               10 = control, 11 = bulk
 *   transfer_flags - uint16_t, bit field with processing flags:
 *     bit 0: less data than specified allowed
 *   buffer_length - uint16_t, total length of data
 *   request type specific data - 8 bytes, see below
 *   seg[] - array with 8 byte elements, see below
 *
 * Request type specific data for isochronous request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |            interval             |           start_frame           | 4
 * +----------------+----------------+----------------+----------------+
 * |       number_of_packets         |       nr_frame_desc_segs        | 8
 * +----------------+----------------+----------------+----------------+
 *   interval - uint16_t, time interval in msecs between frames
 *   start_frame - uint16_t, start frame number
 *   number_of_packets - uint16_t, number of packets to transfer
 *   nr_frame_desc_segs - uint16_t number of seg[] frame descriptors elements
 *
 * Request type specific data for interrupt request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |            interval             |                0                | 4
 * +----------------+----------------+----------------+----------------+
 * |                                 0                                 | 8
 * +----------------+----------------+----------------+----------------+
 *   interval - uint16_t, time in msecs until interruption
 *
 * Request type specific data for control request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |                      data of setup packet                         | 4
 * |                                                                   | 8
 * +----------------+----------------+----------------+----------------+
 *
 * Request type specific data for bulk request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |                                 0                                 | 4
 * |                                 0                                 | 8
 * +----------------+----------------+----------------+----------------+
 *
 * Request type specific data for unlink request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |           unlink_id             |                0                | 4
 * +----------------+----------------+----------------+----------------+
 * |                                 0                                 | 8
 * +----------------+----------------+----------------+----------------+
 *   unlink_id - uint16_t, request id of request to terminate
 *
 * seg[] array element layout:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |                               gref                                | 4
 * +----------------+----------------+----------------+----------------+
 * |             offset              |             length              | 8
 * +----------------+----------------+----------------+----------------+
 *   gref - uint32_t, grant reference of buffer page
 *   offset - uint16_t, offset of buffer start in page
 *   length - uint16_t, length of buffer in page
 *
 *-------------------------- USB I/O response --------------------------------
 *
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |          start_frame            | 4
 * +----------------+----------------+----------------+----------------+
 * |                              status                               | 8
 * +----------------+----------------+----------------+----------------+
 * |                          actual_length                            | 12
 * +----------------+----------------+----------------+----------------+
 * |                           error_count                             | 16
 * +----------------+----------------+----------------+----------------+
 *   id - uint16_t, id of the request this response belongs to
 *   start_frame - uint16_t, start_frame this response (iso requests only)
 *   status - int32_t, XENUSB_STATUS_* (non-iso requests)
 *   actual_length - uint32_t, actual size of data transferred
 *   error_count - uint32_t, number of errors (iso requests)
 */

enum xenusb_spec_version {
	XENUSB_VER_UNKNOWN = 0,
	XENUSB_VER_USB11,
	XENUSB_VER_USB20,
	XENUSB_VER_USB30,	/* not supported yet */
};

/*
 *  USB pipe in xenusb_request
 *
 *  - port number:      bits 0-4
 *                              (USB_MAXCHILDREN is 31)
 *
 *  - operation flag:   bit 5
 *                              (0 = submit urb,
 *                               1 = unlink urb)
 *
 *  - direction:        bit 7
 *                              (0 = Host-to-Device [Out]
 *                               1 = Device-to-Host [In])
 *
 *  - device address:   bits 8-14
 *
 *  - endpoint:         bits 15-18
 *
 *  - pipe type:        bits 30-31
 *                              (00 = isochronous, 01 = interrupt,
 *                               10 = control, 11 = bulk)
 */

#define XENUSB_PIPE_PORT_MASK	0x0000001f
#define XENUSB_PIPE_UNLINK	0x00000020
#define XENUSB_PIPE_DIR		0x00000080
#define XENUSB_PIPE_DEV_MASK	0x0000007f
#define XENUSB_PIPE_DEV_SHIFT	8
#define XENUSB_PIPE_EP_MASK	0x0000000f
#define XENUSB_PIPE_EP_SHIFT	15
#define XENUSB_PIPE_TYPE_MASK	0x00000003
#define XENUSB_PIPE_TYPE_SHIFT	30
#define XENUSB_PIPE_TYPE_ISOC	0
#define XENUSB_PIPE_TYPE_INT	1
#define XENUSB_PIPE_TYPE_CTRL	2
#define XENUSB_PIPE_TYPE_BULK	3

#define xenusb_pipeportnum(pipe)		((pipe) & XENUSB_PIPE_PORT_MASK)
#define xenusb_setportnum_pipe(pipe, portnum)	((pipe) | (portnum))

#define xenusb_pipeunlink(pipe)			((pipe) & XENUSB_PIPE_UNLINK)
#define xenusb_pipesubmit(pipe)			(!xenusb_pipeunlink(pipe))
#define xenusb_setunlink_pipe(pipe)		((pipe) | XENUSB_PIPE_UNLINK)

#define xenusb_pipein(pipe)			((pipe) & XENUSB_PIPE_DIR)
#define xenusb_pipeout(pipe)			(!xenusb_pipein(pipe))

#define xenusb_pipedevice(pipe)			\
	(((pipe) >> XENUSB_PIPE_DEV_SHIFT) & XENUSB_PIPE_DEV_MASK)

#define xenusb_pipeendpoint(pipe)		\
	(((pipe) >> XENUSB_PIPE_EP_SHIFT) & XENUSB_PIPE_EP_MASK)

#define xenusb_pipetype(pipe)			\
	(((pipe) >> XENUSB_PIPE_TYPE_SHIFT) & XENUSB_PIPE_TYPE_MASK)
#define xenusb_pipeisoc(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_ISOC)
#define xenusb_pipeint(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_INT)
#define xenusb_pipectrl(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_CTRL)
#define xenusb_pipebulk(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_BULK)

#define XENUSB_MAX_SEGMENTS_PER_REQUEST (16)
#define XENUSB_MAX_PORTNR		31
#define XENUSB_RING_SIZE		4096

/*
 * RING for transferring urbs.
 */
struct xenusb_request_segment {
	grant_ref_t gref;
	uint16_t offset;
	uint16_t length;
};

struct xenusb_urb_request {
	uint16_t id;			/* request id */
	uint16_t nr_buffer_segs;	/* number of urb->transfer_buffer segments */

	/* basic urb parameter */
	uint32_t pipe;
	uint16_t transfer_flags;
#define XENUSB_SHORT_NOT_OK	0x0001
	uint16_t buffer_length;
	union {
		uint8_t ctrl[8];	/* setup_packet (Ctrl) */

		struct {
			uint16_t interval;	/* maximum (1024*8) in usb core */
			uint16_t start_frame;	/* start frame */
			uint16_t number_of_packets;	/* number of ISO packet */
			uint16_t nr_frame_desc_segs;	/* number of iso_frame_desc segments */
		} isoc;

		struct {
			uint16_t interval;	/* maximum (1024*8) in usb core */
			uint16_t pad[3];
		} intr;

		struct {
			uint16_t unlink_id;	/* unlink request id */
			uint16_t pad[3];
		} unlink;

	} u;

	/* urb data segments */
	struct xenusb_request_segment seg[XENUSB_MAX_SEGMENTS_PER_REQUEST];
};

struct xenusb_urb_response {
	uint16_t id;		/* request id */
	uint16_t start_frame;	/* start frame (ISO) */
	int32_t status;		/* status (non-ISO) */
#define XENUSB_STATUS_OK	0
#define XENUSB_STATUS_NODEV	(-19)
#define XENUSB_STATUS_INVAL	(-22)
#define XENUSB_STATUS_STALL	(-32)
#define XENUSB_STATUS_IOERROR	(-71)
#define XENUSB_STATUS_BABBLE	(-75)
#define XENUSB_STATUS_SHUTDOWN	(-108)
	int32_t actual_length;	/* actual transfer length */
	int32_t error_count;	/* number of ISO errors */
};

DEFINE_RING_TYPES(xenusb_urb, struct xenusb_urb_request, struct xenusb_urb_response);
#define XENUSB_URB_RING_SIZE __CONST_RING_SIZE(xenusb_urb, XENUSB_RING_SIZE)

/*
 * RING for notifying connect/disconnect events to frontend
 */
struct xenusb_conn_request {
	uint16_t id;
};

struct xenusb_conn_response {
	uint16_t id;		/* request id */
	uint8_t portnum;	/* port number */
	uint8_t speed;		/* usb_device_speed */
#define XENUSB_SPEED_NONE	0
#define XENUSB_SPEED_LOW	1
#define XENUSB_SPEED_FULL	2
#define XENUSB_SPEED_HIGH	3
};

DEFINE_RING_TYPES(xenusb_conn, struct xenusb_conn_request, struct xenusb_conn_response);
#define XENUSB_CONN_RING_SIZE __CONST_RING_SIZE(xenusb_conn, XENUSB_RING_SIZE)

#endif /* __XEN_PUBLIC_IO_USBIF_H__ */
