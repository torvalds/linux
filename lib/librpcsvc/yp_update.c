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
 *
 * ypupdate client-side library function.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Center for Telecommunications Research
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/ypupdate_prot.h>
#include <rpc/key_prot.h>

#ifndef WINDOW
#define WINDOW (60*60)
#endif

#ifndef TIMEOUT
#define TIMEOUT 300
#endif

int
yp_update(char *domain, char *map, unsigned int ypop, char *key, int keylen,
    char *data, int datalen)
{
	char *master;
	int rval;
	unsigned int res;
	struct ypupdate_args upargs;
	struct ypdelete_args delargs;
	CLIENT *clnt;
	char netname[MAXNETNAMELEN+1];
	des_block des_key;
	struct timeval timeout;

	/* Get the master server name for 'domain.' */
	if ((rval = yp_master(domain, map, &master)))
		return(rval);

	/* Check that ypupdated is running there. */
	if (getrpcport(master, YPU_PROG, YPU_VERS, ypop))
		return(YPERR_DOMAIN);

	/* Get a handle. */
	if ((clnt = clnt_create(master, YPU_PROG, YPU_VERS, "tcp")) == NULL)
		return(YPERR_RPC);

	/*
	 * Assemble netname of server.
	 * NOTE: It's difficult to discern from the documentation, but
	 * when you make a Secure RPC call, the netname you pass should
	 * be the netname of the guy on the other side, not your own
	 * netname. This is how the client side knows what public key
	 * to use for the initial exchange. Passing your own netname
	 * only works if the server on the other side is running under
	 * your UID.
	 */
	if (!host2netname(netname, master, domain)) {
		clnt_destroy(clnt);
		return(YPERR_BADARGS);
	}

	/* Make up a DES session key. */
	key_gendes(&des_key);

	/* Set up DES authentication. */
	if ((clnt->cl_auth = (AUTH *)authdes_create(netname, WINDOW, NULL,
			&des_key)) == NULL) {
		clnt_destroy(clnt);
		return(YPERR_RESRC);
	}

	/* Set a timeout for clnt_call(). */
	timeout.tv_usec = 0;
	timeout.tv_sec = TIMEOUT;

	/*
	 * Make the call. Note that we use clnt_call() here rather than
	 * the rpcgen-erated client stubs. We could use those stubs, but
	 * then we'd have to do some gymnastics to get at the error
	 * information to figure out what error code to send back to the
	 * caller. With clnt_call(), we get the error status returned to
	 * us right away, and we only have to exert a small amount of
	 * extra effort.
	 */
	switch (ypop) {
	case YPOP_CHANGE:
		upargs.mapname = map;
		upargs.key.yp_buf_len = keylen;
		upargs.key.yp_buf_val = key;
		upargs.datum.yp_buf_len = datalen;
		upargs.datum.yp_buf_val = data;

		if ((rval = clnt_call(clnt, YPU_CHANGE,
			(xdrproc_t)xdr_ypupdate_args, &upargs,
			(xdrproc_t)xdr_u_int, &res, timeout)) != RPC_SUCCESS) {
			if (rval == RPC_AUTHERROR)
				res = YPERR_ACCESS;
			else
				res = YPERR_RPC;
		}

		break;
	case YPOP_INSERT:
		upargs.mapname = map;
		upargs.key.yp_buf_len = keylen;
		upargs.key.yp_buf_val = key;
		upargs.datum.yp_buf_len = datalen;
		upargs.datum.yp_buf_val = data;

		if ((rval = clnt_call(clnt, YPU_INSERT,
			(xdrproc_t)xdr_ypupdate_args, &upargs,
			(xdrproc_t)xdr_u_int, &res, timeout)) != RPC_SUCCESS) {
			if (rval == RPC_AUTHERROR)
				res = YPERR_ACCESS;
			else
				res = YPERR_RPC;
		}

		break;
	case YPOP_DELETE:
		delargs.mapname = map;
		delargs.key.yp_buf_len = keylen;
		delargs.key.yp_buf_val = key;

		if ((rval = clnt_call(clnt, YPU_DELETE,
			(xdrproc_t)xdr_ypdelete_args, &delargs,
			(xdrproc_t)xdr_u_int, &res, timeout)) != RPC_SUCCESS) {
			if (rval == RPC_AUTHERROR)
				res = YPERR_ACCESS;
			else
				res = YPERR_RPC;
		}

		break;
	case YPOP_STORE:
		upargs.mapname = map;
		upargs.key.yp_buf_len = keylen;
		upargs.key.yp_buf_val = key;
		upargs.datum.yp_buf_len = datalen;
		upargs.datum.yp_buf_val = data;

		if ((rval = clnt_call(clnt, YPU_STORE,
			(xdrproc_t)xdr_ypupdate_args, &upargs,
			(xdrproc_t)xdr_u_int, &res, timeout)) != RPC_SUCCESS) {
			if (rval == RPC_AUTHERROR)
				res = YPERR_ACCESS;
			else
				res = YPERR_RPC;
		}

		break;
	default:
		res = YPERR_BADARGS;
		break;
	}

	/* All done: tear down the connection. */
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	free(master);

	return(res);
}
