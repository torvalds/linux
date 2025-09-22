/*	$OpenBSD: pmap_rmt.h,v 1.5 2010/09/01 14:43:34 millert Exp $	*/
/*	$NetBSD: pmap_rmt.h,v 1.4 1994/10/26 00:57:01 cgd Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: @(#)pmap_rmt.h 1.2 88/02/08 SMI 
 *	@(#)pmap_rmt.h	2.1 88/07/29 4.0 RPCSRC
 */

/*
 * Structures and XDR routines for parameters to and replies from
 * the portmapper remote-call-service.
 */

#ifndef _RPC_PMAPRMT_H
#define _RPC_PMAPRMT_H
#include <sys/cdefs.h>

struct rmtcallargs {
	unsigned long prog, vers, proc, arglen;
	caddr_t args_ptr;
	xdrproc_t xdr_args;
};

struct rmtcallres {
	unsigned long *port_ptr;
	unsigned long resultslen;
	caddr_t results_ptr;
	xdrproc_t xdr_results;
};

__BEGIN_DECLS
extern bool_t xdr_rmtcall_args(XDR *, struct rmtcallargs *);
extern bool_t xdr_rmtcallres(XDR *, struct rmtcallres *);
__END_DECLS

#endif /* !_RPC_PMAPRMT_H */
