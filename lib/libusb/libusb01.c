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

/*
 * This file contains the emulation layer for LibUSB v0.1 from sourceforge.
 */

#ifdef LIBUSB_GLOBAL_INCLUDE_FILE
#include LIBUSB_GLOBAL_INCLUDE_FILE
#else
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/queue.h>
#endif

#include "libusb20.h"
#include "libusb20_desc.h"
#include "libusb20_int.h"
#include "usb.h"

/*
 * The two following macros were taken from the original LibUSB v0.1
 * for sake of compatibility:
 */
#define	LIST_ADD(begin, ent)	   \
  do {				   \
    if (begin) {		   \
      ent->next = begin;	   \
      ent->next->prev = ent;	   \
    } else {			   \
      ent->next = NULL;		   \
    }				   \
    ent->prev = NULL;		   \
    begin = ent;		   \
  } while(0)

#define	LIST_DEL(begin, ent)		 \
  do {					 \
    if (ent->prev) {			 \
      ent->prev->next = ent->next;	 \
    } else {				 \
      begin = ent->next;		 \
    }					 \
    if (ent->next) {			 \
      ent->next->prev = ent->prev;	 \
    }					 \
    ent->prev = NULL;			 \
    ent->next = NULL;			 \
  } while (0)

struct usb_bus *usb_busses = NULL;

static struct usb_bus usb_global_bus = {
	.dirname = {"/dev/usb"},
	.root_dev = NULL,
	.devices = NULL,
};

static struct libusb20_backend *usb_backend = NULL;

struct usb_parse_state {

	struct {
		struct libusb20_endpoint *currep;
		struct libusb20_interface *currifc;
		struct libusb20_config *currcfg;
		struct libusb20_me_struct *currextra;
	}	a;

	struct {
		struct usb_config_descriptor *currcfg;
		struct usb_interface_descriptor *currifc;
		struct usb_endpoint_descriptor *currep;
		struct usb_interface *currifcw;
		uint8_t *currextra;
	}	b;

	uint8_t	preparse;
};

static struct libusb20_transfer *
usb_get_transfer_by_ep_no(usb_dev_handle * dev, uint8_t ep_no)
{
	struct libusb20_device *pdev = (void *)dev;
	struct libusb20_transfer *xfer;
	int err;
	uint32_t bufsize;
	uint8_t x;
	uint8_t speed;

	x = (ep_no & LIBUSB20_ENDPOINT_ADDRESS_MASK) * 2;

	if (ep_no & LIBUSB20_ENDPOINT_DIR_MASK) {
		/* this is an IN endpoint */
		x |= 1;
	}
	speed = libusb20_dev_get_speed(pdev);

	/* select a sensible buffer size */
	if (speed == LIBUSB20_SPEED_LOW) {
		bufsize = 256;
	} else if (speed == LIBUSB20_SPEED_FULL) {
		bufsize = 4096;
	} else if (speed == LIBUSB20_SPEED_SUPER) {
		bufsize = 65536;
	} else {
		bufsize = 16384;
	}

	xfer = libusb20_tr_get_pointer(pdev, x);

	if (xfer == NULL)
		return (xfer);

	err = libusb20_tr_open(xfer, bufsize, 1, ep_no);
	if (err == LIBUSB20_ERROR_BUSY) {
		/* already opened */
		return (xfer);
	} else if (err) {
		return (NULL);
	}
	/* success */
	return (xfer);
}

usb_dev_handle *
usb_open(struct usb_device *dev)
{
	int err;

	err = libusb20_dev_open(dev->dev, 16 * 2);
	if (err == LIBUSB20_ERROR_BUSY) {
		/*
		 * Workaround buggy USB applications which open the USB
		 * device multiple times:
		 */
		return (dev->dev);
	}
	if (err)
		return (NULL);

	/*
	 * Dequeue USB device from backend queue so that it does not get
	 * freed when the backend is re-scanned:
	 */
	libusb20_be_dequeue_device(usb_backend, dev->dev);

	return (dev->dev);
}

int
usb_close(usb_dev_handle * udev)
{
	struct usb_device *dev;
	int err;

	err = libusb20_dev_close((void *)udev);

	if (err)
		return (-1);

	if (usb_backend != NULL) {
		/*
		 * Enqueue USB device to backend queue so that it gets freed
		 * when the backend is re-scanned:
		 */
		libusb20_be_enqueue_device(usb_backend, (void *)udev);
	} else {
		/*
		 * The backend is gone. Free device data so that we
		 * don't start leaking memory!
		 */
		dev = usb_device(udev);
		libusb20_dev_free((void *)udev);
		LIST_DEL(usb_global_bus.devices, dev);
		free(dev);
	}
	return (0);
}

