/*-
 * Copyright (c) 2015-2017 Patrick Kelsey
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
 * This is an implementation of TCP Fast Open (TFO) [RFC7413]. To include
 * this code, add the following line to your kernel config:
 *
 * options TCP_RFC7413
 *
 *
 * The generated TFO cookies are the 64-bit output of
 * SipHash24(key=<16-byte-key>, msg=<client-ip>).  Multiple concurrent valid
 * keys are supported so that time-based rolling cookie invalidation
 * policies can be implemented in the system.  The default number of
 * concurrent keys is 2.  This can be adjusted in the kernel config as
 * follows:
 *
 * options TCP_RFC7413_MAX_KEYS=<num-keys>
 *
 *
 * In addition to the facilities defined in RFC7413, this implementation
 * supports a pre-shared key (PSK) mode of operation in which the TFO server
 * requires the client to be in posession of a shared secret in order for
 * the client to be able to successfully open TFO connections with the
 * server.  This is useful, for example, in environments where TFO servers
 * are exposed to both internal and external clients and only wish to allow
 * TFO connections from internal clients.
 *
 * In the PSK mode of operation, the server generates and sends TFO cookies
 * to requesting clients as usual.  However, when validating cookies
 * received in TFO SYNs from clients, the server requires the
 * client-supplied cookie to equal SipHash24(key=<16-byte-psk>,
 * msg=<cookie-sent-to-client>).
 *
 * Multiple concurrent valid pre-shared keys are supported so that
 * time-based rolling PSK invalidation policies can be implemented in the
 * system.  The default number of concurrent pre-shared keys is 2.  This can
 * be adjusted in the kernel config as follows:
 *
 * options TCP_RFC7413_MAX_PSKS=<num-psks>
 *
 *
 * The following TFO-specific sysctls are defined:
 *
 * net.inet.tcp.fastopen.acceptany (RW, default 0)
 *     When non-zero, all client-supplied TFO cookies will be considered to
 *     be valid.
 *
 * net.inet.tcp.fastopen.autokey (RW, default 120)
 *     When this and net.inet.tcp.fastopen.server_enable are non-zero, a new
 *     key will be automatically generated after this many seconds.
 *
 * net.inet.tcp.fastopen.ccache_bucket_limit
 *                     (RWTUN, default TCP_FASTOPEN_CCACHE_BUCKET_LIMIT_DEFAULT)
 *     The maximum number of entries in a client cookie cache bucket.
 *
 * net.inet.tcp.fastopen.ccache_buckets
 *                          (RDTUN, default TCP_FASTOPEN_CCACHE_BUCKETS_DEFAULT)
 *     The number of client cookie cache buckets.
 *
 * net.inet.tcp.fastopen.ccache_list (RO)
 *     Print the client cookie cache.
 *
 * net.inet.tcp.fastopen.client_enable (RW, default 0)
 *     When zero, no new active (i.e., client) TFO connections can be
 *     created.  On the transition from enabled to disabled, the client
 *     cookie cache is cleared and disabled.  The transition from enabled to
 *     disabled does not affect any active TFO connections in progress; it
 *     only prevents new ones from being made.
 *
 * net.inet.tcp.fastopen.keylen (RD)
 *     The key length in bytes.
 *
 * net.inet.tcp.fastopen.maxkeys (RD)
 *     The maximum number of keys supported.
 *
 * net.inet.tcp.fastopen.maxpsks (RD)
 *     The maximum number of pre-shared keys supported.
 *
 * net.inet.tcp.fastopen.numkeys (RD)
 *     The current number of keys installed.
 *
 * net.inet.tcp.fastopen.numpsks (RD)
 *     The current number of pre-shared keys installed.
 *
 * net.inet.tcp.fastopen.path_disable_time
 *                          (RW, default TCP_FASTOPEN_PATH_DISABLE_TIME_DEFAULT)
 *     When a failure occurs while trying to create a new active (i.e.,
 *     client) TFO connection, new active connections on the same path, as
 *     determined by the tuple {client_ip, server_ip, server_port}, will be
 *     forced to be non-TFO for this many seconds.  Note that the path
 *     disable mechanism relies on state stored in client cookie cache
 *     entries, so it is possible for the disable time for a given path to
 *     be reduced if the corresponding client cookie cache entry is reused
 *     due to resource pressure before the disable period has elapsed.
 *
 * net.inet.tcp.fastopen.psk_enable (RW, default 0)
 *     When non-zero, pre-shared key (PSK) mode is enabled for all TFO
 *     servers.  On the transition from enabled to disabled, all installed
 *     pre-shared keys are removed.
 *
 * net.inet.tcp.fastopen.server_enable (RW, default 0)
 *     When zero, no new passive (i.e., server) TFO connections can be
 *     created.  On the transition from enabled to disabled, all installed
 *     keys and pre-shared keys are removed.  On the transition from
 *     disabled to enabled, if net.inet.tcp.fastopen.autokey is non-zero and
 *     there are no keys installed, a new key will be generated immediately.
 *     The transition from enabled to disabled does not affect any passive
 *     TFO connections in progress; it only prevents new ones from being
 *     made.
 *
 * net.inet.tcp.fastopen.setkey (WR)
 *     Install a new key by writing net.inet.tcp.fastopen.keylen bytes to
 *     this sysctl.
 *
 * net.inet.tcp.fastopen.setpsk (WR)
 *     Install a new pre-shared key by writing net.inet.tcp.fastopen.keylen
 *     bytes to this sysctl.
 *
 * In order for TFO connections to be created via a listen socket, that
 * socket must have the TCP_FASTOPEN socket option set on it.  This option
 * can be set on the socket either before or after the listen() is invoked.
 * Clearing this option on a listen socket after it has been set has no
 * effect on existing TFO connections or TFO connections in progress; it
 * only prevents new TFO connections from being made.
 *
 * For passively-created sockets, the TCP_FASTOPEN socket option can be
 * queried to determine whether the connection was established using TFO.
 * Note that connections that are established via a TFO SYN, but that fall
 * back to using a non-TFO SYN|ACK will have the TCP_FASTOPEN socket option
 * set.
 *
 * Per the RFC, this implementation limits the number of TFO connections
 * that can be in the SYN_RECEIVED state on a per listen-socket basis.
 * Whenever this limit is exceeded, requests for new TFO connections are
 * serviced as non-TFO requests.  Without such a limit, given a valid TFO
 * cookie, an attacker could keep the listen queue in an overflow condition
 * using a TFO SYN flood.  This implementation sets the limit at half the
 * configured listen backlog.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/hash.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rmlock.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <crypto/siphash/siphash.h>

#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_fastopen.h>


#define	TCP_FASTOPEN_KEY_LEN	SIPHASH_KEY_LENGTH

#if TCP_FASTOPEN_PSK_LEN != TCP_FASTOPEN_KEY_LEN
#error TCP_FASTOPEN_PSK_LEN must be equal to TCP_FASTOPEN_KEY_LEN
#endif

/*
 * Because a PSK-mode setsockopt() uses tcpcb.t_tfo_cookie.client to hold
 * the PSK until the connect occurs.
 */
