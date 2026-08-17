// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sunrpc/svc_xprt.h>

#include "lockd.h"

/*
 * xdr.h defines SM_MAXSTRLEN and SM_PRIV_SIZE as macros.
 * nlm4xdr_gen.h defines them as enum constants. Undefine the
 * macros to allow the xdrgen enum definitions to be used.
 */
#undef SM_MAXSTRLEN
#undef SM_PRIV_SIZE

#include "share.h"
#include "nlm4xdr_gen.h"

/*
 * Wrapper structures combine xdrgen types with legacy lockd_lock.
 * The xdrgen field must be first so the structure can be cast
 * to its XDR type for the RPC dispatch layer.
 */
struct nlm4_testargs_wrapper {
	struct nlm4_testargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm4_testargs_wrapper, xdrgen) == 0);

struct nlm4_lockargs_wrapper {
	struct nlm4_lockargs		xdrgen;
	struct lockd_cookie		cookie;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm4_lockargs_wrapper, xdrgen) == 0);

struct nlm4_cancargs_wrapper {
	struct nlm4_cancargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm4_cancargs_wrapper, xdrgen) == 0);

struct nlm4_unlockargs_wrapper {
	struct nlm4_unlockargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm4_unlockargs_wrapper, xdrgen) == 0);

struct nlm4_notifyargs_wrapper {
	struct nlm4_notifyargs		xdrgen;
	struct lockd_reboot		reboot;
};

static_assert(offsetof(struct nlm4_notifyargs_wrapper, xdrgen) == 0);

struct nlm4_notify_wrapper {
	struct nlm4_notify		xdrgen;
};

static_assert(offsetof(struct nlm4_notify_wrapper, xdrgen) == 0);

struct nlm4_testres_wrapper {
	struct nlm4_testres		xdrgen;
	struct lockd_lock		lock;
};

struct nlm4_shareargs_wrapper {
	struct nlm4_shareargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm4_shareargs_wrapper, xdrgen) == 0);

static_assert(offsetof(struct nlm4_testres_wrapper, xdrgen) == 0);

struct nlm4_res_wrapper {
	struct nlm4_res			xdrgen;
	struct lockd_cookie		cookie;
};

static_assert(offsetof(struct nlm4_res_wrapper, xdrgen) == 0);

struct nlm4_shareres_wrapper {
	struct nlm4_shareres		xdrgen;
};

static_assert(offsetof(struct nlm4_shareres_wrapper, xdrgen) == 0);

static __be32
nlm4_netobj_to_cookie(struct lockd_cookie *cookie, netobj *object)
{
	if (object->len > NLM_MAXCOOKIELEN)
		return nlm_lck_denied_nolocks;
	cookie->len = object->len;
	memcpy(cookie->data, object->data, object->len);
	return nlm_granted;
}

static __be32
nlm4_lock_to_lockd_lock(struct lockd_lock *lock, struct nlm4_lock *alock)
{
	if (alock->fh.len > NFS_MAXFHSIZE)
		return nlm_lck_denied;
	lock->fh.size = alock->fh.len;
	memcpy(lock->fh.data, alock->fh.data, alock->fh.len);
	lock->oh.len = alock->oh.len;
	lock->oh.data = alock->oh.data;
	lock->svid = alock->svid;
	lockd_set_file_lock_range4(&lock->fl, alock->l_offset, alock->l_len);
	return nlm_granted;
}

static struct nlm_host *
nlm4svc_lookup_host(struct svc_rqst *rqstp, string caller, bool monitored)
{
	struct nlm_host *host;

	if (!nlmsvc_ops)
		return NULL;
	host = nlmsvc_lookup_host(rqstp, caller.data, caller.len);
	if (!host)
		return NULL;
	if (monitored && nsm_monitor(host) < 0) {
		nlmsvc_release_host(host);
		return NULL;
	}
	return host;
}

