/*	$NetBSD: clnt.h,v 1.14 2000/06/02 22:57:55 fvdl Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010, Oracle America, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the "Oracle America, Inc." nor the names of its
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
 *	from: @(#)clnt.h 1.31 94/04/29 SMI
 *	from: @(#)clnt.h	2.1 88/07/29 4.0 RPCSRC
 * $FreeBSD$
 */

/*
 * clnt.h - Client side remote procedure call interface.
 */

#ifndef _RPC_CLNT_H_
#define _RPC_CLNT_H_
#include <rpc/clnt_stat.h>
#include <sys/cdefs.h>
#include <netconfig.h>
#include <sys/un.h>

/*
 * Well-known IPV6 RPC broadcast address.
 */
#define RPCB_MULTICAST_ADDR "ff02::202"

/*
 * the following errors are in general unrecoverable.  The caller
 * should give up rather than retry.
 */
#define IS_UNRECOVERABLE_RPC(s) (((s) == RPC_AUTHERROR) || \
	((s) == RPC_CANTENCODEARGS) || \
	((s) == RPC_CANTDECODERES) || \
	((s) == RPC_VERSMISMATCH) || \
	((s) == RPC_PROCUNAVAIL) || \
	((s) == RPC_PROGUNAVAIL) || \
	((s) == RPC_PROGVERSMISMATCH) || \
	((s) == RPC_CANTDECODEARGS))

/*
 * Error info.
 */
struct rpc_err {
	enum clnt_stat re_status;
	union {
		int RE_errno;		/* related system error */
		enum auth_stat RE_why;	/* why the auth error occurred */
		struct {
			rpcvers_t low;	/* lowest version supported */
			rpcvers_t high;	/* highest version supported */
		} RE_vers;
		struct {		/* maybe meaningful if RPC_FAILED */
			int32_t s1;
			int32_t s2;
		} RE_lb;		/* life boot & debugging only */
	} ru;
#define	re_errno	ru.RE_errno
#define	re_why		ru.RE_why
#define	re_vers		ru.RE_vers
#define	re_lb		ru.RE_lb
};


/*
 * Client rpc handle.
 * Created by individual implementations
 * Client is responsible for initializing auth, see e.g. auth_none.c.
 */
typedef struct __rpc_client {
	AUTH	*cl_auth;			/* authenticator */
	struct clnt_ops {
		/* call remote procedure */
		enum clnt_stat	(*cl_call)(struct __rpc_client *,
				    rpcproc_t, xdrproc_t, void *, xdrproc_t,
				        void *, struct timeval);
		/* abort a call */
		void		(*cl_abort)(struct __rpc_client *);
		/* get specific error code */
		void		(*cl_geterr)(struct __rpc_client *,
					struct rpc_err *);
		/* frees results */
		bool_t		(*cl_freeres)(struct __rpc_client *,
					xdrproc_t, void *);
		/* destroy this structure */
		void		(*cl_destroy)(struct __rpc_client *);
		/* the ioctl() of rpc */
		bool_t          (*cl_control)(struct __rpc_client *, u_int,
				    void *);
	} *cl_ops;
	void 			*cl_private;	/* private stuff */
	char			*cl_netid;	/* network token */
	char			*cl_tp;		/* device name */
} CLIENT;


/*
 * Timers used for the pseudo-transport protocol when using datagrams
 */
struct rpc_timers {
	u_short		rt_srtt;	/* smoothed round-trip time */
	u_short		rt_deviate;	/* estimated deviation */
	u_long		rt_rtxcur;	/* current (backed-off) rto */
};

/*      
 * Feedback values used for possible congestion and rate control
 */
#define FEEDBACK_REXMIT1	1	/* first retransmit */
#define FEEDBACK_OK		2	/* no retransmits */    

/* Used to set version of portmapper used in broadcast */
  
#define CLCR_SET_LOWVERS	3
#define CLCR_GET_LOWVERS	4
 
#define RPCSMALLMSGSIZE 400	/* a more reasonable packet size */

/*
 * client side rpc interface ops
 *
 * Parameter types are:
 *
 */

/*
 * enum clnt_stat
 * CLNT_CALL(rh, proc, xargs, argsp, xres, resp, timeout)
 * 	CLIENT *rh;
 *	rpcproc_t proc;
 *	xdrproc_t xargs;
 *	void *argsp;
 *	xdrproc_t xres;
 *	void *resp;
 *	struct timeval timeout;
 */
#define	CLNT_CALL(rh, proc, xargs, argsp, xres, resp, secs) \
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, \
		argsp, xres, resp, secs))
#define	clnt_call(rh, proc, xargs, argsp, xres, resp, secs) \
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, \
		argsp, xres, resp, secs))

