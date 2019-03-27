/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Hans Petter Selasky. All rights reserved.
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

/*
 * This file contains sub-routines to build up USB descriptors from
 * USB templates.
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
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#include <dev/usb/usb_cdc.h>
#include <dev/usb/usb_core.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_util.h>

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/template/usb_template.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

MODULE_DEPEND(usb_template, usb, 1, 1, 1);
MODULE_VERSION(usb_template, 1);

/* function prototypes */

static int	sysctl_hw_usb_template_power(SYSCTL_HANDLER_ARGS);
static void	usb_make_raw_desc(struct usb_temp_setup *, const uint8_t *);
static void	usb_make_endpoint_desc(struct usb_temp_setup *,
		    const struct usb_temp_endpoint_desc *);
static void	usb_make_interface_desc(struct usb_temp_setup *,
		    const struct usb_temp_interface_desc *);
static void	usb_make_config_desc(struct usb_temp_setup *,
		    const struct usb_temp_config_desc *);
static void	usb_make_device_desc(struct usb_temp_setup *,
		    const struct usb_temp_device_desc *);
static uint8_t	usb_hw_ep_match(const struct usb_hw_ep_profile *, uint8_t,
		    uint8_t);
static uint8_t	usb_hw_ep_find_match(struct usb_hw_ep_scratch *,
		    struct usb_hw_ep_scratch_sub *, uint8_t);
static uint8_t	usb_hw_ep_get_needs(struct usb_hw_ep_scratch *, uint8_t,
		    uint8_t);
static usb_error_t usb_hw_ep_resolve(struct usb_device *,
		    struct usb_descriptor *);
static const struct usb_temp_device_desc *usb_temp_get_tdd(struct usb_device *);
static void	*usb_temp_get_device_desc(struct usb_device *);
static void	*usb_temp_get_qualifier_desc(struct usb_device *);
static void	*usb_temp_get_config_desc(struct usb_device *, uint16_t *,
		    uint8_t);
static const void *usb_temp_get_string_desc(struct usb_device *, uint16_t,
		    uint8_t);
static const void *usb_temp_get_vendor_desc(struct usb_device *,
		    const struct usb_device_request *, uint16_t *plen);
static const void *usb_temp_get_hub_desc(struct usb_device *);
static usb_error_t usb_temp_get_desc(struct usb_device *,
		    struct usb_device_request *, const void **, uint16_t *);
static usb_error_t usb_temp_setup_by_index(struct usb_device *,
		    uint16_t index);
static void	usb_temp_init(void *);

SYSCTL_NODE(_hw_usb, OID_AUTO, templates, CTLFLAG_RW, 0,
    "USB device side templates");
SYSCTL_PROC(_hw_usb, OID_AUTO, template_power,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_hw_usb_template_power,
    "I", "USB bus power consumption in mA at 5V");

static int	usb_template_power = 500;	/* 500mA */

static int
sysctl_hw_usb_template_power(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = usb_template_power;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (val < 0 || val > 500)
		return (EINVAL);

	usb_template_power = val;

	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_decode_str_desc
 *
 * Helper function to decode string descriptors into a C string.
 *------------------------------------------------------------------------*/
void
usb_decode_str_desc(struct usb_string_descriptor *sd, char *buf, size_t buflen)
{
	size_t i;

	if (sd->bLength < 2) {
		buf[0] = '\0';
		return;
	}

	for (i = 0; i < buflen - 1 && i < (sd->bLength / 2) - 1; i++)
		buf[i] = UGETW(sd->bString[i]);

	buf[i] = '\0';
}

/*------------------------------------------------------------------------*
 *	usb_temp_sysctl
 *
 * Callback for SYSCTL_PROC(9), to set and retrieve template string
 * descriptors.
 *------------------------------------------------------------------------*/
int
usb_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	char buf[128];
	struct usb_string_descriptor *sd = arg1;
	size_t len, sdlen = arg2;
	int error;

	usb_decode_str_desc(sd, buf, sizeof(buf));

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	len = usb_make_str_desc(sd, sdlen, buf);
	if (len == 0)
		return (EINVAL);

	return (0);
}


/*------------------------------------------------------------------------*
 *	usb_make_raw_desc
 *
 * This function will insert a raw USB descriptor into the generated
 * USB configuration.
 *------------------------------------------------------------------------*/
static void
usb_make_raw_desc(struct usb_temp_setup *temp,
    const uint8_t *raw)
{
	void *dst;
	uint8_t len;

	/*
         * The first byte of any USB descriptor gives the length.
         */
	if (raw) {
		len = raw[0];
		if (temp->buf) {
			dst = USB_ADD_BYTES(temp->buf, temp->size);
			memcpy(dst, raw, len);

			/* check if we have got a CDC union descriptor */

			if ((raw[0] == sizeof(struct usb_cdc_union_descriptor)) &&
			    (raw[1] == UDESC_CS_INTERFACE) &&
			    (raw[2] == UDESCSUB_CDC_UNION)) {
				struct usb_cdc_union_descriptor *ud = (void *)dst;

				/* update the interface numbers */

				ud->bMasterInterface +=
				    temp->bInterfaceNumber;
				ud->bSlaveInterface[0] +=
				    temp->bInterfaceNumber;
			}

			/* check if we have got an interface association descriptor */

			if ((raw[0] == sizeof(struct usb_interface_assoc_descriptor)) &&
			    (raw[1] == UDESC_IFACE_ASSOC)) {
				struct usb_interface_assoc_descriptor *iad = (void *)dst;

				/* update the interface number */

				iad->bFirstInterface +=
				    temp->bInterfaceNumber;
			}

			/* check if we have got a call management descriptor */

			if ((raw[0] == sizeof(struct usb_cdc_cm_descriptor)) &&
			    (raw[1] == UDESC_CS_INTERFACE) &&
			    (raw[2] == UDESCSUB_CDC_CM)) {
				struct usb_cdc_cm_descriptor *ccd = (void *)dst;

				/* update the interface number */

				ccd->bDataInterface +=
				    temp->bInterfaceNumber;
			}
		}
		temp->size += len;
	}
}

