/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017-2018 Yandex LLC
 * Copyright (c) 2017-2018 Andrey V. Elsukov <ae@FreeBSD.org>
 * Copyright (c) 2002 Luigi Rizzo, Universita` di Pisa
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipfw.h"
#ifndef INET
#error IPFIREWALL requires INET.
#endif /* INET */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/hash.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/pcpu.h>
#include <sys/queue.h>
#include <sys/rmlock.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>

#include <netinet/ip6.h>	/* IN6_ARE_ADDR_EQUAL */
#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#endif

#include <netpfil/ipfw/ip_fw_private.h>

#include <machine/in_cksum.h>	/* XXX for in_cksum */

#ifdef MAC
#include <security/mac/mac_framework.h>
#endif

/*
 * Description of dynamic states.
 *
 * Dynamic states are stored in lists accessed through a hash tables
 * whose size is curr_dyn_buckets. This value can be modified through
 * the sysctl variable dyn_buckets.
 *
 * Currently there are four tables: dyn_ipv4, dyn_ipv6, dyn_ipv4_parent,
 * and dyn_ipv6_parent.
 *
 * When a packet is received, its address fields hashed, then matched
 * against the entries in the corresponding list by addr_type.
 * Dynamic states can be used for different purposes:
 *  + stateful rules;
 *  + enforcing limits on the number of sessions;
 *  + in-kernel NAT (not implemented yet)
 *
 * The lifetime of dynamic states is regulated by dyn_*_lifetime,
 * measured in seconds and depending on the flags.
 *
 * The total number of dynamic states is equal to UMA zone items count.
 * The max number of dynamic states is dyn_max. When we reach
 * the maximum number of rules we do not create anymore. This is
 * done to avoid consuming too much memory, but also too much
 * time when searching on each packet (ideally, we should try instead
 * to put a limit on the length of the list on each bucket...).
 *
 * Each state holds a pointer to the parent ipfw rule so we know what
 * action to perform. Dynamic rules are removed when the parent rule is
 * deleted.
 *
 * There are some limitations with dynamic rules -- we do not
 * obey the 'randomized match', and we do not do multiple
 * passes through the firewall. XXX check the latter!!!
 */

/* By default use jenkins hash function */
#define	IPFIREWALL_JENKINSHASH

#define	DYN_COUNTER_INC(d, dir, pktlen)	do {	\
	(d)->pcnt_ ## dir++;			\
	(d)->bcnt_ ## dir += pktlen;		\
	} while (0)

#define	DYN_REFERENCED		0x01
/*
 * DYN_REFERENCED flag is used to show that state keeps reference to named
 * object, and this reference should be released when state becomes expired.
 */

struct dyn_data {
	void		*parent;	/* pointer to parent rule */
	uint32_t	chain_id;	/* cached ruleset id */
	uint32_t	f_pos;		/* cached rule index */

	uint32_t	hashval;	/* hash value used for hash resize */
	uint16_t	fibnum;		/* fib used to send keepalives */
	uint8_t		_pad[3];
	uint8_t		flags;		/* internal flags */
	uint16_t	rulenum;	/* parent rule number */
	uint32_t	ruleid;		/* parent rule id */

	uint32_t	state;		/* TCP session state and flags */
	uint32_t	ack_fwd;	/* most recent ACKs in forward */
	uint32_t	ack_rev;	/* and reverse direction (used */
					/* to generate keepalives) */
	uint32_t	sync;		/* synchronization time */
	uint32_t	expire;		/* expire time */

	uint64_t	pcnt_fwd;	/* bytes counter in forward */
	uint64_t	bcnt_fwd;	/* packets counter in forward */
	uint64_t	pcnt_rev;	/* bytes counter in reverse */
	uint64_t	bcnt_rev;	/* packets counter in reverse */
};

#define	DPARENT_COUNT_DEC(p)	do {			\
	MPASS(p->count > 0);				\
	ck_pr_dec_32(&(p)->count);			\
} while (0)
#define	DPARENT_COUNT_INC(p)	ck_pr_inc_32(&(p)->count)
#define	DPARENT_COUNT(p)	ck_pr_load_32(&(p)->count)
struct dyn_parent {
	void		*parent;	/* pointer to parent rule */
	uint32_t	count;		/* number of linked states */
	uint8_t		_pad[2];
	uint16_t	rulenum;	/* parent rule number */
	uint32_t	ruleid;		/* parent rule id */
	uint32_t	hashval;	/* hash value used for hash resize */
	uint32_t	expire;		/* expire time */
};

struct dyn_ipv4_state {
	uint8_t		type;		/* State type */
	uint8_t		proto;		/* UL Protocol */
	uint16_t	kidx;		/* named object index */
	uint16_t	sport, dport;	/* ULP source and destination ports */
	in_addr_t	src, dst;	/* IPv4 source and destination */

	union {
		struct dyn_data	*data;
		struct dyn_parent *limit;
	};
	CK_SLIST_ENTRY(dyn_ipv4_state)	entry;
	SLIST_ENTRY(dyn_ipv4_state)	expired;
};
CK_SLIST_HEAD(dyn_ipv4ck_slist, dyn_ipv4_state);
VNET_DEFINE_STATIC(struct dyn_ipv4ck_slist *, dyn_ipv4);
VNET_DEFINE_STATIC(struct dyn_ipv4ck_slist *, dyn_ipv4_parent);

SLIST_HEAD(dyn_ipv4_slist, dyn_ipv4_state);
VNET_DEFINE_STATIC(struct dyn_ipv4_slist, dyn_expired_ipv4);
#define	V_dyn_ipv4			VNET(dyn_ipv4)
#define	V_dyn_ipv4_parent		VNET(dyn_ipv4_parent)
#define	V_dyn_expired_ipv4		VNET(dyn_expired_ipv4)

#ifdef INET6
struct dyn_ipv6_state {
	uint8_t		type;		/* State type */
	uint8_t		proto;		/* UL Protocol */
	uint16_t	kidx;		/* named object index */
	uint16_t	sport, dport;	/* ULP source and destination ports */
	struct in6_addr	src, dst;	/* IPv6 source and destination */
	uint32_t	zoneid;		/* IPv6 scope zone id */
	union {
		struct dyn_data	*data;
		struct dyn_parent *limit;
	};
	CK_SLIST_ENTRY(dyn_ipv6_state)	entry;
	SLIST_ENTRY(dyn_ipv6_state)	expired;
};
CK_SLIST_HEAD(dyn_ipv6ck_slist, dyn_ipv6_state);
VNET_DEFINE_STATIC(struct dyn_ipv6ck_slist *, dyn_ipv6);
VNET_DEFINE_STATIC(struct dyn_ipv6ck_slist *, dyn_ipv6_parent);

SLIST_HEAD(dyn_ipv6_slist, dyn_ipv6_state);
VNET_DEFINE_STATIC(struct dyn_ipv6_slist, dyn_expired_ipv6);
#define	V_dyn_ipv6			VNET(dyn_ipv6)
#define	V_dyn_ipv6_parent		VNET(dyn_ipv6_parent)
#define	V_dyn_expired_ipv6		VNET(dyn_expired_ipv6)
#endif /* INET6 */

/*
 * Per-CPU pointer indicates that specified state is currently in use
 * and must not be reclaimed by expiration callout.
 */
static void **dyn_hp_cache;
DPCPU_DEFINE_STATIC(void *, dyn_hp);
#define	DYNSTATE_GET(cpu)	ck_pr_load_ptr(DPCPU_ID_PTR((cpu), dyn_hp))
#define	DYNSTATE_PROTECT(v)	ck_pr_store_ptr(DPCPU_PTR(dyn_hp), (v))
#define	DYNSTATE_RELEASE()	DYNSTATE_PROTECT(NULL)
#define	DYNSTATE_CRITICAL_ENTER()	critical_enter()
#define	DYNSTATE_CRITICAL_EXIT()	do {	\
	DYNSTATE_RELEASE();			\
	critical_exit();			\
} while (0);

/*
 * We keep two version numbers, one is updated when new entry added to
 * the list. Second is updated when an entry deleted from the list.
 * Versions are updated under bucket lock.
 *
 * Bucket "add" version number is used to know, that in the time between
 * state lookup (i.e. ipfw_dyn_lookup_state()) and the followed state
 * creation (i.e. ipfw_dyn_install_state()) another concurrent thread did
 * not install some state in this bucket. Using this info we can avoid
 * additional state lookup, because we are sure that we will not install
 * the state twice.
 *
 * Also doing the tracking of bucket "del" version during lookup we can
 * be sure, that state entry was not unlinked and freed in time between
 * we read the state pointer and protect it with hazard pointer.
 *
 * An entry unlinked from CK list keeps unchanged until it is freed.
 * Unlinked entries are linked into expired lists using "expired" field.
 */

/*
 * dyn_expire_lock is used to protect access to dyn_expired_xxx lists.
 * dyn_bucket_lock is used to get write access to lists in specific bucket.
 * Currently one dyn_bucket_lock is used for all ipv4, ipv4_parent, ipv6,
 * and ipv6_parent lists.
 */
VNET_DEFINE_STATIC(struct mtx, dyn_expire_lock);
VNET_DEFINE_STATIC(struct mtx *, dyn_bucket_lock);
#define	V_dyn_expire_lock		VNET(dyn_expire_lock)
#define	V_dyn_bucket_lock		VNET(dyn_bucket_lock)

/*
 * Bucket's add/delete generation versions.
 */
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv4_add);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv4_del);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv4_parent_add);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv4_parent_del);
#define	V_dyn_ipv4_add			VNET(dyn_ipv4_add)
#define	V_dyn_ipv4_del			VNET(dyn_ipv4_del)
#define	V_dyn_ipv4_parent_add		VNET(dyn_ipv4_parent_add)
#define	V_dyn_ipv4_parent_del		VNET(dyn_ipv4_parent_del)

#ifdef INET6
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv6_add);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv6_del);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv6_parent_add);
VNET_DEFINE_STATIC(uint32_t *, dyn_ipv6_parent_del);
#define	V_dyn_ipv6_add			VNET(dyn_ipv6_add)
#define	V_dyn_ipv6_del			VNET(dyn_ipv6_del)
#define	V_dyn_ipv6_parent_add		VNET(dyn_ipv6_parent_add)
#define	V_dyn_ipv6_parent_del		VNET(dyn_ipv6_parent_del)
#endif /* INET6 */

#define	DYN_BUCKET(h, b)		((h) & (b - 1))
#define	DYN_BUCKET_VERSION(b, v)	ck_pr_load_32(&V_dyn_ ## v[(b)])
#define	DYN_BUCKET_VERSION_BUMP(b, v)	ck_pr_inc_32(&V_dyn_ ## v[(b)])

#define	DYN_BUCKET_LOCK_INIT(lock, b)		\
    mtx_init(&lock[(b)], "IPFW dynamic bucket", NULL, MTX_DEF)
#define	DYN_BUCKET_LOCK_DESTROY(lock, b)	mtx_destroy(&lock[(b)])
#define	DYN_BUCKET_LOCK(b)	mtx_lock(&V_dyn_bucket_lock[(b)])
#define	DYN_BUCKET_UNLOCK(b)	mtx_unlock(&V_dyn_bucket_lock[(b)])
#define	DYN_BUCKET_ASSERT(b)	mtx_assert(&V_dyn_bucket_lock[(b)], MA_OWNED)

#define	DYN_EXPIRED_LOCK_INIT()		\
    mtx_init(&V_dyn_expire_lock, "IPFW expired states list", NULL, MTX_DEF)
#define	DYN_EXPIRED_LOCK_DESTROY()	mtx_destroy(&V_dyn_expire_lock)
#define	DYN_EXPIRED_LOCK()		mtx_lock(&V_dyn_expire_lock)
#define	DYN_EXPIRED_UNLOCK()		mtx_unlock(&V_dyn_expire_lock)

VNET_DEFINE_STATIC(uint32_t, dyn_buckets_max);
VNET_DEFINE_STATIC(uint32_t, curr_dyn_buckets);
VNET_DEFINE_STATIC(struct callout, dyn_timeout);
#define	V_dyn_buckets_max		VNET(dyn_buckets_max)
#define	V_curr_dyn_buckets		VNET(curr_dyn_buckets)
#define	V_dyn_timeout			VNET(dyn_timeout)

/* Maximum length of states chain in a bucket */
VNET_DEFINE_STATIC(uint32_t, curr_max_length);
#define	V_curr_max_length		VNET(curr_max_length)

VNET_DEFINE_STATIC(uint32_t, dyn_keep_states);
#define	V_dyn_keep_states		VNET(dyn_keep_states)

VNET_DEFINE_STATIC(uma_zone_t, dyn_data_zone);
VNET_DEFINE_STATIC(uma_zone_t, dyn_parent_zone);
VNET_DEFINE_STATIC(uma_zone_t, dyn_ipv4_zone);
#ifdef INET6
VNET_DEFINE_STATIC(uma_zone_t, dyn_ipv6_zone);
#define	V_dyn_ipv6_zone			VNET(dyn_ipv6_zone)
#endif /* INET6 */
#define	V_dyn_data_zone			VNET(dyn_data_zone)
#define	V_dyn_parent_zone		VNET(dyn_parent_zone)
#define	V_dyn_ipv4_zone			VNET(dyn_ipv4_zone)

/*
 * Timeouts for various events in handing dynamic rules.
 */
VNET_DEFINE_STATIC(uint32_t, dyn_ack_lifetime);
VNET_DEFINE_STATIC(uint32_t, dyn_syn_lifetime);
VNET_DEFINE_STATIC(uint32_t, dyn_fin_lifetime);
VNET_DEFINE_STATIC(uint32_t, dyn_rst_lifetime);
VNET_DEFINE_STATIC(uint32_t, dyn_udp_lifetime);
VNET_DEFINE_STATIC(uint32_t, dyn_short_lifetime);

#define	V_dyn_ack_lifetime		VNET(dyn_ack_lifetime)
#define	V_dyn_syn_lifetime		VNET(dyn_syn_lifetime)
#define	V_dyn_fin_lifetime		VNET(dyn_fin_lifetime)
#define	V_dyn_rst_lifetime		VNET(dyn_rst_lifetime)
#define	V_dyn_udp_lifetime		VNET(dyn_udp_lifetime)
#define	V_dyn_short_lifetime		VNET(dyn_short_lifetime)

/*
 * Keepalives are sent if dyn_keepalive is set. They are sent every
 * dyn_keepalive_period seconds, in the last dyn_keepalive_interval
 * seconds of lifetime of a rule.
 * dyn_rst_lifetime and dyn_fin_lifetime should be strictly lower
 * than dyn_keepalive_period.
 */
VNET_DEFINE_STATIC(uint32_t, dyn_keepalive_interval);
VNET_DEFINE_STATIC(uint32_t, dyn_keepalive_period);
VNET_DEFINE_STATIC(uint32_t, dyn_keepalive);
VNET_DEFINE_STATIC(time_t, dyn_keepalive_last);

#define	V_dyn_keepalive_interval	VNET(dyn_keepalive_interval)
#define	V_dyn_keepalive_period		VNET(dyn_keepalive_period)
#define	V_dyn_keepalive			VNET(dyn_keepalive)
#define	V_dyn_keepalive_last		VNET(dyn_keepalive_last)

VNET_DEFINE_STATIC(uint32_t, dyn_max);		/* max # of dynamic states */
VNET_DEFINE_STATIC(uint32_t, dyn_count);	/* number of states */
VNET_DEFINE_STATIC(uint32_t, dyn_parent_max);	/* max # of parent states */
VNET_DEFINE_STATIC(uint32_t, dyn_parent_count);	/* number of parent states */

#define	V_dyn_max			VNET(dyn_max)
#define	V_dyn_count			VNET(dyn_count)
#define	V_dyn_parent_max		VNET(dyn_parent_max)
#define	V_dyn_parent_count		VNET(dyn_parent_count)

#define	DYN_COUNT_DEC(name)	do {			\
	MPASS((V_ ## name) > 0);			\
	ck_pr_dec_32(&(V_ ## name));			\
} while (0)
#define	DYN_COUNT_INC(name)	ck_pr_inc_32(&(V_ ## name))
#define	DYN_COUNT(name)		ck_pr_load_32(&(V_ ## name))

static time_t last_log;	/* Log ratelimiting */

/*
 * Get/set maximum number of dynamic states in given VNET instance.
 */
static int
sysctl_dyn_max(SYSCTL_HANDLER_ARGS)
{
	uint32_t nstates;
	int error;

	nstates = V_dyn_max;
	error = sysctl_handle_32(oidp, &nstates, 0, req);
	/* Read operation or some error */
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	V_dyn_max = nstates;
	uma_zone_set_max(V_dyn_data_zone, V_dyn_max);
	return (0);
}

static int
sysctl_dyn_parent_max(SYSCTL_HANDLER_ARGS)
{
	uint32_t nstates;
	int error;

	nstates = V_dyn_parent_max;
	error = sysctl_handle_32(oidp, &nstates, 0, req);
	/* Read operation or some error */
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	V_dyn_parent_max = nstates;
	uma_zone_set_max(V_dyn_parent_zone, V_dyn_parent_max);
	return (0);
}

static int
sysctl_dyn_buckets(SYSCTL_HANDLER_ARGS)
{
	uint32_t nbuckets;
	int error;

	nbuckets = V_dyn_buckets_max;
	error = sysctl_handle_32(oidp, &nbuckets, 0, req);
	/* Read operation or some error */
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	if (nbuckets > 256)
		V_dyn_buckets_max = 1 << fls(nbuckets - 1);
	else
		return (EINVAL);
	return (0);
}

SYSCTL_DECL(_net_inet_ip_fw);

SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_count,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(dyn_count), 0,
    "Current number of dynamic states.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_parent_count,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(dyn_parent_count), 0,
    "Current number of parent states. ");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, curr_dyn_buckets,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(curr_dyn_buckets), 0,
    "Current number of buckets for states hash table.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, curr_max_length,
    CTLFLAG_VNET | CTLFLAG_RD, &VNET_NAME(curr_max_length), 0,
    "Current maximum length of states chains in hash buckets.");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_buckets,
    CTLFLAG_VNET | CTLTYPE_U32 | CTLFLAG_RW, 0, 0, sysctl_dyn_buckets,
    "IU", "Max number of buckets for dynamic states hash table.");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_max,
    CTLFLAG_VNET | CTLTYPE_U32 | CTLFLAG_RW, 0, 0, sysctl_dyn_max,
    "IU", "Max number of dynamic states.");
SYSCTL_PROC(_net_inet_ip_fw, OID_AUTO, dyn_parent_max,
    CTLFLAG_VNET | CTLTYPE_U32 | CTLFLAG_RW, 0, 0, sysctl_dyn_parent_max,
    "IU", "Max number of parent dynamic states.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_ack_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_ack_lifetime), 0,
    "Lifetime of dynamic states for TCP ACK.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_syn_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_syn_lifetime), 0,
    "Lifetime of dynamic states for TCP SYN.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_fin_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_fin_lifetime), 0,
    "Lifetime of dynamic states for TCP FIN.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_rst_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_rst_lifetime), 0,
    "Lifetime of dynamic states for TCP RST.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_udp_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_udp_lifetime), 0,
    "Lifetime of dynamic states for UDP.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_short_lifetime,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_short_lifetime), 0,
    "Lifetime of dynamic states for other situations.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_keepalive,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_keepalive), 0,
    "Enable keepalives for dynamic states.");
SYSCTL_U32(_net_inet_ip_fw, OID_AUTO, dyn_keep_states,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(dyn_keep_states), 0,
    "Do not flush dynamic states on rule deletion");


#ifdef IPFIREWALL_DYNDEBUG
#define	DYN_DEBUG(fmt, ...)	do {			\
	printf("%s: " fmt "\n", __func__, __VA_ARGS__);	\
} while (0)
#else
#define	DYN_DEBUG(fmt, ...)
#endif /* !IPFIREWALL_DYNDEBUG */

#ifdef INET6
/* Functions to work with IPv6 states */
static struct dyn_ipv6_state *dyn_lookup_ipv6_state(
    const struct ipfw_flow_id *, uint32_t, const void *,
    struct ipfw_dyn_info *, int);
static int dyn_lookup_ipv6_state_locked(const struct ipfw_flow_id *,
    uint32_t, const void *, int, uint32_t, uint16_t);
static struct dyn_ipv6_state *dyn_alloc_ipv6_state(
    const struct ipfw_flow_id *, uint32_t, uint16_t, uint8_t);
static int dyn_add_ipv6_state(void *, uint32_t, uint16_t,
    const struct ipfw_flow_id *, uint32_t, const void *, int, uint32_t,
    struct ipfw_dyn_info *, uint16_t, uint16_t, uint8_t);
static void dyn_export_ipv6_state(const struct dyn_ipv6_state *,
    ipfw_dyn_rule *);

static uint32_t dyn_getscopeid(const struct ip_fw_args *);
static void dyn_make_keepalive_ipv6(struct mbuf *, const struct in6_addr *,
    const struct in6_addr *, uint32_t, uint32_t, uint32_t, uint16_t,
    uint16_t);
static void dyn_enqueue_keepalive_ipv6(struct mbufq *,
    const struct dyn_ipv6_state *);
static void dyn_send_keepalive_ipv6(struct ip_fw_chain *);

static struct dyn_ipv6_state *dyn_lookup_ipv6_parent(
    const struct ipfw_flow_id *, uint32_t, const void *, uint32_t, uint16_t,
    uint32_t);
static struct dyn_ipv6_state *dyn_lookup_ipv6_parent_locked(
    const struct ipfw_flow_id *, uint32_t, const void *, uint32_t, uint16_t,
    uint32_t);
static struct dyn_ipv6_state *dyn_add_ipv6_parent(void *, uint32_t, uint16_t,
    const struct ipfw_flow_id *, uint32_t, uint32_t, uint32_t, uint16_t);
#endif /* INET6 */

/* Functions to work with limit states */
static void *dyn_get_parent_state(const struct ipfw_flow_id *, uint32_t,
    struct ip_fw *, uint32_t, uint32_t, uint16_t);
static struct dyn_ipv4_state *dyn_lookup_ipv4_parent(
    const struct ipfw_flow_id *, const void *, uint32_t, uint16_t, uint32_t);
static struct dyn_ipv4_state *dyn_lookup_ipv4_parent_locked(
    const struct ipfw_flow_id *, const void *, uint32_t, uint16_t, uint32_t);
static struct dyn_parent *dyn_alloc_parent(void *, uint32_t, uint16_t,
    uint32_t);
static struct dyn_ipv4_state *dyn_add_ipv4_parent(void *, uint32_t, uint16_t,
    const struct ipfw_flow_id *, uint32_t, uint32_t, uint16_t);

static void dyn_tick(void *);
static void dyn_expire_states(struct ip_fw_chain *, ipfw_range_tlv *);
static void dyn_free_states(struct ip_fw_chain *);
static void dyn_export_parent(const struct dyn_parent *, uint16_t, uint8_t,
    ipfw_dyn_rule *);
static void dyn_export_data(const struct dyn_data *, uint16_t, uint8_t,
    uint8_t, ipfw_dyn_rule *);
static uint32_t dyn_update_tcp_state(struct dyn_data *,
    const struct ipfw_flow_id *, const struct tcphdr *, int);
static void dyn_update_proto_state(struct dyn_data *,
    const struct ipfw_flow_id *, const void *, int, int);

/* Functions to work with IPv4 states */
struct dyn_ipv4_state *dyn_lookup_ipv4_state(const struct ipfw_flow_id *,
    const void *, struct ipfw_dyn_info *, int);
static int dyn_lookup_ipv4_state_locked(const struct ipfw_flow_id *,
    const void *, int, uint32_t, uint16_t);
static struct dyn_ipv4_state *dyn_alloc_ipv4_state(
    const struct ipfw_flow_id *, uint16_t, uint8_t);
static int dyn_add_ipv4_state(void *, uint32_t, uint16_t,
    const struct ipfw_flow_id *, const void *, int, uint32_t,
    struct ipfw_dyn_info *, uint16_t, uint16_t, uint8_t);
static void dyn_export_ipv4_state(const struct dyn_ipv4_state *,
    ipfw_dyn_rule *);

/*
 * Named states support.
 */
static char *default_state_name = "default";
struct dyn_state_obj {
	struct named_object	no;
	char			name[64];
};

#define	DYN_STATE_OBJ(ch, cmd)	\
    ((struct dyn_state_obj *)SRV_OBJECT(ch, (cmd)->arg1))
/*
 * Classifier callback.
 * Return 0 if opcode contains object that should be referenced
 * or rewritten.
 */
static int
dyn_classify(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{

	DYN_DEBUG("opcode %d, arg1 %d", cmd->opcode, cmd->arg1);
	/* Don't rewrite "check-state any" */
	if (cmd->arg1 == 0 &&
	    cmd->opcode == O_CHECK_STATE)
		return (1);

	*puidx = cmd->arg1;
	*ptype = 0;
	return (0);
}

static void
dyn_update(ipfw_insn *cmd, uint16_t idx)
{

	cmd->arg1 = idx;
	DYN_DEBUG("opcode %d, arg1 %d", cmd->opcode, cmd->arg1);
}

static int
dyn_findbyname(struct ip_fw_chain *ch, struct tid_info *ti,
    struct named_object **pno)
{
	ipfw_obj_ntlv *ntlv;
	const char *name;

	DYN_DEBUG("uidx %d", ti->uidx);
	if (ti->uidx != 0) {
		if (ti->tlvs == NULL)
			return (EINVAL);
		/* Search ntlv in the buffer provided by user */
		ntlv = ipfw_find_name_tlv_type(ti->tlvs, ti->tlen, ti->uidx,
		    IPFW_TLV_STATE_NAME);
		if (ntlv == NULL)
			return (EINVAL);
		name = ntlv->name;
	} else
		name = default_state_name;
	/*
	 * Search named object with corresponding name.
	 * Since states objects are global - ignore the set value
	 * and use zero instead.
	 */
	*pno = ipfw_objhash_lookup_name_type(CHAIN_TO_SRV(ch), 0,
	    IPFW_TLV_STATE_NAME, name);
	/*
	 * We always return success here.
	 * The caller will check *pno and mark object as unresolved,
	 * then it will automatically create "default" object.
	 */
	return (0);
}

static struct named_object *
dyn_findbykidx(struct ip_fw_chain *ch, uint16_t idx)
{

	DYN_DEBUG("kidx %d", idx);
	return (ipfw_objhash_lookup_kidx(CHAIN_TO_SRV(ch), idx));
}

static int
dyn_create(struct ip_fw_chain *ch, struct tid_info *ti,
    uint16_t *pkidx)
{
	struct namedobj_instance *ni;
	struct dyn_state_obj *obj;
	struct named_object *no;
	ipfw_obj_ntlv *ntlv;
	char *name;

	DYN_DEBUG("uidx %d", ti->uidx);
	if (ti->uidx != 0) {
		if (ti->tlvs == NULL)
			return (EINVAL);
		ntlv = ipfw_find_name_tlv_type(ti->tlvs, ti->tlen, ti->uidx,
		    IPFW_TLV_STATE_NAME);
		if (ntlv == NULL)
			return (EINVAL);
		name = ntlv->name;
	} else
		name = default_state_name;

	ni = CHAIN_TO_SRV(ch);
	obj = malloc(sizeof(*obj), M_IPFW, M_WAITOK | M_ZERO);
	obj->no.name = obj->name;
	obj->no.etlv = IPFW_TLV_STATE_NAME;
	strlcpy(obj->name, name, sizeof(obj->name));

	IPFW_UH_WLOCK(ch);
	no = ipfw_objhash_lookup_name_type(ni, 0,
	    IPFW_TLV_STATE_NAME, name);
	if (no != NULL) {
		/*
		 * Object is already created.
		 * Just return its kidx and bump refcount.
		 */
		*pkidx = no->kidx;
		no->refcnt++;
		IPFW_UH_WUNLOCK(ch);
		free(obj, M_IPFW);
		DYN_DEBUG("\tfound kidx %d", *pkidx);
		return (0);
	}
	if (ipfw_objhash_alloc_idx(ni, &obj->no.kidx) != 0) {
		DYN_DEBUG("\talloc_idx failed for %s", name);
		IPFW_UH_WUNLOCK(ch);
		free(obj, M_IPFW);
		return (ENOSPC);
	}
	ipfw_objhash_add(ni, &obj->no);
	SRV_OBJECT(ch, obj->no.kidx) = obj;
	obj->no.refcnt++;
	*pkidx = obj->no.kidx;
	IPFW_UH_WUNLOCK(ch);
	DYN_DEBUG("\tcreated kidx %d", *pkidx);
	return (0);
}

static void
dyn_destroy(struct ip_fw_chain *ch, struct named_object *no)
{
	struct dyn_state_obj *obj;

	IPFW_UH_WLOCK_ASSERT(ch);

	KASSERT(no->etlv == IPFW_TLV_STATE_NAME,
	    ("%s: wrong object type %u", __func__, no->etlv));
	KASSERT(no->refcnt == 1,
	    ("Destroying object '%s' (type %u, idx %u) with refcnt %u",
	    no->name, no->etlv, no->kidx, no->refcnt));
	DYN_DEBUG("kidx %d", no->kidx);
	obj = SRV_OBJECT(ch, no->kidx);
	SRV_OBJECT(ch, no->kidx) = NULL;
	ipfw_objhash_del(CHAIN_TO_SRV(ch), no);
	ipfw_objhash_free_idx(CHAIN_TO_SRV(ch), no->kidx);

	free(obj, M_IPFW);
}

static struct opcode_obj_rewrite dyn_opcodes[] = {
	{
		O_KEEP_STATE, IPFW_TLV_STATE_NAME,
		dyn_classify, dyn_update,
		dyn_findbyname, dyn_findbykidx,
		dyn_create, dyn_destroy
	},
	{
		O_CHECK_STATE, IPFW_TLV_STATE_NAME,
		dyn_classify, dyn_update,
		dyn_findbyname, dyn_findbykidx,
		dyn_create, dyn_destroy
	},
	{
		O_PROBE_STATE, IPFW_TLV_STATE_NAME,
		dyn_classify, dyn_update,
		dyn_findbyname, dyn_findbykidx,
		dyn_create, dyn_destroy
	},
	{
		O_LIMIT, IPFW_TLV_STATE_NAME,
		dyn_classify, dyn_update,
		dyn_findbyname, dyn_findbykidx,
		dyn_create, dyn_destroy
	},
};

/*
 * IMPORTANT: the hash function for dynamic rules must be commutative
 * in source and destination (ip,port), because rules are bidirectional
 * and we want to find both in the same bucket.
 */
#ifndef IPFIREWALL_JENKINSHASH
static __inline uint32_t
hash_packet(const struct ipfw_flow_id *id)
{
	uint32_t i;

#ifdef INET6
	if (IS_IP6_FLOW_ID(id))
		i = ntohl((id->dst_ip6.__u6_addr.__u6_addr32[2]) ^
		    (id->dst_ip6.__u6_addr.__u6_addr32[3]) ^
		    (id->src_ip6.__u6_addr.__u6_addr32[2]) ^
		    (id->src_ip6.__u6_addr.__u6_addr32[3]));
	else
#endif /* INET6 */
	i = (id->dst_ip) ^ (id->src_ip);
	i ^= (id->dst_port) ^ (id->src_port);
	return (i);
}

static __inline uint32_t
hash_parent(const struct ipfw_flow_id *id, const void *rule)
{

	return (hash_packet(id) ^ ((uintptr_t)rule));
}

#else /* IPFIREWALL_JENKINSHASH */

VNET_DEFINE_STATIC(uint32_t, dyn_hashseed);
#define	V_dyn_hashseed		VNET(dyn_hashseed)

static __inline int
addrcmp4(const struct ipfw_flow_id *id)
{

	if (id->src_ip < id->dst_ip)
		return (0);
	if (id->src_ip > id->dst_ip)
		return (1);
	if (id->src_port <= id->dst_port)
		return (0);
	return (1);
}

#ifdef INET6
static __inline int
addrcmp6(const struct ipfw_flow_id *id)
{
	int ret;

	ret = memcmp(&id->src_ip6, &id->dst_ip6, sizeof(struct in6_addr));
	if (ret < 0)
		return (0);
	if (ret > 0)
		return (1);
	if (id->src_port <= id->dst_port)
		return (0);
	return (1);
}

static __inline uint32_t
hash_packet6(const struct ipfw_flow_id *id)
{
	struct tuple6 {
		struct in6_addr	addr[2];
		uint16_t	port[2];
	} t6;

	if (addrcmp6(id) == 0) {
		t6.addr[0] = id->src_ip6;
		t6.addr[1] = id->dst_ip6;
		t6.port[0] = id->src_port;
		t6.port[1] = id->dst_port;
	} else {
		t6.addr[0] = id->dst_ip6;
		t6.addr[1] = id->src_ip6;
		t6.port[0] = id->dst_port;
		t6.port[1] = id->src_port;
	}
	return (jenkins_hash32((const uint32_t *)&t6,
	    sizeof(t6) / sizeof(uint32_t), V_dyn_hashseed));
}
#endif

static __inline uint32_t
hash_packet(const struct ipfw_flow_id *id)
{
	struct tuple4 {
		in_addr_t	addr[2];
		uint16_t	port[2];
	} t4;

	if (IS_IP4_FLOW_ID(id)) {
		/* All fields are in host byte order */
		if (addrcmp4(id) == 0) {
			t4.addr[0] = id->src_ip;
			t4.addr[1] = id->dst_ip;
			t4.port[0] = id->src_port;
			t4.port[1] = id->dst_port;
		} else {
			t4.addr[0] = id->dst_ip;
			t4.addr[1] = id->src_ip;
			t4.port[0] = id->dst_port;
			t4.port[1] = id->src_port;
		}
		return (jenkins_hash32((const uint32_t *)&t4,
		    sizeof(t4) / sizeof(uint32_t), V_dyn_hashseed));
	} else
#ifdef INET6
	if (IS_IP6_FLOW_ID(id))
		return (hash_packet6(id));
#endif
	return (0);
}

static __inline uint32_t
hash_parent(const struct ipfw_flow_id *id, const void *rule)
{

	return (jenkins_hash32((const uint32_t *)&rule,
	    sizeof(rule) / sizeof(uint32_t), hash_packet(id)));
}
#endif /* IPFIREWALL_JENKINSHASH */

/*
 * Print customizable flow id description via log(9) facility.
 */
static void
print_dyn_rule_flags(const struct ipfw_flow_id *id, int dyn_type,
    int log_flags, char *prefix, char *postfix)
{
	struct in_addr da;
#ifdef INET6
	char src[INET6_ADDRSTRLEN], dst[INET6_ADDRSTRLEN];
#else
	char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
#endif

#ifdef INET6
	if (IS_IP6_FLOW_ID(id)) {
		ip6_sprintf(src, &id->src_ip6);
		ip6_sprintf(dst, &id->dst_ip6);
	} else
#endif
	{
		da.s_addr = htonl(id->src_ip);
		inet_ntop(AF_INET, &da, src, sizeof(src));
		da.s_addr = htonl(id->dst_ip);
		inet_ntop(AF_INET, &da, dst, sizeof(dst));
	}
	log(log_flags, "ipfw: %s type %d %s %d -> %s %d, %d %s\n",
	    prefix, dyn_type, src, id->src_port, dst,
	    id->dst_port, V_dyn_count, postfix);
}

#define	print_dyn_rule(id, dtype, prefix, postfix)	\
	print_dyn_rule_flags(id, dtype, LOG_DEBUG, prefix, postfix)

#define	TIME_LEQ(a,b)	((int)((a)-(b)) <= 0)
#define	TIME_LE(a,b)	((int)((a)-(b)) < 0)
#define	_SEQ_GE(a,b)	((int)((a)-(b)) >= 0)
#define	BOTH_SYN	(TH_SYN | (TH_SYN << 8))
#define	BOTH_FIN	(TH_FIN | (TH_FIN << 8))
#define	TCP_FLAGS	(TH_FLAGS | (TH_FLAGS << 8))
#define	ACK_FWD		0x00010000	/* fwd ack seen */
#define	ACK_REV		0x00020000	/* rev ack seen */
#define	ACK_BOTH	(ACK_FWD | ACK_REV)

static uint32_t
dyn_update_tcp_state(struct dyn_data *data, const struct ipfw_flow_id *pkt,
    const struct tcphdr *tcp, int dir)
{
	uint32_t ack, expire;
	uint32_t state, old;
	uint8_t th_flags;

	expire = data->expire;
	old = state = data->state;
	th_flags = pkt->_flags & (TH_FIN | TH_SYN | TH_RST);
	state |= (dir == MATCH_FORWARD) ? th_flags: (th_flags << 8);
	switch (state & TCP_FLAGS) {
	case TH_SYN:			/* opening */
		expire = time_uptime + V_dyn_syn_lifetime;
		break;

	case BOTH_SYN:			/* move to established */
	case BOTH_SYN | TH_FIN:		/* one side tries to close */
	case BOTH_SYN | (TH_FIN << 8):
		if (tcp == NULL)
			break;
		ack = ntohl(tcp->th_ack);
		if (dir == MATCH_FORWARD) {
			if (data->ack_fwd == 0 ||
			    _SEQ_GE(ack, data->ack_fwd)) {
				state |= ACK_FWD;
				if (data->ack_fwd != ack)
					ck_pr_store_32(&data->ack_fwd, ack);
			}
		} else {
			if (data->ack_rev == 0 ||
			    _SEQ_GE(ack, data->ack_rev)) {
				state |= ACK_REV;
				if (data->ack_rev != ack)
					ck_pr_store_32(&data->ack_rev, ack);
			}
		}
		if ((state & ACK_BOTH) == ACK_BOTH) {
			/*
			 * Set expire time to V_dyn_ack_lifetime only if
			 * we got ACKs for both directions.
			 * We use XOR here to avoid possible state
			 * overwriting in concurrent thread.
			 */
			expire = time_uptime + V_dyn_ack_lifetime;
			ck_pr_xor_32(&data->state, ACK_BOTH);
		} else if ((data->state & ACK_BOTH) != (state & ACK_BOTH))
			ck_pr_or_32(&data->state, state & ACK_BOTH);
		break;

	case BOTH_SYN | BOTH_FIN:	/* both sides closed */
		if (V_dyn_fin_lifetime >= V_dyn_keepalive_period)
			V_dyn_fin_lifetime = V_dyn_keepalive_period - 1;
		expire = time_uptime + V_dyn_fin_lifetime;
		break;

	default:
		if (V_dyn_keepalive != 0 &&
		    V_dyn_rst_lifetime >= V_dyn_keepalive_period)
			V_dyn_rst_lifetime = V_dyn_keepalive_period - 1;
		expire = time_uptime + V_dyn_rst_lifetime;
	}
	/* Save TCP state if it was changed */
	if ((state & TCP_FLAGS) != (old & TCP_FLAGS))
		ck_pr_or_32(&data->state, state & TCP_FLAGS);
	return (expire);
}

/*
 * Update ULP specific state.
 * For TCP we keep sequence numbers and flags. For other protocols
 * currently we update only expire time. Packets and bytes counters
 * are also updated here.
 */
static void
dyn_update_proto_state(struct dyn_data *data, const struct ipfw_flow_id *pkt,
    const void *ulp, int pktlen, int dir)
{
	uint32_t expire;

	/* NOTE: we are in critical section here. */
	switch (pkt->proto) {
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		expire = time_uptime + V_dyn_udp_lifetime;
		break;
	case IPPROTO_TCP:
		expire = dyn_update_tcp_state(data, pkt, ulp, dir);
		break;
	default:
		expire = time_uptime + V_dyn_short_lifetime;
	}
	/*
	 * Expiration timer has the per-second granularity, no need to update
	 * it every time when state is matched.
	 */
	if (data->expire != expire)
		ck_pr_store_32(&data->expire, expire);

	if (dir == MATCH_FORWARD)
		DYN_COUNTER_INC(data, fwd, pktlen);
	else
		DYN_COUNTER_INC(data, rev, pktlen);
}

/*
 * Lookup IPv4 state.
 * Must be called in critical section.
 */
struct dyn_ipv4_state *
dyn_lookup_ipv4_state(const struct ipfw_flow_id *pkt, const void *ulp,
    struct ipfw_dyn_info *info, int pktlen)
{
	struct dyn_ipv4_state *s;
	uint32_t version, bucket;

	bucket = DYN_BUCKET(info->hashval, V_curr_dyn_buckets);
	info->version = DYN_BUCKET_VERSION(bucket, ipv4_add);
restart:
	version = DYN_BUCKET_VERSION(bucket, ipv4_del);
	CK_SLIST_FOREACH(s, &V_dyn_ipv4[bucket], entry) {
		DYNSTATE_PROTECT(s);
		if (version != DYN_BUCKET_VERSION(bucket, ipv4_del))
			goto restart;
		if (s->proto != pkt->proto)
			continue;
		if (info->kidx != 0 && s->kidx != info->kidx)
			continue;
		if (s->sport == pkt->src_port && s->dport == pkt->dst_port &&
		    s->src == pkt->src_ip && s->dst == pkt->dst_ip) {
			info->direction = MATCH_FORWARD;
			break;
		}
		if (s->sport == pkt->dst_port && s->dport == pkt->src_port &&
		    s->src == pkt->dst_ip && s->dst == pkt->src_ip) {
			info->direction = MATCH_REVERSE;
			break;
		}
	}

	if (s != NULL)
		dyn_update_proto_state(s->data, pkt, ulp, pktlen,
		    info->direction);
	return (s);
}

/*
 * Lookup IPv4 state.
 * Simplifed version is used to check that matching state doesn't exist.
 */
static int
dyn_lookup_ipv4_state_locked(const struct ipfw_flow_id *pkt,
    const void *ulp, int pktlen, uint32_t bucket, uint16_t kidx)
{
	struct dyn_ipv4_state *s;
	int dir;

	dir = MATCH_NONE;
	DYN_BUCKET_ASSERT(bucket);
	CK_SLIST_FOREACH(s, &V_dyn_ipv4[bucket], entry) {
		if (s->proto != pkt->proto ||
		    s->kidx != kidx)
			continue;
		if (s->sport == pkt->src_port &&
		    s->dport == pkt->dst_port &&
		    s->src == pkt->src_ip && s->dst == pkt->dst_ip) {
			dir = MATCH_FORWARD;
			break;
		}
		if (s->sport == pkt->dst_port && s->dport == pkt->src_port &&
		    s->src == pkt->dst_ip && s->dst == pkt->src_ip) {
			dir = MATCH_REVERSE;
			break;
		}
	}
	if (s != NULL)
		dyn_update_proto_state(s->data, pkt, ulp, pktlen, dir);
	return (s != NULL);
}

struct dyn_ipv4_state *
dyn_lookup_ipv4_parent(const struct ipfw_flow_id *pkt, const void *rule,
    uint32_t ruleid, uint16_t rulenum, uint32_t hashval)
{
	struct dyn_ipv4_state *s;
	uint32_t version, bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
restart:
	version = DYN_BUCKET_VERSION(bucket, ipv4_parent_del);
	CK_SLIST_FOREACH(s, &V_dyn_ipv4_parent[bucket], entry) {
		DYNSTATE_PROTECT(s);
		if (version != DYN_BUCKET_VERSION(bucket, ipv4_parent_del))
			goto restart;
		/*
		 * NOTE: we do not need to check kidx, because parent rule
		 * can not create states with different kidx.
		 * And parent rule always created for forward direction.
		 */
		if (s->limit->parent == rule &&
		    s->limit->ruleid == ruleid &&
		    s->limit->rulenum == rulenum &&
		    s->proto == pkt->proto &&
		    s->sport == pkt->src_port &&
		    s->dport == pkt->dst_port &&
		    s->src == pkt->src_ip && s->dst == pkt->dst_ip) {
			if (s->limit->expire != time_uptime +
			    V_dyn_short_lifetime)
				ck_pr_store_32(&s->limit->expire,
				    time_uptime + V_dyn_short_lifetime);
			break;
		}
	}
	return (s);
}

static struct dyn_ipv4_state *
dyn_lookup_ipv4_parent_locked(const struct ipfw_flow_id *pkt,
    const void *rule, uint32_t ruleid, uint16_t rulenum, uint32_t bucket)
{
	struct dyn_ipv4_state *s;

	DYN_BUCKET_ASSERT(bucket);
	CK_SLIST_FOREACH(s, &V_dyn_ipv4_parent[bucket], entry) {
		if (s->limit->parent == rule &&
		    s->limit->ruleid == ruleid &&
		    s->limit->rulenum == rulenum &&
		    s->proto == pkt->proto &&
		    s->sport == pkt->src_port &&
		    s->dport == pkt->dst_port &&
		    s->src == pkt->src_ip && s->dst == pkt->dst_ip)
			break;
	}
	return (s);
}


#ifdef INET6
static uint32_t
dyn_getscopeid(const struct ip_fw_args *args)
{

	/*
	 * If source or destination address is an scopeid address, we need
	 * determine the scope zone id to resolve address scope ambiguity.
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&args->f_id.src_ip6) ||
	    IN6_IS_ADDR_LINKLOCAL(&args->f_id.dst_ip6))
		return (in6_getscopezone(args->ifp, IPV6_ADDR_SCOPE_LINKLOCAL));

	return (0);
}

/*
 * Lookup IPv6 state.
 * Must be called in critical section.
 */
static struct dyn_ipv6_state *
dyn_lookup_ipv6_state(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    const void *ulp, struct ipfw_dyn_info *info, int pktlen)
{
	struct dyn_ipv6_state *s;
	uint32_t version, bucket;

	bucket = DYN_BUCKET(info->hashval, V_curr_dyn_buckets);
	info->version = DYN_BUCKET_VERSION(bucket, ipv6_add);
restart:
	version = DYN_BUCKET_VERSION(bucket, ipv6_del);
	CK_SLIST_FOREACH(s, &V_dyn_ipv6[bucket], entry) {
		DYNSTATE_PROTECT(s);
		if (version != DYN_BUCKET_VERSION(bucket, ipv6_del))
			goto restart;
		if (s->proto != pkt->proto || s->zoneid != zoneid)
			continue;
		if (info->kidx != 0 && s->kidx != info->kidx)
			continue;
		if (s->sport == pkt->src_port && s->dport == pkt->dst_port &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->src_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->dst_ip6)) {
			info->direction = MATCH_FORWARD;
			break;
		}
		if (s->sport == pkt->dst_port && s->dport == pkt->src_port &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->dst_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->src_ip6)) {
			info->direction = MATCH_REVERSE;
			break;
		}
	}
	if (s != NULL)
		dyn_update_proto_state(s->data, pkt, ulp, pktlen,
		    info->direction);
	return (s);
}

/*
 * Lookup IPv6 state.
 * Simplifed version is used to check that matching state doesn't exist.
 */
static int
dyn_lookup_ipv6_state_locked(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    const void *ulp, int pktlen, uint32_t bucket, uint16_t kidx)
{
	struct dyn_ipv6_state *s;
	int dir;

	dir = MATCH_NONE;
	DYN_BUCKET_ASSERT(bucket);
	CK_SLIST_FOREACH(s, &V_dyn_ipv6[bucket], entry) {
		if (s->proto != pkt->proto || s->kidx != kidx ||
		    s->zoneid != zoneid)
			continue;
		if (s->sport == pkt->src_port && s->dport == pkt->dst_port &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->src_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->dst_ip6)) {
			dir = MATCH_FORWARD;
			break;
		}
		if (s->sport == pkt->dst_port && s->dport == pkt->src_port &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->dst_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->src_ip6)) {
			dir = MATCH_REVERSE;
			break;
		}
	}
	if (s != NULL)
		dyn_update_proto_state(s->data, pkt, ulp, pktlen, dir);
	return (s != NULL);
}

static struct dyn_ipv6_state *
dyn_lookup_ipv6_parent(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    const void *rule, uint32_t ruleid, uint16_t rulenum, uint32_t hashval)
{
	struct dyn_ipv6_state *s;
	uint32_t version, bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
restart:
	version = DYN_BUCKET_VERSION(bucket, ipv6_parent_del);
	CK_SLIST_FOREACH(s, &V_dyn_ipv6_parent[bucket], entry) {
		DYNSTATE_PROTECT(s);
		if (version != DYN_BUCKET_VERSION(bucket, ipv6_parent_del))
			goto restart;
		/*
		 * NOTE: we do not need to check kidx, because parent rule
		 * can not create states with different kidx.
		 * Also parent rule always created for forward direction.
		 */
		if (s->limit->parent == rule &&
		    s->limit->ruleid == ruleid &&
		    s->limit->rulenum == rulenum &&
		    s->proto == pkt->proto &&
		    s->sport == pkt->src_port &&
		    s->dport == pkt->dst_port && s->zoneid == zoneid &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->src_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->dst_ip6)) {
			if (s->limit->expire != time_uptime +
			    V_dyn_short_lifetime)
				ck_pr_store_32(&s->limit->expire,
				    time_uptime + V_dyn_short_lifetime);
			break;
		}
	}
	return (s);
}

static struct dyn_ipv6_state *
dyn_lookup_ipv6_parent_locked(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    const void *rule, uint32_t ruleid, uint16_t rulenum, uint32_t bucket)
{
	struct dyn_ipv6_state *s;

	DYN_BUCKET_ASSERT(bucket);
	CK_SLIST_FOREACH(s, &V_dyn_ipv6_parent[bucket], entry) {
		if (s->limit->parent == rule &&
		    s->limit->ruleid == ruleid &&
		    s->limit->rulenum == rulenum &&
		    s->proto == pkt->proto &&
		    s->sport == pkt->src_port &&
		    s->dport == pkt->dst_port && s->zoneid == zoneid &&
		    IN6_ARE_ADDR_EQUAL(&s->src, &pkt->src_ip6) &&
		    IN6_ARE_ADDR_EQUAL(&s->dst, &pkt->dst_ip6))
			break;
	}
	return (s);
}

#endif /* INET6 */

/*
 * Lookup dynamic state.
 *  pkt - filled by ipfw_chk() ipfw_flow_id;
 *  ulp - determined by ipfw_chk() upper level protocol header;
 *  dyn_info - info about matched state to return back;
 * Returns pointer to state's parent rule and dyn_info. If there is
 * no state, NULL is returned.
 * On match ipfw_dyn_lookup() updates state's counters.
 */
struct ip_fw *
ipfw_dyn_lookup_state(const struct ip_fw_args *args, const void *ulp,
    int pktlen, const ipfw_insn *cmd, struct ipfw_dyn_info *info)
{
	struct dyn_data *data;
	struct ip_fw *rule;

	IPFW_RLOCK_ASSERT(&V_layer3_chain);

	data = NULL;
	rule = NULL;
	info->kidx = cmd->arg1;
	info->direction = MATCH_NONE;
	info->hashval = hash_packet(&args->f_id);

	DYNSTATE_CRITICAL_ENTER();
	if (IS_IP4_FLOW_ID(&args->f_id)) {
		struct dyn_ipv4_state *s;

		s = dyn_lookup_ipv4_state(&args->f_id, ulp, info, pktlen);
		if (s != NULL) {
			/*
			 * Dynamic states are created using the same 5-tuple,
			 * so it is assumed, that parent rule for O_LIMIT
			 * state has the same address family.
			 */
			data = s->data;
			if (s->type == O_LIMIT) {
				s = data->parent;
				rule = s->limit->parent;
			} else
				rule = data->parent;
		}
	}
#ifdef INET6
	else if (IS_IP6_FLOW_ID(&args->f_id)) {
		struct dyn_ipv6_state *s;

		s = dyn_lookup_ipv6_state(&args->f_id, dyn_getscopeid(args),
		    ulp, info, pktlen);
		if (s != NULL) {
			data = s->data;
			if (s->type == O_LIMIT) {
				s = data->parent;
				rule = s->limit->parent;
			} else
				rule = data->parent;
		}
	}
#endif
	if (data != NULL) {
		/*
		 * If cached chain id is the same, we can avoid rule index
		 * lookup. Otherwise do lookup and update chain_id and f_pos.
		 * It is safe even if there is concurrent thread that want
		 * update the same state, because chain->id can be changed
		 * only under IPFW_WLOCK().
		 */
		if (data->chain_id != V_layer3_chain.id) {
			data->f_pos = ipfw_find_rule(&V_layer3_chain,
			    data->rulenum, data->ruleid);
			/*
			 * Check that found state has not orphaned.
			 * When chain->id being changed the parent
			 * rule can be deleted. If found rule doesn't
			 * match the parent pointer, consider this
			 * result as MATCH_NONE and return NULL.
			 *
			 * This will lead to creation of new similar state
			 * that will be added into head of this bucket.
			 * And the state that we currently have matched
			 * should be deleted by dyn_expire_states().
			 *
			 * In case when dyn_keep_states is enabled, return
			 * pointer to deleted rule and f_pos value
			 * corresponding to penultimate rule.
			 * When we have enabled V_dyn_keep_states, states
			 * that become orphaned will get the DYN_REFERENCED
			 * flag and rule will keep around. So we can return
			 * it. But since it is not in the rules map, we need
			 * return such f_pos value, so after the state
			 * handling if the search will continue, the next rule
			 * will be the last one - the default rule.
			 */
			if (V_layer3_chain.map[data->f_pos] == rule) {
				data->chain_id = V_layer3_chain.id;
				info->f_pos = data->f_pos;
			} else if (V_dyn_keep_states != 0) {
				/*
				 * The original rule pointer is still usable.
				 * So, we return it, but f_pos need to be
				 * changed to point to the penultimate rule.
				 */
				MPASS(V_layer3_chain.n_rules > 1);
				data->chain_id = V_layer3_chain.id;
				data->f_pos = V_layer3_chain.n_rules - 2;
				info->f_pos = data->f_pos;
			} else {
				rule = NULL;
				info->direction = MATCH_NONE;
				DYN_DEBUG("rule %p  [%u, %u] is considered "
				    "invalid in data %p", rule, data->ruleid,
				    data->rulenum, data);
				/* info->f_pos doesn't matter here. */
			}
		} else
			info->f_pos = data->f_pos;
	}
	DYNSTATE_CRITICAL_EXIT();
#if 0
	/*
	 * Return MATCH_NONE if parent rule is in disabled set.
	 * This will lead to creation of new similar state that
	 * will be added into head of this bucket.
	 *
	 * XXXAE: we need to be able update state's set when parent
	 *	  rule set is changed.
	 */
	if (rule != NULL && (V_set_disable & (1 << rule->set))) {
		rule = NULL;
		info->direction = MATCH_NONE;
	}
#endif
	return (rule);
}

static struct dyn_parent *
dyn_alloc_parent(void *parent, uint32_t ruleid, uint16_t rulenum,
    uint32_t hashval)
{
	struct dyn_parent *limit;

	limit = uma_zalloc(V_dyn_parent_zone, M_NOWAIT | M_ZERO);
	if (limit == NULL) {
		if (last_log != time_uptime) {
			last_log = time_uptime;
			log(LOG_DEBUG,
			    "ipfw: Cannot allocate parent dynamic state, "
			    "consider increasing "
			    "net.inet.ip.fw.dyn_parent_max\n");
		}
		return (NULL);
	}

	limit->parent = parent;
	limit->ruleid = ruleid;
	limit->rulenum = rulenum;
	limit->hashval = hashval;
	limit->expire = time_uptime + V_dyn_short_lifetime;
	return (limit);
}

static struct dyn_data *
dyn_alloc_dyndata(void *parent, uint32_t ruleid, uint16_t rulenum,
    const struct ipfw_flow_id *pkt, const void *ulp, int pktlen,
    uint32_t hashval, uint16_t fibnum)
{
	struct dyn_data *data;

	data = uma_zalloc(V_dyn_data_zone, M_NOWAIT | M_ZERO);
	if (data == NULL) {
		if (last_log != time_uptime) {
			last_log = time_uptime;
			log(LOG_DEBUG,
			    "ipfw: Cannot allocate dynamic state, "
			    "consider increasing net.inet.ip.fw.dyn_max\n");
		}
		return (NULL);
	}

	data->parent = parent;
	data->ruleid = ruleid;
	data->rulenum = rulenum;
	data->fibnum = fibnum;
	data->hashval = hashval;
	data->expire = time_uptime + V_dyn_syn_lifetime;
	dyn_update_proto_state(data, pkt, ulp, pktlen, MATCH_FORWARD);
	return (data);
}

static struct dyn_ipv4_state *
dyn_alloc_ipv4_state(const struct ipfw_flow_id *pkt, uint16_t kidx,
    uint8_t type)
{
	struct dyn_ipv4_state *s;

	s = uma_zalloc(V_dyn_ipv4_zone, M_NOWAIT | M_ZERO);
	if (s == NULL)
		return (NULL);

	s->type = type;
	s->kidx = kidx;
	s->proto = pkt->proto;
	s->sport = pkt->src_port;
	s->dport = pkt->dst_port;
	s->src = pkt->src_ip;
	s->dst = pkt->dst_ip;
	return (s);
}

/*
 * Add IPv4 parent state.
 * Returns pointer to parent state. When it is not NULL we are in
 * critical section and pointer protected by hazard pointer.
 * When some error occurs, it returns NULL and exit from critical section
 * is not needed.
 */
static struct dyn_ipv4_state *
dyn_add_ipv4_parent(void *rule, uint32_t ruleid, uint16_t rulenum,
    const struct ipfw_flow_id *pkt, uint32_t hashval, uint32_t version,
    uint16_t kidx)
{
	struct dyn_ipv4_state *s;
	struct dyn_parent *limit;
	uint32_t bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
	DYN_BUCKET_LOCK(bucket);
	if (version != DYN_BUCKET_VERSION(bucket, ipv4_parent_add)) {
		/*
		 * Bucket version has been changed since last lookup,
		 * do lookup again to be sure that state does not exist.
		 */
		s = dyn_lookup_ipv4_parent_locked(pkt, rule, ruleid,
		    rulenum, bucket);
		if (s != NULL) {
			/*
			 * Simultaneous thread has already created this
			 * state. Just return it.
			 */
			DYNSTATE_CRITICAL_ENTER();
			DYNSTATE_PROTECT(s);
			DYN_BUCKET_UNLOCK(bucket);
			return (s);
		}
	}

	limit = dyn_alloc_parent(rule, ruleid, rulenum, hashval);
	if (limit == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		return (NULL);
	}

	s = dyn_alloc_ipv4_state(pkt, kidx, O_LIMIT_PARENT);
	if (s == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		uma_zfree(V_dyn_parent_zone, limit);
		return (NULL);
	}

	s->limit = limit;
	CK_SLIST_INSERT_HEAD(&V_dyn_ipv4_parent[bucket], s, entry);
	DYN_COUNT_INC(dyn_parent_count);
	DYN_BUCKET_VERSION_BUMP(bucket, ipv4_parent_add);
	DYNSTATE_CRITICAL_ENTER();
	DYNSTATE_PROTECT(s);
	DYN_BUCKET_UNLOCK(bucket);
	return (s);
}

static int
dyn_add_ipv4_state(void *parent, uint32_t ruleid, uint16_t rulenum,
    const struct ipfw_flow_id *pkt, const void *ulp, int pktlen,
    uint32_t hashval, struct ipfw_dyn_info *info, uint16_t fibnum,
    uint16_t kidx, uint8_t type)
{
	struct dyn_ipv4_state *s;
	void *data;
	uint32_t bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
	DYN_BUCKET_LOCK(bucket);
	if (info->direction == MATCH_UNKNOWN ||
	    info->kidx != kidx ||
	    info->hashval != hashval ||
	    info->version != DYN_BUCKET_VERSION(bucket, ipv4_add)) {
		/*
		 * Bucket version has been changed since last lookup,
		 * do lookup again to be sure that state does not exist.
		 */
		if (dyn_lookup_ipv4_state_locked(pkt, ulp, pktlen,
		    bucket, kidx) != 0) {
			DYN_BUCKET_UNLOCK(bucket);
			return (EEXIST);
		}
	}

	data = dyn_alloc_dyndata(parent, ruleid, rulenum, pkt, ulp,
	    pktlen, hashval, fibnum);
	if (data == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		return (ENOMEM);
	}

	s = dyn_alloc_ipv4_state(pkt, kidx, type);
	if (s == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		uma_zfree(V_dyn_data_zone, data);
		return (ENOMEM);
	}

	s->data = data;
	CK_SLIST_INSERT_HEAD(&V_dyn_ipv4[bucket], s, entry);
	DYN_COUNT_INC(dyn_count);
	DYN_BUCKET_VERSION_BUMP(bucket, ipv4_add);
	DYN_BUCKET_UNLOCK(bucket);
	return (0);
}

#ifdef INET6
static struct dyn_ipv6_state *
dyn_alloc_ipv6_state(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    uint16_t kidx, uint8_t type)
{
	struct dyn_ipv6_state *s;

	s = uma_zalloc(V_dyn_ipv6_zone, M_NOWAIT | M_ZERO);
	if (s == NULL)
		return (NULL);

	s->type = type;
	s->kidx = kidx;
	s->zoneid = zoneid;
	s->proto = pkt->proto;
	s->sport = pkt->src_port;
	s->dport = pkt->dst_port;
	s->src = pkt->src_ip6;
	s->dst = pkt->dst_ip6;
	return (s);
}

/*
 * Add IPv6 parent state.
 * Returns pointer to parent state. When it is not NULL we are in
 * critical section and pointer protected by hazard pointer.
 * When some error occurs, it return NULL and exit from critical section
 * is not needed.
 */
static struct dyn_ipv6_state *
dyn_add_ipv6_parent(void *rule, uint32_t ruleid, uint16_t rulenum,
    const struct ipfw_flow_id *pkt, uint32_t zoneid, uint32_t hashval,
    uint32_t version, uint16_t kidx)
{
	struct dyn_ipv6_state *s;
	struct dyn_parent *limit;
	uint32_t bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
	DYN_BUCKET_LOCK(bucket);
	if (version != DYN_BUCKET_VERSION(bucket, ipv6_parent_add)) {
		/*
		 * Bucket version has been changed since last lookup,
		 * do lookup again to be sure that state does not exist.
		 */
		s = dyn_lookup_ipv6_parent_locked(pkt, zoneid, rule, ruleid,
		    rulenum, bucket);
		if (s != NULL) {
			/*
			 * Simultaneous thread has already created this
			 * state. Just return it.
			 */
			DYNSTATE_CRITICAL_ENTER();
			DYNSTATE_PROTECT(s);
			DYN_BUCKET_UNLOCK(bucket);
			return (s);
		}
	}

	limit = dyn_alloc_parent(rule, ruleid, rulenum, hashval);
	if (limit == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		return (NULL);
	}

	s = dyn_alloc_ipv6_state(pkt, zoneid, kidx, O_LIMIT_PARENT);
	if (s == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		uma_zfree(V_dyn_parent_zone, limit);
		return (NULL);
	}

	s->limit = limit;
	CK_SLIST_INSERT_HEAD(&V_dyn_ipv6_parent[bucket], s, entry);
	DYN_COUNT_INC(dyn_parent_count);
	DYN_BUCKET_VERSION_BUMP(bucket, ipv6_parent_add);
	DYNSTATE_CRITICAL_ENTER();
	DYNSTATE_PROTECT(s);
	DYN_BUCKET_UNLOCK(bucket);
	return (s);
}

static int
dyn_add_ipv6_state(void *parent, uint32_t ruleid, uint16_t rulenum,
    const struct ipfw_flow_id *pkt, uint32_t zoneid, const void *ulp,
    int pktlen, uint32_t hashval, struct ipfw_dyn_info *info,
    uint16_t fibnum, uint16_t kidx, uint8_t type)
{
	struct dyn_ipv6_state *s;
	struct dyn_data *data;
	uint32_t bucket;

	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
	DYN_BUCKET_LOCK(bucket);
	if (info->direction == MATCH_UNKNOWN ||
	    info->kidx != kidx ||
	    info->hashval != hashval ||
	    info->version != DYN_BUCKET_VERSION(bucket, ipv6_add)) {
		/*
		 * Bucket version has been changed since last lookup,
		 * do lookup again to be sure that state does not exist.
		 */
		if (dyn_lookup_ipv6_state_locked(pkt, zoneid, ulp, pktlen,
		    bucket, kidx) != 0) {
			DYN_BUCKET_UNLOCK(bucket);
			return (EEXIST);
		}
	}

	data = dyn_alloc_dyndata(parent, ruleid, rulenum, pkt, ulp,
	    pktlen, hashval, fibnum);
	if (data == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		return (ENOMEM);
	}

	s = dyn_alloc_ipv6_state(pkt, zoneid, kidx, type);
	if (s == NULL) {
		DYN_BUCKET_UNLOCK(bucket);
		uma_zfree(V_dyn_data_zone, data);
		return (ENOMEM);
	}

	s->data = data;
	CK_SLIST_INSERT_HEAD(&V_dyn_ipv6[bucket], s, entry);
	DYN_COUNT_INC(dyn_count);
	DYN_BUCKET_VERSION_BUMP(bucket, ipv6_add);
	DYN_BUCKET_UNLOCK(bucket);
	return (0);
}
#endif /* INET6 */

static void *
dyn_get_parent_state(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    struct ip_fw *rule, uint32_t hashval, uint32_t limit, uint16_t kidx)
{
	char sbuf[24];
	struct dyn_parent *p;
	void *ret;
	uint32_t bucket, version;

	p = NULL;
	ret = NULL;
	bucket = DYN_BUCKET(hashval, V_curr_dyn_buckets);
	DYNSTATE_CRITICAL_ENTER();
	if (IS_IP4_FLOW_ID(pkt)) {
		struct dyn_ipv4_state *s;

		version = DYN_BUCKET_VERSION(bucket, ipv4_parent_add);
		s = dyn_lookup_ipv4_parent(pkt, rule, rule->id,
		    rule->rulenum, bucket);
		if (s == NULL) {
			/*
			 * Exit from critical section because dyn_add_parent()
			 * will acquire bucket lock.
			 */
			DYNSTATE_CRITICAL_EXIT();

			s = dyn_add_ipv4_parent(rule, rule->id,
			    rule->rulenum, pkt, hashval, version, kidx);
			if (s == NULL)
				return (NULL);
			/* Now we are in critical section again. */
		}
		ret = s;
		p = s->limit;
	}
#ifdef INET6
	else if (IS_IP6_FLOW_ID(pkt)) {
		struct dyn_ipv6_state *s;

		version = DYN_BUCKET_VERSION(bucket, ipv6_parent_add);
		s = dyn_lookup_ipv6_parent(pkt, zoneid, rule, rule->id,
		    rule->rulenum, bucket);
		if (s == NULL) {
			/*
			 * Exit from critical section because dyn_add_parent()
			 * can acquire bucket mutex.
			 */
			DYNSTATE_CRITICAL_EXIT();

			s = dyn_add_ipv6_parent(rule, rule->id,
			    rule->rulenum, pkt, zoneid, hashval, version,
			    kidx);
			if (s == NULL)
				return (NULL);
			/* Now we are in critical section again. */
		}
		ret = s;
		p = s->limit;
	}
#endif
	else {
		DYNSTATE_CRITICAL_EXIT();
		return (NULL);
	}

	/* Check the limit */
	if (DPARENT_COUNT(p) >= limit) {
		DYNSTATE_CRITICAL_EXIT();
		if (V_fw_verbose && last_log != time_uptime) {
			last_log = time_uptime;
			snprintf(sbuf, sizeof(sbuf), "%u drop session",
			    rule->rulenum);
			print_dyn_rule_flags(pkt, O_LIMIT,
			    LOG_SECURITY | LOG_DEBUG, sbuf,
			    "too many entries");
		}
		return (NULL);
	}

	/* Take new session into account. */
	DPARENT_COUNT_INC(p);
	/*
	 * We must exit from critical section because the following code
	 * can acquire bucket mutex.
	 * We rely on the the 'count' field. The state will not expire
	 * until it has some child states, i.e. 'count' field is not zero.
	 * Return state pointer, it will be used by child states as parent.
	 */
	DYNSTATE_CRITICAL_EXIT();
	return (ret);
}

static int
dyn_install_state(const struct ipfw_flow_id *pkt, uint32_t zoneid,
    uint16_t fibnum, const void *ulp, int pktlen, struct ip_fw *rule,
    struct ipfw_dyn_info *info, uint32_t limit, uint16_t limit_mask,
    uint16_t kidx, uint8_t type)
{
	struct ipfw_flow_id id;
	uint32_t hashval, parent_hashval, ruleid, rulenum;
	int ret;

	MPASS(type == O_LIMIT || type == O_KEEP_STATE);

	ruleid = rule->id;
	rulenum = rule->rulenum;
	if (type == O_LIMIT) {
		/* Create masked flow id and calculate bucket */
		id.addr_type = pkt->addr_type;
		id.proto = pkt->proto;
		id.fib = fibnum; /* unused */
		id.src_port = (limit_mask & DYN_SRC_PORT) ?
		    pkt->src_port: 0;
		id.dst_port = (limit_mask & DYN_DST_PORT) ?
		    pkt->dst_port: 0;
		if (IS_IP4_FLOW_ID(pkt)) {
			id.src_ip = (limit_mask & DYN_SRC_ADDR) ?
			    pkt->src_ip: 0;
			id.dst_ip = (limit_mask & DYN_DST_ADDR) ?
			    pkt->dst_ip: 0;
		}
#ifdef INET6
		else if (IS_IP6_FLOW_ID(pkt)) {
			if (limit_mask & DYN_SRC_ADDR)
				id.src_ip6 = pkt->src_ip6;
			else
				memset(&id.src_ip6, 0, sizeof(id.src_ip6));
			if (limit_mask & DYN_DST_ADDR)
				id.dst_ip6 = pkt->dst_ip6;
			else
				memset(&id.dst_ip6, 0, sizeof(id.dst_ip6));
		}
#endif
		else
			return (EAFNOSUPPORT);

		parent_hashval = hash_parent(&id, rule);
		rule = dyn_get_parent_state(&id, zoneid, rule, parent_hashval,
		    limit, kidx);
		if (rule == NULL) {
#if 0
			if (V_fw_verbose && last_log != time_uptime) {
				last_log = time_uptime;
				snprintf(sbuf, sizeof(sbuf),
				    "%u drop session", rule->rulenum);
			print_dyn_rule_flags(pkt, O_LIMIT,
			    LOG_SECURITY | LOG_DEBUG, sbuf,
			    "too many entries");
			}
#endif
			return (EACCES);
		}
		/*
		 * Limit is not reached, create new state.
		 * Now rule points to parent state.
		 */
	}

	hashval = hash_packet(pkt);
	if (IS_IP4_FLOW_ID(pkt))
		ret = dyn_add_ipv4_state(rule, ruleid, rulenum, pkt,
		    ulp, pktlen, hashval, info, fibnum, kidx, type);
#ifdef INET6
	else if (IS_IP6_FLOW_ID(pkt))
		ret = dyn_add_ipv6_state(rule, ruleid, rulenum, pkt,
		    zoneid, ulp, pktlen, hashval, info, fibnum, kidx, type);
#endif /* INET6 */
	else
		ret = EAFNOSUPPORT;

	if (type == O_LIMIT) {
		if (ret != 0) {
			/*
			 * We failed to create child state for O_LIMIT
			 * opcode. Since we already counted it in the parent,
			 * we must revert counter back. The 'rule' points to
			 * parent state, use it to get dyn_parent.
			 *
			 * XXXAE: it should be safe to use 'rule' pointer
			 * without extra lookup, parent state is referenced
			 * and should not be freed.
			 */
			if (IS_IP4_FLOW_ID(&id))
				DPARENT_COUNT_DEC(
				    ((struct dyn_ipv4_state *)rule)->limit);
#ifdef INET6
			else if (IS_IP6_FLOW_ID(&id))
				DPARENT_COUNT_DEC(
				    ((struct dyn_ipv6_state *)rule)->limit);
#endif
		}
	}
	/*
	 * EEXIST means that simultaneous thread has created this
	 * state. Consider this as success.
	 *
	 * XXXAE: should we invalidate 'info' content here?
	 */
	if (ret == EEXIST)
		return (0);
	return (ret);
}

/*
 * Install dynamic state.
 *  chain - ipfw's instance;
 *  rule - the parent rule that installs the state;
 *  cmd - opcode that installs the state;
 *  args - ipfw arguments;
 *  ulp - upper level protocol header;
 *  pktlen - packet length;
 *  info - dynamic state lookup info;
 *  tablearg - tablearg id.
 *
 * Returns non-zero value (failure) if state is not installed because
 * of errors or because session limitations are enforced.
 */
int
ipfw_dyn_install_state(struct ip_fw_chain *chain, struct ip_fw *rule,
    const ipfw_insn_limit *cmd, const struct ip_fw_args *args,
    const void *ulp, int pktlen, struct ipfw_dyn_info *info,
    uint32_t tablearg)
{
	uint32_t limit;
	uint16_t limit_mask;

	if (cmd->o.opcode == O_LIMIT) {
		limit = IP_FW_ARG_TABLEARG(chain, cmd->conn_limit, limit);
		limit_mask = cmd->limit_mask;
	} else {
		limit = 0;
		limit_mask = 0;
	}
	return (dyn_install_state(&args->f_id,
#ifdef INET6
	    IS_IP6_FLOW_ID(&args->f_id) ? dyn_getscopeid(args):
#endif
	    0, M_GETFIB(args->m), ulp, pktlen, rule, info, limit,
	    limit_mask, cmd->o.arg1, cmd->o.opcode));
}

/*
 * Free safe to remove state entries from expired lists.
 */
static void
dyn_free_states(struct ip_fw_chain *chain)
{
	struct dyn_ipv4_state *s4, *s4n;
#ifdef INET6
	struct dyn_ipv6_state *s6, *s6n;
#endif
	int cached_count, i;

	/*
	 * We keep pointers to objects that are in use on each CPU
	 * in the per-cpu dyn_hp pointer. When object is going to be
	 * removed, first of it is unlinked from the corresponding
	 * list. This leads to changing of dyn_bucket_xxx_delver version.
	 * Unlinked objects is placed into corresponding dyn_expired_xxx
	 * list. Reader that is going to dereference object pointer checks
	 * dyn_bucket_xxx_delver version before and after storing pointer
	 * into dyn_hp. If version is the same, the object is protected
	 * from freeing and it is safe to dereference. Othervise reader
	 * tries to iterate list again from the beginning, but this object
	 * now unlinked and thus will not be accessible.
	 *
	 * Copy dyn_hp pointers for each CPU into dyn_hp_cache array.
	 * It does not matter that some pointer can be changed in
	 * time while we are copying. We need to check, that objects
	 * removed in the previous pass are not in use. And if dyn_hp
	 * pointer does not contain it in the time when we are copying,
	 * it will not appear there, because it is already unlinked.
	 * And for new pointers we will not free objects that will be
	 * unlinked in this pass.
	 */
	cached_count = 0;
	CPU_FOREACH(i) {
		dyn_hp_cache[cached_count] = DYNSTATE_GET(i);
		if (dyn_hp_cache[cached_count] != NULL)
			cached_count++;
	}

	/*
	 * Free expired states that are safe to free.
	 * Check each entry from previous pass in the dyn_expired_xxx
	 * list, if pointer to the object is in the dyn_hp_cache array,
	 * keep it until next pass. Otherwise it is safe to free the
	 * object.
	 *
	 * XXXAE: optimize this to use SLIST_REMOVE_AFTER.
	 */
#define	DYN_FREE_STATES(s, next, name)		do {			\
	s = SLIST_FIRST(&V_dyn_expired_ ## name);			\
	while (s != NULL) {						\
		next = SLIST_NEXT(s, expired);				\
		for (i = 0; i < cached_count; i++)			\
			if (dyn_hp_cache[i] == s)			\
				break;					\
		if (i == cached_count) {				\
			if (s->type == O_LIMIT_PARENT &&		\
			    s->limit->count != 0) {			\
				s = next;				\
				continue;				\
			}						\
			SLIST_REMOVE(&V_dyn_expired_ ## name,		\
			    s, dyn_ ## name ## _state, expired);	\
			if (s->type == O_LIMIT_PARENT)			\
				uma_zfree(V_dyn_parent_zone, s->limit);	\
			else						\
				uma_zfree(V_dyn_data_zone, s->data);	\
			uma_zfree(V_dyn_ ## name ## _zone, s);		\
		}							\
		s = next;						\
	}								\
} while (0)

	/*
	 * Protect access to expired lists with DYN_EXPIRED_LOCK.
	 * Userland can invoke ipfw_expire_dyn_states() to delete
	 * specific states, this will lead to modification of expired
	 * lists.
	 *
	 * XXXAE: do we need DYN_EXPIRED_LOCK? We can just use
	 *	  IPFW_UH_WLOCK to protect access to these lists.
	 */
	DYN_EXPIRED_LOCK();
	DYN_FREE_STATES(s4, s4n, ipv4);
#ifdef INET6
	DYN_FREE_STATES(s6, s6n, ipv6);
#endif
	DYN_EXPIRED_UNLOCK();
#undef DYN_FREE_STATES
}

/*
 * Returns:
 * 0 when state is not matched by specified range;
 * 1 when state is matched by specified range;
 * 2 when state is matched by specified range and requested deletion of
 *   dynamic states.
 */
static int
dyn_match_range(uint16_t rulenum, uint8_t set, const ipfw_range_tlv *rt)
{

	MPASS(rt != NULL);
	/* flush all states */
	if (rt->flags & IPFW_RCFLAG_ALL) {
		if (rt->flags & IPFW_RCFLAG_DYNAMIC)
			return (2); /* forced */
		return (1);
	}
	if ((rt->flags & IPFW_RCFLAG_SET) != 0 && set != rt->set)
		return (0);
	if ((rt->flags & IPFW_RCFLAG_RANGE) != 0 &&
	    (rulenum < rt->start_rule || rulenum > rt->end_rule))
		return (0);
	if (rt->flags & IPFW_RCFLAG_DYNAMIC)
		return (2);
	return (1);
}

static void
dyn_acquire_rule(struct ip_fw_chain *ch, struct dyn_data *data,
    struct ip_fw *rule, uint16_t kidx)
{
	struct dyn_state_obj *obj;

	/*
	 * Do not acquire reference twice.
	 * This can happen when rule deletion executed for
	 * the same range, but different ruleset id.
	 */
	if (data->flags & DYN_REFERENCED)
		return;

	IPFW_UH_WLOCK_ASSERT(ch);
	MPASS(kidx != 0);

	data->flags |= DYN_REFERENCED;
	/* Reference the named object */
	obj = SRV_OBJECT(ch, kidx);
	obj->no.refcnt++;
	MPASS(obj->no.etlv == IPFW_TLV_STATE_NAME);

	/* Reference the parent rule */
	rule->refcnt++;
}

static void
dyn_release_rule(struct ip_fw_chain *ch, struct dyn_data *data,
    struct ip_fw *rule, uint16_t kidx)
{
	struct dyn_state_obj *obj;

	IPFW_UH_WLOCK_ASSERT(ch);
	MPASS(kidx != 0);

	obj = SRV_OBJECT(ch, kidx);
	if (obj->no.refcnt == 1)
		dyn_destroy(ch, &obj->no);
	else
		obj->no.refcnt--;

	if (--rule->refcnt == 1)
		ipfw_free_rule(rule);
}

/*
 * We do not keep O_LIMIT_PARENT states when V_dyn_keep_states is enabled.
 * O_LIMIT state is created when new connection is going to be established
 * and there is no matching state. So, since the old parent rule was deleted
 * we can't create new states with old parent, and thus we can not account
 * new connections with already established connections, and can not do
 * proper limiting.
 */
static int
dyn_match_ipv4_state(struct ip_fw_chain *ch, struct dyn_ipv4_state *s,
    const ipfw_range_tlv *rt)
{
	struct ip_fw *rule;
	int ret;

	if (s->type == O_LIMIT_PARENT) {
		rule = s->limit->parent;
		return (dyn_match_range(s->limit->rulenum, rule->set, rt));
	}

	rule = s->data->parent;
	if (s->type == O_LIMIT)
		rule = ((struct dyn_ipv4_state *)rule)->limit->parent;

	ret = dyn_match_range(s->data->rulenum, rule->set, rt);
	if (ret == 0 || V_dyn_keep_states == 0 || ret > 1)
		return (ret);

	dyn_acquire_rule(ch, s->data, rule, s->kidx);
	return (0);
}

#ifdef INET6
static int
dyn_match_ipv6_state(struct ip_fw_chain *ch, struct dyn_ipv6_state *s,
    const ipfw_range_tlv *rt)
{
	struct ip_fw *rule;
	int ret;

	if (s->type == O_LIMIT_PARENT) {
		rule = s->limit->parent;
		return (dyn_match_range(s->limit->rulenum, rule->set, rt));
	}

	rule = s->data->parent;
	if (s->type == O_LIMIT)
		rule = ((struct dyn_ipv6_state *)rule)->limit->parent;

	ret = dyn_match_range(s->data->rulenum, rule->set, rt);
	if (ret == 0 || V_dyn_keep_states == 0 || ret > 1)
		return (ret);

	dyn_acquire_rule(ch, s->data, rule, s->kidx);
	return (0);
}
#endif

/*
 * Unlink expired entries from states lists.
 * @rt can be used to specify the range of states for deletion.
 */
static void
dyn_expire_states(struct ip_fw_chain *ch, ipfw_range_tlv *rt)
{
	struct dyn_ipv4_slist expired_ipv4;
#ifdef INET6
	struct dyn_ipv6_slist expired_ipv6;
	struct dyn_ipv6_state *s6, *s6n, *s6p;
#endif
	struct dyn_ipv4_state *s4, *s4n, *s4p;
	void *rule;
	int bucket, removed, length, max_length;

	IPFW_UH_WLOCK_ASSERT(ch);

	/*
	 * Unlink expired states from each bucket.
	 * With acquired bucket lock iterate entries of each lists:
	 * ipv4, ipv4_parent, ipv6, and ipv6_parent. Check expired time
	 * and unlink entry from the list, link entry into temporary
	 * expired_xxx lists then bump "del" bucket version.
	 *
	 * When an entry is removed, corresponding states counter is
	 * decremented. If entry has O_LIMIT type, parent's reference
	 * counter is decremented.
	 *
	 * NOTE: this function can be called from userspace context
	 * when user deletes rules. In this case all matched states
	 * will be forcedly unlinked. O_LIMIT_PARENT states will be kept
	 * in the expired lists until reference counter become zero.
	 */
#define	DYN_UNLINK_STATES(s, prev, next, exp, af, name, extra)	do {	\
	length = 0;							\
	removed = 0;							\
	prev = NULL;							\
	s = CK_SLIST_FIRST(&V_dyn_ ## name [bucket]);			\
	while (s != NULL) {						\
		next = CK_SLIST_NEXT(s, entry);				\
		if ((TIME_LEQ((s)->exp, time_uptime) && extra) ||	\
		    (rt != NULL &&					\
		     dyn_match_ ## af ## _state(ch, s, rt))) {		\
			if (prev != NULL)				\
				CK_SLIST_REMOVE_AFTER(prev, entry);	\
			else						\
				CK_SLIST_REMOVE_HEAD(			\
				    &V_dyn_ ## name [bucket], entry);	\
			removed++;					\
			SLIST_INSERT_HEAD(&expired_ ## af, s, expired);	\
			if (s->type == O_LIMIT_PARENT)			\
				DYN_COUNT_DEC(dyn_parent_count);	\
			else {						\
				DYN_COUNT_DEC(dyn_count);		\
				if (s->data->flags & DYN_REFERENCED) {	\
					rule = s->data->parent;		\
					if (s->type == O_LIMIT)		\
						rule = ((__typeof(s))	\
						    rule)->limit->parent;\
					dyn_release_rule(ch, s->data,	\
					    rule, s->kidx);		\
				}					\
				if (s->type == O_LIMIT)	{		\
					s = s->data->parent;		\
					DPARENT_COUNT_DEC(s->limit);	\
				}					\
			}						\
		} else {						\
			prev = s;					\
			length++;					\
		}							\
		s = next;						\
	}								\
	if (removed != 0)						\
		DYN_BUCKET_VERSION_BUMP(bucket, name ## _del);		\
	if (length > max_length)				\
		max_length = length;				\
} while (0)

	SLIST_INIT(&expired_ipv4);
#ifdef INET6
	SLIST_INIT(&expired_ipv6);
#endif
	max_length = 0;
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_BUCKET_LOCK(bucket);
		DYN_UNLINK_STATES(s4, s4p, s4n, data->expire, ipv4, ipv4, 1);
		DYN_UNLINK_STATES(s4, s4p, s4n, limit->expire, ipv4,
		    ipv4_parent, (s4->limit->count == 0));
#ifdef INET6
		DYN_UNLINK_STATES(s6, s6p, s6n, data->expire, ipv6, ipv6, 1);
		DYN_UNLINK_STATES(s6, s6p, s6n, limit->expire, ipv6,
		    ipv6_parent, (s6->limit->count == 0));
#endif
		DYN_BUCKET_UNLOCK(bucket);
	}
	/* Update curr_max_length for statistics. */
	V_curr_max_length = max_length;
	/*
	 * Concatenate temporary lists with global expired lists.
	 */
	DYN_EXPIRED_LOCK();
	SLIST_CONCAT(&V_dyn_expired_ipv4, &expired_ipv4,
	    dyn_ipv4_state, expired);
#ifdef INET6
	SLIST_CONCAT(&V_dyn_expired_ipv6, &expired_ipv6,
	    dyn_ipv6_state, expired);
#endif
	DYN_EXPIRED_UNLOCK();
#undef DYN_UNLINK_STATES
#undef DYN_UNREF_STATES
}

static struct mbuf *
dyn_mgethdr(int len, uint16_t fibnum)
{
	struct mbuf *m;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);
#ifdef MAC
	mac_netinet_firewall_send(m);
#endif
	M_SETFIB(m, fibnum);
	m->m_data += max_linkhdr;
	m->m_flags |= M_SKIP_FIREWALL;
	m->m_len = m->m_pkthdr.len = len;
	bzero(m->m_data, len);
	return (m);
}

static void
dyn_make_keepalive_ipv4(struct mbuf *m, in_addr_t src, in_addr_t dst,
    uint32_t seq, uint32_t ack, uint16_t sport, uint16_t dport)
{
	struct tcphdr *tcp;
	struct ip *ip;

	ip = mtod(m, struct ip *);
	ip->ip_v = 4;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_tos = IPTOS_LOWDELAY;
	ip->ip_len = htons(m->m_len);
	ip->ip_off |= htons(IP_DF);
	ip->ip_ttl = V_ip_defttl;
	ip->ip_p = IPPROTO_TCP;
	ip->ip_src.s_addr = htonl(src);
	ip->ip_dst.s_addr = htonl(dst);

	tcp = mtodo(m, sizeof(struct ip));
	tcp->th_sport = htons(sport);
	tcp->th_dport = htons(dport);
	tcp->th_off = sizeof(struct tcphdr) >> 2;
	tcp->th_seq = htonl(seq);
	tcp->th_ack = htonl(ack);
	tcp->th_flags = TH_ACK;
	tcp->th_sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
	    htons(sizeof(struct tcphdr) + IPPROTO_TCP));

	m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
	m->m_pkthdr.csum_flags = CSUM_TCP;
}

static void
dyn_enqueue_keepalive_ipv4(struct mbufq *q, const struct dyn_ipv4_state *s)
{
	struct mbuf *m;

	if ((s->data->state & ACK_FWD) == 0 && s->data->ack_fwd > 0) {
		m = dyn_mgethdr(sizeof(struct ip) + sizeof(struct tcphdr),
		    s->data->fibnum);
		if (m != NULL) {
			dyn_make_keepalive_ipv4(m, s->dst, s->src,
			    s->data->ack_fwd - 1, s->data->ack_rev,
			    s->dport, s->sport);
			if (mbufq_enqueue(q, m)) {
				m_freem(m);
				log(LOG_DEBUG, "ipfw: limit for IPv4 "
				    "keepalive queue is reached.\n");
				return;
			}
		}
	}

	if ((s->data->state & ACK_REV) == 0 && s->data->ack_rev > 0) {
		m = dyn_mgethdr(sizeof(struct ip) + sizeof(struct tcphdr),
		    s->data->fibnum);
		if (m != NULL) {
			dyn_make_keepalive_ipv4(m, s->src, s->dst,
			    s->data->ack_rev - 1, s->data->ack_fwd,
			    s->sport, s->dport);
			if (mbufq_enqueue(q, m)) {
				m_freem(m);
				log(LOG_DEBUG, "ipfw: limit for IPv4 "
				    "keepalive queue is reached.\n");
				return;
			}
		}
	}
}

/*
 * Prepare and send keep-alive packets.
 */
static void
dyn_send_keepalive_ipv4(struct ip_fw_chain *chain)
{
	struct mbufq q;
	struct mbuf *m;
	struct dyn_ipv4_state *s;
	uint32_t bucket;

	mbufq_init(&q, INT_MAX);
	IPFW_UH_RLOCK(chain);
	/*
	 * It is safe to not use hazard pointer and just do lockless
	 * access to the lists, because states entries can not be deleted
	 * while we hold IPFW_UH_RLOCK.
	 */
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		CK_SLIST_FOREACH(s, &V_dyn_ipv4[bucket], entry) {
			/*
			 * Only established TCP connections that will
			 * become expired withing dyn_keepalive_interval.
			 */
			if (s->proto != IPPROTO_TCP ||
			    (s->data->state & BOTH_SYN) != BOTH_SYN ||
			    TIME_LEQ(time_uptime + V_dyn_keepalive_interval,
				s->data->expire))
				continue;
			dyn_enqueue_keepalive_ipv4(&q, s);
		}
	}
	IPFW_UH_RUNLOCK(chain);
	while ((m = mbufq_dequeue(&q)) != NULL)
		ip_output(m, NULL, NULL, 0, NULL, NULL);
}

#ifdef INET6
static void
dyn_make_keepalive_ipv6(struct mbuf *m, const struct in6_addr *src,
    const struct in6_addr *dst, uint32_t zoneid, uint32_t seq, uint32_t ack,
    uint16_t sport, uint16_t dport)
{
	struct tcphdr *tcp;
	struct ip6_hdr *ip6;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_plen = htons(sizeof(struct tcphdr));
	ip6->ip6_nxt = IPPROTO_TCP;
	ip6->ip6_hlim = IPV6_DEFHLIM;
	ip6->ip6_src = *src;
	if (IN6_IS_ADDR_LINKLOCAL(src))
		ip6->ip6_src.s6_addr16[1] = htons(zoneid & 0xffff);
	ip6->ip6_dst = *dst;
	if (IN6_IS_ADDR_LINKLOCAL(dst))
		ip6->ip6_dst.s6_addr16[1] = htons(zoneid & 0xffff);

	tcp = mtodo(m, sizeof(struct ip6_hdr));
	tcp->th_sport = htons(sport);
	tcp->th_dport = htons(dport);
	tcp->th_off = sizeof(struct tcphdr) >> 2;
	tcp->th_seq = htonl(seq);
	tcp->th_ack = htonl(ack);
	tcp->th_flags = TH_ACK;
	tcp->th_sum = in6_cksum_pseudo(ip6, sizeof(struct tcphdr),
	    IPPROTO_TCP, 0);

	m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
	m->m_pkthdr.csum_flags = CSUM_TCP_IPV6;
}

static void
dyn_enqueue_keepalive_ipv6(struct mbufq *q, const struct dyn_ipv6_state *s)
{
	struct mbuf *m;

	if ((s->data->state & ACK_FWD) == 0 && s->data->ack_fwd > 0) {
		m = dyn_mgethdr(sizeof(struct ip6_hdr) +
		    sizeof(struct tcphdr), s->data->fibnum);
		if (m != NULL) {
			dyn_make_keepalive_ipv6(m, &s->dst, &s->src,
			    s->zoneid, s->data->ack_fwd - 1, s->data->ack_rev,
			    s->dport, s->sport);
			if (mbufq_enqueue(q, m)) {
				m_freem(m);
				log(LOG_DEBUG, "ipfw: limit for IPv6 "
				    "keepalive queue is reached.\n");
				return;
			}
		}
	}

	if ((s->data->state & ACK_REV) == 0 && s->data->ack_rev > 0) {
		m = dyn_mgethdr(sizeof(struct ip6_hdr) +
		    sizeof(struct tcphdr), s->data->fibnum);
		if (m != NULL) {
			dyn_make_keepalive_ipv6(m, &s->src, &s->dst,
			    s->zoneid, s->data->ack_rev - 1, s->data->ack_fwd,
			    s->sport, s->dport);
			if (mbufq_enqueue(q, m)) {
				m_freem(m);
				log(LOG_DEBUG, "ipfw: limit for IPv6 "
				    "keepalive queue is reached.\n");
				return;
			}
		}
	}
}

static void
dyn_send_keepalive_ipv6(struct ip_fw_chain *chain)
{
	struct mbufq q;
	struct mbuf *m;
	struct dyn_ipv6_state *s;
	uint32_t bucket;

	mbufq_init(&q, INT_MAX);
	IPFW_UH_RLOCK(chain);
	/*
	 * It is safe to not use hazard pointer and just do lockless
	 * access to the lists, because states entries can not be deleted
	 * while we hold IPFW_UH_RLOCK.
	 */
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		CK_SLIST_FOREACH(s, &V_dyn_ipv6[bucket], entry) {
			/*
			 * Only established TCP connections that will
			 * become expired withing dyn_keepalive_interval.
			 */
			if (s->proto != IPPROTO_TCP ||
			    (s->data->state & BOTH_SYN) != BOTH_SYN ||
			    TIME_LEQ(time_uptime + V_dyn_keepalive_interval,
				s->data->expire))
				continue;
			dyn_enqueue_keepalive_ipv6(&q, s);
		}
	}
	IPFW_UH_RUNLOCK(chain);
	while ((m = mbufq_dequeue(&q)) != NULL)
		ip6_output(m, NULL, NULL, 0, NULL, NULL, NULL);
}
#endif /* INET6 */

static void
dyn_grow_hashtable(struct ip_fw_chain *chain, uint32_t new)
{
#ifdef INET6
	struct dyn_ipv6ck_slist *ipv6, *ipv6_parent;
	uint32_t *ipv6_add, *ipv6_del, *ipv6_parent_add, *ipv6_parent_del;
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4ck_slist *ipv4, *ipv4_parent;
	uint32_t *ipv4_add, *ipv4_del, *ipv4_parent_add, *ipv4_parent_del;
	struct dyn_ipv4_state *s4;
	struct mtx *bucket_lock;
	void *tmp;
	uint32_t bucket;

	MPASS(powerof2(new));
	DYN_DEBUG("grow hash size %u -> %u", V_curr_dyn_buckets, new);
	/*
	 * Allocate and initialize new lists.
	 * XXXAE: on memory pressure this can disable callout timer.
	 */
	bucket_lock = malloc(new * sizeof(struct mtx), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv4 = malloc(new * sizeof(struct dyn_ipv4ck_slist), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv4_parent = malloc(new * sizeof(struct dyn_ipv4ck_slist), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv4_add = malloc(new * sizeof(uint32_t), M_IPFW, M_WAITOK | M_ZERO);
	ipv4_del = malloc(new * sizeof(uint32_t), M_IPFW, M_WAITOK | M_ZERO);
	ipv4_parent_add = malloc(new * sizeof(uint32_t), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv4_parent_del = malloc(new * sizeof(uint32_t), M_IPFW,
	    M_WAITOK | M_ZERO);
#ifdef INET6
	ipv6 = malloc(new * sizeof(struct dyn_ipv6ck_slist), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv6_parent = malloc(new * sizeof(struct dyn_ipv6ck_slist), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv6_add = malloc(new * sizeof(uint32_t), M_IPFW, M_WAITOK | M_ZERO);
	ipv6_del = malloc(new * sizeof(uint32_t), M_IPFW, M_WAITOK | M_ZERO);
	ipv6_parent_add = malloc(new * sizeof(uint32_t), M_IPFW,
	    M_WAITOK | M_ZERO);
	ipv6_parent_del = malloc(new * sizeof(uint32_t), M_IPFW,
	    M_WAITOK | M_ZERO);
#endif
	for (bucket = 0; bucket < new; bucket++) {
		DYN_BUCKET_LOCK_INIT(bucket_lock, bucket);
		CK_SLIST_INIT(&ipv4[bucket]);
		CK_SLIST_INIT(&ipv4_parent[bucket]);
#ifdef INET6
		CK_SLIST_INIT(&ipv6[bucket]);
		CK_SLIST_INIT(&ipv6_parent[bucket]);
#endif
	}

#define DYN_RELINK_STATES(s, hval, i, head, ohead)	do {		\
	while ((s = CK_SLIST_FIRST(&V_dyn_ ## ohead[i])) != NULL) {	\
		CK_SLIST_REMOVE_HEAD(&V_dyn_ ## ohead[i], entry);	\
		CK_SLIST_INSERT_HEAD(&head[DYN_BUCKET(s->hval, new)],	\
		    s, entry);						\
	}								\
} while (0)
	/*
	 * Prevent rules changing from userland.
	 */
	IPFW_UH_WLOCK(chain);
	/*
	 * Hold traffic processing until we finish resize to
	 * prevent access to states lists.
	 */
	IPFW_WLOCK(chain);
	/* Re-link all dynamic states */
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_RELINK_STATES(s4, data->hashval, bucket, ipv4, ipv4);
		DYN_RELINK_STATES(s4, limit->hashval, bucket, ipv4_parent,
		    ipv4_parent);
#ifdef INET6
		DYN_RELINK_STATES(s6, data->hashval, bucket, ipv6, ipv6);
		DYN_RELINK_STATES(s6, limit->hashval, bucket, ipv6_parent,
		    ipv6_parent);
#endif
	}

#define	DYN_SWAP_PTR(old, new, tmp)	do {		\
	tmp = old;					\
	old = new;					\
	new = tmp;					\
} while (0)
	/* Swap pointers */
	DYN_SWAP_PTR(V_dyn_bucket_lock, bucket_lock, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4, ipv4, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4_parent, ipv4_parent, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4_add, ipv4_add, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4_parent_add, ipv4_parent_add, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4_del, ipv4_del, tmp);
	DYN_SWAP_PTR(V_dyn_ipv4_parent_del, ipv4_parent_del, tmp);

#ifdef INET6
	DYN_SWAP_PTR(V_dyn_ipv6, ipv6, tmp);
	DYN_SWAP_PTR(V_dyn_ipv6_parent, ipv6_parent, tmp);
	DYN_SWAP_PTR(V_dyn_ipv6_add, ipv6_add, tmp);
	DYN_SWAP_PTR(V_dyn_ipv6_parent_add, ipv6_parent_add, tmp);
	DYN_SWAP_PTR(V_dyn_ipv6_del, ipv6_del, tmp);
	DYN_SWAP_PTR(V_dyn_ipv6_parent_del, ipv6_parent_del, tmp);
#endif
	bucket = V_curr_dyn_buckets;
	V_curr_dyn_buckets = new;

	IPFW_WUNLOCK(chain);
	IPFW_UH_WUNLOCK(chain);

	/* Release old resources */
	while (bucket-- != 0)
		DYN_BUCKET_LOCK_DESTROY(bucket_lock, bucket);
	free(bucket_lock, M_IPFW);
	free(ipv4, M_IPFW);
	free(ipv4_parent, M_IPFW);
	free(ipv4_add, M_IPFW);
	free(ipv4_parent_add, M_IPFW);
	free(ipv4_del, M_IPFW);
	free(ipv4_parent_del, M_IPFW);
#ifdef INET6
	free(ipv6, M_IPFW);
	free(ipv6_parent, M_IPFW);
	free(ipv6_add, M_IPFW);
	free(ipv6_parent_add, M_IPFW);
	free(ipv6_del, M_IPFW);
	free(ipv6_parent_del, M_IPFW);
#endif
}

/*
 * This function is used to perform various maintenance
 * on dynamic hash lists. Currently it is called every second.
 */
static void
dyn_tick(void *vnetx)
{
	uint32_t buckets;

	CURVNET_SET((struct vnet *)vnetx);
	/*
	 * First free states unlinked in previous passes.
	 */
	dyn_free_states(&V_layer3_chain);
	/*
	 * Now unlink others expired states.
	 * We use IPFW_UH_WLOCK to avoid concurrent call of
	 * dyn_expire_states(). It is the only function that does
	 * deletion of state entries from states lists.
	 */
	IPFW_UH_WLOCK(&V_layer3_chain);
	dyn_expire_states(&V_layer3_chain, NULL);
	IPFW_UH_WUNLOCK(&V_layer3_chain);
	/*
	 * Send keepalives if they are enabled and the time has come.
	 */
	if (V_dyn_keepalive != 0 &&
	    V_dyn_keepalive_last + V_dyn_keepalive_period <= time_uptime) {
		V_dyn_keepalive_last = time_uptime;
		dyn_send_keepalive_ipv4(&V_layer3_chain);
#ifdef INET6
		dyn_send_keepalive_ipv6(&V_layer3_chain);
#endif
	}
	/*
	 * Check if we need to resize the hash:
	 * if current number of states exceeds number of buckets in hash,
	 * and dyn_buckets_max permits to grow the number of buckets, then
	 * do it. Grow hash size to the minimum power of 2 which is bigger
	 * than current states count.
	 */
	if (V_curr_dyn_buckets < V_dyn_buckets_max &&
	    (V_curr_dyn_buckets < V_dyn_count / 2 || (
	    V_curr_dyn_buckets < V_dyn_count && V_curr_max_length > 8))) {
		buckets = 1 << fls(V_dyn_count);
		if (buckets > V_dyn_buckets_max)
			buckets = V_dyn_buckets_max;
		dyn_grow_hashtable(&V_layer3_chain, buckets);
	}

	callout_reset_on(&V_dyn_timeout, hz, dyn_tick, vnetx, 0);
	CURVNET_RESTORE();
}

void
ipfw_expire_dyn_states(struct ip_fw_chain *chain, ipfw_range_tlv *rt)
{
	/*
	 * Do not perform any checks if we currently have no dynamic states
	 */
	if (V_dyn_count == 0)
		return;

	IPFW_UH_WLOCK_ASSERT(chain);
	dyn_expire_states(chain, rt);
}

/*
 * Pass through all states and reset eaction for orphaned rules.
 */
void
ipfw_dyn_reset_eaction(struct ip_fw_chain *ch, uint16_t eaction_id,
    uint16_t default_id, uint16_t instance_id)
{
#ifdef INET6
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4_state *s4;
	struct ip_fw *rule;
	uint32_t bucket;

#define	DYN_RESET_EACTION(s, h, b)					\
	CK_SLIST_FOREACH(s, &V_dyn_ ## h[b], entry) {			\
		if ((s->data->flags & DYN_REFERENCED) == 0)		\
			continue;					\
		rule = s->data->parent;					\
		if (s->type == O_LIMIT)					\
			rule = ((__typeof(s))rule)->limit->parent;	\
		ipfw_reset_eaction(ch, rule, eaction_id,		\
		    default_id, instance_id);				\
	}

	IPFW_UH_WLOCK_ASSERT(ch);
	if (V_dyn_count == 0)
		return;
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_RESET_EACTION(s4, ipv4, bucket);
#ifdef INET6
		DYN_RESET_EACTION(s6, ipv6, bucket);
#endif
	}
}

/*
 * Returns size of dynamic states in legacy format
 */
int
ipfw_dyn_len(void)
{

	return ((V_dyn_count + V_dyn_parent_count) * sizeof(ipfw_dyn_rule));
}

/*
 * Returns number of dynamic states.
 * Marks every named object index used by dynamic states with bit in @bmask.
 * Returns number of named objects accounted in bmask via @nocnt.
 * Used by dump format v1 (current).
 */
uint32_t
ipfw_dyn_get_count(uint32_t *bmask, int *nocnt)
{
#ifdef INET6
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4_state *s4;
	uint32_t bucket;

#define	DYN_COUNT_OBJECTS(s, h, b)					\
	CK_SLIST_FOREACH(s, &V_dyn_ ## h[b], entry) {			\
		MPASS(s->kidx != 0);					\
		if (ipfw_mark_object_kidx(bmask, IPFW_TLV_STATE_NAME,	\
		    s->kidx) != 0)					\
			(*nocnt)++;					\
	}

	IPFW_UH_RLOCK_ASSERT(&V_layer3_chain);

	/* No need to pass through all the buckets. */
	*nocnt = 0;
	if (V_dyn_count + V_dyn_parent_count == 0)
		return (0);

	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_COUNT_OBJECTS(s4, ipv4, bucket);
#ifdef INET6
		DYN_COUNT_OBJECTS(s6, ipv6, bucket);
#endif
	}

	return (V_dyn_count + V_dyn_parent_count);
}

/*
 * Check if rule contains at least one dynamic opcode.
 *
 * Returns 1 if such opcode is found, 0 otherwise.
 */
int
ipfw_is_dyn_rule(struct ip_fw *rule)
{
	int cmdlen, l;
	ipfw_insn *cmd;

	l = rule->cmd_len;
	cmd = rule->cmd;
	cmdlen = 0;
	for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
		cmdlen = F_LEN(cmd);

		switch (cmd->opcode) {
		case O_LIMIT:
		case O_KEEP_STATE:
		case O_PROBE_STATE:
		case O_CHECK_STATE:
			return (1);
		}
	}

	return (0);
}

static void
dyn_export_parent(const struct dyn_parent *p, uint16_t kidx, uint8_t set,
    ipfw_dyn_rule *dst)
{

	dst->dyn_type = O_LIMIT_PARENT;
	dst->kidx = kidx;
	dst->count = (uint16_t)DPARENT_COUNT(p);
	dst->expire = TIME_LEQ(p->expire, time_uptime) ?  0:
	    p->expire - time_uptime;

	/* 'rule' is used to pass up the rule number and set */
	memcpy(&dst->rule, &p->rulenum, sizeof(p->rulenum));

	/* store set number into high word of dst->rule pointer. */
	memcpy((char *)&dst->rule + sizeof(p->rulenum), &set, sizeof(set));

	/* unused fields */
	dst->pcnt = 0;
	dst->bcnt = 0;
	dst->parent = NULL;
	dst->state = 0;
	dst->ack_fwd = 0;
	dst->ack_rev = 0;
	dst->bucket = p->hashval;
	/*
	 * The legacy userland code will interpret a NULL here as a marker
	 * for the last dynamic rule.
	 */
	dst->next = (ipfw_dyn_rule *)1;
}

static void
dyn_export_data(const struct dyn_data *data, uint16_t kidx, uint8_t type,
    uint8_t set, ipfw_dyn_rule *dst)
{

	dst->dyn_type = type;
	dst->kidx = kidx;
	dst->pcnt = data->pcnt_fwd + data->pcnt_rev;
	dst->bcnt = data->bcnt_fwd + data->bcnt_rev;
	dst->expire = TIME_LEQ(data->expire, time_uptime) ?  0:
	    data->expire - time_uptime;

	/* 'rule' is used to pass up the rule number and set */
	memcpy(&dst->rule, &data->rulenum, sizeof(data->rulenum));

	/* store set number into high word of dst->rule pointer. */
	memcpy((char *)&dst->rule + sizeof(data->rulenum), &set, sizeof(set));

	dst->state = data->state;
	if (data->flags & DYN_REFERENCED)
		dst->state |= IPFW_DYN_ORPHANED;

	/* unused fields */
	dst->parent = NULL;
	dst->ack_fwd = data->ack_fwd;
	dst->ack_rev = data->ack_rev;
	dst->count = 0;
	dst->bucket = data->hashval;
	/*
	 * The legacy userland code will interpret a NULL here as a marker
	 * for the last dynamic rule.
	 */
	dst->next = (ipfw_dyn_rule *)1;
}

static void
dyn_export_ipv4_state(const struct dyn_ipv4_state *s, ipfw_dyn_rule *dst)
{
	struct ip_fw *rule;

	switch (s->type) {
	case O_LIMIT_PARENT:
		rule = s->limit->parent;
		dyn_export_parent(s->limit, s->kidx, rule->set, dst);
		break;
	default:
		rule = s->data->parent;
		if (s->type == O_LIMIT)
			rule = ((struct dyn_ipv4_state *)rule)->limit->parent;
		dyn_export_data(s->data, s->kidx, s->type, rule->set, dst);
	}

	dst->id.dst_ip = s->dst;
	dst->id.src_ip = s->src;
	dst->id.dst_port = s->dport;
	dst->id.src_port = s->sport;
	dst->id.fib = s->data->fibnum;
	dst->id.proto = s->proto;
	dst->id._flags = 0;
	dst->id.addr_type = 4;

	memset(&dst->id.dst_ip6, 0, sizeof(dst->id.dst_ip6));
	memset(&dst->id.src_ip6, 0, sizeof(dst->id.src_ip6));
	dst->id.flow_id6 = dst->id.extra = 0;
}

#ifdef INET6
static void
dyn_export_ipv6_state(const struct dyn_ipv6_state *s, ipfw_dyn_rule *dst)
{
	struct ip_fw *rule;

	switch (s->type) {
	case O_LIMIT_PARENT:
		rule = s->limit->parent;
		dyn_export_parent(s->limit, s->kidx, rule->set, dst);
		break;
	default:
		rule = s->data->parent;
		if (s->type == O_LIMIT)
			rule = ((struct dyn_ipv6_state *)rule)->limit->parent;
		dyn_export_data(s->data, s->kidx, s->type, rule->set, dst);
	}

	dst->id.src_ip6 = s->src;
	dst->id.dst_ip6 = s->dst;
	dst->id.dst_port = s->dport;
	dst->id.src_port = s->sport;
	dst->id.fib = s->data->fibnum;
	dst->id.proto = s->proto;
	dst->id._flags = 0;
	dst->id.addr_type = 6;

	dst->id.dst_ip = dst->id.src_ip = 0;
	dst->id.flow_id6 = dst->id.extra = 0;
}
#endif /* INET6 */

/*
 * Fills the buffer given by @sd with dynamic states.
 * Used by dump format v1 (current).
 *
 * Returns 0 on success.
 */
int
ipfw_dump_states(struct ip_fw_chain *chain, struct sockopt_data *sd)
{
#ifdef INET6
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4_state *s4;
	ipfw_obj_dyntlv *dst, *last;
	ipfw_obj_ctlv *ctlv;
	uint32_t bucket;

	if (V_dyn_count == 0)
		return (0);

	/*
	 * IPFW_UH_RLOCK garantees that another userland request
	 * and callout thread will not delete entries from states
	 * lists.
	 */
	IPFW_UH_RLOCK_ASSERT(chain);

	ctlv = (ipfw_obj_ctlv *)ipfw_get_sopt_space(sd, sizeof(*ctlv));
	if (ctlv == NULL)
		return (ENOMEM);
	ctlv->head.type = IPFW_TLV_DYNSTATE_LIST;
	ctlv->objsize = sizeof(ipfw_obj_dyntlv);
	last = NULL;

#define	DYN_EXPORT_STATES(s, af, h, b)				\
	CK_SLIST_FOREACH(s, &V_dyn_ ## h[b], entry) {			\
		dst = (ipfw_obj_dyntlv *)ipfw_get_sopt_space(sd,	\
		    sizeof(ipfw_obj_dyntlv));				\
		if (dst == NULL)					\
			return (ENOMEM);				\
		dyn_export_ ## af ## _state(s, &dst->state);		\
		dst->head.length = sizeof(ipfw_obj_dyntlv);		\
		dst->head.type = IPFW_TLV_DYN_ENT;			\
		last = dst;						\
	}

	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_EXPORT_STATES(s4, ipv4, ipv4_parent, bucket);
		DYN_EXPORT_STATES(s4, ipv4, ipv4, bucket);
#ifdef INET6
		DYN_EXPORT_STATES(s6, ipv6, ipv6_parent, bucket);
		DYN_EXPORT_STATES(s6, ipv6, ipv6, bucket);
#endif /* INET6 */
	}

	/* mark last dynamic rule */
	if (last != NULL)
		last->head.flags = IPFW_DF_LAST; /* XXX: unused */
	return (0);
#undef DYN_EXPORT_STATES
}

/*
 * Fill given buffer with dynamic states (legacy format).
 * IPFW_UH_RLOCK has to be held while calling.
 */
void
ipfw_get_dynamic(struct ip_fw_chain *chain, char **pbp, const char *ep)
{
#ifdef INET6
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4_state *s4;
	ipfw_dyn_rule *p, *last = NULL;
	char *bp;
	uint32_t bucket;

	if (V_dyn_count == 0)
		return;
	bp = *pbp;

	IPFW_UH_RLOCK_ASSERT(chain);

#define	DYN_EXPORT_STATES(s, af, head, b)				\
	CK_SLIST_FOREACH(s, &V_dyn_ ## head[b], entry) {		\
		if (bp + sizeof(*p) > ep)				\
			break;						\
		p = (ipfw_dyn_rule *)bp;				\
		dyn_export_ ## af ## _state(s, p);			\
		last = p;						\
		bp += sizeof(*p);					\
	}

	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_EXPORT_STATES(s4, ipv4, ipv4_parent, bucket);
		DYN_EXPORT_STATES(s4, ipv4, ipv4, bucket);
#ifdef INET6
		DYN_EXPORT_STATES(s6, ipv6, ipv6_parent, bucket);
		DYN_EXPORT_STATES(s6, ipv6, ipv6, bucket);
#endif /* INET6 */
	}

	if (last != NULL) /* mark last dynamic rule */
		last->next = NULL;
	*pbp = bp;
#undef DYN_EXPORT_STATES
}

void
ipfw_dyn_init(struct ip_fw_chain *chain)
{

#ifdef IPFIREWALL_JENKINSHASH
	V_dyn_hashseed = arc4random();
#endif
	V_dyn_max = 16384;		/* max # of states */
	V_dyn_parent_max = 4096;	/* max # of parent states */
	V_dyn_buckets_max = 8192;	/* must be power of 2 */

	V_dyn_ack_lifetime = 300;
	V_dyn_syn_lifetime = 20;
	V_dyn_fin_lifetime = 1;
	V_dyn_rst_lifetime = 1;
	V_dyn_udp_lifetime = 10;
	V_dyn_short_lifetime = 5;

	V_dyn_keepalive_interval = 20;
	V_dyn_keepalive_period = 5;
	V_dyn_keepalive = 1;		/* send keepalives */
	V_dyn_keepalive_last = time_uptime;

	V_dyn_data_zone = uma_zcreate("IPFW dynamic states data",
	    sizeof(struct dyn_data), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	uma_zone_set_max(V_dyn_data_zone, V_dyn_max);

	V_dyn_parent_zone = uma_zcreate("IPFW parent dynamic states",
	    sizeof(struct dyn_parent), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	uma_zone_set_max(V_dyn_parent_zone, V_dyn_parent_max);

	SLIST_INIT(&V_dyn_expired_ipv4);
	V_dyn_ipv4 = NULL;
	V_dyn_ipv4_parent = NULL;
	V_dyn_ipv4_zone = uma_zcreate("IPFW IPv4 dynamic states",
	    sizeof(struct dyn_ipv4_state), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);

#ifdef INET6
	SLIST_INIT(&V_dyn_expired_ipv6);
	V_dyn_ipv6 = NULL;
	V_dyn_ipv6_parent = NULL;
	V_dyn_ipv6_zone = uma_zcreate("IPFW IPv6 dynamic states",
	    sizeof(struct dyn_ipv6_state), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
#endif

	/* Initialize buckets. */
	V_curr_dyn_buckets = 0;
	V_dyn_bucket_lock = NULL;
	dyn_grow_hashtable(chain, 256);

	if (IS_DEFAULT_VNET(curvnet))
		dyn_hp_cache = malloc(mp_ncpus * sizeof(void *), M_IPFW,
		    M_WAITOK | M_ZERO);

	DYN_EXPIRED_LOCK_INIT();
	callout_init(&V_dyn_timeout, 1);
	callout_reset(&V_dyn_timeout, hz, dyn_tick, curvnet);
	IPFW_ADD_OBJ_REWRITER(IS_DEFAULT_VNET(curvnet), dyn_opcodes);
}

void
ipfw_dyn_uninit(int pass)
{
#ifdef INET6
	struct dyn_ipv6_state *s6;
#endif
	struct dyn_ipv4_state *s4;
	int bucket;

	if (pass == 0) {
		callout_drain(&V_dyn_timeout);
		return;
	}
	IPFW_DEL_OBJ_REWRITER(IS_DEFAULT_VNET(curvnet), dyn_opcodes);
	DYN_EXPIRED_LOCK_DESTROY();

#define	DYN_FREE_STATES_FORCED(CK, s, af, name, en)	do {		\
	while ((s = CK ## SLIST_FIRST(&V_dyn_ ## name)) != NULL) {	\
		CK ## SLIST_REMOVE_HEAD(&V_dyn_ ## name, en);	\
		if (s->type == O_LIMIT_PARENT)				\
			uma_zfree(V_dyn_parent_zone, s->limit);		\
		else							\
			uma_zfree(V_dyn_data_zone, s->data);		\
		uma_zfree(V_dyn_ ## af ## _zone, s);			\
	}								\
} while (0)
	for (bucket = 0; bucket < V_curr_dyn_buckets; bucket++) {
		DYN_BUCKET_LOCK_DESTROY(V_dyn_bucket_lock, bucket);

		DYN_FREE_STATES_FORCED(CK_, s4, ipv4, ipv4[bucket], entry);
		DYN_FREE_STATES_FORCED(CK_, s4, ipv4, ipv4_parent[bucket],
		    entry);
#ifdef INET6
		DYN_FREE_STATES_FORCED(CK_, s6, ipv6, ipv6[bucket], entry);
		DYN_FREE_STATES_FORCED(CK_, s6, ipv6, ipv6_parent[bucket],
		    entry);
#endif /* INET6 */
	}
	DYN_FREE_STATES_FORCED(, s4, ipv4, expired_ipv4, expired);
#ifdef INET6
	DYN_FREE_STATES_FORCED(, s6, ipv6, expired_ipv6, expired);
#endif
#undef DYN_FREE_STATES_FORCED

	uma_zdestroy(V_dyn_ipv4_zone);
	uma_zdestroy(V_dyn_data_zone);
	uma_zdestroy(V_dyn_parent_zone);
#ifdef INET6
	uma_zdestroy(V_dyn_ipv6_zone);
	free(V_dyn_ipv6, M_IPFW);
	free(V_dyn_ipv6_parent, M_IPFW);
	free(V_dyn_ipv6_add, M_IPFW);
	free(V_dyn_ipv6_parent_add, M_IPFW);
	free(V_dyn_ipv6_del, M_IPFW);
	free(V_dyn_ipv6_parent_del, M_IPFW);
#endif
	free(V_dyn_bucket_lock, M_IPFW);
	free(V_dyn_ipv4, M_IPFW);
	free(V_dyn_ipv4_parent, M_IPFW);
	free(V_dyn_ipv4_add, M_IPFW);
	free(V_dyn_ipv4_parent_add, M_IPFW);
	free(V_dyn_ipv4_del, M_IPFW);
	free(V_dyn_ipv4_parent_del, M_IPFW);
	if (IS_DEFAULT_VNET(curvnet))
		free(dyn_hp_cache, M_IPFW);
}


