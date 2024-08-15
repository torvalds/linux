// SPDX-License-Identifier: GPL-2.0-or-later
/* Client connection-specific management code.
 *
 * Copyright (C) 2016, 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Client connections need to be cached for a little while after they've made a
 * call so as to handle retransmitted DATA packets in case the server didn't
 * receive the final ACK or terminating ABORT we sent it.
 *
 * There are flags of relevance to the cache:
 *
 *  (2) DONT_REUSE - The connection should be discarded as soon as possible and
 *      should not be reused.  This is set when an exclusive connection is used
 *      or a call ID counter overflows.
 *
 * The caching state may only be changed if the cache lock is held.
 *
 * There are two idle client connection expiry durations.  If the total number
 * of connections is below the reap threshold, we use the normal duration; if
 * it's above, we use the fast duration.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/timer.h>
#include <linux/sched/signal.h>

#include "ar-internal.h"

__read_mostly unsigned int rxrpc_reap_client_connections = 900;
__read_mostly unsigned long rxrpc_conn_idle_client_expiry = 2 * 60 * HZ;
__read_mostly unsigned long rxrpc_conn_idle_client_fast_expiry = 2 * HZ;

/*
 * We use machine-unique IDs for our client connections.
 */
DEFINE_IDR(rxrpc_client_conn_ids);
static DEFINE_SPINLOCK(rxrpc_conn_id_lock);

static void rxrpc_deactivate_bundle(struct rxrpc_bundle *bundle);

/*
 * Get a connection ID and epoch for a client connection from the global pool.
 * The connection struct pointer is then recorded in the idr radix tree.  The
 * epoch doesn't change until the client is rebooted (or, at least, unless the
 * module is unloaded).
 */
static int rxrpc_get_client_connection_id(struct rxrpc_connection *conn,
					  gfp_t gfp)
{
	struct rxrpc_net *rxnet = conn->params.local->rxnet;
	int id;

	_enter("");

	idr_preload(gfp);
	spin_lock(&rxrpc_conn_id_lock);

	id = idr_alloc_cyclic(&rxrpc_client_conn_ids, conn,
			      1, 0x40000000, GFP_NOWAIT);
	if (id < 0)
		goto error;

	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();

	conn->proto.epoch = rxnet->epoch;
	conn->proto.cid = id << RXRPC_CIDSHIFT;
	set_bit(RXRPC_CONN_HAS_IDR, &conn->flags);
	_leave(" [CID %x]", conn->proto.cid);
	return 0;

error:
	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();
	_leave(" = %d", id);
	return id;
}

/*
 * Release a connection ID for a client connection from the global pool.
 */
static void rxrpc_put_client_connection_id(struct rxrpc_connection *conn)
{
	if (test_bit(RXRPC_CONN_HAS_IDR, &conn->flags)) {
		spin_lock(&rxrpc_conn_id_lock);
		idr_remove(&rxrpc_client_conn_ids,
			   conn->proto.cid >> RXRPC_CIDSHIFT);
		spin_unlock(&rxrpc_conn_id_lock);
	}
}

/*
 * Destroy the client connection ID tree.
 */
void rxrpc_destroy_client_conn_ids(void)
{
	struct rxrpc_connection *conn;
	int id;

	if (!idr_is_empty(&rxrpc_client_conn_ids)) {
		idr_for_each_entry(&rxrpc_client_conn_ids, conn, id) {
			pr_err("AF_RXRPC: Leaked client conn %p {%d}\n",
			       conn, refcount_read(&conn->ref));
		}
		BUG();
	}

	idr_destroy(&rxrpc_client_conn_ids);
}

/*
 * Allocate a connection bundle.
 */
static struct rxrpc_bundle *rxrpc_alloc_bundle(struct rxrpc_conn_parameters *cp,
					       gfp_t gfp)
{
	struct rxrpc_bundle *bundle;

	bundle = kzalloc(sizeof(*bundle), gfp);
	if (bundle) {
		bundle->params = *cp;
		rxrpc_get_peer(bundle->params.peer);
		refcount_set(&bundle->ref, 1);
		atomic_set(&bundle->active, 1);
		spin_lock_init(&bundle->channel_lock);
		INIT_LIST_HEAD(&bundle->waiting_calls);
	}
	return bundle;
}

struct rxrpc_bundle *rxrpc_get_bundle(struct rxrpc_bundle *bundle)
{
	refcount_inc(&bundle->ref);
	return bundle;
}

static void rxrpc_free_bundle(struct rxrpc_bundle *bundle)
{
	rxrpc_put_peer(bundle->params.peer);
	kfree(bundle);
}

