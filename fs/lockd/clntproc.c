/*
 * linux/fs/lockd/clntproc.c
 *
 * RPC procedures for the client side NLM implementation
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/nfs_fs.h>
#include <linux/utsname.h>
#include <linux/freezer.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>

#define NLMDBG_FACILITY		NLMDBG_CLIENT
#define NLMCLNT_GRACE_WAIT	(5*HZ)
#define NLMCLNT_POLL_TIMEOUT	(30*HZ)
#define NLMCLNT_MAX_RETRIES	3

static int	nlmclnt_test(struct nlm_rqst *, struct file_lock *);
static int	nlmclnt_lock(struct nlm_rqst *, struct file_lock *);
static int	nlmclnt_unlock(struct nlm_rqst *, struct file_lock *);
static int	nlm_stat_to_errno(__be32 stat);
static void	nlmclnt_locks_init_private(struct file_lock *fl, struct nlm_host *host);
static int	nlmclnt_cancel(struct nlm_host *, int , struct file_lock *);

static const struct rpc_call_ops nlmclnt_unlock_ops;
static const struct rpc_call_ops nlmclnt_cancel_ops;

/*
 * Cookie counter for NLM requests
 */
static atomic_t	nlm_cookie = ATOMIC_INIT(0x1234);

void nlmclnt_next_cookie(struct nlm_cookie *c)
{
	u32	cookie = atomic_inc_return(&nlm_cookie);

	memcpy(c->data, &cookie, 4);
	c->len=4;
}

static struct nlm_lockowner *nlm_get_lockowner(struct nlm_lockowner *lockowner)
{
	atomic_inc(&lockowner->count);
	return lockowner;
}

static void nlm_put_lockowner(struct nlm_lockowner *lockowner)
{
	if (!atomic_dec_and_lock(&lockowner->count, &lockowner->host->h_lock))
		return;
	list_del(&lockowner->list);
	spin_unlock(&lockowner->host->h_lock);
	nlm_release_host(lockowner->host);
	kfree(lockowner);
}

static inline int nlm_pidbusy(struct nlm_host *host, uint32_t pid)
{
	struct nlm_lockowner *lockowner;
	list_for_each_entry(lockowner, &host->h_lockowners, list) {
		if (lockowner->pid == pid)
			return -EBUSY;
	}
	return 0;
}

static inline uint32_t __nlm_alloc_pid(struct nlm_host *host)
{
	uint32_t res;
	do {
		res = host->h_pidcount++;
	} while (nlm_pidbusy(host, res) < 0);
	return res;
}

static struct nlm_lockowner *__nlm_find_lockowner(struct nlm_host *host, fl_owner_t owner)
{
	struct nlm_lockowner *lockowner;
	list_for_each_entry(lockowner, &host->h_lockowners, list) {
		if (lockowner->owner != owner)
			continue;
		return nlm_get_lockowner(lockowner);
	}
	return NULL;
}

static struct nlm_lockowner *nlm_find_lockowner(struct nlm_host *host, fl_owner_t owner)
{
	struct nlm_lockowner *res, *new = NULL;

	spin_lock(&host->h_lock);
	res = __nlm_find_lockowner(host, owner);
	if (res == NULL) {
		spin_unlock(&host->h_lock);
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		spin_lock(&host->h_lock);
		res = __nlm_find_lockowner(host, owner);
		if (res == NULL && new != NULL) {
			res = new;
			atomic_set(&new->count, 1);
			new->owner = owner;
			new->pid = __nlm_alloc_pid(host);
			new->host = nlm_get_host(host);
			list_add(&new->list, &host->h_lockowners);
			new = NULL;
		}
	}
	spin_unlock(&host->h_lock);
	kfree(new);
	return res;
}

/*
 * Initialize arguments for TEST/LOCK/UNLOCK/CANCEL calls
 */