/*------------------------------------------------------------------------*
 *	usb_make_endpoint_desc
 *
 * This function will generate an USB endpoint descriptor from the
 * given USB template endpoint descriptor, which will be inserted into
 * the USB configuration.
 *------------------------------------------------------------------------*/
static void
usb_make_endpoint_desc(struct usb_temp_setup *temp,
    const struct usb_temp_endpoint_desc *ted)
{
	struct usb_endpoint_descriptor *ed;
	const void **rd;
	uint16_t old_size;
	uint16_t mps;
	uint8_t ea;			/* Endpoint Address */
	uint8_t et;			/* Endpiont Type */

	/* Reserve memory */
	old_size = temp->size;

	ea = (ted->bEndpointAddress & (UE_ADDR | UE_DIR_IN | UE_DIR_OUT));
	et = (ted->bmAttributes & UE_XFERTYPE);

	if (et == UE_ISOCHRONOUS) {
		/* account for extra byte fields */
		temp->size += sizeof(*ed) + 2;
	} else {
		temp->size += sizeof(*ed);
	}

	/* Scan all Raw Descriptors first */
	rd = ted->ppRawDesc;
	if (rd) {
		while (*rd) {
			usb_make_raw_desc(temp, *rd);
			rd++;
		}
	}
	if (ted->pPacketSize == NULL) {
		/* not initialized */
		temp->err = USB_ERR_INVAL;
		return;
	}
	mps = ted->pPacketSize->mps[temp->usb_speed];
	if (mps == 0) {
		/* not initialized */
		temp->err = USB_ERR_INVAL;
		return;
	} else if (mps == UE_ZERO_MPS) {
		/* escape for Zero Max Packet Size */
		mps = 0;
	}

	/*
	 * Fill out the real USB endpoint descriptor
	 * in case there is a buffer present:
	 */
	if (temp->buf) {
		ed = USB_ADD_BYTES(temp->buf, old_size);
		if (et == UE_ISOCHRONOUS)
			ed->bLength = sizeof(*ed) + 2;
		else
			ed->bLength = sizeof(*ed);
		ed->bDescriptorType = UDESC_ENDPOINT;
		ed->bEndpointAddress = ea;
		ed->bmAttributes = ted->bmAttributes;
		USETW(ed->wMaxPacketSize, mps);

		/* setup bInterval parameter */

		if (ted->pIntervals &&
		    ted->pIntervals->bInterval[temp->usb_speed]) {
			ed->bInterval =
			    ted->pIntervals->bInterval[temp->usb_speed];
		} else {
			switch (et) {
			case UE_BULK:
			case UE_CONTROL:
				ed->bInterval = 0;	/* not used */
				break;
			case UE_INTERRUPT:
				switch (temp->usb_speed) {
				case USB_SPEED_LOW:
				case USB_SPEED_FULL:
					ed->bInterval = 1;	/* 1 ms */
					break;
				default:
					ed->bInterval = 4;	/* 1 ms */
					break;
				}
				break;
			default:	/* UE_ISOCHRONOUS */
				switch (temp->usb_speed) {
				case USB_SPEED_LOW:
				case USB_SPEED_FULL:
					ed->bInterval = 1;	/* 1 ms */
					break;
				default:
					ed->bInterval = 1;	/* 125 us */
					break;
				}
				break;
			}
		}
	}
	temp->bNumEndpoints++;
}

/*------------------------------------------------------------------------*
 *	usb_make_interface_desc
 *
 * This function will generate an USB interface descriptor from the
 * given USB template interface descriptor, which will be inserted
 * into the USB configuration.
 *------------------------------------------------------------------------*/
static void
usb_make_interface_desc(struct usb_temp_setup *temp,
    const struct usb_temp_interface_desc *tid)
{
	struct usb_interface_descriptor *id;
	const struct usb_temp_endpoint_desc **ted;
	const void **rd;
	uint16_t old_size;

	/* Reserve memory */

	old_size = temp->size;
	temp->size += sizeof(*id);

	/* Update interface and alternate interface numbers */

	if (tid->isAltInterface == 0) {
		temp->bAlternateSetting = 0;
		temp->bInterfaceNumber++;
	} else {
		temp->bAlternateSetting++;
	}

	/* Scan all Raw Descriptors first */

	rd = tid->ppRawDesc;

	if (rd) {
		while (*rd) {
			usb_make_raw_desc(temp, *rd);
			rd++;
		}
	}
	/* Reset some counters */

	temp->bNumEndpoints = 0;

	/* Scan all Endpoint Descriptors second */

	ted = tid->ppEndpoints;
	if (ted) {
		while (*ted) {
			usb_make_endpoint_desc(temp, *ted);
			ted++;
		}
	}
	/*
	 * Fill out the real USB interface descriptor
	 * in case there is a buffer present:
	 */
	if (temp->buf) {
		id = USB_ADD_BYTES(temp->buf, old_size);
		id->bLength = sizeof(*id);
		id->bDescriptorType = UDESC_INTERFACE;
		id->bInterfaceNumber = temp->bInterfaceNumber;
		id->bAlternateSetting = temp->bAlternateSetting;
		id->bNumEndpoints = temp->bNumEndpoints;
		id->bInterfaceClass = tid->bInterfaceClass;
		id->bInterfaceSubClass = tid->bInterfaceSubClass;
		id->bInterfaceProtocol = tid->bInterfaceProtocol;
		id->iInterface = tid->iInterface;
	}
}

