/*	$NetBSD: clnt_soc.h,v 1.1 2000/06/02 22:57:55 fvdl Exp $	*/
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
 * Copyright (c) 1984 - 1991 by Sun Microsystems, Inc.
 */

/*
 * clnt.h - Client side remote procedure call interface.
 */

#ifndef _RPC_CLNT_SOC_H
#define _RPC_CLNT_SOC_H

/* derived from clnt_soc.h 1.3 88/12/17 SMI     */

/*
 * All the following declarations are only for backward compatibility
 * with TS-RPC.
 */

#include <sys/cdefs.h>

#define UDPMSGSIZE      8800    /* rpc imposed limit on udp msg size */  

/*
 * TCP based rpc
 * CLIENT *
 * clnttcp_create(raddr, prog, vers, sockp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	u_long prog;
 *	u_long version;
 *	register int *sockp;
 *	u_int sendsz;
 *	u_int recvsz;
 */
__BEGIN_DECLS
extern CLIENT *clnttcp_create(struct sockaddr_in *, u_long, u_long, int *,
			      u_int, u_int);
__END_DECLS

/*
 * Raw (memory) rpc.
 */
__BEGIN_DECLS
extern CLIENT *clntraw_create(u_long, u_long);
__END_DECLS


/*
 * UDP based rpc.
 * CLIENT *
 * clntudp_create(raddr, program, version, wait, sockp)
 *	struct sockaddr_in *raddr;
 *	u_long program;
 *	u_long version;
 *	struct timeval wait;
 *	int *sockp;
 *
 * Same as above, but you specify max packet sizes.
 * CLIENT *
 * clntudp_bufcreate(raddr, program, version, wait, sockp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	u_long program;
 *	u_long version;
 *	struct timeval wait;
 *	int *sockp;
 *	u_int sendsz;
 *	u_int recvsz;
 */
__BEGIN_DECLS
extern CLIENT *clntudp_create(struct sockaddr_in *, u_long, u_long, 
			      struct timeval, int *);
extern CLIENT *clntudp_bufcreate(struct sockaddr_in *, u_long, u_long,
				 struct timeval, int *, u_int, u_int);
__END_DECLS

#endif /* _RPC_CLNT_SOC_H */
