/*	$OpenBSD: usbdi.h,v 1.73 2023/10/01 15:58:12 krw Exp $ */
/*	$NetBSD: usbdi.h,v 1.62 2002/07/11 21:14:35 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdi.h,v 1.18 1999/11/17 22:33:49 n_hibma Exp $	*/

/*
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

#ifndef _USBDI_H_
#define _USBDI_H_

struct usbd_bus;
struct usbd_device;
struct usbd_interface;
struct usbd_pipe;
struct usbd_xfer;

typedef enum {
	USBD_NORMAL_COMPLETION = 0, /* must be 0 */
	USBD_IN_PROGRESS,	/* 1 */
	/* errors */
	USBD_PENDING_REQUESTS,	/* 2 */
	USBD_NOT_STARTED,	/* 3 */
	USBD_INVAL,		/* 4 */
	USBD_NOMEM,		/* 5 */
	USBD_CANCELLED,		/* 6 */
	USBD_BAD_ADDRESS,	/* 7 */
	USBD_IN_USE,		/* 8 */
	USBD_NO_ADDR,		/* 9 */
	USBD_SET_ADDR_FAILED,	/* 10 */
	USBD_NO_POWER,		/* 11 */
	USBD_TOO_DEEP,		/* 12 */
	USBD_IOERROR,		/* 13 */
	USBD_NOT_CONFIGURED,	/* 14 */
	USBD_TIMEOUT,		/* 15 */
	USBD_SHORT_XFER,	/* 16 */
	USBD_STALLED,		/* 17 */
	USBD_INTERRUPTED,	/* 18 */

	USBD_ERROR_MAX		/* must be last */
} usbd_status;

typedef void (*usbd_callback)(struct usbd_xfer *, void *, usbd_status);

/* Open flags */
#define USBD_EXCLUSIVE_USE	0x01

/* Use default (specified by ep. desc.) interval on interrupt pipe */
#define USBD_DEFAULT_INTERVAL	(-1)

/* Request flags */
#define USBD_NO_COPY		0x01	/* do not copy data to DMA buffer */
#define USBD_SYNCHRONOUS	0x02	/* wait for completion */
/* in usb.h #define USBD_SHORT_XFER_OK	0x04*/	/* allow short reads */
#define USBD_FORCE_SHORT_XFER	0x08	/* force last short packet on write */
#define USBD_CATCH		0x10	/* catch signals while sleeping */

#define USBD_NO_TIMEOUT 0
#define USBD_DEFAULT_TIMEOUT 5000 /* ms = 5 s */

#define DEVINFOSIZE 1024

usbd_status usbd_open_pipe(struct usbd_interface *iface, u_int8_t address,
    u_int8_t flags, struct usbd_pipe **pipe);
usbd_status usbd_close_pipe(struct usbd_pipe *pipe);
usbd_status usbd_transfer(struct usbd_xfer *req);
struct usbd_xfer *usbd_alloc_xfer(struct usbd_device *);
void usbd_free_xfer(struct usbd_xfer *xfer);
void usbd_setup_xfer(struct usbd_xfer *xfer, struct usbd_pipe *pipe,
    void *priv, void *buffer, u_int32_t length, u_int16_t flags,
    u_int32_t timeout, usbd_callback);
void usbd_setup_default_xfer(struct usbd_xfer *xfer, struct usbd_device *dev,
    void *priv, u_int32_t timeout, usb_device_request_t *req,
    void *buffer, u_int32_t length, u_int16_t flags, usbd_callback);
void usbd_setup_isoc_xfer(struct usbd_xfer *xfer, struct usbd_pipe *pipe,
    void *priv, u_int16_t *frlengths, u_int32_t nframes,
    u_int16_t flags, usbd_callback);
void usbd_get_xfer_status(struct usbd_xfer *xfer, void **priv,
    void **buffer, u_int32_t *count, usbd_status *status);
usb_endpoint_descriptor_t *usbd_interface2endpoint_descriptor(
    struct usbd_interface *iface, u_int8_t address);
void usbd_abort_pipe(struct usbd_pipe *pipe);
usbd_status usbd_clear_endpoint_stall(struct usbd_pipe *pipe);
usbd_status usbd_clear_endpoint_stall_async(struct usbd_pipe *pipe);
void usbd_clear_endpoint_toggle(struct usbd_pipe *pipe);
usbd_status usbd_device2interface_handle(struct usbd_device *dev,
    u_int8_t ifaceno, struct usbd_interface **iface);

void *usbd_alloc_buffer(struct usbd_xfer *xfer, u_int32_t size);
void usbd_free_buffer(struct usbd_xfer *xfer);
usbd_status usbd_open_pipe_intr(struct usbd_interface *iface, u_int8_t address,
    u_int8_t flags, struct usbd_pipe **pipe, void *priv,
    void *buffer, u_int32_t length, usbd_callback, int);
usbd_status usbd_do_request(struct usbd_device *pipe, usb_device_request_t *req,
    void *data);
usbd_status usbd_request_async(struct usbd_xfer *xfer,
    usb_device_request_t *req, void *priv, usbd_callback callback);
usbd_status usbd_do_request_flags(struct usbd_device *pipe,
    usb_device_request_t *req, void *data, u_int16_t flags, int*, u_int32_t);
usb_interface_descriptor_t *usbd_get_interface_descriptor(
    struct usbd_interface *iface);
