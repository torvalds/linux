// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/net/sunrpc/clnt.c
 *
 *  This file contains the high-level RPC interface.
 *  It is modeled as a finite state machine to support both synchronous
 *  and asynchronous requests.
 *
 *  -	RPC header generation and argument serialization.
 *  -	Credential refresh.
 *  -	TCP connect handling.
 *  -	Retry of operation when it is suspected the operation failed because
 *	of uid squashing on the server, or when the credentials were stale
 *	and need to be refreshed, or when a packet was damaged in transit.
 *	This may be have to be moved to the VFS layer.
 *
 *  Copyright (C) 1992,1993 Rick Sladkey <jrs@world.std.com>
 *  Copyright (C) 1995,1996 Olaf Kirch <okir@monad.swb.de>
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/utsname.h>
#include <linux/workqueue.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/un.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/bc_xprt.h>
#include <trace/events/sunrpc.h>

#include "sunrpc.h"
#include "netns.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_CALL
#endif

#define dprint_status(t)					\
	dprintk("RPC: %5u %s (status %d)\n", t->tk_pid,		\
			__func__, t->tk_status)

/*
 * All RPC clients are linked into this list
 */

static DECLARE_WAIT_QUEUE_HEAD(destroy_wait);


static void	call_start(struct rpc_task *task);
static void	call_reserve(struct rpc_task *task);
static void	call_reserveresult(struct rpc_task *task);
static void	call_allocate(struct rpc_task *task);
static void	call_encode(struct rpc_task *task);
static void	call_decode(struct rpc_task *task);
static void	call_bind(struct rpc_task *task);
static void	call_bind_status(struct rpc_task *task);
static void	call_transmit(struct rpc_task *task);
static void	call_status(struct rpc_task *task);
static void	call_transmit_status(struct rpc_task *task);
static void	call_refresh(struct rpc_task *task);
static void	call_refreshresult(struct rpc_task *task);
static void	call_connect(struct rpc_task *task);
static void	call_connect_status(struct rpc_task *task);

static int	rpc_encode_header(struct rpc_task *task,
				  struct xdr_stream *xdr);
static int	rpc_decode_header(struct rpc_task *task,
				  struct xdr_stream *xdr);
static int	rpc_ping(struct rpc_clnt *clnt);
static void	rpc_check_timeout(struct rpc_task *task);

static void rpc_register_client(struct rpc_clnt *clnt)
{
	struct net *net = rpc_net_ns(clnt);
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	spin_lock(&sn->rpc_client_lock);
	list_add(&clnt->cl_clients, &sn->all_clients);
	spin_unlock(&sn->rpc_client_lock);
}

static void rpc_unregister_client(struct rpc_clnt *clnt)
{
	struct net *net = rpc_net_ns(clnt);
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	spin_lock(&sn->rpc_client_lock);
	list_del(&clnt->cl_clients);
	spin_unlock(&sn->rpc_client_lock);
}

static void __rpc_clnt_remove_pipedir(struct rpc_clnt *clnt)
{
	rpc_remove_client_dir(clnt);
}

static void rpc_clnt_remove_pipedir(struct rpc_clnt *clnt)
{
	struct net *net = rpc_net_ns(clnt);
	struct super_block *pipefs_sb;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		__rpc_clnt_remove_pipedir(clnt);
		rpc_put_sb_net(net);
	}
}

static struct dentry *rpc_setup_pipedir_sb(struct super_block *sb,
				    struct rpc_clnt *clnt)
{
	static uint32_t clntid;
	const char *dir_name = clnt->cl_program->pipe_dir_name;
	char name[15];
	struct dentry *dir, *dentry;

	dir = rpc_d_lookup_sb(sb, dir_name);
	if (dir == NULL) {
		pr_info("RPC: pipefs directory doesn't exist: %s\n", dir_name);
		return dir;
	}
	for (;;) {
		snprintf(name, sizeof(name), "clnt%x", (unsigned int)clntid++);
		name[sizeof(name) - 1] = '\0';
		dentry = rpc_create_client_dir(dir, name, clnt);
		if (!IS_ERR(dentry))
			break;
		if (dentry == ERR_PTR(-EEXIST))
			continue;
		printk(KERN_INFO "RPC: Couldn't create pipefs entry"
				" %s/%s, error %ld\n",
				dir_name, name, PTR_ERR(dentry));
		break;
	}
	dput(dir);
	return dentry;
}

static int
rpc_setup_pipedir(struct super_block *pipefs_sb, struct rpc_clnt *clnt)
{
	struct dentry *dentry;

	if (clnt->cl_program->pipe_dir_name != NULL) {
		dentry = rpc_setup_pipedir_sb(pipefs_sb, clnt);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}
	return 0;
}

static int rpc_clnt_skip_event(struct rpc_clnt *clnt, unsigned long event)
{
	if (clnt->cl_program->pipe_dir_name == NULL)
		return 1;

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		if (clnt->cl_pipedir_objects.pdh_dentry != NULL)
			return 1;
		if (atomic_read(&clnt->cl_count) == 0)
			return 1;
		break;
	case RPC_PIPEFS_UMOUNT:
		if (clnt->cl_pipedir_objects.pdh_dentry == NULL)
			return 1;
		break;
	}
	return 0;
}

static int __rpc_clnt_handle_event(struct rpc_clnt *clnt, unsigned long event,
				   struct super_block *sb)
{
	struct dentry *dentry;

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		dentry = rpc_setup_pipedir_sb(sb, clnt);
		if (!dentry)
			return -ENOENT;
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
		break;
	case RPC_PIPEFS_UMOUNT:
		__rpc_clnt_remove_pipedir(clnt);
		break;
	default:
		printk(KERN_ERR "%s: unknown event: %ld\n", __func__, event);
		return -ENOTSUPP;
	}
	return 0;
}

static int __rpc_pipefs_event(struct rpc_clnt *clnt, unsigned long event,
				struct super_block *sb)
{
	int error = 0;

	for (;; clnt = clnt->cl_parent) {
		if (!rpc_clnt_skip_event(clnt, event))
			error = __rpc_clnt_handle_event(clnt, event, sb);
		if (error || clnt == clnt->cl_parent)
			break;
	}
	return error;
}

static struct rpc_clnt *rpc_get_client_for_event(struct net *net, int event)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct rpc_clnt *clnt;

	spin_lock(&sn->rpc_client_lock);
	list_for_each_entry(clnt, &sn->all_clients, cl_clients) {
		if (rpc_clnt_skip_event(clnt, event))
			continue;
		spin_unlock(&sn->rpc_client_lock);
		return clnt;
	}
	spin_unlock(&sn->rpc_client_lock);
	return NULL;
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct super_block *sb = ptr;
	struct rpc_clnt *clnt;
	int error = 0;

	while ((clnt = rpc_get_client_for_event(sb->s_fs_info, event))) {
		error = __rpc_pipefs_event(clnt, event, sb);
		if (error)
			break;
	}
	return error;
}

static struct notifier_block rpc_clients_block = {
	.notifier_call	= rpc_pipefs_event,
	.priority	= SUNRPC_PIPEFS_RPC_PRIO,
};

int rpc_clients_notifier_register(void)
{
	return rpc_pipefs_notifier_register(&rpc_clients_block);
}

void rpc_clients_notifier_unregister(void)
{
	return rpc_pipefs_notifier_unregister(&rpc_clients_block);
}

static struct rpc_xprt *rpc_clnt_set_transport(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		const struct rpc_timeout *timeout)
{
	struct rpc_xprt *old;

	spin_lock(&clnt->cl_lock);
	old = rcu_dereference_protected(clnt->cl_xprt,
			lockdep_is_held(&clnt->cl_lock));

	if (!xprt_bound(xprt))
		clnt->cl_autobind = 1;

	clnt->cl_timeout = timeout;
	rcu_assign_pointer(clnt->cl_xprt, xprt);
	spin_unlock(&clnt->cl_lock);

	return old;
}

static void rpc_clnt_set_nodename(struct rpc_clnt *clnt, const char *nodename)
{
	clnt->cl_nodelen = strlcpy(clnt->cl_nodename,
			nodename, sizeof(clnt->cl_nodename));
}

static int rpc_client_register(struct rpc_clnt *clnt,
			       rpc_authflavor_t pseudoflavor,
			       const char *client_name)
{
	struct rpc_auth_create_args auth_args = {
		.pseudoflavor = pseudoflavor,
		.target_name = client_name,
	};
	struct rpc_auth *auth;
	struct net *net = rpc_net_ns(clnt);
	struct super_block *pipefs_sb;
	int err;

	rpc_clnt_debugfs_register(clnt);

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		err = rpc_setup_pipedir(pipefs_sb, clnt);
		if (err)
			goto out;
	}

	rpc_register_client(clnt);
	if (pipefs_sb)
		rpc_put_sb_net(net);

	auth = rpcauth_create(&auth_args, clnt);
	if (IS_ERR(auth)) {
		dprintk("RPC:       Couldn't create auth handle (flavor %u)\n",
				pseudoflavor);
		err = PTR_ERR(auth);
		goto err_auth;
	}
	return 0;
err_auth:
	pipefs_sb = rpc_get_sb_net(net);
	rpc_unregister_client(clnt);
	__rpc_clnt_remove_pipedir(clnt);
out:
	if (pipefs_sb)
		rpc_put_sb_net(net);
	rpc_clnt_debugfs_unregister(clnt);
	return err;
}

static DEFINE_IDA(rpc_clids);

void rpc_cleanup_clids(void)
{
	ida_destroy(&rpc_clids);
}

static int rpc_alloc_clid(struct rpc_clnt *clnt)
{
	int clid;

	clid = ida_simple_get(&rpc_clids, 0, 0, GFP_KERNEL);
	if (clid < 0)
		return clid;
	clnt->cl_clid = clid;
	return 0;
}

static void rpc_free_clid(struct rpc_clnt *clnt)
{
	ida_simple_remove(&rpc_clids, clnt->cl_clid);
}

static struct rpc_clnt * rpc_new_client(const struct rpc_create_args *args,
		struct rpc_xprt_switch *xps,
		struct rpc_xprt *xprt,
		struct rpc_clnt *parent)
{
	const struct rpc_program *program = args->program;
	const struct rpc_version *version;
	struct rpc_clnt *clnt = NULL;
	const struct rpc_timeout *timeout;
	const char *nodename = args->nodename;
	int err;

	/* sanity check the name before trying to print it */
	dprintk("RPC:       creating %s client for %s (xprt %p)\n",
			program->name, args->servername, xprt);

	err = rpciod_up();
	if (err)
		goto out_no_rpciod;

	err = -EINVAL;
	if (args->version >= program->nrvers)
		goto out_err;
	version = program->version[args->version];
	if (version == NULL)
		goto out_err;

	err = -ENOMEM;
	clnt = kzalloc(sizeof(*clnt), GFP_KERNEL);
	if (!clnt)
		goto out_err;
	clnt->cl_parent = parent ? : clnt;

	err = rpc_alloc_clid(clnt);
	if (err)
		goto out_no_clid;

