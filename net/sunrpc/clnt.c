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
 *  NB: BSD uses a more intelligent approach to guessing when a request
 *  or reply has been lost by keeping the RTO estimate for each procedure.
 *  We currently make do with a constant timeout value.
 *
 *  Copyright (C) 1992,1993 Rick Sladkey <jrs@world.std.com>
 *  Copyright (C) 1995,1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <asm/system.h>

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/utsname.h>
#include <linux/workqueue.h>
#include <linux/in6.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/metrics.h>


#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_CALL
#endif

#define dprint_status(t)					\
	dprintk("RPC: %5u %s (status %d)\n", t->tk_pid,		\
			__FUNCTION__, t->tk_status)

/*
 * All RPC clients are linked into this list
 */
static LIST_HEAD(all_clients);
static DEFINE_SPINLOCK(rpc_client_lock);

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
static void	call_timeout(struct rpc_task *task);
static void	call_connect(struct rpc_task *task);
static void	call_connect_status(struct rpc_task *task);
static __be32 *	call_header(struct rpc_task *task);
static __be32 *	call_verify(struct rpc_task *task);

static int	rpc_ping(struct rpc_clnt *clnt, int flags);

static void rpc_register_client(struct rpc_clnt *clnt)
{
	spin_lock(&rpc_client_lock);
	list_add(&clnt->cl_clients, &all_clients);
	spin_unlock(&rpc_client_lock);
}

static void rpc_unregister_client(struct rpc_clnt *clnt)
{
	spin_lock(&rpc_client_lock);
	list_del(&clnt->cl_clients);
	spin_unlock(&rpc_client_lock);
}

static int
rpc_setup_pipedir(struct rpc_clnt *clnt, char *dir_name)
{
	static uint32_t clntid;
	int error;

	clnt->cl_vfsmnt = ERR_PTR(-ENOENT);
	clnt->cl_dentry = ERR_PTR(-ENOENT);
	if (dir_name == NULL)
		return 0;

	clnt->cl_vfsmnt = rpc_get_mount();
	if (IS_ERR(clnt->cl_vfsmnt))
		return PTR_ERR(clnt->cl_vfsmnt);

	for (;;) {
		snprintf(clnt->cl_pathname, sizeof(clnt->cl_pathname),
				"%s/clnt%x", dir_name,
				(unsigned int)clntid++);
		clnt->cl_pathname[sizeof(clnt->cl_pathname) - 1] = '\0';
		clnt->cl_dentry = rpc_mkdir(clnt->cl_pathname, clnt);
		if (!IS_ERR(clnt->cl_dentry))
			return 0;
		error = PTR_ERR(clnt->cl_dentry);
		if (error != -EEXIST) {
			printk(KERN_INFO "RPC: Couldn't create pipefs entry %s, error %d\n",
					clnt->cl_pathname, error);
			rpc_put_mount();
			return error;
		}
	}
}

static struct rpc_clnt * rpc_new_client(const struct rpc_create_args *args, struct rpc_xprt *xprt)
{
	struct rpc_program	*program = args->program;
	struct rpc_version	*version;
	struct rpc_clnt		*clnt = NULL;
	struct rpc_auth		*auth;
	int err;
	size_t len;

	/* sanity check the name before trying to print it */
	err = -EINVAL;
	len = strlen(args->servername);
	if (len > RPC_MAXNETNAMELEN)
		goto out_no_rpciod;
	len++;

	dprintk("RPC:       creating %s client for %s (xprt %p)\n",
			program->name, args->servername, xprt);

	err = rpciod_up();
	if (err)
		goto out_no_rpciod;
	err = -EINVAL;
	if (!xprt)
		goto out_no_xprt;

	if (args->version >= program->nrvers)
		goto out_err;
	version = program->version[args->version];
	if (version == NULL)
		goto out_err;

	err = -ENOMEM;
	clnt = kzalloc(sizeof(*clnt), GFP_KERNEL);
	if (!clnt)
		goto out_err;
	clnt->cl_parent = clnt;

	clnt->cl_server = clnt->cl_inline_name;
	if (len > sizeof(clnt->cl_inline_name)) {
		char *buf = kmalloc(len, GFP_KERNEL);
		if (buf != NULL)
			clnt->cl_server = buf;
		else
			len = sizeof(clnt->cl_inline_name);
	}
	strlcpy(clnt->cl_server, args->servername, len);

	clnt->cl_xprt     = xprt;
	clnt->cl_procinfo = version->procs;
	clnt->cl_maxproc  = version->nrprocs;
	clnt->cl_protname = program->name;
	clnt->cl_prog     = program->number;
	clnt->cl_vers     = version->number;
	clnt->cl_stats    = program->stats;
	clnt->cl_metrics  = rpc_alloc_iostats(clnt);
	err = -ENOMEM;
	if (clnt->cl_metrics == NULL)
		goto out_no_stats;
	clnt->cl_program  = program;
	INIT_LIST_HEAD(&clnt->cl_tasks);
	spin_lock_init(&clnt->cl_lock);

	if (!xprt_bound(clnt->cl_xprt))
		clnt->cl_autobind = 1;

	clnt->cl_timeout = xprt->timeout;
	if (args->timeout != NULL) {
		memcpy(&clnt->cl_timeout_default, args->timeout,
				sizeof(clnt->cl_timeout_default));
		clnt->cl_timeout = &clnt->cl_timeout_default;
	}