#if TCP_FASTOPEN_MAX_COOKIE_LEN < TCP_FASTOPEN_PSK_LEN
#error TCP_FASTOPEN_MAX_COOKIE_LEN must be >= TCP_FASTOPEN_PSK_LEN
#endif

#define TCP_FASTOPEN_CCACHE_BUCKET_LIMIT_DEFAULT	16
#define TCP_FASTOPEN_CCACHE_BUCKETS_DEFAULT		2048 /* must be power of 2 */

#define TCP_FASTOPEN_PATH_DISABLE_TIME_DEFAULT		900 /* seconds */

#if !defined(TCP_RFC7413_MAX_KEYS) || (TCP_RFC7413_MAX_KEYS < 1)
#define	TCP_FASTOPEN_MAX_KEYS	2
#else
#define	TCP_FASTOPEN_MAX_KEYS	TCP_RFC7413_MAX_KEYS
#endif

#if TCP_FASTOPEN_MAX_KEYS > 10
#undef TCP_FASTOPEN_MAX_KEYS
#define	TCP_FASTOPEN_MAX_KEYS	10
#endif

#if !defined(TCP_RFC7413_MAX_PSKS) || (TCP_RFC7413_MAX_PSKS < 1)
#define	TCP_FASTOPEN_MAX_PSKS	2
#else
#define	TCP_FASTOPEN_MAX_PSKS	TCP_RFC7413_MAX_PSKS
#endif

#if TCP_FASTOPEN_MAX_PSKS > 10
#undef TCP_FASTOPEN_MAX_PSKS
#define	TCP_FASTOPEN_MAX_PSKS	10
#endif

struct tcp_fastopen_keylist {
	unsigned int newest;
	unsigned int newest_psk;
	uint8_t key[TCP_FASTOPEN_MAX_KEYS][TCP_FASTOPEN_KEY_LEN];
	uint8_t psk[TCP_FASTOPEN_MAX_PSKS][TCP_FASTOPEN_KEY_LEN];
};

struct tcp_fastopen_callout {
	struct callout c;
	struct vnet *v;
};

static struct tcp_fastopen_ccache_entry *tcp_fastopen_ccache_lookup(
    struct in_conninfo *, struct tcp_fastopen_ccache_bucket **);
static struct tcp_fastopen_ccache_entry *tcp_fastopen_ccache_create(
    struct tcp_fastopen_ccache_bucket *, struct in_conninfo *, uint16_t, uint8_t,
    uint8_t *);
static void tcp_fastopen_ccache_bucket_trim(struct tcp_fastopen_ccache_bucket *,
    unsigned int);
static void tcp_fastopen_ccache_entry_drop(struct tcp_fastopen_ccache_entry *,
    struct tcp_fastopen_ccache_bucket *);

SYSCTL_NODE(_net_inet_tcp, OID_AUTO, fastopen, CTLFLAG_RW, 0, "TCP Fast Open");

VNET_DEFINE_STATIC(int, tcp_fastopen_acceptany) = 0;
#define	V_tcp_fastopen_acceptany	VNET(tcp_fastopen_acceptany)
SYSCTL_INT(_net_inet_tcp_fastopen, OID_AUTO, acceptany,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(tcp_fastopen_acceptany), 0,
    "Accept any non-empty cookie");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_autokey) = 120;
#define	V_tcp_fastopen_autokey	VNET(tcp_fastopen_autokey)
static int sysctl_net_inet_tcp_fastopen_autokey(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, autokey,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_autokey, "IU",
    "Number of seconds between auto-generation of a new key; zero disables");

static int sysctl_net_inet_tcp_fastopen_ccache_bucket_limit(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, ccache_bucket_limit,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RWTUN, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_ccache_bucket_limit, "IU",
    "Max entries per bucket in client cookie cache");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_ccache_buckets) =
    TCP_FASTOPEN_CCACHE_BUCKETS_DEFAULT;
#define	V_tcp_fastopen_ccache_buckets VNET(tcp_fastopen_ccache_buckets)
SYSCTL_UINT(_net_inet_tcp_fastopen, OID_AUTO, ccache_buckets,
    CTLFLAG_VNET | CTLFLAG_RDTUN, &VNET_NAME(tcp_fastopen_ccache_buckets), 0,
    "Client cookie cache number of buckets (power of 2)");

VNET_DEFINE(unsigned int, tcp_fastopen_client_enable) = 1;
static int sysctl_net_inet_tcp_fastopen_client_enable(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, client_enable,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_client_enable, "IU",
    "Enable/disable TCP Fast Open client functionality");

SYSCTL_INT(_net_inet_tcp_fastopen, OID_AUTO, keylen,
    CTLFLAG_RD, SYSCTL_NULL_INT_PTR, TCP_FASTOPEN_KEY_LEN,
    "Key length in bytes");

SYSCTL_INT(_net_inet_tcp_fastopen, OID_AUTO, maxkeys,
    CTLFLAG_RD, SYSCTL_NULL_INT_PTR, TCP_FASTOPEN_MAX_KEYS,
    "Maximum number of keys supported");