	clnt->cl_cred	  = get_cred(args->cred);
	clnt->cl_procinfo = version->procs;
	clnt->cl_maxproc  = version->nrprocs;
	clnt->cl_prog     = args->prognumber ? : program->number;
	clnt->cl_vers     = version->number;
	clnt->cl_stats    = program->stats;
	clnt->cl_metrics  = rpc_alloc_iostats(clnt);
	rpc_init_pipe_dir_head(&clnt->cl_pipedir_objects);
	err = -ENOMEM;
	if (clnt->cl_metrics == NULL)
		goto out_no_stats;
	clnt->cl_program  = program;
	INIT_LIST_HEAD(&clnt->cl_tasks);
	spin_lock_init(&clnt->cl_lock);

	timeout = xprt->timeout;
	if (args->timeout != NULL) {
		memcpy(&clnt->cl_timeout_default, args->timeout,
				sizeof(clnt->cl_timeout_default));
		timeout = &clnt->cl_timeout_default;
	}

	rpc_clnt_set_transport(clnt, xprt, timeout);
	xprt_iter_init(&clnt->cl_xpi, xps);
	xprt_switch_put(xps);

	clnt->cl_rtt = &clnt->cl_rtt_default;
	rpc_init_rtt(&clnt->cl_rtt_default, clnt->cl_timeout->to_initval);

	atomic_set(&clnt->cl_count, 1);

	if (nodename == NULL)
		nodename = utsname()->nodename;
	/* save the nodename */
	rpc_clnt_set_nodename(clnt, nodename);

	err = rpc_client_register(clnt, args->authflavor, args->client_name);
	if (err)
		goto out_no_path;
	if (parent)
		atomic_inc(&parent->cl_count);
	return clnt;

out_no_path:
	rpc_free_iostats(clnt->cl_metrics);
out_no_stats:
	put_cred(clnt->cl_cred);
	rpc_free_clid(clnt);
out_no_clid:
	kfree(clnt);
out_err:
	rpciod_down();
out_no_rpciod:
	xprt_switch_put(xps);
	xprt_put(xprt);
	return ERR_PTR(err);
}

static struct rpc_clnt *rpc_create_xprt(struct rpc_create_args *args,
					struct rpc_xprt *xprt)
{
	struct rpc_clnt *clnt = NULL;
	struct rpc_xprt_switch *xps;

	if (args->bc_xprt && args->bc_xprt->xpt_bc_xps) {
		WARN_ON_ONCE(!(args->protocol & XPRT_TRANSPORT_BC));
		xps = args->bc_xprt->xpt_bc_xps;
		xprt_switch_get(xps);
	} else {
		xps = xprt_switch_alloc(xprt, GFP_KERNEL);
		if (xps == NULL) {
			xprt_put(xprt);
			return ERR_PTR(-ENOMEM);
		}
		if (xprt->bc_xprt) {
			xprt_switch_get(xps);
			xprt->bc_xprt->xpt_bc_xps = xps;
		}
	}
	clnt = rpc_new_client(args, xps, xprt, NULL);
	if (IS_ERR(clnt))
		return clnt;

	if (!(args->flags & RPC_CLNT_CREATE_NOPING)) {
		int err = rpc_ping(clnt);
		if (err != 0) {
			rpc_shutdown_client(clnt);
			return ERR_PTR(err);
		}
	}

	clnt->cl_softrtry = 1;
	if (args->flags & (RPC_CLNT_CREATE_HARDRTRY|RPC_CLNT_CREATE_SOFTERR)) {
		clnt->cl_softrtry = 0;
		if (args->flags & RPC_CLNT_CREATE_SOFTERR)
			clnt->cl_softerr = 1;
	}

	if (args->flags & RPC_CLNT_CREATE_AUTOBIND)
		clnt->cl_autobind = 1;
	if (args->flags & RPC_CLNT_CREATE_NO_RETRANS_TIMEOUT)
		clnt->cl_noretranstimeo = 1;
	if (args->flags & RPC_CLNT_CREATE_DISCRTRY)
		clnt->cl_discrtry = 1;
	if (!(args->flags & RPC_CLNT_CREATE_QUIET))
		clnt->cl_chatty = 1;

	return clnt;
}

/**
 * rpc_create - create an RPC client and transport with one call
 * @args: rpc_clnt create argument structure
 *
 * Creates and initializes an RPC transport and an RPC client.
 *
 * It can ping the server in order to determine if it is up, and to see if
 * it supports this program and version.  RPC_CLNT_CREATE_NOPING disables
 * this behavior so asynchronous tasks can also use rpc_create.
 */
struct rpc_clnt *rpc_create(struct rpc_create_args *args)
{
	struct rpc_xprt *xprt;
	struct xprt_create xprtargs = {
		.net = args->net,
		.ident = args->protocol,
		.srcaddr = args->saddress,
		.dstaddr = args->address,
		.addrlen = args->addrsize,
		.servername = args->servername,
		.bc_xprt = args->bc_xprt,
	};
	char servername[48];
	struct rpc_clnt *clnt;
	int i;

	if (args->bc_xprt) {
		WARN_ON_ONCE(!(args->protocol & XPRT_TRANSPORT_BC));
		xprt = args->bc_xprt->xpt_bc_xprt;
		if (xprt) {
			xprt_get(xprt);
			return rpc_create_xprt(args, xprt);
		}
	}

	if (args->flags & RPC_CLNT_CREATE_INFINITE_SLOTS)
		xprtargs.flags |= XPRT_CREATE_INFINITE_SLOTS;
	if (args->flags & RPC_CLNT_CREATE_NO_IDLE_TIMEOUT)
		xprtargs.flags |= XPRT_CREATE_NO_IDLE_TIMEOUT;
	/*
	 * If the caller chooses not to specify a hostname, whip
	 * up a string representation of the passed-in address.
	 */
	if (xprtargs.servername == NULL) {
		struct sockaddr_un *sun =
				(struct sockaddr_un *)args->address;
		struct sockaddr_in *sin =
				(struct sockaddr_in *)args->address;
		struct sockaddr_in6 *sin6 =
				(struct sockaddr_in6 *)args->address;

		servername[0] = '\0';
		switch (args->address->sa_family) {
		case AF_LOCAL:
			snprintf(servername, sizeof(servername), "%s",
				 sun->sun_path);
			break;
		case AF_INET:
			snprintf(servername, sizeof(servername), "%pI4",
				 &sin->sin_addr.s_addr);
			break;
		case AF_INET6:
			snprintf(servername, sizeof(servername), "%pI6",
				 &sin6->sin6_addr);
			break;
		default:
			/* caller wants default server name, but
			 * address family isn't recognized. */
			return ERR_PTR(-EINVAL);
		}
		xprtargs.servername = servername;
	}

	xprt = xprt_create_transport(&xprtargs);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;

	/*
	 * By default, kernel RPC client connects from a reserved port.
	 * CAP_NET_BIND_SERVICE will not be set for unprivileged requesters,
	 * but it is always enabled for rpciod, which handles the connect
	 * operation.
	 */
	xprt->resvport = 1;
	if (args->flags & RPC_CLNT_CREATE_NONPRIVPORT)
		xprt->resvport = 0;
	xprt->reuseport = 0;
	if (args->flags & RPC_CLNT_CREATE_REUSEPORT)
		xprt->reuseport = 1;

	clnt = rpc_create_xprt(args, xprt);
	if (IS_ERR(clnt) || args->nconnect <= 1)
		return clnt;

	for (i = 0; i < args->nconnect - 1; i++) {
		if (rpc_clnt_add_xprt(clnt, &xprtargs, NULL, NULL) < 0)
			break;
	}
	return clnt;
}
EXPORT_SYMBOL_GPL(rpc_create);

/*
 * This function clones the RPC client structure. It allows us to share the
 * same transport while varying parameters such as the authentication
 * flavour.
 */
static struct rpc_clnt *__rpc_clone_client(struct rpc_create_args *args,
					   struct rpc_clnt *clnt)
{
	struct rpc_xprt_switch *xps;
	struct rpc_xprt *xprt;
	struct rpc_clnt *new;
	int err;

	err = -ENOMEM;
	rcu_read_lock();
	xprt = xprt_get(rcu_dereference(clnt->cl_xprt));
	xps = xprt_switch_get(rcu_dereference(clnt->cl_xpi.xpi_xpswitch));
	rcu_read_unlock();
	if (xprt == NULL || xps == NULL) {
		xprt_put(xprt);
		xprt_switch_put(xps);
		goto out_err;
	}
	args->servername = xprt->servername;
	args->nodename = clnt->cl_nodename;

	new = rpc_new_client(args, xps, xprt, clnt);
	if (IS_ERR(new)) {
		err = PTR_ERR(new);
		goto out_err;
	}

	/* Turn off autobind on clones */
	new->cl_autobind = 0;
	new->cl_softrtry = clnt->cl_softrtry;
	new->cl_softerr = clnt->cl_softerr;
	new->cl_noretranstimeo = clnt->cl_noretranstimeo;
	new->cl_discrtry = clnt->cl_discrtry;
	new->cl_chatty = clnt->cl_chatty;
	new->cl_principal = clnt->cl_principal;
	return new;

out_err:
	dprintk("RPC:       %s: returned error %d\n", __func__, err);
	return ERR_PTR(err);
}

/**
 * rpc_clone_client - Clone an RPC client structure
 *
 * @clnt: RPC client whose parameters are copied
 *
 * Returns a fresh RPC client or an ERR_PTR.
 */
struct rpc_clnt *rpc_clone_client(struct rpc_clnt *clnt)
{
	struct rpc_create_args args = {
		.program	= clnt->cl_program,
		.prognumber	= clnt->cl_prog,
		.version	= clnt->cl_vers,
		.authflavor	= clnt->cl_auth->au_flavor,
		.cred		= clnt->cl_cred,
	};
	return __rpc_clone_client(&args, clnt);
}
EXPORT_SYMBOL_GPL(rpc_clone_client);

/**
 * rpc_clone_client_set_auth - Clone an RPC client structure and set its auth
 *
 * @clnt: RPC client whose parameters are copied
 * @flavor: security flavor for new client
 *
 * Returns a fresh RPC client or an ERR_PTR.
 */
struct rpc_clnt *
rpc_clone_client_set_auth(struct rpc_clnt *clnt, rpc_authflavor_t flavor)
{
	struct rpc_create_args args = {
		.program	= clnt->cl_program,
		.prognumber	= clnt->cl_prog,
		.version	= clnt->cl_vers,
		.authflavor	= flavor,
		.cred		= clnt->cl_cred,
	};
	return __rpc_clone_client(&args, clnt);
}
EXPORT_SYMBOL_GPL(rpc_clone_client_set_auth);

/**
 * rpc_switch_client_transport: switch the RPC transport on the fly
 * @clnt: pointer to a struct rpc_clnt
 * @args: pointer to the new transport arguments
 * @timeout: pointer to the new timeout parameters
 *
 * This function allows the caller to switch the RPC transport for the
 * rpc_clnt structure 'clnt' to allow it to connect to a mirrored NFS
 * server, for instance.  It assumes that the caller has ensured that
 * there are no active RPC tasks by using some form of locking.
 *
 * Returns zero if "clnt" is now using the new xprt.  Otherwise a
 * negative errno is returned, and "clnt" continues to use the old
 * xprt.
 */
