/*-
 * hid.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: hid.c,v 1.3 2004/02/17 22:14:57 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <stdio.h>
#include <string.h>
#include <usbhid.h>
#include "bthid_config.h"
#include "bthidcontrol.h"

extern uint32_t verbose;

static void hid_dump_descriptor	(report_desc_t r);
static void hid_dump_item	(char const *label, struct hid_item *h);

static int
hid_dump(bdaddr_t *bdaddr, int argc, char **argv)
{
	struct hid_device	*hd = NULL;
	int			 e = FAILED;

	if (read_config_file() == 0) {
		if ((hd = get_hid_device(bdaddr)) != NULL) {
			hid_dump_descriptor(hd->desc);
			e = OK;
		} 

		clean_config();
	}

	return (e);
}

static int
hid_forget(bdaddr_t *bdaddr, int argc, char **argv)
{
	struct hid_device	*hd = NULL;
	int			 e = FAILED;

	if (read_config_file() == 0) {
		if (read_hids_file() == 0) {
			if ((hd = get_hid_device(bdaddr)) != NULL) {
				hd->new_device = 1;
				if (write_hids_file() == 0)
					e = OK;
			}
		}

		clean_config();
	}

	return (e);
}

static int
hid_known(bdaddr_t *bdaddr, int argc, char **argv)
{
	struct hid_device	*hd = NULL;
	struct hostent		*he = NULL;
	int			 e = FAILED;

	if (read_config_file() == 0) {
		if (read_hids_file() == 0) {
			e = OK;

			for (hd = get_next_hid_device(hd);
			     hd != NULL;
			     hd = get_next_hid_device(hd)) {
				if (hd->new_device)
					continue;

				he = bt_gethostbyaddr((char *) &hd->bdaddr,
						sizeof(hd->bdaddr),
						AF_BLUETOOTH);

				fprintf(stdout,
"%s %s\n",				bt_ntoa(&hd->bdaddr, NULL),
					(he != NULL && he->h_name != NULL)?
						he->h_name : "");
			}
		}

		clean_config();
	}

	return (e);
}

static void
hid_dump_descriptor(report_desc_t r)
{
	struct hid_data	*d = NULL;
	struct hid_item	 h;

	for (d = hid_start_parse(r, ~0, -1); hid_get_item(d, &h); ) {
		switch (h.kind) {
		case hid_collection:
			fprintf(stdout,
"Collection page=%s usage=%s\n", hid_usage_page(HID_PAGE(h.usage)),
				 hid_usage_in_page(h.usage));
			break;

		case hid_endcollection:
			fprintf(stdout, "End collection\n");
			break;

		case hid_input:
			hid_dump_item("Input  ", &h);
			break;

		case hid_output:
			hid_dump_item("Output ", &h);
			break;

		case hid_feature:
			hid_dump_item("Feature", &h);
			break;
		}
	}

	hid_end_parse(d);
}

static void
hid_dump_item(char const *label, struct hid_item *h)
{
	if ((h->flags & HIO_CONST) && !verbose)
		return;

	fprintf(stdout,
"%s id=%u size=%u count=%u page=%s usage=%s%s%s%s%s%s%s%s%s%s",
		label, (uint8_t) h->report_ID, h->report_size, h->report_count,
		hid_usage_page(HID_PAGE(h->usage)),
		hid_usage_in_page(h->usage),
		h->flags & HIO_CONST ? " Const" : "",
		h->flags & HIO_VARIABLE ? " Variable" : "",
		h->flags & HIO_RELATIVE ? " Relative" : "",
		h->flags & HIO_WRAP ? " Wrap" : "",
		h->flags & HIO_NONLINEAR ? " NonLinear" : "",
		h->flags & HIO_NOPREF ? " NoPref" : "",
		h->flags & HIO_NULLSTATE ? " NullState" : "",
		h->flags & HIO_VOLATILE ? " Volatile" : "",
		h->flags & HIO_BUFBYTES ? " BufBytes" : "");

	fprintf(stdout,
", logical range %d..%d",
		h->logical_minimum, h->logical_maximum);

	if (h->physical_minimum != h->physical_maximum)
		fprintf(stdout,
", physical range %d..%d",
			h->physical_minimum, h->physical_maximum);

	if (h->unit)
		fprintf(stdout,
", unit=0x%02x exp=%d", h->unit, h->unit_exponent);

	fprintf(stdout, "\n");
}

struct bthid_command	hid_commands[] = {
{
"Dump",
"Dump HID descriptor for the specified device in human readable form. The\n" \
"device must have an entry in the Bluetooth HID daemon configuration file.\n",
hid_dump
},
{
"Known",
"List all known to the Bluetooth HID daemon devices.\n",
hid_known
},
{
"Forget",
"Forget (mark as new) specified HID device. This command is useful when it\n" \
"is required to remove device from the known HIDs file. This should be done\n" \
"when reset button was pressed on the device or the battery was changed. The\n"\
"Bluetooth HID daemon should be restarted.\n",
hid_forget
},
{ NULL, NULL, NULL }
};

