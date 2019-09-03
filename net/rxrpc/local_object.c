// SPDX-License-Identifier: GPL-2.0-or-later
/* Local endpoint object management
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
#include <net/udp.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static void rxrpc_local_processor(struct work_struct *);
static void rxrpc_local_rcu(struct rcu_head *);

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
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		/* If the choice of UDP6 port is left up to the transport, then
		 * the endpoint record doesn't match.
		 */
		return ((u16 __force)local->srx.transport.sin6.sin6_port -
			(u16 __force)srx->transport.sin6.sin6_port) ?:
			memcmp(&local->srx.transport.sin6.sin6_addr,
			       &srx->transport.sin6.sin6_addr,
			       sizeof(struct in6_addr));
#endif
	default:
		BUG();
	}
}

/*
 * Allocate a new local endpoint.
 */
static struct rxrpc_local *rxrpc_alloc_local(struct rxrpc_net *rxnet,
					     const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;

	local = kzalloc(sizeof(struct rxrpc_local), GFP_KERNEL);
	if (local) {
		atomic_set(&local->usage, 1);
		atomic_set(&local->active_users, 1);
		local->rxnet = rxnet;
		INIT_LIST_HEAD(&local->link);
		INIT_WORK(&local->processor, rxrpc_local_processor);
		init_rwsem(&local->defrag_sem);
		skb_queue_head_init(&local->reject_queue);
		skb_queue_head_init(&local->event_queue);
		local->client_conns = RB_ROOT;
		spin_lock_init(&local->client_conns_lock);
		spin_lock_init(&local->lock);
		rwlock_init(&local->services_lock);
		local->debug_id = atomic_inc_return(&rxrpc_debug_id);
		memcpy(&local->srx, srx, sizeof(*srx));
		local->srx.srx_service = 0;
		trace_rxrpc_local(local->debug_id, rxrpc_local_new, 1, NULL);
	}

	_leave(" = %p", local);
	return local;
}

/*
 * create the local socket
 * - must be called with rxrpc_local_mutex locked
 */
static int rxrpc_open_socket(struct rxrpc_local *local, struct net *net)
{
	struct sock *usk;
	int ret, opt;

	_enter("%p{%d,%d}",
	       local, local->srx.transport_type, local->srx.transport.family);

	/* create a socket to represent the local endpoint */
	ret = sock_create_kern(net, local->srx.transport.family,
			       local->srx.transport_type, 0, &local->socket);
	if (ret < 0) {
		_leave(" = %d [socket]", ret);
		return ret;
	}

	/* set the socket up */
	usk = local->socket->sk;
	inet_sk(usk)->mc_loop = 0;

	/* Enable CHECKSUM_UNNECESSARY to CHECKSUM_COMPLETE conversion */
	inet_inc_convert_csum(usk);

	rcu_assign_sk_user_data(usk, local);

	udp_sk(usk)->encap_type = UDP_ENCAP_RXRPC;
	udp_sk(usk)->encap_rcv = rxrpc_input_packet;
	udp_sk(usk)->encap_destroy = NULL;
	udp_sk(usk)->gro_receive = NULL;
	udp_sk(usk)->gro_complete = NULL;

	udp_encap_enable();
#if IS_ENABLED(CONFIG_AF_RXRPC_IPV6)
	if (local->srx.transport.family == AF_INET6)
		udpv6_encap_enable();
#endif
	usk->sk_error_report = rxrpc_error_report;

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

	switch (local->srx.transport.family) {
	case AF_INET6:
		/* we want to receive ICMPv6 errors */
		opt = 1;
		ret = kernel_setsockopt(local->socket, SOL_IPV6, IPV6_RECVERR,
					(char *) &opt, sizeof(opt));
		if (ret < 0) {
			_debug("setsockopt failed");
			goto error;
		}

		/* we want to set the don't fragment bit */
		opt = IPV6_PMTUDISC_DO;
		ret = kernel_setsockopt(local->socket, SOL_IPV6, IPV6_MTU_DISCOVER,
					(char *) &opt, sizeof(opt));
		if (ret < 0) {
			_debug("setsockopt failed");
			goto error;
		}

		/* Fall through and set IPv4 options too otherwise we don't get
		 * errors from IPv4 packets sent through the IPv6 socket.
		 */
		/* Fall through */
	case AF_INET:
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

		/* We want receive timestamps. */
		opt = 1;
		ret = kernel_setsockopt(local->socket, SOL_SOCKET, SO_TIMESTAMPNS_OLD,
					(char *)&opt, sizeof(opt));
		if (ret < 0) {
			_debug("setsockopt failed");
			goto error;
		}
		break;

	default:
		BUG();
	}

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
struct rxrpc_local *rxrpc_lookup_local(struct net *net,
				       const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;
	struct rxrpc_net *rxnet = rxrpc_net(net);
	struct list_head *cursor;
	const char *age;
	long diff;
	int ret;

	_enter("{%d,%d,%pISp}",
	       srx->transport_type, srx->transport.family, &srx->transport);

	mutex_lock(&rxnet->local_mutex);

	for (cursor = rxnet->local_endpoints.next;
	     cursor != &rxnet->local_endpoints;
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
		if (!rxrpc_use_local(local))
			break;

		age = "old";
		goto found;
	}

	local = rxrpc_alloc_local(rxnet, srx);
	if (!local)
		goto nomem;

	ret = rxrpc_open_socket(local, net);
	if (ret < 0)
		goto sock_error;

	if (cursor != &rxnet->local_endpoints)
		list_replace_init(cursor, &local->link);
	else
		list_add_tail(&local->link, cursor);
	age = "new";

found:
	mutex_unlock(&rxnet->local_mutex);

	_net("LOCAL %s %d {%pISp}",
	     age, local->debug_id, &local->srx.transport);

	_leave(" = %p", local);
	return local;

nomem:
	ret = -ENOMEM;
sock_error:
	mutex_unlock(&rxnet->local_mutex);
	if (local)
		call_rcu(&local->rcu, rxrpc_local_rcu);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

addr_in_use:
	mutex_unlock(&rxnet->local_mutex);
	_leave(" = -EADDRINUSE");
	return ERR_PTR(-EADDRINUSE);
}

/*
 * Get a ref on a local endpoint.
 */
struct rxrpc_local *rxrpc_get_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	int n;

	n = atomic_inc_return(&local->usage);
	trace_rxrpc_local(local->debug_id, rxrpc_local_got, n, here);
	return local;
}

