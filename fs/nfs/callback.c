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
#include <linux/smp_lock.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/nfs_fs.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sunrpc/svcauth_gss.h>

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_rqst *rqst;
	struct task_struct *task;
};

static struct nfs_callback_data nfs_callback_info;
static DEFINE_MUTEX(nfs_callback_mutex);
static struct svc_program nfs4_callback_program;

unsigned int nfs_callback_set_tcpport;
unsigned short nfs_callback_tcpport;
static const int nfs_set_port_min = 0;
static const int nfs_set_port_max = 65535;

/*
 * If the kernel has IPv6 support available, always listen for
 * both AF_INET and AF_INET6 requests.
 */
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static const sa_family_t	nfs_callback_family = AF_INET6;
#else
static const sa_family_t	nfs_callback_family = AF_INET;
#endif

static int param_set_port(const char *val, struct kernel_param *kp)
{
	char *endp;
	int num = simple_strtol(val, &endp, 0);
	if (endp == val || *endp || num < nfs_set_port_min || num > nfs_set_port_max)
		return -EINVAL;
	*((int *)kp->arg) = num;
	return 0;
}

module_param_call(callback_tcpport, param_set_port, param_get_int,
		 &nfs_callback_set_tcpport, 0644);

/*
 * This is the callback kernel thread.
 */
static int
nfs_callback_svc(void *vrqstp)
{
	int err, preverr = 0;
	struct svc_rqst *rqstp = vrqstp;

	set_freezable();

	/*
	 * FIXME: do we really need to run this under the BKL? If so, please
	 * add a comment about what it's intended to protect.
	 */
	lock_kernel();
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
	unlock_kernel();
	return 0;
}

/*
 * Bring up the callback thread if it is not already up.
 */
int nfs_callback_up(void)
{
	struct svc_serv *serv = NULL;
	int ret = 0;

	mutex_lock(&nfs_callback_mutex);
	if (nfs_callback_info.users++ || nfs_callback_info.task != NULL)
		goto out;
	serv = svc_create(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE,
				nfs_callback_family, NULL);
	ret = -ENOMEM;
	if (!serv)
		goto out_err;

	ret = svc_create_xprt(serv, "tcp", nfs_callback_set_tcpport,
			      SVC_SOCK_ANONYMOUS);
	if (ret <= 0)
		goto out_err;
	nfs_callback_tcpport = ret;
	dprintk("NFS: Callback listener port = %u (af %u)\n",
			nfs_callback_tcpport, nfs_callback_family);

	nfs_callback_info.rqst = svc_prepare_thread(serv, &serv->sv_pools[0]);
	if (IS_ERR(nfs_callback_info.rqst)) {
		ret = PTR_ERR(nfs_callback_info.rqst);
		nfs_callback_info.rqst = NULL;
		goto out_err;
	}

	svc_sock_update_bufs(serv);

	nfs_callback_info.task = kthread_run(nfs_callback_svc,
					     nfs_callback_info.rqst,
					     "nfsv4-svc");
	if (IS_ERR(nfs_callback_info.task)) {
		ret = PTR_ERR(nfs_callback_info.task);
		svc_exit_thread(nfs_callback_info.rqst);
		nfs_callback_info.rqst = NULL;
		nfs_callback_info.task = NULL;
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
	nfs_callback_info.users--;
	goto out;
}

/*
 * Kill the callback thread if it's no longer being used.
 */
void nfs_callback_down(void)
{
	mutex_lock(&nfs_callback_mutex);
	nfs_callback_info.users--;
	if (nfs_callback_info.users == 0 && nfs_callback_info.task != NULL) {
		kthread_stop(nfs_callback_info.task);
		svc_exit_thread(nfs_callback_info.rqst);
		nfs_callback_info.rqst = NULL;
		nfs_callback_info.task = NULL;
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

static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	struct nfs_client *clp;
	RPC_IFDEBUG(char buf[RPC_MAX_ADDRBUFLEN]);
	int ret = SVC_OK;

	/* Don't talk to strangers */
	clp = nfs_find_client(svc_addr(rqstp), 4);
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
