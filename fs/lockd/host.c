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
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/mutex.h>

#include <net/ipv6.h>

#define NLMDBG_FACILITY		NLMDBG_HOSTCACHE
#define NLM_HOST_NRHASH		32
#define NLM_HOST_REBIND		(60 * HZ)
#define NLM_HOST_EXPIRE		(300 * HZ)
#define NLM_HOST_COLLECT	(120 * HZ)

static struct hlist_head	nlm_hosts[NLM_HOST_NRHASH];
static unsigned long		next_gc;
static int			nrhosts;
static DEFINE_MUTEX(nlm_host_mutex);

static void			nlm_gc_hosts(void);

struct nlm_lookup_host_info {
	const int		server;		/* search for server|client */
	const struct sockaddr	*sap;		/* address to search for */
	const size_t		salen;		/* it's length */
	const unsigned short	protocol;	/* transport to search for*/
	const u32		version;	/* NLM version to search for */
	const char		*hostname;	/* remote's hostname */
	const size_t		hostname_len;	/* it's length */
	const struct sockaddr	*src_sap;	/* our address (optional) */
	const size_t		src_len;	/* it's length */
	const int		noresvport;	/* use non-priv port */
};

/*
 * Hash function must work well on big- and little-endian platforms
 */
static unsigned int __nlm_hash32(const __be32 n)
{
	unsigned int hash = (__force u32)n ^ ((__force u32)n >> 16);
	return hash ^ (hash >> 8);
}

static unsigned int __nlm_hash_addr4(const struct sockaddr *sap)
{
	const struct sockaddr_in *sin = (struct sockaddr_in *)sap;
	return __nlm_hash32(sin->sin_addr.s_addr);
}

static unsigned int __nlm_hash_addr6(const struct sockaddr *sap)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	const struct in6_addr addr = sin6->sin6_addr;
	return __nlm_hash32(addr.s6_addr32[0]) ^
	       __nlm_hash32(addr.s6_addr32[1]) ^
	       __nlm_hash32(addr.s6_addr32[2]) ^
	       __nlm_hash32(addr.s6_addr32[3]);
}

static unsigned int nlm_hash_address(const struct sockaddr *sap)
{
	unsigned int hash;

	switch (sap->sa_family) {
	case AF_INET:
		hash = __nlm_hash_addr4(sap);
		break;
	case AF_INET6:
		hash = __nlm_hash_addr6(sap);
		break;
	default:
		hash = 0;
	}
	return hash & (NLM_HOST_NRHASH - 1);
}

static void nlm_clear_port(struct sockaddr *sap)
{
	switch (sap->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sap)->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)sap)->sin6_port = 0;
		break;
	}
}

/*
 * Common host lookup routine for server & client
 */
static struct nlm_host *nlm_lookup_host(struct nlm_lookup_host_info *ni)
{
	struct hlist_head *chain;
	struct hlist_node *pos;
	struct nlm_host	*host;
	struct nsm_handle *nsm = NULL;

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
	chain = &nlm_hosts[nlm_hash_address(ni->sap)];
	hlist_for_each_entry(host, pos, chain, h_hash) {
		if (!nlm_cmp_addr(nlm_addr(host), ni->sap))
			continue;

		/* See if we have an NSM handle for this client */
		if (!nsm)
			nsm = host->h_nsmhandle;

		if (host->h_proto != ni->protocol)
			continue;
		if (host->h_version != ni->version)
			continue;
		if (host->h_server != ni->server)
			continue;
		if (ni->server &&
		    !nlm_cmp_addr(nlm_srcaddr(host), ni->src_sap))
			continue;

		/* Move to head of hash chain. */
		hlist_del(&host->h_hash);
		hlist_add_head(&host->h_hash, chain);

		nlm_get_host(host);
		dprintk("lockd: nlm_lookup_host found host %s (%s)\n",
				host->h_name, host->h_addrbuf);
		goto out;
	}

	/*
	 * The host wasn't in our hash table.  If we don't
	 * have an NSM handle for it yet, create one.
	 */
	if (nsm)
		atomic_inc(&nsm->sm_count);
	else {
		host = NULL;
		nsm = nsm_get_handle(ni->sap, ni->salen,
					ni->hostname, ni->hostname_len);
		if (!nsm) {
			dprintk("lockd: nlm_lookup_host failed; "
				"no nsm handle\n");
			goto out;
		}
	}

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host) {
		nsm_release(nsm);
		dprintk("lockd: nlm_lookup_host failed; no memory\n");
		goto out;
	}
	host->h_name	   = nsm->sm_name;
	host->h_addrbuf    = nsm->sm_addrbuf;
	memcpy(nlm_addr(host), ni->sap, ni->salen);
	host->h_addrlen = ni->salen;
	nlm_clear_port(nlm_addr(host));
	memcpy(nlm_srcaddr(host), ni->src_sap, ni->src_len);
	host->h_version    = ni->version;
	host->h_proto      = ni->protocol;
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
	host->h_server	   = ni->server;
	host->h_noresvport = ni->noresvport;
	hlist_add_head(&host->h_hash, chain);
	INIT_LIST_HEAD(&host->h_lockowners);
	spin_lock_init(&host->h_lock);
	INIT_LIST_HEAD(&host->h_granted);
	INIT_LIST_HEAD(&host->h_reclaim);

	nrhosts++;

	dprintk("lockd: nlm_lookup_host created host %s\n",
			host->h_name);

