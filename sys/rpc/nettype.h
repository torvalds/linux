/*	$NetBSD: nettype.h,v 1.2 2000/07/06 03:17:19 christos Exp $	*/
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
 * nettype.h, Nettype definitions.
 * All for the topmost layer of rpc
 *
 */

#ifndef	_RPC_NETTYPE_H
#define	_RPC_NETTYPE_H

#ifdef _KERNEL
#include <rpc/netconfig.h>
#else
#include <netconfig.h>
#endif

#define	_RPC_NONE	0
#define	_RPC_NETPATH	1
#define	_RPC_VISIBLE	2
#define	_RPC_CIRCUIT_V	3
#define	_RPC_DATAGRAM_V	4
#define	_RPC_CIRCUIT_N	5
#define	_RPC_DATAGRAM_N	6
#define	_RPC_TCP	7
#define	_RPC_UDP	8

__BEGIN_DECLS
extern void *__rpc_setconf(const char *);
extern void __rpc_endconf(void *);
extern struct netconfig *__rpc_getconf(void *);
extern struct netconfig *__rpc_getconfip(const char *);
__END_DECLS

#endif	/* !_RPC_NETTYPE_H */
