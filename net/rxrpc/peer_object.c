// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC remote transport endpoint record management
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/ip6_route.h>
#include "ar-internal.h"

static const struct sockaddr_rxrpc rxrpc_null_addr;

/*
 * Hash a peer key.
 */
static unsigned long rxrpc_peer_hash_key(struct rxrpc_local *local,
					 const struct sockaddr_rxrpc *srx)
{
	const u16 *p;
	unsigned int i, size;
	unsigned long hash_key;

	_enter("");

	hash_key = (unsigned long)local / __alignof__(*local);
	hash_key += srx->transport_type;
	hash_key += srx->transport_len;
	hash_key += srx->transport.family;

	switch (srx->transport.family) {
	case AF_INET:
		hash_key += (u16 __force)srx->transport.sin.sin_port;
		size = sizeof(srx->transport.sin.sin_addr);
		p = (u16 *)&srx->transport.sin.sin_addr;
		break;
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		hash_key += (u16 __force)srx->transport.sin.sin_port;
		size = sizeof(srx->transport.sin6.sin6_addr);
		p = (u16 *)&srx->transport.sin6.sin6_addr;
		break;
#endif
	default:
		WARN(1, "AF_RXRPC: Unsupported transport address family\n");
		return 0;
	}

	/* Step through the peer address in 16-bit portions for speed */
	for (i = 0; i < size; i += sizeof(*p), p++)
		hash_key += *p;

	_leave(" 0x%lx", hash_key);
	return hash_key;
}

/*
 * Compare a peer to a key.  Return -ve, 0 or +ve to indicate less than, same
 * or greater than.
 *
 * Unfortunately, the primitives in linux/hashtable.h don't allow for sorted
 * buckets and mid-bucket insertion, so we don't make full use of this
 * information at this point.
 */
static long rxrpc_peer_cmp_key(const struct rxrpc_peer *peer,
			       struct rxrpc_local *local,
			       const struct sockaddr_rxrpc *srx,
			       unsigned long hash_key)
{
	long diff;

	diff = ((peer->hash_key - hash_key) ?:
		((unsigned long)peer->local - (unsigned long)local) ?:
		(peer->srx.transport_type - srx->transport_type) ?:
		(peer->srx.transport_len - srx->transport_len) ?:
		(peer->srx.transport.family - srx->transport.family));
	if (diff != 0)
		return diff;

	switch (srx->transport.family) {
	case AF_INET:
		return ((u16 __force)peer->srx.transport.sin.sin_port -
			(u16 __force)srx->transport.sin.sin_port) ?:
			memcmp(&peer->srx.transport.sin.sin_addr,
			       &srx->transport.sin.sin_addr,
			       sizeof(struct in_addr));
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		return ((u16 __force)peer->srx.transport.sin6.sin6_port -
			(u16 __force)srx->transport.sin6.sin6_port) ?:
			memcmp(&peer->srx.transport.sin6.sin6_addr,
			       &srx->transport.sin6.sin6_addr,
			       sizeof(struct in6_addr));
#endif
	default:
		BUG();
	}
}

/*
 * Look up a remote transport endpoint for the specified address using RCU.
 */
static struct rxrpc_peer *__rxrpc_lookup_peer_rcu(
	struct rxrpc_local *local,
	const struct sockaddr_rxrpc *srx,
	unsigned long hash_key)
{
	struct rxrpc_peer *peer;
	struct rxrpc_net *rxnet = local->rxnet;

	hash_for_each_possible_rcu(rxnet->peer_hash, peer, hash_link, hash_key) {
		if (rxrpc_peer_cmp_key(peer, local, srx, hash_key) == 0 &&
		    refcount_read(&peer->ref) > 0)
			return peer;
	}

	return NULL;
}

/*
 * Look up a remote transport endpoint for the specified address using RCU.
 */
struct rxrpc_peer *rxrpc_lookup_peer_rcu(struct rxrpc_local *local,
					 const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_peer *peer;
	unsigned long hash_key = rxrpc_peer_hash_key(local, srx);

	peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
	if (peer)
		_leave(" = %p {u=%d}", peer, refcount_read(&peer->ref));
	return peer;
}