static void nlmclnt_setlockargs(struct nlm_rqst *req, struct file_lock *fl)
{
	struct nlm_args	*argp = &req->a_args;
	struct nlm_lock	*lock = &argp->lock;

	nlmclnt_next_cookie(&argp->cookie);
	argp->state   = nsm_local_state;
	memcpy(&lock->fh, NFS_FH(fl->fl_file->f_path.dentry->d_inode), sizeof(struct nfs_fh));
	lock->caller  = utsname()->nodename;
	lock->oh.data = req->a_owner;
	lock->oh.len  = snprintf(req->a_owner, sizeof(req->a_owner), "%u@%s",
				(unsigned int)fl->fl_u.nfs_fl.owner->pid,
				utsname()->nodename);
	lock->svid = fl->fl_u.nfs_fl.owner->pid;
	lock->fl.fl_start = fl->fl_start;
	lock->fl.fl_end = fl->fl_end;
	lock->fl.fl_type = fl->fl_type;
}

static void nlmclnt_release_lockargs(struct nlm_rqst *req)
{
	BUG_ON(req->a_args.lock.fl.fl_ops != NULL);
}

/**
 * nlmclnt_proc - Perform a single client-side lock request
 * @host: address of a valid nlm_host context representing the NLM server
 * @cmd: fcntl-style file lock operation to perform
 * @fl: address of arguments for the lock operation
 *
 */
int nlmclnt_proc(struct nlm_host *host, int cmd, struct file_lock *fl)
{
	struct nlm_rqst		*call;
	int			status;

	nlm_get_host(host);
	call = nlm_alloc_call(host);
	if (call == NULL)
		return -ENOMEM;

	nlmclnt_locks_init_private(fl, host);
	/* Set up the argument struct */
	nlmclnt_setlockargs(call, fl);

	if (IS_SETLK(cmd) || IS_SETLKW(cmd)) {
		if (fl->fl_type != F_UNLCK) {
			call->a_args.block = IS_SETLKW(cmd) ? 1 : 0;
			status = nlmclnt_lock(call, fl);
		} else
			status = nlmclnt_unlock(call, fl);
	} else if (IS_GETLK(cmd))
		status = nlmclnt_test(call, fl);
	else
		status = -EINVAL;

	fl->fl_ops->fl_release_private(fl);
	fl->fl_ops = NULL;

	dprintk("lockd: clnt proc returns %d\n", status);
	return status;
}
EXPORT_SYMBOL_GPL(nlmclnt_proc);

/*
 * Allocate an NLM RPC call struct
 *
 * Note: the caller must hold a reference to host. In case of failure,
 * this reference will be released.
 */
struct nlm_rqst *nlm_alloc_call(struct nlm_host *host)
{
	struct nlm_rqst	*call;

	for(;;) {
		call = kzalloc(sizeof(*call), GFP_KERNEL);
		if (call != NULL) {
			atomic_set(&call->a_count, 1);
			locks_init_lock(&call->a_args.lock.fl);
			locks_init_lock(&call->a_res.lock.fl);
			call->a_host = host;
			return call;
		}
		if (signalled())
			break;
		printk("nlm_alloc_call: failed, waiting for memory\n");
		schedule_timeout_interruptible(5*HZ);
	}
	nlm_release_host(host);
	return NULL;
}

void nlm_release_call(struct nlm_rqst *call)
{
	if (!atomic_dec_and_test(&call->a_count))
		return;
	nlm_release_host(call->a_host);
	nlmclnt_release_lockargs(call);
	kfree(call);
}

static void nlmclnt_rpc_release(void *data)
{
	lock_kernel();
	nlm_release_call(data);
	unlock_kernel();
}

static int nlm_wait_on_grace(wait_queue_head_t *queue)
{
	DEFINE_WAIT(wait);
	int status = -EINTR;

	prepare_to_wait(queue, &wait, TASK_INTERRUPTIBLE);
	if (!signalled ()) {
		schedule_timeout(NLMCLNT_GRACE_WAIT);
		try_to_freeze();
		if (!signalled ())
			status = 0;
	}
	finish_wait(queue, &wait);
	return status;
}

