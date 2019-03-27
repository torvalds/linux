/*	$NetBSD: svc.h,v 1.17 2000/06/02 22:57:56 fvdl Exp $	*/

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
 *
 *	from: @(#)svc.h 1.35 88/12/17 SMI
 *	from: @(#)svc.h      1.27    94/04/25 SMI
 * $FreeBSD$
 */

/*
 * svc.h, Server-side remote procedure call interface.
 *
 * Copyright (C) 1986-1993 by Sun Microsystems, Inc.
 */

#ifndef _RPC_SVC_H
#define _RPC_SVC_H
#include <sys/cdefs.h>

#ifdef _KERNEL
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_sx.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#endif

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received.  The two most notable transports are TCP and UDP;  they are
 * created and registered by routines in svc_tcp.c and svc_udp.c, respectively;
 * they in turn call xprt_register and xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * port number from the transport handle are registered with the binder.
 * These data are registered with the rpc svc system via svc_register.
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

/*
 *      Service control requests
 */
#define SVCGET_VERSQUIET	1
#define SVCSET_VERSQUIET	2
#define SVCGET_CONNMAXREC	3
#define SVCSET_CONNMAXREC	4

/*
 * Operations for rpc_control().
 */
#define RPC_SVC_CONNMAXREC_SET  0	/* set max rec size, enable nonblock */
#define RPC_SVC_CONNMAXREC_GET  1

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

struct __rpc_svcxprt;
struct mbuf;

struct xp_ops {
#ifdef _KERNEL
	/* receive incoming requests */
	bool_t	(*xp_recv)(struct __rpc_svcxprt *, struct rpc_msg *,
	    struct sockaddr **, struct mbuf **);
	/* get transport status */
	enum xprt_stat (*xp_stat)(struct __rpc_svcxprt *);
	/* get transport acknowledge sequence */
	bool_t (*xp_ack)(struct __rpc_svcxprt *, uint32_t *);
	/* send reply */
	bool_t	(*xp_reply)(struct __rpc_svcxprt *, struct rpc_msg *,
	    struct sockaddr *, struct mbuf *, uint32_t *);
	/* destroy this struct */
	void	(*xp_destroy)(struct __rpc_svcxprt *);
	/* catch-all function */
	bool_t  (*xp_control)(struct __rpc_svcxprt *, const u_int, void *);
#else
	/* receive incoming requests */
	bool_t	(*xp_recv)(struct __rpc_svcxprt *, struct rpc_msg *);
	/* get transport status */
	enum xprt_stat (*xp_stat)(struct __rpc_svcxprt *);
	/* get arguments */
	bool_t	(*xp_getargs)(struct __rpc_svcxprt *, xdrproc_t, void *);
	/* send reply */
	bool_t	(*xp_reply)(struct __rpc_svcxprt *, struct rpc_msg *);
	/* free mem allocated for args */
	bool_t	(*xp_freeargs)(struct __rpc_svcxprt *, xdrproc_t, void *);
	/* destroy this struct */
	void	(*xp_destroy)(struct __rpc_svcxprt *);
#endif
};

#ifndef _KERNEL
struct xp_ops2 {
	/* catch-all function */
	bool_t  (*xp_control)(struct __rpc_svcxprt *, const u_int, void *);
};
#endif

#ifdef _KERNEL
struct __rpc_svcpool;
struct __rpc_svcgroup;
struct __rpc_svcthread;
#endif

/*
 * Server side transport handle. In the kernel, transports have a
 * reference count which tracks the number of currently assigned
 * worker threads plus one for the service pool's reference.
 * For NFSv4.1 sessions, a reference is also held for a backchannel.
 */
typedef struct __rpc_svcxprt {
#ifdef _KERNEL
	volatile u_int	xp_refs;
	struct sx	xp_lock;
	struct __rpc_svcpool *xp_pool;  /* owning pool (see below) */
	struct __rpc_svcgroup *xp_group; /* owning group (see below) */
	TAILQ_ENTRY(__rpc_svcxprt) xp_link;
	TAILQ_ENTRY(__rpc_svcxprt) xp_alink;
	bool_t		xp_registered;	/* xprt_register has been called */
	bool_t		xp_active;	/* xprt_active has been called */
	struct __rpc_svcthread *xp_thread; /* assigned service thread */
	struct socket*	xp_socket;
	const struct xp_ops *xp_ops;
	char		*xp_netid;	/* network token */
	struct sockaddr_storage xp_ltaddr; /* local transport address */
	struct sockaddr_storage	xp_rtaddr; /* remote transport address */
	void		*xp_p1;		/* private: for use by svc ops */
	void		*xp_p2;		/* private: for use by svc ops */
	void		*xp_p3;		/* private: for use by svc lib */
	int		xp_type;	/* transport type */
	int		xp_idletimeout; /* idle time before closing */
	time_t		xp_lastactive;	/* time of last RPC */
	u_int64_t	xp_sockref;	/* set by nfsv4 to identify socket */
	int		xp_upcallset;	/* socket upcall is set up */
	uint32_t	xp_snd_cnt;	/* # of bytes to send to socket */
	uint32_t	xp_snt_cnt;	/* # of bytes sent to socket */
#else
	int		xp_fd;
	u_short		xp_port;	 /* associated port number */
	const struct xp_ops *xp_ops;
	int		xp_addrlen;	 /* length of remote address */
	struct sockaddr_in xp_raddr;	 /* remote addr. (backward ABI compat) */
	/* XXX - fvdl stick this here for ABI backward compat reasons */
	const struct xp_ops2 *xp_ops2;
	char		*xp_tp;		 /* transport provider device name */
	char		*xp_netid;	 /* network token */
	struct netbuf	xp_ltaddr;	 /* local transport address */
	struct netbuf	xp_rtaddr;	 /* remote transport address */
	struct opaque_auth xp_verf;	 /* raw response verifier */
	void		*xp_p1;		 /* private: for use by svc ops */
	void		*xp_p2;		 /* private: for use by svc ops */
	void		*xp_p3;		 /* private: for use by svc lib */
	int		xp_type;	 /* transport type */
#endif
} SVCXPRT;

/*
 * Interface to server-side authentication flavors.
 */
typedef struct __rpc_svcauth {
	struct svc_auth_ops {
#ifdef _KERNEL
		int   (*svc_ah_wrap)(struct __rpc_svcauth *,  struct mbuf **);
		int   (*svc_ah_unwrap)(struct __rpc_svcauth *, struct mbuf **);
		void  (*svc_ah_release)(struct __rpc_svcauth *);
#else
		int   (*svc_ah_wrap)(struct __rpc_svcauth *, XDR *,
		    xdrproc_t, caddr_t);
		int   (*svc_ah_unwrap)(struct __rpc_svcauth *, XDR *,
		    xdrproc_t, caddr_t);
#endif
	} *svc_ah_ops;
	void *svc_ah_private;
} SVCAUTH;

/*
 * Server transport extensions (accessed via xp_p3).
 */
typedef struct __rpc_svcxprt_ext {
	int		xp_flags;	/* versquiet */
	SVCAUTH		xp_auth;	/* interface to auth methods */
} SVCXPRT_EXT;

#ifdef _KERNEL

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * appropriate procedure.
 */
struct svc_callout {
	TAILQ_ENTRY(svc_callout) sc_link;
	rpcprog_t	    sc_prog;
	rpcvers_t	    sc_vers;
	char		   *sc_netid;
	void		    (*sc_dispatch)(struct svc_req *, SVCXPRT *);
};
TAILQ_HEAD(svc_callout_list, svc_callout);

/*
 * The services connection loss list
 * The dispatch routine takes request structs and runs the
 * appropriate procedure.
 */
struct svc_loss_callout {
	TAILQ_ENTRY(svc_loss_callout) slc_link;
	void		    (*slc_dispatch)(SVCXPRT *);
};
TAILQ_HEAD(svc_loss_callout_list, svc_loss_callout);

/*
 * Service request
 */
struct svc_req {
	STAILQ_ENTRY(svc_req) rq_link;	/* list of requests for a thread */
	struct __rpc_svcthread *rq_thread; /* thread which is to execute this */
	uint32_t	rq_xid;		/* RPC transaction ID */
	uint32_t	rq_prog;	/* service program number */
	uint32_t	rq_vers;	/* service protocol version */
	uint32_t	rq_proc;	/* the desired procedure */
	size_t		rq_size;	/* space used by request */
	struct mbuf	*rq_args;	/* XDR-encoded procedure arguments */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	struct opaque_auth rq_verf;	/* verifier for the reply */
	void		*rq_clntcred;	/* read only cooked cred */
	SVCAUTH		rq_auth;	/* interface to auth methods */
	SVCXPRT		*rq_xprt;	/* associated transport */
	struct sockaddr	*rq_addr;	/* reply address or NULL if connected */
	void		*rq_p1;		/* application workspace */
	int		rq_p2;		/* application workspace */
	uint64_t	rq_p3;		/* application workspace */
	uint32_t	rq_reply_seq;	/* reply socket sequence # */
	char		rq_credarea[3*MAX_AUTH_BYTES];
};
STAILQ_HEAD(svc_reqlist, svc_req);

#define svc_getrpccaller(rq)					\
	((rq)->rq_addr ? (rq)->rq_addr :			\
	    (struct sockaddr *) &(rq)->rq_xprt->xp_rtaddr)

/*
 * This structure is used to manage a thread which is executing
 * requests from a service pool. A service thread is in one of three
 * states:
 *
 *	SVCTHREAD_SLEEPING	waiting for a request to process
 *	SVCTHREAD_ACTIVE	processing a request
 *	SVCTHREAD_EXITING	exiting after finishing current request
 *
 * Threads which have no work to process sleep on the pool's sp_active
 * list. When a transport becomes active, it is assigned a service
 * thread to read and execute pending RPCs.
 */
typedef struct __rpc_svcthread {
	struct mtx_padalign	st_lock; /* protects st_reqs field */
	struct __rpc_svcpool	*st_pool;
	SVCXPRT			*st_xprt; /* transport we are processing */
	struct svc_reqlist	st_reqs;  /* RPC requests to execute */
	struct cv		st_cond; /* sleeping for work */
	LIST_ENTRY(__rpc_svcthread) st_ilink; /* idle threads list */
	LIST_ENTRY(__rpc_svcthread) st_alink; /* application thread list */
	int		st_p2;		/* application workspace */
	uint64_t	st_p3;		/* application workspace */
} SVCTHREAD;
LIST_HEAD(svcthread_list, __rpc_svcthread);

/*
 * A thread group contain all information needed to assign subset of
 * transports to subset of threads.  On systems with many CPUs and many
 * threads that allows to reduce lock congestion and improve performance.
 * Hundreds of threads on dozens of CPUs sharing the single pool lock do
 * not scale well otherwise.
 */
TAILQ_HEAD(svcxprt_list, __rpc_svcxprt);
enum svcpool_state {
	SVCPOOL_INIT,		/* svc_run not called yet */
	SVCPOOL_ACTIVE,		/* normal running state */
	SVCPOOL_THREADWANTED,	/* new service thread requested */
	SVCPOOL_THREADSTARTING,	/* new service thread started */
	SVCPOOL_CLOSING		/* svc_exit called */
};
typedef struct __rpc_svcgroup {
	struct mtx_padalign sg_lock;	/* protect the thread/req lists */
	struct __rpc_svcpool	*sg_pool;
	enum svcpool_state sg_state;	/* current pool state */
	struct svcxprt_list sg_xlist;	/* all transports in the group */
	struct svcxprt_list sg_active;	/* transports needing service */
	struct svcthread_list sg_idlethreads; /* idle service threads */

	int		sg_minthreads;	/* minimum service thread count */
	int		sg_maxthreads;	/* maximum service thread count */
	int		sg_threadcount; /* current service thread count */
	time_t		sg_lastcreatetime; /* when we last started a thread */
	time_t		sg_lastidlecheck;  /* when we last checked idle transports */
} SVCGROUP;

/*
 * In the kernel, we can't use global variables to store lists of
 * transports etc. since otherwise we could not have two unrelated RPC
 * services running, each on its own thread. We solve this by
 * importing a tiny part of a Solaris kernel concept, SVCPOOL.
 *
 * A service pool contains a set of transports and service callbacks
 * for a set of related RPC services. The pool handle should be passed
 * when creating new transports etc. Future work may include extending
 * this to support something similar to the Solaris multi-threaded RPC
 * server.
 */
typedef SVCTHREAD *pool_assign_fn(SVCTHREAD *, struct svc_req *);
typedef void pool_done_fn(SVCTHREAD *, struct svc_req *);
#define	SVC_MAXGROUPS	16
typedef struct __rpc_svcpool {
	struct mtx_padalign sp_lock;	/* protect the transport lists */
	const char	*sp_name;	/* pool name (e.g. "nfsd", "NLM" */
	enum svcpool_state sp_state;	/* current pool state */
	struct proc	*sp_proc;	/* process which is in svc_run */
	struct svc_callout_list sp_callouts; /* (prog,vers)->dispatch list */
	struct svc_loss_callout_list sp_lcallouts; /* loss->dispatch list */
	int		sp_minthreads;	/* minimum service thread count */
	int		sp_maxthreads;	/* maximum service thread count */

	/*
	 * Hooks to allow an application to control request to thread
	 * placement.
	 */
	pool_assign_fn	*sp_assign;
	pool_done_fn	*sp_done;

	/*
	 * These variables are used to put an upper bound on the
	 * amount of memory used by RPC requests which are queued
	 * waiting for execution.
	 */
	unsigned long	sp_space_low;
	unsigned long	sp_space_high;
	unsigned long	sp_space_used;
	unsigned long	sp_space_used_highest;
	bool_t		sp_space_throttled;
	int		sp_space_throttle_count;

	struct replay_cache *sp_rcache; /* optional replay cache */
	struct sysctl_ctx_list sp_sysctl;

	int		sp_groupcount;	/* Number of groups in the pool. */
	int		sp_nextgroup;	/* Next group to assign port. */
	SVCGROUP	sp_groups[SVC_MAXGROUPS]; /* Thread/port groups. */
} SVCPOOL;

#else

/*
 * Service request
 */
struct svc_req {
	uint32_t	rq_prog;	/* service program number */
	uint32_t	rq_vers;	/* service protocol version */
	uint32_t	rq_proc;	/* the desired procedure */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	void		*rq_clntcred;	/* read only cooked cred */
	SVCXPRT		*rq_xprt;	/* associated transport */
};

/*
 *  Approved way of getting address of caller
 */
#define svc_getrpccaller(x) (&(x)->xp_rtaddr)

#endif

/*
 * Operations defined on an SVCXPRT handle
 *
 * SVCXPRT		*xprt;
 * struct rpc_msg	*msg;
 * xdrproc_t		 xargs;
 * void *		 argsp;
 */
#ifdef _KERNEL

#define SVC_ACQUIRE(xprt)			\
	refcount_acquire(&(xprt)->xp_refs)

#define SVC_RELEASE(xprt)			\
	if (refcount_release(&(xprt)->xp_refs))	\
		SVC_DESTROY(xprt)

#define SVC_RECV(xprt, msg, addr, args)			\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg), (addr), (args))

#define SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define SVC_ACK(xprt, ack)				\
	((xprt)->xp_ops->xp_ack == NULL ? FALSE :	\
	    ((ack) == NULL ? TRUE : (*(xprt)->xp_ops->xp_ack)((xprt), (ack))))

#define SVC_REPLY(xprt, msg, addr, m, seq)			\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg), (addr), (m), (seq))

#define SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)

#define SVC_CONTROL(xprt, rq, in)			\
	(*(xprt)->xp_ops->xp_control)((xprt), (rq), (in))

#else

#define SVC_RECV(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define svc_recv(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))

#define SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define svc_stat(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define SVC_GETARGS(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define svc_getargs(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define SVC_REPLY(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define svc_reply(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define SVC_FREEARGS(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define svc_freeargs(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define svc_destroy(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)

#define SVC_CONTROL(xprt, rq, in)			\
	(*(xprt)->xp_ops2->xp_control)((xprt), (rq), (in))

#endif

#define SVC_EXT(xprt)					\
	((SVCXPRT_EXT *) xprt->xp_p3)

#define SVC_AUTH(xprt)					\
	(SVC_EXT(xprt)->xp_auth)

/*
 * Operations defined on an SVCAUTH handle
 */
#ifdef _KERNEL
#define SVCAUTH_WRAP(auth, mp)		\
	((auth)->svc_ah_ops->svc_ah_wrap(auth, mp))
#define SVCAUTH_UNWRAP(auth, mp)	\
	((auth)->svc_ah_ops->svc_ah_unwrap(auth, mp))
#define SVCAUTH_RELEASE(auth)	\
	((auth)->svc_ah_ops->svc_ah_release(auth))
#else
#define SVCAUTH_WRAP(auth, xdrs, xfunc, xwhere)		\
	((auth)->svc_ah_ops->svc_ah_wrap(auth, xdrs, xfunc, xwhere))
#define SVCAUTH_UNWRAP(auth, xdrs, xfunc, xwhere)	\
	((auth)->svc_ah_ops->svc_ah_unwrap(auth, xdrs, xfunc, xwhere))
#endif

/*
 * Service registration
 *
 * svc_reg(xprt, prog, vers, dispatch, nconf)
 *	const SVCXPRT *xprt;
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 *	const void (*dispatch)();
 *	const struct netconfig *nconf;
 */

__BEGIN_DECLS
extern bool_t	svc_reg(SVCXPRT *, const rpcprog_t, const rpcvers_t,
			void (*)(struct svc_req *, SVCXPRT *),
			const struct netconfig *);
__END_DECLS

/*
 * Service un-registration
 *
 * svc_unreg(prog, vers)
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 */

__BEGIN_DECLS
#ifdef _KERNEL
extern void	svc_unreg(SVCPOOL *, const rpcprog_t, const rpcvers_t);
#else
extern void	svc_unreg(const rpcprog_t, const rpcvers_t);
#endif
__END_DECLS

#ifdef _KERNEL
/*
 * Service connection loss registration
 *
 * svc_loss_reg(xprt, dispatch)
 *	const SVCXPRT *xprt;
 *	const void (*dispatch)();
 */

__BEGIN_DECLS
extern bool_t	svc_loss_reg(SVCXPRT *, void (*)(SVCXPRT *));
__END_DECLS

/*
 * Service connection loss un-registration
 *
 * svc_loss_unreg(xprt, dispatch)
 *	const SVCXPRT *xprt;
 *	const void (*dispatch)();
 */

__BEGIN_DECLS
extern void	svc_loss_unreg(SVCPOOL *, void (*)(SVCXPRT *));
__END_DECLS
#endif

/*
 * Transport registration.
 *
 * xprt_register(xprt)
 *	SVCXPRT *xprt;
 */
__BEGIN_DECLS
extern void	xprt_register(SVCXPRT *);
__END_DECLS

/*
 * Transport un-register
 *
 * xprt_unregister(xprt)
 *	SVCXPRT *xprt;
 */
__BEGIN_DECLS
extern void	xprt_unregister(SVCXPRT *);
extern void	__xprt_unregister_unlocked(SVCXPRT *);
__END_DECLS

#ifdef _KERNEL

/*
 * Called when a transport has pending requests.
 */
__BEGIN_DECLS
extern void	xprt_active(SVCXPRT *);
extern void	xprt_inactive(SVCXPRT *);
extern void	xprt_inactive_locked(SVCXPRT *);
extern void	xprt_inactive_self(SVCXPRT *);
__END_DECLS

#endif

/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, tcp based rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */

__BEGIN_DECLS
#ifdef _KERNEL
extern bool_t	svc_sendreply(struct svc_req *, xdrproc_t, void *);
extern bool_t	svc_sendreply_mbuf(struct svc_req *, struct mbuf *);
extern void	svcerr_decode(struct svc_req *);
extern void	svcerr_weakauth(struct svc_req *);
extern void	svcerr_noproc(struct svc_req *);
extern void	svcerr_progvers(struct svc_req *, rpcvers_t, rpcvers_t);
extern void	svcerr_auth(struct svc_req *, enum auth_stat);
extern void	svcerr_noprog(struct svc_req *);
extern void	svcerr_systemerr(struct svc_req *);
#else
extern bool_t	svc_sendreply(SVCXPRT *, xdrproc_t, void *);
extern void	svcerr_decode(SVCXPRT *);
extern void	svcerr_weakauth(SVCXPRT *);
extern void	svcerr_noproc(SVCXPRT *);
extern void	svcerr_progvers(SVCXPRT *, rpcvers_t, rpcvers_t);
extern void	svcerr_auth(SVCXPRT *, enum auth_stat);
extern void	svcerr_noprog(SVCXPRT *);
extern void	svcerr_systemerr(SVCXPRT *);
#endif
extern int	rpc_reg(rpcprog_t, rpcvers_t, rpcproc_t,
			char *(*)(char *), xdrproc_t, xdrproc_t,
			char *);
__END_DECLS

/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (co-existant) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq is provided.  It must be passed readfds, the
 * "in-place" results of a select system call (see select, section 2).
 */

#ifndef _KERNEL
/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select
 */
extern int svc_maxfd;
#ifdef FD_SETSIZE
extern fd_set svc_fdset;
#define svc_fds svc_fdset.fds_bits[0]	/* compatibility */
#else
extern int svc_fds;
#endif /* def FD_SETSIZE */
#endif

/*
 * a small program implemented by the svc_rpc implementation itself;
 * also see clnt.h for protocol numbers.
 */
__BEGIN_DECLS
extern void rpctest_service(void);
__END_DECLS

__BEGIN_DECLS
extern SVCXPRT *svc_xprt_alloc(void);
extern void	svc_xprt_free(SVCXPRT *);
#ifndef _KERNEL
extern void	svc_getreq(int);
extern void	svc_getreqset(fd_set *);
extern void	svc_getreq_common(int);
struct pollfd;
extern void	svc_getreq_poll(struct pollfd *, int);
extern void	svc_run(void);
extern void	svc_exit(void);
#else
extern void	svc_run(SVCPOOL *);
extern void	svc_exit(SVCPOOL *);
extern bool_t	svc_getargs(struct svc_req *, xdrproc_t, void *);
extern bool_t	svc_freeargs(struct svc_req *, xdrproc_t, void *);
extern void	svc_freereq(struct svc_req *);

#endif
__END_DECLS

/*
 * Socket to use on svcxxx_create call to get default socket
 */
#define	RPC_ANYSOCK	-1
#define RPC_ANYFD	RPC_ANYSOCK

/*
 * These are the existing service side transport implementations
 */

__BEGIN_DECLS

#ifdef _KERNEL

/*
 * Create a new service pool.
 */
extern SVCPOOL* svcpool_create(const char *name,
    struct sysctl_oid_list *sysctl_base);

/*
 * Destroy a service pool, including all registered transports.
 */
extern void svcpool_destroy(SVCPOOL *pool);

/*
 * Close a service pool.  Similar to svcpool_destroy(), but it does not
 * free the data structures.  As such, the pool can be used again.
 */
extern void svcpool_close(SVCPOOL *pool);

/*
 * Transport independent svc_create routine.
 */
extern int svc_create(SVCPOOL *, void (*)(struct svc_req *, SVCXPRT *),
    const rpcprog_t, const rpcvers_t, const char *);
/*
 *      void (*dispatch)();             -- dispatch routine
 *      const rpcprog_t prognum;        -- program number
 *      const rpcvers_t versnum;        -- version number
 *      const char *nettype;            -- network type
 */


/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */

extern SVCXPRT *svc_tp_create(SVCPOOL *, void (*)(struct svc_req *, SVCXPRT *),
    const rpcprog_t, const rpcvers_t, const char *uaddr,
    const struct netconfig *);
        /*
         * void (*dispatch)();            -- dispatch routine
         * const rpcprog_t prognum;       -- program number
         * const rpcvers_t versnum;       -- version number
	 * const char *uaddr;		  -- universal address of service
         * const struct netconfig *nconf; -- netconfig structure
         */

extern SVCXPRT *svc_dg_create(SVCPOOL *, struct socket *,
    const size_t, const size_t);
        /*
         * struct socket *;                             -- open connection
         * const size_t sendsize;                        -- max send size
         * const size_t recvsize;                        -- max recv size
         */

extern SVCXPRT *svc_vc_create(SVCPOOL *, struct socket *,
    const size_t, const size_t);
        /*
         * struct socket *;                             -- open connection
         * const size_t sendsize;                        -- max send size
         * const size_t recvsize;                        -- max recv size
         */

extern SVCXPRT *svc_vc_create_backchannel(SVCPOOL *);

extern void *clnt_bck_create(struct socket *, const rpcprog_t, const rpcvers_t);
	/*
	 * struct socket *;			-- server transport socket
	 * const rpcprog_t prog;		-- RPC program number
	 * const rpcvers_t vers;		-- RPC program version
	 */

/*
 * Generic TLI create routine
 */
extern SVCXPRT *svc_tli_create(SVCPOOL *, struct socket *,
    const struct netconfig *, const struct t_bind *, const size_t, const size_t);
/*
 *      struct socket * so;             -- connection end point
 *      const struct netconfig *nconf;  -- netconfig structure for network
 *      const struct t_bind *bindaddr;  -- local bind address
 *      const size_t sendsz;             -- max sendsize
 *      const size_t recvsz;             -- max recvsize
 */

#else /* !_KERNEL */

/*
 * Transport independent svc_create routine.
 */
extern int svc_create(void (*)(struct svc_req *, SVCXPRT *),
			   const rpcprog_t, const rpcvers_t, const char *);
/*
 *      void (*dispatch)();             -- dispatch routine
 *      const rpcprog_t prognum;        -- program number
 *      const rpcvers_t versnum;        -- version number
 *      const char *nettype;            -- network type
 */


/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */

extern SVCXPRT *svc_tp_create(void (*)(struct svc_req *, SVCXPRT *),
				   const rpcprog_t, const rpcvers_t,
				   const struct netconfig *);
        /*
         * void (*dispatch)();            -- dispatch routine
         * const rpcprog_t prognum;       -- program number
         * const rpcvers_t versnum;       -- version number
         * const struct netconfig *nconf; -- netconfig structure
         */

/*
 * Generic TLI create routine
 */
extern SVCXPRT *svc_tli_create(const int, const struct netconfig *,
			       const struct t_bind *, const u_int,
			       const u_int);
/*
 *      const int fd;                   -- connection end point
 *      const struct netconfig *nconf;  -- netconfig structure for network
 *      const struct t_bind *bindaddr;  -- local bind address
 *      const u_int sendsz;             -- max sendsize
 *      const u_int recvsz;             -- max recvsize
 */

/*
 * Connectionless and connectionful create routines
 */

extern SVCXPRT *svc_vc_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_vc_create().
 */
extern SVCXPRT *svcunix_create(int, u_int, u_int, char *);

extern SVCXPRT *svc_dg_create(const int, const u_int, const u_int);
        /*
         * const int fd;                                -- open connection
         * const u_int sendsize;                        -- max send size
         * const u_int recvsize;                        -- max recv size
         */


/*
 * the routine takes any *open* connection
 * descriptor as its first input and is used for open connections.
 */
extern SVCXPRT *svc_fd_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_fd_create().
 */
extern SVCXPRT *svcunixfd_create(int, u_int, u_int);

/*
 * Memory based rpc (for speed check and testing)
 */
extern SVCXPRT *svc_raw_create(void);

/*
 * svc_dg_enable_cache() enables the cache on dg transports.
 */
int svc_dg_enablecache(SVCXPRT *, const u_int);

int __rpc_get_local_uid(SVCXPRT *_transp, uid_t *_uid);

#endif	/* !_KERNEL */

__END_DECLS

#ifndef _KERNEL
/* for backward compatibility */
#include <rpc/svc_soc.h>
#endif

#endif /* !_RPC_SVC_H */
