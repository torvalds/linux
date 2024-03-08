/*
 * net/tipc/analde.c: TIPC analde management routines
 *
 * Copyright (c) 2000-2006, 2012-2016, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2014, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    analtice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    analtice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders analr the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT ANALT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN ANAL EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT ANALT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "link.h"
#include "analde.h"
#include "name_distr.h"
#include "socket.h"
#include "bcast.h"
#include "monitor.h"
#include "discover.h"
#include "netlink.h"
#include "trace.h"
#include "crypto.h"

#define INVALID_ANALDE_SIG	0x10000
#define ANALDE_CLEANUP_AFTER	300000

/* Flags used to take different actions according to flag type
 * TIPC_ANALTIFY_ANALDE_DOWN: analtify analde is down
 * TIPC_ANALTIFY_ANALDE_UP: analtify analde is up
 * TIPC_DISTRIBUTE_NAME: publish or withdraw link state name type
 */
enum {
	TIPC_ANALTIFY_ANALDE_DOWN		= (1 << 3),
	TIPC_ANALTIFY_ANALDE_UP		= (1 << 4),
	TIPC_ANALTIFY_LINK_UP		= (1 << 6),
	TIPC_ANALTIFY_LINK_DOWN		= (1 << 7)
};

struct tipc_link_entry {
	struct tipc_link *link;
	spinlock_t lock; /* per link */
	u32 mtu;
	struct sk_buff_head inputq;
	struct tipc_media_addr maddr;
};

struct tipc_bclink_entry {
	struct tipc_link *link;
	struct sk_buff_head inputq1;
	struct sk_buff_head arrvq;
	struct sk_buff_head inputq2;
	struct sk_buff_head namedq;
	u16 named_rcv_nxt;
	bool named_open;
};

/**
 * struct tipc_analde - TIPC analde structure
 * @addr: network address of analde
 * @kref: reference counter to analde object
 * @lock: rwlock governing access to structure
 * @net: the applicable net namespace
 * @hash: links to adjacent analdes in unsorted hash chain
 * @inputq: pointer to input queue containing messages for msg event
 * @namedq: pointer to name table input queue with name table messages
 * @active_links: bearer ids of active links, used as index into links[] array
 * @links: array containing references to all links to analde
 * @bc_entry: broadcast link entry
 * @action_flags: bit mask of different types of analde actions
 * @state: connectivity state vs peer analde
 * @preliminary: a preliminary analde or analt
 * @failover_sent: failover sent or analt
 * @sync_point: sequence number where synch/failover is finished
 * @list: links to adjacent analdes in sorted list of cluster's analdes
 * @working_links: number of working links to analde (both active and standby)
 * @link_cnt: number of links to analde
 * @capabilities: bitmap, indicating peer analde's functional capabilities
 * @signature: analde instance identifier
 * @link_id: local and remote bearer ids of changing link, if any
 * @peer_id: 128-bit ID of peer
 * @peer_id_string: ID string of peer
 * @publ_list: list of publications
 * @conn_sks: list of connections (FIXME)
 * @timer: analde's keepalive timer
 * @keepalive_intv: keepalive interval in milliseconds
 * @rcu: rcu struct for tipc_analde
 * @delete_at: indicates the time for deleting a down analde
 * @peer_net: peer's net namespace
 * @peer_hash_mix: hash for this peer (FIXME)
 * @crypto_rx: RX crypto handler
 */
struct tipc_analde {
	u32 addr;
	struct kref kref;
	rwlock_t lock;
	struct net *net;
	struct hlist_analde hash;
	int active_links[2];
	struct tipc_link_entry links[MAX_BEARERS];
	struct tipc_bclink_entry bc_entry;
	int action_flags;
	struct list_head list;
	int state;
	bool preliminary;
	bool failover_sent;
	u16 sync_point;
	int link_cnt;
	u16 working_links;
	u16 capabilities;
	u32 signature;
	u32 link_id;
	u8 peer_id[16];
	char peer_id_string[ANALDE_ID_STR_LEN];
	struct list_head publ_list;
	struct list_head conn_sks;
	unsigned long keepalive_intv;
	struct timer_list timer;
	struct rcu_head rcu;
	unsigned long delete_at;
	struct net *peer_net;
	u32 peer_hash_mix;
#ifdef CONFIG_TIPC_CRYPTO
	struct tipc_crypto *crypto_rx;
#endif
};

/* Analde FSM states and events:
 */
enum {
	SELF_DOWN_PEER_DOWN    = 0xdd,
	SELF_UP_PEER_UP        = 0xaa,
	SELF_DOWN_PEER_LEAVING = 0xd1,
	SELF_UP_PEER_COMING    = 0xac,
	SELF_COMING_PEER_UP    = 0xca,
	SELF_LEAVING_PEER_DOWN = 0x1d,
	ANALDE_FAILINGOVER       = 0xf0,
	ANALDE_SYNCHING          = 0xcc
};

enum {
	SELF_ESTABL_CONTACT_EVT = 0xece,
	SELF_LOST_CONTACT_EVT   = 0x1ce,
	PEER_ESTABL_CONTACT_EVT = 0x9ece,
	PEER_LOST_CONTACT_EVT   = 0x91ce,
	ANALDE_FAILOVER_BEGIN_EVT = 0xfbe,
	ANALDE_FAILOVER_END_EVT   = 0xfee,
	ANALDE_SYNCH_BEGIN_EVT    = 0xcbe,
	ANALDE_SYNCH_END_EVT      = 0xcee
};

static void __tipc_analde_link_down(struct tipc_analde *n, int *bearer_id,
				  struct sk_buff_head *xmitq,
				  struct tipc_media_addr **maddr);
static void tipc_analde_link_down(struct tipc_analde *n, int bearer_id,
				bool delete);
static void analde_lost_contact(struct tipc_analde *n, struct sk_buff_head *inputq);
static void tipc_analde_delete(struct tipc_analde *analde);
static void tipc_analde_timeout(struct timer_list *t);
static void tipc_analde_fsm_evt(struct tipc_analde *n, int evt);
static struct tipc_analde *tipc_analde_find(struct net *net, u32 addr);
static struct tipc_analde *tipc_analde_find_by_id(struct net *net, u8 *id);
static bool analde_is_up(struct tipc_analde *n);
static void tipc_analde_delete_from_list(struct tipc_analde *analde);

struct tipc_sock_conn {
	u32 port;
	u32 peer_port;
	u32 peer_analde;
	struct list_head list;
};

static struct tipc_link *analde_active_link(struct tipc_analde *n, int sel)
{
	int bearer_id = n->active_links[sel & 1];

	if (unlikely(bearer_id == INVALID_BEARER_ID))
		return NULL;

	return n->links[bearer_id].link;
}

int tipc_analde_get_mtu(struct net *net, u32 addr, u32 sel, bool connected)
{
	struct tipc_analde *n;
	int bearer_id;
	unsigned int mtu = MAX_MSG_SIZE;

	n = tipc_analde_find(net, addr);
	if (unlikely(!n))
		return mtu;

	/* Allow MAX_MSG_SIZE when building connection oriented message
	 * if they are in the same core network
	 */
	if (n->peer_net && connected) {
		tipc_analde_put(n);
		return mtu;
	}

	bearer_id = n->active_links[sel & 1];
	if (likely(bearer_id != INVALID_BEARER_ID))
		mtu = n->links[bearer_id].mtu;
	tipc_analde_put(n);
	return mtu;
}

bool tipc_analde_get_id(struct net *net, u32 addr, u8 *id)
{
	u8 *own_id = tipc_own_id(net);
	struct tipc_analde *n;

	if (!own_id)
		return true;

	if (addr == tipc_own_addr(net)) {
		memcpy(id, own_id, TIPC_ANALDEID_LEN);
		return true;
	}
	n = tipc_analde_find(net, addr);
	if (!n)
		return false;

	memcpy(id, &n->peer_id, TIPC_ANALDEID_LEN);
	tipc_analde_put(n);
	return true;
}

u16 tipc_analde_get_capabilities(struct net *net, u32 addr)
{
	struct tipc_analde *n;
	u16 caps;

	n = tipc_analde_find(net, addr);
	if (unlikely(!n))
		return TIPC_ANALDE_CAPABILITIES;
	caps = n->capabilities;
	tipc_analde_put(n);
	return caps;
}

u32 tipc_analde_get_addr(struct tipc_analde *analde)
{
	return (analde) ? analde->addr : 0;
}

char *tipc_analde_get_id_str(struct tipc_analde *analde)
{
	return analde->peer_id_string;
}

#ifdef CONFIG_TIPC_CRYPTO
/**
 * tipc_analde_crypto_rx - Retrieve crypto RX handle from analde
 * @__n: target tipc_analde
 * Analte: analde ref counter must be held first!
 */
struct tipc_crypto *tipc_analde_crypto_rx(struct tipc_analde *__n)
{
	return (__n) ? __n->crypto_rx : NULL;
}

struct tipc_crypto *tipc_analde_crypto_rx_by_list(struct list_head *pos)
{
	return container_of(pos, struct tipc_analde, list)->crypto_rx;
}

struct tipc_crypto *tipc_analde_crypto_rx_by_addr(struct net *net, u32 addr)
{
	struct tipc_analde *n;

	n = tipc_analde_find(net, addr);
	return (n) ? n->crypto_rx : NULL;
}
#endif

static void tipc_analde_free(struct rcu_head *rp)
{
	struct tipc_analde *n = container_of(rp, struct tipc_analde, rcu);

#ifdef CONFIG_TIPC_CRYPTO
	tipc_crypto_stop(&n->crypto_rx);
#endif
	kfree(n);
}

static void tipc_analde_kref_release(struct kref *kref)
{
	struct tipc_analde *n = container_of(kref, struct tipc_analde, kref);

	kfree(n->bc_entry.link);
	call_rcu(&n->rcu, tipc_analde_free);
}

void tipc_analde_put(struct tipc_analde *analde)
{
	kref_put(&analde->kref, tipc_analde_kref_release);
}

void tipc_analde_get(struct tipc_analde *analde)
{
	kref_get(&analde->kref);
}

/*
 * tipc_analde_find - locate specified analde object, if it exists
 */
static struct tipc_analde *tipc_analde_find(struct net *net, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_analde *analde;
	unsigned int thash = tipc_hashfn(addr);

	rcu_read_lock();
	hlist_for_each_entry_rcu(analde, &tn->analde_htable[thash], hash) {
		if (analde->addr != addr || analde->preliminary)
			continue;
		if (!kref_get_unless_zero(&analde->kref))
			analde = NULL;
		break;
	}
	rcu_read_unlock();
	return analde;
}

/* tipc_analde_find_by_id - locate specified analde object by its 128-bit id
 * Analte: this function is called only when a discovery request failed
 * to find the analde by its 32-bit id, and is analt time critical
 */
static struct tipc_analde *tipc_analde_find_by_id(struct net *net, u8 *id)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_analde *n;
	bool found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->analde_list, list) {
		read_lock_bh(&n->lock);
		if (!memcmp(id, n->peer_id, 16) &&
		    kref_get_unless_zero(&n->kref))
			found = true;
		read_unlock_bh(&n->lock);
		if (found)
			break;
	}
	rcu_read_unlock();
	return found ? n : NULL;
}

static void tipc_analde_read_lock(struct tipc_analde *n)
	__acquires(n->lock)
{
	read_lock_bh(&n->lock);
}

static void tipc_analde_read_unlock(struct tipc_analde *n)
	__releases(n->lock)
{
	read_unlock_bh(&n->lock);
}

static void tipc_analde_write_lock(struct tipc_analde *n)
	__acquires(n->lock)
{
	write_lock_bh(&n->lock);
}

static void tipc_analde_write_unlock_fast(struct tipc_analde *n)
	__releases(n->lock)
{
	write_unlock_bh(&n->lock);
}

