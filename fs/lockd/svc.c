/*
 * linux/fs/lockd/svc.c
 *
 * This is the central lockd service.
 *
 * FIXME: Separate the lockd NFS server functionality from the lockd NFS
 * 	  client functionality. Oh why didn't Sun create two separate
 *	  services in the first place?
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/moduleparam.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <net/ip.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>
#include <linux/nfs.h>

#define NLMDBG_FACILITY		NLMDBG_SVC
#define LOCKD_BUFSIZE		(1024 + NLMSVC_XDRSIZE)
#define ALLOWED_SIGS		(sigmask(SIGKILL))

static struct svc_program	nlmsvc_program;

struct nlmsvc_binding *		nlmsvc_ops;
EXPORT_SYMBOL(nlmsvc_ops);

static DEFINE_MUTEX(nlmsvc_mutex);
static unsigned int		nlmsvc_users;
static pid_t			nlmsvc_pid;
static struct svc_serv		*nlmsvc_serv;
int				nlmsvc_grace_period;
unsigned long			nlmsvc_timeout;

static DECLARE_COMPLETION(lockd_start_done);
static DECLARE_WAIT_QUEUE_HEAD(lockd_exit);

/*
 * These can be set at insmod time (useful for NFS as root filesystem),
 * and also changed through the sysctl interface.  -- Jamie Lokier, Aug 2003
 */
static unsigned long		nlm_grace_period;
static unsigned long		nlm_timeout = LOCKD_DFLT_TIMEO;
static int			nlm_udpport, nlm_tcpport;
int				nsm_use_hostnames = 0;

/*
 * Constants needed for the sysctl interface.
 */
static const unsigned long	nlm_grace_period_min = 0;
static const unsigned long	nlm_grace_period_max = 240;
static const unsigned long	nlm_timeout_min = 3;
static const unsigned long	nlm_timeout_max = 20;
static const int		nlm_port_min = 0, nlm_port_max = 65535;

static struct ctl_table_header * nlm_sysctl_table;

static unsigned long set_grace_period(void)
{
	unsigned long grace_period;

	/* Note: nlm_timeout should always be nonzero */
	if (nlm_grace_period)
		grace_period = ((nlm_grace_period + nlm_timeout - 1)
				/ nlm_timeout) * nlm_timeout * HZ;
	else
		grace_period = nlm_timeout * 5 * HZ;
	nlmsvc_grace_period = 1;
	return grace_period + jiffies;
}

static inline void clear_grace_period(void)
{
	nlmsvc_grace_period = 0;
}

/*
 * This is the lockd kernel thread
 */
