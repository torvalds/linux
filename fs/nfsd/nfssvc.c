/*
 * linux/fs/nfsd/nfssvc.c
 *
 * Central processing for nfsd.
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/nfs.h>
#include <linux/in.h>
#include <linux/uio.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/freezer.h>
#include <linux/fs_struct.h>
#include <linux/kthread.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/cache.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/stats.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/bind.h>
#include <linux/nfsacl.h>

#define NFSDDBG_FACILITY	NFSDDBG_SVC

extern struct svc_program	nfsd_program;
static int			nfsd(void *vrqstp);
struct timeval			nfssvc_boot;
static atomic_t			nfsd_busy;
static unsigned long		nfsd_last_call;
static DEFINE_SPINLOCK(nfsd_call_lock);

/*
 * nfsd_mutex protects nfsd_serv -- both the pointer itself and the members
 * of the svc_serv struct. In particular, ->sv_nrthreads but also to some
 * extent ->sv_temp_socks and ->sv_permsocks. It also protects nfsdstats.th_cnt
 *
 * If (out side the lock) nfsd_serv is non-NULL, then it must point to a
 * properly initialised 'struct svc_serv' with ->sv_nrthreads > 0. That number
 * of nfsd threads must exist and each must listed in ->sp_all_threads in each
 * entry of ->sv_pools[].
 *
 * Transitions of the thread count between zero and non-zero are of particular
 * interest since the svc_serv needs to be created and initialized at that
 * point, or freed.
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
struct svc_serv 		*nfsd_serv;

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
static struct svc_stat	nfsd_acl_svcstats;
static struct svc_version *	nfsd_acl_version[] = {
	[2] = &nfsd_acl_version2,
	[3] = &nfsd_acl_version3,
};

#define NFSD_ACL_MINVERS            2
#define NFSD_ACL_NRVERS		ARRAY_SIZE(nfsd_acl_version)
static struct svc_version *nfsd_acl_versions[NFSD_ACL_NRVERS];

static struct svc_program	nfsd_acl_program = {
	.pg_prog		= NFS_ACL_PROGRAM,
	.pg_nvers		= NFSD_ACL_NRVERS,
	.pg_vers		= nfsd_acl_versions,
	.pg_name		= "nfsacl",
	.pg_class		= "nfsd",
	.pg_stats		= &nfsd_acl_svcstats,
	.pg_authenticate	= &svc_set_client,
};

static struct svc_stat	nfsd_acl_svcstats = {
	.program	= &nfsd_acl_program,
};
#endif /* defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL) */

static struct svc_version *	nfsd_version[] = {
	[2] = &nfsd_version2,
#if defined(CONFIG_NFSD_V3)
	[3] = &nfsd_version3,
#endif
#if defined(CONFIG_NFSD_V4)
	[4] = &nfsd_version4,
#endif
};

#define NFSD_MINVERS    	2
#define NFSD_NRVERS		ARRAY_SIZE(nfsd_version)
static struct svc_version *nfsd_versions[NFSD_NRVERS];

struct svc_program		nfsd_program = {
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
	.pg_next		= &nfsd_acl_program,
#endif
	.pg_prog		= NFS_PROGRAM,		/* program number */
	.pg_nvers		= NFSD_NRVERS,		/* nr of entries in nfsd_version */
	.pg_vers		= nfsd_versions,	/* version table */
	.pg_name		= "nfsd",		/* program name */
	.pg_class		= "nfsd",		/* authentication class */
	.pg_stats		= &nfsd_svcstats,	/* version table */
	.pg_authenticate	= &svc_set_client,	/* export authentication */

};

int nfsd_vers(int vers, enum vers_op change)
{
	if (vers < NFSD_MINVERS || vers >= NFSD_NRVERS)
		return -1;
	switch(change) {
	case NFSD_SET:
		nfsd_versions[vers] = nfsd_version[vers];
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
		if (vers < NFSD_ACL_NRVERS)
			nfsd_acl_versions[vers] = nfsd_acl_version[vers];
#endif
		break;
	case NFSD_CLEAR:
		nfsd_versions[vers] = NULL;
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
		if (vers < NFSD_ACL_NRVERS)
			nfsd_acl_versions[vers] = NULL;
#endif
		break;
	case NFSD_TEST:
		return nfsd_versions[vers] != NULL;
	case NFSD_AVAIL:
		return nfsd_version[vers] != NULL;
	}
	return 0;
}
/*
 * Maximum number of nfsd processes
 */
