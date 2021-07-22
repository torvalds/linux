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
		    atomic_read(&peer->usage) > 0)
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
	if (peer) {
		_net("PEER %d {%pISp}", peer->debug_id, &peer->srx.transport);
		_leave(" = %p {u=%d}", peer, atomic_read(&peer->usage));
	}
	return peer;
}

/*
 * assess the MTU size for the network interface through which this peer is
 * reached
 */
static void rxrpc_assess_MTU_size(struct rxrpc_sock *rx,
				  struct rxrpc_peer *peer)
{
	struct net *net = sock_net(&rx->sk);
	struct dst_entry *dst;
	struct rtable *rt;
	struct flowi fl;
	struct flowi4 *fl4 = &fl.u.ip4;
#ifdef CONFIG_AF_RXRPC_IPV6
	struct flowi6 *fl6 = &fl.u.ip6;
#endif

	peer->if_mtu = 1500;

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
	dst_release(dst);

	_leave(" [if_mtu %u]", peer->if_mtu);
}

/*
 * Allocate a peer.
 */
struct rxrpc_peer *rxrpc_alloc_peer(struct rxrpc_local *local, gfp_t gfp)
{
	const void *here = __builtin_return_address(0);
	struct rxrpc_peer *peer;

	_enter("");

	peer = kzalloc(sizeof(struct rxrpc_peer), gfp);
	if (peer) {
		atomic_set(&peer->usage, 1);
		peer->local = rxrpc_get_local(local);
		INIT_HLIST_HEAD(&peer->error_targets);
		peer->service_conns = RB_ROOT;
		seqlock_init(&peer->service_conn_lock);
		spin_lock_init(&peer->lock);
		spin_lock_init(&peer->rtt_input_lock);
		peer->debug_id = atomic_inc_return(&rxrpc_debug_id);

		rxrpc_peer_init_rtt(peer);

		if (RXRPC_TX_SMSS > 2190)
			peer->cong_cwnd = 2;
		else if (RXRPC_TX_SMSS > 1095)
			peer->cong_cwnd = 3;
		else
			peer->cong_cwnd = 4;
		trace_rxrpc_peer(peer->debug_id, rxrpc_peer_new, 1, here);
	}

	_leave(" = %p", peer);
	return peer;
}

/*
 * Initialise peer record.
 */
static void rxrpc_init_peer(struct rxrpc_sock *rx, struct rxrpc_peer *peer,
			    unsigned long hash_key)
{
	peer->hash_key = hash_key;
	rxrpc_assess_MTU_size(rx, peer);
	peer->mtu = peer->if_mtu;
	peer->rtt_last_req = ktime_get_real();

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
	peer->maxdata = peer->mtu - peer->hdrsize;
}

/*
 * Set up a new peer.
 */
static struct rxrpc_peer *rxrpc_create_peer(struct rxrpc_sock *rx,
					    struct rxrpc_local *local,
					    struct sockaddr_rxrpc *srx,
					    unsigned long hash_key,
					    gfp_t gfp)
{
	struct rxrpc_peer *peer;

	_enter("");

	peer = rxrpc_alloc_peer(local, gfp);
	if (peer) {
		memcpy(&peer->srx, srx, sizeof(*srx));
		rxrpc_init_peer(rx, peer, hash_key);
	}

	_leave(" = %p", peer);
	return peer;
}

/*
 * Set up a new incoming peer.  There shouldn't be any other matching peers
 * since we've already done a search in the list from the non-reentrant context
 * (the data_ready handler) that is the only place we can add new peers.
 */
void rxrpc_new_incoming_peer(struct rxrpc_sock *rx, struct rxrpc_local *local,
			     struct rxrpc_peer *peer)
{
	struct rxrpc_net *rxnet = local->rxnet;
	unsigned long hash_key;

	hash_key = rxrpc_peer_hash_key(local, &peer->srx);
	rxrpc_init_peer(rx, peer, hash_key);

	spin_lock(&rxnet->peer_hash_lock);
	hash_add_rcu(rxnet->peer_hash, &peer->hash_link, hash_key);
	list_add_tail(&peer->keepalive_link, &rxnet->peer_keepalive_new);
	spin_unlock(&rxnet->peer_hash_lock);
}

/*
 * obtain a remote transport endpoint for the specified address
 */
struct rxrpc_peer *rxrpc_lookup_peer(struct rxrpc_sock *rx,
				     struct rxrpc_local *local,
				     struct sockaddr_rxrpc *srx, gfp_t gfp)
{
	struct rxrpc_peer *peer, *candidate;
	struct rxrpc_net *rxnet = local->rxnet;
	unsigned long hash_key = rxrpc_peer_hash_key(local, srx);

