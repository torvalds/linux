// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/uio.h>
#include <linux/smp.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <linux/inetdevice.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svc_xprt.h>
#include <net/ip.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <linux/lockd/lockd.h>
#include <linux/nfs.h>

#include "netns.h"
#include "procfs.h"
#include "netlink.h"

#define NLMDBG_FACILITY		NLMDBG_SVC
#define LOCKD_BUFSIZE		(1024 + NLMSVC_XDRSIZE)

static struct svc_program	nlmsvc_program;

const struct nlmsvc_binding	*nlmsvc_ops;
EXPORT_SYMBOL_GPL(nlmsvc_ops);

static DEFINE_MUTEX(nlmsvc_mutex);
static unsigned int		nlmsvc_users;
static struct svc_serv		*nlmsvc_serv;

static void nlmsvc_request_retry(struct timer_list *tl)
{
	svc_wake_up(nlmsvc_serv);
}
DEFINE_TIMER(nlmsvc_retry, nlmsvc_request_retry);

unsigned int lockd_net_id;

/*
 * These can be set at insmod time (useful for NFS as root filesystem),
 * and also changed through the sysctl interface.  -- Jamie Lokier, Aug 2003
 */
static unsigned long		nlm_grace_period;
unsigned long			nlm_timeout = LOCKD_DFLT_TIMEO;
static int			nlm_udpport, nlm_tcpport;

/*
 * Constants needed for the sysctl interface.
 */
static const unsigned long	nlm_grace_period_min = 0;
static const unsigned long	nlm_grace_period_max = 240;
static const unsigned long	nlm_timeout_min = 3;
static const unsigned long	nlm_timeout_max = 20;

#ifdef CONFIG_SYSCTL
static const int		nlm_port_min = 0, nlm_port_max = 65535;
static struct ctl_table_header * nlm_sysctl_table;
#endif

static unsigned long get_lockd_grace_period(struct net *net)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	/* Return the net-ns specific grace period, if there is one */
	if (ln->gracetime)
		return ln->gracetime * HZ;

	/* Note: nlm_timeout should always be nonzero */
	if (nlm_grace_period)
		return roundup(nlm_grace_period, nlm_timeout) * HZ;
	else
		return nlm_timeout * 5 * HZ;
}

static void grace_ender(struct work_struct *grace)
{
	struct delayed_work *dwork = to_delayed_work(grace);
	struct lockd_net *ln = container_of(dwork, struct lockd_net,
					    grace_period_end);

	locks_end_grace(&ln->lockd_manager);
}

static void set_grace_period(struct net *net)
{
	unsigned long grace_period = get_lockd_grace_period(net);
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	locks_start_grace(net, &ln->lockd_manager);
	cancel_delayed_work_sync(&ln->grace_period_end);
	schedule_delayed_work(&ln->grace_period_end, grace_period);
}

/*
 * This is the lockd kernel thread
 */
static int
lockd(void *vrqstp)
{
	struct svc_rqst *rqstp = vrqstp;
	struct net *net = &init_net;
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	svc_thread_init_status(rqstp, 0);

	/* try_to_freeze() is called from svc_recv() */
	set_freezable();

	dprintk("NFS locking service started (ver " LOCKD_VERSION ").\n");

	/*
	 * The main request loop. We don't terminate until the last
	 * NFS mount or NFS daemon has gone away.
	 */
	while (!svc_thread_should_stop(rqstp)) {
		nlmsvc_retry_blocked(rqstp);
		svc_recv(rqstp);
	}
	if (nlmsvc_ops)
		nlmsvc_invalidate_all();
	nlm_shutdown_hosts();
	cancel_delayed_work_sync(&ln->grace_period_end);
	locks_end_grace(&ln->lockd_manager);

	dprintk("lockd_down: service stopped\n");

	svc_exit_thread(rqstp);
	return 0;
}

static int create_lockd_listener(struct svc_serv *serv, const char *name,
				 struct net *net, const int family,
				 const unsigned short port,
				 const struct cred *cred)
{
	struct svc_xprt *xprt;

	xprt = svc_find_xprt(serv, name, net, family, 0);
	if (xprt == NULL)
		return svc_xprt_create(serv, name, net, family, port,
				       SVC_SOCK_DEFAULTS, cred);
	svc_xprt_put(xprt);
	return 0;
}