/*------------------------------------------------------------------------*
 *	usb_make_config_desc
 *
 * This function will generate an USB config descriptor from the given
 * USB template config descriptor, which will be inserted into the USB
 * configuration.
 *------------------------------------------------------------------------*/
static void
usb_make_config_desc(struct usb_temp_setup *temp,
    const struct usb_temp_config_desc *tcd)
{
	struct usb_config_descriptor *cd;
	const struct usb_temp_interface_desc **tid;
	uint16_t old_size;
	int power;

	/* Reserve memory */

	old_size = temp->size;
	temp->size += sizeof(*cd);

	/* Reset some counters */

	temp->bInterfaceNumber = 0xFF;
	temp->bAlternateSetting = 0;

	/* Scan all the USB interfaces */

	tid = tcd->ppIfaceDesc;
	if (tid) {
		while (*tid) {
			usb_make_interface_desc(temp, *tid);
			tid++;
		}
	}
	/*
	 * Fill out the real USB config descriptor
	 * in case there is a buffer present:
	 */
	if (temp->buf) {
		cd = USB_ADD_BYTES(temp->buf, old_size);

		/* compute total size */
		old_size = temp->size - old_size;

		cd->bLength = sizeof(*cd);
		cd->bDescriptorType = UDESC_CONFIG;
		USETW(cd->wTotalLength, old_size);
		cd->bNumInterface = temp->bInterfaceNumber + 1;
		cd->bConfigurationValue = temp->bConfigurationValue;
		cd->iConfiguration = tcd->iConfiguration;
		cd->bmAttributes = tcd->bmAttributes;

		power = usb_template_power;
		cd->bMaxPower = power / 2; /* 2 mA units */
		cd->bmAttributes |= UC_REMOTE_WAKEUP;
		if (power > 0) {
			cd->bmAttributes |= UC_BUS_POWERED;
			cd->bmAttributes &= ~UC_SELF_POWERED;
		} else {
			cd->bmAttributes &= ~UC_BUS_POWERED;
			cd->bmAttributes |= UC_SELF_POWERED;
		}
	}
}

/*------------------------------------------------------------------------*
 *	usb_make_device_desc
 *
 * This function will generate an USB device descriptor from the
 * given USB template device descriptor.
 *------------------------------------------------------------------------*/
static void
usb_make_device_desc(struct usb_temp_setup *temp,
    const struct usb_temp_device_desc *tdd)
{
	struct usb_temp_data *utd;
	const struct usb_temp_config_desc **tcd;
	uint16_t old_size;

	/* Reserve memory */

	old_size = temp->size;
	temp->size += sizeof(*utd);

	/* Scan all the USB configs */

	temp->bConfigurationValue = 1;
	tcd = tdd->ppConfigDesc;
	if (tcd) {
		while (*tcd) {
			usb_make_config_desc(temp, *tcd);
			temp->bConfigurationValue++;
			tcd++;
		}
	}
	/*
	 * Fill out the real USB device descriptor
	 * in case there is a buffer present:
	 */

	if (temp->buf) {
		utd = USB_ADD_BYTES(temp->buf, old_size);

		/* Store a pointer to our template device descriptor */
		utd->tdd = tdd;

		/* Fill out USB device descriptor */
		utd->udd.bLength = sizeof(utd->udd);
		utd->udd.bDescriptorType = UDESC_DEVICE;
		utd->udd.bDeviceClass = tdd->bDeviceClass;
		utd->udd.bDeviceSubClass = tdd->bDeviceSubClass;
		utd->udd.bDeviceProtocol = tdd->bDeviceProtocol;
		USETW(utd->udd.idVendor, tdd->idVendor);
		USETW(utd->udd.idProduct, tdd->idProduct);
		USETW(utd->udd.bcdDevice, tdd->bcdDevice);
		utd->udd.iManufacturer = tdd->iManufacturer;
		utd->udd.iProduct = tdd->iProduct;
		utd->udd.iSerialNumber = tdd->iSerialNumber;
		utd->udd.bNumConfigurations = temp->bConfigurationValue - 1;

		/*
		 * Fill out the USB device qualifier. Pretend that we
		 * don't support any other speeds by setting
		 * "bNumConfigurations" equal to zero. That saves us
		 * generating an extra set of configuration
		 * descriptors.
		 */
		utd->udq.bLength = sizeof(utd->udq);
		utd->udq.bDescriptorType = UDESC_DEVICE_QUALIFIER;
		utd->udq.bDeviceClass = tdd->bDeviceClass;
		utd->udq.bDeviceSubClass = tdd->bDeviceSubClass;
		utd->udq.bDeviceProtocol = tdd->bDeviceProtocol;
		utd->udq.bNumConfigurations = 0;
		USETW(utd->udq.bcdUSB, 0x0200);
		utd->udq.bMaxPacketSize0 = 0;

		switch (temp->usb_speed) {
		case USB_SPEED_LOW:
			USETW(utd->udd.bcdUSB, 0x0110);
			utd->udd.bMaxPacketSize = 8;
			break;
		case USB_SPEED_FULL:
			USETW(utd->udd.bcdUSB, 0x0110);
			utd->udd.bMaxPacketSize = 32;
			break;
		case USB_SPEED_HIGH:
			USETW(utd->udd.bcdUSB, 0x0200);
			utd->udd.bMaxPacketSize = 64;
			break;
		case USB_SPEED_VARIABLE:
			USETW(utd->udd.bcdUSB, 0x0250);
			utd->udd.bMaxPacketSize = 255;	/* 512 bytes */
			break;
		case USB_SPEED_SUPER:
			USETW(utd->udd.bcdUSB, 0x0300);
			utd->udd.bMaxPacketSize = 9;	/* 2**9 = 512 bytes */
			break;
		default:
			temp->err = USB_ERR_INVAL;
			break;
		}
	}
}