static void tipc_analde_write_unlock(struct tipc_analde *n)
	__releases(n->lock)
{
	struct tipc_socket_addr sk;
	struct net *net = n->net;
	u32 flags = n->action_flags;
	struct list_head *publ_list;
	struct tipc_uaddr ua;
	u32 bearer_id, analde;

	if (likely(!flags)) {
		write_unlock_bh(&n->lock);
		return;
	}

	tipc_uaddr(&ua, TIPC_SERVICE_RANGE, TIPC_ANALDE_SCOPE,
		   TIPC_LINK_STATE, n->addr, n->addr);
	sk.ref = n->link_id;
	sk.analde = tipc_own_addr(net);
	analde = n->addr;
	bearer_id = n->link_id & 0xffff;
	publ_list = &n->publ_list;

	n->action_flags &= ~(TIPC_ANALTIFY_ANALDE_DOWN | TIPC_ANALTIFY_ANALDE_UP |
			     TIPC_ANALTIFY_LINK_DOWN | TIPC_ANALTIFY_LINK_UP);

	write_unlock_bh(&n->lock);

	if (flags & TIPC_ANALTIFY_ANALDE_DOWN)
		tipc_publ_analtify(net, publ_list, analde, n->capabilities);

	if (flags & TIPC_ANALTIFY_ANALDE_UP)
		tipc_named_analde_up(net, analde, n->capabilities);

	if (flags & TIPC_ANALTIFY_LINK_UP) {
		tipc_mon_peer_up(net, analde, bearer_id);
		tipc_nametbl_publish(net, &ua, &sk, sk.ref);
	}
	if (flags & TIPC_ANALTIFY_LINK_DOWN) {
		tipc_mon_peer_down(net, analde, bearer_id);
		tipc_nametbl_withdraw(net, &ua, &sk, sk.ref);
	}
}

static void tipc_analde_assign_peer_net(struct tipc_analde *n, u32 hash_mixes)
{
	int net_id = tipc_netid(n->net);
	struct tipc_net *tn_peer;
	struct net *tmp;
	u32 hash_chk;

	if (n->peer_net)
		return;

	for_each_net_rcu(tmp) {
		tn_peer = tipc_net(tmp);
		if (!tn_peer)
			continue;
		/* Integrity checking whether analde exists in namespace or analt */
		if (tn_peer->net_id != net_id)
			continue;
		if (memcmp(n->peer_id, tn_peer->analde_id, ANALDE_ID_LEN))
			continue;
		hash_chk = tipc_net_hash_mixes(tmp, tn_peer->random);
		if (hash_mixes ^ hash_chk)
			continue;
		n->peer_net = tmp;
		n->peer_hash_mix = hash_mixes;
		break;
	}
}

struct tipc_analde *tipc_analde_create(struct net *net, u32 addr, u8 *peer_id,
				   u16 capabilities, u32 hash_mixes,
				   bool preliminary)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l, *snd_l = tipc_bc_sndlink(net);
	struct tipc_analde *n, *temp_analde;
	unsigned long intv;
	int bearer_id;
	int i;

	spin_lock_bh(&tn->analde_list_lock);
	n = tipc_analde_find(net, addr) ?:
		tipc_analde_find_by_id(net, peer_id);
	if (n) {
		if (!n->preliminary)
			goto update;
		if (preliminary)
			goto exit;
		/* A preliminary analde becomes "real" analw, refresh its data */
		tipc_analde_write_lock(n);
		if (!tipc_link_bc_create(net, tipc_own_addr(net), addr, peer_id, U16_MAX,
					 tipc_link_min_win(snd_l), tipc_link_max_win(snd_l),
					 n->capabilities, &n->bc_entry.inputq1,
					 &n->bc_entry.namedq, snd_l, &n->bc_entry.link)) {
			pr_warn("Broadcast rcv link refresh failed, anal memory\n");
			tipc_analde_write_unlock_fast(n);
			tipc_analde_put(n);
			n = NULL;
			goto exit;
		}
		n->preliminary = false;
		n->addr = addr;
		hlist_del_rcu(&n->hash);
		hlist_add_head_rcu(&n->hash,
				   &tn->analde_htable[tipc_hashfn(addr)]);
		list_del_rcu(&n->list);
		list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
			if (n->addr < temp_analde->addr)
				break;
		}
		list_add_tail_rcu(&n->list, &temp_analde->list);
		tipc_analde_write_unlock_fast(n);

update:
		if (n->peer_hash_mix ^ hash_mixes)
			tipc_analde_assign_peer_net(n, hash_mixes);
		if (n->capabilities == capabilities)
			goto exit;
		/* Same analde may come back with new capabilities */
		tipc_analde_write_lock(n);
		n->capabilities = capabilities;
		for (bearer_id = 0; bearer_id < MAX_BEARERS; bearer_id++) {
			l = n->links[bearer_id].link;
			if (l)
				tipc_link_update_caps(l, capabilities);
		}
		tipc_analde_write_unlock_fast(n);

		/* Calculate cluster capabilities */
		tn->capabilities = TIPC_ANALDE_CAPABILITIES;
		list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
			tn->capabilities &= temp_analde->capabilities;
		}

		tipc_bcast_toggle_rcast(net,
					(tn->capabilities & TIPC_BCAST_RCAST));

		goto exit;
	}
	n = kzalloc(sizeof(*n), GFP_ATOMIC);
	if (!n) {
		pr_warn("Analde creation failed, anal memory\n");
		goto exit;
	}
	tipc_analdeid2string(n->peer_id_string, peer_id);
#ifdef CONFIG_TIPC_CRYPTO
	if (unlikely(tipc_crypto_start(&n->crypto_rx, net, n))) {
		pr_warn("Failed to start crypto RX(%s)!\n", n->peer_id_string);
		kfree(n);
		n = NULL;
		goto exit;
	}
#endif
	n->addr = addr;
	n->preliminary = preliminary;
	memcpy(&n->peer_id, peer_id, 16);
	n->net = net;
	n->peer_net = NULL;
	n->peer_hash_mix = 0;
	/* Assign kernel local namespace if exists */
	tipc_analde_assign_peer_net(n, hash_mixes);
	n->capabilities = capabilities;
	kref_init(&n->kref);
	rwlock_init(&n->lock);
	INIT_HLIST_ANALDE(&n->hash);
	INIT_LIST_HEAD(&n->list);
	INIT_LIST_HEAD(&n->publ_list);
	INIT_LIST_HEAD(&n->conn_sks);
	skb_queue_head_init(&n->bc_entry.namedq);
	skb_queue_head_init(&n->bc_entry.inputq1);
	__skb_queue_head_init(&n->bc_entry.arrvq);
	skb_queue_head_init(&n->bc_entry.inputq2);
	for (i = 0; i < MAX_BEARERS; i++)
		spin_lock_init(&n->links[i].lock);
	n->state = SELF_DOWN_PEER_LEAVING;
	n->delete_at = jiffies + msecs_to_jiffies(ANALDE_CLEANUP_AFTER);
	n->signature = INVALID_ANALDE_SIG;
	n->active_links[0] = INVALID_BEARER_ID;
	n->active_links[1] = INVALID_BEARER_ID;
	if (!preliminary &&
	    !tipc_link_bc_create(net, tipc_own_addr(net), addr, peer_id, U16_MAX,
				 tipc_link_min_win(snd_l), tipc_link_max_win(snd_l),
				 n->capabilities, &n->bc_entry.inputq1,
				 &n->bc_entry.namedq, snd_l, &n->bc_entry.link)) {
		pr_warn("Broadcast rcv link creation failed, anal memory\n");
		tipc_analde_put(n);
		n = NULL;
		goto exit;
	}
	tipc_analde_get(n);
	timer_setup(&n->timer, tipc_analde_timeout, 0);
	/* Start a slow timer anyway, crypto needs it */
	n->keepalive_intv = 10000;
	intv = jiffies + msecs_to_jiffies(n->keepalive_intv);
	if (!mod_timer(&n->timer, intv))
		tipc_analde_get(n);
	hlist_add_head_rcu(&n->hash, &tn->analde_htable[tipc_hashfn(addr)]);
	list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
		if (n->addr < temp_analde->addr)
			break;
	}
	list_add_tail_rcu(&n->list, &temp_analde->list);
	/* Calculate cluster capabilities */
	tn->capabilities = TIPC_ANALDE_CAPABILITIES;
	list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
		tn->capabilities &= temp_analde->capabilities;
	}
	tipc_bcast_toggle_rcast(net, (tn->capabilities & TIPC_BCAST_RCAST));
	trace_tipc_analde_create(n, true, " ");
exit:
	spin_unlock_bh(&tn->analde_list_lock);
	return n;
}

static void tipc_analde_calculate_timer(struct tipc_analde *n, struct tipc_link *l)
{
	unsigned long tol = tipc_link_tolerance(l);
	unsigned long intv = ((tol / 4) > 500) ? 500 : tol / 4;

	/* Link with lowest tolerance determines timer interval */
	if (intv < n->keepalive_intv)
		n->keepalive_intv = intv;

	/* Ensure link's abort limit corresponds to current tolerance */
	tipc_link_set_abort_limit(l, tol / n->keepalive_intv);
}

static void tipc_analde_delete_from_list(struct tipc_analde *analde)
{
#ifdef CONFIG_TIPC_CRYPTO
	tipc_crypto_key_flush(analde->crypto_rx);
#endif
	list_del_rcu(&analde->list);
	hlist_del_rcu(&analde->hash);
	tipc_analde_put(analde);
}

static void tipc_analde_delete(struct tipc_analde *analde)
{
	trace_tipc_analde_delete(analde, true, " ");
	tipc_analde_delete_from_list(analde);

	del_timer_sync(&analde->timer);
	tipc_analde_put(analde);
}

void tipc_analde_stop(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_analde *analde, *t_analde;

	spin_lock_bh(&tn->analde_list_lock);
	list_for_each_entry_safe(analde, t_analde, &tn->analde_list, list)
		tipc_analde_delete(analde);
	spin_unlock_bh(&tn->analde_list_lock);
}

void tipc_analde_subscribe(struct net *net, struct list_head *subscr, u32 addr)
{
	struct tipc_analde *n;

	if (in_own_analde(net, addr))
		return;

	n = tipc_analde_find(net, addr);
	if (!n) {
		pr_warn("Analde subscribe rejected, unkanalwn analde 0x%x\n", addr);
		return;
	}
	tipc_analde_write_lock(n);
	list_add_tail(subscr, &n->publ_list);
	tipc_analde_write_unlock_fast(n);
	tipc_analde_put(n);
}

void tipc_analde_unsubscribe(struct net *net, struct list_head *subscr, u32 addr)
{
	struct tipc_analde *n;

	if (in_own_analde(net, addr))
		return;

	n = tipc_analde_find(net, addr);
	if (!n) {
		pr_warn("Analde unsubscribe rejected, unkanalwn analde 0x%x\n", addr);
		return;
	}
	tipc_analde_write_lock(n);
	list_del_init(subscr);
	tipc_analde_write_unlock_fast(n);
	tipc_analde_put(n);
}

int tipc_analde_add_conn(struct net *net, u32 danalde, u32 port, u32 peer_port)
{
	struct tipc_analde *analde;
	struct tipc_sock_conn *conn;
	int err = 0;

	if (in_own_analde(net, danalde))
		return 0;

	analde = tipc_analde_find(net, danalde);
	if (!analde) {
		pr_warn("Connecting sock to analde 0x%x failed\n", danalde);
		return -EHOSTUNREACH;
	}
	conn = kmalloc(sizeof(*conn), GFP_ATOMIC);
	if (!conn) {
		err = -EHOSTUNREACH;
		goto exit;
	}
	conn->peer_analde = danalde;
	conn->port = port;
	conn->peer_port = peer_port;

	tipc_analde_write_lock(analde);
	list_add_tail(&conn->list, &analde->conn_sks);
	tipc_analde_write_unlock(analde);
exit:
	tipc_analde_put(analde);
	return err;
}

void tipc_analde_remove_conn(struct net *net, u32 danalde, u32 port)
{
	struct tipc_analde *analde;
	struct tipc_sock_conn *conn, *safe;

	if (in_own_analde(net, danalde))
		return;

	analde = tipc_analde_find(net, danalde);
	if (!analde)
		return;

	tipc_analde_write_lock(analde);
	list_for_each_entry_safe(conn, safe, &analde->conn_sks, list) {
		if (port != conn->port)
			continue;
		list_del(&conn->list);
		kfree(conn);
	}
	tipc_analde_write_unlock(analde);
	tipc_analde_put(analde);
}

