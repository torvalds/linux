/*-
 * sdp.c
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
 * $Id: sdp.c,v 1.3 2004/02/17 22:14:57 max Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <errno.h>
#include <sdp.h>
#include <stdio.h>
#include <string.h>
#include <usbhid.h>
#include "bthid_config.h"
#include "bthidcontrol.h"

static int32_t hid_sdp_query				(bdaddr_t const *local, struct hid_device *hd, int32_t *error);
static int32_t hid_sdp_parse_protocol_descriptor_list	(sdp_attr_p a);
static int32_t hid_sdp_parse_hid_descriptor		(sdp_attr_p a);
static int32_t hid_sdp_parse_boolean			(sdp_attr_p a);

/*
 * Hard coded attibute IDs taken from the
 * DEVICE IDENTIFICATION PROFILE SPECIFICATION V13 p.12
 */

#define SDP_ATTR_DEVICE_ID_SERVICE_VENDORID  0x0201
#define SDP_ATTR_DEVICE_ID_SERVICE_PRODUCTID 0x0202
#define SDP_ATTR_DEVICE_ID_SERVICE_VERSION   0x0203
#define SDP_ATTR_DEVICE_ID_RANGE SDP_ATTR_RANGE( \
 SDP_ATTR_DEVICE_ID_SERVICE_VENDORID, SDP_ATTR_DEVICE_ID_SERVICE_VERSION )

static uint16_t		service = SDP_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE;
static uint16_t		service_devid = SDP_SERVICE_CLASS_PNP_INFORMATION;
static uint32_t 	attrs_devid   = SDP_ATTR_DEVICE_ID_RANGE;

static uint32_t		attrs[] = {
SDP_ATTR_RANGE(	SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
		SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST),
SDP_ATTR_RANGE	(SDP_ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS,
		SDP_ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS),
SDP_ATTR_RANGE(	0x0205,		/* HIDReconnectInitiate */
		0x0205),
SDP_ATTR_RANGE(	0x0206,		/* HIDDescriptorList */
		0x0206),
SDP_ATTR_RANGE(	0x0209,		/* HIDBatteryPower */
		0x0209),
SDP_ATTR_RANGE(	0x020d,		/* HIDNormallyConnectable */
		0x020d)
	};
#define	nattrs	(sizeof(attrs)/sizeof(attrs[0]))

static sdp_attr_t	values[8];
#define	nvalues	(sizeof(values)/sizeof(values[0]))

static uint8_t		buffer[nvalues][512];

/*
 * Query remote device
 */

#undef	hid_sdp_query_exit
#define	hid_sdp_query_exit(e) {		\
	if (error != NULL)		\
		*error = (e);		\
	if (ss != NULL) {		\
		sdp_close(ss);		\
		ss = NULL;		\
	}				\
	return (((e) == 0)? 0 : -1);	\
}

static void
hid_init_return_values() {
	int i;
	for (i = 0; i < nvalues; i ++) {
		values[i].flags = SDP_ATTR_INVALID;
		values[i].attr = 0;
		values[i].vlen = sizeof(buffer[i]);
		values[i].value = buffer[i];
	}
}