int rpc_switch_client_transport(struct rpc_clnt *clnt,
		struct xprt_create *args,
		const struct rpc_timeout *timeout)
{
	const struct rpc_timeout *old_timeo;
	rpc_authflavor_t pseudoflavor;
	struct rpc_xprt_switch *xps, *oldxps;
	struct rpc_xprt *xprt, *old;
	struct rpc_clnt *parent;
	int err;

	xprt = xprt_create_transport(args);
	if (IS_ERR(xprt)) {
		dprintk("RPC:       failed to create new xprt for clnt %p\n",
			clnt);
		return PTR_ERR(xprt);
	}

	xps = xprt_switch_alloc(xprt, GFP_KERNEL);
	if (xps == NULL) {
		xprt_put(xprt);
		return -ENOMEM;
	}

	pseudoflavor = clnt->cl_auth->au_flavor;

	old_timeo = clnt->cl_timeout;
	old = rpc_clnt_set_transport(clnt, xprt, timeout);
	oldxps = xprt_iter_xchg_switch(&clnt->cl_xpi, xps);

	rpc_unregister_client(clnt);
	__rpc_clnt_remove_pipedir(clnt);
	rpc_clnt_debugfs_unregister(clnt);

	/*
	 * A new transport was created.  "clnt" therefore
	 * becomes the root of a new cl_parent tree.  clnt's
	 * children, if it has any, still point to the old xprt.
	 */
	parent = clnt->cl_parent;
	clnt->cl_parent = clnt;

	/*
	 * The old rpc_auth cache cannot be re-used.  GSS
	 * contexts in particular are between a single
	 * client and server.
	 */
	err = rpc_client_register(clnt, pseudoflavor, NULL);
	if (err)
		goto out_revert;

	synchronize_rcu();
	if (parent != clnt)
		rpc_release_client(parent);
	xprt_switch_put(oldxps);
	xprt_put(old);
	dprintk("RPC:       replaced xprt for clnt %p\n", clnt);
	return 0;

out_revert:
	xps = xprt_iter_xchg_switch(&clnt->cl_xpi, oldxps);
	rpc_clnt_set_transport(clnt, old, old_timeo);
	clnt->cl_parent = parent;
	rpc_client_register(clnt, pseudoflavor, NULL);
	xprt_switch_put(xps);
	xprt_put(xprt);
	dprintk("RPC:       failed to switch xprt for clnt %p\n", clnt);
	return err;
}
EXPORT_SYMBOL_GPL(rpc_switch_client_transport);

static
int rpc_clnt_xprt_iter_init(struct rpc_clnt *clnt, struct rpc_xprt_iter *xpi)
{
	struct rpc_xprt_switch *xps;

	rcu_read_lock();
	xps = xprt_switch_get(rcu_dereference(clnt->cl_xpi.xpi_xpswitch));
	rcu_read_unlock();
	if (xps == NULL)
		return -EAGAIN;
	xprt_iter_init_listall(xpi, xps);
	xprt_switch_put(xps);
	return 0;
}

/**
 * rpc_clnt_iterate_for_each_xprt - Apply a function to all transports
 * @clnt: pointer to client
 * @fn: function to apply
 * @data: void pointer to function data
 *
 * Iterates through the list of RPC transports currently attached to the
 * client and applies the function fn(clnt, xprt, data).
 *
 * On error, the iteration stops, and the function returns the error value.
 */
int rpc_clnt_iterate_for_each_xprt(struct rpc_clnt *clnt,
		int (*fn)(struct rpc_clnt *, struct rpc_xprt *, void *),
		void *data)
{
	struct rpc_xprt_iter xpi;
	int ret;

	ret = rpc_clnt_xprt_iter_init(clnt, &xpi);
	if (ret)
		return ret;
	for (;;) {
		struct rpc_xprt *xprt = xprt_iter_get_next(&xpi);

		if (!xprt)
			break;
		ret = fn(clnt, xprt, data);
		xprt_put(xprt);
		if (ret < 0)
			break;
	}
	xprt_iter_destroy(&xpi);
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_clnt_iterate_for_each_xprt);

/*
 * Kill all tasks for the given client.
 * XXX: kill their descendants as well?
 */
void rpc_killall_tasks(struct rpc_clnt *clnt)
{
	struct rpc_task	*rovr;


	if (list_empty(&clnt->cl_tasks))
		return;
	dprintk("RPC:       killing all tasks for client %p\n", clnt);
	/*
	 * Spin lock all_tasks to prevent changes...
	 */
	spin_lock(&clnt->cl_lock);
	list_for_each_entry(rovr, &clnt->cl_tasks, tk_task)
		rpc_signal_task(rovr);
	spin_unlock(&clnt->cl_lock);
}
EXPORT_SYMBOL_GPL(rpc_killall_tasks);

/*
 * Properly shut down an RPC client, terminating all outstanding
 * requests.
 */
void rpc_shutdown_client(struct rpc_clnt *clnt)
{
	might_sleep();

	dprintk_rcu("RPC:       shutting down %s client for %s\n",
			clnt->cl_program->name,
			rcu_dereference(clnt->cl_xprt)->servername);

	while (!list_empty(&clnt->cl_tasks)) {
		rpc_killall_tasks(clnt);
		wait_event_timeout(destroy_wait,
			list_empty(&clnt->cl_tasks), 1*HZ);
	}

	rpc_release_client(clnt);
}
EXPORT_SYMBOL_GPL(rpc_shutdown_client);

/*
 * Free an RPC client
 */
static struct rpc_clnt *
rpc_free_client(struct rpc_clnt *clnt)
{
	struct rpc_clnt *parent = NULL;

	dprintk_rcu("RPC:       destroying %s client for %s\n",
			clnt->cl_program->name,
			rcu_dereference(clnt->cl_xprt)->servername);
	if (clnt->cl_parent != clnt)
		parent = clnt->cl_parent;
	rpc_clnt_debugfs_unregister(clnt);
	rpc_clnt_remove_pipedir(clnt);
	rpc_unregister_client(clnt);
	rpc_free_iostats(clnt->cl_metrics);
	clnt->cl_metrics = NULL;
	xprt_put(rcu_dereference_raw(clnt->cl_xprt));
	xprt_iter_destroy(&clnt->cl_xpi);
	rpciod_down();
	put_cred(clnt->cl_cred);
	rpc_free_clid(clnt);
	kfree(clnt);
	return parent;
}

/*
 * Free an RPC client
 */
static struct rpc_clnt *
rpc_free_auth(struct rpc_clnt *clnt)
{
	if (clnt->cl_auth == NULL)
		return rpc_free_client(clnt);

	/*
	 * Note: RPCSEC_GSS may need to send NULL RPC calls in order to
	 *       release remaining GSS contexts. This mechanism ensures
	 *       that it can do so safely.
	 */
	atomic_inc(&clnt->cl_count);
	rpcauth_release(clnt->cl_auth);
	clnt->cl_auth = NULL;
	if (atomic_dec_and_test(&clnt->cl_count))
		return rpc_free_client(clnt);
	return NULL;
}

/*
 * Release reference to the RPC client
 */
void
rpc_release_client(struct rpc_clnt *clnt)
{
	dprintk("RPC:       rpc_release_client(%p)\n", clnt);

	do {
		if (list_empty(&clnt->cl_tasks))
			wake_up(&destroy_wait);
		if (!atomic_dec_and_test(&clnt->cl_count))
			break;
		clnt = rpc_free_auth(clnt);
	} while (clnt != NULL);
}
EXPORT_SYMBOL_GPL(rpc_release_client);

/**
 * rpc_bind_new_program - bind a new RPC program to an existing client
 * @old: old rpc_client
 * @program: rpc program to set
 * @vers: rpc program version
 *
 * Clones the rpc client and sets up a new RPC program. This is mainly
 * of use for enabling different RPC programs to share the same transport.
 * The Sun NFSv2/v3 ACL protocol can do this.
 */
struct rpc_clnt *rpc_bind_new_program(struct rpc_clnt *old,
				      const struct rpc_program *program,
				      u32 vers)
{
	struct rpc_create_args args = {
		.program	= program,
		.prognumber	= program->number,
		.version	= vers,
		.authflavor	= old->cl_auth->au_flavor,
		.cred		= old->cl_cred,
	};
	struct rpc_clnt *clnt;
	int err;

	clnt = __rpc_clone_client(&args, old);
	if (IS_ERR(clnt))
		goto out;
	err = rpc_ping(clnt);
	if (err != 0) {
		rpc_shutdown_client(clnt);
		clnt = ERR_PTR(err);
	}
out:
	return clnt;
}
EXPORT_SYMBOL_GPL(rpc_bind_new_program);

struct rpc_xprt *
rpc_task_get_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt)
{
	struct rpc_xprt_switch *xps;

	if (!xprt)
		return NULL;
	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	atomic_long_inc(&xps->xps_queuelen);
	rcu_read_unlock();
	atomic_long_inc(&xprt->queuelen);

	return xprt;
}

static void
rpc_task_release_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt)
{
	struct rpc_xprt_switch *xps;

	atomic_long_dec(&xprt->queuelen);
	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	atomic_long_dec(&xps->xps_queuelen);
	rcu_read_unlock();

	xprt_put(xprt);
}

void rpc_task_release_transport(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	if (xprt) {
		task->tk_xprt = NULL;
		if (task->tk_client)
			rpc_task_release_xprt(task->tk_client, xprt);
		else
			xprt_put(xprt);
	}
}
EXPORT_SYMBOL_GPL(rpc_task_release_transport);

void rpc_task_release_client(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;

	rpc_task_release_transport(task);
	if (clnt != NULL) {
		/* Remove from client task list */
		spin_lock(&clnt->cl_lock);
		list_del(&task->tk_task);
		spin_unlock(&clnt->cl_lock);
		task->tk_client = NULL;

		rpc_release_client(clnt);
	}
}

static struct rpc_xprt *
rpc_task_get_first_xprt(struct rpc_clnt *clnt)
{
	struct rpc_xprt *xprt;

	rcu_read_lock();
	xprt = xprt_get(rcu_dereference(clnt->cl_xprt));
	rcu_read_unlock();
	return rpc_task_get_xprt(clnt, xprt);
}

static struct rpc_xprt *
rpc_task_get_next_xprt(struct rpc_clnt *clnt)
{
	return rpc_task_get_xprt(clnt, xprt_iter_get_next(&clnt->cl_xpi));
}

static
void rpc_task_set_transport(struct rpc_task *task, struct rpc_clnt *clnt)
{
	if (task->tk_xprt)
		return;
	if (task->tk_flags & RPC_TASK_NO_ROUND_ROBIN)
		task->tk_xprt = rpc_task_get_first_xprt(clnt);
	else
		task->tk_xprt = rpc_task_get_next_xprt(clnt);
}

static
void rpc_task_set_client(struct rpc_task *task, struct rpc_clnt *clnt)
{

	if (clnt != NULL) {
		rpc_task_set_transport(task, clnt);
		task->tk_client = clnt;
		atomic_inc(&clnt->cl_count);
		if (clnt->cl_softrtry)
			task->tk_flags |= RPC_TASK_SOFT;
		if (clnt->cl_softerr)
			task->tk_flags |= RPC_TASK_TIMEOUT;
		if (clnt->cl_noretranstimeo)
			task->tk_flags |= RPC_TASK_NO_RETRANS_TIMEOUT;
		if (atomic_read(&clnt->cl_swapper))
			task->tk_flags |= RPC_TASK_SWAPPER;
		/* Add to the client's list of all tasks */
		spin_lock(&clnt->cl_lock);
		list_add_tail(&task->tk_task, &clnt->cl_tasks);
		spin_unlock(&clnt->cl_lock);
	}
}

