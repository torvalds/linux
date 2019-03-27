/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include "ypxfr_extern.h"

const char *
ypxfrerr_string(ypxfrstat code)
{
	switch (code) {
	case YPXFR_SUCC:
		return ("Map successfully transferred");
		break;
	case YPXFR_AGE:
		return ("Master's version not newer");
		break;
	case YPXFR_NOMAP:
		return ("No such map in server's domain");
		break;
	case YPXFR_NODOM:
		return ("Domain not supported by server");
		break;
	case YPXFR_RSRC:
		return ("Local resource allocation failure");
		break;
	case YPXFR_RPC:
		return ("RPC failure talking to server");
		break;
	case YPXFR_MADDR:
		return ("Could not get master server address");
		break;
	case YPXFR_YPERR:
		return ("NIS server/map database error");
		break;
	case YPXFR_BADARGS:
		return ("Request arguments bad");
		break;
	case YPXFR_DBM:
		return ("Local database operation failed");
		break;
	case YPXFR_FILE:
		return ("Local file I/O operation failed");
		break;
	case YPXFR_SKEW:
		return ("Map version skew during transfer");
		break;
	case YPXFR_CLEAR:
		return ("Couldn't send \"clear\" request to local ypserv");
		break;
	case YPXFR_FORCE:
		return ("No local order number in map -- use -f flag");
		break;
	case YPXFR_XFRERR:
		return ("General ypxfr error");
		break;
	case YPXFR_REFUSED:
		return ("Transfer request refused by ypserv");
		break;
	default:
		return ("Unknown error code");
		break;
	}
}

/*
 * These are wrappers for the usual yp_master() and yp_order() functions.
 * They can use either local yplib functions (the real yp_master() and
 * yp_order()) or do direct RPCs to a specified server. The latter is
 * necessary if ypxfr is run on a machine that isn't configured as an
 * NIS client (this can happen very easily: a given machine need not be
 * an NIS client in order to be an NIS server).
 */

/*
 * Careful: yp_master() returns a pointer to a dynamically allocated
 * buffer. Calling ypproc_master_2() ourselves also returns a pointer
 * to dynamically allocated memory, though this time it's memory
 * allocated by the XDR routines. We have to rememver to free() or
 * xdr_free() the memory as required to avoid leaking memory.
 */
char *
ypxfr_get_master(char *domain, char *map, char *source, const int yplib)
{
	static char mastername[MAXPATHLEN + 2];

	bzero((char *)&mastername, sizeof(mastername));

	if (yplib) {
		int res;
		char *master;
		if ((res = yp_master(domain, map, &master))) {
			switch (res) {
			case YPERR_DOMAIN:
				yp_errno = (enum ypstat)YPXFR_NODOM;
				break;
			case YPERR_MAP:
				yp_errno = (enum ypstat)YPXFR_NOMAP;
				break;
			case YPERR_YPERR:
			default:
				yp_errno = (enum ypstat)YPXFR_YPERR;
				break;
			}
			return(NULL);
		} else {
			snprintf(mastername, sizeof(mastername), "%s", master);
			free(master);
			return((char *)&mastername);
		}
	} else {
		CLIENT *clnt;
		ypresp_master *resp;
		ypreq_nokey req;

		if ((clnt = clnt_create(source,YPPROG,YPVERS,"udp")) == NULL) {
			yp_error("%s",clnt_spcreateerror("failed to \
create udp handle to ypserv"));
			yp_errno = (enum ypstat)YPXFR_RPC;
			return(NULL);
		}

		req.map = map;
		req.domain = domain;
		if ((resp = ypproc_master_2(&req, clnt)) == NULL) {
			yp_error("%s",clnt_sperror(clnt,"YPPROC_MASTER \
failed"));
			clnt_destroy(clnt);
			yp_errno = (enum ypstat)YPXFR_RPC;
			return(NULL);
		}
		clnt_destroy(clnt);
		if (resp->stat != YP_TRUE) {
			switch (resp->stat) {
			case YP_NODOM:
				yp_errno = (enum ypstat)YPXFR_NODOM;
				break;
			case YP_NOMAP:
				yp_errno = (enum ypstat)YPXFR_NOMAP;
				break;
			case YP_YPERR:
			default:
				yp_errno = (enum ypstat)YPXFR_YPERR;
				break;
			}
			return(NULL);
		}
		snprintf(mastername, sizeof(mastername), "%s", resp->peer);
/*		xdr_free(xdr_ypresp_master, (char *)&resp); */
		return((char *)&mastername);
	}
}

unsigned long
ypxfr_get_order(char *domain, char *map, char *source, const int yplib)
{
	if (yplib) {
		unsigned int order;
		int res;
		if ((res = yp_order(domain, map, &order))) {
			switch (res) {
			case YPERR_DOMAIN:
				yp_errno = (enum ypstat)YPXFR_NODOM;
				break;
			case YPERR_MAP:
				yp_errno = (enum ypstat)YPXFR_NOMAP;
				break;
			case YPERR_YPERR:
			default:
				yp_errno = (enum ypstat)YPXFR_YPERR;
				break;
			}
			return(0);
		} else
			return(order);
	} else {
		CLIENT *clnt;
		ypresp_order *resp;
		ypreq_nokey req;

		if ((clnt = clnt_create(source,YPPROG,YPVERS,"udp")) == NULL) {
			yp_error("%s",clnt_spcreateerror("couldn't create \
udp handle to ypserv"));
			yp_errno = (enum ypstat)YPXFR_RPC;
			return(0);
		}
		req.map = map;
		req.domain = domain;
		if ((resp = ypproc_order_2(&req, clnt)) == NULL) {
			yp_error("%s", clnt_sperror(clnt, "YPPROC_ORDER \
failed"));
			clnt_destroy(clnt);
			yp_errno = (enum ypstat)YPXFR_RPC;
			return(0);
		}
		clnt_destroy(clnt);
		if (resp->stat != YP_TRUE) {
			switch (resp->stat) {
			case YP_NODOM:
				yp_errno = (enum ypstat)YPXFR_NODOM;
				break;
			case YP_NOMAP:
				yp_errno = (enum ypstat)YPXFR_NOMAP;
				break;
			case YP_YPERR:
			default:
				yp_errno = (enum ypstat)YPXFR_YPERR;
				break;
			}
			return(0);
		}
		return(resp->ordernum);
	}
}

int
ypxfr_match(char *server, char *domain, char *map, char *key,
    unsigned long keylen)
{
	ypreq_key ypkey;
	ypresp_val *ypval;
	CLIENT *clnt;
	static char buf[YPMAXRECORD + 2];

	bzero(buf, sizeof(buf));

	if ((clnt = clnt_create(server, YPPROG,YPVERS,"udp")) == NULL) {
		yp_error("failed to create UDP handle: %s",
					clnt_spcreateerror(server));
		return(0);
	}

	ypkey.domain = domain;
	ypkey.map = map;
	ypkey.key.keydat_len = keylen;
	ypkey.key.keydat_val = key;

	if ((ypval = ypproc_match_2(&ypkey, clnt)) == NULL) {
		clnt_destroy(clnt);
		yp_error("%s: %s", server,
				clnt_sperror(clnt,"YPPROC_MATCH failed"));
		return(0);
	}

	clnt_destroy(clnt);

	if (ypval->stat != YP_TRUE) {
		xdr_free((xdrproc_t)xdr_ypresp_val, ypval);
		return(0);
	}

	xdr_free((xdrproc_t)xdr_ypresp_val, ypval);

	return(1);
}