int
usb_get_string(usb_dev_handle * dev, int strindex,
    int langid, char *buf, size_t buflen)
{
	int err;

	if (dev == NULL)
		return (-1);

	if (buflen > 65535)
		buflen = 65535;

	err = libusb20_dev_req_string_sync((void *)dev,
	    strindex, langid, buf, buflen);

	if (err)
		return (-1);

	return (0);
}

int
usb_get_string_simple(usb_dev_handle * dev, int strindex,
    char *buf, size_t buflen)
{
	int err;

	if (dev == NULL)
		return (-1);

	if (buflen > 65535)
		buflen = 65535;

	err = libusb20_dev_req_string_simple_sync((void *)dev,
	    strindex, buf, buflen);

	if (err)
		return (-1);

	return (strlen(buf));
}

int
usb_get_descriptor_by_endpoint(usb_dev_handle * udev, int ep, uint8_t type,
    uint8_t ep_index, void *buf, int size)
{
	memset(buf, 0, size);

	if (udev == NULL)
		return (-1);

	if (size > 65535)
		size = 65535;

	return (usb_control_msg(udev, ep | USB_ENDPOINT_IN,
	    USB_REQ_GET_DESCRIPTOR, (type << 8) + ep_index, 0,
	    buf, size, 1000));
}

int
usb_get_descriptor(usb_dev_handle * udev, uint8_t type, uint8_t desc_index,
    void *buf, int size)
{
	memset(buf, 0, size);

	if (udev == NULL)
		return (-1);

	if (size > 65535)
		size = 65535;

	return (usb_control_msg(udev, USB_ENDPOINT_IN, USB_REQ_GET_DESCRIPTOR,
	    (type << 8) + desc_index, 0, buf, size, 1000));
}

int
usb_parse_descriptor(uint8_t *source, char *description, void *dest)
{
	uint8_t *sp = source;
	uint8_t *dp = dest;
	uint16_t w;
	uint32_t d;
	char *cp;

	for (cp = description; *cp; cp++) {
		switch (*cp) {
		case 'b':		/* 8-bit byte */
			*dp++ = *sp++;
			break;
			/*
			 * 16-bit word, convert from little endian to CPU
			 */
		case 'w':
			w = (sp[1] << 8) | sp[0];
			sp += 2;
			/* Align to word boundary */
			dp += ((dp - (uint8_t *)0) & 1);
			*((uint16_t *)dp) = w;
			dp += 2;
			break;
			/*
			 * 32-bit dword, convert from little endian to CPU
			 */
		case 'd':
			d = (sp[3] << 24) | (sp[2] << 16) |
			    (sp[1] << 8) | sp[0];
			sp += 4;
			/* Align to word boundary */
			dp += ((dp - (uint8_t *)0) & 1);
			/* Align to double word boundary */
			dp += ((dp - (uint8_t *)0) & 2);
			*((uint32_t *)dp) = d;
			dp += 4;
			break;
		}
	}
	return (sp - source);
}

static void
usb_parse_extra(struct usb_parse_state *ps, uint8_t **pptr, int *plen)
{
	void *ptr;
	uint16_t len;

	ptr = ps->a.currextra->ptr;
	len = ps->a.currextra->len;

	if (ps->preparse == 0) {
		memcpy(ps->b.currextra, ptr, len);
		*pptr = ps->b.currextra;
		*plen = len;
	}
	ps->b.currextra += len;
	return;
}

static void
usb_parse_endpoint(struct usb_parse_state *ps)
{
	struct usb_endpoint_descriptor *bep;
	struct libusb20_endpoint *aep;

	aep = ps->a.currep;
	bep = ps->b.currep++;

	if (ps->preparse == 0) {
		/* copy descriptor fields */
		bep->bLength = aep->desc.bLength;
		bep->bDescriptorType = aep->desc.bDescriptorType;
		bep->bEndpointAddress = aep->desc.bEndpointAddress;
		bep->bmAttributes = aep->desc.bmAttributes;
		bep->wMaxPacketSize = aep->desc.wMaxPacketSize;
		bep->bInterval = aep->desc.bInterval;
		bep->bRefresh = aep->desc.bRefresh;
		bep->bSynchAddress = aep->desc.bSynchAddress;
	}
	ps->a.currextra = &aep->extra;
	usb_parse_extra(ps, &bep->extra, &bep->extralen);
	return;
}

