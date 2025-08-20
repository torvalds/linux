// SPDX-License-Identifier: GPL-2.0
/*
 * Central processing for nfsd.
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched/signal.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/fs_struct.h>
#include <linux/swap.h>
#include <linux/siphash.h>

#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/lockd/bind.h>
#include <linux/nfsacl.h>
#include <linux/nfslocalio.h>
#include <linux/seq_file.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/net_namespace.h>
#include "nfsd.h"
#include "cache.h"
#include "vfs.h"
#include "netns.h"
#include "filecache.h"

#include "trace.h"

#define NFSDDBG_FACILITY	NFSDDBG_SVC

atomic_t			nfsd_th_cnt = ATOMIC_INIT(0);
static int			nfsd(void *vrqstp);
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
static int			nfsd_acl_rpcbind_set(struct net *,
						     const struct svc_program *,
						     u32, int,
						     unsigned short,
						     unsigned short);
static __be32			nfsd_acl_init_request(struct svc_rqst *,
						const struct svc_program *,
						struct svc_process_info *);
#endif
static int			nfsd_rpcbind_set(struct net *,
						 const struct svc_program *,
						 u32, int,
						 unsigned short,
						 unsigned short);
static __be32			nfsd_init_request(struct svc_rqst *,
						const struct svc_program *,
						struct svc_process_info *);

/*
 * nfsd_mutex protects nn->nfsd_serv -- both the pointer itself and some members
 * of the svc_serv struct such as ->sv_temp_socks and ->sv_permsocks.
 *
 * Finally, the nfsd_mutex also protects some of the global variables that are
 * accessed when nfsd starts and that are settable via the write_* routines in
 * nfsctl.c. In particular:
 *
 *	user_recovery_dirname
 *	user_lease_time
 *	nfsd_versions
 */
DEFINE_MUTEX(nfsd_mutex);

#if IS_ENABLED(CONFIG_NFS_LOCALIO)
static const struct svc_version *localio_versions[] = {
	[1] = &localio_version1,
};

#define NFSD_LOCALIO_NRVERS		ARRAY_SIZE(localio_versions)

#endif /* CONFIG_NFS_LOCALIO */

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
static const struct svc_version *nfsd_acl_version[] = {
# if defined(CONFIG_NFSD_V2_ACL)
	[2] = &nfsd_acl_version2,
# endif
# if defined(CONFIG_NFSD_V3_ACL)
	[3] = &nfsd_acl_version3,
# endif
};

#define NFSD_ACL_MINVERS	2
#define NFSD_ACL_NRVERS		ARRAY_SIZE(nfsd_acl_version)

#endif /* defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL) */

static const struct svc_version *nfsd_version[NFSD_MAXVERS+1] = {
#if defined(CONFIG_NFSD_V2)
	[2] = &nfsd_version2,
#endif
	[3] = &nfsd_version3,
#if defined(CONFIG_NFSD_V4)
	[4] = &nfsd_version4,
#endif
};

struct svc_program		nfsd_programs[] = {
	{
	.pg_prog		= NFS_PROGRAM,		/* program number */
	.pg_nvers		= NFSD_MAXVERS+1,	/* nr of entries in nfsd_version */
	.pg_vers		= nfsd_version,		/* version table */
	.pg_name		= "nfsd",		/* program name */
	.pg_class		= "nfsd",		/* authentication class */
	.pg_authenticate	= svc_set_client,	/* export authentication */
	.pg_init_request	= nfsd_init_request,
	.pg_rpcbind_set		= nfsd_rpcbind_set,
	},
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
	{
	.pg_prog		= NFS_ACL_PROGRAM,
	.pg_nvers		= NFSD_ACL_NRVERS,
	.pg_vers		= nfsd_acl_version,
	.pg_name		= "nfsacl",
	.pg_class		= "nfsd",
	.pg_authenticate	= svc_set_client,
	.pg_init_request	= nfsd_acl_init_request,
	.pg_rpcbind_set		= nfsd_acl_rpcbind_set,
	},
#endif /* defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL) */
#if IS_ENABLED(CONFIG_NFS_LOCALIO)
	{
	.pg_prog		= NFS_LOCALIO_PROGRAM,
	.pg_nvers		= NFSD_LOCALIO_NRVERS,
	.pg_vers		= localio_versions,
	.pg_name		= "nfslocalio",
	.pg_class		= "nfsd",
	.pg_authenticate	= svc_set_client,
	.pg_init_request	= svc_generic_init_request,
	.pg_rpcbind_set		= svc_generic_rpcbind_set,
	}
#endif /* CONFIG_NFS_LOCALIO */
};