	clnt->cl_rtt = &clnt->cl_rtt_default;
	rpc_init_rtt(&clnt->cl_rtt_default, clnt->cl_timeout->to_initval);

	kref_init(&clnt->cl_kref);

	err = rpc_setup_pipedir(clnt, program->pipe_dir_name);
	if (err < 0)
		goto out_no_path;

	auth = rpcauth_create(args->authflavor, clnt);
	if (IS_ERR(auth)) {
		printk(KERN_INFO "RPC: Couldn't create auth handle (flavor %u)\n",
				args->authflavor);
		err = PTR_ERR(auth);
		goto out_no_auth;
	}

	/* save the nodename */
	clnt->cl_nodelen = strlen(utsname()->nodename);
	if (clnt->cl_nodelen > UNX_MAXNODENAME)
		clnt->cl_nodelen = UNX_MAXNODENAME;
	memcpy(clnt->cl_nodename, utsname()->nodename, clnt->cl_nodelen);
	rpc_register_client(clnt);
	return clnt;

out_no_auth:
	if (!IS_ERR(clnt->cl_dentry)) {
		rpc_rmdir(clnt->cl_dentry);
		rpc_put_mount();
	}
out_no_path:
	rpc_free_iostats(clnt->cl_metrics);
out_no_stats:
	if (clnt->cl_server != clnt->cl_inline_name)
		kfree(clnt->cl_server);
	kfree(clnt);
out_err:
	xprt_put(xprt);
out_no_xprt:
	rpciod_down();
out_no_rpciod:
	return ERR_PTR(err);
}

/*
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
	struct rpc_clnt *clnt;
	struct xprt_create xprtargs = {
		.ident = args->protocol,
		.srcaddr = args->saddress,
		.dstaddr = args->address,
		.addrlen = args->addrsize,
	};
	char servername[48];

	xprt = xprt_create_transport(&xprtargs);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;

	/*
	 * If the caller chooses not to specify a hostname, whip
	 * up a string representation of the passed-in address.
	 */
	if (args->servername == NULL) {
		servername[0] = '\0';
		switch (args->address->sa_family) {
		case AF_INET: {
			struct sockaddr_in *sin =
					(struct sockaddr_in *)args->address;
			snprintf(servername, sizeof(servername), NIPQUAD_FMT,
				 NIPQUAD(sin->sin_addr.s_addr));
			break;
		}
		case AF_INET6: {
			struct sockaddr_in6 *sin =
					(struct sockaddr_in6 *)args->address;
			snprintf(servername, sizeof(servername), NIP6_FMT,
				 NIP6(sin->sin6_addr));
			break;
		}
		default:
			/* caller wants default server name, but
			 * address family isn't recognized. */
			return ERR_PTR(-EINVAL);
		}
		args->servername = servername;
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

	clnt = rpc_new_client(args, xprt);
	if (IS_ERR(clnt))
		return clnt;

	if (!(args->flags & RPC_CLNT_CREATE_NOPING)) {
		int err = rpc_ping(clnt, RPC_TASK_SOFT);
		if (err != 0) {
			rpc_shutdown_client(clnt);
			return ERR_PTR(err);
		}
	}

	clnt->cl_softrtry = 1;
	if (args->flags & RPC_CLNT_CREATE_HARDRTRY)
		clnt->cl_softrtry = 0;

	if (args->flags & RPC_CLNT_CREATE_AUTOBIND)
		clnt->cl_autobind = 1;
	if (args->flags & RPC_CLNT_CREATE_DISCRTRY)
		clnt->cl_discrtry = 1;

	return clnt;
}
EXPORT_SYMBOL_GPL(rpc_create);

/*
 * This function clones the RPC client structure. It allows us to share the
 * same transport while varying parameters such as the authentication
 * flavour.
 */
