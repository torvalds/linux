%/*-
% * $FreeBSD$
% *
% * Copyright (c) 2009, Sun Microsystems, Inc.
% * All rights reserved.
% *
% * Redistribution and use in source and binary forms, with or without
% * modification, are permitted provided that the following conditions are met:
% * - Redistributions of source code must retain the above copyright notice,
% *   this list of conditions and the following disclaimer.
% * - Redistributions in binary form must reproduce the above copyright notice,
% *   this list of conditions and the following disclaimer in the documentation
% *   and/or other materials provided with the distribution.
% * - Neither the name of Sun Microsystems, Inc. nor the names of its
% *   contributors may be used to endorse or promote products derived
% *   from this software without specific prior written permission.
% *
% * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
% * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
% * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
% * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
% * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
% * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
% * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
% * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
% * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
% * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
% * POSSIBILITY OF SUCH DAMAGE.
% */
%/*
% * Copyright (c) 1988 by Sun Microsystems, Inc.
% */

%/* from rpcb_prot.x */

#ifdef RPC_HDR
%
%/* #pragma ident	"@(#)rpcb_prot.x	1.5	94/04/29 SMI" */
%
%#ifndef _KERNEL
%
#endif

/*
 * rpcb_prot.x
 * rpcbind protocol, versions 3 and 4, in RPC Language
 */
%
%/*
% * The following procedures are supported by the protocol in version 3:
% *
% * RPCBPROC_NULL() returns ()
% * 	takes nothing, returns nothing
% *
% * RPCBPROC_SET(rpcb) returns (bool_t)
% * 	TRUE is success, FALSE is failure.  Registers the tuple
% *	[prog, vers, address, owner, netid].
% *	Finds out owner and netid information on its own.
% *
% * RPCBPROC_UNSET(rpcb) returns (bool_t)
% *	TRUE is success, FALSE is failure.  Un-registers tuple
% *	[prog, vers, netid].  addresses is ignored.
% *	If netid is NULL, unregister all.
% *
% * RPCBPROC_GETADDR(rpcb) returns (string).
% *	0 is failure.  Otherwise returns the universal address where the
% *	triple [prog, vers, netid] is registered.  Ignore address and owner.
% *
% * RPCBPROC_DUMP() RETURNS (rpcblist_ptr)
% *	used to dump the entire rpcbind maps
% *
% * RPCBPROC_CALLIT(rpcb_rmtcallargs)
% * 	RETURNS (rpcb_rmtcallres);
% * 	Calls the procedure on the remote machine.  If it is not registered,
% *	this procedure is quiet; i.e. it does not return error information!!!
% *	This routine only passes null authentication parameters.
% *	It has no interface to xdr routines for RPCBPROC_CALLIT.
% *
% * RPCBPROC_GETTIME() returns (int).
% *	Gets the remote machines time
% *
% * RPCBPROC_UADDR2TADDR(strint) RETURNS (struct netbuf)
% *	Returns the netbuf address from universal address.
% *
% * RPCBPROC_TADDR2UADDR(struct netbuf) RETURNS (string)
% *	Returns the universal address from netbuf address.
% *
% * END OF RPCBIND VERSION 3 PROCEDURES
% */
%/*
% * Except for RPCBPROC_CALLIT, the procedures above are carried over to
% * rpcbind version 4.  Those below are added or modified for version 4.
% * NOTE: RPCBPROC_BCAST HAS THE SAME FUNCTIONALITY AND PROCEDURE NUMBER
% * AS RPCBPROC_CALLIT.
% *
% * RPCBPROC_BCAST(rpcb_rmtcallargs)
% * 	RETURNS (rpcb_rmtcallres);
% * 	Calls the procedure on the remote machine.  If it is not registered,
% *	this procedure IS quiet; i.e. it DOES NOT return error information!!!
% *	This routine should be used for broadcasting and nothing else.
% *
% * RPCBPROC_GETVERSADDR(rpcb) returns (string).
% *	0 is failure.  Otherwise returns the universal address where the
% *	triple [prog, vers, netid] is registered.  Ignore address and owner.
% *	Same as RPCBPROC_GETADDR except that if the given version number
% *	is not available, the address is not returned.
% *
% * RPCBPROC_INDIRECT(rpcb_rmtcallargs)
% * 	RETURNS (rpcb_rmtcallres);
% * 	Calls the procedure on the remote machine.  If it is not registered,
% *	this procedure is NOT quiet; i.e. it DOES return error information!!!
% * 	as any normal application would expect.
% *
% * RPCBPROC_GETADDRLIST(rpcb) returns (rpcb_entry_list_ptr).
% *	Same as RPCBPROC_GETADDR except that it returns a list of all the
% *	addresses registered for the combination (prog, vers) (for all
% *	transports).
% *
% * RPCBPROC_GETSTAT(void) returns (rpcb_stat_byvers)
% *	Returns the statistics about the kind of requests received by rpcbind.
% */
%
%/*
% * A mapping of (program, version, network ID) to address
% */
struct rpcb {
	rpcprog_t r_prog;		/* program number */
	rpcvers_t r_vers;		/* version number */
	string r_netid<>;		/* network id */
	string r_addr<>;		/* universal address */
	string r_owner<>;		/* owner of this service */
};
#ifdef RPC_HDR
%
%typedef rpcb RPCB;
%
#endif
%
%/*
% * A list of mappings
% *
% * Below are two definitions for the rpcblist structure.  This is done because
% * xdr_rpcblist() is specified to take a struct rpcblist **, rather than a
% * struct rpcblist * that rpcgen would produce.  One version of the rpcblist
% * structure (actually called rp__list) is used with rpcgen, and the other is
% * defined only in the header file for compatibility with the specified
% * interface.
% */

