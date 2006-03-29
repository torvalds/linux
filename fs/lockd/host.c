/*
 * linux/fs/lockd/host.c
 *
 * Management for NLM peer hosts. The nlm_host struct is shared
 * between client and server implementation. The only reason to
 * do so is to reduce code bloat.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>
#include <linux/mutex.h>


#define NLMDBG_FACILITY		NLMDBG_HOSTCACHE
#define NLM_HOST_MAX		64
#define NLM_HOST_NRHASH		32
#define NLM_ADDRHASH(addr)	(ntohl(addr) & (NLM_HOST_NRHASH-1))
#define NLM_HOST_REBIND		(60 * HZ)
#define NLM_HOST_EXPIRE		((nrhosts > NLM_HOST_MAX)? 300 * HZ : 120 * HZ)
#define NLM_HOST_COLLECT	((nrhosts > NLM_HOST_MAX)? 120 * HZ :  60 * HZ)
#define NLM_HOST_ADDR(sv)	(&(sv)->s_nlmclnt->cl_xprt->addr)

static struct nlm_host *	nlm_hosts[NLM_HOST_NRHASH];
static unsigned long		next_gc;
static int			nrhosts;
static DEFINE_MUTEX(nlm_host_mutex);


static void			nlm_gc_hosts(void);

/*
 * Find an NLM server handle in the cache. If there is none, create it.
 */
struct nlm_host *
nlmclnt_lookup_host(struct sockaddr_in *sin, int proto, int version)
{
	return nlm_lookup_host(0, sin, proto, version);
}

/*
 * Find an NLM client handle in the cache. If there is none, create it.
 */
struct nlm_host *
nlmsvc_lookup_host(struct svc_rqst *rqstp)
{
	return nlm_lookup_host(1, &rqstp->rq_addr,
			       rqstp->rq_prot, rqstp->rq_vers);
}

/*
 * Common host lookup routine for server & client
 */
struct nlm_host *
nlm_lookup_host(int server, struct sockaddr_in *sin,
					int proto, int version)
{
	struct nlm_host	*host, **hp;
	u32		addr;
	int		hash;

	dprintk("lockd: nlm_lookup_host(%08x, p=%d, v=%d)\n",
			(unsigned)(sin? ntohl(sin->sin_addr.s_addr) : 0), proto, version);

	hash = NLM_ADDRHASH(sin->sin_addr.s_addr);

	/* Lock hash table */
	mutex_lock(&nlm_host_mutex);

	if (time_after_eq(jiffies, next_gc))
		nlm_gc_hosts();

	for (hp = &nlm_hosts[hash]; (host = *hp) != 0; hp = &host->h_next) {
		if (host->h_proto != proto)
			continue;
		if (host->h_version != version)
			continue;
		if (host->h_server != server)
			continue;

		if (nlm_cmp_addr(&host->h_addr, sin)) {
			if (hp != nlm_hosts + hash) {
				*hp = host->h_next;
				host->h_next = nlm_hosts[hash];
				nlm_hosts[hash] = host;
			}
			nlm_get_host(host);
			mutex_unlock(&nlm_host_mutex);
			return host;
		}
	}

	/* Ooops, no host found, create it */
	dprintk("lockd: creating host entry\n");

	if (!(host = (struct nlm_host *) kmalloc(sizeof(*host), GFP_KERNEL)))
		goto nohost;
	memset(host, 0, sizeof(*host));

	addr = sin->sin_addr.s_addr;
	sprintf(host->h_name, "%u.%u.%u.%u", NIPQUAD(addr));

	host->h_addr       = *sin;
	host->h_addr.sin_port = 0;	/* ouch! */
	host->h_version    = version;
	host->h_proto      = proto;
	host->h_rpcclnt    = NULL;
	init_MUTEX(&host->h_sema);
	host->h_nextrebind = jiffies + NLM_HOST_REBIND;
	host->h_expires    = jiffies + NLM_HOST_EXPIRE;
	atomic_set(&host->h_count, 1);
	init_waitqueue_head(&host->h_gracewait);
	host->h_state      = 0;			/* pseudo NSM state */
	host->h_nsmstate   = 0;			/* real NSM state */
	host->h_server	   = server;
	host->h_next       = nlm_hosts[hash];
	nlm_hosts[hash]    = host;
	INIT_LIST_HEAD(&host->h_lockowners);
	spin_lock_init(&host->h_lock);
	INIT_LIST_HEAD(&host->h_granted);
	INIT_LIST_HEAD(&host->h_reclaim);

	if (++nrhosts > NLM_HOST_MAX)
		next_gc = 0;

nohost:
	mutex_unlock(&nlm_host_mutex);
	return host;
}