void rxrpc_put_bundle(struct rxrpc_bundle *bundle)
{
	unsigned int d = bundle->debug_id;
	bool dead;
	int r;

	dead = __refcount_dec_and_test(&bundle->ref, &r);

	_debug("PUT B=%x %d", d, r - 1);
	if (dead)
		rxrpc_free_bundle(bundle);
}

/*
 * Allocate a client connection.
 */
static struct rxrpc_connection *
rxrpc_alloc_client_connection(struct rxrpc_bundle *bundle, gfp_t gfp)
{
	struct rxrpc_connection *conn;
	struct rxrpc_net *rxnet = bundle->params.local->rxnet;
	int ret;

	_enter("");

	conn = rxrpc_alloc_connection(gfp);
	if (!conn) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	refcount_set(&conn->ref, 1);
	conn->bundle		= bundle;
	conn->params		= bundle->params;
	conn->out_clientflag	= RXRPC_CLIENT_INITIATED;
	conn->state		= RXRPC_CONN_CLIENT;
	conn->service_id	= conn->params.service_id;

	ret = rxrpc_get_client_connection_id(conn, gfp);
	if (ret < 0)
		goto error_0;

	ret = rxrpc_init_client_conn_security(conn);
	if (ret < 0)
		goto error_1;

	atomic_inc(&rxnet->nr_conns);
	write_lock(&rxnet->conn_lock);
	list_add_tail(&conn->proc_link, &rxnet->conn_proc_list);
	write_unlock(&rxnet->conn_lock);

	rxrpc_get_bundle(bundle);
	rxrpc_get_peer(conn->params.peer);
	rxrpc_get_local(conn->params.local);
	key_get(conn->params.key);

	trace_rxrpc_conn(conn->debug_id, rxrpc_conn_new_client,
			 refcount_read(&conn->ref),
			 __builtin_return_address(0));

	atomic_inc(&rxnet->nr_client_conns);
	trace_rxrpc_client(conn, -1, rxrpc_client_alloc);
	_leave(" = %p", conn);
	return conn;

error_1:
	rxrpc_put_client_connection_id(conn);
error_0:
	kfree(conn);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * Determine if a connection may be reused.
 */
static bool rxrpc_may_reuse_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_net *rxnet;
	int id_cursor, id, distance, limit;

	if (!conn)
		goto dont_reuse;

	rxnet = conn->params.local->rxnet;
	if (test_bit(RXRPC_CONN_DONT_REUSE, &conn->flags))
		goto dont_reuse;

	if (conn->state != RXRPC_CONN_CLIENT ||
	    conn->proto.epoch != rxnet->epoch)
		goto mark_dont_reuse;

	/* The IDR tree gets very expensive on memory if the connection IDs are
	 * widely scattered throughout the number space, so we shall want to
	 * kill off connections that, say, have an ID more than about four
	 * times the maximum number of client conns away from the current
	 * allocation point to try and keep the IDs concentrated.
	 */
	id_cursor = idr_get_cursor(&rxrpc_client_conn_ids);
	id = conn->proto.cid >> RXRPC_CIDSHIFT;
	distance = id - id_cursor;
	if (distance < 0)
		distance = -distance;
	limit = max_t(unsigned long, atomic_read(&rxnet->nr_conns) * 4, 1024);
	if (distance > limit)
		goto mark_dont_reuse;

	return true;

mark_dont_reuse:
	set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);
dont_reuse:
	return false;
}

/*
 * Look up the conn bundle that matches the connection parameters, adding it if
 * it doesn't yet exist.
 */
