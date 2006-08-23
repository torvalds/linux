/*
 * linux/net/sunrpc/pmap_clnt.c
 *
 * In-kernel RPC portmapper client.
 *
 * Portmapper supports version 2 of the rpcbind protocol (RFC 1833).
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/in.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_PMAP
#endif

#define PMAP_SET		1
#define PMAP_UNSET		2
#define PMAP_GETPORT		3

struct portmap_args {
	u32			pm_prog;
	u32			pm_vers;
	u32			pm_prot;
	unsigned short		pm_port;
	struct rpc_task *	pm_task;
};

static struct rpc_procinfo	pmap_procedures[];
static struct rpc_clnt *	pmap_create(char *, struct sockaddr_in *, int, int);
static void			pmap_getport_done(struct rpc_task *, void *);
static struct rpc_program	pmap_program;

static void pmap_getport_prepare(struct rpc_task *task, void *calldata)
{
	struct portmap_args *map = calldata;
	struct rpc_message msg = {
		.rpc_proc	= &pmap_procedures[PMAP_GETPORT],
		.rpc_argp	= map,
		.rpc_resp	= &map->pm_port,
	};

	rpc_call_setup(task, &msg, 0);
}

static inline struct portmap_args *pmap_map_alloc(void)
{
	return kmalloc(sizeof(struct portmap_args), GFP_NOFS);
}

static inline void pmap_map_free(struct portmap_args *map)
{
	kfree(map);
}

static void pmap_map_release(void *data)
{
	pmap_map_free(data);
}

static const struct rpc_call_ops pmap_getport_ops = {
	.rpc_call_prepare	= pmap_getport_prepare,
	.rpc_call_done		= pmap_getport_done,
	.rpc_release		= pmap_map_release,
};

static inline void pmap_wake_portmap_waiters(struct rpc_xprt *xprt)
{
	xprt_clear_binding(xprt);
	rpc_wake_up(&xprt->binding);
}

/**
 * rpc_getport - obtain the port for a given RPC service on a given host
 * @task: task that is waiting for portmapper request
 *
 * This one can be called for an ongoing RPC request, and can be used in
 * an async (rpciod) context.
 */
void rpc_getport(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_xprt *xprt = task->tk_xprt;
	struct sockaddr_in addr;
	struct portmap_args *map;
	struct rpc_clnt	*pmap_clnt;
	struct rpc_task *child;

	dprintk("RPC: %4d rpc_getport(%s, %u, %u, %d)\n",
			task->tk_pid, clnt->cl_server,
			clnt->cl_prog, clnt->cl_vers, xprt->prot);

	/* Autobind on cloned rpc clients is discouraged */
	BUG_ON(clnt->cl_parent != clnt);

	if (xprt_test_and_set_binding(xprt)) {
		task->tk_status = -EACCES;	/* tell caller to check again */
		rpc_sleep_on(&xprt->binding, task, NULL, NULL);
		return;
	}

	/* Someone else may have bound if we slept */
	if (xprt_bound(xprt)) {
		task->tk_status = 0;
		goto bailout_nofree;
	}

	map = pmap_map_alloc();
	if (!map) {
		task->tk_status = -ENOMEM;
		goto bailout_nofree;
	}
	map->pm_prog = clnt->cl_prog;
	map->pm_vers = clnt->cl_vers;
	map->pm_prot = xprt->prot;
	map->pm_port = 0;
	map->pm_task = task;

	rpc_peeraddr(clnt, (struct sockaddr *) &addr, sizeof(addr));
	pmap_clnt = pmap_create(clnt->cl_server, &addr, map->pm_prot, 0);
	if (IS_ERR(pmap_clnt)) {
		task->tk_status = PTR_ERR(pmap_clnt);
		goto bailout;
	}

	child = rpc_run_task(pmap_clnt, RPC_TASK_ASYNC, &pmap_getport_ops, map);
	if (IS_ERR(child)) {
		task->tk_status = -EIO;
		goto bailout;
	}
	rpc_release_task(child);

	rpc_sleep_on(&xprt->binding, task, NULL, NULL);

	task->tk_xprt->stat.bind_count++;
	return;

bailout:
	pmap_map_free(map);
bailout_nofree:
	pmap_wake_portmap_waiters(xprt);
}

