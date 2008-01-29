/* RxRPC remote transport endpoint management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/route.h>
#include "ar-internal.h"

static LIST_HEAD(rxrpc_peers);
static DEFINE_RWLOCK(rxrpc_peer_lock);
static DECLARE_WAIT_QUEUE_HEAD(rxrpc_peer_wq);

static void rxrpc_destroy_peer(struct work_struct *work);

/*
 * assess the MTU size for the network interface through which this peer is
 * reached
 */
static void rxrpc_assess_MTU_size(struct rxrpc_peer *peer)
{
	struct rtable *rt;
	struct flowi fl;
	int ret;

	peer->if_mtu = 1500;

	memset(&fl, 0, sizeof(fl));

	switch (peer->srx.transport.family) {
	case AF_INET:
		fl.oif = 0;
		fl.proto = IPPROTO_UDP,
		fl.nl_u.ip4_u.saddr = 0;
		fl.nl_u.ip4_u.daddr = peer->srx.transport.sin.sin_addr.s_addr;
		fl.nl_u.ip4_u.tos = 0;
		/* assume AFS.CM talking to AFS.FS */
		fl.uli_u.ports.sport = htons(7001);
		fl.uli_u.ports.dport = htons(7000);
		break;
	default:
		BUG();
	}

	ret = ip_route_output_key(&init_net, &rt, &fl);
	if (ret < 0) {
		_leave(" [route err %d]", ret);
		return;
	}

	peer->if_mtu = dst_mtu(&rt->u.dst);
	dst_release(&rt->u.dst);

	_leave(" [if_mtu %u]", peer->if_mtu);
}

/*
 * allocate a new peer
 */
static struct rxrpc_peer *rxrpc_alloc_peer(struct sockaddr_rxrpc *srx,
					   gfp_t gfp)
{
	struct rxrpc_peer *peer;

	_enter("");

	peer = kzalloc(sizeof(struct rxrpc_peer), gfp);
	if (peer) {
		INIT_WORK(&peer->destroyer, &rxrpc_destroy_peer);
		INIT_LIST_HEAD(&peer->link);
		INIT_LIST_HEAD(&peer->error_targets);
		spin_lock_init(&peer->lock);
		atomic_set(&peer->usage, 1);
		peer->debug_id = atomic_inc_return(&rxrpc_debug_id);
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

		peer->hdrsize += sizeof(struct rxrpc_header);
		peer->maxdata = peer->mtu - peer->hdrsize;
	}

	_leave(" = %p", peer);
	return peer;
}

/*
 * obtain a remote transport endpoint for the specified address
 */
struct rxrpc_peer *rxrpc_get_peer(struct sockaddr_rxrpc *srx, gfp_t gfp)
{
	struct rxrpc_peer *peer, *candidate;
	const char *new = "old";
	int usage;

	_enter("{%d,%d,%u.%u.%u.%u+%hu}",
	       srx->transport_type,
	       srx->transport_len,
	       NIPQUAD(srx->transport.sin.sin_addr),
	       ntohs(srx->transport.sin.sin_port));

	/* search the peer list first */
	read_lock_bh(&rxrpc_peer_lock);
	list_for_each_entry(peer, &rxrpc_peers, link) {
		_debug("check PEER %d { u=%d t=%d l=%d }",
		       peer->debug_id,
		       atomic_read(&peer->usage),
		       peer->srx.transport_type,
		       peer->srx.transport_len);

		if (atomic_read(&peer->usage) > 0 &&
		    peer->srx.transport_type == srx->transport_type &&
		    peer->srx.transport_len == srx->transport_len &&
		    memcmp(&peer->srx.transport,
			   &srx->transport,
			   srx->transport_len) == 0)
			goto found_extant_peer;
	}
	read_unlock_bh(&rxrpc_peer_lock);

	/* not yet present - create a candidate for a new record and then
	 * redo the search */
	candidate = rxrpc_alloc_peer(srx, gfp);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	write_lock_bh(&rxrpc_peer_lock);

	list_for_each_entry(peer, &rxrpc_peers, link) {
		if (atomic_read(&peer->usage) > 0 &&
		    peer->srx.transport_type == srx->transport_type &&
		    peer->srx.transport_len == srx->transport_len &&
		    memcmp(&peer->srx.transport,
			   &srx->transport,
			   srx->transport_len) == 0)
			goto found_extant_second;
	}

