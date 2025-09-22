/*	$OpenBSD: usbdivar.h,v 1.85 2025/03/01 14:43:03 kirill Exp $ */
/*	$NetBSD: usbdivar.h,v 1.70 2002/07/11 21:14:36 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdivar.h,v 1.11 1999/11/17 22:33:51 n_hibma Exp $	*/

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

#ifndef _USBDIVAR_H_
#define _USBDIVAR_H_

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <sys/timeout.h>

/* From usb_mem.h */
struct usb_dma_block;
struct usb_dma {
	struct usb_dma_block	*block;
	u_int			 offs;
#define USB_DMA_COHERENT		(1 << 0)
};

struct usbd_xfer;
struct usbd_pipe;

struct usbd_endpoint {
	usb_endpoint_descriptor_t *edesc;
	usb_endpoint_ss_comp_descriptor_t *esscd;
	int			refcnt;
	int			savedtoggle;
};

struct usbd_bus_methods {
	usbd_status	      (*open_pipe)(struct usbd_pipe *);
	int		      (*dev_setaddr)(struct usbd_device *, int);
	void		      (*soft_intr)(void *);
	void		      (*do_poll)(struct usbd_bus *);
	struct usbd_xfer *    (*allocx)(struct usbd_bus *);
	void		      (*freex)(struct usbd_bus *, struct usbd_xfer *);
};

struct usbd_pipe_methods {
	usbd_status	      (*transfer)(struct usbd_xfer *);
	usbd_status	      (*start)(struct usbd_xfer *);
	void		      (*abort)(struct usbd_xfer *);
	void		      (*close)(struct usbd_pipe *);
	void		      (*cleartoggle)(struct usbd_pipe *);
	void		      (*done)(struct usbd_xfer *);
};

struct usbd_tt {
	struct usbd_hub	       *hub;
	void		       *hcpriv;
};

struct usbd_port {
	usb_port_status_t	status;
	u_int16_t		power;	/* mA of current on port */
	u_int8_t		portno;
	u_int8_t		restartcnt;
#define USBD_RESTART_MAX 5
	u_int8_t		reattach;
	struct usbd_device     *device;	/* Connected device */
	struct usbd_device     *parent;	/* The ports hub */
	struct usbd_tt	       *tt; /* Transaction translator (if any) */
};

struct usbd_hub {
	int		      (*explore)(struct usbd_device *);
	void		       *hubsoftc;
	struct usbd_port       *ports;
	int			nports;
	u_int8_t		powerdelay;
	u_int8_t		ttthink;
	u_int8_t		multi;
};

struct usbd_bus {
	/* Filled by HC driver */
	struct device		bdev; /* base device, host adapter */
	const struct usbd_bus_methods *methods;
#if NBPFILTER > 0
	void			*bpfif;
	caddr_t			bpf;
#endif
	u_int32_t		pipe_size; /* size of a pipe struct */
	/* Filled by usb driver */
	struct usbd_device     *root_hub;
	struct usbd_device	*devices[USB_MAX_DEVICES];
	char			use_polling;
	char			dying;
	int			flags;
#define USB_BUS_CONFIG_PENDING	0x01
#define USB_BUS_DISCONNECTING	0x02
	struct device	       *usbctl;
	struct usb_device_stats	stats;
	int 			intr_context;
	u_int			no_intrs;
	int			usbrev;	/* USB revision */
#define USBREV_UNKNOWN	0
#define USBREV_PRE_1_0	1
#define USBREV_1_0	2
#define USBREV_1_1	3
#define USBREV_2_0	4
#define USBREV_3_0	5
#define USBREV_STR { "unknown", "pre 1.0", "1.0", "1.1", "2.0", "3.0" }
	void		       *soft; /* soft interrupt cookie */
	bus_dma_tag_t		dmatag;	/* DMA tag */
	int			dmaflags;
};

struct usbd_device {
	struct usbd_bus	       *bus;           /* our controller */
	struct usbd_pipe       *default_pipe;  /* pipe 0 */
	u_int8_t		dying;	       /* hardware removed */
	u_int8_t		ref_cnt;       /* # of procs using device */
	u_int8_t		address;       /* device address */
	u_int8_t		config;	       /* current configuration # */
	u_int8_t		depth;         /* distance from root hub */
	u_int8_t		speed;         /* low/full/high speed */
	u_int8_t		self_powered;  /* flag for self powered */
	u_int16_t		power;         /* mA the device uses */
	int16_t			langid;	       /* language for strings */
#define USBD_NOLANG (-1)
	struct usbd_port       *powersrc;      /* upstream hub port, or 0 */
	struct usbd_device     *myhub; 	       /* upstream hub */
	struct usbd_port       *myhsport;      /* closest high speed port */
	struct usbd_endpoint	def_ep;	       /* for pipe 0 */
	usb_endpoint_descriptor_t def_ep_desc; /* for pipe 0 */
	struct usbd_interface  *ifaces;        /* array of all interfaces */
	usb_device_descriptor_t ddesc;         /* device descriptor */
	usb_config_descriptor_t *cdesc;	       /* full config descr */
	const struct usbd_quirks     *quirks;  /* device quirks, always set */
	struct usbd_hub	       *hub;           /* only if this is a hub */
	struct device         **subdevs;       /* sub-devices, 0 terminated */
	int			nsubdev;       /* size of the `subdevs' array */
	int			ndevs;	       /* # of subdevs */

