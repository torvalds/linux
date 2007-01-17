/*
 *  linux/include/linux/sunrpc/clnt.h
 *
 *  Declarations for the high-level RPC client interface
 *
 *  Copyright (C) 1995, 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_CLNT_H
#define _LINUX_SUNRPC_CLNT_H

#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/timer.h>
#include <asm/signal.h>

struct rpc_inode;

/*
 * The high-level client handle
 */
struct rpc_clnt {
	atomic_t		cl_count;	/* Number of clones */
	atomic_t		cl_users;	/* number of references */
	struct rpc_xprt *	cl_xprt;	/* transport */
	struct rpc_procinfo *	cl_procinfo;	/* procedure info */
	u32			cl_prog,	/* RPC program number */
				cl_vers,	/* RPC version number */
				cl_maxproc;	/* max procedure number */

	char *			cl_server;	/* server machine name */
	char *			cl_protname;	/* protocol name */
	struct rpc_auth *	cl_auth;	/* authenticator */
	struct rpc_stat *	cl_stats;	/* per-program statistics */
	struct rpc_iostats *	cl_metrics;	/* per-client statistics */

	unsigned int		cl_softrtry : 1,/* soft timeouts */
				cl_intr     : 1,/* interruptible */
				cl_autobind : 1,/* use getport() */
				cl_oneshot  : 1,/* dispose after use */
				cl_dead     : 1;/* abandoned */

	struct rpc_rtt *	cl_rtt;		/* RTO estimator data */

	int			cl_nodelen;	/* nodename length */
	char 			cl_nodename[UNX_MAXNODENAME];
	char			cl_pathname[30];/* Path in rpc_pipe_fs */
	struct vfsmount *	cl_vfsmnt;
	struct dentry *		cl_dentry;	/* inode */
	struct rpc_clnt *	cl_parent;	/* Points to parent of clones */
	struct rpc_rtt		cl_rtt_default;
	struct rpc_program *	cl_program;
	char			cl_inline_name[32];
};

/*
 * General RPC program info
 */
#define RPC_MAXVERSION		4
struct rpc_program {
	char *			name;		/* protocol name */
	u32			number;		/* program number */
	unsigned int		nrvers;		/* number of versions */
	struct rpc_version **	version;	/* version array */
	struct rpc_stat *	stats;		/* statistics */
	char *			pipe_dir_name;	/* path to rpc_pipefs dir */
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
	kxdrproc_t		p_encode;	/* XDR encode function */
	kxdrproc_t		p_decode;	/* XDR decode function */
	unsigned int		p_bufsiz;	/* req. buffer size */
	unsigned int		p_count;	/* call count */
	unsigned int		p_timer;	/* Which RTT timer to use */
	u32			p_statidx;	/* Which procedure to account */
	char *			p_name;		/* name of procedure */
};

#ifdef __KERNEL__

struct rpc_create_args {
	int			protocol;
	struct sockaddr		*address;
	size_t			addrsize;
	struct rpc_timeout	*timeout;
	char			*servername;
	struct rpc_program	*program;
	u32			version;
	rpc_authflavor_t	authflavor;
	unsigned long		flags;
};

/* Values for "flags" field */
#define RPC_CLNT_CREATE_HARDRTRY	(1UL << 0)
#define RPC_CLNT_CREATE_INTR		(1UL << 1)
#define RPC_CLNT_CREATE_AUTOBIND	(1UL << 2)
#define RPC_CLNT_CREATE_ONESHOT		(1UL << 3)
#define RPC_CLNT_CREATE_NONPRIVPORT	(1UL << 4)
#define RPC_CLNT_CREATE_NOPING		(1UL << 5)

struct rpc_clnt *rpc_create(struct rpc_create_args *args);
struct rpc_clnt	*rpc_bind_new_program(struct rpc_clnt *,
				struct rpc_program *, int);
struct rpc_clnt *rpc_clone_client(struct rpc_clnt *);
int		rpc_shutdown_client(struct rpc_clnt *);
int		rpc_destroy_client(struct rpc_clnt *);
void		rpc_release_client(struct rpc_clnt *);
void		rpc_getport(struct rpc_task *);
int		rpc_register(u32, u32, int, unsigned short, int *);

void		rpc_call_setup(struct rpc_task *, struct rpc_message *, int);

int		rpc_call_async(struct rpc_clnt *clnt, struct rpc_message *msg,
			       int flags, const struct rpc_call_ops *tk_ops,
			       void *calldata);
int		rpc_call_sync(struct rpc_clnt *clnt, struct rpc_message *msg,
			      int flags);
void		rpc_restart_call(struct rpc_task *);
void		rpc_clnt_sigmask(struct rpc_clnt *clnt, sigset_t *oldset);
void		rpc_clnt_sigunmask(struct rpc_clnt *clnt, sigset_t *oldset);
void		rpc_setbufsize(struct rpc_clnt *, unsigned int, unsigned int);
size_t		rpc_max_payload(struct rpc_clnt *);
void		rpc_force_rebind(struct rpc_clnt *);
int		rpc_ping(struct rpc_clnt *clnt, int flags);
size_t		rpc_peeraddr(struct rpc_clnt *, struct sockaddr *, size_t);
char *		rpc_peeraddr2str(struct rpc_clnt *, enum rpc_display_format_t);

/*
 * Helper function for NFSroot support
 */
int		rpc_getport_external(struct sockaddr_in *, __u32, __u32, int);

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_CLNT_H */
