/*	$OpenBSD: usb_subr.c,v 1.164 2025/03/01 14:43:03 kirill Exp $ */
/*	$NetBSD: usb_subr.c,v 1.103 2003/01/10 11:19:13 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usb_subr.c,v 1.18 1999/11/17 22:33:47 n_hibma Exp $	*/

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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (usbdebug>(n)) printf x; } while (0)
extern int usbdebug;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

usbd_status	usbd_set_config(struct usbd_device *, int);
void		usbd_devinfo(struct usbd_device *, int, char *, size_t);
char		*usbd_get_string(struct usbd_device *, int, char *, size_t);
int		usbd_getnewaddr(struct usbd_bus *);
int		usbd_print(void *, const char *);
void		usbd_free_iface_data(struct usbd_device *, int);
int		usbd_cache_devinfo(struct usbd_device *);
usbd_status	usbd_probe_and_attach(struct device *,
		    struct usbd_device *, int, int);

int		usbd_printBCD(char *cp, size_t len, int bcd);
void		usb_free_device(struct usbd_device *);
int		usbd_parse_idesc(struct usbd_device *, struct usbd_interface *);

#ifdef USBVERBOSE
#include <dev/usb/usbdevs_data.h>
#endif /* USBVERBOSE */

const char * const usbd_error_strs[] = {
	"NORMAL_COMPLETION",
	"IN_PROGRESS",
	"PENDING_REQUESTS",
	"NOT_STARTED",
	"INVAL",
	"NOMEM",
	"CANCELLED",
	"BAD_ADDRESS",
	"IN_USE",
	"NO_ADDR",
	"SET_ADDR_FAILED",
	"NO_POWER",
	"TOO_DEEP",
	"IOERROR",
	"NOT_CONFIGURED",
	"TIMEOUT",
	"SHORT_XFER",
	"STALLED",
	"INTERRUPTED",
	"XXX",
};

const char *
usbd_errstr(usbd_status err)
{
	static char buffer[5];

	if (err < USBD_ERROR_MAX)
		return (usbd_error_strs[err]);
	else {
		snprintf(buffer, sizeof(buffer), "%d", err);
		return (buffer);
	}
}

usbd_status
usbd_get_string_desc(struct usbd_device *dev, int sindex, int langid,
    usb_string_descriptor_t *sdesc, int *sizep)
{
	usb_device_request_t req;
	usbd_status err;
	int actlen;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_STRING, sindex);
	USETW(req.wIndex, langid);
	USETW(req.wLength, 2);	/* size and descriptor type first */
	err = usbd_do_request_flags(dev, &req, sdesc, USBD_SHORT_XFER_OK,
	    &actlen, USBD_DEFAULT_TIMEOUT);
	if (err)
		return (err);

	if (actlen < 2)
		return (USBD_SHORT_XFER);

	USETW(req.wLength, sdesc->bLength);	/* the whole string */
	err = usbd_do_request_flags(dev, &req, sdesc, USBD_SHORT_XFER_OK,
	    &actlen, USBD_DEFAULT_TIMEOUT);
	if (err)
		return (err);

	if (actlen != sdesc->bLength) {
		DPRINTFN(-1, ("%s: expected %d, got %d\n", __func__,
		    sdesc->bLength, actlen));
	}

	*sizep = actlen;
	return (USBD_NORMAL_COMPLETION);
}

char *
usbd_get_string(struct usbd_device *dev, int si, char *buf, size_t buflen)
{
	int swap = dev->quirks->uq_flags & UQ_SWAP_UNICODE;
	usb_string_descriptor_t us;
	char *s;
	int i, n;
	u_int16_t c;
	usbd_status err;
	int size;

	if (si == 0)
		return (0);
	if (dev->quirks->uq_flags & UQ_NO_STRINGS)
		return (0);
	if (dev->langid == USBD_NOLANG) {
		/* Set up default language */
		err = usbd_get_string_desc(dev, USB_LANGUAGE_TABLE, 0, &us,
		    &size);
		if (err || size < 4)
			dev->langid = 0; /* Well, just pick English then */
		else {
			/* Pick the first language as the default. */
			dev->langid = UGETW(us.bString[0]);
		}
	}
	err = usbd_get_string_desc(dev, si, dev->langid, &us, &size);
	if (err)
		return (0);
	s = buf;
	n = size / 2 - 1;
	for (i = 0; i < n && i < buflen ; i++) {
		c = UGETW(us.bString[i]);
		/* Convert from Unicode, handle buggy strings. */
		if ((c & 0xff00) == 0)
			*s++ = c;
		else if ((c & 0x00ff) == 0 && swap)
			*s++ = c >> 8;
		else
			*s++ = '?';
	}
	if (buflen > 0)
		*s++ = 0;
	return (buf);
}

static void
usbd_trim_spaces(char *p)
{
	char *q, *e;

	if (p == NULL)
		return;
	q = e = p;
	while (*q == ' ')	/* skip leading spaces */
		q++;
	while ((*p = *q++))	/* copy string */
		if (*p++ != ' ') /* remember last non-space */
			e = p;
	*e = 0;			/* kill trailing spaces */
}