/*
 * assess the MTU size for the network interface through which this peer is
 * reached
 */
static void rxrpc_assess_MTU_size(struct rxrpc_local *local,
				  struct rxrpc_peer *peer)
{
	struct net *net = local->net;
	struct dst_entry *dst;
	struct rtable *rt;
	struct flowi fl;
	struct flowi4 *fl4 = &fl.u.ip4;
#ifdef CONFIG_AF_RXRPC_IPV6
	struct flowi6 *fl6 = &fl.u.ip6;
#endif

	peer->if_mtu = 1500;
	if (peer->max_data < peer->if_mtu - peer->hdrsize) {
		trace_rxrpc_pmtud_reduce(peer, 0, peer->if_mtu - peer->hdrsize,
					 rxrpc_pmtud_reduce_route);
		peer->max_data = peer->if_mtu - peer->hdrsize;
	}

	memset(&fl, 0, sizeof(fl));
	switch (peer->srx.transport.family) {
	case AF_INET:
		rt = ip_route_output_ports(
			net, fl4, NULL,
			peer->srx.transport.sin.sin_addr.s_addr, 0,
			htons(7000), htons(7001), IPPROTO_UDP, 0, 0);
		if (IS_ERR(rt)) {
			_leave(" [route err %ld]", PTR_ERR(rt));
			return;
		}
		dst = &rt->dst;
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		fl6->flowi6_iif = LOOPBACK_IFINDEX;
		fl6->flowi6_scope = RT_SCOPE_UNIVERSE;
		fl6->flowi6_proto = IPPROTO_UDP;
		memcpy(&fl6->daddr, &peer->srx.transport.sin6.sin6_addr,
		       sizeof(struct in6_addr));
		fl6->fl6_dport = htons(7001);
		fl6->fl6_sport = htons(7000);
		dst = ip6_route_output(net, NULL, fl6);
		if (dst->error) {
			_leave(" [route err %d]", dst->error);
			return;
		}
		break;
#endif

	default:
		BUG();
	}

	peer->if_mtu = dst_mtu(dst);
	peer->hdrsize += dst->header_len + dst->trailer_len;
	peer->tx_seg_max = dst->dev->gso_max_segs;
	dst_release(dst);

	peer->max_data		= umin(RXRPC_JUMBO(1), peer->if_mtu - peer->hdrsize);
	peer->pmtud_good	= 500;
	peer->pmtud_bad		= peer->if_mtu - peer->hdrsize + 1;
	peer->pmtud_trial	= umin(peer->max_data, peer->pmtud_bad - 1);
	peer->pmtud_pending	= true;

	_leave(" [if_mtu %u]", peer->if_mtu);
}

/*
 * Allocate a peer.
 */
struct rxrpc_peer *rxrpc_alloc_peer(struct rxrpc_local *local, gfp_t gfp,
				    enum rxrpc_peer_trace why)
{
	struct rxrpc_peer *peer;

	_enter("");

	peer = kzalloc(sizeof(struct rxrpc_peer), gfp);
	if (peer) {
		refcount_set(&peer->ref, 1);
		peer->local = rxrpc_get_local(local, rxrpc_local_get_peer);
		INIT_HLIST_HEAD(&peer->error_targets);
		peer->service_conns = RB_ROOT;
		seqlock_init(&peer->service_conn_lock);
		spin_lock_init(&peer->lock);
		peer->debug_id = atomic_inc_return(&rxrpc_debug_id);
		peer->recent_srtt_us = UINT_MAX;
		peer->cong_ssthresh = RXRPC_TX_MAX_WINDOW;
		trace_rxrpc_peer(peer->debug_id, 1, why);
	}

	_leave(" = %p", peer);
	return peer;
}

/*
 * Initialise peer record.
 */
static void rxrpc_init_peer(struct rxrpc_local *local, struct rxrpc_peer *peer,
			    unsigned long hash_key)
{
	peer->hash_key = hash_key;


	switch (peer->srx.transport.family) {
	case AF_INET:
		peer->hdrsize = sizeof(struct iphdr);
		break;
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		peer->hdrsize = sizeof(struct ipv6hdr);
		break;
#endif
	default:
		BUG();
	}

	switch (peer->srx.transport_type) {
	case SOCK_DGRAM:
		peer->hdrsize += sizeof(struct udphdr);
		break;
	default:
		BUG();
	}

	peer->hdrsize += sizeof(struct rxrpc_wire_header);
	peer->max_data = peer->if_mtu - peer->hdrsize;

	rxrpc_assess_MTU_size(local, peer);
}