/*
 * void
 * CLNT_ABORT(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_ABORT(rh)	((*(rh)->cl_ops->cl_abort)(rh))
#define	clnt_abort(rh)	((*(rh)->cl_ops->cl_abort)(rh))

/*
 * struct rpc_err
 * CLNT_GETERR(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_GETERR(rh,errp)	((*(rh)->cl_ops->cl_geterr)(rh, errp))
#define	clnt_geterr(rh,errp)	((*(rh)->cl_ops->cl_geterr)(rh, errp))


/*
 * bool_t
 * CLNT_FREERES(rh, xres, resp);
 * 	CLIENT *rh;
 *	xdrproc_t xres;
 *	void *resp;
 */
#define	CLNT_FREERES(rh,xres,resp) ((*(rh)->cl_ops->cl_freeres)(rh,xres,resp))
#define	clnt_freeres(rh,xres,resp) ((*(rh)->cl_ops->cl_freeres)(rh,xres,resp))

/*
 * bool_t
 * CLNT_CONTROL(cl, request, info)
 *      CLIENT *cl;
 *      u_int request;
 *      char *info;
 */
#define	CLNT_CONTROL(cl,rq,in) ((*(cl)->cl_ops->cl_control)(cl,rq,in))
#define	clnt_control(cl,rq,in) ((*(cl)->cl_ops->cl_control)(cl,rq,in))

/*
 * control operations that apply to both udp and tcp transports
 */
#define CLSET_TIMEOUT		1	/* set timeout (timeval) */
#define CLGET_TIMEOUT		2	/* get timeout (timeval) */
#define CLGET_SERVER_ADDR	3	/* get server's address (sockaddr) */
#define CLGET_FD		6	/* get connections file descriptor */
#define CLGET_SVC_ADDR		7	/* get server's address (netbuf) */
#define CLSET_FD_CLOSE		8	/* close fd while clnt_destroy */
#define CLSET_FD_NCLOSE		9	/* Do not close fd while clnt_destroy */
#define CLGET_XID 		10	/* Get xid */
#define CLSET_XID		11	/* Set xid */
#define CLGET_VERS		12	/* Get version number */
#define CLSET_VERS		13	/* Set version number */
#define CLGET_PROG		14	/* Get program number */
#define CLSET_PROG		15	/* Set program number */
#define CLSET_SVC_ADDR		16	/* get server's address (netbuf) */
#define CLSET_PUSH_TIMOD	17	/* push timod if not already present */
#define CLSET_POP_TIMOD		18	/* pop timod */
/*
 * Connectionless only control operations
 */
#define CLSET_RETRY_TIMEOUT 4   /* set retry timeout (timeval) */
#define CLGET_RETRY_TIMEOUT 5   /* get retry timeout (timeval) */
#define CLSET_ASYNC		19
#define CLSET_CONNECT		20	/* Use connect() for UDP. (int) */

/*
 * void
 * CLNT_DESTROY(rh);
 * 	CLIENT *rh;
 */
#define	CLNT_DESTROY(rh)	((*(rh)->cl_ops->cl_destroy)(rh))
#define	clnt_destroy(rh)	((*(rh)->cl_ops->cl_destroy)(rh))


/*
 * RPCTEST is a test program which is accessible on every rpc
 * transport/port.  It is used for testing, performance evaluation,
 * and network administration.
 */

#define RPCTEST_PROGRAM		((rpcprog_t)1)
#define RPCTEST_VERSION		((rpcvers_t)1)
#define RPCTEST_NULL_PROC	((rpcproc_t)2)
#define RPCTEST_NULL_BATCH_PROC	((rpcproc_t)3)

/*
 * By convention, procedure 0 takes null arguments and returns them
 */

#define NULLPROC ((rpcproc_t)0)

/*
 * Below are the client handle creation routines for the various
 * implementations of client side rpc.  They can return NULL if a
 * creation failure occurs.
 */

/*
 * Generic client creation routine. Supported protocols are those that
 * belong to the nettype namespace (/etc/netconfig).
 */
__BEGIN_DECLS
extern CLIENT *clnt_create(const char *, const rpcprog_t, const rpcvers_t,
			   const char *);
/*
 *
 * 	const char *hostname;			-- hostname
 *	const rpcprog_t prog;			-- program number
 *	const rpcvers_t vers;			-- version number
 *	const char *nettype;			-- network type
 */

 /*
 * Generic client creation routine. Just like clnt_create(), except
 * it takes an additional timeout parameter.
 */
extern CLIENT * clnt_create_timed(const char *, const rpcprog_t,
	const rpcvers_t, const char *, const struct timeval *);
