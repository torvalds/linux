/*	$NetBSD: sdp.c,v 1.2 2008/12/06 20:01:14 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $FreeBSD$ */

#include <sys/cdefs.h>
__RCSID("$NetBSD: sdp.c,v 1.2 2008/12/06 20:01:14 plunky Exp $");

#include <string.h>

#define L2CAP_SOCKET_CHECKED
#include "sdp.h"

/*
 * SDP data stream manipulation routines
 */

/* Bluetooth Base UUID */
static const uuid_t BASE_UUID = {
	0x00000000,
	0x0000,
	0x1000,
	0x80,
	0x00,
	{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
};

/*
 * _sdp_match_uuid16(ptr, limit, uuid)
 *
 *	examine SDP data stream at ptr for a UUID, and return
 *	true if it matches the supplied short alias bluetooth UUID.
 *	limit is the first address past the end of valid data.
 */
bool
_sdp_match_uuid16(uint8_t **ptr, uint8_t *limit, uint16_t uuid)
{
	uint8_t *p = *ptr;
	uuid_t u1, u2;

	memcpy(&u1, &BASE_UUID, sizeof(uuid_t));
	u1.time_low = uuid;

	if (!_sdp_get_uuid(&p, limit, &u2)
	    || !uuid_equal(&u1, &u2, NULL))
		return false;

	*ptr = p;
	return true;
}

/*
 * _sdp_get_uuid(ptr, limit, uuid)
 *
 *	examine SDP data stream at ptr for a UUID, and extract
 *	to given storage, advancing ptr.
 *	limit is the first address past the end of valid data.
 */
bool
_sdp_get_uuid(uint8_t **ptr, uint8_t *limit, uuid_t *uuid)
{
	uint8_t *p = *ptr;

	if (p + 1 > limit)
		return false;

	switch (*p++) {
	case SDP_DATA_UUID16:
		if (p + 2 > limit)
			return false;

		memcpy(uuid, &BASE_UUID, sizeof(uuid_t));
		uuid->time_low = be16dec(p);
		p += 2;
		break;

	case SDP_DATA_UUID32:
		if (p + 4 > limit)
			return false;

		memcpy(uuid, &BASE_UUID, sizeof(uuid_t));
		uuid->time_low = be32dec(p);
		p += 4;
		break;

	case SDP_DATA_UUID128:
		if (p + 16 > limit)
			return false;

		uuid_dec_be(p, uuid);
		p += 16;
		break;

	default:
		return false;
	}

	*ptr = p;
	return true;
}

/*
 * _sdp_get_seq(ptr, limit, seq)
 *
 *	examine SDP data stream at ptr for a sequence. return
 *	seq pointer if found and advance ptr to next object.
 *	limit is the first address past the end of valid data.
 */
bool
_sdp_get_seq(uint8_t **ptr, uint8_t *limit, uint8_t **seq)
{
	uint8_t *p = *ptr;
	int32_t l;

	if (p + 1 > limit)
		return false;

	switch (*p++) {
	case SDP_DATA_SEQ8:
		if (p + 1 > limit)
			return false;

		l = *p;
		p += 1;
		break;

	case SDP_DATA_SEQ16:
		if (p + 2 > limit)
			return false;

		l = be16dec(p);
		p += 2;
		break;

	case SDP_DATA_SEQ32:
		if (p + 4 > limit)
			return false;

		l = be32dec(p);
		p += 4;
		break;

	default:
		return false;
	}
	if (p + l > limit)
		return false;

	*seq = p;
	*ptr = p + l;
	return true;
}

/*
 * _sdp_get_uint16(ptr, limit, value)
 *
 *	examine SDP data stream at ptr for a uint16_t, and
 *	extract to given storage, advancing ptr.
 *	limit is the first address past the end of valid data.
 */
bool
_sdp_get_uint16(uint8_t **ptr, uint8_t *limit, uint16_t *value)
{
	uint8_t *p = *ptr;
	uint16_t v;

	if (p + 1 > limit)
		return false;

	switch (*p++) {
	case SDP_DATA_UINT16:
		if (p + 2 > limit)
			return false;

		v = be16dec(p);
		p += 2;
		break;

	default:
		return false;
	}

	*value = v;
	*ptr = p;
	return true;
}