SYSCTL_INT(_net_inet_tcp_fastopen, OID_AUTO, maxpsks,
    CTLFLAG_RD, SYSCTL_NULL_INT_PTR, TCP_FASTOPEN_MAX_PSKS,
    "Maximum number of pre-shared keys supported");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_numkeys) = 0;
#define	V_tcp_fastopen_numkeys	VNET(tcp_fastopen_numkeys)
SYSCTL_UINT(_net_inet_tcp_fastopen, OID_AUTO, numkeys,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(tcp_fastopen_numkeys), 0,
    "Number of keys installed");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_numpsks) = 0;
#define	V_tcp_fastopen_numpsks	VNET(tcp_fastopen_numpsks)
SYSCTL_UINT(_net_inet_tcp_fastopen, OID_AUTO, numpsks,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(tcp_fastopen_numpsks), 0,
    "Number of pre-shared keys installed");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_path_disable_time) =
    TCP_FASTOPEN_PATH_DISABLE_TIME_DEFAULT;
#define	V_tcp_fastopen_path_disable_time VNET(tcp_fastopen_path_disable_time)
SYSCTL_UINT(_net_inet_tcp_fastopen, OID_AUTO, path_disable_time,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(tcp_fastopen_path_disable_time), 0,
    "Seconds a TFO failure disables a {client_ip, server_ip, server_port} path");

VNET_DEFINE_STATIC(unsigned int, tcp_fastopen_psk_enable) = 0;
#define	V_tcp_fastopen_psk_enable	VNET(tcp_fastopen_psk_enable)
static int sysctl_net_inet_tcp_fastopen_psk_enable(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, psk_enable,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_psk_enable, "IU",
    "Enable/disable TCP Fast Open server pre-shared key mode");

VNET_DEFINE(unsigned int, tcp_fastopen_server_enable) = 0;
static int sysctl_net_inet_tcp_fastopen_server_enable(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, server_enable,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_server_enable, "IU",
    "Enable/disable TCP Fast Open server functionality");

static int sysctl_net_inet_tcp_fastopen_setkey(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, setkey,
    CTLFLAG_VNET | CTLTYPE_OPAQUE | CTLFLAG_WR, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_setkey, "",
    "Install a new key");

static int sysctl_net_inet_tcp_fastopen_setpsk(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, setpsk,
    CTLFLAG_VNET | CTLTYPE_OPAQUE | CTLFLAG_WR, NULL, 0,
    &sysctl_net_inet_tcp_fastopen_setpsk, "",
    "Install a new pre-shared key");

static int sysctl_net_inet_tcp_fastopen_ccache_list(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_net_inet_tcp_fastopen, OID_AUTO, ccache_list,
    CTLFLAG_VNET | CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_SKIP, NULL, 0,
    sysctl_net_inet_tcp_fastopen_ccache_list, "A",
    "List of all client cookie cache entries");

VNET_DEFINE_STATIC(struct rmlock, tcp_fastopen_keylock);
#define	V_tcp_fastopen_keylock	VNET(tcp_fastopen_keylock)

#define TCP_FASTOPEN_KEYS_RLOCK(t)	rm_rlock(&V_tcp_fastopen_keylock, (t))
#define TCP_FASTOPEN_KEYS_RUNLOCK(t)	rm_runlock(&V_tcp_fastopen_keylock, (t))
#define TCP_FASTOPEN_KEYS_WLOCK()	rm_wlock(&V_tcp_fastopen_keylock)
#define TCP_FASTOPEN_KEYS_WUNLOCK()	rm_wunlock(&V_tcp_fastopen_keylock)

VNET_DEFINE_STATIC(struct tcp_fastopen_keylist, tcp_fastopen_keys);
#define V_tcp_fastopen_keys	VNET(tcp_fastopen_keys)

VNET_DEFINE_STATIC(struct tcp_fastopen_callout, tcp_fastopen_autokey_ctx);
#define V_tcp_fastopen_autokey_ctx	VNET(tcp_fastopen_autokey_ctx)

VNET_DEFINE_STATIC(uma_zone_t, counter_zone);
#define	V_counter_zone			VNET(counter_zone)

static MALLOC_DEFINE(M_TCP_FASTOPEN_CCACHE, "tfo_ccache", "TFO client cookie cache buckets");

VNET_DEFINE_STATIC(struct tcp_fastopen_ccache, tcp_fastopen_ccache);
#define V_tcp_fastopen_ccache	VNET(tcp_fastopen_ccache)

#define	CCB_LOCK(ccb)		mtx_lock(&(ccb)->ccb_mtx)
#define	CCB_UNLOCK(ccb)		mtx_unlock(&(ccb)->ccb_mtx)
#define	CCB_LOCK_ASSERT(ccb)	mtx_assert(&(ccb)->ccb_mtx, MA_OWNED)


