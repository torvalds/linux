/*-
 * util.c
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
 * $Id: util.c,v 1.5 2003/09/08 02:29:35 max Exp $
 * $FreeBSD$
 */

#include <netinet/in.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <stdio.h>
#include <sdp.h>

/*
 * SDP attribute description
 */

struct sdp_attr_desc {
	uint32_t	 attr;
	char const	*desc;
};
typedef struct sdp_attr_desc	sdp_attr_desc_t;
typedef struct sdp_attr_desc *	sdp_attr_desc_p;

static sdp_attr_desc_t	sdp_uuids_desc[] = {
{ SDP_UUID_PROTOCOL_SDP, "SDP", },
{ SDP_UUID_PROTOCOL_UDP, "UDP", },
{ SDP_UUID_PROTOCOL_RFCOMM, "RFCOMM", },
{ SDP_UUID_PROTOCOL_TCP, "TCP", },
{ SDP_UUID_PROTOCOL_TCS_BIN, "TCS BIN", },
{ SDP_UUID_PROTOCOL_TCS_AT, "TCS AT", },
{ SDP_UUID_PROTOCOL_OBEX, "OBEX", },
{ SDP_UUID_PROTOCOL_IP, "IP", },
{ SDP_UUID_PROTOCOL_FTP, "FTP", },
{ SDP_UUID_PROTOCOL_HTTP, "HTTP", },
{ SDP_UUID_PROTOCOL_WSP, "WSP", },
{ SDP_UUID_PROTOCOL_BNEP, "BNEP", },
{ SDP_UUID_PROTOCOL_UPNP, "UPNP", },
{ SDP_UUID_PROTOCOL_HIDP, "HIDP", },
{ SDP_UUID_PROTOCOL_HARDCOPY_CONTROL_CHANNEL, "Hardcopy Control Channel", },
{ SDP_UUID_PROTOCOL_HARDCOPY_DATA_CHANNEL, "Hardcopy Data Channel", },
{ SDP_UUID_PROTOCOL_HARDCOPY_NOTIFICATION, "Hardcopy Notification", },
{ SDP_UUID_PROTOCOL_AVCTP, "AVCTP", },
{ SDP_UUID_PROTOCOL_AVDTP, "AVDTP", },
{ SDP_UUID_PROTOCOL_CMPT, "CMPT", },
{ SDP_UUID_PROTOCOL_UDI_C_PLANE, "UDI C-Plane", },
{ SDP_UUID_PROTOCOL_L2CAP, "L2CAP", },
/* Service Class IDs/Bluetooth Profile IDs */
{ SDP_SERVICE_CLASS_SERVICE_DISCOVERY_SERVER, "Service Discovery Server", },
{ SDP_SERVICE_CLASS_BROWSE_GROUP_DESCRIPTOR, "Browse Group Descriptor", },
{ SDP_SERVICE_CLASS_PUBLIC_BROWSE_GROUP, "Public Browse Group", },
{ SDP_SERVICE_CLASS_SERIAL_PORT, "Serial Port", },
{ SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP, "LAN Access Using PPP", },
{ SDP_SERVICE_CLASS_DIALUP_NETWORKING, "Dial-Up Networking", },
{ SDP_SERVICE_CLASS_IR_MC_SYNC, "IrMC Sync", },
{ SDP_SERVICE_CLASS_OBEX_OBJECT_PUSH, "OBEX Object Push", },
{ SDP_SERVICE_CLASS_OBEX_FILE_TRANSFER, "OBEX File Transfer", },
{ SDP_SERVICE_CLASS_IR_MC_SYNC_COMMAND, "IrMC Sync Command", },
{ SDP_SERVICE_CLASS_HEADSET, "Headset", },
{ SDP_SERVICE_CLASS_CORDLESS_TELEPHONY, "Cordless Telephony", },
{ SDP_SERVICE_CLASS_AUDIO_SOURCE, "Audio Source", },
{ SDP_SERVICE_CLASS_AUDIO_SINK, "Audio Sink", },
{ SDP_SERVICE_CLASS_AV_REMOTE_CONTROL_TARGET, "A/V Remote Control Target", },
{ SDP_SERVICE_CLASS_ADVANCED_AUDIO_DISTRIBUTION, "Advanced Audio Distribution", },
{ SDP_SERVICE_CLASS_AV_REMOTE_CONTROL, "A/V Remote Control", },
{ SDP_SERVICE_CLASS_VIDEO_CONFERENCING, "Video Conferencing", },
{ SDP_SERVICE_CLASS_INTERCOM, "Intercom", },
{ SDP_SERVICE_CLASS_FAX, "Fax", },
{ SDP_SERVICE_CLASS_HEADSET_AUDIO_GATEWAY, "Headset Audio Gateway", },
{ SDP_SERVICE_CLASS_WAP, "WAP", },
{ SDP_SERVICE_CLASS_WAP_CLIENT, "WAP Client", },
{ SDP_SERVICE_CLASS_PANU, "PANU", },
{ SDP_SERVICE_CLASS_NAP, "Network Access Point", },
{ SDP_SERVICE_CLASS_GN, "GN", },
{ SDP_SERVICE_CLASS_DIRECT_PRINTING, "Direct Printing", },
{ SDP_SERVICE_CLASS_REFERENCE_PRINTING, "Reference Printing", },
{ SDP_SERVICE_CLASS_IMAGING, "Imaging", },
{ SDP_SERVICE_CLASS_IMAGING_RESPONDER, "Imaging Responder", },
{ SDP_SERVICE_CLASS_IMAGING_AUTOMATIC_ARCHIVE, "Imaging Automatic Archive", },
{ SDP_SERVICE_CLASS_IMAGING_REFERENCED_OBJECTS, "Imaging Referenced Objects", },
{ SDP_SERVICE_CLASS_HANDSFREE, "Handsfree", },
{ SDP_SERVICE_CLASS_HANDSFREE_AUDIO_GATEWAY, "Handsfree Audio Gateway", },
{ SDP_SERVICE_CLASS_DIRECT_PRINTING_REFERENCE_OBJECTS, "Direct Printing Reference Objects", },
{ SDP_SERVICE_CLASS_REFLECTED_UI, "Reflected UI", },
{ SDP_SERVICE_CLASS_BASIC_PRINTING, "Basic Printing", },
{ SDP_SERVICE_CLASS_PRINTING_STATUS, "Printing Status", },
{ SDP_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE, "Human Interface Device", },
{ SDP_SERVICE_CLASS_HARDCOPY_CABLE_REPLACEMENT, "Hardcopy Cable Replacement", },
{ SDP_SERVICE_CLASS_HCR_PRINT, "HCR Print", },
{ SDP_SERVICE_CLASS_HCR_SCAN, "HCR Scan", },
{ SDP_SERVICE_CLASS_COMMON_ISDN_ACCESS, "Common ISDN Access", },
{ SDP_SERVICE_CLASS_VIDEO_CONFERENCING_GW, "Video Conferencing Gateway", },
{ SDP_SERVICE_CLASS_UDI_MT, "UDI MT", },
{ SDP_SERVICE_CLASS_UDI_TA, "UDI TA", },
{ SDP_SERVICE_CLASS_AUDIO_VIDEO, "Audio/Video", },
{ SDP_SERVICE_CLASS_SIM_ACCESS, "SIM Access", },
{ SDP_SERVICE_CLASS_PHONEBOOK_ACCESS_PCE, "Phonebook Access - PCE", },
{ SDP_SERVICE_CLASS_PHONEBOOK_ACCESS_PSE, "Phonebook Access - PSE", },
{ SDP_SERVICE_CLASS_PHONEBOOK_ACCESS, "Phonebook Access", },
{ SDP_SERVICE_CLASS_PNP_INFORMATION, "PNP Information", },
{ SDP_SERVICE_CLASS_GENERIC_NETWORKING, "Generic Networking", },
{ SDP_SERVICE_CLASS_GENERIC_FILE_TRANSFER, "Generic File Transfer", },
{ SDP_SERVICE_CLASS_GENERIC_AUDIO, "Generic Audio", },
{ SDP_SERVICE_CLASS_GENERIC_TELEPHONY, "Generic Telephony", },
{ SDP_SERVICE_CLASS_UPNP, "UPNP", },
{ SDP_SERVICE_CLASS_UPNP_IP, "UPNP IP", },
{ SDP_SERVICE_CLASS_ESDP_UPNP_IP_PAN, "ESDP UPNP IP PAN", },
{ SDP_SERVICE_CLASS_ESDP_UPNP_IP_LAP, "ESDP UPNP IP LAP", },
{ SDP_SERVICE_CLASS_ESDP_UPNP_L2CAP, "ESDP UPNP L2CAP", },
{ SDP_SERVICE_CLASS_VIDEO_SOURCE, "Video Source", },
{ SDP_SERVICE_CLASS_VIDEO_SINK, "Video Sink", },
{ SDP_SERVICE_CLASS_VIDEO_DISTRIBUTION, "Video Distribution", },
{ 0xffff, NULL, }
};

