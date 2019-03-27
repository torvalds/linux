/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Andre Oppermann, Internet Business Solutions AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The tcp_hostcache moves the tcp-specific cached metrics from the routing
 * table to a dedicated structure indexed by the remote IP address.  It keeps
 * information on the measured TCP parameters of past TCP sessions to allow
 * better initial start values to be used with later connections to/from the
 * same source.  Depending on the network parameters (delay, max MTU,
 * congestion window) between local and remote sites, this can lead to
 * significant speed-ups for new TCP connections after the first one.
 *
 * Due to the tcp_hostcache, all TCP-specific metrics information in the
 * routing table have been removed.  The inpcb no longer keeps a pointer to
 * the routing entry, and protocol-initiated route cloning has been removed
 * as well.  With these changes, the routing table has gone back to being
 * more lightwight and only carries information related to packet forwarding.
 *
 * tcp_hostcache is designed for multiple concurrent access in SMP
 * environments and high contention.  All bucket rows have their own lock and
 * thus multiple lookups and modifies can be done at the same time as long as
 * they are in different bucket rows.  If a request for insertion of a new
 * record can't be satisfied, it simply returns an empty structure.  Nobody
 * and nothing outside of tcp_hostcache.c will ever point directly to any
 * entry in the tcp_hostcache.  All communication is done in an
 * object-oriented way and only functions of tcp_hostcache will manipulate
 * hostcache entries.  Otherwise, we are unable to achieve good behaviour in
 * concurrent access situations.  Since tcp_hostcache is only caching
 * information, there are no fatal consequences if we either can't satisfy
 * any particular request or have to drop/overwrite an existing entry because
 * of bucket limit memory constrains.
 */

/*
 * Many thanks to jlemon for basic structure of tcp_syncache which is being
 * followed here.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_hostcache.h>
#ifdef INET6
#include <netinet6/tcp6_var.h>
#endif

#include <vm/uma.h>

/* Arbitrary values */
#define TCP_HOSTCACHE_HASHSIZE		512
#define TCP_HOSTCACHE_BUCKETLIMIT	30
#define TCP_HOSTCACHE_EXPIRE		60*60	/* one hour */
#define TCP_HOSTCACHE_PRUNE		5*60	/* every 5 minutes */

VNET_DEFINE_STATIC(struct tcp_hostcache, tcp_hostcache);
#define	V_tcp_hostcache		VNET(tcp_hostcache)

VNET_DEFINE_STATIC(struct callout, tcp_hc_callout);
#define	V_tcp_hc_callout	VNET(tcp_hc_callout)

static struct hc_metrics *tcp_hc_lookup(struct in_conninfo *);
static struct hc_metrics *tcp_hc_insert(struct in_conninfo *);
static int sysctl_tcp_hc_list(SYSCTL_HANDLER_ARGS);
static int sysctl_tcp_hc_purgenow(SYSCTL_HANDLER_ARGS);
static void tcp_hc_purge_internal(int);
static void tcp_hc_purge(void *);

static SYSCTL_NODE(_net_inet_tcp, OID_AUTO, hostcache, CTLFLAG_RW, 0,
    "TCP Host cache");

VNET_DEFINE(int, tcp_use_hostcache) = 1;
#define V_tcp_use_hostcache  VNET(tcp_use_hostcache)
SYSCTL_INT(_net_inet_tcp_hostcache, OID_AUTO, enable, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_use_hostcache), 0,
    "Enable the TCP hostcache");

SYSCTL_UINT(_net_inet_tcp_hostcache, OID_AUTO, cachelimit, CTLFLAG_VNET | CTLFLAG_RDTUN,
    &VNET_NAME(tcp_hostcache.cache_limit), 0,
    "Overall entry limit for hostcache");

SYSCTL_UINT(_net_inet_tcp_hostcache, OID_AUTO, hashsize, CTLFLAG_VNET | CTLFLAG_RDTUN,
    &VNET_NAME(tcp_hostcache.hashsize), 0,
    "Size of TCP hostcache hashtable");

