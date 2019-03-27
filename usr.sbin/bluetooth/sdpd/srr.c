/*-
 * srr.c
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
 * $Id: srr.c,v 1.1 2004/01/13 01:54:39 max Exp $
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

/*
 * Prepare Service Register response
 */

int32_t
server_prepare_service_register_response(server_p srv, int32_t fd)
{
	uint8_t const	*req = srv->req + sizeof(sdp_pdu_t);
	uint8_t const	*req_end = req + ((sdp_pdu_p)(srv->req))->len;
	uint8_t		*rsp = srv->fdidx[fd].rsp;

	profile_t	*profile = NULL;
	provider_t	*provider = NULL;
	bdaddr_t	*bdaddr = NULL;
	int32_t		 uuid;

	/*
	 * Minimal Service Register Request
	 *
	 * value16	- uuid 2 bytes
	 * bdaddr	- BD_ADDR 6 bytes
	 */

	if (!srv->fdidx[fd].control ||
	    !srv->fdidx[fd].priv || req_end - req < 8)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get ServiceClass UUID */
	SDP_GET16(uuid, req);

	/* Get BD_ADDR */
	bdaddr = (bdaddr_p) req;
	req += sizeof(*bdaddr);

	/* Lookup profile descriptror */
	profile = profile_get_descriptor(uuid);
	if (profile == NULL)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Validate user data */
	if (req_end - req < profile->dsize ||
	    profile->valid == NULL ||
	    (profile->valid)(req, req_end - req) == 0)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Register provider */
	provider = provider_register(profile, bdaddr, fd, req, req_end - req);
	if (provider == NULL)
		return (SDP_ERROR_CODE_INSUFFICIENT_RESOURCES);

	SDP_PUT16(0, rsp);
	SDP_PUT32(provider->handle, rsp);
	
	/* Set reply size */
	srv->fdidx[fd].rsp_limit = srv->fdidx[fd].omtu - sizeof(sdp_pdu_t);
	srv->fdidx[fd].rsp_size = rsp - srv->fdidx[fd].rsp;
	srv->fdidx[fd].rsp_cs = 0;

	return (0);
}

/*
 * Send Service Register Response
 */

int32_t
server_send_service_register_response(server_p srv, int32_t fd)
{
	struct iovec	iov[2];
	sdp_pdu_t	pdu;
	int32_t		size;

	assert(srv->fdidx[fd].rsp_size < srv->fdidx[fd].rsp_limit);

	pdu.pid = SDP_PDU_ERROR_RESPONSE;
	pdu.tid = ((sdp_pdu_p)(srv->req))->tid;
	pdu.len = htons(srv->fdidx[fd].rsp_size);

	iov[0].iov_base = &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = srv->fdidx[fd].rsp;
	iov[1].iov_len = srv->fdidx[fd].rsp_size;

	do {
		size = writev(fd, (struct iovec const *) &iov, sizeof(iov)/sizeof(iov[0]));
	} while (size < 0 && errno == EINTR);

	srv->fdidx[fd].rsp_cs = 0;
	srv->fdidx[fd].rsp_size = 0;
	srv->fdidx[fd].rsp_limit = 0;

	return ((size < 0)? errno : 0);
}

