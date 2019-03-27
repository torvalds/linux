%/*
% * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
% * unrestricted use provided that this legend is included on all tape
% * media and as a part of the software program in whole or part.  Users
% * may copy or modify Sun RPC without charge, but are not authorized
% * to license or distribute it to anyone else except as part of a product or
% * program developed by the user.
% *
% * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
% * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
% * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
% *
% * Sun RPC is provided with no support and without any obligation on the
% * part of Sun Microsystems, Inc. to assist in its use, correction,
% * modification or enhancement.
% *
% * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
% * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
% * OR ANY PART THEREOF.
% *
% * In no event will Sun Microsystems, Inc. be liable for any lost revenue
% * or profits or other special, indirect and consequential damages, even if
% * Sun has been advised of the possibility of such damages.
% *
% * Sun Microsystems, Inc.
% * 2550 Garcia Avenue
% * Mountain View, California  94043
% */
%/*
% * Copyright (c) 1984,1989 by Sun Microsystems, Inc.
% */

%/* from pmap_prot.x */

#ifdef RPC_HDR
%
%#pragma ident	"@(#)pmap_prot.x	1.6	94/04/29 SMI"
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
%
%#ifndef _KERNEL
%
#endif

/*
 * Port Mapper Protocol Specification (in RPC Language)
 * derived from RFC 1057
 */

%/*
% * Protocol for the local binder service, or pmap.
% *
% * Copyright (C) 1984, Sun Microsystems, Inc.
% *
% * The following procedures are supported by the protocol:
% *
% * PMAPPROC_NULL() returns ()
% * 	takes nothing, returns nothing
% *
% * PMAPPROC_SET(struct pmap) returns (bool_t)
% * 	TRUE is success, FALSE is failure.  Registers the tuple
% *	[prog, vers, prot, port].
% *
% * PMAPPROC_UNSET(struct pmap) returns (bool_t)
% *	TRUE is success, FALSE is failure.  Un-registers pair
% *	[prog, vers].  prot and port are ignored.
% *
% * PMAPPROC_GETPORT(struct pmap) returns (long unsigned).
% *	0 is failure.  Otherwise returns the port number where the pair
% *	[prog, vers] is registered.  It may lie!
% *
% * PMAPPROC_DUMP() RETURNS (struct pmaplist_ptr)
% *
% * PMAPPROC_CALLIT(unsigned, unsigned, unsigned, string<>)
% * 	RETURNS (port, string<>);
% * usage: encapsulatedresults = PMAPPROC_CALLIT(prog, vers, proc,
% *						encapsulatedargs);
% * 	Calls the procedure on the local machine.  If it is not registered,
% *	this procedure is quite; ie it does not return error information!!!
% *	This procedure only is supported on rpc/udp and calls via
% *	rpc/udp.  This routine only passes null authentication parameters.
% *	This file has no interface to xdr routines for PMAPPROC_CALLIT.
% *
% * The service supports remote procedure calls on udp/ip or tcp/ip socket 111.
% */
%
const PMAPPORT = 111;	/* portmapper port number */
%
%
%/*
% * A mapping of (program, version, protocol) to port number
% */

struct pmap {
	unsigned long pm_prog;
	unsigned long pm_vers;
	unsigned long pm_prot;
	unsigned long pm_port;
};
#ifdef RPC_HDR
%
%typedef pmap PMAP;
%
#endif
%
%/*
% * Supported values for the "prot" field
% */
%
const PMAP_IPPROTO_TCP = 6;	/* protocol number for TCP/IP */
const PMAP_IPPROTO_UDP = 17;	/* protocol number for UDP/IP */
%
%
%/*
% * A list of mappings
% *
% * Below are two definitions for the pmaplist structure.  This is done because
% * xdr_pmaplist() is specified to take a struct pmaplist **, rather than a
% * struct pmaplist * that rpcgen would produce.  One version of the pmaplist
% * structure (actually called pm__list) is used with rpcgen, and the other is
% * defined only in the header file for compatibility with the specified
% * interface.
% */

