/*	$OpenBSD: print-usbpcap.c,v 1.6 2023/03/13 13:36:56 claudio Exp $ */

/*
 * Copyright (c) 2018 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <dev/usb/usb.h>
#include <dev/usb/usbpcap.h>

#include <pcap.h>

#include "interface.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

const char *usbpcap_xfer_type[] = {"isoc", "intr", "ctrl", "bulk"};
const char *usbpcap_control_stages[] = {"setup", "data", "status"};
const char *usbpcap_request_codes[] = {
	"GET_STATUS", "CLEAR_FEATURE", "?", "SET_FEATURE", "?", "SET_ADDRESS",
	"GET_DESCRIPTOR", "SET_DESCRIPTOR", "GET_CONFIG", "SET_CONFIG",
	"GET_INTERFACE", "SET_INTERFACE", "SYNCH_FRAME",
};

void	 usbpcap_print_descriptor(int);
void	 usbpcap_print_request_type(uByte);

void
usbpcap_if_print(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int caplen = h->caplen;
	const struct usbpcap_pkt_hdr *uph;
	u_int16_t hdrlen;

	ts_print(&h->ts);

	/* set required globals */
	snapend = p + caplen;

	/* check length */
	uph = (struct usbpcap_pkt_hdr *)p;
	TCHECK(uph->uph_hlen);
	hdrlen = letoh16(uph->uph_hlen);
	if (hdrlen < sizeof(*uph)) {
		printf("[usb: invalid header length %u!]", hdrlen);
		goto out;
	}

	TCHECK(uph[0]);

	printf("bus %u %c addr %u: ep%u",
	    letoh16(uph->uph_bus),
	     ((uph->uph_info & USBPCAP_INFO_DIRECTION_IN) ? '<' : '>'),
	    letoh16(uph->uph_devaddr), UE_GET_ADDR(uph->uph_epaddr));

	if (uph->uph_xfertype < nitems(usbpcap_xfer_type))
		printf(" %s", usbpcap_xfer_type[uph->uph_xfertype]);
	else
		printf(" ??");

	printf(" dlen=%u", letoh32(uph->uph_dlen));

	TCHECK2(uph[0], hdrlen);

	if (uph->uph_xfertype == USBPCAP_TRANSFER_CONTROL) {
		struct usbpcap_ctl_hdr *ctl_hdr = (struct usbpcap_ctl_hdr *)p;

		TCHECK(ctl_hdr->uch_stage);
		if (ctl_hdr->uch_stage < nitems(usbpcap_control_stages))
			printf(" stage=%s",
			    usbpcap_control_stages[ctl_hdr->uch_stage]);
		else
			printf(" stage=?");

		if (ctl_hdr->uch_stage == USBPCAP_CONTROL_STAGE_SETUP) {
			usb_device_request_t *req;

			req = (usb_device_request_t *)
			    (p + sizeof(struct usbpcap_ctl_hdr));

			/* Setup packets must be 8 bytes in size as per
			 * 9.3 USB Device Requests. */
			if (letoh32(uph->uph_dlen != 8)) {
				printf("[usb: invalid data length %u!]",
				   letoh32(uph->uph_dlen));
				goto out;
			}
			TCHECK(req[0]);

			usbpcap_print_request_type(req->bmRequestType);

			if (req->bRequest < nitems(usbpcap_request_codes))
				printf(" bRequest=%s",
				    usbpcap_request_codes[req->bRequest]);
			else
				printf(" bRequest=?");

			if (req->bRequest == UR_GET_DESCRIPTOR)
				usbpcap_print_descriptor(UGETW(req->wValue));
			else
				printf(" wValue=0x%04x", UGETW(req->wValue));

			printf(" wIndex=%04x", UGETW(req->wIndex));
			printf(" wLength=%u", UGETW(req->wLength));
		}
	}

	if (xflag) {
		if (eflag)
			default_print(p, caplen);
		else
			default_print(p + hdrlen, caplen - hdrlen);
	}
out:
	putchar('\n');
	return;
trunc:
	printf("[|usb]\n");
}