int
usbd_cache_devinfo(struct usbd_device *dev)
{
	usb_device_descriptor_t *udd = &dev->ddesc;

	dev->serial = malloc(USB_MAX_STRING_LEN, M_USB, M_NOWAIT);
	if (dev->serial == NULL)
		return (ENOMEM);

	if (usbd_get_string(dev, udd->iSerialNumber, dev->serial, USB_MAX_STRING_LEN) != NULL) {
		usbd_trim_spaces(dev->serial);
	} else {
		free(dev->serial, M_USB, USB_MAX_STRING_LEN);
		dev->serial = NULL;
	}

	dev->vendor = malloc(USB_MAX_STRING_LEN, M_USB, M_NOWAIT);
	if (dev->vendor == NULL)
		return (ENOMEM);

	if (usbd_get_string(dev, udd->iManufacturer, dev->vendor, USB_MAX_STRING_LEN) != NULL) {
		usbd_trim_spaces(dev->vendor);
	} else {
#ifdef USBVERBOSE
		const struct usb_known_vendor *ukv;

		for (ukv = usb_known_vendors; ukv->vendorname != NULL; ukv++) {
			if (ukv->vendor == UGETW(udd->idVendor)) {
				strlcpy(dev->vendor, ukv->vendorname,
				    USB_MAX_STRING_LEN);
				break;
			}
		}
		if (ukv->vendorname == NULL)
#endif
			snprintf(dev->vendor, USB_MAX_STRING_LEN, "vendor 0x%04x",
			    UGETW(udd->idVendor));
	}

	dev->product = malloc(USB_MAX_STRING_LEN, M_USB, M_NOWAIT);
	if (dev->product == NULL)
		return (ENOMEM);

	if (usbd_get_string(dev, udd->iProduct, dev->product, USB_MAX_STRING_LEN) != NULL) {
		usbd_trim_spaces(dev->product);
	} else {
#ifdef USBVERBOSE
		const struct usb_known_product *ukp;

		for (ukp = usb_known_products; ukp->productname != NULL; ukp++) {
			if (ukp->vendor == UGETW(udd->idVendor) &&
			    (ukp->product == UGETW(udd->idProduct))) {
				strlcpy(dev->product, ukp->productname,
				    USB_MAX_STRING_LEN);
				break;
			}
		}
		if (ukp->productname == NULL)
#endif
			snprintf(dev->product, USB_MAX_STRING_LEN, "product 0x%04x",
			    UGETW(udd->idProduct));
	}

	return (0);
}

int
usbd_printBCD(char *cp, size_t len, int bcd)
{
	int l;

	l = snprintf(cp, len, "%x.%02x", bcd >> 8, bcd & 0xff);
	if (l == -1 || len == 0)
		return (0);
	if (l >= len)
		return len - 1;
	return (l);
}

void
usbd_devinfo(struct usbd_device *dev, int showclass, char *base, size_t len)
{
	usb_device_descriptor_t *udd = &dev->ddesc;
	char *cp = base;
	int bcdDevice, bcdUSB;

	snprintf(cp, len, "\"%s %s\"", dev->vendor, dev->product);
	cp += strlen(cp);
	if (showclass) {
		snprintf(cp, base + len - cp, ", class %d/%d",
		    udd->bDeviceClass, udd->bDeviceSubClass);
		cp += strlen(cp);
	}
	bcdUSB = UGETW(udd->bcdUSB);
	bcdDevice = UGETW(udd->bcdDevice);
	snprintf(cp, base + len - cp, " rev ");
	cp += strlen(cp);
	usbd_printBCD(cp, base + len - cp, bcdUSB);
	cp += strlen(cp);
	snprintf(cp, base + len - cp, "/");
	cp += strlen(cp);
	usbd_printBCD(cp, base + len - cp, bcdDevice);
	cp += strlen(cp);
	snprintf(cp, base + len - cp, " addr %d", dev->address);
}

/* Delay for a certain number of ms */
void
usb_delay_ms(struct usbd_bus *bus, u_int ms)
{
	static int usb_delay_wchan;

	if (bus->use_polling || cold)
		delay((ms+1) * 1000);
	else
		tsleep_nsec(&usb_delay_wchan, PRIBIO, "usbdly",
		    MSEC_TO_NSEC(ms));
}

/* Delay given a device handle. */
void
usbd_delay_ms(struct usbd_device *dev, u_int ms)
{
	if (usbd_is_dying(dev))
		return;

	usb_delay_ms(dev->bus, ms);
}

usbd_status
usbd_port_disown_to_1_1(struct usbd_device *dev, int port)
{
	usb_port_status_t ps;
	usbd_status err;
	int n;

	err = usbd_set_port_feature(dev, port, UHF_PORT_DISOWN_TO_1_1);
	DPRINTF(("%s: port %d disown request done, error=%s\n", __func__,
	    port, usbd_errstr(err)));
	if (err)
		return (err);
	n = 10;
	do {
		/* Wait for device to recover from reset. */
		usbd_delay_ms(dev, USB_PORT_RESET_DELAY);
		err = usbd_get_port_status(dev, port, &ps);
		if (err) {
			DPRINTF(("%s: get status failed %d\n", __func__, err));
			return (err);
		}
		/* If the device disappeared, just give up. */
		if (!(UGETW(ps.wPortStatus) & UPS_CURRENT_CONNECT_STATUS))
			return (USBD_NORMAL_COMPLETION);
	} while ((UGETW(ps.wPortChange) & UPS_C_PORT_RESET) == 0 && --n > 0);
	if (n == 0)
		return (USBD_TIMEOUT);

	return (err);
}

int
usbd_reset_port(struct usbd_device *dev, int port)
{
	usb_port_status_t ps;
	int n;

	if (usbd_set_port_feature(dev, port, UHF_PORT_RESET))
		return (EIO);
	DPRINTF(("%s: port %d reset done\n", __func__, port));
	n = 10;
	do {
		/* Wait for device to recover from reset. */
		usbd_delay_ms(dev, USB_PORT_RESET_DELAY);
		if (usbd_get_port_status(dev, port, &ps)) {
			DPRINTF(("%s: get status failed\n", __func__));
			return (EIO);
		}
		/* If the device disappeared, just give up. */
		if (!(UGETW(ps.wPortStatus) & UPS_CURRENT_CONNECT_STATUS))
			return (0);
	} while ((UGETW(ps.wPortChange) & UPS_C_PORT_RESET) == 0 && --n > 0);

	/* Clear port reset even if a timeout occurred. */
	if (usbd_clear_port_feature(dev, port, UHF_C_PORT_RESET)) {
		DPRINTF(("%s: clear port feature failed\n", __func__));
		return (EIO);
	}

	if (n == 0)
		return (ETIMEDOUT);

	/* Wait for the device to recover from reset. */
	usbd_delay_ms(dev, USB_PORT_RESET_RECOVERY);
	return (0);
}

