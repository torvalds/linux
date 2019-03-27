/*-
 * ssar.c
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
 * $Id: ssar.c,v 1.4 2004/01/12 22:54:31 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <string.h>
#include "profile.h"
#include "provider.h"
#include "server.h"
#include "uuid-private.h"

/* from sar.c */
int32_t server_prepare_attr_list(provider_p const provider,
		uint8_t const *req, uint8_t const * const req_end,
		uint8_t *rsp, uint8_t const * const rsp_end);

/*
 * Scan an attribute for matching UUID.
 */
static int
server_search_uuid_sub(uint8_t *buf, uint8_t const * const eob, const uint128_t *uuid)
{
        int128_t duuid;
        uint32_t value;
        uint8_t type;

        while (buf < eob) {

                SDP_GET8(type, buf);

                switch (type) {
                case SDP_DATA_UUID16:
                        if (buf + 2 > eob)
                                continue;
                        SDP_GET16(value, buf);

                        memcpy(&duuid, &uuid_base, sizeof(duuid));
                        duuid.b[2] = value >> 8 & 0xff;
                        duuid.b[3] = value & 0xff;

                        if (memcmp(&duuid, uuid, sizeof(duuid)) == 0)
                                return (0);
                        break;
                case SDP_DATA_UUID32:
                        if (buf + 4 > eob)
                                continue;
                        SDP_GET32(value, buf);
                        memcpy(&duuid, &uuid_base, sizeof(duuid));
                        duuid.b[0] = value >> 24 & 0xff;
                        duuid.b[1] = value >> 16 & 0xff;
                        duuid.b[2] = value >> 8 & 0xff;
                        duuid.b[3] = value & 0xff;

                        if (memcmp(&duuid, uuid, sizeof(duuid)) == 0)
                                return (0);
                        break;
                case SDP_DATA_UUID128:
                        if (buf + 16 > eob)
                                continue;
                        SDP_GET_UUID128(&duuid, buf);

                        if (memcmp(&duuid, uuid, sizeof(duuid)) == 0)
                                return (0);
                        break;
                case SDP_DATA_UINT8:
                case SDP_DATA_INT8:
                case SDP_DATA_SEQ8:
                        buf++;
                        break;
                case SDP_DATA_UINT16:
                case SDP_DATA_INT16:
                case SDP_DATA_SEQ16:
                        buf += 2;
                        break;
                case SDP_DATA_UINT32:
                case SDP_DATA_INT32:
                case SDP_DATA_SEQ32:
                        buf += 4;
                        break;
                case SDP_DATA_UINT64:
                case SDP_DATA_INT64:
                        buf += 8;
                        break;
                case SDP_DATA_UINT128:
                case SDP_DATA_INT128:
                        buf += 16;
                        break;
                case SDP_DATA_STR8:
                        if (buf + 1 > eob)
                                continue;
                        SDP_GET8(value, buf);
                        buf += value;
                        break;
                case SDP_DATA_STR16:
                        if (buf + 2 > eob)
                                continue;
                        SDP_GET16(value, buf);
                        if (value > (eob - buf))
                                return (1);
                        buf += value;
                        break;
                case SDP_DATA_STR32:
                        if (buf + 4 > eob)
                                continue;
                        SDP_GET32(value, buf);
                        if (value > (eob - buf))
                                return (1);
                        buf += value;
                        break;
                case SDP_DATA_BOOL:
                        buf += 1;
                        break;
                default:
                        return (1);
                }
        }
        return (1);
}

/*
 * Search a provider for matching UUID in its attributes.
 */
static int
server_search_uuid(provider_p const provider, const uint128_t *uuid)
{
        uint8_t buffer[256];
        const attr_t *attr;
        int len;

        for (attr = provider->profile->attrs; attr->create != NULL; attr++) {

                len = attr->create(buffer, buffer + sizeof(buffer),
                    (const uint8_t *)provider->profile, sizeof(*provider->profile));
                if (len < 0)
                        continue;
                if (server_search_uuid_sub(buffer, buffer + len, uuid) == 0)
                        return (0);
        }
        return (1);
}

/*
 * Prepare SDP Service Search Attribute Response
 */