static struct rxrpc_bundle *rxrpc_look_up_bundle(struct rxrpc_conn_parameters *cp,
						 gfp_t gfp)
{
	static atomic_t rxrpc_bundle_id;
	struct rxrpc_bundle *bundle, *candidate;
	struct rxrpc_local *local = cp->local;
	struct rb_node *p, **pp, *parent;
	long diff;

	_enter("{%px,%x,%u,%u}",
	       cp->peer, key_serial(cp->key), cp->security_level, cp->upgrade);

	if (cp->exclusive)
		return rxrpc_alloc_bundle(cp, gfp);

	/* First, see if the bundle is already there. */
	_debug("search 1");
	spin_lock(&local->client_bundles_lock);
	p = local->client_bundles.rb_node;
	while (p) {
		bundle = rb_entry(p, struct rxrpc_bundle, local_node);

#define cmp(X) ((long)bundle->params.X - (long)cp->X)
		diff = (cmp(peer) ?:
			cmp(key) ?:
			cmp(security_level) ?:
			cmp(upgrade));
#undef cmp
		if (diff < 0)
			p = p->rb_left;
		else if (diff > 0)
			p = p->rb_right;
		else
			goto found_bundle;
	}
	spin_unlock(&local->client_bundles_lock);
	_debug("not found");

	/* It wasn't.  We need to add one. */
	candidate = rxrpc_alloc_bundle(cp, gfp);
	if (!candidate)
		return NULL;

	_debug("search 2");
	spin_lock(&local->client_bundles_lock);
	pp = &local->client_bundles.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		bundle = rb_entry(parent, struct rxrpc_bundle, local_node);

#define cmp(X) ((long)bundle->params.X - (long)cp->X)
		diff = (cmp(peer) ?:
			cmp(key) ?:
			cmp(security_level) ?:
			cmp(upgrade));
#undef cmp
		if (diff < 0)
			pp = &(*pp)->rb_left;
		else if (diff > 0)
			pp = &(*pp)->rb_right;
		else
			goto found_bundle_free;
	}

	_debug("new bundle");
	candidate->debug_id = atomic_inc_return(&rxrpc_bundle_id);
	rb_link_node(&candidate->local_node, parent, pp);
	rb_insert_color(&candidate->local_node, &local->client_bundles);
	rxrpc_get_bundle(candidate);
	spin_unlock(&local->client_bundles_lock);
	_leave(" = %u [new]", candidate->debug_id);
	return candidate;

found_bundle_free:
	rxrpc_free_bundle(candidate);
found_bundle:
	rxrpc_get_bundle(bundle);
	atomic_inc(&bundle->active);
	spin_unlock(&local->client_bundles_lock);
	_leave(" = %u [found]", bundle->debug_id);
	return bundle;
}

/*
 * Create or find a client bundle to use for a call.
 *
 * If we return with a connection, the call will be on its waiting list.  It's
 * left to the caller to assign a channel and wake up the call.
 */
static struct rxrpc_bundle *rxrpc_prep_call(struct rxrpc_sock *rx,
					    struct rxrpc_call *call,
					    struct rxrpc_conn_parameters *cp,
					    struct sockaddr_rxrpc *srx,
					    gfp_t gfp)
{
	struct rxrpc_bundle *bundle;

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	cp->peer = rxrpc_lookup_peer(rx, cp->local, srx, gfp);
	if (!cp->peer)
		goto error;

	call->cong_cwnd = cp->peer->cong_cwnd;
	if (call->cong_cwnd >= call->cong_ssthresh)
		call->cong_mode = RXRPC_CALL_CONGEST_AVOIDANCE;
	else
		call->cong_mode = RXRPC_CALL_SLOW_START;
	if (cp->upgrade)
		__set_bit(RXRPC_CALL_UPGRADE, &call->flags);

	/* Find the client connection bundle. */
	bundle = rxrpc_look_up_bundle(cp, gfp);
	if (!bundle)
		goto error;

	/* Get this call queued.  Someone else may activate it whilst we're
	 * lining up a new connection, but that's fine.
	 */
	spin_lock(&bundle->channel_lock);
	list_add_tail(&call->chan_wait_link, &bundle->waiting_calls);
	spin_unlock(&bundle->channel_lock);

	_leave(" = [B=%x]", bundle->debug_id);
	return bundle;

error:
	_leave(" = -ENOMEM");
	return ERR_PTR(-ENOMEM);
}

/*
 * Allocate a new connection and add it into a bundle.
 */
static void rxrpc_add_conn_to_bundle(struct rxrpc_bundle *bundle, gfp_t gfp)
	__releases(bundle->channel_lock)
{
	struct rxrpc_connection *candidate = NULL, *old = NULL;
	bool conflict;
	int i;

	_enter("");

	conflict = bundle->alloc_conn;
	if (!conflict)
		bundle->alloc_conn = true;
	spin_unlock(&bundle->channel_lock);
	if (conflict) {
		_leave(" [conf]");
		return;
	}

	candidate = rxrpc_alloc_client_connection(bundle, gfp);

	spin_lock(&bundle->channel_lock);
	bundle->alloc_conn = false;

	if (IS_ERR(candidate)) {
		bundle->alloc_error = PTR_ERR(candidate);
		spin_unlock(&bundle->channel_lock);
		_leave(" [err %ld]", PTR_ERR(candidate));
		return;
	}

	bundle->alloc_error = 0;

	for (i = 0; i < ARRAY_SIZE(bundle->conns); i++) {
		unsigned int shift = i * RXRPC_MAXCALLS;
		int j;

		old = bundle->conns[i];
		if (!rxrpc_may_reuse_conn(old)) {
			if (old)
				trace_rxrpc_client(old, -1, rxrpc_client_replace);
			candidate->bundle_shift = shift;
			atomic_inc(&bundle->active);
			bundle->conns[i] = candidate;
			for (j = 0; j < RXRPC_MAXCALLS; j++)
				set_bit(shift + j, &bundle->avail_chans);
			candidate = NULL;
			break;
		}

		old = NULL;
	}

	spin_unlock(&bundle->channel_lock);

	if (candidate) {
		_debug("discard C=%x", candidate->debug_id);
		trace_rxrpc_client(candidate, -1, rxrpc_client_duplicate);
		rxrpc_put_connection(candidate);
	}

	rxrpc_put_connection(old);
	_leave("");
}