void
usbpcap_print_descriptor(int value)
{
	printf(" type=");
	switch (value >> 8) {
	case UDESC_DEVICE:
		printf("DEVICE");
		break;
	case UDESC_CONFIG:
		printf("CONFIGURATION");
		break;
	case UDESC_STRING:
		printf("STRING");
		break;
	case UDESC_INTERFACE:
		printf("INTERFACE");
		break;
	case UDESC_ENDPOINT:
		printf("ENDPOINT");
		break;
	case UDESC_DEVICE_QUALIFIER:
		printf("DEVICE_QUALIFIER");
		break;
	case UDESC_OTHER_SPEED_CONFIGURATION:
		printf("OTHER_SPEED_CONFIGURATION");
		break;
	case UDESC_INTERFACE_POWER:
		printf("INTERFACE_POWER");
		break;
	case UDESC_OTG:
		printf("OTG");
		break;
	case UDESC_DEBUG:
		printf("DEBUG");
		break;
	case UDESC_IFACE_ASSOC:
		printf("INTERFACE_ASSOCIATION");
		break;
	case UDESC_BOS:
		printf("BOS");
		break;
	case UDESC_DEVICE_CAPABILITY:
		printf("DEVICE_CAPABILITY");
		break;
	case UDESC_CS_DEVICE:
		printf("CS_DEVICE");
		break;
	case UDESC_CS_CONFIG:
		printf("CS_CONFIGURATION");
		break;
	case UDESC_CS_STRING:
		printf("CS_STRING");
		break;
	case UDESC_CS_INTERFACE:
		printf("CS_INTERFACE");
		break;
	case UDESC_CS_ENDPOINT:
		printf("CS_ENDPOINT");
		break;
	case UDESC_HUB:
		printf("HUB");
		break;
	case UDESC_SS_HUB:
		printf("SS_HUB");
		break;
	case UDESC_ENDPOINT_SS_COMP:
		printf("SS_COMPANION");
		break;
	default:
		printf("?");
	}

	printf(" index=0x%02x", value & 0xff);
}

void
usbpcap_print_request_type(uByte request_type)
{
	printf(" bmRequestType=");

	switch (request_type) {
	case UT_READ_DEVICE:
		printf("UT_READ_DEVICE");
		break;
	case UT_READ_INTERFACE:
		printf("UT_READ_INTERFACE");
		break;
	case UT_READ_ENDPOINT:
		printf("UT_READ_ENDPOINT");
		break;
	case UT_WRITE_DEVICE:
		printf("UT_WRITE_DEVICE");
		break;
	case UT_WRITE_INTERFACE:
		printf("UT_WRITE_INTERFACE");
		break;
	case UT_WRITE_ENDPOINT:
		printf("UT_WRITE_ENDPOINT");
		break;
	case UT_READ_CLASS_DEVICE:
		printf("UT_READ_CLASS_DEVICE");
		break;
	case UT_READ_CLASS_INTERFACE:
		printf("UT_READ_CLASS_INTERFACE");
		break;
	case UT_READ_CLASS_OTHER:
		printf("UT_READ_CLASS_OTHER");
		break;
	case UT_READ_CLASS_ENDPOINT:
		printf("UT_READ_CLASS_ENDPOINT");
		break;
	case UT_WRITE_CLASS_DEVICE:
		printf("UT_WRITE_CLASS_DEVICE");
		break;
	case UT_WRITE_CLASS_INTERFACE:
		printf("UT_WRITE_CLASS_INTERFACE");
		break;
	case UT_WRITE_CLASS_OTHER:
		printf("UT_WRITE_CLASS_OTHER");
		break;
	case UT_WRITE_CLASS_ENDPOINT:
		printf("UT_WRITE_CLASS_ENDPOINT");
		break;
	case UT_READ_VENDOR_DEVICE:
		printf("UT_READ_VENDOR_DEVICE");
		break;
	case UT_READ_VENDOR_INTERFACE:
		printf("UT_READ_VENDOR_INTERFACE");
		break;
	case UT_READ_VENDOR_OTHER:
		printf("UT_READ_VENDOR_OTHER");
		break;
	case UT_READ_VENDOR_ENDPOINT:
		printf("UT_READ_VENDOR_ENDPOINT");
		break;
	case UT_WRITE_VENDOR_DEVICE:
		printf("UT_WRITE_VENDOR_DEVICE");
		break;
	case UT_WRITE_VENDOR_INTERFACE:
		printf("UT_WRITE_VENDOR_INTERFACE");
		break;
	case UT_WRITE_VENDOR_OTHER:
		printf("UT_WRITE_VENDOR_OTHER");
		break;
	case UT_WRITE_VENDOR_ENDPOINT:
		printf("UT_WRITE_VENDOR_ENDPOINT");
		break;
	default:
		printf("?");
	}
}
