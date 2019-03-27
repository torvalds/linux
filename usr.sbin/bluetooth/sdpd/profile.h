/*
 * profile.h
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
 * $Id: profile.h,v 1.6 2004/01/13 19:31:54 max Exp $
 * $FreeBSD$
 */

#ifndef _PROFILE_H_
#define _PROFILE_H_

/*
 * Attribute descriptor
 */

typedef int32_t	(profile_attr_create_t)(
			uint8_t *buf, uint8_t const * const eob,
			uint8_t const *data, uint32_t datalen);
typedef profile_attr_create_t *	profile_attr_create_p;

typedef int32_t	(profile_data_valid_t)(
			uint8_t const *data, uint32_t datalen);
typedef profile_data_valid_t *	profile_data_valid_p;

struct attr
{
	uint16_t		attr;	/* attribute id */
	profile_attr_create_p	create;	/* create attr value */
};

typedef struct attr	attr_t;
typedef struct attr *	attr_p;

/*
 * Profile descriptor
 */


struct profile
{
	uint16_t		uuid;	/* profile uuid */
	uint16_t		dsize;	/* profile data size */
	profile_data_valid_p	valid;	/* profile data validator */
	attr_t const * const	attrs;	/* supported attributes */
};

typedef struct profile	profile_t;
typedef struct profile *profile_p;

profile_p		profile_get_descriptor(uint16_t uuid);
profile_attr_create_p	profile_get_attr(const profile_p profile, uint16_t attr);

profile_attr_create_t	common_profile_create_service_record_handle;
profile_attr_create_t	common_profile_create_service_class_id_list;
profile_attr_create_t	common_profile_create_bluetooth_profile_descriptor_list;
profile_attr_create_t	common_profile_create_language_base_attribute_id_list;
profile_attr_create_t	common_profile_create_service_provider_name;
profile_attr_create_t	common_profile_create_string8;
profile_attr_create_t	common_profile_create_service_availability;
profile_attr_create_t	rfcomm_profile_create_protocol_descriptor_list;
profile_attr_create_t	obex_profile_create_protocol_descriptor_list;
profile_attr_create_t	obex_profile_create_supported_formats_list;
profile_attr_create_t	bnep_profile_create_protocol_descriptor_list;
profile_attr_create_t	bnep_profile_create_security_description;

profile_data_valid_t	common_profile_always_valid;
profile_data_valid_t	common_profile_server_channel_valid;
profile_data_valid_t	obex_profile_data_valid;

#endif /* ndef _PROFILE_H_ */