static void
rpc_task_set_rpc_message(struct rpc_task *task, const struct rpc_message *msg)
{
	if (msg != NULL) {
		task->tk_msg.rpc_proc = msg->rpc_proc;
		task->tk_msg.rpc_argp = msg->rpc_argp;
		task->tk_msg.rpc_resp = msg->rpc_resp;
		task->tk_msg.rpc_cred = msg->rpc_cred;
		if (!(task->tk_flags & RPC_TASK_CRED_NOREF))
			get_cred(task->tk_msg.rpc_cred);
	}
}

/*
 * Default callback for async RPC calls
 */
static void
rpc_default_callback(struct rpc_task *task, void *data)
{
}

static const struct rpc_call_ops rpc_default_ops = {
	.rpc_call_done = rpc_default_callback,
};

/**
 * rpc_run_task - Allocate a new RPC task, then run rpc_execute against it
 * @task_setup_data: pointer to task initialisation data
 */
struct rpc_task *rpc_run_task(const struct rpc_task_setup *task_setup_data)
{
	struct rpc_task *task;

	task = rpc_new_task(task_setup_data);

	if (!RPC_IS_ASYNC(task))
		task->tk_flags |= RPC_TASK_CRED_NOREF;

	rpc_task_set_client(task, task_setup_data->rpc_client);
	rpc_task_set_rpc_message(task, task_setup_data->rpc_message);

	if (task->tk_action == NULL)
		rpc_call_start(task);

	atomic_inc(&task->tk_count);
	rpc_execute(task);
	return task;
}
EXPORT_SYMBOL_GPL(rpc_run_task);

/**
 * rpc_call_sync - Perform a synchronous RPC call
 * @clnt: pointer to RPC client
 * @msg: RPC call parameters
 * @flags: RPC call flags
 */
int rpc_call_sync(struct rpc_clnt *clnt, const struct rpc_message *msg, int flags)
{
	struct rpc_task	*task;
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_message = msg,
		.callback_ops = &rpc_default_ops,
		.flags = flags,
	};
	int status;

	WARN_ON_ONCE(flags & RPC_TASK_ASYNC);
	if (flags & RPC_TASK_ASYNC) {
		rpc_release_calldata(task_setup_data.callback_ops,
			task_setup_data.callback_data);
		return -EINVAL;
	}

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	status = task->tk_status;
	rpc_put_task(task);
	return status;
}
EXPORT_SYMBOL_GPL(rpc_call_sync);

/**
 * rpc_call_async - Perform an asynchronous RPC call
 * @clnt: pointer to RPC client
 * @msg: RPC call parameters
 * @flags: RPC call flags
 * @tk_ops: RPC call ops
 * @data: user call data
 */
int
rpc_call_async(struct rpc_clnt *clnt, const struct rpc_message *msg, int flags,
	       const struct rpc_call_ops *tk_ops, void *data)
{
	struct rpc_task	*task;
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_message = msg,
		.callback_ops = tk_ops,
		.callback_data = data,
		.flags = flags|RPC_TASK_ASYNC,
	};

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}
EXPORT_SYMBOL_GPL(rpc_call_async);

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static void call_bc_encode(struct rpc_task *task);

/**
 * rpc_run_bc_task - Allocate a new RPC task for backchannel use, then run
 * rpc_execute against it
 * @req: RPC request
 */
struct rpc_task *rpc_run_bc_task(struct rpc_rqst *req)
{
	struct rpc_task *task;
	struct rpc_task_setup task_setup_data = {
		.callback_ops = &rpc_default_ops,
		.flags = RPC_TASK_SOFTCONN |
			RPC_TASK_NO_RETRANS_TIMEOUT,
	};

	dprintk("RPC: rpc_run_bc_task req= %p\n", req);
	/*
	 * Create an rpc_task to send the data
	 */
	task = rpc_new_task(&task_setup_data);
	xprt_init_bc_request(req, task);

	task->tk_action = call_bc_encode;
	atomic_inc(&task->tk_count);
	WARN_ON_ONCE(atomic_read(&task->tk_count) != 2);
	rpc_execute(task);

	dprintk("RPC: rpc_run_bc_task: task= %p\n", task);
	return task;
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

/**
 * rpc_prepare_reply_pages - Prepare to receive a reply data payload into pages
 * @req: RPC request to prepare
 * @pages: vector of struct page pointers
 * @base: offset in first page where receive should start, in bytes
 * @len: expected size of the upper layer data payload, in bytes
 * @hdrsize: expected size of upper layer reply header, in XDR words
 *
 */
void rpc_prepare_reply_pages(struct rpc_rqst *req, struct page **pages,
			     unsigned int base, unsigned int len,
			     unsigned int hdrsize)
{
	/* Subtract one to force an extra word of buffer space for the
	 * payload's XDR pad to fall into the rcv_buf's tail iovec.
	 */
	hdrsize += RPC_REPHDRSIZE + req->rq_cred->cr_auth->au_ralign - 1;

	xdr_inline_pages(&req->rq_rcv_buf, hdrsize << 2, pages, base, len);
	trace_rpc_reply_pages(req);
}
EXPORT_SYMBOL_GPL(rpc_prepare_reply_pages);

void
rpc_call_start(struct rpc_task *task)
{
	task->tk_action = call_start;
}
EXPORT_SYMBOL_GPL(rpc_call_start);

/**
 * rpc_peeraddr - extract remote peer address from clnt's xprt
 * @clnt: RPC client structure
 * @buf: target buffer
 * @bufsize: length of target buffer
 *
 * Returns the number of bytes that are actually in the stored address.
 */
size_t rpc_peeraddr(struct rpc_clnt *clnt, struct sockaddr *buf, size_t bufsize)
{
	size_t bytes;
	struct rpc_xprt *xprt;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);

	bytes = xprt->addrlen;
	if (bytes > bufsize)
		bytes = bufsize;
	memcpy(buf, &xprt->addr, bytes);
	rcu_read_unlock();

	return bytes;
}
EXPORT_SYMBOL_GPL(rpc_peeraddr);

/**
 * rpc_peeraddr2str - return remote peer address in printable format
 * @clnt: RPC client structure
 * @format: address format
 *
 * NB: the lifetime of the memory referenced by the returned pointer is
 * the same as the rpc_xprt itself.  As long as the caller uses this
 * pointer, it must hold the RCU read lock.
 */
const char *rpc_peeraddr2str(struct rpc_clnt *clnt,
			     enum rpc_display_format_t format)
{
	struct rpc_xprt *xprt;

	xprt = rcu_dereference(clnt->cl_xprt);

	if (xprt->address_strings[format] != NULL)
		return xprt->address_strings[format];
	else
		return "unprintable";
}
EXPORT_SYMBOL_GPL(rpc_peeraddr2str);

static const struct sockaddr_in rpc_inaddr_loopback = {
	.sin_family		= AF_INET,
	.sin_addr.s_addr	= htonl(INADDR_ANY),
};

static const struct sockaddr_in6 rpc_in6addr_loopback = {
	.sin6_family		= AF_INET6,
	.sin6_addr		= IN6ADDR_ANY_INIT,
};

/*
 * Try a getsockname() on a connected datagram socket.  Using a
 * connected datagram socket prevents leaving a socket in TIME_WAIT.
 * This conserves the ephemeral port number space.
 *
 * Returns zero and fills in "buf" if successful; otherwise, a
 * negative errno is returned.
 */
static int rpc_sockname(struct net *net, struct sockaddr *sap, size_t salen,
			struct sockaddr *buf)
{
	struct socket *sock;
	int err;

	err = __sock_create(net, sap->sa_family,
				SOCK_DGRAM, IPPROTO_UDP, &sock, 1);
	if (err < 0) {
		dprintk("RPC:       can't create UDP socket (%d)\n", err);
		goto out;
	}

	switch (sap->sa_family) {
	case AF_INET:
		err = kernel_bind(sock,
				(struct sockaddr *)&rpc_inaddr_loopback,
				sizeof(rpc_inaddr_loopback));
		break;
	case AF_INET6:
		err = kernel_bind(sock,
				(struct sockaddr *)&rpc_in6addr_loopback,
				sizeof(rpc_in6addr_loopback));
		break;
	default:
		err = -EAFNOSUPPORT;
		goto out;
	}
	if (err < 0) {
		dprintk("RPC:       can't bind UDP socket (%d)\n", err);
		goto out_release;
	}

	err = kernel_connect(sock, sap, salen, 0);
	if (err < 0) {
		dprintk("RPC:       can't connect UDP socket (%d)\n", err);
		goto out_release;
	}

	err = kernel_getsockname(sock, buf);
	if (err < 0) {
		dprintk("RPC:       getsockname failed (%d)\n", err);
		goto out_release;
	}

	err = 0;
	if (buf->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)buf;
		sin6->sin6_scope_id = 0;
	}
	dprintk("RPC:       %s succeeded\n", __func__);

out_release:
	sock_release(sock);
out:
	return err;
}

/*
 * Scraping a connected socket failed, so we don't have a useable
 * local address.  Fallback: generate an address that will prevent
 * the server from calling us back.
 *
 * Returns zero and fills in "buf" if successful; otherwise, a
 * negative errno is returned.
 */
static int rpc_anyaddr(int family, struct sockaddr *buf, size_t buflen)
{
	switch (family) {
	case AF_INET:
		if (buflen < sizeof(rpc_inaddr_loopback))
			return -EINVAL;
		memcpy(buf, &rpc_inaddr_loopback,
				sizeof(rpc_inaddr_loopback));
		break;
	case AF_INET6:
		if (buflen < sizeof(rpc_in6addr_loopback))
			return -EINVAL;
		memcpy(buf, &rpc_in6addr_loopback,
				sizeof(rpc_in6addr_loopback));
		break;
	default:
		dprintk("RPC:       %s: address family not supported\n",
			__func__);
		return -EAFNOSUPPORT;
	}
	dprintk("RPC:       %s: succeeded\n", __func__);
	return 0;
}

/**
 * rpc_localaddr - discover local endpoint address for an RPC client
 * @clnt: RPC client structure
 * @buf: target buffer
 * @buflen: size of target buffer, in bytes
 *
 * Returns zero and fills in "buf" and "buflen" if successful;
 * otherwise, a negative errno is returned.
 *
 * This works even if the underlying transport is not currently connected,
 * or if the upper layer never previously provided a source address.
 *
 * The result of this function call is transient: multiple calls in
 * succession may give different results, depending on how local
 * networking configuration changes over time.
 */
int rpc_localaddr(struct rpc_clnt *clnt, struct sockaddr *buf, size_t buflen)
{
	struct sockaddr_storage address;
	struct sockaddr *sap = (struct sockaddr *)&address;
	struct rpc_xprt *xprt;
	struct net *net;
	size_t salen;
	int err;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	salen = xprt->addrlen;
	memcpy(sap, &xprt->addr, salen);
	net = get_net(xprt->xprt_net);
	rcu_read_unlock();

	rpc_set_port(sap, 0);
	err = rpc_sockname(net, sap, salen, buf);
	put_net(net);
	if (err != 0)
		/* Couldn't discover local address, return ANYADDR */
		return rpc_anyaddr(sap->sa_family, buf, buflen);
	return 0;
}
EXPORT_SYMBOL_GPL(rpc_localaddr);

