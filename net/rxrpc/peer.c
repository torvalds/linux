/* peer.c: Rx RPC peer management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
#include "internal.h"

__RXACCT_DECL(atomic_t rxrpc_peer_count);
LIST_HEAD(rxrpc_peers);
DECLARE_RWSEM(rxrpc_peers_sem);
unsigned long rxrpc_peer_timeout = 12 * 60 * 60;

static void rxrpc_peer_do_timeout(struct rxrpc_peer *peer);

static void __rxrpc_peer_timeout(rxrpc_timer_t *timer)
{
	struct rxrpc_peer *peer =
		list_entry(timer, struct rxrpc_peer, timeout);

	_debug("Rx PEER TIMEOUT [%p{u=%d}]", peer, atomic_read(&peer->usage));

	rxrpc_peer_do_timeout(peer);
}

static const struct rxrpc_timer_ops rxrpc_peer_timer_ops = {
	.timed_out	= __rxrpc_peer_timeout,
};

/*****************************************************************************/
/*
 * create a peer record
 */
static int __rxrpc_create_peer(struct rxrpc_transport *trans, __be32 addr,
			       struct rxrpc_peer **_peer)
{
	struct rxrpc_peer *peer;

	_enter("%p,%08x", trans, ntohl(addr));

	/* allocate and initialise a peer record */
	peer = kzalloc(sizeof(struct rxrpc_peer), GFP_KERNEL);
	if (!peer) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	atomic_set(&peer->usage, 1);

	INIT_LIST_HEAD(&peer->link);
	INIT_LIST_HEAD(&peer->proc_link);
	INIT_LIST_HEAD(&peer->conn_idlist);
	INIT_LIST_HEAD(&peer->conn_active);
	INIT_LIST_HEAD(&peer->conn_graveyard);
	spin_lock_init(&peer->conn_gylock);
	init_waitqueue_head(&peer->conn_gy_waitq);
	rwlock_init(&peer->conn_idlock);
	rwlock_init(&peer->conn_lock);
	atomic_set(&peer->conn_count, 0);
	spin_lock_init(&peer->lock);
	rxrpc_timer_init(&peer->timeout, &rxrpc_peer_timer_ops);

	peer->addr.s_addr = addr;

	peer->trans = trans;
	peer->ops = trans->peer_ops;

	__RXACCT(atomic_inc(&rxrpc_peer_count));
	*_peer = peer;
	_leave(" = 0 (%p)", peer);

	return 0;
} /* end __rxrpc_create_peer() */

/*****************************************************************************/
/*
 * find a peer record on the specified transport
 * - returns (if successful) with peer record usage incremented
 * - resurrects it from the graveyard if found there
 */
int rxrpc_peer_lookup(struct rxrpc_transport *trans, __be32 addr,
		      struct rxrpc_peer **_peer)
{
	struct rxrpc_peer *peer, *candidate = NULL;
	struct list_head *_p;
	int ret;

	_enter("%p{%hu},%08x", trans, trans->port, ntohl(addr));

	/* [common case] search the transport's active list first */
	read_lock(&trans->peer_lock);
	list_for_each(_p, &trans->peer_active) {
		peer = list_entry(_p, struct rxrpc_peer, link);
		if (peer->addr.s_addr == addr)
			goto found_active;
	}
	read_unlock(&trans->peer_lock);

	/* [uncommon case] not active - create a candidate for a new record */
	ret = __rxrpc_create_peer(trans, addr, &candidate);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	/* search the active list again, just in case it appeared whilst we
	 * were busy */
	write_lock(&trans->peer_lock);
	list_for_each(_p, &trans->peer_active) {
		peer = list_entry(_p, struct rxrpc_peer, link);
		if (peer->addr.s_addr == addr)
			goto found_active_second_chance;
	}

	/* search the transport's graveyard list */
	spin_lock(&trans->peer_gylock);
	list_for_each(_p, &trans->peer_graveyard) {
		peer = list_entry(_p, struct rxrpc_peer, link);
		if (peer->addr.s_addr == addr)
			goto found_in_graveyard;
	}
	spin_unlock(&trans->peer_gylock);