/*
 * Add a connection to a bundle if there are no usable connections or we have
 * connections waiting for extra capacity.
 */
static void rxrpc_maybe_add_conn(struct rxrpc_bundle *bundle, gfp_t gfp)
{
	struct rxrpc_call *call;
	int i, usable;

	_enter("");

	spin_lock(&bundle->channel_lock);

	/* See if there are any usable connections. */
	usable = 0;
	for (i = 0; i < ARRAY_SIZE(bundle->conns); i++)
		if (rxrpc_may_reuse_conn(bundle->conns[i]))
			usable++;

	if (!usable && !list_empty(&bundle->waiting_calls)) {
		call = list_first_entry(&bundle->waiting_calls,
					struct rxrpc_call, chan_wait_link);
		if (test_bit(RXRPC_CALL_UPGRADE, &call->flags))
			bundle->try_upgrade = true;
	}

	if (!usable)
		goto alloc_conn;

	if (!bundle->avail_chans &&
	    !bundle->try_upgrade &&
	    !list_empty(&bundle->waiting_calls) &&
	    usable < ARRAY_SIZE(bundle->conns))
		goto alloc_conn;

	spin_unlock(&bundle->channel_lock);
	_leave("");
	return;

alloc_conn:
	return rxrpc_add_conn_to_bundle(bundle, gfp);
}

/*
 * Assign a channel to the call at the front of the queue and wake the call up.
 * We don't increment the callNumber counter until this number has been exposed
 * to the world.
 */
static void rxrpc_activate_one_channel(struct rxrpc_connection *conn,
				       unsigned int channel)
{
	struct rxrpc_channel *chan = &conn->channels[channel];
	struct rxrpc_bundle *bundle = conn->bundle;
	struct rxrpc_call *call = list_entry(bundle->waiting_calls.next,
					     struct rxrpc_call, chan_wait_link);
	u32 call_id = chan->call_counter + 1;

	_enter("C=%x,%u", conn->debug_id, channel);

	trace_rxrpc_client(conn, channel, rxrpc_client_chan_activate);

	/* Cancel the final ACK on the previous call if it hasn't been sent yet
	 * as the DATA packet will implicitly ACK it.
	 */
	clear_bit(RXRPC_CONN_FINAL_ACK_0 + channel, &conn->flags);
	clear_bit(conn->bundle_shift + channel, &bundle->avail_chans);

	rxrpc_see_call(call);
	list_del_init(&call->chan_wait_link);
	call->peer	= rxrpc_get_peer(conn->params.peer);
	call->conn	= rxrpc_get_connection(conn);
	call->cid	= conn->proto.cid | channel;
	call->call_id	= call_id;
	call->security	= conn->security;
	call->security_ix = conn->security_ix;
	call->service_id = conn->service_id;

	trace_rxrpc_connect_call(call);
	_net("CONNECT call %08x:%08x as call %d on conn %d",
	     call->cid, call->call_id, call->debug_id, conn->debug_id);

	write_lock_bh(&call->state_lock);
	call->state = RXRPC_CALL_CLIENT_SEND_REQUEST;
	write_unlock_bh(&call->state_lock);

	/* Paired with the read barrier in rxrpc_connect_call().  This orders
	 * cid and epoch in the connection wrt to call_id without the need to
	 * take the channel_lock.
	 *
	 * We provisionally assign a callNumber at this point, but we don't
	 * confirm it until the call is about to be exposed.
	 *
	 * TODO: Pair with a barrier in the data_ready handler when that looks
	 * at the call ID through a connection channel.
	 */
	smp_wmb();

	chan->call_id		= call_id;
	chan->call_debug_id	= call->debug_id;
	rcu_assign_pointer(chan->call, call);
	wake_up(&call->waitq);
}

