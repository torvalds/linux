/*	$OpenBSD: yp_first.c,v 1.12 2022/08/02 16:59:29 deraadt Exp $ */
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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "ypinternal.h"

int
yp_first(const char *indomain, const char *inmap, char **outkey, int *outkeylen,
    char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_nokey yprnk;
	struct dom_binding *ysd = NULL;
	struct timeval  tv;
	int tries = 0, r;

	if (indomain == NULL || *indomain == '\0' ||
	    strlen(indomain) > YPMAXDOMAIN || inmap == NULL ||
	    *inmap == '\0' || strlen(inmap) > YPMAXMAP)
		return YPERR_BADARGS;

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

again:
	if (_yp_dobind(indomain, &ysd) != 0)
		return YPERR_DOMAIN;

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprnk.domain = (char *)indomain;
	yprnk.map = (char *)inmap;
	(void)memset(&yprkv, 0, sizeof yprkv);

	r = clnt_call(ysd->dom_client, YPPROC_FIRST,
	    xdr_ypreq_nokey, &yprnk, xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS) {
		if (tries++)
			clnt_perror(ysd->dom_client, "yp_first: clnt_call");
		_yp_unbind(ysd);
		goto again;
	}
	if (!(r = ypprot_err(yprkv.stat))) {
		*outkeylen = yprkv.key.keydat_len;
		*outvallen = yprkv.val.valdat_len;
		if ((*outkey = malloc(*outkeylen + 1)) == NULL ||
		    (*outval = malloc(*outvallen + 1)) == NULL) {
			free(*outkey);
			r = YPERR_RESRC;
		} else {
			(void)memcpy(*outkey, yprkv.key.keydat_val, *outkeylen);
			(*outkey)[*outkeylen] = '\0';
			(void)memcpy(*outval, yprkv.val.valdat_val, *outvallen);
			(*outval)[*outvallen] = '\0';
		}
	}
	xdr_free(xdr_ypresp_key_val, (char *) &yprkv);
	_yp_unbind(ysd);
	return r;
}
DEF_WEAK(yp_first);