struct nlm_host *
nlm_find_client(void)
{
	/* find a nlm_host for a client for which h_killed == 0.
	 * and return it
	 */
	int hash;
	mutex_lock(&nlm_host_mutex);
	for (hash = 0 ; hash < NLM_HOST_NRHASH; hash++) {
		struct nlm_host *host, **hp;
		for (hp = &nlm_hosts[hash]; (host = *hp) != 0; hp = &host->h_next) {
			if (host->h_server &&
			    host->h_killed == 0) {
				nlm_get_host(host);
				mutex_unlock(&nlm_host_mutex);
				return host;
			}
		}
	}
	mutex_unlock(&nlm_host_mutex);
	return NULL;
}

				
/*
 * Create the NLM RPC client for an NLM peer
 */
struct rpc_clnt *
nlm_bind_host(struct nlm_host *host)
{
	struct rpc_clnt	*clnt;
	struct rpc_xprt	*xprt;

	dprintk("lockd: nlm_bind_host(%08x)\n",
			(unsigned)ntohl(host->h_addr.sin_addr.s_addr));

	/* Lock host handle */
	down(&host->h_sema);

	/* If we've already created an RPC client, check whether
	 * RPC rebind is required
	 */
	if ((clnt = host->h_rpcclnt) != NULL) {
		xprt = clnt->cl_xprt;
		if (time_after_eq(jiffies, host->h_nextrebind)) {
			rpc_force_rebind(clnt);
			host->h_nextrebind = jiffies + NLM_HOST_REBIND;
			dprintk("lockd: next rebind in %ld jiffies\n",
					host->h_nextrebind - jiffies);
		}
	} else {
		xprt = xprt_create_proto(host->h_proto, &host->h_addr, NULL);
		if (IS_ERR(xprt))
			goto forgetit;

		xprt_set_timeout(&xprt->timeout, 5, nlmsvc_timeout);
		xprt->resvport = 1;	/* NLM requires a reserved port */

		/* Existing NLM servers accept AUTH_UNIX only */
		clnt = rpc_new_client(xprt, host->h_name, &nlm_program,
					host->h_version, RPC_AUTH_UNIX);
		if (IS_ERR(clnt))
			goto forgetit;
		clnt->cl_autobind = 1;	/* turn on pmap queries */
		clnt->cl_softrtry = 1; /* All queries are soft */

		host->h_rpcclnt = clnt;
	}

	up(&host->h_sema);
	return clnt;

forgetit:
	printk("lockd: couldn't create RPC handle for %s\n", host->h_name);
	up(&host->h_sema);
	return NULL;
}

/*
 * Force a portmap lookup of the remote lockd port
 */
void
nlm_rebind_host(struct nlm_host *host)
{
	dprintk("lockd: rebind host %s\n", host->h_name);
	if (host->h_rpcclnt && time_after_eq(jiffies, host->h_nextrebind)) {
		rpc_force_rebind(host->h_rpcclnt);
		host->h_nextrebind = jiffies + NLM_HOST_REBIND;
	}
}

/*
 * Increment NLM host count
 */
