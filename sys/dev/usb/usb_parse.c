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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USB_DEBUG_VAR usb_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

/*------------------------------------------------------------------------*
 *	usb_desc_foreach
 *
 * This function is the safe way to iterate across the USB config
 * descriptor. It contains several checks against invalid
 * descriptors. If the "desc" argument passed to this function is
 * "NULL" the first descriptor, if any, will be returned.
 *
 * Return values:
 *   NULL: End of descriptors
 *   Else: Next descriptor after "desc"
 *------------------------------------------------------------------------*/
struct usb_descriptor *
usb_desc_foreach(struct usb_config_descriptor *cd, 
    struct usb_descriptor *_desc)
{
	uint8_t *desc_next;
	uint8_t *start;
	uint8_t *end;
	uint8_t *desc;

	/* be NULL safe */
	if (cd == NULL)
		return (NULL);

	/* We assume that the "wTotalLength" has been checked. */
	start = (uint8_t *)cd;
	end = start + UGETW(cd->wTotalLength);
	desc = (uint8_t *)_desc;

	/* Get start of next USB descriptor. */
	if (desc == NULL)
		desc = start;
	else
		desc = desc + desc[0];

	/* Check that the next USB descriptor is within the range. */
	if ((desc < start) || (desc >= end))
		return (NULL);		/* out of range, or EOD */

	/* Check that the second next USB descriptor is within range. */
	desc_next = desc + desc[0];
	if ((desc_next < start) || (desc_next > end))
		return (NULL);		/* out of range */

	/* Check minimum descriptor length. */
	if (desc[0] < 3)
		return (NULL);		/* too short descriptor */

	/* Return start of next descriptor. */
	return ((struct usb_descriptor *)desc);
}

/*------------------------------------------------------------------------*
 *	usb_idesc_foreach
 *
 * This function will iterate the interface descriptors in the config
 * descriptor. The parse state structure should be zeroed before
 * calling this function the first time.
 *
 * Return values:
 *   NULL: End of descriptors
 *   Else: A valid interface descriptor
 *------------------------------------------------------------------------*/
struct usb_interface_descriptor *
usb_idesc_foreach(struct usb_config_descriptor *cd,
    struct usb_idesc_parse_state *ps)
{
	struct usb_interface_descriptor *id;
	uint8_t new_iface;

	/* retrieve current descriptor */
	id = (struct usb_interface_descriptor *)ps->desc;
	/* default is to start a new interface */
	new_iface = 1;

	while (1) {
		id = (struct usb_interface_descriptor *)
		    usb_desc_foreach(cd, (struct usb_descriptor *)id);
		if (id == NULL)
			break;
		if ((id->bDescriptorType == UDESC_INTERFACE) &&
		    (id->bLength >= sizeof(*id))) {
			if (ps->iface_no_last == id->bInterfaceNumber)
				new_iface = 0;
			ps->iface_no_last = id->bInterfaceNumber;
			break;
		}
	}

	if (ps->desc == NULL) {
		/* first time or zero descriptors */
	} else if (new_iface) {
		/* new interface */
		ps->iface_index ++;
		ps->iface_index_alt = 0;
	} else {
		/* new alternate interface */
		ps->iface_index_alt ++;
	}
#if (USB_IFACE_MAX <= 0)
#error "USB_IFACE_MAX must be defined greater than zero"
#endif
	/* check for too many interfaces */
	if (ps->iface_index >= USB_IFACE_MAX) {
		DPRINTF("Interface limit reached\n");
		id = NULL;
	}

	/* store and return current descriptor */
	ps->desc = (struct usb_descriptor *)id;
	return (id);
}

/*------------------------------------------------------------------------*
 *	usb_edesc_foreach
 *
 * This function will iterate all the endpoint descriptors within an
 * interface descriptor. Starting value for the "ped" argument should
 * be a valid interface descriptor.
 *
 * Return values:
 *   NULL: End of descriptors
 *   Else: A valid endpoint descriptor
 *------------------------------------------------------------------------*/
struct usb_endpoint_descriptor *
usb_edesc_foreach(struct usb_config_descriptor *cd,
    struct usb_endpoint_descriptor *ped)
{
	struct usb_descriptor *desc;

	desc = ((struct usb_descriptor *)ped);

	while ((desc = usb_desc_foreach(cd, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			break;
		}
		if (desc->bDescriptorType == UDESC_ENDPOINT) {
			if (desc->bLength < sizeof(*ped)) {
				/* endpoint descriptor is invalid */
				break;
			}
			return ((struct usb_endpoint_descriptor *)desc);
		}
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_ed_comp_foreach
 *
 * This function will iterate all the endpoint companion descriptors
 * within an endpoint descriptor in an interface descriptor. Starting
 * value for the "ped" argument should be a valid endpoint companion
 * descriptor.
 *
 * Return values:
 *   NULL: End of descriptors
 *   Else: A valid endpoint companion descriptor
 *------------------------------------------------------------------------*/
struct usb_endpoint_ss_comp_descriptor *
usb_ed_comp_foreach(struct usb_config_descriptor *cd,
    struct usb_endpoint_ss_comp_descriptor *ped)
{
	struct usb_descriptor *desc;

	desc = ((struct usb_descriptor *)ped);

	while ((desc = usb_desc_foreach(cd, desc))) {
		if (desc->bDescriptorType == UDESC_INTERFACE)
			break;
		if (desc->bDescriptorType == UDESC_ENDPOINT)
			break;
		if (desc->bDescriptorType == UDESC_ENDPOINT_SS_COMP) {
			if (desc->bLength < sizeof(*ped)) {
				/* endpoint companion descriptor is invalid */
				break;
			}
			return ((struct usb_endpoint_ss_comp_descriptor *)desc);
		}
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usbd_get_no_descriptors
 *
 * This function will count the total number of descriptors in the
 * configuration descriptor of type "type".
 *------------------------------------------------------------------------*/
uint8_t
usbd_get_no_descriptors(struct usb_config_descriptor *cd, uint8_t type)
{
	struct usb_descriptor *desc = NULL;
	uint8_t count = 0;

	while ((desc = usb_desc_foreach(cd, desc))) {
		if (desc->bDescriptorType == type) {
			count++;
			if (count == 0xFF)
				break;			/* crazy */
		}
	}
	return (count);
}

/*------------------------------------------------------------------------*
 *	usbd_get_no_alts
 *
 * Return value:
 *   Number of alternate settings for the given interface descriptor
 *   pointer. If the USB descriptor is corrupt, the returned value can
 *   be greater than the actual number of alternate settings.
 *------------------------------------------------------------------------*/
uint8_t
usbd_get_no_alts(struct usb_config_descriptor *cd,
    struct usb_interface_descriptor *id)
{
	struct usb_descriptor *desc;
	uint8_t n;
	uint8_t ifaceno;

	/* Reset interface count */

	n = 0;

	/* Get the interface number */

	ifaceno = id->bInterfaceNumber;

	/* Iterate all the USB descriptors */

	desc = NULL;
	while ((desc = usb_desc_foreach(cd, desc))) {
		if ((desc->bDescriptorType == UDESC_INTERFACE) &&
		    (desc->bLength >= sizeof(*id))) {
			id = (struct usb_interface_descriptor *)desc;
			if (id->bInterfaceNumber == ifaceno) {
				n++;
				if (n == 0xFF)
					break;		/* crazy */
			}
		}
	}
	return (n);
}
