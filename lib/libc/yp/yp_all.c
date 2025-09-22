/*	$OpenBSD: yp_all.c,v 1.16 2022/08/02 16:59:29 deraadt Exp $ */
/*
 * Copyright (c) 1992, 1993 Theo de Raadt <deraadt@theos.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "ypinternal.h"

static int (*ypresp_allfn)(u_long, char *, int, char *, int, void *);
static void *ypresp_data;

static bool_t
_xdr_ypresp_all_seq(XDR *xdrs, u_long *objp)
{
	struct ypresp_all out;
	u_long status;
	char *key, *val;
	int size;
	int done = 0;  /* set to 1 when the user does not want more data */
	bool_t rc = TRUE;  /* FALSE at the end of loop signals failure */

	memset(&out, 0, sizeof out);
	while (rc && !done) {
		rc = FALSE;
		if (!xdr_ypresp_all(xdrs, &out)) {
			*objp = (u_long)YP_YPERR;
			goto fail;
		}
		if (out.more == 0)
			goto fail;
		status = out.ypresp_all_u.val.stat;
		if (status == YP_TRUE) {
			size = out.ypresp_all_u.val.key.keydat_len;
			if ((key = malloc(size + 1)) == NULL) {
				*objp = (u_long)YP_YPERR;
				goto fail;
			}
			(void)memcpy(key, out.ypresp_all_u.val.key.keydat_val,
			    size);
			key[size] = '\0';

			size = out.ypresp_all_u.val.val.valdat_len;
			if ((val = malloc(size + 1)) == NULL) {
				free(key);
				*objp = (u_long)YP_YPERR;
				goto fail;
			}
			(void)memcpy(val, out.ypresp_all_u.val.val.valdat_val,
			    size);
			val[size] = '\0';

			done = (*ypresp_allfn)(status, key,
			    out.ypresp_all_u.val.key.keydat_len, val,
			    out.ypresp_all_u.val.val.valdat_len, ypresp_data);
			free(key);
			free(val);
		} else
			done = 1;
		if (status != YP_NOMORE)
			*objp = status;
		rc = TRUE;
fail:
		xdr_free(xdr_ypresp_all, (char *)&out);
	}
	return rc;
}

int
yp_all(const char *dom, const char *inmap, struct ypall_callback *incallback)
{
	struct ypreq_nokey yprnk;
	struct dom_binding ypbinding;
	struct timeval  tv;
	int connected = 1;
	u_long		status;
	int		r = 0, s;

	if (dom == NULL || strlen(dom) == 0)
		return YPERR_BADARGS;

	if (strlen(dom) > YPMAXDOMAIN || inmap == NULL ||
	    *inmap == '\0' || strlen(inmap) > YPMAXMAP || incallback == NULL)
		return YPERR_BADARGS;

again:
	s = ypconnect(SOCK_STREAM);
	if (s == -1)
		return YPERR_DOMAIN;	/* YP not running */
	ypbinding.dom_socket = s;
	ypbinding.dom_server_addr.sin_port = -1; /* don't consult portmap */

	ypbinding.dom_client = clnttcp_create(&ypbinding.dom_server_addr,
	    YPPROG, YPVERS, &ypbinding.dom_socket, 0, 0);
	if (ypbinding.dom_client == NULL) {
		close(ypbinding.dom_socket);
		ypbinding.dom_socket = -1;
		clnt_pcreateerror("clnttcp_create");
		goto again;
	}
	clnt_control(ypbinding.dom_client, CLSET_CONNECTED, &connected);

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;
	yprnk.domain = (char *)dom;
	yprnk.map = (char *)inmap;
	ypresp_allfn = incallback->foreach;
	ypresp_data = (void *) incallback->data;
	(void) clnt_call(ypbinding.dom_client, YPPROC_ALL,
	    xdr_ypreq_nokey, &yprnk, _xdr_ypresp_all_seq, &status, tv);
	close(ypbinding.dom_socket);
	clnt_destroy(ypbinding.dom_client);

	if (status != YP_FALSE)
		r = ypprot_err(status);
	return r;
}