static void
usb_parse_iface_sub(struct usb_parse_state *ps)
{
	struct libusb20_interface *aifc;
	struct usb_interface_descriptor *bifc;
	uint8_t x;

	aifc = ps->a.currifc;
	bifc = ps->b.currifc++;

	if (ps->preparse == 0) {
		/* copy descriptor fields */
		bifc->bLength = aifc->desc.bLength;
		bifc->bDescriptorType = aifc->desc.bDescriptorType;
		bifc->bInterfaceNumber = aifc->desc.bInterfaceNumber;
		bifc->bAlternateSetting = aifc->desc.bAlternateSetting;
		bifc->bNumEndpoints = aifc->num_endpoints;
		bifc->bInterfaceClass = aifc->desc.bInterfaceClass;
		bifc->bInterfaceSubClass = aifc->desc.bInterfaceSubClass;
		bifc->bInterfaceProtocol = aifc->desc.bInterfaceProtocol;
		bifc->iInterface = aifc->desc.iInterface;
		bifc->endpoint = ps->b.currep;
	}
	for (x = 0; x != aifc->num_endpoints; x++) {
		ps->a.currep = aifc->endpoints + x;
		usb_parse_endpoint(ps);
	}

	ps->a.currextra = &aifc->extra;
	usb_parse_extra(ps, &bifc->extra, &bifc->extralen);
	return;
}

static void
usb_parse_iface(struct usb_parse_state *ps)
{
	struct libusb20_interface *aifc;
	struct usb_interface *bifc;
	uint8_t x;

	aifc = ps->a.currifc;
	bifc = ps->b.currifcw++;

	if (ps->preparse == 0) {
		/* initialise interface wrapper */
		bifc->altsetting = ps->b.currifc;
		bifc->num_altsetting = aifc->num_altsetting + 1;
	}
	usb_parse_iface_sub(ps);

	for (x = 0; x != aifc->num_altsetting; x++) {
		ps->a.currifc = aifc->altsetting + x;
		usb_parse_iface_sub(ps);
	}
	return;
}

static void
usb_parse_config(struct usb_parse_state *ps)
{
	struct libusb20_config *acfg;
	struct usb_config_descriptor *bcfg;
	uint8_t x;

	acfg = ps->a.currcfg;
	bcfg = ps->b.currcfg;

	if (ps->preparse == 0) {
		/* initialise config wrapper */
		bcfg->bLength = acfg->desc.bLength;
		bcfg->bDescriptorType = acfg->desc.bDescriptorType;
		bcfg->wTotalLength = acfg->desc.wTotalLength;
		bcfg->bNumInterfaces = acfg->num_interface;
		bcfg->bConfigurationValue = acfg->desc.bConfigurationValue;
		bcfg->iConfiguration = acfg->desc.iConfiguration;
		bcfg->bmAttributes = acfg->desc.bmAttributes;
		bcfg->MaxPower = acfg->desc.bMaxPower;
		bcfg->interface = ps->b.currifcw;
	}
	for (x = 0; x != acfg->num_interface; x++) {
		ps->a.currifc = acfg->interface + x;
		usb_parse_iface(ps);
	}

	ps->a.currextra = &acfg->extra;
	usb_parse_extra(ps, &bcfg->extra, &bcfg->extralen);
	return;
}

int
usb_parse_configuration(struct usb_config_descriptor *config,
    uint8_t *buffer)
{
	struct usb_parse_state ps;
	uint8_t *ptr;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;

	if ((buffer == NULL) || (config == NULL)) {
		return (-1);
	}
	memset(&ps, 0, sizeof(ps));

	ps.a.currcfg = libusb20_parse_config_desc(buffer);
	ps.b.currcfg = config;
	if (ps.a.currcfg == NULL) {
		/* could not parse config or out of memory */
		return (-1);
	}
	/* do the pre-parse */
	ps.preparse = 1;
	usb_parse_config(&ps);

	a = ((uint8_t *)(ps.b.currifcw) - ((uint8_t *)0));
	b = ((uint8_t *)(ps.b.currifc) - ((uint8_t *)0));
	c = ((uint8_t *)(ps.b.currep) - ((uint8_t *)0));
	d = ((uint8_t *)(ps.b.currextra) - ((uint8_t *)0));

	/* allocate memory for our configuration */
	ptr = malloc(a + b + c + d);
	if (ptr == NULL) {
		/* free config structure */
		free(ps.a.currcfg);
		return (-1);
	}

	/* "currifcw" must be first, hence this pointer is freed */
	ps.b.currifcw = (void *)(ptr);
	ps.b.currifc = (void *)(ptr + a);
	ps.b.currep = (void *)(ptr + a + b);
	ps.b.currextra = (void *)(ptr + a + b + c);

	/* generate a libusb v0.1 compatible structure */
	ps.preparse = 0;
	usb_parse_config(&ps);

	/* free config structure */
	free(ps.a.currcfg);

	return (0);			/* success */
}