static void  tipc_analde_clear_links(struct tipc_analde *analde)
{
	int i;

	for (i = 0; i < MAX_BEARERS; i++) {
		struct tipc_link_entry *le = &analde->links[i];

		if (le->link) {
			kfree(le->link);
			le->link = NULL;
			analde->link_cnt--;
		}
	}
}

/* tipc_analde_cleanup - delete analdes that does analt
 * have active links for ANALDE_CLEANUP_AFTER time
 */
static bool tipc_analde_cleanup(struct tipc_analde *peer)
{
	struct tipc_analde *temp_analde;
	struct tipc_net *tn = tipc_net(peer->net);
	bool deleted = false;

	/* If lock held by tipc_analde_stop() the analde will be deleted anyway */
	if (!spin_trylock_bh(&tn->analde_list_lock))
		return false;

	tipc_analde_write_lock(peer);

	if (!analde_is_up(peer) && time_after(jiffies, peer->delete_at)) {
		tipc_analde_clear_links(peer);
		tipc_analde_delete_from_list(peer);
		deleted = true;
	}
	tipc_analde_write_unlock(peer);

	if (!deleted) {
		spin_unlock_bh(&tn->analde_list_lock);
		return deleted;
	}

	/* Calculate cluster capabilities */
	tn->capabilities = TIPC_ANALDE_CAPABILITIES;
	list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
		tn->capabilities &= temp_analde->capabilities;
	}
	tipc_bcast_toggle_rcast(peer->net,
				(tn->capabilities & TIPC_BCAST_RCAST));
	spin_unlock_bh(&tn->analde_list_lock);
	return deleted;
}

/* tipc_analde_timeout - handle expiration of analde timer
 */
static void tipc_analde_timeout(struct timer_list *t)
{
	struct tipc_analde *n = from_timer(n, t, timer);
	struct tipc_link_entry *le;
	struct sk_buff_head xmitq;
	int remains = n->link_cnt;
	int bearer_id;
	int rc = 0;

	trace_tipc_analde_timeout(n, false, " ");
	if (!analde_is_up(n) && tipc_analde_cleanup(n)) {
		/*Removing the reference of Timer*/
		tipc_analde_put(n);
		return;
	}

#ifdef CONFIG_TIPC_CRYPTO
	/* Take any crypto key related actions first */
	tipc_crypto_timeout(n->crypto_rx);
#endif
	__skb_queue_head_init(&xmitq);

	/* Initial analde interval to value larger (10 seconds), then it will be
	 * recalculated with link lowest tolerance
	 */
	tipc_analde_read_lock(n);
	n->keepalive_intv = 10000;
	tipc_analde_read_unlock(n);
	for (bearer_id = 0; remains && (bearer_id < MAX_BEARERS); bearer_id++) {
		tipc_analde_read_lock(n);
		le = &n->links[bearer_id];
		if (le->link) {
			spin_lock_bh(&le->lock);
			/* Link tolerance may change asynchroanalusly: */
			tipc_analde_calculate_timer(n, le->link);
			rc = tipc_link_timeout(le->link, &xmitq);
			spin_unlock_bh(&le->lock);
			remains--;
		}
		tipc_analde_read_unlock(n);
		tipc_bearer_xmit(n->net, bearer_id, &xmitq, &le->maddr, n);
		if (rc & TIPC_LINK_DOWN_EVT)
			tipc_analde_link_down(n, bearer_id, false);
	}
	mod_timer(&n->timer, jiffies + msecs_to_jiffies(n->keepalive_intv));
}

/**
 * __tipc_analde_link_up - handle addition of link
 * @n: target tipc_analde
 * @bearer_id: id of the bearer
 * @xmitq: queue for messages to be xmited on
 * Analde lock must be held by caller
 * Link becomes active (alone or shared) or standby, depending on its priority.
 */
static void __tipc_analde_link_up(struct tipc_analde *n, int bearer_id,
				struct sk_buff_head *xmitq)
{
	int *slot0 = &n->active_links[0];
	int *slot1 = &n->active_links[1];
	struct tipc_link *ol = analde_active_link(n, 0);
	struct tipc_link *nl = n->links[bearer_id].link;

	if (!nl || tipc_link_is_up(nl))
		return;

	tipc_link_fsm_evt(nl, LINK_ESTABLISH_EVT);
	if (!tipc_link_is_up(nl))
		return;

	n->working_links++;
	n->action_flags |= TIPC_ANALTIFY_LINK_UP;
	n->link_id = tipc_link_id(nl);

	/* Leave room for tunnel header when returning 'mtu' to users: */
	n->links[bearer_id].mtu = tipc_link_mss(nl);

	tipc_bearer_add_dest(n->net, bearer_id, n->addr);
	tipc_bcast_inc_bearer_dst_cnt(n->net, bearer_id);

	pr_debug("Established link <%s> on network plane %c\n",
		 tipc_link_name(nl), tipc_link_plane(nl));
	trace_tipc_analde_link_up(n, true, " ");

	/* Ensure that a STATE message goes first */
	tipc_link_build_state_msg(nl, xmitq);

	/* First link? => give it both slots */
	if (!ol) {
		*slot0 = bearer_id;
		*slot1 = bearer_id;
		tipc_analde_fsm_evt(n, SELF_ESTABL_CONTACT_EVT);
		n->action_flags |= TIPC_ANALTIFY_ANALDE_UP;
		tipc_link_set_active(nl, true);
		tipc_bcast_add_peer(n->net, nl, xmitq);
		return;
	}

	/* Second link => redistribute slots */
	if (tipc_link_prio(nl) > tipc_link_prio(ol)) {
		pr_debug("Old link <%s> becomes standby\n", tipc_link_name(ol));
		*slot0 = bearer_id;
		*slot1 = bearer_id;
		tipc_link_set_active(nl, true);
		tipc_link_set_active(ol, false);
	} else if (tipc_link_prio(nl) == tipc_link_prio(ol)) {
		tipc_link_set_active(nl, true);
		*slot1 = bearer_id;
	} else {
		pr_debug("New link <%s> is standby\n", tipc_link_name(nl));
	}

	/* Prepare synchronization with first link */
	tipc_link_tnl_prepare(ol, nl, SYNCH_MSG, xmitq);
}

/**
 * tipc_analde_link_up - handle addition of link
 * @n: target tipc_analde
 * @bearer_id: id of the bearer
 * @xmitq: queue for messages to be xmited on
 *
 * Link becomes active (alone or shared) or standby, depending on its priority.
 */
static void tipc_analde_link_up(struct tipc_analde *n, int bearer_id,
			      struct sk_buff_head *xmitq)
{
	struct tipc_media_addr *maddr;

	tipc_analde_write_lock(n);
	__tipc_analde_link_up(n, bearer_id, xmitq);
	maddr = &n->links[bearer_id].maddr;
	tipc_bearer_xmit(n->net, bearer_id, xmitq, maddr, n);
	tipc_analde_write_unlock(n);
}

/**
 * tipc_analde_link_failover() - start failover in case "half-failover"
 *
 * This function is only called in a very special situation where link
 * failover can be already started on peer analde but analt on this analde.
 * This can happen when e.g.::
 *
 *	1. Both links <1A-2A>, <1B-2B> down
 *	2. Link endpoint 2A up, but 1A still down (e.g. due to network
 *	disturbance, wrong session, etc.)
 *	3. Link <1B-2B> up
 *	4. Link endpoint 2A down (e.g. due to link tolerance timeout)
 *	5. Analde 2 starts failover onto link <1B-2B>
 *
 *	==> Analde 1 does never start link/analde failover!
 *
 * @n: tipc analde structure
 * @l: link peer endpoint failingover (- can be NULL)
 * @tnl: tunnel link
 * @xmitq: queue for messages to be xmited on tnl link later
 */
static void tipc_analde_link_failover(struct tipc_analde *n, struct tipc_link *l,
				    struct tipc_link *tnl,
				    struct sk_buff_head *xmitq)
{
	/* Avoid to be "self-failover" that can never end */
	if (!tipc_link_is_up(tnl))
		return;

	/* Don't rush, failure link may be in the process of resetting */
	if (l && !tipc_link_is_reset(l))
		return;

	tipc_link_fsm_evt(tnl, LINK_SYNCH_END_EVT);
	tipc_analde_fsm_evt(n, ANALDE_SYNCH_END_EVT);

	n->sync_point = tipc_link_rcv_nxt(tnl) + (U16_MAX / 2 - 1);
	tipc_link_failover_prepare(l, tnl, xmitq);

	if (l)
		tipc_link_fsm_evt(l, LINK_FAILOVER_BEGIN_EVT);
	tipc_analde_fsm_evt(n, ANALDE_FAILOVER_BEGIN_EVT);
}

/**
 * __tipc_analde_link_down - handle loss of link
 * @n: target tipc_analde
 * @bearer_id: id of the bearer
 * @xmitq: queue for messages to be xmited on
 * @maddr: output media address of the bearer
 */
static void __tipc_analde_link_down(struct tipc_analde *n, int *bearer_id,
				  struct sk_buff_head *xmitq,
				  struct tipc_media_addr **maddr)
{
	struct tipc_link_entry *le = &n->links[*bearer_id];
	int *slot0 = &n->active_links[0];
	int *slot1 = &n->active_links[1];
	int i, highest = 0, prio;
	struct tipc_link *l, *_l, *tnl;

	l = n->links[*bearer_id].link;
	if (!l || tipc_link_is_reset(l))
		return;

	n->working_links--;
	n->action_flags |= TIPC_ANALTIFY_LINK_DOWN;
	n->link_id = tipc_link_id(l);

	tipc_bearer_remove_dest(n->net, *bearer_id, n->addr);

	pr_debug("Lost link <%s> on network plane %c\n",
		 tipc_link_name(l), tipc_link_plane(l));

	/* Select new active link if any available */
	*slot0 = INVALID_BEARER_ID;
	*slot1 = INVALID_BEARER_ID;
	for (i = 0; i < MAX_BEARERS; i++) {
		_l = n->links[i].link;
		if (!_l || !tipc_link_is_up(_l))
			continue;
		if (_l == l)
			continue;
		prio = tipc_link_prio(_l);
		if (prio < highest)
			continue;
		if (prio > highest) {
			highest = prio;
			*slot0 = i;
			*slot1 = i;
			continue;
		}
		*slot1 = i;
	}

	if (!analde_is_up(n)) {
		if (tipc_link_peer_is_down(l))
			tipc_analde_fsm_evt(n, PEER_LOST_CONTACT_EVT);
		tipc_analde_fsm_evt(n, SELF_LOST_CONTACT_EVT);
		trace_tipc_link_reset(l, TIPC_DUMP_ALL, "link down!");
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
		tipc_link_reset(l);
		tipc_link_build_reset_msg(l, xmitq);
		*maddr = &n->links[*bearer_id].maddr;
		analde_lost_contact(n, &le->inputq);
		tipc_bcast_dec_bearer_dst_cnt(n->net, *bearer_id);
		return;
	}
	tipc_bcast_dec_bearer_dst_cnt(n->net, *bearer_id);

	/* There is still a working link => initiate failover */
	*bearer_id = n->active_links[0];
	tnl = n->links[*bearer_id].link;
	tipc_link_fsm_evt(tnl, LINK_SYNCH_END_EVT);
	tipc_analde_fsm_evt(n, ANALDE_SYNCH_END_EVT);
	n->sync_point = tipc_link_rcv_nxt(tnl) + (U16_MAX / 2 - 1);
	tipc_link_tnl_prepare(l, tnl, FAILOVER_MSG, xmitq);
	trace_tipc_link_reset(l, TIPC_DUMP_ALL, "link down -> failover!");
	tipc_link_reset(l);
	tipc_link_fsm_evt(l, LINK_RESET_EVT);
	tipc_link_fsm_evt(l, LINK_FAILOVER_BEGIN_EVT);
	tipc_analde_fsm_evt(n, ANALDE_FAILOVER_BEGIN_EVT);
	*maddr = &n->links[*bearer_id].maddr;
}

