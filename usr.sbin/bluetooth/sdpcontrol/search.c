/*-
 * search.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: search.c,v 1.2 2003/09/08 17:35:15 max Exp $
 * $FreeBSD$
 */

#include <netinet/in.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <ctype.h>
#include <sdp.h>
#include <stdio.h>
#include <stdlib.h>
#include "sdpcontrol.h"

/* List of the attributes we are looking for */
static uint32_t	attrs[] =
{
	SDP_ATTR_RANGE(	SDP_ATTR_SERVICE_RECORD_HANDLE,
			SDP_ATTR_SERVICE_RECORD_HANDLE),
	SDP_ATTR_RANGE(	SDP_ATTR_SERVICE_CLASS_ID_LIST,
			SDP_ATTR_SERVICE_CLASS_ID_LIST),
	SDP_ATTR_RANGE(	SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
			SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST),
	SDP_ATTR_RANGE(	SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
			SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST)
};
#define attrs_len	(sizeof(attrs)/sizeof(attrs[0]))

/* Buffer for the attributes */
#define NRECS	25	/* request this much records from the SDP server */
#define	BSIZE	256	/* one attribute buffer size */
static uint8_t		buffer[NRECS * attrs_len][BSIZE];

/* SDP attributes */
static sdp_attr_t	values[NRECS * attrs_len];
#define values_len	(sizeof(values)/sizeof(values[0]))

/*
 * Print Service Class ID List
 *
 * The ServiceClassIDList attribute consists of a data element sequence in 
 * which each data element is a UUID representing the service classes that
 * a given service record conforms to. The UUIDs are listed in order from 
 * the most specific class to the most general class. The ServiceClassIDList
 * must contain at least one service class UUID.
 */

static void
print_service_class_id_list(uint8_t const *start, uint8_t const *end)
{
	uint32_t	type, len, value;

	if (end - start < 2) {
		fprintf(stderr, "Invalid Service Class ID List. " \
				"Too short, len=%zd\n", end - start);
		return;
	}

	SDP_GET8(type, start);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, start);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, start);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, start);
		break;

	default:
		fprintf(stderr, "Invalid Service Class ID List. " \
				"Not a sequence, type=%#x\n", type);
		return;
		/* NOT REACHED */
	}

	if (len > (end - start)) {
		fprintf(stderr, "Invalid Service Class ID List. " \
				"Too long len=%d\n", len);
		return;
	}

	while (start < end) {
		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_UUID16:
			SDP_GET16(value, start);
			fprintf(stdout, "\t%s (%#4.4x)\n",
					sdp_uuid2desc(value), value);
			break;

		case SDP_DATA_UUID32:
			SDP_GET32(value, start);
			fprintf(stdout, "\t%#8.8x\n", value);
			break;

		case SDP_DATA_UUID128: {
			int128_t	uuid;

			SDP_GET_UUID128(&uuid, start);
			fprintf(stdout, "\t%#8.8x-%4.4x-%4.4x-%4.4x-%4.4x%8.8x\n",
					ntohl(*(uint32_t *)&uuid.b[0]),
					ntohs(*(uint16_t *)&uuid.b[4]),
					ntohs(*(uint16_t *)&uuid.b[6]),
					ntohs(*(uint16_t *)&uuid.b[8]),
					ntohs(*(uint16_t *)&uuid.b[10]),
					ntohl(*(uint32_t *)&uuid.b[12]));
			} break;

		default:
			fprintf(stderr, "Invalid Service Class ID List. " \
					"Not a UUID, type=%#x\n", type);
			return;
			/* NOT REACHED */
		}
	}
} /* print_service_class_id_list */

/*
 * Print Protocol Descriptor List
 *
 * If the ProtocolDescriptorList describes a single stack, it takes the form 
 * of a data element sequence in which each element of the sequence is a 
 * protocol descriptor. Each protocol descriptor is, in turn, a data element 
 * sequence whose first element is a UUID identifying the protocol and whose 
 * successive elements are protocol-specific parameters. The protocol 
 * descriptors are listed in order from the lowest layer protocol to the 
 * highest layer protocol used to gain access to the service. If it is possible
 * for more than one kind of protocol stack to be used to gain access to the 
 * service, the ProtocolDescriptorList takes the form of a data element 
 * alternative where each member is a data element sequence as described above.
 */