void
usb_destroy_configuration(struct usb_device *dev)
{
	uint8_t c;

	if (dev->config == NULL) {
		return;
	}
	for (c = 0; c != dev->descriptor.bNumConfigurations; c++) {
		struct usb_config_descriptor *cf = &dev->config[c];

		if (cf->interface != NULL) {
			free(cf->interface);
			cf->interface = NULL;
		}
	}

	free(dev->config);
	dev->config = NULL;
	return;
}

void
usb_fetch_and_parse_descriptors(usb_dev_handle * udev)
{
	struct usb_device *dev;
	struct libusb20_device *pdev;
	uint8_t *ptr;
	int error;
	uint32_t size;
	uint16_t len;
	uint8_t x;

	if (udev == NULL) {
		/* be NULL safe */
		return;
	}
	dev = usb_device(udev);
	pdev = (void *)udev;

	if (dev->descriptor.bNumConfigurations == 0) {
		/* invalid device */
		return;
	}
	size = dev->descriptor.bNumConfigurations *
	    sizeof(struct usb_config_descriptor);

	dev->config = malloc(size);
	if (dev->config == NULL) {
		/* out of memory */
		return;
	}
	memset(dev->config, 0, size);

	for (x = 0; x != dev->descriptor.bNumConfigurations; x++) {

		error = (pdev->methods->get_config_desc_full) (
		    pdev, &ptr, &len, x);

		if (error) {
			usb_destroy_configuration(dev);
			return;
		}
		usb_parse_configuration(dev->config + x, ptr);

		/* free config buffer */
		free(ptr);
	}
	return;
}

static int
usb_std_io(usb_dev_handle * dev, int ep, char *bytes, int size,
    int timeout, int is_intr)
{
	struct libusb20_transfer *xfer;
	uint32_t temp;
	uint32_t maxsize;
	uint32_t actlen;
	char *oldbytes;

	xfer = usb_get_transfer_by_ep_no(dev, ep);
	if (xfer == NULL)
		return (-1);

	if (libusb20_tr_pending(xfer)) {
		/* there is already a transfer ongoing */
		return (-1);
	}
	maxsize = libusb20_tr_get_max_total_length(xfer);
	oldbytes = bytes;

	/*
	 * We allow transferring zero bytes which is the same
	 * equivalent to a zero length USB packet.
	 */
	do {

		temp = size;
		if (temp > maxsize) {
			/* find maximum possible length */
			temp = maxsize;
		}
		if (is_intr)
			libusb20_tr_setup_intr(xfer, bytes, temp, timeout);
		else
			libusb20_tr_setup_bulk(xfer, bytes, temp, timeout);

		libusb20_tr_start(xfer);

		while (1) {

			if (libusb20_dev_process((void *)dev) != 0) {
				/* device detached */
				return (-1);
			}
			if (libusb20_tr_pending(xfer) == 0) {
				/* transfer complete */
				break;
			}
			/* wait for USB event from kernel */
			libusb20_dev_wait_process((void *)dev, -1);
		}

		switch (libusb20_tr_get_status(xfer)) {
		case 0:
			/* success */
			break;
		case LIBUSB20_TRANSFER_TIMED_OUT:
			/* transfer timeout */
			return (-ETIMEDOUT);
		default:
			/* other transfer error */
			return (-ENXIO);
		}
		actlen = libusb20_tr_get_actual_length(xfer);

		bytes += actlen;
		size -= actlen;

		if (actlen != temp) {
			/* short transfer */
			break;
		}
	} while (size > 0);

	return (bytes - oldbytes);
}

