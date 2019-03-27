/*	$NetBSD: svc_auth.h,v 1.8 2000/06/02 22:57:57 fvdl Exp $	*/

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
 *	from: @(#)svc_auth.h 1.6 86/07/16 SMI
 *	@(#)svc_auth.h	2.1 88/07/29 4.0 RPCSRC
 * $FreeBSD$
 */

/*
 * svc_auth.h, Service side of rpc authentication.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifndef _RPC_SVC_AUTH_H
#define _RPC_SVC_AUTH_H

/*
 * Server side authenticator
 */
__BEGIN_DECLS
extern enum auth_stat _authenticate(struct svc_req *, struct rpc_msg *);
#ifdef _KERNEL
extern int svc_auth_reg(int,
    enum auth_stat (*)(struct svc_req *, struct rpc_msg *),
    int (*)(struct svc_req *, struct ucred **, int *));
#else
extern int svc_auth_reg(int, enum auth_stat (*)(struct svc_req *,
                          struct rpc_msg *));
#endif


extern int svc_getcred(struct svc_req *, struct ucred **, int *);
/*
 * struct svc_req *req;                 -- RPC request
 * struct ucred **crp			-- Kernel cred to modify
 * int *flavorp				-- Return RPC auth flavor
 *
 * Retrieve unix creds corresponding to an RPC request, if
 * possible. The auth flavor (AUTH_NONE or AUTH_UNIX) is returned in
 * *flavorp. If the flavor is AUTH_UNIX the caller's ucred pointer
 * will be modified to point at a ucred structure which reflects the
 * values from the request. The caller should call crfree on this
 * pointer.
 *
 * Return's non-zero if credentials were retrieved from the request,
 * otherwise zero.
 */

__END_DECLS

#endif /* !_RPC_SVC_AUTH_H */
