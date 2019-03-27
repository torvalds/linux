/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Hans Petter Selasky <hselasky@freebsd.org>
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
 * $FreeBSD$
 */

#include <sys/queue.h>
#define	L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <string.h>
#include "profile.h"
#include "provider.h"

static int32_t
audio_sink_profile_create_service_class_id_list(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{
	static const uint16_t service_classes[] = {
		SDP_SERVICE_CLASS_AUDIO_SINK,
	};

	return (common_profile_create_service_class_id_list(
	    buf, eob,
	    (uint8_t const *)service_classes,
	    sizeof(service_classes)));
}

static int32_t
audio_sink_profile_create_protocol_descriptor_list(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{
	provider_p provider = (provider_p) data;
	sdp_audio_sink_profile_p audio_sink = (sdp_audio_sink_profile_p) provider->data;

	if (buf + 18 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(16, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(6, buf);

	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_L2CAP, buf);

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(audio_sink->psm, buf);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(6, buf);

	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_UUID_PROTOCOL_AVDTP, buf);

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(audio_sink->protover, buf);

	return (18);
}

static int32_t
audio_sink_profile_create_browse_group_list(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{

	if (buf + 5 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_SEQ8, buf);
	SDP_PUT8(3, buf);

	SDP_PUT8(SDP_DATA_UUID16, buf);
	SDP_PUT16(SDP_SERVICE_CLASS_PUBLIC_BROWSE_GROUP, buf);

	return (5);
}

static int32_t
audio_sink_profile_create_bluetooth_profile_descriptor_list(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{
	static const uint16_t profile_descriptor_list[] = {
		SDP_SERVICE_CLASS_ADVANCED_AUDIO_DISTRIBUTION,
		0x0100
	};

	return (common_profile_create_bluetooth_profile_descriptor_list(
	    buf, eob,
	    (uint8_t const *)profile_descriptor_list,
	    sizeof(profile_descriptor_list)));
}

static int32_t
audio_sink_profile_create_service_name(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{
	static const char service_name[] = "Audio SNK";

	return (common_profile_create_string8(
	    buf, eob,
	    (uint8_t const *)service_name, strlen(service_name)));
}

static int32_t
audio_sink_create_supported_features(
    uint8_t *buf, uint8_t const *const eob,
    uint8_t const *data, uint32_t datalen)
{
	provider_p provider = (provider_p) data;
	sdp_audio_sink_profile_p audio_sink = (sdp_audio_sink_profile_p) provider->data;

	if (buf + 3 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(audio_sink->features, buf);

	return (3);
}

static int32_t
audio_sink_profile_valid(uint8_t const *data, uint32_t datalen)
{

	if (datalen < sizeof(struct sdp_audio_sink_profile))
		return (0);
	return (1);
}

static const attr_t audio_sink_profile_attrs[] = {
	{SDP_ATTR_SERVICE_RECORD_HANDLE,
	common_profile_create_service_record_handle},
	{SDP_ATTR_SERVICE_CLASS_ID_LIST,
	audio_sink_profile_create_service_class_id_list},
	{SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
	audio_sink_profile_create_protocol_descriptor_list},
	{SDP_ATTR_BROWSE_GROUP_LIST,
	audio_sink_profile_create_browse_group_list},
	{SDP_ATTR_LANGUAGE_BASE_ATTRIBUTE_ID_LIST,
	common_profile_create_language_base_attribute_id_list},
	{SDP_ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
	audio_sink_profile_create_bluetooth_profile_descriptor_list},
	{SDP_ATTR_PRIMARY_LANGUAGE_BASE_ID + SDP_ATTR_SERVICE_NAME_OFFSET,
	audio_sink_profile_create_service_name},
	{SDP_ATTR_PRIMARY_LANGUAGE_BASE_ID + SDP_ATTR_PROVIDER_NAME_OFFSET,
	common_profile_create_service_provider_name},
	{SDP_ATTR_SUPPORTED_FEATURES,
	audio_sink_create_supported_features},
	{}				/* end entry */
};

profile_t audio_sink_profile_descriptor = {
	SDP_SERVICE_CLASS_AUDIO_SINK,
	sizeof(sdp_audio_sink_profile_t),
	audio_sink_profile_valid,
	(attr_t const *const)&audio_sink_profile_attrs
};
