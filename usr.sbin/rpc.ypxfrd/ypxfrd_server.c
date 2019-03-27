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

#include "ypxfrd.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <machine/endian.h>
#include "ypxfrd_extern.h"

int forked = 0;
int children = 0;
int fp = 0;

static bool_t
xdr_my_xfr(register XDR *xdrs, xfr *objp)
{
	unsigned char buf[XFRBLOCKSIZE];

	while (1) {
		if ((objp->xfr_u.xfrblock_buf.xfrblock_buf_len =
		    read(fp, &buf, XFRBLOCKSIZE)) != -1) {
			objp->ok = TRUE;
			objp->xfr_u.xfrblock_buf.xfrblock_buf_val = (char *)&buf;
		} else {
			objp->ok = FALSE;
			objp->xfr_u.xfrstat = XFR_READ_ERR;
			yp_error("read error: %s", strerror(errno));
		}

		/* Serialize */
		if (!xdr_xfr(xdrs, objp))
			return(FALSE);
		if (objp->ok == FALSE)
			return(TRUE);
		if (objp->xfr_u.xfrblock_buf.xfrblock_buf_len < XFRBLOCKSIZE) {
			objp->ok = FALSE;
			objp->xfr_u.xfrstat = XFR_DONE;
			if (!xdr_xfr(xdrs, objp))
				return(FALSE);
			return(TRUE);
		}
	}
}

struct xfr *
ypxfrd_getmap_1_svc(ypxfr_mapname *argp, struct svc_req *rqstp)
{
	static struct xfr  result;
	char buf[MAXPATHLEN];

	result.ok = FALSE;
	result.xfr_u.xfrstat = XFR_DENIED;

	if (yp_validdomain(argp->xfrdomain)) {
		return(&result);
	}

	if (yp_access(argp->xfrmap, (struct svc_req *)rqstp)) {
		return(&result);
	}

	snprintf (buf, sizeof(buf), "%s/%s/%s", yp_dir, argp->xfrdomain,
							argp->xfrmap);
	if (access(buf, R_OK) == -1) {
		result.xfr_u.xfrstat = XFR_ACCESS;
		return(&result);
	}

	if (argp->xfr_db_type != XFR_DB_BSD_HASH &&
	    argp->xfr_db_type != XFR_DB_ANY) {
		result.xfr_u.xfrstat = XFR_DB_TYPE_MISMATCH;
		return(&result);
	}

#if BYTE_ORDER == LITTLE_ENDIAN
	if (argp->xfr_byte_order == XFR_ENDIAN_BIG) {
#else
	if (argp->xfr_byte_order == XFR_ENDIAN_LITTLE) {
#endif
		result.xfr_u.xfrstat = XFR_DB_ENDIAN_MISMATCH;
		return(&result);
	}

#ifndef DEBUG
	if (children < MAX_CHILDREN && fork()) {
		children++;
		forked = 0;
		return (NULL);
	} else {
		forked++;
	}
#endif
	if ((fp = open(buf, O_RDONLY)) == -1) {
		result.xfr_u.xfrstat = XFR_READ_ERR;
		return(&result);
	}

	/* Start sending the file. */

	svc_sendreply(rqstp->rq_xprt, (xdrproc_t)xdr_my_xfr, &result);

	close(fp);

	return (NULL);
}