#define	NFSD_MAXSERVS		8192

int nfsd_nrthreads(void)
{
	int rv = 0;
	mutex_lock(&nfsd_mutex);
	if (nfsd_serv)
		rv = nfsd_serv->sv_nrthreads;
	mutex_unlock(&nfsd_mutex);
	return rv;
}

static void nfsd_last_thread(struct svc_serv *serv)
{
	/* When last nfsd thread exits we need to do some clean-up */
	struct svc_xprt *xprt;
	list_for_each_entry(xprt, &serv->sv_permsocks, xpt_list)
		lockd_down();
	nfsd_serv = NULL;
	nfsd_racache_shutdown();
	nfs4_state_shutdown();

	printk(KERN_WARNING "nfsd: last server has exited, flushing export "
			    "cache\n");
	nfsd_export_flush();
}

void nfsd_reset_versions(void)
{
	int found_one = 0;
	int i;

	for (i = NFSD_MINVERS; i < NFSD_NRVERS; i++) {
		if (nfsd_program.pg_vers[i])
			found_one = 1;
	}

	if (!found_one) {
		for (i = NFSD_MINVERS; i < NFSD_NRVERS; i++)
			nfsd_program.pg_vers[i] = nfsd_version[i];
#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
		for (i = NFSD_ACL_MINVERS; i < NFSD_ACL_NRVERS; i++)
			nfsd_acl_program.pg_vers[i] =
				nfsd_acl_version[i];
#endif
	}
}


int nfsd_create_serv(void)
{
	int err = 0;

	WARN_ON(!mutex_is_locked(&nfsd_mutex));
	if (nfsd_serv) {
		svc_get(nfsd_serv);
		return 0;
	}
	if (nfsd_max_blksize == 0) {
		/* choose a suitable default */
		struct sysinfo i;
		si_meminfo(&i);
		/* Aim for 1/4096 of memory per thread
		 * This gives 1MB on 4Gig machines
		 * But only uses 32K on 128M machines.
		 * Bottom out at 8K on 32M and smaller.
		 * Of course, this is only a default.
		 */
		nfsd_max_blksize = NFSSVC_MAXBLKSIZE;
		i.totalram <<= PAGE_SHIFT - 12;
		while (nfsd_max_blksize > i.totalram &&
		       nfsd_max_blksize >= 8*1024*2)
			nfsd_max_blksize /= 2;
	}

	atomic_set(&nfsd_busy, 0);
	nfsd_serv = svc_create_pooled(&nfsd_program, nfsd_max_blksize,
				      AF_INET,
				      nfsd_last_thread, nfsd, THIS_MODULE);
	if (nfsd_serv == NULL)
		err = -ENOMEM;

	do_gettimeofday(&nfssvc_boot);		/* record boot time */
	return err;
}

static int nfsd_init_socks(int port)
{
	int error;
	if (!list_empty(&nfsd_serv->sv_permsocks))
		return 0;

	error = svc_create_xprt(nfsd_serv, "udp", port,
					SVC_SOCK_DEFAULTS);
	if (error < 0)
		return error;

	error = lockd_up();
	if (error < 0)
		return error;

	error = svc_create_xprt(nfsd_serv, "tcp", port,
					SVC_SOCK_DEFAULTS);
	if (error < 0)
		return error;

	error = lockd_up();
	if (error < 0)
		return error;

	return 0;
}

int nfsd_nrpools(void)
{
	if (nfsd_serv == NULL)
		return 0;
	else
		return nfsd_serv->sv_nrpools;
}

int nfsd_get_nrthreads(int n, int *nthreads)
{
	int i = 0;

	if (nfsd_serv != NULL) {
		for (i = 0; i < nfsd_serv->sv_nrpools && i < n; i++)
			nthreads[i] = nfsd_serv->sv_pools[i].sp_nrthreads;
	}

	return 0;
}

