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
#include <linux/utsname.h>
#include <linux/workqueue.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/metrics.h>


#define RPC_SLACK_SPACE		(1024)	/* total overkill */

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_CALL
#endif

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

static struct rpc_clnt * rpc_new_client(struct rpc_xprt *xprt, char *servname, struct rpc_program *program, u32 vers, rpc_authflavor_t flavor)
{
	struct rpc_version	*version;
	struct rpc_clnt		*clnt = NULL;
	struct rpc_auth		*auth;
	int err;
	int len;

	dprintk("RPC: creating %s client for %s (xprt %p)\n",
		program->name, servname, xprt);

	err = -EINVAL;
	if (!xprt)
		goto out_no_xprt;
	if (vers >= program->nrvers || !(version = program->version[vers]))
		goto out_err;

	err = -ENOMEM;
	clnt = kzalloc(sizeof(*clnt), GFP_KERNEL);
	if (!clnt)
		goto out_err;
	atomic_set(&clnt->cl_users, 0);
	atomic_set(&clnt->cl_count, 1);
	clnt->cl_parent = clnt;

	clnt->cl_server = clnt->cl_inline_name;
	len = strlen(servname) + 1;
	if (len > sizeof(clnt->cl_inline_name)) {
		char *buf = kmalloc(len, GFP_KERNEL);
		if (buf != 0)
			clnt->cl_server = buf;
		else
			len = sizeof(clnt->cl_inline_name);
	}
	strlcpy(clnt->cl_server, servname, len);

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

	if (!xprt_bound(clnt->cl_xprt))
		clnt->cl_autobind = 1;

	clnt->cl_rtt = &clnt->cl_rtt_default;
	rpc_init_rtt(&clnt->cl_rtt_default, xprt->timeout.to_initval);

	err = rpc_setup_pipedir(clnt, program->pipe_dir_name);
	if (err < 0)
		goto out_no_path;

	auth = rpcauth_create(flavor, clnt);
	if (IS_ERR(auth)) {
		printk(KERN_INFO "RPC: Couldn't create auth handle (flavor %u)\n",
				flavor);
		err = PTR_ERR(auth);
		goto out_no_auth;
	}

	/* save the nodename */
	clnt->cl_nodelen = strlen(utsname()->nodename);
	if (clnt->cl_nodelen > UNX_MAXNODENAME)
		clnt->cl_nodelen = UNX_MAXNODENAME;
	memcpy(clnt->cl_nodename, utsname()->nodename, clnt->cl_nodelen);
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

	xprt = xprt_create_transport(args->protocol, args->address,
					args->addrsize, args->timeout);
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

	dprintk("RPC:       creating %s client for %s (xprt %p)\n",
		args->program->name, args->servername, xprt);

	clnt = rpc_new_client(xprt, args->servername, args->program,
				args->version, args->authflavor);
	if (IS_ERR(clnt))
		return clnt;

	if (!(args->flags & RPC_CLNT_CREATE_NOPING)) {
		int err = rpc_ping(clnt, RPC_TASK_SOFT|RPC_TASK_NOINTR);
		if (err != 0) {
			rpc_shutdown_client(clnt);
			return ERR_PTR(err);
		}
	}

	clnt->cl_softrtry = 1;
	if (args->flags & RPC_CLNT_CREATE_HARDRTRY)
		clnt->cl_softrtry = 0;

	if (args->flags & RPC_CLNT_CREATE_INTR)
		clnt->cl_intr = 1;
	if (args->flags & RPC_CLNT_CREATE_AUTOBIND)
		clnt->cl_autobind = 1;
	if (args->flags & RPC_CLNT_CREATE_ONESHOT)
		clnt->cl_oneshot = 1;

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
	atomic_set(&new->cl_count, 1);
	atomic_set(&new->cl_users, 0);
	new->cl_metrics = rpc_alloc_iostats(clnt);
	if (new->cl_metrics == NULL)
		goto out_no_stats;
	err = rpc_setup_pipedir(new, clnt->cl_program->pipe_dir_name);
	if (err != 0)
		goto out_no_path;
	new->cl_parent = clnt;
	atomic_inc(&clnt->cl_count);
	new->cl_xprt = xprt_get(clnt->cl_xprt);
	/* Turn off autobind on clones */
	new->cl_autobind = 0;
	new->cl_oneshot = 0;
	new->cl_dead = 0;
	rpc_init_rtt(&new->cl_rtt_default, clnt->cl_xprt->timeout.to_initval);
	if (new->cl_auth)
		atomic_inc(&new->cl_auth->au_count);
	return new;
out_no_path:
	rpc_free_iostats(new->cl_metrics);
out_no_stats:
	kfree(new);
out_no_clnt:
	dprintk("RPC: %s returned error %d\n", __FUNCTION__, err);
	return ERR_PTR(err);
}