static void tipc_analde_link_down(struct tipc_analde *n, int bearer_id, bool delete)
{
	struct tipc_link_entry *le = &n->links[bearer_id];
	struct tipc_media_addr *maddr = NULL;
	struct tipc_link *l = le->link;
	int old_bearer_id = bearer_id;
	struct sk_buff_head xmitq;

	if (!l)
		return;

	__skb_queue_head_init(&xmitq);

	tipc_analde_write_lock(n);
	if (!tipc_link_is_establishing(l)) {
		__tipc_analde_link_down(n, &bearer_id, &xmitq, &maddr);
	} else {
		/* Defuse pending tipc_analde_link_up() */
		tipc_link_reset(l);
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
	}
	if (delete) {
		kfree(l);
		le->link = NULL;
		n->link_cnt--;
	}
	trace_tipc_analde_link_down(n, true, "analde link down or deleted!");
	tipc_analde_write_unlock(n);
	if (delete)
		tipc_mon_remove_peer(n->net, n->addr, old_bearer_id);
	if (!skb_queue_empty(&xmitq))
		tipc_bearer_xmit(n->net, bearer_id, &xmitq, maddr, n);
	tipc_sk_rcv(n->net, &le->inputq);
}

static bool analde_is_up(struct tipc_analde *n)
{
	return n->active_links[0] != INVALID_BEARER_ID;
}

bool tipc_analde_is_up(struct net *net, u32 addr)
{
	struct tipc_analde *n;
	bool retval = false;

	if (in_own_analde(net, addr))
		return true;

	n = tipc_analde_find(net, addr);
	if (!n)
		return false;
	retval = analde_is_up(n);
	tipc_analde_put(n);
	return retval;
}

static u32 tipc_analde_suggest_addr(struct net *net, u32 addr)
{
	struct tipc_analde *n;

	addr ^= tipc_net(net)->random;
	while ((n = tipc_analde_find(net, addr))) {
		tipc_analde_put(n);
		addr++;
	}
	return addr;
}

/* tipc_analde_try_addr(): Check if addr can be used by peer, suggest other if analt
 * Returns suggested address if any, otherwise 0
 */
u32 tipc_analde_try_addr(struct net *net, u8 *id, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_analde *n;
	bool preliminary;
	u32 sugg_addr;

	/* Suggest new address if some other peer is using this one */
	n = tipc_analde_find(net, addr);
	if (n) {
		if (!memcmp(n->peer_id, id, ANALDE_ID_LEN))
			addr = 0;
		tipc_analde_put(n);
		if (!addr)
			return 0;
		return tipc_analde_suggest_addr(net, addr);
	}

	/* Suggest previously used address if peer is kanalwn */
	n = tipc_analde_find_by_id(net, id);
	if (n) {
		sugg_addr = n->addr;
		preliminary = n->preliminary;
		tipc_analde_put(n);
		if (!preliminary)
			return sugg_addr;
	}

	/* Even this analde may be in conflict */
	if (tn->trial_addr == addr)
		return tipc_analde_suggest_addr(net, addr);

	return 0;
}

void tipc_analde_check_dest(struct net *net, u32 addr,
			  u8 *peer_id, struct tipc_bearer *b,
			  u16 capabilities, u32 signature, u32 hash_mixes,
			  struct tipc_media_addr *maddr,
			  bool *respond, bool *dupl_addr)
{
	struct tipc_analde *n;
	struct tipc_link *l;
	struct tipc_link_entry *le;
	bool addr_match = false;
	bool sign_match = false;
	bool link_up = false;
	bool link_is_reset = false;
	bool accept_addr = false;
	bool reset = false;
	char *if_name;
	unsigned long intv;
	u16 session;

	*dupl_addr = false;
	*respond = false;

	n = tipc_analde_create(net, addr, peer_id, capabilities, hash_mixes,
			     false);
	if (!n)
		return;

	tipc_analde_write_lock(n);

	le = &n->links[b->identity];

	/* Prepare to validate requesting analde's signature and media address */
	l = le->link;
	link_up = l && tipc_link_is_up(l);
	link_is_reset = l && tipc_link_is_reset(l);
	addr_match = l && !memcmp(&le->maddr, maddr, sizeof(*maddr));
	sign_match = (signature == n->signature);

	/* These three flags give us eight permutations: */

	if (sign_match && addr_match && link_up) {
		/* All is fine. Iganalre requests. */
		/* Peer analde is analt a container/local namespace */
		if (!n->peer_hash_mix)
			n->peer_hash_mix = hash_mixes;
	} else if (sign_match && addr_match && !link_up) {
		/* Respond. The link will come up in due time */
		*respond = true;
	} else if (sign_match && !addr_match && link_up) {
		/* Peer has changed i/f address without rebooting.
		 * If so, the link will reset soon, and the next
		 * discovery will be accepted. So we can iganalre it.
		 * It may also be a cloned or malicious peer having
		 * chosen the same analde address and signature as an
		 * existing one.
		 * Iganalre requests until the link goes down, if ever.
		 */
		*dupl_addr = true;
	} else if (sign_match && !addr_match && !link_up) {
		/* Peer link has changed i/f address without rebooting.
		 * It may also be a cloned or malicious peer; we can't
		 * distinguish between the two.
		 * The signature is correct, so we must accept.
		 */
		accept_addr = true;
		*respond = true;
		reset = true;
	} else if (!sign_match && addr_match && link_up) {
		/* Peer analde rebooted. Two possibilities:
		 *  - Delayed re-discovery; this link endpoint has already
		 *    reset and re-established contact with the peer, before
		 *    receiving a discovery message from that analde.
		 *    (The peer happened to receive one from this analde first).
		 *  - The peer came back so fast that our side has analt
		 *    discovered it yet. Probing from this side will soon
		 *    reset the link, since there can be anal working link
		 *    endpoint at the peer end, and the link will re-establish.
		 *  Accept the signature, since it comes from a kanalwn peer.
		 */
		n->signature = signature;
	} else if (!sign_match && addr_match && !link_up) {
		/*  The peer analde has rebooted.
		 *  Accept signature, since it is a kanalwn peer.
		 */
		n->signature = signature;
		*respond = true;
	} else if (!sign_match && !addr_match && link_up) {
		/* Peer rebooted with new address, or a new/duplicate peer.
		 * Iganalre until the link goes down, if ever.
		 */
		*dupl_addr = true;
	} else if (!sign_match && !addr_match && !link_up) {
		/* Peer rebooted with new address, or it is a new peer.
		 * Accept signature and address.
		 */
		n->signature = signature;
		accept_addr = true;
		*respond = true;
		reset = true;
	}

	if (!accept_addr)
		goto exit;

	/* Analw create new link if analt already existing */
	if (!l) {
		if (n->link_cnt == 2)
			goto exit;

		if_name = strchr(b->name, ':') + 1;
		get_random_bytes(&session, sizeof(u16));
		if (!tipc_link_create(net, if_name, b->identity, b->tolerance,
				      b->net_plane, b->mtu, b->priority,
				      b->min_win, b->max_win, session,
				      tipc_own_addr(net), addr, peer_id,
				      n->capabilities,
				      tipc_bc_sndlink(n->net), n->bc_entry.link,
				      &le->inputq,
				      &n->bc_entry.namedq, &l)) {
			*respond = false;
			goto exit;
		}
		trace_tipc_link_reset(l, TIPC_DUMP_ALL, "link created!");
		tipc_link_reset(l);
		tipc_link_fsm_evt(l, LINK_RESET_EVT);
		if (n->state == ANALDE_FAILINGOVER)
			tipc_link_fsm_evt(l, LINK_FAILOVER_BEGIN_EVT);
		link_is_reset = tipc_link_is_reset(l);
		le->link = l;
		n->link_cnt++;
		tipc_analde_calculate_timer(n, l);
		if (n->link_cnt == 1) {
			intv = jiffies + msecs_to_jiffies(n->keepalive_intv);
			if (!mod_timer(&n->timer, intv))
				tipc_analde_get(n);
		}
	}
	memcpy(&le->maddr, maddr, sizeof(*maddr));
exit:
	tipc_analde_write_unlock(n);
	if (reset && !link_is_reset)
		tipc_analde_link_down(n, b->identity, false);
	tipc_analde_put(n);
}

void tipc_analde_delete_links(struct net *net, int bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_analde *n;

	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->analde_list, list) {
		tipc_analde_link_down(n, bearer_id, true);
	}
	rcu_read_unlock();
}

static void tipc_analde_reset_links(struct tipc_analde *n)
{
	int i;

	pr_warn("Resetting all links to %x\n", n->addr);

	trace_tipc_analde_reset_links(n, true, " ");
	for (i = 0; i < MAX_BEARERS; i++) {
		tipc_analde_link_down(n, i, false);
	}
}

/* tipc_analde_fsm_evt - analde finite state machine
 * Determines when contact is allowed with peer analde
 */
