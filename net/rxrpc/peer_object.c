/* RxRPC remote transport endpoint record management
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/route.h>
#include "ar-internal.h"

static DEFINE_HASHTABLE(rxrpc_peer_hash, 10);
static DEFINE_SPINLOCK(rxrpc_peer_hash_lock);

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

	hash_for_each_possible_rcu(rxrpc_peer_hash, peer, hash_link, hash_key) {
		if (rxrpc_peer_cmp_key(peer, local, srx, hash_key) == 0) {
			if (atomic_read(&peer->usage) == 0)
				return NULL;
			return peer;
		}
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
		switch (srx->transport.family) {
		case AF_INET:
			_net("PEER %d {%d,%u,%pI4+%hu}",
			     peer->debug_id,
			     peer->srx.transport_type,
			     peer->srx.transport.family,
			     &peer->srx.transport.sin.sin_addr,
			     ntohs(peer->srx.transport.sin.sin_port));
			break;
		}

		_leave(" = %p {u=%d}", peer, atomic_read(&peer->usage));
	}
	return peer;
}

/*
 * assess the MTU size for the network interface through which this peer is
 * reached
 */
static void rxrpc_assess_MTU_size(struct rxrpc_peer *peer)
{
	struct rtable *rt;
	struct flowi4 fl4;

	peer->if_mtu = 1500;

	rt = ip_route_output_ports(&init_net, &fl4, NULL,
				   peer->srx.transport.sin.sin_addr.s_addr, 0,
				   htons(7000), htons(7001),
				   IPPROTO_UDP, 0, 0);
	if (IS_ERR(rt)) {
		_leave(" [route err %ld]", PTR_ERR(rt));
		return;
	}

	peer->if_mtu = dst_mtu(&rt->dst);
	dst_release(&rt->dst);

	_leave(" [if_mtu %u]", peer->if_mtu);
}

/*
 * Allocate a peer.
 */
struct rxrpc_peer *rxrpc_alloc_peer(struct rxrpc_local *local, gfp_t gfp)
{
	struct rxrpc_peer *peer;

	_enter("");

	peer = kzalloc(sizeof(struct rxrpc_peer), gfp);
	if (peer) {
		atomic_set(&peer->usage, 1);
		peer->local = local;
		INIT_HLIST_HEAD(&peer->error_targets);
		INIT_WORK(&peer->error_distributor,
			  &rxrpc_peer_error_distributor);
		peer->service_conns = RB_ROOT;
		seqlock_init(&peer->service_conn_lock);
		spin_lock_init(&peer->lock);
		peer->debug_id = atomic_inc_return(&rxrpc_debug_id);
	}

	_leave(" = %p", peer);
	return peer;
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

	peer = rxrpc_alloc_peer(local, gfp);
	if (peer) {
		peer->hash_key = hash_key;
		memcpy(&peer->srx, srx, sizeof(*srx));

		rxrpc_assess_MTU_size(peer);
		peer->mtu = peer->if_mtu;

		if (srx->transport.family == AF_INET) {
			peer->hdrsize = sizeof(struct iphdr);
			switch (srx->transport_type) {
			case SOCK_DGRAM:
				peer->hdrsize += sizeof(struct udphdr);
				break;
			default:
				BUG();
				break;
			}
		} else {
			BUG();
		}

		peer->hdrsize += sizeof(struct rxrpc_wire_header);
		peer->maxdata = peer->mtu - peer->hdrsize;
	}

	_leave(" = %p", peer);
	return peer;
}

/*
 * obtain a remote transport endpoint for the specified address
 */
struct rxrpc_peer *rxrpc_lookup_peer(struct rxrpc_local *local,
				     struct sockaddr_rxrpc *srx, gfp_t gfp)
{
	struct rxrpc_peer *peer, *candidate;
	unsigned long hash_key = rxrpc_peer_hash_key(local, srx);

	_enter("{%d,%d,%pI4+%hu}",
	       srx->transport_type,
	       srx->transport_len,
	       &srx->transport.sin.sin_addr,
	       ntohs(srx->transport.sin.sin_port));

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
		candidate = rxrpc_create_peer(local, srx, hash_key, gfp);
		if (!candidate) {
			_leave(" = NULL [nomem]");
			return NULL;
		}

		spin_lock(&rxrpc_peer_hash_lock);

		/* Need to check that we aren't racing with someone else */
		peer = __rxrpc_lookup_peer_rcu(local, srx, hash_key);
		if (peer && !rxrpc_get_peer_maybe(peer))
			peer = NULL;
		if (!peer)
			hash_add_rcu(rxrpc_peer_hash,
				     &candidate->hash_link, hash_key);

		spin_unlock(&rxrpc_peer_hash_lock);

		if (peer)
			kfree(candidate);
		else
			peer = candidate;
	}

	_net("PEER %d {%d,%pI4+%hu}",
	     peer->debug_id,
	     peer->srx.transport_type,
	     &peer->srx.transport.sin.sin_addr,
	     ntohs(peer->srx.transport.sin.sin_port));

	_leave(" = %p {u=%d}", peer, atomic_read(&peer->usage));
	return peer;
}

/*
 * Discard a ref on a remote peer record.
 */
void __rxrpc_put_peer(struct rxrpc_peer *peer)
{
	ASSERT(hlist_empty(&peer->error_targets));

	spin_lock(&rxrpc_peer_hash_lock);
	hash_del_rcu(&peer->hash_link);
	spin_unlock(&rxrpc_peer_hash_lock);

	kfree_rcu(peer, rcu);
}
