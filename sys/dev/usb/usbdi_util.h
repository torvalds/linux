/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Andrew Thompson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _USB_USBDI_UTIL_H_
#define _USB_USBDI_UTIL_H_

struct cv;

/* structures */

struct usb_idesc_parse_state {
	struct usb_descriptor *desc;
	uint8_t iface_index;		/* current interface index */
	uint8_t iface_no_last;
	uint8_t iface_index_alt;	/* current alternate setting */
};

/* prototypes */

usb_error_t usbd_do_request_proc(struct usb_device *udev, struct usb_process *pproc,
		    struct usb_device_request *req, void *data, uint16_t flags,
		    uint16_t *actlen, usb_timeout_t timeout);

struct usb_descriptor *usb_desc_foreach(struct usb_config_descriptor *cd,
	    struct usb_descriptor *desc);
struct usb_interface_descriptor *usb_idesc_foreach(
	    struct usb_config_descriptor *cd,
	    struct usb_idesc_parse_state *ps);
struct usb_endpoint_descriptor *usb_edesc_foreach(
	    struct usb_config_descriptor *cd,
	    struct usb_endpoint_descriptor *ped);
struct usb_endpoint_ss_comp_descriptor *usb_ed_comp_foreach(
	    struct usb_config_descriptor *cd,
	    struct usb_endpoint_ss_comp_descriptor *ped);
uint8_t usbd_get_no_descriptors(struct usb_config_descriptor *cd,
	    uint8_t type);
uint8_t usbd_get_no_alts(struct usb_config_descriptor *cd,
	    struct usb_interface_descriptor *id);

usb_error_t usbd_req_get_report(struct usb_device *udev, struct mtx *mtx,
		    void *data, uint16_t len, uint8_t iface_index, uint8_t type,
		    uint8_t id);
usb_error_t usbd_req_get_report_descriptor(struct usb_device *udev,
		    struct mtx *mtx, void *d, uint16_t size,
		    uint8_t iface_index);
usb_error_t usbd_req_get_string_any(struct usb_device *udev, struct mtx *mtx,
		    char *buf, uint16_t len, uint8_t string_index);
usb_error_t usbd_req_get_string_desc(struct usb_device *udev, struct mtx *mtx,
		    void *sdesc, uint16_t max_len, uint16_t lang_id,
		    uint8_t string_index);
usb_error_t usbd_req_set_config(struct usb_device *udev, struct mtx *mtx,
		    uint8_t conf);
usb_error_t usbd_req_set_alt_interface_no(struct usb_device *udev,
		    struct mtx *mtx, uint8_t iface_index, uint8_t alt_no);
usb_error_t usbd_req_set_idle(struct usb_device *udev, struct mtx *mtx,
		    uint8_t iface_index, uint8_t duration, uint8_t id);
usb_error_t usbd_req_set_protocol(struct usb_device *udev, struct mtx *mtx,
		    uint8_t iface_index, uint16_t report);
usb_error_t usbd_req_set_report(struct usb_device *udev, struct mtx *mtx,
		    void *data, uint16_t len, uint8_t iface_index,
		    uint8_t type, uint8_t id);

/* The following functions will not return NULL strings. */

const char *usb_get_manufacturer(struct usb_device *);
const char *usb_get_product(struct usb_device *);
const char *usb_get_serial(struct usb_device *);

#endif /* _USB_USBDI_UTIL_H_ */
