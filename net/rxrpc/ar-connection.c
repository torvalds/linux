/* RxRPC virtual connection handler
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
#include <linux/crypto.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static void rxrpc_connection_reaper(struct work_struct *work);

LIST_HEAD(rxrpc_connections);
DEFINE_RWLOCK(rxrpc_connection_lock);
static unsigned long rxrpc_connection_timeout = 10 * 60;
static DECLARE_DELAYED_WORK(rxrpc_connection_reap, rxrpc_connection_reaper);

/*
 * allocate a new client connection bundle
 */
static struct rxrpc_conn_bundle *rxrpc_alloc_bundle(gfp_t gfp)
{
	struct rxrpc_conn_bundle *bundle;

	_enter("");

	bundle = kzalloc(sizeof(struct rxrpc_conn_bundle), gfp);
	if (bundle) {
		INIT_LIST_HEAD(&bundle->unused_conns);
		INIT_LIST_HEAD(&bundle->avail_conns);
		INIT_LIST_HEAD(&bundle->busy_conns);
		init_waitqueue_head(&bundle->chanwait);
		atomic_set(&bundle->usage, 1);
	}

	_leave(" = %p", bundle);
	return bundle;
}

/*
 * compare bundle parameters with what we're looking for
 * - return -ve, 0 or +ve
 */
static inline
int rxrpc_cmp_bundle(const struct rxrpc_conn_bundle *bundle,
		     struct key *key, __be16 service_id)
{
	return (bundle->service_id - service_id) ?:
		((unsigned long) bundle->key - (unsigned long) key);
}

/*
 * get bundle of client connections that a client socket can make use of
 */
struct rxrpc_conn_bundle *rxrpc_get_bundle(struct rxrpc_sock *rx,
					   struct rxrpc_transport *trans,
					   struct key *key,
					   __be16 service_id,
					   gfp_t gfp)
{
	struct rxrpc_conn_bundle *bundle, *candidate;
	struct rb_node *p, *parent, **pp;

	_enter("%p{%x},%x,%hx,",
	       rx, key_serial(key), trans->debug_id, ntohl(service_id));

	if (rx->trans == trans && rx->bundle) {
		atomic_inc(&rx->bundle->usage);
		return rx->bundle;
	}

	/* search the extant bundles first for one that matches the specified
	 * user ID */
	spin_lock(&trans->client_lock);

	p = trans->bundles.rb_node;
	while (p) {
		bundle = rb_entry(p, struct rxrpc_conn_bundle, node);

		if (rxrpc_cmp_bundle(bundle, key, service_id) < 0)
			p = p->rb_left;
		else if (rxrpc_cmp_bundle(bundle, key, service_id) > 0)
			p = p->rb_right;
		else
			goto found_extant_bundle;
	}

	spin_unlock(&trans->client_lock);

	/* not yet present - create a candidate for a new record and then
	 * redo the search */
	candidate = rxrpc_alloc_bundle(gfp);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	candidate->key = key_get(key);
	candidate->service_id = service_id;

	spin_lock(&trans->client_lock);

	pp = &trans->bundles.rb_node;
	parent = NULL;
	while (*pp) {
		parent = *pp;
		bundle = rb_entry(parent, struct rxrpc_conn_bundle, node);

		if (rxrpc_cmp_bundle(bundle, key, service_id) < 0)
			pp = &(*pp)->rb_left;
		else if (rxrpc_cmp_bundle(bundle, key, service_id) > 0)
			pp = &(*pp)->rb_right;
		else
			goto found_extant_second;
	}

	/* second search also failed; add the new bundle */
	bundle = candidate;
	candidate = NULL;

	rb_link_node(&bundle->node, parent, pp);
	rb_insert_color(&bundle->node, &trans->bundles);
	spin_unlock(&trans->client_lock);
	_net("BUNDLE new on trans %d", trans->debug_id);
	if (!rx->bundle && rx->sk.sk_state == RXRPC_CLIENT_CONNECTED) {
		atomic_inc(&bundle->usage);
		rx->bundle = bundle;
	}
	_leave(" = %p [new]", bundle);
	return bundle;

	/* we found the bundle in the list immediately */
found_extant_bundle:
	atomic_inc(&bundle->usage);
	spin_unlock(&trans->client_lock);
	_net("BUNDLE old on trans %d", trans->debug_id);
	if (!rx->bundle && rx->sk.sk_state == RXRPC_CLIENT_CONNECTED) {
		atomic_inc(&bundle->usage);
		rx->bundle = bundle;
	}
	_leave(" = %p [extant %d]", bundle, atomic_read(&bundle->usage));
	return bundle;

	/* we found the bundle on the second time through the list */
found_extant_second:
	atomic_inc(&bundle->usage);
	spin_unlock(&trans->client_lock);
	kfree(candidate);
	_net("BUNDLE old2 on trans %d", trans->debug_id);
	if (!rx->bundle && rx->sk.sk_state == RXRPC_CLIENT_CONNECTED) {
		atomic_inc(&bundle->usage);
		rx->bundle = bundle;
	}
	_leave(" = %p [second %d]", bundle, atomic_read(&bundle->usage));
	return bundle;
}