int
usb_bulk_write(usb_dev_handle * dev, int ep, char *bytes,
    int size, int timeout)
{
	return (usb_std_io(dev, ep & ~USB_ENDPOINT_DIR_MASK,
	    bytes, size, timeout, 0));
}

int
usb_bulk_read(usb_dev_handle * dev, int ep, char *bytes,
    int size, int timeout)
{
	return (usb_std_io(dev, ep | USB_ENDPOINT_DIR_MASK,
	    bytes, size, timeout, 0));
}

int
usb_interrupt_write(usb_dev_handle * dev, int ep, char *bytes,
    int size, int timeout)
{
	return (usb_std_io(dev, ep & ~USB_ENDPOINT_DIR_MASK,
	    bytes, size, timeout, 1));
}

int
usb_interrupt_read(usb_dev_handle * dev, int ep, char *bytes,
    int size, int timeout)
{
	return (usb_std_io(dev, ep | USB_ENDPOINT_DIR_MASK,
	    bytes, size, timeout, 1));
}

int
usb_control_msg(usb_dev_handle * dev, int requesttype, int request,
    int value, int wIndex, char *bytes, int size, int timeout)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	int err;
	uint16_t actlen;

	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);

	req.bmRequestType = requesttype;
	req.bRequest = request;
	req.wValue = value;
	req.wIndex = wIndex;
	req.wLength = size;

	err = libusb20_dev_request_sync((void *)dev, &req, bytes,
	    &actlen, timeout, 0);

	if (err)
		return (-1);

	return (actlen);
}

int
usb_set_configuration(usb_dev_handle * udev, int bConfigurationValue)
{
	struct usb_device *dev;
	int err;
	uint8_t i;

	/*
	 * Need to translate from "bConfigurationValue" to
	 * configuration index:
	 */

	if (bConfigurationValue == 0) {
		/* unconfigure */
		i = 255;
	} else {
		/* lookup configuration index */
		dev = usb_device(udev);

		/* check if the configuration array is not there */
		if (dev->config == NULL) {
			return (-1);
		}
		for (i = 0;; i++) {
			if (i == dev->descriptor.bNumConfigurations) {
				/* "bConfigurationValue" not found */
				return (-1);
			}
			if ((dev->config + i)->bConfigurationValue ==
			    bConfigurationValue) {
				break;
			}
		}
	}

	err = libusb20_dev_set_config_index((void *)udev, i);

	if (err)
		return (-1);

	return (0);
}

int
usb_claim_interface(usb_dev_handle * dev, int interface)
{
	struct libusb20_device *pdev = (void *)dev;

	pdev->claimed_interface = interface;

	return (0);
}

int
usb_release_interface(usb_dev_handle * dev, int interface)
{
	/* do nothing */
	return (0);
}

int
usb_set_altinterface(usb_dev_handle * dev, int alternate)
{
	struct libusb20_device *pdev = (void *)dev;
	int err;
	uint8_t iface;

	iface = pdev->claimed_interface;

	err = libusb20_dev_set_alt_index((void *)dev, iface, alternate);

	if (err)
		return (-1);

	return (0);
}

int
usb_resetep(usb_dev_handle * dev, unsigned int ep)
{
	/* emulate an endpoint reset through clear-STALL */
	return (usb_clear_halt(dev, ep));
}

int
usb_clear_halt(usb_dev_handle * dev, unsigned int ep)
{
	struct libusb20_transfer *xfer;

	xfer = usb_get_transfer_by_ep_no(dev, ep);
	if (xfer == NULL)
		return (-1);

	libusb20_tr_clear_stall_sync(xfer);

	return (0);
}

int
usb_reset(usb_dev_handle * dev)
{
	int err;

	err = libusb20_dev_reset((void *)dev);

	if (err)
		return (-1);

	/*
	 * Be compatible with LibUSB from sourceforge and close the
	 * handle after reset!
	 */
	return (usb_close(dev));
}

int
usb_check_connected(usb_dev_handle * dev)
{
	int err;

	err = libusb20_dev_check_connected((void *)dev);

	if (err)
		return (-1);

	return (0);
}

const char *
usb_strerror(void)
{
	/* TODO */
	return ("Unknown error");
}

void
usb_init(void)
{
	/* nothing to do */
	return;
}

void
usb_set_debug(int level)
{
	/* use kernel UGEN debugging if you need to see what is going on */
	return;
}

int
usb_find_busses(void)
{
	usb_busses = &usb_global_bus;
	return (1);
}