out:
	mutex_unlock(&nlm_host_mutex);
	return host;
}

/*
 * Destroy a host
 */
static void
nlm_destroy_host(struct nlm_host *host)
{
	struct rpc_clnt	*clnt;

	BUG_ON(!list_empty(&host->h_lockowners));
	BUG_ON(atomic_read(&host->h_count));

	nsm_unmonitor(host);
	nsm_release(host->h_nsmhandle);

	clnt = host->h_rpcclnt;
	if (clnt != NULL)
		rpc_shutdown_client(clnt);
	kfree(host);
}

/**
 * nlmclnt_lookup_host - Find an NLM host handle matching a remote server
 * @sap: network address of server
 * @salen: length of server address
 * @protocol: transport protocol to use
 * @version: NLM protocol version
 * @hostname: '\0'-terminated hostname of server
 * @noresvport: 1 if non-privileged port should be used
 *
 * Returns an nlm_host structure that matches the passed-in
 * [server address, transport protocol, NLM version, server hostname].
 * If one doesn't already exist in the host cache, a new handle is
 * created and returned.
 */
struct nlm_host *nlmclnt_lookup_host(const struct sockaddr *sap,
				     const size_t salen,
				     const unsigned short protocol,
				     const u32 version,
				     const char *hostname,
				     int noresvport)
{
	const struct sockaddr source = {
		.sa_family	= AF_UNSPEC,
	};
	struct nlm_lookup_host_info ni = {
		.server		= 0,
		.sap		= sap,
		.salen		= salen,
		.protocol	= protocol,
		.version	= version,
		.hostname	= hostname,
		.hostname_len	= strlen(hostname),
		.src_sap	= &source,
		.src_len	= sizeof(source),
		.noresvport	= noresvport,
	};

	dprintk("lockd: %s(host='%s', vers=%u, proto=%s)\n", __func__,
			(hostname ? hostname : "<none>"), version,
			(protocol == IPPROTO_UDP ? "udp" : "tcp"));

	return nlm_lookup_host(&ni);
}

/**
 * nlmsvc_lookup_host - Find an NLM host handle matching a remote client
 * @rqstp: incoming NLM request
 * @hostname: name of client host
 * @hostname_len: length of client hostname
 *
 * Returns an nlm_host structure that matches the [client address,
 * transport protocol, NLM version, client hostname] of the passed-in
 * NLM request.  If one doesn't already exist in the host cache, a
 * new handle is created and returned.
 *
 * Before possibly creating a new nlm_host, construct a sockaddr
 * for a specific source address in case the local system has
 * multiple network addresses.  The family of the address in
 * rq_daddr is guaranteed to be the same as the family of the
 * address in rq_addr, so it's safe to use the same family for
 * the source address.
 */
struct nlm_host *nlmsvc_lookup_host(const struct svc_rqst *rqstp,
				    const char *hostname,
				    const size_t hostname_len)
{
	struct sockaddr_in sin = {
		.sin_family	= AF_INET,
	};
	struct sockaddr_in6 sin6 = {
		.sin6_family	= AF_INET6,
	};
	struct nlm_lookup_host_info ni = {
		.server		= 1,
		.sap		= svc_addr(rqstp),
		.salen		= rqstp->rq_addrlen,
		.protocol	= rqstp->rq_prot,
		.version	= rqstp->rq_vers,
		.hostname	= hostname,
		.hostname_len	= hostname_len,
		.src_len	= rqstp->rq_addrlen,
	};

	dprintk("lockd: %s(host='%*s', vers=%u, proto=%s)\n", __func__,
			(int)hostname_len, hostname, rqstp->rq_vers,
			(rqstp->rq_prot == IPPROTO_UDP ? "udp" : "tcp"));

	switch (ni.sap->sa_family) {
	case AF_INET:
		sin.sin_addr.s_addr = rqstp->rq_daddr.addr.s_addr;
		ni.src_sap = (struct sockaddr *)&sin;
		break;
	case AF_INET6:
		ipv6_addr_copy(&sin6.sin6_addr, &rqstp->rq_daddr.addr6);
		ni.src_sap = (struct sockaddr *)&sin6;
		break;
	default:
		return NULL;
	}

	return nlm_lookup_host(&ni);
}

/*
 * Create the NLM RPC client for an NLM peer
 */