/*
 * release a bundle
 */
void rxrpc_put_bundle(struct rxrpc_transport *trans,
		      struct rxrpc_conn_bundle *bundle)
{
	_enter("%p,%p{%d}",trans, bundle, atomic_read(&bundle->usage));

	if (atomic_dec_and_lock(&bundle->usage, &trans->client_lock)) {
		_debug("Destroy bundle");
		rb_erase(&bundle->node, &trans->bundles);
		spin_unlock(&trans->client_lock);
		ASSERT(list_empty(&bundle->unused_conns));
		ASSERT(list_empty(&bundle->avail_conns));
		ASSERT(list_empty(&bundle->busy_conns));
		ASSERTCMP(bundle->num_conns, ==, 0);
		key_put(bundle->key);
		kfree(bundle);
	}

	_leave("");
}

/*
 * allocate a new connection
 */
static struct rxrpc_connection *rxrpc_alloc_connection(gfp_t gfp)
{
	struct rxrpc_connection *conn;

	_enter("");

	conn = kzalloc(sizeof(struct rxrpc_connection), gfp);
	if (conn) {
		INIT_WORK(&conn->processor, &rxrpc_process_connection);
		INIT_LIST_HEAD(&conn->bundle_link);
		conn->calls = RB_ROOT;
		skb_queue_head_init(&conn->rx_queue);
		rwlock_init(&conn->lock);
		spin_lock_init(&conn->state_lock);
		atomic_set(&conn->usage, 1);
		conn->debug_id = atomic_inc_return(&rxrpc_debug_id);
		conn->avail_calls = RXRPC_MAXCALLS;
		conn->size_align = 4;
		conn->header_size = sizeof(struct rxrpc_header);
	}

	_leave(" = %p{%d}", conn, conn->debug_id);
	return conn;
}

/*
 * assign a connection ID to a connection and add it to the transport's
 * connection lookup tree
 * - called with transport client lock held
 */