	/* we can now add the new candidate to the list
	 * - tell the application layer that this peer has been added
	 */
	rxrpc_get_transport(trans);
	peer = candidate;
	candidate = NULL;

	if (peer->ops && peer->ops->adding) {
		ret = peer->ops->adding(peer);
		if (ret < 0) {
			write_unlock(&trans->peer_lock);
			__RXACCT(atomic_dec(&rxrpc_peer_count));
			kfree(peer);
			rxrpc_put_transport(trans);
			_leave(" = %d", ret);
			return ret;
		}
	}

	atomic_inc(&trans->peer_count);

 make_active:
	list_add_tail(&peer->link, &trans->peer_active);

 success_uwfree:
	write_unlock(&trans->peer_lock);

	if (candidate) {
		__RXACCT(atomic_dec(&rxrpc_peer_count));
		kfree(candidate);
	}

	if (list_empty(&peer->proc_link)) {
		down_write(&rxrpc_peers_sem);
		list_add_tail(&peer->proc_link, &rxrpc_peers);
		up_write(&rxrpc_peers_sem);
	}

 success:
	*_peer = peer;

	_leave(" = 0 (%p{u=%d cc=%d})",
	       peer,
	       atomic_read(&peer->usage),
	       atomic_read(&peer->conn_count));
	return 0;

	/* handle the peer being found in the active list straight off */
 found_active:
	rxrpc_get_peer(peer);
	read_unlock(&trans->peer_lock);
	goto success;

	/* handle resurrecting a peer from the graveyard */
 found_in_graveyard:
	rxrpc_get_peer(peer);
	rxrpc_get_transport(peer->trans);
	rxrpc_krxtimod_del_timer(&peer->timeout);
	list_del_init(&peer->link);
	spin_unlock(&trans->peer_gylock);
	goto make_active;

	/* handle finding the peer on the second time through the active
	 * list */
 found_active_second_chance:
	rxrpc_get_peer(peer);
	goto success_uwfree;

} /* end rxrpc_peer_lookup() */

/*****************************************************************************/
/*
 * finish with a peer record
 * - it gets sent to the graveyard from where it can be resurrected or timed
 *   out
 */
void rxrpc_put_peer(struct rxrpc_peer *peer)
{
	struct rxrpc_transport *trans = peer->trans;

	_enter("%p{cc=%d a=%08x}",
	       peer,
	       atomic_read(&peer->conn_count),
	       ntohl(peer->addr.s_addr));

	/* sanity check */
	if (atomic_read(&peer->usage) <= 0)
		BUG();

	write_lock(&trans->peer_lock);
	spin_lock(&trans->peer_gylock);
	if (likely(!atomic_dec_and_test(&peer->usage))) {
		spin_unlock(&trans->peer_gylock);
		write_unlock(&trans->peer_lock);
		_leave("");
		return;
	}

	/* move to graveyard queue */
	list_del(&peer->link);
	write_unlock(&trans->peer_lock);

	list_add_tail(&peer->link, &trans->peer_graveyard);

	BUG_ON(!list_empty(&peer->conn_active));

	rxrpc_krxtimod_add_timer(&peer->timeout, rxrpc_peer_timeout * HZ);

	spin_unlock(&trans->peer_gylock);

	rxrpc_put_transport(trans);

	_leave(" [killed]");
} /* end rxrpc_put_peer() */

/*****************************************************************************/
/*
 * handle a peer timing out in the graveyard
 * - called from krxtimod
 */
