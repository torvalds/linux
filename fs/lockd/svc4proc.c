/*
 * linux/fs/lockd/svc4proc.c
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

static u32	nlm4svc_callback(struct svc_rqst *, u32, struct nlm_res *);
static void	nlm4svc_callback_exit(struct rpc_task *);

/*
 * Obtain client and file from arguments
 */
static u32
nlm4svc_retrieve_args(struct svc_rqst *rqstp, struct nlm_args *argp,
			struct nlm_host **hostp, struct nlm_file **filp)
{
	struct nlm_host		*host = NULL;
	struct nlm_file		*file = NULL;
	struct nlm_lock		*lock = &argp->lock;
	u32			error = 0;

	/* nfsd callbacks must have been installed for this procedure */
	if (!nlmsvc_ops)
		return nlm_lck_denied_nolocks;

	/* Obtain host handle */
	if (!(host = nlmsvc_lookup_host(rqstp))
	 || (argp->monitor && !host->h_monitored && nsm_monitor(host) < 0))
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
	if (host)
		nlm_release_host(host);
 	if (error)
		return error;	
	return nlm_lck_denied_nolocks;
}

/*
 * NULL: Test for presence of service
 */
static int
nlm4svc_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	dprintk("lockd: NULL          called\n");
	return rpc_success;
}

/*
 * TEST: Check for conflicting lock
 */
