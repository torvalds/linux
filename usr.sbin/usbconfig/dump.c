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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <err.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include "dump.h"

#define	DUMP0(n,type,field,...) dump_field(pdev, "  ", #field, n->field);
#define	DUMP1(n,type,field,...) dump_field(pdev, "    ", #field, n->field);
#define	DUMP2(n,type,field,...) dump_field(pdev, "      ", #field, n->field);
#define	DUMP3(n,type,field,...) dump_field(pdev, "        ", #field, n->field);

const char *
dump_mode(uint8_t value)
{
	if (value == LIBUSB20_MODE_HOST)
		return ("HOST");
	return ("DEVICE");
}

const char *
dump_speed(uint8_t value)
{
	;				/* style fix */
	switch (value) {
	case LIBUSB20_SPEED_LOW:
		return ("LOW (1.5Mbps)");
	case LIBUSB20_SPEED_FULL:
		return ("FULL (12Mbps)");
	case LIBUSB20_SPEED_HIGH:
		return ("HIGH (480Mbps)");
	case LIBUSB20_SPEED_VARIABLE:
		return ("VARIABLE (52-480Mbps)");
	case LIBUSB20_SPEED_SUPER:
		return ("SUPER (5.0Gbps)");
	default:
		break;
	}
	return ("UNKNOWN ()");
}

const char *
dump_power_mode(uint8_t value)
{
	;				/* style fix */
	switch (value) {
	case LIBUSB20_POWER_OFF:
		return ("OFF");
	case LIBUSB20_POWER_ON:
		return ("ON");
	case LIBUSB20_POWER_SAVE:
		return ("SAVE");
	case LIBUSB20_POWER_SUSPEND:
		return ("SUSPEND");
	case LIBUSB20_POWER_RESUME:
		return ("RESUME");
	default:
		return ("UNKNOWN");
	}
}

static void
dump_field(struct libusb20_device *pdev, const char *plevel,
    const char *field, uint32_t value)
{
	uint8_t temp_string[256];

	printf("%s%s = 0x%04x ", plevel, field, value);

	if (strlen(plevel) == 8) {
		/* Endpoint Descriptor */

		if (strcmp(field, "bEndpointAddress") == 0) {
			if (value & 0x80)
				printf(" <IN>\n");
			else
				printf(" <OUT>\n");
			return;
		}
		if (strcmp(field, "bmAttributes") == 0) {
			switch (value & 0x03) {
			case 0:
				printf(" <CONTROL>\n");
				break;
			case 1:
				switch (value & 0x0C) {
				case 0x00:
					printf(" <ISOCHRONOUS>\n");
					break;
				case 0x04:
					printf(" <ASYNC-ISOCHRONOUS>\n");
					break;
				case 0x08:
					printf(" <ADAPT-ISOCHRONOUS>\n");
					break;
				default:
					printf(" <SYNC-ISOCHRONOUS>\n");
					break;
				}
				break;
			case 2:
				printf(" <BULK>\n");
				break;
			default:
				printf(" <INTERRUPT>\n");
				break;
			}
			return;
		}
	}
	if ((field[0] == 'i') && (field[1] != 'd')) {
		/* Indirect String Descriptor */
		if (value == 0) {
			printf(" <no string>\n");
			return;
		}
		if (libusb20_dev_req_string_simple_sync(pdev, value,
		    temp_string, sizeof(temp_string))) {
			printf(" <retrieving string failed>\n");
			return;
		}
		printf(" <%s>\n", temp_string);
		return;
	}
	if (strlen(plevel) == 2 || strlen(plevel) == 6) {

		/* Device and Interface Descriptor class codes */

		if (strcmp(field, "bInterfaceClass") == 0 ||
		    strcmp(field, "bDeviceClass") == 0) {

			switch (value) {
			case 0x00:
				printf(" <Probed by interface class>\n");
				break;
			case 0x01:
				printf(" <Audio device>\n");
				break;
			case 0x02:
				printf(" <Communication device>\n");
				break;
			case 0x03:
				printf(" <HID device>\n");
				break;
			case 0x05:
				printf(" <Physical device>\n");
				break;
			case 0x06:
				printf(" <Still imaging>\n");
				break;
			case 0x07:
				printf(" <Printer device>\n");
				break;
			case 0x08:
				printf(" <Mass storage>\n");
				break;
			case 0x09:
				printf(" <HUB>\n");
				break;
			case 0x0A:
				printf(" <CDC-data>\n");
				break;
			case 0x0B:
				printf(" <Smart card>\n");
				break;
			case 0x0D:
				printf(" <Content security>\n");
				break;
			case 0x0E:
				printf(" <Video device>\n");
				break;
			case 0x0F:
				printf(" <Personal healthcare>\n");
				break;
			case 0x10:
				printf(" <Audio and video device>\n");
				break;
			case 0x11:
				printf(" <Billboard device>\n");
				break;
			case 0xDC:
				printf(" <Diagnostic device>\n");
				break;
			case 0xE0:
				printf(" <Wireless controller>\n");
				break;
			case 0xEF:
				printf(" <Miscellaneous device>\n");
				break;
			case 0xFE:
				printf(" <Application specific>\n");
				break;
			case 0xFF:
				printf(" <Vendor specific>\n");
				break;
			default:
				printf(" <Unknown>\n");
				break;
			}
			return;
		}
	}
	/* No additional information */
	printf("\n");
}

