/*
 * usbif.h
 *
 * USB I/O interface for Xen guest OSes.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __XEN_PUBLIC_IO_USBIF_H__
#define __XEN_PUBLIC_IO_USBIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
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
 */

enum usb_spec_version {
	USB_VER_UNKNOWN = 0,
	USB_VER_USB11,
	USB_VER_USB20,
	USB_VER_USB30,	/* not supported yet */
};

/*
 *  USB pipe in usbif_request
 *
 *  - port number:	bits 0-4
 *				(USB_MAXCHILDREN is 31)
 *
 *  - operation flag:	bit 5
 *				(0 = submit urb,
 *				 1 = unlink urb)
 *
 *  - direction:		bit 7
 *				(0 = Host-to-Device [Out]
 *				 1 = Device-to-Host [In])
 *
 *  - device address:	bits 8-14
 *
 *  - endpoint:		bits 15-18
 *
 *  - pipe type:	bits 30-31
 *				(00 = isochronous, 01 = interrupt,
 *				 10 = control, 11 = bulk)
 */

#define USBIF_PIPE_PORT_MASK	0x0000001f
#define USBIF_PIPE_UNLINK	0x00000020
#define USBIF_PIPE_DIR		0x00000080
#define USBIF_PIPE_DEV_MASK	0x0000007f
#define USBIF_PIPE_DEV_SHIFT	8
#define USBIF_PIPE_EP_MASK	0x0000000f
#define USBIF_PIPE_EP_SHIFT	15
#define USBIF_PIPE_TYPE_MASK	0x00000003
#define USBIF_PIPE_TYPE_SHIFT	30
#define USBIF_PIPE_TYPE_ISOC	0
#define USBIF_PIPE_TYPE_INT	1
#define USBIF_PIPE_TYPE_CTRL	2
#define USBIF_PIPE_TYPE_BULK	3

#define usbif_pipeportnum(pipe)			((pipe) & USBIF_PIPE_PORT_MASK)
#define usbif_setportnum_pipe(pipe, portnum)	((pipe) | (portnum))

#define usbif_pipeunlink(pipe)			((pipe) & USBIF_PIPE_UNLINK)
#define usbif_pipesubmit(pipe)			(!usbif_pipeunlink(pipe))
#define usbif_setunlink_pipe(pipe)		((pipe) | USBIF_PIPE_UNLINK)

#define usbif_pipein(pipe)			((pipe) & USBIF_PIPE_DIR)
#define usbif_pipeout(pipe)			(!usbif_pipein(pipe))

#define usbif_pipedevice(pipe)			\
		(((pipe) >> USBIF_PIPE_DEV_SHIFT) & USBIF_PIPE_DEV_MASK)

#define usbif_pipeendpoint(pipe)		\
		(((pipe) >> USBIF_PIPE_EP_SHIFT) & USBIF_PIPE_EP_MASK)

#define usbif_pipetype(pipe)			\
		(((pipe) >> USBIF_PIPE_TYPE_SHIFT) & USBIF_PIPE_TYPE_MASK)
#define usbif_pipeisoc(pipe)	(usbif_pipetype(pipe) == USBIF_PIPE_TYPE_ISOC)
#define usbif_pipeint(pipe)	(usbif_pipetype(pipe) == USBIF_PIPE_TYPE_INT)
#define usbif_pipectrl(pipe)	(usbif_pipetype(pipe) == USBIF_PIPE_TYPE_CTRL)
#define usbif_pipebulk(pipe)	(usbif_pipetype(pipe) == USBIF_PIPE_TYPE_BULK)

#define USBIF_MAX_SEGMENTS_PER_REQUEST (16)
#define USBIF_MAX_PORTNR	31

/*
 * RING for transferring urbs.
 */
struct usbif_request_segment {
	grant_ref_t gref;
	uint16_t offset;
	uint16_t length;
};

struct usbif_urb_request {
	uint16_t id; /* request id */
	uint16_t nr_buffer_segs; /* number of urb->transfer_buffer segments */

	/* basic urb parameter */
	uint32_t pipe;
	uint16_t transfer_flags;
	uint16_t buffer_length;
	union {
		uint8_t ctrl[8]; /* setup_packet (Ctrl) */

		struct {
			uint16_t interval; /* maximum (1024*8) in usb core */
			uint16_t start_frame; /* start frame */
			uint16_t number_of_packets; /* number of ISO packet */
			uint16_t nr_frame_desc_segs; /* number of iso_frame_desc segments */
		} isoc;

		struct {
			uint16_t interval; /* maximum (1024*8) in usb core */
			uint16_t pad[3];
		} intr;

		struct {
			uint16_t unlink_id; /* unlink request id */
			uint16_t pad[3];
		} unlink;

	} u;

	/* urb data segments */
	struct usbif_request_segment seg[USBIF_MAX_SEGMENTS_PER_REQUEST];
};
typedef struct usbif_urb_request usbif_urb_request_t;

struct usbif_urb_response {
	uint16_t id; /* request id */
	uint16_t start_frame;  /* start frame (ISO) */
	int32_t status; /* status (non-ISO) */
	int32_t actual_length; /* actual transfer length */
	int32_t error_count; /* number of ISO errors */
};
typedef struct usbif_urb_response usbif_urb_response_t;

DEFINE_RING_TYPES(usbif_urb, struct usbif_urb_request, struct usbif_urb_response);
#define USB_URB_RING_SIZE __CONST_RING_SIZE(usbif_urb, PAGE_SIZE)

/*
 * RING for notifying connect/disconnect events to frontend
 */
struct usbif_conn_request {
	uint16_t id;
};
typedef struct usbif_conn_request usbif_conn_request_t;

struct usbif_conn_response {
	uint16_t id; /* request id */
	uint8_t portnum; /* port number */
	uint8_t speed; /* usb_device_speed */
#define USBIF_SPEED_NONE	0
#define USBIF_SPEED_LOW		1
#define USBIF_SPEED_FULL	2
#define USBIF_SPEED_HIGH	3
};
typedef struct usbif_conn_response usbif_conn_response_t;

DEFINE_RING_TYPES(usbif_conn, struct usbif_conn_request, struct usbif_conn_response);
#define USB_CONN_RING_SIZE __CONST_RING_SIZE(usbif_conn, PAGE_SIZE)

#endif /* __XEN_PUBLIC_IO_USBIF_H__ */