static void
print_protocol_descriptor(uint8_t const *start, uint8_t const *end)
{
	union {
		uint8_t		uint8;
		uint16_t	uint16;
		uint32_t	uint32;
		uint64_t	uint64;
		int128_t	int128;
	}			value;
	uint32_t		type, len, param;

	/* Get Protocol UUID */
	SDP_GET8(type, start);
	switch (type) {
	case SDP_DATA_UUID16:
		SDP_GET16(value.uint16, start);
		fprintf(stdout, "\t%s (%#4.4x)\n", sdp_uuid2desc(value.uint16),
				value.uint16);
		break;

	case SDP_DATA_UUID32:
		SDP_GET32(value.uint32, start);
		fprintf(stdout, "\t%#8.8x\n", value.uint32);
		break;

	case SDP_DATA_UUID128:
		SDP_GET_UUID128(&value.int128, start);
		fprintf(stdout, "\t%#8.8x-%4.4x-%4.4x-%4.4x-%4.4x%8.8x\n",
				ntohl(*(uint32_t *)&value.int128.b[0]),
				ntohs(*(uint16_t *)&value.int128.b[4]),
				ntohs(*(uint16_t *)&value.int128.b[6]),
				ntohs(*(uint16_t *)&value.int128.b[8]),
				ntohs(*(uint16_t *)&value.int128.b[10]),
				ntohl(*(uint32_t *)&value.int128.b[12]));
		break;

	default:
		fprintf(stderr, "Invalid Protocol Descriptor. " \
				"Not a UUID, type=%#x\n", type);
		return;
		/* NOT REACHED */
	}

	/* Protocol specific parameters */
	for (param = 1; start < end; param ++) {
		fprintf(stdout, "\t\tProtocol specific parameter #%d: ", param);

		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_NIL:
			fprintf(stdout, "nil\n");
			break;

		case SDP_DATA_UINT8:
		case SDP_DATA_INT8:
		case SDP_DATA_BOOL:
			SDP_GET8(value.uint8, start);
			fprintf(stdout, "u/int8/bool %u\n", value.uint8);
			break;

		case SDP_DATA_UINT16:
		case SDP_DATA_INT16:
		case SDP_DATA_UUID16:
			SDP_GET16(value.uint16, start);
			fprintf(stdout, "u/int/uuid16 %u\n", value.uint16);
			break;

		case SDP_DATA_UINT32:
		case SDP_DATA_INT32:
		case SDP_DATA_UUID32:
			SDP_GET32(value.uint32, start);
			fprintf(stdout, "u/int/uuid32 %u\n", value.uint32);
			break;

		case SDP_DATA_UINT64:
		case SDP_DATA_INT64:
			SDP_GET64(value.uint64, start);
			fprintf(stdout, "u/int64 %ju\n", value.uint64);
			break;

		case SDP_DATA_UINT128:
		case SDP_DATA_INT128:
			SDP_GET128(&value.int128, start);
			fprintf(stdout, "u/int128 %#8.8x%8.8x%8.8x%8.8x\n",
				*(uint32_t *)&value.int128.b[0],
				*(uint32_t *)&value.int128.b[4],
				*(uint32_t *)&value.int128.b[8],
				*(uint32_t *)&value.int128.b[12]);
			break;

		case SDP_DATA_UUID128:
			SDP_GET_UUID128(&value.int128, start);
			fprintf(stdout, "uuid128 %#8.8x-%4.4x-%4.4x-%4.4x-%4.4x%8.8x\n",
				ntohl(*(uint32_t *)&value.int128.b[0]),
				ntohs(*(uint16_t *)&value.int128.b[4]),
				ntohs(*(uint16_t *)&value.int128.b[6]),
				ntohs(*(uint16_t *)&value.int128.b[8]),
				ntohs(*(uint16_t *)&value.int128.b[10]),
				ntohl(*(uint32_t *)&value.int128.b[12]));
			break;

		case SDP_DATA_STR8:
		case SDP_DATA_URL8:
			SDP_GET8(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%c", *start);
			fprintf(stdout, "\n");
			break;

		case SDP_DATA_STR16:
		case SDP_DATA_URL16:
			SDP_GET16(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%c", *start);
			fprintf(stdout, "\n");
			break;

		case SDP_DATA_STR32:
		case SDP_DATA_URL32:
			SDP_GET32(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%c", *start);
			fprintf(stdout, "\n");
			break;

		case SDP_DATA_SEQ8:
		case SDP_DATA_ALT8:
			SDP_GET8(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%#2.2x ", *start);
			fprintf(stdout, "\n");
			break;

		case SDP_DATA_SEQ16:
		case SDP_DATA_ALT16:
			SDP_GET16(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%#2.2x ", *start);
			fprintf(stdout, "\n");
			break;

		case SDP_DATA_SEQ32:
		case SDP_DATA_ALT32:
			SDP_GET32(len, start);
			for (; start < end && len > 0; start ++, len --)
				fprintf(stdout, "%#2.2x ", *start);
			fprintf(stdout, "\n");
			break;

		default:
			fprintf(stderr, "Invalid Protocol Descriptor. " \
					"Unknown data type: %#02x\n", type);
			return;
			/* NOT REACHED */
		}
	}
} /* print_protocol_descriptor */

static void
print_protocol_descriptor_list(uint8_t const *start, uint8_t const *end)
{
	uint32_t	type, len;

	if (end - start < 2) {
		fprintf(stderr, "Invalid Protocol Descriptor List. " \
				"Too short, len=%zd\n", end - start);
		return;
	}

	SDP_GET8(type, start);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, start);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, start);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, start);
		break;

	default:
		fprintf(stderr, "Invalid Protocol Descriptor List. " \
				"Not a sequence, type=%#x\n", type);
		return;
		/* NOT REACHED */
	}

	if (len > (end - start)) {
		fprintf(stderr, "Invalid Protocol Descriptor List. " \
				"Too long, len=%d\n", len);
		return;
	}

	while (start < end) {
		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_SEQ8:
			SDP_GET8(len, start);
			break;

		case SDP_DATA_SEQ16:
			SDP_GET16(len, start);
			break;

		case SDP_DATA_SEQ32:
			SDP_GET32(len, start);
			break;

		default:
			fprintf(stderr, "Invalid Protocol Descriptor List. " \
					"Not a sequence, type=%#x\n", type);
			return;
			/* NOT REACHED */
		}

		if (len > (end - start)) {
			fprintf(stderr, "Invalid Protocol Descriptor List. " \
					"Too long, len=%d\n", len);
			return;
		}

		print_protocol_descriptor(start, start + len);
		start += len;
	}
} /* print_protocol_descriptor_list */

