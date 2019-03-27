/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008 Sam Leffler, Errno Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_FREEBSD_H_
#define _NET80211_IEEE80211_FREEBSD_H_

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/time.h>

/*
 * Common state locking definitions.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_com_lock" */
	struct mtx	mtx;
} ieee80211_com_lock_t;
#define	IEEE80211_LOCK_INIT(_ic, _name) do {				\
	ieee80211_com_lock_t *cl = &(_ic)->ic_comlock;			\
	snprintf(cl->name, sizeof(cl->name), "%s_com_lock", _name);	\
	mtx_init(&cl->mtx, cl->name, NULL, MTX_DEF | MTX_RECURSE);	\
} while (0)
#define	IEEE80211_LOCK_OBJ(_ic)	(&(_ic)->ic_comlock.mtx)
#define	IEEE80211_LOCK_DESTROY(_ic) mtx_destroy(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_LOCK(_ic)	   mtx_lock(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_UNLOCK(_ic)	   mtx_unlock(IEEE80211_LOCK_OBJ(_ic))
#define	IEEE80211_LOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_LOCK_OBJ(_ic), MA_OWNED)
#define	IEEE80211_UNLOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_LOCK_OBJ(_ic), MA_NOTOWNED)

/*
 * Transmit lock.
 *
 * This is a (mostly) temporary lock designed to serialise all of the
 * transmission operations throughout the stack.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_tx_lock" */
	struct mtx	mtx;
} ieee80211_tx_lock_t;
#define	IEEE80211_TX_LOCK_INIT(_ic, _name) do {				\
	ieee80211_tx_lock_t *cl = &(_ic)->ic_txlock;			\
	snprintf(cl->name, sizeof(cl->name), "%s_tx_lock", _name);	\
	mtx_init(&cl->mtx, cl->name, NULL, MTX_DEF);	\
} while (0)
#define	IEEE80211_TX_LOCK_OBJ(_ic)	(&(_ic)->ic_txlock.mtx)
#define	IEEE80211_TX_LOCK_DESTROY(_ic) mtx_destroy(IEEE80211_TX_LOCK_OBJ(_ic))
#define	IEEE80211_TX_LOCK(_ic)	   mtx_lock(IEEE80211_TX_LOCK_OBJ(_ic))
#define	IEEE80211_TX_UNLOCK(_ic)	   mtx_unlock(IEEE80211_TX_LOCK_OBJ(_ic))
#define	IEEE80211_TX_LOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_TX_LOCK_OBJ(_ic), MA_OWNED)
#define	IEEE80211_TX_UNLOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_TX_LOCK_OBJ(_ic), MA_NOTOWNED)

/*
 * Stageq / ni_tx_superg lock
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_ff_lock" */
	struct mtx	mtx;
} ieee80211_ff_lock_t;
#define IEEE80211_FF_LOCK_INIT(_ic, _name) do {				\
	ieee80211_ff_lock_t *fl = &(_ic)->ic_fflock;			\
	snprintf(fl->name, sizeof(fl->name), "%s_ff_lock", _name);	\
	mtx_init(&fl->mtx, fl->name, NULL, MTX_DEF);			\
} while (0)
#define IEEE80211_FF_LOCK_OBJ(_ic)	(&(_ic)->ic_fflock.mtx)
#define IEEE80211_FF_LOCK_DESTROY(_ic)	mtx_destroy(IEEE80211_FF_LOCK_OBJ(_ic))
#define IEEE80211_FF_LOCK(_ic)		mtx_lock(IEEE80211_FF_LOCK_OBJ(_ic))
#define IEEE80211_FF_UNLOCK(_ic)	mtx_unlock(IEEE80211_FF_LOCK_OBJ(_ic))
#define IEEE80211_FF_LOCK_ASSERT(_ic) \
	mtx_assert(IEEE80211_FF_LOCK_OBJ(_ic), MA_OWNED)

/*
 * Node locking definitions.
 */
