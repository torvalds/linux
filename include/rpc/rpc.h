/*	$OpenBSD: rpc.h,v 1.11 2010/09/01 14:43:34 millert Exp $	*/
/*	$NetBSD: rpc.h,v 1.5 1994/12/04 01:15:30 cgd Exp $	*/

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
 *	from: @(#)rpc.h 1.9 88/02/08 SMI 
 *	@(#)rpc.h	2.4 89/07/11 4.0 RPCSRC
 */

/*
 * rpc.h, Just includes the billions of rpc header files necessary to
 * do remote procedure calling.
 */
#ifndef _RPC_RPC_H
#define _RPC_RPC_H

#include <rpc/types.h>		/* some typedefs */
#include <netinet/in.h>

/* external data representation interfaces */
#include <rpc/xdr.h>		/* generic (de)serializer */

/* Client side only authentication */
#include <rpc/auth.h>		/* generic authenticator (client side) */

/* Client side (mostly) remote procedure call */
#include <rpc/clnt.h>		/* generic rpc stuff */

/* Client side (mostly) pmap functions */
#include <rpc/pmap_clnt.h>	/* generic pmap stuff */

/* semi-private protocol headers */
#include <rpc/rpc_msg.h>	/* protocol for rpc messages */
#include <rpc/auth_unix.h>	/* protocol for unix style cred */
/*
 *  Uncomment-out the next line if you are building the rpc library with    
 *  DES Authentication (see the README file in the secure_rpc/ directory).
 */
#ifdef notdef
#include <rpc/auth_des.h>	/* protocol for des style cred */
#endif

/* Server side only remote procedure callee */
#include <rpc/svc.h>		/* service manager and multiplexer */
#include <rpc/svc_auth.h>	/* service side authenticator */

/*
 * COMMENT OUT THE NEXT INCLUDE (or add to the #ifndef) IF RUNNING ON
 * A VERSION OF UNIX THAT USES SUN'S NFS SOURCE.  These systems will
 * already have the structures defined by <rpc/netdb.h> included in <netdb.h>.
 */
/* routines for parsing /etc/rpc */

struct rpcent {
      char    *r_name;        /* name of server for this rpc program */
      char    **r_aliases;    /* alias list */
      int     r_number;       /* rpc program number */
};

__BEGIN_DECLS
extern struct rpcent *getrpcbyname(char *);
extern struct rpcent *getrpcbynumber(int);
extern struct rpcent *getrpcent(void);
extern void setrpcent(int);
extern void endrpcent(void);

extern int get_myaddress(struct sockaddr_in *);
extern int registerrpc(int, int, int, char *(*)(char [UDPMSGSIZE]),
	xdrproc_t, xdrproc_t);
extern int callrpc(char *, int, int, int, xdrproc_t, char *,
	xdrproc_t , char *);
extern int getrpcport(char *, int, int, int);

extern bool_t xdr_opaque_auth(XDR *, struct opaque_auth *);

extern int _rpc_dtablesize(void);

__END_DECLS

#endif /* !_RPC_RPC_H */