static int32_t
hid_sdp_query(bdaddr_t const *local, struct hid_device *hd, int32_t *error)
{
	void			*ss = NULL;
	uint8_t			*hid_descriptor = NULL, *v;
	int32_t			 i, control_psm = -1, interrupt_psm = -1,
				 reconnect_initiate = -1,
				 normally_connectable = 0, battery_power = 0,
				 hid_descriptor_length = -1, type;
	int16_t 		 vendor_id = 0, product_id = 0, version = 0;
	bdaddr_t		 sdp_local;
	char			 devname[HCI_DEVNAME_SIZE];

	if (local == NULL)
		local = NG_HCI_BDADDR_ANY;
	if (hd == NULL)
		hid_sdp_query_exit(EINVAL);

	hid_init_return_values();

	if ((ss = sdp_open(local, &hd->bdaddr)) == NULL)
		hid_sdp_query_exit(ENOMEM);
	if (sdp_error(ss) != 0)
		hid_sdp_query_exit(sdp_error(ss));
	if (sdp_search(ss, 1, &service, nattrs, attrs, nvalues, values) != 0)
                hid_sdp_query_exit(sdp_error(ss));

	for (i = 0; i < nvalues; i ++) {
		if (values[i].flags != SDP_ATTR_OK)
			continue;

		switch (values[i].attr) {
		case SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST:
			control_psm = hid_sdp_parse_protocol_descriptor_list(&values[i]);
			break;

		case SDP_ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS:
			interrupt_psm = hid_sdp_parse_protocol_descriptor_list(&values[i]);
			break;

		case 0x0205: /* HIDReconnectInitiate */
			reconnect_initiate = hid_sdp_parse_boolean(&values[i]);
			break;

		case 0x0206: /* HIDDescriptorList */
			if (hid_sdp_parse_hid_descriptor(&values[i]) == 0) {
				hid_descriptor = values[i].value;
				hid_descriptor_length = values[i].vlen;
			}
			break;

		case 0x0209: /* HIDBatteryPower */
			battery_power = hid_sdp_parse_boolean(&values[i]);
			break;

		case 0x020d: /* HIDNormallyConnectable */
			normally_connectable = hid_sdp_parse_boolean(&values[i]);
			break;
		}
	}

	hid_init_return_values();

	if (sdp_search(ss, 1, &service_devid, 1, &attrs_devid, nvalues, values) != 0)
                hid_sdp_query_exit(sdp_error(ss));

	/* Try extract HCI bdaddr from opened SDP session */
	if (sdp_get_lcaddr(ss, &sdp_local) != 0 ||
	    bt_devname(devname, &sdp_local) == 0)
		hid_sdp_query_exit(ENOATTR);

        sdp_close(ss);
        ss = NULL;

	/* If search is successful, scan through return vals */
	for (i = 0; i < 3; i ++ ) {
		if (values[i].flags == SDP_ATTR_INVALID )
			continue;

		/* Expecting tag + uint16_t on all 3 attributes */
		if (values[i].vlen != 3)
			continue;

		/* Make sure, we're reading a uint16_t */
		v = values[i].value;
		SDP_GET8(type, v);
		if (type != SDP_DATA_UINT16 )
			continue;

		switch (values[i].attr) {
			case SDP_ATTR_DEVICE_ID_SERVICE_VENDORID:
				SDP_GET16(vendor_id, v);
				break;
			case SDP_ATTR_DEVICE_ID_SERVICE_PRODUCTID:
				SDP_GET16(product_id, v);
				break;
			case SDP_ATTR_DEVICE_ID_SERVICE_VERSION:
				SDP_GET16(version, v);
				break;
			default:
				break;
		}
	}

	if (control_psm == -1 || interrupt_psm == -1 ||
	    reconnect_initiate == -1 ||
	    hid_descriptor == NULL || hid_descriptor_length == -1)
		hid_sdp_query_exit(ENOATTR);
	hd->name = bt_devremote_name_gen(devname, &hd->bdaddr);
	hd->vendor_id = vendor_id;
	hd->product_id = product_id;
	hd->version = version;
	hd->control_psm = control_psm;
	hd->interrupt_psm = interrupt_psm;
	hd->reconnect_initiate = reconnect_initiate? 1 : 0;
	hd->battery_power = battery_power? 1 : 0;
	hd->normally_connectable = normally_connectable? 1 : 0;
	hd->desc = hid_use_report_desc(hid_descriptor, hid_descriptor_length);
	if (hd->desc == NULL)
		hid_sdp_query_exit(ENOMEM);

	return (0);
}

/*
 * seq len				2
 *	seq len				2
 *		uuid value		3
 *		uint16 value		3
 *		seq len			2
 *			uuid value	3
 */

static int32_t
hid_sdp_parse_protocol_descriptor_list(sdp_attr_p a)
{
	uint8_t	*ptr = a->value;
	uint8_t	*end = a->value + a->vlen;
	int32_t	 type, len, uuid, psm;

	if (end - ptr < 15)
		return (-1);

	if (a->attr == SDP_ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS) {
		SDP_GET8(type, ptr);
		switch (type) {
		case SDP_DATA_SEQ8:
			SDP_GET8(len, ptr);
			break;

		case SDP_DATA_SEQ16:
			SDP_GET16(len, ptr);
			break;

		case SDP_DATA_SEQ32:
			SDP_GET32(len, ptr);
			break;

		default:
			return (-1);
		}
		if (ptr + len > end)
			return (-1);
	}

	SDP_GET8(type, ptr);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, ptr);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, ptr);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, ptr);
		break;

	default:
		return (-1);
	}
	if (ptr + len > end)
		return (-1);

	/* Protocol */
	SDP_GET8(type, ptr);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, ptr);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, ptr);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, ptr);
		break;

	default:
		return (-1);
	}
	if (ptr + len > end)
		return (-1);

	/* UUID */
	if (ptr + 3 > end)
		return (-1);
	SDP_GET8(type, ptr);
	switch (type) {
	case SDP_DATA_UUID16:
		SDP_GET16(uuid, ptr);
		if (uuid != SDP_UUID_PROTOCOL_L2CAP)
			return (-1);
		break;

	case SDP_DATA_UUID32:  /* XXX FIXME can we have 32-bit UUID */
	case SDP_DATA_UUID128: /* XXX FIXME can we have 128-bit UUID */
	default:
		return (-1);
	}

	/* PSM */
	if (ptr + 3 > end)
		return (-1);
	SDP_GET8(type, ptr);
	if (type != SDP_DATA_UINT16)
		return (-1);
	SDP_GET16(psm, ptr);

	return (psm);
}

