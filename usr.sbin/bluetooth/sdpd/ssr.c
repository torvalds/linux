/*-
 * ssr.c
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
 * $Id: ssr.c,v 1.5 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <sdp.h>
#include <string.h>
#include "profile.h"
#include "provider.h"
#include "server.h"
#include "uuid-private.h"

/*
 * Prepare SDP Service Search Response
 */

int32_t
server_prepare_service_search_response(server_p srv, int32_t fd)
{
	uint8_t const	*req = srv->req + sizeof(sdp_pdu_t);
	uint8_t const	*req_end = req + ((sdp_pdu_p)(srv->req))->len;
	uint8_t		*rsp = srv->fdidx[fd].rsp;
	uint8_t const	*rsp_end = rsp + NG_L2CAP_MTU_MAXIMUM;

	uint8_t		*ptr = NULL;
	provider_t	*provider = NULL;
	int32_t		 type, ssplen, rsp_limit, rcount, cslen, cs;
	uint128_t	 uuid, puuid;

	/*
	 * Minimal SDP Service Search Request
	 *
	 * seq8 len8		- 2 bytes
	 *	uuid16 value16	- 3 bytes ServiceSearchPattern
	 * value16		- 2 bytes MaximumServiceRecordCount
	 * value8		- 1 byte  ContinuationState
	 */

	if (req_end - req < 8)
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

	ptr = (uint8_t *) req + ssplen;

	/* Get MaximumServiceRecordCount */
	if (ptr + 2 > req_end)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);
	
	SDP_GET16(rsp_limit, ptr);
	if (rsp_limit <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get ContinuationState */
	if (ptr + 1 > req_end)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	SDP_GET8(cslen, ptr);
	if (cslen != 0) {
		if (cslen != 2 || req_end - ptr != 2)
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

		SDP_GET16(cs, ptr);
	} else
		cs = 0;

	/* Process the request. First, check continuation state */
	if (srv->fdidx[fd].rsp_cs != cs)
		return (SDP_ERROR_CODE_INVALID_CONTINUATION_STATE);
	if (srv->fdidx[fd].rsp_size > 0)
		return (0);

	/*
	 * Service Search Response format
	 *
	 * value16	- 2 bytes TotalServiceRecordCount (not incl.)
	 * value16	- 2 bytes CurrentServiceRecordCount (not incl.)
	 * value32	- 4 bytes handle
	 * [ value32 ]
	 *
	 * Calculate how many record handles we can fit 
	 * in our reply buffer and adjust rlimit.
	 */

	ptr = rsp;
	rcount = (rsp_end - ptr) / 4;
	if (rcount < rsp_limit)
		rsp_limit = rcount;

	/* Look for the record handles */
	for (rcount = 0; ssplen > 0 && rcount < rsp_limit; ) {
		SDP_GET8(type, req);
		ssplen --;

		switch (type) {
		case SDP_DATA_UUID16:
			if (ssplen < 2)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(&uuid, &uuid_base, sizeof(uuid));
			uuid.b[2] = *req ++;
			uuid.b[3] = *req ++;
			ssplen -= 2;
			break;

		case SDP_DATA_UUID32:
			if (ssplen < 4)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(&uuid, &uuid_base, sizeof(uuid));
			uuid.b[0] = *req ++;
			uuid.b[1] = *req ++;
			uuid.b[2] = *req ++;
			uuid.b[3] = *req ++;
			ssplen -= 4;
			break;

		case SDP_DATA_UUID128:
			if (ssplen < 16)
				return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

			memcpy(uuid.b, req, 16);
			req += 16;
			ssplen -= 16; 
			break;

		default:
			return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);
			/* NOT REACHED */
		}

		for (provider = provider_get_first();
		     provider != NULL && rcount < rsp_limit;
		     provider = provider_get_next(provider)) {
			if (!provider_match_bdaddr(provider, &srv->req_sa.l2cap_bdaddr))
				continue;

			memcpy(&puuid, &uuid_base, sizeof(puuid));
			puuid.b[2] = provider->profile->uuid >> 8;
			puuid.b[3] = provider->profile->uuid;

			if (memcmp(&uuid, &puuid, sizeof(uuid)) == 0 ||
			    memcmp(&uuid, &uuid_public_browse_group, sizeof(uuid)) == 0) {
				SDP_PUT32(provider->handle, ptr);
				rcount ++;
			}
		}
	}

	/* Set reply size (not counting PDU header and continuation state) */
	srv->fdidx[fd].rsp_limit = srv->fdidx[fd].omtu - sizeof(sdp_pdu_t) - 4;
	srv->fdidx[fd].rsp_size = ptr - rsp;
	srv->fdidx[fd].rsp_cs = 0;

	return (0);
}

/*
 * Send SDP Service Search Response
 */

int32_t
server_send_service_search_response(server_p srv, int32_t fd)
{
	uint8_t		*rsp = srv->fdidx[fd].rsp + srv->fdidx[fd].rsp_cs;
	uint8_t		*rsp_end = srv->fdidx[fd].rsp + srv->fdidx[fd].rsp_size;

	struct iovec	iov[4];
	sdp_pdu_t	pdu;
	uint16_t	rcounts[2];
	uint8_t		cs[3];
	int32_t		size;

	/* First update continuation state (assume we will send all data) */
	size = rsp_end - rsp;
	srv->fdidx[fd].rsp_cs += size;

	if (size + 1 > srv->fdidx[fd].rsp_limit) {
		/*
		 * We need to split out response. Add 3 more bytes for the
		 * continuation state and move rsp_end and rsp_cs backwards. 
		 */

		while ((rsp_end - rsp) + 3 > srv->fdidx[fd].rsp_limit) {
			rsp_end -= 4;
			srv->fdidx[fd].rsp_cs -= 4;
		}

		cs[0] = 2;
		cs[1] = srv->fdidx[fd].rsp_cs >> 8;
		cs[2] = srv->fdidx[fd].rsp_cs & 0xff;
	} else
		cs[0] = 0;

	assert(rsp_end >= rsp);

	rcounts[0] = srv->fdidx[fd].rsp_size / 4; /* TotalServiceRecordCount */
	rcounts[1] = (rsp_end - rsp) / 4; /* CurrentServiceRecordCount */

	pdu.pid = SDP_PDU_SERVICE_SEARCH_RESPONSE;
	pdu.tid = ((sdp_pdu_p)(srv->req))->tid;
	pdu.len = htons(sizeof(rcounts) + rcounts[1] * 4 + 1 + cs[0]);

	rcounts[0] = htons(rcounts[0]);
	rcounts[1] = htons(rcounts[1]);

	iov[0].iov_base = &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = rcounts;
	iov[1].iov_len = sizeof(rcounts);

	iov[2].iov_base = rsp;
	iov[2].iov_len = rsp_end - rsp;

	iov[3].iov_base = cs;
	iov[3].iov_len = 1 + cs[0];

	do {
		size = writev(fd, (struct iovec const *) &iov, sizeof(iov)/sizeof(iov[0]));
	} while (size < 0 && errno == EINTR);

	/* Check if we have sent (or failed to sent) last response chunk */
	if (srv->fdidx[fd].rsp_cs == srv->fdidx[fd].rsp_size) {
		srv->fdidx[fd].rsp_cs = 0;
		srv->fdidx[fd].rsp_size = 0;
		srv->fdidx[fd].rsp_limit = 0;
	}

	return ((size < 0)? errno : 0);
}