/*
 * Remove a connection from the idle list if it's on it.
 */
static void rxrpc_unidle_conn(struct rxrpc_bundle *bundle, struct rxrpc_connection *conn)
{
	struct rxrpc_net *rxnet = bundle->params.local->rxnet;
	bool drop_ref;

	if (!list_empty(&conn->cache_link)) {
		drop_ref = false;
		spin_lock(&rxnet->client_conn_cache_lock);
		if (!list_empty(&conn->cache_link)) {
			list_del_init(&conn->cache_link);
			drop_ref = true;
		}
		spin_unlock(&rxnet->client_conn_cache_lock);
		if (drop_ref)
			rxrpc_put_connection(conn);
	}
}

/*
 * Assign channels and callNumbers to waiting calls with channel_lock
 * held by caller.
 */
static void rxrpc_activate_channels_locked(struct rxrpc_bundle *bundle)
{
	struct rxrpc_connection *conn;
	unsigned long avail, mask;
	unsigned int channel, slot;

	if (bundle->try_upgrade)
		mask = 1;
	else
		mask = ULONG_MAX;

	while (!list_empty(&bundle->waiting_calls)) {
		avail = bundle->avail_chans & mask;
		if (!avail)
			break;
		channel = __ffs(avail);
		clear_bit(channel, &bundle->avail_chans);

		slot = channel / RXRPC_MAXCALLS;
		conn = bundle->conns[slot];
		if (!conn)
			break;

		if (bundle->try_upgrade)
			set_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags);
		rxrpc_unidle_conn(bundle, conn);

		channel &= (RXRPC_MAXCALLS - 1);
		conn->act_chans	|= 1 << channel;
		rxrpc_activate_one_channel(conn, channel);
	}
}

/*
 * Assign channels and callNumbers to waiting calls.
 */
static void rxrpc_activate_channels(struct rxrpc_bundle *bundle)
{
	_enter("B=%x", bundle->debug_id);

	trace_rxrpc_client(NULL, -1, rxrpc_client_activate_chans);

	if (!bundle->avail_chans)
		return;

	spin_lock(&bundle->channel_lock);
	rxrpc_activate_channels_locked(bundle);
	spin_unlock(&bundle->channel_lock);
	_leave("");
}

/*
 * Wait for a callNumber and a channel to be granted to a call.
 */
static int rxrpc_wait_for_channel(struct rxrpc_bundle *bundle,
				  struct rxrpc_call *call, gfp_t gfp)
{
	DECLARE_WAITQUEUE(myself, current);
	int ret = 0;

	_enter("%d", call->debug_id);

	if (!gfpflags_allow_blocking(gfp)) {
		rxrpc_maybe_add_conn(bundle, gfp);
		rxrpc_activate_channels(bundle);
		ret = bundle->alloc_error ?: -EAGAIN;
		goto out;
	}

