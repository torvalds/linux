/*-
 * service.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: service.c,v 1.1 2004/01/13 19:32:36 max Exp $
 * $FreeBSD$
 */

#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sdp-int.h>
#include <sdp.h>

static int32_t sdp_receive_error_pdu(sdp_session_p ss);

int32_t
sdp_register_service(void *xss, uint16_t uuid, bdaddr_p const bdaddr,
		uint8_t const *data, uint32_t datalen, uint32_t *handle)
{
	sdp_session_p	ss = (sdp_session_p) xss;
	struct iovec	iov[4];
	sdp_pdu_t	pdu;
	int32_t		len;

	if (ss == NULL)
		return (-1);
	if (bdaddr == NULL || data == NULL ||
	    datalen == 0 || !(ss->flags & SDP_SESSION_LOCAL)) {
		ss->error = EINVAL;
		return (-1);
	}
	if (sizeof(pdu)+sizeof(uuid)+sizeof(*bdaddr)+datalen > SDP_LOCAL_MTU) {
		ss->error = EMSGSIZE;
		return (-1);
	}

	pdu.pid = SDP_PDU_SERVICE_REGISTER_REQUEST;
	pdu.tid = htons(++ss->tid);
	pdu.len = htons(sizeof(uuid) + sizeof(*bdaddr) + datalen);

	uuid = htons(uuid);

	iov[0].iov_base = (void *) &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = (void *) &uuid;
	iov[1].iov_len = sizeof(uuid);

	iov[2].iov_base = (void *) bdaddr;
	iov[2].iov_len = sizeof(*bdaddr);

	iov[3].iov_base = (void *) data;
	iov[3].iov_len = datalen;

	do {
		len = writev(ss->s, iov, sizeof(iov)/sizeof(iov[0]));
	} while (len < 0 && errno == EINTR);

	if (len < 0) {
		ss->error = errno;
		return (-1);
	}

	len = sdp_receive_error_pdu(ss);
	if (len < 0)
		return (-1);
	if (len != sizeof(pdu) + sizeof(uint16_t) + sizeof(uint32_t)) {
		ss->error = EIO;
		return (-1);
	}

	if (handle != NULL) {
		*handle  = (uint32_t) ss->rsp[--len];
		*handle |= (uint32_t) ss->rsp[--len] << 8;
		*handle |= (uint32_t) ss->rsp[--len] << 16;
		*handle |= (uint32_t) ss->rsp[--len] << 24;
	}

	return (0);
}

int32_t
sdp_unregister_service(void *xss, uint32_t handle)
{
	sdp_session_p	ss = (sdp_session_p) xss;
	struct iovec	iov[2];
	sdp_pdu_t	pdu;
	int32_t		len;

	if (ss == NULL)
		return (-1);
	if (!(ss->flags & SDP_SESSION_LOCAL)) {
		ss->error = EINVAL;
		return (-1);
	}
	if (sizeof(pdu) + sizeof(handle) > SDP_LOCAL_MTU) {
		ss->error = EMSGSIZE;
		return (-1);
	}

	pdu.pid = SDP_PDU_SERVICE_UNREGISTER_REQUEST;
	pdu.tid = htons(++ss->tid);
	pdu.len = htons(sizeof(handle));

	handle = htonl(handle);

	iov[0].iov_base = (void *) &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = (void *) &handle;
	iov[1].iov_len = sizeof(handle);

	do {
		len = writev(ss->s, iov, sizeof(iov)/sizeof(iov[0]));
	} while (len < 0 && errno == EINTR);

	if (len < 0) {
		ss->error = errno;
		return (-1);
	}

	return ((sdp_receive_error_pdu(ss) < 0)? -1 : 0);
}

int32_t
sdp_change_service(void *xss, uint32_t handle,
		uint8_t const *data, uint32_t datalen)
{
	sdp_session_p	ss = (sdp_session_p) xss;
	struct iovec	iov[3];
	sdp_pdu_t	pdu;
	int32_t		len;

	if (ss == NULL)
		return (-1);
	if (data == NULL || datalen == 0 || !(ss->flags & SDP_SESSION_LOCAL)) {
		ss->error = EINVAL;
		return (-1);
	}
	if (sizeof(pdu) + sizeof(handle) + datalen > SDP_LOCAL_MTU) {
		ss->error = EMSGSIZE;
		return (-1);
	}

	pdu.pid = SDP_PDU_SERVICE_CHANGE_REQUEST;
	pdu.tid = htons(++ss->tid);
	pdu.len = htons(sizeof(handle) + datalen);

	handle = htons(handle);

	iov[0].iov_base = (void *) &pdu;
	iov[0].iov_len = sizeof(pdu);

	iov[1].iov_base = (void *) &handle;
	iov[1].iov_len = sizeof(handle);

	iov[2].iov_base = (void *) data;
	iov[2].iov_len = datalen;

	do {
		len = writev(ss->s, iov, sizeof(iov)/sizeof(iov[0]));
	} while (len < 0 && errno == EINTR);

	if (len < 0) {
		ss->error = errno;
		return (-1);
	}

	return ((sdp_receive_error_pdu(ss) < 0)? -1 : 0);
}

static int32_t
sdp_receive_error_pdu(sdp_session_p ss)
{
	sdp_pdu_p	pdu;
	int32_t		len;
	uint16_t	error;

	do {
		len = read(ss->s, ss->rsp, ss->rsp_e - ss->rsp);
	} while (len < 0 && errno == EINTR);

	if (len < 0) {
		ss->error = errno;
		return (-1);
	}

	pdu = (sdp_pdu_p) ss->rsp;
	pdu->tid = ntohs(pdu->tid);
	pdu->len = ntohs(pdu->len);

	if (pdu->pid != SDP_PDU_ERROR_RESPONSE || pdu->tid != ss->tid ||
	    pdu->len < 2 || pdu->len != len - sizeof(*pdu)) {
		ss->error = EIO;
		return (-1);
	}

	error  = (uint16_t) ss->rsp[sizeof(pdu)] << 8;
	error |= (uint16_t) ss->rsp[sizeof(pdu) + 1];

	if (error != 0) {
		ss->error = EIO;
		return (-1);
	}

	return (len);
}