/*
 * Generic NLM call
 */
static int
nlmclnt_call(struct rpc_cred *cred, struct nlm_rqst *req, u32 proc)
{
	struct nlm_host	*host = req->a_host;
	struct rpc_clnt	*clnt;
	struct nlm_args	*argp = &req->a_args;
	struct nlm_res	*resp = &req->a_res;
	struct rpc_message msg = {
		.rpc_argp	= argp,
		.rpc_resp	= resp,
		.rpc_cred	= cred,
	};
	int		status;

	dprintk("lockd: call procedure %d on %s\n",
			(int)proc, host->h_name);

	do {
		if (host->h_reclaiming && !argp->reclaim)
			goto in_grace_period;

		/* If we have no RPC client yet, create one. */
		if ((clnt = nlm_bind_host(host)) == NULL)
			return -ENOLCK;
		msg.rpc_proc = &clnt->cl_procinfo[proc];

		/* Perform the RPC call. If an error occurs, try again */
		if ((status = rpc_call_sync(clnt, &msg, 0)) < 0) {
			dprintk("lockd: rpc_call returned error %d\n", -status);
			switch (status) {
			case -EPROTONOSUPPORT:
				status = -EINVAL;
				break;
			case -ECONNREFUSED:
			case -ETIMEDOUT:
			case -ENOTCONN:
				nlm_rebind_host(host);
				status = -EAGAIN;
				break;
			case -ERESTARTSYS:
				return signalled () ? -EINTR : status;
			default:
				break;
			}
			break;
		} else
		if (resp->status == nlm_lck_denied_grace_period) {
			dprintk("lockd: server in grace period\n");
			if (argp->reclaim) {
				printk(KERN_WARNING
				     "lockd: spurious grace period reject?!\n");
				return -ENOLCK;
			}
		} else {
			if (!argp->reclaim) {
				/* We appear to be out of the grace period */
				wake_up_all(&host->h_gracewait);
			}
			dprintk("lockd: server returns status %d\n", resp->status);
			return 0;	/* Okay, call complete */
		}

in_grace_period:
		/*
		 * The server has rebooted and appears to be in the grace
		 * period during which locks are only allowed to be
		 * reclaimed.
		 * We can only back off and try again later.
		 */
		status = nlm_wait_on_grace(&host->h_gracewait);
	} while (status == 0);

	return status;
}

/*
 * Generic NLM call, async version.
 */
static struct rpc_task *__nlm_async_call(struct nlm_rqst *req, u32 proc, struct rpc_message *msg, const struct rpc_call_ops *tk_ops)
{
	struct nlm_host	*host = req->a_host;
	struct rpc_clnt	*clnt;
	struct rpc_task_setup task_setup_data = {
		.rpc_message = msg,
		.callback_ops = tk_ops,
		.callback_data = req,
		.flags = RPC_TASK_ASYNC,
	};

	dprintk("lockd: call procedure %d on %s (async)\n",
			(int)proc, host->h_name);

	/* If we have no RPC client yet, create one. */
	clnt = nlm_bind_host(host);
	if (clnt == NULL)
		goto out_err;
	msg->rpc_proc = &clnt->cl_procinfo[proc];
	task_setup_data.rpc_client = clnt;

        /* bootstrap and kick off the async RPC call */
	return rpc_run_task(&task_setup_data);
out_err:
	tk_ops->rpc_release(req);
	return ERR_PTR(-ENOLCK);
}

static int nlm_do_async_call(struct nlm_rqst *req, u32 proc, struct rpc_message *msg, const struct rpc_call_ops *tk_ops)
{
	struct rpc_task *task;

	task = __nlm_async_call(req, proc, msg, tk_ops);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}

/*
 * NLM asynchronous call.
 */
int nlm_async_call(struct nlm_rqst *req, u32 proc, const struct rpc_call_ops *tk_ops)
{
	struct rpc_message msg = {
		.rpc_argp	= &req->a_args,
		.rpc_resp	= &req->a_res,
	};
	return nlm_do_async_call(req, proc, &msg, tk_ops);
}

