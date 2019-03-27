/*
 * profile.c
 */

/*-
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
 * $Id: profile.c,v 1.6 2004/01/13 19:31:54 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <string.h>
#include "profile.h"
#include "provider.h"

/*
 * Lookup profile descriptor
 */

profile_p
profile_get_descriptor(uint16_t uuid)
{
  	extern	profile_t	audio_sink_profile_descriptor;
	extern	profile_t	audio_source_profile_descriptor;
	extern	profile_t	dun_profile_descriptor;
	extern	profile_t	ftrn_profile_descriptor;
	extern	profile_t	irmc_profile_descriptor;
	extern	profile_t	irmc_command_profile_descriptor;
	extern	profile_t	lan_profile_descriptor;
	extern	profile_t	opush_profile_descriptor;
	extern	profile_t	sp_profile_descriptor;
	extern	profile_t	nap_profile_descriptor;
	extern	profile_t	gn_profile_descriptor;
	extern	profile_t	panu_profile_descriptor;

	static const profile_p	profiles[] = {
		&audio_sink_profile_descriptor,
		&audio_source_profile_descriptor,
		&dun_profile_descriptor,
		&ftrn_profile_descriptor,
		&irmc_profile_descriptor,
		&irmc_command_profile_descriptor,
		&lan_profile_descriptor,
		&opush_profile_descriptor,
		&sp_profile_descriptor,
		&nap_profile_descriptor,
		&gn_profile_descriptor,
		&panu_profile_descriptor
	};

	int32_t			i;

	for (i = 0; i < sizeof(profiles)/sizeof(profiles[0]); i++)
		if (profiles[i]->uuid == uuid)
			return (profiles[i]);

	return (NULL);
}

/*
 * Look attribute in the profile descripror
 */

profile_attr_create_p
profile_get_attr(const profile_p profile, uint16_t attr)
{
	attr_p	ad = (attr_p) profile->attrs;

	for (; ad->create != NULL; ad ++)
		if (ad->attr == attr)
			return (ad->create);

	return (NULL);
}

/*
 * uint32 value32 - 5 bytes
 */

int32_t
common_profile_create_service_record_handle(
	uint8_t *buf, uint8_t const * const eob,
	uint8_t const *data, uint32_t datalen)
{
	if (buf + 5 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_UINT32, buf);
	SDP_PUT32(((provider_p) data)->handle, buf);

	return (5);
}

/*
 * seq8 len8			- 2 bytes
 *	uuid16 value16		- 3 bytes
 *	[ uuid16 value ]
 */

int32_t
common_profile_create_service_class_id_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	int32_t	len = 3 * (datalen >>= 1);

	if (len <= 0 || len > 0xff || buf + 2 + len > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(len, buf);

	for (; datalen > 0; datalen --) {
		SDP_PUT8(SDP_DATA_UUID16, buf);
		SDP_PUT16(*((uint16_t const *)data), buf);
		data += sizeof(uint16_t);
	}

	return (2 + len);
}

/*
 * seq8 len8			- 2 bytes
 *	seq 8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 *		uint16 value16	- 3 bytes
 *	[ seq 8 len8
 *		uuid16 value16
 *		uint16 value16 ]
 */

int32_t
common_profile_create_bluetooth_profile_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	int32_t	len = 8 * (datalen >>= 2);
	
	if (len <= 0 || len > 0xff || buf + 2 + len > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(len, buf);

	for (; datalen > 0; datalen --) {
		SDP_PUT8(SDP_DATA_SEQ8, buf);
		SDP_PUT8(6, buf);
		SDP_PUT8(SDP_DATA_UUID16, buf);
		SDP_PUT16(*((uint16_t const *)data), buf);
		data += sizeof(uint16_t);
		SDP_PUT8(SDP_DATA_UINT16, buf);
		SDP_PUT16(*((uint16_t const *)data), buf);
		data += sizeof(uint16_t);
	}

	return (2 + len);
}

/*
 * seq8 len8		- 2 bytes
 *	uint16 value16	- 3 bytes
 *	uint16 value16	- 3 bytes
 *	uint16 value16	- 3 bytes
 */

int32_t
common_profile_create_language_base_attribute_id_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	if (buf + 11 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(9, buf);

	/*
	 * Language code per ISO 639:1988. Use "en".
	 */

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(((0x65 << 8) | 0x6e), buf);

	/* 
	 * Encoding. Recommended is UTF-8. ISO639 UTF-8 MIBenum is 106 
	 * (http://www.iana.org/assignments/character-sets)
	 */

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(106, buf);

	/* 
	 * Offset (Primary Language Base is 0x100)
	 */

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(SDP_ATTR_PRIMARY_LANGUAGE_BASE_ID, buf);

	return (11);
}