usb_interface_descriptor_t *
usbd_find_idesc(usb_config_descriptor_t *cd, int ifaceno, int altno)
{
	char *p = (char *)cd;
	char *end = p + UGETW(cd->wTotalLength);
	usb_interface_descriptor_t *d;
	int curidx, lastidx, curaidx = 0;

	for (curidx = lastidx = -1; p < end; ) {
		d = (usb_interface_descriptor_t *)p;
		DPRINTFN(4,("usbd_find_idesc: ifaceno=%d(%d) altno=%d(%d) "
			    "len=%d type=%d\n",
			    ifaceno, curidx, altno, curaidx,
			    d->bLength, d->bDescriptorType));
		if (d->bLength == 0) /* bad descriptor */
			break;
		p += d->bLength;
		if (p <= end && d->bDescriptorType == UDESC_INTERFACE) {
			if (d->bInterfaceNumber != lastidx) {
				lastidx = d->bInterfaceNumber;
				curidx++;
				curaidx = 0;
			} else
				curaidx++;
			if (ifaceno == curidx && altno == curaidx)
				return (d);
		}
	}
	return (NULL);
}

usb_endpoint_descriptor_t *
usbd_find_edesc(usb_config_descriptor_t *cd, int ifaceno, int altno,
		int endptidx)
{
	char *p = (char *)cd;
	char *end = p + UGETW(cd->wTotalLength);
	usb_interface_descriptor_t *d;
	usb_endpoint_descriptor_t *e;
	int curidx;

	d = usbd_find_idesc(cd, ifaceno, altno);
	if (d == NULL)
		return (NULL);
	if (endptidx >= d->bNumEndpoints) /* quick exit */
		return (NULL);

	curidx = -1;
	for (p = (char *)d + d->bLength; p < end; ) {
		e = (usb_endpoint_descriptor_t *)p;
		if (e->bLength == 0) /* bad descriptor */
			break;
		p += e->bLength;
		if (p <= end && e->bDescriptorType == UDESC_INTERFACE)
			return (NULL);
		if (p <= end && e->bDescriptorType == UDESC_ENDPOINT) {
			curidx++;
			if (curidx == endptidx)
				return (e);
		}
	}
	return (NULL);
}

usbd_status
usbd_fill_iface_data(struct usbd_device *dev, int ifaceno, int altno)
{
	struct usbd_interface *ifc = &dev->ifaces[ifaceno];
	usb_interface_descriptor_t *idesc;
	int nendpt;

	DPRINTFN(4,("%s: ifaceno=%d altno=%d\n", __func__, ifaceno, altno));

	idesc = usbd_find_idesc(dev->cdesc, ifaceno, altno);
	if (idesc == NULL)
		return (USBD_INVAL);

	nendpt = idesc->bNumEndpoints;
	DPRINTFN(4,("%s: found idesc nendpt=%d\n", __func__, nendpt));

	ifc->device = dev;
	ifc->idesc = idesc;
	ifc->index = ifaceno;
	ifc->altindex = altno;
	ifc->endpoints = NULL;
	ifc->priv = NULL;
	LIST_INIT(&ifc->pipes);
	ifc->nendpt = nendpt;

	if (nendpt != 0) {
		ifc->endpoints = mallocarray(nendpt, sizeof(*ifc->endpoints),
		    M_USB, M_NOWAIT | M_ZERO);
		if (ifc->endpoints == NULL)
			return (USBD_NOMEM);
	}

	if (usbd_parse_idesc(dev, ifc)) {
		free(ifc->endpoints, M_USB, nendpt * sizeof(*ifc->endpoints));
		ifc->endpoints = NULL;
		return (USBD_INVAL);
	}

	return (USBD_NORMAL_COMPLETION);
}

int
usbd_parse_idesc(struct usbd_device *dev, struct usbd_interface *ifc)
{
#define ed ((usb_endpoint_descriptor_t *)p)
#define essd ((usb_endpoint_ss_comp_descriptor_t *)pp)
	char *p, *pp, *end;
	int i;

	p = (char *)ifc->idesc + ifc->idesc->bLength;
	end = (char *)dev->cdesc + UGETW(dev->cdesc->wTotalLength);

	for (i = 0; i < ifc->idesc->bNumEndpoints; i++) {
		for (; p < end; p += ed->bLength) {
			if (p + ed->bLength <= end && ed->bLength != 0 &&
			    ed->bDescriptorType == UDESC_ENDPOINT)
				break;

			if (ed->bLength == 0 ||
			    ed->bDescriptorType == UDESC_INTERFACE)
			    	return (-1);
		}

		if (p >= end)
			return (-1);

		pp = p + ed->bLength;
		if (pp >= end || essd->bLength == 0 ||
		    essd->bDescriptorType != UDESC_ENDPOINT_SS_COMP)
			pp = NULL;

		if (dev->speed == USB_SPEED_HIGH) {
			unsigned int mps;

			/* Control and bulk endpoints have max packet limits. */
			switch (UE_GET_XFERTYPE(ed->bmAttributes)) {
			case UE_CONTROL:
				mps = USB_2_MAX_CTRL_PACKET;
				goto check;
			case UE_BULK:
				mps = USB_2_MAX_BULK_PACKET;
			check:
				if (UGETW(ed->wMaxPacketSize) != mps) {
					USETW(ed->wMaxPacketSize, mps);
					DPRINTF(("%s: bad max packet size\n",
					    __func__));
				}
				break;
			default:
				break;
			}
		}

		ifc->endpoints[i].edesc = ed;
		ifc->endpoints[i].esscd = essd;
		ifc->endpoints[i].refcnt = 0;
		ifc->endpoints[i].savedtoggle = 0;
		p += ed->bLength;
	}

	return (0);
#undef ed
#undef essd
}

void
usbd_free_iface_data(struct usbd_device *dev, int ifcno)
{
	struct usbd_interface *ifc = &dev->ifaces[ifcno];

	free(ifc->endpoints, M_USB, ifc->nendpt * sizeof(*ifc->endpoints));
	ifc->endpoints = NULL;
}

usbd_status
usbd_set_config(struct usbd_device *dev, int conf)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_CONFIG;
	USETW(req.wValue, conf);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(dev, &req, 0));
}

usbd_status
usbd_set_config_no(struct usbd_device *dev, int no, int msg)
{
	int index;
	usb_config_descriptor_t cd;
	usbd_status err;

	DPRINTFN(5,("%s: %d\n", __func__, no));
	/* Figure out what config index to use. */
	for (index = 0; index < dev->ddesc.bNumConfigurations; index++) {
		err = usbd_get_desc(dev, UDESC_CONFIG, index,
		    USB_CONFIG_DESCRIPTOR_SIZE, &cd);
		if (err || cd.bDescriptorType != UDESC_CONFIG)
			return (err);
		if (cd.bConfigurationValue == no)
			return (usbd_set_config_index(dev, index, msg));
	}
	return (USBD_INVAL);
}

