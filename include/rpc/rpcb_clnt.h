/*	$NetBSD: rpcb_clnt.h,v 1.1 2000/06/02 22:57:56 fvdl Exp $	*/
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

/*
 * rpcb_clnt.h
 * Supplies C routines to get to rpcbid services.
 *
 */

/*
 * Usage:
 *	success = rpcb_set(program, version, nconf, address);
 *	success = rpcb_unset(program, version, nconf);
 *	success = rpcb_getaddr(program, version, nconf, host);
 *	head = rpcb_getmaps(nconf, host);
 *	clnt_stat = rpcb_rmtcall(nconf, host, program, version, procedure,
 *		xdrargs, argsp, xdrres, resp, tout, addr_ptr)
 *	success = rpcb_gettime(host, timep)
 *	uaddr = rpcb_taddr2uaddr(nconf, taddr);
 *	taddr = rpcb_uaddr2uaddr(nconf, uaddr);
 */

#ifndef _RPC_RPCB_CLNT_H
#define	_RPC_RPCB_CLNT_H

/* #pragma ident	"@(#)rpcb_clnt.h	1.13	94/04/25 SMI" */
/* rpcb_clnt.h 1.3 88/12/05 SMI */

#include <rpc/types.h>
#include <rpc/rpcb_prot.h>

__BEGIN_DECLS
extern bool_t rpcb_set(const rpcprog_t, const rpcvers_t,
		       const struct netconfig  *, const struct netbuf *);
extern bool_t rpcb_unset(const rpcprog_t, const rpcvers_t,
			 const struct netconfig *);
extern rpcblist	*rpcb_getmaps(const struct netconfig *, const char *);
extern enum clnt_stat rpcb_rmtcall(const struct netconfig *,
				   const char *, const rpcprog_t,
				   const rpcvers_t, const rpcproc_t,
				   const xdrproc_t, const caddr_t,
				   const xdrproc_t, const caddr_t,
				   const struct timeval,
				   const struct netbuf *);
extern bool_t rpcb_getaddr(const rpcprog_t, const rpcvers_t,
			   const struct netconfig *, struct netbuf *,
			   const  char *);
extern bool_t rpcb_gettime(const char *, time_t *);
extern char *rpcb_taddr2uaddr(struct netconfig *, struct netbuf *);
extern struct netbuf *rpcb_uaddr2taddr(struct netconfig *, char *);
__END_DECLS

#endif	/* !_RPC_RPCB_CLNT_H */