void
tcp_fastopen_init(void)
{
	unsigned int i;
	
	V_counter_zone = uma_zcreate("tfo", sizeof(unsigned int),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	rm_init(&V_tcp_fastopen_keylock, "tfo_keylock");
	callout_init_rm(&V_tcp_fastopen_autokey_ctx.c,
	    &V_tcp_fastopen_keylock, 0);
	V_tcp_fastopen_autokey_ctx.v = curvnet;
	V_tcp_fastopen_keys.newest = TCP_FASTOPEN_MAX_KEYS - 1;
	V_tcp_fastopen_keys.newest_psk = TCP_FASTOPEN_MAX_PSKS - 1;

	/* May already be non-zero if kernel tunable was set */
	if (V_tcp_fastopen_ccache.bucket_limit == 0)
		V_tcp_fastopen_ccache.bucket_limit =
		    TCP_FASTOPEN_CCACHE_BUCKET_LIMIT_DEFAULT;

	/* May already be non-zero if kernel tunable was set */
	if ((V_tcp_fastopen_ccache_buckets == 0) ||
	    !powerof2(V_tcp_fastopen_ccache_buckets))
		V_tcp_fastopen_ccache.buckets =
			TCP_FASTOPEN_CCACHE_BUCKETS_DEFAULT;
	else
		V_tcp_fastopen_ccache.buckets = V_tcp_fastopen_ccache_buckets;

	V_tcp_fastopen_ccache.mask = V_tcp_fastopen_ccache.buckets - 1;
	V_tcp_fastopen_ccache.secret = arc4random();

	V_tcp_fastopen_ccache.base = malloc(V_tcp_fastopen_ccache.buckets *
	    sizeof(struct tcp_fastopen_ccache_bucket), M_TCP_FASTOPEN_CCACHE,
	    M_WAITOK | M_ZERO);

	for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {
		TAILQ_INIT(&V_tcp_fastopen_ccache.base[i].ccb_entries);
		mtx_init(&V_tcp_fastopen_ccache.base[i].ccb_mtx, "tfo_ccache_bucket",
			 NULL, MTX_DEF);
		if (V_tcp_fastopen_client_enable) {
			/* enable bucket */
			V_tcp_fastopen_ccache.base[i].ccb_num_entries = 0;
		} else {
			/* disable bucket */
			V_tcp_fastopen_ccache.base[i].ccb_num_entries = -1;
		}
		V_tcp_fastopen_ccache.base[i].ccb_ccache = &V_tcp_fastopen_ccache;
	}

	/*
	 * Note that while the total number of entries in the cookie cache
	 * is limited by the table management logic to
	 * V_tcp_fastopen_ccache.buckets *
	 * V_tcp_fastopen_ccache.bucket_limit, the total number of items in
	 * this zone can exceed that amount by the number of CPUs in the
	 * system times the maximum number of unallocated items that can be
	 * present in each UMA per-CPU cache for this zone.
	 */
	V_tcp_fastopen_ccache.zone = uma_zcreate("tfo_ccache_entries",
	    sizeof(struct tcp_fastopen_ccache_entry), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_CACHE, 0);
}

void
tcp_fastopen_destroy(void)
{
	struct tcp_fastopen_ccache_bucket *ccb;
	unsigned int i;

	for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {		
		ccb = &V_tcp_fastopen_ccache.base[i];
		tcp_fastopen_ccache_bucket_trim(ccb, 0);
		mtx_destroy(&ccb->ccb_mtx);
	}

	KASSERT(uma_zone_get_cur(V_tcp_fastopen_ccache.zone) == 0,
	    ("%s: TFO ccache zone allocation count not 0", __func__));
	uma_zdestroy(V_tcp_fastopen_ccache.zone);
	free(V_tcp_fastopen_ccache.base, M_TCP_FASTOPEN_CCACHE);

	callout_drain(&V_tcp_fastopen_autokey_ctx.c);
	rm_destroy(&V_tcp_fastopen_keylock);
	uma_zdestroy(V_counter_zone);
}

unsigned int *
tcp_fastopen_alloc_counter(void)
{
	unsigned int *counter;
	counter = uma_zalloc(V_counter_zone, M_NOWAIT);
	if (counter)
		*counter = 1;
	return (counter);
}

void
tcp_fastopen_decrement_counter(unsigned int *counter)
{
	if (*counter == 1)
		uma_zfree(V_counter_zone, counter);
	else
		atomic_subtract_int(counter, 1);
}

static void
tcp_fastopen_addkey_locked(uint8_t *key)
{

	V_tcp_fastopen_keys.newest++;
	if (V_tcp_fastopen_keys.newest == TCP_FASTOPEN_MAX_KEYS)
		V_tcp_fastopen_keys.newest = 0;
	memcpy(V_tcp_fastopen_keys.key[V_tcp_fastopen_keys.newest], key,
	    TCP_FASTOPEN_KEY_LEN);
	if (V_tcp_fastopen_numkeys < TCP_FASTOPEN_MAX_KEYS)
		V_tcp_fastopen_numkeys++;
}

static void
tcp_fastopen_addpsk_locked(uint8_t *psk)
{

	V_tcp_fastopen_keys.newest_psk++;
	if (V_tcp_fastopen_keys.newest_psk == TCP_FASTOPEN_MAX_PSKS)
		V_tcp_fastopen_keys.newest_psk = 0;
	memcpy(V_tcp_fastopen_keys.psk[V_tcp_fastopen_keys.newest_psk], psk,
	    TCP_FASTOPEN_KEY_LEN);
	if (V_tcp_fastopen_numpsks < TCP_FASTOPEN_MAX_PSKS)
		V_tcp_fastopen_numpsks++;
}

static void
tcp_fastopen_autokey_locked(void)
{
	uint8_t newkey[TCP_FASTOPEN_KEY_LEN];

	arc4rand(newkey, TCP_FASTOPEN_KEY_LEN, 0);
	tcp_fastopen_addkey_locked(newkey);
}

static void
tcp_fastopen_autokey_callout(void *arg)
{
	struct tcp_fastopen_callout *ctx = arg;

	CURVNET_SET(ctx->v);
	tcp_fastopen_autokey_locked();
	callout_reset(&ctx->c, V_tcp_fastopen_autokey * hz,
		      tcp_fastopen_autokey_callout, ctx);
	CURVNET_RESTORE();
}


static uint64_t
tcp_fastopen_make_cookie(uint8_t key[SIPHASH_KEY_LENGTH], struct in_conninfo *inc)
{
	SIPHASH_CTX ctx;
	uint64_t siphash;

	SipHash24_Init(&ctx);
	SipHash_SetKey(&ctx, key);
	switch (inc->inc_flags & INC_ISIPV6) {
#ifdef INET
	case 0:
		SipHash_Update(&ctx, &inc->inc_faddr, sizeof(inc->inc_faddr));
		break;
#endif
#ifdef INET6
	case INC_ISIPV6:
		SipHash_Update(&ctx, &inc->inc6_faddr, sizeof(inc->inc6_faddr));
		break;
#endif
	}
	SipHash_Final((u_int8_t *)&siphash, &ctx);

	return (siphash);
}

static uint64_t
tcp_fastopen_make_psk_cookie(uint8_t *psk, uint8_t *cookie, uint8_t cookie_len)
{
	SIPHASH_CTX ctx;
	uint64_t psk_cookie;

	SipHash24_Init(&ctx);
	SipHash_SetKey(&ctx, psk);
	SipHash_Update(&ctx, cookie, cookie_len);
	SipHash_Final((u_int8_t *)&psk_cookie, &ctx);

	return (psk_cookie);
}

static int
tcp_fastopen_find_cookie_match_locked(uint8_t *wire_cookie, uint64_t *cur_cookie)
{
	unsigned int i, psk_index;
	uint64_t psk_cookie;

	if (V_tcp_fastopen_psk_enable) {
		psk_index = V_tcp_fastopen_keys.newest_psk;
		for (i = 0; i < V_tcp_fastopen_numpsks; i++) {
			psk_cookie =
			    tcp_fastopen_make_psk_cookie(
				 V_tcp_fastopen_keys.psk[psk_index],
				 (uint8_t *)cur_cookie,
				 TCP_FASTOPEN_COOKIE_LEN);

			if (memcmp(wire_cookie, &psk_cookie,
				   TCP_FASTOPEN_COOKIE_LEN) == 0)
				return (1);

			if (psk_index == 0)
				psk_index = TCP_FASTOPEN_MAX_PSKS - 1;
			else
				psk_index--;
		}
	} else if (memcmp(wire_cookie, cur_cookie, TCP_FASTOPEN_COOKIE_LEN) == 0)
		return (1);

	return (0);
}

/*
 * Return values:
 *	-1	the cookie is invalid and no valid cookie is available
 *	 0	the cookie is invalid and the latest cookie has been returned
 *	 1	the cookie is valid and the latest cookie has been returned
 */
int
tcp_fastopen_check_cookie(struct in_conninfo *inc, uint8_t *cookie,
    unsigned int len, uint64_t *latest_cookie)
{
	struct rm_priotracker tracker;
	unsigned int i, key_index;
	int rv;
	uint64_t cur_cookie;

	if (V_tcp_fastopen_acceptany) {
		*latest_cookie = 0;
		return (1);
	}

	TCP_FASTOPEN_KEYS_RLOCK(&tracker);
	if (len != TCP_FASTOPEN_COOKIE_LEN) {
		if (V_tcp_fastopen_numkeys > 0) {
			*latest_cookie =
			    tcp_fastopen_make_cookie(
				V_tcp_fastopen_keys.key[V_tcp_fastopen_keys.newest],
				inc);
			rv = 0;
		} else
			rv = -1;
		goto out;
	}

	/*
	 * Check against each available key, from newest to oldest.
	 */
	key_index = V_tcp_fastopen_keys.newest;
	for (i = 0; i < V_tcp_fastopen_numkeys; i++) {
		cur_cookie =
		    tcp_fastopen_make_cookie(V_tcp_fastopen_keys.key[key_index],
			inc);
		if (i == 0)
			*latest_cookie = cur_cookie;
		rv = tcp_fastopen_find_cookie_match_locked(cookie, &cur_cookie);
		if (rv)
			goto out;
		if (key_index == 0)
			key_index = TCP_FASTOPEN_MAX_KEYS - 1;
		else
			key_index--;
	}
	rv = 0;

 out:
	TCP_FASTOPEN_KEYS_RUNLOCK(&tracker);
	return (rv);
}

static int
sysctl_net_inet_tcp_fastopen_autokey(SYSCTL_HANDLER_ARGS)
{
	int error;
	unsigned int new;

	new = V_tcp_fastopen_autokey;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (new > (INT_MAX / hz))
			return (EINVAL);

		TCP_FASTOPEN_KEYS_WLOCK();
		if (V_tcp_fastopen_server_enable) {
			if (V_tcp_fastopen_autokey && !new)
				callout_stop(&V_tcp_fastopen_autokey_ctx.c);
			else if (new)
				callout_reset(&V_tcp_fastopen_autokey_ctx.c,
				    new * hz, tcp_fastopen_autokey_callout,
				    &V_tcp_fastopen_autokey_ctx);
		}
		V_tcp_fastopen_autokey = new;
		TCP_FASTOPEN_KEYS_WUNLOCK();
	}

	return (error);
}