#ifdef CONFIG_ROOT_NFS
/**
 * rpc_getport_external - obtain the port for a given RPC service on a given host
 * @sin: address of remote peer
 * @prog: RPC program number to bind
 * @vers: RPC version number to bind
 * @prot: transport protocol to use to make this request
 *
 * This one is called from outside the RPC client in a synchronous task context.
 */
int rpc_getport_external(struct sockaddr_in *sin, __u32 prog, __u32 vers, int prot)
{
	struct portmap_args map = {
		.pm_prog	= prog,
		.pm_vers	= vers,
		.pm_prot	= prot,
		.pm_port	= 0
	};
	struct rpc_message msg = {
		.rpc_proc	= &pmap_procedures[PMAP_GETPORT],
		.rpc_argp	= &map,
		.rpc_resp	= &map.pm_port,
	};
	struct rpc_clnt	*pmap_clnt;
	char		hostname[32];
	int		status;

	dprintk("RPC:      rpc_getport_external(%u.%u.%u.%u, %u, %u, %d)\n",
			NIPQUAD(sin->sin_addr.s_addr), prog, vers, prot);

	sprintf(hostname, "%u.%u.%u.%u", NIPQUAD(sin->sin_addr.s_addr));
	pmap_clnt = pmap_create(hostname, sin, prot, 0);
	if (IS_ERR(pmap_clnt))
		return PTR_ERR(pmap_clnt);

	/* Setup the call info struct */
	status = rpc_call_sync(pmap_clnt, &msg, 0);

	if (status >= 0) {
		if (map.pm_port != 0)
			return map.pm_port;
		status = -EACCES;
	}
	return status;
}
#endif

/*
 * Portmapper child task invokes this callback via tk_exit.
 */
static void pmap_getport_done(struct rpc_task *child, void *data)
{
	struct portmap_args *map = data;
	struct rpc_task *task = map->pm_task;
	struct rpc_xprt *xprt = task->tk_xprt;
	int status = child->tk_status;

	if (status < 0) {
		/* Portmapper not available */
		xprt->ops->set_port(xprt, 0);
		task->tk_status = status;
	} else if (map->pm_port == 0) {
		/* Requested RPC service wasn't registered */
		xprt->ops->set_port(xprt, 0);
		task->tk_status = -EACCES;
	} else {
		/* Succeeded */
		xprt->ops->set_port(xprt, map->pm_port);
		xprt_set_bound(xprt);
		task->tk_status = 0;
	}

	dprintk("RPC: %4d pmap_getport_done(status %d, port %u)\n",
			child->tk_pid, child->tk_status, map->pm_port);

	pmap_wake_portmap_waiters(xprt);
}

/**
 * rpc_register - set or unset a port registration with the local portmapper
 * @prog: RPC program number to bind
 * @vers: RPC version number to bind
 * @prot: transport protocol to use to make this request
 * @port: port value to register
 * @okay: result code
 *
 * port == 0 means unregister, port != 0 means register.
 */
