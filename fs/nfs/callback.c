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
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfs_fs.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/bc_xprt.h>

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_serv *serv;
	struct svc_rqst *rqst;
	struct task_struct *task;
};

static struct nfs_callback_data nfs_callback_info[NFS4_MAX_MINOR_VERSION + 1];
static DEFINE_MUTEX(nfs_callback_mutex);
static struct svc_program nfs4_callback_program;

unsigned int nfs_callback_set_tcpport;
unsigned short nfs_callback_tcpport;
unsigned short nfs_callback_tcpport6;
#define NFS_CALLBACK_MAXPORTNR (65535U)

static int param_set_portnr(const char *val, const struct kernel_param *kp)
{
	unsigned long num;
	int ret;

	if (!val)
		return -EINVAL;
	ret = strict_strtoul(val, 0, &num);
	if (ret == -EINVAL || num > NFS_CALLBACK_MAXPORTNR)
		return -EINVAL;
	*((unsigned int *)kp->arg) = num;
	return 0;
}
static struct kernel_param_ops param_ops_portnr = {
	.set = param_set_portnr,
	.get = param_get_uint,
};
#define param_check_portnr(name, p) __param_check(name, p, unsigned int);

module_param_named(callback_tcpport, nfs_callback_set_tcpport, portnr, 0644);

/*
 * This is the NFSv4 callback kernel thread.
 */
static int
nfs4_callback_svc(void *vrqstp)
{
	int err, preverr = 0;
	struct svc_rqst *rqstp = vrqstp;

	set_freezable();

	while (!kthread_should_stop()) {
		/*
		 * Listen for a request on the socket
		 */
		err = svc_recv(rqstp, MAX_SCHEDULE_TIMEOUT);
		if (err == -EAGAIN || err == -EINTR) {
			preverr = err;
			continue;
		}
		if (err < 0) {
			if (err != preverr) {
				printk(KERN_WARNING "%s: unexpected error "
					"from svc_recv (%d)\n", __func__, err);
				preverr = err;
			}
			schedule_timeout_uninterruptible(HZ);
			continue;
		}
		preverr = err;
		svc_process(rqstp);
	}
	return 0;
}

/*
 * Prepare to bring up the NFSv4 callback service
 */
struct svc_rqst *
nfs4_callback_up(struct svc_serv *serv)
{
	int ret;

	ret = svc_create_xprt(serv, "tcp", &init_net, PF_INET,
				nfs_callback_set_tcpport, SVC_SOCK_ANONYMOUS);
	if (ret <= 0)
		goto out_err;
	nfs_callback_tcpport = ret;
	dprintk("NFS: Callback listener port = %u (af %u)\n",
			nfs_callback_tcpport, PF_INET);

	ret = svc_create_xprt(serv, "tcp", &init_net, PF_INET6,
				nfs_callback_set_tcpport, SVC_SOCK_ANONYMOUS);
	if (ret > 0) {
		nfs_callback_tcpport6 = ret;
		dprintk("NFS: Callback listener port = %u (af %u)\n",
				nfs_callback_tcpport6, PF_INET6);
	} else if (ret == -EAFNOSUPPORT)
		ret = 0;
	else
		goto out_err;

	return svc_prepare_thread(serv, &serv->sv_pools[0]);

out_err:
	if (ret == 0)
		ret = -ENOMEM;
	return ERR_PTR(ret);
}

#if defined(CONFIG_NFS_V4_1)
/*
 *  * CB_SEQUENCE operations will fail until the callback sessionid is set.
 *   */
