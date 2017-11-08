/*
 * linux/fs/nfs/callback.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback handling
 */

#include <linux/completion.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfs_fs.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/bc_xprt.h>

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"
#include "netns.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_serv *serv;
};

static struct nfs_callback_data nfs_callback_info[NFS4_MAX_MINOR_VERSION + 1];
static DEFINE_MUTEX(nfs_callback_mutex);
static struct svc_program nfs4_callback_program;

static int nfs4_callback_up_net(struct svc_serv *serv, struct net *net)
{
	int ret;
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	ret = svc_create_xprt(serv, "tcp", net, PF_INET,
				nfs_callback_set_tcpport, SVC_SOCK_ANONYMOUS);
	if (ret <= 0)
		goto out_err;
	nn->nfs_callback_tcpport = ret;
	dprintk("NFS: Callback listener port = %u (af %u, net %x)\n",
		nn->nfs_callback_tcpport, PF_INET, net->ns.inum);

	ret = svc_create_xprt(serv, "tcp", net, PF_INET6,
				nfs_callback_set_tcpport, SVC_SOCK_ANONYMOUS);
	if (ret > 0) {
		nn->nfs_callback_tcpport6 = ret;
		dprintk("NFS: Callback listener port = %u (af %u, net %x\n",
			nn->nfs_callback_tcpport6, PF_INET6, net->ns.inum);
	} else if (ret != -EAFNOSUPPORT)
		goto out_err;
	return 0;

out_err:
	return (ret) ? ret : -ENOMEM;
}

/*
 * This is the NFSv4 callback kernel thread.
 */
static int
nfs4_callback_svc(void *vrqstp)
{
	int err;
	struct svc_rqst *rqstp = vrqstp;

	set_freezable();

	while (!kthread_freezable_should_stop(NULL)) {

		if (signal_pending(current))
			flush_signals(current);
		/*
		 * Listen for a request on the socket
		 */
		err = svc_recv(rqstp, MAX_SCHEDULE_TIMEOUT);
		if (err == -EAGAIN || err == -EINTR)
			continue;
		svc_process(rqstp);
	}
	svc_exit_thread(rqstp);
	module_put_and_exit(0);
	return 0;
}

#if defined(CONFIG_NFS_V4_1)
/*
 * The callback service for NFSv4.1 callbacks
 */
static int
nfs41_callback_svc(void *vrqstp)
{
	struct svc_rqst *rqstp = vrqstp;
	struct svc_serv *serv = rqstp->rq_server;
	struct rpc_rqst *req;
	int error;
	DEFINE_WAIT(wq);

	set_freezable();

	while (!kthread_freezable_should_stop(NULL)) {

		if (signal_pending(current))
			flush_signals(current);

		prepare_to_wait(&serv->sv_cb_waitq, &wq, TASK_INTERRUPTIBLE);
		spin_lock_bh(&serv->sv_cb_lock);
		if (!list_empty(&serv->sv_cb_list)) {
			req = list_first_entry(&serv->sv_cb_list,
					struct rpc_rqst, rq_bc_list);
			list_del(&req->rq_bc_list);
			spin_unlock_bh(&serv->sv_cb_lock);
			finish_wait(&serv->sv_cb_waitq, &wq);
			dprintk("Invoking bc_svc_process()\n");
			error = bc_svc_process(serv, req, rqstp);
			dprintk("bc_svc_process() returned w/ error code= %d\n",
				error);
		} else {
			spin_unlock_bh(&serv->sv_cb_lock);
			if (!kthread_should_stop())
				schedule();
			finish_wait(&serv->sv_cb_waitq, &wq);
		}
	}
	svc_exit_thread(rqstp);
	module_put_and_exit(0);
	return 0;
}

static inline void nfs_callback_bc_serv(u32 minorversion, struct rpc_xprt *xprt,
		struct svc_serv *serv)
{
	if (minorversion)
		/*
		 * Save the svc_serv in the transport so that it can
		 * be referenced when the session backchannel is initialized
		 */
		xprt->bc_serv = serv;
}
#else
static inline void nfs_callback_bc_serv(u32 minorversion, struct rpc_xprt *xprt,
		struct svc_serv *serv)
{
}
#endif /* CONFIG_NFS_V4_1 */

static int nfs_callback_start_svc(int minorversion, struct rpc_xprt *xprt,
				  struct svc_serv *serv)
{
	int nrservs = nfs_callback_nr_threads;
	int ret;

	nfs_callback_bc_serv(minorversion, xprt, serv);

	if (nrservs < NFS4_MIN_NR_CALLBACK_THREADS)
		nrservs = NFS4_MIN_NR_CALLBACK_THREADS;

