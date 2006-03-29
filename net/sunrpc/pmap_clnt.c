/*
 * linux/net/sunrpc/pmap.c
 *
 * Portmapper client.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/in.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_PMAP
#endif

#define PMAP_SET		1
#define PMAP_UNSET		2
#define PMAP_GETPORT		3

static struct rpc_procinfo	pmap_procedures[];
static struct rpc_clnt *	pmap_create(char *, struct sockaddr_in *, int, int);
static void			pmap_getport_done(struct rpc_task *);
static struct rpc_program	pmap_program;
static DEFINE_SPINLOCK(pmap_lock);

/*
 * Obtain the port for a given RPC service on a given host. This one can
 * be called for an ongoing RPC request.
 */
void
rpc_getport(struct rpc_task *task, struct rpc_clnt *clnt)
{
	struct rpc_portmap *map = clnt->cl_pmap;
	struct sockaddr_in *sap = &clnt->cl_xprt->addr;
	struct rpc_message msg = {
		.rpc_proc	= &pmap_procedures[PMAP_GETPORT],
		.rpc_argp	= map,
		.rpc_resp	= &clnt->cl_port,
		.rpc_cred	= NULL
	};
	struct rpc_clnt	*pmap_clnt;
	struct rpc_task	*child;

	dprintk("RPC: %4d rpc_getport(%s, %d, %d, %d)\n",
			task->tk_pid, clnt->cl_server,
			map->pm_prog, map->pm_vers, map->pm_prot);

	/* Autobind on cloned rpc clients is discouraged */
	BUG_ON(clnt->cl_parent != clnt);

	spin_lock(&pmap_lock);
	if (map->pm_binding) {
		rpc_sleep_on(&map->pm_bindwait, task, NULL, NULL);
		spin_unlock(&pmap_lock);
		return;
	}
	map->pm_binding = 1;
	spin_unlock(&pmap_lock);

	pmap_clnt = pmap_create(clnt->cl_server, sap, map->pm_prot, 0);
	if (IS_ERR(pmap_clnt)) {
		task->tk_status = PTR_ERR(pmap_clnt);
		goto bailout;
	}
	task->tk_status = 0;

	/*
	 * Note: rpc_new_child will release client after a failure.
	 */
	if (!(child = rpc_new_child(pmap_clnt, task)))
		goto bailout;

	/* Setup the call info struct */
	rpc_call_setup(child, &msg, 0);

	/* ... and run the child task */
	task->tk_xprt->stat.bind_count++;
	rpc_run_child(task, child, pmap_getport_done);
	return;

bailout:
	spin_lock(&pmap_lock);
	map->pm_binding = 0;
	rpc_wake_up(&map->pm_bindwait);
	spin_unlock(&pmap_lock);
	rpc_exit(task, -EIO);
}

#ifdef CONFIG_ROOT_NFS
int
rpc_getport_external(struct sockaddr_in *sin, __u32 prog, __u32 vers, int prot)
{
	struct rpc_portmap map = {
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

	dprintk("RPC:      rpc_getport_external(%u.%u.%u.%u, %d, %d, %d)\n",
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

static void
pmap_getport_done(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_xprt *xprt = task->tk_xprt;
	struct rpc_portmap *map = clnt->cl_pmap;

	dprintk("RPC: %4d pmap_getport_done(status %d, port %d)\n",
			task->tk_pid, task->tk_status, clnt->cl_port);

	xprt->ops->set_port(xprt, 0);
	if (task->tk_status < 0) {
		/* Make the calling task exit with an error */
		task->tk_action = rpc_exit_task;
	} else if (clnt->cl_port == 0) {
		/* Program not registered */
		rpc_exit(task, -EACCES);
	} else {
		xprt->ops->set_port(xprt, clnt->cl_port);
		clnt->cl_port = htons(clnt->cl_port);
	}
	spin_lock(&pmap_lock);
	map->pm_binding = 0;
	rpc_wake_up(&map->pm_bindwait);
	spin_unlock(&pmap_lock);
}

/*
 * Set or unset a port registration with the local portmapper.
 * port == 0 means unregister, port != 0 means register.
 */
int
rpc_register(u32 prog, u32 vers, int prot, unsigned short port, int *okay)
{
	struct sockaddr_in	sin = {
		.sin_family	= AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct rpc_portmap	map = {
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

	dprintk("RPC: registering (%d, %d, %d, %d) with portmapper.\n",
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

static struct rpc_clnt *
pmap_create(char *hostname, struct sockaddr_in *srvaddr, int proto, int privileged)
{
	struct rpc_xprt	*xprt;
	struct rpc_clnt	*clnt;

	/* printk("pmap: create xprt\n"); */
	xprt = xprt_create_proto(proto, srvaddr, NULL);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;
	xprt->ops->set_port(xprt, RPC_PMAP_PORT);
	if (!privileged)
		xprt->resvport = 0;

	/* printk("pmap: create clnt\n"); */
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
static int
xdr_encode_mapping(struct rpc_rqst *req, u32 *p, struct rpc_portmap *map)
{
	dprintk("RPC: xdr_encode_mapping(%d, %d, %d, %d)\n",
		map->pm_prog, map->pm_vers, map->pm_prot, map->pm_port);
	*p++ = htonl(map->pm_prog);
	*p++ = htonl(map->pm_vers);
	*p++ = htonl(map->pm_prot);
	*p++ = htonl(map->pm_port);

	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
xdr_decode_port(struct rpc_rqst *req, u32 *p, unsigned short *portp)
{
	*portp = (unsigned short) ntohl(*p++);
	return 0;
}

static int
xdr_decode_bool(struct rpc_rqst *req, u32 *p, unsigned int *boolp)
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
