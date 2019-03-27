/*-
 * sur.c
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
 * $Id: sur.c,v 1.1 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <sdp.h>
#include <string.h>
#include "profile.h"
#include "provider.h"
#include "server.h"

/*
 * Prepare Service Unregister response
 */

int32_t
server_prepare_service_unregister_response(server_p srv, int32_t fd)
{
	uint8_t const	*req = srv->req + sizeof(sdp_pdu_t);
	uint8_t const	*req_end = req + ((sdp_pdu_p)(srv->req))->len;
	uint8_t		*rsp = srv->fdidx[fd].rsp;

	provider_t	*provider = NULL;
	uint32_t	 handle;

	/*
	 * Minimal Service Unregister Request
	 *
	 * value32	- uuid 4 bytes
	 */

	if (!srv->fdidx[fd].control ||
	    !srv->fdidx[fd].priv || req_end - req < 4)
		return (SDP_ERROR_CODE_INVALID_REQUEST_SYNTAX);

	/* Get handle */
	SDP_GET32(handle, req);

	/* Lookup provider */
	provider = provider_by_handle(handle);
	if (provider == NULL || provider->fd != fd)
		return (SDP_ERROR_CODE_INVALID_SERVICE_RECORD_HANDLE);

	provider_unregister(provider);
	SDP_PUT16(0, rsp);

	/* Set reply size */
	srv->fdidx[fd].rsp_limit = srv->fdidx[fd].omtu - sizeof(sdp_pdu_t);
	srv->fdidx[fd].rsp_size = rsp - srv->fdidx[fd].rsp;
	srv->fdidx[fd].rsp_cs = 0;

	return (0);
}