	if (serv->sv_nrthreads-1 == nrservs)
		return 0;

	ret = serv->sv_ops->svo_setup(serv, NULL, nrservs);
	if (ret) {
		serv->sv_ops->svo_setup(serv, NULL, 0);
		return ret;
	}
	dprintk("nfs_callback_up: service started\n");
	return 0;
}

static void nfs_callback_down_net(u32 minorversion, struct svc_serv *serv, struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	if (--nn->cb_users[minorversion])
		return;

	dprintk("NFS: destroy per-net callback data; net=%x\n", net->ns.inum);
	svc_shutdown_net(serv, net);
}

static int nfs_callback_up_net(int minorversion, struct svc_serv *serv,
			       struct net *net, struct rpc_xprt *xprt)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	int ret;

	if (nn->cb_users[minorversion]++)
		return 0;

	dprintk("NFS: create per-net callback data; net=%x\n", net->ns.inum);

	ret = svc_bind(serv, net);
	if (ret < 0) {
		printk(KERN_WARNING "NFS: bind callback service failed\n");
		goto err_bind;
	}

	ret = -EPROTONOSUPPORT;
	if (!IS_ENABLED(CONFIG_NFS_V4_1) || minorversion == 0)
		ret = nfs4_callback_up_net(serv, net);
	else if (xprt->ops->bc_up)
		ret = xprt->ops->bc_up(serv, net);

	if (ret < 0) {
		printk(KERN_ERR "NFS: callback service start failed\n");
		goto err_socks;
	}
	return 0;

err_socks:
	svc_rpcb_cleanup(serv, net);
err_bind:
	nn->cb_users[minorversion]--;
	dprintk("NFS: Couldn't create callback socket: err = %d; "
			"net = %x\n", ret, net->ns.inum);
	return ret;
}

static const struct svc_serv_ops nfs40_cb_sv_ops = {
	.svo_function		= nfs4_callback_svc,
	.svo_enqueue_xprt	= svc_xprt_do_enqueue,
	.svo_setup		= svc_set_num_threads_sync,
	.svo_module		= THIS_MODULE,
};
#if defined(CONFIG_NFS_V4_1)
static const struct svc_serv_ops nfs41_cb_sv_ops = {
	.svo_function		= nfs41_callback_svc,
	.svo_enqueue_xprt	= svc_xprt_do_enqueue,
	.svo_setup		= svc_set_num_threads_sync,
	.svo_module		= THIS_MODULE,
};

static const struct svc_serv_ops *nfs4_cb_sv_ops[] = {
	[0] = &nfs40_cb_sv_ops,
	[1] = &nfs41_cb_sv_ops,
};
#else
static const struct svc_serv_ops *nfs4_cb_sv_ops[] = {
	[0] = &nfs40_cb_sv_ops,
	[1] = NULL,
};
#endif

static struct svc_serv *nfs_callback_create_svc(int minorversion)
{
	struct nfs_callback_data *cb_info = &nfs_callback_info[minorversion];
	const struct svc_serv_ops *sv_ops;
	struct svc_serv *serv;

	/*
	 * Check whether we're already up and running.
	 */
	if (cb_info->serv) {
		/*
		 * Note: increase service usage, because later in case of error
		 * svc_destroy() will be called.
		 */
		svc_get(cb_info->serv);
		return cb_info->serv;
	}

	switch (minorversion) {
	case 0:
		sv_ops = nfs4_cb_sv_ops[0];
		break;
	default:
		sv_ops = nfs4_cb_sv_ops[1];
	}

	if (sv_ops == NULL)
		return ERR_PTR(-ENOTSUPP);

	/*
	 * Sanity check: if there's no task,
	 * we should be the first user ...
	 */
	if (cb_info->users)
		printk(KERN_WARNING "nfs_callback_create_svc: no kthread, %d users??\n",
			cb_info->users);

	serv = svc_create_pooled(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE, sv_ops);
	if (!serv) {
		printk(KERN_ERR "nfs_callback_create_svc: create service failed\n");
		return ERR_PTR(-ENOMEM);
	}
	cb_info->serv = serv;
	/* As there is only one thread we need to over-ride the
	 * default maximum of 80 connections
	 */
	serv->sv_maxconn = 1024;
	dprintk("nfs_callback_create_svc: service created\n");
	return serv;
}

/*
 * Bring up the callback thread if it is not already up.
 */
