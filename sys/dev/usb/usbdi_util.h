/*	$OpenBSD: usbdi_util.h,v 1.31 2021/02/24 03:54:05 jsg Exp $ */
/*	$NetBSD: usbdi_util.h,v 1.28 2002/07/11 21:14:36 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdi_util.h,v 1.9 1999/11/17 22:33:50 n_hibma Exp $	*/

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

usbd_status	usbd_get_desc(struct usbd_device *dev, int type,
		    int index, int len, void *desc);
usbd_status	usbd_get_port_status(struct usbd_device *,
		    int, usb_port_status_t *);
usbd_status	usbd_set_hub_depth(struct usbd_device *, int);
usbd_status	usbd_set_port_feature(struct usbd_device *dev, int, int);
usbd_status	usbd_clear_port_feature(struct usbd_device *, int, int);
usbd_status	usbd_clear_endpoint_feature(struct usbd_device *, int, int);
usbd_status	usbd_get_device_status(struct usbd_device *, usb_status_t *);
usbd_status	usbd_get_hub_descriptor(struct usbd_device *,
		    usb_hub_descriptor_t *, uint8_t);
usbd_status	usbd_get_hub_ss_descriptor(struct usbd_device *,
		    usb_hub_ss_descriptor_t *, uint8_t);
struct usb_hid_descriptor *usbd_get_hid_descriptor(struct usbd_device *,
		   usb_interface_descriptor_t *);
usbd_status	usbd_set_idle(struct usbd_device *, int, int, int);
usbd_status	usbd_get_report_descriptor(struct usbd_device *, int, void *,
		    int);
usbd_status	usbd_get_config(struct usbd_device *dev, u_int8_t *conf);
usbd_status	usbd_get_string_desc(struct usbd_device *dev, int sindex,
		    int langid,usb_string_descriptor_t *sdesc, int *sizep);
void		usbd_delay_ms(struct usbd_device *, u_int);


usbd_status	usbd_set_config_no(struct usbd_device *dev, int no, int msg);
usbd_status	usbd_set_config_index(struct usbd_device *dev, int index,
		    int msg);

void usb_detach_wait(struct device *);
void usb_detach_wakeup(struct device *);