/*
 * Common provider name is "FreeBSD"
 */

int32_t
common_profile_create_service_provider_name(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	char	provider_name[] = "FreeBSD";

	return (common_profile_create_string8(buf, eob,
			(uint8_t const *) provider_name,
			strlen(provider_name)));
}

/*
 * str8 len8 string
 */

int32_t
common_profile_create_string8(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	if (datalen == 0 || datalen > 0xff || buf + 2 + datalen > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_STR8, buf);
	SDP_PUT8(datalen, buf);
	memcpy(buf, data, datalen);

	return (2 + datalen);
}

/*
 * Service Availability
 */

int32_t
common_profile_create_service_availability(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	if (datalen != 1 || buf + 2 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_UINT8, buf);
	SDP_PUT8(data[0], buf);

	return (2);
}

/*
 * seq8 len8			- 2 bytes
 *	seq8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 *	seq8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 *		uint8 value8	- 2 bytes
 */

int32_t
rfcomm_profile_create_protocol_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	if (datalen != 1 || buf + 14 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(12, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(3, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_L2CAP, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(5, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_RFCOMM, buf);
	SDP_PUT8(SDP_DATA_UINT8, buf);
	SDP_PUT8(*data, buf);

	return (14);
}

/*
 * seq8 len8			- 2 bytes
 *	seq8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 *	seq8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 *		uint8 value8	- 2 bytes
 *	seq8 len8		- 2 bytes
 *		uuid16 value16	- 3 bytes
 */

int32_t
obex_profile_create_protocol_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	if (datalen != 1 || buf + 19 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(17, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(3, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_L2CAP, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(5, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_RFCOMM, buf);
	SDP_PUT8(SDP_DATA_UINT8, buf);
	SDP_PUT8(*data, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(3, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_OBEX, buf);

	return (19);
}

/*
 * seq8 len8
 *	uint8 value8	- bytes
 *	[ uint8 value 8 ]
 */

int32_t
obex_profile_create_supported_formats_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	int32_t	len = 2 * datalen;

	if (len <= 0 || len > 0xff || buf + 2 + len > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(len, buf);

	for (; datalen > 0; datalen --) {
		SDP_PUT8(SDP_DATA_UINT8, buf);
		SDP_PUT8(*data++, buf);
	}

	return (2 + len);
}

/*
 * do not check anything
 */

int32_t
common_profile_always_valid(uint8_t const *data, uint32_t datalen)
{
	return (1);
}

/*
 * verify server channel number (the first byte in the data)
 */

int32_t
common_profile_server_channel_valid(uint8_t const *data, uint32_t datalen)
{
	if (data[0] < 1 || data[0] > 30)
		return (0);

	return (1);
}

/*
 * verify server channel number and supported_formats_size 
 * sdp_opush_profile and sdp_irmc_profile
 */

int32_t
obex_profile_data_valid(uint8_t const *data, uint32_t datalen)
{
	sdp_opush_profile_p	opush = (sdp_opush_profile_p) data;

	if (opush->server_channel < 1 ||
	    opush->server_channel > 30 ||
	    opush->supported_formats_size == 0 ||
	    opush->supported_formats_size > sizeof(opush->supported_formats))
		return (0);

	return (1);
}

/*
 * BNEP protocol descriptor
 */

int32_t
bnep_profile_create_protocol_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	/* supported protocol types */
	uint16_t	 ptype[] = {
		0x0800,	/* IPv4 */
		0x0806,	/* ARP */
#ifdef INET6
		0x86dd,	/* IPv6 */
#endif
	};

	uint16_t	 i, psm, version = 0x0100,
			 nptypes = sizeof(ptype)/sizeof(ptype[0]),
			 nptypes_size = nptypes * 3;

	if (datalen != 2 || 18 + nptypes_size > 255 ||
	    buf + 20 + nptypes_size > eob)
		return (-1);

	memcpy(&psm, data, sizeof(psm));

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(18 + nptypes_size, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(6, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_L2CAP, buf);
	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(psm, buf);
	
	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(8 + nptypes_size, buf);
	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_BNEP, buf);
	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(version, buf);
	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(nptypes_size, buf);
	for (i = 0; i < nptypes; i ++) {
		SDP_PUT8(SDP_DATA_UINT16, buf);
		SDP_PUT16(ptype[i], buf);
	}

	return (20 + nptypes_size);
}

/*
 * BNEP security description
 */

int32_t
bnep_profile_create_security_description(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	uint16_t	security_descr;

	if (datalen != 2 || buf + 3 > eob)
		return (-1);

	memcpy(&security_descr, data, sizeof(security_descr));

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(security_descr, buf);

        return (3);
}