usbd_status
usbd_set_config_index(struct usbd_device *dev, int index, int msg)
{
	usb_status_t ds;
	usb_config_descriptor_t cd, *cdp;
	usbd_status err;
	int i, ifcidx, nifc, cdplen, selfpowered, power;

	DPRINTFN(5,("%s: dev=%p index=%d\n", __func__, dev, index));

	/* XXX check that all interfaces are idle */
	if (dev->config != USB_UNCONFIG_NO) {
		DPRINTF(("%s: free old config\n", __func__));
		/* Free all configuration data structures. */
		nifc = dev->cdesc->bNumInterfaces;
		for (ifcidx = 0; ifcidx < nifc; ifcidx++)
			usbd_free_iface_data(dev, ifcidx);
		free(dev->ifaces, M_USB, nifc * sizeof(*dev->ifaces));
		free(dev->cdesc, M_USB, UGETW(dev->cdesc->wTotalLength));
		dev->ifaces = NULL;
		dev->cdesc = NULL;
		dev->config = USB_UNCONFIG_NO;
	}

	if (index == USB_UNCONFIG_INDEX) {
		/* We are unconfiguring the device, so leave unallocated. */
		DPRINTF(("%s: set config 0\n", __func__));
		err = usbd_set_config(dev, USB_UNCONFIG_NO);
		if (err)
			DPRINTF(("%s: setting config=0 failed, error=%s\n",
			    __func__, usbd_errstr(err)));
		return (err);
	}

	/* Get the short descriptor. */
	err = usbd_get_desc(dev, UDESC_CONFIG, index,
	    USB_CONFIG_DESCRIPTOR_SIZE, &cd);
	if (err)
		return (err);
	if (cd.bDescriptorType != UDESC_CONFIG)
		return (USBD_INVAL);
	cdplen = UGETW(cd.wTotalLength);
	cdp = malloc(cdplen, M_USB, M_NOWAIT);
	if (cdp == NULL)
		return (USBD_NOMEM);
	/* Get the full descriptor. */
	for (i = 0; i < 3; i++) {
		err = usbd_get_desc(dev, UDESC_CONFIG, index, cdplen, cdp);
		if (!err)
			break;
		usbd_delay_ms(dev, 200);
	}
	if (err)
		goto bad;

	if (cdp->bDescriptorType != UDESC_CONFIG) {
		DPRINTFN(-1,("%s: bad desc %d\n", __func__,
		    cdp->bDescriptorType));
		err = USBD_INVAL;
		goto bad;
	}

	/* Figure out if the device is self or bus powered. */
	selfpowered = 0;
	if (!(dev->quirks->uq_flags & UQ_BUS_POWERED) &&
	    (cdp->bmAttributes & UC_SELF_POWERED)) {
		/* May be self powered. */
		if (cdp->bmAttributes & UC_BUS_POWERED) {
			/* Must ask device. */
			if (dev->quirks->uq_flags & UQ_POWER_CLAIM) {
				/*
				 * Hub claims to be self powered, but isn't.
				 * It seems that the power status can be
				 * determined by the hub characteristics.
				 */
				usb_hub_descriptor_t hd;
				usb_device_request_t req;
				req.bmRequestType = UT_READ_CLASS_DEVICE;
				req.bRequest = UR_GET_DESCRIPTOR;
				USETW(req.wValue, 0);
				USETW(req.wIndex, 0);
				USETW(req.wLength, USB_HUB_DESCRIPTOR_SIZE);
				err = usbd_do_request(dev, &req, &hd);
				if (!err &&
				    (UGETW(hd.wHubCharacteristics) &
				     UHD_PWR_INDIVIDUAL))
					selfpowered = 1;
				DPRINTF(("%s: charac=0x%04x, error=%s\n",
				    __func__, UGETW(hd.wHubCharacteristics),
				    usbd_errstr(err)));
			} else {
				err = usbd_get_device_status(dev, &ds);
				if (!err &&
				    (UGETW(ds.wStatus) & UDS_SELF_POWERED))
					selfpowered = 1;
				DPRINTF(("%s: status=0x%04x, error=%s\n",
				    __func__, UGETW(ds.wStatus),
				    usbd_errstr(err)));
			}
		} else
			selfpowered = 1;
	}
	DPRINTF(("%s: (addr %d) cno=%d attr=0x%02x, selfpowered=%d, power=%d\n",
	    __func__, dev->address, cdp->bConfigurationValue, cdp->bmAttributes,
	    selfpowered, cdp->bMaxPower * 2));

	/* Check if we have enough power. */
#ifdef USB_DEBUG
	if (dev->powersrc == NULL) {
		DPRINTF(("%s: No power source?\n", __func__));
		err = USBD_IOERROR;
		goto bad;
	}
#endif
	power = cdp->bMaxPower * 2;
	if (power > dev->powersrc->power) {
		DPRINTF(("power exceeded %d %d\n", power,dev->powersrc->power));
		/* XXX print nicer message. */
		if (msg)
			printf("%s: device addr %d (config %d) exceeds power "
			    "budget, %d mA > %d mA\n",
			    dev->bus->bdev.dv_xname, dev->address,
			    cdp->bConfigurationValue,
			    power, dev->powersrc->power);
		err = USBD_NO_POWER;
		goto bad;
	}
	dev->power = power;
	dev->self_powered = selfpowered;

	/* Set the actual configuration value. */
	DPRINTF(("%s: set config %d\n", __func__, cdp->bConfigurationValue));
	err = usbd_set_config(dev, cdp->bConfigurationValue);
	if (err) {
		DPRINTF(("%s: setting config=%d failed, error=%s\n", __func__,
		    cdp->bConfigurationValue, usbd_errstr(err)));
		goto bad;
	}

	/* Allocate and fill interface data. */
	nifc = cdp->bNumInterfaces;
	dev->ifaces = mallocarray(nifc, sizeof(*dev->ifaces), M_USB,
	    M_NOWAIT | M_ZERO);
	if (dev->ifaces == NULL) {
		err = USBD_NOMEM;
		goto bad;
	}
	DPRINTFN(5,("%s: dev=%p cdesc=%p\n", __func__, dev, cdp));
	dev->cdesc = cdp;
	dev->config = cdp->bConfigurationValue;
	for (ifcidx = 0; ifcidx < nifc; ifcidx++) {
		err = usbd_fill_iface_data(dev, ifcidx, 0);
		if (err)
			return (err);
	}

	return (USBD_NORMAL_COMPLETION);

 bad:
	free(cdp, M_USB, cdplen);
	return (err);
}

