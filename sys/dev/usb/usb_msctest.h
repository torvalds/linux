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

#ifndef _USB_MSCTEST_H_
#define	_USB_MSCTEST_H_

enum {
	MSC_EJECT_STOPUNIT,
	MSC_EJECT_REZERO,
	MSC_EJECT_ZTESTOR,
	MSC_EJECT_CMOTECH,
	MSC_EJECT_HUAWEI,
	MSC_EJECT_HUAWEI2,
	MSC_EJECT_TCT,
};

int usb_iface_is_cdrom(struct usb_device *udev,
	    uint8_t iface_index);
usb_error_t usb_msc_eject(struct usb_device *udev,
	    uint8_t iface_index, int method);
usb_error_t usb_msc_auto_quirk(struct usb_device *udev,
	    uint8_t iface_index);
usb_error_t usb_msc_read_10(struct usb_device *udev,
	    uint8_t iface_index, uint32_t lba, uint32_t blocks,
	    void *buffer);
usb_error_t usb_msc_write_10(struct usb_device *udev,
	    uint8_t iface_index, uint32_t lba, uint32_t blocks,
	    void *buffer);
usb_error_t usb_msc_read_capacity(struct usb_device *udev,
	    uint8_t iface_index, uint32_t *lba_last,
	    uint32_t *block_size);
usb_error_t usb_dymo_eject(struct usb_device *udev,
	    uint8_t iface_index);

#endif					/* _USB_MSCTEST_H_ */