static void rxrpc_assign_connection_id(struct rxrpc_connection *conn)
{
	struct rxrpc_connection *xconn;
	struct rb_node *parent, **p;
	__be32 epoch;
	u32 real_conn_id;

	_enter("");

	epoch = conn->epoch;

	write_lock_bh(&conn->trans->conn_lock);

	conn->trans->conn_idcounter += RXRPC_CID_INC;
	if (conn->trans->conn_idcounter < RXRPC_CID_INC)
		conn->trans->conn_idcounter = RXRPC_CID_INC;
	real_conn_id = conn->trans->conn_idcounter;

attempt_insertion:
	parent = NULL;
	p = &conn->trans->client_conns.rb_node;

	while (*p) {
		parent = *p;
		xconn = rb_entry(parent, struct rxrpc_connection, node);

		if (epoch < xconn->epoch)
			p = &(*p)->rb_left;
		else if (epoch > xconn->epoch)
			p = &(*p)->rb_right;
		else if (real_conn_id < xconn->real_conn_id)
			p = &(*p)->rb_left;
		else if (real_conn_id > xconn->real_conn_id)
			p = &(*p)->rb_right;
		else
			goto id_exists;
	}

	/* we've found a suitable hole - arrange for this connection to occupy
	 * it */
	rb_link_node(&conn->node, parent, p);
	rb_insert_color(&conn->node, &conn->trans->client_conns);

	conn->real_conn_id = real_conn_id;
	conn->cid = htonl(real_conn_id);
	write_unlock_bh(&conn->trans->conn_lock);
	_leave(" [CONNID %x CID %x]", real_conn_id, ntohl(conn->cid));
	return;

	/* we found a connection with the proposed ID - walk the tree from that
	 * point looking for the next unused ID */
id_exists:
	for (;;) {
		real_conn_id += RXRPC_CID_INC;
		if (real_conn_id < RXRPC_CID_INC) {
			real_conn_id = RXRPC_CID_INC;
			conn->trans->conn_idcounter = real_conn_id;
			goto attempt_insertion;
		}

		parent = rb_next(parent);
		if (!parent)
			goto attempt_insertion;

		xconn = rb_entry(parent, struct rxrpc_connection, node);
		if (epoch < xconn->epoch ||
		    real_conn_id < xconn->real_conn_id)
			goto attempt_insertion;
	}
}

/*
 * add a call to a connection's call-by-ID tree
 */
static void rxrpc_add_call_ID_to_conn(struct rxrpc_connection *conn,
				      struct rxrpc_call *call)
{
	struct rxrpc_call *xcall;
	struct rb_node *parent, **p;
	__be32 call_id;

	write_lock_bh(&conn->lock);

	call_id = call->call_id;
	p = &conn->calls.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		xcall = rb_entry(parent, struct rxrpc_call, conn_node);

		if (call_id < xcall->call_id)
			p = &(*p)->rb_left;
		else if (call_id > xcall->call_id)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&call->conn_node, parent, p);
	rb_insert_color(&call->conn_node, &conn->calls);

	write_unlock_bh(&conn->lock);
}

/*
 * connect a call on an exclusive connection
 */
static int rxrpc_connect_exclusive(struct rxrpc_sock *rx,
				   struct rxrpc_transport *trans,
				   __be16 service_id,
				   struct rxrpc_call *call,
				   gfp_t gfp)
{
	struct rxrpc_connection *conn;
	int chan, ret;

	_enter("");

	conn = rx->conn;
	if (!conn) {
		/* not yet present - create a candidate for a new connection
		 * and then redo the check */
		conn = rxrpc_alloc_connection(gfp);
		if (IS_ERR(conn)) {
			_leave(" = %ld", PTR_ERR(conn));
			return PTR_ERR(conn);
		}

		conn->trans = trans;
		conn->bundle = NULL;
		conn->service_id = service_id;
		conn->epoch = rxrpc_epoch;
		conn->in_clientflag = 0;
		conn->out_clientflag = RXRPC_CLIENT_INITIATED;
		conn->cid = 0;
		conn->state = RXRPC_CONN_CLIENT;
		conn->avail_calls = RXRPC_MAXCALLS - 1;
		conn->security_level = rx->min_sec_level;
		conn->key = key_get(rx->key);

		ret = rxrpc_init_client_conn_security(conn);
		if (ret < 0) {
			key_put(conn->key);
			kfree(conn);
			_leave(" = %d [key]", ret);
			return ret;
		}

		write_lock_bh(&rxrpc_connection_lock);
		list_add_tail(&conn->link, &rxrpc_connections);
		write_unlock_bh(&rxrpc_connection_lock);

		spin_lock(&trans->client_lock);
		atomic_inc(&trans->usage);

		_net("CONNECT EXCL new %d on TRANS %d",
		     conn->debug_id, conn->trans->debug_id);

		rxrpc_assign_connection_id(conn);
		rx->conn = conn;
	}

	/* we've got a connection with a free channel and we can now attach the
	 * call to it
	 * - we're holding the transport's client lock
	 * - we're holding a reference on the connection
	 */
	for (chan = 0; chan < RXRPC_MAXCALLS; chan++)
		if (!conn->channels[chan])
			goto found_channel;
	goto no_free_channels;

found_channel:
	atomic_inc(&conn->usage);
	conn->channels[chan] = call;
	call->conn = conn;
	call->channel = chan;
	call->cid = conn->cid | htonl(chan);
	call->call_id = htonl(++conn->call_counter);

	_net("CONNECT client on conn %d chan %d as call %x",
	     conn->debug_id, chan, ntohl(call->call_id));

	spin_unlock(&trans->client_lock);

	rxrpc_add_call_ID_to_conn(conn, call);
	_leave(" = 0");
	return 0;

no_free_channels:
	spin_unlock(&trans->client_lock);
	_leave(" = -ENOSR");
	return -ENOSR;
}