void
rpc_setbufsize(struct rpc_clnt *clnt, unsigned int sndsize, unsigned int rcvsize)
{
	struct rpc_xprt *xprt;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	if (xprt->ops->set_buffer_size)
		xprt->ops->set_buffer_size(xprt, sndsize, rcvsize);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(rpc_setbufsize);

/**
 * rpc_net_ns - Get the network namespace for this RPC client
 * @clnt: RPC client to query
 *
 */
struct net *rpc_net_ns(struct rpc_clnt *clnt)
{
	struct net *ret;

	rcu_read_lock();
	ret = rcu_dereference(clnt->cl_xprt)->xprt_net;
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_net_ns);

/**
 * rpc_max_payload - Get maximum payload size for a transport, in bytes
 * @clnt: RPC client to query
 *
 * For stream transports, this is one RPC record fragment (see RFC
 * 1831), as we don't support multi-record requests yet.  For datagram
 * transports, this is the size of an IP packet minus the IP, UDP, and
 * RPC header sizes.
 */
size_t rpc_max_payload(struct rpc_clnt *clnt)
{
	size_t ret;

	rcu_read_lock();
	ret = rcu_dereference(clnt->cl_xprt)->max_payload;
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_max_payload);

/**
 * rpc_max_bc_payload - Get maximum backchannel payload size, in bytes
 * @clnt: RPC client to query
 */
size_t rpc_max_bc_payload(struct rpc_clnt *clnt)
{
	struct rpc_xprt *xprt;
	size_t ret;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	ret = xprt->ops->bc_maxpayload(xprt);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_max_bc_payload);

unsigned int rpc_num_bc_slots(struct rpc_clnt *clnt)
{
	struct rpc_xprt *xprt;
	unsigned int ret;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	ret = xprt->ops->bc_num_slots(xprt);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_num_bc_slots);

/**
 * rpc_force_rebind - force transport to check that remote port is unchanged
 * @clnt: client to rebind
 *
 */
void rpc_force_rebind(struct rpc_clnt *clnt)
{
	if (clnt->cl_autobind) {
		rcu_read_lock();
		xprt_clear_bound(rcu_dereference(clnt->cl_xprt));
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL_GPL(rpc_force_rebind);

static int
__rpc_restart_call(struct rpc_task *task, void (*action)(struct rpc_task *))
{
	task->tk_status = 0;
	task->tk_rpc_status = 0;
	task->tk_action = action;
	return 1;
}

/*
 * Restart an (async) RPC call. Usually called from within the
 * exit handler.
 */
int
rpc_restart_call(struct rpc_task *task)
{
	return __rpc_restart_call(task, call_start);
}
EXPORT_SYMBOL_GPL(rpc_restart_call);

/*
 * Restart an (async) RPC call from the call_prepare state.
 * Usually called from within the exit handler.
 */
int
rpc_restart_call_prepare(struct rpc_task *task)
{
	if (task->tk_ops->rpc_call_prepare != NULL)
		return __rpc_restart_call(task, rpc_prepare_task);
	return rpc_restart_call(task);
}
EXPORT_SYMBOL_GPL(rpc_restart_call_prepare);

const char
*rpc_proc_name(const struct rpc_task *task)
{
	const struct rpc_procinfo *proc = task->tk_msg.rpc_proc;

	if (proc) {
		if (proc->p_name)
			return proc->p_name;
		else
			return "NULL";
	} else
		return "no proc";
}

static void
__rpc_call_rpcerror(struct rpc_task *task, int tk_status, int rpc_status)
{
	task->tk_rpc_status = rpc_status;
	rpc_exit(task, tk_status);
}

static void
rpc_call_rpcerror(struct rpc_task *task, int status)
{
	__rpc_call_rpcerror(task, status, status);
}

/*
 * 0.  Initial state
 *
 *     Other FSM states can be visited zero or more times, but
 *     this state is visited exactly once for each RPC.
 */
static void
call_start(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	int idx = task->tk_msg.rpc_proc->p_statidx;

	trace_rpc_request(task);
	dprintk("RPC: %5u call_start %s%d proc %s (%s)\n", task->tk_pid,
			clnt->cl_program->name, clnt->cl_vers,
			rpc_proc_name(task),
			(RPC_IS_ASYNC(task) ? "async" : "sync"));

	/* Increment call count (version might not be valid for ping) */
	if (clnt->cl_program->version[clnt->cl_vers])
		clnt->cl_program->version[clnt->cl_vers]->counts[idx]++;
	clnt->cl_stats->rpccnt++;
	task->tk_action = call_reserve;
	rpc_task_set_transport(task, clnt);
}

/*
 * 1.	Reserve an RPC call slot
 */
static void
call_reserve(struct rpc_task *task)
{
	dprint_status(task);

	task->tk_status  = 0;
	task->tk_action  = call_reserveresult;
	xprt_reserve(task);
}

static void call_retry_reserve(struct rpc_task *task);

/*
 * 1b.	Grok the result of xprt_reserve()
 */
static void
call_reserveresult(struct rpc_task *task)
{
	int status = task->tk_status;

	dprint_status(task);

	/*
	 * After a call to xprt_reserve(), we must have either
	 * a request slot or else an error status.
	 */
	task->tk_status = 0;
	if (status >= 0) {
		if (task->tk_rqstp) {
			task->tk_action = call_refresh;
			return;
		}

		rpc_call_rpcerror(task, -EIO);
		return;
	}

	/*
	 * Even though there was an error, we may have acquired
	 * a request slot somehow.  Make sure not to leak it.
	 */
	if (task->tk_rqstp)
		xprt_release(task);

	switch (status) {
	case -ENOMEM:
		rpc_delay(task, HZ >> 2);
		/* fall through */
	case -EAGAIN:	/* woken up; retry */
		task->tk_action = call_retry_reserve;
		return;
	default:
		rpc_call_rpcerror(task, status);
	}
}

/*
 * 1c.	Retry reserving an RPC call slot
 */
static void
call_retry_reserve(struct rpc_task *task)
{
	dprint_status(task);

	task->tk_status  = 0;
	task->tk_action  = call_reserveresult;
	xprt_retry_reserve(task);
}

/*
 * 2.	Bind and/or refresh the credentials
 */
static void
call_refresh(struct rpc_task *task)
{
	dprint_status(task);

	task->tk_action = call_refreshresult;
	task->tk_status = 0;
	task->tk_client->cl_stats->rpcauthrefresh++;
	rpcauth_refreshcred(task);
}

/*
 * 2a.	Process the results of a credential refresh
 */
static void
call_refreshresult(struct rpc_task *task)
{
	int status = task->tk_status;

	dprint_status(task);

	task->tk_status = 0;
	task->tk_action = call_refresh;
	switch (status) {
	case 0:
		if (rpcauth_uptodatecred(task)) {
			task->tk_action = call_allocate;
			return;
		}
		/* Use rate-limiting and a max number of retries if refresh
		 * had status 0 but failed to update the cred.
		 */
		/* fall through */
	case -ETIMEDOUT:
		rpc_delay(task, 3*HZ);
		/* fall through */
	case -EAGAIN:
		status = -EACCES;
		/* fall through */
	case -EKEYEXPIRED:
		if (!task->tk_cred_retry)
			break;
		task->tk_cred_retry--;
		dprintk("RPC: %5u %s: retry refresh creds\n",
				task->tk_pid, __func__);
		return;
	}
	dprintk("RPC: %5u %s: refresh creds failed with error %d\n",
				task->tk_pid, __func__, status);
	rpc_call_rpcerror(task, status);
}

/*
 * 2b.	Allocate the buffer. For details, see sched.c:rpc_malloc.
 *	(Note: buffer memory is freed in xprt_release).
 */
static void
call_allocate(struct rpc_task *task)
{
	const struct rpc_auth *auth = task->tk_rqstp->rq_cred->cr_auth;
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	const struct rpc_procinfo *proc = task->tk_msg.rpc_proc;
	int status;

	dprint_status(task);

	task->tk_status = 0;
	task->tk_action = call_encode;

	if (req->rq_buffer)
		return;

	if (proc->p_proc != 0) {
		BUG_ON(proc->p_arglen == 0);
		if (proc->p_decode != NULL)
			BUG_ON(proc->p_replen == 0);
	}

	/*
	 * Calculate the size (in quads) of the RPC call
	 * and reply headers, and convert both values
	 * to byte sizes.
	 */
	req->rq_callsize = RPC_CALLHDRSIZE + (auth->au_cslack << 1) +
			   proc->p_arglen;
	req->rq_callsize <<= 2;
	/*
	 * Note: the reply buffer must at minimum allocate enough space
	 * for the 'struct accepted_reply' from RFC5531.
	 */
	req->rq_rcvsize = RPC_REPHDRSIZE + auth->au_rslack + \
			max_t(size_t, proc->p_replen, 2);
	req->rq_rcvsize <<= 2;

	status = xprt->ops->buf_alloc(task);
	xprt_inject_disconnect(xprt);
	if (status == 0)
		return;
	if (status != -ENOMEM) {
		rpc_call_rpcerror(task, status);
		return;
	}

	dprintk("RPC: %5u rpc_buffer allocation failed\n", task->tk_pid);

	if (RPC_IS_ASYNC(task) || !fatal_signal_pending(current)) {
		task->tk_action = call_allocate;
		rpc_delay(task, HZ>>4);
		return;
	}

	rpc_call_rpcerror(task, -ERESTARTSYS);
}

static int
rpc_task_need_encode(struct rpc_task *task)
{
	return test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate) == 0 &&
		(!(task->tk_flags & RPC_TASK_SENT) ||
		 !(task->tk_flags & RPC_TASK_NO_RETRANS_TIMEOUT) ||
		 xprt_request_need_retransmit(task));
}

static void
rpc_xdr_encode(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct xdr_stream xdr;

	xdr_buf_init(&req->rq_snd_buf,
		     req->rq_buffer,
		     req->rq_callsize);
	xdr_buf_init(&req->rq_rcv_buf,
		     req->rq_rbuffer,
		     req->rq_rcvsize);

	req->rq_reply_bytes_recvd = 0;
	req->rq_snd_buf.head[0].iov_len = 0;
	xdr_init_encode(&xdr, &req->rq_snd_buf,
			req->rq_snd_buf.head[0].iov_base, req);
	xdr_free_bvec(&req->rq_snd_buf);
	if (rpc_encode_header(task, &xdr))
		return;

	task->tk_status = rpcauth_wrap_req(task, &xdr);
}

/*
 * 3.	Encode arguments of an RPC call
 */
static void
call_encode(struct rpc_task *task)
{
	if (!rpc_task_need_encode(task))
		goto out;
	dprint_status(task);
	/* Dequeue task from the receive queue while we're encoding */
	xprt_request_dequeue_xprt(task);
	/* Encode here so that rpcsec_gss can use correct sequence number. */
	rpc_xdr_encode(task);
	/* Did the encode result in an error condition? */
	if (task->tk_status != 0) {
		/* Was the error nonfatal? */
		switch (task->tk_status) {
		case -EAGAIN:
		case -ENOMEM:
			rpc_delay(task, HZ >> 4);
			break;
		case -EKEYEXPIRED:
			if (!task->tk_cred_retry) {
				rpc_exit(task, task->tk_status);
			} else {
				task->tk_action = call_refresh;
				task->tk_cred_retry--;
				dprintk("RPC: %5u %s: retry refresh creds\n",
					task->tk_pid, __func__);
			}
			break;
		default:
			rpc_call_rpcerror(task, task->tk_status);
		}
		return;
	}

	/* Add task to reply queue before transmission to avoid races */
	if (rpc_reply_expected(task))
		xprt_request_enqueue_receive(task);
	xprt_request_enqueue_transmit(task);
out:
	task->tk_action = call_transmit;
	/* Check that the connection is OK */
	if (!xprt_bound(task->tk_xprt))
		task->tk_action = call_bind;
	else if (!xprt_connected(task->tk_xprt))
		task->tk_action = call_connect;
}

/*
 * Helpers to check if the task was already transmitted, and
 * to take action when that is the case.
 */
static bool
rpc_task_transmitted(struct rpc_task *task)
{
	return !test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate);
}

static void
rpc_task_handle_transmitted(struct rpc_task *task)
{
	xprt_end_transmit(task);
	task->tk_action = call_transmit_status;
}

/*
 * 4.	Get the server port number if not yet set
 */
static void
call_bind(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;

	if (rpc_task_transmitted(task)) {
		rpc_task_handle_transmitted(task);
		return;
	}

	if (xprt_bound(xprt)) {
		task->tk_action = call_connect;
		return;
	}

	dprint_status(task);

	task->tk_action = call_bind_status;
	if (!xprt_prepare_transmit(task))
		return;

	xprt->ops->rpcbind(task);
}

/*
 * 4a.	Sort out bind result
 */
static void
call_bind_status(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;
	int status = -EIO;

	if (rpc_task_transmitted(task)) {
		rpc_task_handle_transmitted(task);
		return;
	}

	dprint_status(task);
	trace_rpc_bind_status(task);
	if (task->tk_status >= 0)
		goto out_next;
	if (xprt_bound(xprt)) {
		task->tk_status = 0;
		goto out_next;
	}

	switch (task->tk_status) {
	case -ENOMEM:
		dprintk("RPC: %5u rpcbind out of memory\n", task->tk_pid);
		rpc_delay(task, HZ >> 2);
		goto retry_timeout;
	case -EACCES:
		dprintk("RPC: %5u remote rpcbind: RPC program/version "
				"unavailable\n", task->tk_pid);
		/* fail immediately if this is an RPC ping */
		if (task->tk_msg.rpc_proc->p_proc == 0) {
			status = -EOPNOTSUPP;
			break;
		}
		if (task->tk_rebind_retry == 0)
			break;
		task->tk_rebind_retry--;
		rpc_delay(task, 3*HZ);
		goto retry_timeout;
	case -ENOBUFS:
		rpc_delay(task, HZ >> 2);
		goto retry_timeout;
	case -EAGAIN:
		goto retry_timeout;
	case -ETIMEDOUT:
		dprintk("RPC: %5u rpcbind request timed out\n",
				task->tk_pid);
		goto retry_timeout;
	case -EPFNOSUPPORT:
		/* server doesn't support any rpcbind version we know of */
		dprintk("RPC: %5u unrecognized remote rpcbind service\n",
				task->tk_pid);
		break;
	case -EPROTONOSUPPORT:
		dprintk("RPC: %5u remote rpcbind version unavailable, retrying\n",
				task->tk_pid);
		goto retry_timeout;
	case -ECONNREFUSED:		/* connection problems */
	case -ECONNRESET:
	case -ECONNABORTED:
	case -ENOTCONN:
	case -EHOSTDOWN:
	case -ENETDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -EPIPE:
		dprintk("RPC: %5u remote rpcbind unreachable: %d\n",
				task->tk_pid, task->tk_status);
		if (!RPC_IS_SOFTCONN(task)) {
			rpc_delay(task, 5*HZ);
			goto retry_timeout;
		}
		status = task->tk_status;
		break;
	default:
		dprintk("RPC: %5u unrecognized rpcbind error (%d)\n",
				task->tk_pid, -task->tk_status);
	}

	rpc_call_rpcerror(task, status);
	return;
out_next:
	task->tk_action = call_connect;
	return;
retry_timeout:
	task->tk_status = 0;
	task->tk_action = call_bind;
	rpc_check_timeout(task);
}

/*
 * 4b.	Connect to the RPC server
 */
static void
call_connect(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;

	if (rpc_task_transmitted(task)) {
		rpc_task_handle_transmitted(task);
		return;
	}

	if (xprt_connected(xprt)) {
		task->tk_action = call_transmit;
		return;
	}

	dprintk("RPC: %5u call_connect xprt %p %s connected\n",
			task->tk_pid, xprt,
			(xprt_connected(xprt) ? "is" : "is not"));

	task->tk_action = call_connect_status;
	if (task->tk_status < 0)
		return;
	if (task->tk_flags & RPC_TASK_NOCONNECT) {
		rpc_call_rpcerror(task, -ENOTCONN);
		return;
	}
	if (!xprt_prepare_transmit(task))
		return;
	xprt_connect(task);
}

/*
 * 4c.	Sort out connect result
 */
static void
call_connect_status(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_rqstp->rq_xprt;
	struct rpc_clnt *clnt = task->tk_client;
	int status = task->tk_status;

	if (rpc_task_transmitted(task)) {
		rpc_task_handle_transmitted(task);
		return;
	}

	dprint_status(task);
	trace_rpc_connect_status(task);

	if (task->tk_status == 0) {
		clnt->cl_stats->netreconn++;
		goto out_next;
	}
	if (xprt_connected(xprt)) {
		task->tk_status = 0;
		goto out_next;
	}

	task->tk_status = 0;
	switch (status) {
	case -ECONNREFUSED:
		/* A positive refusal suggests a rebind is needed. */
		if (RPC_IS_SOFTCONN(task))
			break;
		if (clnt->cl_autobind) {
			rpc_force_rebind(clnt);
			goto out_retry;
		}
		/* fall through */
	case -ECONNRESET:
	case -ECONNABORTED:
	case -ENETDOWN:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EPIPE:
	case -EPROTO:
		xprt_conditional_disconnect(task->tk_rqstp->rq_xprt,
					    task->tk_rqstp->rq_connect_cookie);
		if (RPC_IS_SOFTCONN(task))
			break;
		/* retry with existing socket, after a delay */
		rpc_delay(task, 3*HZ);
		/* fall through */
	case -EADDRINUSE:
	case -ENOTCONN:
	case -EAGAIN:
	case -ETIMEDOUT:
		goto out_retry;
	case -ENOBUFS:
		rpc_delay(task, HZ >> 2);
		goto out_retry;
	}
	rpc_call_rpcerror(task, status);
	return;
out_next:
	task->tk_action = call_transmit;
	return;
out_retry:
	/* Check for timeouts before looping back to call_bind */
	task->tk_action = call_bind;
	rpc_check_timeout(task);
}

/*
 * 5.	Transmit the RPC request, and wait for reply
 */
static void
call_transmit(struct rpc_task *task)
{
	if (rpc_task_transmitted(task)) {
		rpc_task_handle_transmitted(task);
		return;
	}

	dprint_status(task);

	task->tk_action = call_transmit_status;
	if (!xprt_prepare_transmit(task))
		return;
	task->tk_status = 0;
	if (test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate)) {
		if (!xprt_connected(task->tk_xprt)) {
			task->tk_status = -ENOTCONN;
			return;
		}
		xprt_transmit(task);
	}
	xprt_end_transmit(task);
}

