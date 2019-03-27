/*	$NetBSD: rpc_com.h,v 1.3 2000/12/10 04:10:08 christos Exp $	*/

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
 *
 * $FreeBSD$
 */
/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * rpc_com.h, Common definitions for both the server and client side.
 * All for the topmost layer of rpc
 *
 * In Sun's tirpc distribution, this was installed as <rpc/rpc_com.h>,
 * but as it contains only non-exported interfaces, it was moved here.
 */

#ifndef _RPC_RPCCOM_H
#define	_RPC_RPCCOM_H

#include <sys/cdefs.h>

/* #pragma ident	"@(#)rpc_com.h	1.11	93/07/05 SMI" */

/*
 * The max size of the transport, if the size cannot be determined
 * by other means.
 */
#define	RPC_MAXDATASIZE 9000
#define	RPC_MAXADDRSIZE 1024

#ifdef _KERNEL

#define __RPC_GETXID(now) ((uint32_t)curproc->p_pid ^ (uint32_t)(now)->tv_sec ^ \
    (uint32_t)(now)->tv_usec)

#else

#define __RPC_GETXID(now) ((uint32_t)getpid() ^ (uint32_t)(now)->tv_sec ^ \
    (uint32_t)(now)->tv_usec)

#endif

__BEGIN_DECLS
#ifndef _KERNEL
extern u_int __rpc_get_a_size(int);
extern int __rpc_dtbsize(void);
extern struct netconfig * __rpcgettp(int);
extern  int  __rpc_get_default_domain(char **);

char *__rpc_taddr2uaddr_af(int, const struct netbuf *);
struct netbuf *__rpc_uaddr2taddr_af(int, const char *);
int __rpc_fixup_addr(struct netbuf *, const struct netbuf *);
int __rpc_sockinfo2netid(struct __rpc_sockinfo *, const char **);
int __rpc_seman2socktype(int);
int __rpc_socktype2seman(int);
void *rpc_nullproc(CLIENT *);
int __rpc_sockisbound(int);

struct netbuf *__rpcb_findaddr_timed(rpcprog_t, rpcvers_t,
    const struct netconfig *, const char *host, CLIENT **clpp,
    struct timeval *tp);

bool_t __rpc_control(int,void *);

char *_get_next_token(char *, int);

bool_t __svc_clean_idle(fd_set *, int, bool_t);
bool_t __xdrrec_setnonblock(XDR *, int);
bool_t __xdrrec_getrec(XDR *, enum xprt_stat *, bool_t);
void __xprt_unregister_unlocked(SVCXPRT *);

SVCXPRT **__svc_xports;
int __svc_maxrec;

#else

#define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))

extern u_int __rpc_get_a_size(int);
extern char *__rpc_taddr2uaddr_af(int, const struct netbuf *);
extern struct netbuf *__rpc_uaddr2taddr_af(int, const char *);
extern int __rpc_seman2socktype(int);
extern int __rpc_socktype2seman(int);
extern int __rpc_sockisbound(struct socket*);
extern int bindresvport(struct socket *so, struct sockaddr *sa);

struct xucred;
struct XDR;
bool_t xdr_authunix_parms(struct XDR *xdrs, uint32_t *time, struct xucred *cred);
#endif

__END_DECLS

#endif /* _RPC_RPCCOM_H */