/* XXX add function for alternate settings */

usbd_status
usbd_setup_pipe(struct usbd_device *dev, struct usbd_interface *iface,
    struct usbd_endpoint *ep, int ival, struct usbd_pipe **pipe)
{
	struct usbd_pipe *p;
	usbd_status err;

	DPRINTF(("%s: dev=%p iface=%p ep=%p pipe=%p\n", __func__,
		    dev, iface, ep, pipe));
	p = malloc(dev->bus->pipe_size, M_USB, M_NOWAIT|M_ZERO);
	if (p == NULL)
		return (USBD_NOMEM);
	p->pipe_size = dev->bus->pipe_size;
	p->device = dev;
	p->iface = iface;
	p->endpoint = ep;
	ep->refcnt++;
	p->interval = ival;
	SIMPLEQ_INIT(&p->queue);
	err = dev->bus->methods->open_pipe(p);
	if (err) {
		DPRINTF(("%s: endpoint=0x%x failed, error=%s\n", __func__,
			 ep->edesc->bEndpointAddress, usbd_errstr(err)));
		free(p, M_USB, dev->bus->pipe_size);
		return (err);
	}
	*pipe = p;
	return (USBD_NORMAL_COMPLETION);
}

int
usbd_set_address(struct usbd_device *dev, int addr)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if (usbd_do_request(dev, &req, 0))
		return (1);

	/* Allow device time to set new address */
	usbd_delay_ms(dev, USB_SET_ADDRESS_SETTLE);

	return (0);
}

int
usbd_getnewaddr(struct usbd_bus *bus)
{
	int addr;

	for (addr = 1; addr < USB_MAX_DEVICES; addr++)
		if (bus->devices[addr] == NULL)
			return (addr);
	return (-1);
}

usbd_status
usbd_probe_and_attach(struct device *parent, struct usbd_device *dev, int port,
    int addr)
{
	/*
	 * Used to correlate audio and wskbd devices as this is the common point
	 * of attachment between the two.
	 */
	static char *cookie = 0;
	struct usb_attach_arg uaa;
	usb_device_descriptor_t *dd = &dev->ddesc;
	int i, confi, nifaces;
	usbd_status err;
	struct device *dv;
	struct usbd_interface **ifaces;
	extern struct rwlock usbpalock;

	rw_enter_write(&usbpalock);

	uaa.device = dev;
	uaa.iface = NULL;
	uaa.ifaces = NULL;
	uaa.nifaces = 0;
	uaa.usegeneric = 0;
	uaa.port = port;
	uaa.configno = UHUB_UNK_CONFIGURATION;
	uaa.ifaceno = UHUB_UNK_INTERFACE;
	uaa.vendor = UGETW(dd->idVendor);
	uaa.product = UGETW(dd->idProduct);
	uaa.release = UGETW(dd->bcdDevice);
	uaa.cookie = ++cookie;

	/* First try with device specific drivers. */
	DPRINTF(("usbd_probe_and_attach trying device specific drivers\n"));
	dv = config_found(parent, &uaa, usbd_print);
	if (dv) {
		dev->subdevs = mallocarray(2, sizeof dv, M_USB, M_NOWAIT);
		if (dev->subdevs == NULL) {
			err = USBD_NOMEM;
			goto fail;
		}
		dev->nsubdev = 2;
		dev->subdevs[dev->ndevs++] = dv;
		dev->subdevs[dev->ndevs] = 0;
		err = USBD_NORMAL_COMPLETION;
		goto fail;
	}

	DPRINTF(("%s: no device specific driver found\n", __func__));

	DPRINTF(("%s: looping over %d configurations\n", __func__,
		 dd->bNumConfigurations));
	/* Next try with interface drivers. */
	for (confi = 0; confi < dd->bNumConfigurations; confi++) {
		DPRINTFN(1,("%s: trying config idx=%d\n", __func__,
			    confi));
		err = usbd_set_config_index(dev, confi, 1);
		if (err) {
#ifdef USB_DEBUG
			DPRINTF(("%s: port %d, set config at addr %d failed, "
				 "error=%s\n", parent->dv_xname, port,
				 addr, usbd_errstr(err)));
#else
			printf("%s: port %d, set config %d at addr %d failed\n",
			    parent->dv_xname, port, confi, addr);
#endif

 			goto fail;
		}
		nifaces = dev->cdesc->bNumInterfaces;
		uaa.configno = dev->cdesc->bConfigurationValue;
		ifaces = mallocarray(nifaces, sizeof(*ifaces), M_USB, M_NOWAIT);
		if (ifaces == NULL) {
			err = USBD_NOMEM;
			goto fail;
		}
		for (i = 0; i < nifaces; i++)
			ifaces[i] = &dev->ifaces[i];
		uaa.ifaces = ifaces;
		uaa.nifaces = nifaces;

		/* add 1 for possible ugen and 1 for NULL terminator */
		dev->subdevs = mallocarray(nifaces + 2, sizeof(dv), M_USB,
		    M_NOWAIT | M_ZERO);
		if (dev->subdevs == NULL) {
			free(ifaces, M_USB, nifaces * sizeof(*ifaces));
			err = USBD_NOMEM;
			goto fail;
		}
		dev->nsubdev = nifaces + 2;

		for (i = 0; i < nifaces; i++) {
			if (usbd_iface_claimed(dev, i))
				continue;
			uaa.iface = ifaces[i];
			uaa.ifaceno = ifaces[i]->idesc->bInterfaceNumber;
			dv = config_found(parent, &uaa, usbd_print);
			if (dv != NULL) {
				dev->subdevs[dev->ndevs++] = dv;
				usbd_claim_iface(dev, i);
			}
		}
		free(ifaces, M_USB, nifaces * sizeof(*ifaces));

		if (dev->ndevs > 0) {
			for (i = 0; i < nifaces; i++) {
				if (!usbd_iface_claimed(dev, i))
					break;
			}
			if (i < nifaces)
				goto generic;
			 else
				goto fail;
		}

		free(dev->subdevs, M_USB, dev->nsubdev * sizeof(*dev->subdevs));
		dev->subdevs = NULL;
		dev->nsubdev = 0;
	}
	/* No interfaces were attached in any of the configurations. */

	if (dd->bNumConfigurations > 1) /* don't change if only 1 config */
		usbd_set_config_index(dev, 0, 0);

	DPRINTF(("%s: no interface drivers found\n", __func__));

generic:
	/* Finally try the generic driver. */
	uaa.iface = NULL;
	uaa.usegeneric = 1;
	uaa.configno = dev->ndevs == 0 ? UHUB_UNK_CONFIGURATION :
	    dev->cdesc->bConfigurationValue;
	uaa.ifaceno = UHUB_UNK_INTERFACE;
	dv = config_found(parent, &uaa, usbd_print);
	if (dv != NULL) {
		if (dev->ndevs == 0) {
			dev->subdevs = mallocarray(2, sizeof dv, M_USB, M_NOWAIT);
			if (dev->subdevs == NULL) {
				err = USBD_NOMEM;
				goto fail;
			}
			dev->nsubdev = 2;
		}
		dev->subdevs[dev->ndevs++] = dv;
		dev->subdevs[dev->ndevs] = 0;
		err = USBD_NORMAL_COMPLETION;
		goto fail;
	}

	/*
	 * The generic attach failed, but leave the device as it is.
	 * We just did not find any drivers, that's all.  The device is
	 * fully operational and not harming anyone.
	 */
	DPRINTF(("%s: generic attach failed\n", __func__));
 	err = USBD_NORMAL_COMPLETION;
fail:
	rw_exit_write(&usbpalock);
	return (err);
}


