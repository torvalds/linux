// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/mutex.h>

#include <linux/sunrpc/svc_xprt.h>

#include <net/ipv6.h>

#include "netns.h"

#define NLMDBG_FACILITY		NLMDBG_HOSTCACHE
#define NLM_HOST_NRHASH		32
#define NLM_HOST_REBIND		(60 * HZ)
#define NLM_HOST_EXPIRE		(300 * HZ)
#define NLM_HOST_COLLECT	(120 * HZ)

static struct hlist_head	nlm_server_hosts[NLM_HOST_NRHASH];
static struct hlist_head	nlm_client_hosts[NLM_HOST_NRHASH];

#define for_each_host(host, chain, table) \
	for ((chain) = (table); \
	     (chain) < (table) + NLM_HOST_NRHASH; ++(chain)) \
		hlist_for_each_entry((host), (chain), h_hash)

#define for_each_host_safe(host, next, chain, table) \
	for ((chain) = (table); \
	     (chain) < (table) + NLM_HOST_NRHASH; ++(chain)) \
		hlist_for_each_entry_safe((host), (next), \
						(chain), h_hash)

static unsigned long		nrhosts;
static DEFINE_MUTEX(nlm_host_mutex);

static void			nlm_gc_hosts(struct net *net);

struct nlm_lookup_host_info {
	const int		server;		/* search for server|client */
	const struct sockaddr	*sap;		/* address to search for */
	const size_t		salen;		/* it's length */
	const unsigned short	protocol;	/* transport to search for*/
	const u32		version;	/* NLM version to search for */
	const char		*hostname;	/* remote's hostname */
	const size_t		hostname_len;	/* it's length */
	const int		noresvport;	/* use non-priv port */
	struct net		*net;		/* network namespace to bind */
	const struct cred	*cred;
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

/*
 * Allocate and initialize an nlm_host.  Common to both client and server.
 */
static struct nlm_host *nlm_alloc_host(struct nlm_lookup_host_info *ni,
				       struct nsm_handle *nsm)
{
	struct nlm_host *host = NULL;
	unsigned long now = jiffies;

	if (nsm != NULL)
		refcount_inc(&nsm->sm_count);
	else {
		host = NULL;
		nsm = nsm_get_handle(ni->net, ni->sap, ni->salen,
					ni->hostname, ni->hostname_len);
		if (unlikely(nsm == NULL)) {
			dprintk("lockd: %s failed; no nsm handle\n",
				__func__);
			goto out;
		}
	}

	host = kmalloc(sizeof(*host), GFP_KERNEL);
	if (unlikely(host == NULL)) {
		dprintk("lockd: %s failed; no memory\n", __func__);
		nsm_release(nsm);
		goto out;
	}

	memcpy(nlm_addr(host), ni->sap, ni->salen);
	host->h_addrlen    = ni->salen;
	rpc_set_port(nlm_addr(host), 0);
	host->h_srcaddrlen = 0;

