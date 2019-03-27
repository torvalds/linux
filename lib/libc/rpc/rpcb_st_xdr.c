/*	$NetBSD: rpcb_st_xdr.c,v 1.3 2000/07/14 08:40:42 fvdl Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright 1991 Sun Microsystems, Inc.
 * rpcb_stat_xdr.c
 */

/*
 * This file was generated from rpcb_prot.x, but includes only those
 * routines used with the rpcbind stats facility.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include "un-namespace.h"

/* Link list of all the stats about getport and getaddr */

bool_t
xdr_rpcbs_addrlist(XDR *xdrs, rpcbs_addrlist *objp)
{
	struct rpcbs_addrlist **pnext;

	    if (!xdr_rpcprog(xdrs, &objp->prog)) {
		return (FALSE);
	    }
	    if (!xdr_rpcvers(xdrs, &objp->vers)) {
		return (FALSE);
	    }
	    if (!xdr_int(xdrs, &objp->success)) {
		return (FALSE);
	    }
	    if (!xdr_int(xdrs, &objp->failure)) {
		return (FALSE);
	    }
	    if (!xdr_string(xdrs, &objp->netid, RPC_MAXDATASIZE)) {
		return (FALSE);
	    }

	    pnext = &objp->next;

	    if (!xdr_pointer(xdrs, (char **) pnext,
			sizeof (rpcbs_addrlist),
			(xdrproc_t)xdr_rpcbs_addrlist)) {
		return (FALSE);
	    }

	return (TRUE);
}

/* Link list of all the stats about rmtcall */

bool_t
xdr_rpcbs_rmtcalllist(XDR *xdrs, rpcbs_rmtcalllist *objp)
{
	int32_t *buf;
	struct rpcbs_rmtcalllist **pnext;

	if (xdrs->x_op == XDR_ENCODE) {
	buf = XDR_INLINE(xdrs, 6 * BYTES_PER_XDR_UNIT);
	if (buf == NULL) {
		if (!xdr_rpcprog(xdrs, &objp->prog)) {
			return (FALSE);
		}
		if (!xdr_rpcvers(xdrs, &objp->vers)) {
			return (FALSE);
		}
		if (!xdr_rpcproc(xdrs, &objp->proc)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->success)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->failure)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->indirect)) {
			return (FALSE);
		}
	} else {
		IXDR_PUT_U_INT32(buf, objp->prog);
		IXDR_PUT_U_INT32(buf, objp->vers);
		IXDR_PUT_U_INT32(buf, objp->proc);
		IXDR_PUT_INT32(buf, objp->success);
		IXDR_PUT_INT32(buf, objp->failure);
		IXDR_PUT_INT32(buf, objp->indirect);
	}
	if (!xdr_string(xdrs, &objp->netid, RPC_MAXDATASIZE)) {
		return (FALSE);
	}
	pnext = &objp->next;
	if (!xdr_pointer(xdrs, (char **) pnext,
			sizeof (rpcbs_rmtcalllist),
			(xdrproc_t)xdr_rpcbs_rmtcalllist)) {
		return (FALSE);
	}
	return (TRUE);
	} else if (xdrs->x_op == XDR_DECODE) {
	buf = XDR_INLINE(xdrs, 6 * BYTES_PER_XDR_UNIT);
	if (buf == NULL) {
		if (!xdr_rpcprog(xdrs, &objp->prog)) {
			return (FALSE);
		}
		if (!xdr_rpcvers(xdrs, &objp->vers)) {
			return (FALSE);
		}
		if (!xdr_rpcproc(xdrs, &objp->proc)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->success)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->failure)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &objp->indirect)) {
			return (FALSE);
		}
	} else {
		objp->prog = (rpcprog_t)IXDR_GET_U_INT32(buf);
		objp->vers = (rpcvers_t)IXDR_GET_U_INT32(buf);
		objp->proc = (rpcproc_t)IXDR_GET_U_INT32(buf);
		objp->success = (int)IXDR_GET_INT32(buf);
		objp->failure = (int)IXDR_GET_INT32(buf);
		objp->indirect = (int)IXDR_GET_INT32(buf);
	}
	if (!xdr_string(xdrs, &objp->netid, RPC_MAXDATASIZE)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **) pnext,
			sizeof (rpcbs_rmtcalllist),
			(xdrproc_t)xdr_rpcbs_rmtcalllist)) {
		return (FALSE);
	}
	return (TRUE);
	}
	if (!xdr_rpcprog(xdrs, &objp->prog)) {
		return (FALSE);
	}
	if (!xdr_rpcvers(xdrs, &objp->vers)) {
		return (FALSE);
	}
	if (!xdr_rpcproc(xdrs, &objp->proc)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->success)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->failure)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->indirect)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->netid, RPC_MAXDATASIZE)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **) pnext,
			sizeof (rpcbs_rmtcalllist),
			(xdrproc_t)xdr_rpcbs_rmtcalllist)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_rpcbs_proc(XDR *xdrs, rpcbs_proc objp)
{
	if (!xdr_vector(xdrs, (char *)(void *)objp, RPCBSTAT_HIGHPROC,
	    sizeof (int), (xdrproc_t)xdr_int)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_rpcbs_addrlist_ptr(XDR *xdrs, rpcbs_addrlist_ptr *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof (rpcbs_addrlist),
			(xdrproc_t)xdr_rpcbs_addrlist)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_rpcbs_rmtcalllist_ptr(XDR *xdrs, rpcbs_rmtcalllist_ptr *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof (rpcbs_rmtcalllist),
			(xdrproc_t)xdr_rpcbs_rmtcalllist)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_rpcb_stat(XDR *xdrs, rpcb_stat *objp)
{

	if (!xdr_rpcbs_proc(xdrs, objp->info)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->setinfo)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->unsetinfo)) {
		return (FALSE);
	}
	if (!xdr_rpcbs_addrlist_ptr(xdrs, &objp->addrinfo)) {
		return (FALSE);
	}
	if (!xdr_rpcbs_rmtcalllist_ptr(xdrs, &objp->rmtinfo)) {
		return (FALSE);
	}
	return (TRUE);
}

/*
 * One rpcb_stat structure is returned for each version of rpcbind
 * being monitored.
 */
bool_t
xdr_rpcb_stat_byvers(XDR *xdrs, rpcb_stat_byvers objp)
{
	if (!xdr_vector(xdrs, (char *)(void *)objp, RPCBVERS_STAT,
	    sizeof (rpcb_stat), (xdrproc_t)xdr_rpcb_stat)) {
		return (FALSE);
	}
	return (TRUE);
}