static void
dump_extra(struct libusb20_me_struct *str, const char *plevel)
{
	const uint8_t *ptr;
	uint8_t x;

	ptr = NULL;

	while ((ptr = libusb20_desc_foreach(str, ptr))) {
		printf("\n" "%sAdditional Descriptor\n\n", plevel);
		printf("%sbLength = 0x%02x\n", plevel, ptr[0]);
		printf("%sbDescriptorType = 0x%02x\n", plevel, ptr[1]);
		if (ptr[0] > 1)
			printf("%sbDescriptorSubType = 0x%02x\n",
			    plevel, ptr[2]);
		printf("%s RAW dump: ", plevel);
		for (x = 0; x != ptr[0]; x++) {
			if ((x % 8) == 0) {
				printf("\n%s 0x%02x | ", plevel, x);
			}
			printf("0x%02x%s", ptr[x],
			    (x != (ptr[0] - 1)) ? ", " : (x % 8) ? "\n" : "");
		}
		printf("\n");
	}
	return;
}

static void
dump_endpoint(struct libusb20_device *pdev,
    struct libusb20_endpoint *ep)
{
	struct LIBUSB20_ENDPOINT_DESC_DECODED *edesc;

	edesc = &ep->desc;
	LIBUSB20_ENDPOINT_DESC(DUMP3, edesc);
	dump_extra(&ep->extra, "  " "  " "  ");
	return;
}

static void
dump_iface(struct libusb20_device *pdev,
    struct libusb20_interface *iface)
{
	struct LIBUSB20_INTERFACE_DESC_DECODED *idesc;
	uint8_t z;

	idesc = &iface->desc;
	LIBUSB20_INTERFACE_DESC(DUMP2, idesc);
	dump_extra(&iface->extra, "  " "  " "  ");

	for (z = 0; z != iface->num_endpoints; z++) {
		printf("\n     Endpoint %u\n", z);
		dump_endpoint(pdev, iface->endpoints + z);
	}
	return;
}

void
dump_device_info(struct libusb20_device *pdev, uint8_t show_ifdrv)
{
	char buf[128];
	uint8_t n;
	unsigned int usage;

	usage = libusb20_dev_get_power_usage(pdev);

	printf("%s, cfg=%u md=%s spd=%s pwr=%s (%umA)\n",
	    libusb20_dev_get_desc(pdev),
	    libusb20_dev_get_config_index(pdev),
	    dump_mode(libusb20_dev_get_mode(pdev)),
	    dump_speed(libusb20_dev_get_speed(pdev)),
	    dump_power_mode(libusb20_dev_get_power_mode(pdev)),
	    usage);

	if (!show_ifdrv)
		return;

	for (n = 0; n != 255; n++) {
		if (libusb20_dev_get_iface_desc(pdev, n, buf, sizeof(buf)))
			break;
		if (buf[0] == 0)
			continue;
		printf("ugen%u.%u.%u: %s\n",
		    libusb20_dev_get_bus_number(pdev),
		    libusb20_dev_get_address(pdev), n, buf);
	}
}