static int
sysctl_net_inet_tcp_fastopen_psk_enable(SYSCTL_HANDLER_ARGS)
{
	int error;
	unsigned int new;

	new = V_tcp_fastopen_psk_enable;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (V_tcp_fastopen_psk_enable && !new) {
			/* enabled -> disabled */
			TCP_FASTOPEN_KEYS_WLOCK();
			V_tcp_fastopen_numpsks = 0;
			V_tcp_fastopen_keys.newest_psk =
			    TCP_FASTOPEN_MAX_PSKS - 1;
			V_tcp_fastopen_psk_enable = 0;
			TCP_FASTOPEN_KEYS_WUNLOCK();
		} else if (!V_tcp_fastopen_psk_enable && new) {
			/* disabled -> enabled */
			TCP_FASTOPEN_KEYS_WLOCK();
			V_tcp_fastopen_psk_enable = 1;
			TCP_FASTOPEN_KEYS_WUNLOCK();
		}
	}
	return (error);
}

static int
sysctl_net_inet_tcp_fastopen_server_enable(SYSCTL_HANDLER_ARGS)
{
	int error;
	unsigned int new;

	new = V_tcp_fastopen_server_enable;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (V_tcp_fastopen_server_enable && !new) {
			/* enabled -> disabled */
			TCP_FASTOPEN_KEYS_WLOCK();
			V_tcp_fastopen_numkeys = 0;
			V_tcp_fastopen_keys.newest = TCP_FASTOPEN_MAX_KEYS - 1;
			if (V_tcp_fastopen_autokey)
				callout_stop(&V_tcp_fastopen_autokey_ctx.c);
			V_tcp_fastopen_numpsks = 0;
			V_tcp_fastopen_keys.newest_psk =
			    TCP_FASTOPEN_MAX_PSKS - 1;
			V_tcp_fastopen_server_enable = 0;
			TCP_FASTOPEN_KEYS_WUNLOCK();
		} else if (!V_tcp_fastopen_server_enable && new) {
			/* disabled -> enabled */
			TCP_FASTOPEN_KEYS_WLOCK();
			if (V_tcp_fastopen_autokey &&
			    (V_tcp_fastopen_numkeys == 0)) {
				tcp_fastopen_autokey_locked();
				callout_reset(&V_tcp_fastopen_autokey_ctx.c,
				    V_tcp_fastopen_autokey * hz,
				    tcp_fastopen_autokey_callout,
				    &V_tcp_fastopen_autokey_ctx);
			}
			V_tcp_fastopen_server_enable = 1;
			TCP_FASTOPEN_KEYS_WUNLOCK();
		}
	}
	return (error);
}