	add_wait_queue_exclusive(&call->waitq, &myself);
	for (;;) {
		rxrpc_maybe_add_conn(bundle, gfp);
		rxrpc_activate_channels(bundle);
		ret = bundle->alloc_error;
		if (ret < 0)
			break;

		switch (call->interruptibility) {
		case RXRPC_INTERRUPTIBLE:
		case RXRPC_PREINTERRUPTIBLE:
			set_current_state(TASK_INTERRUPTIBLE);
			break;
		case RXRPC_UNINTERRUPTIBLE:
		default:
			set_current_state(TASK_UNINTERRUPTIBLE);
			break;
		}
		if (READ_ONCE(call->state) != RXRPC_CALL_CLIENT_AWAIT_CONN)
			break;
		if ((call->interruptibility == RXRPC_INTERRUPTIBLE ||
		     call->interruptibility == RXRPC_PREINTERRUPTIBLE) &&
		    signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	remove_wait_queue(&call->waitq, &myself);
	__set_current_state(TASK_RUNNING);

out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * find a connection for a call
 * - called in process context with IRQs enabled
 */
int rxrpc_connect_call(struct rxrpc_sock *rx,
		       struct rxrpc_call *call,
		       struct rxrpc_conn_parameters *cp,
		       struct sockaddr_rxrpc *srx,
		       gfp_t gfp)
{
	struct rxrpc_bundle *bundle;
	struct rxrpc_net *rxnet = cp->local->rxnet;
	int ret = 0;

	_enter("{%d,%lx},", call->debug_id, call->user_call_ID);

	rxrpc_discard_expired_client_conns(&rxnet->client_conn_reaper);

	bundle = rxrpc_prep_call(rx, call, cp, srx, gfp);
	if (IS_ERR(bundle)) {
		ret = PTR_ERR(bundle);
		goto out;
	}

	if (call->state == RXRPC_CALL_CLIENT_AWAIT_CONN) {
		ret = rxrpc_wait_for_channel(bundle, call, gfp);
		if (ret < 0)
			goto wait_failed;
	}

granted_channel:
	/* Paired with the write barrier in rxrpc_activate_one_channel(). */
	smp_rmb();

out_put_bundle:
	rxrpc_deactivate_bundle(bundle);
	rxrpc_put_bundle(bundle);
out:
	_leave(" = %d", ret);
	return ret;

wait_failed:
	spin_lock(&bundle->channel_lock);
	list_del_init(&call->chan_wait_link);
	spin_unlock(&bundle->channel_lock);

	if (call->state != RXRPC_CALL_CLIENT_AWAIT_CONN) {
		ret = 0;
		goto granted_channel;
	}

	trace_rxrpc_client(call->conn, ret, rxrpc_client_chan_wait_failed);
	rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR, 0, ret);
	rxrpc_disconnect_client_call(bundle, call);
	goto out_put_bundle;
}

/*
 * Note that a call, and thus a connection, is about to be exposed to the
 * world.
 */
void rxrpc_expose_client_call(struct rxrpc_call *call)
{
	unsigned int channel = call->cid & RXRPC_CHANNELMASK;
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_channel *chan = &conn->channels[channel];

	if (!test_and_set_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		/* Mark the call ID as being used.  If the callNumber counter
		 * exceeds ~2 billion, we kill the connection after its
		 * outstanding calls have finished so that the counter doesn't
		 * wrap.
		 */
		chan->call_counter++;
		if (chan->call_counter >= INT_MAX)
			set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);
		trace_rxrpc_client(conn, channel, rxrpc_client_exposed);
	}
}

/*
 * Set the reap timer.
 */
static void rxrpc_set_client_reap_timer(struct rxrpc_net *rxnet)
{
	if (!rxnet->kill_all_client_conns) {
		unsigned long now = jiffies;
		unsigned long reap_at = now + rxrpc_conn_idle_client_expiry;

		if (rxnet->live)
			timer_reduce(&rxnet->client_conn_reap_timer, reap_at);
	}
}

/*
 * Disconnect a client call.
 */
void rxrpc_disconnect_client_call(struct rxrpc_bundle *bundle, struct rxrpc_call *call)
{
	struct rxrpc_connection *conn;
	struct rxrpc_channel *chan = NULL;
	struct rxrpc_net *rxnet = bundle->params.local->rxnet;
	unsigned int channel;
	bool may_reuse;
	u32 cid;

	_enter("c=%x", call->debug_id);

	spin_lock(&bundle->channel_lock);
	set_bit(RXRPC_CALL_DISCONNECTED, &call->flags);

	/* Calls that have never actually been assigned a channel can simply be
	 * discarded.
	 */
	conn = call->conn;
	if (!conn) {
		_debug("call is waiting");
		ASSERTCMP(call->call_id, ==, 0);
		ASSERT(!test_bit(RXRPC_CALL_EXPOSED, &call->flags));
		list_del_init(&call->chan_wait_link);
		goto out;
	}

	cid = call->cid;
	channel = cid & RXRPC_CHANNELMASK;
	chan = &conn->channels[channel];
	trace_rxrpc_client(conn, channel, rxrpc_client_chan_disconnect);

	if (rcu_access_pointer(chan->call) != call) {
		spin_unlock(&bundle->channel_lock);
		BUG();
	}

	may_reuse = rxrpc_may_reuse_conn(conn);

	/* If a client call was exposed to the world, we save the result for
	 * retransmission.
	 *
	 * We use a barrier here so that the call number and abort code can be
	 * read without needing to take a lock.
	 *
	 * TODO: Make the incoming packet handler check this and handle
	 * terminal retransmission without requiring access to the call.
	 */
	if (test_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		_debug("exposed %u,%u", call->call_id, call->abort_code);
		__rxrpc_disconnect_call(conn, call);

		if (test_and_clear_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags)) {
			trace_rxrpc_client(conn, channel, rxrpc_client_to_active);
			bundle->try_upgrade = false;
			if (may_reuse)
				rxrpc_activate_channels_locked(bundle);
		}

	}