/*
 * find a connection for a call
 * - called in process context with IRQs enabled
 */
int rxrpc_connect_call(struct rxrpc_sock *rx,
		       struct rxrpc_transport *trans,
		       struct rxrpc_conn_bundle *bundle,
		       struct rxrpc_call *call,
		       gfp_t gfp)
{
	struct rxrpc_connection *conn, *candidate;
	int chan, ret;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%p,%lx,", rx, call->user_call_ID);

	if (test_bit(RXRPC_SOCK_EXCLUSIVE_CONN, &rx->flags))
		return rxrpc_connect_exclusive(rx, trans, bundle->service_id,
					       call, gfp);

	spin_lock(&trans->client_lock);
	for (;;) {
		/* see if the bundle has a call slot available */
		if (!list_empty(&bundle->avail_conns)) {
			_debug("avail");
			conn = list_entry(bundle->avail_conns.next,
					  struct rxrpc_connection,
					  bundle_link);
			if (--conn->avail_calls == 0)
				list_move(&conn->bundle_link,
					  &bundle->busy_conns);
			ASSERTCMP(conn->avail_calls, <, RXRPC_MAXCALLS);
			ASSERT(conn->channels[0] == NULL ||
			       conn->channels[1] == NULL ||
			       conn->channels[2] == NULL ||
			       conn->channels[3] == NULL);
			atomic_inc(&conn->usage);
			break;
		}

		if (!list_empty(&bundle->unused_conns)) {
			_debug("unused");
			conn = list_entry(bundle->unused_conns.next,
					  struct rxrpc_connection,
					  bundle_link);
			ASSERTCMP(conn->avail_calls, ==, RXRPC_MAXCALLS);
			conn->avail_calls = RXRPC_MAXCALLS - 1;
			ASSERT(conn->channels[0] == NULL &&
			       conn->channels[1] == NULL &&
			       conn->channels[2] == NULL &&
			       conn->channels[3] == NULL);
			atomic_inc(&conn->usage);
			list_move(&conn->bundle_link, &bundle->avail_conns);
			break;
		}

		/* need to allocate a new connection */
		_debug("get new conn [%d]", bundle->num_conns);

		spin_unlock(&trans->client_lock);

		if (signal_pending(current))
			goto interrupted;

		if (bundle->num_conns >= 20) {
			_debug("too many conns");

			if (!(gfp & __GFP_WAIT)) {
				_leave(" = -EAGAIN");
				return -EAGAIN;
			}

			add_wait_queue(&bundle->chanwait, &myself);
			for (;;) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (bundle->num_conns < 20 ||
				    !list_empty(&bundle->unused_conns) ||
				    !list_empty(&bundle->avail_conns))
					break;
				if (signal_pending(current))
					goto interrupted_dequeue;
				schedule();
			}
			remove_wait_queue(&bundle->chanwait, &myself);
			__set_current_state(TASK_RUNNING);
			spin_lock(&trans->client_lock);
			continue;
		}

		/* not yet present - create a candidate for a new connection and then
		 * redo the check */
		candidate = rxrpc_alloc_connection(gfp);
		if (IS_ERR(candidate)) {
			_leave(" = %ld", PTR_ERR(candidate));
			return PTR_ERR(candidate);
		}

		candidate->trans = trans;
		candidate->bundle = bundle;
		candidate->service_id = bundle->service_id;
		candidate->epoch = rxrpc_epoch;
		candidate->in_clientflag = 0;
		candidate->out_clientflag = RXRPC_CLIENT_INITIATED;
		candidate->cid = 0;
		candidate->state = RXRPC_CONN_CLIENT;
		candidate->avail_calls = RXRPC_MAXCALLS;
		candidate->security_level = rx->min_sec_level;
		candidate->key = key_get(bundle->key);

		ret = rxrpc_init_client_conn_security(candidate);
		if (ret < 0) {
			key_put(candidate->key);
			kfree(candidate);
			_leave(" = %d [key]", ret);
			return ret;
		}

		write_lock_bh(&rxrpc_connection_lock);
		list_add_tail(&candidate->link, &rxrpc_connections);
		write_unlock_bh(&rxrpc_connection_lock);

		spin_lock(&trans->client_lock);

		list_add(&candidate->bundle_link, &bundle->unused_conns);
		bundle->num_conns++;
		atomic_inc(&bundle->usage);
		atomic_inc(&trans->usage);

		_net("CONNECT new %d on TRANS %d",
		     candidate->debug_id, candidate->trans->debug_id);

		rxrpc_assign_connection_id(candidate);
		if (candidate->security)
			candidate->security->prime_packet_security(candidate);

		/* leave the candidate lurking in zombie mode attached to the
		 * bundle until we're ready for it */
		rxrpc_put_connection(candidate);
		candidate = NULL;
	}

	/* we've got a connection with a free channel and we can now attach the
	 * call to it
	 * - we're holding the transport's client lock
	 * - we're holding a reference on the connection
	 * - we're holding a reference on the bundle
	 */
	for (chan = 0; chan < RXRPC_MAXCALLS; chan++)
		if (!conn->channels[chan])
			goto found_channel;
	ASSERT(conn->channels[0] == NULL ||
	       conn->channels[1] == NULL ||
	       conn->channels[2] == NULL ||
	       conn->channels[3] == NULL);
	BUG();

