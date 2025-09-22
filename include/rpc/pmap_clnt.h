/*	$OpenBSD: pmap_clnt.h,v 1.7 2010/09/01 14:43:34 millert Exp $	*/
/*	$NetBSD: pmap_clnt.h,v 1.5 1994/12/04 01:12:42 cgd Exp $	*/

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
 *	from: @(#)pmap_clnt.h 1.11 88/02/08 SMI 
 *	@(#)pmap_clnt.h	2.1 88/07/29 4.0 RPCSRC
 */

/*
 * pmap_clnt.h
 * Supplies C routines to get to portmap services.
 */

/*
 * Usage:
 *	success = pmap_set(program, version, protocol, port);
 *	success = pmap_unset(program, version);
 *	port = pmap_getport(address, program, version, protocol);
 *	head = pmap_getmaps(address);
 *	clnt_stat = pmap_rmtcall(address, program, version, procedure,
 *		xdrargs, argsp, xdrres, resp, tout, port_ptr)
 *		(works for udp only.) 
 * 	clnt_stat = clnt_broadcast(program, version, procedure,
 *		xdrargs, argsp,	xdrres, resp, eachresult)
 *		(like pmap_rmtcall, except the call is broadcasted to all
 *		locally connected nets.  For each valid response received,
 *		the procedure eachresult is called.  Its form is:
 *	done = eachresult(resp, raddr)
 *		bool_t done;
 *		caddr_t resp;
 *		struct sockaddr_in raddr;
 *		where resp points to the results of the call and raddr is the
 *		address if the responder to the broadcast.
 */

#ifndef _RPC_PMAPCLNT_H
#define _RPC_PMAPCLNT_H
#include <sys/cdefs.h>

__BEGIN_DECLS
extern bool_t		pmap_set(unsigned long, unsigned long, unsigned int, 
			    int);
extern bool_t		pmap_unset(unsigned long, unsigned long);
extern struct pmaplist	*pmap_getmaps(struct sockaddr_in *);
extern enum clnt_stat	pmap_rmtcall(struct sockaddr_in *, unsigned long, 
			    unsigned long, unsigned long, xdrproc_t, caddr_t, 
			    xdrproc_t, caddr_t, struct timeval, 
			    unsigned long *);
extern enum clnt_stat	clnt_broadcast(unsigned long, unsigned long, 
			    unsigned long, xdrproc_t, char *, xdrproc_t, char *,
			    bool_t (*)(caddr_t, struct sockaddr_in *));
extern unsigned short	pmap_getport(struct sockaddr_in *, unsigned long, 
			    unsigned long, unsigned int);
__END_DECLS

#endif /* !_RPC_PMAPCLNT_H */