int nlm_async_reply(struct nlm_rqst *req, u32 proc, const struct rpc_call_ops *tk_ops)
{
	struct rpc_message msg = {
		.rpc_argp	= &req->a_res,
	};
	return nlm_do_async_call(req, proc, &msg, tk_ops);
}

/*
 * NLM client asynchronous call.
 *
 * Note that although the calls are asynchronous, and are therefore
 *      guaranteed to complete, we still always attempt to wait for
 *      completion in order to be able to correctly track the lock
 *      state.
 */
static int nlmclnt_async_call(struct rpc_cred *cred, struct nlm_rqst *req, u32 proc, const struct rpc_call_ops *tk_ops)
{
	struct rpc_message msg = {
		.rpc_argp	= &req->a_args,
		.rpc_resp	= &req->a_res,
		.rpc_cred	= cred,
	};
	struct rpc_task *task;
	int err;

	task = __nlm_async_call(req, proc, &msg, tk_ops);
	if (IS_ERR(task))
		return PTR_ERR(task);
	err = rpc_wait_for_completion_task(task);
	rpc_put_task(task);
	return err;
}

/*
 * TEST for the presence of a conflicting lock
 */
static int
nlmclnt_test(struct nlm_rqst *req, struct file_lock *fl)
{
	int	status;

	status = nlmclnt_call(nfs_file_cred(fl->fl_file), req, NLMPROC_TEST);
	if (status < 0)
		goto out;

	switch (req->a_res.status) {
		case nlm_granted:
			fl->fl_type = F_UNLCK;
			break;
		case nlm_lck_denied:
			/*
			 * Report the conflicting lock back to the application.
			 */
			fl->fl_start = req->a_res.lock.fl.fl_start;
			fl->fl_end = req->a_res.lock.fl.fl_end;
			fl->fl_type = req->a_res.lock.fl.fl_type;
			fl->fl_pid = 0;
			break;
		default:
			status = nlm_stat_to_errno(req->a_res.status);
	}
out:
	nlm_release_call(req);
	return status;
}

static void nlmclnt_locks_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	new->fl_u.nfs_fl.state = fl->fl_u.nfs_fl.state;
	new->fl_u.nfs_fl.owner = nlm_get_lockowner(fl->fl_u.nfs_fl.owner);
	list_add_tail(&new->fl_u.nfs_fl.list, &fl->fl_u.nfs_fl.owner->host->h_granted);
}

static void nlmclnt_locks_release_private(struct file_lock *fl)
{
	list_del(&fl->fl_u.nfs_fl.list);
	nlm_put_lockowner(fl->fl_u.nfs_fl.owner);
}

static struct file_lock_operations nlmclnt_lock_ops = {
	.fl_copy_lock = nlmclnt_locks_copy_lock,
	.fl_release_private = nlmclnt_locks_release_private,
};

static void nlmclnt_locks_init_private(struct file_lock *fl, struct nlm_host *host)
{
	BUG_ON(fl->fl_ops != NULL);
	fl->fl_u.nfs_fl.state = 0;
	fl->fl_u.nfs_fl.owner = nlm_find_lockowner(host, fl->fl_owner);
	INIT_LIST_HEAD(&fl->fl_u.nfs_fl.list);
	fl->fl_ops = &nlmclnt_lock_ops;
}

static int do_vfs_lock(struct file_lock *fl)
{
	int res = 0;
	switch (fl->fl_flags & (FL_POSIX|FL_FLOCK)) {
		case FL_POSIX:
			res = posix_lock_file_wait(fl->fl_file, fl);
			break;
		case FL_FLOCK:
			res = flock_lock_file_wait(fl->fl_file, fl);
			break;
		default:
			BUG();
	}
	return res;
}

