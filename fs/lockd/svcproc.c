/*
 * linux/fs/lockd/svcproc.c
 *
 * Lockd server procedures. We don't implement the NLM_*_RES 
 * procedures because we don't use the async procedures.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsd.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/share.h>
#include <linux/lockd/sm_inter.h>


#define NLMDBG_FACILITY		NLMDBG_CLIENT

#ifdef CONFIG_LOCKD_V4
static __be32
cast_to_nlm(__be32 status, u32 vers)
{
	/* Note: status is assumed to be in network byte order !!! */
	if (vers != 4){
		switch (status) {
		case nlm_granted:
		case nlm_lck_denied:
		case nlm_lck_denied_nolocks:
		case nlm_lck_blocked:
		case nlm_lck_denied_grace_period:
		case nlm_drop_reply:
			break;
		case nlm4_deadlock:
			status = nlm_lck_denied;
			break;
		default:
			status = nlm_lck_denied_nolocks;
		}
	}

	return (status);
}
#define	cast_status(status) (cast_to_nlm(status, rqstp->rq_vers))
#else
#define cast_status(status) (status)
#endif

/*
 * Obtain client and file from arguments
 */
static __be32
nlmsvc_retrieve_args(struct svc_rqst *rqstp, struct nlm_args *argp,
			struct nlm_host **hostp, struct nlm_file **filp)
{
	struct nlm_host		*host = NULL;
	struct nlm_file		*file = NULL;
	struct nlm_lock		*lock = &argp->lock;
	__be32			error = 0;

	/* nfsd callbacks must have been installed for this procedure */
	if (!nlmsvc_ops)
		return nlm_lck_denied_nolocks;

	/* Obtain host handle */
	if (!(host = nlmsvc_lookup_host(rqstp, lock->caller, lock->len))
	 || (argp->monitor && nsm_monitor(host) < 0))
		goto no_locks;
	*hostp = host;

	/* Obtain file pointer. Not used by FREE_ALL call. */
	if (filp != NULL) {
		if ((error = nlm_lookup_file(rqstp, &file, &lock->fh)) != 0)
			goto no_locks;
		*filp = file;

		/* Set up the missing parts of the file_lock structure */
		lock->fl.fl_file  = file->f_file;
		lock->fl.fl_owner = (fl_owner_t) host;
		lock->fl.fl_lmops = &nlmsvc_lock_operations;
	}

	return 0;

no_locks:
	nlm_release_host(host);
	if (error)
		return error;
	return nlm_lck_denied_nolocks;
}

/*
 * NULL: Test for presence of service
 */
static __be32
nlmsvc_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	dprintk("lockd: NULL          called\n");
	return rpc_success;
}

/*
 * TEST: Check for conflicting lock
 */