int rpc_register(u32 prog, u32 vers, int prot, unsigned short port, int *okay)
{
	struct sockaddr_in	sin = {
		.sin_family	= AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct portmap_args	map = {
		.pm_prog	= prog,
		.pm_vers	= vers,
		.pm_prot	= prot,
		.pm_port	= port,
	};
	struct rpc_message msg = {
		.rpc_proc	= &pmap_procedures[port ? PMAP_SET : PMAP_UNSET],
		.rpc_argp	= &map,
		.rpc_resp	= okay,
	};
	struct rpc_clnt		*pmap_clnt;
	int error = 0;

	dprintk("RPC: registering (%u, %u, %d, %u) with portmapper.\n",
			prog, vers, prot, port);

	pmap_clnt = pmap_create("localhost", &sin, IPPROTO_UDP, 1);
	if (IS_ERR(pmap_clnt)) {
		error = PTR_ERR(pmap_clnt);
		dprintk("RPC: couldn't create pmap client. Error = %d\n", error);
		return error;
	}

	error = rpc_call_sync(pmap_clnt, &msg, 0);

	if (error < 0) {
		printk(KERN_WARNING
			"RPC: failed to contact portmap (errno %d).\n",
			error);
	}
	dprintk("RPC: registration status %d/%d\n", error, *okay);

	/* Client deleted automatically because cl_oneshot == 1 */
	return error;
}

static struct rpc_clnt *pmap_create(char *hostname, struct sockaddr_in *srvaddr, int proto, int privileged)
{
	struct rpc_xprt	*xprt;
	struct rpc_clnt	*clnt;

	xprt = xprt_create_proto(proto, srvaddr, NULL);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;
	xprt->ops->set_port(xprt, RPC_PMAP_PORT);
	xprt_set_bound(xprt);
	if (!privileged)
		xprt->resvport = 0;

	clnt = rpc_new_client(xprt, hostname,
				&pmap_program, RPC_PMAP_VERSION,
				RPC_AUTH_UNIX);
	if (!IS_ERR(clnt)) {
		clnt->cl_softrtry = 1;
		clnt->cl_oneshot  = 1;
	}
	return clnt;
}

/*
 * XDR encode/decode functions for PMAP
 */
static int xdr_encode_mapping(struct rpc_rqst *req, u32 *p, struct portmap_args *map)
{
	dprintk("RPC: xdr_encode_mapping(%u, %u, %u, %u)\n",
		map->pm_prog, map->pm_vers, map->pm_prot, map->pm_port);
	*p++ = htonl(map->pm_prog);
	*p++ = htonl(map->pm_vers);
	*p++ = htonl(map->pm_prot);
	*p++ = htonl(map->pm_port);

	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int xdr_decode_port(struct rpc_rqst *req, u32 *p, unsigned short *portp)
{
	*portp = (unsigned short) ntohl(*p++);
	return 0;
}

static int xdr_decode_bool(struct rpc_rqst *req, u32 *p, unsigned int *boolp)
{
	*boolp = (unsigned int) ntohl(*p++);
	return 0;
}

static struct rpc_procinfo	pmap_procedures[] = {
[PMAP_SET] = {
	  .p_proc		= PMAP_SET,
	  .p_encode		= (kxdrproc_t) xdr_encode_mapping,	
	  .p_decode		= (kxdrproc_t) xdr_decode_bool,
	  .p_bufsiz		= 4,
	  .p_count		= 1,
	  .p_statidx		= PMAP_SET,
	  .p_name		= "SET",
	},
[PMAP_UNSET] = {
	  .p_proc		= PMAP_UNSET,
	  .p_encode		= (kxdrproc_t) xdr_encode_mapping,	
	  .p_decode		= (kxdrproc_t) xdr_decode_bool,
	  .p_bufsiz		= 4,
	  .p_count		= 1,
	  .p_statidx		= PMAP_UNSET,
	  .p_name		= "UNSET",
	},
[PMAP_GETPORT] = {
	  .p_proc		= PMAP_GETPORT,
	  .p_encode		= (kxdrproc_t) xdr_encode_mapping,
	  .p_decode		= (kxdrproc_t) xdr_decode_port,
	  .p_bufsiz		= 4,
	  .p_count		= 1,
	  .p_statidx		= PMAP_GETPORT,
	  .p_name		= "GETPORT",
	},
};

static struct rpc_version	pmap_version2 = {
	.number		= 2,
	.nrprocs	= 4,
	.procs		= pmap_procedures
};

static struct rpc_version *	pmap_version[] = {
	NULL,
	NULL,
	&pmap_version2
};

static struct rpc_stat		pmap_stats;

static struct rpc_program	pmap_program = {
	.name		= "portmap",
	.number		= RPC_PMAP_PROGRAM,
	.nrvers		= ARRAY_SIZE(pmap_version),
	.version	= pmap_version,
	.stats		= &pmap_stats,
};