struct nlm_host * nlm_get_host(struct nlm_host *host)
{
	if (host) {
		dprintk("lockd: get host %s\n", host->h_name);
		atomic_inc(&host->h_count);
		host->h_expires = jiffies + NLM_HOST_EXPIRE;
	}
	return host;
}

/*
 * Release NLM host after use
 */
void nlm_release_host(struct nlm_host *host)
{
	if (host != NULL) {
		dprintk("lockd: release host %s\n", host->h_name);
		BUG_ON(atomic_read(&host->h_count) < 0);
		if (atomic_dec_and_test(&host->h_count)) {
			BUG_ON(!list_empty(&host->h_lockowners));
			BUG_ON(!list_empty(&host->h_granted));
			BUG_ON(!list_empty(&host->h_reclaim));
		}
	}
}

/*
 * Shut down the hosts module.
 * Note that this routine is called only at server shutdown time.
 */
void
nlm_shutdown_hosts(void)
{
	struct nlm_host	*host;
	int		i;

	dprintk("lockd: shutting down host module\n");
	mutex_lock(&nlm_host_mutex);

	/* First, make all hosts eligible for gc */
	dprintk("lockd: nuking all hosts...\n");
	for (i = 0; i < NLM_HOST_NRHASH; i++) {
		for (host = nlm_hosts[i]; host; host = host->h_next)
			host->h_expires = jiffies - 1;
	}

	/* Then, perform a garbage collection pass */
	nlm_gc_hosts();
	mutex_unlock(&nlm_host_mutex);

	/* complain if any hosts are left */
	if (nrhosts) {
		printk(KERN_WARNING "lockd: couldn't shutdown host module!\n");
		dprintk("lockd: %d hosts left:\n", nrhosts);
		for (i = 0; i < NLM_HOST_NRHASH; i++) {
			for (host = nlm_hosts[i]; host; host = host->h_next) {
				dprintk("       %s (cnt %d use %d exp %ld)\n",
					host->h_name, atomic_read(&host->h_count),
					host->h_inuse, host->h_expires);
			}
		}
	}
}

/*
 * Garbage collect any unused NLM hosts.
 * This GC combines reference counting for async operations with
 * mark & sweep for resources held by remote clients.
 */
static void
nlm_gc_hosts(void)
{
	struct nlm_host	**q, *host;
	struct rpc_clnt	*clnt;
	int		i;

	dprintk("lockd: host garbage collection\n");
	for (i = 0; i < NLM_HOST_NRHASH; i++) {
		for (host = nlm_hosts[i]; host; host = host->h_next)
			host->h_inuse = 0;
	}

	/* Mark all hosts that hold locks, blocks or shares */
	nlmsvc_mark_resources();

	for (i = 0; i < NLM_HOST_NRHASH; i++) {
		q = &nlm_hosts[i];
		while ((host = *q) != NULL) {
			if (atomic_read(&host->h_count) || host->h_inuse
			 || time_before(jiffies, host->h_expires)) {
				dprintk("nlm_gc_hosts skipping %s (cnt %d use %d exp %ld)\n",
					host->h_name, atomic_read(&host->h_count),
					host->h_inuse, host->h_expires);
				q = &host->h_next;
				continue;
			}
			dprintk("lockd: delete host %s\n", host->h_name);
			*q = host->h_next;
			/* Don't unmonitor hosts that have been invalidated */
			if (host->h_monitored && !host->h_killed)
				nsm_unmonitor(host);
			if ((clnt = host->h_rpcclnt) != NULL) {
				if (atomic_read(&clnt->cl_users)) {
					printk(KERN_WARNING
						"lockd: active RPC handle\n");
					clnt->cl_dead = 1;
				} else {
					rpc_destroy_client(host->h_rpcclnt);
				}
			}
			kfree(host);
			nrhosts--;
		}
	}

	next_gc = jiffies + NLM_HOST_COLLECT;
}