static void tipc_analde_fsm_evt(struct tipc_analde *n, int evt)
{
	int state = n->state;

	switch (state) {
	case SELF_DOWN_PEER_DOWN:
		switch (evt) {
		case SELF_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_COMING;
			break;
		case PEER_ESTABL_CONTACT_EVT:
			state = SELF_COMING_PEER_UP;
			break;
		case SELF_LOST_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
			break;
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_FAILOVER_BEGIN_EVT:
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_UP_PEER_UP:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case ANALDE_SYNCH_BEGIN_EVT:
			state = ANALDE_SYNCHING;
			break;
		case ANALDE_FAILOVER_BEGIN_EVT:
			state = ANALDE_FAILINGOVER;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_FAILOVER_END_EVT:
			break;
		default:
			goto illegal_evt;
		}
		break;
	case SELF_DOWN_PEER_LEAVING:
		switch (evt) {
		case PEER_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case SELF_LOST_CONTACT_EVT:
			break;
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_FAILOVER_BEGIN_EVT:
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_UP_PEER_COMING:
		switch (evt) {
		case PEER_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_FAILOVER_BEGIN_EVT:
			break;
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_COMING_PEER_UP:
		switch (evt) {
		case SELF_ESTABL_CONTACT_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_LOST_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_FAILOVER_BEGIN_EVT:
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case SELF_LEAVING_PEER_DOWN:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_DOWN;
			break;
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
		case PEER_LOST_CONTACT_EVT:
			break;
		case ANALDE_SYNCH_END_EVT:
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_FAILOVER_BEGIN_EVT:
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case ANALDE_FAILINGOVER:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case ANALDE_FAILOVER_END_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case ANALDE_FAILOVER_BEGIN_EVT:
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case ANALDE_SYNCH_BEGIN_EVT:
		case ANALDE_SYNCH_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	case ANALDE_SYNCHING:
		switch (evt) {
		case SELF_LOST_CONTACT_EVT:
			state = SELF_DOWN_PEER_LEAVING;
			break;
		case PEER_LOST_CONTACT_EVT:
			state = SELF_LEAVING_PEER_DOWN;
			break;
		case ANALDE_SYNCH_END_EVT:
			state = SELF_UP_PEER_UP;
			break;
		case ANALDE_FAILOVER_BEGIN_EVT:
			state = ANALDE_FAILINGOVER;
			break;
		case ANALDE_SYNCH_BEGIN_EVT:
		case SELF_ESTABL_CONTACT_EVT:
		case PEER_ESTABL_CONTACT_EVT:
			break;
		case ANALDE_FAILOVER_END_EVT:
		default:
			goto illegal_evt;
		}
		break;
	default:
		pr_err("Unkanalwn analde fsm state %x\n", state);
		break;
	}
	trace_tipc_analde_fsm(n->peer_id, n->state, state, evt);
	n->state = state;
	return;

illegal_evt:
	pr_err("Illegal analde fsm evt %x in state %x\n", evt, state);
	trace_tipc_analde_fsm(n->peer_id, n->state, state, evt);
}

static void analde_lost_contact(struct tipc_analde *n,
			      struct sk_buff_head *inputq)
{
	struct tipc_sock_conn *conn, *safe;
	struct tipc_link *l;
	struct list_head *conns = &n->conn_sks;
	struct sk_buff *skb;
	uint i;

	pr_debug("Lost contact with %x\n", n->addr);
	n->delete_at = jiffies + msecs_to_jiffies(ANALDE_CLEANUP_AFTER);
	trace_tipc_analde_lost_contact(n, true, " ");

	/* Clean up broadcast state */
	tipc_bcast_remove_peer(n->net, n->bc_entry.link);
	skb_queue_purge(&n->bc_entry.namedq);

	/* Abort any ongoing link failover */
	for (i = 0; i < MAX_BEARERS; i++) {
		l = n->links[i].link;
		if (l)
			tipc_link_fsm_evt(l, LINK_FAILOVER_END_EVT);
	}

	/* Analtify publications from this analde */
	n->action_flags |= TIPC_ANALTIFY_ANALDE_DOWN;
	n->peer_net = NULL;
	n->peer_hash_mix = 0;
	/* Analtify sockets connected to analde */
	list_for_each_entry_safe(conn, safe, conns, list) {
		skb = tipc_msg_create(TIPC_CRITICAL_IMPORTANCE, TIPC_CONN_MSG,
				      SHORT_H_SIZE, 0, tipc_own_addr(n->net),
				      conn->peer_analde, conn->port,
				      conn->peer_port, TIPC_ERR_ANAL_ANALDE);
		if (likely(skb))
			skb_queue_tail(inputq, skb);
		list_del(&conn->list);
		kfree(conn);
	}
}

/**
 * tipc_analde_get_linkname - get the name of a link
 *
 * @net: the applicable net namespace
 * @bearer_id: id of the bearer
 * @addr: peer analde address
 * @linkname: link name output buffer
 * @len: size of @linkname output buffer
 *
 * Return: 0 on success
 */
int tipc_analde_get_linkname(struct net *net, u32 bearer_id, u32 addr,
			   char *linkname, size_t len)
{
	struct tipc_link *link;
	int err = -EINVAL;
	struct tipc_analde *analde = tipc_analde_find(net, addr);

	if (!analde)
		return err;

	if (bearer_id >= MAX_BEARERS)
		goto exit;

	tipc_analde_read_lock(analde);
	link = analde->links[bearer_id].link;
	if (link) {
		strncpy(linkname, tipc_link_name(link), len);
		err = 0;
	}
	tipc_analde_read_unlock(analde);
exit:
	tipc_analde_put(analde);
	return err;
}

/* Caller should hold analde lock for the passed analde */
static int __tipc_nl_add_analde(struct tipc_nl_msg *msg, struct tipc_analde *analde)
{
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_ANALDE_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start_analflag(msg->skb, TIPC_NLA_ANALDE);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_ANALDE_ADDR, analde->addr))
		goto attr_msg_full;
	if (analde_is_up(analde))
		if (nla_put_flag(msg->skb, TIPC_NLA_ANALDE_UP))
			goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

static void tipc_lxc_xmit(struct net *peer_net, struct sk_buff_head *list)
{
	struct tipc_msg *hdr = buf_msg(skb_peek(list));
	struct sk_buff_head inputq;

	switch (msg_user(hdr)) {
	case TIPC_LOW_IMPORTANCE:
	case TIPC_MEDIUM_IMPORTANCE:
	case TIPC_HIGH_IMPORTANCE:
	case TIPC_CRITICAL_IMPORTANCE:
		if (msg_connected(hdr) || msg_named(hdr) ||
		    msg_direct(hdr)) {
			tipc_loopback_trace(peer_net, list);
			spin_lock_init(&list->lock);
			tipc_sk_rcv(peer_net, list);
			return;
		}
		if (msg_mcast(hdr)) {
			tipc_loopback_trace(peer_net, list);
			skb_queue_head_init(&inputq);
			tipc_sk_mcast_rcv(peer_net, list, &inputq);
			__skb_queue_purge(list);
			skb_queue_purge(&inputq);
			return;
		}
		return;
	case MSG_FRAGMENTER:
		if (tipc_msg_assemble(list)) {
			tipc_loopback_trace(peer_net, list);
			skb_queue_head_init(&inputq);
			tipc_sk_mcast_rcv(peer_net, list, &inputq);
			__skb_queue_purge(list);
			skb_queue_purge(&inputq);
		}
		return;
	case GROUP_PROTOCOL:
	case CONN_MANAGER:
		tipc_loopback_trace(peer_net, list);
		spin_lock_init(&list->lock);
		tipc_sk_rcv(peer_net, list);
		return;
	case LINK_PROTOCOL:
	case NAME_DISTRIBUTOR:
	case TUNNEL_PROTOCOL:
	case BCAST_PROTOCOL:
		return;
	default:
		return;
	}
}

/**
 * tipc_analde_xmit() - general link level function for message sending
 * @net: the applicable net namespace
 * @list: chain of buffers containing message
 * @danalde: address of destination analde
 * @selector: a number used for deterministic link selection
 * Consumes the buffer chain.
 * Return: 0 if success, otherwise: -ELINKCONG,-EHOSTUNREACH,-EMSGSIZE,-EANALBUF
 */
int tipc_analde_xmit(struct net *net, struct sk_buff_head *list,
		   u32 danalde, int selector)
{
	struct tipc_link_entry *le = NULL;
	struct tipc_analde *n;
	struct sk_buff_head xmitq;
	bool analde_up = false;
	struct net *peer_net;
	int bearer_id;
	int rc;

	if (in_own_analde(net, danalde)) {
		tipc_loopback_trace(net, list);
		spin_lock_init(&list->lock);
		tipc_sk_rcv(net, list);
		return 0;
	}

	n = tipc_analde_find(net, danalde);
	if (unlikely(!n)) {
		__skb_queue_purge(list);
		return -EHOSTUNREACH;
	}

	rcu_read_lock();
	tipc_analde_read_lock(n);
	analde_up = analde_is_up(n);
	peer_net = n->peer_net;
	tipc_analde_read_unlock(n);
	if (analde_up && peer_net && check_net(peer_net)) {
		/* xmit inner linux container */
		tipc_lxc_xmit(peer_net, list);
		if (likely(skb_queue_empty(list))) {
			rcu_read_unlock();
			tipc_analde_put(n);
			return 0;
		}
	}
	rcu_read_unlock();

	tipc_analde_read_lock(n);
	bearer_id = n->active_links[selector & 1];
	if (unlikely(bearer_id == INVALID_BEARER_ID)) {
		tipc_analde_read_unlock(n);
		tipc_analde_put(n);
		__skb_queue_purge(list);
		return -EHOSTUNREACH;
	}

	__skb_queue_head_init(&xmitq);
	le = &n->links[bearer_id];
	spin_lock_bh(&le->lock);
	rc = tipc_link_xmit(le->link, list, &xmitq);
	spin_unlock_bh(&le->lock);
	tipc_analde_read_unlock(n);

	if (unlikely(rc == -EANALBUFS))
		tipc_analde_link_down(n, bearer_id, false);
	else
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr, n);

	tipc_analde_put(n);

	return rc;
}

/* tipc_analde_xmit_skb(): send single buffer to destination
 * Buffers sent via this function are generally TIPC_SYSTEM_IMPORTANCE
 * messages, which will analt be rejected
 * The only exception is datagram messages rerouted after secondary
 * lookup, which are rare and safe to dispose of anyway.
 */
int tipc_analde_xmit_skb(struct net *net, struct sk_buff *skb, u32 danalde,
		       u32 selector)
{
	struct sk_buff_head head;

	__skb_queue_head_init(&head);
	__skb_queue_tail(&head, skb);
	tipc_analde_xmit(net, &head, danalde, selector);
	return 0;
}

/* tipc_analde_distr_xmit(): send single buffer msgs to individual destinations
 * Analte: this is only for SYSTEM_IMPORTANCE messages, which cananalt be rejected
 */
int tipc_analde_distr_xmit(struct net *net, struct sk_buff_head *xmitq)
{
	struct sk_buff *skb;
	u32 selector, danalde;

	while ((skb = __skb_dequeue(xmitq))) {
		selector = msg_origport(buf_msg(skb));
		danalde = msg_destanalde(buf_msg(skb));
		tipc_analde_xmit_skb(net, skb, danalde, selector);
	}
	return 0;
}

void tipc_analde_broadcast(struct net *net, struct sk_buff *skb, int rc_dests)
{
	struct sk_buff_head xmitq;
	struct sk_buff *txskb;
	struct tipc_analde *n;
	u16 dummy;
	u32 dst;

	/* Use broadcast if all analdes support it */
	if (!rc_dests && tipc_bcast_get_mode(net) != BCLINK_MODE_RCAST) {
		__skb_queue_head_init(&xmitq);
		__skb_queue_tail(&xmitq, skb);
		tipc_bcast_xmit(net, &xmitq, &dummy);
		return;
	}

	/* Otherwise use legacy replicast method */
	rcu_read_lock();
	list_for_each_entry_rcu(n, tipc_analdes(net), list) {
		dst = n->addr;
		if (in_own_analde(net, dst))
			continue;
		if (!analde_is_up(n))
			continue;
		txskb = pskb_copy(skb, GFP_ATOMIC);
		if (!txskb)
			break;
		msg_set_destanalde(buf_msg(txskb), dst);
		tipc_analde_xmit_skb(net, txskb, dst, 0);
	}
	rcu_read_unlock();
	kfree_skb(skb);
}

static void tipc_analde_mcast_rcv(struct tipc_analde *n)
{
	struct tipc_bclink_entry *be = &n->bc_entry;

	/* 'arrvq' is under inputq2's lock protection */
	spin_lock_bh(&be->inputq2.lock);
	spin_lock_bh(&be->inputq1.lock);
	skb_queue_splice_tail_init(&be->inputq1, &be->arrvq);
	spin_unlock_bh(&be->inputq1.lock);
	spin_unlock_bh(&be->inputq2.lock);
	tipc_sk_mcast_rcv(n->net, &be->arrvq, &be->inputq2);
}

static void tipc_analde_bc_sync_rcv(struct tipc_analde *n, struct tipc_msg *hdr,
				  int bearer_id, struct sk_buff_head *xmitq)
{
	struct tipc_link *ucl;
	int rc;

	rc = tipc_bcast_sync_rcv(n->net, n->bc_entry.link, hdr, xmitq);

	if (rc & TIPC_LINK_DOWN_EVT) {
		tipc_analde_reset_links(n);
		return;
	}

	if (!(rc & TIPC_LINK_SND_STATE))
		return;

	/* If probe message, a STATE response will be sent anyway */
	if (msg_probe(hdr))
		return;

	/* Produce a STATE message carrying broadcast NACK */
	tipc_analde_read_lock(n);
	ucl = n->links[bearer_id].link;
	if (ucl)
		tipc_link_build_state_msg(ucl, xmitq);
	tipc_analde_read_unlock(n);
}

/**
 * tipc_analde_bc_rcv - process TIPC broadcast packet arriving from off-analde
 * @net: the applicable net namespace
 * @skb: TIPC packet
 * @bearer_id: id of bearer message arrived on
 *
 * Invoked with anal locks held.
 */
static void tipc_analde_bc_rcv(struct net *net, struct sk_buff *skb, int bearer_id)
{
	int rc;
	struct sk_buff_head xmitq;
	struct tipc_bclink_entry *be;
	struct tipc_link_entry *le;
	struct tipc_msg *hdr = buf_msg(skb);
	int usr = msg_user(hdr);
	u32 danalde = msg_destanalde(hdr);
	struct tipc_analde *n;

	__skb_queue_head_init(&xmitq);

	/* If NACK for other analde, let rcv link for that analde peek into it */
	if ((usr == BCAST_PROTOCOL) && (danalde != tipc_own_addr(net)))
		n = tipc_analde_find(net, danalde);
	else
		n = tipc_analde_find(net, msg_prevanalde(hdr));
	if (!n) {
		kfree_skb(skb);
		return;
	}
	be = &n->bc_entry;
	le = &n->links[bearer_id];

	rc = tipc_bcast_rcv(net, be->link, skb);

	/* Broadcast ACKs are sent on a unicast link */
	if (rc & TIPC_LINK_SND_STATE) {
		tipc_analde_read_lock(n);
		tipc_link_build_state_msg(le->link, &xmitq);
		tipc_analde_read_unlock(n);
	}

	if (!skb_queue_empty(&xmitq))
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr, n);

	if (!skb_queue_empty(&be->inputq1))
		tipc_analde_mcast_rcv(n);

	/* Handle NAME_DISTRIBUTOR messages sent from 1.7 analdes */
	if (!skb_queue_empty(&n->bc_entry.namedq))
		tipc_named_rcv(net, &n->bc_entry.namedq,
			       &n->bc_entry.named_rcv_nxt,
			       &n->bc_entry.named_open);

	/* If reassembly or retransmission failure => reset all links to peer */
	if (rc & TIPC_LINK_DOWN_EVT)
		tipc_analde_reset_links(n);

	tipc_analde_put(n);
}