int
usb_find_devices(void)
{
	struct libusb20_device *pdev;
	struct usb_device *udev;
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;
	int devnum;
	int err;

	/* cleanup after last device search */
	/* close all opened devices, if any */

	while ((pdev = libusb20_be_device_foreach(usb_backend, NULL))) {
		udev = pdev->privLuData;
		libusb20_be_dequeue_device(usb_backend, pdev);
		libusb20_dev_free(pdev);
		if (udev != NULL) {
			LIST_DEL(usb_global_bus.devices, udev);
			free(udev);
		}
	}

	/* free old USB backend, if any */

	libusb20_be_free(usb_backend);

	/* do a new backend device search */
	usb_backend = libusb20_be_alloc_default();
	if (usb_backend == NULL) {
		return (-1);
	}
	/* iterate all devices */

	devnum = 1;
	pdev = NULL;
	while ((pdev = libusb20_be_device_foreach(usb_backend, pdev))) {
		udev = malloc(sizeof(*udev));
		if (udev == NULL)
			break;

		memset(udev, 0, sizeof(*udev));

		udev->bus = &usb_global_bus;

		snprintf(udev->filename, sizeof(udev->filename),
		    "/dev/ugen%u.%u",
		    libusb20_dev_get_bus_number(pdev),
		    libusb20_dev_get_address(pdev));

		ddesc = libusb20_dev_get_device_desc(pdev);

		udev->descriptor.bLength = sizeof(udev->descriptor);
		udev->descriptor.bDescriptorType = ddesc->bDescriptorType;
		udev->descriptor.bcdUSB = ddesc->bcdUSB;
		udev->descriptor.bDeviceClass = ddesc->bDeviceClass;
		udev->descriptor.bDeviceSubClass = ddesc->bDeviceSubClass;
		udev->descriptor.bDeviceProtocol = ddesc->bDeviceProtocol;
		udev->descriptor.bMaxPacketSize0 = ddesc->bMaxPacketSize0;
		udev->descriptor.idVendor = ddesc->idVendor;
		udev->descriptor.idProduct = ddesc->idProduct;
		udev->descriptor.bcdDevice = ddesc->bcdDevice;
		udev->descriptor.iManufacturer = ddesc->iManufacturer;
		udev->descriptor.iProduct = ddesc->iProduct;
		udev->descriptor.iSerialNumber = ddesc->iSerialNumber;
		udev->descriptor.bNumConfigurations =
		    ddesc->bNumConfigurations;
		if (udev->descriptor.bNumConfigurations > USB_MAXCONFIG) {
			/* truncate number of configurations */
			udev->descriptor.bNumConfigurations = USB_MAXCONFIG;
		}
		udev->devnum = devnum++;
		/* link together the two structures */
		udev->dev = pdev;
		pdev->privLuData = udev;

		err = libusb20_dev_open(pdev, 0);
		if (err == 0) {
			/* XXX get all config descriptors by default */
			usb_fetch_and_parse_descriptors((void *)pdev);
			libusb20_dev_close(pdev);
		}
		LIST_ADD(usb_global_bus.devices, udev);
	}

	return (devnum - 1);			/* success */
}

struct usb_device *
usb_device(usb_dev_handle * dev)
{
	struct libusb20_device *pdev;

	pdev = (void *)dev;

	return (pdev->privLuData);
}

struct usb_bus *
usb_get_busses(void)
{
	return (usb_busses);
}

int
usb_get_driver_np(usb_dev_handle * dev, int interface, char *name, int namelen)
{
	struct libusb20_device *pdev;
	char *ptr;
	int err;

	pdev = (void *)dev;

	if (pdev == NULL)
		return (-1);
	if (namelen < 1)
		return (-1);
	if (namelen > 255)
		namelen = 255;

	err = libusb20_dev_get_iface_desc(pdev, interface, name, namelen);
	if (err != 0)
		return (-1);

	/* we only want the driver name */
	ptr = strstr(name, ":");
	if (ptr != NULL)
		*ptr = 0;

	return (0);
}

int
usb_detach_kernel_driver_np(usb_dev_handle * dev, int interface)
{
	struct libusb20_device *pdev;
	int err;

	pdev = (void *)dev;

	if (pdev == NULL)
		return (-1);

	err = libusb20_dev_detach_kernel_driver(pdev, interface);
	if (err != 0)
		return (-1);

	return (0);
}
