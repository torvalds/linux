/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <rpcsvc/ypxfrd.h>
#include <rpcsvc/yp.h>
#include <rpc/rpc.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ypxfr_extern.h"

static int fp = 0;

static bool_t
xdr_my_xfr(register XDR *xdrs, xfr *objp)
{
	while (1) {
		if (!xdr_xfr(xdrs, objp))
			return(FALSE);
		if (objp->ok == TRUE) {
			if (write(fp, objp->xfr_u.xfrblock_buf.xfrblock_buf_val,
			    objp->xfr_u.xfrblock_buf.xfrblock_buf_len) == -1) {
				yp_error("write failed: %s", strerror(errno));
				return(FALSE);
			}
		}
		xdr_free((xdrproc_t)xdr_xfr, (char *)objp);
		if (objp->ok == FALSE) {
			switch (objp->xfr_u.xfrstat) {
			case(XFR_DONE):
				return(TRUE);
				break;
			case(XFR_READ_ERR):
				yp_error("got read error from rpc.ypxfrd");
				return(FALSE);
				break;
			case(XFR_ACCESS):
				yp_error("rpc.ypxfrd couldn't access the map");
				return(FALSE);
				break;
			case(XFR_DENIED):
				yp_error("access to map denied by rpc.ypxfrd");
				return(FALSE);
				break;
			case(XFR_DB_TYPE_MISMATCH):
				yp_error("client/server DB type mismatch");
				return(FALSE);
				break;
			case(XFR_DB_ENDIAN_MISMATCH):
				yp_error("client/server byte order mismatch");
				return(FALSE);
				break;
			default:
				yp_error("got unknown status from rpc.ypxfrd");
				return(FALSE);
				break;
			}
		}
	}
}

#define PERM_SECURE (S_IRUSR|S_IWUSR)

int
ypxfrd_get_map(char *host, char *map, char *domain, char *tmpname)
{
	CLIENT *clnt;
	struct ypxfr_mapname req;
	struct xfr resp;
	struct timeval timeout = { 0, 25 };
	int status = 0;

	req.xfrmap = map;
	req.xfrdomain = domain;
	req.xfrmap_filename = "";
	req.xfr_db_type = XFR_DB_BSD_HASH;	/*
	req.xfr_byte_order = XFR_ENDIAN_ANY;	 * Berkeley DB isn't
						 * byte-order sensitive.
						 */

	bzero((char *)&resp, sizeof(resp));

	if ((clnt = clnt_create(host, YPXFRD_FREEBSD_PROG,
				YPXFRD_FREEBSD_VERS, "tcp")) == NULL) {
		return(1);
	}

	if ((fp = open(tmpname, O_RDWR|O_CREAT, PERM_SECURE)) == -1) {
		clnt_destroy(clnt);
		yp_error("couldn't open %s: %s", tmpname, strerror(errno));
		return(1);
	}

	if (clnt_call(clnt,YPXFRD_GETMAP,
			(xdrproc_t)xdr_ypxfr_mapname, (char *)&req,
			(xdrproc_t)xdr_my_xfr, (char *)&resp,
			timeout) != RPC_SUCCESS) {
		yp_error("%s", clnt_sperror(clnt,"call to rpc.ypxfrd failed"));
		status++;
		unlink(tmpname);
	}

	clnt_destroy(clnt);
	close(fp);
	return(status);
}