static __be32
nlm4svc_lookup_file(struct svc_rqst *rqstp, struct nlm_host *host,
		    struct lockd_lock *lock, struct nlm_file **filp,
		    struct nlm4_lock *xdr_lock, unsigned char type)
{
	bool is_test = (rqstp->rq_proc == NLMPROC4_TEST ||
			rqstp->rq_proc == NLMPROC4_TEST_MSG);
	struct file_lock *fl = &lock->fl;
	struct nlm_file *file = NULL;
	int mode;
	__be32 error;

	if (xdr_lock->fh.len > NFS_MAXFHSIZE)
		return nlm_lck_denied_nolocks;
	lock->fh.size = xdr_lock->fh.len;
	memcpy(lock->fh.data, xdr_lock->fh.data, xdr_lock->fh.len);
	if (xdr_lock->fh.len < LOCKD_FH_HASH_SIZE)
		memset(lock->fh.data + xdr_lock->fh.len, 0,
		       LOCKD_FH_HASH_SIZE - xdr_lock->fh.len);

	lock->oh.len = xdr_lock->oh.len;
	lock->oh.data = xdr_lock->oh.data;

	lock->svid = xdr_lock->svid;
	lock->lock_start = xdr_lock->l_offset;
	lock->lock_len = xdr_lock->l_len;

	if (lock->lock_start > OFFSET_MAX ||
	    (lock->lock_len && ((lock->lock_len - 1) > (OFFSET_MAX - lock->lock_start))))
		return nlm4_fbig;

	locks_init_lock(fl);
	fl->c.flc_type = type;
	lockd_set_file_lock_range4(fl, lock->lock_start, lock->lock_len);

	mode = is_test ? O_RDWR : lock_to_openmode(fl);
	error = nlm_lookup_file(rqstp, &file, lock, mode);
	switch (error) {
	case nlm_granted:
		break;
	case nlm__int__stale_fh:
		return nlm4_stale_fh;
	case nlm__int__failed:
		return nlm4_failed;
	default:
		return error;
	}
	*filp = file;

	fl->c.flc_flags = FL_POSIX;
	fl->c.flc_file = is_test ? nlmsvc_file_file(file)
				 : file->f_file[mode];
	fl->c.flc_pid = current->tgid;
	fl->fl_lmops = &nlmsvc_lock_operations;
	nlmsvc_locks_init_private(fl, host, (pid_t)lock->svid);
	if (!fl->c.flc_owner)
		return nlm_lck_denied_nolocks;

	return nlm_granted;
}

/**
 * nlm4svc_proc_null - NULL: Test for presence of service
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully
 *
 * RPC synopsis:
 *   void NLMPROC4_NULL(void) = 0;
 */
static __be32
nlm4svc_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

