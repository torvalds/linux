/*-
 * sar.c
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
 * $Id: sar.c,v 1.2 2004/01/08 23:46:51 max Exp $
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
#include <stdio.h> /* for NULL */
#include "profile.h"
#include "provider.h"
#include "server.h"

/*
 * Prepare SDP attr/value pair. Check if profile implements the attribute
 * and if so call the attribute value function.
 *
 * uint16 value16	- 3 bytes (attribute)
 * value		- N bytes (value)
 */

static int32_t
server_prepare_attr_value_pair(
		provider_p const provider, uint16_t attr,
		uint8_t *buf, uint8_t const * const eob)
{
	profile_attr_create_p	cf = profile_get_attr(provider->profile, attr);
	int32_t			len;

	if (cf == NULL)
		return (0); /* no attribute */

	if (buf + 3 > eob)
		return (-1);

	SDP_PUT8(SDP_DATA_UINT16, buf);
	SDP_PUT16(attr, buf);

	len = cf(buf, eob, (uint8_t const *) provider, sizeof(*provider));
	if (len < 0)
		return (-1);

	return (3 + len);
}

/*
 * seq16 value16	- 3 bytes
 *	attr value	- 3+ bytes
 *	[ attr value ]
 */

int32_t
server_prepare_attr_list(provider_p const provider,
		uint8_t const *req, uint8_t const * const req_end,
		uint8_t *rsp, uint8_t const * const rsp_end)
{
	uint8_t	*ptr = rsp + 3;
	int32_t	 type, hi, lo, len;

	if (ptr > rsp_end)
		return (-1);

	while (req < req_end) {
		SDP_GET8(type, req);

		switch (type) {
		case SDP_DATA_UINT16:
			if (req + 2 > req_end)
				return (-1);

			SDP_GET16(lo, req);
			hi = lo;
			break;

		case SDP_DATA_UINT32:
			if (req + 4 > req_end)
				return (-1);

			SDP_GET16(lo, req);
			SDP_GET16(hi, req);
			break;

		default:
			return (-1);
			/* NOT REACHED */
		}

		for (; lo <= hi; lo ++) {
			len = server_prepare_attr_value_pair(provider, lo, ptr, rsp_end);
			if (len < 0)
				return (-1);

			ptr += len;
		}
	}

	len = ptr - rsp; /* we put this much bytes in rsp */

	/* Fix SEQ16 header for the rsp */
	SDP_PUT8(SDP_DATA_SEQ16, rsp);
	SDP_PUT16(len - 3, rsp);

	return (len);
}

/*
 * Prepare SDP Service Attribute Response
 */

int32_t
server_prepare_service_attribute_response(server_p srv, int32_t fd)
{
	uint8_t const	*req = srv->req + sizeof(sdp_pdu_t);
	uint8_t const	*req_end = req + ((sdp_pdu_p)(srv->req))->len;
	uint8_t		*rsp = srv->fdidx[fd].rsp;
	uint8_t const	*rsp_end = rsp + NG_L2CAP_MTU_MAXIMUM;

	uint8_t		*ptr = NULL;
	provider_t	*provider = NULL;
	uint32_t	 handle;
	int32_t		 type, rsp_limit, aidlen, cslen, cs;

	/*
	 * Minimal Service Attribute Request request
	 *
	 * value32		- 4 bytes ServiceRecordHandle
	 * value16		- 2 bytes MaximumAttributeByteCount
	 * seq8 len8		- 2 bytes
	 *	uint16 value16	- 3 bytes AttributeIDList
	 * value8		- 1 byte  ContinuationState
	 */

	if (req_end - req < 12)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get ServiceRecordHandle and MaximumAttributeByteCount */
	SDP_GET32(handle, req);
	SDP_GET16(rsp_limit, req);
	if (rsp_limit <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get size of AttributeIDList */
	aidlen = 0;
	SDP_GET8(type, req);
	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(aidlen, req);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(aidlen, req);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(aidlen, req);
 		break;
	}
	if (aidlen <= 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	ptr = (uint8_t *) req + aidlen;

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

	/* Lookup record handle */
	if ((provider = provider_by_handle(handle)) == NULL)
		return (SDP_ERROR_CODE_INVALID_SERVICE_RECORD_HANDLE);

	/*
	 * Service Attribute Response format
	 *
	 * value16		- 2 bytes  AttributeListByteCount (not incl.)
	 * seq8 len16		- 3 bytes
	 *	attr value	- 3+ bytes AttributeList
	 *	[ attr value ]
	 */

	cs = server_prepare_attr_list(provider, req, req+aidlen, rsp, rsp_end);
	if (cs < 0)
		return (SDP_ERROR_CODE_INSUFFICIENT_RESOURCES);

	/* Set reply size (not counting PDU header and continuation state) */
	srv->fdidx[fd].rsp_limit = srv->fdidx[fd].omtu - sizeof(sdp_pdu_t) - 2;
	if (srv->fdidx[fd].rsp_limit > rsp_limit)
		srv->fdidx[fd].rsp_limit = rsp_limit;

	srv->fdidx[fd].rsp_size = cs;
	srv->fdidx[fd].rsp_cs = 0;

	return (0);
}

/*
 * Send SDP Service [Search] Attribute Response 
 */

int32_t
server_send_service_attribute_response(server_p srv, int32_t fd)
{
	uint8_t		*rsp = srv->fdidx[fd].rsp + srv->fdidx[fd].rsp_cs;
	uint8_t		*rsp_end = srv->fdidx[fd].rsp + srv->fdidx[fd].rsp_size;

	struct iovec	iov[4];
	sdp_pdu_t	pdu;
	uint16_t	bcount;
	uint8_t		cs[3];
	int32_t		size;

	/* First update continuation state  (assume we will send all data) */
	size = rsp_end - rsp;
	srv->fdidx[fd].rsp_cs += size;

	if (size + 1 > srv->fdidx[fd].rsp_limit) {
		/*
		 * We need to split out response. Add 3 more bytes for the
		 * continuation state and move rsp_end and rsp_cs backwards.
		 */

		while ((rsp_end - rsp) + 3 > srv->fdidx[fd].rsp_limit) {
			rsp_end --;
			srv->fdidx[fd].rsp_cs --;
		}

		cs[0] = 2;
		cs[1] = srv->fdidx[fd].rsp_cs >> 8;
		cs[2] = srv->fdidx[fd].rsp_cs & 0xff;
	} else
		cs[0] = 0;

	assert(rsp_end >= rsp);

	bcount = rsp_end - rsp;

	if (((sdp_pdu_p)(srv->req))->pid == SDP_PDU_SERVICE_ATTRIBUTE_REQUEST)
		pdu.pid = SDP_PDU_SERVICE_ATTRIBUTE_RESPONSE;
	else
		pdu.pid = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_RESPONSE;

	pdu.tid = ((sdp_pdu_p)(srv->req))->tid;
	pdu.len = htons(sizeof(bcount) + bcount + 1 + cs[0]);

	bcount = htons(bcount);

	iov[0].iov_base = &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = &bcount;
	iov[1].iov_len = sizeof(bcount);

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