static int
sysctl_net_inet_tcp_fastopen_setkey(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint8_t newkey[TCP_FASTOPEN_KEY_LEN];

	if (req->oldptr != NULL || req->oldlen != 0)
		return (EINVAL);
	if (req->newptr == NULL)
		return (EPERM);
	if (req->newlen != sizeof(newkey))
		return (EINVAL);
	error = SYSCTL_IN(req, newkey, sizeof(newkey));
	if (error)
		return (error);

	TCP_FASTOPEN_KEYS_WLOCK();
	tcp_fastopen_addkey_locked(newkey);
	TCP_FASTOPEN_KEYS_WUNLOCK();

	return (0);
}

static int
sysctl_net_inet_tcp_fastopen_setpsk(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint8_t newpsk[TCP_FASTOPEN_KEY_LEN];

	if (req->oldptr != NULL || req->oldlen != 0)
		return (EINVAL);
	if (req->newptr == NULL)
		return (EPERM);
	if (req->newlen != sizeof(newpsk))
		return (EINVAL);
	error = SYSCTL_IN(req, newpsk, sizeof(newpsk));
	if (error)
		return (error);

	TCP_FASTOPEN_KEYS_WLOCK();
	tcp_fastopen_addpsk_locked(newpsk);
	TCP_FASTOPEN_KEYS_WUNLOCK();

	return (0);
}

static int
sysctl_net_inet_tcp_fastopen_ccache_bucket_limit(SYSCTL_HANDLER_ARGS)
{
	struct tcp_fastopen_ccache_bucket *ccb;
	int error;
	unsigned int new;
	unsigned int i;
	
	new = V_tcp_fastopen_ccache.bucket_limit;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if ((new == 0) || (new > INT_MAX))
			error = EINVAL;
		else {
			if (new < V_tcp_fastopen_ccache.bucket_limit) {
				for (i = 0; i < V_tcp_fastopen_ccache.buckets;
				     i++) {
					ccb = &V_tcp_fastopen_ccache.base[i];
					tcp_fastopen_ccache_bucket_trim(ccb, new);
				}
			}
			V_tcp_fastopen_ccache.bucket_limit = new;
		}
			
	}
	return (error);
}

static int
sysctl_net_inet_tcp_fastopen_client_enable(SYSCTL_HANDLER_ARGS)
{
	struct tcp_fastopen_ccache_bucket *ccb;
	int error;
	unsigned int new, i;

	new = V_tcp_fastopen_client_enable;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (V_tcp_fastopen_client_enable && !new) {
			/* enabled -> disabled */
			for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {
				ccb = &V_tcp_fastopen_ccache.base[i];
				KASSERT(ccb->ccb_num_entries > -1,
				    ("%s: ccb->ccb_num_entries %d is negative",
					__func__, ccb->ccb_num_entries));
				tcp_fastopen_ccache_bucket_trim(ccb, 0);
			}
			V_tcp_fastopen_client_enable = 0;
		} else if (!V_tcp_fastopen_client_enable && new) {
			/* disabled -> enabled */
			for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {
				ccb = &V_tcp_fastopen_ccache.base[i];
				CCB_LOCK(ccb);
				KASSERT(TAILQ_EMPTY(&ccb->ccb_entries),
				    ("%s: ccb->ccb_entries not empty", __func__));
				KASSERT(ccb->ccb_num_entries == -1,
				    ("%s: ccb->ccb_num_entries %d not -1", __func__,
					ccb->ccb_num_entries));
				ccb->ccb_num_entries = 0; /* enable bucket */
				CCB_UNLOCK(ccb);
			}			
			V_tcp_fastopen_client_enable = 1;
		}
	}
	return (error);
}

void
tcp_fastopen_connect(struct tcpcb *tp)
{
	struct inpcb *inp;
	struct tcp_fastopen_ccache_bucket *ccb;
	struct tcp_fastopen_ccache_entry *cce;
	sbintime_t now;
	uint16_t server_mss;
	uint64_t psk_cookie;
	
	psk_cookie = 0;
	inp = tp->t_inpcb;
	cce = tcp_fastopen_ccache_lookup(&inp->inp_inc, &ccb);
	if (cce) {
		if (cce->disable_time == 0) {
			if ((cce->cookie_len > 0) &&
			    (tp->t_tfo_client_cookie_len ==
			     TCP_FASTOPEN_PSK_LEN)) {
				psk_cookie =
				    tcp_fastopen_make_psk_cookie(
					tp->t_tfo_cookie.client,
					cce->cookie, cce->cookie_len);
			} else {
				tp->t_tfo_client_cookie_len = cce->cookie_len;
				memcpy(tp->t_tfo_cookie.client, cce->cookie,
				    cce->cookie_len);
			}
			server_mss = cce->server_mss;
			CCB_UNLOCK(ccb);
			if (tp->t_tfo_client_cookie_len ==
			    TCP_FASTOPEN_PSK_LEN && psk_cookie) {
				tp->t_tfo_client_cookie_len =
				    TCP_FASTOPEN_COOKIE_LEN;
				memcpy(tp->t_tfo_cookie.client, &psk_cookie,
				    TCP_FASTOPEN_COOKIE_LEN);
			}
			tcp_mss(tp, server_mss ? server_mss : -1);
			tp->snd_wnd = tp->t_maxseg;
		} else {
			/*
			 * The path is disabled.  Check the time and
			 * possibly re-enable.
			 */
			now = getsbinuptime();
			if (now - cce->disable_time >
			    ((sbintime_t)V_tcp_fastopen_path_disable_time << 32)) {
				/*
				 * Re-enable path.  Force a TFO cookie
				 * request.  Forget the old MSS as it may be
				 * bogus now, and we will rediscover it in
				 * the SYN|ACK.
				 */
				cce->disable_time = 0;
				cce->server_mss = 0;
				cce->cookie_len = 0;
				/*
				 * tp->t_tfo... cookie details are already
				 * zero from the tcpcb init.
				 */
			} else {
				/*
				 * Path is disabled, so disable TFO on this
				 * connection.
				 */
				tp->t_flags &= ~TF_FASTOPEN;
			}
			CCB_UNLOCK(ccb);
			tcp_mss(tp, -1);
			/*
			 * snd_wnd is irrelevant since we are either forcing
			 * a TFO cookie request or disabling TFO - either
			 * way, no data with the SYN.
			 */
		}
	} else {
		/*
		 * A new entry for this path will be created when a SYN|ACK
		 * comes back, or the attempt otherwise fails.
		 */
		CCB_UNLOCK(ccb);
		tcp_mss(tp, -1);
		/*
		 * snd_wnd is irrelevant since we are forcing a TFO cookie
		 * request.
		 */
	}
}

