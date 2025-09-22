/*	$OpenBSD: yp_bind.c,v 1.33 2024/01/22 16:18:06 deraadt Exp $ */
/*
 * Copyright (c) 1992, 1993, 1996 Theo de Raadt <deraadt@theos.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <paths.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "ypinternal.h"

char _yp_domain[HOST_NAME_MAX+1];
int _yplib_timeout = 10;

extern CLIENT *
clntudp_bufcreate_simple(struct sockaddr_in *raddr, u_long program, u_long version,
    struct timeval wait, int *sockp, u_int sendsz, u_int recvsz);

int
_yp_dobind(const char *dom, struct dom_binding **ypdb)
{
	struct dom_binding *ypbinding;
	struct timeval tv;
	int connected = 1;
	int s;

	if (dom == NULL || strlen(dom) == 0)
		return YPERR_BADARGS;

	ypbinding = calloc(1, sizeof (*ypbinding));
	if (ypbinding == NULL)
		return YPERR_RESRC;

again:
	s = ypconnect(SOCK_DGRAM);
	if (s == -1) {
		free(ypbinding);
		return YPERR_YPBIND;	/* YP not running */
	}
	ypbinding->dom_socket = s;
	ypbinding->dom_server_addr.sin_port = -1; /* don't consult portmap */

	tv.tv_sec = _yplib_timeout / 2;
	tv.tv_usec = 0;
	ypbinding->dom_client = clntudp_bufcreate_simple(&ypbinding->dom_server_addr,
	    YPPROG, YPVERS, tv, &ypbinding->dom_socket, UDPMSGSIZE, UDPMSGSIZE);
	if (ypbinding->dom_client == NULL) {
		close(ypbinding->dom_socket);
		ypbinding->dom_socket = -1;
		clnt_pcreateerror("clntudp_create");
		goto again;
	}
	clnt_control(ypbinding->dom_client, CLSET_CONNECTED, &connected);
	*ypdb = ypbinding;
	return 0;
}

void
_yp_unbind(struct dom_binding *ypb)
{
	close(ypb->dom_socket);
	if (ypb->dom_client)
		clnt_destroy(ypb->dom_client);
	free(ypb);
}

/*
 * Check if YP is running.  But do not leave it active, because we
 * may not return from libc with a fd active.
 */
int
yp_bind(const char *dom)
{
	struct dom_binding *ysd;
	int r;

	r = _yp_dobind(dom, &ysd);
	if (r == 0)
		_yp_unbind(ysd);
	return r;
}
DEF_WEAK(yp_bind);

void
yp_unbind(const char *dom)
{
	/* do nothing */
}