	host->h_rpcclnt    = NULL;
	host->h_name	   = nsm->sm_name;
	host->h_version    = ni->version;
	host->h_proto      = ni->protocol;
	host->h_reclaiming = 0;
	host->h_server     = ni->server;
	host->h_noresvport = ni->noresvport;
	host->h_inuse      = 0;
	init_waitqueue_head(&host->h_gracewait);
	init_rwsem(&host->h_rwsem);
	host->h_state      = 0;
	host->h_nsmstate   = 0;
	host->h_pidcount   = 0;
	refcount_set(&host->h_count, 1);
	mutex_init(&host->h_mutex);
	host->h_nextrebind = now + NLM_HOST_REBIND;
	host->h_expires    = now + NLM_HOST_EXPIRE;
	INIT_LIST_HEAD(&host->h_lockowners);
	spin_lock_init(&host->h_lock);
	INIT_LIST_HEAD(&host->h_granted);
	INIT_LIST_HEAD(&host->h_reclaim);
	host->h_nsmhandle  = nsm;
	host->h_addrbuf    = nsm->sm_addrbuf;
	host->net	   = ni->net;
	host->h_cred	   = get_cred(ni->cred);
	strlcpy(host->nodename, utsname()->nodename, sizeof(host->nodename));

out:
	return host;
}

/*
 * Destroy an nlm_host and free associated resources
 *
 * Caller must hold nlm_host_mutex.
 */
static void nlm_destroy_host_locked(struct nlm_host *host)
{
	struct rpc_clnt	*clnt;
	struct lockd_net *ln = net_generic(host->net, lockd_net_id);

	dprintk("lockd: destroy host %s\n", host->h_name);

	hlist_del_init(&host->h_hash);

	nsm_unmonitor(host);
	nsm_release(host->h_nsmhandle);

	clnt = host->h_rpcclnt;
	if (clnt != NULL)
		rpc_shutdown_client(clnt);
	put_cred(host->h_cred);
	kfree(host);

	ln->nrhosts--;
	nrhosts--;
}

/**
 * nlmclnt_lookup_host - Find an NLM host handle matching a remote server
 * @sap: network address of server
 * @salen: length of server address
 * @protocol: transport protocol to use
 * @version: NLM protocol version
 * @hostname: '\0'-terminated hostname of server
 * @noresvport: 1 if non-privileged port should be used
 * @net: pointer to net namespace
 * @cred: pointer to cred
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
				     int noresvport,
				     struct net *net,
				     const struct cred *cred)
{
	struct nlm_lookup_host_info ni = {
		.server		= 0,
		.sap		= sap,
		.salen		= salen,
		.protocol	= protocol,
		.version	= version,
		.hostname	= hostname,
		.hostname_len	= strlen(hostname),
		.noresvport	= noresvport,
		.net		= net,
		.cred		= cred,
	};
	struct hlist_head *chain;
	struct nlm_host	*host;
	struct nsm_handle *nsm = NULL;
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	dprintk("lockd: %s(host='%s', vers=%u, proto=%s)\n", __func__,
			(hostname ? hostname : "<none>"), version,
			(protocol == IPPROTO_UDP ? "udp" : "tcp"));

	mutex_lock(&nlm_host_mutex);

	chain = &nlm_client_hosts[nlm_hash_address(sap)];
	hlist_for_each_entry(host, chain, h_hash) {
		if (host->net != net)
			continue;
		if (!rpc_cmp_addr(nlm_addr(host), sap))
			continue;

		/* Same address. Share an NSM handle if we already have one */
		if (nsm == NULL)
			nsm = host->h_nsmhandle;

		if (host->h_proto != protocol)
			continue;
		if (host->h_version != version)
			continue;

		nlm_get_host(host);
		dprintk("lockd: %s found host %s (%s)\n", __func__,
			host->h_name, host->h_addrbuf);
		goto out;
	}

	host = nlm_alloc_host(&ni, nsm);
	if (unlikely(host == NULL))
		goto out;

	hlist_add_head(&host->h_hash, chain);
	ln->nrhosts++;
	nrhosts++;

	dprintk("lockd: %s created host %s (%s)\n", __func__,
		host->h_name, host->h_addrbuf);

out:
	mutex_unlock(&nlm_host_mutex);
	return host;
}

/**
 * nlmclnt_release_host - release client nlm_host
 * @host: nlm_host to release
 *
 */
