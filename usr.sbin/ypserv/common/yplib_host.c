/*	$OpenBSD: yplib_host.c,v 1.20 2018/04/26 12:42:51 guenther Exp $ */

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

#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "yplib_host.h"

int _yplib_host_timeout = 10;

CLIENT *
yp_bind_host(char *server, u_long program, u_long version, u_short port,
    int usetcp)
{
	struct sockaddr_in rsrv_sin;
	static CLIENT *client;
	struct hostent *h;
	struct timeval tv;
	int rsrv_sock;

	memset(&rsrv_sin, 0, sizeof rsrv_sin);
	rsrv_sin.sin_len = sizeof rsrv_sin;
	rsrv_sin.sin_family = AF_INET;
	rsrv_sock = RPC_ANYSOCK;
	if (port != 0)
		rsrv_sin.sin_port = htons(port);

	if (*server >= '0' && *server <= '9') {
		if (inet_aton(server, &rsrv_sin.sin_addr) == 0) {
			fprintf(stderr, "inet_aton: invalid address %s.\n",
			    server);
			exit(1);
		}
	} else {
		h = gethostbyname(server);
		if (h == NULL) {
			fprintf(stderr, "gethostbyname: unknown host %s.\n",
			    server);
			exit(1);
		}
		rsrv_sin.sin_addr.s_addr = *(u_int32_t *)h->h_addr;
	}

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	if (usetcp)
		client = clnttcp_create(&rsrv_sin, program, version,
		    &rsrv_sock, 0, 0);
	else
		client = clntudp_create(&rsrv_sin, program, version, tv,
		    &rsrv_sock);

	if (client == NULL) {
		fprintf(stderr, "clntudp_create: no contact with host %s.\n",
		    server);
		exit(1);
	}
	return(client);
}

CLIENT *
yp_bind_local(u_long program, u_long version)
{
	struct sockaddr_in rsrv_sin;
	static CLIENT *client;
	struct timeval tv;
	int rsrv_sock;

	memset(&rsrv_sin, 0, sizeof rsrv_sin);
	rsrv_sin.sin_len = sizeof rsrv_sin;
	rsrv_sin.sin_family = AF_INET;
	rsrv_sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	rsrv_sock = RPC_ANYSOCK;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	client = clntudp_create(&rsrv_sin, program, version, tv, &rsrv_sock);
	if (client == NULL) {
		fprintf(stderr,"clntudp_create: no contact with localhost.\n");
		exit(1);
	}
	return(client);
}