bool nfsd_support_version(int vers)
{
	if (vers >= NFSD_MINVERS && vers <= NFSD_MAXVERS)
		return nfsd_version[vers] != NULL;
	return false;
}

int nfsd_vers(struct nfsd_net *nn, int vers, enum vers_op change)
{
	if (vers < NFSD_MINVERS || vers > NFSD_MAXVERS)
		return 0;
	switch(change) {
	case NFSD_SET:
		nn->nfsd_versions[vers] = nfsd_support_version(vers);
		break;
	case NFSD_CLEAR:
		nn->nfsd_versions[vers] = false;
		break;
	case NFSD_TEST:
		return nn->nfsd_versions[vers];
	case NFSD_AVAIL:
		return nfsd_support_version(vers);
	}
	return 0;
}

static void
nfsd_adjust_nfsd_versions4(struct nfsd_net *nn)
{
	unsigned i;

	for (i = 0; i <= NFSD_SUPPORTED_MINOR_VERSION; i++) {
		if (nn->nfsd4_minorversions[i])
			return;
	}
	nfsd_vers(nn, 4, NFSD_CLEAR);
}

int nfsd_minorversion(struct nfsd_net *nn, u32 minorversion, enum vers_op change)
{
	if (minorversion > NFSD_SUPPORTED_MINOR_VERSION &&
	    change != NFSD_AVAIL)
		return -1;

	switch(change) {
	case NFSD_SET:
		nfsd_vers(nn, 4, NFSD_SET);
		nn->nfsd4_minorversions[minorversion] =
			nfsd_vers(nn, 4, NFSD_TEST);
		break;
	case NFSD_CLEAR:
		nn->nfsd4_minorversions[minorversion] = false;
		nfsd_adjust_nfsd_versions4(nn);
		break;
	case NFSD_TEST:
		return nn->nfsd4_minorversions[minorversion];
	case NFSD_AVAIL:
		return minorversion <= NFSD_SUPPORTED_MINOR_VERSION &&
			nfsd_vers(nn, 4, NFSD_AVAIL);
	}
	return 0;
}

bool nfsd_net_try_get(struct net *net) __must_hold(rcu)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	return (nn && percpu_ref_tryget_live(&nn->nfsd_net_ref));
}

void nfsd_net_put(struct net *net) __must_hold(rcu)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	percpu_ref_put(&nn->nfsd_net_ref);
}

static void nfsd_net_done(struct percpu_ref *ref)
{
	struct nfsd_net *nn = container_of(ref, struct nfsd_net, nfsd_net_ref);

	complete(&nn->nfsd_net_confirm_done);
}

static void nfsd_net_free(struct percpu_ref *ref)
{
	struct nfsd_net *nn = container_of(ref, struct nfsd_net, nfsd_net_ref);

	complete(&nn->nfsd_net_free_done);
}

/*
 * Maximum number of nfsd processes
 */
#define	NFSD_MAXSERVS		8192

int nfsd_nrthreads(struct net *net)
{
	int rv = 0;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	mutex_lock(&nfsd_mutex);
	if (nn->nfsd_serv)
		rv = nn->nfsd_serv->sv_nrthreads;
	mutex_unlock(&nfsd_mutex);
	return rv;
}