static int create_lockd_family(struct svc_serv *serv, struct net *net,
			       const int family, const struct cred *cred)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);
	int err;

	err = create_lockd_listener(serv, "udp", net, family,
				    ln->udp_port ? ln->udp_port : nlm_udpport, cred);
	if (err < 0)
		return err;

	return create_lockd_listener(serv, "tcp", net, family,
				     ln->tcp_port ? ln->tcp_port : nlm_tcpport, cred);
}

/*
 * Ensure there are active UDP and TCP listeners for lockd.
 *
 * Even if we have only TCP NFS mounts and/or TCP NFSDs, some
 * local services (such as rpc.statd) still require UDP, and
 * some NFS servers do not yet support NLM over TCP.
 *
 * Returns zero if all listeners are available; otherwise a
 * negative errno value is returned.
 */
static int make_socks(struct svc_serv *serv, struct net *net,
		const struct cred *cred)
{
	static int warned;
	int err;

	err = create_lockd_family(serv, net, PF_INET, cred);
	if (err < 0)
		goto out_err;

	err = create_lockd_family(serv, net, PF_INET6, cred);
	if (err < 0 && err != -EAFNOSUPPORT)
		goto out_err;

	warned = 0;
	return 0;

out_err:
	if (warned++ == 0)
		printk(KERN_WARNING
			"lockd_up: makesock failed, error=%d\n", err);
	svc_xprt_destroy_all(serv, net, true);
	return err;
}

static int lockd_up_net(struct svc_serv *serv, struct net *net,
		const struct cred *cred)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);
	int error;

	if (ln->nlmsvc_users++)
		return 0;

	error = svc_bind(serv, net);
	if (error)
		goto err_bind;

	error = make_socks(serv, net, cred);
	if (error < 0)
		goto err_bind;
	set_grace_period(net);
	dprintk("%s: per-net data created; net=%x\n", __func__, net->ns.inum);
	return 0;

err_bind:
	ln->nlmsvc_users--;
	return error;
}

static void lockd_down_net(struct svc_serv *serv, struct net *net)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	if (ln->nlmsvc_users) {
		if (--ln->nlmsvc_users == 0) {
			nlm_shutdown_hosts_net(net);
			cancel_delayed_work_sync(&ln->grace_period_end);
			locks_end_grace(&ln->lockd_manager);
			svc_xprt_destroy_all(serv, net, true);
		}
	} else {
		pr_err("%s: no users! net=%x\n",
			__func__, net->ns.inum);
		BUG();
	}
}

static int lockd_inetaddr_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct sockaddr_in sin;

	if (event != NETDEV_DOWN)
		goto out;

	if (nlmsvc_serv) {
		dprintk("lockd_inetaddr_event: removed %pI4\n",
			&ifa->ifa_local);
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = ifa->ifa_local;
		svc_age_temp_xprts_now(nlmsvc_serv, (struct sockaddr *)&sin);
	}

out:
	return NOTIFY_DONE;
}

static struct notifier_block lockd_inetaddr_notifier = {
	.notifier_call = lockd_inetaddr_event,
};