/*
 * Properly shut down an RPC client, terminating all outstanding
 * requests. Note that we must be certain that cl_oneshot and
 * cl_dead are cleared, or else the client would be destroyed
 * when the last task releases it.
 */
int
rpc_shutdown_client(struct rpc_clnt *clnt)
{
	dprintk("RPC: shutting down %s client for %s, tasks=%d\n",
			clnt->cl_protname, clnt->cl_server,
			atomic_read(&clnt->cl_users));

	while (atomic_read(&clnt->cl_users) > 0) {
		/* Don't let rpc_release_client destroy us */
		clnt->cl_oneshot = 0;
		clnt->cl_dead = 0;
		rpc_killall_tasks(clnt);
		wait_event_timeout(destroy_wait,
			!atomic_read(&clnt->cl_users), 1*HZ);
	}

	if (atomic_read(&clnt->cl_users) < 0) {
		printk(KERN_ERR "RPC: rpc_shutdown_client clnt %p tasks=%d\n",
				clnt, atomic_read(&clnt->cl_users));
#ifdef RPC_DEBUG
		rpc_show_tasks();
#endif
		BUG();
	}

	return rpc_destroy_client(clnt);
}

/*
 * Delete an RPC client
 */
int
rpc_destroy_client(struct rpc_clnt *clnt)
{
	if (!atomic_dec_and_test(&clnt->cl_count))
		return 1;
	BUG_ON(atomic_read(&clnt->cl_users) != 0);

	dprintk("RPC: destroying %s client for %s\n",
			clnt->cl_protname, clnt->cl_server);
	if (clnt->cl_auth) {
		rpcauth_destroy(clnt->cl_auth);
		clnt->cl_auth = NULL;
	}
	if (!IS_ERR(clnt->cl_dentry)) {
		rpc_rmdir(clnt->cl_dentry);
		rpc_put_mount();
	}
	if (clnt->cl_parent != clnt) {
		rpc_destroy_client(clnt->cl_parent);
		goto out_free;
	}
	if (clnt->cl_server != clnt->cl_inline_name)
		kfree(clnt->cl_server);
out_free:
	rpc_free_iostats(clnt->cl_metrics);
	clnt->cl_metrics = NULL;
	xprt_put(clnt->cl_xprt);
	kfree(clnt);
	return 0;
}

/*
 * Release an RPC client
 */
void
rpc_release_client(struct rpc_clnt *clnt)
{
	dprintk("RPC:      rpc_release_client(%p, %d)\n",
				clnt, atomic_read(&clnt->cl_users));

	if (!atomic_dec_and_test(&clnt->cl_users))
		return;
	wake_up(&destroy_wait);
	if (clnt->cl_oneshot || clnt->cl_dead)
		rpc_destroy_client(clnt);
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
				      int vers)
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
	err = rpc_ping(clnt, RPC_TASK_SOFT|RPC_TASK_NOINTR);
	if (err != 0) {
		rpc_shutdown_client(clnt);
		clnt = ERR_PTR(err);
	}
out:	
	return clnt;
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

/*
 *	Export the signal mask handling for synchronous code that
 *	sleeps on RPC calls
 */
#define RPC_INTR_SIGNALS (sigmask(SIGHUP) | sigmask(SIGINT) | sigmask(SIGQUIT) | sigmask(SIGTERM))
 
static void rpc_save_sigmask(sigset_t *oldset, int intr)
{
	unsigned long	sigallow = sigmask(SIGKILL);
	sigset_t sigmask;

	/* Block all signals except those listed in sigallow */
	if (intr)
		sigallow |= RPC_INTR_SIGNALS;
	siginitsetinv(&sigmask, sigallow);
	sigprocmask(SIG_BLOCK, &sigmask, oldset);
}

static inline void rpc_task_sigmask(struct rpc_task *task, sigset_t *oldset)
{
	rpc_save_sigmask(oldset, !RPC_TASK_UNINTERRUPTIBLE(task));
}

static inline void rpc_restore_sigmask(sigset_t *oldset)
{
	sigprocmask(SIG_SETMASK, oldset, NULL);
}

