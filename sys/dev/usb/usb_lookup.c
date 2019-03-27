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

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/limits.h>
#include <sys/endian.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

/*------------------------------------------------------------------------*
 *	usbd_lookup_id_by_info
 *
 * This functions takes an array of "struct usb_device_id" and tries
 * to match the entries with the information in "struct usbd_lookup_info".
 *
 * NOTE: The "sizeof_id" parameter must be a multiple of the
 * usb_device_id structure size. Else the behaviour of this function
 * is undefined.
 *
 * Return values:
 * NULL: No match found.
 * Else: Pointer to matching entry.
 *------------------------------------------------------------------------*/
const struct usb_device_id *
usbd_lookup_id_by_info(const struct usb_device_id *id, usb_size_t sizeof_id,
    const struct usbd_lookup_info *info)
{
	const struct usb_device_id *id_end;

	if (id == NULL) {
		goto done;
	}
	id_end = (const void *)(((const uint8_t *)id) + sizeof_id);

	/*
	 * Keep on matching array entries until we find a match or
	 * until we reach the end of the matching array:
	 */
	for (; id != id_end; id++) {

		if ((id->match_flag_vendor) &&
		    (id->idVendor != info->idVendor)) {
			continue;
		}
		if ((id->match_flag_product) &&
		    (id->idProduct != info->idProduct)) {
			continue;
		}
		if ((id->match_flag_dev_lo) &&
		    (id->bcdDevice_lo > info->bcdDevice)) {
			continue;
		}
		if ((id->match_flag_dev_hi) &&
		    (id->bcdDevice_hi < info->bcdDevice)) {
			continue;
		}
		if ((id->match_flag_dev_class) &&
		    (id->bDeviceClass != info->bDeviceClass)) {
			continue;
		}
		if ((id->match_flag_dev_subclass) &&
		    (id->bDeviceSubClass != info->bDeviceSubClass)) {
			continue;
		}
		if ((id->match_flag_dev_protocol) &&
		    (id->bDeviceProtocol != info->bDeviceProtocol)) {
			continue;
		}
		if ((id->match_flag_int_class) &&
		    (id->bInterfaceClass != info->bInterfaceClass)) {
			continue;
		}
		if ((id->match_flag_int_subclass) &&
		    (id->bInterfaceSubClass != info->bInterfaceSubClass)) {
			continue;
		}
		if ((id->match_flag_int_protocol) &&
		    (id->bInterfaceProtocol != info->bInterfaceProtocol)) {
			continue;
		}
		/* We found a match! */
		return (id);
	}

done:
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usbd_lookup_id_by_uaa - factored out code
 *
 * Return values:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
int
usbd_lookup_id_by_uaa(const struct usb_device_id *id, usb_size_t sizeof_id,
    struct usb_attach_arg *uaa)
{
	id = usbd_lookup_id_by_info(id, sizeof_id, &uaa->info);
	if (id) {
		/* copy driver info */
		uaa->driver_info = id->driver_info;
		return (0);
	}
	return (ENXIO);
}