static void
lockd(struct svc_rqst *rqstp)
{
	int		err = 0;
	unsigned long grace_period_expire;

	/* Lock module and set up kernel thread */
	/* lockd_up is waiting for us to startup, so will
	 * be holding a reference to this module, so it
	 * is safe to just claim another reference
	 */
	__module_get(THIS_MODULE);
	lock_kernel();

	/*
	 * Let our maker know we're running.
	 */
	nlmsvc_pid = current->pid;
	nlmsvc_serv = rqstp->rq_server;
	complete(&lockd_start_done);

	daemonize("lockd");

	/* Process request with signals blocked, but allow SIGKILL.  */
	allow_signal(SIGKILL);

	/* kick rpciod */
	rpciod_up();

	dprintk("NFS locking service started (ver " LOCKD_VERSION ").\n");

	if (!nlm_timeout)
		nlm_timeout = LOCKD_DFLT_TIMEO;
	nlmsvc_timeout = nlm_timeout * HZ;

	grace_period_expire = set_grace_period();

	/*
	 * The main request loop. We don't terminate until the last
	 * NFS mount or NFS daemon has gone away, and we've been sent a
	 * signal, or else another process has taken over our job.
	 */
	while ((nlmsvc_users || !signalled()) && nlmsvc_pid == current->pid) {
		long timeout = MAX_SCHEDULE_TIMEOUT;
		char buf[RPC_MAX_ADDRBUFLEN];

		if (signalled()) {
			flush_signals(current);
			if (nlmsvc_ops) {
				nlmsvc_invalidate_all();
				grace_period_expire = set_grace_period();
			}
		}

		/*
		 * Retry any blocked locks that have been notified by
		 * the VFS. Don't do this during grace period.
		 * (Theoretically, there shouldn't even be blocked locks
		 * during grace period).
		 */
		if (!nlmsvc_grace_period) {
			timeout = nlmsvc_retry_blocked();
		} else if (time_before(grace_period_expire, jiffies))
			clear_grace_period();

		/*
		 * Find a socket with data available and call its
		 * recvfrom routine.
		 */
		err = svc_recv(rqstp, timeout);
		if (err == -EAGAIN || err == -EINTR)
			continue;
		if (err < 0) {
			printk(KERN_WARNING
			       "lockd: terminating on error %d\n",
			       -err);
			break;
		}

		dprintk("lockd: request from %s\n",
				svc_print_addr(rqstp, buf, sizeof(buf)));

		svc_process(rqstp);
	}

	flush_signals(current);

	/*
	 * Check whether there's a new lockd process before
	 * shutting down the hosts and clearing the slot.
	 */
	if (!nlmsvc_pid || current->pid == nlmsvc_pid) {
		if (nlmsvc_ops)
			nlmsvc_invalidate_all();
		nlm_shutdown_hosts();
		nlmsvc_pid = 0;
		nlmsvc_serv = NULL;
	} else
		printk(KERN_DEBUG
			"lockd: new process, skipping host shutdown\n");
	wake_up(&lockd_exit);

	/* Exit the RPC thread */
	svc_exit_thread(rqstp);

	/* release rpciod */
	rpciod_down();

	/* Release module */
	unlock_kernel();
	module_put_and_exit(0);
}


static int find_socket(struct svc_serv *serv, int proto)
{
	struct svc_sock *svsk;
	int found = 0;
	list_for_each_entry(svsk, &serv->sv_permsocks, sk_list)
		if (svsk->sk_sk->sk_protocol == proto) {
			found = 1;
			break;
		}
	return found;
}

/*
 * Make any sockets that are needed but not present.
 * If nlm_udpport or nlm_tcpport were set as module
 * options, make those sockets unconditionally
 */
static int make_socks(struct svc_serv *serv, int proto)
{
	static int warned;
	int err = 0;

	if (proto == IPPROTO_UDP || nlm_udpport)
		if (!find_socket(serv, IPPROTO_UDP))
			err = svc_makesock(serv, IPPROTO_UDP, nlm_udpport,
						SVC_SOCK_DEFAULTS);
	if (err >= 0 && (proto == IPPROTO_TCP || nlm_tcpport))
		if (!find_socket(serv, IPPROTO_TCP))
			err = svc_makesock(serv, IPPROTO_TCP, nlm_tcpport,
						SVC_SOCK_DEFAULTS);

	if (err >= 0) {
		warned = 0;
		err = 0;
	} else if (warned++ == 0)
		printk(KERN_WARNING
		       "lockd_up: makesock failed, error=%d\n", err);
	return err;
}

/*
 * Bring up the lockd process if it's not already up.
 */
int
lockd_up(int proto) /* Maybe add a 'family' option when IPv6 is supported ?? */
{
	struct svc_serv *	serv;
	int			error = 0;

	mutex_lock(&nlmsvc_mutex);
	/*
	 * Check whether we're already up and running.
	 */
	if (nlmsvc_pid) {
		if (proto)
			error = make_socks(nlmsvc_serv, proto);
		goto out;
	}

	/*
	 * Sanity check: if there's no pid,
	 * we should be the first user ...
	 */
	if (nlmsvc_users)
		printk(KERN_WARNING
			"lockd_up: no pid, %d users??\n", nlmsvc_users);

	error = -ENOMEM;
	serv = svc_create(&nlmsvc_program, LOCKD_BUFSIZE, NULL);
	if (!serv) {
		printk(KERN_WARNING "lockd_up: create service failed\n");
		goto out;
	}

	if ((error = make_socks(serv, proto)) < 0)
		goto destroy_and_out;

	/*
	 * Create the kernel thread and wait for it to start.
	 */
	error = svc_create_thread(lockd, serv);
	if (error) {
		printk(KERN_WARNING
			"lockd_up: create thread failed, error=%d\n", error);
		goto destroy_and_out;
	}
	wait_for_completion(&lockd_start_done);

	/*
	 * Note: svc_serv structures have an initial use count of 1,
	 * so we exit through here on both success and failure.
	 */
destroy_and_out:
	svc_destroy(serv);
out:
	if (!error)
		nlmsvc_users++;
	mutex_unlock(&nlmsvc_mutex);
	return error;
}
EXPORT_SYMBOL(lockd_up);