void rpc_clnt_sigmask(struct rpc_clnt *clnt, sigset_t *oldset)
{
	rpc_save_sigmask(oldset, clnt->cl_intr);
}

void rpc_clnt_sigunmask(struct rpc_clnt *clnt, sigset_t *oldset)
{
	rpc_restore_sigmask(oldset);
}

/*
 * New rpc_call implementation
 */
int rpc_call_sync(struct rpc_clnt *clnt, struct rpc_message *msg, int flags)
{
	struct rpc_task	*task;
	sigset_t	oldset;
	int		status;

	/* If this client is slain all further I/O fails */
	if (clnt->cl_dead) 
		return -EIO;

	BUG_ON(flags & RPC_TASK_ASYNC);

	task = rpc_new_task(clnt, flags, &rpc_default_ops, NULL);
	if (task == NULL)
		return -ENOMEM;

	/* Mask signals on RPC calls _and_ GSS_AUTH upcalls */
	rpc_task_sigmask(task, &oldset);

	rpc_call_setup(task, msg, 0);

	/* Set up the call info struct and execute the task */
	status = task->tk_status;
	if (status != 0) {
		rpc_release_task(task);
		goto out;
	}
	atomic_inc(&task->tk_count);
	status = rpc_execute(task);
	if (status == 0)
		status = task->tk_status;
	rpc_put_task(task);
out:
	rpc_restore_sigmask(&oldset);
	return status;
}

/*
 * New rpc_call implementation
 */
int
rpc_call_async(struct rpc_clnt *clnt, struct rpc_message *msg, int flags,
	       const struct rpc_call_ops *tk_ops, void *data)
{
	struct rpc_task	*task;
	sigset_t	oldset;
	int		status;

	/* If this client is slain all further I/O fails */
	status = -EIO;
	if (clnt->cl_dead) 
		goto out_release;

	flags |= RPC_TASK_ASYNC;

	/* Create/initialize a new RPC task */
	status = -ENOMEM;
	if (!(task = rpc_new_task(clnt, flags, tk_ops, data)))
		goto out_release;

	/* Mask signals on GSS_AUTH upcalls */
	rpc_task_sigmask(task, &oldset);		

	rpc_call_setup(task, msg, 0);

	/* Set up the call info struct and execute the task */
	status = task->tk_status;
	if (status == 0)
		rpc_execute(task);
	else
		rpc_release_task(task);

	rpc_restore_sigmask(&oldset);		
	return status;
out_release:
	rpc_release_calldata(tk_ops, data);
	return status;
}


void
rpc_call_setup(struct rpc_task *task, struct rpc_message *msg, int flags)
{
	task->tk_msg   = *msg;
	task->tk_flags |= flags;
	/* Bind the user cred */
	if (task->tk_msg.rpc_cred != NULL)
		rpcauth_holdcred(task);
	else
		rpcauth_bindcred(task);

	if (task->tk_status == 0)
		task->tk_action = call_start;
	else
		task->tk_action = rpc_exit_task;
}

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
char *rpc_peeraddr2str(struct rpc_clnt *clnt, enum rpc_display_format_t format)
{
	struct rpc_xprt *xprt = clnt->cl_xprt;
	return xprt->ops->print_addr(xprt, format);
}
EXPORT_SYMBOL_GPL(rpc_peeraddr2str);