found_channel:
	conn->channels[chan] = call;
	call->conn = conn;
	call->channel = chan;
	call->cid = conn->cid | htonl(chan);
	call->call_id = htonl(++conn->call_counter);

	_net("CONNECT client on conn %d chan %d as call %x",
	     conn->debug_id, chan, ntohl(call->call_id));

	ASSERTCMP(conn->avail_calls, <, RXRPC_MAXCALLS);
	spin_unlock(&trans->client_lock);

	rxrpc_add_call_ID_to_conn(conn, call);

	_leave(" = 0");
	return 0;

interrupted_dequeue:
	remove_wait_queue(&bundle->chanwait, &myself);
	__set_current_state(TASK_RUNNING);
interrupted:
	_leave(" = -ERESTARTSYS");
	return -ERESTARTSYS;
}

/*
 * get a record of an incoming connection
 */
struct rxrpc_connection *
rxrpc_incoming_connection(struct rxrpc_transport *trans,
			  struct rxrpc_header *hdr,
			  gfp_t gfp)
{
	struct rxrpc_connection *conn, *candidate = NULL;
	struct rb_node *p, **pp;
	const char *new = "old";
	__be32 epoch;
	u32 conn_id;

	_enter("");

	ASSERT(hdr->flags & RXRPC_CLIENT_INITIATED);

	epoch = hdr->epoch;
	conn_id = ntohl(hdr->cid) & RXRPC_CIDMASK;

	/* search the connection list first */
	read_lock_bh(&trans->conn_lock);

	p = trans->server_conns.rb_node;
	while (p) {
		conn = rb_entry(p, struct rxrpc_connection, node);

		_debug("maybe %x", conn->real_conn_id);

		if (epoch < conn->epoch)
			p = p->rb_left;
		else if (epoch > conn->epoch)
			p = p->rb_right;
		else if (conn_id < conn->real_conn_id)
			p = p->rb_left;
		else if (conn_id > conn->real_conn_id)
			p = p->rb_right;
		else
			goto found_extant_connection;
	}
	read_unlock_bh(&trans->conn_lock);

	/* not yet present - create a candidate for a new record and then
	 * redo the search */
	candidate = rxrpc_alloc_connection(gfp);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	candidate->trans = trans;
	candidate->epoch = hdr->epoch;
	candidate->cid = hdr->cid & __constant_cpu_to_be32(RXRPC_CIDMASK);
	candidate->service_id = hdr->serviceId;
	candidate->security_ix = hdr->securityIndex;
	candidate->in_clientflag = RXRPC_CLIENT_INITIATED;
	candidate->out_clientflag = 0;
	candidate->real_conn_id = conn_id;
	candidate->state = RXRPC_CONN_SERVER;
	if (candidate->service_id)
		candidate->state = RXRPC_CONN_SERVER_UNSECURED;

	write_lock_bh(&trans->conn_lock);

	pp = &trans->server_conns.rb_node;
	p = NULL;
	while (*pp) {
		p = *pp;
		conn = rb_entry(p, struct rxrpc_connection, node);

		if (epoch < conn->epoch)
			pp = &(*pp)->rb_left;
		else if (epoch > conn->epoch)
			pp = &(*pp)->rb_right;
		else if (conn_id < conn->real_conn_id)
			pp = &(*pp)->rb_left;
		else if (conn_id > conn->real_conn_id)
			pp = &(*pp)->rb_right;
		else
			goto found_extant_second;
	}

	/* we can now add the new candidate to the list */
	conn = candidate;
	candidate = NULL;
	rb_link_node(&conn->node, p, pp);
	rb_insert_color(&conn->node, &trans->server_conns);
	atomic_inc(&conn->trans->usage);

	write_unlock_bh(&trans->conn_lock);

	write_lock_bh(&rxrpc_connection_lock);
	list_add_tail(&conn->link, &rxrpc_connections);
	write_unlock_bh(&rxrpc_connection_lock);

	new = "new";

success:
	_net("CONNECTION %s %d {%x}", new, conn->debug_id, conn->real_conn_id);

	_leave(" = %p {u=%d}", conn, atomic_read(&conn->usage));
	return conn;

	/* we found the connection in the list immediately */
found_extant_connection:
	if (hdr->securityIndex != conn->security_ix) {
		read_unlock_bh(&trans->conn_lock);
		goto security_mismatch;
	}
	atomic_inc(&conn->usage);
	read_unlock_bh(&trans->conn_lock);
	goto success;

	/* we found the connection on the second time through the list */
found_extant_second:
	if (hdr->securityIndex != conn->security_ix) {
		write_unlock_bh(&trans->conn_lock);
		goto security_mismatch;
	}
	atomic_inc(&conn->usage);
	write_unlock_bh(&trans->conn_lock);
	kfree(candidate);
	goto success;

security_mismatch:
	kfree(candidate);
	_leave(" = -EKEYREJECTED");
	return ERR_PTR(-EKEYREJECTED);
}