/*
 * Called when a new device has been put in the powered state,
 * but not yet in the addressed state.
 * Get initial descriptor, set the address, get full descriptor,
 * and attach a driver.
 */
usbd_status
usbd_new_device(struct device *parent, struct usbd_bus *bus, int depth,
		int speed, int port, struct usbd_port *up)
{
	struct usbd_device *dev, *adev, *hub;
	usb_device_descriptor_t *dd;
	usbd_status err;
	uint32_t mps, mps0;
	int addr, i, p;

	DPRINTF(("%s: bus=%p port=%d depth=%d speed=%d\n", __func__,
		 bus, port, depth, speed));

	/*
	 * Fixed size for ep0 max packet, FULL device variable size is
	 * handled below.
	 */
	switch (speed) {
	case USB_SPEED_LOW:
		mps0 = 8;
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
		mps0 = 64;
		break;
	case USB_SPEED_SUPER:
		mps0 = 512;
		break;
	default:
		return (USBD_INVAL);
	}

	addr = usbd_getnewaddr(bus);
	if (addr < 0) {
		printf("%s: No free USB addresses, new device ignored.\n",
		    bus->bdev.dv_xname);
		return (USBD_NO_ADDR);
	}

	dev = malloc(sizeof *dev, M_USB, M_NOWAIT | M_ZERO);
	if (dev == NULL)
		return (USBD_NOMEM);

	dev->bus = bus;

	/* Set up default endpoint handle. */
	dev->def_ep.edesc = &dev->def_ep_desc;

	/* Set up default endpoint descriptor. */
	dev->def_ep_desc.bLength = USB_ENDPOINT_DESCRIPTOR_SIZE;
	dev->def_ep_desc.bDescriptorType = UDESC_ENDPOINT;
	dev->def_ep_desc.bEndpointAddress = USB_CONTROL_ENDPOINT;
	dev->def_ep_desc.bmAttributes = UE_CONTROL;
	dev->def_ep_desc.bInterval = 0;
	USETW(dev->def_ep_desc.wMaxPacketSize, mps0);

	dev->quirks = &usbd_no_quirk;
	dev->address = USB_START_ADDR;
	dev->ddesc.bMaxPacketSize = 0;
	dev->depth = depth;
	dev->powersrc = up;
	dev->myhub = up->parent;
	dev->speed = speed;
	dev->langid = USBD_NOLANG;

	up->device = dev;

	/* Locate port on upstream high speed hub */
	for (adev = dev, hub = up->parent;
	    hub != NULL && hub->speed != USB_SPEED_HIGH;
	    adev = hub, hub = hub->myhub)
		;
	if (hub) {
		for (p = 0; p < hub->hub->nports; p++) {
			if (hub->hub->ports[p].device == adev) {
				dev->myhsport = &hub->hub->ports[p];
				goto found;
			}
		}
		panic("usbd_new_device: cannot find HS port");
	found:
		DPRINTFN(1,("%s: high speed port %d\n", __func__, p));
	} else {
		dev->myhsport = NULL;
	}

	/* Establish the default pipe. */
	err = usbd_setup_pipe(dev, 0, &dev->def_ep, USBD_DEFAULT_INTERVAL,
	    &dev->default_pipe);
	if (err)
		goto fail;

	dd = &dev->ddesc;

	/* Try to get device descriptor */
	/* 
	 * some device will need small size query at first (XXX: out of spec)
	 * we will get full size descriptor later, just determine the maximum
	 * packet size of the control pipe at this moment.
	 */
	for (i = 0; i < 3; i++) {
		/* Get the first 8 bytes of the device descriptor. */
		/* 8 byte is magic size, some device only return 8 byte for 1st
		 * query (XXX: out of spec) */
		err = usbd_get_desc(dev, UDESC_DEVICE, 0, USB_MAX_IPACKET, dd);
		if (!err)
			break;
		if (err == USBD_TIMEOUT)
			goto fail;
		usbd_delay_ms(dev, 100+50*i);
	}

	/* some device need actual size request for the query. try again */
	if (err) {
		USETW(dev->def_ep_desc.wMaxPacketSize,
			USB_DEVICE_DESCRIPTOR_SIZE);
		usbd_reset_port(up->parent, port);
		for (i = 0; i < 3; i++) {
			err = usbd_get_desc(dev, UDESC_DEVICE, 0, 
				USB_DEVICE_DESCRIPTOR_SIZE, dd);
			if (!err)
				break;
			if (err == USBD_TIMEOUT)
				goto fail;
			usbd_delay_ms(dev, 100+50*i);
		}
	}

	/* XXX some devices need more time to wake up */
	if (err) {
		USETW(dev->def_ep_desc.wMaxPacketSize, USB_MAX_IPACKET);
		usbd_reset_port(up->parent, port);
		usbd_delay_ms(dev, 500);
		err = usbd_get_desc(dev, UDESC_DEVICE, 0, 
			USB_MAX_IPACKET, dd);
	}

	if (err)
		goto fail;

	DPRINTF(("%s: adding unit addr=%d, rev=%02x, class=%d, subclass=%d, "
		 "protocol=%d, maxpacket=%d, len=%d, speed=%d\n", __func__,
		 addr,UGETW(dd->bcdUSB), dd->bDeviceClass, dd->bDeviceSubClass,
		 dd->bDeviceProtocol, dd->bMaxPacketSize, dd->bLength,
		 dev->speed));

	if ((dd->bDescriptorType != UDESC_DEVICE) ||
	    (dd->bLength < USB_DEVICE_DESCRIPTOR_SIZE)) {
		err = USBD_INVAL;
		goto fail;
	}

	mps = dd->bMaxPacketSize;
	if (speed == USB_SPEED_SUPER) {
		if (mps == 0xff)
			mps = 9;
		/* xHCI Section 4.8.2.1 */
		mps = (1 << mps);
	}

	if (mps != mps0) {
		if ((speed == USB_SPEED_LOW) ||
		    (mps != 8 && mps != 16 && mps != 32 && mps != 64)) {
			err = USBD_INVAL;
			goto fail;
		}
		USETW(dev->def_ep_desc.wMaxPacketSize, mps);
	}


	/* Set the address if the HC didn't do it already. */
	if (bus->methods->dev_setaddr != NULL &&
	    bus->methods->dev_setaddr(dev, addr)) {
		err = USBD_SET_ADDR_FAILED;
		goto fail;
 	}

	/* Wait for device to settle before reloading the descriptor. */
	usbd_delay_ms(dev, 10);

	/*
	 * If this device is attached to an xHCI controller, this
	 * address does not correspond to the hardware one.
	 */
	dev->address = addr;

	err = usbd_reload_device_desc(dev);
	if (err)
		goto fail;

	/* send disown request to handover 2.0 to 1.1. */
	if (dev->quirks->uq_flags & UQ_EHCI_NEEDTO_DISOWN) {
		/* only effective when the target device is on ehci */
		if (dev->bus->usbrev == USBREV_2_0) {
			DPRINTF(("%s: disown request issues to dev:%p on usb2.0 bus\n",
				__func__, dev));
			usbd_port_disown_to_1_1(dev->myhub, port);
			/* reset_port required to finish disown request */
			usbd_reset_port(dev->myhub, port);
  			return (USBD_NORMAL_COMPLETION);
		}
	}

	/* Assume 100mA bus powered for now. Changed when configured. */
	dev->power = USB_MIN_POWER;
	dev->self_powered = 0;

	DPRINTF(("%s: new dev (addr %d), dev=%p, parent=%p\n", __func__,
		 addr, dev, parent));

	/* Get device info and cache it */
	err = usbd_cache_devinfo(dev);
	if (err)
		goto fail;

	bus->devices[addr] = dev;

	err = usbd_probe_and_attach(parent, dev, port, addr);
	if (err)
		goto fail;

  	return (USBD_NORMAL_COMPLETION);

fail:
	usb_free_device(dev);
	up->device = NULL;
	return (err);
}