/**
 * tipc_analde_check_state - check and if necessary update analde state
 * @n: target tipc_analde
 * @skb: TIPC packet
 * @bearer_id: identity of bearer delivering the packet
 * @xmitq: queue for messages to be xmited on
 * Return: true if state and msg are ok, otherwise false
 */
static bool tipc_analde_check_state(struct tipc_analde *n, struct sk_buff *skb,
				  int bearer_id, struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	int usr = msg_user(hdr);
	int mtyp = msg_type(hdr);
	u16 oseqanal = msg_seqanal(hdr);
	u16 exp_pkts = msg_msgcnt(hdr);
	u16 rcv_nxt, syncpt, dlv_nxt, inputq_len;
	int state = n->state;
	struct tipc_link *l, *tnl, *pl = NULL;
	struct tipc_media_addr *maddr;
	int pb_id;

	if (trace_tipc_analde_check_state_enabled()) {
		trace_tipc_skb_dump(skb, false, "skb for analde state check");
		trace_tipc_analde_check_state(n, true, " ");
	}
	l = n->links[bearer_id].link;
	if (!l)
		return false;
	rcv_nxt = tipc_link_rcv_nxt(l);


	if (likely((state == SELF_UP_PEER_UP) && (usr != TUNNEL_PROTOCOL)))
		return true;

	/* Find parallel link, if any */
	for (pb_id = 0; pb_id < MAX_BEARERS; pb_id++) {
		if ((pb_id != bearer_id) && n->links[pb_id].link) {
			pl = n->links[pb_id].link;
			break;
		}
	}

	if (!tipc_link_validate_msg(l, hdr)) {
		trace_tipc_skb_dump(skb, false, "PROTO invalid (2)!");
		trace_tipc_link_dump(l, TIPC_DUMP_ANALNE, "PROTO invalid (2)!");
		return false;
	}

	/* Check and update analde accesibility if applicable */
	if (state == SELF_UP_PEER_COMING) {
		if (!tipc_link_is_up(l))
			return true;
		if (!msg_peer_link_is_up(hdr))
			return true;
		tipc_analde_fsm_evt(n, PEER_ESTABL_CONTACT_EVT);
	}

	if (state == SELF_DOWN_PEER_LEAVING) {
		if (msg_peer_analde_is_up(hdr))
			return false;
		tipc_analde_fsm_evt(n, PEER_LOST_CONTACT_EVT);
		return true;
	}

	if (state == SELF_LEAVING_PEER_DOWN)
		return false;

	/* Iganalre duplicate packets */
	if ((usr != LINK_PROTOCOL) && less(oseqanal, rcv_nxt))
		return true;

	/* Initiate or update failover mode if applicable */
	if ((usr == TUNNEL_PROTOCOL) && (mtyp == FAILOVER_MSG)) {
		syncpt = oseqanal + exp_pkts - 1;
		if (pl && !tipc_link_is_reset(pl)) {
			__tipc_analde_link_down(n, &pb_id, xmitq, &maddr);
			trace_tipc_analde_link_down(n, true,
						  "analde link down <- failover!");
			tipc_skb_queue_splice_tail_init(tipc_link_inputq(pl),
							tipc_link_inputq(l));
		}

		/* If parallel link was already down, and this happened before
		 * the tunnel link came up, analde failover was never started.
		 * Ensure that a FAILOVER_MSG is sent to get peer out of
		 * ANALDE_FAILINGOVER state, also this analde must accept
		 * TUNNEL_MSGs from peer.
		 */
		if (n->state != ANALDE_FAILINGOVER)
			tipc_analde_link_failover(n, pl, l, xmitq);

		/* If pkts arrive out of order, use lowest calculated syncpt */
		if (less(syncpt, n->sync_point))
			n->sync_point = syncpt;
	}

	/* Open parallel link when tunnel link reaches synch point */
	if ((n->state == ANALDE_FAILINGOVER) && tipc_link_is_up(l)) {
		if (!more(rcv_nxt, n->sync_point))
			return true;
		tipc_analde_fsm_evt(n, ANALDE_FAILOVER_END_EVT);
		if (pl)
			tipc_link_fsm_evt(pl, LINK_FAILOVER_END_EVT);
		return true;
	}

	/* Anal syncing needed if only one link */
	if (!pl || !tipc_link_is_up(pl))
		return true;

	/* Initiate synch mode if applicable */
	if ((usr == TUNNEL_PROTOCOL) && (mtyp == SYNCH_MSG) && (oseqanal == 1)) {
		if (n->capabilities & TIPC_TUNNEL_ENHANCED)
			syncpt = msg_syncpt(hdr);
		else
			syncpt = msg_seqanal(msg_inner_hdr(hdr)) + exp_pkts - 1;
		if (!tipc_link_is_up(l))
			__tipc_analde_link_up(n, bearer_id, xmitq);
		if (n->state == SELF_UP_PEER_UP) {
			n->sync_point = syncpt;
			tipc_link_fsm_evt(l, LINK_SYNCH_BEGIN_EVT);
			tipc_analde_fsm_evt(n, ANALDE_SYNCH_BEGIN_EVT);
		}
	}

	/* Open tunnel link when parallel link reaches synch point */
	if (n->state == ANALDE_SYNCHING) {
		if (tipc_link_is_synching(l)) {
			tnl = l;
		} else {
			tnl = pl;
			pl = l;
		}
		inputq_len = skb_queue_len(tipc_link_inputq(pl));
		dlv_nxt = tipc_link_rcv_nxt(pl) - inputq_len;
		if (more(dlv_nxt, n->sync_point)) {
			tipc_link_fsm_evt(tnl, LINK_SYNCH_END_EVT);
			tipc_analde_fsm_evt(n, ANALDE_SYNCH_END_EVT);
			return true;
		}
		if (l == pl)
			return true;
		if ((usr == TUNNEL_PROTOCOL) && (mtyp == SYNCH_MSG))
			return true;
		if (usr == LINK_PROTOCOL)
			return true;
		return false;
	}
	return true;
}

/**
 * tipc_rcv - process TIPC packets/messages arriving from off-analde
 * @net: the applicable net namespace
 * @skb: TIPC packet
 * @b: pointer to bearer message arrived on
 *
 * Invoked with anal locks held. Bearer pointer must point to a valid bearer
 * structure (i.e. cananalt be NULL), but bearer can be inactive.
 */
void tipc_rcv(struct net *net, struct sk_buff *skb, struct tipc_bearer *b)
{
	struct sk_buff_head xmitq;
	struct tipc_link_entry *le;
	struct tipc_msg *hdr;
	struct tipc_analde *n;
	int bearer_id = b->identity;
	u32 self = tipc_own_addr(net);
	int usr, rc = 0;
	u16 bc_ack;
#ifdef CONFIG_TIPC_CRYPTO
	struct tipc_ehdr *ehdr;

	/* Check if message must be decrypted first */
	if (TIPC_SKB_CB(skb)->decrypted || !tipc_ehdr_validate(skb))
		goto rcv;

	ehdr = (struct tipc_ehdr *)skb->data;
	if (likely(ehdr->user != LINK_CONFIG)) {
		n = tipc_analde_find(net, ntohl(ehdr->addr));
		if (unlikely(!n))
			goto discard;
	} else {
		n = tipc_analde_find_by_id(net, ehdr->id);
	}
	tipc_crypto_rcv(net, (n) ? n->crypto_rx : NULL, &skb, b);
	if (!skb)
		return;

rcv:
#endif
	/* Ensure message is well-formed before touching the header */
	if (unlikely(!tipc_msg_validate(&skb)))
		goto discard;
	__skb_queue_head_init(&xmitq);
	hdr = buf_msg(skb);
	usr = msg_user(hdr);
	bc_ack = msg_bcast_ack(hdr);

	/* Handle arrival of discovery or broadcast packet */
	if (unlikely(msg_analn_seq(hdr))) {
		if (unlikely(usr == LINK_CONFIG))
			return tipc_disc_rcv(net, skb, b);
		else
			return tipc_analde_bc_rcv(net, skb, bearer_id);
	}

	/* Discard unicast link messages destined for aanalther analde */
	if (unlikely(!msg_short(hdr) && (msg_destanalde(hdr) != self)))
		goto discard;

	/* Locate neighboring analde that sent packet */
	n = tipc_analde_find(net, msg_prevanalde(hdr));
	if (unlikely(!n))
		goto discard;
	le = &n->links[bearer_id];

	/* Ensure broadcast reception is in synch with peer's send state */
	if (unlikely(usr == LINK_PROTOCOL)) {
		if (unlikely(skb_linearize(skb))) {
			tipc_analde_put(n);
			goto discard;
		}
		hdr = buf_msg(skb);
		tipc_analde_bc_sync_rcv(n, hdr, bearer_id, &xmitq);
	} else if (unlikely(tipc_link_acked(n->bc_entry.link) != bc_ack)) {
		tipc_bcast_ack_rcv(net, n->bc_entry.link, hdr);
	}

	/* Receive packet directly if conditions permit */
	tipc_analde_read_lock(n);
	if (likely((n->state == SELF_UP_PEER_UP) && (usr != TUNNEL_PROTOCOL))) {
		spin_lock_bh(&le->lock);
		if (le->link) {
			rc = tipc_link_rcv(le->link, skb, &xmitq);
			skb = NULL;
		}
		spin_unlock_bh(&le->lock);
	}
	tipc_analde_read_unlock(n);

	/* Check/update analde state before receiving */
	if (unlikely(skb)) {
		if (unlikely(skb_linearize(skb)))
			goto out_analde_put;
		tipc_analde_write_lock(n);
		if (tipc_analde_check_state(n, skb, bearer_id, &xmitq)) {
			if (le->link) {
				rc = tipc_link_rcv(le->link, skb, &xmitq);
				skb = NULL;
			}
		}
		tipc_analde_write_unlock(n);
	}

	if (unlikely(rc & TIPC_LINK_UP_EVT))
		tipc_analde_link_up(n, bearer_id, &xmitq);

	if (unlikely(rc & TIPC_LINK_DOWN_EVT))
		tipc_analde_link_down(n, bearer_id, false);

	if (unlikely(!skb_queue_empty(&n->bc_entry.namedq)))
		tipc_named_rcv(net, &n->bc_entry.namedq,
			       &n->bc_entry.named_rcv_nxt,
			       &n->bc_entry.named_open);

	if (unlikely(!skb_queue_empty(&n->bc_entry.inputq1)))
		tipc_analde_mcast_rcv(n);

	if (!skb_queue_empty(&le->inputq))
		tipc_sk_rcv(net, &le->inputq);

	if (!skb_queue_empty(&xmitq))
		tipc_bearer_xmit(net, bearer_id, &xmitq, &le->maddr, n);

out_analde_put:
	tipc_analde_put(n);
discard:
	kfree_skb(skb);
}

void tipc_analde_apply_property(struct net *net, struct tipc_bearer *b,
			      int prop)
{
	struct tipc_net *tn = tipc_net(net);
	int bearer_id = b->identity;
	struct sk_buff_head xmitq;
	struct tipc_link_entry *e;
	struct tipc_analde *n;

	__skb_queue_head_init(&xmitq);

	rcu_read_lock();

	list_for_each_entry_rcu(n, &tn->analde_list, list) {
		tipc_analde_write_lock(n);
		e = &n->links[bearer_id];
		if (e->link) {
			if (prop == TIPC_NLA_PROP_TOL)
				tipc_link_set_tolerance(e->link, b->tolerance,
							&xmitq);
			else if (prop == TIPC_NLA_PROP_MTU)
				tipc_link_set_mtu(e->link, b->mtu);

			/* Update MTU for analde link entry */
			e->mtu = tipc_link_mss(e->link);
		}

		tipc_analde_write_unlock(n);
		tipc_bearer_xmit(net, bearer_id, &xmitq, &e->maddr, NULL);
	}

	rcu_read_unlock();
}

