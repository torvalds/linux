/*	$OpenBSD: clnt.h,v 1.14 2022/12/27 07:44:56 jmc Exp $	*/
/*	$NetBSD: clnt.h,v 1.6 1995/04/29 05:27:58 cgd Exp $	*/

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
 *	from: @(#)clnt.h 1.31 88/02/08 SMI
 *	@(#)clnt.h	2.1 88/07/29 4.0 RPCSRC
 */

/*
 * clnt.h - Client side remote procedure call interface.
 */

#ifndef _RPC_CLNT_H_
#define _RPC_CLNT_H_
#include <sys/cdefs.h>

/*
 * Rpc calls return an enum clnt_stat.  This should be looked at more,
 * since each implementation is required to live with this (implementation
 * independent) list of errors.
 */
enum clnt_stat {
	RPC_SUCCESS=0,			/* call succeeded */
	/*
	 * local errors
	 */
	RPC_CANTENCODEARGS=1,		/* can't encode arguments */
	RPC_CANTDECODERES=2,		/* can't decode results */
	RPC_CANTSEND=3,			/* failure in sending call */
	RPC_CANTRECV=4,			/* failure in receiving result */
	RPC_TIMEDOUT=5,			/* call timed out */
	/*
	 * remote errors
	 */
	RPC_VERSMISMATCH=6,		/* rpc versions not compatible */
	RPC_AUTHERROR=7,		/* authentication error */
	RPC_PROGUNAVAIL=8,		/* program not available */
	RPC_PROGVERSMISMATCH=9,		/* program version mismatched */
	RPC_PROCUNAVAIL=10,		/* procedure unavailable */
	RPC_CANTDECODEARGS=11,		/* decode arguments error */
	RPC_SYSTEMERROR=12,		/* generic "other problem" */

	/*
	 * callrpc & clnt_create errors
	 */
	RPC_UNKNOWNHOST=13,		/* unknown host name */
	RPC_UNKNOWNPROTO=17,		/* unknown protocol */

	/*
	 * _ create errors
	 */
	RPC_PMAPFAILURE=14,		/* the pmapper failed in its call */
	RPC_PROGNOTREGISTERED=15,	/* remote program is not registered */
	/*
	 * unspecified error
	 */
	RPC_FAILED=16
};


/*
 * Error info.
 */
struct rpc_err {
	enum clnt_stat re_status;
	union {
		int RE_errno;		/* related system error */
		enum auth_stat RE_why;	/* why the auth error occurred */
		struct {
			u_int32_t low;	/* lowest version supported */
			u_int32_t high;	/* highest version supported */
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
 * Created by individual implementations, see e.g. rpc_udp.c.
 * Client is responsible for initializing auth, see e.g. auth_none.c.
 */
typedef struct __rpc_client {
	AUTH	*cl_auth;			/* authenticator */
	const struct clnt_ops {
		/* call remote procedure */
		enum clnt_stat	(*cl_call)(struct __rpc_client *,
				    unsigned long, xdrproc_t, caddr_t, 
				    xdrproc_t, caddr_t, struct timeval);
		/* abort a call */
		void		(*cl_abort)(struct __rpc_client *);
		/* get specific error code */
		void		(*cl_geterr)(struct __rpc_client *,
				    struct rpc_err *);
		/* frees results */
		bool_t		(*cl_freeres)(struct __rpc_client *,
				    xdrproc_t, caddr_t);
		/* destroy this structure */
		void		(*cl_destroy)(struct __rpc_client *);
		/* the ioctl() of rpc */
		bool_t          (*cl_control)(struct __rpc_client *, 
				    unsigned int, void *);
	} *cl_ops;
	caddr_t			cl_private;	/* private stuff */
} CLIENT;


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
 *	unsigned long proc;
 *	xdrproc_t xargs;
 *	caddr_t argsp;
 *	xdrproc_t xres;
 *	caddr_t resp;
 *	struct timeval timeout;
 */
#define	CLNT_CALL(rh, proc, xargs, argsp, xres, resp, secs)		\
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, (caddr_t)argsp,	\
	    xres, (caddr_t)resp, secs))
#define	clnt_call(rh, proc, xargs, argsp, xres, resp, secs)		\
	((*(rh)->cl_ops->cl_call)(rh, proc, xargs, (caddr_t)argsp,	\
	    xres, (caddr_t)resp, secs))

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
 *	caddr_t resp;
 */
#define	CLNT_FREERES(rh,xres,resp) ((*(rh)->cl_ops->cl_freeres)(rh,xres,resp))
#define	clnt_freeres(rh,xres,resp) ((*(rh)->cl_ops->cl_freeres)(rh,xres,resp))

/*
 * bool_t
 * CLNT_CONTROL(cl, request, info)
 *      CLIENT *cl;
 *      unsigned int request;
 *      char *info;
 */
#define	CLNT_CONTROL(cl,rq,in) ((*(cl)->cl_ops->cl_control)(cl,rq,in))
#define	clnt_control(cl,rq,in) ((*(cl)->cl_ops->cl_control)(cl,rq,in))

/*
 * control operations that apply to both udp and tcp transports
 */
#define CLSET_TIMEOUT       1   /* set timeout (timeval) */
#define CLGET_TIMEOUT       2   /* get timeout (timeval) */
#define CLGET_SERVER_ADDR   3   /* get server's address (sockaddr) */
/*
 * udp only control operations
 */
#define CLSET_RETRY_TIMEOUT 4   /* set retry timeout (timeval) */
#define CLGET_RETRY_TIMEOUT 5   /* get retry timeout (timeval) */
#define CLSET_CONNECTED	    6	/* socket is connected, so use send() */

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

#define RPCTEST_PROGRAM		((unsigned long)1)
#define RPCTEST_VERSION		((unsigned long)1)
#define RPCTEST_NULL_PROC	((unsigned long)2)
#define RPCTEST_NULL_BATCH_PROC	((unsigned long)3)

/*
 * By convention, procedure 0 takes null arguments and returns them
 */

#define NULLPROC ((unsigned int)0)

/*
 * Below are the client handle creation routines for the various
 * implementations of client side rpc.  They can return NULL if a 
 * creation failure occurs.
 */

/*
 * Memory based rpc (for speed check and testing)
 * CLIENT *
 * clntraw_create(prog, vers)
 *	unsigned long prog;
 *	unsigned long vers;
 */
__BEGIN_DECLS
extern CLIENT *clntraw_create(unsigned long, unsigned long);
__END_DECLS


/*
 * Generic client creation routine. Supported protocols are "udp" and "tcp"
 * CLIENT *
 * clnt_create(host, prog, vers, prot);
 *	char *host; 		-- hostname
 *	unsigned long prog;	-- program number
 *	unsigned long vers;	-- version number
 *	char *prot;		-- protocol
 */
__BEGIN_DECLS
extern CLIENT *clnt_create(char *, unsigned long, unsigned long, char *);
__END_DECLS


/*
 * TCP based rpc
 * CLIENT *
 * clnttcp_create(raddr, prog, vers, sockp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	unsigned long prog;
 *	unsigned long version;
 *	int *sockp;
 *	unsigned int sendsz;
 *	unsigned int recvsz;
 */
__BEGIN_DECLS
extern CLIENT *clnttcp_create(struct sockaddr_in *, unsigned long, 
    unsigned long, int *, unsigned int, unsigned int);
__END_DECLS


/*
 * UDP based rpc.
 * CLIENT *
 * clntudp_create(raddr, program, version, wait, sockp)
 *	struct sockaddr_in *raddr;
 *	unsigned long program;
 *	unsigned long version;
 *	struct timeval wait;
 *	int *sockp;
 *
 * Same as above, but you specify max packet sizes.
 * CLIENT *
 * clntudp_bufcreate(raddr, program, version, wait, sockp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	unsigned long program;
 *	unsigned long version;
 *	struct timeval wait;
 *	int *sockp;
 *	unsigned int sendsz;
 *	unsigned int recvsz;
 */
__BEGIN_DECLS
extern CLIENT *clntudp_create(struct sockaddr_in *, unsigned long, 
    unsigned long, struct timeval, int *);
extern CLIENT *clntudp_bufcreate(struct sockaddr_in *, unsigned long, 
    unsigned long, struct timeval, int *, unsigned int, unsigned int);
__END_DECLS


/*
 * Print why creation failed
 */
__BEGIN_DECLS
extern void clnt_pcreateerror(char *);		/* stderr */
extern char *clnt_spcreateerror(char *);	/* string */
__END_DECLS

/*
 * Like clnt_perror(), but is more verbose in its output
 */ 
__BEGIN_DECLS
extern void clnt_perrno(enum clnt_stat);	/* stderr */
extern char *clnt_sperrno(enum clnt_stat);	/* string */
__END_DECLS

/*
 * Print an English error message, given the client error code
 */
__BEGIN_DECLS
extern void clnt_perror(CLIENT *, char *); 	/* stderr */
extern char *clnt_sperror(CLIENT *, char *);	/* string */
__END_DECLS


/* 
 * If a creation fails, the following allows the user to figure out why.
 */
struct rpc_createerr {
	enum clnt_stat cf_stat;
	struct rpc_err cf_error; /* useful when cf_stat == RPC_PMAPFAILURE */
};

extern struct rpc_createerr rpc_createerr;


#define UDPMSGSIZE	8800	/* rpc imposed limit on udp msg size */
#define RPCSMALLMSGSIZE	400	/* a more reasonable packet size */

#endif /* !_RPC_CLNT_H */
