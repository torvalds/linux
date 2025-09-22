/*	$OpenBSD: pmap_prot.h,v 1.6 2010/09/01 14:43:34 millert Exp $	*/
/*	$NetBSD: pmap_prot.h,v 1.4 1994/10/26 00:57:00 cgd Exp $	*/

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
 *	from: @(#)pmap_prot.h 1.14 88/02/08 SMI 
 *	@(#)pmap_prot.h	2.1 88/07/29 4.0 RPCSRC
 */

/*
 * pmap_prot.h
 * Protocol for the local binder service, or pmap.
 *
 * The following procedures are supported by the protocol:
 *
 * PMAPPROC_NULL() returns ()
 * 	takes nothing, returns nothing
 *
 * PMAPPROC_SET(struct pmap) returns (bool_t)
 * 	TRUE is success, FALSE is failure.  Registers the tuple
 *	[prog, vers, prot, port].
 *
 * PMAPPROC_UNSET(struct pmap) returns (bool_t)
 *	TRUE is success, FALSE is failure.  Un-registers pair
 *	[prog, vers].  prot and port are ignored.
 *
 * PMAPPROC_GETPORT(struct pmap) returns (unsigned long).
 *	0 is failure.  Otherwise returns the port number where the pair
 *	[prog, vers] is registered.  It may lie!
 *
 * PMAPPROC_DUMP() RETURNS (struct pmaplist *)
 *
 * PMAPPROC_CALLIT(unsigned int, unsigned int, unsigned int, string<>)
 * 	RETURNS (port, string<>);
 * usage: encapsulatedresults = PMAPPROC_CALLIT(prog, vers, proc, encapsulatedargs);
 * 	Calls the procedure on the local machine.  If it is not registered,
 *	this procedure is quite; ie it does not return error information!!!
 *	This procedure only is supported on rpc/udp and calls via
 *	rpc/udp.  This routine only passes null authentication parameters.
 *	This file has no interface to xdr routines for PMAPPROC_CALLIT.
 *
 * The service supports remote procedure calls on udp/ip or tcp/ip socket 111.
 */

#ifndef _RPC_PMAPPROT_H
#define _RPC_PMAPPROT_H
#include <sys/cdefs.h>

#define PMAPPORT		((unsigned short)111)
#define PMAPPROG		((unsigned long)100000)
#define PMAPVERS		((unsigned long)2)
#define PMAPVERS_PROTO		((unsigned long)2)
#define PMAPVERS_ORIG		((unsigned long)1)
#define PMAPPROC_NULL		((unsigned long)0)
#define PMAPPROC_SET		((unsigned long)1)
#define PMAPPROC_UNSET		((unsigned long)2)
#define PMAPPROC_GETPORT	((unsigned long)3)
#define PMAPPROC_DUMP		((unsigned long)4)
#define PMAPPROC_CALLIT		((unsigned long)5)

struct pmap {
	unsigned long pm_prog;
	unsigned long pm_vers;
	unsigned long pm_prot;
	unsigned long pm_port;
};

struct pmaplist {
	struct pmap	pml_map;
	struct pmaplist *pml_next;
};

__BEGIN_DECLS
extern bool_t xdr_pmap(XDR *, struct pmap *);
extern bool_t xdr_pmaplist(XDR *, struct pmaplist **);
__END_DECLS

#endif /* !_RPC_PMAPPROT_H */