/*
 * Set up a new peer.
 */
static struct rxrpc_peer *rxrpc_create_peer(struct rxrpc_local *local,
					    struct sockaddr_rxrpc *srx,
					    unsigned long hash_key,
					    gfp_t gfp)
{
	struct rxrpc_peer *peer;

	_enter("");

	peer = rxrpc_alloc_peer(local, gfp, rxrpc_peer_new_client);
	if (peer) {
		memcpy(&peer->srx, srx, sizeof(*srx));
		rxrpc_init_peer(local, peer, hash_key);
	}

	_leave(" = %p", peer);
	return peer;
}

static void rxrpc_free_peer(struct rxrpc_peer *peer)
{
	trace_rxrpc_peer(peer->debug_id, 0, rxrpc_peer_free);
	rxrpc_put_local(peer->local, rxrpc_local_put_peer);
	kfree_rcu(peer, rcu);
}

/*
 * Set up a new incoming peer.  There shouldn't be any other matching peers
 * since we've already done a search in the list from the non-reentrant context
 * (the data_ready handler) that is the only place we can add new peers.
 * Called with interrupts disabled.
 */
void rxrpc_new_incoming_peer(struct rxrpc_local *local, struct rxrpc_peer *peer)
{
	struct rxrpc_net *rxnet = local->rxnet;
	unsigned long hash_key;

	hash_key = rxrpc_peer_hash_key(local, &peer->srx);
	rxrpc_init_peer(local, peer, hash_key);

	spin_lock(&rxnet->peer_hash_lock);
	hash_add_rcu(rxnet->peer_hash, &peer->hash_link, hash_key);
	list_add_tail(&peer->keepalive_link, &rxnet->peer_keepalive_new);
	spin_unlock(&rxnet->peer_hash_lock);
}

/*
 * obtain a remote transport endpoint for the specified address
 */
struct rxrpc_peer *rxrpc_lookup_peer(struct rxrpc_local *local,
				     struct sockaddr_rxrpc *srx, gfp_t gfp)
{
	struct rxrpc_peer *peer, *candidate;
	struct rxrpc_net *rxnet = local->rxnet;
	unsigned long hash_key = rxrpc_peer_hash_key(local, srx);

	_enter("{%pISp}", &srx->transport);

	/* search the peer list first */
	rcu_read_lock();
	peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
	if (peer && !rxrpc_get_peer_maybe(peer, rxrpc_peer_get_lookup_client))
		peer = NULL;
	rcu_read_unlock();

	if (!peer) {
		/* The peer is not yet present in hash - create a candidate
		 * for a new record and then redo the search.
		 */
		candidate = rxrpc_create_peer(local, srx, hash_key, gfp);
		if (!candidate) {
			_leave(" = NULL [nomem]");
			return NULL;
		}

		spin_lock_bh(&rxnet->peer_hash_lock);

		/* Need to check that we aren't racing with someone else */
		peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
		if (peer && !rxrpc_get_peer_maybe(peer, rxrpc_peer_get_lookup_client))
			peer = NULL;
		if (!peer) {
			hash_add_rcu(rxnet->peer_hash,
				     &candidate->hash_link, hash_key);
			list_add_tail(&candidate->keepalive_link,
				      &rxnet->peer_keepalive_new);
		}

		spin_unlock_bh(&rxnet->peer_hash_lock);

		if (peer)
			rxrpc_free_peer(candidate);
		else
			peer = candidate;
	}

	_leave(" = %p {u=%d}", peer, refcount_read(&peer->ref));
	return peer;
}

/*
 * Get a ref on a peer record.
 */
struct rxrpc_peer *rxrpc_get_peer(struct rxrpc_peer *peer, enum rxrpc_peer_trace why)
{
	int r;