struct rpc_clnt *
nlm_bind_host(struct nlm_host *host)
{
	struct rpc_clnt	*clnt;

	dprintk("lockd: nlm_bind_host %s (%s)\n",
			host->h_name, host->h_addrbuf);

	/* Lock host handle */
	mutex_lock(&host->h_mutex);

	/* If we've already created an RPC client, check whether
	 * RPC rebind is required
	 */
	if ((clnt = host->h_rpcclnt) != NULL) {
		if (time_after_eq(jiffies, host->h_nextrebind)) {
			rpc_force_rebind(clnt);
			host->h_nextrebind = jiffies + NLM_HOST_REBIND;
			dprintk("lockd: next rebind in %lu jiffies\n",
					host->h_nextrebind - jiffies);
		}
	} else {
		unsigned long increment = nlmsvc_timeout;
		struct rpc_timeout timeparms = {
			.to_initval	= increment,
			.to_increment	= increment,
			.to_maxval	= increment * 6UL,
			.to_retries	= 5U,
		};
		struct rpc_create_args args = {
			.protocol	= host->h_proto,
			.address	= nlm_addr(host),
			.addrsize	= host->h_addrlen,
			.saddress	= nlm_srcaddr(host),
			.timeout	= &timeparms,
			.servername	= host->h_name,
			.program	= &nlm_program,
			.version	= host->h_version,
			.authflavor	= RPC_AUTH_UNIX,
			.flags		= (RPC_CLNT_CREATE_NOPING |
					   RPC_CLNT_CREATE_AUTOBIND),
		};

		/*
		 * lockd retries server side blocks automatically so we want
		 * those to be soft RPC calls. Client side calls need to be
		 * hard RPC tasks.
		 */
		if (!host->h_server)
			args.flags |= RPC_CLNT_CREATE_HARDRTRY;
		if (host->h_noresvport)
			args.flags |= RPC_CLNT_CREATE_NONPRIVPORT;

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

/**
 * nlm_host_rebooted - Release all resources held by rebooted host
 * @info: pointer to decoded results of NLM_SM_NOTIFY call
 *
 * We were notified that the specified host has rebooted.  Release
 * all resources held by that peer.
 */
void nlm_host_rebooted(const struct nlm_reboot *info)
{
	struct hlist_head *chain;
	struct hlist_node *pos;
	struct nsm_handle *nsm;
	struct nlm_host	*host;

	nsm = nsm_reboot_lookup(info);
	if (unlikely(nsm == NULL))
		return;

	/* Mark all hosts tied to this NSM state as having rebooted.
	 * We run the loop repeatedly, because we drop the host table
	 * lock for this.
	 * To avoid processing a host several times, we match the nsmstate.
	 */
again:	mutex_lock(&nlm_host_mutex);
	for (chain = nlm_hosts; chain < nlm_hosts + NLM_HOST_NRHASH; ++chain) {
		hlist_for_each_entry(host, pos, chain, h_hash) {
			if (host->h_nsmhandle == nsm
			 && host->h_nsmstate != info->state) {
				host->h_nsmstate = info->state;
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
	struct hlist_head *chain;
	struct hlist_node *pos;
	struct nlm_host	*host;

	dprintk("lockd: shutting down host module\n");
	mutex_lock(&nlm_host_mutex);

	/* First, make all hosts eligible for gc */
	dprintk("lockd: nuking all hosts...\n");
	for (chain = nlm_hosts; chain < nlm_hosts + NLM_HOST_NRHASH; ++chain) {
		hlist_for_each_entry(host, pos, chain, h_hash) {
			host->h_expires = jiffies - 1;
			if (host->h_rpcclnt) {
				rpc_shutdown_client(host->h_rpcclnt);
				host->h_rpcclnt = NULL;
			}
		}
	}

	/* Then, perform a garbage collection pass */
	nlm_gc_hosts();
	mutex_unlock(&nlm_host_mutex);

	/* complain if any hosts are left */
	if (nrhosts) {
		printk(KERN_WARNING "lockd: couldn't shutdown host module!\n");
		dprintk("lockd: %d hosts left:\n", nrhosts);
		for (chain = nlm_hosts; chain < nlm_hosts + NLM_HOST_NRHASH; ++chain) {
			hlist_for_each_entry(host, pos, chain, h_hash) {
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
	struct hlist_head *chain;
	struct hlist_node *pos, *next;
	struct nlm_host	*host;

	dprintk("lockd: host garbage collection\n");
	for (chain = nlm_hosts; chain < nlm_hosts + NLM_HOST_NRHASH; ++chain) {
		hlist_for_each_entry(host, pos, chain, h_hash)
			host->h_inuse = 0;
	}

	/* Mark all hosts that hold locks, blocks or shares */
	nlmsvc_mark_resources();

	for (chain = nlm_hosts; chain < nlm_hosts + NLM_HOST_NRHASH; ++chain) {
		hlist_for_each_entry_safe(host, pos, next, chain, h_hash) {
			if (atomic_read(&host->h_count) || host->h_inuse
			 || time_before(jiffies, host->h_expires)) {
				dprintk("nlm_gc_hosts skipping %s (cnt %d use %d exp %ld)\n",
					host->h_name, atomic_read(&host->h_count),
					host->h_inuse, host->h_expires);
				continue;
			}
			dprintk("lockd: delete host %s\n", host->h_name);
			hlist_del_init(&host->h_hash);

			nlm_destroy_host(host);
			nrhosts--;
		}
	}

	next_gc = jiffies + NLM_HOST_COLLECT;
}
