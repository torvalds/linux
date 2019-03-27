/*-
 * lan.c
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
 * $Id: lan.c,v 1.5 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#include <arpa/inet.h>
#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <stdio.h>
#include <string.h>
#include "profile.h"
#include "provider.h"

static int32_t
lan_profile_create_service_class_id_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	static uint16_t	service_classes[] = {
		SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP
	};

	return (common_profile_create_service_class_id_list(
			buf, eob,
			(uint8_t const *) service_classes,
			sizeof(service_classes)));
}

static int32_t
lan_profile_create_bluetooth_profile_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	static uint16_t	profile_descriptor_list[] = {
		SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP,
		0x0100
	};

	return (common_profile_create_bluetooth_profile_descriptor_list(
			buf, eob,
			(uint8_t const *) profile_descriptor_list,
			sizeof(profile_descriptor_list)));
}

static int32_t
lan_profile_create_service_name(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	static char	service_name[] = "LAN Access using PPP";

	return (common_profile_create_string8(
			buf, eob,
			(uint8_t const *) service_name, strlen(service_name)));
}

static int32_t
lan_profile_create_protocol_descriptor_list(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	provider_p		provider = (provider_p) data;
	sdp_lan_profile_p	lan = (sdp_lan_profile_p) provider->data;

	return (rfcomm_profile_create_protocol_descriptor_list(
			buf, eob,
			(uint8_t const *) &lan->server_channel, 1)); 
}

static int32_t
lan_profile_create_service_availability(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	provider_p		provider = (provider_p) data;
	sdp_lan_profile_p	lan = (sdp_lan_profile_p) provider->data;

	return (common_profile_create_service_availability(buf, eob,
			&lan->load_factor, 1));
}

static int32_t
lan_profile_create_ip_subnet(
		uint8_t *buf, uint8_t const * const eob,
		uint8_t const *data, uint32_t datalen)
{
	provider_p		provider = (provider_p) data;
	sdp_lan_profile_p	lan = (sdp_lan_profile_p) provider->data;
	char			net[32];
	int32_t			len;

	len = snprintf(net, sizeof(net), "%s/%d",
			inet_ntoa(* (struct in_addr *) &lan->ip_subnet),
			lan->ip_subnet_radius);

	if (len < 0 || buf + 2 + len > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_STR8, buf);
	SDP_PUT8(len, buf);
	memcpy(buf, net, len);

	return (2 + len);
}

static int32_t
lan_profile_data_valid(uint8_t const *data, uint32_t datalen)
{
	sdp_lan_profile_p	lan = (sdp_lan_profile_p) data;

	if (lan->server_channel < 1 ||
	    lan->server_channel > 30 ||
	    lan->ip_subnet_radius > 32)
		return (0);

	return (1);
}

static attr_t	lan_profile_attrs[] = {
	{ SDP_ATTR_SERVICE_RECORD_HANDLE,
	  common_profile_create_service_record_handle },
	{ SDP_ATTR_SERVICE_CLASS_ID_LIST,
	  lan_profile_create_service_class_id_list },
	{ SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
	  lan_profile_create_bluetooth_profile_descriptor_list },
	{ SDP_ATTR_LANGUAGE_BASE_ATTRIBUTE_ID_LIST,
	  common_profile_create_language_base_attribute_id_list },
	{ SDP_ATTR_PRIMARY_LANGUAGE_BASE_ID + SDP_ATTR_SERVICE_NAME_OFFSET,
	  lan_profile_create_service_name },
	{ SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
	  lan_profile_create_protocol_descriptor_list },
	{ SDP_ATTR_SERVICE_AVAILABILITY,
	  lan_profile_create_service_availability },
	{ SDP_ATTR_IP_SUBNET,
	  lan_profile_create_ip_subnet },
	{ 0, NULL } /* end entry */
};

profile_t	lan_profile_descriptor = {
	SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP,
	sizeof(sdp_lan_profile_t),
	lan_profile_data_valid,
	(attr_t const * const) &lan_profile_attrs
};

