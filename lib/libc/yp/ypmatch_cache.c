/*	$OpenBSD: ypmatch_cache.c,v 1.18 2022/08/02 16:59:30 deraadt Exp $ */
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
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "ypinternal.h"

#ifdef YPMATCHCACHE
static bool_t ypmatch_add(const char *, const char *, u_int, char *, u_int);
static bool_t ypmatch_find(const char *, const char *, u_int, char **, u_int *);

static struct ypmatch_ent {
	struct ypmatch_ent	*next;
	char			*map, *key;
	char			*val;
	int			 keylen, vallen;
	time_t			 expire_t;
} *ypmc;

int _yplib_cache = 5;

static bool_t
ypmatch_add(const char *map, const char *key, u_int keylen, char *val,
    u_int vallen)
{
	struct ypmatch_ent *ep;
	char *newmap = NULL, *newkey = NULL, *newval = NULL;
	time_t t;

	if (keylen == 0 || vallen == 0)
		return (0);

	(void)time(&t);

	/* Allocate all required memory first. */
	if ((newmap = strdup(map)) == NULL ||
	    (newkey = malloc(keylen)) == NULL ||
	    (newval = malloc(vallen)) == NULL) {
		free(newkey);
		free(newmap);
		return 0;
	}

	for (ep = ypmc; ep; ep = ep->next)
		if (ep->expire_t < t)
			break;

	if (ep == NULL) {
		/* No expired node, create a new one. */
		if ((ep = malloc(sizeof *ep)) == NULL) {
			free(newval);
			free(newkey);
			free(newmap);
			return 0;
		}
		ep->next = ypmc;
		ypmc = ep;
	} else {
		/* Reuse the first expired node from the list. */
		free(ep->val);
		free(ep->key);
		free(ep->map);
	}

	/* Now we have all the memory we need, copy the data in. */
	(void)memcpy(newkey, key, keylen);
	(void)memcpy(newval, val, vallen);
	ep->map = newmap;
	ep->key = newkey;
	ep->val = newval;
	ep->keylen = keylen;
	ep->vallen = vallen;
	ep->expire_t = t + _yplib_cache;
	return 1;
}

static bool_t
ypmatch_find(const char *map, const char *key, u_int keylen, char **val,
    u_int *vallen)
{
	struct ypmatch_ent *ep;
	time_t          t;

	if (ypmc == NULL)
		return 0;

	(void) time(&t);

	for (ep = ypmc; ep; ep = ep->next) {
		if (ep->keylen != keylen)
			continue;
		if (strcmp(ep->map, map))
			continue;
		if (memcmp(ep->key, key, keylen))
			continue;
		if (t > ep->expire_t)
			continue;

		*val = ep->val;
		*vallen = ep->vallen;
		return 1;
	}
	return 0;
}
#endif

int
yp_match(const char *indomain, const char *inmap, const char *inkey,
    int inkeylen, char **outval, int *outvallen)
{
	struct dom_binding *ysd;
	struct ypresp_val yprv;
	struct timeval  tv;
	struct ypreq_key yprk;
	int tries = 0, r;

	if (indomain == NULL || *indomain == '\0' ||
	    strlen(indomain) > YPMAXDOMAIN || inmap == NULL ||
	    *inmap == '\0' || strlen(inmap) > YPMAXMAP ||
	    inkey == NULL || inkeylen == 0 || inkeylen >= YPMAXRECORD)
		return YPERR_BADARGS;

	*outval = NULL;
	*outvallen = 0;

again:
	if (_yp_dobind(indomain, &ysd) != 0)
		return YPERR_DOMAIN;

#ifdef YPMATCHCACHE
	if (!strcmp(_yp_domain, indomain) && ypmatch_find(inmap, inkey,
	    inkeylen, &yprv.val.valdat_val, &yprv.val.valdat_len)) {
		*outvallen = yprv.val.valdat_len;
		if ((*outval = malloc(*outvallen + 1)) == NULL) {
			_yp_unbind(ysd);
			return YPERR_RESRC;
		}
		(void)memcpy(*outval, yprv.val.valdat_val, *outvallen);
		(*outval)[*outvallen] = '\0';
		_yp_unbind(ysd);
		return 0;
	}
#endif

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprk.domain = (char *)indomain;
	yprk.map = (char *)inmap;
	yprk.key.keydat_val = (char *) inkey;
	yprk.key.keydat_len = inkeylen;

	memset(&yprv, 0, sizeof yprv);

	r = clnt_call(ysd->dom_client, YPPROC_MATCH,
	    xdr_ypreq_key, &yprk, xdr_ypresp_val, &yprv, tv);
	if (r != RPC_SUCCESS) {
		if (tries++)
			clnt_perror(ysd->dom_client, "yp_match: clnt_call");
		_yp_unbind(ysd);
		goto again;
	}
	if (!(r = ypprot_err(yprv.stat))) {
		*outvallen = yprv.val.valdat_len;
		if ((*outval = malloc(*outvallen + 1)) == NULL) {
			r = YPERR_RESRC;
			goto out;
		}
		(void)memcpy(*outval, yprv.val.valdat_val, *outvallen);
		(*outval)[*outvallen] = '\0';
#ifdef YPMATCHCACHE
		if (strcmp(_yp_domain, indomain) == 0)
			(void)ypmatch_add(inmap, inkey, inkeylen,
			    *outval, *outvallen);
#endif
	}
out:
	xdr_free(xdr_ypresp_val, (char *) &yprv);
	_yp_unbind(ysd);
	return r;
}
DEF_WEAK(yp_match);

int
yp_next(const char *indomain, const char *inmap, const char *inkey,
    int inkeylen, char **outkey, int *outkeylen, char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_key yprk;
	struct dom_binding *ysd;
	struct timeval  tv;
	int tries = 0, r;

	if (indomain == NULL || *indomain == '\0' ||
	    strlen(indomain) > YPMAXDOMAIN || inmap == NULL ||
	    *inmap == '\0' || strlen(inmap) > YPMAXMAP ||
	    inkeylen == 0 || inkeylen >= YPMAXRECORD)
		return YPERR_BADARGS;

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

again:
	if (_yp_dobind(indomain, &ysd) != 0)
		return YPERR_DOMAIN;

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprk.domain = (char *)indomain;
	yprk.map = (char *)inmap;
	yprk.key.keydat_val = (char *)inkey;
	yprk.key.keydat_len = inkeylen;
	(void)memset(&yprkv, 0, sizeof yprkv);

	r = clnt_call(ysd->dom_client, YPPROC_NEXT,
	    xdr_ypreq_key, &yprk, xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS) {
		if (tries++)
			clnt_perror(ysd->dom_client, "yp_next: clnt_call");
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
DEF_WEAK(yp_next);