/*
 * Decrement the user count and bring down lockd if we're the last.
 */
void
lockd_down(void)
{
	static int warned;

	mutex_lock(&nlmsvc_mutex);
	if (nlmsvc_users) {
		if (--nlmsvc_users)
			goto out;
	} else
		printk(KERN_WARNING "lockd_down: no users! pid=%d\n", nlmsvc_pid);

	if (!nlmsvc_pid) {
		if (warned++ == 0)
			printk(KERN_WARNING "lockd_down: no lockd running.\n"); 
		goto out;
	}
	warned = 0;

	kill_proc(nlmsvc_pid, SIGKILL, 1);
	/*
	 * Wait for the lockd process to exit, but since we're holding
	 * the lockd semaphore, we can't wait around forever ...
	 */
	clear_thread_flag(TIF_SIGPENDING);
	interruptible_sleep_on_timeout(&lockd_exit, HZ);
	if (nlmsvc_pid) {
		printk(KERN_WARNING 
			"lockd_down: lockd failed to exit, clearing pid\n");
		nlmsvc_pid = 0;
	}
	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
out:
	mutex_unlock(&nlmsvc_mutex);
}
EXPORT_SYMBOL(lockd_down);

/*
 * Sysctl parameters (same as module parameters, different interface).
 */

static ctl_table nlm_sysctls[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nlm_grace_period",
		.data		= &nlm_grace_period,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.extra1		= (unsigned long *) &nlm_grace_period_min,
		.extra2		= (unsigned long *) &nlm_grace_period_max,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nlm_timeout",
		.data		= &nlm_timeout,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.extra1		= (unsigned long *) &nlm_timeout_min,
		.extra2		= (unsigned long *) &nlm_timeout_max,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nlm_udpport",
		.data		= &nlm_udpport,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= (int *) &nlm_port_min,
		.extra2		= (int *) &nlm_port_max,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nlm_tcpport",
		.data		= &nlm_tcpport,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= (int *) &nlm_port_min,
		.extra2		= (int *) &nlm_port_max,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nsm_use_hostnames",
		.data		= &nsm_use_hostnames,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nsm_local_state",
		.data		= &nsm_local_state,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static ctl_table nlm_sysctl_dir[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nfs",
		.mode		= 0555,
		.child		= nlm_sysctls,
	},
	{ .ctl_name = 0 }
};

static ctl_table nlm_sysctl_root[] = {
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= nlm_sysctl_dir,
	},
	{ .ctl_name = 0 }
};

/*
 * Module (and sysfs) parameters.
 */

#define param_set_min_max(name, type, which_strtol, min, max)		\
static int param_set_##name(const char *val, struct kernel_param *kp)	\
{									\
	char *endp;							\
	__typeof__(type) num = which_strtol(val, &endp, 0);		\
	if (endp == val || *endp || num < (min) || num > (max))		\
		return -EINVAL;						\
	*((int *) kp->arg) = num;					\
	return 0;							\
}

static inline int is_callback(u32 proc)
{
	return proc == NLMPROC_GRANTED
		|| proc == NLMPROC_GRANTED_MSG
		|| proc == NLMPROC_TEST_RES
		|| proc == NLMPROC_LOCK_RES
		|| proc == NLMPROC_CANCEL_RES
		|| proc == NLMPROC_UNLOCK_RES
		|| proc == NLMPROC_NSM_NOTIFY;
}