	_enter("{%pISp}", &srx->transport);

	/* search the peer list first */
	rcu_read_lock();
	peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
	if (peer && !rxrpc_get_peer_maybe(peer))
		peer = NULL;
	rcu_read_unlock();

	if (!peer) {
		/* The peer is not yet present in hash - create a candidate
		 * for a new record and then redo the search.
		 */
		candidate = rxrpc_create_peer(rx, local, srx, hash_key, gfp);
		if (!candidate) {
			_leave(" = NULL [nomem]");
			return NULL;
		}

		spin_lock_bh(&rxnet->peer_hash_lock);

		/* Need to check that we aren't racing with someone else */
		peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
		if (peer && !rxrpc_get_peer_maybe(peer))
			peer = NULL;
		if (!peer) {
			hash_add_rcu(rxnet->peer_hash,
				     &candidate->hash_link, hash_key);
			list_add_tail(&candidate->keepalive_link,
				      &rxnet->peer_keepalive_new);
		}

		spin_unlock_bh(&rxnet->peer_hash_lock);

		if (peer)
			kfree(candidate);
		else
			peer = candidate;
	}

	_net("PEER %d {%pISp}", peer->debug_id, &peer->srx.transport);

	_leave(" = %p {u=%d}", peer, atomic_read(&peer->usage));
	return peer;
}

/*
 * Get a ref on a peer record.
 */
struct rxrpc_peer *rxrpc_get_peer(struct rxrpc_peer *peer)
{
	const void *here = __builtin_return_address(0);
	int n;

	n = atomic_inc_return(&peer->usage);
	trace_rxrpc_peer(peer->debug_id, rxrpc_peer_got, n, here);
	return peer;
}

/*
 * Get a ref on a peer record unless its usage has already reached 0.
 */
struct rxrpc_peer *rxrpc_get_peer_maybe(struct rxrpc_peer *peer)
{
	const void *here = __builtin_return_address(0);

	if (peer) {
		int n = atomic_fetch_add_unless(&peer->usage, 1, 0);
		if (n > 0)
			trace_rxrpc_peer(peer->debug_id, rxrpc_peer_got, n + 1, here);
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

	rxrpc_put_local(peer->local);
	kfree_rcu(peer, rcu);
}

/*
 * Drop a ref on a peer record.
 */
void rxrpc_put_peer(struct rxrpc_peer *peer)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id;
	int n;

	if (peer) {
		debug_id = peer->debug_id;
		n = atomic_dec_return(&peer->usage);
		trace_rxrpc_peer(debug_id, rxrpc_peer_put, n, here);
		if (n == 0)
			__rxrpc_put_peer(peer);
	}
}

/*
 * Drop a ref on a peer record where the caller already holds the
 * peer_hash_lock.
 */
void rxrpc_put_peer_locked(struct rxrpc_peer *peer)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id = peer->debug_id;
	int n;

	n = atomic_dec_return(&peer->usage);
	trace_rxrpc_peer(debug_id, rxrpc_peer_put, n, here);
	if (n == 0) {
		hash_del_rcu(&peer->hash_link);
		list_del_init(&peer->keepalive_link);
		rxrpc_put_local(peer->local);
		kfree_rcu(peer, rcu);
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
			       atomic_read(&peer->usage),
			       &peer->srx.transport);
		}
	}
}

/**
 * rxrpc_kernel_get_peer - Get the peer address of a call
 * @sock: The socket on which the call is in progress.
 * @call: The call to query
 * @_srx: Where to place the result
 *
 * Get the address of the remote peer in a call.
 */
void rxrpc_kernel_get_peer(struct socket *sock, struct rxrpc_call *call,
			   struct sockaddr_rxrpc *_srx)
{
	*_srx = call->peer->srx;
}
EXPORT_SYMBOL(rxrpc_kernel_get_peer);

/**
 * rxrpc_kernel_get_srtt - Get a call's peer smoothed RTT
 * @sock: The socket on which the call is in progress.
 * @call: The call to query
 * @_srtt: Where to store the SRTT value.
 *
 * Get the call's peer smoothed RTT in uS.
 */
bool rxrpc_kernel_get_srtt(struct socket *sock, struct rxrpc_call *call,
			   u32 *_srtt)
{
	struct rxrpc_peer *peer = call->peer;

	if (peer->rtt_count == 0) {
		*_srtt = 1000000; /* 1S */
		return false;
	}

	*_srtt = call->peer->srtt_us >> 3;
	return true;
}
EXPORT_SYMBOL(rxrpc_kernel_get_srtt);
