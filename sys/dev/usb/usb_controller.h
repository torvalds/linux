/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

#ifndef _USB_CONTROLLER_H_
#define	_USB_CONTROLLER_H_

/* defines */

#define	USB_BUS_DMA_TAG_MAX 8

/* structure prototypes  */

struct usb_bus;
struct usb_page;
struct usb_endpoint;
struct usb_page_cache;
struct usb_setup_params;
struct usb_hw_ep_profile;
struct usb_fs_isoc_schedule;
struct usb_endpoint_descriptor;

/* typedefs */

typedef void (usb_bus_mem_sub_cb_t)(struct usb_bus *bus, struct usb_page_cache *pc, struct usb_page *pg, usb_size_t size, usb_size_t align);
typedef void (usb_bus_mem_cb_t)(struct usb_bus *bus, usb_bus_mem_sub_cb_t *scb);

/*
 * The following structure is used to define all the USB BUS
 * callbacks.
 */
struct usb_bus_methods {

	/* USB Device and Host mode - Mandatory */

	usb_handle_req_t *roothub_exec;

	void    (*endpoint_init) (struct usb_device *,
		    struct usb_endpoint_descriptor *, struct usb_endpoint *);
	void    (*xfer_setup) (struct usb_setup_params *);
	void    (*xfer_unsetup) (struct usb_xfer *);
	void    (*get_dma_delay) (struct usb_device *, uint32_t *);
	void    (*device_suspend) (struct usb_device *);
	void    (*device_resume) (struct usb_device *);
	void    (*set_hw_power) (struct usb_bus *);
	void    (*set_hw_power_sleep) (struct usb_bus *, uint32_t);
	/*
	 * The following flag is set if one or more control transfers are
	 * active:
	 */
#define	USB_HW_POWER_CONTROL	0x01
	/*
	 * The following flag is set if one or more bulk transfers are
	 * active:
	 */
#define	USB_HW_POWER_BULK	0x02
	/*
	 * The following flag is set if one or more interrupt transfers are
	 * active:
	 */
#define	USB_HW_POWER_INTERRUPT	0x04
	/*
	 * The following flag is set if one or more isochronous transfers
	 * are active:
	 */
#define	USB_HW_POWER_ISOC	0x08
	/*
	 * The following flag is set if one or more non-root-HUB devices 
	 * are present on the given USB bus:
	 */
#define	USB_HW_POWER_NON_ROOT_HUB 0x10
	/*
	 * The following flag is set if we are suspending
	 */
#define	USB_HW_POWER_SUSPEND 0x20
	/*
	 * The following flag is set if we are resuming
	 */
#define	USB_HW_POWER_RESUME 0x40
	/*
	 * The following flag is set if we are shutting down
	 */
#define	USB_HW_POWER_SHUTDOWN 0x60

	/* USB Device mode only - Mandatory */

	void    (*get_hw_ep_profile) (struct usb_device *udev, const struct usb_hw_ep_profile **ppf, uint8_t ep_addr);
	void    (*xfer_stall) (struct usb_xfer *xfer);
	void    (*set_stall) (struct usb_device *udev, struct usb_endpoint *ep, uint8_t *did_stall);

	/* USB Device mode mandatory. USB Host mode optional. */

	void    (*clear_stall) (struct usb_device *udev, struct usb_endpoint *ep);

	/* Optional transfer polling support */

	void	(*xfer_poll) (struct usb_bus *);

	/* Optional fixed power mode support */

	void	(*get_power_mode) (struct usb_device *udev, int8_t *pmode);

	/* Optional endpoint uninit */

	void    (*endpoint_uninit) (struct usb_device *, struct usb_endpoint *);

	/* Optional device init */

	usb_error_t	(*device_init) (struct usb_device *);

	/* Optional device uninit */

	void	(*device_uninit) (struct usb_device *);

	/* Optional for device and host mode */

	void	(*start_dma_delay) (struct usb_xfer *);

	void	(*device_state_change) (struct usb_device *);

	/* Optional for host mode */

	usb_error_t	(*set_address) (struct usb_device *, struct mtx *, uint16_t);

	/* Optional for device and host mode */

	usb_error_t	(*set_endpoint_mode) (struct usb_device *, struct usb_endpoint *, uint8_t);
};

/*
 * The following structure is used to define all the USB pipe
 * callbacks.
 */
struct usb_pipe_methods {

	/* Mandatory USB Device and Host mode callbacks: */

	void	(*open)(struct usb_xfer *);
	void	(*close)(struct usb_xfer *);

	void	(*enter)(struct usb_xfer *);
	void	(*start)(struct usb_xfer *);

	/* Optional */

	void   *info;
};

/*
 * The following structure keeps information about what a hardware USB
 * endpoint supports.
 */
struct usb_hw_ep_profile {
	uint16_t max_in_frame_size;	/* IN-token direction */
	uint16_t max_out_frame_size;	/* OUT-token direction */
	uint8_t	is_simplex:1;
	uint8_t	support_multi_buffer:1;
	uint8_t	support_bulk:1;
	uint8_t	support_control:1;
	uint8_t	support_interrupt:1;
	uint8_t	support_isochronous:1;
	uint8_t	support_in:1;		/* IN-token is supported */
	uint8_t	support_out:1;		/* OUT-token is supported */
};

/* prototypes */

void	usb_bus_mem_flush_all(struct usb_bus *bus, usb_bus_mem_cb_t *cb);
uint8_t	usb_bus_mem_alloc_all(struct usb_bus *bus, bus_dma_tag_t dmat, usb_bus_mem_cb_t *cb);
void	usb_bus_mem_free_all(struct usb_bus *bus, usb_bus_mem_cb_t *cb);
uint16_t usb_isoc_time_expand(struct usb_bus *bus, uint16_t isoc_time_curr);
void	usb_bus_reset_async_locked(struct usb_bus *bus);
#if USB_HAVE_TT_SUPPORT
uint8_t	usbd_fs_isoc_schedule_alloc_slot(struct usb_xfer *isoc_xfer, uint16_t isoc_time);
#endif

#endif					/* _USB_CONTROLLER_H_ */
