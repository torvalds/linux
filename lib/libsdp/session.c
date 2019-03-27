/*-
 * session.c
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
 * $Id: session.c,v 1.2 2003/09/04 22:12:13 max Exp $
 * $FreeBSD$
 */
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sdp-int.h>
#include <sdp.h>

void *
sdp_open(bdaddr_t const *l, bdaddr_t const *r)
{
	sdp_session_p		ss = NULL;
	struct sockaddr_l2cap	sa;
	socklen_t		size;

	if ((ss = calloc(1, sizeof(*ss))) == NULL)
		goto fail;

	if (l == NULL || r == NULL) {
		ss->error = EINVAL;
		goto fail;
	}

	ss->s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
	if (ss->s < 0) {
		ss->error = errno;
		goto fail;
	}

	sa.l2cap_len = sizeof(sa);
	sa.l2cap_family = AF_BLUETOOTH;
	sa.l2cap_psm = 0;
	sa.l2cap_cid = 0;
	sa.l2cap_bdaddr_type = BDADDR_BREDR;
	
	memcpy(&sa.l2cap_bdaddr, l, sizeof(sa.l2cap_bdaddr));
	if (bind(ss->s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		ss->error = errno;
		goto fail;
	}

	sa.l2cap_psm = htole16(NG_L2CAP_PSM_SDP);
	memcpy(&sa.l2cap_bdaddr, r, sizeof(sa.l2cap_bdaddr));
	if (connect(ss->s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		ss->error = errno;
		goto fail;
	}

	size = sizeof(ss->omtu);
	if (getsockopt(ss->s, SOL_L2CAP, SO_L2CAP_OMTU, &ss->omtu, &size) < 0) {
		ss->error = errno;
		goto fail;
	}
	if ((ss->req = malloc(ss->omtu)) == NULL) {
		ss->error = ENOMEM;
		goto fail;
	}
	ss->req_e = ss->req + ss->omtu;

	size = sizeof(ss->imtu);
	if (getsockopt(ss->s, SOL_L2CAP, SO_L2CAP_IMTU, &ss->imtu, &size) < 0) {
		ss->error = errno;
		goto fail;
	}
	if ((ss->rsp = malloc(ss->imtu)) == NULL) {
		ss->error = ENOMEM;
		goto fail;
	}
	ss->rsp_e = ss->rsp + ss->imtu;
	ss->error = 0;
fail:
	return ((void *) ss);
}

void *
sdp_open_local(char const *control)
{
	sdp_session_p		ss = NULL;
	struct sockaddr_un	sa;

	if ((ss = calloc(1, sizeof(*ss))) == NULL)
		goto fail;

	ss->s = socket(PF_UNIX, SOCK_STREAM, 0);
	if (ss->s < 0) {
		ss->error = errno;
		goto fail;
	}

	if (control == NULL)
		control = SDP_LOCAL_PATH;

	sa.sun_len = sizeof(sa);
	sa.sun_family = AF_UNIX;
	strlcpy(sa.sun_path, control, sizeof(sa.sun_path));

	if (connect(ss->s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		ss->error = errno;
		goto fail;
	}

	ss->flags |= SDP_SESSION_LOCAL;
	ss->imtu = ss->omtu = SDP_LOCAL_MTU;

	if ((ss->req = malloc(ss->omtu)) == NULL) {
		ss->error = ENOMEM;
		goto fail;
	}
	ss->req_e = ss->req + ss->omtu;

	if ((ss->rsp = malloc(ss->imtu)) == NULL) {
		ss->error = ENOMEM;
		goto fail;
	}
	ss->rsp_e = ss->rsp + ss->imtu;
	ss->error = 0;
fail:
	return ((void *) ss);
}

int32_t
sdp_close(void *xss)
{
	sdp_session_p	ss = (sdp_session_p) xss;

	if (ss != NULL) {
		if (ss->s >= 0)
			close(ss->s);

		if (ss->req != NULL)
			free(ss->req);
		if (ss->rsp != NULL)
			free(ss->rsp);

		memset(ss, 0, sizeof(*ss));
		free(ss);
	}

	return (0);
}

int32_t
sdp_error(void *xss)
{
	sdp_session_p	ss = (sdp_session_p) xss;

	return ((ss != NULL)? ss->error : EINVAL);
}

int32_t
sdp_get_lcaddr(void *xss, bdaddr_t *l)
{
	sdp_session_p		ss = (sdp_session_p) xss;
	struct sockaddr_l2cap	sa;
	socklen_t		size;

	if (l == NULL || ss == NULL || ss->flags & SDP_SESSION_LOCAL) {
		ss->error = EINVAL;
		goto fail;
	}

	size = sizeof(sa);
	if (getsockname(ss->s, (struct sockaddr *)&sa, &size) == 0) {
		bdaddr_copy(l, &sa.l2cap_bdaddr);
		ss->error = 0;
	} else
		ss->error = errno;
fail:
	return ((ss->error == 0) ? 0 : -1);
}