usbd_status
usbd_reload_device_desc(struct usbd_device *dev)
{
	usbd_status err;

	/* Get the full device descriptor. */
	err = usbd_get_desc(dev, UDESC_DEVICE, 0,
		USB_DEVICE_DESCRIPTOR_SIZE, &dev->ddesc);
	if (err)
		return (err);

	/* Figure out what's wrong with this device. */
	dev->quirks = usbd_find_quirk(&dev->ddesc);

	return (USBD_NORMAL_COMPLETION);
}

int
usbd_print(void *aux, const char *pnp)
{
	struct usb_attach_arg *uaa = aux;
	char *devinfop;

	devinfop = malloc(DEVINFOSIZE, M_TEMP, M_WAITOK);
	usbd_devinfo(uaa->device, 0, devinfop, DEVINFOSIZE);

	DPRINTFN(15, ("usbd_print dev=%p\n", uaa->device));
	if (pnp) {
		if (!uaa->usegeneric) {
			free(devinfop, M_TEMP, DEVINFOSIZE);
			return (QUIET);
		}
		printf("%s at %s", devinfop, pnp);
	}
	if (uaa->port != 0)
		printf(" port %d", uaa->port);
	if (uaa->configno != UHUB_UNK_CONFIGURATION)
		printf(" configuration %d", uaa->configno);
	if (uaa->ifaceno != UHUB_UNK_INTERFACE)
		printf(" interface %d", uaa->ifaceno);

	if (!pnp)
		printf(" %s\n", devinfop);
	free(devinfop, M_TEMP, DEVINFOSIZE);
	return (UNCONF);
}