int nfs4_set_callback_sessionid(struct nfs_client *clp)
{
	struct svc_serv *serv = clp->cl_rpcclient->cl_xprt->bc_serv;
	struct nfs4_sessionid *bc_sid;

	if (!serv->bc_xprt)
		return -EINVAL;

	/* on success freed in xprt_free */
	bc_sid = kmalloc(sizeof(struct nfs4_sessionid), GFP_KERNEL);
	if (!bc_sid)
		return -ENOMEM;
	memcpy(bc_sid->data, &clp->cl_session->sess_id.data,
		NFS4_MAX_SESSIONID_LEN);
	spin_lock_bh(&serv->sv_cb_lock);
	serv->bc_xprt->xpt_bc_sid = bc_sid;
	spin_unlock_bh(&serv->sv_cb_lock);
	dprintk("%s set xpt_bc_sid=%u:%u:%u:%u for bc_xprt %p\n", __func__,
		((u32 *)bc_sid->data)[0], ((u32 *)bc_sid->data)[1],
		((u32 *)bc_sid->data)[2], ((u32 *)bc_sid->data)[3],
		serv->bc_xprt);
	return 0;
}

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

	while (!kthread_should_stop()) {
		prepare_to_wait(&serv->sv_cb_waitq, &wq, TASK_INTERRUPTIBLE);
		spin_lock_bh(&serv->sv_cb_lock);
		if (!list_empty(&serv->sv_cb_list)) {
			req = list_first_entry(&serv->sv_cb_list,
					struct rpc_rqst, rq_bc_list);
			list_del(&req->rq_bc_list);
			spin_unlock_bh(&serv->sv_cb_lock);
			dprintk("Invoking bc_svc_process()\n");
			error = bc_svc_process(serv, req, rqstp);
			dprintk("bc_svc_process() returned w/ error code= %d\n",
				error);
		} else {
			spin_unlock_bh(&serv->sv_cb_lock);
			schedule();
		}
		finish_wait(&serv->sv_cb_waitq, &wq);
	}
	return 0;
}

/*
 * Bring up the NFSv4.1 callback service
 */
struct svc_rqst *
nfs41_callback_up(struct svc_serv *serv, struct rpc_xprt *xprt)
{
	struct svc_rqst *rqstp;
	int ret;

	/*
	 * Create an svc_sock for the back channel service that shares the
	 * fore channel connection.
	 * Returns the input port (0) and sets the svc_serv bc_xprt on success
	 */
	ret = svc_create_xprt(serv, "tcp-bc", &init_net, PF_INET, 0,
			      SVC_SOCK_ANONYMOUS);
	if (ret < 0) {
		rqstp = ERR_PTR(ret);
		goto out;
	}

	/*
	 * Save the svc_serv in the transport so that it can
	 * be referenced when the session backchannel is initialized
	 */
	xprt->bc_serv = serv;

	INIT_LIST_HEAD(&serv->sv_cb_list);
	spin_lock_init(&serv->sv_cb_lock);
	init_waitqueue_head(&serv->sv_cb_waitq);
	rqstp = svc_prepare_thread(serv, &serv->sv_pools[0]);
	if (IS_ERR(rqstp)) {
		svc_xprt_put(serv->bc_xprt);
		serv->bc_xprt = NULL;
	}
out:
	dprintk("--> %s return %ld\n", __func__,
		IS_ERR(rqstp) ? PTR_ERR(rqstp) : 0);
	return rqstp;
}

static inline int nfs_minorversion_callback_svc_setup(u32 minorversion,
		struct svc_serv *serv, struct rpc_xprt *xprt,
		struct svc_rqst **rqstpp, int (**callback_svc)(void *vrqstp))
{
	if (minorversion) {
		*rqstpp = nfs41_callback_up(serv, xprt);
		*callback_svc = nfs41_callback_svc;
	}
	return minorversion;
}

static inline void nfs_callback_bc_serv(u32 minorversion, struct rpc_xprt *xprt,
		struct nfs_callback_data *cb_info)
{
	if (minorversion)
		xprt->bc_serv = cb_info->serv;
}
#else
static inline int nfs_minorversion_callback_svc_setup(u32 minorversion,
		struct svc_serv *serv, struct rpc_xprt *xprt,
		struct svc_rqst **rqstpp, int (**callback_svc)(void *vrqstp))
{
	return 0;
}