static sdp_attr_desc_t	sdp_attrs_desc[] = {
{ SDP_ATTR_SERVICE_RECORD_HANDLE,
  "Record handle",
  },
{ SDP_ATTR_SERVICE_CLASS_ID_LIST,
  "Service Class ID list",
  },
{ SDP_ATTR_SERVICE_RECORD_STATE,
  "Service Record State",
  },
{ SDP_ATTR_SERVICE_ID,
  "Service ID",
  },
{ SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
  "Protocol Descriptor List",
  },
{ SDP_ATTR_BROWSE_GROUP_LIST,
  "Browse Group List",
  },
{ SDP_ATTR_LANGUAGE_BASE_ATTRIBUTE_ID_LIST,
  "Language Base Attribute ID List",
  },
{ SDP_ATTR_SERVICE_INFO_TIME_TO_LIVE,
  "Service Info Time-To-Live",
  },
{ SDP_ATTR_SERVICE_AVAILABILITY,
  "Service Availability",
  },
{ SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
  "Bluetooh Profile Descriptor List",
  }, 
{ SDP_ATTR_DOCUMENTATION_URL,
  "Documentation URL",
  },
{ SDP_ATTR_CLIENT_EXECUTABLE_URL,
  "Client Executable URL",
  },
{ SDP_ATTR_ICON_URL,
  "Icon URL",
  },
{ SDP_ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS,
  "Additional Protocol Descriptor Lists" },
{ SDP_ATTR_GROUP_ID,
/*SDP_ATTR_IP_SUBNET,
  SDP_ATTR_VERSION_NUMBER_LIST*/
  "Group ID/IP Subnet/Version Number List",
  },
{ SDP_ATTR_SERVICE_DATABASE_STATE,
  "Service Database State",
  },
{ SDP_ATTR_SERVICE_VERSION,
  "Service Version",
  },
{ SDP_ATTR_EXTERNAL_NETWORK,
/*SDP_ATTR_NETWORK,
  SDP_ATTR_SUPPORTED_DATA_STORES_LIST*/
  "External Network/Network/Supported Data Stores List",
  },
{ SDP_ATTR_FAX_CLASS1_SUPPORT,
/*SDP_ATTR_REMOTE_AUDIO_VOLUME_CONTROL*/
  "Fax Class1 Support/Remote Audio Volume Control",
  },
{ SDP_ATTR_FAX_CLASS20_SUPPORT,
/*SDP_ATTR_SUPPORTED_FORMATS_LIST*/
  "Fax Class20 Support/Supported Formats List",
  },
{ SDP_ATTR_FAX_CLASS2_SUPPORT,
  "Fax Class2 Support",
  },
{ SDP_ATTR_AUDIO_FEEDBACK_SUPPORT,
  "Audio Feedback Support",
  },
{ SDP_ATTR_NETWORK_ADDRESS,
  "Network Address",
  },
{ SDP_ATTR_WAP_GATEWAY,
  "WAP Gateway",
  },
{ SDP_ATTR_HOME_PAGE_URL,
  "Home Page URL",
  },
{ SDP_ATTR_WAP_STACK_TYPE,
  "WAP Stack Type",
  },
{ SDP_ATTR_SECURITY_DESCRIPTION,
  "Security Description",
  },
{ SDP_ATTR_NET_ACCESS_TYPE,
  "Net Access Type",
  },
{ SDP_ATTR_MAX_NET_ACCESS_RATE,
  "Max Net Access Rate",
  },
{ SDP_ATTR_IPV4_SUBNET,
  "IPv4 Subnet",
  },
{ SDP_ATTR_IPV6_SUBNET,
  "IPv6 Subnet",
  },
{ SDP_ATTR_SUPPORTED_CAPABALITIES,
  "Supported Capabalities",
  },
{ SDP_ATTR_SUPPORTED_FEATURES,
  "Supported Features",
  },
{ SDP_ATTR_SUPPORTED_FUNCTIONS,
  "Supported Functions",
  },
{ SDP_ATTR_TOTAL_IMAGING_DATA_CAPACITY,
  "Total Imaging Data Capacity",
  },
{ SDP_ATTR_SUPPORTED_REPOSITORIES,
  "Supported Repositories",
  },
{ 0xffff, NULL, }
};

