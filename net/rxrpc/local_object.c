/* Local endpoint object management
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/hashtable.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static void rxrpc_local_processor(struct work_struct *);
static void rxrpc_local_rcu(struct rcu_head *);

static DEFINE_MUTEX(rxrpc_local_mutex);
static LIST_HEAD(rxrpc_local_endpoints);

/*
 * Compare a local to an address.  Return -ve, 0 or +ve to indicate less than,
 * same or greater than.
 *
 * We explicitly don't compare the RxRPC service ID as we want to reject
 * conflicting uses by differing services.  Further, we don't want to share
 * addresses with different options (IPv6), so we don't compare those bits
 * either.
 */
static long rxrpc_local_cmp_key(const struct rxrpc_local *local,
				const struct sockaddr_rxrpc *srx)
{
	long diff;

	diff = ((local->srx.transport_type - srx->transport_type) ?:
		(local->srx.transport_len - srx->transport_len) ?:
		(local->srx.transport.family - srx->transport.family));
	if (diff != 0)
		return diff;

	switch (srx->transport.family) {
	case AF_INET:
		/* If the choice of UDP port is left up to the transport, then
		 * the endpoint record doesn't match.
		 */
		return ((u16 __force)local->srx.transport.sin.sin_port -
			(u16 __force)srx->transport.sin.sin_port) ?:
			memcmp(&local->srx.transport.sin.sin_addr,
			       &srx->transport.sin.sin_addr,
			       sizeof(struct in_addr));
	default:
		BUG();
	}
}

/*
 * Allocate a new local endpoint.
 */
static struct rxrpc_local *rxrpc_alloc_local(const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;

	local = kzalloc(sizeof(struct rxrpc_local), GFP_KERNEL);
	if (local) {
		atomic_set(&local->usage, 1);
		INIT_LIST_HEAD(&local->link);
		INIT_WORK(&local->processor, rxrpc_local_processor);
		INIT_HLIST_HEAD(&local->services);
		init_rwsem(&local->defrag_sem);
		skb_queue_head_init(&local->reject_queue);
		skb_queue_head_init(&local->event_queue);
		local->client_conns = RB_ROOT;
		spin_lock_init(&local->client_conns_lock);
		spin_lock_init(&local->lock);
		rwlock_init(&local->services_lock);
		local->debug_id = atomic_inc_return(&rxrpc_debug_id);
		memcpy(&local->srx, srx, sizeof(*srx));
	}

	_leave(" = %p", local);
	return local;
}

/*
 * create the local socket
 * - must be called with rxrpc_local_mutex locked
 */
static int rxrpc_open_socket(struct rxrpc_local *local)
{
	struct sock *sock;
	int ret, opt;

	_enter("%p{%d}", local, local->srx.transport_type);

	/* create a socket to represent the local endpoint */
	ret = sock_create_kern(&init_net, PF_INET, local->srx.transport_type,
			       IPPROTO_UDP, &local->socket);
	if (ret < 0) {
		_leave(" = %d [socket]", ret);
		return ret;
	}

	/* if a local address was supplied then bind it */
	if (local->srx.transport_len > sizeof(sa_family_t)) {
		_debug("bind");
		ret = kernel_bind(local->socket,
				  (struct sockaddr *)&local->srx.transport,
				  local->srx.transport_len);
		if (ret < 0) {
			_debug("bind failed %d", ret);
			goto error;
		}
	}

	/* we want to receive ICMP errors */
	opt = 1;
	ret = kernel_setsockopt(local->socket, SOL_IP, IP_RECVERR,
				(char *) &opt, sizeof(opt));
	if (ret < 0) {
		_debug("setsockopt failed");
		goto error;
	}

	/* we want to set the don't fragment bit */
	opt = IP_PMTUDISC_DO;
	ret = kernel_setsockopt(local->socket, SOL_IP, IP_MTU_DISCOVER,
				(char *) &opt, sizeof(opt));
	if (ret < 0) {
		_debug("setsockopt failed");
		goto error;
	}

	/* set the socket up */
	sock = local->socket->sk;
	sock->sk_user_data	= local;
	sock->sk_data_ready	= rxrpc_data_ready;
	sock->sk_error_report	= rxrpc_error_report;
	_leave(" = 0");
	return 0;

error:
	kernel_sock_shutdown(local->socket, SHUT_RDWR);
	local->socket->sk->sk_user_data = NULL;
	sock_release(local->socket);
	local->socket = NULL;

	_leave(" = %d", ret);
	return ret;
}

/*
 * Look up or create a new local endpoint using the specified local address.
 */