static inline void nfs_callback_bc_serv(u32 minorversion, struct rpc_xprt *xprt,
		struct nfs_callback_data *cb_info)
{
}
int nfs4_set_callback_sessionid(struct nfs_client *clp)
{
	return 0;
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Bring up the callback thread if it is not already up.
 */
int nfs_callback_up(u32 minorversion, struct rpc_xprt *xprt)
{
	struct svc_serv *serv = NULL;
	struct svc_rqst *rqstp;
	int (*callback_svc)(void *vrqstp);
	struct nfs_callback_data *cb_info = &nfs_callback_info[minorversion];
	char svc_name[12];
	int ret = 0;
	int minorversion_setup;

	mutex_lock(&nfs_callback_mutex);
	if (cb_info->users++ || cb_info->task != NULL) {
		nfs_callback_bc_serv(minorversion, xprt, cb_info);
		goto out;
	}
	serv = svc_create(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE, NULL);
	if (!serv) {
		ret = -ENOMEM;
		goto out_err;
	}

	minorversion_setup =  nfs_minorversion_callback_svc_setup(minorversion,
					serv, xprt, &rqstp, &callback_svc);
	if (!minorversion_setup) {
		/* v4.0 callback setup */
		rqstp = nfs4_callback_up(serv);
		callback_svc = nfs4_callback_svc;
	}

	if (IS_ERR(rqstp)) {
		ret = PTR_ERR(rqstp);
		goto out_err;
	}

	svc_sock_update_bufs(serv);

	sprintf(svc_name, "nfsv4.%u-svc", minorversion);
	cb_info->serv = serv;
	cb_info->rqst = rqstp;
	cb_info->task = kthread_run(callback_svc, cb_info->rqst, svc_name);
	if (IS_ERR(cb_info->task)) {
		ret = PTR_ERR(cb_info->task);
		svc_exit_thread(cb_info->rqst);
		cb_info->rqst = NULL;
		cb_info->task = NULL;
		goto out_err;
	}
out:
	/*
	 * svc_create creates the svc_serv with sv_nrthreads == 1, and then
	 * svc_prepare_thread increments that. So we need to call svc_destroy
	 * on both success and failure so that the refcount is 1 when the
	 * thread exits.
	 */
	if (serv)
		svc_destroy(serv);
	mutex_unlock(&nfs_callback_mutex);
	return ret;
out_err:
	dprintk("NFS: Couldn't create callback socket or server thread; "
		"err = %d\n", ret);
	cb_info->users--;
	goto out;
}

/*
 * Kill the callback thread if it's no longer being used.
 */
void nfs_callback_down(int minorversion)
{
	struct nfs_callback_data *cb_info = &nfs_callback_info[minorversion];

	mutex_lock(&nfs_callback_mutex);
	cb_info->users--;
	if (cb_info->users == 0 && cb_info->task != NULL) {
		kthread_stop(cb_info->task);
		svc_exit_thread(cb_info->rqst);
		cb_info->serv = NULL;
		cb_info->rqst = NULL;
		cb_info->task = NULL;
	}
	mutex_unlock(&nfs_callback_mutex);
}

static int check_gss_callback_principal(struct nfs_client *clp,
					struct svc_rqst *rqstp)
{
	struct rpc_clnt *r = clp->cl_rpcclient;
	char *p = svc_gss_principal(rqstp);

	/*
	 * It might just be a normal user principal, in which case
	 * userspace won't bother to tell us the name at all.
	 */
	if (p == NULL)
		return SVC_DENIED;

	/* Expect a GSS_C_NT_HOSTBASED_NAME like "nfs@serverhostname" */

	if (memcmp(p, "nfs@", 4) != 0)
		return SVC_DENIED;
	p += 4;
	if (strcmp(p, r->cl_server) != 0)
		return SVC_DENIED;
	return SVC_OK;
}

/* pg_authenticate method helper */
static struct nfs_client *nfs_cb_find_client(struct svc_rqst *rqstp)
{
	struct nfs4_sessionid *sessionid = bc_xprt_sid(rqstp);
	int is_cb_compound = rqstp->rq_proc == CB_COMPOUND ? 1 : 0;

	dprintk("--> %s rq_proc %d\n", __func__, rqstp->rq_proc);
	if (svc_is_backchannel(rqstp))
		/* Sessionid (usually) set after CB_NULL ping */
		return nfs4_find_client_sessionid(svc_addr(rqstp), sessionid,
						  is_cb_compound);
	else
		/* No callback identifier in pg_authenticate */
		return nfs4_find_client_no_ident(svc_addr(rqstp));
}

/* pg_authenticate method for nfsv4 callback threads. */
static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	struct nfs_client *clp;
	RPC_IFDEBUG(char buf[RPC_MAX_ADDRBUFLEN]);
	int ret = SVC_OK;

	/* Don't talk to strangers */
	clp = nfs_cb_find_client(rqstp);
	if (clp == NULL)
		return SVC_DROP;

	dprintk("%s: %s NFSv4 callback!\n", __func__,
			svc_print_addr(rqstp, buf, sizeof(buf)));

	switch (rqstp->rq_authop->flavour) {
		case RPC_AUTH_NULL:
			if (rqstp->rq_proc != CB_NULL)
				ret = SVC_DENIED;
			break;
		case RPC_AUTH_UNIX:
			break;
		case RPC_AUTH_GSS:
			ret = check_gss_callback_principal(clp, rqstp);
			break;
		default:
			ret = SVC_DENIED;
	}
	nfs_put_client(clp);
	return ret;
}

/*
 * Define NFS4 callback program
 */
static struct svc_version *nfs4_callback_version[] = {
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