#if IS_ENABLED(CONFIG_IPV6)
static int lockd_inet6addr_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct sockaddr_in6 sin6;

	if (event != NETDEV_DOWN)
		goto out;

	if (nlmsvc_serv) {
		dprintk("lockd_inet6addr_event: removed %pI6\n", &ifa->addr);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = ifa->addr;
		if (ipv6_addr_type(&sin6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
			sin6.sin6_scope_id = ifa->idev->dev->ifindex;
		svc_age_temp_xprts_now(nlmsvc_serv, (struct sockaddr *)&sin6);
	}

out:
	return NOTIFY_DONE;
}

static struct notifier_block lockd_inet6addr_notifier = {
	.notifier_call = lockd_inet6addr_event,
};
#endif

static int lockd_get(void)
{
	struct svc_serv *serv;
	int error;

	if (nlmsvc_serv) {
		nlmsvc_users++;
		return 0;
	}

	/*
	 * Sanity check: if there's no pid,
	 * we should be the first user ...
	 */
	if (nlmsvc_users)
		printk(KERN_WARNING
			"lockd_up: no pid, %d users??\n", nlmsvc_users);

	serv = svc_create(&nlmsvc_program, LOCKD_BUFSIZE, lockd);
	if (!serv) {
		printk(KERN_WARNING "lockd_up: create service failed\n");
		return -ENOMEM;
	}

	error = svc_set_num_threads(serv, NULL, 1);
	if (error < 0) {
		svc_destroy(&serv);
		return error;
	}

	nlmsvc_serv = serv;
	register_inetaddr_notifier(&lockd_inetaddr_notifier);
#if IS_ENABLED(CONFIG_IPV6)
	register_inet6addr_notifier(&lockd_inet6addr_notifier);
#endif
	dprintk("lockd_up: service created\n");
	nlmsvc_users++;
	return 0;
}

static void lockd_put(void)
{
	if (WARN(nlmsvc_users <= 0, "lockd_down: no users!\n"))
		return;
	if (--nlmsvc_users)
		return;

	unregister_inetaddr_notifier(&lockd_inetaddr_notifier);
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&lockd_inet6addr_notifier);
#endif

	svc_set_num_threads(nlmsvc_serv, NULL, 0);
	timer_delete_sync(&nlmsvc_retry);
	svc_destroy(&nlmsvc_serv);
	dprintk("lockd_down: service destroyed\n");
}

/*
 * Bring up the lockd process if it's not already up.
 */