	/* See if we can pass the channel directly to another call. */
	if (may_reuse && !list_empty(&bundle->waiting_calls)) {
		trace_rxrpc_client(conn, channel, rxrpc_client_chan_pass);
		rxrpc_activate_one_channel(conn, channel);
		goto out;
	}

	/* Schedule the final ACK to be transmitted in a short while so that it
	 * can be skipped if we find a follow-on call.  The first DATA packet
	 * of the follow on call will implicitly ACK this call.
	 */
	if (call->completion == RXRPC_CALL_SUCCEEDED &&
	    test_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		unsigned long final_ack_at = jiffies + 2;

		WRITE_ONCE(chan->final_ack_at, final_ack_at);
		smp_wmb(); /* vs rxrpc_process_delayed_final_acks() */
		set_bit(RXRPC_CONN_FINAL_ACK_0 + channel, &conn->flags);
		rxrpc_reduce_conn_timer(conn, final_ack_at);
	}

	/* Deactivate the channel. */
	rcu_assign_pointer(chan->call, NULL);
	set_bit(conn->bundle_shift + channel, &conn->bundle->avail_chans);
	conn->act_chans	&= ~(1 << channel);

	/* If no channels remain active, then put the connection on the idle
	 * list for a short while.  Give it a ref to stop it going away if it
	 * becomes unbundled.
	 */
	if (!conn->act_chans) {
		trace_rxrpc_client(conn, channel, rxrpc_client_to_idle);
		conn->idle_timestamp = jiffies;

		rxrpc_get_connection(conn);
		spin_lock(&rxnet->client_conn_cache_lock);
		list_move_tail(&conn->cache_link, &rxnet->idle_client_conns);
		spin_unlock(&rxnet->client_conn_cache_lock);

		rxrpc_set_client_reap_timer(rxnet);
	}

out:
	spin_unlock(&bundle->channel_lock);
	_leave("");
	return;
}

/*
 * Remove a connection from a bundle.
 */
static void rxrpc_unbundle_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_bundle *bundle = conn->bundle;
	unsigned int bindex;
	bool need_drop = false;
	int i;

	_enter("C=%x", conn->debug_id);

	if (conn->flags & RXRPC_CONN_FINAL_ACK_MASK)
		rxrpc_process_delayed_final_acks(conn, true);

	spin_lock(&bundle->channel_lock);
	bindex = conn->bundle_shift / RXRPC_MAXCALLS;
	if (bundle->conns[bindex] == conn) {
		_debug("clear slot %u", bindex);
		bundle->conns[bindex] = NULL;
		for (i = 0; i < RXRPC_MAXCALLS; i++)
			clear_bit(conn->bundle_shift + i, &bundle->avail_chans);
		need_drop = true;
	}
	spin_unlock(&bundle->channel_lock);

	if (need_drop) {
		rxrpc_deactivate_bundle(bundle);
		rxrpc_put_connection(conn);
	}
}

/*
 * Drop the active count on a bundle.
 */
static void rxrpc_deactivate_bundle(struct rxrpc_bundle *bundle)
{
	struct rxrpc_local *local = bundle->params.local;
	bool need_put = false;

	if (atomic_dec_and_lock(&bundle->active, &local->client_bundles_lock)) {
		if (!bundle->params.exclusive) {
			_debug("erase bundle");
			rb_erase(&bundle->local_node, &local->client_bundles);
			need_put = true;
		}

		spin_unlock(&local->client_bundles_lock);
		if (need_put)
			rxrpc_put_bundle(bundle);
	}
}

/*
 * Clean up a dead client connection.
 */
static void rxrpc_kill_client_conn(struct rxrpc_connection *conn)
{
	struct rxrpc_local *local = conn->params.local;
	struct rxrpc_net *rxnet = local->rxnet;

	_enter("C=%x", conn->debug_id);

	trace_rxrpc_client(conn, -1, rxrpc_client_cleanup);
	atomic_dec(&rxnet->nr_client_conns);

	rxrpc_put_client_connection_id(conn);
	rxrpc_kill_connection(conn);
}

/*
 * Clean up a dead client connections.
 */
void rxrpc_put_client_conn(struct rxrpc_connection *conn)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id = conn->debug_id;
	bool dead;
	int r;

	dead = __refcount_dec_and_test(&conn->ref, &r);
	trace_rxrpc_conn(debug_id, rxrpc_conn_put_client, r - 1, here);
	if (dead)
		rxrpc_kill_client_conn(conn);
}