void
usbd_fill_deviceinfo(struct usbd_device *dev, struct usb_device_info *di)
{
	struct usbd_port *p;
	int i;

	di->udi_bus = dev->bus->usbctl->dv_unit;
	di->udi_addr = dev->address;
	strlcpy(di->udi_vendor, dev->vendor, sizeof(di->udi_vendor));
	strlcpy(di->udi_product, dev->product, sizeof(di->udi_product));
	usbd_printBCD(di->udi_release, sizeof di->udi_release,
	    UGETW(dev->ddesc.bcdDevice));
	di->udi_vendorNo = UGETW(dev->ddesc.idVendor);
	di->udi_productNo = UGETW(dev->ddesc.idProduct);
	di->udi_releaseNo = UGETW(dev->ddesc.bcdDevice);
	di->udi_class = dev->ddesc.bDeviceClass;
	di->udi_subclass = dev->ddesc.bDeviceSubClass;
	di->udi_protocol = dev->ddesc.bDeviceProtocol;
	di->udi_config = dev->config;
	di->udi_power = dev->self_powered ? 0 : dev->power;
	di->udi_speed = dev->speed;
	di->udi_port = dev->powersrc ? dev->powersrc->portno : 0;

	if (dev->subdevs != NULL) {
		for (i = 0; dev->subdevs[i] && i < USB_MAX_DEVNAMES; i++) {
			strncpy(di->udi_devnames[i],
			    dev->subdevs[i]->dv_xname, USB_MAX_DEVNAMELEN);
			di->udi_devnames[i][USB_MAX_DEVNAMELEN-1] = '\0';
		}
	} else
		i = 0;

	for (/*i is set */; i < USB_MAX_DEVNAMES; i++)
		di->udi_devnames[i][0] = 0; /* empty */

	if (dev->hub) {
		for (i = 0;
		    i < nitems(di->udi_ports) && i < dev->hub->nports; i++) {
			p = &dev->hub->ports[i];
			di->udi_ports[i] = UGETW(p->status.wPortChange) << 16 |
			    UGETW(p->status.wPortStatus);
		}
		di->udi_nports = dev->hub->nports;
	} else
		di->udi_nports = 0;

	bzero(di->udi_serial, sizeof(di->udi_serial));
	if (dev->serial != NULL)
		strlcpy(di->udi_serial, dev->serial,
		    sizeof(di->udi_serial));
}

int
usbd_get_routestring(struct usbd_device *dev, uint32_t *route)
{
	struct usbd_device *hub;
	uint32_t r;
	uint8_t port;

	/*
	 * Calculate the Route String.  Assume that there is no hub with
	 * more than 15 ports and that they all have a depth < 6.  See
	 * section 8.9 of USB 3.1 Specification for more details.
	 */
	r = dev->powersrc ? dev->powersrc->portno : 0;
	for (hub = dev->myhub; hub && hub->depth > 1; hub = hub->myhub) {
		port = hub->powersrc ? hub->powersrc->portno : 0;
		if (port > 15)
			return -1;
		r <<= 4;
		r |= port;
	}

	/* Add in the host root port, of which there may be 255. */
	port = (hub && hub->powersrc) ? hub->powersrc->portno : 0;
	r <<= 8;
	r |= port;

	*route = r;
	return 0;
}

int
usbd_get_location(struct usbd_device *dev, struct usbd_interface *iface,
    uint8_t *bus, uint32_t *route, uint8_t *ifaceno)
{
	int i;
	uint32_t r;

	if (dev == NULL || usbd_is_dying(dev) ||
	    dev->cdesc == NULL ||
	    dev->cdesc->bNumInterfaces == 0 ||
	    dev->bus == NULL ||
	    dev->bus->usbctl == NULL ||
	    dev->myhub == NULL ||
	    dev->powersrc == NULL)
		return -1;

	for(i = 0; i < dev->cdesc->bNumInterfaces; i++) {
		if (iface == &dev->ifaces[i]) {
			*bus = dev->bus->usbctl->dv_unit;
			*route = (usbd_get_routestring(dev, &r)) ? 0 : r;
			*ifaceno = i;
			return 0;
		}
	}

	return -1;
}

/* Retrieve a complete descriptor for a certain device and index. */
usb_config_descriptor_t *
usbd_get_cdesc(struct usbd_device *dev, int index, u_int *lenp)
{
	usb_config_descriptor_t *cdesc, *tdesc, cdescr;
	u_int len;
	usbd_status err;

	if (index == USB_CURRENT_CONFIG_INDEX) {
		tdesc = usbd_get_config_descriptor(dev);
		if (tdesc == NULL)
			return (NULL);
		len = UGETW(tdesc->wTotalLength);
		if (lenp)
			*lenp = len;
		cdesc = malloc(len, M_TEMP, M_WAITOK);
		memcpy(cdesc, tdesc, len);
		DPRINTFN(5,("%s: current, len=%u\n", __func__, len));
	} else {
		err = usbd_get_desc(dev, UDESC_CONFIG, index,
		    USB_CONFIG_DESCRIPTOR_SIZE, &cdescr);
		if (err || cdescr.bDescriptorType != UDESC_CONFIG)
			return (NULL);
		len = UGETW(cdescr.wTotalLength);
		DPRINTFN(5,("%s: index=%d, len=%u\n", __func__, index, len));
		if (lenp)
			*lenp = len;
		cdesc = malloc(len, M_TEMP, M_WAITOK);
		err = usbd_get_desc(dev, UDESC_CONFIG, index, len, cdesc);
		if (err) {
			free(cdesc, M_TEMP, len);
			return (NULL);
		}
	}
	return (cdesc);
}

void
usb_free_device(struct usbd_device *dev)
{
	int ifcidx, nifc;

	DPRINTF(("%s: %p\n", __func__, dev));

	if (dev->default_pipe != NULL)
		usbd_close_pipe(dev->default_pipe);
	if (dev->ifaces != NULL) {
		nifc = dev->cdesc->bNumInterfaces;
		for (ifcidx = 0; ifcidx < nifc; ifcidx++)
			usbd_free_iface_data(dev, ifcidx);
		free(dev->ifaces, M_USB, nifc * sizeof(*dev->ifaces));
	}
	if (dev->cdesc != NULL)
		free(dev->cdesc, M_USB, UGETW(dev->cdesc->wTotalLength));
	free(dev->subdevs, M_USB, dev->nsubdev * sizeof(*dev->subdevs));
	dev->bus->devices[dev->address] = NULL;

	if (dev->vendor != NULL)
		free(dev->vendor, M_USB, USB_MAX_STRING_LEN);
	if (dev->product != NULL)
		free(dev->product, M_USB, USB_MAX_STRING_LEN);
	if (dev->serial != NULL)
		free(dev->serial, M_USB, USB_MAX_STRING_LEN);

	free(dev, M_USB, sizeof *dev);
}

/*
 * Should only be called by the USB thread doing bus exploration to
 * avoid connect/disconnect races.
 */
int
usbd_detach(struct usbd_device *dev, struct device *parent)
{
	int i, rv = 0;

	usbd_deactivate(dev);

	if (dev->ndevs > 0) {
		for (i = 0; dev->subdevs[i] != NULL; i++)
			rv |= config_detach(dev->subdevs[i], DETACH_FORCE);
	}

	if (rv == 0)
		usb_free_device(dev);

	return (rv);
}
