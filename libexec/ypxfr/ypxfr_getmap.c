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
#include <time.h>
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include "ypxfr_extern.h"

extern bool_t xdr_ypresp_all_seq(XDR *, unsigned long *);

extern int (*ypresp_allfn)();
extern void *ypresp_data;
extern DB *specdbp;
extern enum ypstat yp_errno;

/*
 * This is largely the same as yp_all() except we do the transfer
 * from a specific server without the aid of ypbind(8). We need to
 * be able to specify the source host explicitly since ypxfr may
 * only transfer maps from the NIS master server for any given domain.
 * However, if we use the libc version of yp_all(), we could end up
 * talking to one of the slaves instead. We do need to dig into libc
 * a little though, since it contains the magic XDR function we need.
 */
int
ypxfr_get_map(char *map, char *domain, char *host,
    int (*callback)(int, char *, int, char *, int, char*))
{
	CLIENT *clnt;
	ypreq_nokey req;
	unsigned long status;
	struct timeval timeout;

	timeout.tv_usec = 0;
	timeout.tv_sec = 10;

	/* YPPROC_ALL is a TCP service */
	if ((clnt = clnt_create(host, YPPROG, YPVERS, "tcp")) == NULL) {
		yp_error("%s", clnt_spcreateerror("failed to \
create tcp handle"));
		yp_errno = (enum ypstat)YPXFR_YPERR;
		return(1);
	}

	req.domain = domain;
	req.map = map;
	ypresp_allfn = callback;
	ypresp_data = NULL;

	(void)clnt_call(clnt, YPPROC_ALL, (xdrproc_t)xdr_ypreq_nokey, &req,
	    (xdrproc_t)xdr_ypresp_all_seq, &status, timeout);

	clnt_destroy(clnt);

	if (status == YP_NOMORE)
		return(0);

	if (status != YP_TRUE) {
		yp_errno = (enum ypstat)YPXFR_YPERR;
		return(1);
	}

	return(0);
}