static int
nlm4svc_proc_test(struct svc_rqst *rqstp, struct nlm_args *argp,
				         struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: TEST4        called\n");
	resp->cookie = argp->cookie;

	/* Don't accept test requests during grace period */
	if (nlmsvc_grace_period) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

	/* Now check for conflicting locks */
	resp->status = nlmsvc_testlock(file, &argp->lock, &resp->lock);

	dprintk("lockd: TEST4          status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

static int
nlm4svc_proc_lock(struct svc_rqst *rqstp, struct nlm_args *argp,
				         struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: LOCK          called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (nlmsvc_grace_period && !argp->reclaim) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

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
	resp->status = nlmsvc_lock(rqstp, file, &argp->lock,
					argp->block, &argp->cookie);

	dprintk("lockd: LOCK          status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

static int
nlm4svc_proc_cancel(struct svc_rqst *rqstp, struct nlm_args *argp,
				           struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: CANCEL        called\n");

	resp->cookie = argp->cookie;

	/* Don't accept requests during grace period */
	if (nlmsvc_grace_period) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

	/* Try to cancel request. */
	resp->status = nlmsvc_cancel_blocked(file, &argp->lock);

	dprintk("lockd: CANCEL        status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * UNLOCK: release a lock
 */
static int
nlm4svc_proc_unlock(struct svc_rqst *rqstp, struct nlm_args *argp,
				           struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: UNLOCK        called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (nlmsvc_grace_period) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

	/* Now try to remove the lock */
	resp->status = nlmsvc_unlock(file, &argp->lock);

	dprintk("lockd: UNLOCK        status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * GRANTED: A server calls us to tell that a process' lock request
 * was granted
 */
static int
nlm4svc_proc_granted(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	resp->cookie = argp->cookie;

	dprintk("lockd: GRANTED       called\n");
	resp->status = nlmclnt_grant(&argp->lock);
	dprintk("lockd: GRANTED       status %d\n", ntohl(resp->status));
	return rpc_success;
}

/*
 * `Async' versions of the above service routines. They aren't really,
 * because we send the callback before the reply proper. I hope this
 * doesn't break any clients.
 */
static int
nlm4svc_proc_test_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void	     *resp)
{
	struct nlm_res	res;
	u32		stat;

	dprintk("lockd: TEST_MSG      called\n");
	memset(&res, 0, sizeof(res));

	if ((stat = nlm4svc_proc_test(rqstp, argp, &res)) == 0)
		stat = nlm4svc_callback(rqstp, NLMPROC_TEST_RES, &res);
	return stat;
}

static int
nlm4svc_proc_lock_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void	     *resp)
{
	struct nlm_res	res;
	u32		stat;

	dprintk("lockd: LOCK_MSG      called\n");
	memset(&res, 0, sizeof(res));

	if ((stat = nlm4svc_proc_lock(rqstp, argp, &res)) == 0)
		stat = nlm4svc_callback(rqstp, NLMPROC_LOCK_RES, &res);
	return stat;
}

static int
nlm4svc_proc_cancel_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
					       void	       *resp)
{
	struct nlm_res	res;
	u32		stat;

	dprintk("lockd: CANCEL_MSG    called\n");
	memset(&res, 0, sizeof(res));

	if ((stat = nlm4svc_proc_cancel(rqstp, argp, &res)) == 0)
		stat = nlm4svc_callback(rqstp, NLMPROC_CANCEL_RES, &res);
	return stat;
}

static int
nlm4svc_proc_unlock_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
                                               void            *resp)
{
	struct nlm_res	res;
	u32		stat;

	dprintk("lockd: UNLOCK_MSG    called\n");
	memset(&res, 0, sizeof(res));

	if ((stat = nlm4svc_proc_unlock(rqstp, argp, &res)) == 0)
		stat = nlm4svc_callback(rqstp, NLMPROC_UNLOCK_RES, &res);
	return stat;
}

static int
nlm4svc_proc_granted_msg(struct svc_rqst *rqstp, struct nlm_args *argp,
                                                void            *resp)
{
	struct nlm_res	res;
	u32		stat;

	dprintk("lockd: GRANTED_MSG   called\n");
	memset(&res, 0, sizeof(res));

	if ((stat = nlm4svc_proc_granted(rqstp, argp, &res)) == 0)
		stat = nlm4svc_callback(rqstp, NLMPROC_GRANTED_RES, &res);
	return stat;
}

/*
 * SHARE: create a DOS share or alter existing share.
 */
static int
nlm4svc_proc_share(struct svc_rqst *rqstp, struct nlm_args *argp,
				          struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: SHARE         called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (nlmsvc_grace_period && !argp->reclaim) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

	/* Now try to create the share */
	resp->status = nlmsvc_share_file(host, file, argp);

	dprintk("lockd: SHARE         status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * UNSHARE: Release a DOS share.
 */
static int
nlm4svc_proc_unshare(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: UNSHARE       called\n");

	resp->cookie = argp->cookie;

	/* Don't accept requests during grace period */
	if (nlmsvc_grace_period) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlm4svc_retrieve_args(rqstp, argp, &host, &file)))
		return rpc_success;

	/* Now try to lock the file */
	resp->status = nlmsvc_unshare_file(host, file, argp);

	dprintk("lockd: UNSHARE       status %d\n", ntohl(resp->status));
	nlm_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * NM_LOCK: Create an unmonitored lock
 */
static int
nlm4svc_proc_nm_lock(struct svc_rqst *rqstp, struct nlm_args *argp,
				            struct nlm_res  *resp)
{
	dprintk("lockd: NM_LOCK       called\n");

	argp->monitor = 0;		/* just clean the monitor flag */
	return nlm4svc_proc_lock(rqstp, argp, resp);
}

/*
 * FREE_ALL: Release all locks and shares held by client
 */
static int
nlm4svc_proc_free_all(struct svc_rqst *rqstp, struct nlm_args *argp,
					     void            *resp)
{
	struct nlm_host	*host;

	/* Obtain client */
	if (nlm4svc_retrieve_args(rqstp, argp, &host, NULL))
		return rpc_success;

	nlmsvc_free_host_resources(host);
	nlm_release_host(host);
	return rpc_success;
}

/*
 * SM_NOTIFY: private callback from statd (not part of official NLM proto)
 */
static int
nlm4svc_proc_sm_notify(struct svc_rqst *rqstp, struct nlm_reboot *argp,
					      void	        *resp)
{
	struct sockaddr_in	saddr = rqstp->rq_addr;
	int			vers = argp->vers;
	int			prot = argp->proto >> 1;

	struct nlm_host		*host;

	dprintk("lockd: SM_NOTIFY     called\n");
	if (saddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK)
	 || ntohs(saddr.sin_port) >= 1024) {
		printk(KERN_WARNING
			"lockd: rejected NSM callback from %08x:%d\n",
			ntohl(rqstp->rq_addr.sin_addr.s_addr),
			ntohs(rqstp->rq_addr.sin_port));
		return rpc_system_err;
	}

	/* Obtain the host pointer for this NFS server and try to
	 * reclaim all locks we hold on this server.
	 */
	saddr.sin_addr.s_addr = argp->addr;

	if ((argp->proto & 1)==0) {
		if ((host = nlmclnt_lookup_host(&saddr, prot, vers)) != NULL) {
			nlmclnt_recovery(host, argp->state);
			nlm_release_host(host);
		}
	} else {
		/* If we run on an NFS server, delete all locks held by the client */

		if ((host = nlm_lookup_host(1, &saddr, prot, vers)) != NULL) {
			nlmsvc_free_host_resources(host);
			nlm_release_host(host);
		}
	}
	return rpc_success;
}

/*
 * client sent a GRANTED_RES, let's remove the associated block
 */
static int
nlm4svc_proc_granted_res(struct svc_rqst *rqstp, struct nlm_res  *argp,
                                                void            *resp)
{
        if (!nlmsvc_ops)
                return rpc_success;

        dprintk("lockd: GRANTED_RES   called\n");

        nlmsvc_grant_reply(rqstp, &argp->cookie, argp->status);
        return rpc_success;
}



/*
 * This is the generic lockd callback for async RPC calls
 */
static u32
nlm4svc_callback(struct svc_rqst *rqstp, u32 proc, struct nlm_res *resp)
{
	struct nlm_host	*host;
	struct nlm_rqst	*call;

	if (!(call = nlmclnt_alloc_call()))
		return rpc_system_err;

	host = nlmclnt_lookup_host(&rqstp->rq_addr,
				rqstp->rq_prot, rqstp->rq_vers);
	if (!host) {
		kfree(call);
		return rpc_system_err;
	}

	call->a_flags = RPC_TASK_ASYNC;
	call->a_host  = host;
	memcpy(&call->a_args, resp, sizeof(*resp));

	if (nlmsvc_async_call(call, proc, nlm4svc_callback_exit) < 0)
		goto error;

	return rpc_success;
 error:
	kfree(call);
	nlm_release_host(host);
	return rpc_system_err;
}

static void
nlm4svc_callback_exit(struct rpc_task *task)
{
	struct nlm_rqst	*call = (struct nlm_rqst *) task->tk_calldata;

	if (task->tk_status < 0) {
		dprintk("lockd: %4d callback failed (errno = %d)\n",
					task->tk_pid, -task->tk_status);
	}
	nlm_release_host(call->a_host);
	kfree(call);
}

/*
 * NLM Server procedures.
 */

#define nlm4svc_encode_norep	nlm4svc_encode_void
#define nlm4svc_decode_norep	nlm4svc_decode_void
#define nlm4svc_decode_testres	nlm4svc_decode_void
#define nlm4svc_decode_lockres	nlm4svc_decode_void
#define nlm4svc_decode_unlockres	nlm4svc_decode_void
#define nlm4svc_decode_cancelres	nlm4svc_decode_void
#define nlm4svc_decode_grantedres	nlm4svc_decode_void

#define nlm4svc_proc_none	nlm4svc_proc_null
#define nlm4svc_proc_test_res	nlm4svc_proc_null
#define nlm4svc_proc_lock_res	nlm4svc_proc_null
#define nlm4svc_proc_cancel_res	nlm4svc_proc_null
#define nlm4svc_proc_unlock_res	nlm4svc_proc_null

struct nlm_void			{ int dummy; };

#define PROC(name, xargt, xrest, argt, rest, respsize)	\
 { .pc_func	= (svc_procfunc) nlm4svc_proc_##name,	\
   .pc_decode	= (kxdrproc_t) nlm4svc_decode_##xargt,	\
   .pc_encode	= (kxdrproc_t) nlm4svc_encode_##xrest,	\
   .pc_release	= NULL,					\
   .pc_argsize	= sizeof(struct nlm_##argt),		\
   .pc_ressize	= sizeof(struct nlm_##rest),		\
   .pc_xdrressize = respsize,				\
 }
#define	Ck	(1+XDR_QUADLEN(NLM_MAXCOOKIELEN))	/* cookie */
#define	No	(1+1024/4)				/* netobj */
#define	St	1					/* status */
#define	Rg	4					/* range (offset + length) */
struct svc_procedure		nlmsvc_procedures4[] = {
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
  PROC(none,		void,		void,		void,	void, 0),
  PROC(none,		void,		void,		void,	void, 0),
  PROC(none,		void,		void,		void,	void, 0),
  PROC(share,		shareargs,	shareres,	args,	res, Ck+St+1),
  PROC(unshare,		shareargs,	shareres,	args,	res, Ck+St+1),
  PROC(nm_lock,		lockargs,	res,		args,	res, Ck+St),
  PROC(free_all,	notify,		void,		args,	void, 1),

};