int nfs_callback_up(u32 minorversion, struct rpc_xprt *xprt)
{
	struct svc_serv *serv;
	struct nfs_callback_data *cb_info = &nfs_callback_info[minorversion];
	int ret;
	struct net *net = xprt->xprt_net;

	mutex_lock(&nfs_callback_mutex);

	serv = nfs_callback_create_svc(minorversion);
	if (IS_ERR(serv)) {
		ret = PTR_ERR(serv);
		goto err_create;
	}

	ret = nfs_callback_up_net(minorversion, serv, net, xprt);
	if (ret < 0)
		goto err_net;

	ret = nfs_callback_start_svc(minorversion, xprt, serv);
	if (ret < 0)
		goto err_start;

	cb_info->users++;
	/*
	 * svc_create creates the svc_serv with sv_nrthreads == 1, and then
	 * svc_prepare_thread increments that. So we need to call svc_destroy
	 * on both success and failure so that the refcount is 1 when the
	 * thread exits.
	 */
err_net:
	if (!cb_info->users)
		cb_info->serv = NULL;
	svc_destroy(serv);
err_create:
	mutex_unlock(&nfs_callback_mutex);
	return ret;

err_start:
	nfs_callback_down_net(minorversion, serv, net);
	dprintk("NFS: Couldn't create server thread; err = %d\n", ret);
	goto err_net;
}

/*
 * Kill the callback thread if it's no longer being used.
 */
void nfs_callback_down(int minorversion, struct net *net)
{
	struct nfs_callback_data *cb_info = &nfs_callback_info[minorversion];
	struct svc_serv *serv;

	mutex_lock(&nfs_callback_mutex);
	serv = cb_info->serv;
	nfs_callback_down_net(minorversion, serv, net);
	cb_info->users--;
	if (cb_info->users == 0) {
		svc_get(serv);
		serv->sv_ops->svo_setup(serv, NULL, 0);
		svc_destroy(serv);
		dprintk("nfs_callback_down: service destroyed\n");
		cb_info->serv = NULL;
	}
	mutex_unlock(&nfs_callback_mutex);
}

/* Boolean check of RPC_AUTH_GSS principal */
int
check_gss_callback_principal(struct nfs_client *clp, struct svc_rqst *rqstp)
{
	char *p = rqstp->rq_cred.cr_principal;

	if (rqstp->rq_authop->flavour != RPC_AUTH_GSS)
		return 1;

	/* No RPC_AUTH_GSS on NFSv4.1 back channel yet */
	if (clp->cl_minorversion != 0)
		return 0;
	/*
	 * It might just be a normal user principal, in which case
	 * userspace won't bother to tell us the name at all.
	 */
	if (p == NULL)
		return 0;

	/*
	 * Did we get the acceptor from userland during the SETCLIENID
	 * negotiation?
	 */
	if (clp->cl_acceptor)
		return !strcmp(p, clp->cl_acceptor);

	/*
	 * Otherwise try to verify it using the cl_hostname. Note that this
	 * doesn't work if a non-canonical hostname was used in the devname.
	 */

	/* Expect a GSS_C_NT_HOSTBASED_NAME like "nfs@serverhostname" */

	if (memcmp(p, "nfs@", 4) != 0)
		return 0;
	p += 4;
	if (strcmp(p, clp->cl_hostname) != 0)
		return 0;
	return 1;
}

/*
 * pg_authenticate method for nfsv4 callback threads.
 *
 * The authflavor has been negotiated, so an incorrect flavor is a server
 * bug. Deny packets with incorrect authflavor.
 *
 * All other checking done after NFS decoding where the nfs_client can be
 * found in nfs4_callback_compound
 */
static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	switch (rqstp->rq_authop->flavour) {
	case RPC_AUTH_NULL:
		if (rqstp->rq_proc != CB_NULL)
			return SVC_DENIED;
		break;
	case RPC_AUTH_GSS:
		/* No RPC_AUTH_GSS support yet in NFSv4.1 */
		 if (svc_is_backchannel(rqstp))
			return SVC_DENIED;
	}
	return SVC_OK;
}

/*
 * Define NFS4 callback program
 */
static const struct svc_version *nfs4_callback_version[] = {
	[1] = &nfs4_callback_version1,
	[4] = &nfs4_callback_version4,
};

static struct svc_stat nfs4_callback_stats;

static struct svc_program nfs4_callback_program = {
	.pg_prog = NFS4_CALLBACK,			/* RPC service number */
	.pg_nvers = ARRAY_SIZE(nfs4_callback_version),	/* Number of entries */
	.pg_vers = nfs4_callback_version,		/* version table */
	.pg_name = "NFSv4 callback",			/* service name */
	.pg_class = "nfs",				/* authentication class */
	.pg_stats = &nfs4_callback_stats,
	.pg_authenticate = nfs_callback_authenticate,
};