struct rpc_clnt *
rpc_clone_client(struct rpc_clnt *clnt)
{
	struct rpc_clnt *new;
	int err = -ENOMEM;

	new = kmemdup(clnt, sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out_no_clnt;
	new->cl_parent = clnt;
	/* Turn off autobind on clones */
	new->cl_autobind = 0;
	INIT_LIST_HEAD(&new->cl_tasks);
	spin_lock_init(&new->cl_lock);
	rpc_init_rtt(&new->cl_rtt_default, clnt->cl_timeout->to_initval);
	new->cl_metrics = rpc_alloc_iostats(clnt);
	if (new->cl_metrics == NULL)
		goto out_no_stats;
	kref_init(&new->cl_kref);
	err = rpc_setup_pipedir(new, clnt->cl_program->pipe_dir_name);
	if (err != 0)
		goto out_no_path;
	if (new->cl_auth)
		atomic_inc(&new->cl_auth->au_count);
	xprt_get(clnt->cl_xprt);
	kref_get(&clnt->cl_kref);
	rpc_register_client(new);
	rpciod_up();
	return new;
out_no_path:
	rpc_free_iostats(new->cl_metrics);
out_no_stats:
	kfree(new);
out_no_clnt:
	dprintk("RPC:       %s: returned error %d\n", __FUNCTION__, err);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(rpc_clone_client);

/*
 * Properly shut down an RPC client, terminating all outstanding
 * requests.
 */
void rpc_shutdown_client(struct rpc_clnt *clnt)
{
	dprintk("RPC:       shutting down %s client for %s\n",
			clnt->cl_protname, clnt->cl_server);

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
static void
rpc_free_client(struct kref *kref)
{
	struct rpc_clnt *clnt = container_of(kref, struct rpc_clnt, cl_kref);

	dprintk("RPC:       destroying %s client for %s\n",
			clnt->cl_protname, clnt->cl_server);
	if (!IS_ERR(clnt->cl_dentry)) {
		rpc_rmdir(clnt->cl_dentry);
		rpc_put_mount();
	}
	if (clnt->cl_parent != clnt) {
		rpc_release_client(clnt->cl_parent);
		goto out_free;
	}
	if (clnt->cl_server != clnt->cl_inline_name)
		kfree(clnt->cl_server);
out_free:
	rpc_unregister_client(clnt);
	rpc_free_iostats(clnt->cl_metrics);
	clnt->cl_metrics = NULL;
	xprt_put(clnt->cl_xprt);
	rpciod_down();
	kfree(clnt);
}

/*
 * Free an RPC client
 */
static void
rpc_free_auth(struct kref *kref)
{
	struct rpc_clnt *clnt = container_of(kref, struct rpc_clnt, cl_kref);

	if (clnt->cl_auth == NULL) {
		rpc_free_client(kref);
		return;
	}

	/*
	 * Note: RPCSEC_GSS may need to send NULL RPC calls in order to
	 *       release remaining GSS contexts. This mechanism ensures
	 *       that it can do so safely.
	 */
	kref_init(kref);
	rpcauth_release(clnt->cl_auth);
	clnt->cl_auth = NULL;
	kref_put(kref, rpc_free_client);
}

/*
 * Release reference to the RPC client
 */
void
rpc_release_client(struct rpc_clnt *clnt)
{
	dprintk("RPC:       rpc_release_client(%p)\n", clnt);

	if (list_empty(&clnt->cl_tasks))
		wake_up(&destroy_wait);
	kref_put(&clnt->cl_kref, rpc_free_auth);
}

/**
 * rpc_bind_new_program - bind a new RPC program to an existing client
 * @old - old rpc_client
 * @program - rpc program to set
 * @vers - rpc program version
 *
 * Clones the rpc client and sets up a new RPC program. This is mainly
 * of use for enabling different RPC programs to share the same transport.
 * The Sun NFSv2/v3 ACL protocol can do this.
 */
struct rpc_clnt *rpc_bind_new_program(struct rpc_clnt *old,
				      struct rpc_program *program,
				      u32 vers)
{
	struct rpc_clnt *clnt;
	struct rpc_version *version;
	int err;

	BUG_ON(vers >= program->nrvers || !program->version[vers]);
	version = program->version[vers];
	clnt = rpc_clone_client(old);
	if (IS_ERR(clnt))
		goto out;
	clnt->cl_procinfo = version->procs;
	clnt->cl_maxproc  = version->nrprocs;
	clnt->cl_protname = program->name;
	clnt->cl_prog     = program->number;
	clnt->cl_vers     = version->number;
	clnt->cl_stats    = program->stats;
	err = rpc_ping(clnt, RPC_TASK_SOFT);
	if (err != 0) {
		rpc_shutdown_client(clnt);
		clnt = ERR_PTR(err);
	}
out:
	return clnt;
}
EXPORT_SYMBOL_GPL(rpc_bind_new_program);

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
	struct rpc_task *task, *ret;

	task = rpc_new_task(task_setup_data);
	if (task == NULL) {
		rpc_release_calldata(task_setup_data->callback_ops,
				task_setup_data->callback_data);
		ret = ERR_PTR(-ENOMEM);
		goto out;
	}

	if (task->tk_status != 0) {
		ret = ERR_PTR(task->tk_status);
		rpc_put_task(task);
		goto out;
	}
	atomic_inc(&task->tk_count);
	rpc_execute(task);
	ret = task;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(rpc_run_task);

/**
 * rpc_call_sync - Perform a synchronous RPC call
 * @clnt: pointer to RPC client
 * @msg: RPC call parameters
 * @flags: RPC call flags
 */
int rpc_call_sync(struct rpc_clnt *clnt, struct rpc_message *msg, int flags)
{
	struct rpc_task	*task;
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_message = msg,
		.callback_ops = &rpc_default_ops,
		.flags = flags,
	};
	int status;

	BUG_ON(flags & RPC_TASK_ASYNC);

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
 * @ops: RPC call ops
 * @data: user call data
 */
int
rpc_call_async(struct rpc_clnt *clnt, struct rpc_message *msg, int flags,
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
 * @size: length of target buffer
 *
 * Returns the number of bytes that are actually in the stored address.
 */
size_t rpc_peeraddr(struct rpc_clnt *clnt, struct sockaddr *buf, size_t bufsize)
{
	size_t bytes;
	struct rpc_xprt *xprt = clnt->cl_xprt;

	bytes = sizeof(xprt->addr);
	if (bytes > bufsize)
		bytes = bufsize;
	memcpy(buf, &clnt->cl_xprt->addr, bytes);
	return xprt->addrlen;
}
EXPORT_SYMBOL_GPL(rpc_peeraddr);

/**
 * rpc_peeraddr2str - return remote peer address in printable format
 * @clnt: RPC client structure
 * @format: address format
 *
 */
const char *rpc_peeraddr2str(struct rpc_clnt *clnt,
			     enum rpc_display_format_t format)
{
	struct rpc_xprt *xprt = clnt->cl_xprt;

	if (xprt->address_strings[format] != NULL)
		return xprt->address_strings[format];
	else
		return "unprintable";
}
EXPORT_SYMBOL_GPL(rpc_peeraddr2str);

void
rpc_setbufsize(struct rpc_clnt *clnt, unsigned int sndsize, unsigned int rcvsize)
{
	struct rpc_xprt *xprt = clnt->cl_xprt;
	if (xprt->ops->set_buffer_size)
		xprt->ops->set_buffer_size(xprt, sndsize, rcvsize);
}
EXPORT_SYMBOL_GPL(rpc_setbufsize);

/*
 * Return size of largest payload RPC client can support, in bytes
 *
 * For stream transports, this is one RPC record fragment (see RFC
 * 1831), as we don't support multi-record requests yet.  For datagram
 * transports, this is the size of an IP packet minus the IP, UDP, and
 * RPC header sizes.
 */
size_t rpc_max_payload(struct rpc_clnt *clnt)
{
	return clnt->cl_xprt->max_payload;
}
EXPORT_SYMBOL_GPL(rpc_max_payload);

/**
 * rpc_force_rebind - force transport to check that remote port is unchanged
 * @clnt: client to rebind
 *
 */
void rpc_force_rebind(struct rpc_clnt *clnt)
{
	if (clnt->cl_autobind)
		xprt_clear_bound(clnt->cl_xprt);
}
EXPORT_SYMBOL_GPL(rpc_force_rebind);

/*
 * Restart an (async) RPC call. Usually called from within the
 * exit handler.
 */
void
rpc_restart_call(struct rpc_task *task)
{
	if (RPC_ASSASSINATED(task))
		return;

	task->tk_action = call_start;
}
EXPORT_SYMBOL_GPL(rpc_restart_call);

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

	dprintk("RPC: %5u call_start %s%d proc %d (%s)\n", task->tk_pid,
			clnt->cl_protname, clnt->cl_vers,
			task->tk_msg.rpc_proc->p_proc,
			(RPC_IS_ASYNC(task) ? "async" : "sync"));

	/* Increment call count */
	task->tk_msg.rpc_proc->p_count++;
	clnt->cl_stats->rpccnt++;
	task->tk_action = call_reserve;
}

/*
 * 1.	Reserve an RPC call slot
 */
static void
call_reserve(struct rpc_task *task)
{
	dprint_status(task);

	if (!rpcauth_uptodatecred(task)) {
		task->tk_action = call_refresh;
		return;
	}

	task->tk_status  = 0;
	task->tk_action  = call_reserveresult;
	xprt_reserve(task);
}

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
			task->tk_action = call_allocate;
			return;
		}

		printk(KERN_ERR "%s: status=%d, but no request slot, exiting\n",
				__FUNCTION__, status);
		rpc_exit(task, -EIO);
		return;
	}

	/*
	 * Even though there was an error, we may have acquired
	 * a request slot somehow.  Make sure not to leak it.
	 */
	if (task->tk_rqstp) {
		printk(KERN_ERR "%s: status=%d, request allocated anyway\n",
				__FUNCTION__, status);
		xprt_release(task);
	}

	switch (status) {
	case -EAGAIN:	/* woken up; retry */
		task->tk_action = call_reserve;
		return;
	case -EIO:	/* probably a shutdown */
		break;
	default:
		printk(KERN_ERR "%s: unrecognized error %d, exiting\n",
				__FUNCTION__, status);
		break;
	}
	rpc_exit(task, status);
}