	char                   *serial;        /* serial number, can be NULL */
	char                   *vendor;        /* vendor string, can be NULL */
	char                   *product;       /* product string, can be NULL */
};

struct usbd_interface {
	struct usbd_device     *device;
	usb_interface_descriptor_t *idesc;
	int			index;
	int			altindex;
	struct usbd_endpoint   *endpoints;
	void		       *priv;
	LIST_HEAD(, usbd_pipe)	pipes;
	uint8_t			claimed;
	uint8_t			nendpt;
};

struct usbd_pipe {
	struct usbd_interface  *iface;
	struct usbd_device     *device;
	struct usbd_endpoint   *endpoint;
	size_t			pipe_size;
	char			running;
	char			aborting;
	SIMPLEQ_HEAD(, usbd_xfer) queue;
	LIST_ENTRY(usbd_pipe)	next;

	struct usbd_xfer	*intrxfer; /* used for repeating requests */
	char			repeat;
	int			interval;

	/* Filled by HC driver. */
	const struct usbd_pipe_methods *methods;
};

struct usbd_xfer {
	struct usbd_pipe       *pipe;
	void		       *priv;
	char		       *buffer;
	u_int32_t		length;
	u_int32_t		actlen;
	u_int16_t		flags;
	u_int32_t		timeout;
	usbd_status		status;
	usbd_callback		callback;
	volatile char		done;
#ifdef DIAGNOSTIC
	u_int32_t		busy_free;
#define XFER_FREE 0x42555359
#define XFER_ONQU 0x4f4e5155
#endif

	/* For control pipe */
	usb_device_request_t	request;

	/* For isoc */
	u_int16_t		*frlengths;
	int			nframes;

	/* For memory allocation */
	struct usbd_device     *device;
	struct usb_dma		dmabuf;

	int			rqflags;
#define URQ_REQUEST	0x01
#define URQ_AUTO_DMABUF	0x10
#define URQ_DEV_DMABUF	0x20

	SIMPLEQ_ENTRY(usbd_xfer) next;

	void		       *hcpriv; /* private use by the HC driver */

	struct usb_task		abort_task;
	struct timeout		timeout_handle;
};

void usbd_dump_iface(struct usbd_interface *);
void usbd_dump_device(struct usbd_device *);
void usbd_dump_endpoint(struct usbd_endpoint *);
void usbd_dump_queue(struct usbd_pipe *);
void usbd_dump_pipe(struct usbd_pipe *);

/* Routines from usb_subr.c */
int		usbctlprint(void *, const char *);
void		usb_delay_ms(struct usbd_bus *, u_int);
usbd_status	usbd_port_disown_to_1_1(struct usbd_device *, int);
int		usbd_reset_port(struct usbd_device *, int);
usbd_status	usbd_setup_pipe(struct usbd_device *,
		    struct usbd_interface *, struct usbd_endpoint *, int,
		    struct usbd_pipe **);
int		usbd_set_address(struct usbd_device *, int);
usbd_status	usbd_new_device(struct device *, struct usbd_bus *,
		    int, int, int, struct usbd_port *);
usbd_status	usbd_fill_iface_data(struct usbd_device *, int, int);

usbd_status	usb_insert_transfer(struct usbd_xfer *);
void		usb_transfer_complete(struct usbd_xfer *);
int		usbd_detach(struct usbd_device *, struct device *);

/* Routines from usb.c */
void		usb_needs_explore(struct usbd_device *, int);
void		usb_needs_reattach(struct usbd_device *);
void		usb_schedsoftintr(struct usbd_bus *);
void		usb_tap(struct usbd_bus *, struct usbd_xfer *, uint8_t);

#define USBTAP_DIR_OUT	0
#define USBTAP_DIR_IN	1

#define	UHUB_UNK_CONFIGURATION	-1
#define	UHUB_UNK_INTERFACE	-1

static inline int
usbd_xfer_isread(struct usbd_xfer *xfer)
{
	if (xfer->rqflags & URQ_REQUEST)
		return (xfer->request.bmRequestType & UT_READ);

	return (xfer->pipe->endpoint->edesc->bEndpointAddress & UE_DIR_IN);
}

#endif /* _USBDIVAR_H_ */