	/* we can now add the new candidate to the list */
	peer = candidate;
	candidate = NULL;

	list_add_tail(&peer->link, &rxrpc_peers);
	write_unlock_bh(&rxrpc_peer_lock);
	new = "new";

success:
	_net("PEER %s %d {%d,%u,%u.%u.%u.%u+%hu}",
	     new,
	     peer->debug_id,
	     peer->srx.transport_type,
	     peer->srx.transport.family,
	     NIPQUAD(peer->srx.transport.sin.sin_addr),
	     ntohs(peer->srx.transport.sin.sin_port));

	_leave(" = %p {u=%d}", peer, atomic_read(&peer->usage));
	return peer;

	/* we found the peer in the list immediately */
found_extant_peer:
	usage = atomic_inc_return(&peer->usage);
	read_unlock_bh(&rxrpc_peer_lock);
	goto success;

	/* we found the peer on the second time through the list */
found_extant_second:
	usage = atomic_inc_return(&peer->usage);
	write_unlock_bh(&rxrpc_peer_lock);
	kfree(candidate);
	goto success;
}

/*
 * find the peer associated with a packet
 */
struct rxrpc_peer *rxrpc_find_peer(struct rxrpc_local *local,
				   __be32 addr, __be16 port)
{
	struct rxrpc_peer *peer;

	_enter("");

	/* search the peer list */
	read_lock_bh(&rxrpc_peer_lock);

	if (local->srx.transport.family == AF_INET &&
	    local->srx.transport_type == SOCK_DGRAM
	    ) {
		list_for_each_entry(peer, &rxrpc_peers, link) {
			if (atomic_read(&peer->usage) > 0 &&
			    peer->srx.transport_type == SOCK_DGRAM &&
			    peer->srx.transport.family == AF_INET &&
			    peer->srx.transport.sin.sin_port == port &&
			    peer->srx.transport.sin.sin_addr.s_addr == addr)
				goto found_UDP_peer;
		}

		goto new_UDP_peer;
	}

	read_unlock_bh(&rxrpc_peer_lock);
	_leave(" = -EAFNOSUPPORT");
	return ERR_PTR(-EAFNOSUPPORT);

found_UDP_peer:
	_net("Rx UDP DGRAM from peer %d", peer->debug_id);
	atomic_inc(&peer->usage);
	read_unlock_bh(&rxrpc_peer_lock);
	_leave(" = %p", peer);
	return peer;

new_UDP_peer:
	_net("Rx UDP DGRAM from NEW peer %d", peer->debug_id);
	read_unlock_bh(&rxrpc_peer_lock);
	_leave(" = -EBUSY [new]");
	return ERR_PTR(-EBUSY);
}

/*
 * release a remote transport endpoint
 */
void rxrpc_put_peer(struct rxrpc_peer *peer)
{
	_enter("%p{u=%d}", peer, atomic_read(&peer->usage));

	ASSERTCMP(atomic_read(&peer->usage), >, 0);

	if (likely(!atomic_dec_and_test(&peer->usage))) {
		_leave(" [in use]");
		return;
	}

	rxrpc_queue_work(&peer->destroyer);
	_leave("");
}

/*
 * destroy a remote transport endpoint
 */
static void rxrpc_destroy_peer(struct work_struct *work)
{
	struct rxrpc_peer *peer =
		container_of(work, struct rxrpc_peer, destroyer);

	_enter("%p{%d}", peer, atomic_read(&peer->usage));

	write_lock_bh(&rxrpc_peer_lock);
	list_del(&peer->link);
	write_unlock_bh(&rxrpc_peer_lock);

	_net("DESTROY PEER %d", peer->debug_id);
	kfree(peer);

	if (list_empty(&rxrpc_peers))
		wake_up_all(&rxrpc_peer_wq);
	_leave("");
}

/*
 * preemptively destroy all the peer records from a transport endpoint rather
 * than waiting for them to time out
 */
void __exit rxrpc_destroy_all_peers(void)
{
	DECLARE_WAITQUEUE(myself,current);

	_enter("");

	/* we simply have to wait for them to go away */
	if (!list_empty(&rxrpc_peers)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&rxrpc_peer_wq, &myself);

		while (!list_empty(&rxrpc_peers)) {
			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);
		}

		remove_wait_queue(&rxrpc_peer_wq, &myself);
		set_current_state(TASK_RUNNING);
	}

	_leave("");
}