/*
 * 5a.	Handle cleanup after a transmission
 */
static void
call_transmit_status(struct rpc_task *task)
{
	task->tk_action = call_status;

	/*
	 * Common case: success.  Force the compiler to put this
	 * test first.
	 */
	if (rpc_task_transmitted(task)) {
		task->tk_status = 0;
		xprt_request_wait_receive(task);
		return;
	}

	switch (task->tk_status) {
	default:
		dprint_status(task);
		break;
	case -EBADMSG:
		task->tk_status = 0;
		task->tk_action = call_encode;
		break;
		/*
		 * Special cases: if we've been waiting on the
		 * socket's write_space() callback, or if the
		 * socket just returned a connection error,
		 * then hold onto the transport lock.
		 */
	case -ENOBUFS:
		rpc_delay(task, HZ>>2);
		/* fall through */
	case -EBADSLT:
	case -EAGAIN:
		task->tk_action = call_transmit;
		task->tk_status = 0;
		break;
	case -ECONNREFUSED:
	case -EHOSTDOWN:
	case -ENETDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -EPERM:
		if (RPC_IS_SOFTCONN(task)) {
			if (!task->tk_msg.rpc_proc->p_proc)
				trace_xprt_ping(task->tk_xprt,
						task->tk_status);
			rpc_call_rpcerror(task, task->tk_status);
			return;
		}
		/* fall through */
	case -ECONNRESET:
	case -ECONNABORTED:
	case -EADDRINUSE:
	case -ENOTCONN:
	case -EPIPE:
		task->tk_action = call_bind;
		task->tk_status = 0;
		break;
	}
	rpc_check_timeout(task);
}

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static void call_bc_transmit(struct rpc_task *task);
static void call_bc_transmit_status(struct rpc_task *task);

static void
call_bc_encode(struct rpc_task *task)
{
	xprt_request_enqueue_transmit(task);
	task->tk_action = call_bc_transmit;
}

/*
 * 5b.	Send the backchannel RPC reply.  On error, drop the reply.  In
 * addition, disconnect on connectivity errors.
 */
static void
call_bc_transmit(struct rpc_task *task)
{
	task->tk_action = call_bc_transmit_status;
	if (test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate)) {
		if (!xprt_prepare_transmit(task))
			return;
		task->tk_status = 0;
		xprt_transmit(task);
	}
	xprt_end_transmit(task);
}