SYSCTL_UINT(_net_inet_tcp_hostcache, OID_AUTO, bucketlimit,
    CTLFLAG_VNET | CTLFLAG_RDTUN, &VNET_NAME(tcp_hostcache.bucket_limit), 0,
    "Per-bucket hash limit for hostcache");

SYSCTL_UINT(_net_inet_tcp_hostcache, OID_AUTO, count, CTLFLAG_VNET | CTLFLAG_RD,
     &VNET_NAME(tcp_hostcache.cache_count), 0,
    "Current number of entries in hostcache");

SYSCTL_INT(_net_inet_tcp_hostcache, OID_AUTO, expire, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_hostcache.expire), 0,
    "Expire time of TCP hostcache entries");

SYSCTL_INT(_net_inet_tcp_hostcache, OID_AUTO, prune, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_hostcache.prune), 0,
    "Time between purge runs");

SYSCTL_INT(_net_inet_tcp_hostcache, OID_AUTO, purge, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_hostcache.purgeall), 0,
    "Expire all entires on next purge run");

SYSCTL_PROC(_net_inet_tcp_hostcache, OID_AUTO, list,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_SKIP, 0, 0,
    sysctl_tcp_hc_list, "A", "List of all hostcache entries");

SYSCTL_PROC(_net_inet_tcp_hostcache, OID_AUTO, purgenow,
    CTLTYPE_INT | CTLFLAG_RW, NULL, 0,
    sysctl_tcp_hc_purgenow, "I", "Immediately purge all entries");

static MALLOC_DEFINE(M_HOSTCACHE, "hostcache", "TCP hostcache");

#define HOSTCACHE_HASH(ip) \
	(((ip)->s_addr ^ ((ip)->s_addr >> 7) ^ ((ip)->s_addr >> 17)) &	\
	  V_tcp_hostcache.hashmask)

/* XXX: What is the recommended hash to get good entropy for IPv6 addresses? */
#define HOSTCACHE_HASH6(ip6)				\
	(((ip6)->s6_addr32[0] ^				\
	  (ip6)->s6_addr32[1] ^				\
	  (ip6)->s6_addr32[2] ^				\
	  (ip6)->s6_addr32[3]) &			\
	 V_tcp_hostcache.hashmask)

#define THC_LOCK(lp)		mtx_lock(lp)
#define THC_UNLOCK(lp)		mtx_unlock(lp)