static int nfsd_init_socks(struct net *net, const struct cred *cred)
{
	int error;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (!list_empty(&nn->nfsd_serv->sv_permsocks))
		return 0;

	error = svc_xprt_create(nn->nfsd_serv, "udp", net, PF_INET, NFS_PORT,
				SVC_SOCK_DEFAULTS, cred);
	if (error < 0)
		return error;

	error = svc_xprt_create(nn->nfsd_serv, "tcp", net, PF_INET, NFS_PORT,
				SVC_SOCK_DEFAULTS, cred);
	if (error < 0)
		return error;

	return 0;
}

static int nfsd_users = 0;

static int nfsd_startup_generic(void)
{
	int ret;

	if (nfsd_users++)
		return 0;

	ret = nfsd_file_cache_init();
	if (ret)
		goto dec_users;

	ret = nfs4_state_start();
	if (ret)
		goto out_file_cache;
	return 0;

out_file_cache:
	nfsd_file_cache_shutdown();
dec_users:
	nfsd_users--;
	return ret;
}

static void nfsd_shutdown_generic(void)
{
	if (--nfsd_users)
		return;

	nfs4_state_shutdown();
	nfsd_file_cache_shutdown();
}

static bool nfsd_needs_lockd(struct nfsd_net *nn)
{
	return nfsd_vers(nn, 2, NFSD_TEST) || nfsd_vers(nn, 3, NFSD_TEST);
}

/**
 * nfsd_copy_write_verifier - Atomically copy a write verifier
 * @verf: buffer in which to receive the verifier cookie
 * @nn: NFS net namespace
 *
 * This function provides a wait-free mechanism for copying the
 * namespace's write verifier without tearing it.
 */
void nfsd_copy_write_verifier(__be32 verf[2], struct nfsd_net *nn)
{
	unsigned int seq;

	do {
		seq = read_seqbegin(&nn->writeverf_lock);
		memcpy(verf, nn->writeverf, sizeof(nn->writeverf));
	} while (read_seqretry(&nn->writeverf_lock, seq));
}

static void nfsd_reset_write_verifier_locked(struct nfsd_net *nn)
{
	struct timespec64 now;
	u64 verf;

	/*
	 * Because the time value is hashed, y2038 time_t overflow
	 * is irrelevant in this usage.
	 */
	ktime_get_raw_ts64(&now);
	verf = siphash_2u64(now.tv_sec, now.tv_nsec, &nn->siphash_key);
	memcpy(nn->writeverf, &verf, sizeof(nn->writeverf));
}

/**
 * nfsd_reset_write_verifier - Generate a new write verifier
 * @nn: NFS net namespace
 *
 * This function updates the ->writeverf field of @nn. This field
 * contains an opaque cookie that, according to Section 18.32.3 of
 * RFC 8881, "the client can use to determine whether a server has
 * changed instance state (e.g., server restart) between a call to
 * WRITE and a subsequent call to either WRITE or COMMIT.  This
 * cookie MUST be unchanged during a single instance of the NFSv4.1
 * server and MUST be unique between instances of the NFSv4.1
 * server."
 */
void nfsd_reset_write_verifier(struct nfsd_net *nn)
{
	write_seqlock(&nn->writeverf_lock);
	nfsd_reset_write_verifier_locked(nn);
	write_sequnlock(&nn->writeverf_lock);
}

/*
 * Crank up a set of per-namespace resources for a new NFSD instance,
 * including lockd, a duplicate reply cache, an open file cache
 * instance, and a cache of NFSv4 state objects.
 */
static int nfsd_startup_net(struct net *net, const struct cred *cred)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int ret;

	if (nn->nfsd_net_up)
		return 0;

	ret = nfsd_startup_generic();
	if (ret)
		return ret;
	ret = nfsd_init_socks(net, cred);
	if (ret)
		goto out_socks;

	if (nfsd_needs_lockd(nn) && !nn->lockd_up) {
		ret = lockd_up(net, cred);
		if (ret)
			goto out_socks;
		nn->lockd_up = true;
	}

	ret = nfsd_file_cache_start_net(net);
	if (ret)
		goto out_lockd;

	ret = nfsd_reply_cache_init(nn);
	if (ret)
		goto out_filecache;