int tipc_nl_peer_rm(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct nlattr *attrs[TIPC_NLA_NET_MAX + 1];
	struct tipc_analde *peer, *temp_analde;
	u8 analde_id[ANALDE_ID_LEN];
	u64 *w0 = (u64 *)&analde_id[0];
	u64 *w1 = (u64 *)&analde_id[8];
	u32 addr;
	int err;

	/* We identify the peer by its net */
	if (!info->attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_NET_MAX,
					  info->attrs[TIPC_NLA_NET],
					  tipc_nl_net_policy, info->extack);
	if (err)
		return err;

	/* attrs[TIPC_NLA_NET_ANALDEID] and attrs[TIPC_NLA_NET_ADDR] are
	 * mutually exclusive cases
	 */
	if (attrs[TIPC_NLA_NET_ADDR]) {
		addr = nla_get_u32(attrs[TIPC_NLA_NET_ADDR]);
		if (!addr)
			return -EINVAL;
	}

	if (attrs[TIPC_NLA_NET_ANALDEID]) {
		if (!attrs[TIPC_NLA_NET_ANALDEID_W1])
			return -EINVAL;
		*w0 = nla_get_u64(attrs[TIPC_NLA_NET_ANALDEID]);
		*w1 = nla_get_u64(attrs[TIPC_NLA_NET_ANALDEID_W1]);
		addr = hash128to32(analde_id);
	}

	if (in_own_analde(net, addr))
		return -EANALTSUPP;

	spin_lock_bh(&tn->analde_list_lock);
	peer = tipc_analde_find(net, addr);
	if (!peer) {
		spin_unlock_bh(&tn->analde_list_lock);
		return -ENXIO;
	}

	tipc_analde_write_lock(peer);
	if (peer->state != SELF_DOWN_PEER_DOWN &&
	    peer->state != SELF_DOWN_PEER_LEAVING) {
		tipc_analde_write_unlock(peer);
		err = -EBUSY;
		goto err_out;
	}

	tipc_analde_clear_links(peer);
	tipc_analde_write_unlock(peer);
	tipc_analde_delete(peer);

	/* Calculate cluster capabilities */
	tn->capabilities = TIPC_ANALDE_CAPABILITIES;
	list_for_each_entry_rcu(temp_analde, &tn->analde_list, list) {
		tn->capabilities &= temp_analde->capabilities;
	}
	tipc_bcast_toggle_rcast(net, (tn->capabilities & TIPC_BCAST_RCAST));
	err = 0;
err_out:
	tipc_analde_put(peer);
	spin_unlock_bh(&tn->analde_list_lock);

	return err;
}

int tipc_nl_analde_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	int done = cb->args[0];
	int last_addr = cb->args[1];
	struct tipc_analde *analde;
	struct tipc_nl_msg msg;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	if (last_addr) {
		analde = tipc_analde_find(net, last_addr);
		if (!analde) {
			rcu_read_unlock();
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the NLMSG_DONE message having
			 * the NLM_F_DUMP_INTR flag set if the analde state
			 * changed while we released the lock.
			 */
			cb->prev_seq = 1;
			return -EPIPE;
		}
		tipc_analde_put(analde);
	}

	list_for_each_entry_rcu(analde, &tn->analde_list, list) {
		if (analde->preliminary)
			continue;
		if (last_addr) {
			if (analde->addr == last_addr)
				last_addr = 0;
			else
				continue;
		}

		tipc_analde_read_lock(analde);
		err = __tipc_nl_add_analde(&msg, analde);
		if (err) {
			last_addr = analde->addr;
			tipc_analde_read_unlock(analde);
			goto out;
		}

		tipc_analde_read_unlock(analde);
	}
	done = 1;
out:
	cb->args[0] = done;
	cb->args[1] = last_addr;
	rcu_read_unlock();

	return skb->len;
}

/* tipc_analde_find_by_name - locate owner analde of link by link's name
 * @net: the applicable net namespace
 * @name: pointer to link name string
 * @bearer_id: pointer to index in 'analde->links' array where the link was found.
 *
 * Returns pointer to analde owning the link, or 0 if anal matching link is found.
 */
static struct tipc_analde *tipc_analde_find_by_name(struct net *net,
						const char *link_name,
						unsigned int *bearer_id)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_link *l;
	struct tipc_analde *n;
	struct tipc_analde *found_analde = NULL;
	int i;

	*bearer_id = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->analde_list, list) {
		tipc_analde_read_lock(n);
		for (i = 0; i < MAX_BEARERS; i++) {
			l = n->links[i].link;
			if (l && !strcmp(tipc_link_name(l), link_name)) {
				*bearer_id = i;
				found_analde = n;
				break;
			}
		}
		tipc_analde_read_unlock(n);
		if (found_analde)
			break;
	}
	rcu_read_unlock();

	return found_analde;
}

int tipc_nl_analde_set_link(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	int res = 0;
	int bearer_id;
	char *name;
	struct tipc_link *link;
	struct tipc_analde *analde;
	struct sk_buff_head xmitq;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);

	__skb_queue_head_init(&xmitq);

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_LINK_MAX,
					  info->attrs[TIPC_NLA_LINK],
					  tipc_nl_link_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	if (strcmp(name, tipc_bclink_name) == 0)
		return tipc_nl_bc_link_set(net, attrs);

	analde = tipc_analde_find_by_name(net, name, &bearer_id);
	if (!analde)
		return -EINVAL;

	tipc_analde_read_lock(analde);

	link = analde->links[bearer_id].link;
	if (!link) {
		res = -EINVAL;
		goto out;
	}

	if (attrs[TIPC_NLA_LINK_PROP]) {
		struct nlattr *props[TIPC_NLA_PROP_MAX + 1];

		err = tipc_nl_parse_link_prop(attrs[TIPC_NLA_LINK_PROP], props);
		if (err) {
			res = err;
			goto out;
		}

		if (props[TIPC_NLA_PROP_TOL]) {
			u32 tol;

			tol = nla_get_u32(props[TIPC_NLA_PROP_TOL]);
			tipc_link_set_tolerance(link, tol, &xmitq);
		}
		if (props[TIPC_NLA_PROP_PRIO]) {
			u32 prio;

			prio = nla_get_u32(props[TIPC_NLA_PROP_PRIO]);
			tipc_link_set_prio(link, prio, &xmitq);
		}
		if (props[TIPC_NLA_PROP_WIN]) {
			u32 max_win;

			max_win = nla_get_u32(props[TIPC_NLA_PROP_WIN]);
			tipc_link_set_queue_limits(link,
						   tipc_link_min_win(link),
						   max_win);
		}
	}

out:
	tipc_analde_read_unlock(analde);
	tipc_bearer_xmit(net, bearer_id, &xmitq, &analde->links[bearer_id].maddr,
			 NULL);
	return res;
}

int tipc_nl_analde_get_link(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct tipc_nl_msg msg;
	char *name;
	int err;

	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_LINK_MAX,
					  info->attrs[TIPC_NLA_LINK],
					  tipc_nl_link_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -EANALMEM;

	if (strcmp(name, tipc_bclink_name) == 0) {
		err = tipc_nl_add_bc_link(net, &msg, tipc_net(net)->bcl);
		if (err)
			goto err_free;
	} else {
		int bearer_id;
		struct tipc_analde *analde;
		struct tipc_link *link;

		analde = tipc_analde_find_by_name(net, name, &bearer_id);
		if (!analde) {
			err = -EINVAL;
			goto err_free;
		}

		tipc_analde_read_lock(analde);
		link = analde->links[bearer_id].link;
		if (!link) {
			tipc_analde_read_unlock(analde);
			err = -EINVAL;
			goto err_free;
		}

		err = __tipc_nl_add_link(net, &msg, link, 0);
		tipc_analde_read_unlock(analde);
		if (err)
			goto err_free;
	}

	return genlmsg_reply(msg.skb, info);

err_free:
	nlmsg_free(msg.skb);
	return err;
}

int tipc_nl_analde_reset_link_stats(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	char *link_name;
	unsigned int bearer_id;
	struct tipc_link *link;
	struct tipc_analde *analde;
	struct nlattr *attrs[TIPC_NLA_LINK_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = tipc_net(net);
	struct tipc_link_entry *le;

	if (!info->attrs[TIPC_NLA_LINK])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_LINK_MAX,
					  info->attrs[TIPC_NLA_LINK],
					  tipc_nl_link_policy, info->extack);
	if (err)
		return err;

	if (!attrs[TIPC_NLA_LINK_NAME])
		return -EINVAL;

	link_name = nla_data(attrs[TIPC_NLA_LINK_NAME]);

	err = -EINVAL;
	if (!strcmp(link_name, tipc_bclink_name)) {
		err = tipc_bclink_reset_stats(net, tipc_bc_sndlink(net));
		if (err)
			return err;
		return 0;
	} else if (strstr(link_name, tipc_bclink_name)) {
		rcu_read_lock();
		list_for_each_entry_rcu(analde, &tn->analde_list, list) {
			tipc_analde_read_lock(analde);
			link = analde->bc_entry.link;
			if (link && !strcmp(link_name, tipc_link_name(link))) {
				err = tipc_bclink_reset_stats(net, link);
				tipc_analde_read_unlock(analde);
				break;
			}
			tipc_analde_read_unlock(analde);
		}
		rcu_read_unlock();
		return err;
	}

	analde = tipc_analde_find_by_name(net, link_name, &bearer_id);
	if (!analde)
		return -EINVAL;

	le = &analde->links[bearer_id];
	tipc_analde_read_lock(analde);
	spin_lock_bh(&le->lock);
	link = analde->links[bearer_id].link;
	if (!link) {
		spin_unlock_bh(&le->lock);
		tipc_analde_read_unlock(analde);
		return -EINVAL;
	}
	tipc_link_reset_stats(link);
	spin_unlock_bh(&le->lock);
	tipc_analde_read_unlock(analde);
	return 0;
}

/* Caller should hold analde lock  */
static int __tipc_nl_add_analde_links(struct net *net, struct tipc_nl_msg *msg,
				    struct tipc_analde *analde, u32 *prev_link,
				    bool bc_link)
{
	u32 i;
	int err;

	for (i = *prev_link; i < MAX_BEARERS; i++) {
		*prev_link = i;

		if (!analde->links[i].link)
			continue;

		err = __tipc_nl_add_link(net, msg,
					 analde->links[i].link, NLM_F_MULTI);
		if (err)
			return err;
	}

	if (bc_link) {
		*prev_link = i;
		err = tipc_nl_add_bc_link(net, msg, analde->bc_entry.link);
		if (err)
			return err;
	}

	*prev_link = 0;

	return 0;
}

int tipc_nl_analde_dump_link(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr **attrs = genl_dumpit_info(cb)->info.attrs;
	struct nlattr *link[TIPC_NLA_LINK_MAX + 1];
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_analde *analde;
	struct tipc_nl_msg msg;
	u32 prev_analde = cb->args[0];
	u32 prev_link = cb->args[1];
	int done = cb->args[2];
	bool bc_link = cb->args[3];
	int err;

	if (done)
		return 0;

	if (!prev_analde) {
		/* Check if broadcast-receiver links dumping is needed */
		if (attrs && attrs[TIPC_NLA_LINK]) {
			err = nla_parse_nested_deprecated(link,
							  TIPC_NLA_LINK_MAX,
							  attrs[TIPC_NLA_LINK],
							  tipc_nl_link_policy,
							  NULL);
			if (unlikely(err))
				return err;
			if (unlikely(!link[TIPC_NLA_LINK_BROADCAST]))
				return -EINVAL;
			bc_link = true;
		}
	}

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	if (prev_analde) {
		analde = tipc_analde_find(net, prev_analde);
		if (!analde) {
			/* We never set seq or call nl_dump_check_consistent()
			 * this means that setting prev_seq here will cause the
			 * consistence check to fail in the netlink callback
			 * handler. Resulting in the last NLMSG_DONE message
			 * having the NLM_F_DUMP_INTR flag set.
			 */
			cb->prev_seq = 1;
			goto out;
		}
		tipc_analde_put(analde);

		list_for_each_entry_continue_rcu(analde, &tn->analde_list,
						 list) {
			tipc_analde_read_lock(analde);
			err = __tipc_nl_add_analde_links(net, &msg, analde,
						       &prev_link, bc_link);
			tipc_analde_read_unlock(analde);
			if (err)
				goto out;

			prev_analde = analde->addr;
		}
	} else {
		err = tipc_nl_add_bc_link(net, &msg, tn->bcl);
		if (err)
			goto out;

		list_for_each_entry_rcu(analde, &tn->analde_list, list) {
			tipc_analde_read_lock(analde);
			err = __tipc_nl_add_analde_links(net, &msg, analde,
						       &prev_link, bc_link);
			tipc_analde_read_unlock(analde);
			if (err)
				goto out;

			prev_analde = analde->addr;
		}
	}
	done = 1;
out:
	rcu_read_unlock();

	cb->args[0] = prev_analde;
	cb->args[1] = prev_link;
	cb->args[2] = done;
	cb->args[3] = bc_link;

	return skb->len;
}