void
tcp_hc_init(void)
{
	u_int cache_limit;
	int i;

	/*
	 * Initialize hostcache structures.
	 */
	V_tcp_hostcache.cache_count = 0;
	V_tcp_hostcache.hashsize = TCP_HOSTCACHE_HASHSIZE;
	V_tcp_hostcache.bucket_limit = TCP_HOSTCACHE_BUCKETLIMIT;
	V_tcp_hostcache.expire = TCP_HOSTCACHE_EXPIRE;
	V_tcp_hostcache.prune = TCP_HOSTCACHE_PRUNE;

	TUNABLE_INT_FETCH("net.inet.tcp.hostcache.hashsize",
	    &V_tcp_hostcache.hashsize);
	if (!powerof2(V_tcp_hostcache.hashsize)) {
		printf("WARNING: hostcache hash size is not a power of 2.\n");
		V_tcp_hostcache.hashsize = TCP_HOSTCACHE_HASHSIZE; /* default */
	}
	V_tcp_hostcache.hashmask = V_tcp_hostcache.hashsize - 1;

	TUNABLE_INT_FETCH("net.inet.tcp.hostcache.bucketlimit",
	    &V_tcp_hostcache.bucket_limit);

	cache_limit = V_tcp_hostcache.hashsize * V_tcp_hostcache.bucket_limit;
	V_tcp_hostcache.cache_limit = cache_limit;
	TUNABLE_INT_FETCH("net.inet.tcp.hostcache.cachelimit",
	    &V_tcp_hostcache.cache_limit);
	if (V_tcp_hostcache.cache_limit > cache_limit)
		V_tcp_hostcache.cache_limit = cache_limit;

	/*
	 * Allocate the hash table.
	 */
	V_tcp_hostcache.hashbase = (struct hc_head *)
	    malloc(V_tcp_hostcache.hashsize * sizeof(struct hc_head),
		   M_HOSTCACHE, M_WAITOK | M_ZERO);

	/*
	 * Initialize the hash buckets.
	 */
	for (i = 0; i < V_tcp_hostcache.hashsize; i++) {
		TAILQ_INIT(&V_tcp_hostcache.hashbase[i].hch_bucket);
		V_tcp_hostcache.hashbase[i].hch_length = 0;
		mtx_init(&V_tcp_hostcache.hashbase[i].hch_mtx, "tcp_hc_entry",
			  NULL, MTX_DEF);
	}

	/*
	 * Allocate the hostcache entries.
	 */
	V_tcp_hostcache.zone =
	    uma_zcreate("hostcache", sizeof(struct hc_metrics),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	uma_zone_set_max(V_tcp_hostcache.zone, V_tcp_hostcache.cache_limit);

	/*
	 * Set up periodic cache cleanup.
	 */
	callout_init(&V_tcp_hc_callout, 1);
	callout_reset(&V_tcp_hc_callout, V_tcp_hostcache.prune * hz,
	    tcp_hc_purge, curvnet);
}

#ifdef VIMAGE
void
tcp_hc_destroy(void)
{
	int i;

	callout_drain(&V_tcp_hc_callout);

	/* Purge all hc entries. */
	tcp_hc_purge_internal(1);

	/* Free the uma zone and the allocated hash table. */
	uma_zdestroy(V_tcp_hostcache.zone);

	for (i = 0; i < V_tcp_hostcache.hashsize; i++)
		mtx_destroy(&V_tcp_hostcache.hashbase[i].hch_mtx);
	free(V_tcp_hostcache.hashbase, M_HOSTCACHE);
}
#endif

/*
 * Internal function: look up an entry in the hostcache or return NULL.
 *
 * If an entry has been returned, the caller becomes responsible for
 * unlocking the bucket row after he is done reading/modifying the entry.
 */
static struct hc_metrics *
tcp_hc_lookup(struct in_conninfo *inc)
{
	int hash;
	struct hc_head *hc_head;
	struct hc_metrics *hc_entry;

	if (!V_tcp_use_hostcache)
		return NULL;

	KASSERT(inc != NULL, ("tcp_hc_lookup with NULL in_conninfo pointer"));

	/*
	 * Hash the foreign ip address.
	 */
	if (inc->inc_flags & INC_ISIPV6)
		hash = HOSTCACHE_HASH6(&inc->inc6_faddr);
	else
		hash = HOSTCACHE_HASH(&inc->inc_faddr);

	hc_head = &V_tcp_hostcache.hashbase[hash];

	/*
	 * Acquire lock for this bucket row; we release the lock if we don't
	 * find an entry, otherwise the caller has to unlock after he is
	 * done.
	 */
	THC_LOCK(&hc_head->hch_mtx);

	/*
	 * Iterate through entries in bucket row looking for a match.
	 */
	TAILQ_FOREACH(hc_entry, &hc_head->hch_bucket, rmx_q) {
		if (inc->inc_flags & INC_ISIPV6) {
			/* XXX: check ip6_zoneid */
			if (memcmp(&inc->inc6_faddr, &hc_entry->ip6,
			    sizeof(inc->inc6_faddr)) == 0)
				return hc_entry;
		} else {
			if (memcmp(&inc->inc_faddr, &hc_entry->ip4,
			    sizeof(inc->inc_faddr)) == 0)
				return hc_entry;
		}
	}

	/*
	 * We were unsuccessful and didn't find anything.
	 */
	THC_UNLOCK(&hc_head->hch_mtx);
	return NULL;
}

/*
 * Internal function: insert an entry into the hostcache or return NULL if
 * unable to allocate a new one.
 *
 * If an entry has been returned, the caller becomes responsible for
 * unlocking the bucket row after he is done reading/modifying the entry.
 */
static struct hc_metrics *
tcp_hc_insert(struct in_conninfo *inc)
{
	int hash;
	struct hc_head *hc_head;
	struct hc_metrics *hc_entry;

	if (!V_tcp_use_hostcache)
		return NULL;

	KASSERT(inc != NULL, ("tcp_hc_insert with NULL in_conninfo pointer"));

	/*
	 * Hash the foreign ip address.
	 */
	if (inc->inc_flags & INC_ISIPV6)
		hash = HOSTCACHE_HASH6(&inc->inc6_faddr);
	else
		hash = HOSTCACHE_HASH(&inc->inc_faddr);

	hc_head = &V_tcp_hostcache.hashbase[hash];

	/*
	 * Acquire lock for this bucket row; we release the lock if we don't
	 * find an entry, otherwise the caller has to unlock after he is
	 * done.
	 */
	THC_LOCK(&hc_head->hch_mtx);

	/*
	 * If the bucket limit is reached, reuse the least-used element.
	 */
	if (hc_head->hch_length >= V_tcp_hostcache.bucket_limit ||
	    V_tcp_hostcache.cache_count >= V_tcp_hostcache.cache_limit) {
		hc_entry = TAILQ_LAST(&hc_head->hch_bucket, hc_qhead);
		/*
		 * At first we were dropping the last element, just to
		 * reacquire it in the next two lines again, which isn't very
		 * efficient.  Instead just reuse the least used element.
		 * We may drop something that is still "in-use" but we can be
		 * "lossy".
		 * Just give up if this bucket row is empty and we don't have
		 * anything to replace.
		 */
		if (hc_entry == NULL) {
			THC_UNLOCK(&hc_head->hch_mtx);
			return NULL;
		}
		TAILQ_REMOVE(&hc_head->hch_bucket, hc_entry, rmx_q);
		V_tcp_hostcache.hashbase[hash].hch_length--;
		V_tcp_hostcache.cache_count--;
		TCPSTAT_INC(tcps_hc_bucketoverflow);
#if 0
		uma_zfree(V_tcp_hostcache.zone, hc_entry);
#endif
	} else {
		/*
		 * Allocate a new entry, or balk if not possible.
		 */
		hc_entry = uma_zalloc(V_tcp_hostcache.zone, M_NOWAIT);
		if (hc_entry == NULL) {
			THC_UNLOCK(&hc_head->hch_mtx);
			return NULL;
		}
	}

	/*
	 * Initialize basic information of hostcache entry.
	 */
	bzero(hc_entry, sizeof(*hc_entry));
	if (inc->inc_flags & INC_ISIPV6) {
		hc_entry->ip6 = inc->inc6_faddr;
		hc_entry->ip6_zoneid = inc->inc6_zoneid;
	} else
		hc_entry->ip4 = inc->inc_faddr;
	hc_entry->rmx_head = hc_head;
	hc_entry->rmx_expire = V_tcp_hostcache.expire;

	/*
	 * Put it upfront.
	 */
	TAILQ_INSERT_HEAD(&hc_head->hch_bucket, hc_entry, rmx_q);
	V_tcp_hostcache.hashbase[hash].hch_length++;
	V_tcp_hostcache.cache_count++;
	TCPSTAT_INC(tcps_hc_added);

	return hc_entry;
}

/*
 * External function: look up an entry in the hostcache and fill out the
 * supplied TCP metrics structure.  Fills in NULL when no entry was found or
 * a value is not set.
 */
void
tcp_hc_get(struct in_conninfo *inc, struct hc_metrics_lite *hc_metrics_lite)
{
	struct hc_metrics *hc_entry;

	if (!V_tcp_use_hostcache)
		return;

	/*
	 * Find the right bucket.
	 */
	hc_entry = tcp_hc_lookup(inc);

	/*
	 * If we don't have an existing object.
	 */
	if (hc_entry == NULL) {
		bzero(hc_metrics_lite, sizeof(*hc_metrics_lite));
		return;
	}
	hc_entry->rmx_hits++;
	hc_entry->rmx_expire = V_tcp_hostcache.expire; /* start over again */

	hc_metrics_lite->rmx_mtu = hc_entry->rmx_mtu;
	hc_metrics_lite->rmx_ssthresh = hc_entry->rmx_ssthresh;
	hc_metrics_lite->rmx_rtt = hc_entry->rmx_rtt;
	hc_metrics_lite->rmx_rttvar = hc_entry->rmx_rttvar;
	hc_metrics_lite->rmx_cwnd = hc_entry->rmx_cwnd;
	hc_metrics_lite->rmx_sendpipe = hc_entry->rmx_sendpipe;
	hc_metrics_lite->rmx_recvpipe = hc_entry->rmx_recvpipe;

	/*
	 * Unlock bucket row.
	 */
	THC_UNLOCK(&hc_entry->rmx_head->hch_mtx);
}

/*
 * External function: look up an entry in the hostcache and return the
 * discovered path MTU.  Returns 0 if no entry is found or value is not
 * set.
 */
uint32_t
tcp_hc_getmtu(struct in_conninfo *inc)
{
	struct hc_metrics *hc_entry;
	uint32_t mtu;

	if (!V_tcp_use_hostcache)
		return 0;

	hc_entry = tcp_hc_lookup(inc);
	if (hc_entry == NULL) {
		return 0;
	}
	hc_entry->rmx_hits++;
	hc_entry->rmx_expire = V_tcp_hostcache.expire; /* start over again */

	mtu = hc_entry->rmx_mtu;
	THC_UNLOCK(&hc_entry->rmx_head->hch_mtx);
	return mtu;
}

/*
 * External function: update the MTU value of an entry in the hostcache.
 * Creates a new entry if none was found.
 */
void
tcp_hc_updatemtu(struct in_conninfo *inc, uint32_t mtu)
{
	struct hc_metrics *hc_entry;

	if (!V_tcp_use_hostcache)
		return;

	/*
	 * Find the right bucket.
	 */
	hc_entry = tcp_hc_lookup(inc);

	/*
	 * If we don't have an existing object, try to insert a new one.
	 */
	if (hc_entry == NULL) {
		hc_entry = tcp_hc_insert(inc);
		if (hc_entry == NULL)
			return;
	}
	hc_entry->rmx_updates++;
	hc_entry->rmx_expire = V_tcp_hostcache.expire; /* start over again */

	hc_entry->rmx_mtu = mtu;

	/*
	 * Put it upfront so we find it faster next time.
	 */
	TAILQ_REMOVE(&hc_entry->rmx_head->hch_bucket, hc_entry, rmx_q);
	TAILQ_INSERT_HEAD(&hc_entry->rmx_head->hch_bucket, hc_entry, rmx_q);

	/*
	 * Unlock bucket row.
	 */
	THC_UNLOCK(&hc_entry->rmx_head->hch_mtx);
}

/*
 * External function: update the TCP metrics of an entry in the hostcache.
 * Creates a new entry if none was found.
 */
void
tcp_hc_update(struct in_conninfo *inc, struct hc_metrics_lite *hcml)
{
	struct hc_metrics *hc_entry;

	if (!V_tcp_use_hostcache)
		return;

	hc_entry = tcp_hc_lookup(inc);
	if (hc_entry == NULL) {
		hc_entry = tcp_hc_insert(inc);
		if (hc_entry == NULL)
			return;
	}
	hc_entry->rmx_updates++;
	hc_entry->rmx_expire = V_tcp_hostcache.expire; /* start over again */

	if (hcml->rmx_rtt != 0) {
		if (hc_entry->rmx_rtt == 0)
			hc_entry->rmx_rtt = hcml->rmx_rtt;
		else
			hc_entry->rmx_rtt = ((uint64_t)hc_entry->rmx_rtt +
			    (uint64_t)hcml->rmx_rtt) / 2;
		TCPSTAT_INC(tcps_cachedrtt);
	}
	if (hcml->rmx_rttvar != 0) {
	        if (hc_entry->rmx_rttvar == 0)
			hc_entry->rmx_rttvar = hcml->rmx_rttvar;
		else
			hc_entry->rmx_rttvar = ((uint64_t)hc_entry->rmx_rttvar +
			    (uint64_t)hcml->rmx_rttvar) / 2;
		TCPSTAT_INC(tcps_cachedrttvar);
	}
	if (hcml->rmx_ssthresh != 0) {
		if (hc_entry->rmx_ssthresh == 0)
			hc_entry->rmx_ssthresh = hcml->rmx_ssthresh;
		else
			hc_entry->rmx_ssthresh =
			    (hc_entry->rmx_ssthresh + hcml->rmx_ssthresh) / 2;
		TCPSTAT_INC(tcps_cachedssthresh);
	}
	if (hcml->rmx_cwnd != 0) {
		if (hc_entry->rmx_cwnd == 0)
			hc_entry->rmx_cwnd = hcml->rmx_cwnd;
		else
			hc_entry->rmx_cwnd = ((uint64_t)hc_entry->rmx_cwnd +
			    (uint64_t)hcml->rmx_cwnd) / 2;
		/* TCPSTAT_INC(tcps_cachedcwnd); */
	}
	if (hcml->rmx_sendpipe != 0) {
		if (hc_entry->rmx_sendpipe == 0)
			hc_entry->rmx_sendpipe = hcml->rmx_sendpipe;
		else
			hc_entry->rmx_sendpipe =
			    ((uint64_t)hc_entry->rmx_sendpipe +
			    (uint64_t)hcml->rmx_sendpipe) /2;
		/* TCPSTAT_INC(tcps_cachedsendpipe); */
	}
	if (hcml->rmx_recvpipe != 0) {
		if (hc_entry->rmx_recvpipe == 0)
			hc_entry->rmx_recvpipe = hcml->rmx_recvpipe;
		else
			hc_entry->rmx_recvpipe =
			    ((uint64_t)hc_entry->rmx_recvpipe +
			    (uint64_t)hcml->rmx_recvpipe) /2;
		/* TCPSTAT_INC(tcps_cachedrecvpipe); */
	}

	TAILQ_REMOVE(&hc_entry->rmx_head->hch_bucket, hc_entry, rmx_q);
	TAILQ_INSERT_HEAD(&hc_entry->rmx_head->hch_bucket, hc_entry, rmx_q);
	THC_UNLOCK(&hc_entry->rmx_head->hch_mtx);
}

/*
 * Sysctl function: prints the list and values of all hostcache entries in
 * unsorted order.
 */
static int
sysctl_tcp_hc_list(SYSCTL_HANDLER_ARGS)
{
	const int linesize = 128;
	struct sbuf sb;
	int i, error;
	struct hc_metrics *hc_entry;
	char ip4buf[INET_ADDRSTRLEN];
#ifdef INET6
	char ip6buf[INET6_ADDRSTRLEN];
#endif

	if (jailed_without_vnet(curthread->td_ucred) != 0)
		return (EPERM);

	sbuf_new(&sb, NULL, linesize * (V_tcp_hostcache.cache_count + 1),
		SBUF_INCLUDENUL);

	sbuf_printf(&sb,
	        "\nIP address        MTU  SSTRESH      RTT   RTTVAR "
		"    CWND SENDPIPE RECVPIPE HITS  UPD  EXP\n");

#define msec(u) (((u) + 500) / 1000)
	for (i = 0; i < V_tcp_hostcache.hashsize; i++) {
		THC_LOCK(&V_tcp_hostcache.hashbase[i].hch_mtx);
		TAILQ_FOREACH(hc_entry, &V_tcp_hostcache.hashbase[i].hch_bucket,
			      rmx_q) {
			sbuf_printf(&sb,
			    "%-15s %5u %8u %6lums %6lums %8u %8u %8u %4lu "
			    "%4lu %4i\n",
			    hc_entry->ip4.s_addr ?
			        inet_ntoa_r(hc_entry->ip4, ip4buf) :
#ifdef INET6
				ip6_sprintf(ip6buf, &hc_entry->ip6),
#else
				"IPv6?",
#endif
			    hc_entry->rmx_mtu,
			    hc_entry->rmx_ssthresh,
			    msec((u_long)hc_entry->rmx_rtt *
				(RTM_RTTUNIT / (hz * TCP_RTT_SCALE))),
			    msec((u_long)hc_entry->rmx_rttvar *
				(RTM_RTTUNIT / (hz * TCP_RTTVAR_SCALE))),
			    hc_entry->rmx_cwnd,
			    hc_entry->rmx_sendpipe,
			    hc_entry->rmx_recvpipe,
			    hc_entry->rmx_hits,
			    hc_entry->rmx_updates,
			    hc_entry->rmx_expire);
		}
		THC_UNLOCK(&V_tcp_hostcache.hashbase[i].hch_mtx);
	}
#undef msec
	error = sbuf_finish(&sb);
	if (error == 0)
		error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return(error);
}

/*
 * Caller has to make sure the curvnet is set properly.
 */
static void
tcp_hc_purge_internal(int all)
{
	struct hc_metrics *hc_entry, *hc_next;
	int i;

	for (i = 0; i < V_tcp_hostcache.hashsize; i++) {
		THC_LOCK(&V_tcp_hostcache.hashbase[i].hch_mtx);
		TAILQ_FOREACH_SAFE(hc_entry,
		    &V_tcp_hostcache.hashbase[i].hch_bucket, rmx_q, hc_next) {
			if (all || hc_entry->rmx_expire <= 0) {
				TAILQ_REMOVE(&V_tcp_hostcache.hashbase[i].hch_bucket,
					      hc_entry, rmx_q);
				uma_zfree(V_tcp_hostcache.zone, hc_entry);
				V_tcp_hostcache.hashbase[i].hch_length--;
				V_tcp_hostcache.cache_count--;
			} else
				hc_entry->rmx_expire -= V_tcp_hostcache.prune;
		}
		THC_UNLOCK(&V_tcp_hostcache.hashbase[i].hch_mtx);
	}
}

/*
 * Expire and purge (old|all) entries in the tcp_hostcache.  Runs
 * periodically from the callout.
 */
static void
tcp_hc_purge(void *arg)
{
	CURVNET_SET((struct vnet *) arg);
	int all = 0;

	if (V_tcp_hostcache.purgeall) {
		all = 1;
		V_tcp_hostcache.purgeall = 0;
	}

	tcp_hc_purge_internal(all);

	callout_reset(&V_tcp_hc_callout, V_tcp_hostcache.prune * hz,
	    tcp_hc_purge, arg);
	CURVNET_RESTORE();
}

/*
 * Expire and purge all entries in hostcache immediately.
 */
static int
sysctl_tcp_hc_purgenow(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return (error);

	tcp_hc_purge_internal(1);

	callout_reset(&V_tcp_hc_callout, V_tcp_hostcache.prune * hz,
	    tcp_hc_purge, curvnet);

	return (0);
}