/*
 * Get a ref on a local endpoint unless its usage has already reached 0.
 */
struct rxrpc_local *rxrpc_get_local_maybe(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);

	if (local) {
		int n = atomic_fetch_add_unless(&local->usage, 1, 0);
		if (n > 0)
			trace_rxrpc_local(local->debug_id, rxrpc_local_got,
					  n + 1, here);
		else
			local = NULL;
	}
	return local;
}

/*
 * Queue a local endpoint and pass the caller's reference to the work item.
 */
void rxrpc_queue_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id = local->debug_id;
	int n = atomic_read(&local->usage);

	if (rxrpc_queue_work(&local->processor))
		trace_rxrpc_local(debug_id, rxrpc_local_queued, n, here);
	else
		rxrpc_put_local(local);
}

/*
 * Drop a ref on a local endpoint.
 */
void rxrpc_put_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	int n;

	if (local) {
		n = atomic_dec_return(&local->usage);
		trace_rxrpc_local(local->debug_id, rxrpc_local_put, n, here);

		if (n == 0)
			call_rcu(&local->rcu, rxrpc_local_rcu);
	}
}

/*
 * Start using a local endpoint.
 */
struct rxrpc_local *rxrpc_use_local(struct rxrpc_local *local)
{
	unsigned int au;

	local = rxrpc_get_local_maybe(local);
	if (!local)
		return NULL;

	au = atomic_fetch_add_unless(&local->active_users, 1, 0);
	if (au == 0) {
		rxrpc_put_local(local);
		return NULL;
	}

	return local;
}

/*
 * Cease using a local endpoint.  Once the number of active users reaches 0, we
 * start the closure of the transport in the work processor.
 */
void rxrpc_unuse_local(struct rxrpc_local *local)
{
	unsigned int au;

	if (local) {
		au = atomic_dec_return(&local->active_users);
		if (au == 0)
			rxrpc_queue_local(local);
		else
			rxrpc_put_local(local);
	}
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
	struct rxrpc_net *rxnet = local->rxnet;

	_enter("%d", local->debug_id);

	local->dead = true;

	mutex_lock(&rxnet->local_mutex);
	list_del_init(&local->link);
	mutex_unlock(&rxnet->local_mutex);

	rxrpc_clean_up_local_conns(local);
	rxrpc_service_connection_reaper(&rxnet->service_conn_reaper);
	ASSERT(!local->service);

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
}

/*
 * Process events on an endpoint.  The work item carries a ref which
 * we must release.
 */
static void rxrpc_local_processor(struct work_struct *work)
{
	struct rxrpc_local *local =
		container_of(work, struct rxrpc_local, processor);
	bool again;

	trace_rxrpc_local(local->debug_id, rxrpc_local_processing,
			  atomic_read(&local->usage), NULL);

	do {
		again = false;
		if (atomic_read(&local->active_users) == 0) {
			rxrpc_local_destroyer(local);
			break;
		}

		if (!skb_queue_empty(&local->reject_queue)) {
			rxrpc_reject_packets(local);
			again = true;
		}

		if (!skb_queue_empty(&local->event_queue)) {
			rxrpc_process_local_events(local);
			again = true;
		}
	} while (again);

	rxrpc_put_local(local);
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
void rxrpc_destroy_all_locals(struct rxrpc_net *rxnet)
{
	struct rxrpc_local *local;

	_enter("");

	flush_workqueue(rxrpc_workqueue);

	if (!list_empty(&rxnet->local_endpoints)) {
		mutex_lock(&rxnet->local_mutex);
		list_for_each_entry(local, &rxnet->local_endpoints, link) {
			pr_err("AF_RXRPC: Leaked local %p {%d}\n",
			       local, atomic_read(&local->usage));
		}
		mutex_unlock(&rxnet->local_mutex);
		BUG();
	}
}
