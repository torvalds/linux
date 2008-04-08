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

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_serv *serv;
	struct task_struct *task;
};

static struct nfs_callback_data nfs_callback_info;
static DEFINE_MUTEX(nfs_callback_mutex);
static struct svc_program nfs4_callback_program;

unsigned int nfs_callback_set_tcpport;
unsigned short nfs_callback_tcpport;
static const int nfs_set_port_min = 0;
static const int nfs_set_port_max = 65535;

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
	nfs_callback_info.task = NULL;
	svc_exit_thread(rqstp);
	return 0;
}

/*
 * Bring up the server process if it is not already up.
 */
int nfs_callback_up(void)
{
	struct svc_serv *serv = NULL;
	struct svc_rqst *rqstp;
	int ret = 0;

	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	if (nfs_callback_info.users++ || nfs_callback_info.task != NULL)
		goto out;
	serv = svc_create(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE, NULL);
	ret = -ENOMEM;
	if (!serv)
		goto out_err;

	ret = svc_create_xprt(serv, "tcp", nfs_callback_set_tcpport,
			      SVC_SOCK_ANONYMOUS);
	if (ret <= 0)
		goto out_err;
	nfs_callback_tcpport = ret;
	dprintk("Callback port = 0x%x\n", nfs_callback_tcpport);

	rqstp = svc_prepare_thread(serv, &serv->sv_pools[0]);
	if (IS_ERR(rqstp)) {
		ret = PTR_ERR(rqstp);
		goto out_err;
	}

	svc_sock_update_bufs(serv);
	nfs_callback_info.serv = serv;

	nfs_callback_info.task = kthread_run(nfs_callback_svc, rqstp,
					     "nfsv4-svc");
	if (IS_ERR(nfs_callback_info.task)) {
		ret = PTR_ERR(nfs_callback_info.task);
		nfs_callback_info.serv = NULL;
		nfs_callback_info.task = NULL;
		svc_exit_thread(rqstp);
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
	unlock_kernel();
	return ret;
out_err:
	dprintk("Couldn't create callback socket or server thread; err = %d\n",
		ret);
	nfs_callback_info.users--;
	goto out;
}

/*
 * Kill the server process if it is not already down.
 */
void nfs_callback_down(void)
{
	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	nfs_callback_info.users--;
	if (nfs_callback_info.users == 0 && nfs_callback_info.task != NULL)
		kthread_stop(nfs_callback_info.task);
	mutex_unlock(&nfs_callback_mutex);
	unlock_kernel();
}

static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	struct nfs_client *clp;
	RPC_IFDEBUG(char buf[RPC_MAX_ADDRBUFLEN]);

	/* Don't talk to strangers */
	clp = nfs_find_client(svc_addr(rqstp), 4);
	if (clp == NULL)
		return SVC_DROP;

	dprintk("%s: %s NFSv4 callback!\n", __FUNCTION__,
			svc_print_addr(rqstp, buf, sizeof(buf)));
	nfs_put_client(clp);

	switch (rqstp->rq_authop->flavour) {
		case RPC_AUTH_NULL:
			if (rqstp->rq_proc != CB_NULL)
				return SVC_DENIED;
			break;
		case RPC_AUTH_UNIX:
			break;
		case RPC_AUTH_GSS:
			/* FIXME: RPCSEC_GSS handling? */
		default:
			return SVC_DENIED;
	}
	return SVC_OK;
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
