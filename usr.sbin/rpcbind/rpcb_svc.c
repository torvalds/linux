/*	$NetBSD: rpcb_svc.c,v 1.1 2000/06/02 23:15:41 fvdl Exp $	*/
/*	$FreeBSD$ */

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
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/* #ident	"@(#)rpcb_svc.c	1.16	93/07/05 SMI" */

/*
 * rpcb_svc.c
 * The server procedure for the version 3 rpcbind (TLI).
 *
 * It maintains a separate list of all the registered services with the
 * version 3 of rpcbind.
 */
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpc/rpcb_prot.h>
#include <netconfig.h>
#include <stdio.h>
#ifdef RPCBIND_DEBUG
#include <stdlib.h>
#endif
#include <string.h>

#include "rpcbind.h"

static void *rpcbproc_getaddr_3_local(void *, struct svc_req *, SVCXPRT *,
					   rpcvers_t);
static void *rpcbproc_dump_3_local(void *, struct svc_req *, SVCXPRT *,
					rpcvers_t);

/*
 * Called by svc_getreqset. There is a separate server handle for
 * every transport that it waits on.
 */
void
rpcb_service_3(struct svc_req *rqstp, SVCXPRT *transp)
{
	union {
		RPCB rpcbproc_set_3_arg;
		RPCB rpcbproc_unset_3_arg;
		RPCB rpcbproc_getaddr_3_local_arg;
		struct rpcb_rmtcallargs rpcbproc_callit_3_arg;
		char *rpcbproc_uaddr2taddr_3_arg;
		struct netbuf rpcbproc_taddr2uaddr_3_arg;
	} argument;
	char *result;
	xdrproc_t xdr_argument, xdr_result;
	void *(*local)(void *, struct svc_req *, SVCXPRT *, rpcvers_t);

	rpcbs_procinfo(RPCBVERS_3_STAT, rqstp->rq_proc);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		/*
		 * Null proc call
		 */
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "RPCBPROC_NULL\n");
#endif
		/* This call just logs, no actual checks */
		check_access(transp, rqstp->rq_proc, NULL, RPCBVERS);
		(void) svc_sendreply(transp, (xdrproc_t)xdr_void, (char *)NULL);
		return;

	case RPCBPROC_SET:
		xdr_argument = (xdrproc_t )xdr_rpcb;
		xdr_result = (xdrproc_t )xdr_bool;
		local = rpcbproc_set_com;
		break;

	case RPCBPROC_UNSET:
		xdr_argument = (xdrproc_t)xdr_rpcb;
		xdr_result = (xdrproc_t)xdr_bool;
		local = rpcbproc_unset_com;
		break;

	case RPCBPROC_GETADDR:
		xdr_argument = (xdrproc_t)xdr_rpcb;
		xdr_result = (xdrproc_t)xdr_wrapstring;
		local = rpcbproc_getaddr_3_local;
		break;

	case RPCBPROC_DUMP:
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "RPCBPROC_DUMP\n");
#endif
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_rpcblist_ptr;
		local = rpcbproc_dump_3_local;
		break;

	case RPCBPROC_CALLIT:
		rpcbproc_callit_com(rqstp, transp, rqstp->rq_proc, RPCBVERS);
		return;

	case RPCBPROC_GETTIME:
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "RPCBPROC_GETTIME\n");
#endif
		xdr_argument = (xdrproc_t)xdr_void;
		xdr_result = (xdrproc_t)xdr_u_long;
		local = rpcbproc_gettime_com;
		break;

	case RPCBPROC_UADDR2TADDR:
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "RPCBPROC_UADDR2TADDR\n");
#endif
		xdr_argument = (xdrproc_t)xdr_wrapstring;
		xdr_result = (xdrproc_t)xdr_netbuf;
		local = rpcbproc_uaddr2taddr_com;
		break;

	case RPCBPROC_TADDR2UADDR:
#ifdef RPCBIND_DEBUG
		if (debugging)
			fprintf(stderr, "RPCBPROC_TADDR2UADDR\n");
#endif
		xdr_argument = (xdrproc_t)xdr_netbuf;
		xdr_result = (xdrproc_t)xdr_wrapstring;
		local = rpcbproc_taddr2uaddr_com;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	(void) memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(transp, (xdrproc_t) xdr_argument,
				(char *) &argument)) {
		svcerr_decode(transp);
		if (debugging)
			(void) fprintf(stderr, "rpcbind: could not decode\n");
		return;
	}
	if (!check_access(transp, rqstp->rq_proc, &argument, RPCBVERS)) {
		svcerr_weakauth(transp);
		goto done;
	}
	result = (*local)(&argument, rqstp, transp, RPCBVERS);
	if (result != NULL && !svc_sendreply(transp, (xdrproc_t)xdr_result,
						result)) {
		svcerr_systemerr(transp);
		if (debugging) {
			(void) fprintf(stderr, "rpcbind: svc_sendreply\n");
			if (doabort) {
				rpcbind_abort();
			}
		}
	}
done:
	if (!svc_freeargs(transp, (xdrproc_t)xdr_argument, (char *)
				&argument)) {
		if (debugging) {
			(void) fprintf(stderr, "unable to free arguments\n");
			if (doabort) {
				rpcbind_abort();
			}
		}
	}
}

/*
 * Lookup the mapping for a program, version and return its
 * address. Assuming that the caller wants the address of the
 * server running on the transport on which the request came.
 *
 * We also try to resolve the universal address in terms of
 * address of the caller.
 */
/* ARGSUSED */
static void *
rpcbproc_getaddr_3_local(void *arg, struct svc_req *rqstp __unused,
			 SVCXPRT *transp __unused, rpcvers_t versnum __unused)
{
	RPCB *regp = (RPCB *)arg;
#ifdef RPCBIND_DEBUG
	if (debugging) {
		char *uaddr;

		uaddr = taddr2uaddr(rpcbind_get_conf(transp->xp_netid),
			    svc_getrpccaller(transp));
		fprintf(stderr, "RPCB_GETADDR req for (%lu, %lu, %s) from %s: ",
		    (unsigned long)regp->r_prog, (unsigned long)regp->r_vers,
		    regp->r_netid, uaddr);
		free(uaddr);
	}
#endif
	return (rpcbproc_getaddr_com(regp, rqstp, transp, RPCBVERS,
	    RPCB_ALLVERS));
}

/* ARGSUSED */
static void *
rpcbproc_dump_3_local(void *arg __unused, struct svc_req *rqstp __unused,
		      SVCXPRT *transp __unused, rpcvers_t versnum __unused)
{
	return ((void *)&list_rbl);
}