void
rpc_setbufsize(struct rpc_clnt *clnt, unsigned int sndsize, unsigned int rcvsize)
{
	struct rpc_xprt *xprt = clnt->cl_xprt;
	if (xprt->ops->set_buffer_size)
		xprt->ops->set_buffer_size(xprt, sndsize, rcvsize);
}

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

	dprintk("RPC: %4d call_start %s%d proc %d (%s)\n", task->tk_pid,
		clnt->cl_protname, clnt->cl_vers, task->tk_msg.rpc_proc->p_proc,
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
	dprintk("RPC: %4d call_reserve\n", task->tk_pid);

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

	dprintk("RPC: %4d call_reserveresult (status %d)\n",
				task->tk_pid, task->tk_status);

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
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = task->tk_xprt;
	unsigned int	bufsiz;

	dprintk("RPC: %4d call_allocate (status %d)\n", 
				task->tk_pid, task->tk_status);
	task->tk_action = call_bind;
	if (req->rq_buffer)
		return;

	/* FIXME: compute buffer requirements more exactly using
	 * auth->au_wslack */
	bufsiz = task->tk_msg.rpc_proc->p_bufsiz + RPC_SLACK_SPACE;

	if (xprt->ops->buf_alloc(task, bufsiz << 1) != NULL)
		return;
	printk(KERN_INFO "RPC: buffer allocation failed for task %p\n", task); 

	if (RPC_IS_ASYNC(task) || !signalled()) {
		xprt_release(task);
		task->tk_action = call_reserve;
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

/*
 * 3.	Encode arguments of an RPC call
 */
static void
call_encode(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct xdr_buf *sndbuf = &req->rq_snd_buf;
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	unsigned int	bufsiz;
	kxdrproc_t	encode;
	__be32		*p;

	dprintk("RPC: %4d call_encode (status %d)\n", 
				task->tk_pid, task->tk_status);

	/* Default buffer setup */
	bufsiz = req->rq_bufsize >> 1;
	sndbuf->head[0].iov_base = (void *)req->rq_buffer;
	sndbuf->head[0].iov_len  = bufsiz;
	sndbuf->tail[0].iov_len  = 0;
	sndbuf->page_len	 = 0;
	sndbuf->len		 = 0;
	sndbuf->buflen		 = bufsiz;
	rcvbuf->head[0].iov_base = (void *)((char *)req->rq_buffer + bufsiz);
	rcvbuf->head[0].iov_len  = bufsiz;
	rcvbuf->tail[0].iov_len  = 0;
	rcvbuf->page_len	 = 0;
	rcvbuf->len		 = 0;
	rcvbuf->buflen		 = bufsiz;

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

	dprintk("RPC: %4d call_bind (status %d)\n",
				task->tk_pid, task->tk_status);

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
	int status = -EACCES;

	if (task->tk_status >= 0) {
		dprintk("RPC: %4d call_bind_status (status %d)\n",
					task->tk_pid, task->tk_status);
		task->tk_status = 0;
		task->tk_action = call_connect;
		return;
	}

	switch (task->tk_status) {
	case -EACCES:
		dprintk("RPC: %4d remote rpcbind: RPC program/version unavailable\n",
				task->tk_pid);
		rpc_delay(task, 3*HZ);
		goto retry_timeout;
	case -ETIMEDOUT:
		dprintk("RPC: %4d rpcbind request timed out\n",
				task->tk_pid);
		goto retry_timeout;
	case -EPFNOSUPPORT:
		dprintk("RPC: %4d remote rpcbind service unavailable\n",
				task->tk_pid);
		break;
	case -EPROTONOSUPPORT:
		dprintk("RPC: %4d remote rpcbind version 2 unavailable\n",
				task->tk_pid);
		break;
	default:
		dprintk("RPC: %4d unrecognized rpcbind error (%d)\n",
				task->tk_pid, -task->tk_status);
		status = -EIO;
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

	dprintk("RPC: %4d call_connect xprt %p %s connected\n",
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

	dprintk("RPC: %5u call_connect_status (status %d)\n", 
				task->tk_pid, task->tk_status);

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
	dprintk("RPC: %4d call_transmit (status %d)\n", 
				task->tk_pid, task->tk_status);

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

	dprintk("RPC: %4d call_status (status %d)\n", 
				task->tk_pid, task->tk_status);

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
		dprintk("RPC: %4d call_timeout (minor)\n", task->tk_pid);
		goto retry;
	}

	dprintk("RPC: %4d call_timeout (major)\n", task->tk_pid);
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

	dprintk("RPC: %4d call_decode (status %d)\n", 
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
		dprintk("%s: too small RPC reply size (%d bytes)\n",
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

	if (decode)
		task->tk_status = rpcauth_unwrap_resp(task, decode, req, p,
						      task->tk_msg.rpc_resp);
	dprintk("RPC: %4d call_decode result %d\n", task->tk_pid,
					task->tk_status);
	return;
out_retry:
	req->rq_received = req->rq_private_buf.len = 0;
	task->tk_status = 0;
}

/*
 * 8.	Refresh the credentials if rejected by the server
 */
static void
call_refresh(struct rpc_task *task)
{
	dprintk("RPC: %4d call_refresh\n", task->tk_pid);

	xprt_release(task);	/* Must do to obtain new XID */
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
	dprintk("RPC: %4d call_refreshresult (status %d)\n", 
				task->tk_pid, task->tk_status);

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
		printk(KERN_WARNING
		       "call_verify: XDR representation not a multiple of"
		       " 4 bytes: 0x%x\n", task->tk_rqstp->rq_rcv_buf.len);
		goto out_eio;
	}
	if ((len -= 3) < 0)
		goto out_overflow;
	p += 1;	/* skip XID */

	if ((n = ntohl(*p++)) != RPC_REPLY) {
		printk(KERN_WARNING "call_verify: not an RPC reply: %x\n", n);
		goto out_garbage;
	}
	if ((n = ntohl(*p++)) != RPC_MSG_ACCEPTED) {
		if (--len < 0)
			goto out_overflow;
		switch ((n = ntohl(*p++))) {
			case RPC_AUTH_ERROR:
				break;
			case RPC_MISMATCH:
				dprintk("%s: RPC call version mismatch!\n", __FUNCTION__);
				error = -EPROTONOSUPPORT;
				goto out_err;
			default:
				dprintk("%s: RPC call rejected, unknown error: %x\n", __FUNCTION__, n);
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
			dprintk("RPC: %4d call_verify: retry stale creds\n",
							task->tk_pid);
			rpcauth_invalcred(task);
			task->tk_action = call_refresh;
			goto out_retry;
		case RPC_AUTH_BADCRED:
		case RPC_AUTH_BADVERF:
			/* possibly garbled cred/verf? */
			if (!task->tk_garb_retry)
				break;
			task->tk_garb_retry--;
			dprintk("RPC: %4d call_verify: retry garbled creds\n",
							task->tk_pid);
			task->tk_action = call_bind;
			goto out_retry;
		case RPC_AUTH_TOOWEAK:
			printk(KERN_NOTICE "call_verify: server %s requires stronger "
			       "authentication.\n", task->tk_client->cl_server);
			break;
		default:
			printk(KERN_WARNING "call_verify: unknown auth error: %x\n", n);
			error = -EIO;
		}
		dprintk("RPC: %4d call_verify: call rejected %d\n",
						task->tk_pid, n);
		goto out_err;
	}
	if (!(p = rpcauth_checkverf(task, p))) {
		printk(KERN_WARNING "call_verify: auth check failed\n");
		goto out_garbage;		/* bad verifier, retry */
	}
	len = p - (__be32 *)iov->iov_base - 1;
	if (len < 0)
		goto out_overflow;
	switch ((n = ntohl(*p++))) {
	case RPC_SUCCESS:
		return p;
	case RPC_PROG_UNAVAIL:
		dprintk("RPC: call_verify: program %u is unsupported by server %s\n",
				(unsigned int)task->tk_client->cl_prog,
				task->tk_client->cl_server);
		error = -EPFNOSUPPORT;
		goto out_err;
	case RPC_PROG_MISMATCH:
		dprintk("RPC: call_verify: program %u, version %u unsupported by server %s\n",
				(unsigned int)task->tk_client->cl_prog,
				(unsigned int)task->tk_client->cl_vers,
				task->tk_client->cl_server);
		error = -EPROTONOSUPPORT;
		goto out_err;
	case RPC_PROC_UNAVAIL:
		dprintk("RPC: call_verify: proc %p unsupported by program %u, version %u on server %s\n",
				task->tk_msg.rpc_proc,
				task->tk_client->cl_prog,
				task->tk_client->cl_vers,
				task->tk_client->cl_server);
		error = -EOPNOTSUPP;
		goto out_err;
	case RPC_GARBAGE_ARGS:
		dprintk("RPC: %4d %s: server saw garbage\n", task->tk_pid, __FUNCTION__);
		break;			/* retry */
	default:
		printk(KERN_WARNING "call_verify: server accept status: %x\n", n);
		/* Also retry */
	}

out_garbage:
	task->tk_client->cl_stats->rpcgarbage++;
	if (task->tk_garb_retry) {
		task->tk_garb_retry--;
		dprintk("RPC %s: retrying %4d\n", __FUNCTION__, task->tk_pid);
		task->tk_action = call_bind;
out_retry:
		return ERR_PTR(-EAGAIN);
	}
	printk(KERN_WARNING "RPC %s: retry failed, exit EIO\n", __FUNCTION__);
out_eio:
	error = -EIO;
out_err:
	rpc_exit(task, error);
	return ERR_PTR(error);
out_overflow:
	printk(KERN_WARNING "RPC %s: server reply was truncated.\n", __FUNCTION__);
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

int rpc_ping(struct rpc_clnt *clnt, int flags)
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