/*
 * Print Bluetooth Profile Descriptor List
 *
 * The BluetoothProfileDescriptorList attribute consists of a data element 
 * sequence in which each element is a profile descriptor that contains 
 * information about a Bluetooth profile to which the service represented by 
 * this service record conforms. Each profile descriptor is a data element 
 * sequence whose first element is the UUID assigned to the profile and whose 
 * second element is a 16-bit profile version number. Each version of a profile
 * is assigned a 16-bit unsigned integer profile version number, which consists
 * of two 8-bit fields. The higher-order 8 bits contain the major version 
 * number field and the lower-order 8 bits contain the minor version number 
 * field.
 */

static void
print_bluetooth_profile_descriptor_list(uint8_t const *start, uint8_t const *end)
{
	uint32_t	type, len, value;

	if (end - start < 2) {
		fprintf(stderr, "Invalid Bluetooth Profile Descriptor List. " \
				"Too short, len=%zd\n", end - start);
		return;
	}

	SDP_GET8(type, start);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, start);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, start);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, start);
		break;

	default:
		fprintf(stderr, "Invalid Bluetooth Profile Descriptor List. " \
				"Not a sequence, type=%#x\n", type);
		return;
		/* NOT REACHED */
	}

	if (len > (end - start)) {
		fprintf(stderr, "Invalid Bluetooth Profile Descriptor List. " \
				"Too long, len=%d\n", len);
		return;
	}

	while (start < end) {
		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_SEQ8:
			SDP_GET8(len, start);
			break;

		case SDP_DATA_SEQ16:
			SDP_GET16(len, start);
			break;

		case SDP_DATA_SEQ32:
			SDP_GET32(len, start);
			break;

		default:
			fprintf(stderr, "Invalid Bluetooth Profile " \
					"Descriptor List. " \
					"Not a sequence, type=%#x\n", type);
			return;
			/* NOT REACHED */
		}

		if (len > (end - start)) {
			fprintf(stderr, "Invalid Bluetooth Profile " \
					"Descriptor List. " \
					"Too long, len=%d\n", len);
			return;
		}

		/* Get UUID */
		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_UUID16:
			SDP_GET16(value, start);
			fprintf(stdout, "\t%s (%#4.4x) ",
					sdp_uuid2desc(value), value);
			break;

		case SDP_DATA_UUID32:
			SDP_GET32(value, start);
			fprintf(stdout, "\t%#8.8x ", value);
			break;

		case SDP_DATA_UUID128: {
			int128_t	uuid;

			SDP_GET_UUID128(&uuid, start);
			fprintf(stdout, "\t%#8.8x-%4.4x-%4.4x-%4.4x-%4.4x%8.8x ",
					ntohl(*(uint32_t *)&uuid.b[0]),
					ntohs(*(uint16_t *)&uuid.b[4]),
					ntohs(*(uint16_t *)&uuid.b[6]),
					ntohs(*(uint16_t *)&uuid.b[8]),
					ntohs(*(uint16_t *)&uuid.b[10]),
					ntohl(*(uint32_t *)&uuid.b[12]));
			} break;

		default:
			fprintf(stderr, "Invalid Bluetooth Profile " \
					"Descriptor List. " \
					"Not a UUID, type=%#x\n", type);
			return;
			/* NOT REACHED */
		}

		/* Get version */
		SDP_GET8(type, start);
		if (type != SDP_DATA_UINT16) {
			fprintf(stderr, "Invalid Bluetooth Profile " \
					"Descriptor List. " \
					"Invalid version type=%#x\n", type);
			return;
		}

		SDP_GET16(value, start);
		fprintf(stdout, "ver. %d.%d\n",
				(value >> 8) & 0xff, value & 0xff);
	}
} /* print_bluetooth_profile_descriptor_list */