char const *
sdp_attr2desc(uint16_t attr)
{
	register sdp_attr_desc_p	a = sdp_attrs_desc;

	for (; a->desc != NULL; a++)
		if (attr == a->attr)
			break;

	return ((a->desc != NULL)? a->desc : "Unknown");
}

char const *
sdp_uuid2desc(uint16_t uuid)
{
	register sdp_attr_desc_p	a = sdp_uuids_desc;

	for (; a->desc != NULL; a++)
		if (uuid == a->attr)
			break;

	return ((a->desc != NULL)? a->desc : "Unknown");
}

void
sdp_print(uint32_t level, uint8_t const *start, uint8_t const *end)
{
	union {
		int8_t		int8;
		int16_t		int16;
		int32_t		int32;
		int64_t		int64;
		int128_t	int128;
		uint8_t		uint8;
		uint16_t	uint16;
		uint32_t	uint32;
		uint64_t	uint64;
	}			value;
	uint8_t			type;
	uint32_t		i;

	if (start == NULL || end == NULL)
		return;

	while (start < end) {
		for (i = 0; i < level; i++)
			printf("\t");

		SDP_GET8(type, start);

		switch (type) {
		case SDP_DATA_NIL:
			printf("nil\n");
			break;

		case SDP_DATA_UINT8:
			SDP_GET8(value.uint8, start);
			printf("uint8 %u\n", value.uint8);
			break;
		case SDP_DATA_UINT16:
			SDP_GET16(value.uint16, start);
			printf("uint16 %u\n", value.uint16);
			break;
		case SDP_DATA_UINT32:
			SDP_GET32(value.uint32, start);
			printf("uint32 %u\n", value.uint32);
			break;
		case SDP_DATA_UINT64:
			SDP_GET64(value.uint64, start);
			printf("uint64 %ju\n", value.uint64);
			break;

		case SDP_DATA_UINT128:
		case SDP_DATA_INT128:
			SDP_GET128(&value.int128, start);
			printf("u/int128 %#8.8x%8.8x%8.8x%8.8x\n",
				*(uint32_t *)&value.int128.b[0],
				*(uint32_t *)&value.int128.b[4],
				*(uint32_t *)&value.int128.b[8],
				*(uint32_t *)&value.int128.b[12]);
			break;

		case SDP_DATA_UUID128:
			SDP_GET_UUID128(&value.int128, start);
			printf("uuid128 %#8.8x-%4.4x-%4.4x-%4.4x-%4.4x%8.8x\n",
				ntohl(*(uint32_t *)&value.int128.b[0]),
				ntohs(*(uint16_t *)&value.int128.b[4]),
				ntohs(*(uint16_t *)&value.int128.b[6]),
				ntohs(*(uint16_t *)&value.int128.b[8]),
				ntohs(*(uint16_t *)&value.int128.b[10]),
				ntohl(*(uint32_t *)&value.int128.b[12]));
			break;

		case SDP_DATA_INT8:
			SDP_GET8(value.int8, start);
			printf("int8 %d\n", value.int8);
			break;
		case SDP_DATA_INT16:
			SDP_GET16(value.int16, start);
			printf("int16 %d\n", value.int16);
			break;
		case SDP_DATA_INT32:
			SDP_GET32(value.int32, start);
			printf("int32 %d\n", value.int32);
			break;
		case SDP_DATA_INT64:
			SDP_GET64(value.int64, start);
			printf("int64 %ju\n", value.int64);
			break;
	
		case SDP_DATA_UUID16:
			SDP_GET16(value.uint16, start);
			printf("uuid16 %#4.4x - %s\n", value.uint16,
				sdp_uuid2desc(value.uint16));
			break;
		case SDP_DATA_UUID32:
			SDP_GET32(value.uint32, start);
			printf("uuid32 %#8.8x\n", value.uint32);
			break;

		case SDP_DATA_STR8:
			SDP_GET8(value.uint8, start);
			printf("str8 %*.*s\n", value.uint8, value.uint8, start);
			start += value.uint8;
			break;
		case SDP_DATA_STR16:
			SDP_GET16(value.uint16, start);
			printf("str16 %*.*s\n", value.uint16, value.uint16, start);
			start += value.uint16;
			break;
		case SDP_DATA_STR32:
			SDP_GET32(value.uint32, start);
			printf("str32 %*.*s\n", value.uint32, value.uint32, start);
			start += value.uint32;
			break;

		case SDP_DATA_BOOL:
			SDP_GET8(value.uint8, start);
			printf("bool %d\n", value.uint8);
			break;

		case SDP_DATA_SEQ8:
			SDP_GET8(value.uint8, start);
			printf("seq8 %d\n", value.uint8);
			sdp_print(level + 1, start, start + value.uint8);
			start += value.uint8;
			break;
		case SDP_DATA_SEQ16:
			SDP_GET16(value.uint16, start);
			printf("seq16 %d\n", value.uint16);
			sdp_print(level + 1, start, start + value.uint16);
			start += value.uint16;
			break;
		case SDP_DATA_SEQ32:
			SDP_GET32(value.uint32, start);
			printf("seq32 %d\n", value.uint32);
			sdp_print(level + 1, start, start + value.uint32);
			start += value.uint32;
			break;

		case SDP_DATA_ALT8:
			SDP_GET8(value.uint8, start);
			printf("alt8 %d\n", value.uint8);
			sdp_print(level + 1, start, start + value.uint8);
			start += value.uint8;
			break;
		case SDP_DATA_ALT16:
			SDP_GET16(value.uint16, start);
			printf("alt16 %d\n", value.uint16);
			sdp_print(level + 1, start, start + value.uint16);
			start += value.uint16;
			break;
		case SDP_DATA_ALT32:
			SDP_GET32(value.uint32, start);
			printf("alt32 %d\n", value.uint32);
			sdp_print(level + 1, start, start + value.uint32);
			start += value.uint32;
			break;

		case SDP_DATA_URL8:
			SDP_GET8(value.uint8, start);
			printf("url8 %*.*s\n", value.uint8, value.uint8, start);
			start += value.uint8;
			break;
		case SDP_DATA_URL16:
			SDP_GET16(value.uint16, start);
			printf("url16 %*.*s\n", value.uint16, value.uint16, start);
			start += value.uint16;
			break;
		case SDP_DATA_URL32:
			SDP_GET32(value.uint32, start);
			printf("url32 %*.*s\n", value.uint32, value.uint32, start);
			start += value.uint32;
			break;
	
		default:
			printf("unknown data type: %#02x\n", *start ++);
			break;
		}
	}
}