static void
call_bc_transmit_status(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (rpc_task_transmitted(task))
		task->tk_status = 0;

	dprint_status(task);

	switch (task->tk_status) {
	case 0:
		/* Success */
	case -ENETDOWN:
	case -EHOSTDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -ECONNRESET:
	case -ECONNREFUSED:
	case -EADDRINUSE:
	case -ENOTCONN:
	case -EPIPE:
		break;
	case -ENOBUFS:
		rpc_delay(task, HZ>>2);
		/* fall through */
	case -EBADSLT:
	case -EAGAIN:
		task->tk_status = 0;
		task->tk_action = call_bc_transmit;
		return;
	case -ETIMEDOUT:
		/*
		 * Problem reaching the server.  Disconnect and let the
		 * forechannel reestablish the connection.  The server will
		 * have to retransmit the backchannel request and we'll
		 * reprocess it.  Since these ops are idempotent, there's no
		 * need to cache our reply at this time.
		 */
		printk(KERN_NOTICE "RPC: Could not send backchannel reply "
			"error: %d\n", task->tk_status);
		xprt_conditional_disconnect(req->rq_xprt,
			req->rq_connect_cookie);
		break;
	default:
		/*
		 * We were unable to reply and will have to drop the
		 * request.  The server should reconnect and retransmit.
		 */
		printk(KERN_NOTICE "RPC: Could not send backchannel reply "
			"error: %d\n", task->tk_status);
		break;
	}
	task->tk_action = rpc_exit_task;
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

/*
 * 6.	Sort out the RPC call status
 */
static void
call_status(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	int		status;

	if (!task->tk_msg.rpc_proc->p_proc)
		trace_xprt_ping(task->tk_xprt, task->tk_status);

	dprint_status(task);

	status = task->tk_status;
	if (status >= 0) {
		task->tk_action = call_decode;
		return;
	}

	trace_rpc_call_status(task);
	task->tk_status = 0;
	switch(status) {
	case -EHOSTDOWN:
	case -ENETDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -EPERM:
		if (RPC_IS_SOFTCONN(task))
			goto out_exit;
		/*
		 * Delay any retries for 3 seconds, then handle as if it
		 * were a timeout.
		 */
		rpc_delay(task, 3*HZ);
		/* fall through */
	case -ETIMEDOUT:
		break;
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ECONNABORTED:
	case -ENOTCONN:
		rpc_force_rebind(clnt);
		break;
	case -EADDRINUSE:
		rpc_delay(task, 3*HZ);
		/* fall through */
	case -EPIPE:
	case -EAGAIN:
		break;
	case -EIO:
		/* shutdown or soft timeout */
		goto out_exit;
	default:
		if (clnt->cl_chatty)
			printk("%s: RPC call returned error %d\n",
			       clnt->cl_program->name, -status);
		goto out_exit;
	}
	task->tk_action = call_encode;
	rpc_check_timeout(task);
	return;
out_exit:
	rpc_call_rpcerror(task, status);
}

static bool
rpc_check_connected(const struct rpc_rqst *req)
{
	/* No allocated request or transport? return true */
	if (!req || !req->rq_xprt)
		return true;
	return xprt_connected(req->rq_xprt);
}

static void
rpc_check_timeout(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;

	if (xprt_adjust_timeout(task->tk_rqstp) == 0)
		return;

	dprintk("RPC: %5u call_timeout (major)\n", task->tk_pid);
	task->tk_timeouts++;

	if (RPC_IS_SOFTCONN(task) && !rpc_check_connected(task->tk_rqstp)) {
		rpc_call_rpcerror(task, -ETIMEDOUT);
		return;
	}

	if (RPC_IS_SOFT(task)) {
		/*
		 * Once a "no retrans timeout" soft tasks (a.k.a NFSv4) has
		 * been sent, it should time out only if the transport
		 * connection gets terminally broken.
		 */
		if ((task->tk_flags & RPC_TASK_NO_RETRANS_TIMEOUT) &&
		    rpc_check_connected(task->tk_rqstp))
			return;

		if (clnt->cl_chatty) {
			pr_notice_ratelimited(
				"%s: server %s not responding, timed out\n",
				clnt->cl_program->name,
				task->tk_xprt->servername);
		}
		if (task->tk_flags & RPC_TASK_TIMEOUT)
			rpc_call_rpcerror(task, -ETIMEDOUT);
		else
			__rpc_call_rpcerror(task, -EIO, -ETIMEDOUT);
		return;
	}

	if (!(task->tk_flags & RPC_CALL_MAJORSEEN)) {
		task->tk_flags |= RPC_CALL_MAJORSEEN;
		if (clnt->cl_chatty) {
			pr_notice_ratelimited(
				"%s: server %s not responding, still trying\n",
				clnt->cl_program->name,
				task->tk_xprt->servername);
		}
	}
	rpc_force_rebind(clnt);
	/*
	 * Did our request time out due to an RPCSEC_GSS out-of-sequence
	 * event? RFC2203 requires the server to drop all such requests.
	 */
	rpcauth_invalcred(task);
}

/*
 * 7.	Decode the RPC reply
 */
static void
call_decode(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	struct xdr_stream xdr;
	int err;

	dprint_status(task);

	if (!task->tk_msg.rpc_proc->p_decode) {
		task->tk_action = rpc_exit_task;
		return;
	}

	if (task->tk_flags & RPC_CALL_MAJORSEEN) {
		if (clnt->cl_chatty) {
			pr_notice_ratelimited("%s: server %s OK\n",
				clnt->cl_program->name,
				task->tk_xprt->servername);
		}
		task->tk_flags &= ~RPC_CALL_MAJORSEEN;
	}

	/*
	 * Ensure that we see all writes made by xprt_complete_rqst()
	 * before it changed req->rq_reply_bytes_recvd.
	 */
	smp_rmb();

	/*
	 * Did we ever call xprt_complete_rqst()? If not, we should assume
	 * the message is incomplete.
	 */
	err = -EAGAIN;
	if (!req->rq_reply_bytes_recvd)
		goto out;

	req->rq_rcv_buf.len = req->rq_private_buf.len;
	trace_xprt_recvfrom(&req->rq_rcv_buf);

	/* Check that the softirq receive buffer is valid */
	WARN_ON(memcmp(&req->rq_rcv_buf, &req->rq_private_buf,
				sizeof(req->rq_rcv_buf)) != 0);

	xdr_init_decode(&xdr, &req->rq_rcv_buf,
			req->rq_rcv_buf.head[0].iov_base, req);
	err = rpc_decode_header(task, &xdr);
out:
	switch (err) {
	case 0:
		task->tk_action = rpc_exit_task;
		task->tk_status = rpcauth_unwrap_resp(task, &xdr);
		dprintk("RPC: %5u %s result %d\n",
			task->tk_pid, __func__, task->tk_status);
		return;
	case -EAGAIN:
		task->tk_status = 0;
		if (task->tk_client->cl_discrtry)
			xprt_conditional_disconnect(req->rq_xprt,
						    req->rq_connect_cookie);
		task->tk_action = call_encode;
		rpc_check_timeout(task);
		break;
	case -EKEYREJECTED:
		task->tk_action = call_reserve;
		rpc_check_timeout(task);
		rpcauth_invalcred(task);
		/* Ensure we obtain a new XID if we retry! */
		xprt_release(task);
	}
}

static int
rpc_encode_header(struct rpc_task *task, struct xdr_stream *xdr)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	__be32 *p;
	int error;

	error = -EMSGSIZE;
	p = xdr_reserve_space(xdr, RPC_CALLHDRSIZE << 2);
	if (!p)
		goto out_fail;
	*p++ = req->rq_xid;
	*p++ = rpc_call;
	*p++ = cpu_to_be32(RPC_VERSION);
	*p++ = cpu_to_be32(clnt->cl_prog);
	*p++ = cpu_to_be32(clnt->cl_vers);
	*p   = cpu_to_be32(task->tk_msg.rpc_proc->p_proc);

	error = rpcauth_marshcred(task, xdr);
	if (error < 0)
		goto out_fail;
	return 0;
out_fail:
	trace_rpc_bad_callhdr(task);
	rpc_call_rpcerror(task, error);
	return error;
}

static noinline int
rpc_decode_header(struct rpc_task *task, struct xdr_stream *xdr)
{
	struct rpc_clnt *clnt = task->tk_client;
	int error;
	__be32 *p;

	/* RFC-1014 says that the representation of XDR data must be a
	 * multiple of four bytes
	 * - if it isn't pointer subtraction in the NFS client may give
	 *   undefined results
	 */
	if (task->tk_rqstp->rq_rcv_buf.len & 3)
		goto out_unparsable;

	p = xdr_inline_decode(xdr, 3 * sizeof(*p));
	if (!p)
		goto out_unparsable;
	p++;	/* skip XID */
	if (*p++ != rpc_reply)
		goto out_unparsable;
	if (*p++ != rpc_msg_accepted)
		goto out_msg_denied;

	error = rpcauth_checkverf(task, xdr);
	if (error)
		goto out_verifier;

	p = xdr_inline_decode(xdr, sizeof(*p));
	if (!p)
		goto out_unparsable;
	switch (*p) {
	case rpc_success:
		return 0;
	case rpc_prog_unavail:
		trace_rpc__prog_unavail(task);
		error = -EPFNOSUPPORT;
		goto out_err;
	case rpc_prog_mismatch:
		trace_rpc__prog_mismatch(task);
		error = -EPROTONOSUPPORT;
		goto out_err;
	case rpc_proc_unavail:
		trace_rpc__proc_unavail(task);
		error = -EOPNOTSUPP;
		goto out_err;
	case rpc_garbage_args:
	case rpc_system_err:
		trace_rpc__garbage_args(task);
		error = -EIO;
		break;
	default:
		goto out_unparsable;
	}

out_garbage:
	clnt->cl_stats->rpcgarbage++;
	if (task->tk_garb_retry) {
		task->tk_garb_retry--;
		task->tk_action = call_encode;
		return -EAGAIN;
	}
out_err:
	rpc_call_rpcerror(task, error);
	return error;

out_unparsable:
	trace_rpc__unparsable(task);
	error = -EIO;
	goto out_garbage;

out_verifier:
	trace_rpc_bad_verifier(task);
	goto out_garbage;

out_msg_denied:
	error = -EACCES;
	p = xdr_inline_decode(xdr, sizeof(*p));
	if (!p)
		goto out_unparsable;
	switch (*p++) {
	case rpc_auth_error:
		break;
	case rpc_mismatch:
		trace_rpc__mismatch(task);
		error = -EPROTONOSUPPORT;
		goto out_err;
	default:
		goto out_unparsable;
	}

	p = xdr_inline_decode(xdr, sizeof(*p));
	if (!p)
		goto out_unparsable;
	switch (*p++) {
	case rpc_autherr_rejectedcred:
	case rpc_autherr_rejectedverf:
	case rpcsec_gsserr_credproblem:
	case rpcsec_gsserr_ctxproblem:
		if (!task->tk_cred_retry)
			break;
		task->tk_cred_retry--;
		trace_rpc__stale_creds(task);
		return -EKEYREJECTED;
	case rpc_autherr_badcred:
	case rpc_autherr_badverf:
		/* possibly garbled cred/verf? */
		if (!task->tk_garb_retry)
			break;
		task->tk_garb_retry--;
		trace_rpc__bad_creds(task);
		task->tk_action = call_encode;
		return -EAGAIN;
	case rpc_autherr_tooweak:
		trace_rpc__auth_tooweak(task);
		pr_warn("RPC: server %s requires stronger authentication.\n",
			task->tk_xprt->servername);
		break;
	default:
		goto out_unparsable;
	}
	goto out_err;
}

static void rpcproc_encode_null(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
		const void *obj)
{
}

static int rpcproc_decode_null(struct rpc_rqst *rqstp, struct xdr_stream *xdr,
		void *obj)
{
	return 0;
}

static const struct rpc_procinfo rpcproc_null = {
	.p_encode = rpcproc_encode_null,
	.p_decode = rpcproc_decode_null,
};

static int rpc_ping(struct rpc_clnt *clnt)
{
	struct rpc_message msg = {
		.rpc_proc = &rpcproc_null,
	};
	int err;
	err = rpc_call_sync(clnt, &msg, RPC_TASK_SOFT | RPC_TASK_SOFTCONN |
			    RPC_TASK_NULLCREDS);
	return err;
}

static
struct rpc_task *rpc_call_null_helper(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt, struct rpc_cred *cred, int flags,
		const struct rpc_call_ops *ops, void *data)
{
	struct rpc_message msg = {
		.rpc_proc = &rpcproc_null,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_xprt = xprt,
		.rpc_message = &msg,
		.rpc_op_cred = cred,
		.callback_ops = (ops != NULL) ? ops : &rpc_default_ops,
		.callback_data = data,
		.flags = flags | RPC_TASK_NULLCREDS,
	};

	return rpc_run_task(&task_setup_data);
}

struct rpc_task *rpc_call_null(struct rpc_clnt *clnt, struct rpc_cred *cred, int flags)
{
	return rpc_call_null_helper(clnt, NULL, cred, flags, NULL, NULL);
}
EXPORT_SYMBOL_GPL(rpc_call_null);