/**
 * nlm4svc_proc_test - TEST: Check for conflicting lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_testres NLMPROC4_TEST(nlm4_testargs) = 1;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The server would be able to grant the
 *				requested lock.
 *   %NLM4_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %NLM4_DENIED_NOLOCKS:	The server could not allocate the resources
 *				needed to process the request.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32 nlm4svc_proc_test(struct svc_rqst *rqstp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm4_testres_wrapper *resp = rqstp->rq_resp;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm4svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     type);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_testlock(rqstp, file, host,
						 &argp->lock, &resp->lock);
	nlmsvc_release_lockowner(&argp->lock);

	if (resp->xdrgen.stat.stat == nlm_lck_denied) {
		struct lockd_lock *conf = &resp->lock;
		struct nlm4_holder *holder = &resp->xdrgen.stat.u.holder;

		holder->exclusive = (conf->fl.c.flc_type != F_RDLCK);
		holder->svid = conf->svid;
		holder->oh.len = conf->oh.len;
		holder->oh.data = conf->oh.data;
		holder->l_offset = conf->fl.fl_start;
		if (conf->fl.fl_end == OFFSET_MAX)
			holder->l_len = 0;
		else
			holder->l_len = conf->fl.fl_end - conf->fl.fl_start + 1;
	}

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

static __be32
nlm4svc_do_lock(struct svc_rqst *rqstp, bool monitored)
{
	struct nlm4_lockargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm4_res_wrapper *resp = rqstp->rq_resp;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm4_netobj_to_cookie(&argp->cookie,
						       &argp->xdrgen.cookie);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name,
				   monitored);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm4svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     type);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_lock(rqstp, file, host, &argp->lock,
					     argp->xdrgen.block, &argp->cookie,
					     argp->xdrgen.reclaim);
	if (resp->xdrgen.stat.stat == nlm__int__deadlock)
		resp->xdrgen.stat.stat = nlm4_deadlock;

	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_lock - LOCK: Establish a monitored lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_res NLMPROC4_LOCK(nlm4_lockargs) = 2;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The requested lock was granted.
 *   %NLM4_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %NLM4_DENIED_NOLOCKS:	The server could not allocate the resources
 *				needed to process the request.
 *   %NLM4_BLOCKED:		The blocking request cannot be granted
 *				immediately. The server will send an
 *				NLMPROC4_GRANTED callback to the client when
 *				the lock can be granted.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DEADLCK:		The request could not be granted and
 *				blocking would cause a deadlock.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32
nlm4svc_proc_lock(struct svc_rqst *rqstp)
{
	return nlm4svc_do_lock(rqstp, true);
}

/**
 * nlm4svc_proc_cancel - CANCEL: Cancel an outstanding blocked lock request
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully
 *   %rpc_drop_reply:		Do not send an RPC reply
 *
 * RPC synopsis:
 *   nlm4_res NLMPROC4_CANCEL(nlm4_cancargs) = 3;
 *
 * Permissible procedure status codes:
 *   %NLM4_LCK_GRANTED:		The requested lock was canceled.
 *   %NLM4_LCK_DENIED:		There was no lock to cancel.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DENIED_NOLOCKS:	A needed resource could not be allocated.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32
nlm4svc_proc_cancel(struct svc_rqst *rqstp)
{
	struct nlm4_cancargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm4_res_wrapper *resp = rqstp->rq_resp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm4svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     type);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_cancel_blocked(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_unlock - UNLOCK: Remove a lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_res NLMPROC4_UNLOCK(nlm4_unlockargs) = 4;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The requested lock was released.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DENIED_NOLOCKS:	A needed resource could not be allocated.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32
nlm4svc_proc_unlock(struct svc_rqst *rqstp)
{
	struct nlm4_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm4_res_wrapper *resp = rqstp->rq_resp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm4svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     F_UNLCK);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_unlock(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_granted - GRANTED: Server grants a previously blocked lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *
 * RPC synopsis:
 *   nlm4_res NLMPROC4_GRANTED(nlm4_testargs) = 5;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The granted lock was accepted.
 *   %NLM4_DENIED:		The procedure failed, possibly due to
 *				internal resource constraints.
 *   %NLM4_DENIED_GRACE_PERIOD:	The client host recently restarted and
 *				its NLM is re-establishing existing locks,
 *				so it is not yet ready to accept callbacks.
 */
static __be32
nlm4svc_proc_granted(struct svc_rqst *rqstp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;
	struct nlm4_res_wrapper *resp = rqstp->rq_resp;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm4_lock_to_lockd_lock(&argp->lock,
							 &argp->xdrgen.alock);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmclnt_grant(svc_addr(rqstp), &argp->lock);

out:
	return rpc_success;
}

/*
 * This is the generic lockd callback for async RPC calls
 */
static void nlm4svc_callback_exit(struct rpc_task *task, void *data)
{
}

static void nlm4svc_callback_release(void *data)
{
	nlmsvc_release_call(data);
}

static const struct rpc_call_ops nlm4svc_callback_ops = {
	.rpc_call_done = nlm4svc_callback_exit,
	.rpc_release = nlm4svc_callback_release,
};

/*
 * Dispatch an async callback RPC to a client with a pre-resolved host.
 * Caller provides a reference to @host; this function takes ownership
 * and releases it via nlmsvc_release_host() before returning.
 */
