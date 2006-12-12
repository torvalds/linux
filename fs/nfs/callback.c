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

#include <net/inet_sock.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "internal.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK

struct nfs_callback_data {
	unsigned int users;
	struct svc_serv *serv;
	pid_t pid;
	struct completion started;
	struct completion stopped;
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
static void nfs_callback_svc(struct svc_rqst *rqstp)
{
	int err;

	__module_get(THIS_MODULE);
	lock_kernel();

	nfs_callback_info.pid = current->pid;
	daemonize("nfsv4-svc");
	/* Process request with signals blocked, but allow SIGKILL.  */
	allow_signal(SIGKILL);

	complete(&nfs_callback_info.started);

	for(;;) {
		if (signalled()) {
			if (nfs_callback_info.users == 0)
				break;
			flush_signals(current);
		}
		/*
		 * Listen for a request on the socket
		 */
		err = svc_recv(rqstp, MAX_SCHEDULE_TIMEOUT);
		if (err == -EAGAIN || err == -EINTR)
			continue;
		if (err < 0) {
			printk(KERN_WARNING
					"%s: terminating on error %d\n",
					__FUNCTION__, -err);
			break;
		}
		dprintk("%s: request from %u.%u.%u.%u\n", __FUNCTION__,
				NIPQUAD(rqstp->rq_addr.sin_addr.s_addr));
		svc_process(rqstp);
	}

	svc_exit_thread(rqstp);
	nfs_callback_info.pid = 0;
	complete(&nfs_callback_info.stopped);
	unlock_kernel();
	module_put_and_exit(0);
}

/*
 * Bring up the server process if it is not already up.
 */
int nfs_callback_up(void)
{
	struct svc_serv *serv;
	struct svc_sock *svsk;
	int ret = 0;

	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	if (nfs_callback_info.users++ || nfs_callback_info.pid != 0)
		goto out;
	init_completion(&nfs_callback_info.started);
	init_completion(&nfs_callback_info.stopped);
	serv = svc_create(&nfs4_callback_program, NFS4_CALLBACK_BUFSIZE, NULL);
	ret = -ENOMEM;
	if (!serv)
		goto out_err;
	/* FIXME: We don't want to register this socket with the portmapper */
	ret = svc_makesock(serv, IPPROTO_TCP, nfs_callback_set_tcpport);
	if (ret < 0)
		goto out_destroy;
	if (!list_empty(&serv->sv_permsocks)) {
		svsk = list_entry(serv->sv_permsocks.next,
				struct svc_sock, sk_list);
		nfs_callback_tcpport = ntohs(inet_sk(svsk->sk_sk)->sport);
		dprintk ("Callback port = 0x%x\n", nfs_callback_tcpport);
	} else
		BUG();
	ret = svc_create_thread(nfs_callback_svc, serv);
	if (ret < 0)
		goto out_destroy;
	nfs_callback_info.serv = serv;
	wait_for_completion(&nfs_callback_info.started);
out:
	mutex_unlock(&nfs_callback_mutex);
	unlock_kernel();
	return ret;
out_destroy:
	svc_destroy(serv);
out_err:
	nfs_callback_info.users--;
	goto out;
}

/*
 * Kill the server process if it is not already up.
 */
void nfs_callback_down(void)
{
	lock_kernel();
	mutex_lock(&nfs_callback_mutex);
	nfs_callback_info.users--;
	do {
		if (nfs_callback_info.users != 0 || nfs_callback_info.pid == 0)
			break;
		if (kill_proc(nfs_callback_info.pid, SIGKILL, 1) < 0)
			break;
	} while (wait_for_completion_timeout(&nfs_callback_info.stopped, 5*HZ) == 0);
	mutex_unlock(&nfs_callback_mutex);
	unlock_kernel();
}

static int nfs_callback_authenticate(struct svc_rqst *rqstp)
{
	struct sockaddr_in *addr = &rqstp->rq_addr;
	struct nfs_client *clp;

	/* Don't talk to strangers */
	clp = nfs_find_client(addr, 4);
	if (clp == NULL)
		return SVC_DROP;
	dprintk("%s: %u.%u.%u.%u NFSv4 callback!\n", __FUNCTION__, NIPQUAD(addr->sin_addr));
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