void nlmclnt_release_host(struct nlm_host *host)
{
	if (host == NULL)
		return;

	dprintk("lockd: release client host %s\n", host->h_name);

	WARN_ON_ONCE(host->h_server);

	if (refcount_dec_and_mutex_lock(&host->h_count, &nlm_host_mutex)) {
		WARN_ON_ONCE(!list_empty(&host->h_lockowners));
		WARN_ON_ONCE(!list_empty(&host->h_granted));
		WARN_ON_ONCE(!list_empty(&host->h_reclaim));

		nlm_destroy_host_locked(host);
		mutex_unlock(&nlm_host_mutex);
	}
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
	struct hlist_head *chain;
	struct nlm_host	*host = NULL;
	struct nsm_handle *nsm = NULL;
	struct sockaddr *src_sap = svc_daddr(rqstp);
	size_t src_len = rqstp->rq_daddrlen;
	struct net *net = SVC_NET(rqstp);
	struct nlm_lookup_host_info ni = {
		.server		= 1,
		.sap		= svc_addr(rqstp),
		.salen		= rqstp->rq_addrlen,
		.protocol	= rqstp->rq_prot,
		.version	= rqstp->rq_vers,
		.hostname	= hostname,
		.hostname_len	= hostname_len,
		.net		= net,
	};
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	dprintk("lockd: %s(host='%.*s', vers=%u, proto=%s)\n", __func__,
			(int)hostname_len, hostname, rqstp->rq_vers,
			(rqstp->rq_prot == IPPROTO_UDP ? "udp" : "tcp"));

	mutex_lock(&nlm_host_mutex);

	if (time_after_eq(jiffies, ln->next_gc))
		nlm_gc_hosts(net);

	chain = &nlm_server_hosts[nlm_hash_address(ni.sap)];
	hlist_for_each_entry(host, chain, h_hash) {
		if (host->net != net)
			continue;
		if (!rpc_cmp_addr(nlm_addr(host), ni.sap))
			continue;

		/* Same address. Share an NSM handle if we already have one */
		if (nsm == NULL)
			nsm = host->h_nsmhandle;

		if (host->h_proto != ni.protocol)
			continue;
		if (host->h_version != ni.version)
			continue;
		if (!rpc_cmp_addr(nlm_srcaddr(host), src_sap))
			continue;

		/* Move to head of hash chain. */
		hlist_del(&host->h_hash);
		hlist_add_head(&host->h_hash, chain);

		nlm_get_host(host);
		dprintk("lockd: %s found host %s (%s)\n",
			__func__, host->h_name, host->h_addrbuf);
		goto out;
	}

	host = nlm_alloc_host(&ni, nsm);
	if (unlikely(host == NULL))
		goto out;

	memcpy(nlm_srcaddr(host), src_sap, src_len);
	host->h_srcaddrlen = src_len;
	hlist_add_head(&host->h_hash, chain);
	ln->nrhosts++;
	nrhosts++;

	refcount_inc(&host->h_count);

	dprintk("lockd: %s created host %s (%s)\n",
		__func__, host->h_name, host->h_addrbuf);

out:
	mutex_unlock(&nlm_host_mutex);
	return host;
}

/**
 * nlmsvc_release_host - release server nlm_host
 * @host: nlm_host to release
 *
 * Host is destroyed later in nlm_gc_host().
 */