void
tcp_fastopen_disable_path(struct tcpcb *tp)
{
	struct in_conninfo *inc = &tp->t_inpcb->inp_inc;
	struct tcp_fastopen_ccache_bucket *ccb;
	struct tcp_fastopen_ccache_entry *cce;

	cce = tcp_fastopen_ccache_lookup(inc, &ccb);
	if (cce) {
		cce->server_mss = 0;
		cce->cookie_len = 0;
		/*
		 * Preserve the existing disable time if it is already
		 * disabled.
		 */
		if (cce->disable_time == 0)
			cce->disable_time = getsbinuptime();
	} else /* use invalid cookie len to create disabled entry */
		tcp_fastopen_ccache_create(ccb, inc, 0,
	   	    TCP_FASTOPEN_MAX_COOKIE_LEN + 1, NULL);

	CCB_UNLOCK(ccb);
	tp->t_flags &= ~TF_FASTOPEN;
}

void
tcp_fastopen_update_cache(struct tcpcb *tp, uint16_t mss,
    uint8_t cookie_len, uint8_t *cookie)
{
	struct in_conninfo *inc = &tp->t_inpcb->inp_inc;
	struct tcp_fastopen_ccache_bucket *ccb;
	struct tcp_fastopen_ccache_entry *cce;

	cce = tcp_fastopen_ccache_lookup(inc, &ccb);
	if (cce) {
		if ((cookie_len >= TCP_FASTOPEN_MIN_COOKIE_LEN) &&
		    (cookie_len <= TCP_FASTOPEN_MAX_COOKIE_LEN) &&
		    ((cookie_len & 0x1) == 0)) {
			cce->server_mss = mss;
			cce->cookie_len = cookie_len;
			memcpy(cce->cookie, cookie, cookie_len);
			cce->disable_time = 0;
		} else {
			/* invalid cookie length, disable entry */
			cce->server_mss = 0;
			cce->cookie_len = 0;
			/*
			 * Preserve the existing disable time if it is
			 * already disabled.
			 */
			if (cce->disable_time == 0)
				cce->disable_time = getsbinuptime();
		}
	} else
		tcp_fastopen_ccache_create(ccb, inc, mss, cookie_len, cookie);

	CCB_UNLOCK(ccb);
}

static struct tcp_fastopen_ccache_entry *
tcp_fastopen_ccache_lookup(struct in_conninfo *inc,
    struct tcp_fastopen_ccache_bucket **ccbp)
{
	struct tcp_fastopen_ccache_bucket *ccb;
	struct tcp_fastopen_ccache_entry *cce;
	uint32_t last_word;
	uint32_t hash;

	hash = jenkins_hash32((uint32_t *)&inc->inc_ie.ie_dependladdr, 4,
	    V_tcp_fastopen_ccache.secret);
	hash = jenkins_hash32((uint32_t *)&inc->inc_ie.ie_dependfaddr, 4,
	    hash);
	last_word = inc->inc_fport;
	hash = jenkins_hash32(&last_word, 1, hash);
	ccb = &V_tcp_fastopen_ccache.base[hash & V_tcp_fastopen_ccache.mask];
	*ccbp = ccb;
	CCB_LOCK(ccb);
	
	/*
	 * Always returns with locked bucket.
	 */
	TAILQ_FOREACH(cce, &ccb->ccb_entries, cce_link)
		if ((!(cce->af == AF_INET6) == !(inc->inc_flags & INC_ISIPV6)) &&
		    (cce->server_port == inc->inc_ie.ie_fport) &&
		    (((cce->af == AF_INET) &&
		      (cce->cce_client_ip.v4.s_addr == inc->inc_laddr.s_addr) &&
		      (cce->cce_server_ip.v4.s_addr == inc->inc_faddr.s_addr)) ||
		     ((cce->af == AF_INET6) &&
		      IN6_ARE_ADDR_EQUAL(&cce->cce_client_ip.v6, &inc->inc6_laddr) &&
		      IN6_ARE_ADDR_EQUAL(&cce->cce_server_ip.v6, &inc->inc6_faddr))))
			break;

	return (cce);
}

static struct tcp_fastopen_ccache_entry *
tcp_fastopen_ccache_create(struct tcp_fastopen_ccache_bucket *ccb,
    struct in_conninfo *inc, uint16_t mss, uint8_t cookie_len, uint8_t *cookie)
{
	struct tcp_fastopen_ccache_entry *cce;
	
	/*
	 * 1. Create a new entry, or
	 * 2. Reclaim an existing entry, or
	 * 3. Fail
	 */

	CCB_LOCK_ASSERT(ccb);
	
