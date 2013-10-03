/*
 *  linux/include/linux/sunrpc/clnt.h
 *
 *  Declarations for the high-level RPC client interface
 *
 *  Copyright (C) 1995, 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_CLNT_H
#define _LINUX_SUNRPC_CLNT_H

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>

#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/timer.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <asm/signal.h>
#include <linux/path.h>
#include <net/ipv6.h>

struct rpc_inode;

/*
 * The high-level client handle
 */
struct rpc_clnt {
	atomic_t		cl_count;	/* Number of references */
	unsigned int		cl_clid;	/* client id */
	struct list_head	cl_clients;	/* Global list of clients */
	struct list_head	cl_tasks;	/* List of tasks */
	spinlock_t		cl_lock;	/* spinlock */
	struct rpc_xprt __rcu *	cl_xprt;	/* transport */
	struct rpc_procinfo *	cl_procinfo;	/* procedure info */
	u32			cl_prog,	/* RPC program number */
				cl_vers,	/* RPC version number */
				cl_maxproc;	/* max procedure number */

	struct rpc_auth *	cl_auth;	/* authenticator */
	struct rpc_stat *	cl_stats;	/* per-program statistics */
	struct rpc_iostats *	cl_metrics;	/* per-client statistics */

	unsigned int		cl_softrtry : 1,/* soft timeouts */
				cl_discrtry : 1,/* disconnect before retry */
				cl_autobind : 1,/* use getport() */
				cl_chatty   : 1;/* be verbose */

	struct rpc_rtt *	cl_rtt;		/* RTO estimator data */
	const struct rpc_timeout *cl_timeout;	/* Timeout strategy */

	int			cl_nodelen;	/* nodename length */
	char 			cl_nodename[UNX_MAXNODENAME];
	struct rpc_pipe_dir_head cl_pipedir_objects;
	struct rpc_clnt *	cl_parent;	/* Points to parent of clones */
	struct rpc_rtt		cl_rtt_default;
	struct rpc_timeout	cl_timeout_default;
	const struct rpc_program *cl_program;
};

/*
 * General RPC program info
 */
#define RPC_MAXVERSION		4
struct rpc_program {
	const char *		name;		/* protocol name */
	u32			number;		/* program number */
	unsigned int		nrvers;		/* number of versions */
	const struct rpc_version **	version;	/* version array */
	struct rpc_stat *	stats;		/* statistics */
	const char *		pipe_dir_name;	/* path to rpc_pipefs dir */
};

struct rpc_version {
	u32			number;		/* version number */
	unsigned int		nrprocs;	/* number of procs */
	struct rpc_procinfo *	procs;		/* procedure array */
};

/*
 * Procedure information
 */
struct rpc_procinfo {
	u32			p_proc;		/* RPC procedure number */
	kxdreproc_t		p_encode;	/* XDR encode function */
	kxdrdproc_t		p_decode;	/* XDR decode function */
	unsigned int		p_arglen;	/* argument hdr length (u32) */
	unsigned int		p_replen;	/* reply hdr length (u32) */
	unsigned int		p_count;	/* call count */
	unsigned int		p_timer;	/* Which RTT timer to use */
	u32			p_statidx;	/* Which procedure to account */
	const char *		p_name;		/* name of procedure */
};

#ifdef __KERNEL__

struct rpc_create_args {
	struct net		*net;
	int			protocol;
	struct sockaddr		*address;
	size_t			addrsize;
	struct sockaddr		*saddress;
	const struct rpc_timeout *timeout;
	const char		*servername;
	const struct rpc_program *program;
	u32			prognumber;	/* overrides program->number */
	u32			version;
	rpc_authflavor_t	authflavor;
	unsigned long		flags;
	char			*client_name;
	struct svc_xprt		*bc_xprt;	/* NFSv4.1 backchannel */
};

/* Values for "flags" field */
#define RPC_CLNT_CREATE_HARDRTRY	(1UL << 0)
#define RPC_CLNT_CREATE_AUTOBIND	(1UL << 2)
#define RPC_CLNT_CREATE_NONPRIVPORT	(1UL << 3)
#define RPC_CLNT_CREATE_NOPING		(1UL << 4)
#define RPC_CLNT_CREATE_DISCRTRY	(1UL << 5)
#define RPC_CLNT_CREATE_QUIET		(1UL << 6)
#define RPC_CLNT_CREATE_INFINITE_SLOTS	(1UL << 7)
#define RPC_CLNT_CREATE_NO_IDLE_TIMEOUT	(1UL << 8)

struct rpc_clnt *rpc_create(struct rpc_create_args *args);
struct rpc_clnt	*rpc_bind_new_program(struct rpc_clnt *,
				const struct rpc_program *, u32);
void rpc_task_reset_client(struct rpc_task *task, struct rpc_clnt *clnt);
struct rpc_clnt *rpc_clone_client(struct rpc_clnt *);
struct rpc_clnt *rpc_clone_client_set_auth(struct rpc_clnt *,
				rpc_authflavor_t);
void		rpc_shutdown_client(struct rpc_clnt *);
void		rpc_release_client(struct rpc_clnt *);
void		rpc_task_release_client(struct rpc_task *);

int		rpcb_create_local(struct net *);
void		rpcb_put_local(struct net *);
int		rpcb_register(struct net *, u32, u32, int, unsigned short);
int		rpcb_v4_register(struct net *net, const u32 program,
				 const u32 version,
				 const struct sockaddr *address,
				 const char *netid);
void		rpcb_getport_async(struct rpc_task *);

void		rpc_call_start(struct rpc_task *);
int		rpc_call_async(struct rpc_clnt *clnt,
			       const struct rpc_message *msg, int flags,
			       const struct rpc_call_ops *tk_ops,
			       void *calldata);
int		rpc_call_sync(struct rpc_clnt *clnt,
			      const struct rpc_message *msg, int flags);
struct rpc_task *rpc_call_null(struct rpc_clnt *clnt, struct rpc_cred *cred,
			       int flags);
int		rpc_restart_call_prepare(struct rpc_task *);
int		rpc_restart_call(struct rpc_task *);
void		rpc_setbufsize(struct rpc_clnt *, unsigned int, unsigned int);
int		rpc_protocol(struct rpc_clnt *);
struct net *	rpc_net_ns(struct rpc_clnt *);
size_t		rpc_max_payload(struct rpc_clnt *);
unsigned long	rpc_get_timeout(struct rpc_clnt *clnt);
void		rpc_force_rebind(struct rpc_clnt *);
size_t		rpc_peeraddr(struct rpc_clnt *, struct sockaddr *, size_t);
const char	*rpc_peeraddr2str(struct rpc_clnt *, enum rpc_display_format_t);
int		rpc_localaddr(struct rpc_clnt *, struct sockaddr *, size_t);

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_CLNT_H */