int nfsd_set_nrthreads(int n, int *nthreads)
{
	int i = 0;
	int tot = 0;
	int err = 0;

	WARN_ON(!mutex_is_locked(&nfsd_mutex));

	if (nfsd_serv == NULL || n <= 0)
		return 0;

	if (n > nfsd_serv->sv_nrpools)
		n = nfsd_serv->sv_nrpools;

	/* enforce a global maximum number of threads */
	tot = 0;
	for (i = 0; i < n; i++) {
		if (nthreads[i] > NFSD_MAXSERVS)
			nthreads[i] = NFSD_MAXSERVS;
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

	/*
	 * There must always be a thread in pool 0; the admin
	 * can't shut down NFS completely using pool_threads.
	 */
	if (nthreads[0] == 0)
		nthreads[0] = 1;

	/* apply the new numbers */
	svc_get(nfsd_serv);
	for (i = 0; i < n; i++) {
		err = svc_set_num_threads(nfsd_serv, &nfsd_serv->sv_pools[i],
				    	  nthreads[i]);
		if (err)
			break;
	}
	svc_destroy(nfsd_serv);

	return err;
}

int
nfsd_svc(unsigned short port, int nrservs)
{
	int	error;

	mutex_lock(&nfsd_mutex);
	dprintk("nfsd: creating service\n");
	error = -EINVAL;
	if (nrservs <= 0)
		nrservs = 0;
	if (nrservs > NFSD_MAXSERVS)
		nrservs = NFSD_MAXSERVS;
	
	/* Readahead param cache - will no-op if it already exists */
	error =	nfsd_racache_init(2*nrservs);
	if (error<0)
		goto out;
	nfs4_state_start();

	nfsd_reset_versions();

	error = nfsd_create_serv();

	if (error)
		goto out;
	error = nfsd_init_socks(port);
	if (error)
		goto failure;

	error = svc_set_num_threads(nfsd_serv, NULL, nrservs);
 failure:
	svc_destroy(nfsd_serv);		/* Release server */
 out:
	mutex_unlock(&nfsd_mutex);
	return error;
}

static inline void
update_thread_usage(int busy_threads)
{
	unsigned long prev_call;
	unsigned long diff;
	int decile;

	spin_lock(&nfsd_call_lock);
	prev_call = nfsd_last_call;
	nfsd_last_call = jiffies;
	decile = busy_threads*10/nfsdstats.th_cnt;
	if (decile>0 && decile <= 10) {
		diff = nfsd_last_call - prev_call;
		if ( (nfsdstats.th_usage[decile-1] += diff) >= NFSD_USAGE_WRAP)
			nfsdstats.th_usage[decile-1] -= NFSD_USAGE_WRAP;
		if (decile == 10)
			nfsdstats.th_fullcnt++;
	}
	spin_unlock(&nfsd_call_lock);
}

/*
 * This is the NFS server kernel thread
 */
static int
nfsd(void *vrqstp)
{
	struct svc_rqst *rqstp = (struct svc_rqst *) vrqstp;
	int err, preverr = 0;

	/* Lock module and set up kernel thread */
	mutex_lock(&nfsd_mutex);

	/* At this point, the thread shares current->fs
	 * with the init process. We need to create files with a
	 * umask of 0 instead of init's umask. */
	if (unshare_fs_struct() < 0) {
		printk("Unable to start nfsd thread: out of memory\n");
		goto out;
	}

	current->fs->umask = 0;

	/*
	 * thread is spawned with all signals set to SIG_IGN, re-enable
	 * the ones that will bring down the thread
	 */
	allow_signal(SIGKILL);
	allow_signal(SIGHUP);
	allow_signal(SIGINT);
	allow_signal(SIGQUIT);

	nfsdstats.th_cnt++;
	mutex_unlock(&nfsd_mutex);

	/*
	 * We want less throttling in balance_dirty_pages() so that nfs to
	 * localhost doesn't cause nfsd to lock up due to all the client's
	 * dirty pages.
	 */
	current->flags |= PF_LESS_THROTTLE;
	set_freezable();

	/*
	 * The main request loop
	 */
	for (;;) {
		/*
		 * Find a socket with data available and call its
		 * recvfrom routine.
		 */
		while ((err = svc_recv(rqstp, 60*60*HZ)) == -EAGAIN)
			;
		if (err == -EINTR)
			break;
		else if (err < 0) {
			if (err != preverr) {
				printk(KERN_WARNING "%s: unexpected error "
					"from svc_recv (%d)\n", __func__, -err);
				preverr = err;
			}
			schedule_timeout_uninterruptible(HZ);
			continue;
		}

		update_thread_usage(atomic_read(&nfsd_busy));
		atomic_inc(&nfsd_busy);

		/* Lock the export hash tables for reading. */
		exp_readlock();

		svc_process(rqstp);

		/* Unlock export hash tables */
		exp_readunlock();
		update_thread_usage(atomic_read(&nfsd_busy));
		atomic_dec(&nfsd_busy);
	}

	/* Clear signals before calling svc_exit_thread() */
	flush_signals(current);

	mutex_lock(&nfsd_mutex);
	nfsdstats.th_cnt --;

out:
	/* Release the thread */
	svc_exit_thread(rqstp);

	/* Release module */
	mutex_unlock(&nfsd_mutex);
	module_put_and_exit(0);
	return 0;
}

static __be32 map_new_errors(u32 vers, __be32 nfserr)
{
	if (nfserr == nfserr_jukebox && vers == 2)
		return nfserr_dropit;
	if (nfserr == nfserr_wrongsec && vers < 4)
		return nfserr_acces;
	return nfserr;
}

int
nfsd_dispatch(struct svc_rqst *rqstp, __be32 *statp)
{
	struct svc_procedure	*proc;
	kxdrproc_t		xdr;
	__be32			nfserr;
	__be32			*nfserrp;

	dprintk("nfsd_dispatch: vers %d proc %d\n",
				rqstp->rq_vers, rqstp->rq_proc);
	proc = rqstp->rq_procinfo;

	/* Check whether we have this call in the cache. */
	switch (nfsd_cache_lookup(rqstp, proc->pc_cachetype)) {
	case RC_INTR:
	case RC_DROPIT:
		return 0;
	case RC_REPLY:
		return 1;
	case RC_DOIT:;
		/* do it */
	}

	/* Decode arguments */
	xdr = proc->pc_decode;
	if (xdr && !xdr(rqstp, (__be32*)rqstp->rq_arg.head[0].iov_base,
			rqstp->rq_argp)) {
		dprintk("nfsd: failed to decode arguments!\n");
		nfsd_cache_update(rqstp, RC_NOCACHE, NULL);
		*statp = rpc_garbage_args;
		return 1;
	}

	/* need to grab the location to store the status, as
	 * nfsv4 does some encoding while processing 
	 */
	nfserrp = rqstp->rq_res.head[0].iov_base
		+ rqstp->rq_res.head[0].iov_len;
	rqstp->rq_res.head[0].iov_len += sizeof(__be32);

	/* Now call the procedure handler, and encode NFS status. */
	nfserr = proc->pc_func(rqstp, rqstp->rq_argp, rqstp->rq_resp);
	nfserr = map_new_errors(rqstp->rq_vers, nfserr);
	if (nfserr == nfserr_dropit) {
		dprintk("nfsd: Dropping request; may be revisited later\n");
		nfsd_cache_update(rqstp, RC_NOCACHE, NULL);
		return 0;
	}

	if (rqstp->rq_proc != 0)
		*nfserrp++ = nfserr;

	/* Encode result.
	 * For NFSv2, additional info is never returned in case of an error.
	 */
	if (!(nfserr && rqstp->rq_vers == 2)) {
		xdr = proc->pc_encode;
		if (xdr && !xdr(rqstp, nfserrp,
				rqstp->rq_resp)) {
			/* Failed to encode result. Release cache entry */
			dprintk("nfsd: failed to encode result!\n");
			nfsd_cache_update(rqstp, RC_NOCACHE, NULL);
			*statp = rpc_system_err;
			return 1;
		}
	}

	/* Store reply in cache. */
	nfsd_cache_update(rqstp, proc->pc_cachetype, statp + 1);
	return 1;
}