static void rxrpc_peer_do_timeout(struct rxrpc_peer *peer)
{
	struct rxrpc_transport *trans = peer->trans;

	_enter("%p{u=%d cc=%d a=%08x}",
	       peer,
	       atomic_read(&peer->usage),
	       atomic_read(&peer->conn_count),
	       ntohl(peer->addr.s_addr));

	BUG_ON(atomic_read(&peer->usage) < 0);

	/* remove from graveyard if still dead */
	spin_lock(&trans->peer_gylock);
	if (atomic_read(&peer->usage) == 0)
		list_del_init(&peer->link);
	else
		peer = NULL;
	spin_unlock(&trans->peer_gylock);

	if (!peer) {
		_leave("");
		return; /* resurrected */
	}

	/* clear all connections on this peer */
	rxrpc_conn_clearall(peer);

	BUG_ON(!list_empty(&peer->conn_active));
	BUG_ON(!list_empty(&peer->conn_graveyard));

	/* inform the application layer */
	if (peer->ops && peer->ops->discarding)
		peer->ops->discarding(peer);

	if (!list_empty(&peer->proc_link)) {
		down_write(&rxrpc_peers_sem);
		list_del(&peer->proc_link);
		up_write(&rxrpc_peers_sem);
	}

	__RXACCT(atomic_dec(&rxrpc_peer_count));
	kfree(peer);

	/* if the graveyard is now empty, wake up anyone waiting for that */
	if (atomic_dec_and_test(&trans->peer_count))
		wake_up(&trans->peer_gy_waitq);

	_leave(" [destroyed]");
} /* end rxrpc_peer_do_timeout() */

/*****************************************************************************/
/*
 * clear all peer records from a transport endpoint
 */
void rxrpc_peer_clearall(struct rxrpc_transport *trans)
{
	DECLARE_WAITQUEUE(myself,current);

	struct rxrpc_peer *peer;
	int err;

	_enter("%p",trans);

	/* there shouldn't be any active peers remaining */
	BUG_ON(!list_empty(&trans->peer_active));

	/* manually timeout all peers in the graveyard */
	spin_lock(&trans->peer_gylock);
	while (!list_empty(&trans->peer_graveyard)) {
		peer = list_entry(trans->peer_graveyard.next,
				  struct rxrpc_peer, link);
		_debug("Clearing peer %p\n", peer);
		err = rxrpc_krxtimod_del_timer(&peer->timeout);
		spin_unlock(&trans->peer_gylock);

		if (err == 0)
			rxrpc_peer_do_timeout(peer);

		spin_lock(&trans->peer_gylock);
	}
	spin_unlock(&trans->peer_gylock);

	/* wait for the the peer graveyard to be completely cleared */
	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&trans->peer_gy_waitq, &myself);

	while (atomic_read(&trans->peer_count) != 0) {
		schedule();
		set_current_state(TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&trans->peer_gy_waitq, &myself);
	set_current_state(TASK_RUNNING);

	_leave("");
} /* end rxrpc_peer_clearall() */

/*****************************************************************************/
/*
 * calculate and cache the Round-Trip-Time for a message and its response
 */
void rxrpc_peer_calculate_rtt(struct rxrpc_peer *peer,
			      struct rxrpc_message *msg,
			      struct rxrpc_message *resp)
{
	unsigned long long rtt;
	int loop;

	_enter("%p,%p,%p", peer, msg, resp);

	/* calculate the latest RTT */
	rtt = resp->stamp.tv_sec - msg->stamp.tv_sec;
	rtt *= 1000000UL;
	rtt += resp->stamp.tv_usec - msg->stamp.tv_usec;

	/* add to cache */
	peer->rtt_cache[peer->rtt_point] = rtt;
	peer->rtt_point++;
	peer->rtt_point %= RXRPC_RTT_CACHE_SIZE;

	if (peer->rtt_usage < RXRPC_RTT_CACHE_SIZE)
		peer->rtt_usage++;

	/* recalculate RTT */
	rtt = 0;
	for (loop = peer->rtt_usage - 1; loop >= 0; loop--)
		rtt += peer->rtt_cache[loop];

	do_div(rtt, peer->rtt_usage);
	peer->rtt = rtt;

	_leave(" RTT=%lu.%lums",
	       (long) (peer->rtt / 1000), (long) (peer->rtt % 1000));

} /* end rxrpc_peer_calculate_rtt() */