/* Perform SDP search command */
static int
do_sdp_search(void *xs, int argc, char **argv)
{
	char		*ep = NULL;
	int32_t		 n, type, value;
	uint16_t	 service;

	/* Parse command line arguments */
	switch (argc) {
	case 1:
		n = strtoul(argv[0], &ep, 16);
		if (*ep != 0) {
			switch (tolower(argv[0][0])) {
			case 'c': /* CIP/CTP */
				switch (tolower(argv[0][1])) {
				case 'i':
					service = SDP_SERVICE_CLASS_COMMON_ISDN_ACCESS;
					break;

				case 't':
					service = SDP_SERVICE_CLASS_CORDLESS_TELEPHONY;
					break;

				default:
					return (USAGE);
					/* NOT REACHED */
				}
				break;

			case 'd': /* DialUp Networking */
				service = SDP_SERVICE_CLASS_DIALUP_NETWORKING;
				break;

			case 'f': /* Fax/OBEX File Transfer */
				switch (tolower(argv[0][1])) {
				case 'a':
					service = SDP_SERVICE_CLASS_FAX;
					break;

				case 't':
					service = SDP_SERVICE_CLASS_OBEX_FILE_TRANSFER;
					break;

				default:
					return (USAGE);
					/* NOT REACHED */
				}
				break;

			case 'g': /* GN */
				service = SDP_SERVICE_CLASS_GN;
				break;

			case 'h': /* Headset/HID */
				switch (tolower(argv[0][1])) {
				case 'i':
					service = SDP_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE;
					break;

				case 's':
					service = SDP_SERVICE_CLASS_HEADSET;
					break;

				default:
					return (USAGE);
					/* NOT REACHED */
				}
				break;

			case 'l': /* LAN Access Using PPP */
				service = SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP;
				break;

			case 'n': /* NAP */
				service = SDP_SERVICE_CLASS_NAP;
				break;

			case 'o': /* OBEX Object Push */
				service = SDP_SERVICE_CLASS_OBEX_OBJECT_PUSH;
				break;

			case 's': /* Serial Port */
				service = SDP_SERVICE_CLASS_SERIAL_PORT;
				break;

			default:
				return (USAGE);
				/* NOT REACHED */
			}
		} else
			service = (uint16_t) n;
		break;

	default:
		return (USAGE);
	}

	/* Initialize attribute values array */
	for (n = 0; n < values_len; n ++) {
		values[n].flags = SDP_ATTR_INVALID;
		values[n].attr = 0;
		values[n].vlen = BSIZE;
		values[n].value = buffer[n];
	}

	/* Do SDP Service Search Attribute Request */
	n = sdp_search(xs, 1, &service, attrs_len, attrs, values_len, values);
	if (n != 0)
		return (ERROR);

	/* Print attributes values */
	for (n = 0; n < values_len; n ++) {
		if (values[n].flags != SDP_ATTR_OK)
			break;

		switch (values[n].attr) {
		case SDP_ATTR_SERVICE_RECORD_HANDLE:
			fprintf(stdout, "\n");
			if (values[n].vlen == 5) {
				SDP_GET8(type, values[n].value);
				if (type == SDP_DATA_UINT32) {
					SDP_GET32(value, values[n].value);
					fprintf(stdout, "Record Handle: " \
							"%#8.8x\n", value);
				} else
					fprintf(stderr, "Invalid type=%#x " \
							"Record Handle " \
							"attribute!\n", type);
			} else
				fprintf(stderr, "Invalid size=%d for Record " \
						"Handle attribute\n",
						values[n].vlen);
			break;

		case SDP_ATTR_SERVICE_CLASS_ID_LIST:
			fprintf(stdout, "Service Class ID List:\n");
			print_service_class_id_list(values[n].value,
					values[n].value + values[n].vlen);
			break;

		case SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST:
			fprintf(stdout, "Protocol Descriptor List:\n");
			print_protocol_descriptor_list(values[n].value,
					values[n].value + values[n].vlen);
			break;

		case SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST:
			fprintf(stdout, "Bluetooth Profile Descriptor List:\n");
			print_bluetooth_profile_descriptor_list(values[n].value,
					values[n].value + values[n].vlen);
			break;

		default:
			fprintf(stderr, "Unexpected attribute ID=%#4.4x\n",
					values[n].attr);
			break;
		}
	}

	return (OK);
} /* do_sdp_search */