/*
 * 2.	Allocate the buffer. For details, see sched.c:rpc_malloc.
 *	(Note: buffer memory is freed in xprt_release).
 */
static void
call_allocate(struct rpc_task *task)
{
	unsigned int slack = task->tk_msg.rpc_cred->cr_auth->au_cslack;
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = task->tk_xprt;
	struct rpc_procinfo *proc = task->tk_msg.rpc_proc;

	dprint_status(task);

	task->tk_status = 0;
	task->tk_action = call_bind;

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
	req->rq_callsize = RPC_CALLHDRSIZE + (slack << 1) + proc->p_arglen;
	req->rq_callsize <<= 2;
	req->rq_rcvsize = RPC_REPHDRSIZE + slack + proc->p_replen;
	req->rq_rcvsize <<= 2;

	req->rq_buffer = xprt->ops->buf_alloc(task,
					req->rq_callsize + req->rq_rcvsize);
	if (req->rq_buffer != NULL)
		return;

	dprintk("RPC: %5u rpc_buffer allocation failed\n", task->tk_pid);

	if (RPC_IS_ASYNC(task) || !signalled()) {
		task->tk_action = call_allocate;
		rpc_delay(task, HZ>>4);
		return;
	}

	rpc_exit(task, -ERESTARTSYS);
}

static inline int
rpc_task_need_encode(struct rpc_task *task)
{
	return task->tk_rqstp->rq_snd_buf.len == 0;
}

static inline void
rpc_task_force_reencode(struct rpc_task *task)
{
	task->tk_rqstp->rq_snd_buf.len = 0;
}