static __be32
nlm4svc_callback(struct svc_rqst *rqstp, struct nlm_host *host, u32 proc,
		 __be32 (*func)(struct svc_rqst *,  struct lockd_res *))
{
	struct nlm_rqst	*call;
	__be32 stat;

	call = nlm_alloc_call(host);
	nlmsvc_release_host(host);
	if (call == NULL)
		return rpc_system_err;

	stat = func(rqstp, &call->a_res);
	if (stat != 0) {
		nlmsvc_release_call(call);
		return stat;
	}

	call->a_flags = RPC_TASK_ASYNC;
	if (nlm_async_reply(call, proc, &nlm4svc_callback_ops) < 0)
		return rpc_system_err;
	return rpc_success;
}

static __be32
__nlm4svc_proc_test_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_lockowner *owner;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm4_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm4svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	owner = argp->lock.fl.c.flc_owner;
	resp->status = nlmsvc_testlock(rqstp, file, host, &argp->lock,
				       &resp->lock);
	nlmsvc_put_lockowner(owner);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ? rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_test_msg - TEST_MSG: Check for conflicting lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC4_TEST_MSG(nlm4_testargs) = 6;
 *
 * The response to this request is delivered via the TEST_RES procedure.
 */
static __be32 nlm4svc_proc_test_msg(struct svc_rqst *rqstp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlm4svc_callback(rqstp, host, NLMPROC4_TEST_RES,
				__nlm4svc_proc_test_msg);
}

static __be32
__nlm4svc_proc_lock_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm4_lockargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm4_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, true);
	if (!host)
		goto out;

	resp->status = nlm4svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	resp->status = nlmsvc_lock(rqstp, file, host, &argp->lock,
				   argp->xdrgen.block, &resp->cookie,
				   argp->xdrgen.reclaim);
	if (resp->status == nlm__int__deadlock)
		resp->status = nlm4_deadlock;
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_lock_msg - LOCK_MSG: Establish a monitored lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC4_LOCK_MSG(nlm4_lockargs) = 7;
 *
 * The response to this request is delivered via the LOCK_RES procedure.
 */
static __be32 nlm4svc_proc_lock_msg(struct svc_rqst *rqstp)
{
	struct nlm4_lockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlm4svc_callback(rqstp, host, NLMPROC4_LOCK_RES,
				__nlm4svc_proc_lock_msg);
}

