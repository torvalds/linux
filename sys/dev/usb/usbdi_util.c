/*	$OpenBSD: usbdi_util.c,v 1.47 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: usbdi_util.c,v 1.40 2002/07/11 21:14:36 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdi_util.c,v 1.14 1999/11/17 22:33:50 n_hibma Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (usbdebug>(n)) printf x; } while (0)
extern int usbdebug;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

usbd_status
usbd_get_desc(struct usbd_device *dev, int type, int index, int len, void *desc)
{
	usb_device_request_t req;

	DPRINTFN(3,("%s: type=%d, index=%d, len=%d\n", __func__, type, index,
	    len));

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, type, index);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(dev, &req, desc));
}

usbd_status
usbd_get_device_status(struct usbd_device *dev, usb_status_t *st)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(usb_status_t));
	return (usbd_do_request(dev, &req, st));
}

usbd_status
usbd_get_hub_descriptor(struct usbd_device *dev, usb_hub_descriptor_t *hd,
    uint8_t nports)
{
	usb_device_request_t req;
	uint16_t len = USB_HUB_DESCRIPTOR_SIZE + (nports + 1) / 8;

	req.bmRequestType = UT_READ_CLASS_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_HUB, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(dev, &req, hd));
}

usbd_status
usbd_get_hub_ss_descriptor(struct usbd_device *dev, usb_hub_ss_descriptor_t *hd,
    uint8_t nports)
{
	usb_device_request_t req;
	uint16_t len = USB_HUB_SS_DESCRIPTOR_SIZE + (nports + 1) / 8;

	req.bmRequestType = UT_READ_CLASS_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_SS_HUB, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(dev, &req, hd));
}

usbd_status
usbd_get_port_status(struct usbd_device *dev, int port, usb_port_status_t *ps)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_CLASS_OTHER;
	req.bRequest = UR_GET_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, port);
	USETW(req.wLength, sizeof *ps);
	return (usbd_do_request(dev, &req, ps));
}

usbd_status
usbd_set_hub_depth(struct usbd_device *dev, int depth)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_CLASS_DEVICE;
	req.bRequest = UR_SET_DEPTH;
	USETW(req.wValue, depth);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return usbd_do_request(dev, &req, 0);
}

usbd_status
usbd_clear_port_feature(struct usbd_device *dev, int port, int sel)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, port);
	USETW(req.wLength, 0);
	return (usbd_do_request(dev, &req, 0));
}

usbd_status
usbd_clear_endpoint_feature(struct usbd_device *dev, int epaddr, int sel)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_ENDPOINT;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, epaddr);
	USETW(req.wLength, 0);
	return (usbd_do_request(dev, &req, 0));
}

usbd_status
usbd_set_port_feature(struct usbd_device *dev, int port, int sel)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, port);
	USETW(req.wLength, 0);
	return (usbd_do_request(dev, &req, 0));
}

usbd_status
usbd_set_idle(struct usbd_device *dev, int ifaceno, int duration, int id)
{
	usb_device_request_t req;

	DPRINTFN(4, ("%s: %d %d\n", __func__, duration, id));
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UR_SET_IDLE;
	USETW2(req.wValue, duration, id);
	USETW(req.wIndex, ifaceno);
	USETW(req.wLength, 0);
	return (usbd_do_request(dev, &req, 0));
}

usbd_status
usbd_get_report_descriptor(struct usbd_device *dev, int ifaceno,
    void *data, int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_INTERFACE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_REPORT, 0); /* report id should be 0 */
	USETW(req.wIndex, ifaceno);
	USETW(req.wLength, len);
	return (usbd_do_request(dev, &req, data));
}

struct usb_hid_descriptor *
usbd_get_hid_descriptor(struct usbd_device *dev, usb_interface_descriptor_t *id)
{
	usb_config_descriptor_t *cdesc = usbd_get_config_descriptor(dev);
	struct usb_hid_descriptor *hd;
	char *p, *end;

	p = (char *)id + id->bLength;
	end = (char *)cdesc + UGETW(cdesc->wTotalLength);

	for (; p < end; p += hd->bLength) {
		hd = (struct usb_hid_descriptor *)p;
		if (p + hd->bLength <= end && hd->bDescriptorType == UDESC_HID)
			return (hd);
		if (hd->bDescriptorType == UDESC_INTERFACE)
			break;
	}
	return (0);
}

usbd_status
usbd_get_config(struct usbd_device *dev, u_int8_t *conf)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_CONFIG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 1);
	return (usbd_do_request(dev, &req, conf));
}

void
usb_detach_wait(struct device *dv)
{
	DPRINTF(("%s: waiting for %s\n", __func__, dv->dv_xname));
	if (tsleep_nsec(dv, PZERO, "usbdet", SEC_TO_NSEC(60)))
		printf("%s: %s didn't detach\n", __func__, dv->dv_xname);
	DPRINTF(("%s: %s done\n", __func__, dv->dv_xname));
}

void
usb_detach_wakeup(struct device *dv)
{
	DPRINTF(("%s: for %s\n", __func__, dv->dv_xname));
	wakeup(dv);
}