static inline void
rpc_xdr_buf_init(struct xdr_buf *buf, void *start, size_t len)
{
	buf->head[0].iov_base = start;
	buf->head[0].iov_len = len;
	buf->tail[0].iov_len = 0;
	buf->page_len = 0;
	buf->flags = 0;
	buf->len = 0;
	buf->buflen = len;
}

/*
 * 3.	Encode arguments of an RPC call
 */
static void
call_encode(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	kxdrproc_t	encode;
	__be32		*p;

	dprint_status(task);

	rpc_xdr_buf_init(&req->rq_snd_buf,
			 req->rq_buffer,
			 req->rq_callsize);
	rpc_xdr_buf_init(&req->rq_rcv_buf,
			 (char *)req->rq_buffer + req->rq_callsize,
			 req->rq_rcvsize);

	/* Encode header and provided arguments */
	encode = task->tk_msg.rpc_proc->p_encode;
	if (!(p = call_header(task))) {
		printk(KERN_INFO "RPC: call_header failed, exit EIO\n");
		rpc_exit(task, -EIO);
		return;
	}
	if (encode == NULL)
		return;

	task->tk_status = rpcauth_wrap_req(task, encode, req, p,
			task->tk_msg.rpc_argp);
	if (task->tk_status == -ENOMEM) {
		/* XXX: Is this sane? */
		rpc_delay(task, 3*HZ);
		task->tk_status = -EAGAIN;
	}
}

/*
 * 4.	Get the server port number if not yet set
 */
static void
call_bind(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	dprint_status(task);

	task->tk_action = call_connect;
	if (!xprt_bound(xprt)) {
		task->tk_action = call_bind_status;
		task->tk_timeout = xprt->bind_timeout;
		xprt->ops->rpcbind(task);
	}
}

/*
 * 4a.	Sort out bind result
 */
static void
call_bind_status(struct rpc_task *task)
{
	int status = -EIO;

	if (task->tk_status >= 0) {
		dprint_status(task);
		task->tk_status = 0;
		task->tk_action = call_connect;
		return;
	}

	switch (task->tk_status) {
	case -EAGAIN:
		dprintk("RPC: %5u rpcbind waiting for another request "
				"to finish\n", task->tk_pid);
		/* avoid busy-waiting here -- could be a network outage. */
		rpc_delay(task, 5*HZ);
		goto retry_timeout;
	case -EACCES:
		dprintk("RPC: %5u remote rpcbind: RPC program/version "
				"unavailable\n", task->tk_pid);
		/* fail immediately if this is an RPC ping */
		if (task->tk_msg.rpc_proc->p_proc == 0) {
			status = -EOPNOTSUPP;
			break;
		}
		rpc_delay(task, 3*HZ);
		goto retry_timeout;
	case -ETIMEDOUT:
		dprintk("RPC: %5u rpcbind request timed out\n",
				task->tk_pid);
		goto retry_timeout;
	case -EPFNOSUPPORT:
		/* server doesn't support any rpcbind version we know of */
		dprintk("RPC: %5u remote rpcbind service unavailable\n",
				task->tk_pid);
		break;
	case -EPROTONOSUPPORT:
		dprintk("RPC: %5u remote rpcbind version unavailable, retrying\n",
				task->tk_pid);
		task->tk_status = 0;
		task->tk_action = call_bind;
		return;
	default:
		dprintk("RPC: %5u unrecognized rpcbind error (%d)\n",
				task->tk_pid, -task->tk_status);
	}

	rpc_exit(task, status);
	return;

retry_timeout:
	task->tk_action = call_timeout;
}

/*
 * 4b.	Connect to the RPC server
 */
static void
call_connect(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	dprintk("RPC: %5u call_connect xprt %p %s connected\n",
			task->tk_pid, xprt,
			(xprt_connected(xprt) ? "is" : "is not"));

	task->tk_action = call_transmit;
	if (!xprt_connected(xprt)) {
		task->tk_action = call_connect_status;
		if (task->tk_status < 0)
			return;
		xprt_connect(task);
	}
}

/*
 * 4c.	Sort out connect result
 */
static void
call_connect_status(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	int status = task->tk_status;

	dprint_status(task);

	task->tk_status = 0;
	if (status >= 0) {
		clnt->cl_stats->netreconn++;
		task->tk_action = call_transmit;
		return;
	}

	/* Something failed: remote service port may have changed */
	rpc_force_rebind(clnt);

	switch (status) {
	case -ENOTCONN:
	case -EAGAIN:
		task->tk_action = call_bind;
		if (!RPC_IS_SOFT(task))
			return;
		/* if soft mounted, test if we've timed out */
	case -ETIMEDOUT:
		task->tk_action = call_timeout;
		return;
	}
	rpc_exit(task, -EIO);
}

/*
 * 5.	Transmit the RPC request, and wait for reply
 */