#ifdef CONFIG_NFSD_V4_2_INTER_SSC
	nfsd4_ssc_init_umount_work(nn);
#endif
	ret = nfs4_state_start_net(net);
	if (ret)
		goto out_reply_cache;

	nn->nfsd_net_up = true;
	return 0;

out_reply_cache:
	nfsd_reply_cache_shutdown(nn);
out_filecache:
	nfsd_file_cache_shutdown_net(net);
out_lockd:
	if (nn->lockd_up) {
		lockd_down(net);
		nn->lockd_up = false;
	}
out_socks:
	nfsd_shutdown_generic();
	return ret;
}

static void nfsd_shutdown_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (!nn->nfsd_net_up)
		return;

	percpu_ref_kill_and_confirm(&nn->nfsd_net_ref, nfsd_net_done);
	wait_for_completion(&nn->nfsd_net_confirm_done);

	nfsd_export_flush(net);
	nfs4_state_shutdown_net(net);
	nfsd_reply_cache_shutdown(nn);
	nfsd_file_cache_shutdown_net(net);
	if (nn->lockd_up) {
		lockd_down(net);
		nn->lockd_up = false;
	}

	wait_for_completion(&nn->nfsd_net_free_done);
	percpu_ref_exit(&nn->nfsd_net_ref);

	nn->nfsd_net_up = false;
	nfsd_shutdown_generic();
}

static DEFINE_SPINLOCK(nfsd_notifier_lock);
static int nfsd_inetaddr_event(struct notifier_block *this, unsigned long event,
	void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct net *net = dev_net(dev);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct sockaddr_in sin;

	if (event != NETDEV_DOWN || !nn->nfsd_serv)
		goto out;

	spin_lock(&nfsd_notifier_lock);
	if (nn->nfsd_serv) {
		dprintk("nfsd_inetaddr_event: removed %pI4\n", &ifa->ifa_local);
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = ifa->ifa_local;
		svc_age_temp_xprts_now(nn->nfsd_serv, (struct sockaddr *)&sin);
	}
	spin_unlock(&nfsd_notifier_lock);

out:
	return NOTIFY_DONE;
}

static struct notifier_block nfsd_inetaddr_notifier = {
	.notifier_call = nfsd_inetaddr_event,
};