/*
 * find a connection based on transport and RxRPC connection ID for an incoming
 * packet
 */
struct rxrpc_connection *rxrpc_find_connection(struct rxrpc_transport *trans,
					       struct rxrpc_header *hdr)
{
	struct rxrpc_connection *conn;
	struct rb_node *p;
	__be32 epoch;
	u32 conn_id;

	_enter(",{%x,%x}", ntohl(hdr->cid), hdr->flags);

	read_lock_bh(&trans->conn_lock);

	conn_id = ntohl(hdr->cid) & RXRPC_CIDMASK;
	epoch = hdr->epoch;

	if (hdr->flags & RXRPC_CLIENT_INITIATED)
		p = trans->server_conns.rb_node;
	else
		p = trans->client_conns.rb_node;

	while (p) {
		conn = rb_entry(p, struct rxrpc_connection, node);

		_debug("maybe %x", conn->real_conn_id);

		if (epoch < conn->epoch)
			p = p->rb_left;
		else if (epoch > conn->epoch)
			p = p->rb_right;
		else if (conn_id < conn->real_conn_id)
			p = p->rb_left;
		else if (conn_id > conn->real_conn_id)
			p = p->rb_right;
		else
			goto found;
	}

	read_unlock_bh(&trans->conn_lock);
	_leave(" = NULL");
	return NULL;

found:
	atomic_inc(&conn->usage);
	read_unlock_bh(&trans->conn_lock);
	_leave(" = %p", conn);
	return conn;
}