static void
call_transmit(struct rpc_task *task)
{
	dprint_status(task);

	task->tk_action = call_status;
	if (task->tk_status < 0)
		return;
	task->tk_status = xprt_prepare_transmit(task);
	if (task->tk_status != 0)
		return;
	task->tk_action = call_transmit_status;
	/* Encode here so that rpcsec_gss can use correct sequence number. */
	if (rpc_task_need_encode(task)) {
		BUG_ON(task->tk_rqstp->rq_bytes_sent != 0);
		call_encode(task);
		/* Did the encode result in an error condition? */
		if (task->tk_status != 0)
			return;
	}
	xprt_transmit(task);
	if (task->tk_status < 0)
		return;
	/*
	 * On success, ensure that we call xprt_end_transmit() before sleeping
	 * in order to allow access to the socket to other RPC requests.
	 */
	call_transmit_status(task);
	if (task->tk_msg.rpc_proc->p_decode != NULL)
		return;
	task->tk_action = rpc_exit_task;
	rpc_wake_up_task(task);
}

/*
 * 5a.	Handle cleanup after a transmission
 */
static void
call_transmit_status(struct rpc_task *task)
{
	task->tk_action = call_status;
	/*
	 * Special case: if we've been waiting on the socket's write_space()
	 * callback, then don't call xprt_end_transmit().
	 */
	if (task->tk_status == -EAGAIN)
		return;
	xprt_end_transmit(task);
	rpc_task_force_reencode(task);
}

/*
 * 6.	Sort out the RPC call status
 */
static void
call_status(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	int		status;

	if (req->rq_received > 0 && !req->rq_bytes_sent)
		task->tk_status = req->rq_received;

	dprint_status(task);

	status = task->tk_status;
	if (status >= 0) {
		task->tk_action = call_decode;
		return;
	}

	task->tk_status = 0;
	switch(status) {
	case -EHOSTDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
		/*
		 * Delay any retries for 3 seconds, then handle as if it
		 * were a timeout.
		 */
		rpc_delay(task, 3*HZ);
	case -ETIMEDOUT:
		task->tk_action = call_timeout;
		if (task->tk_client->cl_discrtry)
			xprt_force_disconnect(task->tk_xprt);
		break;
	case -ECONNREFUSED:
	case -ENOTCONN:
		rpc_force_rebind(clnt);
		task->tk_action = call_bind;
		break;
	case -EAGAIN:
		task->tk_action = call_transmit;
		break;
	case -EIO:
		/* shutdown or soft timeout */
		rpc_exit(task, status);
		break;
	default:
		printk("%s: RPC call returned error %d\n",
			       clnt->cl_protname, -status);
		rpc_exit(task, status);
	}
}

/*
 * 6a.	Handle RPC timeout
 * 	We do not release the request slot, so we keep using the
 *	same XID for all retransmits.
 */
static void
call_timeout(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;

	if (xprt_adjust_timeout(task->tk_rqstp) == 0) {
		dprintk("RPC: %5u call_timeout (minor)\n", task->tk_pid);
		goto retry;
	}

	dprintk("RPC: %5u call_timeout (major)\n", task->tk_pid);
	task->tk_timeouts++;

	if (RPC_IS_SOFT(task)) {
		printk(KERN_NOTICE "%s: server %s not responding, timed out\n",
				clnt->cl_protname, clnt->cl_server);
		rpc_exit(task, -EIO);
		return;
	}

	if (!(task->tk_flags & RPC_CALL_MAJORSEEN)) {
		task->tk_flags |= RPC_CALL_MAJORSEEN;
		printk(KERN_NOTICE "%s: server %s not responding, still trying\n",
			clnt->cl_protname, clnt->cl_server);
	}
	rpc_force_rebind(clnt);

retry:
	clnt->cl_stats->rpcretrans++;
	task->tk_action = call_bind;
	task->tk_status = 0;
}

/*
 * 7.	Decode the RPC reply
 */
static void
call_decode(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	kxdrproc_t	decode = task->tk_msg.rpc_proc->p_decode;
	__be32		*p;

	dprintk("RPC: %5u call_decode (status %d)\n",
			task->tk_pid, task->tk_status);

	if (task->tk_flags & RPC_CALL_MAJORSEEN) {
		printk(KERN_NOTICE "%s: server %s OK\n",
			clnt->cl_protname, clnt->cl_server);
		task->tk_flags &= ~RPC_CALL_MAJORSEEN;
	}

	if (task->tk_status < 12) {
		if (!RPC_IS_SOFT(task)) {
			task->tk_action = call_bind;
			clnt->cl_stats->rpcretrans++;
			goto out_retry;
		}
		dprintk("RPC:       %s: too small RPC reply size (%d bytes)\n",
				clnt->cl_protname, task->tk_status);
		task->tk_action = call_timeout;
		goto out_retry;
	}

	/*
	 * Ensure that we see all writes made by xprt_complete_rqst()
	 * before it changed req->rq_received.
	 */
	smp_rmb();
	req->rq_rcv_buf.len = req->rq_private_buf.len;

	/* Check that the softirq receive buffer is valid */
	WARN_ON(memcmp(&req->rq_rcv_buf, &req->rq_private_buf,
				sizeof(req->rq_rcv_buf)) != 0);

	/* Verify the RPC header */
	p = call_verify(task);
	if (IS_ERR(p)) {
		if (p == ERR_PTR(-EAGAIN))
			goto out_retry;
		return;
	}

	task->tk_action = rpc_exit_task;

	if (decode) {
		task->tk_status = rpcauth_unwrap_resp(task, decode, req, p,
						      task->tk_msg.rpc_resp);
	}
	dprintk("RPC: %5u call_decode result %d\n", task->tk_pid,
			task->tk_status);
	return;
out_retry:
	req->rq_received = req->rq_private_buf.len = 0;
	task->tk_status = 0;
	if (task->tk_client->cl_discrtry)
		xprt_force_disconnect(task->tk_xprt);
}