#if IS_ENABLED(CONFIG_IPV6)
static int nfsd_inet6addr_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct net_device *dev = ifa->idev->dev;
	struct net *net = dev_net(dev);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct sockaddr_in6 sin6;

	if (event != NETDEV_DOWN || !nn->nfsd_serv)
		goto out;

	spin_lock(&nfsd_notifier_lock);
	if (nn->nfsd_serv) {
		dprintk("nfsd_inet6addr_event: removed %pI6\n", &ifa->addr);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = ifa->addr;
		if (ipv6_addr_type(&sin6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
			sin6.sin6_scope_id = ifa->idev->dev->ifindex;
		svc_age_temp_xprts_now(nn->nfsd_serv, (struct sockaddr *)&sin6);
	}
	spin_unlock(&nfsd_notifier_lock);

out:
	return NOTIFY_DONE;
}

static struct notifier_block nfsd_inet6addr_notifier = {
	.notifier_call = nfsd_inet6addr_event,
};
#endif

/* Only used under nfsd_mutex, so this atomic may be overkill: */
static atomic_t nfsd_notifier_refcount = ATOMIC_INIT(0);

/**
 * nfsd_destroy_serv - tear down NFSD's svc_serv for a namespace
 * @net: network namespace the NFS service is associated with
 */
void nfsd_destroy_serv(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv = nn->nfsd_serv;

	lockdep_assert_held(&nfsd_mutex);

	spin_lock(&nfsd_notifier_lock);
	nn->nfsd_serv = NULL;
	spin_unlock(&nfsd_notifier_lock);

	/* check if the notifier still has clients */
	if (atomic_dec_return(&nfsd_notifier_refcount) == 0) {
		unregister_inetaddr_notifier(&nfsd_inetaddr_notifier);
#if IS_ENABLED(CONFIG_IPV6)
		unregister_inet6addr_notifier(&nfsd_inet6addr_notifier);
#endif
	}

	/*
	 * write_ports can create the server without actually starting
	 * any threads.  If we get shut down before any threads are
	 * started, then nfsd_destroy_serv will be run before any of this
	 * other initialization has been done except the rpcb information.
	 */
	svc_xprt_destroy_all(serv, net, true);
	nfsd_shutdown_net(net);
	svc_destroy(&serv);
}

void nfsd_reset_versions(struct nfsd_net *nn)
{
	int i;

	for (i = 0; i <= NFSD_MAXVERS; i++)
		if (nfsd_vers(nn, i, NFSD_TEST))
			return;

	for (i = 0; i <= NFSD_MAXVERS; i++)
		if (i != 4)
			nfsd_vers(nn, i, NFSD_SET);
		else {
			int minor = 0;
			while (nfsd_minorversion(nn, minor, NFSD_SET) >= 0)
				minor++;
		}
}

static int nfsd_get_default_max_blksize(void)
{
	struct sysinfo i;
	unsigned long long target;
	unsigned long ret;

	si_meminfo(&i);
	target = (i.totalram - i.totalhigh) << PAGE_SHIFT;
	/*
	 * Aim for 1/4096 of memory per thread This gives 1MB on 4Gig
	 * machines, but only uses 32K on 128M machines.  Bottom out at
	 * 8K on 32M and smaller.  Of course, this is only a default.
	 */
	target >>= 12;

	ret = NFSSVC_DEFBLKSIZE;
	while (ret > target && ret >= 8*1024*2)
		ret /= 2;
	return ret;
}

void nfsd_shutdown_threads(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv;

	mutex_lock(&nfsd_mutex);
	serv = nn->nfsd_serv;
	if (serv == NULL) {
		mutex_unlock(&nfsd_mutex);
		return;
	}

	/* Kill outstanding nfsd threads */
	svc_set_num_threads(serv, NULL, 0);
	nfsd_destroy_serv(net);
	mutex_unlock(&nfsd_mutex);
}

struct svc_rqst *nfsd_current_rqst(void)
{
	if (kthread_func(current) == nfsd)
		return kthread_data(current);
	return NULL;
}

int nfsd_create_serv(struct net *net)
{
	int error;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv;

	WARN_ON(!mutex_is_locked(&nfsd_mutex));
	if (nn->nfsd_serv)
		return 0;

	error = percpu_ref_init(&nn->nfsd_net_ref, nfsd_net_free,
				0, GFP_KERNEL);
	if (error)
		return error;
	init_completion(&nn->nfsd_net_free_done);
	init_completion(&nn->nfsd_net_confirm_done);

	if (nfsd_max_blksize == 0)
		nfsd_max_blksize = nfsd_get_default_max_blksize();
	nfsd_reset_versions(nn);
	serv = svc_create_pooled(nfsd_programs, ARRAY_SIZE(nfsd_programs),
				 &nn->nfsd_svcstats,
				 nfsd_max_blksize, nfsd);
	if (serv == NULL)
		return -ENOMEM;

	error = svc_bind(serv, net);
	if (error < 0) {
		svc_destroy(&serv);
		return error;
	}
	spin_lock(&nfsd_notifier_lock);
	nn->nfsd_serv = serv;
	spin_unlock(&nfsd_notifier_lock);

	/* check if the notifier is already set */
	if (atomic_inc_return(&nfsd_notifier_refcount) == 1) {
		register_inetaddr_notifier(&nfsd_inetaddr_notifier);
#if IS_ENABLED(CONFIG_IPV6)
		register_inet6addr_notifier(&nfsd_inet6addr_notifier);
#endif
	}
	nfsd_reset_write_verifier(nn);
	return 0;
}

int nfsd_nrpools(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	if (nn->nfsd_serv == NULL)
		return 0;
	else
		return nn->nfsd_serv->sv_nrpools;
}

int nfsd_get_nrthreads(int n, int *nthreads, struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv = nn->nfsd_serv;
	int i;

	if (serv)
		for (i = 0; i < serv->sv_nrpools && i < n; i++)
			nthreads[i] = serv->sv_pools[i].sp_nrthreads;
	return 0;
}

/**
 * nfsd_set_nrthreads - set the number of running threads in the net's service
 * @n: number of array members in @nthreads
 * @nthreads: array of thread counts for each pool
 * @net: network namespace to operate within
 *
 * This function alters the number of running threads for the given network
 * namespace in each pool. If passed an array longer then the number of pools
 * the extra pool settings are ignored. If passed an array shorter than the
 * number of pools, the missing values are interpreted as 0's.
 *
 * Returns 0 on success or a negative errno on error.
 */
int nfsd_set_nrthreads(int n, int *nthreads, struct net *net)
{
	int i = 0;
	int tot = 0;
	int err = 0;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	lockdep_assert_held(&nfsd_mutex);

	if (nn->nfsd_serv == NULL || n <= 0)
		return 0;

	/*
	 * Special case: When n == 1, pass in NULL for the pool, so that the
	 * change is distributed equally among them.
	 */
	if (n == 1)
		return svc_set_num_threads(nn->nfsd_serv, NULL, nthreads[0]);

	if (n > nn->nfsd_serv->sv_nrpools)
		n = nn->nfsd_serv->sv_nrpools;

	/* enforce a global maximum number of threads */
	tot = 0;
	for (i = 0; i < n; i++) {
		nthreads[i] = min(nthreads[i], NFSD_MAXSERVS);
		tot += nthreads[i];
	}
	if (tot > NFSD_MAXSERVS) {
		/* total too large: scale down requested numbers */
		for (i = 0; i < n && tot > 0; i++) {
			int new = nthreads[i] * NFSD_MAXSERVS / tot;
			tot -= (nthreads[i] - new);
			nthreads[i] = new;
		}
		for (i = 0; i < n && tot > 0; i++) {
			nthreads[i]--;
			tot--;
		}
	}

	/* apply the new numbers */
	for (i = 0; i < n; i++) {
		err = svc_set_num_threads(nn->nfsd_serv,
					  &nn->nfsd_serv->sv_pools[i],
					  nthreads[i]);
		if (err)
			goto out;
	}

	/* Anything undefined in array is considered to be 0 */
	for (i = n; i < nn->nfsd_serv->sv_nrpools; ++i) {
		err = svc_set_num_threads(nn->nfsd_serv,
					  &nn->nfsd_serv->sv_pools[i],
					  0);
		if (err)
			goto out;
	}
out:
	return err;
}

/**
 * nfsd_svc: start up or shut down the nfsd server
 * @n: number of array members in @nthreads
 * @nthreads: array of thread counts for each pool
 * @net: network namespace to operate within
 * @cred: credentials to use for xprt creation
 * @scope: server scope value (defaults to nodename)
 *
 * Adjust the number of threads in each pool and return the new
 * total number of threads in the service.
 */
int
nfsd_svc(int n, int *nthreads, struct net *net, const struct cred *cred, const char *scope)
{
	int	error;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_serv *serv;

	lockdep_assert_held(&nfsd_mutex);

	dprintk("nfsd: creating service\n");

	strscpy(nn->nfsd_name, scope ? scope : utsname()->nodename,
		sizeof(nn->nfsd_name));

	error = nfsd_create_serv(net);
	if (error)
		goto out;
	serv = nn->nfsd_serv;

	error = nfsd_startup_net(net, cred);
	if (error)
		goto out_put;
	error = nfsd_set_nrthreads(n, nthreads, net);
	if (error)
		goto out_put;
	error = serv->sv_nrthreads;
out_put:
	if (serv->sv_nrthreads == 0)
		nfsd_destroy_serv(net);
out:
	return error;
}

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
static bool
nfsd_support_acl_version(int vers)
{
	if (vers >= NFSD_ACL_MINVERS && vers < NFSD_ACL_NRVERS)
		return nfsd_acl_version[vers] != NULL;
	return false;
}

static int
nfsd_acl_rpcbind_set(struct net *net, const struct svc_program *progp,
		     u32 version, int family, unsigned short proto,
		     unsigned short port)
{
	if (!nfsd_support_acl_version(version) ||
	    !nfsd_vers(net_generic(net, nfsd_net_id), version, NFSD_TEST))
		return 0;
	return svc_generic_rpcbind_set(net, progp, version, family,
			proto, port);
}

static __be32
nfsd_acl_init_request(struct svc_rqst *rqstp,
		      const struct svc_program *progp,
		      struct svc_process_info *ret)
{
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	int i;

	if (likely(nfsd_support_acl_version(rqstp->rq_vers) &&
	    nfsd_vers(nn, rqstp->rq_vers, NFSD_TEST)))
		return svc_generic_init_request(rqstp, progp, ret);

	ret->mismatch.lovers = NFSD_ACL_NRVERS;
	for (i = NFSD_ACL_MINVERS; i < NFSD_ACL_NRVERS; i++) {
		if (nfsd_support_acl_version(rqstp->rq_vers) &&
		    nfsd_vers(nn, i, NFSD_TEST)) {
			ret->mismatch.lovers = i;
			break;
		}
	}
	if (ret->mismatch.lovers == NFSD_ACL_NRVERS)
		return rpc_prog_unavail;
	ret->mismatch.hivers = NFSD_ACL_MINVERS;
	for (i = NFSD_ACL_NRVERS - 1; i >= NFSD_ACL_MINVERS; i--) {
		if (nfsd_support_acl_version(rqstp->rq_vers) &&
		    nfsd_vers(nn, i, NFSD_TEST)) {
			ret->mismatch.hivers = i;
			break;
		}
	}
	return rpc_prog_mismatch;
}
#endif

static int
nfsd_rpcbind_set(struct net *net, const struct svc_program *progp,
		 u32 version, int family, unsigned short proto,
		 unsigned short port)
{
	if (!nfsd_vers(net_generic(net, nfsd_net_id), version, NFSD_TEST))
		return 0;
	return svc_generic_rpcbind_set(net, progp, version, family,
			proto, port);
}

static __be32
nfsd_init_request(struct svc_rqst *rqstp,
		  const struct svc_program *progp,
		  struct svc_process_info *ret)
{
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	int i;

	if (likely(nfsd_vers(nn, rqstp->rq_vers, NFSD_TEST)))
		return svc_generic_init_request(rqstp, progp, ret);

	ret->mismatch.lovers = NFSD_MAXVERS + 1;
	for (i = NFSD_MINVERS; i <= NFSD_MAXVERS; i++) {
		if (nfsd_vers(nn, i, NFSD_TEST)) {
			ret->mismatch.lovers = i;
			break;
		}
	}
	if (ret->mismatch.lovers > NFSD_MAXVERS)
		return rpc_prog_unavail;
	ret->mismatch.hivers = NFSD_MINVERS;
	for (i = NFSD_MAXVERS; i >= NFSD_MINVERS; i--) {
		if (nfsd_vers(nn, i, NFSD_TEST)) {
			ret->mismatch.hivers = i;
			break;
		}
	}
	return rpc_prog_mismatch;
}

/*
 * This is the NFS server kernel thread
 */
static int
nfsd(void *vrqstp)
{
	struct svc_rqst *rqstp = (struct svc_rqst *) vrqstp;
	struct svc_xprt *perm_sock = list_entry(rqstp->rq_server->sv_permsocks.next, typeof(struct svc_xprt), xpt_list);
	struct net *net = perm_sock->xpt_net;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	/* At this point, the thread shares current->fs
	 * with the init process. We need to create files with the
	 * umask as defined by the client instead of init's umask.
	 */
	svc_thread_init_status(rqstp, unshare_fs_struct());

	current->fs->umask = 0;

	atomic_inc(&nfsd_th_cnt);

	set_freezable();

	/*
	 * The main request loop
	 */
	while (!svc_thread_should_stop(rqstp)) {
		svc_recv(rqstp);
		nfsd_file_net_dispose(nn);
	}

	atomic_dec(&nfsd_th_cnt);

	/* Release the thread */
	svc_exit_thread(rqstp);
	return 0;
}

/**
 * nfsd_dispatch - Process an NFS or NFSACL or LOCALIO Request
 * @rqstp: incoming request
 *
 * This RPC dispatcher integrates the NFS server's duplicate reply cache.
 *
 * Return values:
 *  %0: Processing complete; do not send a Reply
 *  %1: Processing complete; send Reply in rqstp->rq_res
 */
int nfsd_dispatch(struct svc_rqst *rqstp)
{
	const struct svc_procedure *proc = rqstp->rq_procinfo;
	__be32 *statp = rqstp->rq_accept_statp;
	struct nfsd_cacherep *rp;
	unsigned int start, len;
	__be32 *nfs_reply;

	/*
	 * Give the xdr decoder a chance to change this if it wants
	 * (necessary in the NFSv4.0 compound case)
	 */
	rqstp->rq_cachetype = proc->pc_cachetype;

	/*
	 * ->pc_decode advances the argument stream past the NFS
	 * Call header, so grab the header's starting location and
	 * size now for the call to nfsd_cache_lookup().
	 */
	start = xdr_stream_pos(&rqstp->rq_arg_stream);
	len = xdr_stream_remaining(&rqstp->rq_arg_stream);
	if (!proc->pc_decode(rqstp, &rqstp->rq_arg_stream))
		goto out_decode_err;

	/*
	 * Release rq_status_counter setting it to an odd value after the rpc
	 * request has been properly parsed. rq_status_counter is used to
	 * notify the consumers if the rqstp fields are stable
	 * (rq_status_counter is odd) or not meaningful (rq_status_counter
	 * is even).
	 */
	smp_store_release(&rqstp->rq_status_counter, rqstp->rq_status_counter | 1);

	rp = NULL;
	switch (nfsd_cache_lookup(rqstp, start, len, &rp)) {
	case RC_DOIT:
		break;
	case RC_REPLY:
		goto out_cached_reply;
	case RC_DROPIT:
		goto out_dropit;
	}

	nfs_reply = xdr_inline_decode(&rqstp->rq_res_stream, 0);
	*statp = proc->pc_func(rqstp);
	if (test_bit(RQ_DROPME, &rqstp->rq_flags))
		goto out_update_drop;

	if (!proc->pc_encode(rqstp, &rqstp->rq_res_stream))
		goto out_encode_err;

	/*
	 * Release rq_status_counter setting it to an even value after the rpc
	 * request has been properly processed.
	 */
	smp_store_release(&rqstp->rq_status_counter, rqstp->rq_status_counter + 1);

	nfsd_cache_update(rqstp, rp, rqstp->rq_cachetype, nfs_reply);
out_cached_reply:
	return 1;

out_decode_err:
	trace_nfsd_garbage_args_err(rqstp);
	*statp = rpc_garbage_args;
	return 1;

out_update_drop:
	nfsd_cache_update(rqstp, rp, RC_NOCACHE, NULL);
out_dropit:
	return 0;

out_encode_err:
	trace_nfsd_cant_encode_err(rqstp);
	nfsd_cache_update(rqstp, rp, RC_NOCACHE, NULL);
	*statp = rpc_system_err;
	return 1;
}

/**
 * nfssvc_decode_voidarg - Decode void arguments
 * @rqstp: Server RPC transaction context
 * @xdr: XDR stream positioned at arguments to decode
 *
 * Return values:
 *   %false: Arguments were not valid
 *   %true: Decoding was successful
 */
bool nfssvc_decode_voidarg(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	return true;
}

/**
 * nfssvc_encode_voidres - Encode void results
 * @rqstp: Server RPC transaction context
 * @xdr: XDR stream into which to encode results
 *
 * Return values:
 *   %false: Local error while encoding
 *   %true: Encoding was successful
 */
bool nfssvc_encode_voidres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	return true;
}