/*
 * release a virtual connection
 */
void rxrpc_put_connection(struct rxrpc_connection *conn)
{
	_enter("%p{u=%d,d=%d}",
	       conn, atomic_read(&conn->usage), conn->debug_id);

	ASSERTCMP(atomic_read(&conn->usage), >, 0);

	conn->put_time = xtime.tv_sec;
	if (atomic_dec_and_test(&conn->usage)) {
		_debug("zombie");
		rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);
	}

	_leave("");
}

/*
 * destroy a virtual connection
 */
static void rxrpc_destroy_connection(struct rxrpc_connection *conn)
{
	_enter("%p{%d}", conn, atomic_read(&conn->usage));

	ASSERTCMP(atomic_read(&conn->usage), ==, 0);

	_net("DESTROY CONN %d", conn->debug_id);

	if (conn->bundle)
		rxrpc_put_bundle(conn->trans, conn->bundle);

	ASSERT(RB_EMPTY_ROOT(&conn->calls));
	rxrpc_purge_queue(&conn->rx_queue);

	rxrpc_clear_conn_security(conn);
	rxrpc_put_transport(conn->trans);
	kfree(conn);
	_leave("");
}

/*
 * reap dead connections
 */
void rxrpc_connection_reaper(struct work_struct *work)
{
	struct rxrpc_connection *conn, *_p;
	unsigned long now, earliest, reap_time;

	LIST_HEAD(graveyard);

	_enter("");

	now = xtime.tv_sec;
	earliest = ULONG_MAX;

	write_lock_bh(&rxrpc_connection_lock);
	list_for_each_entry_safe(conn, _p, &rxrpc_connections, link) {
		_debug("reap CONN %d { u=%d,t=%ld }",
		       conn->debug_id, atomic_read(&conn->usage),
		       (long) now - (long) conn->put_time);

		if (likely(atomic_read(&conn->usage) > 0))
			continue;

		spin_lock(&conn->trans->client_lock);
		write_lock(&conn->trans->conn_lock);
		reap_time = conn->put_time + rxrpc_connection_timeout;

		if (atomic_read(&conn->usage) > 0) {
			;
		} else if (reap_time <= now) {
			list_move_tail(&conn->link, &graveyard);
			if (conn->out_clientflag)
				rb_erase(&conn->node,
					 &conn->trans->client_conns);
			else
				rb_erase(&conn->node,
					 &conn->trans->server_conns);
			if (conn->bundle) {
				list_del_init(&conn->bundle_link);
				conn->bundle->num_conns--;
			}

		} else if (reap_time < earliest) {
			earliest = reap_time;
		}

		write_unlock(&conn->trans->conn_lock);
		spin_unlock(&conn->trans->client_lock);
	}
	write_unlock_bh(&rxrpc_connection_lock);

	if (earliest != ULONG_MAX) {
		_debug("reschedule reaper %ld", (long) earliest - now);
		ASSERTCMP(earliest, >, now);
		rxrpc_queue_delayed_work(&rxrpc_connection_reap,
					 (earliest - now) * HZ);
	}

	/* then destroy all those pulled out */
	while (!list_empty(&graveyard)) {
		conn = list_entry(graveyard.next, struct rxrpc_connection,
				  link);
		list_del_init(&conn->link);

		ASSERTCMP(atomic_read(&conn->usage), ==, 0);
		rxrpc_destroy_connection(conn);
	}

	_leave("");
}

/*
 * preemptively destroy all the connection records rather than waiting for them
 * to time out
 */
void __exit rxrpc_destroy_all_connections(void)
{
	_enter("");

	rxrpc_connection_timeout = 0;
	cancel_delayed_work(&rxrpc_connection_reap);
	rxrpc_queue_delayed_work(&rxrpc_connection_reap, 0);

	_leave("");
}