struct rxrpc_local *rxrpc_lookup_local(const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;
	struct list_head *cursor;
	const char *age;
	long diff;
	int ret;

	if (srx->transport.family == AF_INET) {
		_enter("{%d,%u,%pI4+%hu}",
		       srx->transport_type,
		       srx->transport.family,
		       &srx->transport.sin.sin_addr,
		       ntohs(srx->transport.sin.sin_port));
	} else {
		_enter("{%d,%u}",
		       srx->transport_type,
		       srx->transport.family);
		return ERR_PTR(-EAFNOSUPPORT);
	}

	mutex_lock(&rxrpc_local_mutex);

	for (cursor = rxrpc_local_endpoints.next;
	     cursor != &rxrpc_local_endpoints;
	     cursor = cursor->next) {
		local = list_entry(cursor, struct rxrpc_local, link);

		diff = rxrpc_local_cmp_key(local, srx);
		if (diff < 0)
			continue;
		if (diff > 0)
			break;

		/* Services aren't allowed to share transport sockets, so
		 * reject that here.  It is possible that the object is dying -
		 * but it may also still have the local transport address that
		 * we want bound.
		 */
		if (srx->srx_service) {
			local = NULL;
			goto addr_in_use;
		}

		/* Found a match.  We replace a dying object.  Attempting to
		 * bind the transport socket may still fail if we're attempting
		 * to use a local address that the dying object is still using.
		 */
		if (!rxrpc_get_local_maybe(local)) {
			cursor = cursor->next;
			list_del_init(&local->link);
			break;
		}

		age = "old";
		goto found;
	}

	local = rxrpc_alloc_local(srx);
	if (!local)
		goto nomem;

	ret = rxrpc_open_socket(local);
	if (ret < 0)
		goto sock_error;

	list_add_tail(&local->link, cursor);
	age = "new";

found:
	mutex_unlock(&rxrpc_local_mutex);

	_net("LOCAL %s %d {%d,%u,%pI4+%hu}",
	     age,
	     local->debug_id,
	     local->srx.transport_type,
	     local->srx.transport.family,
	     &local->srx.transport.sin.sin_addr,
	     ntohs(local->srx.transport.sin.sin_port));

	_leave(" = %p", local);
	return local;

nomem:
	ret = -ENOMEM;
sock_error:
	mutex_unlock(&rxrpc_local_mutex);
	kfree(local);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

addr_in_use:
	mutex_unlock(&rxrpc_local_mutex);
	_leave(" = -EADDRINUSE");
	return ERR_PTR(-EADDRINUSE);
}

/*
 * A local endpoint reached its end of life.
 */
void __rxrpc_put_local(struct rxrpc_local *local)
{
	_enter("%d", local->debug_id);
	rxrpc_queue_work(&local->processor);
}

/*
 * Destroy a local endpoint's socket and then hand the record to RCU to dispose
 * of.
 *
 * Closing the socket cannot be done from bottom half context or RCU callback
 * context because it might sleep.
 */
static void rxrpc_local_destroyer(struct rxrpc_local *local)
{
	struct socket *socket = local->socket;

	_enter("%d", local->debug_id);

	/* We can get a race between an incoming call packet queueing the
	 * processor again and the work processor starting the destruction
	 * process which will shut down the UDP socket.
	 */
	if (local->dead) {
		_leave(" [already dead]");
		return;
	}
	local->dead = true;

	mutex_lock(&rxrpc_local_mutex);
	list_del_init(&local->link);
	mutex_unlock(&rxrpc_local_mutex);

	ASSERT(RB_EMPTY_ROOT(&local->client_conns));
	ASSERT(hlist_empty(&local->services));

	if (socket) {
		local->socket = NULL;
		kernel_sock_shutdown(socket, SHUT_RDWR);
		socket->sk->sk_user_data = NULL;
		sock_release(socket);
	}

	/* At this point, there should be no more packets coming in to the
	 * local endpoint.
	 */
	rxrpc_purge_queue(&local->reject_queue);
	rxrpc_purge_queue(&local->event_queue);

	_debug("rcu local %d", local->debug_id);
	call_rcu(&local->rcu, rxrpc_local_rcu);
}

/*
 * Process events on an endpoint
 */
static void rxrpc_local_processor(struct work_struct *work)
{
	struct rxrpc_local *local =
		container_of(work, struct rxrpc_local, processor);
	bool again;

	_enter("%d", local->debug_id);

	do {
		again = false;
		if (atomic_read(&local->usage) == 0)
			return rxrpc_local_destroyer(local);

		if (!skb_queue_empty(&local->reject_queue)) {
			rxrpc_reject_packets(local);
			again = true;
		}

		if (!skb_queue_empty(&local->event_queue)) {
			rxrpc_process_local_events(local);
			again = true;
		}
	} while (again);
}

/*
 * Destroy a local endpoint after the RCU grace period expires.
 */
static void rxrpc_local_rcu(struct rcu_head *rcu)
{
	struct rxrpc_local *local = container_of(rcu, struct rxrpc_local, rcu);

	_enter("%d", local->debug_id);

	ASSERT(!work_pending(&local->processor));

	_net("DESTROY LOCAL %d", local->debug_id);
	kfree(local);
	_leave("");
}

/*
 * Verify the local endpoint list is empty by this point.
 */
void __exit rxrpc_destroy_all_locals(void)
{
	struct rxrpc_local *local;

	_enter("");

	flush_workqueue(rxrpc_workqueue);

	if (!list_empty(&rxrpc_local_endpoints)) {
		mutex_lock(&rxrpc_local_mutex);
		list_for_each_entry(local, &rxrpc_local_endpoints, link) {
			pr_err("AF_RXRPC: Leaked local %p {%d}\n",
			       local, atomic_read(&local->usage));
		}
		mutex_unlock(&rxrpc_local_mutex);
		BUG();
	}

	rcu_barrier();
}