typedef struct {
	char		name[16];		/* e.g. "ath0_node_lock" */
	struct mtx	mtx;
} ieee80211_node_lock_t;
#define	IEEE80211_NODE_LOCK_INIT(_nt, _name) do {			\
	ieee80211_node_lock_t *nl = &(_nt)->nt_nodelock;		\
	snprintf(nl->name, sizeof(nl->name), "%s_node_lock", _name);	\
	mtx_init(&nl->mtx, nl->name, NULL, MTX_DEF | MTX_RECURSE);	\
} while (0)
#define	IEEE80211_NODE_LOCK_OBJ(_nt)	(&(_nt)->nt_nodelock.mtx)
#define	IEEE80211_NODE_LOCK_DESTROY(_nt) \
	mtx_destroy(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_LOCK(_nt) \
	mtx_lock(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_IS_LOCKED(_nt) \
	mtx_owned(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_UNLOCK(_nt) \
	mtx_unlock(IEEE80211_NODE_LOCK_OBJ(_nt))
#define	IEEE80211_NODE_LOCK_ASSERT(_nt)	\
	mtx_assert(IEEE80211_NODE_LOCK_OBJ(_nt), MA_OWNED)

/*
 * Power-save queue definitions. 
 */
typedef struct mtx ieee80211_psq_lock_t;
#define	IEEE80211_PSQ_INIT(_psq, _name) \
	mtx_init(&(_psq)->psq_lock, _name, "802.11 ps q", MTX_DEF)
#define	IEEE80211_PSQ_DESTROY(_psq)	mtx_destroy(&(_psq)->psq_lock)
#define	IEEE80211_PSQ_LOCK(_psq)	mtx_lock(&(_psq)->psq_lock)
#define	IEEE80211_PSQ_UNLOCK(_psq)	mtx_unlock(&(_psq)->psq_lock)

#ifndef IF_PREPEND_LIST
#define _IF_PREPEND_LIST(ifq, mhead, mtail, mcount) do {	\
	(mtail)->m_nextpkt = (ifq)->ifq_head;			\
	if ((ifq)->ifq_tail == NULL)				\
		(ifq)->ifq_tail = (mtail);			\
	(ifq)->ifq_head = (mhead);				\
	(ifq)->ifq_len += (mcount);				\
} while (0)
#define IF_PREPEND_LIST(ifq, mhead, mtail, mcount) do {		\
	IF_LOCK(ifq);						\
	_IF_PREPEND_LIST(ifq, mhead, mtail, mcount);		\
	IF_UNLOCK(ifq);						\
} while (0)
#endif /* IF_PREPEND_LIST */
 
/*
 * Age queue definitions.
 */
typedef struct mtx ieee80211_ageq_lock_t;
#define	IEEE80211_AGEQ_INIT(_aq, _name) \
	mtx_init(&(_aq)->aq_lock, _name, "802.11 age q", MTX_DEF)
#define	IEEE80211_AGEQ_DESTROY(_aq)	mtx_destroy(&(_aq)->aq_lock)
#define	IEEE80211_AGEQ_LOCK(_aq)	mtx_lock(&(_aq)->aq_lock)
#define	IEEE80211_AGEQ_UNLOCK(_aq)	mtx_unlock(&(_aq)->aq_lock)

/*
 * 802.1x MAC ACL database locking definitions.
 */
typedef struct mtx acl_lock_t;
#define	ACL_LOCK_INIT(_as, _name) \
	mtx_init(&(_as)->as_lock, _name, "802.11 ACL", MTX_DEF)
#define	ACL_LOCK_DESTROY(_as)		mtx_destroy(&(_as)->as_lock)
#define	ACL_LOCK(_as)			mtx_lock(&(_as)->as_lock)
#define	ACL_UNLOCK(_as)			mtx_unlock(&(_as)->as_lock)
#define	ACL_LOCK_ASSERT(_as) \
	mtx_assert((&(_as)->as_lock), MA_OWNED)

/*
 * Scan table definitions.
 */
typedef struct mtx ieee80211_scan_table_lock_t;
#define	IEEE80211_SCAN_TABLE_LOCK_INIT(_st, _name) \
	mtx_init(&(_st)->st_lock, _name, "802.11 scan table", MTX_DEF)
#define	IEEE80211_SCAN_TABLE_LOCK_DESTROY(_st)	mtx_destroy(&(_st)->st_lock)
#define	IEEE80211_SCAN_TABLE_LOCK(_st)		mtx_lock(&(_st)->st_lock)
#define	IEEE80211_SCAN_TABLE_UNLOCK(_st)	mtx_unlock(&(_st)->st_lock)

typedef struct mtx ieee80211_scan_iter_lock_t;
#define	IEEE80211_SCAN_ITER_LOCK_INIT(_st, _name) \
	mtx_init(&(_st)->st_scanlock, _name, "802.11 scangen", MTX_DEF)
#define	IEEE80211_SCAN_ITER_LOCK_DESTROY(_st)	mtx_destroy(&(_st)->st_scanlock)
#define	IEEE80211_SCAN_ITER_LOCK(_st)		mtx_lock(&(_st)->st_scanlock)
#define	IEEE80211_SCAN_ITER_UNLOCK(_st)	mtx_unlock(&(_st)->st_scanlock)

/*
 * Mesh node/routing definitions.
 */
typedef struct mtx ieee80211_rte_lock_t;
#define	MESH_RT_ENTRY_LOCK_INIT(_rt, _name) \
	mtx_init(&(rt)->rt_lock, _name, "802.11s route entry", MTX_DEF)
#define	MESH_RT_ENTRY_LOCK_DESTROY(_rt) \
	mtx_destroy(&(_rt)->rt_lock)
#define	MESH_RT_ENTRY_LOCK(rt)	mtx_lock(&(rt)->rt_lock)
#define	MESH_RT_ENTRY_LOCK_ASSERT(rt) mtx_assert(&(rt)->rt_lock, MA_OWNED)
#define	MESH_RT_ENTRY_UNLOCK(rt)	mtx_unlock(&(rt)->rt_lock)

typedef struct mtx ieee80211_rt_lock_t;
#define	MESH_RT_LOCK(ms)	mtx_lock(&(ms)->ms_rt_lock)
#define	MESH_RT_LOCK_ASSERT(ms)	mtx_assert(&(ms)->ms_rt_lock, MA_OWNED)
#define	MESH_RT_UNLOCK(ms)	mtx_unlock(&(ms)->ms_rt_lock)
#define	MESH_RT_LOCK_INIT(ms, name) \
	mtx_init(&(ms)->ms_rt_lock, name, "802.11s routing table", MTX_DEF)
#define	MESH_RT_LOCK_DESTROY(ms) \
	mtx_destroy(&(ms)->ms_rt_lock)

/*
 * Node reference counting definitions.
 *
 * ieee80211_node_initref	initialize the reference count to 1
 * ieee80211_node_incref	add a reference
 * ieee80211_node_decref	remove a reference
 * ieee80211_node_dectestref	remove a reference and return 1 if this
 *				is the last reference, otherwise 0
 * ieee80211_node_refcnt	reference count for printing (only)
 */
#include <machine/atomic.h>

struct ieee80211vap;
int	ieee80211_com_vincref(struct ieee80211vap *);
void	ieee80211_com_vdecref(struct ieee80211vap *);
void	ieee80211_com_vdetach(struct ieee80211vap *);

#define ieee80211_node_initref(_ni) \
	do { ((_ni)->ni_refcnt = 1); } while (0)
#define ieee80211_node_incref(_ni) \
	atomic_add_int(&(_ni)->ni_refcnt, 1)
#define	ieee80211_node_decref(_ni) \
	atomic_subtract_int(&(_ni)->ni_refcnt, 1)
struct ieee80211_node;
int	ieee80211_node_dectestref(struct ieee80211_node *ni);
#define	ieee80211_node_refcnt(_ni)	(_ni)->ni_refcnt

struct ifqueue;
void	ieee80211_drain_ifq(struct ifqueue *);
void	ieee80211_flush_ifq(struct ifqueue *, struct ieee80211vap *);

void	ieee80211_vap_destroy(struct ieee80211vap *);

#define	IFNET_IS_UP_RUNNING(_ifp) \
	(((_ifp)->if_flags & IFF_UP) && \
	 ((_ifp)->if_drv_flags & IFF_DRV_RUNNING))

#define	msecs_to_ticks(ms)	MSEC_2_TICKS(ms)
#define	ticks_to_msecs(t)	TICKS_2_MSEC(t)
#define	ticks_to_secs(t)	((t) / hz)

#define ieee80211_time_after(a,b) 	((long)(b) - (long)(a) < 0)
#define ieee80211_time_before(a,b)	ieee80211_time_after(b,a)
#define ieee80211_time_after_eq(a,b)	((long)(a) - (long)(b) >= 0)
#define ieee80211_time_before_eq(a,b)	ieee80211_time_after_eq(b,a)

struct mbuf *ieee80211_getmgtframe(uint8_t **frm, int headroom, int pktlen);

/* tx path usage */
#define	M_ENCAP		M_PROTO1		/* 802.11 encap done */
#define	M_EAPOL		M_PROTO3		/* PAE/EAPOL frame */
#define	M_PWR_SAV	M_PROTO4		/* bypass PS handling */
#define	M_MORE_DATA	M_PROTO5		/* more data frames to follow */
#define	M_FF		M_PROTO6		/* fast frame / A-MSDU */
#define	M_TXCB		M_PROTO7		/* do tx complete callback */
#define	M_AMPDU_MPDU	M_PROTO8		/* ok for A-MPDU aggregation */
#define	M_FRAG		M_PROTO9		/* frame fragmentation */
#define	M_FIRSTFRAG	M_PROTO10		/* first frame fragment */
#define	M_LASTFRAG	M_PROTO11		/* last frame fragment */

#define	M_80211_TX \
	(M_ENCAP|M_EAPOL|M_PWR_SAV|M_MORE_DATA|M_FF|M_TXCB| \
	 M_AMPDU_MPDU|M_FRAG|M_FIRSTFRAG|M_LASTFRAG)

/* rx path usage */
#define	M_AMPDU		M_PROTO1		/* A-MPDU subframe */
#define	M_WEP		M_PROTO2		/* WEP done by hardware */
#if 0
#define	M_AMPDU_MPDU	M_PROTO8		/* A-MPDU re-order done */
#endif
#define	M_80211_RX	(M_AMPDU|M_WEP|M_AMPDU_MPDU)

#define	IEEE80211_MBUF_TX_FLAG_BITS \
	M_FLAG_BITS \
	"\15M_ENCAP\17M_EAPOL\20M_PWR_SAV\21M_MORE_DATA\22M_FF\23M_TXCB" \
	"\24M_AMPDU_MPDU\25M_FRAG\26M_FIRSTFRAG\27M_LASTFRAG"

#define	IEEE80211_MBUF_RX_FLAG_BITS \
	M_FLAG_BITS \
	"\15M_AMPDU\16M_WEP\24M_AMPDU_MPDU"

/*
 * Store WME access control bits in the vlan tag.
 * This is safe since it's done after the packet is classified
 * (where we use any previous tag) and because it's passed
 * directly in to the driver and there's no chance someone
 * else will clobber them on us.
 */
#define	M_WME_SETAC(m, ac) \
	((m)->m_pkthdr.ether_vtag = (ac))
#define	M_WME_GETAC(m)	((m)->m_pkthdr.ether_vtag)

/*
 * Mbufs on the power save queue are tagged with an age and
 * timed out.  We reuse the hardware checksum field in the
 * mbuf packet header to store this data.
 */
#define	M_AGE_SET(m,v)		(m->m_pkthdr.csum_data = v)
#define	M_AGE_GET(m)		(m->m_pkthdr.csum_data)
#define	M_AGE_SUB(m,adj)	(m->m_pkthdr.csum_data -= adj)

/*
 * Store the sequence number.
 */
#define	M_SEQNO_SET(m, seqno) \
	((m)->m_pkthdr.tso_segsz = (seqno))
#define	M_SEQNO_GET(m)	((m)->m_pkthdr.tso_segsz)

#define	MTAG_ABI_NET80211	1132948340	/* net80211 ABI */

struct ieee80211_cb {
	void	(*func)(struct ieee80211_node *, void *, int status);
	void	*arg;
};
#define	NET80211_TAG_CALLBACK	0	/* xmit complete callback */
int	ieee80211_add_callback(struct mbuf *m,
		void (*func)(struct ieee80211_node *, void *, int), void *arg);
void	ieee80211_process_callback(struct ieee80211_node *, struct mbuf *, int);

#define	NET80211_TAG_XMIT_PARAMS	1
/* See below; this is after the bpf_params definition */

#define	NET80211_TAG_RECV_PARAMS	2

#define	NET80211_TAG_TOA_PARAMS		3

struct ieee80211com;
int	ieee80211_parent_xmitpkt(struct ieee80211com *, struct mbuf *);
int	ieee80211_vap_xmitpkt(struct ieee80211vap *, struct mbuf *);

void	get_random_bytes(void *, size_t);

void	ieee80211_sysctl_attach(struct ieee80211com *);
void	ieee80211_sysctl_detach(struct ieee80211com *);
void	ieee80211_sysctl_vattach(struct ieee80211vap *);
void	ieee80211_sysctl_vdetach(struct ieee80211vap *);

SYSCTL_DECL(_net_wlan);
int	ieee80211_sysctl_msecs_ticks(SYSCTL_HANDLER_ARGS);

void	ieee80211_load_module(const char *);

/*
 * A "policy module" is an adjunct module to net80211 that provides
 * functionality that typically includes policy decisions.  This
 * modularity enables extensibility and vendor-supplied functionality.
 */
#define	_IEEE80211_POLICY_MODULE(policy, name, version)			\
typedef void (*policy##_setup)(int);					\
SET_DECLARE(policy##_set, policy##_setup);				\
static int								\
wlan_##name##_modevent(module_t mod, int type, void *unused)		\
{									\
	policy##_setup * const *iter, f;				\
	switch (type) {							\
	case MOD_LOAD:							\
		SET_FOREACH(iter, policy##_set) {			\
			f = (void*) *iter;				\
			f(type);					\
		}							\
		return 0;						\
	case MOD_UNLOAD:						\
	case MOD_QUIESCE:						\
		if (nrefs) {						\
			printf("wlan_" #name ": still in use "		\
				"(%u dynamic refs)\n", nrefs);		\
			return EBUSY;					\
		}							\
		if (type == MOD_UNLOAD) {				\
			SET_FOREACH(iter, policy##_set) {		\
				f = (void*) *iter;			\
				f(type);				\
			}						\
		}							\
		return 0;						\
	}								\
	return EINVAL;							\
}									\
static moduledata_t name##_mod = {					\
	"wlan_" #name,							\
	wlan_##name##_modevent,						\
	0								\
};									\
DECLARE_MODULE(wlan_##name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);\
MODULE_VERSION(wlan_##name, version);					\
MODULE_DEPEND(wlan_##name, wlan, 1, 1, 1)

/*
 * Crypto modules implement cipher support.
 */
#define	IEEE80211_CRYPTO_MODULE(name, version)				\
_IEEE80211_POLICY_MODULE(crypto, name, version);			\
static void								\
name##_modevent(int type)						\
{									\
	if (type == MOD_LOAD)						\
		ieee80211_crypto_register(&name);			\
	else								\
		ieee80211_crypto_unregister(&name);			\
}									\
TEXT_SET(crypto##_set, name##_modevent)

/*
 * Scanner modules provide scanning policy.
 */
#define	IEEE80211_SCANNER_MODULE(name, version)				\
	_IEEE80211_POLICY_MODULE(scanner, name, version)

#define	IEEE80211_SCANNER_ALG(name, alg, v)				\
static void								\
name##_modevent(int type)						\
{									\
	if (type == MOD_LOAD)						\
		ieee80211_scanner_register(alg, &v);			\
	else								\
		ieee80211_scanner_unregister(alg, &v);			\
}									\
TEXT_SET(scanner_set, name##_modevent);					\

/*
 * ACL modules implement acl policy.
 */
#define	IEEE80211_ACL_MODULE(name, alg, version)			\
_IEEE80211_POLICY_MODULE(acl, name, version);				\
static void								\
alg##_modevent(int type)						\
{									\
	if (type == MOD_LOAD)						\
		ieee80211_aclator_register(&alg);			\
	else								\
		ieee80211_aclator_unregister(&alg);			\
}									\
TEXT_SET(acl_set, alg##_modevent);					\

/*
 * Authenticator modules handle 802.1x/WPA authentication.
 */
#define	IEEE80211_AUTH_MODULE(name, version)				\
	_IEEE80211_POLICY_MODULE(auth, name, version)

#define	IEEE80211_AUTH_ALG(name, alg, v)				\
static void								\
name##_modevent(int type)						\
{									\
	if (type == MOD_LOAD)						\
		ieee80211_authenticator_register(alg, &v);		\
	else								\
		ieee80211_authenticator_unregister(alg);		\
}									\
TEXT_SET(auth_set, name##_modevent)

/*
 * Rate control modules provide tx rate control support.
 */
#define	IEEE80211_RATECTL_MODULE(alg, version)				\
	_IEEE80211_POLICY_MODULE(ratectl, alg, version);		\

#define	IEEE80211_RATECTL_ALG(name, alg, v)				\
static void								\
alg##_modevent(int type)						\
{									\
	if (type == MOD_LOAD)						\
		ieee80211_ratectl_register(alg, &v);			\
	else								\
		ieee80211_ratectl_unregister(alg);			\
}									\
TEXT_SET(ratectl##_set, alg##_modevent)

struct ieee80211req;
typedef int ieee80211_ioctl_getfunc(struct ieee80211vap *,
    struct ieee80211req *);
SET_DECLARE(ieee80211_ioctl_getset, ieee80211_ioctl_getfunc);
#define	IEEE80211_IOCTL_GET(_name, _get) TEXT_SET(ieee80211_ioctl_getset, _get)

typedef int ieee80211_ioctl_setfunc(struct ieee80211vap *,
    struct ieee80211req *);
SET_DECLARE(ieee80211_ioctl_setset, ieee80211_ioctl_setfunc);
#define	IEEE80211_IOCTL_SET(_name, _set) TEXT_SET(ieee80211_ioctl_setset, _set)
#endif /* _KERNEL */

/* XXX this stuff belongs elsewhere */
/*
 * Message formats for messages from the net80211 layer to user
 * applications via the routing socket.  These messages are appended
 * to an if_announcemsghdr structure.
 */
struct ieee80211_join_event {
	uint8_t		iev_addr[6];
};

struct ieee80211_leave_event {
	uint8_t		iev_addr[6];
};

struct ieee80211_replay_event {
	uint8_t		iev_src[6];	/* src MAC */
	uint8_t		iev_dst[6];	/* dst MAC */
	uint8_t		iev_cipher;	/* cipher type */
	uint8_t		iev_keyix;	/* key id/index */
	uint64_t	iev_keyrsc;	/* RSC from key */
	uint64_t	iev_rsc;	/* RSC from frame */
};

struct ieee80211_michael_event {
	uint8_t		iev_src[6];	/* src MAC */
	uint8_t		iev_dst[6];	/* dst MAC */
	uint8_t		iev_cipher;	/* cipher type */
	uint8_t		iev_keyix;	/* key id/index */
};

struct ieee80211_wds_event {
	uint8_t		iev_addr[6];
};

struct ieee80211_csa_event {
	uint32_t	iev_flags;	/* channel flags */
	uint16_t	iev_freq;	/* setting in Mhz */
	uint8_t		iev_ieee;	/* IEEE channel number */
	uint8_t		iev_mode;	/* CSA mode */
	uint8_t		iev_count;	/* CSA count */
};

struct ieee80211_cac_event {
	uint32_t	iev_flags;	/* channel flags */
	uint16_t	iev_freq;	/* setting in Mhz */
	uint8_t		iev_ieee;	/* IEEE channel number */
	/* XXX timestamp? */
	uint8_t		iev_type;	/* IEEE80211_NOTIFY_CAC_* */
};

struct ieee80211_radar_event {
	uint32_t	iev_flags;	/* channel flags */
	uint16_t	iev_freq;	/* setting in Mhz */
	uint8_t		iev_ieee;	/* IEEE channel number */
	/* XXX timestamp? */
};

struct ieee80211_auth_event {
	uint8_t		iev_addr[6];
};

struct ieee80211_deauth_event {
	uint8_t		iev_addr[6];
};

struct ieee80211_country_event {
	uint8_t		iev_addr[6];
	uint8_t		iev_cc[2];	/* ISO country code */
};

struct ieee80211_radio_event {
	uint8_t		iev_state;	/* 1 on, 0 off */
};

#define	RTM_IEEE80211_ASSOC	100	/* station associate (bss mode) */
#define	RTM_IEEE80211_REASSOC	101	/* station re-associate (bss mode) */
#define	RTM_IEEE80211_DISASSOC	102	/* station disassociate (bss mode) */
#define	RTM_IEEE80211_JOIN	103	/* station join (ap mode) */
#define	RTM_IEEE80211_LEAVE	104	/* station leave (ap mode) */
#define	RTM_IEEE80211_SCAN	105	/* scan complete, results available */
#define	RTM_IEEE80211_REPLAY	106	/* sequence counter replay detected */
#define	RTM_IEEE80211_MICHAEL	107	/* Michael MIC failure detected */
#define	RTM_IEEE80211_REJOIN	108	/* station re-associate (ap mode) */
#define	RTM_IEEE80211_WDS	109	/* WDS discovery (ap mode) */
#define	RTM_IEEE80211_CSA	110	/* Channel Switch Announcement event */
#define	RTM_IEEE80211_RADAR	111	/* radar event */
#define	RTM_IEEE80211_CAC	112	/* Channel Availability Check event */
#define	RTM_IEEE80211_DEAUTH	113	/* station deauthenticate */
#define	RTM_IEEE80211_AUTH	114	/* station authenticate (ap mode) */
#define	RTM_IEEE80211_COUNTRY	115	/* discovered country code (sta mode) */
#define	RTM_IEEE80211_RADIO	116	/* RF kill switch state change */

/*
 * Structure prepended to raw packets sent through the bpf
 * interface when set to DLT_IEEE802_11_RADIO.  This allows
 * user applications to specify pretty much everything in
 * an Atheros tx descriptor.  XXX need to generalize.
 *
 * XXX cannot be more than 14 bytes as it is copied to a sockaddr's
 * XXX sa_data area.
 */
struct ieee80211_bpf_params {
	uint8_t		ibp_vers;	/* version */
#define	IEEE80211_BPF_VERSION	0
	uint8_t		ibp_len;	/* header length in bytes */
	uint8_t		ibp_flags;
#define	IEEE80211_BPF_SHORTPRE	0x01	/* tx with short preamble */
#define	IEEE80211_BPF_NOACK	0x02	/* tx with no ack */
#define	IEEE80211_BPF_CRYPTO	0x04	/* tx with h/w encryption */
#define	IEEE80211_BPF_FCS	0x10	/* frame incldues FCS */
#define	IEEE80211_BPF_DATAPAD	0x20	/* frame includes data padding */
#define	IEEE80211_BPF_RTS	0x40	/* tx with RTS/CTS */
#define	IEEE80211_BPF_CTS	0x80	/* tx with CTS only */
	uint8_t		ibp_pri;	/* WME/WMM AC+tx antenna */
	uint8_t		ibp_try0;	/* series 1 try count */
	uint8_t		ibp_rate0;	/* series 1 IEEE tx rate */
	uint8_t		ibp_power;	/* tx power (device units) */
	uint8_t		ibp_ctsrate;	/* IEEE tx rate for CTS */
	uint8_t		ibp_try1;	/* series 2 try count */
	uint8_t		ibp_rate1;	/* series 2 IEEE tx rate */
	uint8_t		ibp_try2;	/* series 3 try count */
	uint8_t		ibp_rate2;	/* series 3 IEEE tx rate */
	uint8_t		ibp_try3;	/* series 4 try count */
	uint8_t		ibp_rate3;	/* series 4 IEEE tx rate */
};

#ifdef _KERNEL
struct ieee80211_tx_params {
	struct ieee80211_bpf_params params;
};
int	ieee80211_add_xmit_params(struct mbuf *m,
	    const struct ieee80211_bpf_params *);
int	ieee80211_get_xmit_params(struct mbuf *m,
	    struct ieee80211_bpf_params *);

struct ieee80211_rx_params;
struct ieee80211_rx_stats;

int	ieee80211_add_rx_params(struct mbuf *m,
	    const struct ieee80211_rx_stats *rxs);
int	ieee80211_get_rx_params(struct mbuf *m,
	    struct ieee80211_rx_stats *rxs);
const struct ieee80211_rx_stats * ieee80211_get_rx_params_ptr(struct mbuf *m);

struct ieee80211_toa_params {
	int request_id;
};
int	ieee80211_add_toa_params(struct mbuf *m,
	    const struct ieee80211_toa_params *p);
int	ieee80211_get_toa_params(struct mbuf *m,
	    struct ieee80211_toa_params *p);

#define	IEEE80211_F_SURVEY_TIME		0x00000001
#define	IEEE80211_F_SURVEY_TIME_BUSY	0x00000002
#define	IEEE80211_F_SURVEY_NOISE_DBM	0x00000004
#define	IEEE80211_F_SURVEY_TSC		0x00000008
struct ieee80211_channel_survey {
	uint32_t s_flags;
	uint32_t s_time;
	uint32_t s_time_busy;
	int32_t s_noise;
	uint64_t s_tsc;
};

#endif /* _KERNEL */

/*
 * Malloc API.  Other BSD operating systems have slightly
 * different malloc/free namings (eg DragonflyBSD.)
 */
#define	IEEE80211_MALLOC	malloc
#define	IEEE80211_FREE		free

/* XXX TODO: get rid of WAITOK, fix all the users of it? */
#define	IEEE80211_M_NOWAIT	M_NOWAIT
#define	IEEE80211_M_WAITOK	M_WAITOK
#define	IEEE80211_M_ZERO	M_ZERO

/* XXX TODO: the type fields */

#endif /* _NET80211_IEEE80211_FREEBSD_H_ */