int
yp_match_host(CLIENT *client, char *indomain, char *inmap, const char *inkey,
    int inkeylen, char **outval, int *outvallen)
{
	struct ypresp_val yprv;
	struct ypreq_key yprk;
	struct timeval tv;
	int r;

	*outval = NULL;
	*outvallen = 0;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	yprk.domain = indomain;
	yprk.map = inmap;
	yprk.key.keydat_val = (char *)inkey;
	yprk.key.keydat_len = inkeylen;

	memset(&yprv, 0, sizeof yprv);

	r = clnt_call(client, YPPROC_MATCH,
	    xdr_ypreq_key, &yprk, xdr_ypresp_val, &yprv, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_match_host: clnt_call");
	if ( !(r = ypprot_err(yprv.stat)) ) {
		*outvallen = yprv.val.valdat_len;
		*outval = malloc(*outvallen+1);
		memcpy(*outval, yprv.val.valdat_val, *outvallen);
		(*outval)[*outvallen] = '\0';
	}
	xdr_free(xdr_ypresp_val, (char *)&yprv);
	return r;
}

int
yp_first_host(CLIENT *client, char *indomain, char *inmap, char **outkey,
    int *outkeylen, char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_nokey yprnk;
	struct timeval tv;
	int r;

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;
	memset(&yprkv, 0, sizeof yprkv);

	r = clnt_call(client, YPPROC_FIRST,
	    xdr_ypreq_nokey, &yprnk, xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_first_host: clnt_call");
	if ( !(r = ypprot_err(yprkv.stat)) ) {
		*outkeylen = yprkv.key.keydat_len;
		*outkey = malloc(*outkeylen+1);
		memcpy(*outkey, yprkv.key.keydat_val, *outkeylen);
		(*outkey)[*outkeylen] = '\0';
		*outvallen = yprkv.val.valdat_len;
		*outval = malloc(*outvallen+1);
		memcpy(*outval, yprkv.val.valdat_val, *outvallen);
		(*outval)[*outvallen] = '\0';
	}
	xdr_free(xdr_ypresp_key_val, (char *)&yprkv);
	return r;
}

int
yp_next_host(CLIENT *client, char *indomain, char *inmap, char *inkey,
    int inkeylen, char **outkey, int *outkeylen, char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_key yprk;
	struct timeval tv;
	int r;

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	yprk.domain = indomain;
	yprk.map = inmap;
	yprk.key.keydat_val = inkey;
	yprk.key.keydat_len = inkeylen;
	memset(&yprkv, 0, sizeof yprkv);

	r = clnt_call(client, YPPROC_NEXT,
	    xdr_ypreq_key, &yprk, xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_next_host: clnt_call");
	if ( !(r = ypprot_err(yprkv.stat)) ) {
		*outkeylen = yprkv.key.keydat_len;
		*outkey = malloc(*outkeylen+1);
		memcpy(*outkey, yprkv.key.keydat_val, *outkeylen);
		(*outkey)[*outkeylen] = '\0';
		*outvallen = yprkv.val.valdat_len;
		*outval = malloc(*outvallen+1);
		memcpy(*outval, yprkv.val.valdat_val, *outvallen);
		(*outval)[*outvallen] = '\0';
	}
	xdr_free(xdr_ypresp_key_val, (char *)&yprkv);
	return r;
}

int (*ypserv_ypresp_allfn)(u_long, char *, int, char *, int, void *);
void *ypserv_ypresp_data;

bool_t
ypserv_xdr_ypresp_all_seq(XDR *xdrs, u_long *objp)
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

			done = (*ypserv_ypresp_allfn)(status, key,
			    out.ypresp_all_u.val.key.keydat_len, val,
			    out.ypresp_all_u.val.val.valdat_len, ypserv_ypresp_data);
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
yp_all_host(CLIENT *client, char *indomain, char *inmap,
    struct ypall_callback *incallback)
{
	struct ypreq_nokey yprnk;
	struct timeval tv;
	u_long status;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;
	ypserv_ypresp_allfn = incallback->foreach;
	ypserv_ypresp_data = (void *)incallback->data;

	(void) clnt_call(client, YPPROC_ALL,
	    xdr_ypreq_nokey, &yprnk, ypserv_xdr_ypresp_all_seq, &status, tv);
	if (status != YP_FALSE)
		return ypprot_err(status);
	return 0;
}

int
yp_order_host(CLIENT *client, char *indomain, char *inmap, u_int32_t *outorder)
{
	struct ypresp_order ypro;
	struct ypreq_nokey yprnk;
	struct timeval tv;
	int r;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;

	memset(&ypro, 0, sizeof ypro);

	r = clnt_call(client, YPPROC_ORDER,
	    xdr_ypreq_nokey, &yprnk, xdr_ypresp_order, &ypro, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_order_host: clnt_call");
	*outorder = ypro.ordernum;
	xdr_free(xdr_ypresp_order, (char *)&ypro);
	return ypprot_err(ypro.stat);
}

int
yp_master_host(CLIENT *client, char *indomain, char *inmap, char **outname)
{
	struct ypresp_master yprm;
	struct ypreq_nokey yprnk;
	struct timeval tv;
	int r;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;
	yprnk.domain = indomain;
	yprnk.map = inmap;

	memset(&yprm, 0, sizeof yprm);

	r = clnt_call(client, YPPROC_MASTER,
	    xdr_ypreq_nokey, &yprnk, xdr_ypresp_master, &yprm, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_master: clnt_call");
	if (!(r = ypprot_err(yprm.stat)))
		*outname = strdup(yprm.peer);
	xdr_free(xdr_ypresp_master, (char *)&yprm);
	return r;
}

int
yp_maplist_host(CLIENT *client, char *indomain, struct ypmaplist **outmaplist)
{
	struct ypresp_maplist ypml;
	struct timeval tv;
	int r;

	tv.tv_sec = _yplib_host_timeout;
	tv.tv_usec = 0;

	memset(&ypml, 0, sizeof ypml);

	r = clnt_call(client, YPPROC_MAPLIST,
	    xdr_domainname, &indomain, xdr_ypresp_maplist, &ypml, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_maplist: clnt_call");
	*outmaplist = ypml.maps;
	/* NO: xdr_free(xdr_ypresp_maplist, &ypml);*/
	return ypprot_err(ypml.stat);
}
