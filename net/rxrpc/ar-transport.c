/* RxRPC point-to-point transport session management
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
#include <linux/slab.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Time after last use at which transport record is cleaned up.
 */
unsigned int rxrpc_transport_expiry = 3600 * 24;

static void rxrpc_transport_reaper(struct work_struct *work);

static LIST_HEAD(rxrpc_transports);
static DEFINE_RWLOCK(rxrpc_transport_lock);
static DECLARE_DELAYED_WORK(rxrpc_transport_reap, rxrpc_transport_reaper);

/*
 * allocate a new transport session manager
 */
static struct rxrpc_transport *rxrpc_alloc_transport(struct rxrpc_local *local,
						     struct rxrpc_peer *peer,
						     gfp_t gfp)
{
	struct rxrpc_transport *trans;

	_enter("");

	trans = kzalloc(sizeof(struct rxrpc_transport), gfp);
	if (trans) {
		trans->local = local;
		trans->peer = peer;
		INIT_LIST_HEAD(&trans->link);
		trans->bundles = RB_ROOT;
		trans->client_conns = RB_ROOT;
		trans->server_conns = RB_ROOT;
		skb_queue_head_init(&trans->error_queue);
		spin_lock_init(&trans->client_lock);
		rwlock_init(&trans->conn_lock);
		atomic_set(&trans->usage, 1);
		trans->conn_idcounter = peer->srx.srx_service << 16;
		trans->debug_id = atomic_inc_return(&rxrpc_debug_id);

		if (peer->srx.transport.family == AF_INET) {
			switch (peer->srx.transport_type) {
			case SOCK_DGRAM:
				INIT_WORK(&trans->error_handler,
					  rxrpc_UDP_error_handler);
				break;
			default:
				BUG();
				break;
			}
		} else {
			BUG();
		}
	}

	_leave(" = %p", trans);
	return trans;
}

/*
 * obtain a transport session for the nominated endpoints
 */
struct rxrpc_transport *rxrpc_get_transport(struct rxrpc_local *local,
					    struct rxrpc_peer *peer,
					    gfp_t gfp)
{
	struct rxrpc_transport *trans, *candidate;
	const char *new = "old";
	int usage;

	_enter("{%pI4+%hu},{%pI4+%hu},",
	       &local->srx.transport.sin.sin_addr,
	       ntohs(local->srx.transport.sin.sin_port),
	       &peer->srx.transport.sin.sin_addr,
	       ntohs(peer->srx.transport.sin.sin_port));

	/* search the transport list first */
	read_lock_bh(&rxrpc_transport_lock);
	list_for_each_entry(trans, &rxrpc_transports, link) {
		if (trans->local == local && trans->peer == peer)
			goto found_extant_transport;
	}
	read_unlock_bh(&rxrpc_transport_lock);

	/* not yet present - create a candidate for a new record and then
	 * redo the search */
	candidate = rxrpc_alloc_transport(local, peer, gfp);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	write_lock_bh(&rxrpc_transport_lock);

	list_for_each_entry(trans, &rxrpc_transports, link) {
		if (trans->local == local && trans->peer == peer)
			goto found_extant_second;
	}

	/* we can now add the new candidate to the list */
	trans = candidate;
	candidate = NULL;
	usage = atomic_read(&trans->usage);

	rxrpc_get_local(trans->local);
	atomic_inc(&trans->peer->usage);
	list_add_tail(&trans->link, &rxrpc_transports);
	write_unlock_bh(&rxrpc_transport_lock);
	new = "new";

success:
	_net("TRANSPORT %s %d local %d -> peer %d",
	     new,
	     trans->debug_id,
	     trans->local->debug_id,
	     trans->peer->debug_id);

	_leave(" = %p {u=%d}", trans, usage);
	return trans;

	/* we found the transport in the list immediately */
found_extant_transport:
	usage = atomic_inc_return(&trans->usage);
	read_unlock_bh(&rxrpc_transport_lock);
	goto success;

	/* we found the transport on the second time through the list */
found_extant_second:
	usage = atomic_inc_return(&trans->usage);
	write_unlock_bh(&rxrpc_transport_lock);
	kfree(candidate);
	goto success;
}