/*------------------------------------------------------------------------*
 *	usb_hw_ep_match
 *
 * Return values:
 *    0: The endpoint profile does not match the criteria
 * Else: The endpoint profile matches the criteria
 *------------------------------------------------------------------------*/
static uint8_t
usb_hw_ep_match(const struct usb_hw_ep_profile *pf,
    uint8_t ep_type, uint8_t ep_dir_in)
{
	if (ep_type == UE_CONTROL) {
		/* special */
		return (pf->support_control);
	}
	if ((pf->support_in && ep_dir_in) ||
	    (pf->support_out && !ep_dir_in)) {
		if ((pf->support_interrupt && (ep_type == UE_INTERRUPT)) ||
		    (pf->support_isochronous && (ep_type == UE_ISOCHRONOUS)) ||
		    (pf->support_bulk && (ep_type == UE_BULK))) {
			return (1);
		}
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_hw_ep_find_match
 *
 * This function is used to find the best matching endpoint profile
 * for and endpoint belonging to an USB descriptor.
 *
 * Return values:
 *    0: Success. Got a match.
 * Else: Failure. No match.
 *------------------------------------------------------------------------*/
static uint8_t
usb_hw_ep_find_match(struct usb_hw_ep_scratch *ues,
    struct usb_hw_ep_scratch_sub *ep, uint8_t is_simplex)
{
	const struct usb_hw_ep_profile *pf;
	uint16_t distance;
	uint16_t temp;
	uint16_t max_frame_size;
	uint8_t n;
	uint8_t best_n;
	uint8_t dir_in;
	uint8_t dir_out;

	distance = 0xFFFF;
	best_n = 0;

	if ((!ep->needs_in) && (!ep->needs_out)) {
		return (0);		/* we are done */
	}
	if (ep->needs_ep_type == UE_CONTROL) {
		dir_in = 1;
		dir_out = 1;
	} else {
		if (ep->needs_in) {
			dir_in = 1;
			dir_out = 0;
		} else {
			dir_in = 0;
			dir_out = 1;
		}
	}

	for (n = 1; n != (USB_EP_MAX / 2); n++) {

		/* get HW endpoint profile */
		(ues->methods->get_hw_ep_profile) (ues->udev, &pf, n);
		if (pf == NULL) {
			/* end of profiles */
			break;
		}
		/* check if IN-endpoint is reserved */
		if (dir_in || pf->is_simplex) {
			if (ues->bmInAlloc[n / 8] & (1 << (n % 8))) {
				/* mismatch */
				continue;
			}
		}
		/* check if OUT-endpoint is reserved */
		if (dir_out || pf->is_simplex) {
			if (ues->bmOutAlloc[n / 8] & (1 << (n % 8))) {
				/* mismatch */
				continue;
			}
		}
		/* check simplex */
		if (pf->is_simplex == is_simplex) {
			/* mismatch */
			continue;
		}
		/* check if HW endpoint matches */
		if (!usb_hw_ep_match(pf, ep->needs_ep_type, dir_in)) {
			/* mismatch */
			continue;
		}
		/* get maximum frame size */
		if (dir_in)
			max_frame_size = pf->max_in_frame_size;
		else
			max_frame_size = pf->max_out_frame_size;

		/* check if we have a matching profile */
		if (max_frame_size >= ep->max_frame_size) {
			temp = (max_frame_size - ep->max_frame_size);
			if (distance > temp) {
				distance = temp;
				best_n = n;
				ep->pf = pf;
			}
		}
	}

	/* see if we got a match */
	if (best_n != 0) {
		/* get the correct profile */
		pf = ep->pf;

		/* reserve IN-endpoint */
		if (dir_in) {
			ues->bmInAlloc[best_n / 8] |=
			    (1 << (best_n % 8));
			ep->hw_endpoint_in = best_n | UE_DIR_IN;
			ep->needs_in = 0;
		}
		/* reserve OUT-endpoint */
		if (dir_out) {
			ues->bmOutAlloc[best_n / 8] |=
			    (1 << (best_n % 8));
			ep->hw_endpoint_out = best_n | UE_DIR_OUT;
			ep->needs_out = 0;
		}
		return (0);		/* got a match */
	}
	return (1);			/* failure */
}

/*------------------------------------------------------------------------*
 *	usb_hw_ep_get_needs
 *
 * This function will figure out the type and number of endpoints
 * which are needed for an USB configuration.
 *
 * Return values:
 *    0: Success.
 * Else: Failure.
 *------------------------------------------------------------------------*/
static uint8_t
usb_hw_ep_get_needs(struct usb_hw_ep_scratch *ues,
    uint8_t ep_type, uint8_t is_complete)
{
	const struct usb_hw_ep_profile *pf;
	struct usb_hw_ep_scratch_sub *ep_iface;
	struct usb_hw_ep_scratch_sub *ep_curr;
	struct usb_hw_ep_scratch_sub *ep_max;
	struct usb_hw_ep_scratch_sub *ep_end;
	struct usb_descriptor *desc;
	struct usb_interface_descriptor *id;
	struct usb_endpoint_descriptor *ed;
	enum usb_dev_speed speed;
	uint16_t wMaxPacketSize;
	uint16_t temp;
	uint8_t ep_no;

	ep_iface = ues->ep_max;
	ep_curr = ues->ep_max;
	ep_end = ues->ep + USB_EP_MAX;
	ep_max = ues->ep_max;
	desc = NULL;
	speed = usbd_get_speed(ues->udev);

repeat:

	while ((desc = usb_desc_foreach(ues->cd, desc))) {

		if ((desc->bDescriptorType == UDESC_INTERFACE) &&
		    (desc->bLength >= sizeof(*id))) {

			id = (void *)desc;

			if (id->bAlternateSetting == 0) {
				/* going forward */
				ep_iface = ep_max;
			} else {
				/* reset */
				ep_curr = ep_iface;
			}
		}
		if ((desc->bDescriptorType == UDESC_ENDPOINT) &&
		    (desc->bLength >= sizeof(*ed))) {

			ed = (void *)desc;

			goto handle_endpoint_desc;
		}
	}
	ues->ep_max = ep_max;
	return (0);

handle_endpoint_desc:
	temp = (ed->bmAttributes & UE_XFERTYPE);

	if (temp == ep_type) {

		if (ep_curr == ep_end) {
			/* too many endpoints */
			return (1);	/* failure */
		}
		wMaxPacketSize = UGETW(ed->wMaxPacketSize);
		if ((wMaxPacketSize & 0xF800) &&
		    (speed == USB_SPEED_HIGH)) {
			/* handle packet multiplier */
			temp = (wMaxPacketSize >> 11) & 3;
			wMaxPacketSize &= 0x7FF;
			if (temp == 1) {
				wMaxPacketSize *= 2;
			} else {
				wMaxPacketSize *= 3;
			}
		}
		/*
		 * Check if we have a fixed endpoint number, else the
		 * endpoint number is allocated dynamically:
		 */
		ep_no = (ed->bEndpointAddress & UE_ADDR);
		if (ep_no != 0) {

			/* get HW endpoint profile */
			(ues->methods->get_hw_ep_profile)
			    (ues->udev, &pf, ep_no);
			if (pf == NULL) {
				/* HW profile does not exist - failure */
				DPRINTFN(0, "Endpoint profile %u "
				    "does not exist\n", ep_no);
				return (1);
			}
			/* reserve fixed endpoint number */
			if (ep_type == UE_CONTROL) {
				ues->bmInAlloc[ep_no / 8] |=
				    (1 << (ep_no % 8));
				ues->bmOutAlloc[ep_no / 8] |=
				    (1 << (ep_no % 8));
				if ((pf->max_in_frame_size < wMaxPacketSize) ||
				    (pf->max_out_frame_size < wMaxPacketSize)) {
					DPRINTFN(0, "Endpoint profile %u "
					    "has too small buffer\n", ep_no);
					return (1);
				}
			} else if (ed->bEndpointAddress & UE_DIR_IN) {
				ues->bmInAlloc[ep_no / 8] |=
				    (1 << (ep_no % 8));
				if (pf->max_in_frame_size < wMaxPacketSize) {
					DPRINTFN(0, "Endpoint profile %u "
					    "has too small buffer\n", ep_no);
					return (1);
				}
			} else {
				ues->bmOutAlloc[ep_no / 8] |=
				    (1 << (ep_no % 8));
				if (pf->max_out_frame_size < wMaxPacketSize) {
					DPRINTFN(0, "Endpoint profile %u "
					    "has too small buffer\n", ep_no);
					return (1);
				}
			}
		} else if (is_complete) {

			/* check if we have enough buffer space */
			if (wMaxPacketSize >
			    ep_curr->max_frame_size) {
				return (1);	/* failure */
			}
			if (ed->bEndpointAddress & UE_DIR_IN) {
				ed->bEndpointAddress =
				    ep_curr->hw_endpoint_in;
			} else {
				ed->bEndpointAddress =
				    ep_curr->hw_endpoint_out;
			}

		} else {

			/* compute the maximum frame size */
			if (ep_curr->max_frame_size < wMaxPacketSize) {
				ep_curr->max_frame_size = wMaxPacketSize;
			}
			if (temp == UE_CONTROL) {
				ep_curr->needs_in = 1;
				ep_curr->needs_out = 1;
			} else {
				if (ed->bEndpointAddress & UE_DIR_IN) {
					ep_curr->needs_in = 1;
				} else {
					ep_curr->needs_out = 1;
				}
			}
			ep_curr->needs_ep_type = ep_type;
		}

		ep_curr++;
		if (ep_max < ep_curr) {
			ep_max = ep_curr;
		}
	}
	goto repeat;
}

/*------------------------------------------------------------------------*
 *	usb_hw_ep_resolve
 *
 * This function will try to resolve endpoint requirements by the
 * given endpoint profiles that the USB hardware reports.
 *
 * Return values:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_hw_ep_resolve(struct usb_device *udev,
    struct usb_descriptor *desc)
{
	struct usb_hw_ep_scratch *ues;
	struct usb_hw_ep_scratch_sub *ep;
	const struct usb_hw_ep_profile *pf;
	const struct usb_bus_methods *methods;
	struct usb_device_descriptor *dd;
	uint16_t mps;

	if (desc == NULL)
		return (USB_ERR_INVAL);

	/* get bus methods */
	methods = udev->bus->methods;

	if (methods->get_hw_ep_profile == NULL)
		return (USB_ERR_INVAL);

	if (desc->bDescriptorType == UDESC_DEVICE) {

		if (desc->bLength < sizeof(*dd))
			return (USB_ERR_INVAL);

		dd = (void *)desc;

		/* get HW control endpoint 0 profile */
		(methods->get_hw_ep_profile) (udev, &pf, 0);
		if (pf == NULL) {
			return (USB_ERR_INVAL);
		}
		if (!usb_hw_ep_match(pf, UE_CONTROL, 0)) {
			DPRINTFN(0, "Endpoint 0 does not "
			    "support control\n");
			return (USB_ERR_INVAL);
		}
		mps = dd->bMaxPacketSize;

		if (udev->speed == USB_SPEED_FULL) {
			/*
			 * We can optionally choose another packet size !
			 */
			while (1) {
				/* check if "mps" is ok */
				if (pf->max_in_frame_size >= mps) {
					break;
				}
				/* reduce maximum packet size */
				mps /= 2;

				/* check if "mps" is too small */
				if (mps < 8) {
					return (USB_ERR_INVAL);
				}
			}

			dd->bMaxPacketSize = mps;

		} else {
			/* We only have one choice */
			if (mps == 255) {
				mps = 512;
			}
			/* Check if we support the specified wMaxPacketSize */
			if (pf->max_in_frame_size < mps) {
				return (USB_ERR_INVAL);
			}
		}
		return (0);		/* success */
	}
	if (desc->bDescriptorType != UDESC_CONFIG)
		return (USB_ERR_INVAL);
	if (desc->bLength < sizeof(*(ues->cd)))
		return (USB_ERR_INVAL);

	ues = udev->scratch.hw_ep_scratch;

	memset(ues, 0, sizeof(*ues));

	ues->ep_max = ues->ep;
	ues->cd = (void *)desc;
	ues->methods = methods;
	ues->udev = udev;

	/* Get all the endpoints we need */

	if (usb_hw_ep_get_needs(ues, UE_ISOCHRONOUS, 0) ||
	    usb_hw_ep_get_needs(ues, UE_INTERRUPT, 0) ||
	    usb_hw_ep_get_needs(ues, UE_CONTROL, 0) ||
	    usb_hw_ep_get_needs(ues, UE_BULK, 0)) {
		DPRINTFN(0, "Could not get needs\n");
		return (USB_ERR_INVAL);
	}
	for (ep = ues->ep; ep != ues->ep_max; ep++) {

		while (ep->needs_in || ep->needs_out) {

			/*
		         * First try to use a simplex endpoint.
		         * Then try to use a duplex endpoint.
		         */
			if (usb_hw_ep_find_match(ues, ep, 1) &&
			    usb_hw_ep_find_match(ues, ep, 0)) {
				DPRINTFN(0, "Could not find match\n");
				return (USB_ERR_INVAL);
			}
		}
	}

	ues->ep_max = ues->ep;

	/* Update all endpoint addresses */

	if (usb_hw_ep_get_needs(ues, UE_ISOCHRONOUS, 1) ||
	    usb_hw_ep_get_needs(ues, UE_INTERRUPT, 1) ||
	    usb_hw_ep_get_needs(ues, UE_CONTROL, 1) ||
	    usb_hw_ep_get_needs(ues, UE_BULK, 1)) {
		DPRINTFN(0, "Could not update endpoint address\n");
		return (USB_ERR_INVAL);
	}
	return (0);			/* success */
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_tdd
 *
 * Returns:
 *  NULL: No USB template device descriptor found.
 *  Else: Pointer to the USB template device descriptor.
 *------------------------------------------------------------------------*/
static const struct usb_temp_device_desc *
usb_temp_get_tdd(struct usb_device *udev)
{
	if (udev->usb_template_ptr == NULL) {
		return (NULL);
	}
	return (udev->usb_template_ptr->tdd);
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_device_desc
 *
 * Returns:
 *  NULL: No USB device descriptor found.
 *  Else: Pointer to USB device descriptor.
 *------------------------------------------------------------------------*/
static void *
usb_temp_get_device_desc(struct usb_device *udev)
{
	struct usb_device_descriptor *dd;

	if (udev->usb_template_ptr == NULL) {
		return (NULL);
	}
	dd = &udev->usb_template_ptr->udd;
	if (dd->bDescriptorType != UDESC_DEVICE) {
		/* sanity check failed */
		return (NULL);
	}
	return (dd);
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_qualifier_desc
 *
 * Returns:
 *  NULL: No USB device_qualifier descriptor found.
 *  Else: Pointer to USB device_qualifier descriptor.
 *------------------------------------------------------------------------*/
static void *
usb_temp_get_qualifier_desc(struct usb_device *udev)
{
	struct usb_device_qualifier *dq;

	if (udev->usb_template_ptr == NULL) {
		return (NULL);
	}
	dq = &udev->usb_template_ptr->udq;
	if (dq->bDescriptorType != UDESC_DEVICE_QUALIFIER) {
		/* sanity check failed */
		return (NULL);
	}
	return (dq);
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_config_desc
 *
 * Returns:
 *  NULL: No USB config descriptor found.
 *  Else: Pointer to USB config descriptor having index "index".
 *------------------------------------------------------------------------*/
static void *
usb_temp_get_config_desc(struct usb_device *udev,
    uint16_t *pLength, uint8_t index)
{
	struct usb_device_descriptor *dd;
	struct usb_config_descriptor *cd;
	uint16_t temp;

	if (udev->usb_template_ptr == NULL) {
		return (NULL);
	}
	dd = &udev->usb_template_ptr->udd;
	cd = (void *)(udev->usb_template_ptr + 1);

	if (index >= dd->bNumConfigurations) {
		/* out of range */
		return (NULL);
	}
	while (index--) {
		if (cd->bDescriptorType != UDESC_CONFIG) {
			/* sanity check failed */
			return (NULL);
		}
		temp = UGETW(cd->wTotalLength);
		cd = USB_ADD_BYTES(cd, temp);
	}

	if (pLength) {
		*pLength = UGETW(cd->wTotalLength);
	}
	return (cd);
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_vendor_desc
 *
 * Returns:
 *  NULL: No vendor descriptor found.
 *  Else: Pointer to a vendor descriptor.
 *------------------------------------------------------------------------*/
static const void *
usb_temp_get_vendor_desc(struct usb_device *udev,
    const struct usb_device_request *req, uint16_t *plen)
{
	const struct usb_temp_device_desc *tdd;

	tdd = usb_temp_get_tdd(udev);
	if (tdd == NULL) {
		return (NULL);
	}
	if (tdd->getVendorDesc == NULL) {
		return (NULL);
	}
	return ((tdd->getVendorDesc) (req, plen));
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_string_desc
 *
 * Returns:
 *  NULL: No string descriptor found.
 *  Else: Pointer to a string descriptor.
 *------------------------------------------------------------------------*/
static const void *
usb_temp_get_string_desc(struct usb_device *udev,
    uint16_t lang_id, uint8_t string_index)
{
	const struct usb_temp_device_desc *tdd;

	tdd = usb_temp_get_tdd(udev);
	if (tdd == NULL) {
		return (NULL);
	}
	if (tdd->getStringDesc == NULL) {
		return (NULL);
	}
	return ((tdd->getStringDesc) (lang_id, string_index));
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_hub_desc
 *
 * Returns:
 *  NULL: No USB HUB descriptor found.
 *  Else: Pointer to a USB HUB descriptor.
 *------------------------------------------------------------------------*/
static const void *
usb_temp_get_hub_desc(struct usb_device *udev)
{
	return (NULL);			/* needs to be implemented */
}

/*------------------------------------------------------------------------*
 *	usb_temp_get_desc
 *
 * This function is a demultiplexer for local USB device side control
 * endpoint requests.
 *------------------------------------------------------------------------*/
static usb_error_t
usb_temp_get_desc(struct usb_device *udev, struct usb_device_request *req,
    const void **pPtr, uint16_t *pLength)
{
	const uint8_t *buf;
	uint16_t len;

	buf = NULL;
	len = 0;

	switch (req->bmRequestType) {
	case UT_READ_DEVICE:
		switch (req->bRequest) {
		case UR_GET_DESCRIPTOR:
			goto tr_handle_get_descriptor;
		default:
			goto tr_stalled;
		}
	case UT_READ_CLASS_DEVICE:
		switch (req->bRequest) {
		case UR_GET_DESCRIPTOR:
			goto tr_handle_get_class_descriptor;
		default:
			goto tr_stalled;
		}
	default:
		goto tr_stalled;
	}

tr_handle_get_descriptor:
	switch (req->wValue[1]) {
	case UDESC_DEVICE:
		if (req->wValue[0]) {
			goto tr_stalled;
		}
		buf = usb_temp_get_device_desc(udev);
		goto tr_valid;
	case UDESC_DEVICE_QUALIFIER:
		if (udev->speed != USB_SPEED_HIGH) {
			goto tr_stalled;
		}
		if (req->wValue[0]) {
			goto tr_stalled;
		}
		buf = usb_temp_get_qualifier_desc(udev);
		goto tr_valid;
	case UDESC_OTHER_SPEED_CONFIGURATION:
		if (udev->speed != USB_SPEED_HIGH) {
			goto tr_stalled;
		}
	case UDESC_CONFIG:
		buf = usb_temp_get_config_desc(udev,
		    &len, req->wValue[0]);
		goto tr_valid;
	case UDESC_STRING:
		buf = usb_temp_get_string_desc(udev,
		    UGETW(req->wIndex), req->wValue[0]);
		goto tr_valid;
	default:
		goto tr_stalled;
	}

tr_handle_get_class_descriptor:
	if (req->wValue[0]) {
		goto tr_stalled;
	}
	buf = usb_temp_get_hub_desc(udev);
	goto tr_valid;

tr_valid:
	if (buf == NULL)
		goto tr_stalled;
	if (len == 0)
		len = buf[0];
	*pPtr = buf;
	*pLength = len;
	return (0);	/* success */

tr_stalled:
	/* try to get a vendor specific descriptor */
	len = 0;
	buf = usb_temp_get_vendor_desc(udev, req, &len);
	if (buf != NULL)
		goto tr_valid;
	*pPtr = NULL;
	*pLength = 0;
	return (0);	/* we ignore failures */
}

/*------------------------------------------------------------------------*
 *	usb_temp_setup
 *
 * This function generates USB descriptors according to the given USB
 * template device descriptor. It will also try to figure out the best
 * matching endpoint addresses using the hardware endpoint profiles.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usb_temp_setup(struct usb_device *udev,
    const struct usb_temp_device_desc *tdd)
{
	struct usb_temp_setup *uts;
	void *buf;
	usb_error_t error;
	uint8_t n;
	uint8_t do_unlock;

	/* be NULL safe */
	if (tdd == NULL)
		return (0);

	/* Protect scratch area */
	do_unlock = usbd_ctrl_lock(udev);

	uts = udev->scratch.temp_setup;

	memset(uts, 0, sizeof(*uts));

	uts->usb_speed = udev->speed;
	uts->self_powered = udev->flags.self_powered;

	/* first pass */

	usb_make_device_desc(uts, tdd);

	if (uts->err) {
		/* some error happened */
		goto done;
	}
	/* sanity check */
	if (uts->size == 0) {
		uts->err = USB_ERR_INVAL;
		goto done;
	}
	/* allocate zeroed memory */
	uts->buf = usbd_alloc_config_desc(udev, uts->size);
	/*
	 * Allow malloc() to return NULL regardless of M_WAITOK flag.
	 * This helps when porting the software to non-FreeBSD
	 * systems.
	 */
	if (uts->buf == NULL) {
		/* could not allocate memory */
		uts->err = USB_ERR_NOMEM;
		goto done;
	}
	/* second pass */

	uts->size = 0;

	usb_make_device_desc(uts, tdd);

	/*
	 * Store a pointer to our descriptors:
	 */
	udev->usb_template_ptr = uts->buf;

	if (uts->err) {
		/* some error happened during second pass */
		goto done;
	}
	/*
	 * Resolve all endpoint addresses !
	 */
	buf = usb_temp_get_device_desc(udev);
	uts->err = usb_hw_ep_resolve(udev, buf);
	if (uts->err) {
		DPRINTFN(0, "Could not resolve endpoints for "
		    "Device Descriptor, error = %s\n",
		    usbd_errstr(uts->err));
		goto done;
	}
	for (n = 0;; n++) {

		buf = usb_temp_get_config_desc(udev, NULL, n);
		if (buf == NULL) {
			break;
		}
		uts->err = usb_hw_ep_resolve(udev, buf);
		if (uts->err) {
			DPRINTFN(0, "Could not resolve endpoints for "
			    "Config Descriptor %u, error = %s\n", n,
			    usbd_errstr(uts->err));
			goto done;
		}
	}
done:
	error = uts->err;
	if (error)
		usb_temp_unsetup(udev);
	if (do_unlock)
		usbd_ctrl_unlock(udev);
	return (error);
}

/*------------------------------------------------------------------------*
 *	usb_temp_unsetup
 *
 * This function frees any memory associated with the currently
 * setup template, if any.
 *------------------------------------------------------------------------*/
void
usb_temp_unsetup(struct usb_device *udev)
{
	usbd_free_config_desc(udev, udev->usb_template_ptr);
	udev->usb_template_ptr = NULL;
}

static usb_error_t
usb_temp_setup_by_index(struct usb_device *udev, uint16_t index)
{
	usb_error_t err;

	switch (index) {
	case USB_TEMP_MSC:
		err = usb_temp_setup(udev, &usb_template_msc);
		break;
	case USB_TEMP_CDCE:
		err = usb_temp_setup(udev, &usb_template_cdce);
		break;
	case USB_TEMP_MTP:
		err = usb_temp_setup(udev, &usb_template_mtp);
		break;
	case USB_TEMP_MODEM:
		err = usb_temp_setup(udev, &usb_template_modem);
		break;
	case USB_TEMP_AUDIO:
		err = usb_temp_setup(udev, &usb_template_audio);
		break;
	case USB_TEMP_KBD:
		err = usb_temp_setup(udev, &usb_template_kbd);
		break;
	case USB_TEMP_MOUSE:
		err = usb_temp_setup(udev, &usb_template_mouse);
		break;
	case USB_TEMP_PHONE:
		err = usb_temp_setup(udev, &usb_template_phone);
		break;
	case USB_TEMP_SERIALNET:
		err = usb_temp_setup(udev, &usb_template_serialnet);
		break;
	case USB_TEMP_MIDI:
		err = usb_temp_setup(udev, &usb_template_midi);
		break;
	case USB_TEMP_MULTI:
		err = usb_temp_setup(udev, &usb_template_multi);
		break;
	default:
		return (USB_ERR_INVAL);
	}

	return (err);
}

static void
usb_temp_init(void *arg)
{
	/* register our functions */
	usb_temp_get_desc_p = &usb_temp_get_desc;
	usb_temp_setup_by_index_p = &usb_temp_setup_by_index;
	usb_temp_unsetup_p = &usb_temp_unsetup;
}

SYSINIT(usb_temp_init, SI_SUB_LOCK, SI_ORDER_FIRST, usb_temp_init, NULL);
SYSUNINIT(usb_temp_unload, SI_SUB_LOCK, SI_ORDER_ANY, usb_temp_unload, NULL);