struct rp__list {
	rpcb rpcb_map;
	struct rp__list *rpcb_next;
};

typedef rp__list *rpcblist_ptr;		/* results of RPCBPROC_DUMP */

#ifdef RPC_HDR
%
%typedef struct rp__list rpcblist;
%typedef struct rp__list RPCBLIST;
%
%#ifndef __cplusplus
%struct rpcblist {
%	RPCB rpcb_map;
%	struct rpcblist *rpcb_next;
%};
%#endif
%
%#ifdef __cplusplus
%extern "C" {
%#endif
%extern  bool_t xdr_rpcblist(XDR *, rpcblist**);
%#ifdef	__cplusplus
%}
%#endif
%
#endif

%
%/*
% * Arguments of remote calls
% */
struct rpcb_rmtcallargs {
	rpcprog_t prog;			/* program number */
	rpcvers_t vers;			/* version number */
	rpcproc_t proc;			/* procedure number */
	opaque args<>;			/* argument */
};
#ifdef RPC_HDR
%
%/*
% * Client-side only representation of rpcb_rmtcallargs structure.
% *
% * The routine that XDRs the rpcb_rmtcallargs structure must deal with the
% * opaque arguments in the "args" structure.  xdr_rpcb_rmtcallargs() needs to
% * be passed the XDR routine that knows the args' structure.  This routine
% * doesn't need to go over-the-wire (and it wouldn't make sense anyway) since
% * the application being called already knows the args structure.  So we use a
% * different "XDR" structure on the client side, r_rpcb_rmtcallargs, which
% * includes the args' XDR routine.
% */
%struct r_rpcb_rmtcallargs {
%	rpcprog_t prog;
%	rpcvers_t vers;
%	rpcproc_t proc;
%	struct {
%		u_int args_len;
%		char *args_val;
%	} args;
%	xdrproc_t	xdr_args;	/* encodes args */
%};
%
#endif	/* def RPC_HDR */
%
%/*
% * Results of the remote call
% */
struct rpcb_rmtcallres {
	string addr<>;			/* remote universal address */
	opaque results<>;		/* result */
};
#ifdef RPC_HDR
%
%/*
% * Client-side only representation of rpcb_rmtcallres structure.
% */
%struct r_rpcb_rmtcallres {
%	char *addr;
%	struct {
%		u_int32_t results_len;
%		char *results_val;
%	} results;
%	xdrproc_t	xdr_res;	/* decodes results */
%};
#endif /* RPC_HDR */
%
%/*
% * rpcb_entry contains a merged address of a service on a particular
% * transport, plus associated netconfig information.  A list of rpcb_entrys
% * is returned by RPCBPROC_GETADDRLIST.  See netconfig.h for values used
% * in r_nc_* fields.
% */
struct rpcb_entry {
	string		r_maddr<>;	/* merged address of service */
	string		r_nc_netid<>;	/* netid field */
	unsigned int	r_nc_semantics;	/* semantics of transport */
	string		r_nc_protofmly<>; /* protocol family */
	string		r_nc_proto<>;	/* protocol name */
};
%
%/*
% * A list of addresses supported by a service.
% */
struct rpcb_entry_list {
	rpcb_entry rpcb_entry_map;
	struct rpcb_entry_list *rpcb_entry_next;
};

typedef rpcb_entry_list *rpcb_entry_list_ptr;

%
%/*
% * rpcbind statistics
% */
%
const rpcb_highproc_2 = RPCBPROC_CALLIT;
const rpcb_highproc_3 = RPCBPROC_TADDR2UADDR;
const rpcb_highproc_4 = RPCBPROC_GETSTAT;