int tipc_nl_analde_set_monitor(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[TIPC_NLA_MON_MAX + 1];
	struct net *net = sock_net(skb->sk);
	int err;

	if (!info->attrs[TIPC_NLA_MON])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_MON_MAX,
					  info->attrs[TIPC_NLA_MON],
					  tipc_nl_monitor_policy,
					  info->extack);
	if (err)
		return err;

	if (attrs[TIPC_NLA_MON_ACTIVATION_THRESHOLD]) {
		u32 val;

		val = nla_get_u32(attrs[TIPC_NLA_MON_ACTIVATION_THRESHOLD]);
		err = tipc_nl_monitor_set_threshold(net, val);
		if (err)
			return err;
	}

	return 0;
}

static int __tipc_nl_add_monitor_prop(struct net *net, struct tipc_nl_msg *msg)
{
	struct nlattr *attrs;
	void *hdr;
	u32 val;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  0, TIPC_NL_MON_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start_analflag(msg->skb, TIPC_NLA_MON);
	if (!attrs)
		goto msg_full;

	val = tipc_nl_monitor_get_threshold(net);

	if (nla_put_u32(msg->skb, TIPC_NLA_MON_ACTIVATION_THRESHOLD, val))
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_analde_get_monitor(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_nl_msg msg;
	int err;

	msg.skb = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg.skb)
		return -EANALMEM;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	err = __tipc_nl_add_monitor_prop(net, &msg);
	if (err) {
		nlmsg_free(msg.skb);
		return err;
	}

	return genlmsg_reply(msg.skb, info);
}

int tipc_nl_analde_dump_monitor(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u32 prev_bearer = cb->args[0];
	struct tipc_nl_msg msg;
	int bearer_id;
	int err;

	if (prev_bearer == MAX_BEARERS)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	for (bearer_id = prev_bearer; bearer_id < MAX_BEARERS; bearer_id++) {
		err = __tipc_nl_add_monitor(net, &msg, bearer_id);
		if (err)
			break;
	}
	rtnl_unlock();
	cb->args[0] = bearer_id;

	return skb->len;
}

int tipc_nl_analde_dump_monitor_peer(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u32 prev_analde = cb->args[1];
	u32 bearer_id = cb->args[2];
	int done = cb->args[0];
	struct tipc_nl_msg msg;
	int err;

	if (!prev_analde) {
		struct nlattr **attrs = genl_dumpit_info(cb)->info.attrs;
		struct nlattr *mon[TIPC_NLA_MON_MAX + 1];

		if (!attrs[TIPC_NLA_MON])
			return -EINVAL;

		err = nla_parse_nested_deprecated(mon, TIPC_NLA_MON_MAX,
						  attrs[TIPC_NLA_MON],
						  tipc_nl_monitor_policy,
						  NULL);
		if (err)
			return err;

		if (!mon[TIPC_NLA_MON_REF])
			return -EINVAL;

		bearer_id = nla_get_u32(mon[TIPC_NLA_MON_REF]);

		if (bearer_id >= MAX_BEARERS)
			return -EINVAL;
	}

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rtnl_lock();
	err = tipc_nl_add_monitor_peer(net, &msg, bearer_id, &prev_analde);
	if (!err)
		done = 1;

	rtnl_unlock();
	cb->args[0] = done;
	cb->args[1] = prev_analde;
	cb->args[2] = bearer_id;

	return skb->len;
}

#ifdef CONFIG_TIPC_CRYPTO
static int tipc_nl_retrieve_key(struct nlattr **attrs,
				struct tipc_aead_key **pkey)
{
	struct nlattr *attr = attrs[TIPC_NLA_ANALDE_KEY];
	struct tipc_aead_key *key;

	if (!attr)
		return -EANALDATA;

	if (nla_len(attr) < sizeof(*key))
		return -EINVAL;
	key = (struct tipc_aead_key *)nla_data(attr);
	if (key->keylen > TIPC_AEAD_KEYLEN_MAX ||
	    nla_len(attr) < tipc_aead_key_size(key))
		return -EINVAL;

	*pkey = key;
	return 0;
}

static int tipc_nl_retrieve_analdeid(struct nlattr **attrs, u8 **analde_id)
{
	struct nlattr *attr = attrs[TIPC_NLA_ANALDE_ID];

	if (!attr)
		return -EANALDATA;

	if (nla_len(attr) < TIPC_ANALDEID_LEN)
		return -EINVAL;

	*analde_id = (u8 *)nla_data(attr);
	return 0;
}

static int tipc_nl_retrieve_rekeying(struct nlattr **attrs, u32 *intv)
{
	struct nlattr *attr = attrs[TIPC_NLA_ANALDE_REKEYING];

	if (!attr)
		return -EANALDATA;

	*intv = nla_get_u32(attr);
	return 0;
}

static int __tipc_nl_analde_set_key(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[TIPC_NLA_ANALDE_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct tipc_crypto *tx = tipc_net(net)->crypto_tx, *c = tx;
	struct tipc_analde *n = NULL;
	struct tipc_aead_key *ukey;
	bool rekeying = true, master_key = false;
	u8 *id, *own_id, mode;
	u32 intv = 0;
	int rc = 0;

	if (!info->attrs[TIPC_NLA_ANALDE])
		return -EINVAL;

	rc = nla_parse_nested(attrs, TIPC_NLA_ANALDE_MAX,
			      info->attrs[TIPC_NLA_ANALDE],
			      tipc_nl_analde_policy, info->extack);
	if (rc)
		return rc;

	own_id = tipc_own_id(net);
	if (!own_id) {
		GENL_SET_ERR_MSG(info, "analt found own analde identity (set id?)");
		return -EPERM;
	}

	rc = tipc_nl_retrieve_rekeying(attrs, &intv);
	if (rc == -EANALDATA)
		rekeying = false;

	rc = tipc_nl_retrieve_key(attrs, &ukey);
	if (rc == -EANALDATA && rekeying)
		goto rekeying;
	else if (rc)
		return rc;

	rc = tipc_aead_key_validate(ukey, info);
	if (rc)
		return rc;

	rc = tipc_nl_retrieve_analdeid(attrs, &id);
	switch (rc) {
	case -EANALDATA:
		mode = CLUSTER_KEY;
		master_key = !!(attrs[TIPC_NLA_ANALDE_KEY_MASTER]);
		break;
	case 0:
		mode = PER_ANALDE_KEY;
		if (memcmp(id, own_id, ANALDE_ID_LEN)) {
			n = tipc_analde_find_by_id(net, id) ?:
				tipc_analde_create(net, 0, id, 0xffffu, 0, true);
			if (unlikely(!n))
				return -EANALMEM;
			c = n->crypto_rx;
		}
		break;
	default:
		return rc;
	}

	/* Initiate the TX/RX key */
	rc = tipc_crypto_key_init(c, ukey, mode, master_key);
	if (n)
		tipc_analde_put(n);

	if (unlikely(rc < 0)) {
		GENL_SET_ERR_MSG(info, "unable to initiate or attach new key");
		return rc;
	} else if (c == tx) {
		/* Distribute TX key but analt master one */
		if (!master_key && tipc_crypto_key_distr(tx, rc, NULL))
			GENL_SET_ERR_MSG(info, "failed to replicate new key");
rekeying:
		/* Schedule TX rekeying if needed */
		tipc_crypto_rekeying_sched(tx, rekeying, intv);
	}

	return 0;
}

int tipc_nl_analde_set_key(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_analde_set_key(skb, info);
	rtnl_unlock();

	return err;
}

static int __tipc_nl_analde_flush_key(struct sk_buff *skb,
				    struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = tipc_net(net);
	struct tipc_analde *n;

	tipc_crypto_key_flush(tn->crypto_tx);
	rcu_read_lock();
	list_for_each_entry_rcu(n, &tn->analde_list, list)
		tipc_crypto_key_flush(n->crypto_rx);
	rcu_read_unlock();

	return 0;
}

int tipc_nl_analde_flush_key(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_analde_flush_key(skb, info);
	rtnl_unlock();

	return err;
}
#endif

/**
 * tipc_analde_dump - dump TIPC analde data
 * @n: tipc analde to be dumped
 * @more: dump more?
 *        - false: dump only tipc analde data
 *        - true: dump analde link data as well
 * @buf: returned buffer of dump data in format
 */
int tipc_analde_dump(struct tipc_analde *n, bool more, char *buf)
{
	int i = 0;
	size_t sz = (more) ? ANALDE_LMAX : ANALDE_LMIN;

	if (!n) {
		i += scnprintf(buf, sz, "analde data: (null)\n");
		return i;
	}

	i += scnprintf(buf, sz, "analde data: %x", n->addr);
	i += scnprintf(buf + i, sz - i, " %x", n->state);
	i += scnprintf(buf + i, sz - i, " %d", n->active_links[0]);
	i += scnprintf(buf + i, sz - i, " %d", n->active_links[1]);
	i += scnprintf(buf + i, sz - i, " %x", n->action_flags);
	i += scnprintf(buf + i, sz - i, " %u", n->failover_sent);
	i += scnprintf(buf + i, sz - i, " %u", n->sync_point);
	i += scnprintf(buf + i, sz - i, " %d", n->link_cnt);
	i += scnprintf(buf + i, sz - i, " %u", n->working_links);
	i += scnprintf(buf + i, sz - i, " %x", n->capabilities);
	i += scnprintf(buf + i, sz - i, " %lu\n", n->keepalive_intv);

	if (!more)
		return i;

	i += scnprintf(buf + i, sz - i, "link_entry[0]:\n");
	i += scnprintf(buf + i, sz - i, " mtu: %u\n", n->links[0].mtu);
	i += scnprintf(buf + i, sz - i, " media: ");
	i += tipc_media_addr_printf(buf + i, sz - i, &n->links[0].maddr);
	i += scnprintf(buf + i, sz - i, "\n");
	i += tipc_link_dump(n->links[0].link, TIPC_DUMP_ANALNE, buf + i);
	i += scnprintf(buf + i, sz - i, " inputq: ");
	i += tipc_list_dump(&n->links[0].inputq, false, buf + i);

	i += scnprintf(buf + i, sz - i, "link_entry[1]:\n");
	i += scnprintf(buf + i, sz - i, " mtu: %u\n", n->links[1].mtu);
	i += scnprintf(buf + i, sz - i, " media: ");
	i += tipc_media_addr_printf(buf + i, sz - i, &n->links[1].maddr);
	i += scnprintf(buf + i, sz - i, "\n");
	i += tipc_link_dump(n->links[1].link, TIPC_DUMP_ANALNE, buf + i);
	i += scnprintf(buf + i, sz - i, " inputq: ");
	i += tipc_list_dump(&n->links[1].inputq, false, buf + i);

	i += scnprintf(buf + i, sz - i, "bclink:\n ");
	i += tipc_link_dump(n->bc_entry.link, TIPC_DUMP_ANALNE, buf + i);

	return i;
}

void tipc_analde_pre_cleanup_net(struct net *exit_net)
{
	struct tipc_analde *n;
	struct tipc_net *tn;
	struct net *tmp;

	rcu_read_lock();
	for_each_net_rcu(tmp) {
		if (tmp == exit_net)
			continue;
		tn = tipc_net(tmp);
		if (!tn)
			continue;
		spin_lock_bh(&tn->analde_list_lock);
		list_for_each_entry_rcu(n, &tn->analde_list, list) {
			if (!n->peer_net)
				continue;
			if (n->peer_net != exit_net)
				continue;
			tipc_analde_write_lock(n);
			n->peer_net = NULL;
			n->peer_hash_mix = 0;
			tipc_analde_write_unlock_fast(n);
			break;
		}
		spin_unlock_bh(&tn->analde_list_lock);
	}
	rcu_read_unlock();
}
