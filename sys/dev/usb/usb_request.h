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

#ifndef _USB_REQUEST_H_
#define	_USB_REQUEST_H_

struct usb_process;

usb_error_t usbd_req_clear_hub_feature(struct usb_device *udev,
		    struct mtx *mtx, uint16_t sel);
usb_error_t usbd_req_clear_port_feature(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port, uint16_t sel);
usb_error_t usbd_req_get_alt_interface_no(struct usb_device *udev,
		    struct mtx *mtx, uint8_t *alt_iface_no,
		    uint8_t iface_index);
usb_error_t usbd_req_get_config(struct usb_device *udev, struct mtx *mtx,
		    uint8_t *pconf);
usb_error_t usbd_req_get_descriptor_ptr(struct usb_device *udev,
		    struct usb_config_descriptor **ppcd, uint16_t wValue);
usb_error_t usbd_req_get_config_desc(struct usb_device *udev, struct mtx *mtx,
		    struct usb_config_descriptor *d, uint8_t conf_index);
usb_error_t usbd_req_get_config_desc_full(struct usb_device *udev,
		    struct mtx *mtx, struct usb_config_descriptor **ppcd,
		    uint8_t conf_index);
usb_error_t usbd_req_get_desc(struct usb_device *udev, struct mtx *mtx,
		    uint16_t *actlen, void *desc, uint16_t min_len,
		    uint16_t max_len, uint16_t id, uint8_t type,
		    uint8_t index, uint8_t retries);
usb_error_t usbd_req_get_device_desc(struct usb_device *udev, struct mtx *mtx,
		    struct usb_device_descriptor *d);
usb_error_t usbd_req_get_device_status(struct usb_device *udev,
		    struct mtx *mtx, struct usb_status *st);
usb_error_t usbd_req_get_hub_descriptor(struct usb_device *udev,
		    struct mtx *mtx, struct usb_hub_descriptor *hd,
		    uint8_t nports);
usb_error_t usbd_req_get_ss_hub_descriptor(struct usb_device *udev,
		    struct mtx *mtx, struct usb_hub_ss_descriptor *hd,
		    uint8_t nports);
usb_error_t usbd_req_get_hub_status(struct usb_device *udev, struct mtx *mtx,
		    struct usb_hub_status *st);
usb_error_t usbd_req_get_port_status(struct usb_device *udev, struct mtx *mtx,
		    struct usb_port_status *ps, uint8_t port);
usb_error_t usbd_req_reset_port(struct usb_device *udev, struct mtx *mtx,
		    uint8_t port);
usb_error_t usbd_req_warm_reset_port(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port);
usb_error_t usbd_req_set_address(struct usb_device *udev, struct mtx *mtx,
		    uint16_t addr);
usb_error_t usbd_req_set_hub_feature(struct usb_device *udev, struct mtx *mtx,
		    uint16_t sel);
usb_error_t usbd_req_set_port_feature(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port, uint16_t sel);
usb_error_t usbd_setup_device_desc(struct usb_device *udev, struct mtx *mtx);
usb_error_t usbd_req_re_enumerate(struct usb_device *udev, struct mtx *mtx);
usb_error_t usbd_req_clear_device_feature(struct usb_device *udev,
		    struct mtx *mtx, uint16_t sel);
usb_error_t usbd_req_set_device_feature(struct usb_device *udev,
		    struct mtx *mtx, uint16_t sel);
usb_error_t usbd_req_set_hub_u1_timeout(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port, uint8_t timeout);
usb_error_t usbd_req_set_hub_u2_timeout(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port, uint8_t timeout);
usb_error_t usbd_req_set_hub_depth(struct usb_device *udev,
		    struct mtx *mtx, uint16_t depth);
usb_error_t usbd_req_reset_tt(struct usb_device *udev, struct mtx *mtx,
		    uint8_t port);
usb_error_t usbd_req_clear_tt_buffer(struct usb_device *udev, struct mtx *mtx,
		    uint8_t port, uint8_t addr, uint8_t type, uint8_t endpoint);
usb_error_t usbd_req_set_port_link_state(struct usb_device *udev,
		    struct mtx *mtx, uint8_t port, uint8_t link_state);
usb_error_t usbd_req_set_lpm_info(struct usb_device *udev, struct mtx *mtx,
		    uint8_t port, uint8_t besl, uint8_t addr, uint8_t rwe);

void *	usbd_alloc_config_desc(struct usb_device *, uint32_t);
void	usbd_free_config_desc(struct usb_device *, void *);

#endif					/* _USB_REQUEST_H_ */