struct pm__list {
	pmap pml_map;
	struct pm__list *pml_next;
};

typedef pm__list *pmaplist_ptr;		/* results of PMAPPROC_DUMP */

#ifdef RPC_HDR
%
%typedef struct pm__list pmaplist;
%typedef struct pm__list PMAPLIST;
%
%#ifndef __cplusplus
%struct pmaplist {
%	PMAP pml_map;
%	struct pmaplist *pml_next;
%};
%#endif
%
%#ifdef __cplusplus
%extern "C" {
%#endif
%extern  bool_t xdr_pmaplist(XDR *, pmaplist**);
%#ifdef	__cplusplus
%}
%#endif
%
#endif

%
%/*
% * Arguments to callit
% */

struct rmtcallargs {
	unsigned long prog;
	unsigned long vers;
	unsigned long proc;
	opaque args<>;
};
#ifdef RPC_HDR
%
%/*
% * Client-side only representation of rmtcallargs structure.
% *
% * The routine that XDRs the rmtcallargs structure must deal with the
% * opaque arguments in the "args" structure.  xdr_rmtcall_args() needs to be
% * passed the XDR routine that knows the args' structure.  This routine
% * doesn't need to go over-the-wire (and it wouldn't make sense anyway) since
% * the application being called knows the args structure already.  So we use a
% * different "XDR" structure on the client side, p_rmtcallargs, which includes
% * the args' XDR routine.
% */
%struct p_rmtcallargs {
%	u_long prog;
%	u_long vers;
%	u_long proc;
%	struct {
%		u_int args_len;
%		char *args_val;
%	} args;
%	xdrproc_t	xdr_args;	/* encodes args */
%};
%
#endif	/* def RPC_HDR */
%
%
%/*
% * Results of callit
% */

struct rmtcallres {
	unsigned long port;
	opaque res<>;
};
#ifdef RPC_HDR
%
%/*
% * Client-side only representation of rmtcallres structure.
% */
%struct p_rmtcallres {
%	u_long port;
%	struct {
%		u_int res_len;
%		char *res_val;
%	} res;
%	xdrproc_t	xdr_res;	/* decodes res */
%};
%
#endif	/* def RPC_HDR */

/*
 * Port mapper procedures
 */

program PMAPPROG {
   version PMAPVERS {
	void
	PMAPPROC_NULL(void)	= 0;

	bool
	PMAPPROC_SET(pmap)	= 1;

	bool
	PMAPPROC_UNSET(pmap)	= 2;

	unsigned long
	PMAPPROC_GETPORT(pmap)	= 3;

	pmaplist_ptr
	PMAPPROC_DUMP(void)	= 4;

	rmtcallres
	PMAPPROC_CALLIT(rmtcallargs)  = 5;
   } = 2;
} = 100000;
%
#ifdef RPC_HDR
%#define PMAPVERS_PROTO		((u_long)2)
%#define PMAPVERS_ORIG		((u_long)1)
%
%#else		/* ndef _KERNEL */
%
%#include <rpc/pmap_rmt.h>
%
%#ifdef __cplusplus
%extern "C" {
%#endif
%
%#define	PMAPPORT 111
%
%struct pmap {
%	long unsigned pm_prog;
%	long unsigned pm_vers;
%	long unsigned pm_prot;
%	long unsigned pm_port;
%};
%typedef struct pmap PMAP;
%extern bool_t xdr_pmap (XDR *, struct pmap *);
%
%struct pmaplist {
%	struct pmap pml_map;
%	struct pmaplist *pml_next;
%};
%typedef struct pmaplist PMAPLIST;
%typedef struct pmaplist *pmaplist_ptr;
%
%
%#ifdef __cplusplus
%}
%#endif
%
%#endif		/* ndef _KERNEL */
#endif

