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

static struct nlm_host *	nlm_hosts[NLM_HOST_NRHASH];
static unsigned long		next_gc;
static int			nrhosts;
static DEFINE_MUTEX(nlm_host_mutex);


static void			nlm_gc_hosts(void);
static struct nsm_handle *	__nsm_find(const struct sockaddr_in *,
					const char *, int, int);

/*
 * Find an NLM server handle in the cache. If there is none, create it.
 */
struct nlm_host *
nlmclnt_lookup_host(const struct sockaddr_in *sin, int proto, int version,
			const char *hostname, int hostname_len)
{
	return nlm_lookup_host(0, sin, proto, version,
			       hostname, hostname_len);
}

/*
 * Find an NLM client handle in the cache. If there is none, create it.
 */
struct nlm_host *
nlmsvc_lookup_host(struct svc_rqst *rqstp,
			const char *hostname, int hostname_len)
{
	return nlm_lookup_host(1, &rqstp->rq_addr,
			       rqstp->rq_prot, rqstp->rq_vers,
			       hostname, hostname_len);
}

/*
 * Common host lookup routine for server & client
 */
struct nlm_host *
nlm_lookup_host(int server, const struct sockaddr_in *sin,
					int proto, int version,
					const char *hostname,
					int hostname_len)
{
	struct nlm_host	*host, **hp;
	struct nsm_handle *nsm = NULL;
	int		hash;

	dprintk("lockd: nlm_lookup_host(%u.%u.%u.%u, p=%d, v=%d, my role=%s, name=%.*s)\n",
			NIPQUAD(sin->sin_addr.s_addr), proto, version,
			server? "server" : "client",
			hostname_len,
			hostname? hostname : "<none>");


	hash = NLM_ADDRHASH(sin->sin_addr.s_addr);

	/* Lock hash table */
	mutex_lock(&nlm_host_mutex);

	if (time_after_eq(jiffies, next_gc))
		nlm_gc_hosts();

	/* We may keep several nlm_host objects for a peer, because each
	 * nlm_host is identified by
	 * (address, protocol, version, server/client)
	 * We could probably simplify this a little by putting all those
	 * different NLM rpc_clients into one single nlm_host object.
	 * This would allow us to have one nlm_host per address.
	 */
	for (hp = &nlm_hosts[hash]; (host = *hp) != 0; hp = &host->h_next) {
		if (!nlm_cmp_addr(&host->h_addr, sin))
			continue;

		/* See if we have an NSM handle for this client */
		if (!nsm && (nsm = host->h_nsmhandle) != 0)
			atomic_inc(&nsm->sm_count);

		if (host->h_proto != proto)
			continue;
		if (host->h_version != version)
			continue;
		if (host->h_server != server)
			continue;

		if (hp != nlm_hosts + hash) {
			*hp = host->h_next;
			host->h_next = nlm_hosts[hash];
			nlm_hosts[hash] = host;
		}
		nlm_get_host(host);
		goto out;
	}

	/* Sadly, the host isn't in our hash table yet. See if
	 * we have an NSM handle for it. If not, create one.
	 */
	if (!nsm && !(nsm = nsm_find(sin, hostname, hostname_len)))
		goto out;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host) {
		nsm_release(nsm);
		goto out;
	}
	host->h_name	   = nsm->sm_name;
	host->h_addr       = *sin;
	host->h_addr.sin_port = 0;	/* ouch! */
	host->h_version    = version;
	host->h_proto      = proto;
	host->h_rpcclnt    = NULL;
	mutex_init(&host->h_mutex);
	host->h_nextrebind = jiffies + NLM_HOST_REBIND;
	host->h_expires    = jiffies + NLM_HOST_EXPIRE;
	atomic_set(&host->h_count, 1);
	init_waitqueue_head(&host->h_gracewait);
	init_rwsem(&host->h_rwsem);
	host->h_state      = 0;			/* pseudo NSM state */
	host->h_nsmstate   = 0;			/* real NSM state */
	host->h_nsmhandle  = nsm;
	host->h_server	   = server;
	host->h_next       = nlm_hosts[hash];
	nlm_hosts[hash]    = host;
	INIT_LIST_HEAD(&host->h_lockowners);
	spin_lock_init(&host->h_lock);
	INIT_LIST_HEAD(&host->h_granted);
	INIT_LIST_HEAD(&host->h_reclaim);

	if (++nrhosts > NLM_HOST_MAX)
		next_gc = 0;