/*
 * 8.	Refresh the credentials if rejected by the server
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
 * 8a.	Process the results of a credential refresh
 */
static void
call_refreshresult(struct rpc_task *task)
{
	int status = task->tk_status;

	dprint_status(task);

	task->tk_status = 0;
	task->tk_action = call_reserve;
	if (status >= 0 && rpcauth_uptodatecred(task))
		return;
	if (status == -EACCES) {
		rpc_exit(task, -EACCES);
		return;
	}
	task->tk_action = call_refresh;
	if (status != -ETIMEDOUT)
		rpc_delay(task, 3*HZ);
	return;
}

/*
 * Call header serialization
 */
static __be32 *
call_header(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	__be32		*p = req->rq_svec[0].iov_base;

	/* FIXME: check buffer size? */

	p = xprt_skip_transport_header(task->tk_xprt, p);
	*p++ = req->rq_xid;		/* XID */
	*p++ = htonl(RPC_CALL);		/* CALL */
	*p++ = htonl(RPC_VERSION);	/* RPC version */
	*p++ = htonl(clnt->cl_prog);	/* program number */
	*p++ = htonl(clnt->cl_vers);	/* program version */
	*p++ = htonl(task->tk_msg.rpc_proc->p_proc);	/* procedure */
	p = rpcauth_marshcred(task, p);
	req->rq_slen = xdr_adjust_iovec(&req->rq_svec[0], p);
	return p;
}

/*
 * Reply header verification
 */
static __be32 *
call_verify(struct rpc_task *task)
{
	struct kvec *iov = &task->tk_rqstp->rq_rcv_buf.head[0];
	int len = task->tk_rqstp->rq_rcv_buf.len >> 2;
	__be32	*p = iov->iov_base;
	u32 n;
	int error = -EACCES;

	if ((task->tk_rqstp->rq_rcv_buf.len & 3) != 0) {
		/* RFC-1014 says that the representation of XDR data must be a
		 * multiple of four bytes
		 * - if it isn't pointer subtraction in the NFS client may give
		 *   undefined results
		 */
		dprintk("RPC: %5u %s: XDR representation not a multiple of"
		       " 4 bytes: 0x%x\n", task->tk_pid, __FUNCTION__,
		       task->tk_rqstp->rq_rcv_buf.len);
		goto out_eio;
	}
	if ((len -= 3) < 0)
		goto out_overflow;
	p += 1;	/* skip XID */

	if ((n = ntohl(*p++)) != RPC_REPLY) {
		dprintk("RPC: %5u %s: not an RPC reply: %x\n",
				task->tk_pid, __FUNCTION__, n);
		goto out_garbage;
	}
	if ((n = ntohl(*p++)) != RPC_MSG_ACCEPTED) {
		if (--len < 0)
			goto out_overflow;
		switch ((n = ntohl(*p++))) {
			case RPC_AUTH_ERROR:
				break;
			case RPC_MISMATCH:
				dprintk("RPC: %5u %s: RPC call version "
						"mismatch!\n",
						task->tk_pid, __FUNCTION__);
				error = -EPROTONOSUPPORT;
				goto out_err;
			default:
				dprintk("RPC: %5u %s: RPC call rejected, "
						"unknown error: %x\n",
						task->tk_pid, __FUNCTION__, n);
				goto out_eio;
		}
		if (--len < 0)
			goto out_overflow;
		switch ((n = ntohl(*p++))) {
		case RPC_AUTH_REJECTEDCRED:
		case RPC_AUTH_REJECTEDVERF:
		case RPCSEC_GSS_CREDPROBLEM:
		case RPCSEC_GSS_CTXPROBLEM:
			if (!task->tk_cred_retry)
				break;
			task->tk_cred_retry--;
			dprintk("RPC: %5u %s: retry stale creds\n",
					task->tk_pid, __FUNCTION__);
			rpcauth_invalcred(task);
			/* Ensure we obtain a new XID! */
			xprt_release(task);
			task->tk_action = call_refresh;
			goto out_retry;
		case RPC_AUTH_BADCRED:
		case RPC_AUTH_BADVERF:
			/* possibly garbled cred/verf? */
			if (!task->tk_garb_retry)
				break;
			task->tk_garb_retry--;
			dprintk("RPC: %5u %s: retry garbled creds\n",
					task->tk_pid, __FUNCTION__);
			task->tk_action = call_bind;
			goto out_retry;
		case RPC_AUTH_TOOWEAK:
			printk(KERN_NOTICE "call_verify: server %s requires stronger "
			       "authentication.\n", task->tk_client->cl_server);
			break;
		default:
			dprintk("RPC: %5u %s: unknown auth error: %x\n",
					task->tk_pid, __FUNCTION__, n);
			error = -EIO;
		}
		dprintk("RPC: %5u %s: call rejected %d\n",
				task->tk_pid, __FUNCTION__, n);
		goto out_err;
	}
	if (!(p = rpcauth_checkverf(task, p))) {
		dprintk("RPC: %5u %s: auth check failed\n",
				task->tk_pid, __FUNCTION__);
		goto out_garbage;		/* bad verifier, retry */
	}
	len = p - (__be32 *)iov->iov_base - 1;
	if (len < 0)
		goto out_overflow;
	switch ((n = ntohl(*p++))) {
	case RPC_SUCCESS:
		return p;
	case RPC_PROG_UNAVAIL:
		dprintk("RPC: %5u %s: program %u is unsupported by server %s\n",
				task->tk_pid, __FUNCTION__,
				(unsigned int)task->tk_client->cl_prog,
				task->tk_client->cl_server);
		error = -EPFNOSUPPORT;
		goto out_err;
	case RPC_PROG_MISMATCH:
		dprintk("RPC: %5u %s: program %u, version %u unsupported by "
				"server %s\n", task->tk_pid, __FUNCTION__,
				(unsigned int)task->tk_client->cl_prog,
				(unsigned int)task->tk_client->cl_vers,
				task->tk_client->cl_server);
		error = -EPROTONOSUPPORT;
		goto out_err;
	case RPC_PROC_UNAVAIL:
		dprintk("RPC: %5u %s: proc %p unsupported by program %u, "
				"version %u on server %s\n",
				task->tk_pid, __FUNCTION__,
				task->tk_msg.rpc_proc,
				task->tk_client->cl_prog,
				task->tk_client->cl_vers,
				task->tk_client->cl_server);
		error = -EOPNOTSUPP;
		goto out_err;
	case RPC_GARBAGE_ARGS:
		dprintk("RPC: %5u %s: server saw garbage\n",
				task->tk_pid, __FUNCTION__);
		break;			/* retry */
	default:
		dprintk("RPC: %5u %s: server accept status: %x\n",
				task->tk_pid, __FUNCTION__, n);
		/* Also retry */
	}

out_garbage:
	task->tk_client->cl_stats->rpcgarbage++;
	if (task->tk_garb_retry) {
		task->tk_garb_retry--;
		dprintk("RPC: %5u %s: retrying\n",
				task->tk_pid, __FUNCTION__);
		task->tk_action = call_bind;
out_retry:
		return ERR_PTR(-EAGAIN);
	}
out_eio:
	error = -EIO;
out_err:
	rpc_exit(task, error);
	dprintk("RPC: %5u %s: call failed with error %d\n", task->tk_pid,
			__FUNCTION__, error);
	return ERR_PTR(error);
out_overflow:
	dprintk("RPC: %5u %s: server reply was truncated.\n", task->tk_pid,
			__FUNCTION__);
	goto out_garbage;
}