const RPCBSTAT_HIGHPROC = 13;	/* # of procs in rpcbind V4 plus one */
const RPCBVERS_STAT = 3;	/* provide only for rpcbind V2, V3 and V4 */
const RPCBVERS_4_STAT = 2;
const RPCBVERS_3_STAT = 1;
const RPCBVERS_2_STAT = 0;
%
%/* Link list of all the stats about getport and getaddr */
struct rpcbs_addrlist {
	rpcprog_t prog;
	rpcvers_t vers;
	int success;
	int failure;
	string netid<>;
	struct rpcbs_addrlist *next;
};
%
%/* Link list of all the stats about rmtcall */
struct rpcbs_rmtcalllist {
	rpcprog_t prog;
	rpcvers_t vers;
	rpcproc_t proc;
	int success;
	int failure;
	int indirect;	/* whether callit or indirect */
	string netid<>;
	struct rpcbs_rmtcalllist *next;
};

typedef int rpcbs_proc[RPCBSTAT_HIGHPROC];
typedef rpcbs_addrlist *rpcbs_addrlist_ptr;
typedef rpcbs_rmtcalllist *rpcbs_rmtcalllist_ptr;

struct rpcb_stat {
	rpcbs_proc		info;
	int			setinfo;
	int			unsetinfo;
	rpcbs_addrlist_ptr	addrinfo;
	rpcbs_rmtcalllist_ptr	rmtinfo;
};
%
%/*
% * One rpcb_stat structure is returned for each version of rpcbind
% * being monitored.
% */

typedef rpcb_stat rpcb_stat_byvers[RPCBVERS_STAT];

#ifdef RPC_HDR
%
%/*
% * We don't define netbuf in RPCL, since it would contain structure member
% * names that would conflict with the definition of struct netbuf in
% * <tiuser.h>.  Instead we merely declare the XDR routine xdr_netbuf() here,
% * and implement it ourselves in rpc/rpcb_prot.c.
% */
%#ifdef __cplusplus
%extern "C" bool_t xdr_netbuf(XDR *, struct netbuf *);
%
%#else /* __STDC__ */
%extern  bool_t xdr_netbuf(XDR *, struct netbuf *);
%
%#endif
#endif /* def RPC_HDR */

/*
 * rpcbind procedures
 */