/*
 *
 *	const char *hostname;			-- hostname
 *	const rpcprog_t prog;			-- program number
 *	const rpcvers_t vers;			-- version number
 *	const char *nettype;			-- network type
 *	const struct timeval *tp;		-- timeout
 */

/*
 * Generic client creation routine. Supported protocols are which belong
 * to the nettype name space.
 */
extern CLIENT *clnt_create_vers(const char *, const rpcprog_t, rpcvers_t *,
				const rpcvers_t, const rpcvers_t,
				const char *);
/*
 *	const char *host;		-- hostname
 *	const rpcprog_t prog;		-- program number
 *	rpcvers_t *vers_out;		-- servers highest available version
 *	const rpcvers_t vers_low;	-- low version number
 *	const rpcvers_t vers_high;	-- high version number
 *	const char *nettype;		-- network type
 */

/*
 * Generic client creation routine. Supported protocols are which belong
 * to the nettype name space.
 */
extern CLIENT * clnt_create_vers_timed(const char *, const rpcprog_t,
	rpcvers_t *, const rpcvers_t, const rpcvers_t, const char *,
	const struct timeval *);
/*
 *	const char *host;		-- hostname
 *	const rpcprog_t prog;		-- program number
 *	rpcvers_t *vers_out;		-- servers highest available version
 *	const rpcvers_t vers_low;	-- low version number
 *	const rpcvers_t vers_high;	-- high version number
 *	const char *nettype;		-- network type
 *	const struct timeval *tp	-- timeout
 */

/*
 * Generic client creation routine. It takes a netconfig structure
 * instead of nettype
 */
extern CLIENT *clnt_tp_create(const char *, const rpcprog_t,
			      const rpcvers_t, const struct netconfig *);
/*
 *	const char *hostname;			-- hostname
 *	const rpcprog_t prog;			-- program number
 *	const rpcvers_t vers;			-- version number
 *	const struct netconfig *netconf; 	-- network config structure
 */

/*
 * Generic client creation routine. Just like clnt_tp_create(), except
 * it takes an additional timeout parameter.
 */
extern CLIENT * clnt_tp_create_timed(const char *, const rpcprog_t,
	const rpcvers_t, const struct netconfig *, const struct timeval *);
/*
 *	const char *hostname;			-- hostname
 *	const rpcprog_t prog;			-- program number
 *	const rpcvers_t vers;			-- version number
 *	const struct netconfig *netconf; 	-- network config structure
 *	const struct timeval *tp		-- timeout
 */

/*
 * Generic TLI create routine. Only provided for compatibility.
 */

extern CLIENT *clnt_tli_create(const int, const struct netconfig *,
			       struct netbuf *, const rpcprog_t,
			       const rpcvers_t, const u_int, const u_int);
/*
 *	const register int fd;		-- fd
 *	const struct netconfig *nconf;	-- netconfig structure
 *	struct netbuf *svcaddr;		-- servers address
 *	const u_long prog;			-- program number
 *	const u_long vers;			-- version number
 *	const u_int sendsz;			-- send size
 *	const u_int recvsz;			-- recv size
 */

/*
 * Low level clnt create routine for connectionful transports, e.g. tcp.
 */
extern CLIENT *clnt_vc_create(const int, const struct netbuf *,
			      const rpcprog_t, const rpcvers_t,
			      u_int, u_int);
/*
 * Added for compatibility to old rpc 4.0. Obsoleted by clnt_vc_create().
 */
extern CLIENT *clntunix_create(struct sockaddr_un *,
			       u_long, u_long, int *, u_int, u_int);
/*
 *	const int fd;				-- open file descriptor
 *	const struct netbuf *svcaddr;		-- servers address
 *	const rpcprog_t prog;			-- program number
 *	const rpcvers_t vers;			-- version number
 *	const u_int sendsz;			-- buffer recv size
 *	const u_int recvsz;			-- buffer send size
 */

/*
 * Low level clnt create routine for connectionless transports, e.g. udp.
 */
extern CLIENT *clnt_dg_create(const int, const struct netbuf *,
			      const rpcprog_t, const rpcvers_t,
			      const u_int, const u_int);
/*
 *	const int fd;				-- open file descriptor
 *	const struct netbuf *svcaddr;		-- servers address
 *	const rpcprog_t program;		-- program number
 *	const rpcvers_t version;		-- version number
 *	const u_int sendsz;			-- buffer recv size
 *	const u_int recvsz;			-- buffer send size
 */

/*
 * Memory based rpc (for speed check and testing)
 * CLIENT *
 * clnt_raw_create(prog, vers)
 *	u_long prog;
 *	u_long vers;
 */