/* Perform SDP browse command */
static int
do_sdp_browse(void *xs, int argc, char **argv)
{
#undef	_STR
#undef	STR
#define	_STR(x)	#x
#define	STR(x)	_STR(x)

	static char const * const	av[] = {
		STR(SDP_SERVICE_CLASS_PUBLIC_BROWSE_GROUP),
		NULL
	}; 

	switch (argc) {
	case 0:
		argc = 1;
		argv = (char **) av;
		/* FALL THROUGH */
	case 1:
		return (do_sdp_search(xs, argc, argv));
	}

	return (USAGE);
} /* do_sdp_browse */

/* List of SDP commands */
struct sdp_command	sdp_commands[] = {
{
"Browse [<Group>]",
"Browse for services. The <Group> parameter is a 16-bit UUID of the group\n" \
"to browse. If omitted <Group> is set to Public Browse Group.\n\n" \
"\t<Group> - xxxx; 16-bit UUID of the group to browse\n",
do_sdp_browse
},
{
"Search <Service>",
"Search for the <Service>. The <Service> parameter is a 16-bit UUID of the\n" \
"service to search for. For some services it is possible to use service name\n"\
"instead of service UUID\n\n" \
"\t<Service> - xxxx; 16-bit UUID of the service to search for\n\n" \
"\tKnown service names\n" \
"\t===================\n" \
"\tCIP   - Common ISDN Access\n" \
"\tCTP   - Cordless Telephony\n" \
"\tDUN   - DialUp Networking\n" \
"\tFAX   - Fax\n" \
"\tFTRN  - OBEX File Transfer\n" \
"\tGN    - GN\n" \
"\tHID   - Human Interface Device\n" \
"\tHSET  - Headset\n" \
"\tLAN   - LAN Access Using PPP\n" \
"\tNAP   - Network Access Point\n" \
"\tOPUSH - OBEX Object Push\n" \
"\tSP    - Serial Port\n",
do_sdp_search
},
{ NULL, NULL, NULL }
};