program RPCBPROG {
	version RPCBVERS {
		bool
		RPCBPROC_SET(rpcb) = 1;

		bool
		RPCBPROC_UNSET(rpcb) = 2;

		string
		RPCBPROC_GETADDR(rpcb) = 3;

		rpcblist_ptr
		RPCBPROC_DUMP(void) = 4;

		rpcb_rmtcallres
		RPCBPROC_CALLIT(rpcb_rmtcallargs) = 5;

		unsigned int
		RPCBPROC_GETTIME(void) = 6;

		struct netbuf
		RPCBPROC_UADDR2TADDR(string) = 7;

		string
		RPCBPROC_TADDR2UADDR(struct netbuf) = 8;
	} = 3;

	version RPCBVERS4 {
		bool
		RPCBPROC_SET(rpcb) = 1;

		bool
		RPCBPROC_UNSET(rpcb) = 2;

		string
		RPCBPROC_GETADDR(rpcb) = 3;

		rpcblist_ptr
		RPCBPROC_DUMP(void) = 4;

		/*
		 * NOTE: RPCBPROC_BCAST has the same functionality as CALLIT;
		 * the new name is intended to indicate that this
		 * procedure should be used for broadcast RPC, and
		 * RPCBPROC_INDIRECT should be used for indirect calls.
		 */
		rpcb_rmtcallres
		RPCBPROC_BCAST(rpcb_rmtcallargs) = RPCBPROC_CALLIT;

		unsigned int
		RPCBPROC_GETTIME(void) = 6;

		struct netbuf
		RPCBPROC_UADDR2TADDR(string) = 7;

		string
		RPCBPROC_TADDR2UADDR(struct netbuf) = 8;

		string
		RPCBPROC_GETVERSADDR(rpcb) = 9;

		rpcb_rmtcallres
		RPCBPROC_INDIRECT(rpcb_rmtcallargs) = 10;

		rpcb_entry_list_ptr
		RPCBPROC_GETADDRLIST(rpcb) = 11;

		rpcb_stat_byvers
		RPCBPROC_GETSTAT(void) = 12;
	} = 4;
} = 100000;
#ifdef RPC_HDR
%
%#define	RPCBVERS_3		RPCBVERS
%#define	RPCBVERS_4		RPCBVERS4
%
%#define	_PATH_RPCBINDSOCK	"/var/run/rpcbind.sock"
%
%#else		/* ndef _KERNEL */
%#ifdef __cplusplus
%extern "C" {
%#endif
%
%/*
% * A mapping of (program, version, network ID) to address
% */
%struct rpcb {
%	rpcprog_t r_prog;		/* program number */
%	rpcvers_t r_vers;		/* version number */
%	char *r_netid;			/* network id */
%	char *r_addr;			/* universal address */
%	char *r_owner;			/* owner of the mapping */
%};
%typedef struct rpcb RPCB;
%
%/*
% * A list of mappings
% */
%struct rpcblist {
%	RPCB rpcb_map;
%	struct rpcblist *rpcb_next;
%};
%typedef struct rpcblist RPCBLIST;
%typedef struct rpcblist *rpcblist_ptr;
%
%/*
% * Remote calls arguments
% */
%struct rpcb_rmtcallargs {
%	rpcprog_t prog;			/* program number */
%	rpcvers_t vers;			/* version number */
%	rpcproc_t proc;			/* procedure number */
%	u_int32_t arglen;			/* arg len */
%	caddr_t args_ptr;		/* argument */
%	xdrproc_t xdr_args;		/* XDR routine for argument */
%};
%typedef struct rpcb_rmtcallargs rpcb_rmtcallargs;
%
%/*
% * Remote calls results
% */
%struct rpcb_rmtcallres {
%	char *addr_ptr;			/* remote universal address */
%	u_int32_t resultslen;		/* results length */
%	caddr_t results_ptr;		/* results */
%	xdrproc_t xdr_results;		/* XDR routine for result */
%};
%typedef struct rpcb_rmtcallres rpcb_rmtcallres;
%
%struct rpcb_entry {
%	char *r_maddr;
%	char *r_nc_netid;
%	unsigned int r_nc_semantics;
%	char *r_nc_protofmly;
%	char *r_nc_proto;
%};
%typedef struct rpcb_entry rpcb_entry;
%
%/*
% * A list of addresses supported by a service.
% */
%
%struct rpcb_entry_list {
%	rpcb_entry rpcb_entry_map;
%	struct rpcb_entry_list *rpcb_entry_next;
%};
%typedef struct rpcb_entry_list rpcb_entry_list;
%
%typedef rpcb_entry_list *rpcb_entry_list_ptr;
%
%/*
% * rpcbind statistics
% */
%
%#define	rpcb_highproc_2 RPCBPROC_CALLIT
%#define	rpcb_highproc_3 RPCBPROC_TADDR2UADDR
%#define	rpcb_highproc_4 RPCBPROC_GETSTAT
%#define	RPCBSTAT_HIGHPROC 13
%#define	RPCBVERS_STAT 3
%#define	RPCBVERS_4_STAT 2
%#define	RPCBVERS_3_STAT 1
%#define	RPCBVERS_2_STAT 0
%
%/* Link list of all the stats about getport and getaddr */
%
%struct rpcbs_addrlist {
%	rpcprog_t prog;
%	rpcvers_t vers;
%	int success;
%	int failure;
%	char *netid;
%	struct rpcbs_addrlist *next;
%};
%typedef struct rpcbs_addrlist rpcbs_addrlist;
%
%/* Link list of all the stats about rmtcall */
%
%struct rpcbs_rmtcalllist {
%	rpcprog_t prog;
%	rpcvers_t vers;
%	rpcproc_t proc;
%	int success;
%	int failure;
%	int indirect;
%	char *netid;
%	struct rpcbs_rmtcalllist *next;
%};
%typedef struct rpcbs_rmtcalllist rpcbs_rmtcalllist;
%
%typedef int rpcbs_proc[RPCBSTAT_HIGHPROC];
%
%typedef rpcbs_addrlist *rpcbs_addrlist_ptr;
%
%typedef rpcbs_rmtcalllist *rpcbs_rmtcalllist_ptr;
%
%struct rpcb_stat {
%	rpcbs_proc info;
%	int setinfo;
%	int unsetinfo;
%	rpcbs_addrlist_ptr addrinfo;
%	rpcbs_rmtcalllist_ptr rmtinfo;
%};
%typedef struct rpcb_stat rpcb_stat;
%
%/*
% * One rpcb_stat structure is returned for each version of rpcbind
% * being monitored.
% */
%
%typedef rpcb_stat rpcb_stat_byvers[RPCBVERS_STAT];
%
%#ifdef __cplusplus
%}
%#endif
%
%#endif		/* ndef _KERNEL */
#endif		/* RPC_HDR */