/*
 * LOCK: Try to create a lock
 *
 *			Programmer Harassment Alert
 *
 * When given a blocking lock request in a sync RPC call, the HPUX lockd
 * will faithfully return LCK_BLOCKED but never cares to notify us when
 * the lock could be granted. This way, our local process could hang
 * around forever waiting for the callback.
 *
 *  Solution A:	Implement busy-waiting
 *  Solution B: Use the async version of the call (NLM_LOCK_{MSG,RES})
 *
 * For now I am implementing solution A, because I hate the idea of
 * re-implementing lockd for a third time in two months. The async
 * calls shouldn't be too hard to do, however.
 *
 * This is one of the lovely things about standards in the NFS area:
 * they're so soft and squishy you can't really blame HP for doing this.
 */
static int
nlmclnt_lock(struct nlm_rqst *req, struct file_lock *fl)
{
	struct rpc_cred *cred = nfs_file_cred(fl->fl_file);
	struct nlm_host	*host = req->a_host;
	struct nlm_res	*resp = &req->a_res;
	struct nlm_wait *block = NULL;
	unsigned char fl_flags = fl->fl_flags;
	unsigned char fl_type;
	int status = -ENOLCK;

	if (nsm_monitor(host) < 0) {
		printk(KERN_NOTICE "lockd: failed to monitor %s\n",
					host->h_name);
		goto out;
	}
	fl->fl_flags |= FL_ACCESS;
	status = do_vfs_lock(fl);
	fl->fl_flags = fl_flags;
	if (status < 0)
		goto out;

	block = nlmclnt_prepare_block(host, fl);
again:
	/*
	 * Initialise resp->status to a valid non-zero value,
	 * since 0 == nlm_lck_granted
	 */
	resp->status = nlm_lck_blocked;
	for(;;) {
		/* Reboot protection */
		fl->fl_u.nfs_fl.state = host->h_state;
		status = nlmclnt_call(cred, req, NLMPROC_LOCK);
		if (status < 0)
			break;
		/* Did a reclaimer thread notify us of a server reboot? */
		if (resp->status ==  nlm_lck_denied_grace_period)
			continue;
		if (resp->status != nlm_lck_blocked)
			break;
		/* Wait on an NLM blocking lock */
		status = nlmclnt_block(block, req, NLMCLNT_POLL_TIMEOUT);
		if (status < 0)
			break;
		if (resp->status != nlm_lck_blocked)
			break;
	}

	/* if we were interrupted while blocking, then cancel the lock request
	 * and exit
	 */
	if (resp->status == nlm_lck_blocked) {
		if (!req->a_args.block)
			goto out_unlock;
		if (nlmclnt_cancel(host, req->a_args.block, fl) == 0)
			goto out_unblock;
	}

	if (resp->status == nlm_granted) {
		down_read(&host->h_rwsem);
		/* Check whether or not the server has rebooted */
		if (fl->fl_u.nfs_fl.state != host->h_state) {
			up_read(&host->h_rwsem);
			goto again;
		}
		/* Ensure the resulting lock will get added to granted list */
		fl->fl_flags |= FL_SLEEP;
		if (do_vfs_lock(fl) < 0)
			printk(KERN_WARNING "%s: VFS is out of sync with lock manager!\n", __func__);
		up_read(&host->h_rwsem);
		fl->fl_flags = fl_flags;
		status = 0;
	}
	if (status < 0)
		goto out_unlock;
	/*
	 * EAGAIN doesn't make sense for sleeping locks, and in some
	 * cases NLM_LCK_DENIED is returned for a permanent error.  So
	 * turn it into an ENOLCK.
	 */
	if (resp->status == nlm_lck_denied && (fl_flags & FL_SLEEP))
		status = -ENOLCK;
	else
		status = nlm_stat_to_errno(resp->status);
out_unblock:
	nlmclnt_finish_block(block);
out:
	nlm_release_call(req);
	return status;
out_unlock:
	/* Fatal error: ensure that we remove the lock altogether */
	dprintk("lockd: lock attempt ended in fatal error.\n"
		"       Attempting to unlock.\n");
	nlmclnt_finish_block(block);
	fl_type = fl->fl_type;
	fl->fl_type = F_UNLCK;
	down_read(&host->h_rwsem);
	do_vfs_lock(fl);
	up_read(&host->h_rwsem);
	fl->fl_type = fl_type;
	fl->fl_flags = fl_flags;
	nlmclnt_async_call(cred, req, NLMPROC_UNLOCK, &nlmclnt_unlock_ops);
	return status;
}