void nlmsvc_release_host(struct nlm_host *host)
{
	if (host == NULL)
		return;

	dprintk("lockd: release server host %s\n", host->h_name);

	WARN_ON_ONCE(!host->h_server);
	refcount_dec(&host->h_count);
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
		nlm_rebind_host(host);
	} else {
		unsigned long increment = nlmsvc_timeout;
		struct rpc_timeout timeparms = {
			.to_initval	= increment,
			.to_increment	= increment,
			.to_maxval	= increment * 6UL,
			.to_retries	= 5U,
		};
		struct rpc_create_args args = {
			.net		= host->net,
			.protocol	= host->h_proto,
			.address	= nlm_addr(host),
			.addrsize	= host->h_addrlen,
			.timeout	= &timeparms,
			.servername	= host->h_name,
			.program	= &nlm_program,
			.version	= host->h_version,
			.authflavor	= RPC_AUTH_UNIX,
			.flags		= (RPC_CLNT_CREATE_NOPING |
					   RPC_CLNT_CREATE_AUTOBIND |
					   RPC_CLNT_CREATE_REUSEPORT),
			.cred		= host->h_cred,
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
		if (host->h_srcaddrlen)
			args.saddress = nlm_srcaddr(host);

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

/**
 * nlm_rebind_host - If needed, force a portmap lookup of the peer's lockd port
 * @host: NLM host handle for peer
 *
 * This is not needed when using a connection-oriented protocol, such as TCP.
 * The existing autobind mechanism is sufficient to force a rebind when
 * required, e.g. on connection state transitions.
 */
void
nlm_rebind_host(struct nlm_host *host)
{
	if (host->h_proto != IPPROTO_UDP)
		return;

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
		refcount_inc(&host->h_count);
		host->h_expires = jiffies + NLM_HOST_EXPIRE;
	}
	return host;
}

static struct nlm_host *next_host_state(struct hlist_head *cache,
					struct nsm_handle *nsm,
					const struct nlm_reboot *info)
{
	struct nlm_host *host;
	struct hlist_head *chain;

	mutex_lock(&nlm_host_mutex);
	for_each_host(host, chain, cache) {
		if (host->h_nsmhandle == nsm
		    && host->h_nsmstate != info->state) {
			host->h_nsmstate = info->state;
			host->h_state++;

			nlm_get_host(host);
			mutex_unlock(&nlm_host_mutex);
			return host;
		}
	}

	mutex_unlock(&nlm_host_mutex);
	return NULL;
}

/**
 * nlm_host_rebooted - Release all resources held by rebooted host
 * @net:  network namespace
 * @info: pointer to decoded results of NLM_SM_NOTIFY call
 *
 * We were notified that the specified host has rebooted.  Release
 * all resources held by that peer.
 */
void nlm_host_rebooted(const struct net *net, const struct nlm_reboot *info)
{
	struct nsm_handle *nsm;
	struct nlm_host	*host;

	nsm = nsm_reboot_lookup(net, info);
	if (unlikely(nsm == NULL))
		return;

	/* Mark all hosts tied to this NSM state as having rebooted.
	 * We run the loop repeatedly, because we drop the host table
	 * lock for this.
	 * To avoid processing a host several times, we match the nsmstate.
	 */
	while ((host = next_host_state(nlm_server_hosts, nsm, info)) != NULL) {
		nlmsvc_free_host_resources(host);
		nlmsvc_release_host(host);
	}
	while ((host = next_host_state(nlm_client_hosts, nsm, info)) != NULL) {
		nlmclnt_recovery(host);
		nlmclnt_release_host(host);
	}

	nsm_release(nsm);
}

static void nlm_complain_hosts(struct net *net)
{
	struct hlist_head *chain;
	struct nlm_host	*host;

	if (net) {
		struct lockd_net *ln = net_generic(net, lockd_net_id);

		if (ln->nrhosts == 0)
			return;
		pr_warn("lockd: couldn't shutdown host module for net %x!\n",
			net->ns.inum);
		dprintk("lockd: %lu hosts left in net %x:\n", ln->nrhosts,
			net->ns.inum);
	} else {
		if (nrhosts == 0)
			return;
		printk(KERN_WARNING "lockd: couldn't shutdown host module!\n");
		dprintk("lockd: %lu hosts left:\n", nrhosts);
	}

	for_each_host(host, chain, nlm_server_hosts) {
		if (net && host->net != net)
			continue;
		dprintk("       %s (cnt %d use %d exp %ld net %x)\n",
			host->h_name, refcount_read(&host->h_count),
			host->h_inuse, host->h_expires, host->net->ns.inum);
	}
}

void
nlm_shutdown_hosts_net(struct net *net)
{
	struct hlist_head *chain;
	struct nlm_host	*host;

	mutex_lock(&nlm_host_mutex);

	/* First, make all hosts eligible for gc */
	dprintk("lockd: nuking all hosts in net %x...\n",
		net ? net->ns.inum : 0);
	for_each_host(host, chain, nlm_server_hosts) {
		if (net && host->net != net)
			continue;
		host->h_expires = jiffies - 1;
		if (host->h_rpcclnt) {
			rpc_shutdown_client(host->h_rpcclnt);
			host->h_rpcclnt = NULL;
		}
	}

	/* Then, perform a garbage collection pass */
	nlm_gc_hosts(net);
	nlm_complain_hosts(net);
	mutex_unlock(&nlm_host_mutex);
}

/*
 * Shut down the hosts module.
 * Note that this routine is called only at server shutdown time.
 */
void
nlm_shutdown_hosts(void)
{
	dprintk("lockd: shutting down host module\n");
	nlm_shutdown_hosts_net(NULL);
}

/*
 * Garbage collect any unused NLM hosts.
 * This GC combines reference counting for async operations with
 * mark & sweep for resources held by remote clients.
 */
static void
nlm_gc_hosts(struct net *net)
{
	struct hlist_head *chain;
	struct hlist_node *next;
	struct nlm_host	*host;

	dprintk("lockd: host garbage collection for net %x\n",
		net ? net->ns.inum : 0);
	for_each_host(host, chain, nlm_server_hosts) {
		if (net && host->net != net)
			continue;
		host->h_inuse = 0;
	}

	/* Mark all hosts that hold locks, blocks or shares */
	nlmsvc_mark_resources(net);

	for_each_host_safe(host, next, chain, nlm_server_hosts) {
		if (net && host->net != net)
			continue;
		if (host->h_inuse || time_before(jiffies, host->h_expires)) {
			dprintk("nlm_gc_hosts skipping %s "
				"(cnt %d use %d exp %ld net %x)\n",
				host->h_name, refcount_read(&host->h_count),
				host->h_inuse, host->h_expires,
				host->net->ns.inum);
			continue;
		}
		if (refcount_dec_if_one(&host->h_count))
			nlm_destroy_host_locked(host);
	}

	if (net) {
		struct lockd_net *ln = net_generic(net, lockd_net_id);

		ln->next_gc = jiffies + NLM_HOST_COLLECT;
	}
}
