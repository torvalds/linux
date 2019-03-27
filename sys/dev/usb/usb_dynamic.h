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

#ifndef _USB_DYNAMIC_H_
#define	_USB_DYNAMIC_H_

/* prototypes */

struct usb_device;
struct usbd_lookup_info;
struct usb_device_request;

/* typedefs */

typedef usb_error_t	(usb_temp_setup_by_index_t)(struct usb_device *udev,
			    uint16_t index);
typedef uint8_t		(usb_test_quirk_t)(const struct usbd_lookup_info *info,
			    uint16_t quirk);
typedef int		(usb_quirk_ioctl_t)(unsigned long cmd, caddr_t data,
			    int fflag, struct thread *td);
typedef void		(usb_temp_unsetup_t)(struct usb_device *udev);
typedef void		(usb_linux_free_device_t)(struct usb_device *udev);

/* global function pointers */

extern usb_handle_req_t *usb_temp_get_desc_p;
extern usb_temp_setup_by_index_t *usb_temp_setup_by_index_p;
extern usb_linux_free_device_t *usb_linux_free_device_p;
extern usb_temp_unsetup_t *usb_temp_unsetup_p;
extern usb_test_quirk_t *usb_test_quirk_p;
extern usb_quirk_ioctl_t *usb_quirk_ioctl_p;
extern devclass_t usb_devclass_ptr;

/* function prototypes */

void	usb_linux_unload(void *);
void	usb_temp_unload(void *);
void	usb_quirk_unload(void *);
void	usb_bus_unload(void *);

#endif					/* _USB_DYNAMIC_H_ */