/*
 * RECLAIM: Try to reclaim a lock
 */
int
nlmclnt_reclaim(struct nlm_host *host, struct file_lock *fl)
{
	struct nlm_rqst reqst, *req;
	int		status;

	req = &reqst;
	memset(req, 0, sizeof(*req));
	locks_init_lock(&req->a_args.lock.fl);
	locks_init_lock(&req->a_res.lock.fl);
	req->a_host  = host;
	req->a_flags = 0;

	/* Set up the argument struct */
	nlmclnt_setlockargs(req, fl);
	req->a_args.reclaim = 1;

	status = nlmclnt_call(nfs_file_cred(fl->fl_file), req, NLMPROC_LOCK);
	if (status >= 0 && req->a_res.status == nlm_granted)
		return 0;

	printk(KERN_WARNING "lockd: failed to reclaim lock for pid %d "
				"(errno %d, status %d)\n", fl->fl_pid,
				status, ntohl(req->a_res.status));

	/*
	 * FIXME: This is a serious failure. We can
	 *
	 *  a.	Ignore the problem
	 *  b.	Send the owning process some signal (Linux doesn't have
	 *	SIGLOST, though...)
	 *  c.	Retry the operation
	 *
	 * Until someone comes up with a simple implementation
	 * for b or c, I'll choose option a.
	 */

	return -ENOLCK;
}

/*
 * UNLOCK: remove an existing lock
 */
static int
nlmclnt_unlock(struct nlm_rqst *req, struct file_lock *fl)
{
	struct nlm_host	*host = req->a_host;
	struct nlm_res	*resp = &req->a_res;
	int status;
	unsigned char fl_flags = fl->fl_flags;

	/*
	 * Note: the server is supposed to either grant us the unlock
	 * request, or to deny it with NLM_LCK_DENIED_GRACE_PERIOD. In either
	 * case, we want to unlock.
	 */
	fl->fl_flags |= FL_EXISTS;
	down_read(&host->h_rwsem);
	status = do_vfs_lock(fl);
	up_read(&host->h_rwsem);
	fl->fl_flags = fl_flags;
	if (status == -ENOENT) {
		status = 0;
		goto out;
	}

	atomic_inc(&req->a_count);
	status = nlmclnt_async_call(nfs_file_cred(fl->fl_file), req,
			NLMPROC_UNLOCK, &nlmclnt_unlock_ops);
	if (status < 0)
		goto out;

	if (resp->status == nlm_granted)
		goto out;

	if (resp->status != nlm_lck_denied_nolocks)
		printk("lockd: unexpected unlock status: %d\n", resp->status);
	/* What to do now? I'm out of my depth... */
	status = -ENOLCK;
out:
	nlm_release_call(req);
	return status;
}

static void nlmclnt_unlock_callback(struct rpc_task *task, void *data)
{
	struct nlm_rqst	*req = data;
	u32 status = ntohl(req->a_res.status);

	if (RPC_ASSASSINATED(task))
		goto die;

	if (task->tk_status < 0) {
		dprintk("lockd: unlock failed (err = %d)\n", -task->tk_status);
		goto retry_rebind;
	}
	if (status == NLM_LCK_DENIED_GRACE_PERIOD) {
		rpc_delay(task, NLMCLNT_GRACE_WAIT);
		goto retry_unlock;
	}
	if (status != NLM_LCK_GRANTED)
		printk(KERN_WARNING "lockd: unexpected unlock status: %d\n", status);
die:
	return;
 retry_rebind:
	lock_kernel();
	nlm_rebind_host(req->a_host);
	unlock_kernel();
 retry_unlock:
	rpc_restart_call(task);
}