static __be32
nlmsvc_proc_test(struct svc_rqst *rqstp, struct nlm_args *argp,
				         struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;
	__be32 rc = rpc_success;

	dprintk("lockd: TEST          called\n");
	resp->cookie = argp->cookie;

	/* Don't accept test requests during grace period */
	if (locks_in_grace()) {
		resp->status = nlm_lck_denied_grace_period;
		return rc;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

	/* Now check for conflicting locks */
	resp->status = cast_status(nlmsvc_testlock(rqstp, file, host, &argp->lock, &resp->lock, &resp->cookie));
	if (resp->status == nlm_drop_reply)
		rc = rpc_drop_reply;
	else
		dprintk("lockd: TEST          status %d vers %d\n",
			ntohl(resp->status), rqstp->rq_vers);

	nlm_release_host(host);
	nlm_release_file(file);
	return rc;
}

static __be32
nlmsvc_proc_lock(struct svc_rqst *rqstp, struct nlm_args *argp,
				         struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;
	__be32 rc = rpc_success;

	dprintk("lockd: LOCK          called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (locks_in_grace() && !argp->reclaim) {
		resp->status = nlm_lck_denied_grace_period;
		return rc;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

#if 0
	/* If supplied state doesn't match current state, we assume it's
	 * an old request that time-warped somehow. Any error return would
	 * do in this case because it's irrelevant anyway.
	 *
	 * NB: We don't retrieve the remote host's state yet.
	 */
	if (host->h_nsmstate && host->h_nsmstate != argp->state) {
		resp->status = nlm_lck_denied_nolocks;
	} else
#endif

	/* Now try to lock the file */
	resp->status = cast_status(nlmsvc_lock(rqstp, file, host, &argp->lock,
					       argp->block, &argp->cookie));
	if (resp->status == nlm_drop_reply)
		rc = rpc_drop_reply;
	else
		dprintk("lockd: LOCK         status %d\n", ntohl(resp->status));

	nlm_release_host(host);
	nlm_release_file(file);
	return rc;
}

static __be32
nlmsvc_proc_cancel(struct svc_rqst *rqstp, struct nlm_args *argp,
				           struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: CANCEL        called\n");

	resp->cookie = argp->cookie;

	/* Don't accept requests during grace period */
	if (locks_in_grace()) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

	/* Try to cancel request. */
	resp->status = cast_status(nlmsvc_cancel_blocked(file, &argp->lock));

	dprintk("lockd: CANCEL        status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * UNLOCK: release a lock
 */
static __be32
nlmsvc_proc_unlock(struct svc_rqst *rqstp, struct nlm_args *argp,
				           struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: UNLOCK        called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (locks_in_grace()) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

	/* Now try to remove the lock */
	resp->status = cast_status(nlmsvc_unlock(file, &argp->lock));

	dprintk("lockd: UNLOCK        status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * GRANTED: A server calls us to tell that a process' lock request
 * was granted
 */
static __be32
nlmsvc_proc_granted(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	resp->cookie = argp->cookie;

	dprintk("lockd: GRANTED       called\n");
	resp->status = nlmclnt_grant(svc_addr_in(rqstp), &argp->lock);
	dprintk("lockd: GRANTED       status %d\n", ntohl(resp->status));
	return rpc_success;
}

/*
 * This is the generic lockd callback for async RPC calls
 */
static void nlmsvc_callback_exit(struct rpc_task *task, void *data)
{
	dprintk("lockd: %5u callback returned %d\n", task->tk_pid,
			-task->tk_status);
}

static void nlmsvc_callback_release(void *data)
{
	lock_kernel();
	nlm_release_call(data);
	unlock_kernel();
}

static const struct rpc_call_ops nlmsvc_callback_ops = {
	.rpc_call_done = nlmsvc_callback_exit,
	.rpc_release = nlmsvc_callback_release,
};

/*
 * `Async' versions of the above service routines. They aren't really,
 * because we send the callback before the reply proper. I hope this
 * doesn't break any clients.
 */
static __be32 nlmsvc_callback(struct svc_rqst *rqstp, u32 proc, struct nlm_args *argp,
		__be32 (*func)(struct svc_rqst *, struct nlm_args *, struct nlm_res  *))
{
	struct nlm_host	*host;
	struct nlm_rqst	*call;
	__be32 stat;

	host = nlmsvc_lookup_host(rqstp,
				  argp->lock.caller,
				  argp->lock.len);
	if (host == NULL)
		return rpc_system_err;

	call = nlm_alloc_call(host);
	if (call == NULL)
		return rpc_system_err;

	stat = func(rqstp, argp, &call->a_res);
	if (stat != 0) {
		nlm_release_call(call);
		return stat;
	}

	call->a_flags = RPC_TASK_ASYNC;
	if (nlm_async_reply(call, proc, &nlmsvc_callback_ops) < 0)
		return rpc_system_err;
	return rpc_success;
}

static __be32 nlmsvc_proc_test_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void	     *resp)
{
	dprintk("lockd: TEST_MSG      called\n");
	return nlmsvc_callback(rqstp, NLMPROC_TEST_RES, argp, nlmsvc_proc_test);
}

static __be32 nlmsvc_proc_lock_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void	     *resp)
{
	dprintk("lockd: LOCK_MSG      called\n");
	return nlmsvc_callback(rqstp, NLMPROC_LOCK_RES, argp, nlmsvc_proc_lock);
}

static __be32 nlmsvc_proc_cancel_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					       void	       *resp)
{
	dprintk("lockd: CANCEL_MSG    called\n");
	return nlmsvc_callback(rqstp, NLMPROC_CANCEL_RES, argp, nlmsvc_proc_cancel);
}

static __be32
nlmsvc_proc_unlock_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
                                               void            *resp)
{
	dprintk("lockd: UNLOCK_MSG    called\n");
	return nlmsvc_callback(rqstp, NLMPROC_UNLOCK_RES, argp, nlmsvc_proc_unlock);
}

static __be32
nlmsvc_proc_granted_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
                                                void            *resp)
{
	dprintk("lockd: GRANTED_MSG   called\n");
	return nlmsvc_callback(rqstp, NLMPROC_GRANTED_RES, argp, nlmsvc_proc_granted);
}

/*
 * SHARE: create a DOS share or alter existing share.
 */
static __be32
nlmsvc_proc_share(struct svc_rqst *rqstp, struct nlm_args *argp,
				          struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: SHARE         called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (locks_in_grace() && !argp->reclaim) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

	/* Now try to create the share */
	resp->status = cast_status(nlmsvc_share_file(host, file, argp));

	dprintk("lockd: SHARE         status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * UNSHARE: Release a DOS share.
 */
static __be32
nlmsvc_proc_unshare(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: UNSHARE       called\n");

	resp->cookie = argp->cookie;

	/* Don't accept requests during grace period */
	if (locks_in_grace()) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm_drop_reply ? rpc_drop_reply :rpc_success;

	/* Now try to unshare the file */
	resp->status = cast_status(nlmsvc_unshare_file(host, file, argp));

	dprintk("lockd: UNSHARE       status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * NM_LOCK: Create an unmonitored lock
 */
static __be32
nlmsvc_proc_nm_lock(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	dprintk("lockd: NM_LOCK       called\n");

	argp->monitor = 0;		/* just clean the monitor flag */
	return nlmsvc_proc_lock(rqstp, argp, resp);
}

/*
 * FREE_ALL: Release all locks and shares held by client
 */
static __be32
nlmsvc_proc_free_all(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void            *resp)
{
	struct nlm_host	*host;

	/* Obtain client */
	if (nlmsvc_retrieve_args(rqstp, argp, &host, NULL))
		return rpc_success;

	nlmsvc_free_host_resources(host);
	nlm_release_host(host);
	return rpc_success;
}

/*
 * SM_NOTIFY: private callback from statd (not part of official NLM proto)
 */
static __be32
nlmsvc_proc_sm_notify(struct svc_rqst *rqstp, struct nlm_reboot *argp,
					      void	        *resp)
{
	struct sockaddr_in	saddr;

	memcpy(&saddr, svc_addr_in(rqstp), sizeof(saddr));

	dprintk("lockd: SM_NOTIFY     called\n");
	if (saddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK)
	 || ntohs(saddr.sin_port) >= 1024) {
		char buf[RPC_MAX_ADDRBUFLEN];
		printk(KERN_WARNING "lockd: rejected NSM callback from %s\n",
				svc_print_addr(rqstp, buf, sizeof(buf)));
		return rpc_system_err;
	}

	/* Obtain the host pointer for this NFS server and try to
	 * reclaim all locks we hold on this server.
	 */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_addr.s_addr = argp->addr;
	nlm_host_rebooted(&saddr, argp->mon, argp->len, argp->state);

	return rpc_success;
}

/*
 * client sent a GRANTED_RES, let's remove the associated block
 */
static __be32
nlmsvc_proc_granted_res(struct svc_rqst *rqstp, struct nlm_res  *argp,
                                                void            *resp)
{
	if (!nlmsvc_ops)
		return rpc_success;

	dprintk("lockd: GRANTED_RES   called\n");

	nlmsvc_grant_reply(&argp->cookie, argp->status);
	return rpc_success;
}

/*
 * NLM Server procedures.
 */

#define nlmsvc_encode_norep	nlmsvc_encode_void
#define nlmsvc_decode_norep	nlmsvc_decode_void
#define nlmsvc_decode_testres	nlmsvc_decode_void
#define nlmsvc_decode_lockres	nlmsvc_decode_void
#define nlmsvc_decode_unlockres	nlmsvc_decode_void
#define nlmsvc_decode_cancelres	nlmsvc_decode_void
#define nlmsvc_decode_grantedres	nlmsvc_decode_void

#define nlmsvc_proc_none	nlmsvc_proc_null
#define nlmsvc_proc_test_res	nlmsvc_proc_null
#define nlmsvc_proc_lock_res	nlmsvc_proc_null
#define nlmsvc_proc_cancel_res	nlmsvc_proc_null
#define nlmsvc_proc_unlock_res	nlmsvc_proc_null

struct nlm_void			{ int dummy; };

#define PROC(name, xargt, xrest, argt, rest, respsize)	\
 { .pc_func	= (svc_procfunc) nlmsvc_proc_##name,	\
   .pc_decode	= (kxdrproc_t) nlmsvc_decode_##xargt,	\
   .pc_encode	= (kxdrproc_t) nlmsvc_encode_##xrest,	\
   .pc_release	= NULL,					\
   .pc_argsize	= sizeof(struct nlm_##argt),		\
   .pc_ressize	= sizeof(struct nlm_##rest),		\
   .pc_xdrressize = respsize,				\
 }

#define	Ck	(1+XDR_QUADLEN(NLM_MAXCOOKIELEN))	/* cookie */
#define	St	1				/* status */
#define	No	(1+1024/4)			/* Net Obj */
#define	Rg	2				/* range - offset + size */

struct svc_procedure		nlmsvc_procedures[] = {
  PROC(null,		void,		void,		void,	void, 1),
  PROC(test,		testargs,	testres,	args,	res, Ck+St+2+No+Rg),
  PROC(lock,		lockargs,	res,		args,	res, Ck+St),
  PROC(cancel,		cancargs,	res,		args,	res, Ck+St),
  PROC(unlock,		unlockargs,	res,		args,	res, Ck+St),
  PROC(granted,		testargs,	res,		args,	res, Ck+St),
  PROC(test_msg,	testargs,	norep,		args,	void, 1),
  PROC(lock_msg,	lockargs,	norep,		args,	void, 1),
  PROC(cancel_msg,	cancargs,	norep,		args,	void, 1),
  PROC(unlock_msg,	unlockargs,	norep,		args,	void, 1),
  PROC(granted_msg,	testargs,	norep,		args,	void, 1),
  PROC(test_res,	testres,	norep,		res,	void, 1),
  PROC(lock_res,	lockres,	norep,		res,	void, 1),
  PROC(cancel_res,	cancelres,	norep,		res,	void, 1),
  PROC(unlock_res,	unlockres,	norep,		res,	void, 1),
  PROC(granted_res,	res,		norep,		res,	void, 1),
  /* statd callback */
  PROC(sm_notify,	reboot,		void,		reboot,	void, 1),
  PROC(none,		void,		void,		void,	void, 1),
  PROC(none,		void,		void,		void,	void, 1),
  PROC(none,		void,		void,		void,	void, 1),
  PROC(share,		shareargs,	shareres,	args,	res, Ck+St+1),
  PROC(unshare,		shareargs,	shareres,	args,	res, Ck+St+1),
  PROC(nm_lock,		lockargs,	res,		args,	res, Ck+St),
  PROC(free_all,	notify,		void,		args,	void, 0),

};