usb_config_descriptor_t *usbd_get_config_descriptor(struct usbd_device *dev);
usb_device_descriptor_t *usbd_get_device_descriptor(struct usbd_device *dev);
usbd_status usbd_set_interface(struct usbd_interface *, int);
int usbd_get_no_alts(usb_config_descriptor_t *, int);
void usbd_fill_deviceinfo(struct usbd_device *, struct usb_device_info *);
int usbd_get_routestring(struct usbd_device *, uint32_t *);
int usbd_get_location(struct usbd_device *, struct usbd_interface *, uint8_t *,
    uint32_t *, uint8_t *);
usb_config_descriptor_t *usbd_get_cdesc(struct usbd_device *, int, u_int *);
int usbd_get_interface_altindex(struct usbd_interface *iface);

usb_interface_descriptor_t *usbd_find_idesc(usb_config_descriptor_t *cd,
    int iindex, int ano);
usb_endpoint_descriptor_t *usbd_find_edesc(usb_config_descriptor_t *cd,
    int ifaceno, int altno, int endptidx);

void usbd_dopoll(struct usbd_device *);
void usbd_set_polling(struct usbd_device *iface, int on);

const char *usbd_errstr(usbd_status err);

const struct usbd_quirks *usbd_get_quirks(struct usbd_device *);
usb_endpoint_descriptor_t *usbd_get_endpoint_descriptor(
    struct usbd_interface *iface, u_int8_t address);

usbd_status usbd_reload_device_desc(struct usbd_device *);

int usbd_ratecheck(struct timeval *last);

int usbd_get_devcnt(struct usbd_device *);
void usbd_claim_iface(struct usbd_device *, int);
int usbd_iface_claimed(struct usbd_device *, int);

int usbd_is_dying(struct usbd_device *);
void usbd_deactivate(struct usbd_device *);

void usbd_ref_incr(struct usbd_device *);
void usbd_ref_decr(struct usbd_device *);
void usbd_ref_wait(struct usbd_device *);

/* An iterator for descriptors. */
struct usbd_desc_iter {
	const uByte *cur;
	const uByte *end;
};
void usbd_desc_iter_init(struct usbd_device *, struct usbd_desc_iter *);
const usb_descriptor_t *usbd_desc_iter_next(struct usbd_desc_iter *);

int usbd_str(usb_string_descriptor_t *, int, const char *);

/*
 * The usb_task structs form a queue of things to run in the USB task
 * threads.  Normally this is just device discovery when a connect/disconnect
 * has been detected.  But it may also be used by drivers that need to
 * perform (short) tasks that must have a process context.
 */
struct usb_task {
	TAILQ_ENTRY(usb_task) next;
	struct usbd_device *dev;
	void (*fun)(void *);
	void *arg;
	char type;
#define	USB_TASK_TYPE_GENERIC	0
#define USB_TASK_TYPE_EXPLORE	1
#define USB_TASK_TYPE_ABORT	2
	u_int state;
#define	USB_TASK_STATE_NONE	0x0
#define	USB_TASK_STATE_ONQ	0x1
#define	USB_TASK_STATE_RUN	0x2

};

void usb_add_task(struct usbd_device *, struct usb_task *);
void usb_rem_task(struct usbd_device *, struct usb_task *);
void usb_wait_task(struct usbd_device *, struct usb_task *);
void usb_rem_wait_task(struct usbd_device *, struct usb_task *);
#define usb_init_task(t, f, a, y) \
	((t)->fun = (f),	\
	(t)->arg = (a),		\
	(t)->type = (y),	\
	(t)->state = USB_TASK_STATE_NONE)

struct usb_devno {
	u_int16_t ud_vendor;
	u_int16_t ud_product;
};
const struct usb_devno *usbd_match_device(const struct usb_devno *tbl,
    u_int nentries, u_int sz, u_int16_t vendor, u_int16_t product);
#define usb_lookup(tbl, vendor, product) \
	usbd_match_device((const struct usb_devno *)(tbl), sizeof (tbl) / sizeof ((tbl)[0]), sizeof ((tbl)[0]), (vendor), (product))
#define	USB_PRODUCT_ANY		0xffff

/* Attach data */
struct usb_attach_arg {
	int			port;
	int			configno;
	int			ifaceno;
	int			vendor;
	int			product;
	int			release;
	struct usbd_device	*device;	/* current device */
	struct usbd_interface	*iface; /* current interface */
	int			usegeneric;
	struct usbd_interface	**ifaces;/* all interfaces */
	int			nifaces; /* number of interfaces */
	void			*cookie;
};

/* Match codes. */
/* First five codes is for a whole device. */
#define UMATCH_VENDOR_PRODUCT_REV			14
#define UMATCH_VENDOR_PRODUCT				13
#define UMATCH_VENDOR_DEVCLASS_DEVPROTO			12
#define UMATCH_DEVCLASS_DEVSUBCLASS_DEVPROTO		11
#define UMATCH_DEVCLASS_DEVSUBCLASS			10
/* Next six codes are for interfaces. */
#define UMATCH_VENDOR_PRODUCT_REV_CONF_IFACE		 9
#define UMATCH_VENDOR_PRODUCT_CONF_IFACE		 8
#define UMATCH_VENDOR_IFACESUBCLASS_IFACEPROTO		 7
#define UMATCH_VENDOR_IFACESUBCLASS			 6
#define UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO	 5
#define UMATCH_IFACECLASS_IFACESUBCLASS			 4
#define UMATCH_IFACECLASS				 3
#define UMATCH_IFACECLASS_GENERIC			 2
/* Generic driver */
#define UMATCH_GENERIC					 1
/* No match */
#define UMATCH_NONE					 0

#define	IPL_USB		IPL_BIO
#define	IPL_SOFTUSB	IPL_SOFTNET

#define splusb()	splraise(IPL_SOFTUSB)
#define splhardusb()	splraise(IPL_USB)


#endif /* _USBDI_H_ */