	cce = NULL;
	if (ccb->ccb_num_entries < V_tcp_fastopen_ccache.bucket_limit)
		cce = uma_zalloc(V_tcp_fastopen_ccache.zone, M_NOWAIT);

	if (cce == NULL) {
		/*
		 * At bucket limit, or out of memory - reclaim last
		 * entry in bucket.
		 */
		cce = TAILQ_LAST(&ccb->ccb_entries, bucket_entries);
		if (cce == NULL) {
			/* XXX count this event */
			return (NULL);
		}

		TAILQ_REMOVE(&ccb->ccb_entries, cce, cce_link);
	} else
		ccb->ccb_num_entries++;

	TAILQ_INSERT_HEAD(&ccb->ccb_entries, cce, cce_link);
	cce->af = (inc->inc_flags & INC_ISIPV6) ? AF_INET6 : AF_INET;
	if (cce->af == AF_INET) {
		cce->cce_client_ip.v4 = inc->inc_laddr;
		cce->cce_server_ip.v4 = inc->inc_faddr;
	} else {
		cce->cce_client_ip.v6 = inc->inc6_laddr;
		cce->cce_server_ip.v6 = inc->inc6_faddr;
	}
	cce->server_port = inc->inc_fport;
	if ((cookie_len >= TCP_FASTOPEN_MIN_COOKIE_LEN) &&
	    (cookie_len <= TCP_FASTOPEN_MAX_COOKIE_LEN) &&
	    ((cookie_len & 0x1) == 0)) {
		cce->server_mss = mss;
		cce->cookie_len = cookie_len;
		memcpy(cce->cookie, cookie, cookie_len);
		cce->disable_time = 0;
	} else {
		/* invalid cookie length, disable cce */
		cce->server_mss = 0;
		cce->cookie_len = 0;
		cce->disable_time = getsbinuptime();
	}
	
	return (cce);
}

static void
tcp_fastopen_ccache_bucket_trim(struct tcp_fastopen_ccache_bucket *ccb,
    unsigned int limit)
{
	struct tcp_fastopen_ccache_entry *cce, *cce_tmp;
	unsigned int entries;
	
	CCB_LOCK(ccb);
	entries = 0;
	TAILQ_FOREACH_SAFE(cce, &ccb->ccb_entries, cce_link, cce_tmp) {
		entries++;
		if (entries > limit)
			tcp_fastopen_ccache_entry_drop(cce, ccb);
	}
	KASSERT(ccb->ccb_num_entries <= (int)limit,
	    ("%s: ccb->ccb_num_entries %d exceeds limit %d", __func__,
		ccb->ccb_num_entries, limit));
	if (limit == 0) {
		KASSERT(TAILQ_EMPTY(&ccb->ccb_entries),
		    ("%s: ccb->ccb_entries not empty", __func__));
		ccb->ccb_num_entries = -1; /* disable bucket */
	}
	CCB_UNLOCK(ccb);
}

static void
tcp_fastopen_ccache_entry_drop(struct tcp_fastopen_ccache_entry *cce,
    struct tcp_fastopen_ccache_bucket *ccb)
{

	CCB_LOCK_ASSERT(ccb);

	TAILQ_REMOVE(&ccb->ccb_entries, cce, cce_link);
	ccb->ccb_num_entries--;
	uma_zfree(V_tcp_fastopen_ccache.zone, cce);
}

static int
sysctl_net_inet_tcp_fastopen_ccache_list(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct tcp_fastopen_ccache_bucket *ccb;
	struct tcp_fastopen_ccache_entry *cce;
	sbintime_t now, duration, limit;
	const int linesize = 128;
	int i, error, num_entries;
	unsigned int j;
#ifdef INET6
	char clt_buf[INET6_ADDRSTRLEN], srv_buf[INET6_ADDRSTRLEN];
#else
	char clt_buf[INET_ADDRSTRLEN], srv_buf[INET_ADDRSTRLEN];
#endif

	if (jailed_without_vnet(curthread->td_ucred) != 0)
		return (EPERM);

	/* Only allow root to read the client cookie cache */
	if (curthread->td_ucred->cr_uid != 0)
		return (EPERM);

	num_entries = 0;
	for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {
		ccb = &V_tcp_fastopen_ccache.base[i];
		CCB_LOCK(ccb);
		if (ccb->ccb_num_entries > 0)
			num_entries += ccb->ccb_num_entries;
		CCB_UNLOCK(ccb);
	}
	sbuf_new(&sb, NULL, linesize * (num_entries + 1), SBUF_INCLUDENUL);

	sbuf_printf(&sb,
	            "\nLocal IP address     Remote IP address     Port   MSS"
	            " Disabled Cookie\n");

	now = getsbinuptime();
	limit = (sbintime_t)V_tcp_fastopen_path_disable_time << 32;
	for (i = 0; i < V_tcp_fastopen_ccache.buckets; i++) {
		ccb = &V_tcp_fastopen_ccache.base[i];
		CCB_LOCK(ccb);
		TAILQ_FOREACH(cce, &ccb->ccb_entries, cce_link) {
			if (cce->disable_time != 0) {
				duration = now - cce->disable_time;
				if (limit >= duration)
					duration = limit - duration;
				else
					duration = 0;
			} else
				duration = 0;
			sbuf_printf(&sb,
			            "%-20s %-20s %5u %5u ",
			            inet_ntop(cce->af, &cce->cce_client_ip,
			                clt_buf, sizeof(clt_buf)),
			            inet_ntop(cce->af, &cce->cce_server_ip,
			                srv_buf, sizeof(srv_buf)),
			            ntohs(cce->server_port),
			            cce->server_mss);
			if (duration > 0)
				sbuf_printf(&sb, "%7ds ", sbintime_getsec(duration));
			else
				sbuf_printf(&sb, "%8s ", "No");
			for (j = 0; j < cce->cookie_len; j++)
				sbuf_printf(&sb, "%02x", cce->cookie[j]);
			sbuf_putc(&sb, '\n');
		}
		CCB_UNLOCK(ccb);
	}
	error = sbuf_finish(&sb);
	if (error == 0)
		error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return (error);
}