void
dump_be_quirk_names(struct libusb20_backend *pbe)
{
	struct libusb20_quirk q;
	uint16_t x;
	int error;

	memset(&q, 0, sizeof(q));

	printf("\nDumping list of supported quirks:\n\n");

	for (x = 0; x != 0xFFFF; x++) {

		error = libusb20_be_get_quirk_name(pbe, x, &q);
		if (error) {
			if (x == 0) {
				printf("No quirk names - maybe the USB quirk "
				    "module has not been loaded.\n");
			}
			break;
		}
		if (strcmp(q.quirkname, "UQ_NONE"))
			printf("%s\n", q.quirkname);
	}
	printf("\n");
	return;
}

void
dump_be_dev_quirks(struct libusb20_backend *pbe)
{
	struct libusb20_quirk q;
	uint16_t x;
	int error;

	memset(&q, 0, sizeof(q));

	printf("\nDumping current device quirks:\n\n");

	for (x = 0; x != 0xFFFF; x++) {

		error = libusb20_be_get_dev_quirk(pbe, x, &q);
		if (error) {
			if (x == 0) {
				printf("No device quirks - maybe the USB quirk "
				    "module has not been loaded.\n");
			}
			break;
		}
		if (strcmp(q.quirkname, "UQ_NONE")) {
			printf("VID=0x%04x PID=0x%04x REVLO=0x%04x "
			    "REVHI=0x%04x QUIRK=%s\n",
			    q.vid, q.pid, q.bcdDeviceLow,
			    q.bcdDeviceHigh, q.quirkname);
		}
	}
	printf("\n");
	return;
}

void
dump_device_desc(struct libusb20_device *pdev)
{
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;

	ddesc = libusb20_dev_get_device_desc(pdev);
	LIBUSB20_DEVICE_DESC(DUMP0, ddesc);
	return;
}

void
dump_config(struct libusb20_device *pdev, uint8_t all_cfg)
{
	struct LIBUSB20_CONFIG_DESC_DECODED *cdesc;
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;
	struct libusb20_config *pcfg = NULL;
	uint8_t cfg_index;
	uint8_t cfg_index_end;
	uint8_t x;
	uint8_t y;

	ddesc = libusb20_dev_get_device_desc(pdev);

	if (all_cfg) {
		cfg_index = 0;
		cfg_index_end = ddesc->bNumConfigurations;
	} else {
		cfg_index = libusb20_dev_get_config_index(pdev);
		cfg_index_end = cfg_index + 1;
	}

	for (; cfg_index != cfg_index_end; cfg_index++) {

		pcfg = libusb20_dev_alloc_config(pdev, cfg_index);
		if (!pcfg) {
			continue;
		}
		printf("\n Configuration index %u\n\n", cfg_index);
		cdesc = &(pcfg->desc);
		LIBUSB20_CONFIG_DESC(DUMP1, cdesc);
		dump_extra(&(pcfg->extra), "  " "  ");

		for (x = 0; x != pcfg->num_interface; x++) {
			printf("\n    Interface %u\n", x);
			dump_iface(pdev, pcfg->interface + x);
			printf("\n");
			for (y = 0; y != (pcfg->interface + x)->num_altsetting; y++) {
				printf("\n    Interface %u Alt %u\n", x, y + 1);
				dump_iface(pdev,
				    (pcfg->interface + x)->altsetting + y);
				printf("\n");
			}
		}
		printf("\n");
		free(pcfg);
	}
	return;
}

void
dump_string_by_index(struct libusb20_device *pdev, uint8_t str_index)
{
	char *pbuf;
	uint8_t n;
	uint8_t len;

	pbuf = malloc(256);
	if (pbuf == NULL)
		err(1, "out of memory");

	if (str_index == 0) {
		/* language table */
		if (libusb20_dev_req_string_sync(pdev,
		    str_index, 0, pbuf, 256)) {
			printf("STRING_0x%02x = <read error>\n", str_index);
		} else {
			printf("STRING_0x%02x = ", str_index);
			len = (uint8_t)pbuf[0];
			for (n = 0; n != len; n++) {
				printf("0x%02x%s", (uint8_t)pbuf[n],
				    (n != (len - 1)) ? ", " : "");
			}
			printf("\n");
		}
	} else {
		/* ordinary string */
		if (libusb20_dev_req_string_simple_sync(pdev,
		    str_index, pbuf, 256)) {
			printf("STRING_0x%02x = <read error>\n", str_index);
		} else {
			printf("STRING_0x%02x = <%s>\n", str_index, pbuf);
		}
	}
	free(pbuf);
}