	__refcount_inc(&peer->ref, &r);
	trace_rxrpc_peer(peer->debug_id, r + 1, why);
	return peer;
}

/*
 * Get a ref on a peer record unless its usage has already reached 0.
 */
struct rxrpc_peer *rxrpc_get_peer_maybe(struct rxrpc_peer *peer,
					enum rxrpc_peer_trace why)
{
	int r;

	if (peer) {
		if (__refcount_inc_not_zero(&peer->ref, &r))
			trace_rxrpc_peer(peer->debug_id, r + 1, why);
		else
			peer = NULL;
	}
	return peer;
}

/*
 * Discard a peer record.
 */
static void __rxrpc_put_peer(struct rxrpc_peer *peer)
{
	struct rxrpc_net *rxnet = peer->local->rxnet;

	ASSERT(hlist_empty(&peer->error_targets));

	spin_lock_bh(&rxnet->peer_hash_lock);
	hash_del_rcu(&peer->hash_link);
	list_del_init(&peer->keepalive_link);
	spin_unlock_bh(&rxnet->peer_hash_lock);

	rxrpc_free_peer(peer);
}

/*
 * Drop a ref on a peer record.
 */
void rxrpc_put_peer(struct rxrpc_peer *peer, enum rxrpc_peer_trace why)
{
	unsigned int debug_id;
	bool dead;
	int r;

	if (peer) {
		debug_id = peer->debug_id;
		dead = __refcount_dec_and_test(&peer->ref, &r);
		trace_rxrpc_peer(debug_id, r - 1, why);
		if (dead)
			__rxrpc_put_peer(peer);
	}
}

/*
 * Make sure all peer records have been discarded.
 */
void rxrpc_destroy_all_peers(struct rxrpc_net *rxnet)
{
	struct rxrpc_peer *peer;
	int i;

	for (i = 0; i < HASH_SIZE(rxnet->peer_hash); i++) {
		if (hlist_empty(&rxnet->peer_hash[i]))
			continue;

		hlist_for_each_entry(peer, &rxnet->peer_hash[i], hash_link) {
			pr_err("Leaked peer %u {%u} %pISp\n",
			       peer->debug_id,
			       refcount_read(&peer->ref),
			       &peer->srx.transport);
		}
	}
}

/**
 * rxrpc_kernel_get_call_peer - Get the peer address of a call
 * @sock: The socket on which the call is in progress.
 * @call: The call to query
 *
 * Get a record for the remote peer in a call.
 */
struct rxrpc_peer *rxrpc_kernel_get_call_peer(struct socket *sock, struct rxrpc_call *call)
{
	return call->peer;
}
EXPORT_SYMBOL(rxrpc_kernel_get_call_peer);

/**
 * rxrpc_kernel_get_srtt - Get a call's peer smoothed RTT
 * @peer: The peer to query
 *
 * Get the call's peer smoothed RTT in uS or UINT_MAX if we have no samples.
 */
unsigned int rxrpc_kernel_get_srtt(const struct rxrpc_peer *peer)
{
	return READ_ONCE(peer->recent_srtt_us);
}
EXPORT_SYMBOL(rxrpc_kernel_get_srtt);

/**
 * rxrpc_kernel_remote_srx - Get the address of a peer
 * @peer: The peer to query
 *
 * Get a pointer to the address from a peer record.  The caller is responsible
 * for making sure that the address is not deallocated.
 */
const struct sockaddr_rxrpc *rxrpc_kernel_remote_srx(const struct rxrpc_peer *peer)
{
	return peer ? &peer->srx : &rxrpc_null_addr;
}
EXPORT_SYMBOL(rxrpc_kernel_remote_srx);

/**
 * rxrpc_kernel_remote_addr - Get the peer transport address of a call
 * @peer: The peer to query
 *
 * Get a pointer to the transport address from a peer record.  The caller is
 * responsible for making sure that the address is not deallocated.
 */
const struct sockaddr *rxrpc_kernel_remote_addr(const struct rxrpc_peer *peer)
{
	return (const struct sockaddr *)
		(peer ? &peer->srx.transport : &rxrpc_null_addr.transport);
}
EXPORT_SYMBOL(rxrpc_kernel_remote_addr);