struct rpc_cb_add_xprt_calldata {
	struct rpc_xprt_switch *xps;
	struct rpc_xprt *xprt;
};

static void rpc_cb_add_xprt_done(struct rpc_task *task, void *calldata)
{
	struct rpc_cb_add_xprt_calldata *data = calldata;

	if (task->tk_status == 0)
		rpc_xprt_switch_add_xprt(data->xps, data->xprt);
}

static void rpc_cb_add_xprt_release(void *calldata)
{
	struct rpc_cb_add_xprt_calldata *data = calldata;

	xprt_put(data->xprt);
	xprt_switch_put(data->xps);
	kfree(data);
}

static const struct rpc_call_ops rpc_cb_add_xprt_call_ops = {
	.rpc_call_done = rpc_cb_add_xprt_done,
	.rpc_release = rpc_cb_add_xprt_release,
};

/**
 * rpc_clnt_test_and_add_xprt - Test and add a new transport to a rpc_clnt
 * @clnt: pointer to struct rpc_clnt
 * @xps: pointer to struct rpc_xprt_switch,
 * @xprt: pointer struct rpc_xprt
 * @dummy: unused
 */
int rpc_clnt_test_and_add_xprt(struct rpc_clnt *clnt,
		struct rpc_xprt_switch *xps, struct rpc_xprt *xprt,
		void *dummy)
{
	struct rpc_cb_add_xprt_calldata *data;
	struct rpc_task *task;

	data = kmalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return -ENOMEM;
	data->xps = xprt_switch_get(xps);
	data->xprt = xprt_get(xprt);
	if (rpc_xprt_switch_has_addr(data->xps, (struct sockaddr *)&xprt->addr)) {
		rpc_cb_add_xprt_release(data);
		goto success;
	}

	task = rpc_call_null_helper(clnt, xprt, NULL,
			RPC_TASK_SOFT|RPC_TASK_SOFTCONN|RPC_TASK_ASYNC|RPC_TASK_NULLCREDS,
			&rpc_cb_add_xprt_call_ops, data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
success:
	return 1;
}
EXPORT_SYMBOL_GPL(rpc_clnt_test_and_add_xprt);

/**
 * rpc_clnt_setup_test_and_add_xprt()
 *
 * This is an rpc_clnt_add_xprt setup() function which returns 1 so:
 *   1) caller of the test function must dereference the rpc_xprt_switch
 *   and the rpc_xprt.
 *   2) test function must call rpc_xprt_switch_add_xprt, usually in
 *   the rpc_call_done routine.
 *
 * Upon success (return of 1), the test function adds the new
 * transport to the rpc_clnt xprt switch
 *
 * @clnt: struct rpc_clnt to get the new transport
 * @xps:  the rpc_xprt_switch to hold the new transport
 * @xprt: the rpc_xprt to test
 * @data: a struct rpc_add_xprt_test pointer that holds the test function
 *        and test function call data
 */
int rpc_clnt_setup_test_and_add_xprt(struct rpc_clnt *clnt,
				     struct rpc_xprt_switch *xps,
				     struct rpc_xprt *xprt,
				     void *data)
{
	struct rpc_task *task;
	struct rpc_add_xprt_test *xtest = (struct rpc_add_xprt_test *)data;
	int status = -EADDRINUSE;

	xprt = xprt_get(xprt);
	xprt_switch_get(xps);

	if (rpc_xprt_switch_has_addr(xps, (struct sockaddr *)&xprt->addr))
		goto out_err;

	/* Test the connection */
	task = rpc_call_null_helper(clnt, xprt, NULL,
				    RPC_TASK_SOFT | RPC_TASK_SOFTCONN | RPC_TASK_NULLCREDS,
				    NULL, NULL);
	if (IS_ERR(task)) {
		status = PTR_ERR(task);
		goto out_err;
	}
	status = task->tk_status;
	rpc_put_task(task);

	if (status < 0)
		goto out_err;

	/* rpc_xprt_switch and rpc_xprt are deferrenced by add_xprt_test() */
	xtest->add_xprt_test(clnt, xprt, xtest->data);

	xprt_put(xprt);
	xprt_switch_put(xps);

	/* so that rpc_clnt_add_xprt does not call rpc_xprt_switch_add_xprt */
	return 1;
out_err:
	xprt_put(xprt);
	xprt_switch_put(xps);
	pr_info("RPC:   rpc_clnt_test_xprt failed: %d addr %s not added\n",
		status, xprt->address_strings[RPC_DISPLAY_ADDR]);
	return status;
}
EXPORT_SYMBOL_GPL(rpc_clnt_setup_test_and_add_xprt);

/**
 * rpc_clnt_add_xprt - Add a new transport to a rpc_clnt
 * @clnt: pointer to struct rpc_clnt
 * @xprtargs: pointer to struct xprt_create
 * @setup: callback to test and/or set up the connection
 * @data: pointer to setup function data
 *
 * Creates a new transport using the parameters set in args and
 * adds it to clnt.
 * If ping is set, then test that connectivity succeeds before
 * adding the new transport.
 *
 */
int rpc_clnt_add_xprt(struct rpc_clnt *clnt,
		struct xprt_create *xprtargs,
		int (*setup)(struct rpc_clnt *,
			struct rpc_xprt_switch *,
			struct rpc_xprt *,
			void *),
		void *data)
{
	struct rpc_xprt_switch *xps;
	struct rpc_xprt *xprt;
	unsigned long connect_timeout;
	unsigned long reconnect_timeout;
	unsigned char resvport, reuseport;
	int ret = 0;

	rcu_read_lock();
	xps = xprt_switch_get(rcu_dereference(clnt->cl_xpi.xpi_xpswitch));
	xprt = xprt_iter_xprt(&clnt->cl_xpi);
	if (xps == NULL || xprt == NULL) {
		rcu_read_unlock();
		xprt_switch_put(xps);
		return -EAGAIN;
	}
	resvport = xprt->resvport;
	reuseport = xprt->reuseport;
	connect_timeout = xprt->connect_timeout;
	reconnect_timeout = xprt->max_reconnect_timeout;
	rcu_read_unlock();

	xprt = xprt_create_transport(xprtargs);
	if (IS_ERR(xprt)) {
		ret = PTR_ERR(xprt);
		goto out_put_switch;
	}
	xprt->resvport = resvport;
	xprt->reuseport = reuseport;
	if (xprt->ops->set_connect_timeout != NULL)
		xprt->ops->set_connect_timeout(xprt,
				connect_timeout,
				reconnect_timeout);

	rpc_xprt_switch_set_roundrobin(xps);
	if (setup) {
		ret = setup(clnt, xps, xprt, data);
		if (ret != 0)
			goto out_put_xprt;
	}
	rpc_xprt_switch_add_xprt(xps, xprt);
out_put_xprt:
	xprt_put(xprt);
out_put_switch:
	xprt_switch_put(xps);
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_clnt_add_xprt);

struct connect_timeout_data {
	unsigned long connect_timeout;
	unsigned long reconnect_timeout;
};

static int
rpc_xprt_set_connect_timeout(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		void *data)
{
	struct connect_timeout_data *timeo = data;

	if (xprt->ops->set_connect_timeout)
		xprt->ops->set_connect_timeout(xprt,
				timeo->connect_timeout,
				timeo->reconnect_timeout);
	return 0;
}

void
rpc_set_connect_timeout(struct rpc_clnt *clnt,
		unsigned long connect_timeout,
		unsigned long reconnect_timeout)
{
	struct connect_timeout_data timeout = {
		.connect_timeout = connect_timeout,
		.reconnect_timeout = reconnect_timeout,
	};
	rpc_clnt_iterate_for_each_xprt(clnt,
			rpc_xprt_set_connect_timeout,
			&timeout);
}
EXPORT_SYMBOL_GPL(rpc_set_connect_timeout);

void rpc_clnt_xprt_switch_put(struct rpc_clnt *clnt)
{
	rcu_read_lock();
	xprt_switch_put(rcu_dereference(clnt->cl_xpi.xpi_xpswitch));
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(rpc_clnt_xprt_switch_put);

void rpc_clnt_xprt_switch_add_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt)
{
	rcu_read_lock();
	rpc_xprt_switch_add_xprt(rcu_dereference(clnt->cl_xpi.xpi_xpswitch),
				 xprt);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(rpc_clnt_xprt_switch_add_xprt);

bool rpc_clnt_xprt_switch_has_addr(struct rpc_clnt *clnt,
				   const struct sockaddr *sap)
{
	struct rpc_xprt_switch *xps;
	bool ret;

	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	ret = rpc_xprt_switch_has_addr(xps, sap);
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_clnt_xprt_switch_has_addr);

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
static void rpc_show_header(void)
{
	printk(KERN_INFO "-pid- flgs status -client- --rqstp- "
		"-timeout ---ops--\n");
}

static void rpc_show_task(const struct rpc_clnt *clnt,
			  const struct rpc_task *task)
{
	const char *rpc_waitq = "none";

	if (RPC_IS_QUEUED(task))
		rpc_waitq = rpc_qname(task->tk_waitqueue);

	printk(KERN_INFO "%5u %04x %6d %8p %8p %8ld %8p %sv%u %s a:%ps q:%s\n",
		task->tk_pid, task->tk_flags, task->tk_status,
		clnt, task->tk_rqstp, rpc_task_timeout(task), task->tk_ops,
		clnt->cl_program->name, clnt->cl_vers, rpc_proc_name(task),
		task->tk_action, rpc_waitq);
}

void rpc_show_tasks(struct net *net)
{
	struct rpc_clnt *clnt;
	struct rpc_task *task;
	int header = 0;
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	spin_lock(&sn->rpc_client_lock);
	list_for_each_entry(clnt, &sn->all_clients, cl_clients) {
		spin_lock(&clnt->cl_lock);
		list_for_each_entry(task, &clnt->cl_tasks, tk_task) {
			if (!header) {
				rpc_show_header();
				header++;
			}
			rpc_show_task(clnt, task);
		}
		spin_unlock(&clnt->cl_lock);
	}
	spin_unlock(&sn->rpc_client_lock);
}
#endif

#if IS_ENABLED(CONFIG_SUNRPC_SWAP)
static int
rpc_clnt_swap_activate_callback(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		void *dummy)
{
	return xprt_enable_swap(xprt);
}

int
rpc_clnt_swap_activate(struct rpc_clnt *clnt)
{
	if (atomic_inc_return(&clnt->cl_swapper) == 1)
		return rpc_clnt_iterate_for_each_xprt(clnt,
				rpc_clnt_swap_activate_callback, NULL);
	return 0;
}
EXPORT_SYMBOL_GPL(rpc_clnt_swap_activate);

static int
rpc_clnt_swap_deactivate_callback(struct rpc_clnt *clnt,
		struct rpc_xprt *xprt,
		void *dummy)
{
	xprt_disable_swap(xprt);
	return 0;
}

void
rpc_clnt_swap_deactivate(struct rpc_clnt *clnt)
{
	if (atomic_dec_if_positive(&clnt->cl_swapper) == 0)
		rpc_clnt_iterate_for_each_xprt(clnt,
				rpc_clnt_swap_deactivate_callback, NULL);
}
EXPORT_SYMBOL_GPL(rpc_clnt_swap_deactivate);
#endif /* CONFIG_SUNRPC_SWAP */
