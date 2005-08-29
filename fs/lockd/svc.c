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

#include <linux/config.h>
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

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/lockd/lockd.h>
#include <linux/nfs.h>

#define NLMDBG_FACILITY		NLMDBG_SVC
#define LOCKD_BUFSIZE		(1024 + NLMSVC_XDRSIZE)
#define ALLOWED_SIGS		(sigmask(SIGKILL))

static struct svc_program	nlmsvc_program;

struct nlmsvc_binding *		nlmsvc_ops;
EXPORT_SYMBOL(nlmsvc_ops);

static DECLARE_MUTEX(nlmsvc_sema);
static unsigned int		nlmsvc_users;
static pid_t			nlmsvc_pid;
int				nlmsvc_grace_period;
unsigned long			nlmsvc_timeout;

static DECLARE_MUTEX_LOCKED(lockd_start);
static DECLARE_WAIT_QUEUE_HEAD(lockd_exit);

/*
 * These can be set at insmod time (useful for NFS as root filesystem),
 * and also changed through the sysctl interface.  -- Jamie Lokier, Aug 2003
 */
static unsigned long		nlm_grace_period;
static unsigned long		nlm_timeout = LOCKD_DFLT_TIMEO;
static int			nlm_udpport, nlm_tcpport;

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
	struct svc_serv	*serv = rqstp->rq_server;
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
	up(&lockd_start);

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
		err = svc_recv(serv, rqstp, timeout);
		if (err == -EAGAIN || err == -EINTR)
			continue;
		if (err < 0) {
			printk(KERN_WARNING
			       "lockd: terminating on error %d\n",
			       -err);
			break;
		}

		dprintk("lockd: request from %08x\n",
			(unsigned)ntohl(rqstp->rq_addr.sin_addr.s_addr));

		svc_process(serv, rqstp);

	}

	/*
	 * Check whether there's a new lockd process before
	 * shutting down the hosts and clearing the slot.
	 */
	if (!nlmsvc_pid || current->pid == nlmsvc_pid) {
		if (nlmsvc_ops)
			nlmsvc_invalidate_all();
		nlm_shutdown_hosts();
		nlmsvc_pid = 0;
	} else
		printk(KERN_DEBUG
			"lockd: new process, skipping host shutdown\n");
	wake_up(&lockd_exit);

	flush_signals(current);

	/* Exit the RPC thread */
	svc_exit_thread(rqstp);

	/* release rpciod */
	rpciod_down();

	/* Release module */
	unlock_kernel();
	module_put_and_exit(0);
}

/*
 * Bring up the lockd process if it's not already up.
 */
int
lockd_up(void)
{
	static int		warned;
	struct svc_serv *	serv;
	int			error = 0;

	down(&nlmsvc_sema);
	/*
	 * Unconditionally increment the user count ... this is
	 * the number of clients who _want_ a lockd process.
	 */
	nlmsvc_users++; 
	/*
	 * Check whether we're already up and running.
	 */
	if (nlmsvc_pid)
		goto out;

	/*
	 * Sanity check: if there's no pid,
	 * we should be the first user ...
	 */
	if (nlmsvc_users > 1)
		printk(KERN_WARNING
			"lockd_up: no pid, %d users??\n", nlmsvc_users);

	error = -ENOMEM;
	serv = svc_create(&nlmsvc_program, LOCKD_BUFSIZE);
	if (!serv) {
		printk(KERN_WARNING "lockd_up: create service failed\n");
		goto out;
	}

	if ((error = svc_makesock(serv, IPPROTO_UDP, nlm_udpport)) < 0 
#ifdef CONFIG_NFSD_TCP
	 || (error = svc_makesock(serv, IPPROTO_TCP, nlm_tcpport)) < 0
#endif
		) {
		if (warned++ == 0) 
			printk(KERN_WARNING
				"lockd_up: makesock failed, error=%d\n", error);
		goto destroy_and_out;
	} 
	warned = 0;

	/*
	 * Create the kernel thread and wait for it to start.
	 */
	error = svc_create_thread(lockd, serv);
	if (error) {
		printk(KERN_WARNING
			"lockd_up: create thread failed, error=%d\n", error);
		goto destroy_and_out;
	}
	down(&lockd_start);

	/*
	 * Note: svc_serv structures have an initial use count of 1,
	 * so we exit through here on both success and failure.
	 */
destroy_and_out:
	svc_destroy(serv);
out:
	up(&nlmsvc_sema);
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

	down(&nlmsvc_sema);
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
	up(&nlmsvc_sema);
}
EXPORT_SYMBOL(lockd_down);

/*
 * Sysctl parameters (same as module parameters, different interface).
 */

/* Something that isn't CTL_ANY, CTL_NONE or a value that may clash. */
#define CTL_UNNUMBERED		-2

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
 * Module (and driverfs) parameters.
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

/*
 * Initialising and terminating the module.
 */

static int __init init_nlm(void)
{
	nlm_sysctl_table = register_sysctl_table(nlm_sysctl_root, 0);
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

#define NLM_NRVERS	(sizeof(nlmsvc_version)/sizeof(nlmsvc_version[0]))
static struct svc_program	nlmsvc_program = {
	.pg_prog		= NLM_PROGRAM,		/* program number */
	.pg_nvers		= NLM_NRVERS,		/* number of entries in nlmsvc_version */
	.pg_vers		= nlmsvc_version,	/* version table */
	.pg_name		= "lockd",		/* service name */
	.pg_class		= "nfsd",		/* share authentication with nfsd */
	.pg_stats		= &nlmsvc_stats,	/* stats table */
	.pg_authenticate = &lockd_authenticate	/* export authentication */
};