out:
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

	dprintk("lockd: nlm_bind_host(%08x)\n",
			(unsigned)ntohl(host->h_addr.sin_addr.s_addr));

	/* Lock host handle */
	mutex_lock(&host->h_mutex);

	/* If we've already created an RPC client, check whether
	 * RPC rebind is required
	 */
	if ((clnt = host->h_rpcclnt) != NULL) {
		if (time_after_eq(jiffies, host->h_nextrebind)) {
			rpc_force_rebind(clnt);
			host->h_nextrebind = jiffies + NLM_HOST_REBIND;
			dprintk("lockd: next rebind in %ld jiffies\n",
					host->h_nextrebind - jiffies);
		}
	} else {
		unsigned long increment = nlmsvc_timeout * HZ;
		struct rpc_timeout timeparms = {
			.to_initval	= increment,
			.to_increment	= increment,
			.to_maxval	= increment * 6UL,
			.to_retries	= 5U,
		};
		struct rpc_create_args args = {
			.protocol	= host->h_proto,
			.address	= (struct sockaddr *)&host->h_addr,
			.addrsize	= sizeof(host->h_addr),
			.timeout	= &timeparms,
			.servername	= host->h_name,
			.program	= &nlm_program,
			.version	= host->h_version,
			.authflavor	= RPC_AUTH_UNIX,
			.flags		= (RPC_CLNT_CREATE_HARDRTRY |
					   RPC_CLNT_CREATE_AUTOBIND),
		};

		clnt = rpc_create(&args);
		if (!IS_ERR(clnt))
			host->h_rpcclnt = clnt;
		else {
			printk("lockd: couldn't create RPC handle for %s\n", host->h_name);
			clnt = NULL;
		}
	}

	mutex_unlock(&host->h_mutex);
	return clnt;
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
 * We were notified that the host indicated by address &sin
 * has rebooted.
 * Release all resources held by that peer.
 */
void nlm_host_rebooted(const struct sockaddr_in *sin,
				const char *hostname, int hostname_len,
				u32 new_state)
{
	struct nsm_handle *nsm;
	struct nlm_host	*host, **hp;
	int		hash;

	dprintk("lockd: nlm_host_rebooted(%s, %u.%u.%u.%u)\n",
			hostname, NIPQUAD(sin->sin_addr));

	/* Find the NSM handle for this peer */
	if (!(nsm = __nsm_find(sin, hostname, hostname_len, 0)))
		return;

	/* When reclaiming locks on this peer, make sure that
	 * we set up a new notification */
	nsm->sm_monitored = 0;

	/* Mark all hosts tied to this NSM state as having rebooted.
	 * We run the loop repeatedly, because we drop the host table
	 * lock for this.
	 * To avoid processing a host several times, we match the nsmstate.
	 */
again:	mutex_lock(&nlm_host_mutex);
	for (hash = 0; hash < NLM_HOST_NRHASH; hash++) {
		for (hp = &nlm_hosts[hash]; (host = *hp); hp = &host->h_next) {
			if (host->h_nsmhandle == nsm
			 && host->h_nsmstate != new_state) {
				host->h_nsmstate = new_state;
				host->h_state++;

				nlm_get_host(host);
				mutex_unlock(&nlm_host_mutex);

				if (host->h_server) {
					/* We're server for this guy, just ditch
					 * all the locks he held. */
					nlmsvc_free_host_resources(host);
				} else {
					/* He's the server, initiate lock recovery. */
					nlmclnt_recovery(host);
				}

				nlm_release_host(host);
				goto again;
			}
		}
	}

	mutex_unlock(&nlm_host_mutex);
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

			/*
			 * Unmonitor unless host was invalidated (i.e. lockd restarted)
			 */
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


/*
 * Manage NSM handles
 */
static LIST_HEAD(nsm_handles);
static DECLARE_MUTEX(nsm_sema);

static struct nsm_handle *
__nsm_find(const struct sockaddr_in *sin,
		const char *hostname, int hostname_len,
		int create)
{
	struct nsm_handle *nsm = NULL;
	struct list_head *pos;

	if (!sin)
		return NULL;

	if (hostname && memchr(hostname, '/', hostname_len) != NULL) {
		if (printk_ratelimit()) {
			printk(KERN_WARNING "Invalid hostname \"%.*s\" "
					    "in NFS lock request\n",
				hostname_len, hostname);
		}
		return NULL;
	}

	down(&nsm_sema);
	list_for_each(pos, &nsm_handles) {
		nsm = list_entry(pos, struct nsm_handle, sm_link);

		if (!nlm_cmp_addr(&nsm->sm_addr, sin))
			continue;
		atomic_inc(&nsm->sm_count);
		goto out;
	}

	if (!create) {
		nsm = NULL;
		goto out;
	}

	nsm = kzalloc(sizeof(*nsm) + hostname_len + 1, GFP_KERNEL);
	if (nsm != NULL) {
		nsm->sm_addr = *sin;
		nsm->sm_name = (char *) (nsm + 1);
		memcpy(nsm->sm_name, hostname, hostname_len);
		nsm->sm_name[hostname_len] = '\0';
		atomic_set(&nsm->sm_count, 1);

		list_add(&nsm->sm_link, &nsm_handles);
	}

out:	up(&nsm_sema);
	return nsm;
}

struct nsm_handle *
nsm_find(const struct sockaddr_in *sin, const char *hostname, int hostname_len)
{
	return __nsm_find(sin, hostname, hostname_len, 1);
}

/*
 * Release an NSM handle
 */
void
nsm_release(struct nsm_handle *nsm)
{
	if (!nsm)
		return;
	if (atomic_dec_and_test(&nsm->sm_count)) {
		down(&nsm_sema);
		if (atomic_read(&nsm->sm_count) == 0) {
			list_del(&nsm->sm_link);
			kfree(nsm);
		}
		up(&nsm_sema);
	}
}