/*
 * seq len			2
 *	seq len			2
 *		uint8 value8	2
 * 		str value	3
 */

static int32_t
hid_sdp_parse_hid_descriptor(sdp_attr_p a)
{
	uint8_t	*ptr = a->value;
	uint8_t	*end = a->value + a->vlen;
	int32_t	 type, len, descriptor_type;

	if (end - ptr < 9)
		return (-1);

	SDP_GET8(type, ptr);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, ptr);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, ptr);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, ptr);
		break;

	default:
		return (-1);
	}
	if (ptr + len > end)
		return (-1);

	while (ptr < end) {
		/* Descriptor */
		SDP_GET8(type, ptr);
		switch (type) {
		case SDP_DATA_SEQ8:
			if (ptr + 1 > end)
				return (-1);
			SDP_GET8(len, ptr);
			break;

		case SDP_DATA_SEQ16:
			if (ptr + 2 > end)
				return (-1);
			SDP_GET16(len, ptr);
			break;

		case SDP_DATA_SEQ32:
			if (ptr + 4 > end)
				return (-1);
			SDP_GET32(len, ptr);
			break;

		default:
			return (-1);
		}

		/* Descripor type */
		if (ptr + 1 > end)
			return (-1);
		SDP_GET8(type, ptr);
		if (type != SDP_DATA_UINT8 || ptr + 1 > end)
			return (-1);
		SDP_GET8(descriptor_type, ptr);

		/* Descriptor value */
		if (ptr + 1 > end)
			return (-1);
		SDP_GET8(type, ptr);
		switch (type) {
		case SDP_DATA_STR8:
			if (ptr + 1 > end)
				return (-1);
			SDP_GET8(len, ptr);
			break;

		case SDP_DATA_STR16:
			if (ptr + 2 > end)
				return (-1);
			SDP_GET16(len, ptr);
			break;

		case SDP_DATA_STR32:
			if (ptr + 4 > end)
				return (-1);
			SDP_GET32(len, ptr);
			break;

		default:
			return (-1);
		}
		if (ptr + len > end)
			return (-1);

		if (descriptor_type == UDESC_REPORT && len > 0) {
			a->value = ptr;
			a->vlen = len;

			return (0);
		}

		ptr += len;
	}

	return (-1);
}

/* bool8 int8 */
static int32_t
hid_sdp_parse_boolean(sdp_attr_p a)
{
	if (a->vlen != 2 || a->value[0] != SDP_DATA_BOOL)
		return (-1);

	return (a->value[1]);
}

/* Perform SDP query */
static int32_t
hid_query(bdaddr_t *bdaddr, int argc, char **argv)
{
	struct hid_device	hd;
	int			e;

	memcpy(&hd.bdaddr, bdaddr, sizeof(hd.bdaddr));
	if (hid_sdp_query(NULL, &hd, &e) < 0) {
		fprintf(stderr, "Could not perform SDP query on the " \
			"device %s. %s (%d)\n", bt_ntoa(bdaddr, NULL),
			strerror(e), e);
		return (FAILED);
	}

	print_hid_device(&hd, stdout);

	return (OK);
}

struct bthid_command	sdp_commands[] =
{
{
"Query",
"Perform SDP query to the specified device and print HID configuration entry\n"\
"for the device. The configuration entry should be appended to the Bluetooth\n"\
"HID daemon configuration file and the daemon should be restarted.\n",
hid_query
},
{ NULL, NULL, NULL }
};