int32_t
server_prepare_service_search_attribute_response(server_p srv, int32_t fd)
{
	uint8_t const	*req = srv->req + sizeof(sdp_pdu_t);
	uint8_t const	*req_end = req + ((sdp_pdu_p)(srv->req))->len;
	uint8_t		*rsp = srv->fdidx[fd].rsp;
	uint8_t const	*rsp_end = rsp + NG_L2CAP_MTU_MAXIMUM;

	uint8_t const	*sspptr = NULL, *aidptr = NULL;
	uint8_t		*ptr = NULL;

	provider_t	*provider = NULL;
	int32_t		 type, rsp_limit, ssplen, aidlen, cslen, cs;
	uint128_t	 uuid, puuid;

	/*
	 * Minimal Service Search Attribute Request request
	 *
	 * seq8 len8		- 2 bytes
	 *	uuid16 value16  - 3 bytes ServiceSearchPattern
	 * value16		- 2 bytes MaximumAttributeByteCount
	 * seq8 len8		- 2 bytes
	 *	uint16 value16	- 3 bytes AttributeIDList
	 * value8		- 1 byte  ContinuationState
	 */

	if (req_end - req < 13)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get size of ServiceSearchPattern */
	ssplen = 0;
	SDP_GET8(type, req);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(ssplen, req);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(ssplen, req);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(ssplen, req);
		break;
	}
	if (ssplen <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	sspptr = req;
	req += ssplen;

	/* Get MaximumAttributeByteCount */
	if (req + 2 > req_end)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	SDP_GET16(rsp_limit, req);
	if (rsp_limit <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get size of AttributeIDList */
	if (req + 1 > req_end)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	aidlen = 0;
	SDP_GET8(type, req);
	switch (type) {
	case SDP_DATA_SEQ8:
		if (req + 1 > req_end)
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

		SDP_GET8(aidlen, req);
		break;

	case SDP_DATA_SEQ16:
		if (req + 2 > req_end)
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

		SDP_GET16(aidlen, req);
		break;

	case SDP_DATA_SEQ32:
		if (req + 4 > req_end)
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

		SDP_GET32(aidlen, req);
		break;
	}
	if (aidlen <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	aidptr = req;
	req += aidlen;

	/* Get ContinuationState */
	if (req + 1 > req_end)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	SDP_GET8(cslen, req);
	if (cslen != 0) {
		if (cslen != 2 || req_end - req != 2)
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

		SDP_GET16(cs, req);
	} else
		cs = 0;

	/* Process the request. First, check continuation state */
	if (srv->fdidx[fd].rsp_cs != cs)
		return (SDP_ERROR_CODE_INVALID_CONTINUATION_STATE);
	if (srv->fdidx[fd].rsp_size > 0)
		return (0);

	/*
	 * Service Search Attribute Response format
	 *
	 * value16		- 2 bytes  AttributeListByteCount (not incl.)
	 * seq8 len16		- 3 bytes
	 *	attr list	- 3+ bytes AttributeLists
	 *	[ attr list ]
	 */

	ptr = rsp + 3;

	while (ssplen > 0) {
		SDP_GET8(type, sspptr);
		ssplen --;

		switch (type) {
		case SDP_DATA_UUID16:
			if (ssplen < 2)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(&uuid, &uuid_base, sizeof(uuid));
			uuid.b[2] = *sspptr ++;
			uuid.b[3] = *sspptr ++;
			ssplen -= 2;
			break;

		case SDP_DATA_UUID32:
			if (ssplen < 4)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(&uuid, &uuid_base, sizeof(uuid));
			uuid.b[0] = *sspptr ++;
			uuid.b[1] = *sspptr ++;
			uuid.b[2] = *sspptr ++;
			uuid.b[3] = *sspptr ++;
			ssplen -= 4;
			break;

		case SDP_DATA_UUID128:
			if (ssplen < 16)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(uuid.b, sspptr, 16);
			sspptr += 16;	
			ssplen -= 16; 
			break;

		default:
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);
			/* NOT REACHED */
		}

		for (provider = provider_get_first();
		     provider != NULL;
		     provider = provider_get_next(provider)) {
			if (!provider_match_bdaddr(provider, &srv->req_sa.l2cap_bdaddr))
				continue;

			memcpy(&puuid, &uuid_base, sizeof(puuid));
			puuid.b[2] = provider->profile->uuid >> 8;
			puuid.b[3] = provider->profile->uuid;

			if (memcmp(&uuid, &puuid, sizeof(uuid)) != 0 &&
			    memcmp(&uuid, &uuid_public_browse_group, sizeof(uuid)) != 0 &&
			    server_search_uuid(provider, &uuid) != 0)
				continue;

			cs = server_prepare_attr_list(provider,
				aidptr, aidptr + aidlen, ptr, rsp_end);
			if (cs < 0)
				return (SDP_ERROR_CODE_INSUFFICIENT_RESOURCES);

			ptr += cs;
		}
	}

	/* Set reply size (not counting PDU header and continuation state) */
	srv->fdidx[fd].rsp_limit = srv->fdidx[fd].omtu - sizeof(sdp_pdu_t) - 2;
	if (srv->fdidx[fd].rsp_limit > rsp_limit)
		srv->fdidx[fd].rsp_limit = rsp_limit;

	srv->fdidx[fd].rsp_size = ptr - rsp;
	srv->fdidx[fd].rsp_cs = 0;

	/* Fix AttributeLists sequence header */
	ptr = rsp;
	SDP_PUT8(SDP_DATA_SEQ16, ptr);
	SDP_PUT16(srv->fdidx[fd].rsp_size - 3, ptr);

	return (0);
}