static int lockd_authenticate(struct svc_rqst *rqstp)
{
	rqstp->rq_client = NULL;
	switch (rqstp->rq_authop->flavour) {
		case RPC_AUTH_NULL:
		case RPC_AUTH_UNIX:
			if (rqstp->rq_proc == 0)
				return SVC_OK;
			if (is_callback(rqstp->rq_proc)) {
				/* Leave it to individual procedures to
				 * call nlmsvc_lookup_host(rqstp)
				 */
				return SVC_OK;
			}
			return svc_set_client(rqstp);
	}
	return SVC_DENIED;
}


param_set_min_max(port, int, simple_strtol, 0, 65535)
param_set_min_max(grace_period, unsigned long, simple_strtoul,
		  nlm_grace_period_min, nlm_grace_period_max)
param_set_min_max(timeout, unsigned long, simple_strtoul,
		  nlm_timeout_min, nlm_timeout_max)

MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_DESCRIPTION("NFS file locking service version " LOCKD_VERSION ".");
MODULE_LICENSE("GPL");

module_param_call(nlm_grace_period, param_set_grace_period, param_get_ulong,
		  &nlm_grace_period, 0644);
module_param_call(nlm_timeout, param_set_timeout, param_get_ulong,
		  &nlm_timeout, 0644);
module_param_call(nlm_udpport, param_set_port, param_get_int,
		  &nlm_udpport, 0644);
module_param_call(nlm_tcpport, param_set_port, param_get_int,
		  &nlm_tcpport, 0644);
module_param(nsm_use_hostnames, bool, 0644);

/*
 * Initialising and terminating the module.
 */

static int __init init_nlm(void)
{
	nlm_sysctl_table = register_sysctl_table(nlm_sysctl_root);
	return nlm_sysctl_table ? 0 : -ENOMEM;
}

static void __exit exit_nlm(void)
{
	/* FIXME: delete all NLM clients */
	nlm_shutdown_hosts();
	unregister_sysctl_table(nlm_sysctl_table);
}

module_init(init_nlm);
module_exit(exit_nlm);

/*
 * Define NLM program and procedures
 */
static struct svc_version	nlmsvc_version1 = {
		.vs_vers	= 1,
		.vs_nproc	= 17,
		.vs_proc	= nlmsvc_procedures,
		.vs_xdrsize	= NLMSVC_XDRSIZE,
};
static struct svc_version	nlmsvc_version3 = {
		.vs_vers	= 3,
		.vs_nproc	= 24,
		.vs_proc	= nlmsvc_procedures,
		.vs_xdrsize	= NLMSVC_XDRSIZE,
};
#ifdef CONFIG_LOCKD_V4
static struct svc_version	nlmsvc_version4 = {
		.vs_vers	= 4,
		.vs_nproc	= 24,
		.vs_proc	= nlmsvc_procedures4,
		.vs_xdrsize	= NLMSVC_XDRSIZE,
};
#endif
static struct svc_version *	nlmsvc_version[] = {
	[1] = &nlmsvc_version1,
	[3] = &nlmsvc_version3,
#ifdef CONFIG_LOCKD_V4
	[4] = &nlmsvc_version4,
#endif
};

static struct svc_stat		nlmsvc_stats;

#define NLM_NRVERS	ARRAY_SIZE(nlmsvc_version)
static struct svc_program	nlmsvc_program = {
	.pg_prog		= NLM_PROGRAM,		/* program number */
	.pg_nvers		= NLM_NRVERS,		/* number of entries in nlmsvc_version */
	.pg_vers		= nlmsvc_version,	/* version table */
	.pg_name		= "lockd",		/* service name */
	.pg_class		= "nfsd",		/* share authentication with nfsd */
	.pg_stats		= &nlmsvc_stats,	/* stats table */
	.pg_authenticate = &lockd_authenticate	/* export authentication */
};