/*
 * find the transport connecting two endpoints
 */
struct rxrpc_transport *rxrpc_find_transport(struct rxrpc_local *local,
					     struct rxrpc_peer *peer)
{
	struct rxrpc_transport *trans;

	_enter("{%pI4+%hu},{%pI4+%hu},",
	       &local->srx.transport.sin.sin_addr,
	       ntohs(local->srx.transport.sin.sin_port),
	       &peer->srx.transport.sin.sin_addr,
	       ntohs(peer->srx.transport.sin.sin_port));

	/* search the transport list */
	read_lock_bh(&rxrpc_transport_lock);

	list_for_each_entry(trans, &rxrpc_transports, link) {
		if (trans->local == local && trans->peer == peer)
			goto found_extant_transport;
	}

	read_unlock_bh(&rxrpc_transport_lock);
	_leave(" = NULL");
	return NULL;

found_extant_transport:
	atomic_inc(&trans->usage);
	read_unlock_bh(&rxrpc_transport_lock);
	_leave(" = %p", trans);
	return trans;
}

/*
 * release a transport session
 */
void rxrpc_put_transport(struct rxrpc_transport *trans)
{
	_enter("%p{u=%d}", trans, atomic_read(&trans->usage));

	ASSERTCMP(atomic_read(&trans->usage), >, 0);

	trans->put_time = ktime_get_seconds();
	if (unlikely(atomic_dec_and_test(&trans->usage))) {
		_debug("zombie");
		/* let the reaper determine the timeout to avoid a race with
		 * overextending the timeout if the reaper is running at the
		 * same time */
		rxrpc_queue_delayed_work(&rxrpc_transport_reap, 0);
	}
	_leave("");
}

/*
 * clean up a transport session
 */
static void rxrpc_cleanup_transport(struct rxrpc_transport *trans)
{
	_net("DESTROY TRANS %d", trans->debug_id);

	rxrpc_purge_queue(&trans->error_queue);

	rxrpc_put_local(trans->local);
	rxrpc_put_peer(trans->peer);
	kfree(trans);
}

/*
 * reap dead transports that have passed their expiry date
 */
static void rxrpc_transport_reaper(struct work_struct *work)
{
	struct rxrpc_transport *trans, *_p;
	unsigned long now, earliest, reap_time;

	LIST_HEAD(graveyard);

	_enter("");

	now = ktime_get_seconds();
	earliest = ULONG_MAX;

	/* extract all the transports that have been dead too long */
	write_lock_bh(&rxrpc_transport_lock);
	list_for_each_entry_safe(trans, _p, &rxrpc_transports, link) {
		_debug("reap TRANS %d { u=%d t=%ld }",
		       trans->debug_id, atomic_read(&trans->usage),
		       (long) now - (long) trans->put_time);

		if (likely(atomic_read(&trans->usage) > 0))
			continue;

		reap_time = trans->put_time + rxrpc_transport_expiry;
		if (reap_time <= now)
			list_move_tail(&trans->link, &graveyard);
		else if (reap_time < earliest)
			earliest = reap_time;
	}
	write_unlock_bh(&rxrpc_transport_lock);

	if (earliest != ULONG_MAX) {
		_debug("reschedule reaper %ld", (long) earliest - now);
		ASSERTCMP(earliest, >, now);
		rxrpc_queue_delayed_work(&rxrpc_transport_reap,
					 (earliest - now) * HZ);
	}

	/* then destroy all those pulled out */
	while (!list_empty(&graveyard)) {
		trans = list_entry(graveyard.next, struct rxrpc_transport,
				   link);
		list_del_init(&trans->link);

		ASSERTCMP(atomic_read(&trans->usage), ==, 0);
		rxrpc_cleanup_transport(trans);
	}

	_leave("");
}

/*
 * preemptively destroy all the transport session records rather than waiting
 * for them to time out
 */
void __exit rxrpc_destroy_all_transports(void)
{
	_enter("");

	rxrpc_transport_expiry = 0;
	cancel_delayed_work(&rxrpc_transport_reap);
	rxrpc_queue_delayed_work(&rxrpc_transport_reap, 0);

	_leave("");
}