/*
 * Discard expired client connections from the idle list.  Each conn in the
 * idle list has been exposed and holds an extra ref because of that.
 *
 * This may be called from conn setup or from a work item so cannot be
 * considered non-reentrant.
 */
void rxrpc_discard_expired_client_conns(struct work_struct *work)
{
	struct rxrpc_connection *conn;
	struct rxrpc_net *rxnet =
		container_of(work, struct rxrpc_net, client_conn_reaper);
	unsigned long expiry, conn_expires_at, now;
	unsigned int nr_conns;

	_enter("");

	if (list_empty(&rxnet->idle_client_conns)) {
		_leave(" [empty]");
		return;
	}

	/* Don't double up on the discarding */
	if (!spin_trylock(&rxnet->client_conn_discard_lock)) {
		_leave(" [already]");
		return;
	}

	/* We keep an estimate of what the number of conns ought to be after
	 * we've discarded some so that we don't overdo the discarding.
	 */
	nr_conns = atomic_read(&rxnet->nr_client_conns);

next:
	spin_lock(&rxnet->client_conn_cache_lock);

	if (list_empty(&rxnet->idle_client_conns))
		goto out;

	conn = list_entry(rxnet->idle_client_conns.next,
			  struct rxrpc_connection, cache_link);

	if (!rxnet->kill_all_client_conns) {
		/* If the number of connections is over the reap limit, we
		 * expedite discard by reducing the expiry timeout.  We must,
		 * however, have at least a short grace period to be able to do
		 * final-ACK or ABORT retransmission.
		 */
		expiry = rxrpc_conn_idle_client_expiry;
		if (nr_conns > rxrpc_reap_client_connections)
			expiry = rxrpc_conn_idle_client_fast_expiry;
		if (conn->params.local->service_closed)
			expiry = rxrpc_closed_conn_expiry * HZ;

		conn_expires_at = conn->idle_timestamp + expiry;

		now = READ_ONCE(jiffies);
		if (time_after(conn_expires_at, now))
			goto not_yet_expired;
	}

	trace_rxrpc_client(conn, -1, rxrpc_client_discard);
	list_del_init(&conn->cache_link);

	spin_unlock(&rxnet->client_conn_cache_lock);

	rxrpc_unbundle_conn(conn);
	rxrpc_put_connection(conn); /* Drop the ->cache_link ref */

	nr_conns--;
	goto next;

not_yet_expired:
	/* The connection at the front of the queue hasn't yet expired, so
	 * schedule the work item for that point if we discarded something.
	 *
	 * We don't worry if the work item is already scheduled - it can look
	 * after rescheduling itself at a later time.  We could cancel it, but
	 * then things get messier.
	 */
	_debug("not yet");
	if (!rxnet->kill_all_client_conns)
		timer_reduce(&rxnet->client_conn_reap_timer, conn_expires_at);

out:
	spin_unlock(&rxnet->client_conn_cache_lock);
	spin_unlock(&rxnet->client_conn_discard_lock);
	_leave("");
}

/*
 * Preemptively destroy all the client connection records rather than waiting
 * for them to time out
 */
void rxrpc_destroy_all_client_connections(struct rxrpc_net *rxnet)
{
	_enter("");

	spin_lock(&rxnet->client_conn_cache_lock);
	rxnet->kill_all_client_conns = true;
	spin_unlock(&rxnet->client_conn_cache_lock);

	del_timer_sync(&rxnet->client_conn_reap_timer);

	if (!rxrpc_queue_work(&rxnet->client_conn_reaper))
		_debug("destroy: queue failed");

	_leave("");
}

/*
 * Clean up the client connections on a local endpoint.
 */
void rxrpc_clean_up_local_conns(struct rxrpc_local *local)
{
	struct rxrpc_connection *conn, *tmp;
	struct rxrpc_net *rxnet = local->rxnet;
	LIST_HEAD(graveyard);

	_enter("");

	spin_lock(&rxnet->client_conn_cache_lock);

	list_for_each_entry_safe(conn, tmp, &rxnet->idle_client_conns,
				 cache_link) {
		if (conn->params.local == local) {
			trace_rxrpc_client(conn, -1, rxrpc_client_discard);
			list_move(&conn->cache_link, &graveyard);
		}
	}

	spin_unlock(&rxnet->client_conn_cache_lock);

	while (!list_empty(&graveyard)) {
		conn = list_entry(graveyard.next,
				  struct rxrpc_connection, cache_link);
		list_del_init(&conn->cache_link);
		rxrpc_unbundle_conn(conn);
		rxrpc_put_connection(conn);
	}

	_leave(" [culled]");
}