static int rpcproc_encode_null(void *rqstp, __be32 *data, void *obj)
{
	return 0;
}

static int rpcproc_decode_null(void *rqstp, __be32 *data, void *obj)
{
	return 0;
}

static struct rpc_procinfo rpcproc_null = {
	.p_encode = rpcproc_encode_null,
	.p_decode = rpcproc_decode_null,
};

static int rpc_ping(struct rpc_clnt *clnt, int flags)
{
	struct rpc_message msg = {
		.rpc_proc = &rpcproc_null,
	};
	int err;
	msg.rpc_cred = authnull_ops.lookup_cred(NULL, NULL, 0);
	err = rpc_call_sync(clnt, &msg, flags);
	put_rpccred(msg.rpc_cred);
	return err;
}

struct rpc_task *rpc_call_null(struct rpc_clnt *clnt, struct rpc_cred *cred, int flags)
{
	struct rpc_message msg = {
		.rpc_proc = &rpcproc_null,
		.rpc_cred = cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.rpc_message = &msg,
		.callback_ops = &rpc_default_ops,
		.flags = flags,
	};
	return rpc_run_task(&task_setup_data);
}
EXPORT_SYMBOL_GPL(rpc_call_null);

#ifdef RPC_DEBUG
void rpc_show_tasks(void)
{
	struct rpc_clnt *clnt;
	struct rpc_task *t;

	spin_lock(&rpc_client_lock);
	if (list_empty(&all_clients))
		goto out;
	printk("-pid- proc flgs status -client- -prog- --rqstp- -timeout "
		"-rpcwait -action- ---ops--\n");
	list_for_each_entry(clnt, &all_clients, cl_clients) {
		if (list_empty(&clnt->cl_tasks))
			continue;
		spin_lock(&clnt->cl_lock);
		list_for_each_entry(t, &clnt->cl_tasks, tk_task) {
			const char *rpc_waitq = "none";
			int proc;

			if (t->tk_msg.rpc_proc)
				proc = t->tk_msg.rpc_proc->p_proc;
			else
				proc = -1;

			if (RPC_IS_QUEUED(t))
				rpc_waitq = rpc_qname(t->u.tk_wait.rpc_waitq);

			printk("%5u %04d %04x %6d %8p %6d %8p %8ld %8s %8p %8p\n",
				t->tk_pid, proc,
				t->tk_flags, t->tk_status,
				t->tk_client,
				(t->tk_client ? t->tk_client->cl_prog : 0),
				t->tk_rqstp, t->tk_timeout,
				rpc_waitq,
				t->tk_action, t->tk_ops);
		}
		spin_unlock(&clnt->cl_lock);
	}
out:
	spin_unlock(&rpc_client_lock);
}
#endif