int lockd_up(struct net *net, const struct cred *cred)
{
	int error;

	mutex_lock(&nlmsvc_mutex);

	error = lockd_get();
	if (error)
		goto err;

	error = lockd_up_net(nlmsvc_serv, net, cred);
	if (error < 0) {
		lockd_put();
		goto err;
	}

err:
	mutex_unlock(&nlmsvc_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(lockd_up);

/*
 * Decrement the user count and bring down lockd if we're the last.
 */
void
lockd_down(struct net *net)
{
	mutex_lock(&nlmsvc_mutex);
	lockd_down_net(nlmsvc_serv, net);
	lockd_put();
	mutex_unlock(&nlmsvc_mutex);
}
EXPORT_SYMBOL_GPL(lockd_down);

#ifdef CONFIG_SYSCTL

/*
 * Sysctl parameters (same as module parameters, different interface).
 */

static const struct ctl_table nlm_sysctls[] = {
	{
		.procname	= "nlm_grace_period",
		.data		= &nlm_grace_period,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= (unsigned long *) &nlm_grace_period_min,
		.extra2		= (unsigned long *) &nlm_grace_period_max,
	},
	{
		.procname	= "nlm_timeout",
		.data		= &nlm_timeout,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= (unsigned long *) &nlm_timeout_min,
		.extra2		= (unsigned long *) &nlm_timeout_max,
	},
	{
		.procname	= "nlm_udpport",
		.data		= &nlm_udpport,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (int *) &nlm_port_min,
		.extra2		= (int *) &nlm_port_max,
	},
	{
		.procname	= "nlm_tcpport",
		.data		= &nlm_tcpport,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (int *) &nlm_port_min,
		.extra2		= (int *) &nlm_port_max,
	},
	{
		.procname	= "nsm_use_hostnames",
		.data		= &nsm_use_hostnames,
		.maxlen		= sizeof(bool),
		.mode		= 0644,
		.proc_handler	= proc_dobool,
	},
	{
		.procname	= "nsm_local_state",
		.data		= &nsm_local_state,
		.maxlen		= sizeof(nsm_local_state),
		.mode		= 0644,
		.proc_handler	= proc_douintvec,
		.extra1		= SYSCTL_ZERO,
	},
};

#endif	/* CONFIG_SYSCTL */

/*
 * Module (and sysfs) parameters.
 */

#define param_set_min_max(name, type, which_strtol, min, max)		\
static int param_set_##name(const char *val, const struct kernel_param *kp) \
{									\
	char *endp;							\
	__typeof__(type) num = which_strtol(val, &endp, 0);		\
	if (endp == val || *endp || num < (min) || num > (max))		\
		return -EINVAL;						\
	*((type *) kp->arg) = num;					\
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


static enum svc_auth_status lockd_authenticate(struct svc_rqst *rqstp)
{
	rqstp->rq_client = NULL;
	switch (rqstp->rq_authop->flavour) {
		case RPC_AUTH_NULL:
		case RPC_AUTH_UNIX:
			rqstp->rq_auth_stat = rpc_auth_ok;
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
	rqstp->rq_auth_stat = rpc_autherr_badcred;
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

static int lockd_init_net(struct net *net)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	INIT_DELAYED_WORK(&ln->grace_period_end, grace_ender);
	INIT_LIST_HEAD(&ln->lockd_manager.list);
	ln->lockd_manager.block_opens = false;
	INIT_LIST_HEAD(&ln->nsm_handles);
	return 0;
}

static void lockd_exit_net(struct net *net)
{
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	WARN_ONCE(!list_empty(&ln->lockd_manager.list),
		  "net %x %s: lockd_manager.list is not empty\n",
		  net->ns.inum, __func__);
	WARN_ONCE(!list_empty(&ln->nsm_handles),
		  "net %x %s: nsm_handles list is not empty\n",
		  net->ns.inum, __func__);
	WARN_ONCE(delayed_work_pending(&ln->grace_period_end),
		  "net %x %s: grace_period_end was not cancelled\n",
		  net->ns.inum, __func__);
}

static struct pernet_operations lockd_net_ops = {
	.init = lockd_init_net,
	.exit = lockd_exit_net,
	.id = &lockd_net_id,
	.size = sizeof(struct lockd_net),
};


/*
 * Initialising and terminating the module.
 */

static int __init init_nlm(void)
{
	int err;

#ifdef CONFIG_SYSCTL
	err = -ENOMEM;
	nlm_sysctl_table = register_sysctl("fs/nfs", nlm_sysctls);
	if (nlm_sysctl_table == NULL)
		goto err_sysctl;
#endif
	err = register_pernet_subsys(&lockd_net_ops);
	if (err)
		goto err_pernet;

	err = genl_register_family(&lockd_nl_family);
	if (err)
		goto err_netlink;

	err = lockd_create_procfs();
	if (err)
		goto err_procfs;

	return 0;

err_procfs:
	genl_unregister_family(&lockd_nl_family);
err_netlink:
	unregister_pernet_subsys(&lockd_net_ops);
err_pernet:
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(nlm_sysctl_table);
err_sysctl:
#endif
	return err;
}

static void __exit exit_nlm(void)
{
	/* FIXME: delete all NLM clients */
	nlm_shutdown_hosts();
	genl_unregister_family(&lockd_nl_family);
	lockd_remove_procfs();
	unregister_pernet_subsys(&lockd_net_ops);
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(nlm_sysctl_table);
#endif
}

module_init(init_nlm);
module_exit(exit_nlm);

/**
 * nlmsvc_dispatch - Process an NLM Request
 * @rqstp: incoming request
 *
 * Return values:
 *  %0: Processing complete; do not send a Reply
 *  %1: Processing complete; send Reply in rqstp->rq_res
 */
static int nlmsvc_dispatch(struct svc_rqst *rqstp)
{
	const struct svc_procedure *procp = rqstp->rq_procinfo;
	__be32 *statp = rqstp->rq_accept_statp;

	if (!procp->pc_decode(rqstp, &rqstp->rq_arg_stream))
		goto out_decode_err;

	*statp = procp->pc_func(rqstp);
	if (*statp == rpc_drop_reply)
		return 0;
	if (*statp != rpc_success)
		return 1;

	if (!procp->pc_encode(rqstp, &rqstp->rq_res_stream))
		goto out_encode_err;

	return 1;

out_decode_err:
	*statp = rpc_garbage_args;
	return 1;

out_encode_err:
	*statp = rpc_system_err;
	return 1;
}

/*
 * Define NLM program and procedures
 */
static DEFINE_PER_CPU_ALIGNED(unsigned long, nlmsvc_version1_count[17]);
static const struct svc_version	nlmsvc_version1 = {
	.vs_vers	= 1,
	.vs_nproc	= 17,
	.vs_proc	= nlmsvc_procedures,
	.vs_count	= nlmsvc_version1_count,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= NLMSVC_XDRSIZE,
};

static DEFINE_PER_CPU_ALIGNED(unsigned long,
			      nlmsvc_version3_count[ARRAY_SIZE(nlmsvc_procedures)]);
static const struct svc_version	nlmsvc_version3 = {
	.vs_vers	= 3,
	.vs_nproc	= ARRAY_SIZE(nlmsvc_procedures),
	.vs_proc	= nlmsvc_procedures,
	.vs_count	= nlmsvc_version3_count,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= NLMSVC_XDRSIZE,
};

#ifdef CONFIG_LOCKD_V4
static DEFINE_PER_CPU_ALIGNED(unsigned long,
			      nlmsvc_version4_count[ARRAY_SIZE(nlmsvc_procedures4)]);
static const struct svc_version	nlmsvc_version4 = {
	.vs_vers	= 4,
	.vs_nproc	= ARRAY_SIZE(nlmsvc_procedures4),
	.vs_proc	= nlmsvc_procedures4,
	.vs_count	= nlmsvc_version4_count,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= NLMSVC_XDRSIZE,
};
#endif

static const struct svc_version *nlmsvc_version[] = {
	[1] = &nlmsvc_version1,
	[3] = &nlmsvc_version3,
#ifdef CONFIG_LOCKD_V4
	[4] = &nlmsvc_version4,
#endif
};

#define NLM_NRVERS	ARRAY_SIZE(nlmsvc_version)
static struct svc_program	nlmsvc_program = {
	.pg_prog		= NLM_PROGRAM,		/* program number */
	.pg_nvers		= NLM_NRVERS,		/* number of entries in nlmsvc_version */
	.pg_vers		= nlmsvc_version,	/* version table */
	.pg_name		= "lockd",		/* service name */
	.pg_class		= "nfsd",		/* share authentication with nfsd */
	.pg_authenticate	= &lockd_authenticate,	/* export authentication */
	.pg_init_request	= svc_generic_init_request,
	.pg_rpcbind_set		= svc_generic_rpcbind_set,
};

/**
 * lockd_nl_server_set_doit - set the lockd server parameters via netlink
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * This updates the per-net values. When updating the values in the init_net
 * namespace, also update the "legacy" global values.
 *
 * Return 0 on success or a negative errno.
 */
int lockd_nl_server_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct lockd_net *ln = net_generic(net, lockd_net_id);
	const struct nlattr *attr;

	if (GENL_REQ_ATTR_CHECK(info, LOCKD_A_SERVER_GRACETIME))
		return -EINVAL;

	if (info->attrs[LOCKD_A_SERVER_GRACETIME] ||
	    info->attrs[LOCKD_A_SERVER_TCP_PORT] ||
	    info->attrs[LOCKD_A_SERVER_UDP_PORT]) {
		attr = info->attrs[LOCKD_A_SERVER_GRACETIME];
		if (attr) {
			u32 gracetime = nla_get_u32(attr);

			if (gracetime > nlm_grace_period_max)
				return -EINVAL;

			ln->gracetime = gracetime;

			if (net == &init_net)
				nlm_grace_period = gracetime;
		}

		attr = info->attrs[LOCKD_A_SERVER_TCP_PORT];
		if (attr) {
			ln->tcp_port = nla_get_u16(attr);
			if (net == &init_net)
				nlm_tcpport = ln->tcp_port;
		}

		attr = info->attrs[LOCKD_A_SERVER_UDP_PORT];
		if (attr) {
			ln->udp_port = nla_get_u16(attr);
			if (net == &init_net)
				nlm_udpport = ln->udp_port;
		}
	}
	return 0;
}

/**
 * lockd_nl_server_get_doit - get lockd server parameters via netlink
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Return 0 on success or a negative errno.
 */
int lockd_nl_server_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct lockd_net *ln = net_generic(net, lockd_net_id);
	void *hdr;
	int err;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_iput(skb, info);
	if (!hdr) {
		err = -EMSGSIZE;
		goto err_free_msg;
	}

	err = nla_put_u32(skb, LOCKD_A_SERVER_GRACETIME, ln->gracetime) ||
	      nla_put_u16(skb, LOCKD_A_SERVER_TCP_PORT, ln->tcp_port) ||
	      nla_put_u16(skb, LOCKD_A_SERVER_UDP_PORT, ln->udp_port);
	if (err)
		goto err_free_msg;

	genlmsg_end(skb, hdr);

	return genlmsg_reply(skb, info);
err_free_msg:
	nlmsg_free(skb);

	return err;
}