static __be32
__nlm4svc_proc_cancel_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm4_cancargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct net *net = SVC_NET(rqstp);
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm4_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	resp->status = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->status = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm4svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	resp->status = nlmsvc_cancel_blocked(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_cancel_msg - CANCEL_MSG: Cancel an outstanding lock request
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC4_CANCEL_MSG(nlm4_cancargs) = 8;
 *
 * The response to this request is delivered via the CANCEL_RES procedure.
 */
static __be32 nlm4svc_proc_cancel_msg(struct svc_rqst *rqstp)
{
	struct nlm4_cancargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlm4svc_callback(rqstp, host, NLMPROC4_CANCEL_RES,
				__nlm4svc_proc_cancel_msg);
}

static __be32
__nlm4svc_proc_unlock_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm4_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm4_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	resp->status = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->status = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm4svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, F_UNLCK);
	if (resp->status)
		goto out;

	resp->status = nlmsvc_unlock(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_unlock_msg - UNLOCK_MSG: Remove an existing lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC4_UNLOCK_MSG(nlm4_unlockargs) = 9;
 *
 * The response to this request is delivered via the UNLOCK_RES procedure.
 */
static __be32 nlm4svc_proc_unlock_msg(struct svc_rqst *rqstp)
{
	struct nlm4_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlm4svc_callback(rqstp, host, NLMPROC4_UNLOCK_RES,
				__nlm4svc_proc_unlock_msg);
}

static __be32
__nlm4svc_proc_granted_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;

	resp->status = nlm_lck_denied;
	if (nlm4_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	if (nlm4_lock_to_lockd_lock(&argp->lock, &argp->xdrgen.alock))
		goto out;

	resp->status = nlmclnt_grant(svc_addr(rqstp), &argp->lock);

out:
	return rpc_success;
}

/**
 * nlm4svc_proc_granted_msg - GRANTED_MSG: Blocked lock has been granted
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC4_GRANTED_MSG(nlm4_testargs) = 10;
 *
 * The response to this request is delivered via the GRANTED_RES procedure.
 */
static __be32 nlm4svc_proc_granted_msg(struct svc_rqst *rqstp)
{
	struct nlm4_testargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlm4svc_callback(rqstp, host, NLMPROC4_GRANTED_RES,
				__nlm4svc_proc_granted_msg);
}

/**
 * nlm4svc_proc_granted_res - GRANTED_RES: Lock Granted result
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *
 * RPC synopsis:
 *   void NLMPROC4_GRANTED_RES(nlm4_res) = 15;
 */
static __be32 nlm4svc_proc_granted_res(struct svc_rqst *rqstp)
{
	struct nlm4_res_wrapper *argp = rqstp->rq_argp;

	if (!nlmsvc_ops)
		return rpc_success;

	if (nlm4_netobj_to_cookie(&argp->cookie, &argp->xdrgen.cookie))
		return rpc_success;
	nlmsvc_grant_reply(&argp->cookie, argp->xdrgen.stat.stat);

	return rpc_success;
}

/**
 * nlm4svc_proc_sm_notify - SM_NOTIFY: Peer has rebooted
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_system_err:		RPC execution failed.
 *
 * The SM_NOTIFY procedure is a private callback from Linux statd and is
 * not part of the official NLM protocol.
 *
 * RPC synopsis:
 *   void NLMPROC4_SM_NOTIFY(nlm4_notifyargs) = 16;
 */
static __be32 nlm4svc_proc_sm_notify(struct svc_rqst *rqstp)
{
	struct nlm4_notifyargs_wrapper *argp = rqstp->rq_argp;
	struct lockd_reboot *reboot = &argp->reboot;

	if (!nlm_privileged_requester(rqstp)) {
		char buf[RPC_MAX_ADDRBUFLEN];

		pr_warn("lockd: rejected NSM callback from %s\n",
			svc_print_addr(rqstp, buf, sizeof(buf)));
		return rpc_system_err;
	}

	reboot->len = argp->xdrgen.notify.name.len;
	reboot->mon = (char *)argp->xdrgen.notify.name.data;
	reboot->state = argp->xdrgen.notify.state;
	memcpy(&reboot->priv.data, argp->xdrgen.private,
	       sizeof(reboot->priv.data));

	nlm_host_rebooted(SVC_NET(rqstp), reboot);

	return rpc_success;
}

/**
 * nlm4svc_proc_unused - stub for unused procedures
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_proc_unavail:	Program can't support procedure.
 */
static __be32 nlm4svc_proc_unused(struct svc_rqst *rqstp)
{
	return rpc_proc_unavail;
}

/**
 * nlm4svc_proc_share - SHARE: Open a file using DOS file-sharing modes
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_shareres NLMPROC4_SHARE(nlm4_shareargs) = 20;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The requested share lock was granted.
 *   %NLM4_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DENIED_NOLOCKS:	A needed resource could not be allocated.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32 nlm4svc_proc_share(struct svc_rqst *rqstp)
{
	struct nlm4_shareargs_wrapper *argp = rqstp->rq_argp;
	struct nlm4_shareres_wrapper *resp = rqstp->rq_resp;
	struct lockd_lock *lock = &argp->lock;
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;
	struct nlm4_lock xdr_lock = {
		.fh		= argp->xdrgen.share.fh,
		.oh		= argp->xdrgen.share.oh,
		.svid		= LOCKD_SHARE_SVID,
	};

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(SVC_NET(rqstp)) && !argp->xdrgen.reclaim)
		goto out;

	resp->xdrgen.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.share.caller_name, true);
	if (!host)
		goto out;

	resp->xdrgen.stat = nlm4svc_lookup_file(rqstp, host, lock, &file,
						&xdr_lock, F_RDLCK);
	if (resp->xdrgen.stat)
		goto out;

	resp->xdrgen.stat = nlmsvc_share_file(host, file, &lock->oh,
					      argp->xdrgen.share.access,
					      argp->xdrgen.share.mode);

	nlmsvc_release_lockowner(lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_unshare - UNSHARE: Release a share reservation
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_shareres NLMPROC4_UNSHARE(nlm4_shareargs) = 21;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The share reservation was released.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DENIED_NOLOCKS:	A needed resource could not be allocated.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32 nlm4svc_proc_unshare(struct svc_rqst *rqstp)
{
	struct nlm4_shareargs_wrapper *argp = rqstp->rq_argp;
	struct nlm4_shareres_wrapper *resp = rqstp->rq_resp;
	struct lockd_lock *lock = &argp->lock;
	struct nlm4_lock xdr_lock = {
		.fh		= argp->xdrgen.share.fh,
		.oh		= argp->xdrgen.share.oh,
		.svid		= LOCKD_SHARE_SVID,
	};
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(SVC_NET(rqstp)))
		goto out;

	resp->xdrgen.stat = nlm_lck_denied_nolocks;
	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.share.caller_name, true);
	if (!host)
		goto out;

	resp->xdrgen.stat = nlm4svc_lookup_file(rqstp, host, lock, &file,
						&xdr_lock, F_RDLCK);
	if (resp->xdrgen.stat)
		goto out;

	resp->xdrgen.stat = nlmsvc_unshare_file(host, file, &lock->oh);

	nlmsvc_release_lockowner(lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlm4svc_proc_nm_lock - NM_LOCK: Establish a non-monitored lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm4_res NLMPROC4_NM_LOCK(nlm4_lockargs) = 22;
 *
 * Permissible procedure status codes:
 *   %NLM4_GRANTED:		The requested lock was granted.
 *   %NLM4_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %NLM4_DENIED_NOLOCKS:	The server could not allocate the resources
 *				needed to process the request.
 *   %NLM4_BLOCKED:		The blocking request cannot be granted
 *				immediately. The server will send an
 *				NLMPROC4_GRANTED callback to the client when
 *				the lock can be granted.
 *   %NLM4_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %NLM4_DEADLCK:		The request could not be granted and
 *				blocking would cause a deadlock.
 *   %NLM4_STALE_FH:		The request specified an invalid file handle.
 *   %NLM4_FBIG:		The request specified a length or offset
 *				that exceeds the range supported by the
 *				server.
 *   %NLM4_FAILED:		The request failed for an unspecified reason.
 */
static __be32 nlm4svc_proc_nm_lock(struct svc_rqst *rqstp)
{
	return nlm4svc_do_lock(rqstp, false);
}

/**
 * nlm4svc_proc_free_all - FREE_ALL: Discard client's lock and share state
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *
 * RPC synopsis:
 *   void NLMPROC4_FREE_ALL(nlm4_notify) = 23;
 */
static __be32 nlm4svc_proc_free_all(struct svc_rqst *rqstp)
{
	struct nlm4_notify_wrapper *argp = rqstp->rq_argp;
	struct nlm_host	*host;

	host = nlm4svc_lookup_host(rqstp, argp->xdrgen.name, false);
	if (!host)
		goto out;

	nlmsvc_free_host_resources(host);

	nlmsvc_release_host(host);

out:
	return rpc_success;
}


/*
 * NLMv4 Server procedures.
 */

static const struct svc_procedure nlm4svc_procedures[24] = {
	[NLMPROC4_NULL] = {
		.pc_func	= nlm4svc_proc_null,
		.pc_decode	= nlm4_svc_decode_void,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= XDR_void,
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "NULL",
	},
	[NLMPROC4_TEST] = {
		.pc_func	= nlm4svc_proc_test,
		.pc_decode	= nlm4_svc_decode_nlm4_testargs,
		.pc_encode	= nlm4_svc_encode_nlm4_testres,
		.pc_argsize	= sizeof(struct nlm4_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_testres_wrapper),
		.pc_xdrressize	= NLM4_nlm4_testres_sz,
		.pc_name	= "TEST",
	},
	[NLMPROC4_LOCK] = {
		.pc_func	= nlm4svc_proc_lock,
		.pc_decode	= nlm4_svc_decode_nlm4_lockargs,
		.pc_encode	= nlm4_svc_encode_nlm4_res,
		.pc_argsize	= sizeof(struct nlm4_lockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_res_wrapper),
		.pc_xdrressize	= NLM4_nlm4_res_sz,
		.pc_name	= "LOCK",
	},
	[NLMPROC4_CANCEL] = {
		.pc_func	= nlm4svc_proc_cancel,
		.pc_decode	= nlm4_svc_decode_nlm4_cancargs,
		.pc_encode	= nlm4_svc_encode_nlm4_res,
		.pc_argsize	= sizeof(struct nlm4_cancargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_res_wrapper),
		.pc_xdrressize	= NLM4_nlm4_res_sz,
		.pc_name	= "CANCEL",
	},
	[NLMPROC4_UNLOCK] = {
		.pc_func	= nlm4svc_proc_unlock,
		.pc_decode	= nlm4_svc_decode_nlm4_unlockargs,
		.pc_encode	= nlm4_svc_encode_nlm4_res,
		.pc_argsize	= sizeof(struct nlm4_unlockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_res_wrapper),
		.pc_xdrressize	= NLM4_nlm4_res_sz,
		.pc_name	= "UNLOCK",
	},
	[NLMPROC4_GRANTED] = {
		.pc_func	= nlm4svc_proc_granted,
		.pc_decode	= nlm4_svc_decode_nlm4_testargs,
		.pc_encode	= nlm4_svc_encode_nlm4_res,
		.pc_argsize	= sizeof(struct nlm4_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_res_wrapper),
		.pc_xdrressize	= NLM4_nlm4_res_sz,
		.pc_name	= "GRANTED",
	},
	[NLMPROC4_TEST_MSG] = {
		.pc_func	= nlm4svc_proc_test_msg,
		.pc_decode	= nlm4_svc_decode_nlm4_testargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "TEST_MSG",
	},
	[NLMPROC4_LOCK_MSG] = {
		.pc_func	= nlm4svc_proc_lock_msg,
		.pc_decode	= nlm4_svc_decode_nlm4_lockargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_lockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "LOCK_MSG",
	},
	[NLMPROC4_CANCEL_MSG] = {
		.pc_func	= nlm4svc_proc_cancel_msg,
		.pc_decode	= nlm4_svc_decode_nlm4_cancargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_cancargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "CANCEL_MSG",
	},
	[NLMPROC4_UNLOCK_MSG] = {
		.pc_func	= nlm4svc_proc_unlock_msg,
		.pc_decode	= nlm4_svc_decode_nlm4_unlockargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_unlockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNLOCK_MSG",
	},
	[NLMPROC4_GRANTED_MSG] = {
		.pc_func	= nlm4svc_proc_granted_msg,
		.pc_decode	= nlm4_svc_decode_nlm4_testargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "GRANTED_MSG",
	},
	[NLMPROC4_TEST_RES] = {
		.pc_func	= nlm4svc_proc_null,
		.pc_decode	= nlm4_svc_decode_nlm4_testres,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_testres),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "TEST_RES",
	},
	[NLMPROC4_LOCK_RES] = {
		.pc_func	= nlm4svc_proc_null,
		.pc_decode	= nlm4_svc_decode_nlm4_res,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_res),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "LOCK_RES",
	},
	[NLMPROC4_CANCEL_RES] = {
		.pc_func	= nlm4svc_proc_null,
		.pc_decode	= nlm4_svc_decode_nlm4_res,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_res),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "CANCEL_RES",
	},
	[NLMPROC4_UNLOCK_RES] = {
		.pc_func	= nlm4svc_proc_null,
		.pc_decode	= nlm4_svc_decode_nlm4_res,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_res),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNLOCK_RES",
	},
	[NLMPROC4_GRANTED_RES] = {
		.pc_func	= nlm4svc_proc_granted_res,
		.pc_decode	= nlm4_svc_decode_nlm4_res,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_res_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "GRANTED_RES",
	},
	[NLMPROC4_SM_NOTIFY] = {
		.pc_func	= nlm4svc_proc_sm_notify,
		.pc_decode	= nlm4_svc_decode_nlm4_notifyargs,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_notifyargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "SM_NOTIFY",
	},
	[17] = {
		.pc_func	= nlm4svc_proc_unused,
		.pc_decode	= nlm4_svc_decode_void,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= 0,
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNUSED",
	},
	[18] = {
		.pc_func	= nlm4svc_proc_unused,
		.pc_decode	= nlm4_svc_decode_void,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= 0,
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNUSED",
	},
	[19] = {
		.pc_func	= nlm4svc_proc_unused,
		.pc_decode	= nlm4_svc_decode_void,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= 0,
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNUSED",
	},
	[NLMPROC4_SHARE] = {
		.pc_func	= nlm4svc_proc_share,
		.pc_decode	= nlm4_svc_decode_nlm4_shareargs,
		.pc_encode	= nlm4_svc_encode_nlm4_shareres,
		.pc_argsize	= sizeof(struct nlm4_shareargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_shareres_wrapper),
		.pc_xdrressize	= NLM4_nlm4_shareres_sz,
		.pc_name	= "SHARE",
	},
	[NLMPROC4_UNSHARE] = {
		.pc_func	= nlm4svc_proc_unshare,
		.pc_decode	= nlm4_svc_decode_nlm4_shareargs,
		.pc_encode	= nlm4_svc_encode_nlm4_shareres,
		.pc_argsize	= sizeof(struct nlm4_shareargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_shareres_wrapper),
		.pc_xdrressize	= NLM4_nlm4_shareres_sz,
		.pc_name	= "UNSHARE",
	},
	[NLMPROC4_NM_LOCK] = {
		.pc_func	= nlm4svc_proc_nm_lock,
		.pc_decode	= nlm4_svc_decode_nlm4_lockargs,
		.pc_encode	= nlm4_svc_encode_nlm4_res,
		.pc_argsize	= sizeof(struct nlm4_lockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm4_res_wrapper),
		.pc_xdrressize	= NLM4_nlm4_res_sz,
		.pc_name	= "NM_LOCK",
	},
	[NLMPROC4_FREE_ALL] = {
		.pc_func	= nlm4svc_proc_free_all,
		.pc_decode	= nlm4_svc_decode_nlm4_notify,
		.pc_encode	= nlm4_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm4_notify_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "FREE_ALL",
	},
};

/*
 * Storage requirements for XDR arguments and results
 */
union nlm4svc_xdrstore {
	struct nlm4_testargs_wrapper	testargs;
	struct nlm4_lockargs_wrapper	lockargs;
	struct nlm4_cancargs_wrapper	cancargs;
	struct nlm4_unlockargs_wrapper	unlockargs;
	struct nlm4_notifyargs_wrapper	notifyargs;
	struct nlm4_shareargs_wrapper	shareargs;
	struct nlm4_notify_wrapper	notify;
	struct nlm4_testres_wrapper	testres;
	struct nlm4_res_wrapper		res;
	struct nlm4_shareres_wrapper	shareres;
};

static DEFINE_PER_CPU_ALIGNED(unsigned long,
			      nlm4svc_call_counters[ARRAY_SIZE(nlm4svc_procedures)]);

const struct svc_version nlmsvc_version4 = {
	.vs_vers	= 4,
	.vs_nproc	= ARRAY_SIZE(nlm4svc_procedures),
	.vs_proc	= nlm4svc_procedures,
	.vs_count	= nlm4svc_call_counters,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= sizeof(union nlm4svc_xdrstore),
};