static const struct rpc_call_ops nlmclnt_unlock_ops = {
	.rpc_call_done = nlmclnt_unlock_callback,
	.rpc_release = nlmclnt_rpc_release,
};

/*
 * Cancel a blocked lock request.
 * We always use an async RPC call for this in order not to hang a
 * process that has been Ctrl-C'ed.
 */
static int nlmclnt_cancel(struct nlm_host *host, int block, struct file_lock *fl)
{
	struct nlm_rqst	*req;
	int status;

	dprintk("lockd: blocking lock attempt was interrupted by a signal.\n"
		"       Attempting to cancel lock.\n");

	req = nlm_alloc_call(nlm_get_host(host));
	if (!req)
		return -ENOMEM;
	req->a_flags = RPC_TASK_ASYNC;

	nlmclnt_setlockargs(req, fl);
	req->a_args.block = block;

	atomic_inc(&req->a_count);
	status = nlmclnt_async_call(nfs_file_cred(fl->fl_file), req,
			NLMPROC_CANCEL, &nlmclnt_cancel_ops);
	if (status == 0 && req->a_res.status == nlm_lck_denied)
		status = -ENOLCK;
	nlm_release_call(req);
	return status;
}

static void nlmclnt_cancel_callback(struct rpc_task *task, void *data)
{
	struct nlm_rqst	*req = data;
	u32 status = ntohl(req->a_res.status);

	if (RPC_ASSASSINATED(task))
		goto die;

	if (task->tk_status < 0) {
		dprintk("lockd: CANCEL call error %d, retrying.\n",
					task->tk_status);
		goto retry_cancel;
	}

	dprintk("lockd: cancel status %u (task %u)\n",
			status, task->tk_pid);

	switch (status) {
	case NLM_LCK_GRANTED:
	case NLM_LCK_DENIED_GRACE_PERIOD:
	case NLM_LCK_DENIED:
		/* Everything's good */
		break;
	case NLM_LCK_DENIED_NOLOCKS:
		dprintk("lockd: CANCEL failed (server has no locks)\n");
		goto retry_cancel;
	default:
		printk(KERN_NOTICE "lockd: weird return %d for CANCEL call\n",
			status);
	}

die:
	return;

retry_cancel:
	/* Don't ever retry more than 3 times */
	if (req->a_retries++ >= NLMCLNT_MAX_RETRIES)
		goto die;
	lock_kernel();
	nlm_rebind_host(req->a_host);
	unlock_kernel();
	rpc_restart_call(task);
	rpc_delay(task, 30 * HZ);
}

static const struct rpc_call_ops nlmclnt_cancel_ops = {
	.rpc_call_done = nlmclnt_cancel_callback,
	.rpc_release = nlmclnt_rpc_release,
};

/*
 * Convert an NLM status code to a generic kernel errno
 */
static int
nlm_stat_to_errno(__be32 status)
{
	switch(ntohl(status)) {
	case NLM_LCK_GRANTED:
		return 0;
	case NLM_LCK_DENIED:
		return -EAGAIN;
	case NLM_LCK_DENIED_NOLOCKS:
	case NLM_LCK_DENIED_GRACE_PERIOD:
		return -ENOLCK;
	case NLM_LCK_BLOCKED:
		printk(KERN_NOTICE "lockd: unexpected status NLM_BLOCKED\n");
		return -ENOLCK;
#ifdef CONFIG_LOCKD_V4
	case NLM_DEADLCK:
		return -EDEADLK;
	case NLM_ROFS:
		return -EROFS;
	case NLM_STALE_FH:
		return -ESTALE;
	case NLM_FBIG:
		return -EOVERFLOW;
	case NLM_FAILED:
		return -ENOLCK;
#endif
	}
	printk(KERN_NOTICE "lockd: unexpected server status %d\n", status);
	return -ENOLCK;
}