extern CLIENT *clnt_raw_create(rpcprog_t, rpcvers_t);

__END_DECLS


/*
 * Print why creation failed
 */
__BEGIN_DECLS
extern void clnt_pcreateerror(const char *);			/* stderr */
extern char *clnt_spcreateerror(const char *);			/* string */
__END_DECLS

/*
 * Like clnt_perror(), but is more verbose in its output
 */
__BEGIN_DECLS
extern void clnt_perrno(enum clnt_stat);		/* stderr */
extern char *clnt_sperrno(enum clnt_stat);		/* string */
__END_DECLS

/*
 * Print an English error message, given the client error code
 */
__BEGIN_DECLS
extern void clnt_perror(CLIENT *, const char *);	 	/* stderr */
extern char *clnt_sperror(CLIENT *, const char *);		/* string */
__END_DECLS


/*
 * If a creation fails, the following allows the user to figure out why.
 */
struct rpc_createerr {
	enum clnt_stat cf_stat;
	struct rpc_err cf_error; /* useful when cf_stat == RPC_PMAPFAILURE */
};

__BEGIN_DECLS
extern struct rpc_createerr	*__rpc_createerr(void);
__END_DECLS
#define rpc_createerr		(*(__rpc_createerr()))

/*
 * The simplified interface:
 * enum clnt_stat
 * rpc_call(host, prognum, versnum, procnum, inproc, in, outproc, out, nettype)
 *	const char *host;
 *	const rpcprog_t prognum;
 *	const rpcvers_t versnum;
 *	const rpcproc_t procnum;
 *	const xdrproc_t inproc, outproc;
 *	const char *in;
 *	char *out;
 *	const char *nettype;
 */
__BEGIN_DECLS
extern enum clnt_stat rpc_call(const char *, const rpcprog_t,
			       const rpcvers_t, const rpcproc_t,
			       const xdrproc_t, const char *,
			       const xdrproc_t, char *, const char *);
__END_DECLS

/*
 * RPC broadcast interface
 * The call is broadcasted to all locally connected nets.
 *
 * extern enum clnt_stat
 * rpc_broadcast(prog, vers, proc, xargs, argsp, xresults, resultsp,
 *			eachresult, nettype)
 *	const rpcprog_t		prog;		-- program number
 *	const rpcvers_t		vers;		-- version number
 *	const rpcproc_t		proc;		-- procedure number
 *	const xdrproc_t	xargs;		-- xdr routine for args
 *	caddr_t		argsp;		-- pointer to args
 *	const xdrproc_t	xresults;	-- xdr routine for results
 *	caddr_t		resultsp;	-- pointer to results
 *	const resultproc_t	eachresult;	-- call with each result
 *	const char		*nettype;	-- Transport type
 *
 * For each valid response received, the procedure eachresult is called.
 * Its form is:
 *		done = eachresult(resp, raddr, nconf)
 *			bool_t done;
 *			caddr_t resp;
 *			struct netbuf *raddr;
 *			struct netconfig *nconf;
 * where resp points to the results of the call and raddr is the
 * address if the responder to the broadcast.  nconf is the transport
 * on which the response was received.
 *
 * extern enum clnt_stat
 * rpc_broadcast_exp(prog, vers, proc, xargs, argsp, xresults, resultsp,
 *			eachresult, inittime, waittime, nettype)
 *	const rpcprog_t		prog;		-- program number
 *	const rpcvers_t		vers;		-- version number
 *	const rpcproc_t		proc;		-- procedure number
 *	const xdrproc_t	xargs;		-- xdr routine for args
 *	caddr_t		argsp;		-- pointer to args
 *	const xdrproc_t	xresults;	-- xdr routine for results
 *	caddr_t		resultsp;	-- pointer to results
 *	const resultproc_t	eachresult;	-- call with each result
 *	const int 		inittime;	-- how long to wait initially
 *	const int 		waittime;	-- maximum time to wait
 *	const char		*nettype;	-- Transport type
 */

typedef bool_t (*resultproc_t)(caddr_t, ...);

__BEGIN_DECLS
extern enum clnt_stat rpc_broadcast(const rpcprog_t, const rpcvers_t,
				    const rpcproc_t, const xdrproc_t,
				    caddr_t, const xdrproc_t, caddr_t,
				    const resultproc_t, const char *);
extern enum clnt_stat rpc_broadcast_exp(const rpcprog_t, const rpcvers_t,
					const rpcproc_t, const xdrproc_t,
					caddr_t, const xdrproc_t, caddr_t,
					const resultproc_t, const int,
					const int, const char *);
__END_DECLS

/* For backward compatibility */
#include <rpc/clnt_soc.h>

#endif /* !_RPC_CLNT_H_ */
